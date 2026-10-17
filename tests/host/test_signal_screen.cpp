// src/signal_screen.h: the OLED BT Signal screen. The words and thresholds are
// the app's (signalQuality: >= -5 Excellent, >= -15 Good, >= -25 Fair, else
// Weak), the bar maps -30..0 dB to empty..full, no reading shows "--" and
// "unknown" with an empty bar, and every frame stays inside the content area.
#include <cstring>
#include "bit_canvas.h"
#include "signal_screen.h"
#include "test.h"

namespace {
BitCanvas frame(bool valid, int8_t db) {
    BitCanvas c;
    signal_screen::draw(c, valid, db);
    return c;
}

void check_fits(const BitCanvas &c) {
    CHECK_EQ(c.off_screen, 0);
    int x0, y0, x1, y1;
    CHECK(c.bounds(x0, y0, x1, y1));
    CHECK(x0 >= 6);     // right of the chrome strip ('>' / '<' at x 0..4)
    CHECK(x1 <= 127);
    CHECK(y0 >= 9);     // below the title (y 0..7)
    CHECK(y1 <= 63);
}

// The lit pixels of one row of the bar's fill, x 8..125.
int fill_px(const BitCanvas &c) {
    const int y = signal_screen::kBarY + signal_screen::kBarH / 2;
    int n = 0;
    // loop: NOT-WORTH — host test, one row
    for (int x = 8; x <= 125; x++) n += c.p[y][x] ? 1 : 0;
    return n;
}
}  // namespace

TEST(signal_screen_labels_match_the_app) {
    using signal_screen::label;
    CHECK(std::strcmp(label(5), "Excellent") == 0);
    CHECK(std::strcmp(label(0), "Excellent") == 0);
    CHECK(std::strcmp(label(-5), "Excellent") == 0);
    CHECK(std::strcmp(label(-6), "Good") == 0);
    CHECK(std::strcmp(label(-15), "Good") == 0);
    CHECK(std::strcmp(label(-16), "Fair") == 0);
    CHECK(std::strcmp(label(-25), "Fair") == 0);
    CHECK(std::strcmp(label(-26), "Weak") == 0);
    CHECK(std::strcmp(label(-128), "Weak") == 0);
}

TEST(signal_screen_bar_maps_minus_30_to_0) {
    using signal_screen::bar_fill;
    using signal_screen::kBarInner;
    CHECK_EQ(bar_fill(0), kBarInner);
    CHECK_EQ(bar_fill(5), kBarInner);     // above the range: full
    CHECK_EQ(bar_fill(127), kBarInner);
    CHECK_EQ(bar_fill(-15), kBarInner / 2);
    CHECK_EQ(bar_fill(-30), 0);
    CHECK_EQ(bar_fill(-31), 0);
    CHECK_EQ(bar_fill(-128), 0);
    CHECK(bar_fill(-29) > 0);             // a link at -29 still shows something
    // loop: NOT-WORTH — host test, every value
    for (int v = -30; v < 0; v++) CHECK(bar_fill((int8_t) v) <= bar_fill((int8_t) (v + 1)));
}

TEST(signal_screen_texts) {
    char b[24];
    signal_screen::value_text(true, 0, b);
    CHECK(std::strcmp(b, "Signal: 0 dB") == 0);
    signal_screen::value_text(true, -18, b);
    CHECK(std::strcmp(b, "Signal: -18 dB") == 0);
    signal_screen::value_text(false, 0, b);
    CHECK(std::strcmp(b, "Signal: --") == 0);
    signal_screen::link_text(true, -8, b);
    CHECK(std::strcmp(b, "Link: Good") == 0);
    signal_screen::link_text(false, 0, b);
    CHECK(std::strcmp(b, "Link: unknown") == 0);
    // The longest line fits the content area (20 characters after x = 6).
    signal_screen::value_text(true, -128, b);
    CHECK(std::strlen(b) <= (size_t) signal_screen::kMaxChars);
    signal_screen::link_text(true, 0, b);
    CHECK(std::strlen(b) <= (size_t) signal_screen::kMaxChars);
}

TEST(signal_screen_every_frame_fits) {
    // loop: NOT-WORTH — host test, every value
    for (int v = -128; v <= 127; v++) check_fits(frame(true, (int8_t) v));
    check_fits(frame(false, 0));
}

TEST(signal_screen_in_range_only_at_zero) {
    const BitCanvas zero = frame(true, 0), plus = frame(true, 5), none = frame(false, 0);
    auto row_lit = [](const BitCanvas &c, int y0) {
        // loop: NOT-WORTH — host test, one text row
        for (int y = y0; y < y0 + 7; y++)
            // loop: NOT-WORTH — host test, one row
            for (int x = 0; x < 128; x++)
                if (c.p[y][x]) return true;
        return false;
    };
    CHECK(row_lit(zero, signal_screen::kRangeY));
    CHECK(!row_lit(plus, signal_screen::kRangeY));
    CHECK(!row_lit(none, signal_screen::kRangeY));
    CHECK(!row_lit(frame(true, -8), signal_screen::kRangeY));
}

TEST(signal_screen_no_reading_has_an_empty_bar) {
    CHECK_EQ(fill_px(frame(false, 0)), 0);
    CHECK_EQ(fill_px(frame(true, 0)), signal_screen::kBarInner);
    CHECK_EQ(fill_px(frame(true, 5)), signal_screen::kBarInner);
    CHECK_EQ(fill_px(frame(true, -28)), signal_screen::bar_fill(-28));
}

TEST(signal_screen_bar_margins_even) {
    // A full bar: outline, 1 px gap, fill, 1 px gap, outline, on both sides.
    const BitCanvas c = frame(true, 0);
    const int y = signal_screen::kBarY + signal_screen::kBarH / 2;
    CHECK(c.p[y][6]);
    CHECK(!c.p[y][7]);
    CHECK(c.p[y][8]);
    CHECK(c.p[y][125]);
    CHECK(!c.p[y][126]);
    CHECK(c.p[y][127]);
    // Top and bottom the same: fill rows 36..41 inside outline rows 34 and 43.
    CHECK(c.p[signal_screen::kBarY][20]);
    CHECK(!c.p[signal_screen::kBarY + 1][20]);
    CHECK(c.p[signal_screen::kBarY + 2][20]);
    CHECK(c.p[signal_screen::kBarY + signal_screen::kBarH - 3][20]);
    CHECK(!c.p[signal_screen::kBarY + signal_screen::kBarH - 2][20]);
    CHECK(c.p[signal_screen::kBarY + signal_screen::kBarH - 1][20]);
}
