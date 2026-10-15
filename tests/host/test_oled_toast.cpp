// src/oled_toast.h: the status line on every OLED screen (error pass F1).
#include <cstring>
#include "bit_canvas.h"
#include "oled_toast.h"
#include "test.h"

using namespace oled_toast;

TEST(oled_toast_shows_then_goes_once) {
    Toast t;
    CHECK(t.current(0) == nullptr);
    CHECK(!t.take_ended());
    t.show(kLightbar, 1'000'000);
    CHECK(std::strcmp(t.current(1'000'000), kLightbar) == 0);
    CHECK(t.current(1'000'000 + kShowUs - 1) != nullptr);
    CHECK(!t.take_ended());
    CHECK(t.current(1'000'000 + kShowUs) == nullptr);
    CHECK(t.take_ended());   // the screen redraws in full once
    CHECK(!t.take_ended());
    CHECK(t.current(9'000'000) == nullptr);
    CHECK(!t.take_ended());
}

TEST(oled_toast_new_message_replaces_and_restarts) {
    Toast t;
    t.show(kSlot, 0);
    t.show(kPair, kShowUs - 10);
    CHECK(std::strcmp(t.current(kShowUs + 10), kPair) == 0);
    CHECK(t.current(2 * kShowUs) == nullptr);
}

// Drawn the way oled.cpp's draw_toast does: every message fits its bar, right
// of the chrome strip, inside the panel.
TEST(oled_toast_every_message_fits) {
    for (const char *m : {kBrightness, kLightbar, kSlot, kPair}) {
        BitCanvas c;
        c.clear_rect(kBarX, kBarY, kBarW, kBarH);
        c.text(kTextX, kTextY, m);
        c.invert(kBarX, kBarY, kBarW, kBarH);
        CHECK_EQ(c.off_screen, 0);
        int x0, y0, x1, y1;
        CHECK(c.bounds(x0, y0, x1, y1));
        CHECK_EQ(x0, 6);
        CHECK_EQ(x1, 127);
        CHECK_EQ(y0, 55);
        CHECK_EQ(y1, 63);
        CHECK(kTextX + (int) std::strlen(m) * 6 - 1 <= 126);  // a lit column after the text
    }
}
