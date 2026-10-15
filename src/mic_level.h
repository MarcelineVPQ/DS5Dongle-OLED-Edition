//
// Level control for the mic jitter buffer (audio.cpp mic_drain, core0). The
// buffer pre-fills kTarget frames (30 ms) and then plays one 10 ms frame per
// 10 ms of the PC's clock. Without control it only ever grows: a Bluetooth
// burst, or a late packet that was concealed (Opus PLC) and then arrived
// anyway, leaves an extra frame queued for good, and the controller's clock
// running a little faster than the PC's adds more, up to the 8-frame cap
// (80 ms of mic delay; headset audit 2026-10, finding 5).
//
// The control: at each frame boundary the queued level is sampled; when its
// lowest value over a kWindowUs window stayed above kTarget, that many frames
// (lowest - kTarget) were never needed to ride out the jitter, so that many
// are dropped. Each drop prefers a quiet frame (peak below kQuietPeak, a
// pause in speech) and waits up to kQuietWaitUs for one, then takes the next
// frame anyway. The lowest level only reaches kTarget again, so the 30 ms
// cushion for jitter is never touched; a burst that PLC concealed and that
// then arrived late is gone within about a window. Pure, no Pico SDK, for the
// host tests.
//

#ifndef DS5_BRIDGE_MIC_LEVEL_H
#define DS5_BRIDGE_MIC_LEVEL_H

#include <cstdint>

struct MicLevel {
    static constexpr int kTarget = 3;                 // = MIC_PLAYOUT_START (frames)
    static constexpr uint32_t kWindowUs = 1000000;  // the level must stay above kTarget this long
    static constexpr uint32_t kQuietWaitUs = 500000; // then wait this long for a quiet frame
    static constexpr int kQuietPeak = 1024;           // |sample| below this = quiet (-30 dBFS)

    uint32_t window_start = 0;
    int window_min = 0x7FFF;
    int pending = 0;           // frames still to drop from the last window's excess
    uint32_t armed_at = 0;     // when the current drop started waiting for a quiet frame
    uint32_t drops = 0;        // frames dropped since boot (0xFD diagnostics)
    uint8_t level = 0;         // last sampled level (frames queued at a boundary)
    uint8_t max_level = 0;     // highest level this session

    // A new mic session started (the pre-fill is done).
    void start(uint32_t now) {
        window_start = now;
        window_min = 0x7FFF;
        pending = 0;
        max_level = 0;
    }

    // A frame boundary: `queued` frames wait, counting the one about to be
    // played (the level the pre-fill starts playing at is kTarget). True = an
    // excess is armed; the caller then asks drop() with that frame's peak.
    bool sample(uint32_t now, int queued) {
        const uint8_t q = static_cast<uint8_t>(queued < 0 ? 0 : (queued > 255 ? 255 : queued));
        level = q;
        if (q > max_level) max_level = q;
        if (queued < window_min) window_min = queued;
        if (now - window_start >= kWindowUs) {
            const int excess = window_min > kTarget ? window_min - kTarget : 0;
            if (excess > 0 && pending == 0) armed_at = now;
            pending = excess;
            window_start = now;
            window_min = 0x7FFF;
        }
        // Never below the target: the excess went away by itself (e.g. a gap
        // drained it), or the drops so far took it.
        if (pending > queued - kTarget) pending = queued > kTarget ? queued - kTarget : 0;
        return pending > 0;
    }

    // Drop the frame about to be played (peak = its largest |sample|)?
    bool drop(uint32_t now, int peak) {
        // error: NOT-WORTH — not a failure: false = keep this frame (no excess)
        if (pending <= 0) return false;
        // error: NOT-WORTH — not a failure: false = keep this loud frame, wait for a quiet one
        if (peak >= kQuietPeak && now - armed_at < kQuietWaitUs) return false;
        pending--;
        // error: LOGGED-OK — counter (0xFD [44..47], scripts/mic_diag.sh bt-trace); a drop is the level control working, not an error the user acts on
        drops++;
        armed_at = now;  // the next one waits for its own quiet frame
        return true;
    }
};

// Largest |sample| of a decoded mic frame (for MicLevel::drop).
inline int mic_frame_peak(const int16_t *s, int n) {
    int peak = 0;
    // loop: HOT-PATH-OK — N = 480 samples, once per 10 ms frame and only while a drop is armed
    for (int i = 0; i < n; i++) {
        const int v = s[i] < 0 ? -s[i] : s[i];
        if (v > peak) peak = v;
    }
    return peak;
}

#endif  // DS5_BRIDGE_MIC_LEVEL_H
