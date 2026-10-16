#include "oled.h"
#include "oled_font.h"
#include "oled_flush.h"
#include "oled_keys.h"
#include "oled_vpad.h"
#include "oled_pad.h"
#include "title_scroll.h"
#include "version_label.h"
#include "bt.h"
#include "slots.h"
#include "slots_footer.h"
#include "audio.h"
#include "config.h"
#include "state_mgr.h"
#include "state_override.h"

#include <cstdio>
#include <cstddef>
#include <cstring>
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "cmd.h"
#include "pico/time.h"

extern uint8_t interrupt_in_data[63]; // defined in main.cpp

// Mic diagnostic counters (defined in main.cpp).
extern uint32_t bt_31_packet_count();
extern uint32_t host_out02_total();
extern uint32_t host_out02_trig_allow();
extern uint32_t host_out02_to_bt();
extern uint32_t host_out02_trig_folded();
extern uint8_t  bt_31_last_byte2();
extern uint8_t  bt_31_b2_or_mask();
extern uint16_t bt_31_len_min();
extern uint16_t bt_31_len_max();
extern void     bt_31_mic_prefix(uint8_t out[6]);
extern bool     spk_active; // main.cpp: true while host USB speaker stream is open

// Global (not in the anon namespace below) so state_mgr.cpp can extern it:
// true while an OLED lightbar mode or the charging pulse owns the LED, which
// tells state_update() to ignore the host's AllowLedColor writes.
bool g_lightbar_override = false;

namespace {

constexpr uint kPinDC = 8;
constexpr uint kPinCS = 9;
constexpr uint kPinCLK = 10;
constexpr uint kPinMOSI = 11;
constexpr uint kPinRST = 12;
constexpr uint kPinKey0 = 15;
constexpr uint kPinKey1 = 17;

constexpr int kW = 128;
constexpr int kH = 64;
constexpr int kRowBytes = kW / 8;
constexpr int kFbBytes = kRowBytes * kH;

uint8_t fb[kFbBytes];
// What the panel shows right now (a copy of fb at every flush), for the app's
// screen mirror (0xEF). Main-loop only, like fb and the USB stack.
uint8_t fb_shown[kFbBytes];
uint8_t fb_shown_seq = 0;

uint32_t last_render_us = 0;
constexpr uint32_t kFrameUs = 100000;
// KEY0 / KEY1: the press logic (debounce, short / long, the KEY0+KEY1 reboot
// chord) is oledkeys::step() in oled_keys.h, shared with the app's remote
// presses (0xF6 0x18, protocol 1.18) and host-tested. KEY0 was once a
// double-click reboot; that moved to the chord because rapid forward
// navigation kept tripping it.
oledkeys::Buttons g_keys;
oledkeys::Injector g_key_inject;

// The controller buttons the screens read: the controller's own (while one
// is linked) ORed with the app's virtual presses (0xF6 0x19, protocol 1.19,
// oled_vpad.h). Refreshed by vpad_tick() every pass; only the OLED reads
// it, so games never see a virtual press. b7 = D-pad + face, b8 bit 1 = R1.
oledvpad::Injector g_vpad;
oledvpad::View g_in{0x08, 0};

// Brightness levels (SH1107 contrast register 0x81). User cycles via KEY1 long-press.
constexpr uint8_t kBrightLevels[] = {0xFF, 0x7F, 0x3F, 0x10};
constexpr int kNumBrightLevels = sizeof(kBrightLevels) / sizeof(kBrightLevels[0]);
int bright_idx = 0;
uint8_t current_contrast = 0xFF;

// Auto-dim / auto-off after idle. Tracks last button/input activity.
// Tier 1: Active → full brightness (bright_idx).
// Tier 2: idle > dim threshold → contrast drops to kDimContrast (deep dim).
// Tier 3: idle > off threshold → SH1107 panel turned fully off (cmd 0xAE)
//         to prevent OLED burn-in on long unattended sits.
// The two thresholds are user-configurable (Config_body.screen_dim_timeout /
// screen_off_timeout, minutes; 0 = tier disabled) — issue #5. last_activity_us
// is 64-bit µs so the full 0..250 min range is representable without the ~71 min
// wrap of time_us_32().
// kDimContrast tuned by eye: 0x10 looked like only ~10% reduction on this
// panel (contrast-vs-brightness is heavily non-linear near the bottom of
// the register range). 0x02 is visibly dim while still legible up close.
uint64_t last_activity_us = 0;
uint32_t last_input_hash = 0;
constexpr uint8_t kDimContrast = 0x01;
enum OledPowerState { OLED_ACTIVE, OLED_DIM, OLED_OFF };
OledPowerState oled_power_state = OLED_ACTIVE;
bool prev_bt_connected = false;

// Screen ordering — single source of truth. Reorder by editing this block;
// oled_loop's switch and handle_buttons' KEY1 contextual checks use these
// names, so the indices can move without touching that code.
constexpr int kScreenStatus    = 0;
constexpr int kScreenSlots     = 1;
constexpr int kScreenLightbar  = 2;
constexpr int kScreenTriggers  = 3;
constexpr int kScreenGyro      = 4;
constexpr int kScreenTouchpad  = 5;
constexpr int kScreenDiag      = 6;
constexpr int kScreenCpu       = 7;
constexpr int kScreenRssi      = 8;
constexpr int kScreenVU        = 9;
constexpr int kScreenSettings  = 10;
constexpr int kNumScreens      = 11;
int current_screen = 0;

// Lightbar mode cycle: 0=LIVE, 1-4=FAV0-3, 5=BREATHING, 6=RAINBOW, 7=FADE,
// 8=HOST (passthrough — let the host/game own the LED). HOST is the default so
// the dongle doesn't hijack game player-indicator LEDs out of the box. Keep
// this numbering in sync with Config_body::lightbar_mode (src/config.h).
constexpr int kLbModeHost = 8;
constexpr int kNumLbModes = 9;

// Each setting defines its display, storage and adjustment in one place.
enum SetKind : uint8_t { SK_NUM, SK_BOOL, SK_POWER, SK_ACTION };
enum SetFormat : uint8_t { SF_PLAIN, SF_OFF0, SF_GAME0, SF_PC0, SF_REDUCE };
enum : uint8_t { SIF_WRAP = 1, SIF_FLOAT = 2, SIF_FLOAT10 = 4, SIF_GRID = 8 };
enum : uint8_t { ACT_NONE, ACT_RESET, ACT_WIPE };
struct SetItem {
    const char *label, *unit, *hint;
    const char* const* names;
    SetKind kind;
    SetFormat fmt;
    uint8_t flags, act, off; // Config_body offset, or the mask for SK_POWER
    int16_t lo, hi, step;
};
static const char* const kOnOff[] = {"off", "on"};
static const char* const kDisabled[] = {"on", "off"};
static const char* const kPoll[] = {"250Hz", "500Hz", "RT"};
static const char* const kCtrl[] = {"DS5", "DSE", "Auto"};
static const char* const kAutoHap[] = {"Off", "Fallback", "Mix", "Replace"};
static const char* const kAHLp[] = {"80Hz", "160Hz", "250Hz", "400Hz"};
static const char* const kSpkOut[] = {"Auto", "SPK", "Hset", "off"};
static const char* const kMicIn[] = {"Auto", "Int", "Hset"};
#define CFG_OFF(field) static_cast<uint8_t>(offsetof(Config_body, field))
static const SetItem kSetItems[] = {
    {"Hap Left", nullptr, nullptr, nullptr, SK_NUM, SF_PLAIN, SIF_FLOAT10, ACT_NONE, CFG_OFF(haptics_gain), 10, 60, 5},
    {"Hap Right", nullptr, nullptr, nullptr, SK_NUM, SF_PLAIN, SIF_FLOAT10, ACT_NONE, CFG_OFF(haptics_gain_right), 10, 60, 5},
    {"Spk Vol", "dB", nullptr, nullptr, SK_NUM, SF_PLAIN, SIF_FLOAT | SIF_GRID, ACT_NONE, CFG_OFF(speaker_volume), -100, 12, 5},
    {"Inact", "min", nullptr, nullptr, SK_NUM, SF_PLAIN, 0, ACT_NONE, CFG_OFF(inactive_time), 10, 60, 5},
    {"InactDC", nullptr, nullptr, kDisabled, SK_BOOL, SF_PLAIN, 0, ACT_NONE, CFG_OFF(disable_inactive_disconnect), 0, 1, 1},
    {"Pico LED", nullptr, nullptr, kDisabled, SK_BOOL, SF_PLAIN, 0, ACT_NONE, CFG_OFF(disable_pico_led), 0, 1, 1},
    {"Poll", nullptr, nullptr, kPoll, SK_NUM, SF_PLAIN, SIF_WRAP, ACT_NONE, CFG_OFF(polling_rate_mode), 0, 2, 1},
    {"AudBuf", nullptr, nullptr, nullptr, SK_NUM, SF_PLAIN, 0, ACT_NONE, CFG_OFF(audio_buffer_length), 16, 128, 4},
    {"Ctrl", nullptr, nullptr, kCtrl, SK_NUM, SF_PLAIN, SIF_WRAP, ACT_NONE, CFG_OFF(controller_mode), 0, 2, 1},
    {"AutoHap", nullptr, nullptr, kAutoHap, SK_NUM, SF_PLAIN, SIF_WRAP, ACT_NONE, CFG_OFF(auto_haptics_enable), 0, 3, 1},
    {"AH Gain", "%", nullptr, nullptr, SK_NUM, SF_PLAIN, 0, ACT_NONE, CFG_OFF(auto_haptics_gain), 0, 200, 10},
    {"AH LP", nullptr, nullptr, kAHLp, SK_NUM, SF_PLAIN, SIF_WRAP, ACT_NONE, CFG_OFF(auto_haptics_lowpass), 0, 3, 1},
    {"ScrDim", "min", nullptr, nullptr, SK_NUM, SF_OFF0, 0, ACT_NONE, CFG_OFF(screen_dim_timeout), 0, 250, 1},
    {"ScrOff", "min", nullptr, nullptr, SK_NUM, SF_OFF0, 0, ACT_NONE, CFG_OFF(screen_off_timeout), 0, 250, 1},
    {"BT Mic", nullptr, nullptr, nullptr, SK_BOOL, SF_PLAIN, 0, ACT_NONE, CFG_OFF(bt_mic_enable), 0, 1, 1},
    {"CtrlWake", nullptr, nullptr, nullptr, SK_BOOL, SF_PLAIN, 0, ACT_NONE, CFG_OFF(controller_wakes_display), 0, 1, 1},
    {"TrigEff", nullptr, nullptr, nullptr, SK_NUM, SF_REDUCE, 0, ACT_NONE, CFG_OFF(trigger_reduce), 0, 10, 1},
    {"SpkBoost", nullptr, nullptr, nullptr, SK_NUM, SF_GAME0, 0, ACT_NONE, CFG_OFF(speaker_gain), 0, 7, 1},
    {"VolLock", nullptr, nullptr, nullptr, SK_BOOL, SF_PLAIN, 0, ACT_NONE, CFG_OFF(lock_volume), 0, 1, 1},
    {"SPK Out", nullptr, nullptr, kSpkOut, SK_NUM, SF_PLAIN, SIF_WRAP, ACT_NONE, CFG_OFF(speaker_select), 0, 3, 1},
    {"Mic In", nullptr, nullptr, kMicIn, SK_NUM, SF_PLAIN, SIF_WRAP, ACT_NONE, CFG_OFF(mic_select), 0, 2, 1},
    {"Mic Lvl", nullptr, nullptr, nullptr, SK_NUM, SF_PC0, 0, ACT_NONE, CFG_OFF(mic_level), 0, kMicLevelMax, 4},
    {"Echo Cancel", nullptr, nullptr, nullptr, SK_BOOL, SF_PLAIN, 0, ACT_NONE, CFG_OFF(mic_echo_cancel), 0, 1, 1},
    {"Sleep CtrlOff", nullptr, nullptr, nullptr, SK_POWER, SF_PLAIN, 0, ACT_NONE, POWER_SLEEP_CONTROLLER_OFF, 0, 1, 1},
    {"PS Wakes PC", nullptr, nullptr, nullptr, SK_POWER, SF_PLAIN, 0, ACT_NONE, POWER_PS_WAKES_PC, 0, 1, 1},
    {"Reset to defaults", nullptr, "Hold Tri 2s = RESET", nullptr, SK_ACTION, SF_PLAIN, 0, ACT_RESET, 0, 0, 0, 1},
    {"Wipe all slots", nullptr, "Hold Tri 2s = WIPE", nullptr, SK_ACTION, SF_PLAIN, 0, ACT_WIPE, 0, 0, 0, 1},
};
#undef CFG_OFF
constexpr int kNumSettingsItems = sizeof(kSetItems) / sizeof(kSetItems[0]);
Config_body settings_local{};
uint8_t settings_power = POWER_FLAGS_DEFAULT;  // PC sleep options (Config::power_flags, not in the body)
int settings_sel = 0;
bool settings_dirty = false;
bool settings_init_done = false;
uint8_t settings_last_dpad = 8;  // 8 = released
uint8_t settings_last_face = 0;
// Save / reset / wipe feedback: 2 s in the footer, not over the title.
const char* settings_save_status = "";
uint32_t settings_status_until_us = 0;
void settings_show_status(const char *text) {
    settings_save_status = text;
    settings_status_until_us = (uint32_t)time_us_32() + 2000000;
}

// Factory-reset hold-Triangle-2s state. Borrowed from zurce/DS5Dongle-OLED's
// "hold to wipe" UX pattern (https://github.com/zurce/DS5Dongle-OLED).
uint32_t settings_tri_press_us = 0;
bool settings_reset_triggered = false;
constexpr uint32_t kResetHoldUs = 2000000;

uint8_t lb_r = 0, lb_g = 0, lb_b = 0;

// Lightbar mode + favorite slots: 0 = LIVE tilt preview; 1..4 = saved slots F0..F3.
// These are seeded from flash (lightbar_load_config) at boot; the defaults here
// only apply before that runs. lb_dirty tracks an unsaved mode/favorite change
// so we persist once on leaving the Lightbar screen instead of per button press.
int lb_mode = kLbModeHost;
uint8_t lb_fav_r[4] = {255, 0,   0,   255}; // Red, Green, Blue, White defaults
uint8_t lb_fav_g[4] = {0,   255, 0,   255};
uint8_t lb_fav_b[4] = {0,   0,   255, 255};
uint8_t lb_last_face = 0;
bool lb_dirty = false;

uint32_t rumble_off_at_us = 0;
bool rumble_active = false;
constexpr uint32_t kRumbleBurstUs = 250000;

int trigger_preset = 0;
const char* const kTrigPresetNames[] = {"Off", "Feedback", "Weapon", "Vibration", "Bow", "Gallop", "Machine"};

// Rising-edge trackers for the screens whose K1=cycle action moved to a
// controller button. Trigger Test uses △ (byte 7 bit 7); Lightbar uses R1
// (byte 8 bit 1) because △ is already taken on Lightbar for "save current
// RGB to favorite slot 0".
uint8_t triggers_last_face = 0;
uint8_t lb_last_buttons = 0;
constexpr int kNumTrigPresets = 7;

void cmd(uint8_t c) {
    gpio_put(kPinDC, 0);
    gpio_put(kPinCS, 0);
    spi_write_blocking(spi1, &c, 1);
    gpio_put(kPinCS, 1);
}

// One chip-select frame per call (D/C held), for oled_flush::stream_frame.
// spi_write_blocking returns only once the last bit is out, so D/C and CS
// never change under a byte in flight.
struct SpiSink {
    static void send(bool is_data, const uint8_t *p, int n) {
        gpio_put(kPinDC, is_data ? 1 : 0);
        gpio_put(kPinCS, 0);
        spi_write_blocking(spi1, p, static_cast<size_t>(n));
        gpio_put(kPinCS, 1);
    }
    void cmd(const uint8_t *p, int n) { send(false, p, n); }
    void data(const uint8_t *p, int n) { send(true, p, n); }
};
static_assert(oled_flush::kW == kW && oled_flush::kH == kH && oled_flush::kFbBytes == kFbBytes,
              "oled_flush.h and oled.cpp disagree on the framebuffer");

// False until the panel holds a frame we sent (its RAM is undefined after
// reset), so the first flush always goes out in full.
bool panel_in_sync = false;

void hw_reset() {
    gpio_put(kPinRST, 1); sleep_ms(100);
    gpio_put(kPinRST, 0); sleep_ms(100);
    gpio_put(kPinRST, 1); sleep_ms(100);
}

void sh1107_set_contrast(uint8_t value) {
    if (value == current_contrast) return;
    current_contrast = value;
    cmd(0x81); cmd(value);
}

void sh1107_init() {
    cmd(0xAE);
    cmd(0x00); cmd(0x10);
    cmd(0xB0);
    cmd(0xDC); cmd(0x00);
    cmd(0x81); cmd(0x6F);
    current_contrast = 0x6F;  // so the first frame applies the saved brightness level
    cmd(0x21);
    cmd(0xA0);
    cmd(0xC0);
    cmd(0xA4);
    cmd(0xA6);
    cmd(0xA8); cmd(0x3F);
    cmd(0xD3); cmd(0x60);
    cmd(0xD5); cmd(0x41);
    cmd(0xD9); cmd(0x22);
    cmd(0xDB); cmd(0x35);
    cmd(0xAD); cmd(0x8A);
    sleep_ms(50);
    cmd(0xAF);
}

// Forward-declared so flush_fb can paint the per-button arrows on top of
// the rendered framebuffer just before SPI sends it to the OLED. Body
// lives near the other text-drawing helpers below.
void draw_button_chrome();

// 129 SPI transactions per frame (was one per byte, ~1,150), and none at all
// when the frame equals what the panel already shows (static screens, the dim
// pulse between moves). fb_shown_seq still counts every frame for the mirror.
// An unchanged frame still goes out every kResendFrames, so a panel whose RAM
// got disturbed (ESD, a loose ribbon) heals within ~2 s like before.
constexpr uint8_t kResendFrames = 20;
uint8_t frames_skipped = 0;
void flush_fb_raw() {
    fb_shown_seq++;
    if (panel_in_sync && frames_skipped < kResendFrames &&
        memcmp(fb_shown, fb, sizeof(fb_shown)) == 0) {
        frames_skipped++;
        return;
    }
    frames_skipped = 0;
    memcpy(fb_shown, fb, sizeof(fb_shown));
    SpiSink sink;
    oled_flush::stream_frame(fb, sink);
    panel_in_sync = true;
}

void flush_fb() {
    draw_button_chrome();
    flush_fb_raw();
}

uint32_t fb_generation = 0;
void fb_clear() {
    memset(fb, 0, sizeof(fb));
    ++fb_generation; // invalidate partial redraws after another screen clears the buffer
}

// Clear a row band without changing neighbouring pixels or scroll markers.
void rect_clear(int x, int y, int w, int h) {
    const int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    const int x1 = x + w > kW ? kW : x + w;
    const int y1 = y + h > kH ? kH : y + h;
    if (x0 >= x1 || y0 >= y1) return;
    const int first = x0 / 8, last = (x1 - 1) / 8;
    // loop: HOT-PATH-OK — at most 64 rows, only when a diagnostic band changes
    for (int row = y0; row < y1; ++row) {
        // loop: HOT-PATH-OK — at most 16 framebuffer bytes per row
        for (int col = first; col <= last; ++col) {
            uint8_t mask = 0xff;
            if (col == first) mask &= 0xffu >> (x0 & 7);
            if (col == last) mask &= 0xffu << (7 - ((x1 - 1) & 7));
            fb[row * kRowBytes + col] &= static_cast<uint8_t>(~mask);
        }
    }
}

void px(int x, int y, bool on) {
    if (x < 0 || x >= kW || y < 0 || y >= kH) return;
    uint8_t *p = &fb[y * kRowBytes + (x / 8)];
    uint8_t m = 1 << (7 - (x % 8));
    if (on) *p |= m; else *p &= ~m;
}

void rect_outline(int x, int y, int w, int h) {
    // loop: HOT-PATH-OK — w ≤ 128 px, OLED render at 10 Hz (kFrameUs 100 ms)
    for (int i = 0; i < w; i++) { px(x + i, y, true); px(x + i, y + h - 1, true); }
    // loop: HOT-PATH-OK — h ≤ 64 px, OLED render at 10 Hz
    for (int i = 0; i < h; i++) { px(x, y + i, true); px(x + w - 1, y + i, true); }
}

void rect_filled(int x, int y, int w, int h) {
    // loop: HOT-PATH-OK — h ≤ 64 rows, OLED render at 10 Hz
    for (int j = 0; j < h; j++)
        // loop: HOT-PATH-OK — w x h ≤ 8192 px but used for small boxes, OLED render at 10 Hz
        for (int i = 0; i < w; i++)
            px(x + i, y + j, true);
}

// XOR-invert every pixel in a region (used to flash a control "pressed").
void rect_invert(int x, int y, int w, int h) {
    // loop: HOT-PATH-OK — h rows of a pressed-control region, OLED render at 10 Hz
    for (int j = 0; j < h; j++)
        // loop: HOT-PATH-OK — w px of a pressed-control region, OLED render at 10 Hz
        for (int i = 0; i < w; i++) {
            const int xx = x + i, yy = y + j;
            if (xx < 0 || xx >= kW || yy < 0 || yy >= kH) continue;
            fb[yy * kRowBytes + (xx / 8)] ^= 1 << (7 - (xx % 8));
        }
}

void draw_char(int x, int y, char c) {
    if (c < 0x20 || c > 0x7E) return;
    const uint8_t *g = kFont5x7[c - 0x20];
    // loop: HOT-PATH-OK — 5 font columns per char, OLED render at 10 Hz
    for (int col = 0; col < kFontW; col++) {
        uint8_t bits = g[col];
        // loop: HOT-PATH-OK — 7 font rows per column, OLED render at 10 Hz
        for (int row = 0; row < kFontH; row++) {
            if (bits & (1 << row)) px(x + col, y + row, true);
        }
    }
}

void draw_text(int x, int y, const char *s) {
    // loop: HOT-PATH-OK — ≤ ~21 chars per line, OLED render at 10 Hz
    while (*s) {
        draw_char(x, y, *s++);
        x += 6;
    }
}

// draw_text, but only pixels with x in [x_min, x_max) are drawn.
void draw_text_clipped(int x, int y, const char *s, int x_min, int x_max) {
    // loop: HOT-PATH-OK — ≤ title length chars, scrolling title at 10 Hz
    for (; *s; s++, x += 6) {
        if (x + kFontW <= x_min) continue;
        if (x >= x_max) break;
        const char c = *s;
        if (c < 0x20 || c > 0x7E) continue;
        const uint8_t *g = kFont5x7[c - 0x20];
        // loop: HOT-PATH-OK — 5 font columns, scrolling title at 10 Hz
        for (int col = 0; col < kFontW; col++) {
            const int xx = x + col;
            if (xx < x_min || xx >= x_max) continue;
            // loop: HOT-PATH-OK — 7 font rows, scrolling title at 10 Hz
            for (int row = 0; row < kFontH; row++) {
                if (g[col] & (1 << row)) px(xx, y + row, true);
            }
        }
    }
}

// The version without "-oled-edition" (version_label.h), for every place the
// OLED shows it. The Status title is "DS5 B. <version>": a release or dev
// build ("DS5 B. v0.9.5-dev.4", 19 characters) fits the 114 px between the
// chrome strip and the link icon exactly.
constexpr auto kVersionLabel = version_label::make("", FIRMWARE_VERSION);
constexpr auto kStatusTitle = version_label::make("DS5 B. ", FIRMWARE_VERSION);

// A title longer than its space (local builds, "+local.c3915e0") scrolls
// ping-pong: one pixel per frame (100 ms, ~1.7 characters a second, the
// readable range for short tickers), 1.5 s pauses at each end. A title that
// fits is drawn once and costs nothing (unchanged frames aren't flushed).
void draw_title_scrolling(int x_min, int x_max, int y, const char *s, int n) {
    static TitleScroll scroll;
    const int overflow = (n * 6 - 1) - (x_max - x_min);
    if (overflow <= 0) {
        draw_text(x_min, y, s);
        return;
    }
    draw_text_clipped(x_min - scroll.step(time_us_64(), overflow), y, s, x_min, x_max);
}

// Button-chrome strip on the left edge of every screen. KEY0 (top button)
// shows '>' at y=8; KEY1 (bottom button) shows '<' at y=49. Painted by
// flush_fb() on top of the rendered framebuffer so it never gets clobbered.
// Per-screen renderers reserve x ∈ [0..5] (5-wide glyph + 1 padding) and
// start main content at kContentX.
constexpr int kContentX = 6;
void draw_button_chrome() {
    draw_char(0, 8,  '>');
    draw_char(0, 49, '<');
}

// Pixel-art icon support. Visual approach inspired by zurce/DS5Dongle-OLED
// (https://github.com/zurce/DS5Dongle-OLED) — credit to zurce for the idea
// of decorating the OLED with small bitmaps instead of bare text/shapes.
// Bitmap layout: row-major, MSB = leftmost pixel, ceil(w/8) bytes per row.
void draw_icon(int x, int y, const uint8_t *bitmap, int w, int h) {
    const int row_bytes = (w + 7) / 8;
    // loop: HOT-PATH-OK — icon rows (≤ 16), OLED render at 10 Hz
    for (int row = 0; row < h; row++) {
        // loop: HOT-PATH-OK — icon columns (≤ 16), OLED render at 10 Hz
        for (int col = 0; col < w; col++) {
            const uint8_t byte = bitmap[row * row_bytes + (col / 8)];
            const uint8_t mask = (uint8_t)(1u << (7 - (col % 8)));
            if (byte & mask) px(x + col, y + row, true);
        }
    }
}

// 8x8 "link active" filled circle (drawn when DS5 is paired)
static const uint8_t kIconLinkOn[8] = {
    0b00111100,
    0b01111110,
    0b11111111,
    0b11111111,
    0b11111111,
    0b11111111,
    0b01111110,
    0b00111100,
};
// 8x8 "link inactive" hollow circle (drawn when waiting for DS5)
static const uint8_t kIconLinkOff[8] = {
    0b00111100,
    0b01000010,
    0b10000001,
    0b10000001,
    0b10000001,
    0b10000001,
    0b01000010,
    0b00111100,
};

void send_rumble(uint8_t amplitude) {
    uint8_t pkt[78] = {};
    pkt[0] = 0x31;
    pkt[1] = 0x00;
    pkt[2] = 0x10;
    pkt[3] = 0x03;
    pkt[5] = amplitude;
    pkt[6] = amplitude;
    bt_write(pkt, sizeof(pkt));
}

void rumble_burst_tick(uint32_t now) {
    if (rumble_active && (int32_t)(now - rumble_off_at_us) >= 0) {
        send_rumble(0);
        rumble_active = false;
    }
}

// Trigger effect param format follows dualsensectl's reverse-engineering.
// Modes 0x21/0x25/0x26 use bitpacked 10-zone arrays, not raw position bytes.
void send_trigger_effect(int preset) {
    uint8_t pkt[78] = {};
    pkt[0] = 0x31;
    pkt[2] = 0x10;
    pkt[3] = 0x0C; // valid_flag0: RIGHT_TRIGGER_MOTOR_ENABLE | LEFT_TRIGGER_MOTOR_ENABLE

    uint8_t mode = 0x05; // OFF
    uint8_t p[9] = {0};

    switch (preset) {
        case 0: // Off
            mode = 0x05;
            break;
        case 1: { // Feedback — all 10 zones at max strength 8
            mode = 0x21;
            const uint16_t active = 0x03FF;
            uint32_t strength = 0;
            // loop: NOT-WORTH — 10 trigger zones, on a KEY press / leaving the screen
            for (int i = 0; i < 10; i++) strength |= (uint32_t)(7u << (3 * i));
            p[0] = active & 0xFF;
            p[1] = (active >> 8) & 0xFF;
            p[2] = strength & 0xFF;
            p[3] = (strength >> 8) & 0xFF;
            p[4] = (strength >> 16) & 0xFF;
            p[5] = (strength >> 24) & 0xFF;
            break;
        }
        case 2: { // Weapon — snap between positions 3 and 5, force 8
            mode = 0x25;
            const uint16_t start_stop = (1u << 3) | (1u << 5);
            p[0] = start_stop & 0xFF;
            p[1] = (start_stop >> 8) & 0xFF;
            p[2] = 7; // force = strength - 1
            break;
        }
        case 3: { // Vibration — all 10 zones at amplitude 8, frequency 30 Hz
            mode = 0x26;
            const uint16_t active = 0x03FF;
            uint32_t strength = 0;
            // loop: NOT-WORTH — 10 trigger zones, on a KEY press
            for (int i = 0; i < 10; i++) strength |= (uint32_t)(7u << (3 * i));
            p[0] = active & 0xFF;
            p[1] = (active >> 8) & 0xFF;
            p[2] = strength & 0xFF;
            p[3] = (strength >> 8) & 0xFF;
            p[4] = (strength >> 16) & 0xFF;
            p[5] = (strength >> 24) & 0xFF;
            p[8] = 30;
            break;
        }
        case 4: { // Bow — drawing resistance + snap at position 6
            mode = 0x22;
            const uint16_t start_stop = (1u << 2) | (1u << 6);
            const uint8_t force_pair = 7u | (7u << 3); // strength=8, snap=8
            p[0] = start_stop & 0xFF;
            p[1] = (start_stop >> 8) & 0xFF;
            p[2] = force_pair;
            break;
        }
        case 5: { // Galloping
            mode = 0x23;
            const uint16_t start_stop = (1u << 0) | (1u << 9);
            const uint8_t ratio = (5u & 0x07) | ((1u & 0x07) << 3);
            p[0] = start_stop & 0xFF;
            p[1] = (start_stop >> 8) & 0xFF;
            p[2] = ratio;
            p[3] = 5; // frequency
            break;
        }
        case 6: { // Machine gun
            mode = 0x27;
            const uint16_t start_stop = (1u << 1) | (1u << 8);
            const uint8_t force_pair = 7u | (7u << 3);
            p[0] = start_stop & 0xFF;
            p[1] = (start_stop >> 8) & 0xFF;
            p[2] = force_pair;
            p[3] = 20; // frequency
            p[4] = 0;  // period
            break;
        }
    }

    pkt[13] = mode;
    // loop: NOT-WORTH — 9 effect bytes (left), on a KEY press
    for (int i = 0; i < 9; i++) pkt[14 + i] = p[i];
    pkt[24] = mode;
    // loop: NOT-WORTH — 9 effect bytes (right), on a KEY press
    for (int i = 0; i < 9; i++) pkt[25 + i] = p[i];

    state_stamp_host_rumble(pkt + 3);
    bt_write(pkt, sizeof(pkt));
}

void send_lightbar_color(uint8_t r, uint8_t g, uint8_t b);

void handle_buttons() {
    const uint32_t now = time_us_32();
    // A key is down when its pin is low (pull-up; no OLED wired = released)
    // or the app's remote press holds it (oled_key_inject).
    bool v0 = false, v1 = false;
    oledkeys::levels(g_key_inject, now, v0, v1);
    const uint8_t ev = oledkeys::step(g_keys, !gpio_get(kPinKey0) || v0, !gpio_get(kPinKey1) || v1, now);
    // error: NOT-WORTH — nothing; no key event this pass (polling, not a failure)
    if (ev == 0) return;
    if (ev & oledkeys::kActivity) last_activity_us = time_us_64();
    if (ev & oledkeys::kReboot) watchdog_reboot(0, 0, 0);
    // KEY0 = next screen, KEY1 short = back. Trigger-preset / lightbar-mode
    // cycling lives on the DualSense triangle (triggers_handle_input(),
    // lightbar_handle_input()).
    if (ev & oledkeys::kNext) {
        current_screen = (current_screen + 1) % kNumScreens;
        last_render_us = 0;
    }
    if (ev & oledkeys::kBack) {
        current_screen = (current_screen - 1 + kNumScreens) % kNumScreens;
        last_render_us = 0;
    }
    if (ev & oledkeys::kBrightness) {  // KEY1 long press
        bright_idx = (bright_idx + 1) % kNumBrightLevels;
        // Persist so the choice survives a power cycle (issue #9). Keep
        // settings_local in sync too, so a later Settings-screen save can't
        // clobber screen_brightness with its stale snapshot.
        Config_body b = get_config();
        b.screen_brightness = (uint8_t)bright_idx;
        set_config(b);
        // error: SILENT-BUG — nothing; the long-press screen brightness applies now but a failed save loses it at power-off Change: show "Save FAIL" on whichever screen is up (batch: an OLED toast on every screen)
        config_save();
        settings_local.screen_brightness = (uint8_t)bright_idx;
    }
}

// --- Charge ETA tracker --------------------------------------------------
// The DS5 only reports battery in 10% steps (interrupt_in_data[52] low
// nibble, 0..10; high nibble is power-state, 1 == charging). We can't read a
// finer percentage over BT, so a smooth countdown is impossible. Instead we
// time how long each 10% step takes while charging and extrapolate the
// remaining steps. Sampled once per frame from oled_loop (continuously, so
// the estimate stays current even while the panel is dimmed/off and even when
// the user is on another screen); render_screen reads g_charge_eta.
//
// Taper correction: Li-ion CC/CV charging slows sharply near the top, so a
// flat "time per step × steps left" runs optimistic in the last ~20%. Each
// measured step is normalised to a bulk-equivalent duration (divide out the
// step's taper weight); the remaining steps are then re-weighted. This makes
// the estimate consistent whether the user plugged in near-empty or near-full.
struct ChargeEta {
    bool charging;    // pstate == 1 (so the token shows only while charging)
    bool valid;       // minutes is meaningful (provisional or measured)
    bool provisional; // true until a full step is timed — using the default rate
    int  minutes;     // estimated minutes to 100%
};
ChargeEta g_charge_eta{};

// Default bulk-step duration used for a provisional estimate before any real
// step has been timed, so the token shows "~Nm?" immediately on plug-in instead
// of sitting on "~--m" for ~15-20 min. Tuned to an observed ~15 min per 10% on
// this dongle's charge current; it self-corrects to the measured rate (and drops
// the "?") as soon as the first clean step completes.
constexpr float kDefaultStepUs = 15.0f * 60.0f * 1000000.0f;

// Ceiling on a single timed step's bulk-equivalent duration. A genuine idle 10%
// step on this dongle is ~15 min; anything past ~30 min is almost always an
// anomalous/under-load sample (e.g. the controller in use while charging, or a
// battery-nibble bounce) that would otherwise balloon the projection — observed
// reading ~222m at 70% off one ~47-min step. We clamp such samples instead of
// trusting them, and pair that with a median over kRing steps so one bad reading
// can't dominate the estimate.
constexpr float kMaxStepUs = 30.0f * 60.0f * 1000000.0f;

// Relative time the step *ending* at `to_level` (10% units, 1..10) takes vs a
// bulk step. Tuned to the Li-ion CV taper: ~80% onward stretches out.
static float charge_step_weight(int to_level) {
    if (to_level >= 10) return 2.2f;  // 90→100% (constant-voltage tail)
    if (to_level == 9)  return 1.5f;  // 80→90%  (taper begins)
    return 1.0f;                      // bulk constant-current region
}

void sample_charge_eta() {
    constexpr int kRing = 5;                 // median over the last few steps
    static float    ring[kRing] = {0};       // bulk-equivalent step durations (us)
    static int      ring_count = 0;
    static int      ring_head = 0;
    static int      cur_step = -1;           // last observed 10% step
    static uint64_t step_start_us = 0;
    static bool     was_charging = false;
    static bool     first_step_pending = false;  // discard the partial step at plug-in

    const uint8_t pwr   = interrupt_in_data[52];
    int           step  = pwr & 0x0F;
    if (step > 10) step = 10;
    const uint8_t pstate = pwr >> 4;
    const bool charging = bt_is_connected() && (pstate == 1);

    if (!charging) {
        g_charge_eta = ChargeEta{};          // clears charging/valid/minutes
        ring_count = ring_head = 0;
        cur_step = -1;
        was_charging = false;
        return;
    }

    const uint64_t now = time_us_64();
    if (!was_charging) {
        // Just plugged in: start timing from here. The step in progress is
        // partial, so its duration gets discarded when it completes.
        cur_step = step;
        step_start_us = now;
        ring_count = ring_head = 0;
        first_step_pending = true;
        was_charging = true;
    } else if (step == cur_step + 1) {
        // One clean step completed. Skip the first (partial) one; otherwise
        // record its bulk-equivalent duration.
        const float dur = (float)(now - step_start_us);
        if (first_step_pending) {
            first_step_pending = false;
        } else {
            float be = dur / charge_step_weight(step);
            if (be > kMaxStepUs) be = kMaxStepUs;   // clamp under-load/anomalous outliers
            ring[ring_head] = be;
            ring_head = (ring_head + 1) % kRing;
            if (ring_count < kRing) ring_count++;
        }
        cur_step = step;
        step_start_us = now;
    } else if (step != cur_step) {
        // Multi-step jump (e.g. woke from sleep across several steps) or a
        // small dip under heavy use — can't attribute timing cleanly, so just
        // resync without polluting the ring.
        cur_step = step;
        step_start_us = now;
        first_step_pending = false;
    }

    g_charge_eta.charging = true;
    if (cur_step < 10) {
        // Use the measured rate once we have a timed step; until then fall back
        // to the default rate and flag the estimate provisional (renders "?").
        const bool measured = (ring_count > 0);
        float bulk;
        if (measured) {
            // Median of the timed steps — robust to a single slow/fast outlier
            // in a way the old mean wasn't (one 47-min under-load step used to
            // drag the whole projection up). kRing is tiny, so insertion-sort.
            float tmp[kRing];
            // loop: HOT-PATH-OK — N ≤ kRing = 5 copies, at 10 Hz while charging
            for (int i = 0; i < ring_count; i++) tmp[i] = ring[i];
            // loop: HOT-PATH-OK — insertion sort of ≤ 5 items, at 10 Hz while charging
            for (int i = 1; i < ring_count; i++) {
                const float v = tmp[i];
                int j = i - 1;
                // loop: HOT-PATH-OK — inner shift of the ≤ 5-item insertion sort
                while (j >= 0 && tmp[j] > v) { tmp[j + 1] = tmp[j]; j--; }
                tmp[j + 1] = v;
            }
            bulk = tmp[ring_count / 2];
        } else {
            bulk = kDefaultStepUs;
        }
        float rem_us = 0.0f;
        // loop: HOT-PATH-OK — ≤ 10 charge steps summed, at 10 Hz while charging
        for (int L = cur_step + 1; L <= 10; L++) rem_us += bulk * charge_step_weight(L);
        int mins = (int)(rem_us / 60000000.0f + 0.5f);
        if (mins < 0)   mins = 0;
        if (mins > 999) mins = 999;
        g_charge_eta.valid = true;
        g_charge_eta.provisional = !measured;
        g_charge_eta.minutes = mins;
    } else {
        // cur_step == 10 → essentially full; nothing meaningful to count down.
        g_charge_eta.valid = true;
        g_charge_eta.provisional = false;
        g_charge_eta.minutes = 0;
    }
}

// oled_pad::draw on the framebuffer. Its own noinline function so the glyph
// doesn't grow render_screen's literal pool (see CLAUDE.md).
struct FbCanvas {
    void px(int x, int y, bool on) { ::px(x, y, on); }
    void outline(int x, int y, int w, int h) { rect_outline(x, y, w, h); }
    void filled(int x, int y, int w, int h) { rect_filled(x, y, w, h); }
    void invert(int x, int y, int w, int h) { rect_invert(x, y, w, h); }
};
static_assert(oled_pad::kW == kW && oled_pad::kLeft >= kContentX, "glyph vs. screen / chrome");

// The model comes from the colorway read at connect (cmd_controller_model,
// the identity's [26]): 1 = Edge adds its Fn / back buttons; 0xFF (not known
// yet, or a build without the vendor interface) draws a standard DualSense.
__attribute__((noinline)) void render_status_pad() {
    FbCanvas c;
    oled_pad::draw(c, interrupt_in_data, cmd_controller_model() == 1);
}

__attribute__((noinline)) void render_screen() {
    fb_clear();

    const bool connected = bt_is_connected();

    // FIRMWARE_VERSION is set via CMake (the release tag, or a Git stamp for
    // dev builds); shown without "-oled-edition", scrolling if still too long.
    draw_title_scrolling(kContentX, 120, 0, kStatusTitle.s, kStatusTitle.n);
    draw_icon(120, 0, connected ? kIconLinkOn : kIconLinkOff, 8, 8);

    if (connected) {
        uint8_t a[6];
        bt_get_addr(a);
        char buf[24];
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                 a[0], a[1], a[2], a[3], a[4], a[5]);
        draw_text(kContentX, 9, buf);

        const uint8_t pwr = interrupt_in_data[52];
        int pct = (pwr & 0x0F) * 10;
        if (pct > 100) pct = 100;
        const uint8_t pstate = pwr >> 4;
        char marker = ' ';
        if (pstate == 1) marker = '+';      // Charging
        else if (pstate == 2) marker = '*'; // Complete
        else if (pstate >= 0xA) marker = '!'; // Error
        char bbuf[16];
        snprintf(bbuf, sizeof(bbuf), "%3d%%%c", pct, marker);
        draw_text(oled_pad::kBattTextX, oled_pad::kBattY, bbuf);
        FbCanvas c;
        oled_pad::battery(c, pct);  // x 36..91, over the stick boxes' inner edges

        // Charge ETA, right of the battery (mirrored from the level text). Shown only
        // while charging: "~43m?" is the provisional default-rate estimate shown
        // immediately on plug-in; the "?" drops to "~43m" once a real 10% step
        // has been timed and the measured rate takes over. See sample_charge_eta().
        if (g_charge_eta.charging) {
            char ebuf[8];
            if (g_charge_eta.valid)
                snprintf(ebuf, sizeof(ebuf), "~%dm%s", g_charge_eta.minutes,
                         g_charge_eta.provisional ? "?" : "");
            else
                snprintf(ebuf, sizeof(ebuf), "~--m");
            draw_text(oled_pad::kEtaX, oled_pad::kBattY, ebuf);
        }

        // The controller glyph (src/oled_pad.h): sticks, L2/R2, L1/R1,
        // Create/Options, d-pad and face buttons (+ an Edge's Fn / back buttons),
        // mirrored around x = 63.5.
        render_status_pad();
    } else {
        draw_text(kContentX, 14, "Pair your DualSense:");
        draw_text(kContentX, 26, "1. Hold Create + PS");
        draw_text(kContentX, 36, "2. Wait for light bar");
        draw_text(kContentX, 46, "   to flash blue");
    }

    flush_fb();
}

__attribute__((noinline)) void render_screen_rssi() {
    fb_clear();
    draw_text(kContentX, 0, "BT Signal");
    if (bt_is_connected()) {
        int8_t rssi = 0;
        bt_get_signal_strength(&rssi);
        char buf[24];
        snprintf(buf, sizeof(buf), "RSSI: %d dBm", (int)rssi);
        draw_text(kContentX, 12, buf);

        // Map RSSI range -90..-40 dBm to 0..100% bar
        int pct = ((int)rssi + 90) * 100 / 50;
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        snprintf(buf, sizeof(buf), "Quality: %d%%", pct);
        draw_text(kContentX, 22, buf);
        rect_outline(kContentX, 34, 122, 10);
        int fill = (pct * 118) / 100;
        if (fill > 0) rect_filled(kContentX + 2, 36, fill, 6);

        const char *label = "Poor";
        if (rssi > -55) label = "Excellent";
        else if (rssi > -65) label = "Good";
        else if (rssi > -75) label = "Fair";
        snprintf(buf, sizeof(buf), "Link: %s", label);
        draw_text(kContentX, 48, buf);
    } else {
        draw_text(kContentX, 30, "(no controller)");
    }
    flush_fb();
}

// Diagnostics screen state. Read-only viewport that scrolls with controller
// D-pad up/down. No cursor — there's nothing to select.
int   diag_scroll = 0;
uint8_t diag_last_dpad = 8; // edge-trigger N/E/S/W like settings_handle_input
constexpr int kDiagVisible = 5;

// Per-second rates sampled once per render, shared across format_diag_row's
// rate-based rows so they stay in sync.
struct DiagRates {
    uint32_t usb_rate;
    uint32_t bt_rate;
    uint32_t mic_rate;
    uint32_t bt31_rate;
};
DiagRates g_diag_rates{};

void sample_diag_rates() {
    static uint32_t prev_us_frames = 0, prev_bt_packets = 0, prev_mic_frames = 0, prev_bt31 = 0;
    static uint32_t prev_sample_us = 0;
    const uint32_t now_us = time_us_32();
    const uint32_t cur_us_frames  = audio_usb_frames();
    const uint32_t cur_bt_packets = audio_bt_packets();
    const uint32_t cur_mic_frames = audio_mic_frames();
    const uint32_t cur_bt31       = bt_31_packet_count();
    if (prev_sample_us != 0 && now_us > prev_sample_us) {
        const uint32_t dt_us = now_us - prev_sample_us;
        if (dt_us > 0) {
            g_diag_rates.usb_rate  = (uint32_t)(((uint64_t)(cur_us_frames  - prev_us_frames)  * 1000000u) / dt_us);
            g_diag_rates.bt_rate   = (uint32_t)(((uint64_t)(cur_bt_packets - prev_bt_packets) * 1000000u) / dt_us);
            g_diag_rates.mic_rate  = (uint32_t)(((uint64_t)(cur_mic_frames - prev_mic_frames) * 1000000u) / dt_us);
            g_diag_rates.bt31_rate = (uint32_t)(((uint64_t)(cur_bt31       - prev_bt31)       * 1000000u) / dt_us);
        }
    }
    prev_us_frames  = cur_us_frames;
    prev_bt_packets = cur_bt_packets;
    prev_mic_frames = cur_mic_frames;
    prev_bt31       = cur_bt31;
    prev_sample_us  = now_us;
}

// Row list ordered by relevance: always-useful at top, parked-mic-investigation
// data at bottom. To add a row, bump kNumDiagRows and add a case.
constexpr int kNumDiagRows = 14;
__attribute__((noinline))
void format_diag_row(int idx, char* line, size_t n) {
    switch (idx) {
        case 0: {
            const uint32_t s = time_us_32() / 1000000u;
            snprintf(line, n, "Up:%luh %02lum %02lus",
                     (unsigned long)(s / 3600u),
                     (unsigned long)((s / 60u) % 60u),
                     (unsigned long)(s % 60u));
            break;
        }
        case 1:
            snprintf(line, n, "BT: %s", bt_is_connected() ? "connected" : "waiting");
            break;
        case 2:
            snprintf(line, n, "host02: %lu", (unsigned long)host_out02_total());
            break;
        case 3:
            snprintf(line, n, "trig %lu / tx %lu",
                     (unsigned long)host_out02_trig_allow(),
                     (unsigned long)host_out02_to_bt());
            break;
        case 4:
            // Audio chunks dropped: raw PCM (core1 behind), encoded speaker
            // (BT behind), haptic (waiting on speaker/BT). See audio.cpp.
            snprintf(line, n, "Drop r%lu e%lu h%lu", (unsigned long)audio_fifo_drops(),
                     (unsigned long)opus_fifo_drops(), (unsigned long)audio_hap_drops());
            break;
        case 5:
            snprintf(line, n, "BT31 in: %lu/s", (unsigned long)g_diag_rates.bt31_rate);
            break;
        case 6:
            snprintf(line, n, "USB aud: %lu/s", (unsigned long)g_diag_rates.usb_rate);
            break;
        case 7:
            snprintf(line, n, "BT32 out: %lu/s", (unsigned long)g_diag_rates.bt_rate);
            break;
        case 8:
            snprintf(line, n, "Mic in: %lu/s", (unsigned long)g_diag_rates.mic_rate);
            break;
        case 9:
            snprintf(line, n, "Mic dec=%ld w=%u",
                     (long)audio_mic_last_decoded(),
                     (unsigned)audio_mic_last_wrote());
            break;
        case 10:
            snprintf(line, n, "Mic PLC: %lu", (unsigned long)audio_mic_plc_frames());
            break;
        case 11: {
            uint8_t pfx[6]; bt_31_mic_prefix(pfx);
            snprintf(line, n, "%02X %02X %02X %02X %02X %02X",
                     pfx[0], pfx[1], pfx[2], pfx[3], pfx[4], pfx[5]);
            break;
        }
        case 12:
            snprintf(line, n, "c1 %lu e%lu", (unsigned long)(audio_c1_loops() >> 10),
                     (unsigned long)audio_c1_enc());
            break;
        case 13:
            snprintf(line, n, "raw%lu hap%lu", (unsigned long)audio_raw_push(),
                     (unsigned long)audio_hap_push());
            break;
        default:
            line[0] = '\0';
            break;
    }
}

void diag_handle_input(int visible) {
    // Controller or the app's virtual D-pad (g_in: released with neither).
    const uint8_t dpad = (uint8_t)(g_in.b7 & 0x0F);
    if (dpad != diag_last_dpad && dpad != 8) {
        if      (dpad == 0) diag_scroll--; // up
        else if (dpad == 4) diag_scroll++; // down
    }
    diag_last_dpad = dpad;
    const int max_top = (kNumDiagRows > visible) ? (kNumDiagRows - visible) : 0;
    if (diag_scroll < 0) diag_scroll = 0;
    if (diag_scroll > max_top) diag_scroll = max_top;
}

__attribute__((noinline)) void render_screen_diag() {
    static uint32_t seen_generation = ~0u;
    static char last_row[kDiagVisible][20]{};
    static uint8_t last_markers = 0xff;
    const bool force = seen_generation != fb_generation;
    if (force) {
        fb_clear();
        draw_text(kContentX, 0, "Diagnostics");
        seen_generation = fb_generation;
    }

    sample_diag_rates();

    char line[20]; // 19 glyphs fit before the scroll markers at x=120
    // loop: HOT-PATH-OK — five visible rows at 10 Hz, unchanged strings skip drawing
    for (int i = 0; i < kDiagVisible; i++) {
        if (diag_scroll + i < kNumDiagRows) format_diag_row(diag_scroll + i, line, sizeof(line));
        else line[0] = '\0';
        if (!force && strcmp(last_row[i], line) == 0) continue;
        const int y = 9 + i * 9;
        rect_clear(kContentX, y, 113, 9);
        draw_text(kContentX, y, line);
        strcpy(last_row[i], line);
    }

    const uint8_t markers = (diag_scroll > 0 ? 1 : 0)
        | (diag_scroll + kDiagVisible < kNumDiagRows ? 2 : 0);
    if (force || markers != last_markers) {
        rect_clear(120, 9, 8, 45);
        if (markers & 1) draw_text(120, 9, "^");
        if (markers & 2) draw_text(120, 45, "v");
        last_markers = markers;
    }

    flush_fb();
}

__attribute__((noinline)) void render_screen_cpu(bool entered) {
    fb_clear();
    draw_text(kContentX, 0, "CPU / Clock");

    char buf[24];

    // Configured system clock — compile-time SYS_CLOCK_KHZ, set in main()
    // via set_sys_clock_khz(). This is the *target*.
    const uint32_t set_khz = (uint32_t)SYS_CLOCK_KHZ;
    snprintf(buf, sizeof(buf), "Set : %lu MHz", (unsigned long)(set_khz / 1000u));
    draw_text(kContentX, 12, buf);

    // Actually running clk_sys, measured by the on-chip frequency counter
    // against the crystal reference (not just what we asked for). The counter
    // busy-waits a few ms per call, so measure ONCE on screen entry and cache
    // it — clk_sys is fixed at boot and never changes, so the temperature
    // (which legitimately drifts) is the only thing worth refreshing per
    // frame. cached_real_khz==0 also forces a (re)measure as a safety net.
    static uint32_t cached_real_khz = 0;
    if (entered || cached_real_khz == 0) {
        cached_real_khz = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);
    }
    const uint32_t real_khz = cached_real_khz;
    snprintf(buf, sizeof(buf), "Real: %lu.%01lu MHz",
             (unsigned long)(real_khz / 1000u),
             (unsigned long)((real_khz % 1000u) / 100u));
    draw_text(kContentX, 22, buf);

    // Core voltage actually programmed into the regulator, read back (not the
    // compile-time constant). Codes 0..15 are linear 0.05 V steps from 0.55 V.
    const int vcode = (int)vreg_get_voltage();
    if (vcode >= 0 && vcode <= 0b01111) {
        const unsigned mv = 550u + 50u * (unsigned)vcode;
        snprintf(buf, sizeof(buf), "Vcore: %u.%02u V", mv / 1000u, (mv % 1000u) / 10u);
    } else {
        snprintf(buf, sizeof(buf), "Vcore: code %d", vcode);
    }
    draw_text(kContentX, 32, buf);

    // RP2350 on-die temperature sensor. Smoothed + averaged in cmd.cpp
    // (single source of truth shared with the 0xfc web telemetry) so the
    // reading converges to the true die temp instead of chasing ADC noise.
    const uint16_t raw = cpu_temp_raw_smoothed();
    const float volts = (float)raw * 3.3f / 4096.0f;
    const float temp_c = 27.0f - (volts - 0.706f) / 0.001721f;
    const int t10 = (int)(temp_c * 10.0f + (temp_c >= 0 ? 0.5f : -0.5f));
    snprintf(buf, sizeof(buf), "Temp : %d.%d C", t10 / 10,
             (t10 < 0 ? -t10 : t10) % 10);
    draw_text(kContentX, 42, buf);

    flush_fb();
}

// △ rising edge on the Trigger Test screen cycles trigger_preset and
// re-applies the new effect to the paired controller. KEY1 used to do
// this; moving it to the controller frees K0/K1 for navigation only.
void triggers_handle_input() {
    // Needs a controller: the preset goes to its triggers.
    if (!bt_is_connected()) { triggers_last_face = 0; return; }
    const uint8_t face = g_in.b7 & 0xF0;
    const bool tri_now  = (face & 0x80) != 0;
    const bool tri_prev = (triggers_last_face & 0x80) != 0;
    if (tri_now && !tri_prev) {
        trigger_preset = (trigger_preset + 1) % kNumTrigPresets;
        send_trigger_effect(trigger_preset);
    }
    triggers_last_face = face;
}

__attribute__((noinline)) void render_screen_triggers() {
    fb_clear();
    draw_text(kContentX, 0, "Trigger Test");

    char buf[24];
    snprintf(buf, sizeof(buf), "Mode: %s", kTrigPresetNames[trigger_preset]);
    draw_text(kContentX, 12, buf);

    if (bt_is_connected()) {
        const uint8_t l2 = interrupt_in_data[4];
        const uint8_t r2 = interrupt_in_data[5];
        snprintf(buf, sizeof(buf), "L2:%3d  R2:%3d", l2, r2);
        draw_text(kContentX, 24, buf);

        rect_outline(kContentX, 35, 56, 9);
        int lfill = (l2 * 52) / 255;
        if (lfill > 0) rect_filled(kContentX + 2, 37, lfill, 5);
        rect_outline(72, 35, 56, 9);
        int rfill = (r2 * 52) / 255;
        if (rfill > 0) rect_filled(74, 37, rfill, 5);
    } else {
        draw_text(kContentX, 24, "(no controller)");
    }

    draw_text(kContentX, 56, "Tri=cycle");
    flush_fb();
}

// --- IMU calibration (DS5 feature report 0x05) ---------------------------
// The DualSense ships per-unit gyro/accel calibration in feature report 0x05,
// which bt.cpp already fetches and caches at connect (init_feature). Parsing it
// lets the Gyro Tilt screen and the tilt->RGB lightbar mode use bias- and
// sensitivity-corrected accel instead of raw counts, so the tilt dot recenters
// per controller. Parse + apply mirror SDL's SDL_hidapi_ps5.c (zlib-licensed)
// LoadCalibrationData/ApplyCalibrationData (credit); feature_data[0x05]'s byte
// layout matches SDL's data[] (index 0 = report id, calibration words from 1).
//
// imu_apply keeps accel in the same +-8192 == 1g count space the callers already
// scale by, so existing /8192 (gyro screen) and +-8192 (lightbar) math is
// unchanged — calibration only removes the per-axis zero offset and corrects gain.
struct ImuCal { int16_t bias; float sens; };  // 0..2 gyro P/Y/R, 3..5 accel X/Y/Z
ImuCal g_imu_cal[6];
bool   g_imu_cal_valid = false;   // a plausible calibration was loaded
bool   g_imu_cal_tried = false;   // 0x05 has been seen this connection (good or bad)

constexpr float kGyroResPerDeg = 1024.0f;
constexpr float kAccelResPerG  = 8192.0f;

inline int16_t cal_ld16(const std::vector<uint8_t>& d, int i) {
    return (int16_t)((uint16_t)d[i] | ((uint16_t)d[i + 1] << 8));
}

__attribute__((noinline))
void imu_cal_parse(const std::vector<uint8_t>& d) {
    g_imu_cal_valid = false;
    if (d.size() < 35) return;                 // SDL requires >= 35 calibration bytes

    const int16_t gPB = cal_ld16(d, 1),  gYB = cal_ld16(d, 3),  gRB = cal_ld16(d, 5);
    const int16_t gPp = cal_ld16(d, 7),  gPm = cal_ld16(d, 9);
    const int16_t gYp = cal_ld16(d, 11), gYm = cal_ld16(d, 13);
    const int16_t gRp = cal_ld16(d, 15), gRm = cal_ld16(d, 17);
    const int16_t gSp = cal_ld16(d, 19), gSm = cal_ld16(d, 21);
    const int16_t aXp = cal_ld16(d, 23), aXm = cal_ld16(d, 25);
    const int16_t aYp = cal_ld16(d, 27), aYm = cal_ld16(d, 29);
    const int16_t aZp = cal_ld16(d, 31), aZm = cal_ld16(d, 33);

    const float num = (float)(gSp + gSm) * kGyroResPerDeg;
    g_imu_cal[0] = { gPB, num / (float)(gPp - gPm) };
    g_imu_cal[1] = { gYB, num / (float)(gYp - gYm) };
    g_imu_cal[2] = { gRB, num / (float)(gRp - gRm) };

    int16_t r;
    r = aXp - aXm; g_imu_cal[3] = { (int16_t)(aXp - r / 2), 2.0f * kAccelResPerG / (float)r };
    r = aYp - aYm; g_imu_cal[4] = { (int16_t)(aYp - r / 2), 2.0f * kAccelResPerG / (float)r };
    r = aZp - aZm; g_imu_cal[5] = { (int16_t)(aZp - r / 2), 2.0f * kAccelResPerG / (float)r };

    // Sanity gate (same as SDL): a wild bias or a gain off by >50% means a bad
    // factory cal or a short/garbled read — fall back to raw rather than amplify it.
    // loop: NOT-WORTH — 6 IMU axes, once per connection when 0x05 lands
    for (int i = 0; i < 6; i++) {
        const float divisor = (i < 3) ? 64.0f : 1.0f;
        const int   ab      = g_imu_cal[i].bias < 0 ? -g_imu_cal[i].bias : g_imu_cal[i].bias;
        float       gain    = 1.0f - g_imu_cal[i].sens / divisor;
        if (gain < 0) gain = -gain;
        if (ab > 1024 || gain > 0.5f) return;  // leave g_imu_cal_valid = false
    }
    g_imu_cal_valid = true;
}

// Poll once per frame from oled_loop: parse 0x05 the first time it is available
// for this controller, and reset on disconnect so the next controller re-reads.
void imu_cal_service() {
    if (!bt_is_connected()) { g_imu_cal_valid = false; g_imu_cal_tried = false; return; }
    if (g_imu_cal_tried) return;
    // lifetime: OK — no heap until 0x05 arrives (empty vector), then one copy per connection (g_imu_cal_tried)
    auto d = bt_peek_feature(0x05);
    if (d.size() < 35) return;                 // not arrived yet — retry next frame
    imu_cal_parse(d);
    g_imu_cal_tried = true;
}

// index 0..2 gyro, 3..5 accel. Returns the calibrated value in the same count
// scale the raw value used (+-8192 == 1g for accel); identity when no valid
// calibration is loaded, so behaviour matches the pre-calibration firmware.
inline int16_t imu_apply(int index, int16_t raw) {
    if (!g_imu_cal_valid) return raw;
    return (int16_t)((float)(raw - g_imu_cal[index].bias) * g_imu_cal[index].sens);
}

__attribute__((noinline)) void render_screen_gyro() {
    fb_clear();
    draw_text(kContentX, 0, "Gyro Tilt");
    if (bt_is_connected()) {
        int16_t ax, ay, az;
        memcpy(&ax, &interrupt_in_data[21], 2);
        memcpy(&ay, &interrupt_in_data[23], 2);
        memcpy(&az, &interrupt_in_data[25], 2);
        ax = imu_apply(3, ax);  // bias/sensitivity-corrected accel (identity if no cal)
        ay = imu_apply(4, ay);
        az = imu_apply(5, az);
        char buf[16];
        snprintf(buf, sizeof(buf), "X%+5d", ax); draw_text(kContentX, 10, buf);
        snprintf(buf, sizeof(buf), "Y%+5d", ay); draw_text(50, 10, buf);
        snprintf(buf, sizeof(buf), "Z%+5d", az); draw_text(94, 10, buf);

        const int bx = 44, by = 22, bw = 40, bh = 40;
        rect_outline(bx, by, bw, bh);
        // loop: HOT-PATH-OK — 38 px crosshair line, gyro screen at 10 Hz
        for (int x = bx + 1; x < bx + bw - 1; x++) px(x, by + bh / 2, true);
        // loop: HOT-PATH-OK — 38 px crosshair line, gyro screen at 10 Hz
        for (int y = by + 1; y < by + bh - 1; y++) px(bx + bw / 2, y, true);
        // Plot the two axes that read ~0 when the controller lies flat: X (roll,
        // left/right) and Z (pitch, fwd/back). Gravity rests on Y when flat, so
        // driving the dot from Y pegged it to the bottom edge at rest — using Z
        // keeps the dot centred flat and it tracks as you tilt. (Readout above
        // still shows all three raw axes.)
        // Negated so the dot follows the tilt direction: tilt left -> dot left,
        // tilt forward -> dot up (gravity pulls the opposite way on the axis).
        int dx = -((int)ax * (bw / 2 - 3)) / 8192;
        int dy = -((int)az * (bh / 2 - 3)) / 8192;
        int cx = bx + bw / 2 + dx;
        int cy = by + bh / 2 + dy;
        if (cx < bx + 2) cx = bx + 2;
        if (cx > bx + bw - 3) cx = bx + bw - 3;
        if (cy < by + 2) cy = by + 2;
        if (cy > by + bh - 3) cy = by + bh - 3;
        rect_filled(cx - 1, cy - 1, 3, 3);
    } else {
        draw_text(kContentX, 30, "(no controller)");
    }
    flush_fb();
}

__attribute__((noinline)) void render_screen_touchpad() {
    fb_clear();
    draw_text(kContentX, 0, "Touchpad");
    if (bt_is_connected()) {
        rect_outline(kContentX + 2, 12, 116, 30);
        int active = 0;
        // loop: HOT-PATH-OK — 2 fingers, touchpad screen at 10 Hz
        for (int finger = 0; finger < 2; finger++) {
            const int off = 32 + finger * 4;
            const uint32_t f = (uint32_t)interrupt_in_data[off] |
                               ((uint32_t)interrupt_in_data[off + 1] << 8) |
                               ((uint32_t)interrupt_in_data[off + 2] << 16) |
                               ((uint32_t)interrupt_in_data[off + 3] << 24);
            const bool not_touching = (f >> 7) & 1u;
            if (not_touching) continue;
            const uint16_t fx = (f >> 8) & 0xFFFu;
            const uint16_t fy = (f >> 20) & 0xFFFu;
            int sx = (kContentX + 3) + ((int)fx * 110) / 1919;
            int sy = 13 + ((int)fy * 26) / 1079;
            if (sx < kContentX + 3)   sx = kContentX + 3;
            if (sx > 122) sx = 122;
            if (sy < 13)  sy = 13;
            if (sy > 40)  sy = 40;
            rect_filled(sx - 1, sy - 1, 3, 3);
            active++;
        }
        char buf[20];
        snprintf(buf, sizeof(buf), "Fingers: %d", active);
        draw_text(kContentX, 46, buf);
    } else {
        draw_text(kContentX, 30, "(no controller)");
    }
    flush_fb();
}

void send_lightbar_color(uint8_t r, uint8_t g, uint8_t b) {
    uint8_t pkt[78] = {};
    pkt[0] = 0x31;
    pkt[2] = 0x10;
    pkt[4] = 0x04; // valid_flag1: LIGHTBAR_CONTROL_ENABLE (bit 2)
    pkt[47] = r;   // lightbar_red
    pkt[48] = g;   // lightbar_green
    pkt[49] = b;   // lightbar_blue
    state_stamp_host_rumble(pkt + 3); // ~10 Hz: must not cancel the game's rumble
    bt_write(pkt, sizeof(pkt));
}

// Tiny 32-step sine LUT (no <cmath>). angle 0..255 → amplitude -127..127.
static const int8_t kSine32[32] = {
    0,   24,   49,   70,   90,  106,  117,  125,  127,  125,  117,  106,   90,   70,   49,   24,
    0,  -24,  -49,  -70,  -90, -106, -117, -125, -127, -125, -117, -106,  -90,  -70,  -49,  -24,
};
int sin_lut(uint8_t a) { return kSine32[(a >> 3) & 0x1F]; }

void hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b) {
    if (h >= 360) h %= 360;
    const uint8_t region = (uint8_t)(h / 60);
    const uint16_t remainder = (uint16_t)((h - region * 60u) * 256u / 60u);
    const uint8_t p = (uint8_t)(((uint16_t)v * (255u - s)) >> 8);
    const uint8_t q = (uint8_t)(((uint16_t)v * (255u - (((uint16_t)s * remainder) >> 8))) >> 8);
    const uint8_t t = (uint8_t)(((uint16_t)v * (255u - (((uint16_t)s * (255u - remainder)) >> 8))) >> 8);
    switch (region) {
        case 0:  *r = v; *g = t; *b = p; break;
        case 1:  *r = q; *g = v; *b = p; break;
        case 2:  *r = p; *g = v; *b = t; break;
        case 3:  *r = p; *g = q; *b = v; break;
        case 4:  *r = t; *g = p; *b = v; break;
        default: *r = v; *g = p; *b = q; break;
    }
}

const char* lb_mode_tag(int mode) {
    switch (mode) {
        case 0: return "[LIVE]";
        case 1: return "[FAV0]";
        case 2: return "[FAV1]";
        case 3: return "[FAV2]";
        case 4: return "[FAV3]";
        case 5: return "[BREA]";
        case 6: return "[RAIN]";
        case 7: return "[FADE]";
        case 8: return "[HOST]";
        default: return "[????]";
    }
}

// R1 rising edge on Lightbar cycles lb_mode. Used to be KEY1; that moved
// to back-nav. Triangle on this screen stays as "save current RGB to
// favorite slot 0" (the existing favorite-save UX), so R1 is the next
// free button that doesn't break a mental model.
void lightbar_handle_input() {
    // Needs a controller: the modes and favorites are its lightbar's colors.
    if (!bt_is_connected()) { lb_last_buttons = 0; lb_last_face = 0; return; }
    const uint8_t btns   = g_in.b8;
    const bool r1_now    = (btns & 0x02) != 0;
    const bool r1_prev   = (lb_last_buttons & 0x02) != 0;
    if (r1_now && !r1_prev) {
        lb_mode = (lb_mode + 1) % kNumLbModes;
        lb_dirty = true; // persisted on leaving the Lightbar screen
    }
    lb_last_buttons = btns;

    // Face button rising edge -> save the current color to favorite 0..3.
    // Sampled every pass like R1 (it used to be read at the 10 Hz render, so
    // a tap shorter than a frame could be missed).
    const uint8_t face = g_in.b7 & 0xF0;
    const uint8_t pressed = face & ~lb_last_face;
    lb_last_face = face;
    int save_slot = -1;
    if      (pressed & 0x80) save_slot = 0; // Triangle
    else if (pressed & 0x40) save_slot = 1; // Circle
    else if (pressed & 0x20) save_slot = 2; // Cross
    else if (pressed & 0x10) save_slot = 3; // Square
    if (save_slot >= 0) {
        lb_fav_r[save_slot] = lb_r;
        lb_fav_g[save_slot] = lb_g;
        lb_fav_b[save_slot] = lb_b;
        lb_dirty = true; // persisted on leaving the Lightbar screen
    }
}

__attribute__((noinline)) void render_screen_lightbar() {
    fb_clear();
    draw_text(kContentX, 0, "Lightbar");
    draw_text(86, 0, lb_mode_tag(lb_mode));

    if (bt_is_connected()) {
        // lb_r/lb_g/lb_b are computed every frame by lightbar_service() (which
        // runs ahead of this render in oled_loop), so here we only display them.
        char buf[16];
        snprintf(buf, sizeof(buf), "R:%3u", lb_r); draw_text(kContentX, 12, buf);
        snprintf(buf, sizeof(buf), "G:%3u", lb_g); draw_text(48, 12, buf);
        snprintf(buf, sizeof(buf), "B:%3u", lb_b); draw_text(90, 12, buf);

        const int by = 22, bh = 8;
        rect_outline(kContentX,  by, 38, bh); int rf = (lb_r * 34) / 255; if (rf > 0) rect_filled(kContentX + 2,  by + 2, rf, bh - 4);
        rect_outline(48, by, 38, bh); int gf = (lb_g * 34) / 255; if (gf > 0) rect_filled(50, by + 2, gf, bh - 4);
        rect_outline(90, by, 38, bh); int bf = (lb_b * 34) / 255; if (bf > 0) rect_filled(92, by + 2, bf, bh - 4);

        // Face buttons save to favorites: lightbar_handle_input().
        draw_text(kContentX, 38, "Sv:T=0 C=1 X=2 S=3");
        const char* hint =
            (lb_mode == 0)           ? "Tilt = R/G/B"   :
            (lb_mode == 5)           ? "Breathing FAV0" :
            (lb_mode == 6)           ? "Rainbow sweep"  :
            (lb_mode == 7)           ? "Fade thru FAVs" :
            (lb_mode == kLbModeHost) ? "Host controls"  :
                                       "Locked to fav";
        draw_text(kContentX, 48, hint);
        // No send here: lightbar_service() owns pushing the color to the
        // controller every frame, on this screen and every other.
    } else {
        draw_text(kContentX, 30, "(no controller)");
    }
    draw_text(kContentX, 56, "R1=mode");
    flush_fb();
}

// Compute lb_r/lb_g/lb_b for an OLED lightbar mode (0..7). HOST (8) is handled
// by the caller (no firmware color). noinline keeps the float/HSV literals out
// of lightbar_service's / oled_loop's literal pool (same Thumb reach constraint
// the render_screen_* functions hit).
__attribute__((noinline))
void lightbar_compute_mode(int mode, uint32_t now_ms) {
    if (mode == 0) {
        // LIVE: tilt -> RGB
        int16_t ax, ay, az;
        memcpy(&ax, &interrupt_in_data[21], 2);
        memcpy(&ay, &interrupt_in_data[23], 2);
        memcpy(&az, &interrupt_in_data[25], 2);
        ax = imu_apply(3, ax);  // calibrated accel keeps the +-8192 == 1g scale below
        ay = imu_apply(4, ay);
        az = imu_apply(5, az);
        const int rr = ((int)ax + 8192) * 255 / 16384;
        const int gg = ((int)ay + 8192) * 255 / 16384;
        const int bb = ((int)az + 8192) * 255 / 16384;
        lb_r = (uint8_t)(rr < 0 ? 0 : rr > 255 ? 255 : rr);
        lb_g = (uint8_t)(gg < 0 ? 0 : gg > 255 ? 255 : gg);
        lb_b = (uint8_t)(bb < 0 ? 0 : bb > 255 ? 255 : bb);
    } else if (mode <= 4) {
        // FAV slot: fixed color
        const int slot = mode - 1;
        lb_r = lb_fav_r[slot];
        lb_g = lb_fav_g[slot];
        lb_b = lb_fav_b[slot];
    } else if (mode == 5) {
        // BREATHING: modulate FAV0 brightness with a sine wave (~3 s cycle)
        const uint8_t phase = (uint8_t)(now_ms / 12);
        const int s = sin_lut(phase); // -127..127
        const uint16_t scale = (uint16_t)(32 + (s + 127) / 2); // 32..191
        lb_r = (uint8_t)((lb_fav_r[0] * scale) / 255);
        lb_g = (uint8_t)((lb_fav_g[0] * scale) / 255);
        lb_b = (uint8_t)((lb_fav_b[0] * scale) / 255);
    } else if (mode == 6) {
        // RAINBOW: hue sweep over ~6 s
        const uint16_t hue = (uint16_t)((now_ms / 17) % 360);
        hsv_to_rgb(hue, 255, 255, &lb_r, &lb_g, &lb_b);
    } else {
        // FADE between FAV slots, 2 s per slot
        const uint32_t kSlotMs = 2000;
        const uint32_t total = now_ms % (4 * kSlotMs);
        const int slot = (int)(total / kSlotMs);
        const int next = (slot + 1) & 3;
        const uint16_t blend = (uint16_t)(((total - slot * kSlotMs) * 256u) / kSlotMs);
        lb_r = (uint8_t)((lb_fav_r[slot] * (255 - blend) + lb_fav_r[next] * blend) / 255);
        lb_g = (uint8_t)((lb_fav_g[slot] * (255 - blend) + lb_fav_g[next] * blend) / 255);
        lb_b = (uint8_t)((lb_fav_b[slot] * (255 - blend) + lb_fav_b[next] * blend) / 255);
    }
}

// The single owner of the controller LED. Runs every frame (~10 Hz) from
// oled_loop, on every screen, so a chosen mode "sticks" everywhere instead of
// only while the Lightbar screen renders. Priority:
//   1. Charging  -> amber-orange breathing pulse (status indicator).
//   2. lb_mode != HOST -> the selected OLED mode/color.
//   3. HOST (or disconnected) -> hand the LED back to the host/game.
// When the firmware owns the LED it (a) writes state[] so the color rides every
// host/audio packet and (b) actively pushes it via send_lightbar_color so it
// updates even when the host is idle and animations keep moving. g_lightbar_
// override gates state_update() so host AllowLedColor writes can't stomp us.
__attribute__((noinline))
void lightbar_service() {
    if (!bt_is_connected()) { g_lightbar_override = false; return; }
    const uint32_t now_ms = time_us_32() / 1000;

    if (g_charge_eta.charging) {
        // ~4.6 s breathing cycle (256 phase steps × 18 ms). Base amber
        // (255,100,0) sine-enveloped from dim (24) to bright (240).
        const uint8_t  phase = (uint8_t)(now_ms / 18);
        const int      s     = sin_lut(phase);                          // -127..127
        const uint16_t scale = (uint16_t)(24 + ((s + 127) * 216) / 254); // 24..240
        lb_r = (uint8_t)((255u * scale) / 255u);
        lb_g = (uint8_t)((100u * scale) / 255u);
        lb_b = 0;
    } else if (lb_mode == kLbModeHost) {
        // Reflect the host's current LED on the OLED bars, then stand down.
        state_get_led(&lb_r, &lb_g, &lb_b);
        g_lightbar_override = false;
        return;
    } else {
        lightbar_compute_mode(lb_mode, now_ms);
    }

    g_lightbar_override = true;
    state_set_led(lb_r, lb_g, lb_b);  // carried by the BT-connect init
    // Active push so the LED updates when the host is idle and animations keep
    // moving. Audio frames (0x39) don't carry state, so this runs during audio too.
    send_lightbar_color(lb_r, lb_g, lb_b);
}

void lightbar_load_config() {
    const Config_body& c = get_config();
    lb_mode = c.lightbar_mode;
    if (lb_mode < 0 || lb_mode >= kNumLbModes) lb_mode = kLbModeHost;
    // loop: NOT-WORTH — 4 favorites, at boot and when the host changes config
    for (int i = 0; i < 4; i++) {
        lb_fav_r[i] = c.lb_fav_r[i];
        lb_fav_g[i] = c.lb_fav_g[i];
        lb_fav_b[i] = c.lb_fav_b[i];
    }
    lb_dirty = false;
}

void lightbar_save_config() {
    Config_body b = get_config();
    b.lightbar_mode = (uint8_t)lb_mode;
    // loop: NOT-WORTH — 4 favorites, leaving the Lightbar screen with changes (one flash save)
    for (int i = 0; i < 4; i++) {
        b.lb_fav_r[i] = lb_fav_r[i];
        b.lb_fav_g[i] = lb_fav_g[i];
        b.lb_fav_b[i] = lb_fav_b[i];
    }
    set_config(b);
    // error: SILENT-BUG — nothing; the Lightbar screen's mode/favorites apply now but a failed save on leaving the screen loses them at power-off Change: show "Save FAIL" (batch: an OLED toast on every screen)
    config_save();
    lb_dirty = false;
}

__attribute__((noinline)) void render_screen_vu() {
    fb_clear();
    draw_text(kContentX, 0, "Audio Meters");
    if (bt_is_connected()) {
        const uint8_t spk = audio_peak_speaker();
        const uint8_t hap = audio_peak_haptic();
        char buf[16];
        snprintf(buf, sizeof(buf), "SPK %3u", spk);
        draw_text(kContentX, 14, buf);
        rect_outline(48, 14, 80, 8);
        int sfill = (spk * 76) / 255;
        if (sfill > 0) rect_filled(50, 16, sfill, 4);

        snprintf(buf, sizeof(buf), "HAP %3u", hap);
        draw_text(kContentX, 28, buf);
        rect_outline(48, 28, 80, 8);
        int hfill = (hap * 76) / 255;
        if (hfill > 0) rect_filled(50, 30, hfill, 4);

        draw_text(kContentX, 42, "Live USB audio peaks");
    } else {
        draw_text(kContentX, 30, "(no controller)");
    }
    flush_fb();
}

void settings_adjust(int delta) {
    const SetItem &it = kSetItems[settings_sel];
    settings_dirty = true;
    // error: NOT-WORTH — Reset/Wipe have no editable value; Triangle runs the action
    if (it.kind == SK_ACTION) return;
    uint8_t *p = it.kind == SK_POWER ? &settings_power
        : reinterpret_cast<uint8_t*>(&settings_local) + it.off;
    if (it.kind == SK_BOOL || it.kind == SK_POWER) {
        *p ^= it.kind == SK_POWER ? it.off : 1;
        return;
    }
    auto adjust = [&](float current) {
        int v = it.flags & SIF_FLOAT10 ? static_cast<int>(current * 10.0f + 0.5f) : static_cast<int>(current);
        if (it.flags & SIF_GRID) {
            const auto floor_grid = [&](int x) { return x >= 0 ? x / it.step * it.step : -((-x + it.step - 1) / it.step * it.step); };
            const auto ceil_grid = [&](int x) { return -floor_grid(-x); };
            // loop: NOT-WORTH — abs(delta) steps, one on each D-pad adjustment
            for (int i = 0; i < (delta < 0 ? -delta : delta); ++i)
                v = delta > 0 ? floor_grid(v) + it.step : ceil_grid(v) - it.step;
        } else v += delta * it.step;
        if (it.flags & SIF_WRAP) {
            if (v < it.lo) v = it.hi;
            if (v > it.hi) v = it.lo;
        } else {
            if (v < it.lo) v = it.lo;
            if (v > it.hi) v = it.hi;
        }
        return it.flags & SIF_FLOAT10 ? v / 10.0f : static_cast<float>(v);
    };
    if (it.flags & (SIF_FLOAT | SIF_FLOAT10)) {
        float value;
        memcpy(&value, p, sizeof(value)); // packed floats need not be aligned
        value = adjust(value);
        memcpy(p, &value, sizeof(value));
    } else {
        *p = static_cast<uint8_t>(adjust(*p));
    }
}

void settings_handle_input() {
    // Controller or the app's virtual buttons (g_in: released with neither).
    const uint8_t dpad = (uint8_t)(g_in.b7 & 0x0F);
    const uint8_t face = (uint8_t)(g_in.b7 & 0xF0);

    // Edge-trigger on D-pad direction CHANGE; only pure N/E/S/W to avoid diagonals
    if (dpad != settings_last_dpad && dpad != 8) {
        if      (dpad == 0) settings_sel = (settings_sel - 1 + kNumSettingsItems) % kNumSettingsItems;
        else if (dpad == 4) settings_sel = (settings_sel + 1) % kNumSettingsItems;
        else if (dpad == 6) settings_adjust(-1);
        else if (dpad == 2) settings_adjust(+1);
    }
    settings_last_dpad = dpad;

    // Triangle handling — Reset and Wipe-slots items both require a 2 s hold;
    // every other item saves edits on a normal short press.
    const bool tri_now = (face & 0x80) != 0;
    const bool tri_prev = (settings_last_face & 0x80) != 0;
    const uint8_t action = kSetItems[settings_sel].act;
    const bool is_hold_item = action != ACT_NONE;
    if (tri_now && !tri_prev) {
        settings_tri_press_us = (uint32_t)time_us_32();
        settings_reset_triggered = false;
    }
    if (is_hold_item && tri_now && !settings_reset_triggered
        && ((uint32_t)time_us_32() - settings_tri_press_us) >= kResetHoldUs) {
        settings_reset_triggered = true;
        if (action == ACT_RESET) {
            config_default();
            // error: SHOWN — OLED footer "Reset FAIL" / "Save FAIL"
            if (config_save()) {
                settings_local = get_config();
                bright_idx = settings_local.screen_brightness;
                settings_power = config_power_flags();
                lightbar_load_config(); // refresh RAM lightbar state (no reboot here)
                settings_dirty = false;
                settings_show_status("Reset!");
            } else {
                settings_show_status("Reset FAIL");
            }
        } else {
            // error: SHOWN — OLED footer "Wipe FAIL" (fixed in this pass: always said "Slots wiped!")
            settings_show_status(bt_wipe_all_slots() ? "Slots wiped!" : "Wipe FAIL");
        }
    }
    if (!tri_now && tri_prev) {
        if (!is_hold_item && !settings_reset_triggered) {
            set_config(settings_local);
            bright_idx = get_config().screen_brightness;
            config_set_power_flags(settings_power);
            // error: SHOWN — OLED footer "Reset FAIL" / "Save FAIL"
            const bool saved = config_save();
            settings_show_status(saved ? "Saved!" : "Save FAIL");
            if (saved) settings_dirty = false;  // a failed save keeps the (*) marker
        }
        settings_reset_triggered = false;
    }
    settings_last_face = face;
}

__attribute__((noinline)) void format_settings_item(int idx, char* line, size_t n) {
    const SetItem &it = kSetItems[idx];
    const char *cur = idx == settings_sel ? ">" : " ";
    if (it.kind == SK_ACTION) { snprintf(line, n, "%s %s", cur, it.label); return; }
    const uint8_t *p = reinterpret_cast<const uint8_t*>(&settings_local) + it.off;
    char value[16];
    if (it.flags & SIF_FLOAT10) {
        float f; memcpy(&f, p, sizeof(f));
        const int g = static_cast<int>(f * 10.0f + 0.5f);
        snprintf(value, sizeof(value), "%d.%dx", g / 10, g % 10);
    } else if (it.flags & SIF_FLOAT) {
        float f; memcpy(&f, p, sizeof(f));
        snprintf(value, sizeof(value), "%+d%s", static_cast<int>(f), it.unit ? it.unit : "");
    } else {
        const uint8_t v = it.kind == SK_POWER ? (settings_power & it.off) != 0 : *p;
        if (it.names || it.kind == SK_BOOL || it.kind == SK_POWER) {
            const char* const* names = it.names ? it.names : kOnOff;
            const unsigned index = it.kind == SK_BOOL || it.kind == SK_POWER ? v != 0 : v % (it.hi + 1);
            snprintf(value, sizeof(value), "%s", names[index]);
        } else if (v == 0 && it.fmt != SF_PLAIN) {
            snprintf(value, sizeof(value), "%s", it.fmt == SF_OFF0 ? "off" : it.fmt == SF_PC0 ? "PC" : "game");
        } else {
            snprintf(value, sizeof(value), "%s%u%s", it.fmt == SF_REDUCE ? "-" : "", v, it.unit ? it.unit : "");
        }
    }
    snprintf(line, n, "%s %s %s", cur, it.label, value);
}

__attribute__((noinline)) void render_screen_settings() {
    fb_clear();
    char buf[24];
    snprintf(buf, sizeof(buf), "Settings %s", settings_dirty ? "(*)" : "   ");
    draw_text(kContentX, 0, buf);

    constexpr int kVisible = 5;
    int top = 0;
    if (settings_sel >= kVisible) top = settings_sel - kVisible + 1;
    char line[28];
    // loop: HOT-PATH-OK — kVisible = 5 settings rows, settings screen at 10 Hz
    for (int i = 0; i < kVisible && top + i < kNumSettingsItems; i++) {
        format_settings_item(top + i, line, sizeof(line));
        draw_text(kContentX, 9 + i * 9, line);
    }

    const bool status_live = settings_save_status[0]
        && (int32_t)(time_us_32() - settings_status_until_us) < 0;
    if (status_live) {
        draw_text(kContentX, 56, settings_save_status);
    } else if (kSetItems[settings_sel].hint) {
        draw_text(kContentX, 56, kSetItems[settings_sel].hint);
    } else {
        draw_text(kContentX, 56, "DP nav/adj  Tri=save");
    }
    flush_fb();
}

// ---- Slots screen (Phase G) ----------------------------------------------
// Multi-slot persistent pairing UI. Modeled on zurce/DS5Dongle-OLED.
// Credit to zurce.

int slots_cursor = -1;             // initialized to active slot on first entry
uint8_t slots_last_dpad = 8;
uint8_t slots_last_face = 0;
uint32_t slots_sq_press_us = 0;
bool slots_wipe_triggered = false;
const char* slots_status = "";
uint32_t slots_status_until_us = 0;
constexpr uint32_t kSlotsWipeHoldUs = 1500000;  // 1.5 s

void slots_handle_input() {
    if (slots_cursor < 0) slots_cursor = bt_get_slot();
    // Controller or the app's virtual buttons (g_in: released with neither;
    // without a controller only the app's presses move the cursor. Pressing
    // PS on a paired controller connects it and the dongle switches to its
    // slot, bt.cpp, HID Control open).
    const uint8_t dpad = (uint8_t)(g_in.b7 & 0x0F);
    const uint8_t face = (uint8_t)(g_in.b7 & 0xF0);

    if (dpad != slots_last_dpad && dpad != 8) {
        if      (dpad == 0) slots_cursor = (slots_cursor - 1 + kNumSlots) % kNumSlots;
        else if (dpad == 4) slots_cursor = (slots_cursor + 1) % kNumSlots;
    }
    slots_last_dpad = dpad;

    // Triangle rising edge: switch to cursor slot if different from active
    const bool tri_now = (face & 0x80) != 0;
    const bool tri_prev = (slots_last_face & 0x80) != 0;
    if (tri_now && !tri_prev) {
        if (slots_cursor != bt_get_slot()) {
            // error: SHOWN — OLED Slots status "Save FAIL" / "Wipe FAIL" (fixed in this pass: always said done)
            slots_status = bt_set_slot(slots_cursor) ? "Switched!" : "Save FAIL";
            slots_status_until_us = (uint32_t)time_us_32() + 1500000;
        }
    }

    // Square hold 1.5 s: wipe cursor slot
    const bool sq_now = (face & 0x10) != 0;
    const bool sq_prev = (slots_last_face & 0x10) != 0;
    if (sq_now && !sq_prev) {
        slots_sq_press_us = (uint32_t)time_us_32();
        slots_wipe_triggered = false;
    }
    if (sq_now && !slots_wipe_triggered
        && ((uint32_t)time_us_32() - slots_sq_press_us) >= kSlotsWipeHoldUs) {
        slots_wipe_triggered = true;
        // error: SHOWN — OLED Slots status "Save FAIL" / "Wipe FAIL" (fixed in this pass: always said done)
        slots_status = bt_forget_slot(slots_cursor) ? "Wiped!" : "Wipe FAIL";
        slots_status_until_us = (uint32_t)time_us_32() + 1500000;
    }
    if (!sq_now && sq_prev) slots_wipe_triggered = false;

    slots_last_face = face;
}

__attribute__((noinline)) void render_screen_slots() {
    if (slots_cursor < 0) slots_cursor = bt_get_slot();

    fb_clear();
    char hdr[24];
    const int active = bt_get_slot();
    const bool conn = bt_is_connected();
    snprintf(hdr, sizeof(hdr), "Slots        [s%d %s]", active, conn ? "ON" : "--");
    draw_text(kContentX, 0, hdr);

    // loop: HOT-PATH-OK — kNumSlots = 4 rows, slots screen at 10 Hz
    for (int i = 0; i < kNumSlots; i++) {
        char line[28];
        const char *cursor_mark = (i == slots_cursor) ? ">" : " ";
        const char *active_mark = (i == active) ? "*" : " ";
        if (slot_occupied(i)) {
            uint8_t a[6];
            slot_get_addr(i, a);
            snprintf(line, sizeof(line), "%s%d%s%02X:%02X:%02X:%02X:%02X:%02X",
                     cursor_mark, i, active_mark, a[0], a[1], a[2], a[3], a[4], a[5]);
        } else {
            snprintf(line, sizeof(line), "%s%d%s (empty)", cursor_mark, i, active_mark);
        }
        draw_text(kContentX, 9 + i * 9, line);
    }

    const bool status_live = slots_status[0]
        && (int32_t)(time_us_32() - slots_status_until_us) < 0;
    // Tri / Square are the controller's buttons: with none connected, say
    // what does work (PS on a paired controller switches to its slot), or,
    // with every slot taken, why a new controller can't pair.
    draw_text(kContentX, 56, slots_footer::pick(status_live ? slots_status : nullptr,
                                                conn, !slots_any_empty()));
    flush_fb();
}

void boot_splash() {
    fb_clear();
    auto cx_for = [](const char* s) {
        // loop: NOT-WORTH — strlen of 3 splash lines, boot-only
        int n = 0; while (s[n]) n++;
        return (128 - (n * 6 - 1)) / 2;
    };
    const char* l1 = "DS5 Bridge";
    const char* l2 = kVersionLabel.s;  // was a stale hard-coded "v0.6.0"
    const char* l3 = DS5_BOARD_NAME " + OLED";
    draw_text(cx_for(l1), 16, l1);
    // A version too wide to center (local builds) starts at the left edge;
    // the splash is static, so the end may be cut off there.
    const int cx2 = (128 - (kVersionLabel.n * 6 - 1)) / 2;
    draw_text(cx2 < 0 ? 0 : cx2, 30, l2);
    draw_text(cx_for(l3), 44, l3);
    flush_fb();
    sleep_ms(1500);
}

// Sample controller edges every main-loop pass, independently of the 10 Hz
// display cadence. A short press can begin and end between rendered frames.
// The app's virtual buttons (oled_vpad.h): advance the held press and merge
// it with the controller's buttons into g_in. A press wakes the screen like a
// KEY press (always, whatever CtrlWake says) and starts once it's on.
void vpad_tick() {
    uint8_t held = oledvpad::kNumButtons;
    const uint8_t ev = oledvpad::tick(g_vpad, time_us_32(), oled_power_state == OLED_ACTIVE, held);
    if (ev & oledvpad::kWake) last_activity_us = time_us_64();
    g_in = oledvpad::view(bt_is_connected(), interrupt_in_data[7], interrupt_in_data[8], held);
}

void screen_input_tick() {
    if (oled_power_state != OLED_ACTIVE) return;
    switch (current_screen) {
        case kScreenSlots: slots_handle_input(); break;
        case kScreenLightbar: lightbar_handle_input(); break;
        case kScreenTriggers: triggers_handle_input(); break;
        case kScreenDiag: diag_handle_input(kDiagVisible); break;
        case kScreenSettings:
            if (!settings_init_done) {
                settings_local = get_config();
                settings_power = config_power_flags();
                settings_init_done = true;
            }
            settings_handle_input();
            break;
        default: break;
    }
}

} // namespace

// Configurator trigger preview (0xF6 0x0A): the Trigger Test screen's presets.
void oled_send_trigger_preset(int preset) { send_trigger_effect(preset); }

bool oled_key_inject(uint8_t key, bool long_press) {
    // error: SHOWN — CMD_RESULT_BUSY in the 0xF1 result byte (cmd.cpp 0x18): four presses already waiting
    return oledkeys::push(g_key_inject, key, long_press);
}

bool oled_pad_inject(uint8_t button, uint16_t hold_ms) {
    // error: SHOWN — CMD_RESULT_BUSY in the 0xF1 result byte (cmd.cpp 0x19): four presses already waiting
    return oledvpad::push(g_vpad, button, hold_ms);
}

uint16_t oled_mirror(uint8_t *out, uint16_t cap) {
    if (cap < kOledMirrorBytes) return 0;
    out[0] = (uint8_t) oled_power_state;  // 0 on, 1 dimmed (breathing dot), 2 off
    out[1] = (uint8_t) current_screen;
    out[2] = current_contrast;
    out[3] = fb_shown_seq;
    memcpy(out + 4, fb_shown, sizeof(fb_shown));
    return kOledMirrorBytes;
}

void oled_init() {
    spi_init(spi1, 10 * 1000 * 1000);
    gpio_set_function(kPinCLK, GPIO_FUNC_SPI);
    gpio_set_function(kPinMOSI, GPIO_FUNC_SPI);

    gpio_init(kPinCS);   gpio_set_dir(kPinCS, GPIO_OUT);  gpio_put(kPinCS, 1);
    gpio_init(kPinDC);   gpio_set_dir(kPinDC, GPIO_OUT);  gpio_put(kPinDC, 0);
    gpio_init(kPinRST);  gpio_set_dir(kPinRST, GPIO_OUT); gpio_put(kPinRST, 1);

    gpio_init(kPinKey0); gpio_set_dir(kPinKey0, GPIO_IN); gpio_pull_up(kPinKey0);
    gpio_init(kPinKey1); gpio_set_dir(kPinKey1, GPIO_IN); gpio_pull_up(kPinKey1);

    hw_reset();
    sh1107_init();
    panel_in_sync = false;
    fb_clear();
    boot_splash();

    // Restore the persisted lightbar mode + favorites (config_load() already ran
    // in main() before this). Defaults to HOST passthrough on a fresh flash.
    lightbar_load_config();

    // Restore the persisted OLED brightness (KEY1-long-press choice). config_valid
    // clamps screen_brightness to a legal kBrightLevels index, so this is safe to
    // use directly. Fresh flash → 0 (full brightness). Issue #9.
    bright_idx = get_config().screen_brightness;
}

// Dim-tier renderer: blank the panel and draw a tiny "I'm alive" dot that
// breathes (1s on / 1s off) and walks through 8 evenly-spaced positions every
// ~30 s. Two goals: (1) reduce total pixel-on-time to a tiny fraction so the
// panel barely glows even with the contrast register pinned, (2) prevent any
// single pixel from accumulating wear. noinline keeps oled_loop's literal pool
// in Thumb's 4 KB reach (same constraint the other render_screen_* hit).
__attribute__((noinline))
void render_dim_pulse(uint32_t dim_elapsed_us) {
    fb_clear();
    constexpr uint32_t kPulsePeriodUs = 2UL * 1000000UL; // 2 s blink cycle
    constexpr uint32_t kPulseOnUs     = 1UL * 1000000UL; // 1 s on, 1 s off
    constexpr uint32_t kPosStepUs     = 30UL * 1000000UL; // 30 s per position
    constexpr int kPositions[][2] = {
        { 16,  8}, { 64,  8}, {112,  8},
        {112, 32},
        {112, 56}, { 64, 56}, { 16, 56},
        { 16, 32},
    };
    constexpr int kNumPositions = sizeof(kPositions) / sizeof(kPositions[0]);
    const bool dot_on = (dim_elapsed_us % kPulsePeriodUs) < kPulseOnUs;
    if (dot_on) {
        const int idx = (int)((dim_elapsed_us / kPosStepUs) % (uint32_t)kNumPositions);
        const int cx = kPositions[idx][0];
        const int cy = kPositions[idx][1];
        // 2x2 dot — small enough to barely register, big enough to see across a desk.
        rect_filled(cx, cy, 2, 2);
    }
    flush_fb_raw(); // skip chrome arrows; nothing to navigate to from sleep
}

// The configurator changed config behind the OLED's back: re-read every field
// the OLED caches, and drop the Settings snapshot so a later Triangle-save on
// the dongle can't write the stale copy over the host's changes.
static void sync_host_config_changes() {
    static uint32_t seen_gen = 0;
    const uint32_t gen = cmd_host_config_gen();
    if (gen == seen_gen) return;
    seen_gen = gen;
    lightbar_load_config();
    bright_idx = get_config().screen_brightness;
    settings_init_done = false;
    settings_dirty = false;
}

void oled_loop() {
    sync_host_config_changes();
    handle_buttons();
    vpad_tick();
    screen_input_tick();
    const uint32_t now = time_us_32();
    rumble_burst_tick(now);
    if ((now - last_render_us) < kFrameUs) return;
    last_render_us = now;
    // Track charge progress every frame — before the power-ladder early-returns
    // below, so step timing stays correct even while the panel is dimmed/off.
    sample_charge_eta();
    // Parse the DS5's per-unit IMU calibration once it lands (no-op until then),
    // so the tilt screen + tilt->RGB lightbar use corrected accel. See imu_apply().
    imu_cal_service();
    // Drive the controller LED every frame (any screen / power state): charging
    // pulse, selected OLED mode, or hand-off to the host. See lightbar_service().
    lightbar_service();
    // Bump activity on controller input changes (cheap rolling hash over input
    // bytes). Mirror bt.cpp's inactivity heuristic so resting-controller noise
    // doesn't read as activity: the analog sticks (idata[0..3]) jitter by ±1 LSB
    // at rest, so collapse their rest band [120,140] to a constant, and skip
    // idata[6] (the volatile counter byte bt.cpp's idle check also ignores).
    // Without this the dot/dim tier never engages while a controller is
    // connected, because a stick flicker resets the idle timer every few frames.
    uint32_t hash = 0;
    // loop: HOT-PATH-OK — 10 input bytes hashed for the idle timer, at 10 Hz
    for (int i = 0; i < 10; i++) {
        if (i == 6) continue;
        uint8_t b = interrupt_in_data[i];
        if (i < 4 && b >= 120 && b <= 140) b = 128; // stick deadzone
        hash = hash * 31u + b;
    }
    if (hash != last_input_hash) {
        last_input_hash = hash;
        // Controller input only keeps the panel awake when the user has left
        // "CtrlWake" on (the default). With it off, the dim/off timers count
        // down during gameplay and only KEY0/KEY1 wake the screen — see
        // handle_buttons(), which bumps last_activity_us unconditionally.
        // Issues #8 / #9.
        if (get_config().controller_wakes_display) last_activity_us = time_us_64();
    }
    // Rising-edge: BT-connect itself counts as activity, so the screen wakes
    // the moment a controller pairs rather than waiting for the first input.
    const bool bt_connected_now = bt_is_connected();
    if (bt_connected_now && !prev_bt_connected) last_activity_us = time_us_64();
    prev_bt_connected = bt_connected_now;

    // Power-state ladder: Active → Dim (breathing dot) → Off based on idle time.
    // Thresholds are user-configurable (minutes; 0 = that tier disabled) — #5.
    // While charging we cap the ladder at Dim — the panel keeps doing the
    // low-power breathing dot but never fully sleeps. This stops the user from
    // unplugging the controller just to "wake" the dongle (which would reset the
    // charge-ETA calibration). The dot tier already draws ~no current, so this
    // costs little; sample_charge_eta() runs before this block regardless.
    const uint64_t idle   = time_us_64() - last_activity_us;
    const uint64_t dim_us = (uint64_t)get_config().screen_dim_timeout * 60ULL * 1000000ULL;
    const uint64_t off_us = (uint64_t)get_config().screen_off_timeout * 60ULL * 1000000ULL;
    const bool off_enabled = get_config().screen_off_timeout != 0;
    const bool dim_enabled = get_config().screen_dim_timeout != 0;
    if (off_enabled && idle > off_us && !g_charge_eta.charging) {
        if (oled_power_state != OLED_OFF) {
            cmd(0xAE);
            oled_power_state = OLED_OFF;
        }
        return; // panel is off, nothing to draw
    }
    if (oled_power_state == OLED_OFF) cmd(0xAF); // wake panel before drawing
    if (dim_enabled && idle > dim_us) {
        sh1107_set_contrast(kDimContrast);
        oled_power_state = OLED_DIM;
        render_dim_pulse((uint32_t)(idle - dim_us));
        return; // skip the regular per-screen render path
    }
    sh1107_set_contrast(kBrightLevels[bright_idx]);
    oled_power_state = OLED_ACTIVE;

    // True on the first render after navigating to a different screen.
    // Lets a screen do expensive one-shot work on entry (the CPU screen
    // caches its frequency-counter measurement here).
    static int last_rendered_screen = -1;
    const bool screen_entered = (current_screen != last_rendered_screen);

    // Leaving Trigger Test in either direction → reset the adaptive
    // trigger preset to OFF and push it to the controller. Otherwise
    // the last-cycled effect (Weapon snap, Galloping pulse, etc.)
    // stays active on the DS5 indefinitely, which surprised users
    // who'd just navigated away expecting a clean slate.
    if (last_rendered_screen == kScreenTriggers
        && current_screen != kScreenTriggers) {
        trigger_preset = 0;
        send_trigger_effect(0);
    }

    // Leaving the Lightbar screen → persist mode/favorite changes made there,
    // batched into a single flash write instead of one per button press.
    if (last_rendered_screen == kScreenLightbar
        && current_screen != kScreenLightbar
        && lb_dirty) {
        lightbar_save_config();
    }

    last_rendered_screen = current_screen;

    switch (current_screen) {
        case kScreenStatus:   render_screen();           break;
        case kScreenSlots:    render_screen_slots();     break;
        case kScreenLightbar: render_screen_lightbar();  break;
        case kScreenTriggers: render_screen_triggers();  break;
        case kScreenGyro:     render_screen_gyro();      break;
        case kScreenTouchpad: render_screen_touchpad();  break;
        case kScreenDiag:     render_screen_diag();      break;
        case kScreenCpu:      render_screen_cpu(screen_entered); break;
        case kScreenRssi:     render_screen_rssi();      break;
        case kScreenVU:       render_screen_vu();        break;
        case kScreenSettings: render_screen_settings();  break;
    }
}
