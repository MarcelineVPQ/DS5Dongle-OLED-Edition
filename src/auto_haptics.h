//
// Audio Auto Haptics (borrowed from loteran/DS5Dongle 5d6bc2f): derives a
// haptic-feedback waveform from the speaker audio, so games that never write
// haptic data (e.g. Ghost of Tsushima on Linux+Steam) still produce rumble.
// Host-tested in tests/host/test_auto_haptics.cpp.
//
// Pure, no Pico SDK; always_inline so it folds into audio_loop (RAM-resident,
// core0; tools/check_ram_residency.sh checks for an out-of-line copy).
//

#ifndef DS5_BRIDGE_AUTO_HAPTICS_H
#define DS5_BRIDGE_AUTO_HAPTICS_H

#include <cstdint>

// config.auto_haptics_enable
enum AutoHapticsMode : uint8_t { kAutoOff = 0, kAutoFallback = 1, kAutoMix = 2, kAutoReplace = 3 };

// Mix's limiter: unchanged up to kMixKnee (-3 dBFS), then a soft knee that
// approaches full scale (1.0) and never reaches it; slope 1 at the knee, so
// no corner. 1.0 in comes out at 0.85, 2.0 at 0.94.
constexpr float kMixKnee = 0.7f;
__attribute__((always_inline)) inline float mix_limit(float m) {
    const float a = m < 0.0f ? -m : m;
    if (a <= kMixKnee) return m;
    const float d = (a - kMixKnee) / (1.0f - kMixKnee);
    const float out = kMixKnee + (1.0f - kMixKnee) * d / (1.0f + d);
    return m < 0.0f ? -out : out;
}

struct AutoHaptics {
    // One-pole low-pass coefficients per 48 kHz sample, for the 80 / 160 /
    // 250 / 400 Hz settings (config.auto_haptics_lowpass). Their -3 dB points
    // are 79.8 / 160.1 / 240.2 / 401.7 Hz (the test checks them).
    static constexpr float kLowPass[4] = {0.01039f, 0.02074f, 0.03095f, 0.05123f};
    // Peak envelope of the low-passed signal, per 48 kHz sample: attack 0.40
    // (time constant 0.04 ms), release 1 - exp(-1 / (50 ms x 48 kHz)) (time
    // constant 50 ms). The release used to be 0.025 (0.82 ms), which followed
    // each cycle of an 80-400 Hz signal, so (1 + 3 env) acted as a waveshaper
    // adding harmonics instead of a level boost; at 50 ms it holds through the
    // cycles and boosts loud passages.
    static constexpr float kEnvAttack = 0.40f;
    static constexpr float kEnvRelease = 0.00041658f;
    // Fallback fades the derived haptics in and out: linear, 15 ms (720
    // samples at 48 kHz). They used to switch in one sample, a step on the
    // grips at every switch. The game's own haptics are never faded: they
    // pass at full strength, so an event's attack isn't softened.
    static constexpr float kFadeStep = 1.0f / 720.0f;

    float lp_l = 0.0f, lp_r = 0.0f;
    float env_l = 0.0f, env_r = 0.0f;
    float fade = 0.0f;  // Fallback: the derived haptics' weight, 0..1

    // Once per USB chunk: any other mode resets Fallback's fade, so switching
    // back to Fallback starts from the game's haptics, not mid-fade.
    __attribute__((always_inline)) inline void set_mode(uint8_t mode) {
        if (mode != kAutoFallback) fade = 0.0f;
    }

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

    // The derived pair (al, ar) into the game's own haptics (h_l, h_r, -1..1)
    // by mode, once per 48 kHz frame. Fallback fades the derived pair in while
    // the game's haptics are silent (NativeHapticsPresence, src/audio_levels.h)
    // and out when they return, added to the game's haptics unchanged (the
    // sum is clamped to full scale in haptic_to_s8). Mix adds them, limited
    // only near full scale.
    __attribute__((always_inline)) inline void combine(uint8_t mode, bool fallback_active, float al, float ar,
                                                       float &h_l, float &h_r) {
        if (mode == kAutoReplace) {
            h_l = al; h_r = ar;
        } else if (mode == kAutoMix) {
            h_l = mix_limit(h_l + al);
            h_r = mix_limit(h_r + ar);
        } else if (mode == kAutoFallback) {
            if (fallback_active) fade = fade + kFadeStep >= 1.0f ? 1.0f : fade + kFadeStep;
            else fade = fade - kFadeStep <= 0.0f ? 0.0f : fade - kFadeStep;
            h_l += al * fade;
            h_r += ar * fade;
        }
    }
};

#endif  // DS5_BRIDGE_AUTO_HAPTICS_H
