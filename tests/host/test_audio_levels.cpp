// src/audio_levels.h: the VU meter peaks and the Audio Auto Haptics
// native-haptics detector (PR #18).
#include "audio_levels.h"
#include "test.h"

TEST(native_haptics_starts_silent_and_wakes_on_one_loud_chunk) {
    NativeHapticsPresence d;
    CHECK(d.silent());
    d.record(NativeHapticsPresence::kThreshold, 48);  // at the threshold: still silent
    CHECK(d.silent());
    d.record(NativeHapticsPresence::kThreshold + 1, 48);
    CHECK(!d.silent());
}

TEST(native_haptics_goes_silent_after_100_ms_of_quiet_frames) {
    NativeHapticsPresence d;
    d.record(30000, 48);
    for (int i = 0; i < 99; i++) d.record(0, 48);  // 99 ms of 1 ms packets
    CHECK(!d.silent());
    d.record(0, 48);  // 100 ms
    CHECK(d.silent());
    // Saturates: a long silence doesn't delay the next detection.
    for (int i = 0; i < 100000; i++) d.record(0, 48);
    d.record(1000, 48);
    CHECK(!d.silent());
}

// The delay is time: a packet read in two parts (48 frames as 47 + 1) used
// to count as two reads, so Fallback cut in early. 100 ms is 4800 frames
// however the reads split them.
TEST(native_haptics_timeout_is_frames_not_reads) {
    NativeHapticsPresence d;
    d.record(30000, 48);
    for (int i = 0; i < 99; i++) { d.record(0, 47); d.record(0, 1); }
    CHECK(!d.silent());  // 198 reads, 99 ms
    d.record(0, 48);
    CHECK(d.silent());
}

TEST(native_haptics_full_scale_negative_counts_as_loud) {
    // |-32768| as audio_loop computes it: (uint16_t)32768.
    NativeHapticsPresence d;
    const int16_t s = -32768;
    d.record((uint16_t)(s < 0 ? -s : s), 48);
    CHECK(!d.silent());
}

// The old meter: v decays by 7/8 on each read, the reader shows v >> 7.
static uint8_t old_decay_on_read(uint16_t &v) {
    const uint16_t shown = v;
    v = (uint16_t)((v * 7) / 8);
    return (uint8_t)(shown >> 7);
}

TEST(audio_peak_full_scale_reads_255_not_0) {
    AudioPeak p;
    const int16_t s = -32768;
    p.record((uint16_t)(s < 0 ? -s : s), 1000);
    CHECK_EQ(p.read(1000), 255);
    p.record(32767, 2000);
    CHECK_EQ(p.read(1000), 255);
}

TEST(audio_peak_reads_have_no_side_effect) {
    AudioPeak p;
    p.record(20000, 1000);
    for (int i = 0; i < 1000; i++) CHECK_EQ(p.read(1000), 20000 >> 7);
    CHECK_EQ(p.read(1000 + 250000), p.read(1000 + 250000));
}

TEST(audio_peak_falls_like_the_old_meter_read_at_10_hz) {
    for (uint16_t start : {32767, 20000, 4000, 300}) {
        AudioPeak p;
        uint16_t old = start;
        p.record(start, 0);
        for (int frame = 0; frame < 60; frame++)
            CHECK_EQ(p.read((uint64_t)frame * 100000), old_decay_on_read(old));
    }
}

TEST(audio_peak_is_empty_after_about_4_s_and_held_peak_never_sticks) {
    AudioPeak p;
    p.record(32767, 5'000'000);
    CHECK(p.read(5'000'000 + 1'700'000) < 255 / 8);  // about -20 dB by 1.7 s
    CHECK_EQ(p.read(5'000'000 + 4'200'000), 0);
    CHECK_EQ(p.read(5'000'000 + 3'600'000'000ull), 0);  // an hour of nobody reading
}

TEST(audio_peak_new_louder_peak_restarts_quieter_one_doesnt) {
    AudioPeak p;
    p.record(16000, 0);
    p.record(8000, 50000);                 // below the current level: ignored
    CHECK_EQ(p.read(100000), (16000 * 7 / 8) >> 7);
    p.record(30000, 150000);
    CHECK_EQ(p.read(150000), 30000 >> 7);
    p.record(1000, 200000);
    CHECK_EQ(p.read(250000), (30000 * 7 / 8) >> 7);
}
