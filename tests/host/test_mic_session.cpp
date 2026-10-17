// src/mic_session.h: core1 stops concealing (Opus PLC) as soon as the mic is
// closed instead of for the 300 ms session timeout, and core0 sees each mic
// open once (when it flushes the jitter buffer's leftovers).
#include <cstdint>
#include "mic_session.h"
#include "test.h"

namespace {
using mic_session::Idle;

// core1's idle passes (one per 1 ms) after the last real frame at t = 1000 µs,
// with the mic wanted or not; counts concealed frames until the session ends.
struct Run { int concealed = 0; uint32_t ended_at = 0; };
Run run_after_last_frame(bool wanted) {
    Run r;
    bool session = true;
    const uint32_t last_real = 1000;
    uint32_t last_out = last_real;
    // loop: NOT-WORTH — host test
    for (uint32_t now = last_real + 1000; now < last_real + 1000000 && session; now += 1000) {
        switch (mic_session::idle_step(session, wanted, now, last_real, last_out)) {
            case Idle::End: session = false; r.ended_at = now; break;
            case Idle::Conceal: r.concealed++; last_out += mic_session::kFrameUs; break;
            case Idle::Nothing: break;
        }
    }
    return r;
}
}  // namespace

TEST(mic_session_conceals_a_dropout_until_the_timeout) {
    // The controller went quiet while the PC still records: PLC fills every
    // missed 10 ms slot until 300 ms without a real frame.
    const Run r = run_after_last_frame(true);
    CHECK(r.concealed >= 28 && r.concealed <= 30);
    CHECK_EQ(r.ended_at, 1000u + mic_session::kSessionUs);
}

TEST(mic_session_ends_at_once_when_the_mic_is_closed) {
    // The PC closed the mic (or BT Mic went off): no made-up frames, and the
    // Mic PLC counter doesn't grow by ~28 at every close.
    const Run r = run_after_last_frame(false);
    CHECK_EQ(r.concealed, 0);
    CHECK_EQ(r.ended_at, 2000u);
}

TEST(mic_session_idle_without_a_session_does_nothing) {
    CHECK(mic_session::idle_step(false, true, 5000000, 0, 0) == Idle::Nothing);
    CHECK(mic_session::idle_step(false, false, 5000000, 0, 0) == Idle::Nothing);
    // Jitter under one and a half frames isn't concealed.
    CHECK(mic_session::idle_step(true, true, 14999, 0, 0) == Idle::Nothing);
    CHECK(mic_session::idle_step(true, true, 15000, 0, 0) == Idle::Conceal);
    // The 32-bit µs clock wrapping mid-session changes nothing.
    CHECK(mic_session::idle_step(true, true, 4000u, 0xFFFFF000u, 0xFFFFF000u) == Idle::Nothing);
}

TEST(mic_open_edge_fires_once_per_open) {
    mic_session::OpenEdge e;
    CHECK(!e.opened(false));
    CHECK(e.opened(true));   // the PC opened the mic: flush the leftovers
    CHECK(!e.opened(true));  // still open: keep playing
    CHECK(!e.opened(false)); // closed
    CHECK(e.opened(true));   // opened again: flush again
}
