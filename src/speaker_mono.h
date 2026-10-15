//
// Mono speaker audio on a headset: both ears, not just the right one.
//
// Linux's DualSense UCM profile (alsa-ucm-conf USB-Audio/Sony/DualSense-PS5-
// HiFi.conf) has two output devices on the same 4-channel USB stream: the
// "Speaker" device writes its mono signal to USB channel 2 only (Channel0 1)
// and leaves channel 1 digitally silent, the "Headphones" device writes stereo
// to channels 1/2. The dongle encodes channels 1/2 as the stereo speaker
// stream. With the 0x39 speaker block tagged for the speaker (0x13) that's
// fine, but when Audio output routes it to the headset (0x16: Auto with a
// headset plugged in, or Headset), the Speaker device's mono plays in the
// right ear only.
//
// The USB stream doesn't say which device the host uses, so this watches the
// samples: channel 2 carrying signal while channel 1 stays exactly zero for
// kSpeakerMonoFrames in a row means the host plays the mono Speaker device;
// audio_loop then copies channel 2 into channel 1 while routing to the
// headset. The first non-zero sample on channel 1 ends it at once (stereo
// from the Headphones device or a game stays stereo); silence on both keeps
// the current state, so the gaps of a tune or a narrator line don't restart
// the count.
//
// The same detection keeps that mono device out of the speaker's stereo
// mixdown (speaker_mix_mode below).
//
// Pure, no Pico SDK: it is inlined into audio_loop (RAM-resident, core0).
//

#ifndef DS5_BRIDGE_SPEAKER_MONO_H
#define DS5_BRIDGE_SPEAKER_MONO_H

#include <cstdint>

// 100 ms at 48 kHz: right-only audio this long with an exactly silent left is
// the Speaker device, not a hard-panned sound in a stereo mix.
constexpr uint32_t kSpeakerMonoFrames = 4800;

struct SpeakerMonoDetect {
    uint32_t right_only = 0;  // frames of right-only signal since the left was last non-zero
    bool mono = false;

    // Once per USB read: whether channel 1 / 2 had any non-zero sample in its
    // `frames` frames. Returns true while the stream is the Speaker device's mono.
    __attribute__((always_inline)) inline bool update(bool left, bool right, uint32_t frames) {
        if (left) {
            right_only = 0;
            mono = false;
        } else if (right && !mono) {
            right_only += frames;
            if (right_only >= kSpeakerMonoFrames) mono = true;
        }
        return mono;
    }
};

// What audio_loop hands the encoder from USB channels 1/2.
//
// The controller's own speaker plays one channel of the stereo stream, the
// right one (hid-playstation's jack series; the headset audit's fix 9), so on
// Windows / macOS / Linux's stereo Headphones device anything panned left was
// lost on the speaker. Routed to the speaker, stereo is mixed down to
// (ch1 + ch2) / 2 on both channels: a centred sound keeps its level, a hard
// pan plays at half (-6 dB) instead of not at all, and the sum can never pass
// full scale. Both channels carry the mix, not only ch2, so the speaker gets
// the mix whichever channel it plays, and the stereo encoder spends nothing on
// a side signal nobody hears.
//
// The mono Speaker device (ch1 exactly silent, SpeakerMonoDetect) is never
// mixed: (0 + ch2) / 2 would halve it. It plays ch2 on both channels, on the
// speaker and on the headset. Stereo on the headset stays stereo.
enum class SpeakerMix : uint8_t { Stereo, DupRight, MixDown };

__attribute__((always_inline)) inline SpeakerMix speaker_mix_mode(bool mono, bool to_headset) {
    if (mono) return SpeakerMix::DupRight;
    return to_headset ? SpeakerMix::Stereo : SpeakerMix::MixDown;
}

// One USB frame (ch1, ch2) to the encoder's two float samples, after the
// speaker volume (up to +12 dB), clipped at full scale.
__attribute__((always_inline)) inline void speaker_frame(int16_t ch1, int16_t ch2, SpeakerMix mix, float gain,
                                                         float &out_l, float &out_r) {
    float l = ch1 / 32768.0f;
    float r = ch2 / 32768.0f;
    if (mix == SpeakerMix::DupRight) {
        l = r;
    } else if (mix == SpeakerMix::MixDown) {
        l = r = (l + r) * 0.5f;
    }
    l *= gain;
    r *= gain;
    out_l = l < -1.0f ? -1.0f : (l > 1.0f ? 1.0f : l);
    out_r = r < -1.0f ? -1.0f : (r > 1.0f ? 1.0f : r);
}

#endif
