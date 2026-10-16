// KEY0 / KEY1 on the OLED board: the press logic (debounce, short / long
// press, the KEY0+KEY1 reboot chord) and the app's remote presses (0xF6 0x18,
// protocol 1.18). Pure, no SDK, so the host tests run it (tests/host/test_oled_keys.cpp).
//
// A remote press is not a shortcut to "next screen": the injector holds a
// virtual key down for a while and lets go, and handle_buttons() feeds that
// level through the same step() as the GPIO pins (a key counts as down when
// the pin or the injector holds it). So a remote press debounces, wakes the
// screen, tells short from long and navigates exactly like a finger on the
// board, with or without an OLED wired (no OLED = the pins read released).
#ifndef DS5_BRIDGE_OLED_KEYS_H
#define DS5_BRIDGE_OLED_KEYS_H

#include <stdint.h>

namespace oledkeys {

constexpr uint32_t kDebounceUs = 20000;
constexpr uint32_t kLongPressUs = 1500000;  // KEY1 held longer = brightness cycle
constexpr uint32_t kChordHoldUs = 1000000;  // KEY0 + KEY1 held this long = reboot
// KEY0 held this long where the screen has a hold action (only the Status
// screen with no controller: "pair now", src/pair_prompt.h) fires while still
// held, and its release then does nothing. Longer than the app's remote long
// press (kInjectLongHoldUs, 1.6 s), so a remote KEY0 press always navigates;
// elsewhere a KEY0 hold of any length is still "next screen" on release.
constexpr uint32_t kHoldUs = 2000000;

// What one step() saw; handle_buttons() carries them out.
enum Event : uint8_t {
    kActivity = 0x01,    // a key went down or was released after a press: wake the screen
    kNext = 0x02,        // KEY0 released: next screen
    kBack = 0x04,        // KEY1 short press released: previous screen
    kBrightness = 0x08,  // KEY1 held past the long-press threshold: next brightness level
    kReboot = 0x10,      // KEY0 + KEY1 held kChordHoldUs
    kHold0 = 0x20,       // KEY0 held kHoldUs where the screen allows it (k0_hold)
};

struct Buttons {
    bool k0_prev_down = false;
    bool k1_prev_down = false;
    uint32_t k0_t_us = 0;        // last accepted KEY0 press (debounce)
    uint32_t k1_t_us = 0;        // last accepted KEY1 press (debounce)
    bool k0_armed = false;       // KEY0 pressed, fires on release
    bool k1_armed = false;       // KEY1 pressed, pending short release or long hold
    bool k1_long_fired = false;  // brightness already changed during this press
    uint32_t k1_press_us = 0;    // when KEY1 went down (short vs long)
    uint32_t chord_since_us = 0; // 0 = no chord held
};

// One pass of the key logic. k0_down / k1_down = the key is held now;
// k0_hold = the screen up now has a KEY0 hold action (false: as before).
// Short presses fire on release; KEY1's long press fires once while held.
// The chord cancels both armed presses so releasing either doesn't navigate.
inline uint8_t step(Buttons &b, bool k0_down, bool k1_down, uint32_t now, bool k0_hold = false) {
    uint8_t ev = 0;
    if (k0_down && k1_down) {
        if (b.chord_since_us == 0) b.chord_since_us = now;
        b.k0_armed = false;
        b.k1_armed = false;
        if ((now - b.chord_since_us) >= kChordHoldUs) ev |= kReboot;
    } else {
        b.chord_since_us = 0;
    }

    if (k0_down && !b.k0_prev_down && (now - b.k0_t_us) > kDebounceUs) {
        b.k0_t_us = now;
        b.k0_armed = true;
        ev |= kActivity;
    }
    if (k0_down && b.k0_prev_down && b.k0_armed && k0_hold && (now - b.k0_t_us) >= kHoldUs) {
        b.k0_armed = false;  // the hold acted: its release doesn't navigate
        ev |= kHold0 | kActivity;
    }
    if (!k0_down && b.k0_prev_down && b.k0_armed) {
        b.k0_armed = false;
        ev |= kNext | kActivity;
    }

    if (k1_down && !b.k1_prev_down && (now - b.k1_t_us) > kDebounceUs) {
        b.k1_t_us = now;
        b.k1_press_us = now;
        b.k1_armed = true;
        b.k1_long_fired = false;
        ev |= kActivity;
    }
    if (b.k1_armed && !b.k1_long_fired && (now - b.k1_press_us) > kLongPressUs) {
        b.k1_long_fired = true;
        ev |= kActivity | kBrightness;
    }
    if (!k1_down && b.k1_prev_down && b.k1_armed) {
        b.k1_armed = false;
        ev |= kActivity | (b.k1_long_fired ? 0 : kBack);
    }

    b.k0_prev_down = k0_down;
    b.k1_prev_down = k1_down;
    return ev;
}

// Remote presses: how long the virtual key is held, and how long it stays up
// before the next queued press (longer than the debounce, so that press counts).
constexpr uint32_t kInjectShortHoldUs = 60000;
constexpr uint32_t kInjectLongHoldUs = kLongPressUs + 100000;
constexpr uint32_t kInjectGapUs = 60000;
static_assert(kInjectLongHoldUs < kHoldUs, "a remote long KEY0 press must not be a hold");
constexpr uint8_t kInjectQueue = 4;

struct Injector {
    uint8_t q[kInjectQueue] = {};  // bit0 key (0 KEY0, 1 KEY1), bit1 long press
    uint8_t head = 0;
    uint8_t count = 0;
    enum Phase : uint8_t { Idle, Holding, Gap } phase = Idle;
    uint8_t cur = 0;
    uint32_t t0_us = 0;
};

// Queues a press; false when kInjectQueue presses are already waiting.
inline bool push(Injector &in, uint8_t key, bool long_press) {
    // error: SHOWN — CMD_RESULT_BUSY in the 0xF1 result byte (cmd.cpp 0x18): four presses already waiting
    if (in.count >= kInjectQueue) return false;
    in.q[(uint8_t) ((in.head + in.count) % kInjectQueue)] = (uint8_t) ((key & 1) | (long_press ? 2 : 0));
    in.count++;
    return true;
}

inline bool busy(const Injector &in) { return in.count != 0 || in.phase != Injector::Idle; }

// The virtual keys' levels now; call once per handle_buttons() pass.
inline void levels(Injector &in, uint32_t now, bool &k0_down, bool &k1_down) {
    k0_down = false;
    k1_down = false;
    if (in.phase == Injector::Gap && (now - in.t0_us) >= kInjectGapUs) in.phase = Injector::Idle;
    if (in.phase == Injector::Idle && in.count != 0) {
        in.cur = in.q[in.head];
        in.head = (uint8_t) ((in.head + 1) % kInjectQueue);
        in.count--;
        in.phase = Injector::Holding;
        in.t0_us = now;
    }
    if (in.phase == Injector::Holding) {
        const uint32_t hold = (in.cur & 2) ? kInjectLongHoldUs : kInjectShortHoldUs;
        if ((now - in.t0_us) >= hold) {
            in.phase = Injector::Gap;  // released on this pass
            in.t0_us = now;
            return;
        }
        if (in.cur & 1) k1_down = true;
        else k0_down = true;
    }
}

}  // namespace oledkeys

#endif  // DS5_BRIDGE_OLED_KEYS_H
