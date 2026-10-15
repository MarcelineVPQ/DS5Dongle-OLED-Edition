// src/pair_prompt.h: the OLED Status screen without a controller. Every frame
// stays inside the content area (right of the chrome strip, below the title,
// on the 128x64 panel), the countdown bar's margins are even, and over a whole
// 2-minute pairing window the frame changes once a second at most, exactly
// when the shown time does (so the flush, which skips unchanged frames, sends
// at most one frame a second).
#include <cstring>
#include "bit_canvas.h"
#include "pair_prompt.h"
#include "scan_policy.h"
#include "test.h"

namespace {
BitCanvas frame(bool any_paired, uint32_t secs) {
    BitCanvas c;
    pair_prompt::draw(c, any_paired, secs);
    return c;
}

void check_fits(const BitCanvas &c) {
    CHECK_EQ(c.off_screen, 0);
    int x0, y0, x1, y1;
    CHECK(c.bounds(x0, y0, x1, y1));
    CHECK(x0 >= 6);     // right of the chrome strip ('>' / '<' at x 0..4)
    CHECK(x1 <= 127);
    CHECK(y0 >= 9);     // below the title + link icon (y 0..7)
    CHECK(y1 <= 63);
}
}  // namespace

TEST(pair_prompt_every_frame_fits) {
    for (const bool paired : {false, true}) {
        // loop: NOT-WORTH — host test, every second of a window
        for (uint32_t s = 0; s <= 120; s++) check_fits(frame(paired, s));
    }
    check_fits(frame(true, 599));
}

TEST(pair_prompt_texts) {
    char t[12];
    pair_prompt::time_text(120, t);
    CHECK(std::strcmp(t, "2:00 left") == 0);
    pair_prompt::time_text(61, t);
    CHECK(std::strcmp(t, "1:01 left") == 0);
    pair_prompt::time_text(9, t);
    CHECK(std::strcmp(t, "0:09 left") == 0);
    // Outside a window the two cases differ only in the first line.
    const BitCanvas a = frame(true, 0), b = frame(false, 0);
    CHECK(!a.same(b));
    bool hint_same = true;
    // loop: NOT-WORTH — host test, the hint rows
    for (int y = pair_prompt::kHint1Y; y < 64; y++)
        if (std::memcmp(a.p[y], b.p[y], sizeof(a.p[y])) != 0) hint_same = false;
    CHECK(hint_same);
}

TEST(pair_prompt_bar_margins_even) {
    const BitCanvas c = frame(true, 120);
    const int y = pair_prompt::kBarY + pair_prompt::kBarH / 2;
    int first = -1, last = -1;
    // loop: NOT-WORTH — host test, one row
    for (int x = 0; x < 128; x++)
        if (c.p[y][x]) {
            if (first < 0) first = x;
            last = x;
        }
    CHECK_EQ(first, 6);    // outline
    CHECK_EQ(last, 127);   // outline
    CHECK(!c.p[y][7]);     // 1 px gap, then the fill, then 1 px gap: even
    CHECK(c.p[y][8]);
    CHECK(c.p[y][125]);
    CHECK(!c.p[y][126]);
    CHECK_EQ(pair_prompt::bar_fill(120), pair_prompt::kBarInner);
    CHECK_EQ(pair_prompt::bar_fill(1), 1);
    CHECK_EQ(pair_prompt::bar_fill(0), 0);
}

TEST(pair_prompt_redraws_once_a_second_at_most) {
    ScanPolicy p;
    p.open(0);
    BitCanvas last = frame(true, p.prompt_seconds_left(0));
    uint32_t last_secs = p.prompt_seconds_left(0);
    int changes = 0;
    // loop: NOT-WORTH — host test, every OLED frame (100 ms) of the window and a bit after
    for (uint64_t t = 100'000; t <= 125'000'000; t += 100'000) {
        const uint32_t secs = p.prompt_seconds_left(t);
        const BitCanvas f = frame(true, secs);
        const bool changed = !f.same(last);
        CHECK_EQ(changed, secs != last_secs);  // a new frame exactly when the time shown changes
        if (changed) changes++;
        last = f;
        last_secs = secs;
    }
    CHECK_EQ(changes, 120);  // 2:00 -> 0:01 is 119, then the "Press PS" text
}
