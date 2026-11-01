//
// 48 kHz USB haptics -> 3 kHz Bluetooth haptics (upstream awalol 4929996).
// The ratio is exactly 16:1. Each 3 kHz sample is the mean of the 16 frames
// of its block (a 16-tap boxcar), not just one of them: picking every 16th
// frame (as upstream does, and the WDL resampler before it) folded everything
// from 1.5 to 24 kHz into the haptics band unfiltered. The boxcar costs one
// add per frame and nulls exactly the frequencies that fold onto 0 Hz (every
// multiple of 3 kHz); tests/host/test_haptics_decimate.cpp measures it:
// -0.4 dB at 500 Hz, -1.6 dB at 1 kHz; content that would land at 100 Hz
// (100 Hz from a multiple of 3 kHz) at least 29 dB down, at 500 Hz at least
// 14 dB down.
//
// Pure, no Pico SDK: it is inlined into audio_loop (RAM-resident, core0).
//

#ifndef DS5_BRIDGE_HAPTICS_DECIMATE_H
#define DS5_BRIDGE_HAPTICS_DECIMATE_H

#include <algorithm>
#include <cstdint>

constexpr uint8_t kHapticsDecimation = 16;  // 48000 / 3000

struct HapticDecimator {
    uint8_t phase = 0;
    float sum_l = 0.0f, sum_r = 0.0f;
    // Call once per 48 kHz frame (left, right). True when a 3 kHz sample is
    // ready: l and r then hold the mean of the last 16 frames. The block
    // carries across USB packets of any length.
    __attribute__((always_inline)) inline bool push(float &l, float &r) {
        sum_l += l;
        sum_r += r;
        // error: NOT-WORTH — not a failure: false = this block isn't complete yet
        if (++phase < kHapticsDecimation) return false;
        l = sum_l * (1.0f / kHapticsDecimation);
        r = sum_r * (1.0f / kHapticsDecimation);
        phase = 0;
        sum_l = sum_r = 0.0f;
        return true;
    }
};

// One haptic sample (-1..1 after Auto Haptics) to the int8 the DS5 plays, with
// the grip gain applied after the rate change as upstream a55fd46. Rounded to
// the nearest step (half away from zero); truncation made |x * 127 * gain| < 1
// a dead zone and rounded the two signs differently. No lrintf: libm is in flash.
__attribute__((always_inline)) inline int8_t haptic_to_s8(float x, float gain) {
    const float v = std::clamp(x, -1.0f, 1.0f) * 127.0f * gain;
    const int i = static_cast<int>(v < 0.0f ? v - 0.5f : v + 0.5f);
    return static_cast<int8_t>(std::clamp(i, -128, 127));
}

#endif
