// src/buttons.h + src/buttons.cpp, Tension buttons (protocol 1.24, build step
// 2): every rule of docs/TENSION_BUTTONS.md section 3.5 against the shared
// vectors (fixtures/buttons/vectors.txt, expectations set by hand per rule in
// gen_vectors.py; the app copies the same file), the [58] bits a part needs,
// and a Press-only layout giving exactly remap_apply's output for the same
// table. The store and the protocol are in test_tension.cpp.
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "buttons.h"
#include "fakes.h"
#include "remap.h"
#include "tension.h"
#include "test.h"

using namespace buttons;

namespace {
struct Vector {
    std::string name, expect, needs;
    uint32_t crc = 0;
    ButtonPart part{};
};

std::vector<Vector> load_vectors() {
    std::vector<Vector> out;
    std::ifstream in(std::string(BUTTONS_VECTORS));
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        Vector v;
        std::string crc, hex;
        ls >> v.name >> v.expect >> v.needs >> crc >> hex;
        v.crc = (uint32_t) std::stoul(crc, nullptr, 16);
        CHECK_EQ(hex.size(), 2 * sizeof(ButtonPart));
        auto *b = reinterpret_cast<uint8_t *>(&v.part);
        for (size_t i = 0; i < sizeof(ButtonPart) && 2 * i + 1 < hex.size(); i++)
            b[i] = (uint8_t) std::stoul(hex.substr(2 * i, 2), nullptr, 16);
        out.push_back(v);
    }
    return out;
}

// A legacy table + Edge extras as a set's base layer: one Press entry per
// changed button, sorted by source (what the app's "Make a set from these
// buttons" writes, docs/TENSION_BUTTONS.md section 7.4).
ButtonPart part_from_table(const uint8_t table[16], const uint8_t extra[4]) {
    ButtonPart p{};
    p.bver = kPartVer;
    p.bflags = kUsed;
    p.double_window = 25;
    uint16_t n = 0;
    auto add = [&](uint8_t src, uint8_t to) {
        const uint8_t e[5] = {src, kPress, (uint8_t) (to == kOff ? kNothing : kButton), (uint8_t) (to == kOff ? 0 : to), 0};
        std::memcpy(p.pool + n, e, 5);
        n += 5;
    };
    for (uint8_t s = 0; s < 16; s++)
        if (table[s] != s) add(s, table[s] == 0xFF ? kOff : table[s]);
    for (uint8_t i = 0; i < 4; i++)
        if (extra[i] != kRemapExtraSelf) add((uint8_t) (kFnLeft + i), extra[i] == kRemapExtraOff ? kOff : extra[i]);
    p.pool_len = n;
    return p;
}

uint32_t g_rng = 12345;
uint32_t rnd() {
    g_rng = g_rng * 1103515245u + 12345u;
    return g_rng >> 8;
}
}  // namespace

TEST(buttons_vectors_follow_every_rule) {
    const std::vector<Vector> vs = load_vectors();
    CHECK(vs.size() >= 80);
    int ok = 0, bad = 0;
    for (const Vector &v : vs) {
        const bool valid = part_valid(v.part);
        if (valid != (v.expect == "ok")) std::fprintf(stderr, "  vector %s: part_valid %d\n", v.name.c_str(), valid);
        CHECK_EQ(valid, v.expect == "ok");
        CHECK_EQ(tension::crc32_ieee(reinterpret_cast<const uint8_t *>(&v.part), sizeof(v.part)), v.crc);
        if (v.expect == "ok") {
            ok++;
            const unsigned want = (unsigned) std::stoul(v.needs, nullptr, 16);
            if (part_needs(v.part) != want) std::fprintf(stderr, "  vector %s: needs %02x\n", v.name.c_str(), part_needs(v.part));
            CHECK_EQ(part_needs(v.part), want);
        } else {
            bad++;
        }
    }
    CHECK(ok >= 20);
    CHECK(bad >= 50);
}

TEST(buttons_compile_runs_only_the_built_pieces) {
    const std::vector<Vector> vs = load_vectors();
    Layout l;
    for (const Vector &v : vs) {
        if (v.expect != "ok") continue;
        const bool runs = compile(v.part, l);
        // Step 2 runs a used part that needs nothing past [58] bit0.
        CHECK_EQ(runs, v.needs == "01");
        if (v.name == "cross_circle_swap") {
            CHECK_EQ(l.press[kCross], kCircle);
            CHECK_EQ(l.press[kCircle], kCross);
            CHECK_EQ(l.press[kSquare], kSelf);
        }
        if (v.name == "square_disabled") CHECK_EQ(l.press[kSquare], kOff);
        if (v.name == "left_back_button_is_cross") CHECK_EQ(l.press[kBackLeft], kCross);
    }
}

TEST(buttons_apply_swaps_moves_and_disables) {
    uint8_t table[16], extra[4];
    for (uint8_t i = 0; i < 16; i++) table[i] = i;
    std::memset(extra, kRemapExtraSelf, 4);
    table[kCross] = kCircle;
    table[kCircle] = kCross;
    table[kSquare] = 0xFF;
    extra[2] = kTriangle;  // left back button
    const ButtonPart p = part_from_table(table, extra);
    CHECK(part_valid(p));
    Layout l;
    CHECK(compile(p, l));
    uint8_t r[63]{};
    r[7] = 0x08 | 0x20 | 0x10;  // hat none, Cross and Square down
    r[9] = 0x40 | 0x04 | 0x01;  // left back button, Mute, PS
    apply(l, r);
    CHECK_EQ(r[7], 0x08 | 0x40 | 0x80);  // Circle (from Cross) and Triangle (from the back button); Square off
    CHECK_EQ(r[9], 0x04 | 0x01);         // the back button cleared, Mute and PS kept
}

// Build step 2's check: a Press-only layout gives byte for byte what
// remap_apply gives for the same legacy table and Edge extras.
TEST(buttons_press_only_equals_remap_apply) {
    fake_flash_erase_all();
    g_fake_flash_fail = false;
    remap_load();
    for (int round = 0; round < 300; round++) {
        uint8_t table[16], extra[4];
        for (uint8_t i = 0; i < 16; i++) {
            const uint32_t k = rnd() % 4;
            table[i] = k == 0 ? i : k == 1 ? 0xFF : (uint8_t) (rnd() % 16);
        }
        for (uint8_t i = 0; i < 4; i++) {
            const uint32_t k = rnd() % 3;
            extra[i] = k == 0 ? kRemapExtraSelf : k == 1 ? kRemapExtraOff : (uint8_t) (rnd() % 16);
        }
        if (round == 0) {  // the identity too
            for (uint8_t i = 0; i < 16; i++) table[i] = i;
            std::memset(extra, kRemapExtraSelf, 4);
        }
        CHECK(remap_set_temporary(table));
        CHECK(remap_set_extra(extra, true) == RemapSet::Ok);
        const ButtonPart p = part_from_table(table, extra);
        CHECK(part_valid(p));
        CHECK_EQ(part_needs(p), kFeatLayouts);
        Layout l;
        CHECK(compile(p, l));
        for (int k = 0; k < 40; k++) {
            uint8_t a[63], b[63];
            for (uint8_t &x : a) x = (uint8_t) rnd();
            a[7] = (uint8_t) ((a[7] & 0xF0) | (rnd() % 9));  // a real hat: 0..8 (remap_apply's identity keeps 9..15, a rebuild makes them 8)
            std::memcpy(b, a, 63);
            remap_apply(a);
            apply(l, b);
            CHECK(std::memcmp(a, b, 63) == 0);
        }
    }
    remap_revert();
}

// buttons_apply: the running layout, else remap_apply unchanged.
TEST(buttons_apply_falls_back_to_the_remap_table) {
    fake_flash_erase_all();
    remap_load();
    uint8_t table[16];
    for (uint8_t i = 0; i < 16; i++) table[i] = i;
    table[kCross] = kTriangle;
    CHECK(remap_set_temporary(table));
    buttons_run(nullptr);
    uint8_t r[63]{};
    r[7] = 0x08 | 0x20;  // Cross
    buttons_apply(r);
    CHECK_EQ(r[7], 0x08 | 0x80);  // the remap table: Triangle
    Layout l;
    std::memset(l.press, kSelf, sizeof(l.press));
    l.press[kCross] = kSquare;
    buttons_run(&l);
    r[7] = 0x08 | 0x20;
    buttons_apply(r);
    CHECK_EQ(r[7], 0x08 | 0x10);  // the set replaces the table completely: Square
    buttons_run(nullptr);
    remap_revert();
}
