//
// The speaker's 512 -> 480 rate change (16:15): each 10.67 ms block of the
// PC's 48 kHz audio becomes one 480-sample Opus frame, which the controller
// plays in 10.67 ms (a 45 kHz effective rate; src/report_clock.h). It replaces
// the WDL resampler in linear-interpolation mode, which rolled off the top
// octave (-5.3 dB at 20 kHz) and left images that folded to f - 3 kHz at
// -11 to -24 dB.
//
// A polyphase FIR: a Kaiser-windowed sinc designed at 15 x 48 kHz, 15 phases
// of kTaps taps, -6 dB at 22.5 kHz (half the 45 kHz output rate). Measured by
// tests/host/test_speaker_resample.cpp: flat within 0.05 dB to 20 kHz, images
// of 1-20 kHz tones at least 60 dB down. Each phase sums to exactly 1 (no DC
// ripple between phases). Delay: 15.5 input samples (0.32 ms).
//
// 512 = 16 x 32 and 480 = 15 x 32, so every block starts at phase 0; only the
// last kTaps - 1 input frames carry over. Float multiply-adds on the M33's FPU
// (no soft-double, unlike WDL's double phase), always_inline into core1's
// RAM-resident speaker_proc; the table and history are RAM variables.
// speaker_fir_init() fills the table once, on core0 before core1 starts
// (it uses double sin/sqrt, which run from flash). Pure, no Pico SDK.
//

#ifndef DS5_BRIDGE_SPEAKER_RESAMPLE_H
#define DS5_BRIDGE_SPEAKER_RESAMPLE_H

#include <cmath>
#include <cstring>

namespace speaker_fir {
constexpr int kIn = 512;                   // stereo frames in per block
constexpr int kOut = 480;                  // stereo frames out per block
constexpr int kPhases = 15;
constexpr int kTaps = 32;                  // per phase
constexpr double kCutoffHz = 22500.0;      // -6 dB point, at the 48 kHz input
constexpr double kBeta = 5.0;              // Kaiser window
}  // namespace speaker_fir

// coef[r][t]: phase r, tap t (t = 0 multiplies the newest input frame).
// Not const: a const table would sit in flash.
inline float g_speaker_fir[speaker_fir::kPhases][speaker_fir::kTaps];

// Kaiser window's I0, by its series (boot only).
inline double speaker_fir_i0(double x) {
    double sum = 1.0, term = 1.0;
    // loop: NOT-WORTH — boot only, about 20 terms to converge
    for (int k = 1; k < 50; k++) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

// The prototype filter's tap m of kPhases * kTaps (boot only).
inline double speaker_fir_tap(int m) {
    using namespace speaker_fir;
    constexpr int n = kPhases * kTaps;
    constexpr double fs = 48000.0 * kPhases;
    constexpr double pi = 3.14159265358979323846;
    const double x = 2.0 * kCutoffHz / fs * (m - (n - 1) / 2.0);
    const double sinc = x == 0.0 ? 1.0 : std::sin(pi * x) / (pi * x);
    const double u = 2.0 * m / (n - 1) - 1.0;
    return sinc * speaker_fir_i0(kBeta * std::sqrt(1.0 - u * u)) / speaker_fir_i0(kBeta);
}

// No table on the stack (core0's is small): each phase is computed twice,
// once for its sum.
inline void speaker_fir_init() {
    using namespace speaker_fir;
    // loop: NOT-WORTH — boot only, 15 phases x 32 taps
    for (int r = 0; r < kPhases; r++) {
        double sum = 0.0;
        // loop: NOT-WORTH — boot only, 32 taps
        for (int t = 0; t < kTaps; t++) sum += speaker_fir_tap(kPhases * t + r);
        // loop: NOT-WORTH — boot only, 32 taps
        for (int t = 0; t < kTaps; t++) g_speaker_fir[r][t] = (float) (speaker_fir_tap(kPhases * t + r) / sum);
    }
}

struct SpeakerResampler {
    // The last kTaps - 1 frames of the previous block, then this block (L/R interleaved).
    float buf[(speaker_fir::kTaps - 1 + speaker_fir::kIn) * 2] = {};

    // in: 512 stereo frames, out: 480 stereo frames.
    __attribute__((always_inline)) inline void process(const float *in, float *out) {
        using namespace speaker_fir;
        constexpr int hist = (kTaps - 1) * 2;
        memcpy(buf + hist, in, sizeof(float) * kIn * 2);
        int r = 0, j = 0;  // output k: phase k mod 15, newest input frame k + k / 15
        // loop: HOT-PATH-OK — N = 480 output frames x 32 taps, core1 RAM code, float MACs
        for (int k = 0; k < kOut; k++) {
            const float *c = g_speaker_fir[r];
            const float *x = buf + hist + j * 2;
            float l = 0.0f, rr = 0.0f;
            // loop: HOT-PATH-OK — N = kTaps = 32 taps
            for (int t = 0; t < kTaps; t++) {
                l += c[t] * x[-2 * t];
                rr += c[t] * x[-2 * t + 1];
            }
            out[k * 2] = l;
            out[k * 2 + 1] = rr;
            j++;
            if (++r == kPhases) { r = 0; j++; }
        }
        memmove(buf, buf + kIn * 2, sizeof(float) * hist);
    }
};

#endif  // DS5_BRIDGE_SPEAKER_RESAMPLE_H
