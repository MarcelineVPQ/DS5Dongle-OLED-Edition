// The OLED Status screen without a controller (oled.cpp render_screen): what
// to do next. Inside a pairing window (src/scan_policy.h) it says how to pair
// and counts the window down; outside one, a paired controller connects with
// PS, and KEY0 held 2 s opens a new window ("pair now": the board is off USB
// without a controller, so the app can't). Sony's words (the app's
// docs/GLOSSARY.md): "PS button", "Create button", "pair"; "KEY0" is the
// board's own label for its top key (the '>' in the chrome strip).
//
// The countdown is whole seconds, so the frame changes at most once a second
// and the flush (which skips unchanged frames) sends at most one a second.
// Pure, no Pico SDK: the host tests and the PC renderer draw this exact code.
#ifndef DS5_BRIDGE_PAIR_PROMPT_H
#define DS5_BRIDGE_PAIR_PROMPT_H

#include <stdint.h>

namespace pair_prompt {

// 20 characters fit after kContentX (6 + 20 * 6 - 1 = 125 < 128).
constexpr int kMaxChars = 20;
constexpr int kX = 6;  // oled.cpp kContentX

constexpr char kConnect[] = "Press PS to connect";     // a controller is paired in some slot
constexpr char kNonePaired[] = "No DualSense paired";  // every slot is empty
constexpr char kHint1[] = "Hold KEY0 for 2 s";
constexpr char kHint2[] = "to pair a DualSense";
constexpr char kPairing1[] = "Pairing: hold";
constexpr char kPairing2[] = "Create + PS";
static_assert(sizeof(kConnect) - 1 <= kMaxChars, "Status line too wide");
static_assert(sizeof(kNonePaired) - 1 <= kMaxChars, "Status line too wide");
static_assert(sizeof(kHint1) - 1 <= kMaxChars, "Status line too wide");
static_assert(sizeof(kHint2) - 1 <= kMaxChars, "Status line too wide");
static_assert(sizeof(kPairing1) - 1 <= kMaxChars, "Status line too wide");
static_assert(sizeof(kPairing2) - 1 <= kMaxChars, "Status line too wide");

// Rows (the title is y 0..7).
constexpr int kLineY = 14;    // Press PS / No DualSense paired
constexpr int kHint1Y = 32;
constexpr int kHint2Y = 42;
constexpr int kPair1Y = 12;
constexpr int kPair2Y = 22;
constexpr int kTimeY = 34;    // "1:59 left"
constexpr int kBarY = 46;     // outline 46..55, fill 48..53
constexpr int kBarH = 10;
constexpr int kBarW = 122;    // x 6..127, the content area's full width
constexpr int kBarInner = kBarW - 4;  // the fill, 2 px inside the outline on each side
constexpr uint32_t kFullSecs = 120;   // ScanPolicy::kPairWindowUs

// "m:ss left" for a window with secs left (at most 9:59 shown).
inline void time_text(uint32_t secs, char out[12]) {
    if (secs > 599) secs = 599;
    out[0] = (char) ('0' + secs / 60);
    out[1] = ':';
    out[2] = (char) ('0' + (secs % 60) / 10);
    out[3] = (char) ('0' + secs % 10);
    const char tail[] = " left";
    // loop: NOT-WORTH — 6 bytes (" left" + NUL), once per Status frame without a controller
    for (int i = 0; i < 6; i++) out[4 + i] = tail[i];
}

// The bar's filled width: the share of the 2-minute window left, rounded up so
// it only empties at 0.
inline int bar_fill(uint32_t secs) {
    if (secs >= kFullSecs) return kBarInner;
    return (int) ((secs * (uint32_t) kBarInner + kFullSecs - 1) / kFullSecs);
}

// Draws the screen's body below the title. C: text(x, y, s), outline(x, y, w, h),
// filled(x, y, w, h) (oled.cpp's FbCanvas, or the host renderer's).
template <typename C>
void draw(C &c, bool any_paired, uint32_t secs_left) {
    if (secs_left > 0) {
        c.text(kX, kPair1Y, kPairing1);
        c.text(kX, kPair2Y, kPairing2);
        char t[12];
        time_text(secs_left, t);
        c.text(kX, kTimeY, t);
        c.outline(kX, kBarY, kBarW, kBarH);
        const int fill = bar_fill(secs_left);
        if (fill > 0) c.filled(kX + 2, kBarY + 2, fill, kBarH - 4);
        return;
    }
    c.text(kX, kLineY, any_paired ? kConnect : kNonePaired);
    c.text(kX, kHint1Y, kHint1);
    c.text(kX, kHint2Y, kHint2);
}

}  // namespace pair_prompt

#endif  // DS5_BRIDGE_PAIR_PROMPT_H
