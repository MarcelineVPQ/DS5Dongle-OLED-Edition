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
// every piece, steps 2-8 ([58] bits 0..6); bit 7 stays reserved.
//
// Ideas adapted from HID Remapper (MIT, (c) 2023 Jacek Fedorynski,
// github.com/jfedor2/hid-remapper, read at 51ab8b3): a layer key kept mapped
// on the layer it enters (remapper.cc:416-425, rule 7 below); the tap / hold
// decision timed from the press's rising edge (remapper.cc:1098-1109); "no
// layer held = the base" (remapper.cc:1162-1165); fixed-point accumulators for
// relative motion (remapper.cc:91, 1242-1244). Written fresh, no code copied.
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
// What this firmware runs: every piece (build steps 2-8).
constexpr uint8_t kBuilt = kFeatLayouts | kFeatPressTypes | kFeatLayers | kFeatLive | kFeatKeysMouse | kFeatGyro | kFeatAppSet;

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

// --- Running a layout (docs/TENSION_BUTTONS.md section 4) -----------------
//
// The engine below is pure and runs on core0 only. Every function is
// always_inline: buttons.cpp calls them from its few RAM-resident functions,
// so nothing here becomes a separate function in flash (CORE0_RAM_FUNC,
// tools/check_ram_residency.sh). No lookup tables (they would be read from
// flash), no heap, no 64-bit division. Times are time_us_32() values; every
// comparison is a difference, so the 71-minute wrap doesn't matter.

#define BUTTONS_INLINE __attribute__((always_inline)) inline

// Timings. Each is tunable: a starting value no one has measured.
constexpr uint32_t kTapUs = 50000;      // a tap holds its Press target 50 ms (section 4.4)
constexpr uint32_t kPsPassUs = 500000;  // With PS: a PS press still down 0.5 s with no chord goes out as held (4.5)
constexpr uint32_t kStaleUs = 500000;   // no input report for 0.5 s: release everything (4.8)
// A key going out (section 6 of the build): its modifiers go down first and
// the key kKeyLeadUs later, the key stays down at least kKeyHoldUs, and the
// modifiers stay kKeyTrailUs after it, so a short press is still typed with
// its modifiers. The pacing idea is from SundayMoments/DS5_Bridge (AGPL-3.0;
// idea only, no code taken).
constexpr uint32_t kKeyLeadUs = 20000, kKeyHoldUs = 60000, kKeyTrailUs = 20000;
constexpr uint32_t kMaxDtUs = 20000;  // gyro and mouse motion: a gap counts as 20 ms at most (4.6)
// Gyro as mouse: counts = sum(w x dt_us x speed) >> kGyroShift. Speed 64 and
// a steady w of 1000 counts give about 1900 mouse counts a second (estimate);
// tunable, for the owner's first trial (section 4.6).
constexpr int kGyroShift = 25;
// Gyro as right stick: stick offset = w x speed >> kGyroStickShift, so speed
// 64 reaches full tilt near w = 1000 (estimate; tunable).
constexpr int kGyroStickShift = 9;

// Internal target kind: an Edge button as itself (its own bit in byte 9).
// Never on the wire (the validator refuses kind 7).
constexpr uint8_t kEdgeSelf = 7;

// A compiled ButtonPart: per layer and source its entry (rows 0 = none:
// absent in the base = the button itself, absent in a layer = same as base).
// An entry without a Press row gets the button itself as its Press target,
// so t[0] is always the Press target.
struct Layout {
    uint8_t layers, window, bflags, needs;  // window: Double press, 10 ms; needs: part_needs()
    uint8_t gyro[10];
    uint8_t lflags[kMaxLayers + 1];         // gyro per layer, [0] the base (0)
    bool with_ps[kMaxLayers + 1];           // the layer, with what it takes from the base, has a With PS row
    Entry e[kMaxLayers + 1][kSources];
    Entry self[kSources];                   // "the button itself"
};

BUTTONS_INLINE const Entry *resolve(const Layout &l, uint8_t layer, int s) {
    if (layer != 0 && l.e[layer][s].rows != 0) return &l.e[layer][s];
    if (l.e[0][s].rows != 0) return &l.e[0][s];
    return &l.self[s];
}

// A valid part into a Layout. False: it doesn't change the buttons (not used)
// or needs a piece this firmware lacks; then no set's buttons run.
inline bool compile(const ButtonPart &p, Layout &l) {
    // error: NOT-WORTH — not a failure: this set leaves the buttons alone, so the next level runs (docs/TENSION_BUTTONS.md section 3.7)
    if (!(p.bflags & kUsed)) return false;
    const uint8_t needs = part_needs(p);
    // error: SHOWN — a part with pieces this build lacks is refused as INVALID when sent (0xF1); one already in flash (saved by a later build) keeps its bytes and runs nothing here
    if ((needs & ~kBuilt) != 0) return false;
    memset(&l, 0, sizeof(l));
    l.layers = p.layers;
    l.window = p.double_window;
    l.bflags = p.bflags;
    l.needs = needs;
    memcpy(l.gyro, p.gyro, sizeof(l.gyro));
    // loop: HOT-PATH-OK — N = 3 layers, on a set change
    for (int i = 0; i < kMaxLayers; i++) l.lflags[i + 1] = p.layer[i].lflags;
    // loop: HOT-PATH-OK — N = 22 sources, on a set change
    for (int s = 0; s < kSources; s++) {
        Entry &e = l.self[s];
        e.src = (uint8_t) s;
        e.rows = kPress;
        e.t[0] = s < kFirstEdgeSource ? Target{kButton, (uint8_t) s, 0} : Target{kEdgeSelf, (uint8_t) (s - kFirstEdgeSource), 0};
    }
    unsigned at = 0;
    // loop: HOT-PATH-OK — N ≤ 61 entries, on a set change
    while (at < p.pool_len) {
        Entry e;
        // error: NOT-WORTH — part_valid ran first (the callers' order), so every entry parses
        if (!parse_entry(p.pool + at, p.pool_len - at, e)) break;
        at += e.len;
        if (!(e.rows & kPress)) e.t[0] = l.self[e.src].t[0];  // no Press row: Press is the button itself
        l.e[e.layer][e.src] = e;
    }
    // loop: HOT-PATH-OK — N = 4 layers x 22 sources, on a set change
    for (int L = 0; L <= kMaxLayers; L++)
        // loop: HOT-PATH-OK — N = 22 sources (inner)
        for (int s = 0; s < kSources; s++) l.with_ps[L] = l.with_ps[L] || (resolve(l, (uint8_t) L, s)->rows & kWithPs) != 0;
    return true;
}

// Hat 0 up, 1 up-right, 2 right .. 7 up-left, 8 none.
BUTTONS_INLINE bool hat_has(uint8_t hat, uint8_t src) {
    switch (src) {
        case kUp: return hat == 0 || hat == 1 || hat == 7;
        case kRight: return hat == 1 || hat == 2 || hat == 3;
        case kDown: return hat == 3 || hat == 4 || hat == 5;
        default: /* kLeft */ return hat == 5 || hat == 6 || hat == 7;
    }
}
BUTTONS_INLINE uint8_t hat_from(bool up, bool right, bool down, bool left) {
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
BUTTONS_INLINE uint32_t sources_down(const uint8_t *r) {
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
    d |= (uint32_t) ((b9 >> 4) & 0x0F) << kFirstEdgeSource;  // Fn left 0x10 .. right back 0x80
    return d;
}

// --- The per-button state (section 4.4) --------------------------------------

enum Phase : uint8_t {
    kIdle,
    kDeciding,    // down, waiting for the hold time (or, for a held-back PS, a chord or 0.5 s)
    kWaitDouble,  // released before the hold time, waiting double_window for a second press
    kPressOn,     // Press fired and follows the button until release
    kActOn,       // Hold, Double press or With PS on until release (SrcState::act says which)
};
// Live truth (GET 0xE8 [13], [14]).
enum EvType : uint8_t { kEvDeciding, kEvPress, kEvHold, kEvDouble, kEvWithPs, kEvToggleOn, kEvToggleOff, kEvTurbo, kEvTap };
enum EvPhase : uint8_t { kEvPhaseDeciding, kEvPhaseOn, kEvPhaseReleased };

// One key going out (a source has two: its Press target and its other action).
struct KeyCh {
    uint8_t usage, mods;
    bool on;       // the action holds it
    bool rel;      // let go, finishing the minimum hold and the modifier trail
    bool dropped;  // didn't fit in the six keys: counted once, never sent for this press
    uint32_t t_on, t_off;
};

struct SrcState {
    const Entry *e;  // the entry this press resolved to (at its start, section 4.3)
    uint8_t phase, act, layer;  // act: the row on in kActOn (1 Hold, 2 Double press, 3 With PS); layer: resolved in
    bool latch;      // Toggle
    bool tap;        // a tap of the Press target is on
    bool ps_used;    // the PS button only: a With PS chord used this press
    bool base_on;    // the Press channel before Turbo, last step (Turbo's phase starts on its rising edge)
    bool ch_on[2];   // channel outputs last step: 0 the Press target, 1 the other action
    uint32_t t0, t_rel, t_tap, t_turbo;
    uint32_t seq[2];  // a "while held" layer: when that channel went on (most recent wins)
    KeyCh key[2];
};

struct Event {  // the last event, for GET 0xE8 [12..22]
    uint8_t src, type, phase, layer;
    Target t;
    bool down;
    uint16_t hold_ms;
    uint32_t t0, t1;
};

struct Engine {
    SrcState s[kSources];
    uint32_t prev_down;  // sources down at the last step
    uint32_t act;        // sources whose action is on now (0xE8 [9..11])
    uint32_t pad;        // controller-button targets on: bit 0..17, the Edge's own buttons at 18..21
    uint32_t seq, last_us;
    bool started;
    uint8_t layer, switched;  // the active layer; the switched-on one (0 none)
    bool gyro_switch, gyro_on;
    uint8_t full;        // bit0 L2, bit1 R2: a source sends that target at full pull
    uint8_t pull_to[2];  // L2 / R2 sent with their own analog pull: the target button, 0xFF none
    uint8_t mods, keys[6], mouse_buttons;
    uint8_t dropped;     // keys past six, saturating (kept across a release-everything)
    int32_t acc[3];      // mouse motion remainders, x y wheel, in millionths of a count
    int32_t pend[3];     // whole mouse counts not sent yet (int16 range)
    int64_t gacc[2];     // gyro as mouse: x, y in counts << kGyroShift
    int8_t gstick[2];    // gyro as right stick: x, y offsets
    Event ev;
};

// Release everything (section 4.8): every output off, toggles and layers
// back to base, motion dropped. The engine is trivially copyable, so memset,
// with no temporary on the stack. The dropped-keys count stays.
BUTTONS_INLINE void reset(Engine &g) {
    const uint8_t dropped = g.dropped;
    memset(&g, 0, sizeof(g));
    g.dropped = dropped;
    g.pull_to[0] = g.pull_to[1] = 0xFF;
    g.ev.src = 0xFF;
}

BUTTONS_INLINE int32_t sat16(int32_t v) { return v > 32767 ? 32767 : v < -32767 ? -32767 : v; }

BUTTONS_INLINE void event(Engine &g, int s, const SrcState &st, uint8_t type, uint8_t phase, const Target &t, bool down,
                          uint32_t t1) {
    g.ev.src = (uint8_t) s;
    g.ev.type = type;
    g.ev.phase = phase;
    g.ev.layer = st.layer;
    g.ev.t = t;
    g.ev.down = down;
    g.ev.hold_ms = (uint16_t) (st.e && (st.e->rows & kHold) ? st.e->hold * 10 : 0);
    g.ev.t0 = st.t0;
    g.ev.t1 = t1;
}

// Press fires (at once, or as a tap): a Toggle latch flips here.
BUTTONS_INLINE uint8_t press_fires(SrcState &st) {
    if (st.e->rows & kToggle) {
        st.latch = !st.latch;
        return st.latch ? kEvToggleOn : kEvToggleOff;
    }
    return (st.e->rows & kTurbo) ? kEvTurbo : kEvPress;
}

BUTTONS_INLINE void tap(Engine &g, SrcState &st, int s, uint32_t now) {
    st.phase = kIdle;
    st.tap = true;
    st.t_tap = now;
    const uint8_t type = press_fires(st);
    event(g, s, st, type == kEvPress ? kEvTap : type, kEvPhaseReleased, st.e->t[0], false, st.t_rel);
}

// A key target's channel: on with the action, then finishing its pacing.
BUTTONS_INLINE void key_channel(KeyCh &k, const Target &t, bool on, uint32_t now) {
    if (on && t.kind == kKey) {
        if (!k.on) {
            k.on = true;
            k.rel = false;
            k.dropped = false;
            k.usage = t.a;
            k.mods = t.b;
            k.t_on = now;
        }
    } else if (k.on) {
        k.on = false;
        k.rel = true;
        k.t_off = now;
    }
}

BUTTONS_INLINE void add_motion(Engine &g, int axis, int32_t per_s, uint32_t dt) {
    g.acc[axis] += per_s * (int32_t) dt;  // ≤ 6375 x 20000 plus a remainder < 10^6: fits
    const int32_t whole = g.acc[axis] / 1000000;
    g.acc[axis] -= whole * 1000000;
    g.pend[axis] = sat16(g.pend[axis] + whole);
}

// The keyboard report from the key channels: modifiers OR together, the keys
// already down stay first (oldest first), new ones join in source order while
// there's room; the rest are counted once per press and never sent for it.
BUTTONS_INLINE void build_keys(Engine &g, uint32_t now) {
    uint8_t mods = 0, keep[6] = {0, 0, 0, 0, 0, 0};
    bool want[kSources][2];
    int n = 0;
    // loop: HOT-PATH-OK — N = 22 sources x 2 channels, per pass while a layout with keys runs
    for (int s = 0; s < kSources; s++) {
        // loop: HOT-PATH-OK — N = 2 channels (inner)
        for (int c = 0; c < 2; c++) {
            KeyCh &k = g.s[s].key[c];
            want[s][c] = false;
            if (!k.on && !k.rel) continue;
            const uint32_t lead = k.mods ? kKeyLeadUs : 0, el = now - k.t_on;
            bool usage_down, mods_down;
            if (k.on) {
                mods_down = true;
                usage_down = el >= lead;
            } else {
                const uint32_t held = k.t_off - k.t_on, least = lead + kKeyHoldUs;
                const uint32_t up = held > least ? held : least;
                usage_down = el >= lead && el < up;
                mods_down = el < up + (k.mods ? kKeyTrailUs : 0);
                if (!usage_down && !mods_down) k.rel = false;
            }
            if (mods_down) mods |= k.mods;
            want[s][c] = usage_down && k.usage != 0 && !k.dropped;
        }
    }
    // loop: HOT-PATH-OK — N = 6 keys x 44 channels, per pass while a layout with keys runs
    for (int i = 0; i < 6; i++) {
        const uint8_t u = g.keys[i];
        bool still = false;
        // loop: HOT-PATH-OK — N = 44 channels (inner)
        for (int s = 0; s < kSources && u != 0; s++)
            still = still || (want[s][0] && g.s[s].key[0].usage == u) || (want[s][1] && g.s[s].key[1].usage == u);
        if (still) keep[n++] = u;
    }
    // loop: HOT-PATH-OK — N = 44 channels x 6 keys, per pass while a layout with keys runs
    for (int s = 0; s < kSources; s++) {
        // loop: HOT-PATH-OK — N = 2 channels (inner)
        for (int c = 0; c < 2; c++) {
            if (!want[s][c]) continue;
            KeyCh &k = g.s[s].key[c];
            bool there = false;
            // loop: HOT-PATH-OK — N ≤ 6 keys (inner)
            for (int i = 0; i < n; i++) there = there || keep[i] == k.usage;
            if (there) continue;
            if (n < 6) {
                keep[n++] = k.usage;
            } else {
                k.dropped = true;
                // error: SHOWN — GET 0xE8 [46] keys dropped (the app's live view; the app warns of 7+ keys when you set them, sixKeys); more than six at once can't go in one keyboard report
                if (g.dropped < 255) g.dropped++;
            }
        }
    }
    g.mods = mods;
    memcpy(g.keys, keep, sizeof(keep));
}

// What every channel puts out now, the layer and gyro switches, mouse motion.
BUTTONS_INLINE void outputs(Engine &g, const Layout &l, uint32_t now) {
    uint32_t dt = g.started ? now - g.last_us : 0;
    if (dt > kMaxDtUs) dt = kMaxDtUs;
    g.started = true;
    g.last_us = now;
    g.pad = 0;
    g.act = 0;
    g.full = 0;
    g.pull_to[0] = g.pull_to[1] = 0xFF;
    g.mouse_buttons = 0;
    bool moving[3] = {false, false, false}, gyro_held_on = false, gyro_held_off = false;
    uint32_t best = 0;
    uint8_t held_layer = 0;
    // loop: HOT-PATH-OK — N = 22 sources, per pass while a set's buttons run
    for (int s = 0; s < kSources; s++) {
        SrcState &st = g.s[s];
        const bool busy = st.phase != kIdle || st.tap || st.latch;
        // A press keeps what it resolved to; an idle button shows the layer active now.
        const Entry &E = busy && st.e ? *st.e : *resolve(l, g.layer, s);
        const bool pressed = st.phase == kPressOn || st.tap;
        const bool base = (E.rows & kToggle) ? st.latch : pressed;
        if (base && !st.base_on) st.t_turbo = now;
        st.base_on = base;
        bool on0 = base;
        if (base && (E.rows & kTurbo)) {  // on for 1/(2r), off for 1/(2r), starting on
            const uint32_t period = 1000000u / E.turbo;
            on0 = (now - st.t_turbo) % period < period / 2;
        }
        // L2 / R2 pressing a controller button plainly: its analog pull goes
        // there (an L2 / R2 target uses it), as remap_apply does.
        const bool plain = !(E.rows & (kHold | kDouble | kToggle | kTurbo)) && st.phase != kActOn;
        const bool own_pull = (s == kL2 || s == kR2) && plain && E.t[0].kind == kButton;
        if (own_pull) g.pull_to[s == kR2 ? 1 : 0] = E.t[0].a;
        const bool on1 = st.phase == kActOn;
        // loop: HOT-PATH-OK — N = 2 channels per source
        for (int c = 0; c < 2; c++) {
            const bool on = c == 0 ? on0 : on1;
            const Target t = c == 0 ? E.t[0] : (on1 ? E.t[st.act] : Target{0, 0, 0});
            const bool rise = on && !st.ch_on[c];
            st.ch_on[c] = on;
            if (on) g.act |= 1u << s;
            key_channel(st.key[c], t, on, now);
            if (!on) continue;
            switch (t.kind) {
                case kButton:
                    g.pad |= 1u << t.a;
                    if ((t.a == kL2 || t.a == kR2) && !(c == 0 && own_pull)) g.full |= t.a == kL2 ? 1 : 2;
                    break;
                case kEdgeSelf: g.pad |= 1u << (18 + t.a); break;
                case kMouseButton: g.mouse_buttons |= (uint8_t) (1u << (t.a - 1)); break;
                case kMouseMove: {
                    const int axis = t.a <= 1 ? 1 : t.a <= 3 ? 0 : 2;  // up/down y, left/right x, wheel
                    const int32_t speed = t.a <= 3 ? t.b * 25 : t.b;  // pointer counts or wheel notches a second
                    add_motion(g, axis, (t.a == 0 || t.a == 2 || t.a == 5) ? -speed : speed, dt);
                    moving[axis] = true;
                    break;
                }
                case kLayer:
                    if (t.b == 0) {  // while held: the most recently held one wins
                        if (rise) st.seq[c] = ++g.seq;
                        if (st.seq[c] > best) {
                            best = st.seq[c];
                            held_layer = t.a;
                        }
                    } else if (rise) {  // switch: on, or back to base when it is the one on
                        g.switched = g.switched == t.a ? 0 : t.a;
                    }
                    break;
                case kGyro:
                    if (t.a == 0) gyro_held_on = true;
                    else if (t.a == 1) gyro_held_off = true;
                    else if (rise) g.gyro_switch = !g.gyro_switch;
                    break;
                default: break;  // kNothing; keys are their channel's
            }
        }
    }
    // loop: HOT-PATH-OK — N = 3 motion axes
    for (int i = 0; i < 3; i++)
        if (!moving[i]) g.acc[i] = 0;
    g.layer = best ? held_layer : g.switched;
    // Gyro (section 4.6): on in the layers where it is on, and with flags
    // bit2 only while a Gyro target holds or switches it on.
    bool gyro = false;
    if (l.bflags & kGyroUsed) {
        const bool here = l.lflags[g.layer] != 2;  // 0 same as base (on), 1 on, 2 off
        const bool needs_button = (l.gyro[2] & 0x04) != 0;
        gyro = here && !gyro_held_off && (needs_button ? (gyro_held_on || g.gyro_switch) : !g.gyro_switch);
    }
    if (!gyro) {  // off at once: nothing left in the accumulators
        g.gacc[0] = g.gacc[1] = 0;
        g.gstick[0] = g.gstick[1] = 0;
    }
    g.gyro_on = gyro;
    if (l.needs & kFeatKeysMouse) build_keys(g, now);
}

// One step: the buttons down now (sources_down) at time now. Edges start and
// end presses, timers decide holds, double presses and taps, then every
// output is worked out again. Runs every main-loop pass while a set's
// buttons run, and on every outgoing gamepad report.
BUTTONS_INLINE void step(Engine &g, const Layout &l, uint32_t down, uint32_t now) {
    const uint32_t rise = down & ~g.prev_down, fall = g.prev_down & ~down;
    g.prev_down = down;
    const uint8_t L = g.layer;  // every press starting in this step resolves in the layer active before it
    const bool ps_down = (down >> kPs) & 1u;
    // loop: HOT-PATH-OK — N = 22 sources, per pass while a set's buttons run
    for (int s = 0; s < kSources; s++) {
        SrcState &st = g.s[s];
        if ((rise >> s) & 1u) {
            if (st.phase == kWaitDouble) {  // the second press inside the window
                st.phase = kActOn;
                st.act = 2;
                st.t0 = now;
                event(g, s, st, kEvDouble, kEvPhaseOn, st.e->t[2], true, now);
            } else {
                const Entry *E = resolve(l, L, s);
                st.e = E;
                st.layer = L;
                st.t0 = now;
                st.tap = false;
                if (ps_down && s != kPs && (E->rows & kWithPs)) {  // 1. With PS: a chord, the PS press is used
                    st.phase = kActOn;
                    st.act = 3;
                    g.s[kPs].ps_used = true;
                    event(g, s, st, kEvWithPs, kEvPhaseOn, E->t[3], true, now);
                } else if (!(E->rows & (kHold | kDouble)) && !(s == kPs && l.with_ps[L])) {  // 2. at once
                    st.phase = kPressOn;
                    const uint8_t type = press_fires(st);
                    event(g, s, st, type, kEvPhaseOn, E->t[0], true, now);
                } else {  // 3. deciding
                    st.phase = kDeciding;
                    event(g, s, st, kEvDeciding, kEvPhaseDeciding, Target{0, 0, 0}, true, now);
                }
            }
        } else if ((fall >> s) & 1u) {
            st.t_rel = now;
            if (g.ev.src == s && g.ev.down) {
                g.ev.down = false;
                g.ev.t1 = now;
                g.ev.phase = kEvPhaseReleased;
            }
            if (st.phase == kDeciding) {
                if (s == kPs && st.ps_used) st.phase = kIdle;  // a chord used this PS press: PS never goes out
                else if (st.e->rows & kDouble) st.phase = kWaitDouble;
                else tap(g, st, s, now);
            } else if (st.phase == kPressOn || st.phase == kActOn) {
                st.phase = kIdle;
            }
            if (s == kPs) st.ps_used = false;
        }
        if (st.phase == kDeciding) {
            const uint32_t held = now - st.t0;
            const bool used = s == kPs && st.ps_used;
            if (st.e->rows & kHold) {
                if (!used && held >= st.e->hold * 10000u) {
                    st.phase = kActOn;
                    st.act = 1;
                    event(g, s, st, kEvHold, kEvPhaseOn, st.e->t[1], true, now);
                }
            } else if (s == kPs && l.with_ps[st.layer] && !used && held >= kPsPassUs) {  // a long PS press still works
                st.phase = kPressOn;
                const uint8_t type = press_fires(st);
                event(g, s, st, type, kEvPhaseOn, st.e->t[0], true, now);
            }
        } else if (st.phase == kWaitDouble && now - st.t_rel >= l.window * 10000u) {
            tap(g, st, s, now);
        }
        if (st.tap && now - st.t_tap >= kTapUs) st.tap = false;
    }
    outputs(g, l, now);
}

// Gyro as mouse or right stick (section 4.6), once per input report: h and v
// are the horizontal axis (yaw, or roll with gyro[1] = 1) and pitch with the
// zero already taken off, dt_us the time since the previous report.
// Unverified: which way the controller's axes count. Turning left and tilting
// up are taken as positive, so they move left and up (both negated); the
// invert flags turn either around.
BUTTONS_INLINE void gyro_input(Engine &g, const Layout &l, int32_t h, int32_t v, uint32_t dt_us) {
    // error: NOT-WORTH — not a failure: gyro is off in this layer or for this layout
    if (!g.gyro_on) return;
    if (dt_us > kMaxDtUs) dt_us = kMaxDtUs;
    int32_t w[2] = {-h, -v};
    if (l.gyro[2] & 0x01) w[0] = -w[0];
    if (l.gyro[2] & 0x02) w[1] = -w[1];
    const int32_t T = l.gyro[5] * 4;  // steadiness: small motion tightened, w x |w| / T below T
    // loop: HOT-PATH-OK — N = 2 axes, per input report while gyro is on
    for (int i = 0; i < 2; i++) {
        const int32_t a = w[i] < 0 ? -w[i] : w[i];
        if (T > 0 && a < T) w[i] = w[i] * a / T;
        const int32_t speed = l.gyro[3 + i];
        if (l.gyro[0] == 1) {
            g.gacc[i] += (int64_t) w[i] * (int64_t) dt_us * speed;
            const int64_t whole = g.gacc[i] >> kGyroShift;  // floor; the remainder stays (0 .. 2^kGyroShift - 1)
            g.gacc[i] -= whole * ((int64_t) 1 << kGyroShift);
            const int32_t c = whole > 32767 ? 32767 : whole < -32767 ? -32767 : (int32_t) whole;
            g.pend[i] = sat16(g.pend[i] + c);
        } else {
            const int32_t o = (w[i] * speed) >> kGyroStickShift;
            g.gstick[i] = (int8_t) (o > 127 ? 127 : o < -128 ? -128 : o);
        }
    }
}

// The layout's gamepad half on an outgoing input report copy (never
// interrupt_in_data), as remap_apply does for the legacy table: targets OR
// together, an L2 / R2 target takes the largest pull sent there (any other
// source counts as full), every source whose action isn't "itself, now" is
// cleared, and the hat, bytes 7 and 8 and byte 9's PS, touch pad and Edge
// bits are rebuilt (Mute is never a source and stays). Gyro as right stick
// adds to the right stick.
BUTTONS_INLINE void apply(const Engine &g, const Layout &l, uint8_t *r) {
    const uint32_t p = g.pad;
    uint8_t lt = (g.full & 1) ? 0xFF : 0, rt = (g.full & 2) ? 0xFF : 0;
    const uint8_t pl = r[4], pr = r[5];
    if (g.pull_to[0] == kL2 && pl > lt) lt = pl;
    if (g.pull_to[0] == kR2 && pl > rt) rt = pl;
    if (g.pull_to[1] == kL2 && pr > lt) lt = pr;
    if (g.pull_to[1] == kR2 && pr > rt) rt = pr;
    r[4] = lt;
    r[5] = rt;
#define BUTTONS_ON(t) (((p >> (t)) & 1u) != 0)
    r[7] = (uint8_t) (hat_from(BUTTONS_ON(kUp), BUTTONS_ON(kRight), BUTTONS_ON(kDown), BUTTONS_ON(kLeft)) |
                      (BUTTONS_ON(kSquare) ? 0x10 : 0) | (BUTTONS_ON(kCross) ? 0x20 : 0) |
                      (BUTTONS_ON(kCircle) ? 0x40 : 0) | (BUTTONS_ON(kTriangle) ? 0x80 : 0));
    r[8] = (uint8_t) ((BUTTONS_ON(kL1) ? 0x01 : 0) | (BUTTONS_ON(kR1) ? 0x02 : 0) | (BUTTONS_ON(kL2) ? 0x04 : 0) |
                      (BUTTONS_ON(kR2) ? 0x08 : 0) | (BUTTONS_ON(kCreate) ? 0x10 : 0) | (BUTTONS_ON(kOptions) ? 0x20 : 0) |
                      (BUTTONS_ON(kL3) ? 0x40 : 0) | (BUTTONS_ON(kR3) ? 0x80 : 0));
    r[9] = (uint8_t) ((r[9] & 0x0C) | (BUTTONS_ON(kPs) ? 0x01 : 0) | (BUTTONS_ON(kTouchPad) ? 0x02 : 0) |
                      (((p >> 18) & 0x0F) << 4));
#undef BUTTONS_ON
    if (g.gyro_on && l.gyro[0] == 2) {
        // loop: HOT-PATH-OK — N = 2 right-stick bytes, per report while gyro as right stick is on
        for (int i = 0; i < 2; i++) {
            const int v = r[2 + i] + g.gstick[i];
            r[2 + i] = (uint8_t) (v > 255 ? 255 : v < 0 ? 0 : v);
        }
    }
}

// The keyboard report (id 1, 8 bytes) and mouse report (id 2, 7 bytes) of
// the 129-byte descriptor (docs/TENSION_BUTTONS.md section 5.4).
BUTTONS_INLINE void keyboard_report(const Engine &g, uint8_t out[8]) {
    out[0] = g.mods;
    out[1] = 0;
    memcpy(out + 2, g.keys, 6);
}
// Takes up to one report's worth of the pending motion (int16 x, y; int8
// wheel) into out; the rest stays pending.
BUTTONS_INLINE void mouse_report(const Engine &g, uint8_t mb, uint8_t out[7], int32_t took[3]) {
    took[0] = g.pend[0];
    took[1] = g.pend[1];
    took[2] = g.pend[2] > 127 ? 127 : g.pend[2] < -127 ? -127 : g.pend[2];
    out[0] = mb;
    out[1] = (uint8_t) (took[0] & 0xFF);
    out[2] = (uint8_t) ((took[0] >> 8) & 0xFF);
    out[3] = (uint8_t) (took[1] & 0xFF);
    out[4] = (uint8_t) ((took[1] >> 8) & 0xFF);
    out[5] = (uint8_t) (int8_t) took[2];
    out[6] = 0;
}

}  // namespace buttons

// --- Board glue (src/buttons.cpp) -------------------------------------------

// Live truth (GET 0xE8, vendor interface only, docs/TENSION_BUTTONS.md section 6.4).
#define PICO_CMD_BUTTONS_LIVE 0xe8
// GET 0xE8 [4] while Feel it runs (flags bit2): its buttons aren't a slot's.
constexpr uint8_t kRunFeelIt = 0xFC;

// The layout that runs now (docs/TENSION_BUTTONS.md section 3.7, levels 1-2:
// Feel it, then the set chosen now), or nullptr: then the legacy remap table
// runs (levels 3-4). `run_id` is what GET 0xE8 [4] shows (a slot 0..7, 0xFD
// the app's, kRunFeelIt). Set by tension.cpp on every change of what runs;
// releases everything (section 4.8).
void buttons_run(const buttons::Layout *layout, uint8_t run_id);
// interrupt_loop, on the outgoing report copy after calib_apply: the running
// set's buttons, else remap_apply unchanged. RAM-resident on RP2350.
void buttons_apply(uint8_t *report);
// interrupt_loop, every pass before the gamepad report: the timers (hold,
// double press, tap, turbo, motion), gyro, and the keyboard and mouse
// reports. True: the gamepad output changed without a new input report (so
// real-time polling sends one). RAM-resident on RP2350.
bool buttons_task();
// on_bt_data, after each new input report lands in interrupt_in_data.
void buttons_on_input();
// 0xF6 0x21 [1, seconds 0..10] (section 5.5): pause keys and mouse; 0 ends it.
uint8_t buttons_pause(const uint8_t *a, uint16_t n);
bool buttons_paused();  // identity [59] bit1
// GET 0xE8: 63 bytes (0 when reqlen is shorter).
uint16_t buttons_live_get(uint8_t *b, uint16_t reqlen);

#endif  // DS5_BRIDGE_BUTTONS_H
