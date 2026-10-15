// A status line on every OLED screen (error pass batch F1): a save that fails
// outside the Settings / Slots screens (which have their own footer) says so
// on whatever screen is up, for kShowUs, as an inverted bar over the bottom
// row: the KEY1 long-press brightness, the Lightbar screen's mode/favorites,
// the slot a connecting controller takes or is paired into. oled.cpp draws it
// in flush_fb(); oled_notice() (oled.h) shows one and wakes the screen.
// Pure, no Pico SDK, for the host tests (tests/host/test_oled_toast.cpp).
#ifndef DS5_BRIDGE_OLED_TOAST_H
#define DS5_BRIDGE_OLED_TOAST_H

#include <stdint.h>

namespace oled_toast {

constexpr uint64_t kShowUs = 3000000;  // long enough to read twice
constexpr int kMaxChars = 20;          // x 7..126 inside the bar (6..127)
constexpr int kBarX = 6, kBarY = 55, kBarW = 122, kBarH = 9;  // the bottom row, right of the chrome strip
constexpr int kTextX = 7, kTextY = 56;

// The messages (kept short, the screen's "Save FAIL" style plus what failed).
constexpr char kBrightness[] = "Bright: Save FAIL";
constexpr char kLightbar[] = "Lightbar: Save FAIL";
constexpr char kSlot[] = "Slot: Save FAIL";
constexpr char kPair[] = "Pair: Save FAIL";
static_assert(sizeof(kBrightness) - 1 <= kMaxChars, "toast too wide");
static_assert(sizeof(kLightbar) - 1 <= kMaxChars, "toast too wide");
static_assert(sizeof(kSlot) - 1 <= kMaxChars, "toast too wide");
static_assert(sizeof(kPair) - 1 <= kMaxChars, "toast too wide");

struct Toast {
    const char *msg = nullptr;
    uint64_t until = 0;
    bool ended = false;  // it went away: a screen that redraws in parts must redraw in full

    // A new message replaces the one showing (and restarts the time).
    void show(const char *m, uint64_t now) {
        msg = m;
        until = now + kShowUs;
    }
    // The message to draw now, or nullptr.
    const char *current(uint64_t now) {
        if (msg && now >= until) {
            msg = nullptr;
            ended = true;
        }
        return msg;
    }
    // True once after the message went away.
    bool take_ended() {
        const bool e = ended;
        ended = false;
        return e;
    }
};

}  // namespace oled_toast

#endif  // DS5_BRIDGE_OLED_TOAST_H
