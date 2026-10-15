//
// Created by awalol on 2026/5/4.
//

#ifndef DS5_BRIDGE_CONFIG_H
#define DS5_BRIDGE_CONFIG_H

#include <cstdint>

// The 0x39 report's AudioBufferLength floor. Since the packed 0x39 report
// (two speaker frames per report, upstream PR #227) the controller's speaker
// garbles below 28 and needs 32 to stay steady (upstream issue #252, still open
// upstream, which keeps allowing 16). 16..31 from older firmware or an older
// app reads as 32, the closest steady setting.
constexpr uint8_t kAudioBufferMin = 32;
constexpr uint8_t kAudioBufferMax = 128;

struct __attribute__((packed)) Config_body {
    uint8_t config_version; // Config Version
    float haptics_gain; // [1.0,6.0] left grip (and both while the app links them)
    float speaker_volume; // [-100,+12] dB digital gain on the speaker PCM; >0 boosts (hard-clipped at full scale)
    uint8_t inactive_time; // [5,60] min
    uint8_t disable_inactive_disconnect; // bool: 0 disable,1 enable
    uint8_t disable_pico_led; // bool
    uint8_t polling_rate_mode; // 0: 250Hz, 1: 500Hz, 2: real-time
    uint8_t audio_buffer_length; // [32,128] (kAudioBufferMin); 16..31 saved by older firmware read as 32
    uint8_t controller_mode; // 0: DS5, 1: DSE, 2: Auto
    uint8_t current_slot;    // [0..3] active multi-pairing slot (OLED Edition Phase G)
    // Audio Auto Haptics — derive haptic feedback from speaker audio for games that
    // send no per-frame native haptic data. DSP borrowed from loteran/DS5Dongle 5d6bc2f.
    uint8_t auto_haptics_enable;  // 0=Off (default), 1=Fallback, 2=Mix, 3=Replace
    uint8_t auto_haptics_gain;    // [0,200] percent, default 100
    uint8_t auto_haptics_lowpass; // 0=80Hz, 1=160Hz (default), 2=250Hz, 3=400Hz
    // Lightbar (OLED Edition Phase H): persisted so the chosen mode/colors
    // survive reboot and stick across all screens. lightbar_mode indexes the
    // OLED Lightbar screen's mode list — 0=LIVE, 1..4=FAV0..3, 5=BREATHING,
    // 6=RAINBOW, 7=FADE, 8=HOST (passthrough, the safe default that lets the
    // host/game own the LED). Keep this numbering in sync with kNumLbModes /
    // kLbModeHost in src/oled.cpp. Erased flash (0xFF) → HOST + white favorites.
    uint8_t lightbar_mode;
    uint8_t lb_fav_r[4];
    uint8_t lb_fav_g[4];
    uint8_t lb_fav_b[4];
    // OLED idle power-ladder thresholds, in minutes. 0 = that tier disabled.
    // Defaults preserve the original hardcoded ladder (2 min dim, 15 min off).
    // Range [0,250] (0xFF erased flash → default via config_valid clamp). The
    // idle timer is 64-bit µs so the full range is representable. Issue #5.
    uint8_t screen_dim_timeout;
    uint8_t screen_off_timeout;
    // DualSense mic over Bluetooth (Phase I). 0 = off, 1 = on (default). When on,
    // the dongle asserts the DS5 mic-enable bit so the controller streams its mic
    // over BT and the dongle decodes it to the USB capture endpoint. Costs extra
    // DS5 battery (keeps its audio subsystem awake), hence the toggle.
    uint8_t bt_mic_enable;
    // OLED brightness, as an index into kBrightLevels[] (src/oled.cpp). Persisted
    // so the KEY1-long-press brightness choice survives a power cycle. Erased
    // flash (0xFF) → clamped to 0 (full brightness) by config_valid. Issue #9.
    uint8_t screen_brightness;
    // When 0, controller input no longer keeps the OLED awake — only the OLED's
    // own KEY0/KEY1 do — so the dim/off timers actually count down during
    // gameplay and the panel can sleep while the controller is in use. Default 1
    // preserves the original "any controller activity wakes the screen"
    // behavior. Issues #8 (dim timeout never fired during play) and #9.
    uint8_t controller_wakes_display;
    // The narrator's firmware lines (src/voice.cpp): 0xFF off (default, also
    // erased flash), 1..7 = on, in en de fr es pt-BR zh-CN ja. Set by the app
    // (0xF6 func 0x0B, saved on its own so it never saves other edits).
    uint8_t narrator_voice;
    // Right grip haptics strength [1.0,6.0] (protocol 1.6). Older configs and
    // erased flash (NaN) take the left value, so both sides stay as they were.
    float haptics_gain_right;
    // Protocol 1.13 (upstream's batch 3, ported): appended, so a body saved
    // by older firmware (42 bytes, the rest of the page 0xFF) reads every one
    // as invalid and takes the default below.
    // Weaker adaptive triggers: 0 = the game's own strength (default; the
    // game's report passes through untouched, upstream #228), 1..10 = the
    // controller's TriggerMotorPowerReduction forced on every game report
    // (upstream a109481 / 0fba807 / a7824d9).
    uint8_t trigger_reduce;
    // The controller amp's pre-gain (SetStateData SpeakerCompPreGain): 0 = the
    // game's own (default), 1..7 forced (upstream cc9be13). Not speaker_volume,
    // which is our digital gain on the PCM before the encoder.
    uint8_t speaker_gain;
    // 1 = games can't change the controller's volume, mic volume, mute or mute
    // light (their allow bits are cleared on the forward); the PC's own volume
    // still applies (upstream ca1af99). Default 0.
    uint8_t lock_volume;
    // Which microphone the controller uses: 0 auto (default), 1 built-in, 2
    // headset (SetStateData MicSelect, upstream ea93fad). Off stays
    // bt_mic_enable.
    uint8_t mic_select;
    // Where the controller plays the PC's audio: 0 auto (headset when one is
    // plugged in, default), 1 speaker, 2 headset, 3 off (upstream ea93fad).
    uint8_t speaker_select;
    // A GPIO that shows the controller connection (upstream 86da98c, #225):
    // 0xFF off (default), else one of status_gpio.h's free pins. Mode 0 = high
    // while connected, 1 = a 200 ms high pulse when a controller connects.
    uint8_t status_gpio_pin;
    uint8_t status_gpio_mode;
    // Protocol 1.16: the controller's microphone level (SetStateData
    // VolumeMic, the only mic level the controller has: one absolute,
    // non-linear byte that tops out at 0x40). 0 = follow the PC's mic level
    // (default, as before: the USB mic feature unit's 0..48 dB maps onto
    // 0..0x40), 1..64 = this level, whatever the PC's slider says (its mute
    // still works). Appended like the 1.13 fields: 0xFF from older firmware
    // reads as invalid and takes the default.
    uint8_t mic_level;
    // Protocol 1.17: the controller's own echo cancellation (SetStateData
    // AudioControl EchoCancelEnable, [7] bit 2) for its microphone. 1 = on
    // (default), 0 = off. Forced either way on every report that sets
    // AudioControl (a game's, the PC driver's, the connect-time state) and
    // pushed at once on a change. 0xFF from older firmware takes the default.
    uint8_t mic_echo_cancel;
};

// The body as protocol 1.6 to 1.12 sent it. A config GET (0xF7) for the
// whole 63 bytes still answers these 42 bytes and then the remap block at 42,
// so older apps keep finding it; a shorter GET returns the whole body (the
// app asks for sizeof(Config_body) from protocol 1.13 on).
constexpr unsigned kConfigBodyV16 = 42;
static_assert(__builtin_offsetof(Config_body, trigger_reduce) == kConfigBodyV16, "protocol 1.13 fields start after the 1.6 body");
static_assert(__builtin_offsetof(Config_body, mic_level) == 49, "protocol 1.16's mic level follows the 49-byte 1.13 body");
static_assert(__builtin_offsetof(Config_body, mic_echo_cancel) == 50, "protocol 1.17's echo cancellation follows the 50-byte 1.16 body");
static_assert(sizeof(Config_body) == 51, "update cmd.cpp, the app's config.ts and CLAUDE.md with the body");

// What the dongle does when the PC sleeps (protocol 1.14, src/usb_power.h).
// Kept after the body, not in it: set and saved on its own (0xF6 0x17, like
// the narrator) and read in identity [47], so it never rides on a body write
// and needs no room in the 63-byte 0xF7 GET (see kConfigBodyV16).
enum : uint8_t {
    POWER_SLEEP_CONTROLLER_OFF = 0x01, // controller off once the PC has slept 3 s (default on, as upstream)
    POWER_PS_WAKES_PC = 0x02,          // PS button wakes the PC (default off, as upstream enable_wake)
    POWER_FLAGS_DEFAULT = POWER_SLEEP_CONTROLLER_OFF,
    POWER_FLAGS_MASK = 0x03,
};

struct __attribute__((packed)) Config {
    uint32_t magic;
    uint16_t version;
    uint32_t crc32; // Config_body crc32, only calc and verify when save
    uint16_t size;  // Config_body size
    Config_body body;
    // POWER_* bits. Not in the CRC (older firmware wrote 0xFF here, which reads
    // as the default, as does anything outside POWER_FLAGS_MASK).
    uint8_t power_flags;
};

void config_default();
void config_load();
bool config_save();
bool config_unsaved();  // RAM settings differ from flash
// Set the narrator byte in RAM and in flash, leaving every other saved
// setting as it is in flash (unsaved edits stay unsaved).
bool config_save_narrator(uint8_t value);
uint8_t config_power_flags();
// Set the POWER_* bits in RAM and in flash, like config_save_narrator (other
// unsaved edits stay unsaved). False = invalid bits or the flash write failed.
bool config_save_power_flags(uint8_t flags);
// RAM only (the OLED Settings screen; its Triangle save writes them).
void config_set_power_flags(uint8_t flags);
const Config_body& get_config();
void set_config(const uint8_t *new_config, const uint16_t len);
void config_valid();
void set_config(const Config_body &new_config);
extern bool is_dse;
// True when the dongle presents a DualSense (mode DS5, or Auto with a DualSense
// connected); false for the Edge ID. Defined in usb_descriptors.cpp.
bool ds_mode();

#endif //DS5_BRIDGE_CONFIG_H
