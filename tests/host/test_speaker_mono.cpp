// src/speaker_mono.h: Linux's mono Speaker device (USB channel 2 only) is
// copied to both headset ears; stereo stays stereo.
#include "speaker_mono.h"
#include "test.h"

// 1 ms USB reads, as the host sends them.
static bool feed(SpeakerMonoDetect &d, bool left, bool right, uint32_t ms) {
    bool mono = d.mono;
    for (uint32_t i = 0; i < ms; i++) mono = d.update(left, right, 48);
    return mono;
}

TEST(speaker_mono_right_only_turns_on_after_100ms) {
    SpeakerMonoDetect d;
    CHECK(!feed(d, false, true, 99));
    CHECK(feed(d, false, true, 1));
}

TEST(speaker_mono_stereo_never_turns_on) {
    SpeakerMonoDetect d;
    CHECK(!feed(d, true, true, 10000));
    CHECK(!feed(d, true, false, 1000));  // left-only: not the Speaker device
}

TEST(speaker_mono_one_left_sample_ends_it_at_once) {
    SpeakerMonoDetect d;
    CHECK(feed(d, false, true, 200));
    CHECK(!d.update(true, true, 48));
    // and the count starts over
    CHECK(!feed(d, false, true, 99));
    CHECK(feed(d, false, true, 1));
}

TEST(speaker_mono_silence_keeps_the_state) {
    SpeakerMonoDetect d;
    CHECK(feed(d, false, true, 100));
    CHECK(feed(d, false, false, 5000));  // gap between notes / lines
    CHECK(feed(d, false, true, 1));
    // a gap mid-count keeps the count too
    SpeakerMonoDetect e;
    CHECK(!feed(e, false, true, 60));
    CHECK(!feed(e, false, false, 500));
    CHECK(feed(e, false, true, 40));
}

TEST(speaker_mono_hard_right_pan_shorter_than_100ms_stays_stereo) {
    SpeakerMonoDetect d;
    CHECK(!feed(d, false, true, 80));
    CHECK(!feed(d, true, true, 1));
    CHECK(!feed(d, false, true, 80));
}

TEST(speaker_mono_count_saturates_while_on) {
    SpeakerMonoDetect d;
    feed(d, false, true, 100);
    const uint32_t at = d.right_only;
    feed(d, false, true, 1000000);  // hours of right-only audio: no overflow
    CHECK_EQ(d.right_only, at);
    CHECK(d.mono);
}

// ---- What the encoder gets: speaker_mix_mode + speaker_frame ----

static void frame(int16_t l, int16_t r, SpeakerMix m, float gain, float &ol, float &or_) {
    speaker_frame(l, r, m, gain, ol, or_);
}

TEST(speaker_mix_mode_picks_by_route_and_mono) {
    CHECK(speaker_mix_mode(false, false) == SpeakerMix::MixDown);  // stereo to the speaker
    CHECK(speaker_mix_mode(false, true) == SpeakerMix::Stereo);    // stereo to the headset
    CHECK(speaker_mix_mode(true, false) == SpeakerMix::DupRight);  // mono Speaker device, never halved
    CHECK(speaker_mix_mode(true, true) == SpeakerMix::DupRight);   // both headset ears
}

TEST(speaker_mixdown_carries_both_channels_on_both) {
    float l, r;
    // Hard left: before, the speaker (right channel) played nothing.
    frame(16384, 0, SpeakerMix::MixDown, 1.0f, l, r);
    CHECK(l == 0.25f && r == 0.25f);
    // Hard right: the same level as hard left.
    frame(0, 16384, SpeakerMix::MixDown, 1.0f, l, r);
    CHECK(l == 0.25f && r == 0.25f);
    // Centred (L == R): unity, no level change.
    frame(12000, 12000, SpeakerMix::MixDown, 1.0f, l, r);
    CHECK(l == 12000 / 32768.0f && r == l);
    // Opposite phase cancels, as any mono sum does.
    frame(8000, -8000, SpeakerMix::MixDown, 1.0f, l, r);
    CHECK(l == 0.0f && r == 0.0f);
}

TEST(speaker_mixdown_never_clips_at_unity_gain) {
    float l, r;
    const int16_t ext[] = {-32768, -32767, -1, 0, 1, 32767};
    // loop: NOT-WORTH — host test, 6 x 6 extremes
    for (int16_t a : ext) {
        // loop: NOT-WORTH — host test, inner of 6 x 6
        for (int16_t b : ext) {
            frame(a, b, SpeakerMix::MixDown, 1.0f, l, r);
            CHECK(l >= -1.0f && l <= 1.0f);
            CHECK(l == r);
            // exact: (a + b) / 2 in float, no rounding, no wrap (int16 sums wrap)
            CHECK(l == ((float) a + (float) b) / 65536.0f);
        }
    }
    frame(32767, 32767, SpeakerMix::MixDown, 1.0f, l, r);
    CHECK(l == 32767 / 32768.0f);
    frame(-32768, -32768, SpeakerMix::MixDown, 1.0f, l, r);
    CHECK(l == -1.0f);
}

TEST(speaker_frame_gain_then_clip) {
    float l, r;
    // +12 dB (x3.98) on a mixdown of loud stereo: clipped at full scale.
    frame(30000, 20000, SpeakerMix::MixDown, 3.98f, l, r);
    CHECK(l == 1.0f && r == 1.0f);
    frame(-30000, -20000, SpeakerMix::MixDown, 3.98f, l, r);
    CHECK(l == -1.0f && r == -1.0f);
    // -6 dB on a quiet one: exact.
    frame(4000, 2000, SpeakerMix::MixDown, 0.5f, l, r);
    CHECK(l == 1500 / 32768.0f && r == l);
    // muted
    frame(30000, -30000, SpeakerMix::Stereo, 0.0f, l, r);
    CHECK(l == 0.0f && r == 0.0f);
}

TEST(speaker_frame_stereo_and_dup_unchanged) {
    float l, r;
    frame(1000, -2000, SpeakerMix::Stereo, 1.0f, l, r);  // headset stereo: as before
    CHECK(l == 1000 / 32768.0f && r == -2000 / 32768.0f);
    frame(0, 5000, SpeakerMix::DupRight, 1.0f, l, r);    // mono device: ch2 on both, full level
    CHECK(l == 5000 / 32768.0f && r == l);
    frame(-32768, 32767, SpeakerMix::Stereo, 2.0f, l, r);
    CHECK(l == -1.0f && r == 1.0f);
}
