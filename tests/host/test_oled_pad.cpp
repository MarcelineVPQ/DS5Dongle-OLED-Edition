// The Status screen's controller glyph (src/oled_pad.h) must stay mirror-
// symmetric around the screen's vertical center line (x <-> 127 - x), inside
// its area, and light every button it shows (Create/Options included; on an
// Edge also Fn L/R and the back buttons). A standard DualSense must draw
// exactly what it drew before the Edge marks (golden frame hashes), and the
// battery row must span the stick boxes' inner edges, mirrored.
#include <cstring>
#include <iterator>
#include <random>
#include <vector>
#include "oled_pad.h"
#include "test.h"

namespace {
constexpr int W = 128, H = 64;

struct Frame {
    bool p[H][W] = {};
    void px(int x, int y, bool on) {
        CHECK(x >= 0 && x < W && y >= 0 && y < H);
        if (x >= 0 && x < W && y >= 0 && y < H) p[y][x] = on;
    }
    void outline(int x, int y, int w, int h) {
        for (int i = 0; i < w; i++) { px(x + i, y, true); px(x + i, y + h - 1, true); }
        for (int i = 0; i < h; i++) { px(x, y + i, true); px(x + w - 1, y + i, true); }
    }
    void filled(int x, int y, int w, int h) {
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) px(x + i, y + j, true);
    }
    void invert(int x, int y, int w, int h) {
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) px(x + i, y + j, !p[y + j][x + i]);
    }
    void clear(int x, int y, int w, int h) {
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) px(x + i, y + j, false);
    }
};

struct Report {
    uint8_t d[63];
    Report() {
        std::memset(d, 0, sizeof(d));
        d[0] = d[1] = d[2] = d[3] = 128;
        d[7] = 0x08;  // d-pad released
    }
};

Frame render(const Report &r, bool edge = false) {
    Frame f;
    oled_pad::draw(f, r.d, edge);
    return f;
}

// The same controller state seen in a mirror: left and right parts swap,
// horizontal stick axes flip, the d-pad's E/W and Square/Circle swap.
Report mirrored(const Report &r) {
    Report m = r;
    m.d[0] = (uint8_t) (255 - r.d[2]);
    m.d[1] = r.d[3];
    m.d[2] = (uint8_t) (255 - r.d[0]);
    m.d[3] = r.d[1];
    m.d[4] = r.d[5];
    m.d[5] = r.d[4];
    const int hat = r.d[7] & 0x0F;
    uint8_t face = r.d[7] & 0xA0;            // Triangle, Cross stay
    if (r.d[7] & 0x10) face |= 0x40;         // Square <-> Circle
    if (r.d[7] & 0x40) face |= 0x10;
    m.d[7] = (uint8_t) (face | (hat < 8 ? (8 - hat) % 8 : hat));
    uint8_t b8 = 0;
    for (int bit = 0; bit < 8; bit += 2) {   // L1/R1, L2/R2, Create/Options, L3/R3
        if (r.d[8] & (1 << bit)) b8 |= (uint8_t) (2 << bit);
        if (r.d[8] & (2 << bit)) b8 |= (uint8_t) (1 << bit);
    }
    m.d[8] = b8;
    uint8_t b9 = r.d[9] & 0x0F;
    for (int bit = 4; bit < 8; bit += 2) {   // Fn L/R, LB/RB
        if (r.d[9] & (1 << bit)) b9 |= (uint8_t) (2 << bit);
        if (r.d[9] & (2 << bit)) b9 |= (uint8_t) (1 << bit);
    }
    m.d[9] = b9;
    return m;
}

struct Box { int x0 = W, y0 = H, x1 = -1, y1 = -1; bool empty() const { return x1 < 0; } };
bool operator==(const Box &a, const Box &b) {
    return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
}
Box mirror(Box b) {
    if (b.empty()) return b;
    return {W - 1 - b.x1, b.y0, W - 1 - b.x0, b.y1};
}

// The d-pad's area (and, mirrored, the face buttons').
constexpr int kZone = oled_pad::kBtnStep + oled_pad::kBtn / 2;
bool in_dpad(int x, int y) {
    return x >= oled_pad::kDpadCx - kZone && x <= oled_pad::kDpadCx + kZone &&
           y >= oled_pad::kClusterCy - kZone && y <= oled_pad::kClusterCy + kZone;
}
bool in_cluster(int x, int y) { return in_dpad(x, y) || in_dpad(W - 1 - x, y); }

// Pixels in `zone` that are on in a (and, with b, differ between a and b).
template <class Zone>
Box bbox(const Frame &a, const Frame *b, Zone zone) {
    Box r;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            if (!zone(x, y)) continue;
            if (b ? a.p[y][x] == b->p[y][x] : !a.p[y][x]) continue;
            if (x < r.x0) r.x0 = x;
            if (x > r.x1) r.x1 = x;
            if (y < r.y0) r.y0 = y;
            if (y > r.y1) r.y1 = y;
        }
    return r;
}

// Everything but the d-pad / face buttons: f must equal the mirror image of g.
int asymmetric_pixels(const Frame &f, const Frame &g) {
    int n = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (!in_cluster(x, y) && f.p[y][x] != g.p[y][W - 1 - x]) n++;
    return n;
}

const auto kDpadZone = [](int x, int y) { return in_dpad(x, y); };
const auto kFaceZone = [](int x, int y) { return in_dpad(W - 1 - x, y); };

Report with(int byte, uint8_t v) { Report r; r.d[byte] = v; return r; }
}  // namespace

TEST(oled_pad_stays_in_its_area) {
    Report all;
    all.d[4] = all.d[5] = 255;
    all.d[7] = 0xF1;
    all.d[8] = 0xFF;
    for (const Report &r : {Report(), all, with(0, 0), with(3, 255)}) {
        const Frame f = render(r);
        const Box b = bbox(f, nullptr, [](int, int) { return true; });
        CHECK(b.x0 >= oled_pad::kLeft);   // clear of the button chrome (x 0..5)
        CHECK(b.y0 >= oled_pad::kTop);    // clear of the header rows
        CHECK(b.x1 <= oled_pad::kRight);
        CHECK(b.y1 < H);
    }
}

TEST(oled_pad_mirror_symmetric) {
    // Idle, each mirrored part pressed, sticks and triggers at their extremes.
    std::vector<Report> states = {Report(), with(8, 0x01), with(8, 0x04), with(8, 0x10),
                                  with(8, 0x40), with(8, 0x30), with(4, 128), with(4, 255),
                                  with(4, 1), with(0, 0), with(0, 255), with(1, 0),
                                  with(1, 255), with(0, 127), with(0, 100)};
    Report corner;
    corner.d[0] = corner.d[1] = 0;
    corner.d[2] = 255;
    corner.d[3] = 0;
    states.push_back(corner);
    for (int v = 0; v < 256; v += 17) states.push_back(with(2, (uint8_t) v));
    for (const Report &r : states) {
        const Frame f = render(r), g = render(mirrored(r));
        CHECK_EQ(asymmetric_pixels(f, g), 0);
        // The two clusters' outlines: same box, mirrored.
        CHECK(bbox(f, nullptr, kDpadZone) == mirror(bbox(f, nullptr, kFaceZone)));
    }
    // The symmetric idle picture: also equal to its own mirror image.
    const Frame idle = render(Report());
    CHECK_EQ(asymmetric_pixels(idle, idle), 0);
}

TEST(oled_pad_dpad_mirrors_face_buttons) {
    // Each d-pad direction lights the spot mirrored from its face button:
    // up <-> Triangle, right <-> Square, down <-> Cross, left <-> Circle.
    const Frame idle = render(Report());
    const struct { uint8_t hat, face; } pairs[] = {{0, 0x80}, {2, 0x10}, {4, 0x20}, {6, 0x40}};
    for (const auto &p : pairs) {
        Report d, f;
        d.d[7] = p.hat;
        f.d[7] = (uint8_t) (p.face | 0x08);
        const Box bd = bbox(render(d), &idle, kDpadZone);
        const Box bf = bbox(render(f), &idle, kFaceZone);
        CHECK(!bd.empty());
        CHECK(!bf.empty());
        CHECK(bd == mirror(bf));
    }
}

TEST(oled_pad_every_button_lights) {
    const Frame idle = render(Report());
    const auto changes = [&](const Report &r) {
        const Frame f = render(r);
        return bbox(f, &idle, [](int, int) { return true; });
    };
    // L1 R1 Create Options L3 R3 (L2/R2's digital bits: the bars show the analog value).
    for (int bit : {0, 1, 4, 5, 6, 7}) CHECK(!changes(with(8, (uint8_t) (1 << bit))).empty());
    for (int hat = 0; hat < 8; hat++) CHECK(!changes(with(7, (uint8_t) hat)).empty());
    for (int bit = 4; bit < 8; bit++) CHECK(!changes(with(7, (uint8_t) (0x08 | 1 << bit))).empty());
    // Create / Options light between L1 and R1, in the top row.
    const Box c = changes(with(8, 0x10)), o = changes(with(8, 0x20));
    CHECK(c.y0 >= oled_pad::kTop && c.y1 < oled_pad::kTop + oled_pad::kSmallH);  // fills its outline
    CHECK(c.x0 > oled_pad::kShoulderX + oled_pad::kShoulderW);
    CHECK(c.x1 < W / 2);
    CHECK(o == mirror(c));
}

TEST(oled_pad_stick_rest_is_centered) {
    // 127 and 128 (a centered stick) both put the dot in the middle of the box.
    for (uint8_t v : {127, 128}) {
        Report r;
        r.d[0] = r.d[1] = v;
        const Frame f = render(r);
        const Box dot = bbox(f, nullptr, [](int x, int y) {
            return x > oled_pad::kLeft && x < oled_pad::kLeft + oled_pad::kBox - 1 &&
                   y > oled_pad::kTop && y < oled_pad::kTop + oled_pad::kBox - 1;
        });
        CHECK_EQ(dot.x0 + dot.x1, 2 * oled_pad::kLeft + oled_pad::kBox - 1);
        CHECK_EQ(dot.y0 + dot.y1, 2 * oled_pad::kTop + oled_pad::kBox - 1);
    }
}

namespace {
// FNV-1a over the frame's pixels.
uint32_t frame_hash(const Frame &f) {
    uint32_t h = 2166136261u;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) { h ^= f.p[y][x]; h *= 16777619u; }
    return h;
}
// The states behind kDualSenseGolden, in order.
std::vector<Report> golden_states() {
    std::vector<Report> v;
    const auto add = [&](int byte, uint8_t val, uint8_t b9) {
        Report r;
        r.d[byte] = val;
        r.d[9] = b9;
        v.push_back(r);
    };
    add(7, 0x08, 0x00);                       // idle
    add(7, 0x08, 0xF0);                       // idle + the Edge bits: ignored on a DualSense
    for (int bit = 0; bit < 8; bit++) add(8, (uint8_t) (1 << bit), 0);
    for (int hat = 0; hat < 8; hat++) add(7, (uint8_t) hat, 0);
    for (int bit = 4; bit < 8; bit++) add(7, (uint8_t) (0x08 | 1 << bit), 0);
    for (int a = 0; a < 6; a++) { add(a, 0, 0); add(a, 255, 0); add(a, 77, 0); }
    Report all;
    all.d[0] = all.d[1] = 0;
    all.d[2] = all.d[3] = all.d[4] = all.d[5] = 255;
    all.d[7] = 0xF1;
    all.d[8] = all.d[9] = 0xFF;
    v.push_back(all);
    return v;
}
// Rendered by the glyph before the Edge marks were added (master 34e27f4).
constexpr uint32_t kDualSenseGolden[] = {
    0x259460D3u, 0x259460D3u, 0xC8FAD721u, 0xFF78EDB1u, 0x259460D3u, 0x259460D3u,
    0x6C842652u, 0xEB04FA88u, 0xFF155C60u, 0xDBFB1FC6u, 0xDE93066Cu, 0x22236BA7u,
    0xE203FB98u, 0x468A6AA3u, 0xC10DF1C8u, 0x788CB613u, 0x14064708u, 0xF0212037u,
    0x5F93709Eu, 0x1E2EA062u, 0xA588C4CEu, 0x6537A506u, 0x08971143u, 0xF13CD563u,
    0xBA73D2EDu, 0x8CB1276Bu, 0xE6466863u, 0x58678C57u, 0x9DB00E03u, 0xF1C87CA3u,
    0xD9DFC475u, 0xCAC70EA3u, 0xB413FB4Fu, 0xAE99CA67u, 0x259460D3u, 0x0000E2BDu,
    0xD9991BA9u, 0x259460D3u, 0xBC74564Du, 0x14B14619u, 0x4FE6228Bu,
};
constexpr int kTrigRight = oled_pad::kTrigX + oled_pad::kTrigW - 1;  // L2 bar's last column
bool in_edge_strip(int, int y) { return y >= oled_pad::kEdgeY && y < oled_pad::kEdgeY + oled_pad::kEdgeH; }
}  // namespace

TEST(oled_pad_dualsense_unchanged) {
    const std::vector<Report> states = golden_states();
    CHECK_EQ(states.size(), sizeof(kDualSenseGolden) / sizeof(kDualSenseGolden[0]));
    for (size_t i = 0; i < states.size() && i < std::size(kDualSenseGolden); i++)
        CHECK_EQ(frame_hash(render(states[i])), kDualSenseGolden[i]);
}

TEST(oled_pad_edge_only_adds_its_strip) {
    // An Edge draws the DualSense picture plus the strip, and nothing in the
    // strip's rows on a DualSense.
    for (const Report &r : golden_states()) {
        const Frame d = render(r), e = render(r, true);
        const Box diff = bbox(e, &d, [](int, int) { return true; });
        CHECK(!diff.empty());  // the marks' outlines at least
        CHECK(diff.y0 >= oled_pad::kEdgeY && diff.y1 < oled_pad::kEdgeY + oled_pad::kEdgeH);
        CHECK(diff.x0 > kTrigRight && diff.x1 < W - 1 - kTrigRight);  // between L2 and R2
        CHECK(bbox(d, nullptr, [](int x, int y) {
                  return in_edge_strip(x, y) && x > kTrigRight && x < W - 1 - kTrigRight;
              }).empty());
    }
}

TEST(oled_pad_edge_marks_mirror) {
    std::vector<Report> states = {Report(), with(9, 0x10), with(9, 0x20), with(9, 0x40),
                                  with(9, 0x80), with(9, 0x50), with(9, 0xF0)};
    Report all;
    all.d[7] = 0xF1;
    all.d[8] = all.d[9] = 0xFF;
    states.push_back(all);
    for (const Report &r : states) {
        const Frame f = render(r, true), g = render(mirrored(r), true);
        CHECK_EQ(asymmetric_pixels(f, g), 0);
        CHECK(bbox(f, nullptr, kDpadZone) == mirror(bbox(f, nullptr, kFaceZone)));
    }
    const Frame idle = render(Report(), true);
    CHECK_EQ(asymmetric_pixels(idle, idle), 0);
}

TEST(oled_pad_every_edge_mark_lights) {
    const Frame idle = render(Report(), true), ds_idle = render(Report());
    Box marks[4];
    for (int i = 0; i < 4; i++) {
        const Report r = with(9, (uint8_t) (0x10 << i));
        marks[i] = bbox(render(r, true), &idle, [](int, int) { return true; });
        CHECK(!marks[i].empty());
        CHECK(marks[i].y0 >= oled_pad::kEdgeY && marks[i].y1 < oled_pad::kEdgeY + oled_pad::kEdgeH);
        CHECK(bbox(render(r), &ds_idle, [](int, int) { return true; }).empty());  // a DualSense: nothing
    }
    // Fn L / Fn R inside (under Create / Options), LB / RB outside, mirrored.
    CHECK(marks[0] == mirror(marks[1]));
    CHECK(marks[2] == mirror(marks[3]));
    CHECK(marks[2].x1 < marks[0].x0);
    CHECK(marks[0].x1 < W / 2);
    // Pressed fills the whole mark: its outline was already on.
    const Frame all = render(with(9, 0xF0), true);
    for (int i = 0; i < 4; i++)
        for (int y = marks[i].y0 - 1; y <= marks[i].y1 + 1; y++)
            for (int x = marks[i].x0 - 1; x <= marks[i].x1 + 1; x++) CHECK(all.p[y][x]);
}

TEST(oled_pad_battery_spans_the_boxes) {
    // Outline + nub: from the left stick box's inner edge to the right one's,
    // mirrored around x = 63.5, at every level; fill proportional.
    const int left_inner = oled_pad::kLeft + oled_pad::kBox - 1;
    int last = -1;
    for (int pct = 0; pct <= 100; pct += 5) {
        Frame f;
        oled_pad::battery(f, pct);
        const Box b = bbox(f, nullptr, [](int, int) { return true; });
        CHECK_EQ(b.x0, left_inner);
        CHECK_EQ(b.x1, W - 1 - left_inner);
        CHECK(b == mirror(b));
        CHECK(b.y0 == oled_pad::kBattY && b.y1 < oled_pad::kTop - 1);
        int fill = 0;
        for (int x = oled_pad::kBattX0 + 1; x < oled_pad::kBattX0 + oled_pad::kBattW - 1; x++)
            fill += f.p[oled_pad::kBattY + 2][x];
        CHECK(fill >= last);
        last = fill;
        CHECK_EQ(fill, pct * oled_pad::kBattFill / 100);
        // No gap in the span: the outline's top row, then the nub.
        for (int x = b.x0; x <= b.x1; x++)
            CHECK(f.p[x < oled_pad::kBattX0 + oled_pad::kBattW ? oled_pad::kBattY
                                                               : oled_pad::kBattY + oled_pad::kBattH / 2][x]);
    }
    CHECK_EQ(last, oled_pad::kBattFill);
}

// PR #21: the Status glyph redraws only what changed (draw_changed with a
// cache). After any sequence of controller states the frame must be pixel-
// identical to a fresh full draw of the last state: sticks moving, clicking
// while moving, triggers, every button byte, Edge on/off, forced redraws.
TEST(oled_pad_partial_redraw_equals_full_draw) {
    std::mt19937 rng(2121);
    for (int run = 0; run < 40; run++) {
        Frame f;
        oled_pad::Cache cache;
        Report r;
        bool edge = run & 1;
        for (int step = 0; step < 400; step++) {
            const unsigned what = rng() % 10;
            if (what < 4) {           // a stick axis, small or large move
                const int a = (int) (rng() % 4);
                r.d[a] = (rng() & 1) ? (uint8_t) rng() : (uint8_t) (r.d[a] + (int) (rng() % 7) - 3);
            } else if (what < 6) {
                r.d[4 + rng() % 2] = (uint8_t) rng();
            } else if (what == 6) {
                r.d[7] = (uint8_t) ((rng() & 0xF0) | (rng() % 9));
            } else if (what == 7) {
                r.d[8] ^= (uint8_t) (1u << (rng() % 8));  // incl. L3 / R3 clicks
            } else if (what == 8) {
                r.d[9] ^= (uint8_t) (0x10u << (rng() % 4));
            } else if (rng() % 8 == 0) {
                edge = !edge;
            }
            const bool force = rng() % 50 == 0;
            oled_pad::draw_changed(f, r.d, edge, cache, force);
            const Frame full = render(r, edge);
            CHECK(std::memcmp(f.p, full.p, sizeof(f.p)) == 0);
        }
    }
}
