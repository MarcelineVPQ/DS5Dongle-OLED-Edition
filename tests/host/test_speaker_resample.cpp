// src/speaker_resample.h: the speaker's 512 -> 480 polyphase FIR. Tones go
// through the real block code; the output is read as the controller plays it
// (480 samples per 10.67 ms = 45 kHz). Passband, the roll-off, the image
// that used to fold to f - 3 kHz, DC, and continuity across blocks.
#include "speaker_resample.h"
#include "test.h"
#include <cmath>
#include <vector>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr int kWarm = 2, kBlocks = 30;  // 30 blocks = 14400 output samples: every test tone is bin-centred

// Amplitude of `f` Hz in x (sampled at fs), Goertzel.
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

// A 0.5-amplitude tone (left; right = minus it) through the resampler, the
// left output after the warm-up blocks.
std::vector<double> run(double f, std::vector<double> *right = nullptr) {
    speaker_fir_init();
    static SpeakerResampler rs;  // static: 4 KB
    rs = SpeakerResampler{};
    float in[speaker_fir::kIn * 2], out[speaker_fir::kOut * 2];
    std::vector<double> l;
    long n = 0;
    for (int b = 0; b < kWarm + kBlocks; b++) {
        for (int i = 0; i < speaker_fir::kIn; i++, n++) {
            const float v = (float) (0.5 * std::sin(2.0 * kPi * f * (double) n / 48000.0));
            in[i * 2] = v;
            in[i * 2 + 1] = -v;
        }
        rs.process(in, out);
        if (b < kWarm) continue;
        for (int i = 0; i < speaker_fir::kOut; i++) {
            l.push_back(out[i * 2]);
            if (right) right->push_back(out[i * 2 + 1]);
        }
    }
    return l;
}

double db(double a) { return 20.0 * std::log10(a); }
}  // namespace

TEST(speaker_resample_passband_flat_to_20k) {
    for (double f : {1000.0, 5000.0, 10000.0, 15000.0, 20000.0}) {
        const double g = db(amplitude(run(f), f, 45000.0) / 0.5);
        if (std::fabs(g) >= 0.05) std::fprintf(stderr, "  %g Hz: %.3f dB\n", f, g);
        CHECK(std::fabs(g) < 0.05);
    }
}

// The old linear interpolation left the image at 48 kHz - f, which the 45 kHz
// playout folds to |f - 3 kHz| (10 kHz -> 7 kHz at -24 dB, 20 kHz -> 17 kHz at -11 dB).
TEST(speaker_resample_images_60_db_down) {
    for (double f : {1000.0, 5000.0, 10000.0, 15000.0, 20000.0}) {
        const auto y = run(f);
        const double rel = db(amplitude(y, std::fabs(f - 3000.0), 45000.0) / amplitude(y, f, 45000.0));
        if (rel > -60.0) std::fprintf(stderr, "  %g Hz: image %.1f dB\n", f, rel);
        CHECK(rel <= -60.0);
    }
}

// Above 20 kHz it rolls off towards -6 dB at 22.5 kHz (half the 45 kHz
// playout rate; that point itself can't be read at the output's Nyquist).
TEST(speaker_resample_rolls_off_above_20k) {
    const double g21 = db(amplitude(run(21000.0), 21000.0, 45000.0) / 0.5);
    const double g22 = db(amplitude(run(22000.0), 22000.0, 45000.0) / 0.5);
    CHECK(g21 > -1.0 && g21 < -0.4);  // design: -0.67 dB
    CHECK(g22 > -4.0 && g22 < -2.8);  // design: -3.4 dB
}

TEST(speaker_resample_dc_and_channels) {
    speaker_fir_init();
    for (int r = 0; r < speaker_fir::kPhases; r++) {
        double s = 0;
        for (int t = 0; t < speaker_fir::kTaps; t++) s += g_speaker_fir[r][t];
        CHECK(std::fabs(s - 1.0) < 1e-5);  // every phase passes DC unchanged
    }
    std::vector<double> r;
    const auto l = run(1000.0, &r);
    for (size_t i = 0; i < l.size(); i++) CHECK(std::fabs(l[i] + r[i]) < 1e-6);  // channels independent
}

// No step at block edges: a low tone's output stays smooth across all 30 edges.
TEST(speaker_resample_continuous_across_blocks) {
    const auto y = run(100.0);
    double worst = 0;
    for (size_t i = 1; i < y.size(); i++) worst = std::fmax(worst, std::fabs(y[i] - y[i - 1]));
    // 100 Hz at 0.5 moves at most 2 pi 100 / 45000 * 0.5 = 0.007 per sample
    CHECK(worst < 0.0075);
}
