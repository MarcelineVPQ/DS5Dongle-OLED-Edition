// src/tension.h + src/tension.cpp, Tension (protocol 1.23): the effect bytes
// of every mode against Nielk1's TriggerEffectGenerator revision 6, the stage
// engine (pull and hold transitions, the bow example, hysteresis), Fill in /
// Replace (including a game that toggles its effect quickly), the flash
// record (CRC, layout version, damaged slots) and the 0xF6 / 0xE9 protocol.
//
// The expected bytes below came from a line-by-line Python transliteration of
// Nielk1's C# (rev 6, gist revision f73a299f), floats emulated as 32-bit, not
// from this C++ and not from running the C# itself.
#include <cstring>
#include <vector>
#include "cmd.h"
#include "config.h"
#include "fakes.h"
#include "flash_layout.h"
#include "hardware/flash.h"
#include "tension.h"
#include "test.h"

using namespace tension;

extern uint8_t interrupt_in_data[63];  // fakes.cpp

namespace {
using Bytes = std::vector<uint8_t>;

Bytes enc(uint8_t mode, std::initializer_list<uint8_t> params) {
    uint8_t p[7]{};
    int i = 0;
    for (uint8_t v : params) p[i++] = v;
    CHECK(params_valid(mode, p));
    uint8_t out[kEffectBytes];
    encode(mode, p, out);
    return Bytes(out, out + kEffectBytes);
}
Bytes block(const uint8_t *b) { return Bytes(b, b + kEffectBytes); }
const Bytes kOffBytes = {0x05, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

Stage stage(uint8_t mode, std::initializer_list<uint8_t> params, uint8_t lo, uint8_t hi, uint8_t hold = 0,
            uint8_t ramp = 0, uint8_t to = 0, uint8_t time = 0) {
    Stage s{};
    s.mode = mode;
    int i = 0;
    for (uint8_t v : params) s.p[i++] = v;
    s.pull_min = lo;
    s.pull_max = hi;
    s.hold_min = hold;
    s.ramp = ramp;
    s.ramp_to = to;
    s.ramp_time = time;
    return s;
}

// The bow (owner 2026-11-15): R2 resistance builds to full by 95 % of the
// pull (242 of 255), releases at full pull, then a vibration slowly grows the
// longer it's held. Stages run first-match, so the held stage comes first.
Program bow_r2(uint8_t policy = kReplace) {
    Program pg{};
    pg.count = 3;
    pg.policy = policy;
    pg.stage[0] = stage(kVibration, {0, 1, 30}, 242, 255, 15, 0x80 | 2, 8, 50);  // after 1.5 s: amplitude 1 -> 8 over 5 s
    pg.stage[1] = stage(kOff, {}, 242, 255);                                     // full pull: the tension lets go
    pg.stage[2] = stage(kFeedback, {0, 1}, 0, 241, 0, 2, 8);                     // the draw: strength 1 -> 8 by pull
    return pg;
}

SetBody make_set(const char *name, const Program &l2, const Program &r2) {
    SetBody b{};
    b.ver = kRecordVer;
    std::strncpy(b.name, name, kNameLen);
    b.trig[kL2] = l2;
    b.trig[kR2] = r2;
    return b;
}

// A SetStateData with the game's block on trigger t (allow bit on).
void game_sets(uint8_t *ss, int t, const Bytes &b) {
    ss[0] |= kAllowBit[t];
    std::memcpy(ss + kBlockAt[t], b.data(), kEffectBytes);
}
}  // namespace

// --- Encoding against Nielk1 rev 6 ---------------------------------------

TEST(tension_crc32_is_ieee) {
    const char *s = "123456789";
    CHECK_EQ(crc32_ieee(reinterpret_cast<const uint8_t *>(s), 9), 0xCBF43926u);
}

TEST(tension_encode_official_modes_match_nielk1) {
    CHECK(enc(kOff, {}) == kOffBytes);
    CHECK(enc(kFeedback, {3, 5}) == (Bytes{0x21, 0xF8, 0x03, 0x00, 0x48, 0x92, 0x24, 0, 0, 0, 0}));
    CHECK(enc(kFeedback, {0, 8}) == (Bytes{0x21, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0x3F, 0, 0, 0, 0}));
    CHECK(enc(kFeedback, {4, 0}) == kOffBytes);  // no strength = Off
    CHECK(enc(kWeapon, {3, 5, 8}) == (Bytes{0x25, 0x28, 0x00, 0x07, 0, 0, 0, 0, 0, 0, 0}));
    CHECK(enc(kWeapon, {2, 8, 1}) == (Bytes{0x25, 0x04, 0x01, 0x00, 0, 0, 0, 0, 0, 0, 0}));
    CHECK(enc(kVibration, {0, 8, 30}) == (Bytes{0x26, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0x3F, 0, 0, 0x1E, 0}));
    CHECK(enc(kVibration, {5, 3, 200}) == (Bytes{0x26, 0xE0, 0x03, 0x00, 0x00, 0x49, 0x12, 0, 0, 0xC8, 0}));
    CHECK(enc(kVibration, {5, 3, 0}) == kOffBytes);  // no frequency = Off
    // Multiple Position Feedback, levels 0,1,2,3,4,5,6,7,8,8 (nibbles low first)
    CHECK(enc(kMultiFeedback, {0x10, 0x32, 0x54, 0x76, 0x88}) == (Bytes{0x21, 0xFE, 0x03, 0x40, 0x34, 0xD6, 0x3F, 0, 0, 0, 0}));
    // Multiple Position Vibration, 40 Hz, levels 1,0,3,0,5,0,7,0,8,2
    CHECK(enc(kMultiVibration, {40, 0x01, 0x03, 0x05, 0x07, 0x28}) ==
          (Bytes{0x26, 0x55, 0x03, 0x80, 0x40, 0x18, 0x0F, 0, 0, 0x28, 0}));
}

TEST(tension_slope_feedback_rounds_like_nielk1) {
    // Levels [1,2,3,3,4,5,6,6,7,8]
    CHECK(enc(kSlopeFeedback, {0, 9, 1, 8}) == (Bytes{0x21, 0xFF, 0x03, 0x88, 0x34, 0xB6, 0x3E, 0, 0, 0, 0}));
    // [2,2,3,...]: zone 1 is exactly 2.5 and C#'s Math.Round goes to even (2)
    CHECK(enc(kSlopeFeedback, {0, 2, 2, 3}) == (Bytes{0x21, 0xFF, 0x03, 0x89, 0x24, 0x49, 0x12, 0, 0, 0, 0}));
    // [1,1,1,2,...]: zone 3 is 1.5 (float 1/6 x 3) -> 2
    CHECK(enc(kSlopeFeedback, {0, 6, 1, 2}) == (Bytes{0x21, 0xFF, 0x03, 0x00, 0x92, 0x24, 0x09, 0, 0, 0, 0}));
    // falling: [0,0,8,6,4,3,1,1,1,1]
    CHECK(enc(kSlopeFeedback, {2, 6, 8, 1}) == (Bytes{0x21, 0xFC, 0x03, 0xC0, 0x3B, 0x01, 0x00, 0, 0, 0, 0}));
    CHECK(enc(kSlopeFeedback, {1, 7, 2, 7}) == (Bytes{0x21, 0xFE, 0x03, 0x88, 0x36, 0xD6, 0x36, 0, 0, 0, 0}));
}

TEST(tension_encode_unofficial_modes_match_nielk1) {
    CHECK(enc(kBow, {2, 6, 8, 8}) == (Bytes{0x22, 0x44, 0x00, 0x3F, 0, 0, 0, 0, 0, 0, 0}));
    CHECK(enc(kBow, {0, 8, 3, 5}) == (Bytes{0x22, 0x01, 0x01, 0x22, 0, 0, 0, 0, 0, 0, 0}));
    CHECK(enc(kGalloping, {0, 9, 1, 5, 5}) == (Bytes{0x23, 0x01, 0x02, 0x0D, 0x05, 0, 0, 0, 0, 0, 0}));
    CHECK(enc(kGalloping, {1, 7, 2, 6, 20}) == (Bytes{0x23, 0x82, 0x00, 0x16, 0x14, 0, 0, 0, 0, 0, 0}));
    CHECK(enc(kMachine, {1, 8, 7, 7, 20, 0}) == (Bytes{0x27, 0x02, 0x01, 0x3F, 0x14, 0, 0, 0, 0, 0, 0}));
    CHECK(enc(kMachine, {0, 9, 3, 6, 50, 10}) == (Bytes{0x27, 0x01, 0x02, 0x33, 0x32, 0x0A, 0, 0, 0, 0, 0}));
}

TEST(tension_params_keep_sonys_ranges) {
    auto ok = [](uint8_t mode, std::initializer_list<uint8_t> params) {
        uint8_t p[7]{};
        int i = 0;
        for (uint8_t v : params) p[i++] = v;
        return params_valid(mode, p);
    };
    CHECK(!ok(kFeedback, {10, 1}));
    CHECK(!ok(kFeedback, {0, 9}));
    CHECK(!ok(kFeedback, {0, 1, 1}));  // an unused parameter must be 0
    CHECK(!ok(kWeapon, {1, 5, 8}));    // start 2..7
    CHECK(!ok(kWeapon, {8, 9, 8}));
    CHECK(!ok(kWeapon, {4, 4, 8}));    // end after start
    CHECK(!ok(kSlopeFeedback, {3, 3, 1, 8}));
    CHECK(!ok(kSlopeFeedback, {0, 9, 0, 8}));  // strengths 1..8
    CHECK(!ok(kMultiFeedback, {0x09}));        // a zone above 8
    CHECK(!ok(kBow, {0, 9, 8, 8}));            // end 0..8
    CHECK(!ok(kGalloping, {0, 9, 5, 5, 5}));   // first foot before second
    CHECK(!ok(kMachine, {0, 9, 8, 0, 5, 0}));  // amplitude 0..7
    CHECK(!ok(kModeCount, {}));
    CHECK(!ok(kOff, {1}));
}

// --- The stage engine ------------------------------------------------------

TEST(tension_bow_builds_releases_then_grows_with_hold) {
    const Program pg = bow_r2();
    Run r;
    run_step(r, pg, 0, 1000);
    CHECK_EQ(r.stage, 2);
    CHECK(block(r.want) == enc(kFeedback, {0, 1}));
    run_step(r, pg, 120, 1100);  // 1 + 7 x 120 / 241 = 4.49 -> 4
    CHECK(block(r.want) == enc(kFeedback, {0, 4}));
    run_step(r, pg, 241, 1200);  // full by 95 %
    CHECK(block(r.want) == enc(kFeedback, {0, 8}));
    run_step(r, pg, 250, 2000);  // full pull: released
    CHECK_EQ(r.stage, 1);
    CHECK(block(r.want) == kOffBytes);
    run_step(r, pg, 255, 3499);  // not yet held 1.5 s
    CHECK_EQ(r.stage, 1);
    run_step(r, pg, 255, 3500);  // held 1.5 s: the vibration starts at amplitude 1
    CHECK_EQ(r.stage, 0);
    CHECK(block(r.want) == enc(kVibration, {0, 1, 30}));
    run_step(r, pg, 255, 6000);  // half way through the 5 s ramp: 1 + 3.5 -> 5
    CHECK(block(r.want) == enc(kVibration, {0, 5, 30}));
    CHECK_EQ(r.held_tenths, 40);
    run_step(r, pg, 255, 20000);  // past the ramp: stays at 8
    CHECK(block(r.want) == enc(kVibration, {0, 8, 30}));
}

TEST(tension_stage_edges_have_hysteresis_and_restart) {
    const Program pg = bow_r2();
    Run r;
    run_step(r, pg, 250, 1000);
    run_step(r, pg, 250, 2600);
    CHECK_EQ(r.stage, 0);
    run_step(r, pg, 240, 2700);  // within 3 of the window: still held, timer kept
    CHECK_EQ(r.stage, 0);
    run_step(r, pg, 238, 2800);  // out: back to the draw
    CHECK_EQ(r.stage, 2);
    CHECK(block(r.want) == enc(kFeedback, {0, 8}));  // 1 + 7 x 238 / 241 = 7.9 -> 8
    run_step(r, pg, 250, 3000);  // pulled again: the hold starts over
    CHECK_EQ(r.stage, 1);
    run_step(r, pg, 250, 4000);
    CHECK_EQ(r.stage, 1);
    run_step(r, pg, 0, 5000);
    CHECK_EQ(r.stage, 2);
}

TEST(tension_no_matching_stage_is_off) {
    Program pg{};
    pg.count = 1;
    pg.stage[0] = stage(kWeapon, {2, 6, 8}, 100, 200);
    Run r;
    run_step(r, pg, 50, 1);
    CHECK_EQ(r.stage, -1);
    CHECK(block(r.want) == kOffBytes);
    run_step(r, pg, 150, 2);
    CHECK(block(r.want) == enc(kWeapon, {2, 6, 8}));
}

// --- Fill in / Replace ------------------------------------------------------

namespace {
struct Rig {
    Engine e;
    Program prog;
    const Program *pg[2] = {nullptr, nullptr};
    explicit Rig(const Program &p) : prog(p) {
        pg[kR2] = &prog;
        run_step(e.run[kR2], prog, 0, 0);
    }
    // A game report at `now`; returns its SetStateData after the merge.
    Bytes report(uint32_t now, const Bytes *r2 = nullptr) {
        uint8_t ss[47]{};
        if (r2) game_sets(ss, kR2, *r2);
        merge(e, pg, ss, now);
        return Bytes(ss, ss + 47);
    }
    bool own(uint32_t now, Bytes *out = nullptr) {
        uint8_t ss[47];
        const bool sent = own_report(e, pg, ss, now);
        if (sent && out) *out = Bytes(ss, ss + 47);
        return sent;
    }
};
Program steady(uint8_t policy) {
    Program p{};
    p.count = 1;
    p.policy = policy;
    p.stage[0] = stage(kFeedback, {3, 5}, 0, 255);
    return p;
}
const Bytes kGameWeapon = enc(kWeapon, {2, 6, 8});
Bytes r2_of(const Bytes &ss) { return Bytes(ss.begin() + kBlockAt[kR2], ss.begin() + kBlockAt[kR2] + kEffectBytes); }
}  // namespace

TEST(tension_nothing_running_leaves_reports_untouched) {
    Engine e;
    const Program *pg[2] = {nullptr, nullptr};
    uint8_t ss[47]{};
    game_sets(ss, kR2, kGameWeapon);
    ss[0] |= 0x03;  // rumble bits
    ss[2] = 0x40;
    uint8_t before[47];
    std::memcpy(before, ss, sizeof(ss));
    CHECK_EQ(merge(e, pg, ss, 10), 0);
    CHECK(std::memcmp(before, ss, sizeof(ss)) == 0);
    uint8_t own[47];
    CHECK(!own_report(e, pg, own, 1000));
}

TEST(tension_fill_in_takes_a_quiet_trigger_and_yields_to_the_game) {
    Rig g(steady(kFillIn));
    const Bytes ours = enc(kFeedback, {3, 5});
    Bytes ss;
    CHECK(g.own(100, &ss));  // the game never set R2: ours at once
    CHECK(ss[0] & kAllowBit[kR2]);
    CHECK(!(ss[0] & kAllowBit[kL2]));  // L2 untouched
    CHECK(r2_of(ss) == ours);
    CHECK(!g.own(200));  // already on the controller: no re-send (the re-fire buzz, #11)
    // A game report without trigger bits passes untouched.
    ss = g.report(250);
    CHECK_EQ(ss[0], 0);
    // The game sets an effect: it wins at once, in its own report.
    ss = g.report(300, &kGameWeapon);
    CHECK(r2_of(ss) == kGameWeapon);
    CHECK_EQ(g.e.own[kR2], kOwnerGame);
    CHECK(!g.own(350));
    // It clears it: ours only after the hold-off.
    ss = g.report(400, &kOffBytes);
    CHECK(r2_of(ss) == kOffBytes);
    CHECK_EQ(g.e.own[kR2], kOwnerWaiting);
    CHECK(!g.own(899));
    CHECK(g.own(900, &ss));
    CHECK(r2_of(ss) == ours);
}

TEST(tension_fill_in_doesnt_flicker_with_a_game_that_toggles) {
    Rig g(steady(kFillIn));
    CHECK(g.own(0 + 100));
    uint32_t t = 1000;
    int ours_seen = 0;
    // 2 s of a game setting and clearing its effect every 60 ms (~16 per
    // second), with Tension's own loop running every 5 ms in between.
    for (int i = 0; i < 34; i++, t += 60) {
        const Bytes ss = g.report(t, (i & 1) ? &kOffBytes : &kGameWeapon);
        if (r2_of(ss) == enc(kFeedback, {3, 5})) ours_seen++;
        for (uint32_t d = 5; d < 60; d += 5) ours_seen += g.own(t + d) ? 1 : 0;
    }
    CHECK_EQ(ours_seen, 0);
    // The last report (i = 33) cleared it at t - 60: ours 500 ms after that.
    const uint32_t cleared = t - 60;
    CHECK(!g.own(cleared + 499));
    CHECK(g.own(cleared + 500));
}

TEST(tension_replace_overrides_the_game) {
    Rig g(steady(kReplace));
    const Bytes ours = enc(kFeedback, {3, 5});
    const Bytes ss = g.report(10, &kGameWeapon);
    CHECK(ss[0] & kAllowBit[kR2]);
    CHECK(r2_of(ss) == ours);
    CHECK_EQ(g.e.own[kR2], kOwnerOurs);
    // The game's own effect is remembered for when the set stops.
    g.pg[kR2] = nullptr;
    Bytes back;
    CHECK(g.own(100, &back));
    CHECK(r2_of(back) == kGameWeapon);
    CHECK(!g.own(200));
}

TEST(tension_gives_back_off_when_the_game_never_set_one) {
    Rig g(steady(kReplace));
    CHECK(g.own(100));
    g.pg[kR2] = nullptr;
    Bytes back;
    CHECK(g.own(200, &back));
    CHECK(r2_of(back) == kOffBytes);
}

TEST(tension_resends_after_a_lost_report_and_a_new_link) {
    Rig g(steady(kReplace));
    uint8_t ss[47]{};
    const uint8_t wrote = merge(g.e, g.pg, ss, 10);
    CHECK_EQ(wrote, 2);
    report_lost(g.e, wrote);  // the Bluetooth queue was full
    CHECK(g.own(100));
    CHECK(!g.own(200));
    forget_controller(g.e);  // a new link
    CHECK(g.own(300));
}

TEST(tension_own_reports_are_rate_limited) {
    Program p{};
    p.count = 1;
    p.policy = kReplace;
    p.stage[0] = stage(kVibration, {0, 1, 30}, 0, 255, 0, 0x80 | 2, 8, 1);  // ramps every few ms
    Rig g(p);
    CHECK(g.own(1000));
    run_step(g.e.run[kR2], g.prog, 0, 1005);  // past the 0.1 s ramp: amplitude 8
    CHECK(!g.own(1010));  // 10 ms after the last: wait
    CHECK(g.own(1020));
}

TEST(tension_pause_stands_aside) {
    Rig g(steady(kReplace));
    g.e.paused = true;
    const Bytes ss = g.report(10, &kGameWeapon);
    CHECK(r2_of(ss) == kGameWeapon);
    CHECK(!g.own(100));
    g.e.paused = false;
    forget_controller(g.e);
    CHECK(g.own(200));
}

// --- Validation and the flash record -------------------------------------

TEST(tension_set_validation) {
    const Program none{};
    SetBody b = make_set("Bow", none, bow_r2());
    CHECK(set_valid(b));
    SetBody t = b;
    t.name[1] = 0x7F;
    CHECK(!set_valid(t));
    t = b;
    t.name[0] = 0;
    CHECK(!set_valid(t));  // a name is needed
    t = b;
    t.name[5] = 'x';  // after the NUL
    CHECK(!set_valid(t));
    t = b;
    t.trig[kR2].stage[3].mode = kFeedback;  // past count: must be zero
    CHECK(!set_valid(t));
    t = b;
    t.trig[kR2].policy = 2;
    CHECK(!set_valid(t));
    t = b;
    t.trig[kR2].stage[2].ramp_to = 9;  // ramps Feedback strength to 9
    CHECK(!set_valid(t));
    t = b;
    t.trig[kR2].stage[0].ramp_time = 0;  // a ramp by hold needs a time
    CHECK(!set_valid(t));
    t = b;
    t.trig[kR2].stage[2].ramp = 0x08;  // reserved bits
    CHECK(!set_valid(t));
    t = b;
    t.trig[kR2].count = 5;
    CHECK(!set_valid(t));
    t = b;
    t.ver = 2;
    CHECK(!set_valid(t));
}

TEST(tension_store_round_trip_crc_and_version) {
    alignas(4) static uint8_t img[kStoreBytes];
    Store s;
    store_empty(s);
    s.slot[1].used = 1;
    s.slot[1].body = make_set("Bow", Program{}, bow_r2());
    s.slot[5].used = 1;
    s.slot[5].body = make_set("Rifle", Program{}, steady(kFillIn));
    s.h.active = 5;
    store_seal(s);
    std::memset(img, 0xFF, sizeof(img));
    std::memcpy(img, &s, sizeof(s));

    Store l;
    CHECK(store_load(img, l) == Load::Ok);
    CHECK(std::memcmp(&l, &s, sizeof(s)) == 0);

    // A damaged slot reads as empty, the others stay; the choice pointing at
    // it is dropped.
    img[12 + 5 * 139 + 5 + 3] ^= 0x01;  // a name byte of slot 5
    CHECK(store_load(img, l) == Load::Ok);
    CHECK_EQ(l.slot[5].used, 0);
    CHECK_EQ(l.slot[1].used, 1);
    CHECK_EQ(l.h.active, kNone);
    img[12 + 5 * 139 + 5 + 3] ^= 0x01;

    // A damaged header keeps the sets, not the choice.
    img[6] = 1;  // active = 1 without a new CRC
    CHECK(store_load(img, l) == Load::Ok);
    CHECK_EQ(l.slot[1].used, 1);
    CHECK_EQ(l.h.active, kNone);
    img[6] = 5;

    img[4] = kStoreLayout + 1;  // saved by a newer firmware
    CHECK(store_load(img, l) == Load::Newer);
    CHECK_EQ(l.slot[1].used, 0);
    img[4] = kStoreLayout;

    std::memset(img, 0xFF, sizeof(img));  // erased flash
    CHECK(store_load(img, l) == Load::Empty);
    CHECK_EQ(l.h.active, kNone);
}

// --- The protocol (0xF6 0x1C..0x20, GET 0xE9) -----------------------------

namespace {
void settle();
void fresh() {
    settle();  // nothing left pending by an earlier test
    fake_flash_erase_all();
    g_fake_flash_fail = false;
    g_fake_bt_write_fail = false;
    g_fake_bt_connected = true;
    config_load();
    tension_load();
    g_fake_bt_writes.clear();
    std::memset(interrupt_in_data, 0, 63);
}
uint8_t last_result() {
    uint8_t id[46];
    pico_cmd_get(PICO_CMD_IDENTITY, id, sizeof(id));
    return id[11];
}
void settle() {
    for (int i = 0; i < 3; i++) {
        g_fake_time_us += 100'000;
        cmd_task();
    }
}
void set(std::initializer_list<uint8_t> bytes) {
    const Bytes b(bytes);
    pico_cmd_set(PICO_CMD_SET, b.data(), (uint16_t) b.size());
}
// Stages `body` with 0x1C chunks of 60 bytes, as the app does; its CRC-32.
uint32_t stage_set(const SetBody &body) {
    const auto *p = reinterpret_cast<const uint8_t *>(&body);
    for (uint16_t at = 0; at < sizeof(body); at += 60) {
        const uint16_t n = sizeof(body) - at < 60 ? sizeof(body) - at : 60;
        uint8_t m[63] = {0x1C, kWireVer, (uint8_t) at};
        std::memcpy(m + 3, p + at, n);
        pico_cmd_set(PICO_CMD_SET, m, (uint16_t) (3 + n));
        CHECK_EQ(last_result(), CMD_RESULT_OK);
    }
    return crc32_ieee(p, sizeof(body));
}
void with_crc(uint8_t func, uint8_t b1, uint32_t crc, bool hid = false) {
    uint8_t m[7] = {func, kWireVer, b1};
    std::memcpy(m + 3, &crc, 4);
    if (hid) pico_cmd_set_hid(PICO_CMD_SET, m, sizeof(m));
    else pico_cmd_set(PICO_CMD_SET, m, sizeof(m));
}
}  // namespace

TEST(tension_identity_bit_and_minor) {
    fresh();
    uint8_t id[58]{};
    pico_cmd_get(PICO_CMD_IDENTITY, id, sizeof(id));
    CHECK_EQ(id[5], 23);
    CHECK(id[49] & 0x80);
}

TEST(tension_save_read_back_and_survive_a_restart) {
    fresh();
    const SetBody bow = make_set("Bow", Program{}, bow_r2());
    const uint32_t crc = stage_set(bow);
    with_crc(0x1D, 2, crc);
    CHECK_EQ(last_result(), CMD_RESULT_PENDING);
    settle();
    CHECK_EQ(last_result(), CMD_RESULT_OK);

    uint8_t r[63];
    CHECK_EQ(tension_get(0x01, r, 63), 38);
    CHECK_EQ(r[2], 0x04);  // slot 2 used
    uint32_t listed;
    std::memcpy(&listed, r + 6 + 4 * 2, 4);
    CHECK_EQ(listed, crc);

    // Read it back in three chunks.
    uint8_t back[sizeof(SetBody)];
    for (uint8_t c = 0; c < 3; c++) {
        const uint16_t n = tension_get((uint8_t) (0x40 | 2 << 2 | c), r, 63);
        CHECK_EQ(n, c < 2 ? 60 : 4 + 22);
        CHECK_EQ(r[1], 2);
        CHECK_EQ(r[3], 1);
        std::memcpy(back + c * 56, r + 4, n - 4);
    }
    CHECK(std::memcmp(back, &bow, sizeof(bow)) == 0);

    // The sector holds it (survives a restart).
    tension_load();
    CHECK_EQ(tension_get(0x01, r, 63), 38);
    CHECK_EQ(r[2], 0x04);
    Store s;
    CHECK(store_load(reinterpret_cast<const uint8_t *>(XIP_BASE + TENSION_FLASH_OFFSET), s) == Load::Ok);
    CHECK(std::memcmp(&s.slot[2].body, &bow, sizeof(bow)) == 0);
}

TEST(tension_rejects_bad_crc_bad_sets_and_hid_writes) {
    fresh();
    const uint32_t crc = stage_set(make_set("Bow", Program{}, bow_r2()));
    with_crc(0x1D, 0, crc ^ 1);
    CHECK_EQ(last_result(), CMD_RESULT_INVALID);
    with_crc(0x1D, 8, crc);  // slot 0..7
    CHECK_EQ(last_result(), CMD_RESULT_INVALID);
    with_crc(0x1D, 0, crc, true);  // the game-facing HID interface can't overwrite sets
    CHECK_EQ(last_result(), CMD_RESULT_INVALID);
    set({0x1C, kWireVer, 130, 1, 2, 3, 4, 5});  // past the record's end
    CHECK_EQ(last_result(), CMD_RESULT_INVALID);
    SetBody bad = make_set("Bow", Program{}, bow_r2());
    bad.trig[kR2].stage[0].mode = 12;
    with_crc(0x1D, 0, stage_set(bad));
    CHECK_EQ(last_result(), CMD_RESULT_INVALID);
}

TEST(tension_choose_run_and_send) {
    fresh();
    with_crc(0x1D, 3, stage_set(make_set("Bow", Program{}, bow_r2())));
    settle();
    set({0x20, kWireVer, 4, 1});  // an empty slot
    CHECK_EQ(last_result(), CMD_RESULT_INVALID);
    set({0x20, kWireVer, 3, 1});  // for now (a game's profile)
    CHECK_EQ(last_result(), CMD_RESULT_OK);
    CHECK(std::strcmp(tension_running_name(), "Bow") == 0);
    uint8_t r[63];
    CHECK_EQ(tension_get(0x00, r, 63), 38);
    CHECK(r[0] == 'T' && r[1] == 'N');
    CHECK_EQ(r[3] & 0x05, 0x05);  // running, chosen for now
    CHECK_EQ(r[4], kNone);        // nothing saved
    CHECK_EQ(r[5], 3);

    // The board sends the R2 draw itself (Replace); L2 untouched.
    g_fake_time_us += 1'000'000;
    tension_task();
    tension_task();
    CHECK_EQ(g_fake_bt_writes.size(), 1u);
    const Bytes &pkt = g_fake_bt_writes[0];
    CHECK_EQ(pkt[0], 0x31);
    CHECK_EQ(pkt[3], kAllowBit[kR2]);
    CHECK(Bytes(pkt.begin() + 3 + kBlockAt[kR2], pkt.begin() + 3 + kBlockAt[kR2] + 11) == enc(kFeedback, {0, 1}));
    tension_get(0x00, r, 63);
    CHECK_EQ(r[8 + 15 + 0], kOwnerOurs);  // R2
    CHECK_EQ(r[8 + 15 + 1], 2);           // the draw stage
    CHECK_EQ(r[8 + 0], kOwnerNone);       // L2

    // A restart forgets a temporary choice.
    tension_load();
    CHECK(tension_running_name()[0] == 0);
    // A saved one stays.
    set({0x20, kWireVer, 3, 0});
    CHECK_EQ(last_result(), CMD_RESULT_PENDING);
    settle();
    CHECK_EQ(last_result(), CMD_RESULT_OK);
    tension_load();
    CHECK(std::strcmp(tension_running_name(), "Bow") == 0);
    // Deleting it empties the slot and the choice.
    set({0x1F, kWireVer, 3});
    settle();
    CHECK_EQ(last_result(), CMD_RESULT_OK);
    tension_get(0x01, r, 63);
    CHECK_EQ(r[2], 0);
    CHECK_EQ(r[3], kNone);
    CHECK(tension_running_name()[0] == 0);
}

TEST(tension_feel_it_runs_without_saving) {
    fresh();
    const uint32_t crc = stage_set(make_set("Try", Program{}, steady(kReplace)));
    with_crc(0x1E, 1, crc);
    CHECK_EQ(last_result(), CMD_RESULT_OK);
    CHECK(std::strcmp(tension_running_name(), "Feel it") == 0);
    uint8_t r[63];
    tension_get(0x01, r, 63);
    CHECK_EQ(r[2], 0);  // nothing saved
    set({0x1E, kWireVer, 0});
    CHECK_EQ(last_result(), CMD_RESULT_OK);
    CHECK(tension_running_name()[0] == 0);
}

TEST(tension_failed_save_changes_nothing) {
    fresh();
    const uint32_t crc = stage_set(make_set("Bow", Program{}, bow_r2()));
    g_fake_flash_fail = true;
    with_crc(0x1D, 0, crc);
    settle();
    CHECK_EQ(last_result(), CMD_RESULT_FAILED);
    uint8_t r[63];
    tension_get(0x01, r, 63);
    CHECK_EQ(r[2], 0);
}

TEST(tension_newer_store_is_never_written_over) {
    fresh();
    with_crc(0x1D, 0, stage_set(make_set("Bow", Program{}, bow_r2())));
    settle();
    // Pretend a newer firmware saved the sector.
    g_fake_flash[TENSION_FLASH_OFFSET + 4] = kStoreLayout + 1;
    tension_load();
    uint8_t r[63];
    tension_get(0x00, r, 63);
    CHECK(r[3] & 0x08);
    with_crc(0x1D, 1, stage_set(make_set("Rifle", Program{}, steady(kFillIn))));
    CHECK_EQ(last_result(), CMD_RESULT_FAILED);
    set({0x1F, kWireVer, 0});
    CHECK_EQ(last_result(), CMD_RESULT_FAILED);
    CHECK_EQ(g_fake_flash[TENSION_FLASH_OFFSET + 4], kStoreLayout + 1);
}

TEST(tension_sector_sits_below_calibration) {
    CHECK_EQ(TENSION_FLASH_OFFSET + FLASH_SECTOR_SIZE, CALIB_FLASH_OFFSET);
    CHECK_EQ(RESERVED_FLASH_OFFSET, TENSION_FLASH_OFFSET);
    CHECK(kStoreBytes <= FLASH_SECTOR_SIZE);
}
