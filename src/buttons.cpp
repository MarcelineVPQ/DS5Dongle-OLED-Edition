// Tension buttons (protocol 1.24): the board side of running a layout. The
// layout itself (parse, validate, compile, the press-type engine, gyro math,
// the report bytes) is pure, in buttons.h; tension.cpp decides which one runs
// (docs/TENSION_BUTTONS.md section 3.7) and hands it here. This file steps the
// engine, sends the keyboard and mouse reports on HID instance 1, releases
// everything when it must (section 4.8), pauses keys and mouse (0xF6 0x21)
// and answers the live truth (GET 0xE8). Core0 only: the main loop and
// TinyUSB / BTstack callbacks on it, so no locking.

#include "buttons.h"

#include <cstring>

#include "bt.h"
#include "calib.h"
#include "cmd.h"
#include "core0_ram.h"
#include "pico/time.h"
#include "remap.h"
#include "tusb.h"
#include "usb_keys_mouse.h"
#include "usb_power.h"

extern uint8_t interrupt_in_data[63];  // main.cpp: the controller's latest input report (raw)

namespace {
const buttons::Layout *g_layout = nullptr;  // the running set's buttons, or nullptr: the legacy table
uint8_t g_run_id = 0xFF;                    // 0xE8 [4]: a slot, 0xFD the app's, kRunFeelIt
buttons::Engine g_eng;
bool g_engine_ready = false;  // g_eng has been reset once (it starts as zeros, pull_to must be 0xFF)

// No current input (no controller, no report for kStaleUs, USB suspended, the
// wake keyboard up): everything is released and the engine doesn't step, so
// a frozen report can't hold a key down.
bool g_blocked = false;
bool g_new_input = false;
uint32_t g_input_us = 0, g_prev_input_us = 0;
bool g_have_input = false;

bool g_paused = false;  // 0xF6 0x21: keys and mouse paused until g_pause_until
uint32_t g_pause_until = 0;

uint8_t g_kb_sent[8]{};      // what the host has of the keyboard (all up when the interface is down)
uint8_t g_mb_sent = 0;       // ...and of the mouse buttons
bool g_km_dirty = false;     // a keyboard or mouse report differs from what the host has

struct PadOut {  // the gamepad half of the engine's output, to see a change (real-time polling)
    uint32_t pad;
    uint8_t full, pull0, pull1;
    int8_t sx, sy;
};
PadOut g_pad_last{};

// GET 0xE8 [23..46]: what the host fetched (tud_hid_report_complete_cb), or
// with keys and mouse paused what would have gone out.
struct Fetched {
    uint8_t pad[3], l2, r2, mods, keys[6], mouse;
    int32_t x, y, wheel;  // sums since the last 0xE8 read
    uint32_t count;       // gamepad reports fetched
} g_fetched{};

void reset_engine() {
    buttons::reset(g_eng);
    g_engine_ready = true;
    g_km_dirty = true;  // the next keyboard / mouse report says all up
}

// The engine's step on one input report's buttons. Out of line once, in RAM:
// buttons::step is always_inline, so its body lives here only.
__attribute__((noinline)) void CORE0_RAM_FUNC(run_step)(const uint8_t *report) {
    buttons::step(g_eng, *g_layout, buttons::sources_down(report), time_us_32());
}

__attribute__((always_inline)) inline bool same(const PadOut &a, const PadOut &b) {
    return a.pad == b.pad && a.full == b.full && a.pull0 == b.pull0 && a.pull1 == b.pull1 && a.sx == b.sx && a.sy == b.sy;
}

__attribute__((always_inline)) inline int32_t add_sat16(int32_t a, int32_t b) { return buttons::sat16(a + b); }

// The keyboard and mouse reports to HID instance 1 (section 5.4 point 4):
// the keyboard report when it changed, else the mouse report when motion is
// pending or its buttons changed; one report per 1 ms poll, motion keeps
// adding up meanwhile. Paused: all up goes out once, and 0xE8 shows what
// would have gone out.
__attribute__((noinline)) void CORE0_RAM_FUNC(send_keys_mouse)() {
    uint8_t kb[8];
    buttons::keyboard_report(g_eng, kb);
    uint8_t mb = g_eng.mouse_buttons;
    if (g_paused) {
        g_fetched.mods = kb[0];
        memcpy(g_fetched.keys, kb + 2, 6);
        g_fetched.mouse = mb;
        g_fetched.x = add_sat16(g_fetched.x, g_eng.pend[0]);
        g_fetched.y = add_sat16(g_fetched.y, g_eng.pend[1]);
        g_fetched.wheel = add_sat16(g_fetched.wheel, g_eng.pend[2]);
        g_eng.pend[0] = g_eng.pend[1] = g_eng.pend[2] = 0;
        memset(kb, 0, sizeof(kb));
        mb = 0;
    }
    if (!usb_keys_mouse_on() || tud_suspended()) {
        // Nothing on the bus to send to (or suspended: a report could wake the
        // PC): the host has nothing held, and motion isn't saved up.
        memset(g_kb_sent, 0, sizeof(g_kb_sent));
        g_mb_sent = 0;
        g_eng.pend[0] = g_eng.pend[1] = g_eng.pend[2] = 0;
        g_km_dirty = false;
        return;
    }
    // error: NOT-WORTH — not a failure: the endpoint is busy with the last report; this one goes on a later pass (motion keeps adding up)
    if (!tud_hid_n_ready(KEYS_MOUSE_HID_INSTANCE)) return;
    if (memcmp(kb, g_kb_sent, sizeof(kb)) != 0) {
        // error: LOGGED-OK — not sent this pass (endpoint taken meanwhile); g_kb_sent is unchanged, so the next pass sends it again
        if (tud_hid_n_report(KEYS_MOUSE_HID_INSTANCE, 1, kb, sizeof(kb))) memcpy(g_kb_sent, kb, sizeof(kb));
        return;
    }
    const bool motion = g_eng.pend[0] != 0 || g_eng.pend[1] != 0 || g_eng.pend[2] != 0;
    if (!motion && mb == g_mb_sent) {
        g_km_dirty = false;
        return;
    }
    uint8_t m[7];
    int32_t took[3];
    buttons::mouse_report(g_eng, mb, m, took);
    // error: LOGGED-OK — not sent this pass (endpoint taken meanwhile); the motion stays pending and goes with the next report
    if (tud_hid_n_report(KEYS_MOUSE_HID_INSTANCE, 2, m, sizeof(m))) {
        g_mb_sent = mb;
        // loop: HOT-PATH-OK — N = 3 motion axes
        for (int i = 0; i < 3; i++) g_eng.pend[i] -= took[i];
    }
}
}  // namespace

void buttons_run(const buttons::Layout *layout, uint8_t run_id) {
    reset_engine();  // what runs changes: release everything (section 4.8)
    g_blocked = false;  // a fresh start; buttons_task looks again on its next pass
    g_layout = layout;
    g_run_id = layout ? run_id : 0xFF;
}

void CORE0_RAM_FUNC(buttons_on_input)() {
    g_prev_input_us = g_input_us;
    g_input_us = time_us_32();
    g_have_input = true;
    g_new_input = true;
}

void CORE0_RAM_FUNC(buttons_apply)(uint8_t *report) {
    // error: NOT-WORTH — not a failure: no set's buttons run, so levels 3-4 do (today's temporary or saved table, unchanged)
    if (!g_layout) return remap_apply(report);
    if (!g_engine_ready) reset_engine();
    if (!g_blocked) run_step(report);
    buttons::apply(g_eng, *g_layout, report);
}

bool CORE0_RAM_FUNC(buttons_task)() {
    const uint32_t now = time_us_32();
    if (g_paused && (int32_t) (now - g_pause_until) >= 0) g_paused = false;
    // error: NOT-WORTH — not a failure: no set's buttons run and the host holds no key, so there is nothing to do
    if (!g_layout && !g_km_dirty) return false;
    if (g_layout) {
        if (!g_engine_ready) reset_engine();
        const bool stale = !g_have_input || now - g_input_us >= buttons::kStaleUs;
        const bool block = stale || !bt_is_connected() || tud_suspended() || usb_wake_keyboard();
        if (block) {
            if (!g_blocked) reset_engine();
            g_blocked = true;
            if (stale) g_have_input = false;  // until the next report arrives
        } else {
            g_blocked = false;
            run_step(interrupt_in_data);
            if (g_new_input) {
                g_new_input = false;
                int16_t zero[3];
                // error: NOT-WORTH — false = 0x05 not here yet: gyro as mouse waits for it (section 4.6), as calib_apply's gyro step does
                if (g_eng.gyro_on && calib_gyro_zero(zero)) {
                    int16_t raw[3];
                    memcpy(raw, interrupt_in_data + 15, sizeof(raw));  // pitch, yaw, roll (calib.h)
                    const int axis = g_layout->gyro[1] == 1 ? 2 : 1;   // horizontal: yaw, or roll
                    buttons::gyro_input(g_eng, *g_layout, raw[axis] - zero[axis], raw[0] - zero[0],
                                        g_input_us - g_prev_input_us);
                }
            }
        }
        if (g_eng.mods || g_eng.mouse_buttons || g_eng.pend[0] || g_eng.pend[1] || g_eng.pend[2] || g_eng.keys[0])
            g_km_dirty = true;
    }
    send_keys_mouse();
    // error: NOT-WORTH — not a failure: no set's buttons run, so the gamepad report is remap_apply's and nothing changed here
    if (!g_layout) return false;
    const PadOut o{g_eng.pad, g_eng.full, g_eng.pull_to[0], g_eng.pull_to[1], g_eng.gstick[0], g_eng.gstick[1]};
    // error: NOT-WORTH — a comparison, not a failure: false = the gamepad output is as it was
    const bool changed = !same(o, g_pad_last);
    g_pad_last = o;
    return changed;
}

uint8_t buttons_pause(const uint8_t *a, uint16_t n) {
    // error: SHOWN — CMD_RESULT_INVALID in the 0xF1 result byte: bad framing or more than 10 s, the app's command error
    if (n < 2 || a[0] != 1 || a[1] > 10) return CMD_RESULT_INVALID;
    if (a[1] == 0) {
        g_paused = false;
        return CMD_RESULT_OK;
    }
    if (!g_paused) reset_engine();  // the pause starts: release everything (section 4.8)
    g_paused = true;
    g_pause_until = time_us_32() + a[1] * 1000000u;
    return CMD_RESULT_OK;
}

bool buttons_paused() { return g_paused; }

void CORE0_RAM_FUNC(tud_hid_report_complete_cb)(uint8_t instance, uint8_t const *report, uint16_t len) {
    // The host has taken this report (TinyUSB calls this after the IN
    // transfer): 0xE8 shows only what really left. report[0] is the report id.
    if (instance == 0) {
        // error: NOT-WORTH — not a failure: the wake keyboard's report, or not the DualSense input report
        if (usb_wake_keyboard() || len < 64 || report[0] != 0x01) return;
        g_fetched.l2 = report[1 + 4];
        g_fetched.r2 = report[1 + 5];
        memcpy(g_fetched.pad, report + 1 + 7, 3);
        g_fetched.count++;
    } else if (instance == KEYS_MOUSE_HID_INSTANCE && !g_paused) {
        if (report[0] == 1 && len >= 9) {
            g_fetched.mods = report[1];
            memcpy(g_fetched.keys, report + 3, 6);
        } else if (report[0] == 2 && len >= 8) {
            g_fetched.mouse = report[1];
            g_fetched.x = add_sat16(g_fetched.x, (int16_t) (report[2] | report[3] << 8));
            g_fetched.y = add_sat16(g_fetched.y, (int16_t) (report[4] | report[5] << 8));
            g_fetched.wheel = add_sat16(g_fetched.wheel, (int8_t) report[6]);
        }
    }
}

// GET 0xE8 (docs/TENSION_BUTTONS.md section 6.4): 63 bytes.
uint16_t buttons_live_get(uint8_t *b, uint16_t reqlen) {
    // error: NOT-WORTH — no data; a request shorter than the reply (the app always asks the full length, its transport reports a short reply)
    if (reqlen < 63) return 0;
    if (!g_engine_ready) reset_engine();
    const uint32_t now = time_us_32();
    const bool set = g_layout != nullptr;
    const bool legacy = !set && remap_active();
    memset(b, 0, 63);
    b[0] = 'B';
    b[1] = 'T';
    b[2] = 1;
    b[3] = (uint8_t) ((set || legacy ? 0x01 : 0) | (set ? 0x02 : 0) | (set && g_run_id == kRunFeelIt ? 0x04 : 0) |
                      (usb_keys_mouse_on() ? 0x08 : 0) | (set && g_eng.gyro_on && g_layout->gyro[0] == 1 ? 0x10 : 0) |
                      (g_paused ? 0x20 : 0));
    b[4] = set ? g_run_id : legacy ? 0xFE : 0xFF;
    b[5] = set ? g_eng.layer : 0;
    const uint32_t down = bt_is_connected() ? buttons::sources_down(interrupt_in_data) : 0;
    const uint32_t act = set ? g_eng.act : down;  // without a set's buttons each button's action follows it
    // loop: NOT-WORTH — 3 bytes each, per 0xE8 GET
    for (int i = 0; i < 3; i++) {
        b[6 + i] = (uint8_t) (down >> (8 * i));
        b[9 + i] = (uint8_t) (act >> (8 * i));
    }
    const buttons::Event &e = g_eng.ev;
    b[12] = set ? e.src : 0xFF;
    if (set && e.src != 0xFF) {
        b[13] = e.type;
        b[14] = e.phase;
        const uint32_t ms = ((e.down ? now : e.t1) - e.t0) / 1000;
        const uint16_t ms16 = (uint16_t) (ms > 65535 ? 65535 : ms);
        memcpy(b + 15, &ms16, 2);
        memcpy(b + 17, &e.hold_ms, 2);
        // An Edge button as itself shows as kind 1 with its own source id, 18..21.
        b[19] = e.t.kind == buttons::kEdgeSelf ? buttons::kButton : e.t.kind;
        b[20] = e.t.kind == buttons::kEdgeSelf ? (uint8_t) (buttons::kFirstEdgeSource + e.t.a) : e.t.a;
        b[21] = e.t.b;
        b[22] = e.layer;
    }
    memcpy(b + 23, g_fetched.pad, 3);
    b[26] = g_fetched.l2;
    b[27] = g_fetched.r2;
    b[28] = g_fetched.mods;
    memcpy(b + 29, g_fetched.keys, 6);
    b[35] = g_fetched.mouse;
    const int16_t mv[3] = {(int16_t) g_fetched.x, (int16_t) g_fetched.y, (int16_t) g_fetched.wheel};
    memcpy(b + 36, mv, sizeof(mv));
    g_fetched.x = g_fetched.y = g_fetched.wheel = 0;  // sums since the previous read
    memcpy(b + 42, &g_fetched.count, 4);
    b[46] = g_eng.dropped;
    return 63;
}
