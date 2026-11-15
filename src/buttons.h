//
// Tension buttons (protocol 1.24): button layouts in Tension sets. Design and
// wire layout: docs/TENSION_BUTTONS.md (the contract for both repos).
//
// A set is its SetBody (src/tension.h, the triggers) plus a ButtonPart: a pool
// of button entries, one per (layer, button) that differs. Everything in
// namespace buttons is pure (no SDK): host-tested in tests/host/test_buttons.cpp.
// The board glue (which layout runs, applying it to the outgoing report) is
// src/buttons.cpp and src/tension.cpp, declared at the end of this file.
//
// The validator holds every rule of the doc's section 3.5, so the app and the
// board refuse the same bytes. What this firmware can run is a subset, one
// identity [58] bit per build step: part_needs() says which bits a part needs
// and the board refuses (INVALID) a part needing a bit it lacks. Built now:
// step 2, Press with controller-button targets and Disable ([58] bit0).
//
// Ideas adapted from HID Remapper (MIT, (c) 2023 Jacek Fedorynski,
// github.com/jfedor2/hid-remapper, read at 51ab8b3): a layer key kept mapped
// on the layer it enters (remapper.cc:416-425, rule 7 below). Written fresh,
// no code copied.
//

#ifndef DS5_BRIDGE_BUTTONS_H
#define DS5_BRIDGE_BUTTONS_H

#include <cstdint>
#include <cstring>

namespace buttons {

constexpr int kSources = 22;       // 0..15 the remap table's order, then 16..21 below
constexpr int kButtonTargets = 18; // controller-button targets: the 16 legacy ones, touch pad, PS
constexpr int kMaxLayers = 3;      // extra layers over the base
constexpr int kLayerNameLen = 10;
constexpr int kPoolBytes = 308;
constexpr uint8_t kPartVer = 1;    // ButtonPart::bver when used

// bflags
constexpr uint8_t kUsed = 0x01, kGyroUsed = 0x02;

// Sources, 0..15 exactly the legacy remap order (src/remap.cpp RemapButton).
enum Source : uint8_t {
    kL2 = 0, kL1, kCreate, kUp, kLeft, kDown, kRight, kL3, kR2, kR1, kOptions, kTriangle, kCircle, kCross, kSquare, kR3,
    kTouchPad = 16,  // input byte 9 bit 0x02 (SDL_hidapi_ps5.c, as remap.h cites it)
    kPs = 17,        // byte 9 bit 0x01
    kFnLeft = 18, kFnRight = 19, kBackLeft = 20, kBackRight = 21,  // the Edge's, byte 9 bits 0x10..0x80
};
constexpr int kFirstEdgeSource = kFnLeft;

// Target kinds (docs/TENSION_BUTTONS.md section 3.4).
enum Kind : uint8_t { kNothing = 0, kButton = 1, kKey = 2, kMouseButton = 3, kMouseMove = 4, kLayer = 5, kGyro = 6 };

// The rows byte of an entry.
enum Row : uint8_t { kPress = 0x01, kHold = 0x02, kDouble = 0x04, kWithPs = 0x08, kToggle = 0x10, kTurbo = 0x20 };

// Identity [58]: one bit per piece, as it lands (section 6).
enum Feature : uint8_t {
    kFeatLayouts = 0x01,     // button layouts in sets, Press with controller buttons and Disable
    kFeatPressTypes = 0x02,  // Hold, Double press, With PS, Toggle, Turbo
    kFeatLayers = 0x04,
    kFeatLive = 0x08,        // GET 0xE8
    kFeatKeysMouse = 0x10,   // key and mouse-button targets, 0xF6 0x21
    kFeatGyro = 0x20,        // gyro as mouse, mouse motion targets
    kFeatAppSet = 0x40,      // 0xF6 0x22
};
// What this firmware runs (build step 2).
constexpr uint8_t kBuilt = kFeatLayouts;

struct __attribute__((packed)) Layer {
    char name[kLayerNameLen];  // ASCII 0x20..0x7E then NULs; may be empty
    uint8_t colour;            // 0..5, one of the app's six theme tokens
    uint8_t lflags;            // gyro in this layer: 0 same as base, 1 on, 2 off
};
static_assert(sizeof(Layer) == 12, "Layer is 12 bytes on the wire");

struct __attribute__((packed)) ButtonPart {
    uint8_t bver;           // kPartVer when used, 0 when not
    uint8_t bflags;         // kUsed | kGyroUsed; 0 = this set leaves the buttons alone (all 360 bytes zero)
    uint8_t layers;         // extra layers in use, 0..3
    uint8_t double_window;  // Double press window in 10 ms, 15..50
    uint16_t pool_len;      // bytes of pool in use, 0..308
    Layer layer[kMaxLayers];
    uint8_t gyro[10];       // all zero unless kGyroUsed
    uint8_t pool[kPoolBytes];
};
static_assert(sizeof(ButtonPart) == 360, "ButtonPart is 360 bytes on the wire and in flash");
static_assert(__builtin_offsetof(ButtonPart, layer) == 6 && __builtin_offsetof(ButtonPart, gyro) == 42 &&
              __builtin_offsetof(ButtonPart, pool) == 52, "docs/TENSION_BUTTONS.md section 3.2");

struct Target {
    uint8_t kind, a, b;
};

// One entry of the pool (section 3.3), parsed. t[] is indexed Press, Hold,
// Double press, With PS (an absent row's target is all zero).
struct Entry {
    uint8_t src, layer, rows;
    Target t[4];
    uint8_t hold;   // 10 ms, with a Hold row
    uint8_t turbo;  // presses per second, with a Turbo row
    uint8_t len;    // bytes in the pool
};

inline bool all_zero(const uint8_t *p, unsigned n) {
    bool z = true;
    // loop: HOT-PATH-OK — N ≤ 360 bytes, validating a set (a commit / a load, never per report)
    for (unsigned i = 0; i < n; i++) z = z && p[i] == 0;
    return z;
}

// The entry at p (left = pool bytes from p to pool_len). False: it runs past
// pool_len or has bit 7 of its first byte set.
inline bool parse_entry(const uint8_t *p, unsigned left, Entry &e) {
    e = Entry{};
    // error: SHOWN — false = CMD_RESULT_INVALID in 0xF1 via part_valid's callers; a bad part from flash reads as an empty slot
    if (left < 2 || (p[0] & 0x80)) return false;
    e.src = p[0] & 0x1F;
    e.layer = (p[0] >> 5) & 0x03;
    e.rows = p[1];
    unsigned at = 2;
    // loop: HOT-PATH-OK — N = 4 rows with a target, parsing one entry
    for (int r = 0; r < 4; r++) {
        if (!(e.rows & (1u << r))) continue;
        // error: SHOWN — false = CMD_RESULT_INVALID in 0xF1 via part_valid's callers; a bad part from flash reads as an empty slot
        if (at + 3 > left) return false;
        e.t[r] = {p[at], p[at + 1], p[at + 2]};
        at += 3;
    }
    if (e.rows & kHold) {
        // error: SHOWN — false = CMD_RESULT_INVALID in 0xF1 via part_valid's callers; a bad part from flash reads as an empty slot
        if (at + 1 > left) return false;
        e.hold = p[at++];
    }
    if (e.rows & kTurbo) {
        // error: SHOWN — false = CMD_RESULT_INVALID in 0xF1 via part_valid's callers; a bad part from flash reads as an empty slot
        if (at + 1 > left) return false;
        e.turbo = p[at++];
    }
    e.len = (uint8_t) at;
    return true;
}

// Rule 4 (and 7, 8 for the target alone): a target as section 3.4 allows it.
// `own_layer` = the entry's layer, `layers` and `gyro` from the part.
inline bool target_valid(const Target &t, uint8_t own_layer, uint8_t layers, bool gyro) {
    switch (t.kind) {
        case kNothing: return t.a == 0 && t.b == 0;
        case kButton: return t.a < kButtonTargets && t.b == 0;
        case kKey: return (t.a >= 0x04 && t.a <= 0xA4) || (t.a == 0 && t.b != 0);  // a = 0: modifiers only
        case kMouseButton: return t.a >= 1 && t.a <= 5 && t.b == 0;
        case kMouseMove: return t.a <= 5 && t.b >= 1 && (t.a <= 3 || t.b <= 30);  // pointer 1..255, wheel 1..30
        case kLayer: return t.a >= 1 && t.a <= layers && t.a != own_layer && t.b <= 1;
        case kGyro: return gyro && t.a <= 2 && t.b == 0;
        // error: SHOWN — false = CMD_RESULT_INVALID in 0xF1 via part_valid's callers; a bad part from flash reads as an empty slot
        default: return false;
    }
}

inline bool layer_name_valid(const char *n) {
    bool ok = true, ended = false;
    // loop: HOT-PATH-OK — N = 10 characters, validating a set (not per report)
    for (int i = 0; i < kLayerNameLen; i++) {
        const auto c = (uint8_t) n[i];
        if (c == 0) ended = true;
        else ok = ok && !ended && c >= 0x20 && c <= 0x7E;
    }
    return ok;
}

// The one check for a ButtonPart from the app (0x1D / 0x1E ver 2) and from
// flash: every rule of docs/TENSION_BUTTONS.md section 3.5.
inline bool part_valid(const ButtonPart &p) {
    const auto *raw = reinterpret_cast<const uint8_t *>(&p);
    // Rule 1: not used = all zero; used = bver 1, reserved flag bits zero.
    // error: SHOWN — false = CMD_RESULT_INVALID in 0xF1 via part_valid's callers; a bad part from flash reads as an empty slot
    if (!(p.bflags & kUsed)) return all_zero(raw, sizeof(p));
    // error: SHOWN — false = CMD_RESULT_INVALID in 0xF1 via part_valid's callers; a bad part from flash reads as an empty slot
    if (p.bver != kPartVer || (p.bflags & ~(kUsed | kGyroUsed)) != 0) return false;
    // Rule 2: the header, the layers, the gyro block.
    // error: SHOWN — false = CMD_RESULT_INVALID in 0xF1 via part_valid's callers; a bad part from flash reads as an empty slot
    if (p.layers > kMaxLayers || p.double_window < 15 || p.double_window > 50 || p.pool_len > kPoolBytes) return false;
    // error: SHOWN — false = CMD_RESULT_INVALID in 0xF1 via part_valid's callers; a bad part from flash reads as an empty slot
    if (!all_zero(p.pool + p.pool_len, kPoolBytes - p.pool_len)) return false;
    bool ok = true;
    // loop: HOT-PATH-OK — N = 3 layers, validating a set
    for (int i = 0; i < kMaxLayers; i++) {
        const Layer &l = p.layer[i];
        // error: SHOWN — false = CMD_RESULT_INVALID in 0xF1 via part_valid's callers; a bad part from flash reads as an empty slot
        if (i >= p.layers) ok = ok && all_zero(reinterpret_cast<const uint8_t *>(&l), sizeof(l));
        else ok = ok && layer_name_valid(l.name) && l.colour <= 5 && l.lflags <= 2;
    }
    const bool gyro = (p.bflags & kGyroUsed) != 0;
    // Gyro (section 3.2): [0] mode 1 mouse, 2 right stick (open question 5,
    // approved by the owner 2026-11-15); [1] axis 0 yaw, 1 roll; [2] flags
    // bits 0..2; [3], [4] speeds 1..255; [5] steadiness 0..255; [6..9] zero.
    // error: SHOWN — false = CMD_RESULT_INVALID in 0xF1 via part_valid's callers; a bad part from flash reads as an empty slot
    if (gyro) ok = ok && (p.gyro[0] == 1 || p.gyro[0] == 2) && p.gyro[1] <= 1 && (p.gyro[2] & ~0x07) == 0 &&
                   p.gyro[3] >= 1 && p.gyro[4] >= 1 &&
                   // error: SHOWN — as above: false = CMD_RESULT_INVALID in 0xF1
                   all_zero(p.gyro + 6, 4);
    // error: SHOWN — as above: false = CMD_RESULT_INVALID in 0xF1
    else ok = ok && all_zero(p.gyro, sizeof(p.gyro));
    // error: SHOWN — false = CMD_RESULT_INVALID in 0xF1 via part_valid's callers; a bad part from flash reads as an empty slot
    if (!ok) return false;
    // Rules 3..8: the entries.
    uint32_t present[kMaxLayers + 1]{}, enters[kMaxLayers + 1]{};  // bit = source: has an entry in layer L / switches into L
    int prev = -1;
    unsigned at = 0;
    // loop: HOT-PATH-OK — N ≤ 61 entries (308 bytes, 5 at least each), validating a set
    while (at < p.pool_len) {
        Entry e;
        // error: SHOWN — false = CMD_RESULT_INVALID in 0xF1 via part_valid's callers; a bad part from flash reads as an empty slot
        if (!parse_entry(p.pool + at, p.pool_len - at, e)) return false;
        at += e.len;
        const int key = e.layer * 32 + e.src;
        // Rule 3: a source, a layer in use, strictly increasing (layer, source), some row.
        // error: SHOWN — false = CMD_RESULT_INVALID in 0xF1 via part_valid's callers; a bad part from flash reads as an empty slot
        if (e.src >= kSources || e.layer > p.layers || key <= prev || e.rows == 0 || (e.rows & 0xC0) != 0) return false;
        prev = key;
        present[e.layer] |= 1u << e.src;
        // Rule 4: targets, hold time, turbo rate (rules 7 and 8 per target too).
        // loop: HOT-PATH-OK — N = 4 rows, validating one entry
        for (int r = 0; r < 4; r++) {
            if (!(e.rows & (1u << r))) continue;
            ok = ok && target_valid(e.t[r], e.layer, p.layers, gyro);
            if (e.t[r].kind == kLayer && e.t[r].b == 1) enters[e.t[r].a & 0x03] |= 1u << e.src;
        }
        if (e.rows & kHold) ok = ok && e.hold >= 15 && e.hold <= 200;
        if (e.rows & kTurbo) ok = ok && e.turbo >= 2 && e.turbo <= 30;
        // Rule 5: Toggle and Turbo need a Press that is a controller button, a
        // key or a mouse button (no Press row = the button itself).
        if (e.rows & (kToggle | kTurbo)) {
            const uint8_t k = e.t[0].kind;
            ok = ok && (!(e.rows & kPress) || k == kButton || k == kKey || k == kMouseButton);
        }
        // Rule 6: the PS button keeps its Press (only Hold and Double press);
        // With PS never on the PS button.
        if (e.src == kPs) ok = ok && (e.rows & ~(kHold | kDouble)) == 0;
        // error: SHOWN — false = CMD_RESULT_INVALID in 0xF1 via part_valid's callers; a bad part from flash reads as an empty slot
        if (!ok) return false;
    }
    // Rule 7: a button that switches into layer L has no entry in L, so it
    // keeps "switch back" there and no one is trapped in L.
    // loop: HOT-PATH-OK — N = 3 layers, validating a set
    for (int l = 1; l <= kMaxLayers; l++) ok = ok && (present[l] & enters[l]) == 0;
    return ok;
}

// The identity [58] bits a valid part needs to run as saved.
inline uint8_t part_needs(const ButtonPart &p) {
    // error: NOT-WORTH — not a failure: a part that leaves the buttons alone needs nothing
    if (!(p.bflags & kUsed)) return 0;
    uint8_t n = kFeatLayouts;
    if (p.layers > 0) n |= kFeatLayers;
    if (p.bflags & kGyroUsed) n |= kFeatGyro;
    unsigned at = 0;
    // loop: HOT-PATH-OK — N ≤ 61 entries, on a commit / a set change
    while (at < p.pool_len) {
        Entry e;
        // error: NOT-WORTH — part_valid ran first (the callers' order), so every entry parses
        if (!parse_entry(p.pool + at, p.pool_len - at, e)) break;
        at += e.len;
        if (e.rows & ~kPress) n |= kFeatPressTypes;
        // loop: HOT-PATH-OK — N = 4 rows
        for (int r = 0; r < 4; r++) {
            if (!(e.rows & (1u << r))) continue;
            const uint8_t k = e.t[r].kind;
            if (k == kKey || k == kMouseButton) n |= kFeatKeysMouse;
            if (k == kMouseMove) n |= kFeatKeysMouse | kFeatGyro;
            if (k == kLayer) n |= kFeatLayers;
            if (k == kGyro) n |= kFeatGyro;
        }
    }
    return n;
}

// --- Running a layout (build step 2: the base layer's Press) --------------

// What each source does now: a controller button 0..17, kOff (Disable), or
// kSelf (the button itself).
constexpr uint8_t kSelf = 0xFF, kOff = 0xFE;
struct Layout {
    uint8_t press[kSources];
};

// A valid part into a Layout. False: it doesn't change the buttons (not used)
// or needs a piece this firmware lacks; then no set's buttons run.
inline bool compile(const ButtonPart &p, Layout &l) {
    // error: NOT-WORTH — not a failure: this set leaves the buttons alone, so the next level runs (docs/TENSION_BUTTONS.md section 3.7)
    if (!(p.bflags & kUsed)) return false;
    // error: SHOWN — a part with pieces this build lacks is refused as INVALID when sent (0xF1); one already in flash (saved by a later build) keeps its bytes and runs nothing here
    if ((part_needs(p) & ~kBuilt) != 0) return false;
    memset(l.press, kSelf, sizeof(l.press));
    unsigned at = 0;
    // loop: HOT-PATH-OK — N ≤ 61 entries, on a set change
    while (at < p.pool_len) {
        Entry e;
        // error: NOT-WORTH — part_valid ran first (the callers' order), so every entry parses
        if (!parse_entry(p.pool + at, p.pool_len - at, e)) break;
        at += e.len;
        l.press[e.src] = e.t[0].kind == kButton ? e.t[0].a : kOff;  // only base Press rows here (part_needs above)
    }
    return true;
}

// Hat 0 up, 1 up-right, 2 right .. 7 up-left, 8 none.
__attribute__((always_inline)) inline bool hat_has(uint8_t hat, uint8_t src) {
    switch (src) {
        case kUp: return hat == 0 || hat == 1 || hat == 7;
        case kRight: return hat == 1 || hat == 2 || hat == 3;
        case kDown: return hat == 3 || hat == 4 || hat == 5;
        default: /* kLeft */ return hat == 5 || hat == 6 || hat == 7;
    }
}
__attribute__((always_inline)) inline uint8_t hat_from(bool up, bool right, bool down, bool left) {
    if (up && right && !down && !left) return 1;
    if (right && down && !up && !left) return 3;
    if (down && left && !up && !right) return 5;
    if (left && up && !right && !down) return 7;
    if (up && !down) return 0;
    if (right && !left) return 2;
    if (down && !up) return 4;
    if (left && !right) return 6;
    return 8;
}

// The sources physically down in an input report (63 bytes, no report id),
// bit = source: byte 7 the D-pad hat (low nibble) and the face buttons, byte 8
// the shoulders, sticks, Create and Options, byte 9 PS, the touch pad and the
// Edge's buttons. Plain code, no lookup table, so the RAM-resident caller
// reads nothing from flash.
__attribute__((always_inline)) inline uint32_t sources_down(const uint8_t *r) {
    const uint8_t hat = r[7] & 0x0F, b7 = r[7], b8 = r[8], b9 = r[9];
    uint32_t d = 0;
    if (b8 & 0x04) d |= 1u << kL2;
    if (b8 & 0x01) d |= 1u << kL1;
    if (b8 & 0x10) d |= 1u << kCreate;
    if (hat_has(hat, kUp)) d |= 1u << kUp;
    if (hat_has(hat, kLeft)) d |= 1u << kLeft;
    if (hat_has(hat, kDown)) d |= 1u << kDown;
    if (hat_has(hat, kRight)) d |= 1u << kRight;
    if (b8 & 0x40) d |= 1u << kL3;
    if (b8 & 0x08) d |= 1u << kR2;
    if (b8 & 0x02) d |= 1u << kR1;
    if (b8 & 0x20) d |= 1u << kOptions;
    if (b7 & 0x80) d |= 1u << kTriangle;
    if (b7 & 0x40) d |= 1u << kCircle;
    if (b7 & 0x20) d |= 1u << kCross;
    if (b7 & 0x10) d |= 1u << kSquare;
    if (b8 & 0x80) d |= 1u << kR3;
    if (b9 & 0x02) d |= 1u << kTouchPad;
    if (b9 & 0x01) d |= 1u << kPs;
    d |= (uint32_t) ((b9 >> 4) & 0x0F) << kFnLeft;  // Fn left 0x10 .. back right 0x80
    return d;
}

// The layout on an outgoing input report copy (never interrupt_in_data), as
// remap_apply does for the legacy table: controller-button targets OR
// together, L2/R2's analog pull is the largest of the sources sent there (a
// digital source counts as full), every source that isn't "itself" is
// cleared, and the D-pad hat, bytes 7 and 8 and byte 9's PS and touch pad bits
// are rebuilt. The Edge's buttons keep their own bit while "itself" (the PC
// only knows them in Edge mode); Mute is never a source. Inlined into the
// RAM-resident buttons_apply (always_inline, as haptics_decimate.h does).
__attribute__((always_inline)) inline void apply(const Layout &l, uint8_t *r) {
    const uint32_t down_mask = sources_down(r);
    bool on[kButtonTargets]{};
    uint8_t analog[kButtonTargets]{};
    uint8_t b9 = (uint8_t) (r[9] & ~0x03);  // PS (0x01) and touch pad (0x02) are rebuilt below
    // loop: HOT-PATH-OK — N = 22 sources, per input report while a set's buttons run
    for (int s = 0; s < kSources; s++) {
        const bool down = (down_mask >> s) & 1u;
        uint8_t t = l.press[s];
        if (s >= kFirstEdgeSource) {
            if (t == kSelf) continue;  // the Edge's own bit stays as it is
            b9 &= (uint8_t) ~(0x10u << (s - kFirstEdgeSource));
        } else if (t == kSelf) {
            t = (uint8_t) s;
        }
        if (t >= kButtonTargets) continue;  // Disable
        const uint8_t a = s == kL2 ? r[4] : s == kR2 ? r[5] : (down ? 0xFF : 0);
        if (down) on[t] = true;
        if (a > analog[t]) analog[t] = a;
    }
    r[4] = analog[kL2];
    r[5] = analog[kR2];
    r[7] = (uint8_t) (hat_from(on[kUp], on[kRight], on[kDown], on[kLeft]) | (on[kSquare] ? 0x10 : 0) |
                      (on[kCross] ? 0x20 : 0) | (on[kCircle] ? 0x40 : 0) | (on[kTriangle] ? 0x80 : 0));
    r[8] = (uint8_t) ((on[kL1] ? 0x01 : 0) | (on[kR1] ? 0x02 : 0) | (on[kL2] ? 0x04 : 0) | (on[kR2] ? 0x08 : 0) |
                      (on[kCreate] ? 0x10 : 0) | (on[kOptions] ? 0x20 : 0) | (on[kL3] ? 0x40 : 0) | (on[kR3] ? 0x80 : 0));
    r[9] = (uint8_t) (b9 | (on[kPs] ? 0x01 : 0) | (on[kTouchPad] ? 0x02 : 0));
}

}  // namespace buttons

// --- Board glue (src/buttons.cpp) -------------------------------------------

// The layout that runs now (docs/TENSION_BUTTONS.md section 3.7, levels 1-2:
// Feel it, then the set chosen now), or nullptr: then the legacy remap table
// runs (levels 3-4). Set by tension.cpp on every change of what runs.
void buttons_run(const buttons::Layout *layout);
// interrupt_loop, on the outgoing report copy after calib_apply: the running
// set's buttons, else remap_apply unchanged. RAM-resident on RP2350.
void buttons_apply(uint8_t *report);

#endif  // DS5_BRIDGE_BUTTONS_H
