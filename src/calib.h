//
// Stick, trigger and gyro calibration, kept in the dongle (its own flash
// sector), one per pairing slot, and applied to the OUTGOING host report only,
// like remap: the raw interrupt_in_data (OLED, live input 0xF0) stays
// uncorrected, so the app can measure from it. Nothing is ever written to the
// controller.
//

#ifndef DS5_BRIDGE_CALIB_H
#define DS5_BRIDGE_CALIB_H

#include <cstdint>

#include "stick_curve.h"

constexpr uint8_t kCalibProtoVer = 2;

struct __attribute__((packed)) CalibStick {
    int8_t  cx, cy;     // resting center, as an offset from 128
    uint8_t deadzone;   // radius (0..64, of 128) treated as centered
    uint8_t outer;      // radius (64..128) that counts as full tilt
};
struct __attribute__((packed)) CalibTrigger {
    uint8_t lo, hi;     // raw value read as 0 and as 255 (lo < hi)
};
struct __attribute__((packed)) CalibBody {
    CalibStick   stick[2];    // left, right
    CalibTrigger trigger[2];  // L2, R2
    // The raw gyro reading at rest, in report order (int16 at report offsets
    // 15, 17, 19: pitch, yaw, roll); 0, 0, 0 = not calibrated. Hosts already
    // subtract the controller's factory bias (feature report 0x05), so the
    // dongle subtracts only what's left: rest - factory bias.
    int16_t      gyro[3];
};
constexpr uint16_t kCalibBodySize = sizeof(CalibBody); // 18

void calib_load();
// Main loop: picks up the connected controller's factory gyro bias.
void calib_service();
// Correct a 63-byte host report copy in place (sticks [0..3], triggers [4,5],
// gyro int16 at [15,17,19]) with the current slot's calibration. Each part is
// a no-op while it is identity.
void calib_apply(uint8_t *report);
// One trigger (0 L2, 1 R2) as calib_apply corrects it: lo..hi stretched to
// 0..255, the raw value when that trigger isn't calibrated. Tension's stages
// read it (protocol 1.23).
uint8_t calib_trigger(int i, uint8_t raw);
enum class CalibSet : uint8_t { Ok, Invalid, NotSaved };
CalibSet calib_set(uint8_t slot, const uint8_t *body);
void calib_get(uint8_t slot, uint8_t out[kCalibBodySize]);

// Stick response curves (proto 1.10), one pair (left, right) per pairing slot,
// stored after the calibration bodies in the same flash sector (older saves
// and erased flash read as Linear). Applied in calib_apply after the stick
// calibration step. A temporary pair (a game's profile) lives in RAM only and
// ends with curves_revert() (0xF6 0x12), a reboot or a saved set; a saved set
// never writes the temporary pair.
constexpr uint8_t kCurveProtoVer = 1;
// left, right; temporary = RAM only for that slot.
CalibSet curves_set(uint8_t slot, const StickCurve *pair, bool temporary);
void curves_get(uint8_t slot, StickCurve out[2]);         // saved pair
bool curves_get_active(uint8_t slot, StickCurve out[2]);  // in use now; true = temporary
void curves_revert();
bool curves_temporary();

#endif // DS5_BRIDGE_CALIB_H
