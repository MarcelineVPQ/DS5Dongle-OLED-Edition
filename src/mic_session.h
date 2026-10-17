// The mic session's edges (audio.cpp; pure, no Pico SDK, host-tested in
// tests/host/test_mic_session.cpp).
//
// core1 (mic_proc): with no new packet, a session conceals one frame per
// missed 10 ms slot (Opus PLC) until kSessionUs (300 ms) without a real frame. When
// the PC closed the mic (or BT Mic went off) the controller stops on purpose,
// so concealing for those 300 ms only filled the jitter buffer with made-up
// frames and added ~28 to the Mic PLC counter at every close: the session now
// ends as soon as the mic isn't wanted.
//
// core0 (mic_drain): frames left in the jitter buffer from the last session
// (its tail, concealed frames) played first when the PC opened the mic again,
// and counted towards the next pre-fill. They are flushed when the PC opens it.
#pragma once

#include <cstdint>

namespace mic_session {

constexpr uint32_t kFrameUs = 10000;     // 10 ms per Opus frame @ 48 kHz
constexpr uint32_t kSessionUs = 300000;  // no real frame this long → session over

enum class Idle : uint8_t { Nothing, End, Conceal };

// core1, a pass without a new packet. Inlined into the RAM-resident mic_proc.
static inline __attribute__((always_inline)) Idle idle_step(bool session, bool wanted, uint32_t now,
                                                            uint32_t last_real_us, uint32_t last_out_us) {
    if (!session) return Idle::Nothing;
    if (!wanted || now - last_real_us >= kSessionUs) return Idle::End;
    // Half a frame of slack before concealing, so normal BT jitter isn't
    // mistaken for loss.
    if (now - last_out_us >= kFrameUs + kFrameUs / 2) return Idle::Conceal;
    return Idle::Nothing;
}

// core0: true once per open (the PC set the mic interface's alt setting).
struct OpenEdge {
    bool was_open = false;
    inline __attribute__((always_inline)) bool opened(bool open) {
        const bool edge = open && !was_open;
        was_open = open;
        return edge;
    }
};

}  // namespace mic_session
