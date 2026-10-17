// The "This one" identify screen (protocol 1.20, 0xF6 0x1A): with several
// boards on one PC the app asks the one it is talking to to show itself. The
// whole panel flashes (inverted every kFlashUs) for the asked time with the
// board's name and the pairing slot in use, at full brightness whatever the
// dim / off timers say; the board's LED blinks in step, so a board without the
// OLED add-on shows itself too. When it ends the screen goes back to what it
// was (oled.cpp lets the power ladder and the current screen take over again)
// and the LED to its usual state.
//
// Pure, no Pico SDK: the host tests and the PC renderer draw this exact code.
#ifndef DS5_BRIDGE_IDENTIFY_SCREEN_H
#define DS5_BRIDGE_IDENTIFY_SCREEN_H

#include <stdint.h>

#include "oled_font.h"

namespace identify {

constexpr uint8_t kDefaultTenths = 30;  // no duration byte: 3 s
constexpr uint8_t kMaxTenths = 100;     // 10 s at most
constexpr uint64_t kTenthUs = 100000;
constexpr uint64_t kFlashUs = 400000;   // inverted / normal halves, 1.25 flashes a second

constexpr int kW = 128, kH = 64;
constexpr char kTitle[] = "THIS ONE";
constexpr int kTitleScale = 2;
constexpr int kTitleY = 10;          // 14 px tall (7 x 2)
constexpr int kNameY = 32;           // the board name, 1x
constexpr int kSlotY = 44;           // "Slot 0"
constexpr int kNameMax = 16;         // DS5_BOARD_NAME's limit (cmd.cpp)
static_assert(sizeof(kTitle) - 1 <= 10, "title too wide for 2x");

// The running identify: start / stop / what to show now.
struct State {
    uint64_t start_us = 0;
    uint64_t until_us = 0;
    bool running = false;
    bool ended = false;  // went away: redraw the screen underneath and put the LED back

    // tenths 0 stops; a new one replaces the running one (and restarts the time).
    void start(uint64_t now, uint8_t tenths) {
        if (tenths == 0) {
            if (running) ended = true;
            running = false;
            return;
        }
        if (tenths > kMaxTenths) tenths = kMaxTenths;
        start_us = now;
        until_us = now + tenths * kTenthUs;
        running = true;
    }
    bool active(uint64_t now) {
        if (running && now >= until_us) {
            running = false;
            ended = true;
        }
        return running;
    }
    // The flash: the inverted half (and the LED's on half).
    bool inverted(uint64_t now) const { return ((now - start_us) / kFlashUs) % 2 == 0; }
    bool take_ended() {
        const bool e = ended;
        ended = false;
        return e;
    }
};

inline int text_width(const char *s, int scale) {
    int n = 0;
    // loop: NOT-WORTH — at most 16 characters, once per identify frame
    while (s[n] && n < kNameMax) n++;
    return n == 0 ? 0 : (n * 6 - 1) * scale;
}

// One glyph row of s at scale (filled squares per font pixel), left at x.
// C: filled(x, y, w, h) (oled.cpp's FbCanvas, or the host renderer's).
template <typename C>
void text_scaled(C &c, int x, int y, const char *s, int scale) {
    // loop: NOT-WORTH — at most 16 characters, once per identify frame (10 Hz while it runs)
    for (int i = 0; s[i] && i < kNameMax; i++, x += 6 * scale) {
        const char ch = s[i];
        if (ch < 0x20 || ch > 0x7E) continue;
        const uint8_t *g = kFont5x7[ch - 0x20];
        // loop: HOT-PATH-OK — 5 x 7 font pixels per glyph, 10 Hz while it runs
        for (int col = 0; col < kFontW; col++)
            // loop: HOT-PATH-OK — 7 rows per glyph column
            for (int row = 0; row < kFontH; row++)
                if (g[col] & (1 << row)) c.filled(x + col * scale, y + row * scale, scale, scale);
    }
}

inline int centered_x(const char *s, int scale) { return (kW - text_width(s, scale) + 1) / 2; }

// The slot line, "Slot 0".."Slot 9" (zero-based like the OLED's Slots screen).
inline void slot_text(uint8_t slot, char out[8]) {
    const char head[] = "Slot ";
    // loop: NOT-WORTH — 5 bytes
    for (int i = 0; i < 5; i++) out[i] = head[i];
    out[5] = (char) ('0' + (slot % 10));
    out[6] = 0;
}

// The whole frame (no chrome strip: nothing to navigate while it shows).
// C: filled, outline, text, invert (oled.cpp's FbCanvas or BitCanvas). The
// caller clears the frame first.
template <typename C>
void draw(C &c, const char *board_name, uint8_t slot, bool inverted) {
    c.outline(0, 0, kW, kH);
    c.outline(2, 2, kW - 4, kH - 4);
    text_scaled(c, centered_x(kTitle, kTitleScale), kTitleY, kTitle, kTitleScale);
    if (board_name && board_name[0]) text_scaled(c, centered_x(board_name, 1), kNameY, board_name, 1);
    char st[8];
    slot_text(slot, st);
    text_scaled(c, centered_x(st, 1), kSlotY, st, 1);
    if (inverted) c.invert(0, 0, kW, kH);
}

}  // namespace identify

#endif  // DS5_BRIDGE_IDENTIFY_SCREEN_H
