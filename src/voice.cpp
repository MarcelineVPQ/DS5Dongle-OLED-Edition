// The narrator's firmware lines (see voice.h).
//
// The lines live in flash as src/voice_bank.bin (tools/build_voice_bank.py):
// Opus packets already encoded the way the DualSense's speaker takes them
// (48 kHz stereo, 480-sample frames carrying 10.67 ms of her voice each, the
// time the controller takes to play one; CELT), stored compact and padded to the 0x39
// report's fixed frame size as they go out (opus_packet_pad). Nothing is
// decoded or encoded here, so core1's speaker path is untouched: while a line
// plays, its frames simply take the place of the host's speaker frames in the
// audio report (game audio pauses for the second or two), and when the host
// isn't streaming audio at all, voice_task() sends the reports itself, at the
// rate the controller plays them (src/report_clock.h). The connected line
// waits for a ready path, and a cold path gets silence first (voice_cue.h).
#include "voice.h"

#include <cstring>
#include "audio.h"
#include "bt.h"
#include "config.h"
#include "opus.h"
#include "pico/time.h"
#include "report_clock.h"
#include "usb.h"
#include "voice_cue.h"

extern uint8_t interrupt_in_data[63];
extern uint32_t bt_31_packet_count();  // main.cpp: the controller's input reports

__asm__(
    ".section .rodata.voice_bank,\"a\"\n"
    ".balign 4\n"
    ".global voice_bank\n"
    "voice_bank:\n"
    ".incbin \"" VOICE_BANK_FILE "\"\n"
    ".global voice_bank_end\n"
    "voice_bank_end:\n"
    ".previous\n");
extern "C" const uint8_t voice_bank[];
extern "C" const uint8_t voice_bank_end[];

namespace {
constexpr uint32_t kHeader = 8;
// A slot switch drops the controller after its line; never wait longer (the
// longest slot line, 294 frames, plus its silence is 3.52 s).
constexpr uint64_t kSlotLineMaxUs = 4500 * 1000;
// Two frames per report, one report per 21.33 ms: the rate the host path
// sends them and the controller plays them (it was 20 ms, 6.7 % fast, so a
// line outran the controller's buffer). The first three go out at once, so
// the buffer has a head start and a busy main-loop pass can't run it dry.
AudioReportClock s_clock{3};

const uint8_t *s_pos = nullptr;      // next frame (length byte), null = no clip
uint16_t s_left = 0;                 // clip frames left
uint16_t s_pre = 0;                  // silent frames still to send before the clip
uint16_t s_post = 0;                 // ...and after it
ConnectCueGate s_cue;                // the pending connected line
bool s_was_connected = false;
bool s_battery_warned = false;
uint8_t s_battery_low_reads = 0;
uint64_t s_next_battery_us = 0;
bool s_disconnect_after = false;
uint64_t s_disconnect_by_us = 0;

bool bank_ok() {
    return voice_bank_end - voice_bank > (ptrdiff_t) kHeader && memcmp(voice_bank, "DS5V", 4) == 0 &&
           voice_bank[4] == 1 && voice_bank[5] == kVoiceNumLangs && voice_bank[6] == VOICE_NUM_LINES;
}

// The configured language (0..6), or -1 with the narrator off.
int configured_lang() {
    const uint8_t v = get_config().narrator_voice;
    return v >= 1 && v <= kVoiceNumLangs ? v - 1 : -1;
}
} // namespace

void voice_say_lang(VoiceLine line, uint8_t lang) {
    // error: SHOWN — the app's 0x0C preview is refused first with CMD_RESULT_INVALID/FAILED (bad line, no controller); a broken bank (bank_ok) is a build defect the voice tests catch
    if (line >= VOICE_NUM_LINES || lang >= kVoiceNumLangs || !bt_is_connected() || !bank_ok()) return;
    const uint8_t *entry = voice_bank + kHeader + (lang * VOICE_NUM_LINES + line) * 8;
    uint32_t offset;
    uint16_t frames;
    memcpy(&offset, entry, 4);
    memcpy(&frames, entry + 4, 2);
    // error: NOT-WORTH — nothing; an empty/out-of-range entry is a build defect of voice_bank.bin, checked by tools/build_voice_bank.py
    if (offset >= (uint32_t) (voice_bank_end - voice_bank) || frames == 0) return;
    // Host audio flowing: the path is warm and her frames take the host's
    // place seamlessly. Otherwise the controller has had no speaker audio:
    // silence first and after (voice_cue.h). A line cut short by a new one
    // keeps the path warm, so it needs no new lead-in.
    const bool cold = !audio_host_streaming() && !voice_active();
    s_pos = voice_bank + offset;
    s_left = frames;
    s_pre = cold ? kVoicePreRollFrames : 0;
    s_post = audio_host_streaming() ? 0 : kVoicePostRollFrames;
}

void voice_say(VoiceLine line) {
    const int lang = configured_lang();
    // The host muting the controller's speaker mutes her too.
    if (lang >= 0 && !mute[0]) voice_say_lang(line, (uint8_t) lang);
}

bool voice_active() { return s_pos != nullptr || s_pre != 0 || s_post != 0; }

void voice_disconnect_after_line() {
    s_disconnect_after = true;
    s_disconnect_by_us = time_us_64() + kSlotLineMaxUs;
}

static bool take_one(uint8_t *frame, uint16_t frame_size) {
    memset(frame, 0, frame_size);
    uint8_t len;
    if (s_pre > 0 || (!s_pos && s_post > 0)) {
        uint16_t &n = s_pre > 0 ? s_pre : s_post;
        n--;
        len = sizeof(kOpusSilence10msStereo);
        memcpy(frame, kOpusSilence10msStereo, len);
    } else {
        if (!s_pos || s_left == 0) return false;
        len = s_pos[0];
        if (len == 0 || len > frame_size || s_pos + 1 + len > voice_bank_end) {
            s_pos = nullptr;
            s_post = 0;
            return false;
        }
        memcpy(frame, s_pos + 1, len);
        s_pos += 1 + len;
        if (--s_left == 0) s_pos = nullptr;
    }
    // A valid Opus packet of exactly frame_size bytes (padding, not trailing
    // garbage the decoder would read as audio).
    // error: NOT-WORTH — the frame is zeroed (silence); padding a valid packet to a larger size can't fail
    if (len < frame_size && opus_packet_pad(frame, len, frame_size) != OPUS_OK) memset(frame, 0, frame_size);
    return true;
}

// The bank's lines have an even number of frames, so a report is never half
// voice, half nothing.
bool voice_take_frames(uint8_t *frame0, uint8_t *frame1, uint16_t frame_size) {
    if (!voice_active()) return false;
    take_one(frame0, frame_size);
    take_one(frame1, frame_size);
    return true;
}

// One frame, for the one-frame 0x36 report (packet_mode 1, protocol 1.22).
bool voice_take_frame(uint8_t *frame, uint16_t frame_size) {
    // error: NOT-WORTH — not a failure: false = no line playing, the caller keeps the PC's frame
    if (!voice_active()) return false;
    take_one(frame, frame_size);
    return true;
}

void voice_task() {
    const uint64_t now = time_us_64();
    const bool connected = bt_is_connected();
    if (connected && !s_was_connected) {
        s_cue.arm(now, bt_31_packet_count());
        s_battery_warned = false;
        s_battery_low_reads = 0;
    }
    if (!connected) {
        s_pos = nullptr;
        s_pre = s_post = 0;
        s_cue.disarm();
        s_disconnect_after = false;
    }
    s_was_connected = connected;
    if (!connected) return;

    if (s_cue.armed &&
        s_cue.tick(now, bt_31_packet_count(), audio_connect_state_sent(), usb_host_speaker_set_us()) !=
            ConnectCueGate::kWait) {
        voice_say(VOICE_CONNECTED);
    }

    // Battery low, once per connection: discharging at 10 % or less (the
    // Pico LED's threshold), three reads in a row so a dip doesn't trigger it.
    // Charging, or back above 30 %, re-arms it.
    if (now >= s_next_battery_us) {
        s_next_battery_us = now + 1000 * 1000;
        const uint8_t level = interrupt_in_data[52] & 0x0F;
        const uint8_t power = interrupt_in_data[52] >> 4;
        if (power != 0 || level >= 3) {
            s_battery_warned = false;
            s_battery_low_reads = 0;
        } else if (level <= 1 && !s_battery_warned && ++s_battery_low_reads >= 3 && !s_cue.armed) {
            s_battery_warned = true;
            voice_say(VOICE_BATTERY_LOW);
        }
    }

    if (s_disconnect_after && (!voice_active() || now >= s_disconnect_by_us)) {
        s_disconnect_after = false;
        s_pos = nullptr;
        s_pre = s_post = 0;
        // error: NOT-WORTH — nothing; false only when there is no ACL, i.e. already disconnected
        bt_disconnect();
        return;
    }

    // The haptic feedback test is sending its own reports: her frames ride in
    // them; her clock picks up from now when it ends.
    // No host audio flowing: the line's reports go out on their own clock.
    // The host's or the haptic test's reports carry her frames otherwise; her
    // clock starts over (with its head start) when they stop.
    if (!voice_active() || audio_grip_test_sending() || audio_host_streaming()) {
        s_clock.reset();
        return;
    }
    if (s_clock.due(now)) {
        audio_send_voice_report();
        s_clock.sent(now);
    }
}
