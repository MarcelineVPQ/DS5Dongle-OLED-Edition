// The OLED Settings screen (pure, host-tested in tests/host/test_settings_menu.cpp):
// its items, their groups, the value text, the D-pad adjustment, the button
// state machine (category menu <-> group page) and the drawing.
//
// Layout A (Marcy 2026-10-17): a menu of 6 groups, each opening a page of its
// items. The selected row is an inverted bar; a changed, unsaved value on it
// shows in a dark window cut out of the bar (kNotch). A scroll bar at x=125..127 shows where the
// page is. Display only: every item keeps its Config_body field (CFG_OFF) or
// power flag, so the save (settings_merge.h), the protocol and the app are
// unchanged.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "config.h"
#include "settings_merge.h"
#include "state_override.h"  // kMicLevelMax

namespace settings_menu {

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
inline const char* const kOnOff[] = {"off", "on"};
inline const char* const kDisabled[] = {"on", "off"};
inline const char* const kPoll[] = {"250Hz", "500Hz", "RT"};
inline const char* const kCtrl[] = {"DS5", "DSE", "Auto"};
inline const char* const kAutoHap[] = {"Off", "Fallback", "Mix", "Replace"};
inline const char* const kAHLp[] = {"80Hz", "160Hz", "250Hz", "400Hz"};
inline const char* const kSpkOut[] = {"Auto", "SPK", "Hset", "off"};
inline const char* const kMicIn[] = {"Auto", "Int", "Hset"};
#define CFG_OFF(field) static_cast<uint8_t>(offsetof(Config_body, field))
// Group by group (kGroups), most used first in each.
inline const SetItem kItems[] = {
    // Haptics/trigger
    {"Vibration L", nullptr, nullptr, nullptr, SK_NUM, SF_PLAIN, SIF_FLOAT10, ACT_NONE, CFG_OFF(haptics_gain), 10, 60, 5},
    {"Vibration R", nullptr, nullptr, nullptr, SK_NUM, SF_PLAIN, SIF_FLOAT10, ACT_NONE, CFG_OFF(haptics_gain_right), 10, 60, 5},
    {"Trig. effect", nullptr, nullptr, nullptr, SK_NUM, SF_REDUCE, 0, ACT_NONE, CFG_OFF(trigger_reduce), 0, 10, 1},
    {"AutoHap", nullptr, nullptr, kAutoHap, SK_NUM, SF_PLAIN, SIF_WRAP, ACT_NONE, CFG_OFF(auto_haptics_enable), 0, 3, 1},
    {"AutoHap gain", "%", nullptr, nullptr, SK_NUM, SF_PLAIN, 0, ACT_NONE, CFG_OFF(auto_haptics_gain), 0, 200, 10},
    {"Auto filter", nullptr, nullptr, kAHLp, SK_NUM, SF_PLAIN, SIF_WRAP, ACT_NONE, CFG_OFF(auto_haptics_lowpass), 0, 3, 1},
    // Audio
    {"Spk volume", "dB", nullptr, nullptr, SK_NUM, SF_PLAIN, SIF_FLOAT | SIF_GRID, ACT_NONE, CFG_OFF(speaker_volume), -100, 12, 5},
    {"Spk boost", nullptr, nullptr, nullptr, SK_NUM, SF_GAME0, 0, ACT_NONE, CFG_OFF(speaker_gain), 0, 7, 1},
    {"Lock volume", nullptr, nullptr, nullptr, SK_BOOL, SF_PLAIN, 0, ACT_NONE, CFG_OFF(lock_volume), 0, 1, 1},
    {"Audio out", nullptr, nullptr, kSpkOut, SK_NUM, SF_PLAIN, SIF_WRAP, ACT_NONE, CFG_OFF(speaker_select), 0, 3, 1},
    {"Built-in mic", nullptr, nullptr, nullptr, SK_BOOL, SF_PLAIN, 0, ACT_NONE, CFG_OFF(bt_mic_enable), 0, 1, 1},
    {"Mic used", nullptr, nullptr, kMicIn, SK_NUM, SF_PLAIN, SIF_WRAP, ACT_NONE, CFG_OFF(mic_select), 0, 2, 1},
    {"Mic level", nullptr, nullptr, nullptr, SK_NUM, SF_PC0, 0, ACT_NONE, CFG_OFF(mic_level), 0, kMicLevelMax, 4},
    {"Echo cancel", nullptr, nullptr, nullptr, SK_BOOL, SF_PLAIN, 0, ACT_NONE, CFG_OFF(mic_echo_cancel), 0, 1, 1},
    {"Audio buffer", nullptr, nullptr, nullptr, SK_NUM, SF_PLAIN, 0, ACT_NONE, CFG_OFF(audio_buffer_length), kAudioBufferMin, kAudioBufferMax, 4},
    // Connection
    {"Controller", nullptr, nullptr, kCtrl, SK_NUM, SF_PLAIN, SIF_WRAP, ACT_NONE, CFG_OFF(controller_mode), 0, 2, 1},
    {"Poll rate", nullptr, nullptr, kPoll, SK_NUM, SF_PLAIN, SIF_WRAP, ACT_NONE, CFG_OFF(polling_rate_mode), 0, 2, 1},
    {"Disconnect", nullptr, nullptr, kDisabled, SK_BOOL, SF_PLAIN, 0, ACT_NONE, CFG_OFF(disable_inactive_disconnect), 0, 1, 1},
    {"Idle time", "min", nullptr, nullptr, SK_NUM, SF_PLAIN, 0, ACT_NONE, CFG_OFF(inactive_time), 10, 60, 5},
    // Screen & lights
    {"Screen dim", "min", nullptr, nullptr, SK_NUM, SF_OFF0, 0, ACT_NONE, CFG_OFF(screen_dim_timeout), 0, 250, 1},
    {"Screen off", "min", nullptr, nullptr, SK_NUM, SF_OFF0, 0, ACT_NONE, CFG_OFF(screen_off_timeout), 0, 250, 1},
    {"Wake on input", nullptr, nullptr, nullptr, SK_BOOL, SF_PLAIN, 0, ACT_NONE, CFG_OFF(controller_wakes_display), 0, 1, 1},
    {"Status LED", nullptr, nullptr, kDisabled, SK_BOOL, SF_PLAIN, 0, ACT_NONE, CFG_OFF(disable_pico_led), 0, 1, 1},
    // PC sleep
    {"Turn off ctrl", nullptr, nullptr, nullptr, SK_POWER, SF_PLAIN, 0, ACT_NONE, POWER_SLEEP_CONTROLLER_OFF, 0, 1, 1},
    {"PS wakes PC", nullptr, nullptr, nullptr, SK_POWER, SF_PLAIN, 0, ACT_NONE, POWER_PS_WAKES_PC, 0, 1, 1},
    // Reset
    {"Reset to defaults", nullptr, "Hold Tri 2s = RESET", nullptr, SK_ACTION, SF_PLAIN, 0, ACT_RESET, 0, 0, 0, 1},
    {"Wipe all slots", nullptr, "Hold Tri 2s = WIPE", nullptr, SK_ACTION, SF_PLAIN, 0, ACT_WIPE, 0, 0, 0, 1},
};
#undef CFG_OFF
constexpr int kNumItems = sizeof(kItems) / sizeof(kItems[0]);

struct Group { const char *name; uint8_t first, count; };
inline constexpr Group kGroups[] = {
    {"Haptics/trigger", 0, 6},
    {"Audio", 6, 9},
    {"Connection", 15, 4},
    {"Screen & lights", 19, 4},
    {"PC sleep", 23, 2},
    {"Reset", 25, 2},
};
constexpr int kNumGroups = sizeof(kGroups) / sizeof(kGroups[0]);
constexpr bool groups_tile_items() {
    int next = 0;
    // loop: NOT-WORTH — compile time, 6 groups
    for (const Group &g : kGroups) {
        // error: NOT-WORTH — compile time: the static_assert below fails the build
        if (g.first != next || g.count == 0) return false;
        next += g.count;
    }
    return next == kNumItems;
}
static_assert(groups_tile_items(), "kGroups must cover kItems in order, each item once");

// Geometry: title y=0, 5 rows at y=9+i*9, footer y=56, content from x=6 (right
// of the flush chrome). A row is 19 glyphs: the label from column 1, the value
// right-aligned to column 18. The bar spans x=8..122 and y-1..y+7 (4 px either
// side of the text, 1 px above and below); the scroll bar x=125..127.
constexpr int kX = 6, kGlyphs = 19, kGlyphW = 6, kVisible = 5;
constexpr int kTitleY = 0, kRowY0 = 9, kRowH = 9, kFooterY = 56;
constexpr int kLabelCol = 1, kValueEndCol = kGlyphs - 1;
constexpr int kBarX = 8, kBarW = 115, kNotchCap = 2;
constexpr int kScrollX = 125, kScrollW = 3, kScrollY = kRowY0, kScrollH = kVisible * kRowH - 2;
static_assert(kBarX + kBarW < kScrollX, "the bar stops short of the scroll bar");
static_assert(kRowY0 + (kVisible - 1) * kRowH + kRowH - 1 < kFooterY - 2, "the rows stay out of the footer band");

constexpr const char *kMenuFooter = "X=open  Tri=save";
constexpr const char *kPageFooter = "O=back  Tri=save";

// The value text of item `it` ("1.0x", "-20dB", "off"…); "" for an action.
__attribute__((noinline)) inline void format_value(const SetItem &it, const Config_body &local, uint8_t power,
                                                   char *value, size_t n) {
    value[0] = 0;
    if (it.kind == SK_ACTION) return;
    const uint8_t *p = reinterpret_cast<const uint8_t*>(&local) + it.off;
    if (it.flags & SIF_FLOAT10) {
        float f; memcpy(&f, p, sizeof(f));
        const int g = static_cast<int>(f * 10.0f + 0.5f);
        snprintf(value, n, "%d.%dx", g / 10, g % 10);
    } else if (it.flags & SIF_FLOAT) {
        float f; memcpy(&f, p, sizeof(f));
        snprintf(value, n, "%+d%s", static_cast<int>(f), it.unit ? it.unit : "");
    } else {
        const uint8_t v = it.kind == SK_POWER ? (power & it.off) != 0 : *p;
        if (it.names || it.kind == SK_BOOL || it.kind == SK_POWER) {
            const char* const* names = it.names ? it.names : kOnOff;
            const unsigned index = it.kind == SK_BOOL || it.kind == SK_POWER ? v != 0 : v % (it.hi + 1);
            snprintf(value, n, "%s", names[index]);
        } else if (v == 0 && it.fmt != SF_PLAIN) {
            snprintf(value, n, "%s", it.fmt == SF_OFF0 ? "off" : it.fmt == SF_PC0 ? "PC" : "game");
        } else {
            snprintf(value, n, "%s%u%s", it.fmt == SF_REDUCE ? "-" : "", v, it.unit ? it.unit : "");
        }
    }
}

// The size of item `it`'s field (0 for a power flag or an action).
inline uint8_t field_size(const SetItem &it) {
    if (it.kind == SK_POWER || it.kind == SK_ACTION) return 0;
    return (it.flags & (SIF_FLOAT | SIF_FLOAT10)) ? sizeof(float) : 1;
}

// Whether item `it` differs between the screen's copy and what is saved.
inline bool changed(const SetItem &it, const Config_body &local, uint8_t power,
                    const Config_body &saved, uint8_t saved_power) {
    if (it.kind == SK_ACTION) return false;
    if (it.kind == SK_POWER) return ((power ^ saved_power) & it.off) != 0;
    return memcmp(reinterpret_cast<const uint8_t*>(&local) + it.off,
                  reinterpret_cast<const uint8_t*>(&saved) + it.off, field_size(it)) != 0;
}

// One D-pad left (-1) / right (+1) on item `it`.
inline void adjust(const SetItem &it, Config_body &local, uint8_t &power, int delta) {
    // error: NOT-WORTH — Reset/Wipe have no editable value; Triangle runs the action
    if (it.kind == SK_ACTION) return;
    uint8_t *p = it.kind == SK_POWER ? &power : reinterpret_cast<uint8_t*>(&local) + it.off;
    if (it.kind == SK_BOOL || it.kind == SK_POWER) {
        *p ^= it.kind == SK_POWER ? it.off : 1;
        return;
    }
    auto step = [&](float current) {
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
        value = step(value);
        memcpy(p, &value, sizeof(value));
    } else {
        *p = static_cast<uint8_t>(step(*p));
    }
}

// The Config_body fields a save takes from the screen's copy (settings_merge):
// every item's field, by offset, whatever the order on screen. `out` holds kNumItems.
inline size_t merge_fields(settings_merge::Field *out) {
    size_t n = 0;
    // loop: NOT-WORTH — 27 Settings items, once per Triangle save
    for (const SetItem &it : kItems) {
        if (const uint8_t size = field_size(it)) out[n++] = {it.off, size};
    }
    return n;
}

// ---- Buttons ----
// D-pad (b7 low nibble, 8 = released) on change, pure N/E/S/W only; faces
// (b7 high nibble) on their rising edge: Cross 0x20, Circle 0x40, Triangle 0x80.
constexpr uint8_t kCross = 0x20, kCircle = 0x40, kTriangle = 0x80;
constexpr uint32_t kHoldUs = 2000000;

enum Action : uint8_t { kNone, kSave, kReset, kWipe };
struct Step {
    int8_t adjust;  // -1 / +1 on the selected item, else 0
    Action action;
};

struct Nav {
    bool in_group = false;
    uint8_t group = 0;  // the menu cursor, kept while a group is open
    uint8_t row = 0;    // the cursor on the group page
    uint8_t last_dpad = 8, last_face = 0;
    uint32_t tri_press_us = 0;
    bool hold_fired = false;

    int item() const { return kGroups[group].first + row; }
    int rows() const { return in_group ? kGroups[group].count : kNumGroups; }
    int sel() const { return in_group ? row : group; }
    // A 2 s Triangle hold, not a press, runs this row (the Reset page only).
    bool hold_row() const { return in_group && kItems[item()].act != ACT_NONE; }

    Step step(uint8_t dpad, uint8_t face, uint32_t now_us) {
        Step out{0, kNone};
        if (dpad != last_dpad && dpad != 8) {
            const int n = rows();
            uint8_t &cur = in_group ? row : group;
            if (dpad == 0) cur = static_cast<uint8_t>((cur - 1 + n) % n);
            else if (dpad == 4) cur = static_cast<uint8_t>((cur + 1) % n);
            else if (in_group && (dpad == 6 || dpad == 2)) out.adjust = dpad == 6 ? -1 : +1;
            else if (!in_group && dpad == 2) { in_group = true; row = 0; }
        }
        last_dpad = dpad;

        const uint8_t rise = face & ~last_face;
        if (!in_group && (rise & kCross)) { in_group = true; row = 0; }
        else if (in_group && (rise & kCircle)) in_group = false;

        // Triangle: a press saves on its release; on the Reset page a 2 s hold
        // runs the row instead and a short press does nothing.
        const bool tri_now = (face & kTriangle) != 0;
        const bool tri_prev = (last_face & kTriangle) != 0;
        if (tri_now && !tri_prev) {
            tri_press_us = now_us;
            hold_fired = false;
        }
        // error: NOT-WORTH — not a failure: whether this row takes a hold instead of a press
        const bool hold = hold_row();
        if (hold && tri_now && !hold_fired && now_us - tri_press_us >= kHoldUs) {
            hold_fired = true;
            out.action = kItems[item()].act == ACT_RESET ? kReset : kWipe;
        }
        if (!tri_now && tri_prev) {
            if (!hold && !hold_fired) out.action = kSave;
            hold_fired = false;
        }
        last_face = face;
        return out;
    }
};

// ---- Drawing ----
// How the selected row shows a changed, unsaved value: kNotch cuts a window
// out of the bar for it (lit text on dark, 2 px dark either side, then the
// bar's 2 px cap); kFlank puts "<" ">" round it inside the bar. kFlank was
// rejected (contact sheet 2026-10-17): on the longest rows ("Wake on input",
// "AutoHap gain") the arrows leave no gap after the label.
enum AdjStyle : uint8_t { kNotch, kFlank };

// x of the first glyph of an n-glyph value right-aligned to kValueEndCol.
constexpr int value_x(int n) { return kX + (kValueEndCol + 1 - n) * kGlyphW; }
constexpr int row_y(int i) { return kRowY0 + i * kRowH; }
constexpr int top_row(int sel) { return sel >= kVisible ? sel - kVisible + 1 : 0; }

struct View {
    const Config_body &local;
    uint8_t power;
    const Config_body &saved;  // what is on flash now (get_config)
    uint8_t saved_power;
    bool dirty;
    const char *status;        // "Saved!" etc. while it shows, else nullptr
};

// The title text: the menu's or the open group's name, (*) while unsaved.
inline void title_text(const Nav &nav, bool dirty, char *out, size_t n) {
    snprintf(out, n, "%s%s", nav.in_group ? kGroups[nav.group].name : "Settings", dirty ? " (*)" : "");
}

inline const char *footer_text(const Nav &nav, const char *status) {
    if (status) return status;
    if (!nav.in_group) return kMenuFooter;
    const char *hint = kItems[nav.item()].hint;
    return hint ? hint : kPageFooter;
}

// Row `idx` of the current page: its label and value ("" for none).
inline void row_text(const Nav &nav, int idx, const View &v, const char *&label, char *value, size_t n) {
    if (!nav.in_group) {
        label = kGroups[idx].name;
        snprintf(value, n, "%u", kGroups[idx].count);
        return;
    }
    const SetItem &it = kItems[kGroups[nav.group].first + idx];
    label = it.label;
    format_value(it, v.local, v.power, value, n);
}

// C: text(x, y, s), filled(x, y, w, h), invert(x, y, w, h), px(x, y, on)
// (oled.cpp's FbCanvas, or the host's BitCanvas). The caller clears first.
template <AdjStyle S = kNotch, typename C>
void draw(C &c, const Nav &nav, const View &v) {
    char buf[24];
    title_text(nav, v.dirty, buf, sizeof(buf));
    c.text(kX, kTitleY, buf);

    const int n = nav.rows(), sel = nav.sel(), top = top_row(sel);
    // loop: HOT-PATH-OK — kVisible = 5 rows, Settings screen at 10 Hz
    for (int i = 0; i < kVisible && top + i < n; i++) {
        const int idx = top + i, y = row_y(i);
        const char *label;
        char value[16];
        row_text(nav, idx, v, label, value, sizeof(value));
        const bool selected = idx == sel;
        const bool adjusting = selected && nav.in_group
            && changed(kItems[nav.item()], v.local, v.power, v.saved, v.saved_power);
        c.text(kX + kLabelCol * kGlyphW, y, label);
        int vn = static_cast<int>(strlen(value));
        if (adjusting && S == kFlank) {
            char plain[sizeof(value)];
            memcpy(plain, value, sizeof(plain));
            snprintf(value, sizeof(value), "<%s>", plain);
            vn = static_cast<int>(strlen(value));
        }
        if (vn) c.text(value_x(vn), y, value);
        if (selected) c.invert(kBarX, y - 1, kBarW, kRowH);
        if (adjusting && S == kNotch) {
            // A window in the bar: 2 px dark either side of the value, then a 2 px lit cap.
            const int x0 = value_x(vn) - 2;
            c.invert(x0, y - 1, kBarX + kBarW - kNotchCap - x0, kRowH);
        }
    }

    // Scroll bar: a dotted track and a thumb, only when the page scrolls.
    if (n > kVisible) {
        // loop: HOT-PATH-OK — 22 track dots, Settings screen at 10 Hz
        for (int y = kScrollY; y < kScrollY + kScrollH; y += 2) c.px(kScrollX + 1, y, true);
        int th = kScrollH * kVisible / n;
        if (th < 5) th = 5;
        const int ty = kScrollY + (kScrollH - th) * top / (n - kVisible);
        c.filled(kScrollX, ty, kScrollW, th);
    }

    c.text(kX, kFooterY, footer_text(nav, v.status));
}

}  // namespace settings_menu
