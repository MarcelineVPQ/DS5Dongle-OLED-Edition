// src/haptics_decimate.h: 48 kHz -> 3 kHz haptics as the mean of each
// 16-frame block (a boxcar), and the rounding to the controller's int8.
// Tones go through the decimator; the 3 kHz output is read with a Goertzel
// (3000 output samples = 1 Hz bins, every test frequency bin-centred).
#include "haptics_decimate.h"
#include "test.h"
#include <cmath>
#include <cstdint>
#include <vector>

namespace {
constexpr double kPi = 3.14159265358979323846;

double amplitude(const std::vector<double> &x, double f, double fs) {
    const double w = 2.0 * kPi * f / fs, c = 2.0 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (double v : x) {
        const double s = v + c * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    const double re = s1 - s2 * std::cos(w), im = s2 * std::sin(w);
    return 2.0 * std::sqrt(re * re + im * im) / (double) x.size();
}

// One second of a 0.5 tone at f (48 kHz) through the decimator: 3000 samples.
std::vector<double> decimate(double f) {
    HapticDecimator d;
    std::vector<double> out;
    for (int n = 0; n < 48000; n++) {
        float l = (float) (0.5 * std::sin(2.0 * kPi * f * n / 48000.0)), r = -l;
        if (d.push(l, r)) out.push_back(l);
    }
    return out;
}

double db(double a) { return 20.0 * std::log10(a); }
}  // namespace

TEST(haptics_decimate_one_sample_per_16_frames_across_any_reads) {
    HapticDecimator d;
    int taken = 0, frames = 0;
    // Uneven reads: 1, 2, ... 48 frames, repeated; the block carries across them.
    for (int rep = 0; rep < 10; rep++)
        for (int len = 1; len <= 48; len++)
            for (int i = 0; i < len; i++, frames++) {
                float l = 0, r = 0;
                const bool t = d.push(l, r);
                CHECK_EQ(t, frames % 16 == 15);
                taken += t;
            }
    CHECK_EQ(taken, frames / 16);
}

TEST(haptics_decimate_mean_of_the_block) {
    HapticDecimator d;
    float l = 0, r = 0;
    for (int i = 0; i < 15; i++) {
        l = (float) i; r = 1.0f;
        CHECK(!d.push(l, r));
    }
    l = 15.0f; r = 1.0f;
    CHECK(d.push(l, r));
    CHECK(l == 7.5f);  // (0 + ... + 15) / 16
    CHECK(r == 1.0f);  // DC passes unchanged
}

// The boxcar's response, sin(16 pi f / 48k) / (16 sin(pi f / 48k)).
TEST(haptics_decimate_passband) {
    const struct { double f, want_db; } pts[] = {{100, -0.02}, {250, -0.10}, {500, -0.40}, {1000, -1.64}};
    for (const auto &p : pts) {
        const double g = db(amplitude(decimate(p.f), p.f, 3000.0) / 0.5);
        if (std::fabs(g - p.want_db) >= 0.05) std::fprintf(stderr, "  %g Hz: %.3f dB\n", p.f, g);
        CHECK(std::fabs(g - p.want_db) < 0.05);
    }
}

// What used to fold in unattenuated: content near a multiple of 3 kHz lands
// near 0 Hz, where the grips respond most.
TEST(haptics_decimate_alias_rejection) {
    for (int k = 1; k <= 7; k++) {
        for (double off : {-100.0, 100.0}) {
            const double f = 3000.0 * k + off;
            const double rel = db(amplitude(decimate(f), 100.0, 3000.0) / 0.5);
            if (rel > -29.0) std::fprintf(stderr, "  %g Hz -> 100 Hz: %.1f dB\n", f, rel);
            CHECK(rel <= -29.0);
        }
        const double at = db(amplitude(decimate(3000.0 * k + 500.0), 500.0, 3000.0) / 0.5);
        CHECK(at <= -14.0);
    }
    // A multiple of 3 kHz itself (it would land on 0 Hz) is nulled.
    const auto y = decimate(6000.0);
    double peak = 0;
    for (double v : y) peak = std::fmax(peak, std::fabs(v));
    CHECK(peak < 1e-4);
}

// Rounded to the nearest step, half away from zero, symmetric for both signs.
TEST(haptics_to_s8_rounds) {
    const struct { float x, g; int want; } table[] = {
        {0.0f, 1.0f, 0},
        {0.3f / 127.0f, 1.0f, 0},     {-0.3f / 127.0f, 1.0f, 0},
        {0.6f / 127.0f, 1.0f, 1},     {-0.6f / 127.0f, 1.0f, -1},   // was 0: the dead zone
        {1.4f / 127.0f, 1.0f, 1},     {-1.4f / 127.0f, 1.0f, -1},
        {1.6f / 127.0f, 1.0f, 2},     {-1.6f / 127.0f, 1.0f, -2},   // was 1 / -1
        {1.0f, 1.0f, 127},            {-1.0f, 1.0f, -127},
        {1.5f, 1.0f, 127},            {-1.5f, 1.0f, -127},          // clamped before the gain
        {0.5f, 4.0f, 127},            {-0.5f, 4.0f, -128},          // gain clips at int8
        {0.5f, 0.0f, 0},                                            // grip off
        {0.25f, 1.0f, 32},            {-0.25f, 1.0f, -32},          // 31.75
    };
    for (const auto &t : table) {
        const int got = haptic_to_s8(t.x, t.g);
        if (got != t.want) std::fprintf(stderr, "  haptic_to_s8(%g, %g) = %d, want %d\n", t.x, t.g, got, t.want);
        CHECK_EQ(got, t.want);
    }
}
