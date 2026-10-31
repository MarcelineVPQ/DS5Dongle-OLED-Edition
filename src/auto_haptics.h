//
// Audio Auto Haptics (borrowed from loteran/DS5Dongle 5d6bc2f): derives a
// haptic-feedback waveform from the speaker audio, so games that never write
// haptic data (e.g. Ghost of Tsushima on Linux+Steam) still produce rumble.
// Moved out of audio_loop unchanged (same float operations in the same order,
// so the same output bytes) to put it under a host test
// (tests/host/test_auto_haptics.cpp).
//
// Pure, no Pico SDK; always_inline so it folds into audio_loop (RAM-resident,
// core0; tools/check_ram_residency.sh checks for an out-of-line copy).
//

#ifndef DS5_BRIDGE_AUTO_HAPTICS_H
#define DS5_BRIDGE_AUTO_HAPTICS_H

#include <cstdint>

// config.auto_haptics_enable
enum AutoHapticsMode : uint8_t { kAutoOff = 0, kAutoFallback = 1, kAutoMix = 2, kAutoReplace = 3 };

struct AutoHaptics {
    // One-pole low-pass coefficients per 48 kHz sample, for the 80 / 160 /
    // 250 / 400 Hz settings (config.auto_haptics_lowpass). Their -3 dB points
    // are 79.8 / 160.1 / 240.2 / 401.7 Hz (the test checks them).
    static constexpr float kLowPass[4] = {0.01039f, 0.02074f, 0.03095f, 0.05123f};
    // Peak follower on the low-passed signal, per 48 kHz sample: attack 0.40
    // (time constant 0.04 ms), release 0.025 (0.82 ms).
    static constexpr float kEnvAttack = 0.40f;
    static constexpr float kEnvRelease = 0.025f;

    float lp_l = 0.0f, lp_r = 0.0f;
    float env_l = 0.0f, env_r = 0.0f;

    // One 48 kHz speaker frame (-1..1) in, the derived haptic pair out
    // (soft-clipped to -1..1). gain = auto_haptics_gain / 100.
    __attribute__((always_inline)) inline void next(float spk_l, float spk_r, float lp_a, float gain,
                                                     float &al, float &ar) {
        lp_l += lp_a * (spk_l - lp_l);
        lp_r += lp_a * (spk_r - lp_r);
        const float abs_l = lp_l < 0.0f ? -lp_l : lp_l;
        const float abs_r = lp_r < 0.0f ? -lp_r : lp_r;
        env_l = (abs_l > env_l) ? env_l + kEnvAttack * (abs_l - env_l)
                                : env_l + kEnvRelease * (abs_l - env_l);
        env_r = (abs_r > env_r) ? env_r + kEnvAttack * (abs_r - env_r)
                                : env_r + kEnvRelease * (abs_r - env_r);
        al = lp_l * (1.0f + 3.0f * env_l) * gain;
        ar = lp_r * (1.0f + 3.0f * env_r) * gain;
        al = al / (1.0f + (al < 0.0f ? -al : al));
        ar = ar / (1.0f + (ar < 0.0f ? -ar : ar));
    }
};

// The derived pair (al, ar) into the game's own haptics (h_l, h_r, -1..1) by
// mode. Fallback uses it only while the game's haptics are silent
// (NativeHapticsPresence, src/audio_levels.h). Mix soft-clips the sum with
// m / (1 + |m|).
__attribute__((always_inline)) inline void auto_haptics_combine(uint8_t mode, bool fallback_active, float al,
                                                                float ar, float &h_l, float &h_r) {
    if (mode == kAutoReplace) {
        h_l = al; h_r = ar;
    } else if (mode == kAutoMix) {
        const float m_l = h_l + al, m_r = h_r + ar;
        h_l = m_l / (1.0f + (m_l < 0.0f ? -m_l : m_l));
        h_r = m_r / (1.0f + (m_r < 0.0f ? -m_r : m_r));
    } else if (mode == kAutoFallback && fallback_active) {
        h_l = al; h_r = ar;
    }
}

#endif  // DS5_BRIDGE_AUTO_HAPTICS_H
