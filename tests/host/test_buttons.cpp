// src/buttons.h + src/buttons.cpp, Tension buttons (protocol 1.24): every rule
// of docs/TENSION_BUTTONS.md section 3.5 against the shared vectors
// (fixtures/buttons/vectors.txt, expectations set by hand per rule in
// gen_vectors.py; the app copies the same file), the [58] bits a part needs,
// a Press-only layout giving exactly remap_apply's output for the same table
// (step 2), the press types on scripted timelines (step 4), layers (step 5),
// the keyboard and mouse reports (step 6), mouse motion and the gyro's integer
// math (step 7), and GET 0xE8 after a scripted press (step 3). The store, the
// protocol and 0x22 (step 8) are in test_tension.cpp.
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "buttons.h"
#include "cmd.h"
#include "config.h"
#include "fakes.h"
#include "remap.h"
#include "tension.h"
#include "test.h"
#include "tusb.h"  // tud_hid_report_complete_cb (the live-truth capture)
#include "usb_keys_mouse.h"

uint8_t const *tud_descriptor_configuration_cb(uint8_t index);
extern uint8_t interrupt_in_data[63];

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
constexpr uint8_t kOff = 0xFE;  // part_from_table: the button does nothing
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


namespace {
// A used part from raw pool entries (each {src | layer << 5, rows, targets...,
// hold?, turbo?}), `layers` extra layers.
ButtonPart part(std::initializer_list<std::initializer_list<uint8_t>> entries, uint8_t layers = 0) {
    ButtonPart p{};
    p.bver = kPartVer;
    p.bflags = kUsed;
    p.layers = layers;
    p.double_window = 25;  // 0.25 s
    for (auto e : entries) {
        for (uint8_t b : e) p.pool[p.pool_len++] = b;
    }
    return p;
}

constexpr uint32_t bit(int s) { return 1u << s; }

// One layout on a scripted timeline, one engine step per millisecond.
struct Rig {
    Layout l{};
    Engine g{};
    uint32_t now = 7000000;
    explicit Rig(const ButtonPart &p) {
        CHECK(part_valid(p));
        CHECK(compile(p, l));
        reset(g);
    }
    // `down` held for `ms`, a step each millisecond from now (now moves on by ms).
    void hold(uint32_t down, uint32_t ms) {
        for (uint32_t i = 0; i < ms; i++) {
            step(g, l, down, now);
            now += 1000;
        }
    }
    void at(uint32_t down) { step(g, l, down, now); }  // one step at now
    void later(uint32_t ms, uint32_t down) {           // one step ms from now
        now += ms * 1000;
        at(down);
    }
    bool on(int target) const { return ((g.pad >> target) & 1u) != 0; }
    uint8_t key(int i) const { return g.keys[i]; }
};
}  // namespace

TEST(buttons_compile_runs_every_valid_part) {
    const std::vector<Vector> vs = load_vectors();
    static Layout l;
    for (const Vector &v : vs) {
        if (v.expect != "ok") continue;
        // Steps 2-8 are all built: a used part runs, an unused one leaves the buttons alone.
        CHECK_EQ(compile(v.part, l), v.name != "not_used_all_zero");
        if (v.name == "cross_circle_swap") {
            CHECK_EQ(resolve(l, 0, kCross)->t[0].a, kCircle);
            CHECK_EQ(resolve(l, 0, kCircle)->t[0].a, kCross);
            CHECK_EQ(resolve(l, 0, kSquare)->t[0].a, kSquare);  // itself
        }
        if (v.name == "square_disabled") CHECK_EQ(resolve(l, 0, kSquare)->t[0].kind, kNothing);
        if (v.name == "left_back_button_is_cross") CHECK_EQ(resolve(l, 0, kBackLeft)->t[0].a, kCross);
        if (v.name == "toggle_without_press_row") CHECK_EQ(resolve(l, 0, kCross)->t[0].a, kCross);  // no Press row: itself
        if (v.name == "with_ps_on_cross") CHECK(l.with_ps[0]);
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
    Rig r(part_from_table(table, extra));
    uint8_t rep[63]{};
    rep[7] = 0x08 | 0x20 | 0x10;  // hat none, Cross and Square down
    rep[9] = 0x40 | 0x04 | 0x01;  // left back button, Mute, PS
    r.at(sources_down(rep));
    apply(r.g, r.l, rep);
    CHECK_EQ(rep[7], 0x08 | 0x40 | 0x80);  // Circle (from Cross) and Triangle (from the back button); Square off
    CHECK_EQ(rep[9], 0x04 | 0x01);         // the back button cleared, Mute and PS kept
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
        CHECK_EQ(part_needs(p), kFeatLayouts);
        Rig r(p);
        for (int k = 0; k < 40; k++) {
            uint8_t a[63], b[63];
            for (uint8_t &x : a) x = (uint8_t) rnd();
            a[7] = (uint8_t) ((a[7] & 0xF0) | (rnd() % 9));  // a real hat: 0..8 (remap_apply's identity keeps 9..15, a rebuild makes them 8)
            std::memcpy(b, a, 63);
            remap_apply(a);
            r.later(1, sources_down(b));  // Press only: each report's buttons go out as they are
            apply(r.g, r.l, b);
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
    buttons_run(nullptr, 0xFF);
    uint8_t r[63]{};
    r[7] = 0x08 | 0x20;  // Cross
    buttons_apply(r);
    CHECK_EQ(r[7], 0x08 | 0x80);  // the remap table: Triangle
    static Layout l;
    CHECK(compile(part({{kCross, kPress, kButton, kSquare, 0}}), l));
    buttons_run(&l, 0);
    r[7] = 0x08 | 0x20;
    buttons_apply(r);
    CHECK_EQ(r[7], 0x08 | 0x10);  // the set replaces the table completely: Square
    buttons_run(nullptr, 0xFF);
    remap_revert();
}

// --- Step 4: press types (section 4.4, 4.5) ----------------------------------

TEST(buttons_hold_tap_and_hold_at_the_edge) {
    Rig r(part({{kCross, kHold, kButton, kTriangle, 0, 30}}));  // Cross: Hold 0.30 s -> Triangle
    r.hold(bit(kCross), 100);
    CHECK(!r.on(kCross) && !r.on(kTriangle));  // deciding: nothing yet, Cross cleared
    r.at(0);                                   // let go before the hold time: Press (itself) as a tap
    CHECK(r.on(kCross));
    CHECK(!r.on(kTriangle));
    r.later(49, 0);
    CHECK(r.on(kCross));  // the tap lasts kTapUs
    r.later(1, 0);
    CHECK(!r.on(kCross));
    r.later(100, 0);
    r.hold(bit(kCross), 300);  // 0..299 ms: still deciding
    CHECK(!r.on(kTriangle) && !r.on(kCross));
    r.at(bit(kCross));  // exactly 300 ms: Hold
    CHECK(r.on(kTriangle));
    CHECK(!r.on(kCross));
    r.later(500, bit(kCross));
    CHECK(r.on(kTriangle));  // held while the button is
    r.later(1, 0);
    CHECK(!r.on(kTriangle));
    CHECK(!r.on(kCross));  // after a Hold, Press never fires for that press
    r.later(60, 0);
    CHECK(!r.on(kCross));
}

TEST(buttons_double_press_inside_and_outside_the_window) {
    Rig r(part({{kCross, kDouble, kButton, kSquare, 0}}));  // window 25 = 0.25 s
    r.hold(bit(kCross), 50);
    r.hold(0, 100);
    CHECK(!r.on(kCross) && !r.on(kSquare));  // waiting for a second press
    r.hold(bit(kCross), 30);
    CHECK(r.on(kSquare));  // second press inside the window: Double press, held while it is
    CHECK(!r.on(kCross));
    r.at(0);
    CHECK(!r.on(kSquare));
    r.hold(0, 400);
    CHECK(!r.on(kCross));  // and no tap after a double press
    // Outside: the window ends without a second press, Press goes out as a tap.
    r.hold(bit(kCross), 50);
    r.hold(0, 250);  // 0..249 ms after the release
    CHECK(!r.on(kCross));
    r.at(0);  // 250 ms
    CHECK(r.on(kCross));
    r.later(50, 0);
    CHECK(!r.on(kCross));
}

TEST(buttons_with_ps_chord_and_ps_pass_through) {
    Rig r(part({{kCross, kWithPs, kButton, kCircle, 0}}));  // Cross with PS -> Circle
    const uint32_t ps = bit(kPs), x = bit(kCross);
    r.hold(ps, 20);
    CHECK(!r.on(kPs));  // held back: this layer has a With PS row
    r.hold(ps | x, 50);
    CHECK(r.on(kCircle));
    CHECK(!r.on(kCross) && !r.on(kPs));
    r.hold(ps, 20);
    CHECK(!r.on(kCircle));
    r.at(0);
    CHECK(!r.on(kPs));  // the chord used this PS press: PS never reaches the PC
    r.hold(0, 100);
    // PS alone, short: a tap when it is let go.
    r.hold(ps, 100);
    CHECK(!r.on(kPs));
    r.at(0);
    CHECK(r.on(kPs));
    r.later(100, 0);
    // PS alone, long: out as held from 0.5 s, so a long PS press still works.
    r.hold(ps, 500);
    CHECK(!r.on(kPs));
    r.at(ps);
    CHECK(r.on(kPs));
    r.later(300, ps);
    CHECK(r.on(kPs));
    r.later(1, 0);
    CHECK(!r.on(kPs));
    // Cross alone is still Cross.
    r.later(100, x);
    CHECK(r.on(kCross));
    // Without a With PS row, PS passes at once.
    Rig plain(part({{kCross, kPress, kButton, kCircle, 0}}));
    plain.at(ps);
    CHECK(plain.on(kPs));
}

TEST(buttons_toggle_turbo_and_both) {
    Rig t(part({{kCross, kToggle}}));  // Toggle, Press = Cross itself
    t.hold(bit(kCross), 30);
    CHECK(t.on(kCross));
    t.hold(0, 30);
    CHECK(t.on(kCross));  // latched
    t.hold(bit(kCross), 30);
    CHECK(!t.on(kCross));
    t.hold(0, 30);
    CHECK(!t.on(kCross));

    Rig u(part({{kR1, kTurbo, 10}}));  // R1 Turbo, 10 a second: 50 ms on, 50 ms off, starting on
    u.at(bit(kR1));
    CHECK(u.on(kR1));
    u.later(49, bit(kR1));
    CHECK(u.on(kR1));
    u.later(1, bit(kR1));  // 50 ms
    CHECK(!u.on(kR1));
    u.later(49, bit(kR1));
    CHECK(!u.on(kR1));
    u.later(1, bit(kR1));  // 100 ms
    CHECK(u.on(kR1));
    u.later(1, 0);
    CHECK(!u.on(kR1));

    Rig b(part({{kR1, kToggle | kTurbo, 10}}));  // latched auto-fire
    b.hold(bit(kR1), 20);  // the latch goes on at the press: pulses from then
    b.at(0);               // 20 ms: on
    CHECK(b.on(kR1));
    b.later(40, 0);  // 60 ms after the press: off, still latched
    CHECK(!b.on(kR1));
    b.later(50, 0);  // 110 ms: on again
    CHECK(b.on(kR1));
    b.hold(bit(kR1), 10);  // press again: latch off
    b.at(0);
    CHECK(!b.on(kR1));
}

TEST(buttons_release_everything_leaves_nothing_on) {
    Rig r(part({{kCircle, kHold, kButton, kTriangle, 0, 30}, {kCross, kToggle}}));
    r.hold(bit(kCross), 10);
    r.hold(0, 10);
    CHECK(r.on(kCross));  // latched
    r.hold(bit(kCircle), 300);
    r.at(bit(kCircle));
    CHECK(r.on(kTriangle));  // Hold on
    reset(r.g);
    r.later(1, bit(kCircle));  // still held: now a new press, deciding
    CHECK(!r.on(kCross));
    CHECK(!r.on(kTriangle));
    CHECK_EQ(r.g.pad, 0u);
}

// --- Step 5: layers (section 4.3) ---------------------------------------------

TEST(buttons_layer_resolves_once_and_never_sticks) {
    // L1 holds layer 1 (and isn't sent itself); in layer 1 Cross does Square.
    Rig r(part({{kL1, kPress, kLayer, 1, 0}, {kCross | 1 << 5, kPress, kButton, kSquare, 0}}, 1));
    const uint32_t l1 = bit(kL1), x = bit(kCross);
    r.at(x);
    CHECK(r.on(kCross));  // the base
    r.later(1, 0);
    r.later(1, l1);
    CHECK(!r.on(kL1));
    CHECK_EQ(r.g.layer, 1);
    r.later(1, l1 | x);
    CHECK(r.on(kSquare));
    CHECK(!r.on(kCross));
    // L1 let go while Cross is held: Cross keeps the job it started with.
    r.later(1, x);
    CHECK_EQ(r.g.layer, 0);
    CHECK(r.on(kSquare));
    CHECK(!r.on(kCross));
    r.later(1, 0);  // and lets go of it: nothing left on
    CHECK_EQ(r.g.pad, 0u);
    r.later(1, x);
    CHECK(r.on(kCross));  // a new press in the base
}

TEST(buttons_layer_switch_and_switch_back) {
    // R1 switches layer 1 on and, pressed again (it has no entry in layer 1), off.
    Rig r(part({{kR1, kPress, kLayer, 1, 1}, {kCross | 1 << 5, kPress, kButton, kSquare, 0}}, 1));
    const uint32_t r1 = bit(kR1), x = bit(kCross);
    r.hold(r1, 10);
    r.hold(0, 10);
    CHECK_EQ(r.g.layer, 1);
    r.hold(x, 10);
    CHECK(r.on(kSquare));
    r.hold(0, 10);
    r.hold(r1, 10);
    r.hold(0, 10);
    CHECK_EQ(r.g.layer, 0);  // back to the base
    r.at(x);
    CHECK(r.on(kCross));
    CHECK(!r.on(kSquare));
}

TEST(buttons_layer_entered_by_hold) {
    // L1: Press -> R1, Hold 0.30 s -> layer 1 while held; in layer 1 Cross does Square.
    Rig r(part({{kL1, kPress | kHold, kButton, kR1, 0, kLayer, 1, 0, 30}, {kCross | 1 << 5, kPress, kButton, kSquare, 0}}, 1));
    r.hold(bit(kL1), 100);
    r.at(0);
    CHECK(r.on(kR1));  // a tap of L1: its Press
    CHECK_EQ(r.g.layer, 0);
    r.later(100, 0);
    r.hold(bit(kL1), 300);
    r.at(bit(kL1));
    CHECK_EQ(r.g.layer, 1);  // the hold time passed
    CHECK(!r.on(kR1));
    r.later(1, bit(kL1) | bit(kCross));
    CHECK(r.on(kSquare));
    r.later(1, 0);
    CHECK_EQ(r.g.layer, 0);
    CHECK_EQ(r.g.pad, 0u);
}

// --- Step 6: keys and mouse buttons ---------------------------------------------

TEST(buttons_keyboard_report_bytes_and_pacing) {
    Rig f(part({{kR1, kPress, kKey, 0x3E, 0}}));  // R1 -> F5
    uint8_t kb[8];
    f.at(bit(kR1));
    keyboard_report(f.g, kb);
    const uint8_t f5[8] = {0, 0, 0x3E, 0, 0, 0, 0, 0};
    CHECK(std::memcmp(kb, f5, 8) == 0);  // no modifiers: the key at once
    CHECK(!f.on(kR1));                   // R1 cleared from the gamepad
    f.later(10, 0);
    CHECK_EQ(f.key(0), 0x3E);  // a short press is held kKeyHoldUs
    f.later(49, 0);            // 59 ms
    CHECK_EQ(f.key(0), 0x3E);
    f.later(1, 0);  // 60 ms
    CHECK_EQ(f.key(0), 0);

    Rig c(part({{kR1, kPress, kKey, 0x06, 0x01}}));  // R1 -> Left Ctrl + C
    c.at(bit(kR1));
    CHECK_EQ(c.g.mods, 0x01);  // the modifier leads
    CHECK_EQ(c.key(0), 0);
    c.later(19, bit(kR1));
    CHECK_EQ(c.key(0), 0);
    c.later(1, bit(kR1));  // 20 ms: the key
    CHECK_EQ(c.key(0), 0x06);
    c.later(10, 0);  // let go at 30 ms
    CHECK_EQ(c.key(0), 0x06);
    c.later(49, 0);  // 79 ms: lead + the least hold not over yet
    CHECK_EQ(c.key(0), 0x06);
    c.later(1, 0);  // 80 ms: the key up, the modifier trails
    CHECK_EQ(c.key(0), 0);
    CHECK_EQ(c.g.mods, 0x01);
    c.later(19, 0);
    CHECK_EQ(c.g.mods, 0x01);
    c.later(1, 0);  // 100 ms
    CHECK_EQ(c.g.mods, 0);

    Rig m(part({{kCircle, kPress, kMouseButton, 5, 0}, {kCross, kPress, kMouseButton, 1, 0}}));
    m.at(bit(kCross) | bit(kCircle));
    CHECK_EQ(m.g.mouse_buttons, 0x01 | 0x10);  // left, forward
    CHECK(!m.on(kCross) && !m.on(kCircle));
}

TEST(buttons_six_keys_at_most_then_counted) {
    // L2, L1, Create, Up, Left, Down, Right -> A .. G (usages 0x04..0x0A).
    Rig r(part({{kL2, kPress, kKey, 0x04, 0},
                {kL1, kPress, kKey, 0x05, 0},
                {kCreate, kPress, kKey, 0x06, 0},
                {kUp, kPress, kKey, 0x07, 0},
                {kLeft, kPress, kKey, 0x08, 0},
                {kDown, kPress, kKey, 0x09, 0},
                {kRight, kPress, kKey, 0x0A, 0}}));
    const uint32_t all = bit(kL2) | bit(kL1) | bit(kCreate) | bit(kUp) | bit(kLeft) | bit(kDown) | bit(kRight);
    const uint8_t dropped0 = r.g.dropped;
    r.hold(all, 100);
    for (int i = 0; i < 6; i++) CHECK_EQ(r.key(i), 0x04 + i);  // the first six, in source order
    CHECK_EQ(r.g.dropped, dropped0 + 1);                       // the seventh counted once
    r.at(all & ~bit(kL2));  // A let go (held > 60 ms: up at once)
    CHECK_EQ(r.key(0), 0x05);
    CHECK_EQ(r.key(4), 0x09);
    CHECK_EQ(r.key(5), 0);  // G stays out for this press: never sent late
    CHECK_EQ(r.g.dropped, dropped0 + 1);
    reset(r.g);  // release everything: no key left
    CHECK_EQ(r.key(0), 0);
    CHECK_EQ(r.g.mods, 0);
}

// --- Step 7: mouse motion and gyro (section 4.6) ------------------------------

TEST(buttons_mouse_motion_counts_per_second) {
    // Up moves the pointer up at speed 4 (100 counts a second), Down the wheel down at 30 a second.
    Rig r(part({{kUp, kPress, kMouseMove, 0, 4}, {kDown, kPress, kMouseMove, 5, 30}}));
    r.hold(bit(kUp) | bit(kDown), 1001);  // the first step starts the clock: 1000 ms of motion
    CHECK_EQ(r.g.pend[0], 0);
    CHECK_EQ(r.g.pend[1], -100);
    CHECK_EQ(r.g.pend[2], -30);
    uint8_t m[7];
    int32_t took[3];
    mouse_report(r.g, 0, m, took);
    const uint8_t want[7] = {0, 0, 0, 0x9C, 0xFF, 0xE2, 0};  // y -100, wheel -30
    CHECK(std::memcmp(m, want, 7) == 0);
    r.hold(0, 10);
    CHECK_EQ(r.g.acc[1], 0);  // let go: no remainder carried to the next press
}

namespace {
ButtonPart gyro_part(uint8_t mode, uint8_t flags, uint8_t steady, std::initializer_list<std::initializer_list<uint8_t>> entries = {}) {
    ButtonPart p = part(entries);
    p.bflags = kUsed | kGyroUsed;
    const uint8_t g[10] = {mode, 0, flags, 64, 64, steady, 0, 0, 0, 0};
    std::memcpy(p.gyro, g, 10);
    return p;
}
}  // namespace

// Integer gyro math against hand-worked vectors (kGyroShift 25, 2^25 = 33554432).
TEST(buttons_gyro_mouse_integer_math) {
    Rig r(gyro_part(1, 0, 0));
    r.at(0);
    CHECK(r.g.gyro_on);
    // w = -1000 (turning left moves left), dt 4 ms, speed 64: -256000000 >> 25 = floor(-7.63) = -8,
    // remainder -256000000 + 8 x 33554432 = 12435456.
    gyro_input(r.g, r.l, 1000, 0, 4000);
    CHECK_EQ(r.g.pend[0], -8);
    CHECK_EQ(r.g.gacc[0], 12435456);
    // Again: 12435456 - 256000000 = -243564544 -> -8, remainder 24870912 (nothing is lost).
    gyro_input(r.g, r.l, 1000, 0, 4000);
    CHECK_EQ(r.g.pend[0], -16);
    CHECK_EQ(r.g.gacc[0], 24870912);
    // Pitch moves y the same way.
    gyro_input(r.g, r.l, 0, 1000, 4000);
    CHECK_EQ(r.g.pend[1], -8);

    // dt clamped to 20 ms: -1000 x 20000 x 64 = -1.28e9 -> -39, remainder 28622848.
    Rig c(gyro_part(1, 0, 0));
    c.at(0);
    gyro_input(c.g, c.l, 1000, 0, 100000);
    CHECK_EQ(c.g.pend[0], -39);
    CHECK_EQ(c.g.gacc[0], 28622848);

    // Steadiness 8 (T = 32): w = -16 -> -16 x 16 / 32 = -8; -8 x 4000 x 64 = -2048000 -> -1, remainder 31506432.
    Rig s(gyro_part(1, 0, 8));
    s.at(0);
    gyro_input(s.g, s.l, 16, 0, 4000);
    CHECK_EQ(s.g.pend[0], -1);
    CHECK_EQ(s.g.gacc[0], 31506432);

    // Invert horizontal: +256000000 -> 7, remainder 21118976.
    Rig i(gyro_part(1, 0x01, 0));
    i.at(0);
    gyro_input(i.g, i.l, 1000, 0, 4000);
    CHECK_EQ(i.g.pend[0], 7);
    CHECK_EQ(i.g.gacc[0], 21118976);
}

TEST(buttons_gyro_right_stick_and_needs_a_button) {
    Rig r(gyro_part(2, 0, 0));
    r.at(0);
    gyro_input(r.g, r.l, 1000, 0, 4000);  // -1000 x 64 >> 9 = -125
    CHECK_EQ(r.g.gstick[0], -125);
    CHECK_EQ(r.g.pend[0], 0);  // right stick, not the mouse
    uint8_t rep[63]{};
    rep[2] = 128;
    rep[3] = 128;
    apply(r.g, r.l, rep);
    CHECK_EQ(rep[2], 3);
    CHECK_EQ(rep[3], 128);
    Rig s(gyro_part(2, 0, 8));
    s.at(0);
    gyro_input(s.g, s.l, 16, 0, 4000);  // steadiness: -8 x 64 >> 9 = -1
    CHECK_EQ(s.g.gstick[0], -1);

    // Flags bit2: only while a Gyro target holds it on (Cross here); off at once, nothing kept.
    Rig b(gyro_part(1, 0x04, 0, {{kCross, kPress, kGyro, 0, 0}}));
    b.at(0);
    CHECK(!b.g.gyro_on);
    b.later(1, bit(kCross));
    CHECK(b.g.gyro_on);
    CHECK(!b.on(kCross));
    gyro_input(b.g, b.l, 16, 0, 4000);
    CHECK(b.g.gacc[0] != 0);
    b.later(1, 0);
    CHECK(!b.g.gyro_on);
    CHECK_EQ(b.g.gacc[0], 0);
}

// --- Step 3: live truth, GET 0xE8 (section 6.4) -------------------------------

TEST(buttons_live_truth_after_a_scripted_press) {
    fake_flash_erase_all();
    remap_load();
    static Layout l;
    CHECK(compile(part({{kCross, kHold, kButton, kTriangle, 0, 30}}), l));
    g_fake_bt_connected = true;
    g_fake_tud_suspended = false;
    g_fake_tud_mounted = false;  // the keys and mouse interface isn't on the bus here
    g_usb_wake_keyboard = false;
    const uint8_t unpause[2] = {1, 0};
    CHECK_EQ(buttons_pause(unpause, 2), CMD_RESULT_OK);
    g_fake_time_us = 20000000;
    buttons_run(&l, 2);
    std::memset(interrupt_in_data, 0, 63);
    interrupt_in_data[7] = 0x08 | 0x20;  // Cross down
    buttons_on_input();
    buttons_task();
    g_fake_time_us += 100000;
    buttons_on_input();
    buttons_task();
    uint8_t b[63];
    CHECK_EQ(buttons_live_get(b, 62), 0);
    CHECK_EQ(buttons_live_get(b, 63), 63);
    CHECK_EQ(b[0], 'B');
    CHECK_EQ(b[1], 'T');
    CHECK_EQ(b[2], 1);
    CHECK_EQ(b[3], 0x03);  // a layout runs, a set's
    CHECK_EQ(b[4], 2);
    CHECK_EQ(b[5], 0);
    CHECK_EQ(b[6] | b[7] << 8 | b[8] << 16, 1 << kCross);  // physically down
    CHECK_EQ(b[9] | b[10] << 8 | b[11] << 16, 0);          // deciding: no action on yet
    CHECK_EQ(b[12], kCross);
    CHECK_EQ(b[13], kEvDeciding);
    CHECK_EQ(b[14], kEvPhaseDeciding);
    CHECK_EQ(b[15] | b[16] << 8, 100);  // ms down
    CHECK_EQ(b[17] | b[18] << 8, 300);  // ms its Hold needs
    g_fake_time_us += 200000;           // 300 ms: the Hold
    buttons_on_input();
    buttons_task();
    // The gamepad report as it goes out, then fetched by the host.
    uint8_t out[64];
    out[0] = 0x01;
    std::memcpy(out + 1, interrupt_in_data, 63);
    buttons_apply(out + 1);
    tud_hid_report_complete_cb(0, out, 64);
    buttons_live_get(b, 63);
    CHECK_EQ(b[9] | b[10] << 8 | b[11] << 16, 1 << kCross);  // its action is on
    CHECK_EQ(b[13], kEvHold);
    CHECK_EQ(b[14], kEvPhaseOn);
    CHECK_EQ(b[19], kButton);
    CHECK_EQ(b[20], kTriangle);
    CHECK_EQ(b[21], 0);
    CHECK_EQ(b[22], 0);
    CHECK_EQ(b[23], 0x08 | 0x80);  // the PC got Triangle, not Cross
    const uint32_t count0 = (uint32_t) (b[42] | b[43] << 8 | b[44] << 16 | (uint32_t) b[45] << 24);
    CHECK(count0 >= 1);
    // Let go: the event stays, released, its time frozen.
    interrupt_in_data[7] = 0x08;
    g_fake_time_us += 50000;
    buttons_on_input();
    buttons_task();
    g_fake_time_us += 500000;
    buttons_live_get(b, 63);
    CHECK_EQ(b[12], kCross);
    CHECK_EQ(b[14], kEvPhaseReleased);
    CHECK_EQ(b[15] | b[16] << 8, 350);
    // With nothing of a set's running: the legacy table, no event.
    buttons_run(nullptr, 0xFF);
    buttons_live_get(b, 63);
    CHECK_EQ(b[3] & 0x02, 0);
    CHECK_EQ(b[4], 0xFF);  // identity table: nothing runs
    CHECK_EQ(b[12], 0xFF);
    g_fake_bt_connected = false;
}

TEST(buttons_keys_go_out_pause_and_stale_release) {
    fake_flash_erase_all();
    g_fake_flash_fail = false;
    config_load();
    Config_body cb = get_config();
    cb.keys_mouse = 1;
    set_config(cb);
    g_usb_wake_keyboard = false;
    g_fake_tud_mounted = false;
    tud_descriptor_configuration_cb(0);  // enumerated with the switch on
    g_fake_tud_mounted = true;
    CHECK(usb_keys_mouse_on());
    g_fake_bt_connected = true;
    g_fake_tud_suspended = false;
    g_fake_hid_ready = true;
    static Layout l;
    CHECK(compile(part({{kR1, kPress, kKey, 0x3E, 0}}), l));  // R1 -> F5
    g_fake_time_us = 40000000;
    buttons_run(&l, 0);
    g_fake_hid_reports.clear();
    std::memset(interrupt_in_data, 0, 63);
    interrupt_in_data[7] = 0x08;
    interrupt_in_data[8] = 0x02;  // R1
    buttons_on_input();
    buttons_task();
    const std::vector<uint8_t> f5{KEYS_MOUSE_HID_INSTANCE, 1, 0, 0, 0x3E, 0, 0, 0, 0, 0};
    const std::vector<uint8_t> up{KEYS_MOUSE_HID_INSTANCE, 1, 0, 0, 0, 0, 0, 0, 0, 0};
    CHECK_EQ(g_fake_hid_reports.size(), 1u);
    CHECK(g_fake_hid_reports.back() == f5);
    g_fake_time_us += 1000;
    buttons_on_input();
    buttons_task();
    CHECK_EQ(g_fake_hid_reports.size(), 1u);  // unchanged: nothing sent
    // Pause (0xF6 0x21): all up once, nothing after; 0xE8 shows what would go out.
    const uint8_t bad1[2] = {1, 11}, bad2[2] = {2, 1}, five[2] = {1, 5};
    CHECK_EQ(buttons_pause(bad1, 2), CMD_RESULT_INVALID);
    CHECK_EQ(buttons_pause(bad2, 2), CMD_RESULT_INVALID);
    CHECK_EQ(buttons_pause(five, 1), CMD_RESULT_INVALID);
    CHECK_EQ(buttons_pause(five, 2), CMD_RESULT_OK);
    CHECK(buttons_paused());
    uint8_t id[63]{};
    CHECK_EQ(pico_cmd_get(PICO_CMD_IDENTITY, id, 60), 60);
    CHECK_EQ(id[58], kBuilt);
    CHECK_EQ(id[59] & 0x02, 0x02);
    for (int i = 0; i < 5; i++) {
        g_fake_time_us += 1000;
        buttons_on_input();
        buttons_task();
    }
    CHECK_EQ(g_fake_hid_reports.size(), 2u);
    CHECK(g_fake_hid_reports.back() == up);
    uint8_t b[63];
    buttons_live_get(b, 63);
    CHECK_EQ(b[3] & 0x20, 0x20);
    CHECK_EQ(b[29], 0x3E);  // would have sent F5
    // The pause ends by itself: R1, still down, types F5 again.
    g_fake_time_us += 5000000;
    buttons_on_input();
    buttons_task();
    CHECK(!buttons_paused());
    CHECK(g_fake_hid_reports.back() == f5);
    // No input report for kStaleUs: everything released.
    g_fake_time_us += buttons::kStaleUs;
    buttons_task();
    CHECK(g_fake_hid_reports.back() == up);
    buttons_on_input();
    buttons_task();
    CHECK(g_fake_hid_reports.back() == f5);
    // What runs changes to nothing: all up.
    buttons_run(nullptr, 0xFF);
    buttons_task();
    CHECK(g_fake_hid_reports.back() == up);
    const size_t n = g_fake_hid_reports.size();
    buttons_task();
    CHECK_EQ(g_fake_hid_reports.size(), n);  // and then nothing
    g_fake_tud_mounted = false;
    g_fake_bt_connected = false;
    fake_flash_erase_all();
    config_load();
}
