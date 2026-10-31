// src/auto_haptics.h: Audio Auto Haptics (loteran/DS5Dongle 5d6bc2f), moved
// out of audio_loop unchanged. The reference below is the code as it stood
// inline in audio.cpp; the header must give the same floats, bit for bit.
#include "auto_haptics.h"
#include "test.h"
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {
struct OldInline {  // audio.cpp's audio_loop before the move
    float lp_l = 0.0f, lp_r = 0.0f, env_l = 0.0f, env_r = 0.0f;
    void step(uint8_t auto_mode, bool fallback_active, float lp_a, float auto_gain, float spk_l, float spk_r,
              float &h_l, float &h_r) {
        constexpr float ENV_ATK = 0.40f;
        constexpr float ENV_REL = 0.025f;
        lp_l += lp_a * (spk_l - lp_l);
        lp_r += lp_a * (spk_r - lp_r);
        const float abs_l = lp_l < 0.0f ? -lp_l : lp_l;
        const float abs_r = lp_r < 0.0f ? -lp_r : lp_r;
        env_l = (abs_l > env_l) ? env_l + ENV_ATK * (abs_l - env_l) : env_l + ENV_REL * (abs_l - env_l);
        env_r = (abs_r > env_r) ? env_r + ENV_ATK * (abs_r - env_r) : env_r + ENV_REL * (abs_r - env_r);
        float al = lp_l * (1.0f + 3.0f * env_l) * auto_gain;
        float ar = lp_r * (1.0f + 3.0f * env_r) * auto_gain;
        al = al / (1.0f + (al < 0.0f ? -al : al));
        ar = ar / (1.0f + (ar < 0.0f ? -ar : ar));
        if (auto_mode == 3) {
            h_l = al; h_r = ar;
        } else if (auto_mode == 2) {
            float m_l = h_l + al, m_r = h_r + ar;
            h_l = m_l / (1.0f + (m_l < 0.0f ? -m_l : m_l));
            h_r = m_r / (1.0f + (m_r < 0.0f ? -m_r : m_r));
        } else if (auto_mode == 1 && fallback_active) {
            h_l = al; h_r = ar;
        }
    }
};

struct Lcg {
    uint32_t s;
    float next() {
        s = s * 1664525u + 1013904223u;
        return (int32_t) s / 2147483648.0f;  // -1 .. 1
    }
};

bool same_bits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }
}  // namespace

TEST(auto_haptics_matches_the_old_inline_code_bit_for_bit) {
    for (uint8_t mode = 0; mode <= 3; mode++) {
        for (int lp = 0; lp < 4; lp++) {
            for (float gain : {0.0f, 0.5f, 1.0f, 2.0f}) {
                OldInline ref;
                AutoHaptics ah;
                Lcg rng{1234u + mode * 7u + (uint32_t) lp};
                bool ok = true;
                for (int i = 0; i < 4800; i++) {
                    const float sl = rng.next(), sr = rng.next(), nl = rng.next(), nr = rng.next();
                    const bool fb = (i / 500) % 2 == 0;
                    float rl = nl, rr = nr, hl = nl, hr = nr;
                    ref.step(mode, fb, AutoHaptics::kLowPass[lp], gain, sl, sr, rl, rr);
                    if (mode > 0) {
                        float al, ar;
                        ah.next(sl, sr, AutoHaptics::kLowPass[lp], gain, al, ar);
                        auto_haptics_combine(mode, fb, al, ar, hl, hr);
                    }
                    ok = ok && same_bits(rl, hl) && same_bits(rr, hr);
                }
                CHECK(ok);
            }
        }
    }
}

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

TEST(auto_haptics_modes) {
    // Replace: the derived pair; Fallback: the derived pair only while the
    // game's haptics are silent (else the game's stay).
    float hl = 0.3f, hr = -0.3f;
    auto_haptics_combine(kAutoReplace, false, 0.1f, 0.2f, hl, hr);
    CHECK(hl == 0.1f && hr == 0.2f);
    hl = 0.3f; hr = -0.3f;
    auto_haptics_combine(kAutoFallback, false, 0.1f, 0.2f, hl, hr);
    CHECK(hl == 0.3f && hr == -0.3f);
    auto_haptics_combine(kAutoFallback, true, 0.1f, 0.2f, hl, hr);
    CHECK(hl == 0.1f && hr == 0.2f);
    // Mix today: (game + derived) / (1 + |game + derived|), so with nothing
    // derived the game's own haptics come out softer (1.0 -> 0.5). Pinned
    // here as it behaves; whether Mix should leave them alone is the owner's call.
    hl = 1.0f; hr = 0.5f;
    auto_haptics_combine(kAutoMix, false, 0.0f, 0.0f, hl, hr);
    CHECK(hl == 0.5f);
    CHECK(std::fabs(hr - 1.0f / 3.0f) < 1e-6f);
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
