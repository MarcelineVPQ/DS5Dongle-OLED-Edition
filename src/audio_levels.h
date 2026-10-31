//
// Audio observations made in audio_loop (core0, RAM-resident): the VU meter
// peaks and the Audio Auto Haptics native-haptics detector. Pure, no Pico SDK, so tests/host checks it;
// always_inline so it folds into audio_loop (an out-of-line copy would sit in
// flash; tools/check_ram_residency.sh checks for one).
// Based on CLF184's PR #18.
//

#ifndef DS5_BRIDGE_AUDIO_LEVELS_H
#define DS5_BRIDGE_AUDIO_LEVELS_H

#include <cstdint>

// A VU meter peak (OLED Audio Meters, app 0xFB bytes 12/13). audio_loop
// records each USB chunk's peak; reading it changes nothing. It falls by 1/8
// per 100 ms since the peak, as the old decay-on-read did at the OLED's 10 Hz
// (about -20 dB in 1.7 s, empty after ~4 s), but now whoever reads, and how
// often: before, the app's 0xFB polling sped the OLED's fall and nobody
// reading held a peak forever. Full scale (|-32768| = 32768) reads 255; it
// wrapped to 0 before.
struct AudioPeak {
    static constexpr uint32_t kStepUs = 100000;  // one 7/8 step
    static constexpr uint32_t kSteps = 48;       // 32768 * (7/8)^48 < 128: reads 0
    uint16_t peak = 0;
    uint64_t at_us = 0;

    __attribute__((always_inline)) inline uint16_t level(uint64_t now_us) const {
        if (now_us <= at_us) return peak;
        const uint64_t age = now_us - at_us;
        if (age >= (uint64_t)kSteps * kStepUs) return 0;
        uint32_t v = peak;
        // loop: HOT-PATH-OK — N ≤ kSteps = 48 decay steps, usually 0-2
        for (uint32_t n = (uint32_t)age / kStepUs; n && v; n--) v = v * 7 / 8;
        return (uint16_t)v;
    }
    __attribute__((always_inline)) inline void record(uint16_t chunk_peak, uint64_t now_us) {
        if (chunk_peak > level(now_us)) {
            peak = chunk_peak;
            at_us = now_us;
        }
    }
    // 0..255 for the meters.
    uint8_t read(uint64_t now_us) const {
        const uint32_t v = level(now_us) >> 7;
        return (uint8_t)(v > 255 ? 255 : v);
    }
};

// Is the host sending its own HD haptics (ch3/ch4)? Fed the raw ch3/ch4 peak
// of each USB chunk only, never a held meter peak: the VU meter's peak only
// fell when someone read it, so with the OLED off and no app polling, one
// loud haptic moment kept Fallback mode from ever engaging. Counted in 48 kHz
// frames, so the delay is time, not reads: it used to count reads (100 of
// them, 100 ms at one 48-frame packet per read), and a packet read in two
// parts counted twice.
struct NativeHapticsPresence {
    static constexpr uint16_t kThreshold = 256;          // peak above this is "native haptics" (-42 dBFS)
    static constexpr uint32_t kTimeoutFrames = 4800;     // 100 ms at 48 kHz below kThreshold before "silent"
    uint32_t silent_frames = kTimeoutFrames * 2;

    __attribute__((always_inline)) inline bool silent() const { return silent_frames >= kTimeoutFrames; }
    __attribute__((always_inline)) inline void record(uint16_t chunk_peak, uint32_t frames) {
        if (chunk_peak > kThreshold) silent_frames = 0;
        else if (silent_frames < kTimeoutFrames * 2) silent_frames += frames;
    }
};

#endif  // DS5_BRIDGE_AUDIO_LEVELS_H
