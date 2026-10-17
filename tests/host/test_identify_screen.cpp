// src/identify_screen.h: the "THIS ONE" identify screen (protocol 1.20).
// Every frame stays on the panel, the text is centred (left and right margins
// within a pixel), the flash alternates, the timer starts, stops, clamps and
// ends once, and the board name / slot change the frame.
#include <cstring>
#include "bit_canvas.h"
#include "identify_screen.h"
#include "test.h"

namespace {
BitCanvas frame(const char *name, uint8_t slot, bool inverted) {
    BitCanvas c;
    identify::draw(c, name, slot, inverted);
    return c;
}

// Lit columns of the rows y0..y1 (the border's 4 px excluded).
void row_bounds(const BitCanvas &c, int y0, int y1, int &x0, int &x1) {
    x0 = 128; x1 = -1;
    // loop: NOT-WORTH — host test
    for (int y = y0; y <= y1; y++)
        // loop: NOT-WORTH — host test
        for (int x = 4; x < 124; x++)
            if (c.p[y][x]) { if (x < x0) x0 = x; if (x > x1) x1 = x; }
}

void check_centered(const BitCanvas &c, int y, int h) {
    int x0, x1;
    row_bounds(c, y, y + h - 1, x0, x1);
    CHECK(x1 >= x0);
    const int left = x0, right = 127 - x1;
    CHECK(left - right <= 1 && right - left <= 1);
}
}  // namespace

TEST(identify_frames_fit_and_center) {
    const char *names[] = {"Pico 2 W", "Pico W", "", "sixteen_chars_16"};
    // loop: NOT-WORTH — host test
    for (const char *n : names)
        // loop: NOT-WORTH — host test
        for (uint8_t slot = 0; slot < 4; slot++) {
            const BitCanvas c = frame(n, slot, false);
            CHECK_EQ(c.off_screen, 0);
            check_centered(c, identify::kTitleY, 7 * identify::kTitleScale);
            check_centered(c, identify::kSlotY, 7);
            if (n[0]) check_centered(c, identify::kNameY, 7);
            // Inverted = every pixel flipped.
            const BitCanvas inv = frame(n, slot, true);
            CHECK_EQ(inv.off_screen, 0);
            bool flipped = true;
            // loop: NOT-WORTH — host test
            for (int y = 0; y < 64; y++)
                // loop: NOT-WORTH — host test
                for (int x = 0; x < 128; x++) flipped &= inv.p[y][x] != c.p[y][x];
            CHECK(flipped);
        }
}

TEST(identify_frames_differ_by_slot_and_name) {
    CHECK(!frame("Pico 2 W", 0, false).same(frame("Pico 2 W", 1, false)));
    CHECK(!frame("Pico 2 W", 0, false).same(frame("Pico W", 0, false)));
    char t[8];
    identify::slot_text(3, t);
    CHECK(std::strcmp(t, "Slot 3") == 0);
}

TEST(identify_timer) {
    identify::State s;
    CHECK(!s.active(0));
    CHECK(!s.take_ended());
    s.start(1000, 30);
    CHECK(s.active(1000));
    CHECK(s.inverted(1000));                         // starts inverted: seen at once
    CHECK(!s.inverted(1000 + identify::kFlashUs));   // then normal
    CHECK(s.inverted(1000 + 2 * identify::kFlashUs));
    CHECK(s.active(1000 + 2999999));
    CHECK(!s.active(1000 + 3000000));
    CHECK(s.take_ended());
    CHECK(!s.take_ended());                          // once
    // Clamped to 10 s; a new one replaces the running one.
    s.start(0, 250);
    CHECK(s.active(9999999));
    CHECK(!s.active(10000000));
    CHECK(s.take_ended());
    s.start(0, 10);
    s.start(500000, 10);
    CHECK(s.active(1400000));
    // 0 stops a running one (and it ends once); 0 with nothing running is a no-op.
    s.start(1400000, 0);
    CHECK(!s.active(1400000));
    CHECK(s.take_ended());
    s.start(0, 0);
    CHECK(!s.take_ended());
}
