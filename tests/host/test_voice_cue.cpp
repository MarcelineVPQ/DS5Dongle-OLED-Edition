// src/voice_cue.h: when the narrator's connected line starts.
#include "voice_cue.h"
#include "report_clock.h"
#include "test.h"

namespace {
// A connect as the owner's Linux PC does it: input reports every 4 ms from
// the link on, the connect-time state at `state_ms`, the PC's speaker volume
// at `host_ms` (0 = never). Returns when the line starts (ms after the link)
// and how.
struct Run {
    uint64_t at_ms;
    ConnectCueGate::Verdict how;
};
Run connect(uint64_t state_ms, uint64_t host_ms, uint64_t input_gap_from_ms = 0, uint64_t input_gap_ms = 0) {
    ConnectCueGate g;
    const uint64_t t0 = 5'000'000;
    uint32_t inputs = 0;
    g.arm(t0, inputs);
    for (uint64_t us = 0; us <= 4'000'000; us += 1000) {  // 1 ms main-loop passes
        const uint64_t ms = us / 1000;
        const bool in_gap = input_gap_ms && ms >= input_gap_from_ms && ms < input_gap_from_ms + input_gap_ms;
        if (ms % 4 == 0 && !in_gap) inputs++;
        const bool state = state_ms && ms >= state_ms;
        const uint64_t host = host_ms && ms >= host_ms ? t0 + host_ms * 1000 : 0;
        const auto v = g.tick(t0 + us, inputs, state, host);
        if (v != ConnectCueGate::kWait) return {ms, v};
    }
    return {0, ConnectCueGate::kWait};
}
}  // namespace

TEST(voice_cue_starts_when_ready) {
    // State at 0.9 s, host volume at 1.2 s: ready at 1.2 + 0.3 s.
    const Run r = connect(900, 1200);
    CHECK_EQ(r.how, ConnectCueGate::kReady);
    CHECK_EQ(r.at_ms, 1500u);
}

TEST(voice_cue_never_before_the_earliest) {
    const Run r = connect(100, 200);
    CHECK_EQ(r.how, ConnectCueGate::kReady);
    CHECK_EQ(r.at_ms, ConnectCueGate::kEarliestUs / 1000);
}

TEST(voice_cue_waits_for_the_host_to_go_quiet) {
    // A second volume write restarts the quiet time.
    ConnectCueGate g;
    uint32_t inputs = 0;
    g.arm(0, inputs);
    uint64_t host = 0;
    uint64_t started = 0;
    for (uint64_t ms = 1; ms <= 3000 && !started; ms++) {
        if (ms % 4 == 0) inputs++;
        if (ms == 1000 || ms == 1250) host = ms * 1000;
        if (g.tick(ms * 1000, inputs, ms >= 500, host) == ConnectCueGate::kReady) started = ms;
    }
    CHECK_EQ(started, 1550u);
}

TEST(voice_cue_waits_for_steady_input) {
    // Input stops for 200 ms at 1.0 s: steady again 300 ms after it resumes.
    const Run r = connect(900, 1000, 1000, 200);
    CHECK_EQ(r.how, ConnectCueGate::kReady);
    CHECK_EQ(r.at_ms, 1500u);
}

TEST(voice_cue_falls_back_at_the_old_fixed_start) {
    CHECK_EQ(connect(0, 0).how, ConnectCueGate::kFallback);                  // no USB host
    CHECK_EQ(connect(0, 0).at_ms, ConnectCueGate::kLatestUs / 1000);
    CHECK_EQ(connect(900, 0).how, ConnectCueGate::kFallback);                // host never sets volume
    CHECK_EQ(connect(900, 1000, 500, 3000).how, ConnectCueGate::kFallback);  // input stalled
    CHECK_EQ(connect(900, 1000, 500, 3000).at_ms, ConnectCueGate::kLatestUs / 1000);
}

TEST(voice_cue_host_write_before_the_link_does_not_count) {
    ConnectCueGate g;
    g.arm(1'000'000, 0);
    uint32_t inputs = 0;
    ConnectCueGate::Verdict v = ConnectCueGate::kWait;
    uint64_t ms = 1000;
    for (; ms <= 4000 && v == ConnectCueGate::kWait; ms++) {
        if (ms % 4 == 0) inputs++;
        v = g.tick(ms * 1000, inputs, true, 500'000);
    }
    CHECK_EQ(v, ConnectCueGate::kFallback);
}

TEST(voice_cue_fires_once_and_disarms) {
    ConnectCueGate g;
    CHECK_EQ(g.tick(1, 0, true, 0), ConnectCueGate::kWait);  // never armed
    g.arm(0, 0);
    CHECK_EQ(g.tick(3'000'000, 0, false, 0), ConnectCueGate::kFallback);
    CHECK(!g.armed);
    CHECK_EQ(g.tick(3'001'000, 0, false, 0), ConnectCueGate::kWait);
    g.arm(0, 0);
    g.disarm();
    CHECK_EQ(g.tick(3'000'000, 0, false, 0), ConnectCueGate::kWait);
}

TEST(voice_cue_silence_frames) {
    // Pre-roll ~300 ms at the controller's 10.67 ms a frame; both even.
    CHECK(kVoicePreRollFrames * 32 / 3 >= 290 && kVoicePreRollFrames * 32 / 3 <= 310);
    CHECK_EQ(kVoicePreRollFrames % 2, 0);
    CHECK_EQ(kVoicePostRollFrames % 2, 0);
    // The bank's TOC: CELT-only fullband 10 ms (config 30), stereo, one frame.
    CHECK_EQ(kOpusSilence10msStereo[0] >> 3, 30);
    CHECK_EQ(kOpusSilence10msStereo[0] & 0x07, 0x04);
}

// The narrator's clock: three reports at once, then one per 21.33 ms, never
// the old 20 ms: 1.9 s of a line (89 reports) takes as long as the controller
// takes to play it.
TEST(voice_report_clock_matches_the_controller) {
    AudioReportClock c{3};
    uint64_t now = 1'000'000;
    int sent = 0;
    for (; now < 1'000'000 + 300; now += 100)
        if (c.due(now)) { c.sent(now); sent++; }
    CHECK_EQ(sent, 3);
    const uint64_t t0 = now;
    for (; now < t0 + 1'898'667; now += 100)
        if (c.due(now)) { c.sent(now); sent++; }
    CHECK(sent - 3 >= 88 && sent - 3 <= 89);
}

// tools/build_voice_bank.py resamples her lines to 45 kHz (PLAY_RATE) and
// labels them 48 kHz, because the controller plays a 480-sample frame per
// half report (10.67 ms). Encoded at 48 kHz as is, a line played 6.25 % slow
// and 1.1 semitones flat. If the report clock changes, the bank's rate must too.
TEST(voice_bank_rate_matches_the_report_clock) {
    constexpr uint64_t kBankRate = 45000;  // tools/build_voice_bank.py PLAY_RATE
    // 480 samples at kBankRate, in thirds of a microsecond, = half a report
    CHECK_EQ(480ull * 3'000'000 / kBankRate, AudioReportClock::kPeriod3 / 2);
}
