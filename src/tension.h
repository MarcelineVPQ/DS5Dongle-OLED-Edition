//
// Tension (protocol 1.23): your own adaptive trigger effects, run by the board.
// Design and wire layout: docs/TENSION.md.
//
// A set (e.g. "Bow") holds a small program per trigger: up to kStages stages,
// each one of the DualSense's trigger effects plus a condition on the pull
// position (input report bytes [4] L2 / [5] R2) and the time held there. The
// board runs the program (it sees every input report), switches effects
// mid-pull (the trigger runs one effect at a time) and can ramp one parameter
// of the running effect by pull position or by time held. Per trigger the set
// says Fill in (ours only while the game has no effect on that trigger) or
// Replace (always ours).
//
// Everything in namespace tension is pure (no SDK): host-tested in
// tests/host/test_tension.cpp. The board glue (flash, commands, main loop)
// is src/tension.cpp, declared at the end of this file.
//
// The effect encodings follow Nielk1's TriggerEffectGenerator, revision 6
// (https://gist.github.com/Nielk1/6d54cc2c00d2201ccb8c2720ad7538db, newest
// revision f73a299f, 2022-10-07), adapted to C++:
//   MIT License, Copyright (c) 2021-2022 John "Nielk1" Klein.
//   Permission is hereby granted, free of charge, to any person obtaining a
//   copy of this software and associated documentation files (the
//   "Software"), to deal in the Software without restriction, including
//   without limitation the rights to use, copy, modify, merge, publish,
//   distribute, sublicense, and/or sell copies of the Software, and to permit
//   persons to whom the Software is furnished to do so, subject to the
//   following conditions: The above copyright notice and this permission
//   notice shall be included in all copies or substantial portions of the
//   Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
//

#ifndef DS5_BRIDGE_TENSION_H
#define DS5_BRIDGE_TENSION_H

#include <cstdint>
#include <cstring>

#include "buttons.h"

namespace tension {

constexpr uint8_t kWireVer = 1;    // the 0xF6 0x1C..0x20 framing and the 0xE9 replies
constexpr uint8_t kWireVer2 = 2;   // proto 1.24: 0x1C / 0x1D / 0x1E with the buttons, 0xE9 sel 0x80
constexpr uint8_t kRecordVer = 1;  // SetBody::ver
constexpr int kStages = 4;         // per trigger
constexpr int kSlots = 8;          // saved sets on the board
constexpr int kNameLen = 16;       // ASCII 0x20..0x7E, NUL-padded
constexpr int kEffectBytes = 11;   // a trigger's block in SetStateData: mode + 10 bytes
constexpr int kL2 = 0, kR2 = 1;
constexpr uint8_t kNone = 0xFF;    // no set

// Where each trigger's block sits in SetStateData (src/utils.h) and its allow
// bit in valid_flag0 [0]: 0x04 right, 0x08 left.
constexpr int kBlockAt[2] = {21, 10};
constexpr uint8_t kAllowBit[2] = {0x08, 0x04};

// Our effect ids (not the wire's mode bytes). 7..9 are unofficial: left in
// the controller's firmware, not in Sony's API (Nielk1: "might be removed in
// the future"); the app labels them so.
enum Mode : uint8_t {
    kOff = 0, kFeedback = 1, kWeapon = 2, kVibration = 3, kMultiFeedback = 4,
    kSlopeFeedback = 5, kMultiVibration = 6, kBow = 7, kGalloping = 8, kMachine = 9,
    kModeCount = 10,
};
enum Policy : uint8_t { kFillIn = 0, kReplace = 1 };

// The ramp byte: bits 0..2 = parameter index + 1 (0 = no ramp), bits 3..6
// zero, bit 7 the source (0 = pull across the stage's pull window, 1 = time
// held past hold_min, over ramp_time).
constexpr uint8_t kRampIdxMask = 0x07, kRampByHold = 0x80;

struct __attribute__((packed)) Stage {
    uint8_t mode;       // Mode
    uint8_t p[7];       // the effect's parameters (docs/TENSION.md table), unused = 0
    uint8_t pull_min;   // the stage's pull window, 0..255 as calibrated (calib_trigger), min <= max
    uint8_t pull_max;
    uint8_t hold_min;   // tenths of a second inside the window before the stage runs
    uint8_t ramp;       // kRampIdxMask | kRampByHold
    uint8_t ramp_to;    // the ramped parameter's value at the end of the ramp
    uint8_t ramp_time;  // tenths of a second (ramp by hold only, >= 1)
};
static_assert(sizeof(Stage) == 14, "Stage is 14 bytes on the wire");

struct __attribute__((packed)) Program {
    uint8_t count;   // stages in use, 0 = this set leaves the trigger to the game
    uint8_t policy;  // Policy
    Stage stage[kStages];  // stages past count are all zero
};
static_assert(sizeof(Program) == 58, "Program is 58 bytes on the wire");

struct __attribute__((packed)) SetBody {
    uint8_t ver;    // kRecordVer
    uint8_t flags;  // 0 (reserved)
    char name[kNameLen];
    Program trig[2];  // kL2, kR2
};
static_assert(sizeof(SetBody) == 134, "SetBody is 134 bytes on the wire and in flash");
// Proto 1.24: what the app stages (0x1C ver 2) and commits: the SetBody, then
// the set's ButtonPart (src/buttons.h), 494 bytes; the wire bytes are the flash bytes.
constexpr unsigned kStagedBytes = sizeof(SetBody) + sizeof(buttons::ButtonPart);
static_assert(kStagedBytes == 494, "docs/TENSION_BUTTONS.md section 3.1");

// --- CRC-32 (IEEE 802.3 / zlib: reflected 0xEDB88320, init and final xor
// 0xFFFFFFFF; "123456789" -> 0xCBF43926). Used for the staged set the app
// commits and each saved slot; rare (a save, a load), so bitwise.
// The running CRC state over more bytes (start 0xFFFFFFFF, finish with ~).
inline uint32_t crc32_update(uint32_t c, const uint8_t *d, uint32_t n) {
    // loop: NOT-WORTH — ≤ 1128 bytes, on a commit / save / load (never per report)
    for (uint32_t i = 0; i < n; i++) {
        c ^= d[i];
        // loop: NOT-WORTH — 8 bits per byte, on a commit / save / load
        for (int b = 0; b < 8; b++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return c;
}
inline uint32_t crc32_ieee(const uint8_t *d, uint32_t n) { return ~crc32_update(0xFFFFFFFFu, d, n); }

// --- Effect parameters and encoding (Nielk1 rev 6) -------------------------

// How many of p[0..6] each mode uses; the rest must be 0.
constexpr uint8_t kParamCount[kModeCount] = {0, 2, 3, 3, 5, 4, 6, 4, 5, 6};

// Multiple Position Feedback / Vibration pack their ten 0..8 levels in
// nibbles: zone 2i in the low nibble of a byte, zone 2i+1 in the high one.
inline uint8_t zone_level(const uint8_t *packed, int zone) {
    return (uint8_t) ((packed[zone / 2] >> (4 * (zone & 1))) & 0x0F);
}
inline bool zones_valid(const uint8_t *packed) {
    bool ok = true;
    // loop: HOT-PATH-OK — N = 10 zones, validating a set (not per report)
    for (int z = 0; z < 10; z++) ok = ok && zone_level(packed, z) <= 8;
    return ok;
}

// Sony's ranges (isteamdualsense.h via Nielk1 rev 6's checks).
inline bool params_valid(uint8_t mode, const uint8_t *p) {
    if (mode >= kModeCount) return false;  // error: SHOWN — CMD_RESULT_INVALID in 0xF1 (set_valid's caller)
    bool unused_zero = true;
    // loop: HOT-PATH-OK — N ≤ 7 parameters
    for (int i = kParamCount[mode]; i < 7; i++) unused_zero = unused_zero && p[i] == 0;
    switch (mode) {
        case kOff: return unused_zero;
        case kFeedback: return unused_zero && p[0] <= 9 && p[1] <= 8;
        case kWeapon: return unused_zero && p[0] >= 2 && p[0] <= 7 && p[1] <= 8 && p[1] > p[0] && p[2] <= 8;
        case kVibration: return unused_zero && p[0] <= 9 && p[1] <= 8;
        case kMultiFeedback: return unused_zero && zones_valid(p);
        case kSlopeFeedback:
            return unused_zero && p[0] <= 8 && p[1] <= 9 && p[1] > p[0] && p[2] >= 1 && p[2] <= 8 && p[3] >= 1 && p[3] <= 8;
        case kMultiVibration: return unused_zero && zones_valid(p + 1);
        case kBow: return unused_zero && p[0] <= 8 && p[1] <= 8 && p[0] < p[1] && p[2] <= 8 && p[3] <= 8;
        case kGalloping: return unused_zero && p[0] <= 8 && p[1] <= 9 && p[0] < p[1] && p[3] <= 7 && p[2] <= 6 && p[2] < p[3];
        default: /* kMachine */ return unused_zero && p[0] <= 8 && p[1] <= 9 && p[1] > p[0] && p[2] <= 7 && p[3] <= 7;
    }
}

// A parameter a stage may ramp: any scalar one (not the packed zone levels).
inline bool rampable(uint8_t mode, int idx) {
    if (mode >= kModeCount) return false;  // error: NOT-WORTH — set_valid refuses the stage (params_valid also checks the mode)
    if (mode == kMultiFeedback) return false;  // error: NOT-WORTH — a yes/no answer for set_valid, not a failure
    if (mode == kMultiVibration) return idx == 0;
    return idx < kParamCount[mode];
}

inline void put_off(uint8_t *out) {
    memset(out, 0, kEffectBytes);
    out[0] = 0x05;
}
inline void put_zones(uint8_t *out, uint8_t wire_mode, uint16_t active, uint32_t levels) {
    out[0] = wire_mode;
    out[1] = (uint8_t) (active & 0xFF);
    out[2] = (uint8_t) (active >> 8);
    out[3] = (uint8_t) (levels & 0xFF);
    out[4] = (uint8_t) ((levels >> 8) & 0xFF);
    out[5] = (uint8_t) ((levels >> 16) & 0xFF);
    out[6] = (uint8_t) ((levels >> 24) & 0xFF);
}
// Ten 0..8 levels as Nielk1's MultiplePosition*: a zone is active when > 0,
// its 3-bit field = level - 1. False when every level is 0 (then: Off).
inline bool pack_levels(const uint8_t *lv, uint16_t &active, uint32_t &levels) {
    active = 0;
    levels = 0;
    // loop: HOT-PATH-OK — N = 10 zones, encoding one effect
    for (int i = 0; i < 10; i++) {
        if (lv[i] == 0) continue;
        levels |= (uint32_t) ((lv[i] - 1) & 0x07) << (3 * i);
        active |= (uint16_t) (1u << i);
    }
    return active != 0;
}
// round(n / d), halves to even (C#'s Math.Round, which Nielk1's SlopeFeedback
// uses); n >= 0, d > 0. On SlopeFeedback's inputs the float math Nielk1 does
// lands on the same values (docs/TENSION.md, "Encoding").
inline int round_half_even(int n, int d) {
    const int q = n / d, r = n % d;
    if (2 * r > d) return q + 1;
    if (2 * r == d) return q + (q & 1);
    return q;
}

// The 11 bytes of one effect. Parameters must have passed params_valid; an
// effect with no force (strength 0, frequency 0, every zone 0) is Off, as in
// Nielk1's generator.
inline void encode(uint8_t mode, const uint8_t *p, uint8_t *out) {
    put_off(out);
    switch (mode) {
        case kFeedback:
        case kVibration: {
            // error: NOT-WORTH — no force: the effect is Off (Nielk1), not a failure
            if (p[1] == 0 || (mode == kVibration && p[2] == 0)) return;
            uint8_t lv[10]{};
            // loop: HOT-PATH-OK — N ≤ 10 zones, encoding one effect
            for (int i = p[0]; i < 10; i++) lv[i] = p[1];
            uint16_t a;
            uint32_t l;
            // error: NOT-WORTH — the strength is > 0 (checked above), so a zone is always active
            pack_levels(lv, a, l);
            put_zones(out, mode == kFeedback ? 0x21 : 0x26, a, l);
            if (mode == kVibration) out[9] = p[2];
            return;
        }
        case kWeapon:
            // error: NOT-WORTH — no force: the effect is Off (Nielk1), not a failure
            if (p[2] == 0) return;
            put_zones(out, 0x25, (uint16_t) ((1u << p[0]) | (1u << p[1])), 0);
            out[3] = (uint8_t) (p[2] - 1);
            return;
        case kMultiFeedback:
        case kSlopeFeedback:
        case kMultiVibration: {
            uint8_t lv[10]{};
            if (mode == kSlopeFeedback) {
                const int sp = p[0], ep = p[1], ss = p[2], es = p[3], d = ep - sp;
                // loop: HOT-PATH-OK — N ≤ 10 zones, encoding one effect
                for (int i = sp; i < 10; i++) {
                    if (i > ep) { lv[i] = (uint8_t) es; continue; }
                    const int diff = es - ss, k = i - sp;
                    // ss + diff*k/d, rounded; diff may be negative, the value never is
                    lv[i] = (uint8_t) round_half_even(ss * d + diff * k, d);
                }
            } else {
                const uint8_t *packed = mode == kMultiVibration ? p + 1 : p;
                // loop: HOT-PATH-OK — N = 10 zones, encoding one effect
                for (int i = 0; i < 10; i++) lv[i] = zone_level(packed, i);
            }
            uint16_t a;
            uint32_t l;
            // error: NOT-WORTH — every zone 0 (or no frequency): the effect is Off (Nielk1), not a failure
            if (!pack_levels(lv, a, l) || (mode == kMultiVibration && p[0] == 0)) return;
            put_zones(out, mode == kMultiVibration ? 0x26 : 0x21, a, l);
            if (mode == kMultiVibration) out[9] = p[0];
            return;
        }
        case kBow:
            // error: NOT-WORTH — no force: the effect is Off (Nielk1), not a failure
            if (p[2] == 0 || p[3] == 0) return;
            put_zones(out, 0x22, (uint16_t) ((1u << p[0]) | (1u << p[1])), 0);
            out[3] = (uint8_t) (((p[2] - 1) & 0x07) | (((p[3] - 1) & 0x07) << 3));
            return;
        case kGalloping:
            // error: NOT-WORTH — no frequency: the effect is Off (Nielk1), not a failure
            if (p[4] == 0) return;
            put_zones(out, 0x23, (uint16_t) ((1u << p[0]) | (1u << p[1])), 0);
            out[3] = (uint8_t) ((p[3] & 0x07) | ((p[2] & 0x07) << 3));
            out[4] = p[4];
            return;
        case kMachine:
            // error: NOT-WORTH — no frequency: the effect is Off (Nielk1), not a failure
            if (p[4] == 0) return;
            put_zones(out, 0x27, (uint16_t) ((1u << p[0]) | (1u << p[1])), 0);
            out[3] = (uint8_t) ((p[2] & 0x07) | ((p[3] & 0x07) << 3));
            out[4] = p[4];
            out[5] = p[5];
            return;
        default:  // kOff
            return;
    }
}

// --- Validation of a whole set ----------------------------------------------

inline bool stage_zero(const Stage &s) {
    const auto *b = reinterpret_cast<const uint8_t *>(&s);
    bool z = true;
    // loop: HOT-PATH-OK — N = 14 bytes, validating a set (not per report)
    for (unsigned i = 0; i < sizeof(Stage); i++) z = z && b[i] == 0;
    return z;
}

inline bool stage_valid(const Stage &s) {
    const int idx = (s.ramp & kRampIdxMask) - 1;  // -1 = no ramp
    const bool by_hold = (s.ramp & kRampByHold) != 0;
    bool ramp_ok;
    if (idx < 0) {
        ramp_ok = s.ramp == 0 && s.ramp_to == 0 && s.ramp_time == 0;
    } else {
        uint8_t q[7];
        memcpy(q, s.p, sizeof(q));
        q[idx] = s.ramp_to;  // the end value must be a valid effect too (every value between is, then)
        // error: SHOWN — false = CMD_RESULT_INVALID in 0xF1 via set_valid's callers; a bad set from flash reads as an empty slot
        ramp_ok = (s.ramp & 0x78) == 0 && rampable(s.mode, idx) && params_valid(s.mode, q) &&
                  (by_hold ? s.ramp_time >= 1 : (s.ramp_time == 0 && s.pull_max > s.pull_min));
    }
    return params_valid(s.mode, s.p) && s.pull_min <= s.pull_max && ramp_ok;
}

inline bool name_valid(const char *n) {
    bool ok = n[0] != 0, ended = false;
    // loop: HOT-PATH-OK — N = 16 characters, validating a set (not per report)
    for (int i = 0; i < kNameLen; i++) {
        const auto c = (uint8_t) n[i];
        if (c == 0) ended = true;
        else ok = ok && !ended && c >= 0x20 && c <= 0x7E;
    }
    return ok;
}

// The one check for a set from the app (0x1D / 0x1E) and from flash.
inline bool set_valid(const SetBody &b) {
    bool ok = b.ver == kRecordVer && b.flags == 0 && name_valid(b.name);
    // loop: HOT-PATH-OK — N = 2 triggers
    for (const Program &pg : b.trig) {
        ok = ok && pg.count <= kStages && pg.policy <= kReplace;
        // loop: HOT-PATH-OK — N = 4 stages
        for (int i = 0; i < kStages; i++) ok = ok && (i < pg.count ? stage_valid(pg.stage[i]) : stage_zero(pg.stage[i]));  // error: SHOWN — false = CMD_RESULT_INVALID in 0xF1 via set_valid's callers; a bad set from flash reads as an empty slot
    }
    return ok;
}

// --- The stage engine -----------------------------------------------------

// Once inside a stage's pull window, the pull may wander this far past its
// edges before the stage counts as left, so a trigger resting on an edge
// (the raw value jitters by a count or two) doesn't flip effects back and
// forth. Tunable starting value (~1.2 % of travel), not measured.
constexpr uint8_t kPullHysteresis = 3;

struct Run {
    bool inside[kStages]{};
    uint32_t since_ms[kStages]{};  // when the pull entered that stage's window
    int8_t stage = -1;             // the stage running, -1 none
    uint8_t held_tenths = 0;       // time in its window, tenths (status only)
    uint8_t want[kEffectBytes]{0x05};  // what the program wants on the trigger now
};

inline void run_reset(Run &r) { r = Run{}; }

// round(a / d), halves away from zero; d > 0.
inline int div_round(int a, int d) { return a >= 0 ? (2 * a + d) / (2 * d) : -((2 * -a + d) / (2 * d)); }

// One evaluation at pull `pull` (0..255) and time `now_ms`: the first stage
// (in order) whose window holds the pull and whose hold time has passed runs;
// none = Off. Cheap (≤ 4 stages, one encode): every main-loop pass.
inline void run_step(Run &r, const Program &pg, uint8_t pull, uint32_t now_ms) {
    r.stage = -1;
    r.held_tenths = 0;
    put_off(r.want);
    // loop: HOT-PATH-OK — N ≤ 4 stages, per main-loop pass
    for (int i = 0; i < pg.count; i++) {
        const Stage &s = pg.stage[i];
        const int lo = r.inside[i] ? s.pull_min - kPullHysteresis : s.pull_min;
        const int hi = r.inside[i] ? s.pull_max + kPullHysteresis : s.pull_max;
        const bool in = pull >= lo && pull <= hi;
        if (in && !r.inside[i]) r.since_ms[i] = now_ms;
        r.inside[i] = in;
    }
    // loop: HOT-PATH-OK — N ≤ 4 stages, per main-loop pass
    for (int i = 0; i < pg.count; i++) {
        const Stage &s = pg.stage[i];
        const uint32_t held = now_ms - r.since_ms[i];
        const uint32_t hold_min_ms = s.hold_min * 100u;
        if (!r.inside[i] || held < hold_min_ms) continue;
        uint8_t q[7];
        memcpy(q, s.p, sizeof(q));
        const int idx = (s.ramp & kRampIdxMask) - 1;
        if (idx >= 0) {
            int num, den;
            if (s.ramp & kRampByHold) {
                den = s.ramp_time * 100;
                num = (int) (held - hold_min_ms < (uint32_t) den ? held - hold_min_ms : (uint32_t) den);
            } else {
                const int pc = pull < s.pull_min ? s.pull_min : pull > s.pull_max ? s.pull_max : pull;
                num = pc - s.pull_min;
                den = s.pull_max - s.pull_min;
            }
            q[idx] = (uint8_t) (s.p[idx] + div_round((s.ramp_to - s.p[idx]) * num, den));
        }
        encode(s.mode, q, r.want);
        r.stage = (int8_t) i;
        r.held_tenths = (uint8_t) (held / 100 > 255 ? 255 : held / 100);
        return;
    }
}

// --- Fill in / Replace: who owns a trigger -------------------------------

// How long the game's trigger must have been without an effect before Fill
// in takes it. The game wins at once when it sets an effect again; this only
// delays taking over, so a game that clears and sets effects (a weapon
// change, rapid toggling) doesn't hand the trigger back and forth. Tunable
// starting value: ~8x the ~60 ms gaps of a game toggling 16 times a second
// (an unverified claim, docs/TENSION.md), and short enough that a menu's
// clear shows ours within half a second.
constexpr uint32_t kFillHoldOffMs = 500;
// The fewest milliseconds between two reports Tension sends itself (a ramp
// can change the effect every pass). Tunable starting value.
constexpr uint32_t kOwnReportGapMs = 20;

enum Owner : uint8_t { kOwnerNone = 0, kOwnerOurs = 1, kOwnerGame = 2, kOwnerWaiting = 3 };

struct Link {
    uint8_t game[kEffectBytes]{};  // the game's effect as it last set it (allow bit on)
    bool game_set = false;         // the game has set this trigger at all
    bool game_effect = false;      // ...and that was an effect, not Off
    bool had_effect = false;       // the game has had an effect on it at some point
    uint32_t game_off_ms = 0;      // when it last went from an effect to none
    uint8_t ctrl[kEffectBytes]{};  // what the controller has, as far as we know
    bool ctrl_known = false;
    bool ours_on_ctrl = false;     // the controller holds an effect we wrote
};

// "No effect": Off (0x05) or a zeroed block (0x00, not one of the
// controller's modes; games and drivers send it to mean nothing; a judgement,
// docs/TENSION.md).
inline bool is_effect(const uint8_t *b) { return b[0] != 0x00 && b[0] != 0x05; }

// The game set this trigger (its report had the allow bit).
inline void game_latch(Link &l, const uint8_t *block, uint32_t now_ms) {
    const bool eff = is_effect(block);
    if (l.game_effect && !eff) l.game_off_ms = now_ms;
    memcpy(l.game, block, kEffectBytes);
    l.game_set = true;
    l.game_effect = eff;
    if (eff) l.had_effect = true;
}

inline Owner owner(const Link &l, const Program *pg, uint32_t now_ms) {
    if (pg == nullptr || pg->count == 0) return kOwnerNone;
    if (pg->policy == kReplace) return kOwnerOurs;
    if (l.game_effect) return kOwnerGame;
    // Never had an effect, or none for the hold-off: ours.
    return (!l.had_effect || now_ms - l.game_off_ms >= kFillHoldOffMs) ? kOwnerOurs : kOwnerWaiting;
}

// What a trigger goes back to when it stops being ours: the game's own last
// effect, or Off when the game never set one.
inline void give_back(const Link &l, uint8_t *out) {
    if (l.game_set) memcpy(out, l.game, kEffectBytes);
    else put_off(out);
}

struct Engine {
    Run run[2];
    Link link[2];
    Owner own[2] = {kOwnerNone, kOwnerNone};
    uint32_t last_send_ms = 0;
    bool paused = false;  // the OLED Trigger Test owns the triggers
};

// The controller's state is unknown (a new link, a test effect sent behind
// our back): ours goes out again at the next report.
inline void forget_controller(Engine &e) {
    // loop: HOT-PATH-OK — N = 2 triggers
    for (Link &l : e.link) {
        l.ctrl_known = false;
        l.ours_on_ctrl = false;
    }
}

// A full state report on its way to the controller (a game's 0x02 forward,
// or the board's connect-time state): `ss` = its SetStateData bytes. Takes
// the game's trigger blocks as the game's latch, then puts ours where we own
// the trigger. With nothing running and nothing of ours on the controller
// the report goes out exactly as it came (nothing to merge). Returns which
// triggers carry a block Tension wrote (bit 0 L2, bit 1 R2), for report_lost.
inline uint8_t merge(Engine &e, const Program *const pg[2], uint8_t *ss, uint32_t now_ms) {
    uint8_t wrote = 0;
    // loop: HOT-PATH-OK — N = 2 triggers, per forwarded host report
    for (int t = 0; t < 2; t++) {
        Link &l = e.link[t];
        uint8_t *block = ss + kBlockAt[t];
        const bool host_sets = (ss[0] & kAllowBit[t]) != 0;
        if (host_sets) game_latch(l, block, now_ms);
        e.own[t] = owner(l, pg[t], now_ms);
        if (e.paused) {
            if (host_sets) l.ctrl_known = false;  // a test effect or the game's: re-sent on resume
            continue;
        }
        if (e.own[t] == kOwnerOurs) {
            if (host_sets || !l.ctrl_known || memcmp(l.ctrl, e.run[t].want, kEffectBytes) != 0) {
                memcpy(block, e.run[t].want, kEffectBytes);
                ss[0] |= kAllowBit[t];
                memcpy(l.ctrl, e.run[t].want, kEffectBytes);
                l.ctrl_known = true;
                l.ours_on_ctrl = true;
                e.last_send_ms = now_ms;
                wrote |= (uint8_t) (1u << t);
            }
        } else if (l.ours_on_ctrl) {
            if (!host_sets) {
                give_back(l, block);
                ss[0] |= kAllowBit[t];
            }
            wrote |= (uint8_t) (1u << t);  // this report hands the trigger back: it must arrive
            memcpy(l.ctrl, block, kEffectBytes);
            l.ctrl_known = true;
            l.ours_on_ctrl = false;
            e.last_send_ms = now_ms;
        } else if (host_sets) {
            memcpy(l.ctrl, block, kEffectBytes);
            l.ctrl_known = true;
        }
    }
    return wrote;
}

// A report merge() wrote into never left (the Bluetooth queue was full):
// those triggers' state is unknown, and may still be ours, so the next
// report or own_report sends ours again or gives the trigger back.
inline void report_lost(Engine &e, uint8_t wrote) {
    // loop: HOT-PATH-OK — N = 2 triggers
    for (int t = 0; t < 2; t++) {
        if (!(wrote & (1u << t))) continue;
        e.link[t].ctrl_known = false;
        e.link[t].ours_on_ctrl = true;
    }
}

// Main loop, with no game report coming: true = send `ss` (zeroed here, then
// only the trigger blocks that need to change and their allow bits) now.
// Rate-limited to one per kOwnReportGapMs, counting merged reports too.
inline bool own_report(Engine &e, const Program *const pg[2], uint8_t *ss, uint32_t now_ms) {
    if (e.paused || now_ms - e.last_send_ms < kOwnReportGapMs) return false;  // error: NOT-WORTH — nothing to send yet (rate limit / paused), not a failure
    bool any = false;
    // loop: HOT-PATH-OK — N = 2 triggers, per main-loop pass
    for (int t = 0; t < 2; t++) {
        const Link &l = e.link[t];
        e.own[t] = owner(l, pg[t], now_ms);
        const bool need = e.own[t] == kOwnerOurs ? (!l.ctrl_known || memcmp(l.ctrl, e.run[t].want, kEffectBytes) != 0)
                                                 : l.ours_on_ctrl;
        any = any || need;
    }
    if (!any) return false;  // error: NOT-WORTH — nothing to send, not a failure
    memset(ss, 0, 47);
    // merge() with no allow bits set writes exactly the blocks that need it.
    merge(e, pg, ss, now_ms);
    return true;
}

// --- The flash store --------------------------------------------------------

// Two copies in two sectors (A and B). Each save writes the sector that
// doesn't hold the newest copy, with the next sequence number; at boot the
// newest copy whose CRC checks out wins. A power cut during a save leaves a
// copy that fails its CRC (or the erased sector), so the previous one stays.
constexpr uint32_t kStoreMagic = 0x534E4554u;  // 'T','E','N','S' in flash order
// Layout 2 (proto 1.24, docs/TENSION_BUTTONS.md section 3.6): each slot also
// holds its ButtonPart. Layout 1 (proto 1.23) is still read, converted in RAM
// (no buttons), and never written again: the first save writes layout 2.
constexpr uint8_t kStoreLayout = 2;
constexpr uint8_t kStoreLayoutV1 = 1;

struct __attribute__((packed)) StoreHeader {
    uint32_t magic;
    uint8_t layout;     // kStoreLayout; a newer one is never written over
    uint8_t slots;      // kSlots
    uint8_t active;     // the saved choice: 0..7 or kNone
    uint8_t body_size;  // sizeof(SetBody)
    uint32_t seq;       // bumps on every save (wraps); the newer copy wins
    uint32_t crc;       // CRC-32 of the whole Store with this field as 0
};
struct __attribute__((packed)) SlotRec {
    uint32_t crc;  // CRC-32 of body and buttons, 494 bytes (the app's cache key in the 0xE9 list)
    uint8_t used;  // 1 = holds a set (anything else = empty)
    SetBody body;  // zeros when empty
    buttons::ButtonPart buttons;  // zeros when empty or when the set leaves the buttons alone
};
struct __attribute__((packed)) Store {
    StoreHeader h;
    SlotRec slot[kSlots];
};
static_assert(sizeof(Store) == 16 + kSlots * 499 && sizeof(Store) == 4008, "Store layout 2");
// Layout 1, as 1.23 wrote it: read only, to convert.
struct __attribute__((packed)) SlotRecV1 {
    uint32_t crc;  // CRC-32 of body
    uint8_t used;
    SetBody body;
};
struct __attribute__((packed)) StoreV1 {
    StoreHeader h;
    SlotRecV1 slot[kSlots];
};
static_assert(sizeof(StoreV1) == 16 + kSlots * 139, "Store layout 1");
// Written as whole flash pages (256 bytes): 16 pages (layout 1 was 5).
constexpr uint32_t kStorePages = (sizeof(Store) + 255) / 256;
constexpr uint32_t kStoreBytes = kStorePages * 256;
constexpr uint32_t kStoreCrcAt = 12;  // offsetof(StoreHeader, crc)

// CRC-32 of a whole copy (`size` bytes: sizeof(Store), or sizeof(StoreV1)
// for layout 1) with its crc field read as 0.
inline uint32_t store_crc(const uint8_t *f, uint32_t size = sizeof(Store)) {
    const uint8_t zero[4]{};
    uint32_t c = crc32_update(0xFFFFFFFFu, f, kStoreCrcAt);
    c = crc32_update(c, zero, 4);
    return ~crc32_update(c, f + kStoreCrcAt + 4, size - kStoreCrcAt - 4);
}

// A slot's own CRC: its body and buttons (contiguous in the record).
inline uint32_t slot_crc(const SlotRec &r) {
    return crc32_ieee(reinterpret_cast<const uint8_t *>(&r.body), kStagedBytes);
}
static_assert(__builtin_offsetof(SlotRec, buttons) == __builtin_offsetof(SlotRec, body) + sizeof(SetBody),
              "slot_crc reads body and buttons in one run");

inline void store_empty(Store &s) {
    memset(&s, 0, sizeof(s));
    s.h.magic = kStoreMagic;
    s.h.layout = kStoreLayout;
    s.h.slots = kSlots;
    s.h.active = kNone;
    s.h.body_size = sizeof(SetBody);
}

// Fill in every CRC before a save (the sequence number is the caller's).
inline void store_seal(Store &s) {
    // loop: HOT-PATH-OK — N = 8 slots, on a save
    for (SlotRec &r : s.slot) r.crc = r.used == 1 ? slot_crc(r) : 0;
    s.h.crc = 0;
    s.h.crc = store_crc(reinterpret_cast<const uint8_t *>(&s));
}

enum class Load : uint8_t { Empty, Ok, Newer };

// What a sector holds, read in place (no copy); `seq` its sequence number.
inline Load store_check(const uint8_t *f, uint32_t &seq) {
    StoreHeader h;
    memcpy(&h, f, sizeof(h));
    seq = h.seq;
    // error: NOT-WORTH — erased flash or another layout's bytes: no copy here (the other sector, or no sets)
    if (h.magic != kStoreMagic) return Load::Empty;
    // error: SHOWN — 0xE9 status [3] bit3 (the app says the sets were saved by newer firmware and can't be changed here)
    if (h.layout > kStoreLayout) return Load::Newer;
    const uint32_t size = h.layout == kStoreLayoutV1 ? sizeof(StoreV1) : sizeof(Store);
    // error: NOT-WORTH — a damaged or half-written copy: the other sector's copy is used (or no sets)
    if (h.layout < kStoreLayoutV1 || h.slots != kSlots || h.body_size != sizeof(SetBody) || h.crc != store_crc(f, size)) return Load::Empty;
    return Load::Ok;
}

// One sector's copy `f` into `s` (always a usable store). Empty: erased
// flash, something else, or a copy that fails its CRC (a save cut short);
// Newer: saved by a firmware with a newer layout (`s` empty, and never to be
// saved over); Ok: the copy, with any slot whose own CRC or contents fail
// read as empty and the saved choice dropped if it names an empty slot.
inline Load store_load(const uint8_t *f, Store &s) {
    store_empty(s);
    uint32_t seq;
    const Load l = store_check(f, seq);
    // error: NOT-WORTH — passed on: Empty / Newer as store_check says (store_pick decides)
    if (l != Load::Ok) return l;
    if (f[4] == kStoreLayoutV1) {
        // Layout 1 (1.23): each slot's own CRC and set as 1.23 checked them,
        // then its buttons zero (the set leaves them alone) and its CRC over
        // body + buttons. Only RAM changes: nothing is written at boot.
        memcpy(&s.h, f, sizeof(s.h));
        s.h.layout = kStoreLayout;
        // loop: HOT-PATH-OK — N = 8 slots, at boot
        for (int i = 0; i < kSlots; i++) {
            SlotRecV1 v;
            memcpy(&v, f + sizeof(StoreHeader) + i * sizeof(SlotRecV1), sizeof(v));
            // error: NOT-WORTH — a damaged slot reads as empty (the app lists it empty); the others are kept
            const bool ok = v.used == 1 && v.crc == crc32_ieee(reinterpret_cast<const uint8_t *>(&v.body), sizeof(SetBody)) &&
                            set_valid(v.body);
            if (!ok) continue;  // stays empty (store_empty)
            SlotRec &r = s.slot[i];
            r.used = 1;
            r.body = v.body;
            r.crc = slot_crc(r);
        }
    } else {
        memcpy(&s, f, sizeof(s));
        // loop: HOT-PATH-OK — N = 8 slots, at boot
        for (SlotRec &r : s.slot) {
            // error: NOT-WORTH — a damaged slot reads as empty (the app lists it empty); the others are kept
            const bool ok = r.used == 1 && r.crc == slot_crc(r) && set_valid(r.body) && buttons::part_valid(r.buttons);
            // error: NOT-WORTH — a damaged slot reads as empty (the app lists it empty); the others are kept
            if (!ok) memset(&r, 0, sizeof(r));
        }
    }
    if (s.h.active >= kSlots || s.slot[s.h.active].used != 1) s.h.active = kNone;
    return Load::Ok;
}

// a is a later sequence number than b (wrap-safe, like TCP's).
inline bool seq_after(uint32_t a, uint32_t b) { return (int32_t) (a - b) > 0; }

struct Pick {
    Load load;     // Ok (a copy), Empty (none), Newer (either sector is newer firmware's)
    int8_t from;   // 0 = sector A, 1 = sector B, -1 = none
    uint32_t seq;  // the copy's sequence number (0 with none)
};

// Both sectors into `s`: the newest valid copy, layout 1 or 2 (the later
// sequence number wins across layouts). A newer layout in either sector makes
// the whole store read-only (its sets aren't ours to drop).
inline Pick store_pick(const uint8_t *fa, const uint8_t *fb, Store &s) {
    uint32_t sa, sb;
    const Load la = store_check(fa, sa), lb = store_check(fb, sb);
    store_empty(s);
    // error: SHOWN — 0xE9 status [3] bit3 (read-only: saved by newer firmware)
    if (la == Load::Newer || lb == Load::Newer) return {Load::Newer, -1, 0};
    // error: NOT-WORTH — a choice between the two copies (newest valid), not a failure
    const int from = lb == Load::Ok && (la != Load::Ok || seq_after(sb, sa)) ? 1 : la == Load::Ok ? 0 : -1;
    // error: NOT-WORTH — no valid copy in either sector: no sets yet (erased, or both damaged)
    if (from < 0) return {Load::Empty, -1, 0};
    // error: NOT-WORTH — store_check just said Ok for this sector, so the load is Ok
    store_load(from ? fb : fa, s);
    return {Load::Ok, (int8_t) from, from ? sb : sa};
}

}  // namespace tension

// --- Board glue (src/tension.cpp) -------------------------------------------

#define PICO_CMD_TENSION 0xe9  // vendor interface only, GET; wValue high byte = selector

// Boot: read the saved sets and the saved choice (after config_load()).
void tension_load();
// Main loop: run the programs on the current pull, send ours when no game
// report carries it, follow the controller's link.
void tension_task();
// A full state report on its way to the controller (`ss` = its SetStateData):
// the game's 0x02 forward and bt_send_state(). Core0, O(1). Returns the
// triggers Tension wrote into it; pass them to tension_report_lost() when the
// report couldn't be sent.
uint8_t tension_on_state_report(uint8_t *ss);
void tension_report_lost(uint8_t wrote);
// 0xF6 0x12 (a game ended): a temporary choice ends, back to the saved one.
void tension_revert();
// The OLED Trigger Test (or the app's 0x0A preview) drives the triggers:
// Tension stands aside while `on`, then sends ours again.
void tension_pause(bool on);
// The running set's name for the OLED ("" none, "Feel it" for a preview).
const char *tension_running_name();

// 0xF6 functions (cmd.cpp): each returns a CMD_RESULT_*.
uint8_t tension_stage_write(const uint8_t *args, uint16_t n);  // 0x1C, now
uint8_t tension_save_check(const uint8_t *args, uint16_t n);   // 0x1D at SET time: OK = defer
uint8_t tension_save_staged(uint8_t slot);                     // 0x1D from cmd_task
uint8_t tension_feel(const uint8_t *args, uint16_t n);         // 0x1E, now
uint8_t tension_delete_check(const uint8_t *args, uint16_t n); // 0x1F at SET time
uint8_t tension_delete(uint8_t slot);                          // 0x1F from cmd_task
uint8_t tension_select(const uint8_t *args, uint16_t n, bool &defer_save);  // 0x20
uint8_t tension_select_save(uint8_t slot);                     // 0x20 saved, from cmd_task
// 0x22 [2, 1, crc32 over 494] (protocol 1.24, docs/TENSION_BUTTONS.md
// section 6.2): the staged set runs as the set chosen now, for a game, RAM
// only (never written to flash), shown as slot kAppSet; [2, 0] ends it (back
// to the saved choice), as does 0x12.
uint8_t tension_app_set(const uint8_t *args, uint16_t n);       // 0x22, now
constexpr uint8_t kAppSet = 0xFD;
// GET 0xE9 (vendor interface only): the reply for `selector`, ≤ 63 bytes.
uint16_t tension_get(uint8_t selector, uint8_t *buffer, uint16_t reqlen);

#endif  // DS5_BRIDGE_TENSION_H
