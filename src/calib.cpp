#include "calib.h"

#include <cmath>
#include <cstdio>
#include <cstddef>
#include <cstring>

#include "bt.h"
#include "flash_layout.h"
#include "flash_safe.h"
#include "hardware/flash.h"
#include "slots.h"

namespace {

constexpr uint32_t CALIB_MAGIC = 0x44533504u; // "DS5\x04"

struct __attribute__((packed)) CalibData {
    uint32_t  magic;
    uint8_t   ver;
    CalibBody body[kNumSlots];
    // proto 1.10: after the bodies, so calibration saved by older firmware
    // (0xFF here, the rest of the erased page) reads as Linear, and older
    // firmware still reads this sector's calibration (same magic/ver/bodies).
    StickCurve curve[kNumSlots][2];
};
static_assert(sizeof(CalibData) <= FLASH_PAGE_SIZE);
// The curves must stay after the bodies older firmware reads.
static_assert(offsetof(CalibData, curve) == 5 + kNumSlots * sizeof(CalibBody));
static_assert(CALIB_FLASH_OFFSET % FLASH_SECTOR_SIZE == 0);

CalibData g_cal{};

// Which parts of each slot's calibration differ from identity (skipped otherwise).
struct Active { bool stick[2], trigger[2], gyro; };
Active g_active[kNumSlots]{};

// Applied curves: the saved pair per slot, and a temporary pair (RAM only)
// for one slot while a game's profile is on.
StickCurveNodes g_nodes[kNumSlots][2]{};
StickCurve g_tmp_curve[2]{};
StickCurveNodes g_tmp_nodes[2]{};
int g_tmp_slot = -1; // -1: no temporary curves

void build_curves(int slot) {
    // loop: NOT-WORTH — 2 sticks, on load or when the host sets curves
    for (int i = 0; i < 2; i++) stick_curve_build(g_cal.curve[slot][i], g_nodes[slot][i]);
}

void set_linear(StickCurve &c) {
    c.type = kCurveLinear;
    // Default custom points (shown when the app switches to Custom): a gentle
    // slow start, valid on their own.
    static constexpr uint8_t kPts[8] = {51, 31, 102, 77, 153, 133, 204, 194};
    // loop: NOT-WORTH — 8 default points, rare (invalid curve)
    for (int i = 0; i < 8; i++) c.pt[i] = kPts[i];
}

// The connected controller's factory gyro bias (feature report 0x05, words at
// 1, 3, 5: pitch, yaw, roll; same layout and sanity limit as SDL and oled.cpp).
int16_t g_bias[3]{};
bool g_bias_known = false; // 0x05 seen for this connection (bias 0 if implausible)

const CalibData *flash_cal() { return reinterpret_cast<const CalibData *>(XIP_BASE + CALIB_FLASH_OFFSET); }

constexpr CalibStick kStickId{0, 0, 0, 128};

void set_identity(CalibBody &b) {
    // loop: NOT-WORTH — 2 sticks, on a reset / invalid load
    for (auto &s : b.stick) s = kStickId;
    // loop: NOT-WORTH — 2 triggers, on a reset / invalid load
    for (auto &t : b.trigger) t = {0, 255};
    // loop: NOT-WORTH — 3 gyro axes, on a reset / invalid load
    for (int i = 0; i < 3; i++) b.gyro[i] = 0;
}

bool valid(const CalibBody &b) {
    // loop: NOT-WORTH — 2 sticks, validating a host set or a load
    for (const auto &s : b.stick) {
        if (s.deadzone > 64 || s.outer < 64 || s.outer > 128 || s.deadzone >= s.outer) return false;
        if (s.cx < -40 || s.cx > 40 || s.cy < -40 || s.cy > 40) return false;
    }
    // loop: NOT-WORTH — 2 triggers, validating a host set or a load
    for (const auto &t : b.trigger) {
        if (t.lo >= t.hi || t.hi - t.lo < 64) return false;
    }
    return true;
}

void recompute_active(int slot) {
    const CalibBody &b = g_cal.body[slot];
    Active &a = g_active[slot];
    // loop: NOT-WORTH — 2 sticks, after a host set
    for (int i = 0; i < 2; i++) {
        a.stick[i] = memcmp(&b.stick[i], &kStickId, sizeof(CalibStick)) != 0;
        a.trigger[i] = b.trigger[i].lo != 0 || b.trigger[i].hi != 255;
    }
    a.gyro = b.gyro[0] != 0 || b.gyro[1] != 0 || b.gyro[2] != 0;
}

bool save() {
    alignas(4) uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xff, sizeof(page));
    memcpy(page, &g_cal, sizeof(g_cal));
    // error: SHOWN — passed up as NotSaved: CMD_RESULT_FAILED in 0xF1 (0x0D/0x14), the app's command error
    if (!flash_sector_write(CALIB_FLASH_OFFSET, page)) return false;
    return memcmp(flash_cal(), &g_cal, sizeof(g_cal)) == 0;
}

uint8_t clamp_u8(float v) { return (uint8_t) (v < 0 ? 0 : v > 255 ? 255 : lroundf(v)); }

// Center offset, then each half-axis normalized to its own reach (the side the
// center leans toward has less travel), a radial deadzone, and full tilt mapped
// to 0 and 255. The app's correctStick (src/protocol/calibration.ts) matches.
void stick(uint8_t *xy, const CalibStick &c) {
    const float x = (float) xy[0] - 128.0f - c.cx;
    const float y = (float) xy[1] - 128.0f - c.cy;
    const float outer = (float) c.outer;
    const float nx = x / fminf(outer, x >= 0 ? 127.0f - c.cx : 128.0f + c.cx);
    const float ny = y / fminf(outer, y >= 0 ? 127.0f - c.cy : 128.0f + c.cy);
    const float mag = sqrtf(nx * nx + ny * ny);
    const float dz = (float) c.deadzone / outer;
    if (mag <= dz) {
        xy[0] = xy[1] = 128;
        return;
    }
    const float s = fminf(1.0f, (mag - dz) / (1.0f - dz)) / mag;
    const float ox = nx * s, oy = ny * s;
    xy[0] = clamp_u8(128.0f + ox * (ox >= 0 ? 127.0f : 128.0f));
    xy[1] = clamp_u8(128.0f + oy * (oy >= 0 ? 127.0f : 128.0f));
}

int slot_now() {
    const int s = bt_get_slot();
    return s >= 0 && s < kNumSlots ? s : 0;
}

} // namespace

void calib_load() {
    memcpy(&g_cal, flash_cal(), sizeof(g_cal));
    // error: LOGGED-OK — nothing; boot: a blank/old calibration sector means no calibration yet (identity), which the app shows as not calibrated
    bool ok = g_cal.magic == CALIB_MAGIC && g_cal.ver == kCalibProtoVer;
    // loop: NOT-WORTH — boot-only, kNumSlots = 4
    for (int i = 0; ok && i < kNumSlots; i++) ok = valid(g_cal.body[i]);
    if (!ok) {
        g_cal.magic = CALIB_MAGIC;
        g_cal.ver = kCalibProtoVer;
        // loop: NOT-WORTH — boot-only, 4 slot bodies
        for (auto &b : g_cal.body) set_identity(b);
    }
    // Curves are checked one by one: erased (older save) or bad = Linear.
    // loop: NOT-WORTH — boot-only, 4 slots
    for (int i = 0; i < kNumSlots; i++) {
        // loop: NOT-WORTH — boot-only, 2 curves per slot
        for (auto &c : g_cal.curve[i]) {
            if (!ok || !stick_curve_valid(c)) set_linear(c);
        }
        build_curves(i);
    }
    // loop: NOT-WORTH — boot-only, 4 slots
    for (int i = 0; i < kNumSlots; i++) recompute_active(i);
    printf("[Calib] loaded (%s)\n", ok ? "saved" : "identity");
}

void calib_service() {
    if (!bt_is_connected()) {
        g_bias_known = false;
        return;
    }
    if (g_bias_known) return;
    // lifetime: OK — no heap until 0x05 arrives (empty vector), then one copy per connection (g_bias_known)
    const auto d = bt_peek_feature(0x05);
    if (d.size() < 35) return; // not arrived yet
    bool plausible = true;
    int16_t b[3];
    // loop: NOT-WORTH — 3 gyro words, once per connection when 0x05 lands
    for (int i = 0; i < 3; i++) {
        b[i] = (int16_t) ((uint16_t) d[1 + 2 * i] | ((uint16_t) d[2 + 2 * i] << 8));
        if (b[i] > 1024 || b[i] < -1024) plausible = false;
    }
    // Hosts fall back to raw when the factory cal is implausible (SDL), so
    // then the whole rest reading is ours to remove.
    // loop: NOT-WORTH — 3 gyro words, once per connection
    for (int i = 0; i < 3; i++) g_bias[i] = plausible ? b[i] : 0;
    g_bias_known = true;
}

uint8_t calib_trigger(int i, uint8_t raw) {
    const int slot = slot_now();
    // error: NOT-WORTH — an uncalibrated trigger is its raw value (identity), not a failure
    if (!g_active[slot].trigger[i]) return raw;
    const auto &t = g_cal.body[slot].trigger[i];
    return clamp_u8(((float) raw - t.lo) * 255.0f / (float) (t.hi - t.lo));
}

void calib_apply(uint8_t *r) {
    const int slot = slot_now();
    const Active &a = g_active[slot];
    const CalibBody &b = g_cal.body[slot];
    if (a.stick[0]) stick(r + 0, b.stick[0]);
    if (a.stick[1]) stick(r + 2, b.stick[1]);
    // Response curves on the calibrated sticks (n == 0: Linear, skipped).
    const StickCurveNodes *cn = slot == g_tmp_slot ? g_tmp_nodes : g_nodes[slot];
    stick_curve_apply(r + 0, cn[0]);
    stick_curve_apply(r + 2, cn[1]);
    // loop: HOT-PATH-OK — 2 triggers per input report; calib_apply is not CORE0_RAM_FUNC
    for (int i = 0; i < 2; i++) r[4 + i] = calib_trigger(i, r[4 + i]);
    if (a.gyro && g_bias_known) {
        // loop: HOT-PATH-OK — 3 gyro axes per input report, only with gyro calibration on
        for (int i = 0; i < 3; i++) {
            int16_t v;
            memcpy(&v, r + 15 + 2 * i, 2);
            int32_t c = (int32_t) v - ((int32_t) b.gyro[i] - g_bias[i]);
            if (c > 32767) c = 32767;
            if (c < -32768) c = -32768;
            v = (int16_t) c;
            memcpy(r + 15 + 2 * i, &v, 2);
        }
    }
}

CalibSet calib_set(uint8_t slot, const uint8_t *body) {
    // error: SHOWN — CMD_RESULT_INVALID in 0xF1 (cmd.cpp 0x0D/0x14), the app's command error
    if (slot >= kNumSlots) return CalibSet::Invalid;
    CalibBody b;
    memcpy(&b, body, sizeof(b));
    // error: SHOWN — CMD_RESULT_INVALID in 0xF1 (cmd.cpp 0x0D/0x14), the app's command error
    if (!valid(b)) return CalibSet::Invalid;
    g_cal.body[slot] = b;
    recompute_active(slot);
    // error: SHOWN — NotSaved: CMD_RESULT_FAILED in 0xF1, the app's command error
    return save() ? CalibSet::Ok : CalibSet::NotSaved;
}

void calib_get(uint8_t slot, uint8_t out[kCalibBodySize]) {
    memcpy(out, &g_cal.body[slot < kNumSlots ? slot : 0], kCalibBodySize);
}

CalibSet curves_set(uint8_t slot, const StickCurve *pair, bool temporary) {
    // error: SHOWN — CMD_RESULT_INVALID in 0xF1 (cmd.cpp 0x0D/0x14), the app's command error
    if (slot >= kNumSlots || !stick_curve_valid(pair[0]) || !stick_curve_valid(pair[1])) return CalibSet::Invalid;
    if (temporary) {
        // RAM only: g_cal (what saves write) keeps the saved pair.
        // loop: NOT-WORTH — 2 curves, on a host curves set
        for (int i = 0; i < 2; i++) {
            g_tmp_curve[i] = pair[i];
            stick_curve_build(pair[i], g_tmp_nodes[i]);
        }
        g_tmp_slot = slot;
        return CalibSet::Ok;
    }
    g_tmp_slot = -1; // a saved set ends the temporary curves
    g_cal.curve[slot][0] = pair[0];
    g_cal.curve[slot][1] = pair[1];
    build_curves(slot);
    // error: SHOWN — NotSaved: CMD_RESULT_FAILED in 0xF1, the app's command error
    return save() ? CalibSet::Ok : CalibSet::NotSaved;
}

void curves_get(uint8_t slot, StickCurve out[2]) {
    const int s = slot < kNumSlots ? slot : 0;
    out[0] = g_cal.curve[s][0];
    out[1] = g_cal.curve[s][1];
}

bool curves_get_active(uint8_t slot, StickCurve out[2]) {
    if (slot == g_tmp_slot) {
        out[0] = g_tmp_curve[0];
        out[1] = g_tmp_curve[1];
        return true;
    }
    curves_get(slot, out);
    return false;
}

void curves_revert() { g_tmp_slot = -1; }

bool curves_temporary() { return g_tmp_slot >= 0; }
