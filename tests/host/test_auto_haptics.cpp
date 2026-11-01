// src/auto_haptics.h: Audio Auto Haptics (loteran/DS5Dongle 5d6bc2f): the
// low-pass cut-offs, the envelope's attack and 50 ms release, Mix's limiter
// (unchanged below the knee), Fallback's 15 ms crossfade, and the modes.
#include "auto_haptics.h"
#include "test.h"
#include <cmath>
#include <cstdint>

// The four low-pass settings' -3 dB points, as the comment states them.
TEST(auto_haptics_low_pass_cutoffs) {
    const double hz[4] = {79.8, 160.1, 240.2, 401.7};
    for (int i = 0; i < 4; i++) {
        const double a = AutoHaptics::kLowPass[i];
        const double w = 2.0 * M_PI * hz[i] / 48000.0;
        // |H| of y += a (x - y) at w
        const double re = 1.0 - (1.0 - a) * std::cos(w), im = (1.0 - a) * std::sin(w);
        const double db = 20.0 * std::log10(a / std::sqrt(re * re + im * im));
        CHECK(std::fabs(db + 3.01) < 0.01);
    }
}

// Step response of the envelope (lp_a = 1: no low-pass in front): the attack
// is near-instant, the release falls to 1/e in 50 ms (2400 samples).
TEST(auto_haptics_envelope_attack_and_50_ms_release) {
    AutoHaptics ah;
    float al, ar;
    ah.next(0.5f, 0.5f, 1.0f, 1.0f, al, ar);
    CHECK(std::fabs(ah.env_l - 0.2f) < 1e-6f);  // 0.40 of the step in one sample
    for (int i = 0; i < 100; i++) ah.next(0.5f, 0.5f, 1.0f, 1.0f, al, ar);
    CHECK(std::fabs(ah.env_l - 0.5f) < 1e-5f);
    int n = 0;
    while (ah.env_l > 0.5f / (float) M_E && n < 48000) {
        ah.next(0.0f, 0.0f, 1.0f, 1.0f, al, ar);
        n++;
    }
    CHECK(n >= 2398 && n <= 2402);
    // Through an 80 Hz tone the envelope now holds (it used to follow each cycle).
    AutoHaptics t;
    float lo = 1.0f;
    for (int i = 0; i < 48000; i++) {
        t.next((float) std::sin(2.0 * M_PI * 80.0 * i / 48000.0) * 0.5f, 0.0f, 1.0f, 1.0f, al, ar);
        if (i > 24000) lo = std::fmin(lo, t.env_l);
    }
    CHECK(lo > 0.45f);  // within 1 dB of the 0.5 peak all the time
}

TEST(auto_haptics_mix_adds_and_limits_only_near_full_scale) {
    AutoHaptics ah;
    // Nothing derived: the game's haptics pass unchanged up to the knee.
    for (float v : {0.0f, 0.1f, 0.5f, 0.7f, -0.3f, -0.7f}) {
        float hl = v, hr = -v;
        ah.combine(kAutoMix, false, 0.0f, 0.0f, hl, hr);
        CHECK(hl == v && hr == -v);
    }
    // A plain sum below the knee.
    float hl = 0.25f, hr = -0.25f;
    ah.combine(kAutoMix, false, 0.25f, -0.25f, hl, hr);
    CHECK(hl == 0.5f && hr == -0.5f);
    // Above it: continuous, slope 1 at the knee, rising, never reaching 1, symmetric.
    CHECK(std::fabs(mix_limit(0.7001f) - 0.7001f) < 1e-6f);
    CHECK(std::fabs(mix_limit(1.0f) - 0.85f) < 1e-6f);
    float prev = 0.7f;
    for (float m = 0.71f; m < 10.0f; m += 0.01f) {
        const float o = mix_limit(m);
        CHECK(o > prev && o < 1.0f);
        CHECK(mix_limit(-m) == -o);
        prev = o;
    }
}

TEST(auto_haptics_fallback_crossfades_in_15_ms) {
    AutoHaptics ah;
    const float game = 0.4f, derived = -0.2f;
    float hl = 0, hr = 0;
    auto step = [&](bool active) {
        hl = game; hr = game;
        ah.combine(kAutoFallback, active, derived, derived, hl, hr);
    };
    step(false);
    CHECK(hl == game);                                   // the game's while it plays
    for (int i = 0; i < 359; i++) step(true);
    step(true);                                          // 360 samples: halfway (7.5 ms)
    CHECK(std::fabs(hl - 0.5f * (game + derived)) < 1e-3f);
    for (int i = 0; i < 361; i++) step(true);            // 721: fully derived
    CHECK(hl == derived && hr == derived);
    step(false);                                         // back: one step, not a jump
    CHECK(std::fabs(hl - (derived + (game - derived) / 720.0f)) < 1e-4f);
    for (int i = 0; i < 720; i++) step(false);
    CHECK(hl == game);
}

TEST(auto_haptics_replace_and_off) {
    AutoHaptics ah;
    float hl = 0.3f, hr = -0.3f;
    ah.combine(kAutoReplace, false, 0.1f, 0.2f, hl, hr);
    CHECK(hl == 0.1f && hr == 0.2f);
    hl = 0.3f; hr = -0.3f;
    ah.combine(kAutoOff, true, 0.1f, 0.2f, hl, hr);
    CHECK(hl == 0.3f && hr == -0.3f);
}

// Silence in, silence out; a steady tone reaches a bounded, non-zero output.
TEST(auto_haptics_silence_and_bounds) {
    AutoHaptics ah;
    float al = 1, ar = 1;
    for (int i = 0; i < 480; i++) ah.next(0.0f, 0.0f, AutoHaptics::kLowPass[1], 1.0f, al, ar);
    CHECK(al == 0.0f && ar == 0.0f);
    float peak = 0;
    for (int i = 0; i < 48000; i++) {
        const float x = (float) std::sin(2.0 * M_PI * 60.0 * i / 48000.0);
        ah.next(x, -x, AutoHaptics::kLowPass[1], 2.0f, al, ar);
        peak = std::fmax(peak, std::fabs(al));
        CHECK(std::fabs(al) < 1.0f && std::fabs(ar) < 1.0f);
    }
    CHECK(peak > 0.3f);
}
