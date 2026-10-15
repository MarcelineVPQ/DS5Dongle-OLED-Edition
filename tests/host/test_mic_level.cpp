// src/mic_level.h: the mic jitter buffer's level control, plus a simulation of
// the buffer (core1 pushing decoded frames as Bluetooth delivers them, core0
// playing one per 10 ms of the PC's clock) that measures the mic delay it adds
// with and without the control.
#include <cstdio>
#include "mic_level.h"
#include "test.h"

namespace {

constexpr uint32_t kMs = 1000;

// Arrival pattern of decoded frames at core0, per 1 ms tick.
struct Link {
    uint32_t period_us;     // the controller's frame clock (10'000 = same as the PC)
    uint32_t burst_every;   // every Nth frame is held back...
    uint32_t burst_hold_us; // ...this long (a Bluetooth retransmission / burst), PLC fills the gap
};

struct SimResult {
    double avg_ms;  // mean buffer delay over the last 10 s (frames queued x 10 ms)
    int max_level;
    uint32_t drops;
};

// The firmware's buffer: 8 frames, drop-oldest when full (mic_push_decoded),
// PLC inserts a frame when nothing came for 15 ms (mic_proc), playout starts
// at 3 (mic_drain). `control` = with MicLevel.
SimResult simulate(const Link &link, uint32_t seconds, bool control) {
    constexpr int kDepth = 8, kStart = 3;
    int queued = 0;
    bool started = false;
    MicLevel lvl;
    uint64_t next_frame_due = 0;   // controller sends a frame
    uint64_t held_until = 0;       // a burst holds delivery until then
    int held = 0;                  // frames held in the burst
    uint64_t last_real = 0, last_out = 0;
    uint64_t next_play = 0;        // PC pulls one frame per 10 ms
    uint32_t n = 0;
    double sum = 0;
    int samples = 0, max_level = 0;
    // loop: NOT-WORTH — host simulation, 1 ms ticks
    for (uint64_t t = 0; t < (uint64_t) seconds * 1'000'000; t += kMs) {
        // Controller side: one frame per period; some are held back and then
        // arrive together (a burst).
        if (t >= next_frame_due) {
            next_frame_due += link.period_us;
            n++;
            if (link.burst_every && n % link.burst_every == 0 && held_until <= t) held_until = t + link.burst_hold_us;
            held++;
        }
        if (held && t >= held_until) {
            // loop: NOT-WORTH — host simulation, a few held frames
            while (held) {
                if (queued == kDepth) queued--;  // drop-oldest
                queued++;
                held--;
            }
            last_real = t;
            last_out = t;
        } else if (last_real && t - last_real < 300'000 && t - last_out >= 15'000) {
            if (queued == kDepth) queued--;
            queued++;  // PLC frame
            last_out += 10'000;
        }
        // PC side.
        if (!started && queued >= kStart) { started = true; lvl.start((uint32_t) t); next_play = t; }
        if (started && t >= next_play) {
            next_play += 10'000;
            if (queued > 0) {
                const int q = queued;
                queued--;
                if (control && lvl.sample((uint32_t) t, q) && q > 1 && lvl.drop((uint32_t) t, /*peak=*/0)) queued--;
            }
        }
        if (t >= (uint64_t)(seconds - 10) * 1'000'000) { sum += queued * 10.0; samples++; }
        if (queued > max_level) max_level = queued;
    }
    return {sum / samples, max_level, lvl.drops};
}

}  // namespace

TEST(mic_level_no_drop_at_target) {
    MicLevel m;
    m.start(0);
    // loop: NOT-WORTH — host test, 5 s of 10 ms boundaries
    for (uint32_t t = 0; t < 5'000'000; t += 10'000) {
        const bool armed = m.sample(t, (t / 10'000) % 2 ? 3 : 4);  // dips to 3 every other frame
        CHECK(!armed);
    }
    CHECK_EQ(m.drops, 0u);
}

TEST(mic_level_drops_the_excess_preferring_quiet) {
    MicLevel m;
    m.start(0);
    uint32_t t = 0;
    // loop: NOT-WORTH — host test, 1 s of 10 ms boundaries at level 6
    for (; t < 1'000'000; t += 10'000) CHECK(!m.sample(t, 6));
    CHECK(m.sample(t, 6));                 // the window ended above target: armed
    CHECK(!m.drop(t, 20000));              // loud: wait for a quiet frame
    t += 10'000;
    CHECK(m.sample(t, 6));
    CHECK(m.drop(t, 100));                 // quiet: dropped
    CHECK_EQ(m.drops, 1u);
    CHECK_EQ(m.pending, 2);                // level 6 vs target 3: three in all
    CHECK(m.sample(t + 10'000, 5));
    CHECK(m.drop(t + 10'000, 0));
    CHECK(m.sample(t + 20'000, 4));
    CHECK(m.drop(t + 20'000, 0));
    CHECK(!m.sample(t + 30'000, 3));       // at target: nothing more
    CHECK_EQ(m.drops, 3u);
}

TEST(mic_level_loud_speech_still_drops_after_half_a_second) {
    MicLevel m;
    m.start(0);
    uint32_t t = 0;
    // loop: NOT-WORTH — host test
    for (; t <= 1'000'000; t += 10'000) m.sample(t, 5);
    bool dropped = false;
    // loop: NOT-WORTH — host test
    for (; t < 1'600'000 && !dropped; t += 10'000) dropped = m.sample(t, 5) && m.drop(t, 30000);
    CHECK(dropped);
    CHECK(t - 1'000'000 >= MicLevel::kQuietWaitUs);
}

TEST(mic_level_disarms_when_the_excess_drains) {
    MicLevel m;
    m.start(0);
    uint32_t t = 0;
    // loop: NOT-WORTH — host test
    for (; t <= 1'000'000; t += 10'000) m.sample(t, 5);
    CHECK(m.pending > 0);
    CHECK(!m.sample(t, 3));  // a gap brought it back to target by itself
    CHECK(!m.drop(t, 0));
    CHECK_EQ(m.drops, 0u);
}

TEST(mic_level_max_and_level_readouts) {
    MicLevel m;
    m.start(0);
    m.sample(0, 4);
    m.sample(10'000, 7);
    m.sample(20'000, 5);
    CHECK_EQ(m.level, 5);
    CHECK_EQ(m.max_level, 7);
    m.start(30'000);
    CHECK_EQ(m.max_level, 0);
}

TEST(mic_frame_peak_is_largest_magnitude) {
    const int16_t s[5] = {3, -32768, 100, 0, 32767};
    CHECK_EQ(mic_frame_peak(s, 5), 32768);
    const int16_t q[3] = {-5, 4, 2};
    CHECK_EQ(mic_frame_peak(q, 3), 5);
}

// The latency the buffer adds, before (no control) and after, for links that
// make it grow: the controller's clock 0.5 % fast, and Bluetooth bursts that
// PLC conceals before the late frames arrive anyway.
TEST(mic_level_simulated_latency_before_after) {
    const Link links[] = {
        {9'950, 0, 0},          // controller clock 0.5 % fast, no loss
        {10'000, 100, 40'000},  // a 40 ms burst every 1 s: ~3 % concealed (the owner's board, audit)
        {10'000, 50, 40'000},   // twice that: ~6 % concealed
        {9'980, 100, 60'000},   // clock +0.2 % and a 60 ms burst every 1 s
    };
    const char *names[] = {"clock +0.5%", "burst 3%", "burst 6%", "clock+burst"};
    // loop: NOT-WORTH — host test, 4 cases
    for (int i = 0; i < 4; i++) {
        const SimResult before = simulate(links[i], 120, false);
        const SimResult after = simulate(links[i], 120, true);
        std::fprintf(stderr, "  mic buffer %-16s before avg %5.1f ms max %d frames | after avg %5.1f ms max %d frames, %u drops\n",
                     names[i], before.avg_ms, before.max_level, after.avg_ms, after.max_level, after.drops);
        CHECK(after.avg_ms <= before.avg_ms);
        CHECK(after.max_level <= before.max_level);
    }
    // Clock drift alone: back at the 30 ms pre-fill.
    CHECK(simulate(links[0], 120, true).avg_ms <= 35.0);
}
