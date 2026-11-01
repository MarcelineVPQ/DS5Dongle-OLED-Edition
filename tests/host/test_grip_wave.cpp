// src/grip_wave.h, the haptic feedback test's wave and its report clock:
// level and strength scaling, side selection, a steady wave with no gap across
// 0x39 packets (64 sample pairs each), exactly the asked number of samples,
// silence after, and reports at the haptic packet rate.
#include "grip_wave.h"
#include "test.h"
#include <cstdint>
#include <vector>

namespace {
constexpr int kPairsPerReport = 64;  // two 32-sample haptic frames per 0x39

// Play `reports` packets; the samples of one grip, back to back.
std::vector<int8_t> play(GripWave &w, int reports, float gl, float gr, int side) {
    std::vector<int8_t> out;
    for (int r = 0; r < reports; r++) {
        int8_t pkt[kPairsPerReport * 2];
        w.fill(pkt, kPairsPerReport, gl, gr);
        for (int i = 0; i < kPairsPerReport; i++) out.push_back(pkt[i * 2 + side]);
    }
    return out;
}
}  // namespace

// A 125 Hz square wave: 12 samples up, 12 down, at a quarter of full scale
// times the grip's strength (clamped at full scale), the same int8 math as
// the host's haptics (haptic_to_s8).
TEST(grip_wave_level_and_strength) {
    for (float gain : {1.0f, 2.0f, 3.5f, 4.0f, 6.0f}) {
        GripWave w;
        w.start(true, false, 48);
        const auto s = play(w, 1, gain, 1.0f, 0);
        const int8_t hi = haptic_to_s8(kGripWaveLevel, gain);  // the low half is exactly -hi
        for (int i = 0; i < 48; i++) CHECK_EQ(s[i], (i % 24) < 12 ? hi : (int8_t) -hi);
    }
    CHECK_EQ(haptic_to_s8(kGripWaveLevel, 1.0f), 32);   // strength 1: a quarter (31.75, rounded)
    CHECK_EQ(haptic_to_s8(kGripWaveLevel, 4.0f), 127);  // 4 and up: full
    CHECK_EQ(haptic_to_s8(kGripWaveLevel, 6.0f), 127);
}

// Each grip gets its own strength; the other side is silent.
TEST(grip_wave_sides) {
    GripWave w;
    w.start(true, false, 320);
    int8_t pkt[kPairsPerReport * 2];
    for (int r = 0; r < 5; r++) {
        w.fill(pkt, kPairsPerReport, 2.0f, 6.0f);
        for (int i = 0; i < kPairsPerReport; i++) {
            CHECK(pkt[i * 2] == 64 || pkt[i * 2] == -64);
            CHECK_EQ(pkt[i * 2 + 1], 0);
        }
    }
    w.start(false, true, 300);
    for (int r = 0; r < 4; r++) {
        w.fill(pkt, kPairsPerReport, 2.0f, 6.0f);
        for (int i = 0; i < kPairsPerReport; i++) {
            CHECK_EQ(pkt[i * 2], 0);
            CHECK(pkt[i * 2 + 1] == 127 || pkt[i * 2 + 1] == -127);
        }
    }
    w.start(true, true, 64);
    w.fill(pkt, kPairsPerReport, 1.0f, 2.0f);
    for (int i = 0; i < kPairsPerReport; i++) {
        CHECK(pkt[i * 2] == 32 || pkt[i * 2] == -32);
        CHECK(pkt[i * 2 + 1] == 64 || pkt[i * 2 + 1] == -64);
        CHECK_EQ(pkt[i * 2] > 0, pkt[i * 2 + 1] > 0);  // in phase
    }
    w.start(false, false, 300);  // no side: nothing runs
    CHECK(!w.active());
}

// Steady for the whole time: every sample nonzero, the square wave's period
// unbroken across packet boundaries (64 isn't a multiple of 24), exactly
// tenths x 300 samples, then silence.
TEST(grip_wave_steady_then_stops) {
    for (uint32_t tenths : {1u, 5u, 16u, 50u}) {
        GripWave w;
        const uint32_t n = tenths * kGripSamplesPerTenth;
        w.start(true, true, n);
        const int reports = (int) (n / kPairsPerReport) + 3;
        const auto s = play(w, reports, 1.0f, 1.0f, 0);
        for (uint32_t i = 0; i < n; i++) CHECK_EQ(s[i], (i % 24) < 12 ? 32 : -32);
        for (size_t i = n; i < s.size(); i++) CHECK_EQ(s[i], 0);
        CHECK(!w.active());
    }
}

// A replacement keeps the phase (no gap or click), stop ends it at once, and
// a start from idle begins at the top of the wave.
TEST(grip_wave_replace_and_stop) {
    GripWave w;
    w.start(true, false, 1000);
    std::vector<int8_t> s = play(w, 1, 1.0f, 1.0f, 0);  // 64 samples
    w.start(true, true, 1000);
    const auto t = play(w, 1, 1.0f, 1.0f, 0);
    s.insert(s.end(), t.begin(), t.end());
    for (int i = 0; i < 128; i++) CHECK_EQ(s[i], (i % 24) < 12 ? 32 : -32);
    w.stop();
    CHECK(!w.active());
    for (int8_t v : play(w, 1, 1.0f, 1.0f, 0)) CHECK_EQ(v, 0);
    w.start(true, false, 10);
    CHECK_EQ(play(w, 1, 1.0f, 1.0f, 0)[0], 32);
}

// The test's own reports: two at once, then one per 64 / 3000 s (21333.3 us),
// exact over time; a late one is caught up, a stall resyncs.
TEST(grip_report_clock) {
    AudioReportClock c;
    uint64_t now = 1'000'000;
    int sent = 0;
    auto step = [&](uint64_t until) {  // 100 us main-loop passes
        for (; now < until; now += 100) {
            if (c.due(now)) {
                c.sent(now);
                sent++;
            }
        }
    };
    step(now + 100);
    CHECK_EQ(sent, 1);
    step(now + 100);
    CHECK_EQ(sent, 2);  // the head start
    step(now + 21'000);
    CHECK_EQ(sent, 2);
    step(now + 600);
    CHECK_EQ(sent, 3);
    const uint64_t t0 = now;
    step(t0 + 3'000'000);  // 3 s: 140.6 reports
    CHECK(sent - 3 >= 140 && sent - 3 <= 141);
    // 50 ms late: caught up (two due at once), not dropped.
    now += 50'000;
    const int before = sent;
    step(now + 200);
    CHECK(sent - before >= 2 && sent - before <= 3);
    // A 1 s stall: resync, one report, then the normal clock.
    now += 1'000'000;
    const int b2 = sent;
    step(now + 21'000);
    CHECK_EQ(sent - b2, 1);
    c.reset();
    CHECK(c.due(now));  // a fresh start gets its head start again
}
