//
// Created by awalol on 2026/3/5.
//

#include <malloc.h>
#include <unistd.h>
#include "audio.h"
#include "audio_levels.h"
#include "auto_haptics.h"
#include "bt.h"
#include "core0_ram.h"
#include "grip_wave.h"
#include "haptics_decimate.h"
#include "mic_level.h"
#include "mic_session.h"
#include "speaker_mono.h"
#include "speaker_resample.h"
#include "tusb.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "opus.h"
#include "utils.h"
#include "pico/multicore.h"
#include "pico/flash.h"
#include "pico/util/queue.h"
#include "pico/time.h"
#include "config.h"
#include "state_mgr.h"
#include "usb.h"
#include "voice.h"
#include "perf.h"

#define INPUT_CHANNELS    4
#define SAMPLE_SIZE       64
// Upstream's packed audio report (awalol 8eb8612): two haptic + two speaker
// frames per BT packet, no SetStateData. Controller state now travels in its own
// 0x31 reports (main.cpp forwards every host output report), so it isn't
// re-broadcast at the audio rate any more.
#define REPORT_SIZE       547
#define REPORT_ID         0x39

// DualSense microphone, ported from awalol/DS5Dongle's `mic` branch.
// The DS5 sends mic audio as Opus packets embedded in BT input report
// 0x31 when bit 1 of byte 2 is set; payload is 71 bytes of Opus at
// offset 4, decoded to mono 48 kHz 10 ms frames (480 samples).
#define MIC_CHANNELS      1
#define MIC_FRAMES        480
#define MIC_OPUS_SIZE     71

using std::clamp;
using std::max;

extern uint8_t reportSeqCounter; // shared with main.cpp's 0x31 forwards
static uint8_t packetCounter = 0;
static bool plug_headset = false;
alignas(8) static uint32_t audio_core1_stack[8192];
queue_t audio_fifo;

// Core1 -> core0 hand-off of encoded speaker frames and core0's finished
// haptic frames. Ported from upstream (awalol 8eb8612): a queue per stream
// instead of one shared opus_buf that core1 overwrote and core0 re-read —
// which could send the same speaker frame twice or skip one (crackle).
// audio_bt_task() sends two haptic and two speaker frames per 0x39.
#define SPEAKER_OPUS_SIZE 200
struct audio_spk_element { uint8_t data[SPEAKER_OPUS_SIZE]; };
struct haptics_element { int8_t data[SAMPLE_SIZE]; };
static queue_t audio_spk_fifo;
static queue_t haptics_fifo;

// Mic ingress queue — filled from on_bt_data() (BT poll, core0), drained by
// mic_proc() on core1, which owns the decoder. Decoding used to run on core0
// inside audio_loop, stalling the USB/BT pump every 10 ms (#13: "sound
// stutters when mic is on"); upstream moved it to core1 and so do we.
queue_t mic_fifo;
struct mic_element { uint8_t data[MIC_OPUS_SIZE]; };
static volatile uint32_t g_mic_frames = 0;
static volatile int32_t  g_mic_last_decoded = 0;  // opus_decode return value
static volatile uint16_t g_mic_last_want = 0;     // bytes we asked TinyUSB to send
static volatile uint16_t g_mic_last_wrote = 0;    // bytes TinyUSB accepted
uint32_t audio_mic_frames() { return g_mic_frames; }
int32_t  audio_mic_last_decoded() { return g_mic_last_decoded; }
uint16_t audio_mic_last_want()    { return g_mic_last_want; }
uint16_t audio_mic_last_wrote()   { return g_mic_last_wrote; }

// Mic jitter buffer + packet-loss concealment. Decoded mono frames land here
// (core1 decodes as Opus arrives; core0 drains at the host's USB pace) so bursty
// BT delivery is smoothed and a dropped frame is concealed via Opus PLC instead
// of underrunning the host with a click/hole. Design ported from
// SundayMoments/DS5_Bridge (credit there). PLC keeps voice continuous on a
// lossy BT link (e.g. controller moved away, USB 3.0 RF interference).
// Upstream has no PLC (it plays silence on loss) — this is one of ours we keep.
struct mic_decoded_element { int16_t mono[MIC_FRAMES]; };
static queue_t mic_decode_fifo;
static constexpr int      MIC_DECODE_DEPTH  = 8;       // jitter-buffer capacity (frames)
static constexpr int      MIC_PLAYOUT_START = 3;       // pre-buffer before playout begins
// 1 ms of 48 kHz stereo int16 = 48 frames = 192 bytes. The mic IN FIFO is only
// 4 USB packets deep (~784 B), so the old whole-frame 1920 B write was always
// truncated by TinyUSB; feeding 1 ms slices as space frees up slaves the mic to
// the host clock (upstream 370032e, written for macOS CoreAudio).
static constexpr uint32_t MIC_SLICE_FRAMES  = 48;
static constexpr uint32_t MIC_SLICE_BYTES   = MIC_SLICE_FRAMES * 2 * sizeof(int16_t);
static volatile uint32_t g_mic_last_real_us = 0;      // core1 writes, core0 reads (32-bit: no tearing)
static volatile bool     g_mic_session      = false;  // real frames arrived within mic_session::kSessionUs
static volatile bool     g_mic_wanted       = false;  // audio_mic_wanted(), set by core0 for core1's mic_proc
static volatile uint32_t g_mic_plc_frames = 0;        // concealed frames generated (Diag)
uint32_t audio_mic_plc_frames() { return g_mic_plc_frames; }
// The jitter buffer's level control (src/mic_level.h); core0 only (mic_drain).
static MicLevel g_mic_level;
uint32_t audio_mic_level_drops() { return g_mic_level.drops; }
// Mic audio lost elsewhere (0xFD [54..59], OLED Diagnostics "Mic i b u"):
// Opus packets dropped because mic_fifo was full (core1 behind), decoded
// frames dropped because the jitter buffer was full, and 1 ms silent slices
// written while playing because the jitter buffer ran dry (a hole in the audio).
static volatile uint32_t g_mic_in_drops = 0;      // core0 writes
static volatile uint32_t g_mic_buf_drops = 0;     // core1 writes
static volatile uint32_t g_mic_underruns = 0;     // core0 writes
uint32_t audio_mic_in_drops() { return g_mic_in_drops; }
uint32_t audio_mic_buf_drops() { return g_mic_buf_drops; }
uint32_t audio_mic_underruns() { return g_mic_underruns; }
uint8_t audio_mic_level() { return g_mic_level.level; }
uint8_t audio_mic_level_max() { return g_mic_level.max_level; }

// Host has the mic IN interface open (alt != 0). Set from tud_audio_set_itf_cb.
// The DS5 only streams its mic while something is actually recording — before,
// it streamed whenever bt_mic_enable was on, burning BT airtime and CPU on
// audio nobody read. Gating idea from upstream 8d25a38.
static volatile bool mic_active = false;
void audio_set_mic_active(bool active) { mic_active = active; }
bool audio_mic_wanted() { return mic_active && get_config().bt_mic_enable; }

struct audio_raw_element {
    float data[512 * 2];
};

void set_headset(bool state) {
    plug_headset = state;
}

// Protocol 1.13 speaker_select (upstream ea93fad): 0 auto (the headset when one
// is plugged in), 1 speaker, 2 headset, 3 off. The 0x39 speaker block's tag
// picks the output: 0x13 speaker, 0x16 headset.
static inline __attribute__((always_inline)) bool speaker_to_headset() {
    const uint8_t sel = get_config().speaker_select;
    return sel == 2 || (sel == 0 && plug_headset);
}
static inline __attribute__((always_inline)) uint8_t speaker_block_tag() {
    return (speaker_to_headset() ? 0x16 : 0x13) | 1 << 6 | 1 << 7;
}
// Off: the PC's audio isn't converted, encoded or sent (the narrator still
// speaks, as with no encoder).
static inline __attribute__((always_inline)) bool speaker_output_on() { return get_config().speaker_select != 3; }

// Drop counters for the OLED Diagnostics "Drop" row. Each is a 10.7 ms chunk
// thrown away because the next stage fell behind:
//   raw - core0 dropped PCM because core1 hadn't encoded the previous chunk
//   enc - core1 dropped an encoded speaker frame because BT hadn't sent it
//   hap - core0 dropped a haptic frame waiting for its speaker pair / BT
static volatile uint32_t g_drop_raw = 0;
static volatile uint32_t g_drop_enc = 0;
static volatile uint32_t g_drop_hap = 0;
uint32_t audio_fifo_drops() { return g_drop_raw; }
uint32_t opus_fifo_drops() { return g_drop_enc; }
uint32_t audio_hap_drops() { return g_drop_hap; }
// Pipeline liveness counters (Diag rows "c1" / "raw"): core1 loop iterations,
// speaker frames core1 encoded, raw PCM frames core0 queued, haptic frames queued.
static volatile uint32_t g_c1_loops = 0, g_c1_enc = 0, g_raw_push = 0, g_hap_push = 0;
uint32_t audio_c1_loops() { return g_c1_loops; }
// Free heap: what malloc still has plus what it hasn't claimed yet.
extern "C" char __HeapLimit;
static uint32_t free_heap_bytes() {
    const struct mallinfo m = mallinfo();
    const char *top = static_cast<const char *>(sbrk(0));
    return (uint32_t) m.fordblks + (uint32_t) (&__HeapLimit - top);
}
// Core1 health (0xF5 byte [60]): bit0 started, bit1 speaker encoder failed (no
// memory), bit2 mic decoder failed. Free heap when core1 started ([61..62], KiB).
static volatile uint32_t g_c1_flags = 0, g_c1_free_heap = 0;
// The speaker encoder exists: only then does the BT audio report wait for
// speaker frames (without it, haptics go out alone, like the Pico W build).
static volatile bool g_spk_ready = false;
// Core1 start-up stage (0xF5 byte [60] bits 3..6, issue #15): the last step
// core1_entry reached, so a core1 that never loops shows where it stopped.
// 0 never entered, 1 entered, 2 flash lockout registered, 3 cycle counter on,
// 4 creating the speaker encoder, 5 encoder created (or failed), 6 encoder +
// resampler set up, 7 creating the mic decoder, 8 decoder created (or failed),
// 9 in the loop. The Pico W build (no speaker) goes 3 -> 7.
static volatile uint8_t g_c1_stage = 0;
uint32_t audio_c1_stage() { return g_c1_stage; }
uint32_t audio_c1_flags() { return g_c1_flags; }
uint32_t audio_c1_free_heap() { return g_c1_free_heap; }
uint32_t audio_c1_enc() { return g_c1_enc; }
uint32_t audio_raw_push() { return g_raw_push; }
uint32_t audio_hap_push() { return g_hap_push; }

// Monotonic byte-flow counters for the OLED Diagnostics screen and the web
// emulator's USB / BT rate display. Updated below.
static volatile uint32_t g_usb_frames = 0;
static volatile uint32_t g_bt_packets = 0;
static volatile uint32_t g_usb_out_overruns = 0;  // audio_loop found the PC's speaker FIFO full
uint32_t audio_usb_out_overruns() { return g_usb_out_overruns; }
uint32_t audio_usb_frames() { return g_usb_frames; }
uint32_t audio_bt_packets() { return g_bt_packets; }

// Peak meters for the OLED Audio Meters screen and the app's 0xFB. audio_loop
// records each chunk's peak; reads have no side effect and the release runs on
// elapsed time (src/audio_levels.h). All on core0.
static AudioPeak g_peak_spk, g_peak_hap;
uint8_t audio_peak_speaker() { return g_peak_spk.read(time_us_64()); }
uint8_t audio_peak_haptic() { return g_peak_hap.read(time_us_64()); }

// Most-recent Opus TOC byte (first byte of the packet). Used by the OLED
// Diagnostics screen to decode the frame's bandwidth + duration config
// without serial.
static volatile uint8_t g_mic_toc = 0;
uint8_t audio_mic_last_toc() { return g_mic_toc; }

// Push a 71-byte Opus mic packet from the BT handler into the mic_fifo.
// Called from src/main.cpp's on_bt_data() when the DS5 sends a mic-tagged
// 0x31 input report. Drops the oldest queued packet if the FIFO is full —
// preferring fresh audio over backlog on overload.
void CORE0_RAM_FUNC(mic_add_queue)(const uint8_t *data) {
    if (!audio_mic_wanted()) return;
    static mic_element packet{};
    memcpy(packet.data, data, MIC_OPUS_SIZE);
    g_mic_toc = data[0]; // first byte of the Opus packet
    // Full: drop the oldest and add again (testing queue_is_full first raced
    // core1, which could take one in between).
    // error: LOGGED-OK — counter g_mic_in_drops below; a full queue drops the oldest packet
    if (!queue_try_add(&mic_fifo, &packet)) {
        queue_try_remove(&mic_fifo, NULL);
        // error: LOGGED-OK — counter g_mic_in_drops (0xFD, OLED Diagnostics "Mic i"); a drop is a glitch, not a user action
        g_mic_in_drops = g_mic_in_drops + 1;
        // error: NOT-WORTH — only this core adds, so the slot just freed is still free
        queue_try_add(&mic_fifo, &packet);
    }
}

// Tell the DS5 whether to stream its mic (0x32 audio-control report, as
// upstream's update_mic_status). Sent on every change of audio_mic_wanted()
// and on each new BT link; the setting is sticky on the controller, so the
// old ~4 Hz keepalive isn't needed.
static bool update_mic_status(bool enable) {
    uint8_t pkt[142]{};
    pkt[0] = 0x32;
    pkt[1] = reportSeqCounter << 4;
    reportSeqCounter = (reportSeqCounter + 1) & 0x0F;
    pkt[2] = 0x11 | 0 << 6 | 1 << 7;
    pkt[3] = 1;
    pkt[4] = enable ? 0b00000011 : 0b00000010;
    // error: LOGGED-OK — counter bt_send_drops (0xFD, OLED Diagnostics); false = not sent, mic_status_task sends it again
    return bt_write(pkt, sizeof(pkt));
}

static volatile bool mic_status_dirty = true;
void audio_on_bt_connected() { mic_status_dirty = true; }
bool audio_connect_state_sent() { return bt_is_connected() && !mic_status_dirty; }

static void mic_status_task() {
    static bool sent = false;
    const bool want = audio_mic_wanted();
    if (!bt_is_connected()) return;
    if (!mic_status_dirty && want == sent) return;
    // Sent once per change, so it must not be lost: with the output queue
    // full it waits for room, and a report that still didn't go out is sent
    // again on the next pass.
    // error: NOT-WORTH — not a failure: the output queue is full, the 0x32 waits for the next pass
    if (bt_send_full()) return;
    if (mic_status_dirty) bt_send_state(); // new link: re-apply state now the DS5 is ready
    // error: LOGGED-OK — counter bt_send_drops (0xFD, OLED Diagnostics); not sent: mic_status_dirty / sent stay as they were, so the next pass sends it again
    if (!update_mic_status(want)) return;
    sent = want;
    mic_status_dirty = false;
}

// Mic out: feed decoded PCM to the USB IN FIFO in 1 ms slices whenever it has
// room, so delivery follows the host's clock instead of our own timer. Pre-
// buffers MIC_PLAYOUT_START frames per session to absorb BT jitter; on underrun
// writes a silent slice so the host stream never starves (CoreAudio resets on
// starvation). Silence also covers BT Mic off while the host has the mic open.
static void mic_drain() {
    static bool started = false;
    static bool have = false;
    static uint32_t offset = 0;
    static mic_decoded_element frame{};

    static mic_session::OpenEdge open_edge;

    // The PC opened the mic: drop what the last session left in the jitter
    // buffer (its tail and concealed frames), so playout starts on new audio.
    if (open_edge.opened(mic_active)) {
        // loop: HOT-PATH-OK — at most MIC_DECODE_DEPTH (8) frames, once per mic open
        while (queue_try_remove(&mic_decode_fifo, nullptr)) {}
    }
    if (!mic_active) { started = false; have = false; return; }
    tu_fifo_t *ff = tud_audio_get_ep_in_ff();
    if (ff == nullptr) return;

    const uint32_t now = time_us_32();
    if (!started && queue_get_level(&mic_decode_fifo) >= MIC_PLAYOUT_START) {
        started = true;
        g_mic_level.start(now);
    }
    if (started && !g_mic_session && queue_is_empty(&mic_decode_fifo) && !have) {
        started = false; // session over — re-buffer when the mic comes back
    }

    // loop: HOT-PATH-OK — bounded by free USB mic FIFO space; one 48-frame slice each
    while (tu_fifo_remaining(ff) >= MIC_SLICE_BYTES) {
        static int16_t slice[MIC_SLICE_FRAMES * 2];
        if (!have) {
            const int queued = started ? (int) queue_get_level(&mic_decode_fifo) : 0;
            if (started && queue_try_remove(&mic_decode_fifo, &frame)) {
                have = true;
                offset = 0;
                // Level control: when the buffer stayed above its 3-frame
                // target for 1 s, skip one frame, a quiet one if one comes
                // within 0.5 s (src/mic_level.h). `queued` counts this frame
                // too; armed means more than 3, so a next frame is there.
                if (g_mic_level.sample(now, queued) && queued > 1
                    && g_mic_level.drop(now, mic_frame_peak(frame.mono, MIC_FRAMES))) {
                    // error: NOT-WORTH — nothing; queued > 1 means a frame is waiting, and only this core removes from mic_decode_fifo
                    queue_try_remove(&mic_decode_fifo, &frame);
                }
            } else {
                // Playing and the jitter buffer is empty: a 1 ms hole.
                // error: LOGGED-OK — counter g_mic_underruns (0xFD, OLED Diagnostics "Mic u"); BT jitter or a slow controller clock, nothing for the user to do
                if (started) g_mic_underruns = g_mic_underruns + 1;
                memset(slice, 0, sizeof(slice));
                // error: LOGGED-OK — g_mic_last_wrote / last_want in OLED Diag + 0xF5; a short USB write is an underrun the PC's audio stack conceals
                g_mic_last_wrote = tud_audio_write(slice, MIC_SLICE_BYTES);
                g_mic_last_want  = MIC_SLICE_BYTES;
                break;
            }
        }
        // loop: HOT-PATH-OK — N = 48 frames per slice, mono to L/R copy
        for (uint32_t i = 0; i < MIC_SLICE_FRAMES; i++) {
            const int16_t s = frame.mono[offset + i];
            slice[i * 2]     = s; // mono duplicated to L/R — our UAC1 mic is 2-ch like a real DS5
            slice[i * 2 + 1] = s;
        }
        // error: LOGGED-OK — g_mic_last_wrote / last_want in OLED Diag + 0xF5; a short USB write is an underrun the PC's audio stack conceals
        g_mic_last_wrote = tud_audio_write(slice, MIC_SLICE_BYTES);
        g_mic_last_want  = MIC_SLICE_BYTES;
        offset += MIC_SLICE_FRAMES;
        if (offset >= MIC_FRAMES) have = false;
    }
}

// The PC opened the speaker stream (USB alt setting != 0): audio_loop drops
// the previous stream's leftovers. Set and read on core0 (tud_task, audio_loop).
static bool s_spk_stream_opened = false;
void audio_speaker_stream_opened() { s_spk_stream_opened = true; }

// When the host last sent audio through us: while it streams, the narrator's
// frames ride in its reports; otherwise voice_task() sends them on its own.
static uint64_t s_host_audio_us = 0;
bool audio_host_streaming() { return time_us_64() - s_host_audio_us < 60 * 1000; }

// The 0x39 report's header and its haptic block's tag ([0..11]), for every
// report this file sends; the 128 haptic bytes (2 frames) follow at [12].
static inline __attribute__((always_inline)) void report_header(uint8_t *pkt) {
    pkt[0] = REPORT_ID;
    pkt[1] = reportSeqCounter << 4;
    reportSeqCounter = (reportSeqCounter + 1) & 0x0F;
    pkt[2] = 0x11 | 0 << 6 | 1 << 7;
    pkt[3] = 6;
    // bit 0 = mic-enable, bit 6 required (upstream's byte-4 research).
    pkt[4] = audio_mic_wanted() ? 0b01111111 : 0b01111110;
    const auto buf_len = get_config().audio_buffer_length;
    pkt[5] = buf_len;
    pkt[6] = buf_len;
    pkt[7] = buf_len;
    pkt[8] = buf_len; // AudioBufferLength
    pkt[9] = packetCounter += 2;
    // Haptics Audio Data (2 frames)
    pkt[10] = 0x12 | 1 << 6 | 1 << 7;
    pkt[11] = SAMPLE_SIZE;
}

// Send one 0x39 once two haptic frames and two speaker frames are queued
// (upstream audio_bt_task). Both streams come from the same USB audio at the
// same rate, so they stay paired; this halves the BT packet rate vs one
// report per frame.
static void CORE0_RAM_FUNC(audio_bt_task)() {
    if (queue_get_level(&haptics_fifo) < 2) return;
#if !DISABLE_SPEAKER_PROC
    const bool spk_ready = g_spk_ready && speaker_output_on();
    if (spk_ready && queue_get_level(&audio_spk_fifo) < 2) return;
#endif

    uint8_t pkt[REPORT_SIZE]{};
    report_header(pkt);
    static haptics_element hap{};
    queue_try_remove(&haptics_fifo, &hap);
    memcpy(pkt + 12, hap.data, SAMPLE_SIZE);
    queue_try_remove(&haptics_fifo, &hap);
    memcpy(pkt + 12 + SAMPLE_SIZE, hap.data, SAMPLE_SIZE);
#if !DISABLE_SPEAKER_PROC
    // No speaker encoder (core1 couldn't get its memory, issue #15) or the
    // speaker set to off (1.13): haptics
    // alone, as the Pico W build sends them; the narrator's pre-encoded lines
    // can still take the speaker block.
    if (!spk_ready) {
        if (voice_take_frames(pkt + 142, pkt + 142 + SPEAKER_OPUS_SIZE, SPEAKER_OPUS_SIZE)) {
            pkt[140] = speaker_block_tag();
            pkt[141] = SPEAKER_OPUS_SIZE;
        }
        // error: LOGGED-OK — counter bt_send_drops (0xFD, OLED Diagnostics); a full output queue loses this report's audio, and it isn't counted as sent
        if (bt_write(pkt, sizeof(pkt))) g_bt_packets = g_bt_packets + 1;
        return;
    }
    // Speaker Audio Data (2 frames)
    pkt[140] = speaker_block_tag(); // Speaker: 0x13
    // L Headset Mono: 0x14
    // L Headset R Speaker: 0x15
    // Headset: 0x16
    pkt[141] = SPEAKER_OPUS_SIZE;
    static audio_spk_element spk{};
    queue_try_remove(&audio_spk_fifo, &spk);
    memcpy(pkt + 142, spk.data, SPEAKER_OPUS_SIZE);
    queue_try_remove(&audio_spk_fifo, &spk);
    memcpy(pkt + 142 + SPEAKER_OPUS_SIZE, spk.data, SPEAKER_OPUS_SIZE);
    // A narrator line takes the speaker's place while it plays.
    voice_take_frames(pkt + 142, pkt + 142 + SPEAKER_OPUS_SIZE, SPEAKER_OPUS_SIZE);
#endif

    // error: LOGGED-OK — counter bt_send_drops (0xFD, OLED Diagnostics); a full output queue loses this report's audio, and it isn't counted as sent
    if (bt_write(pkt, sizeof(pkt))) g_bt_packets = g_bt_packets + 1;
}

// A report carrying only the narrator: silent haptics, her two frames. For
// when the host isn't streaming audio (voice_task paces it, 20 ms apart).
void audio_send_voice_report() {
#if !DISABLE_SPEAKER_PROC
    uint8_t pkt[REPORT_SIZE]{};
    report_header(pkt);  // haptics: 2 frames of silence
    pkt[140] = speaker_block_tag();
    pkt[141] = SPEAKER_OPUS_SIZE;
    if (!voice_take_frames(pkt + 142, pkt + 142 + SPEAKER_OPUS_SIZE, SPEAKER_OPUS_SIZE)) return;
    // error: LOGGED-OK — counter bt_send_drops (0xFD, OLED Diagnostics); a full output queue loses this report's audio, and it isn't counted as sent
    if (bt_write(pkt, sizeof(pkt))) g_bt_packets = g_bt_packets + 1;
#endif
}

// The haptic feedback test (0xF6 0x0E, src/grip_wave.h): while it runs, its
// wave takes the place of the host's / Auto Haptics' samples in audio_loop.
// With no host audio flowing nothing would carry them, so grip_test_task
// sends its own 0x39 reports at the haptic packet rate (64 samples, 21.33 ms
// each): the wave's two frames, and the narrator's frames if she is speaking
// (voice_task leaves her reports to it meanwhile), else no speaker block (as
// audio_bt_task sends haptics with the speaker off). When the time is up it
// sends one silent report so the controller's last packet is silence.
static GripWave s_grip;
static AudioReportClock s_grip_clock;  // two reports of head start
static bool s_grip_tail = false;  // the silent closing report is still to send

void audio_grip_test_start(bool left, bool right, uint32_t samples) {
    s_grip.start(left, right, samples);
    if (!s_grip.active()) s_grip_tail = s_grip_clock.running;
}
void audio_grip_test_stop() {
    if (s_grip.active()) s_grip_tail = s_grip_clock.running;
    s_grip.stop();
}
bool audio_grip_test_running() { return s_grip.active(); }
bool audio_grip_test_sending() { return s_grip_clock.running; }

static void CORE0_RAM_FUNC(grip_test_task)() {
    if (!s_grip.active() && !s_grip_tail) {
        s_grip_clock.reset();
        return;
    }
    if (!bt_is_connected()) {  // gone: nothing to play it on
        s_grip.stop();
        s_grip_tail = false;
        s_grip_clock.reset();
        return;
    }
    if (audio_host_streaming()) {  // audio_loop puts the wave in the host's reports
        s_grip_tail = false;
        s_grip_clock.reset();
        return;
    }
    const uint64_t now = time_us_64();
    if (!s_grip_clock.due(now)) return;
    uint8_t pkt[REPORT_SIZE]{};
    report_header(pkt);
    const bool tail = !s_grip.active();
    s_grip.fill(reinterpret_cast<int8_t *>(pkt + 12), SAMPLE_SIZE, get_config().haptics_gain,
                get_config().haptics_gain_right);
#if !DISABLE_SPEAKER_PROC
    if (voice_take_frames(pkt + 142, pkt + 142 + SPEAKER_OPUS_SIZE, SPEAKER_OPUS_SIZE)) {
        pkt[140] = speaker_block_tag();
        pkt[141] = SPEAKER_OPUS_SIZE;
    }
#endif
    // error: LOGGED-OK — counter bt_send_drops (0xFD, OLED Diagnostics); a full output queue loses this report's audio, and it isn't counted as sent
    if (bt_write(pkt, sizeof(pkt))) g_bt_packets = g_bt_packets + 1;
    s_grip_clock.sent(now);
    s_grip_tail = !tail && !s_grip.active();
}

void CORE0_RAM_FUNC(audio_loop)() {
    // Only after enumeration: BT traffic during the fresh-pair feature handshake
    // delays controller detection past the connection watchdog (~10-15 s drop).
    if (tud_mounted()) mic_status_task();
    g_mic_wanted = audio_mic_wanted();  // core1 stops concealing once the mic is closed
    mic_drain();
    audio_bt_task();
    grip_test_task();

    // 1. 读取 USB 音频数据
    if (!tud_audio_available()) return;
    // The PC's speaker FIFO (16 ms, overwritable: TinyUSB overwrites the
    // oldest audio when we fall behind, silently) found full or overrun.
    // A lower bound: an overrun past twice its size reads as just full.
    {
        const tu_fifo_t *out_ff = tud_audio_get_ep_out_ff();
        // error: LOGGED-OK — counter g_usb_out_overruns (0xFD, OLED Diagnostics "USB ovr"); the main loop fell behind, nothing for the user to do
        if (out_ff != nullptr && tu_ff_overflow_count(out_ff->depth, out_ff->wr_idx, out_ff->rd_idx) >= out_ff->depth)
            g_usb_out_overruns = g_usb_out_overruns + 1;
    }

    // Up to 4 USB packets (4 ms) per pass, so one slow main-loop pass doesn't
    // leave the 16-packet FIFO to fill and overwrite; normally there is one.
    // Static: 1.5 KB is too much for core0's stack.
    static int16_t raw[192 * 4];
    uint32_t bytes_read = tud_audio_read(raw, sizeof(raw)); // 每次读入 384 bytes (one packet), up to 4
    int frames = bytes_read / (INPUT_CHANNELS * sizeof(int16_t));
    if (frames == 0) {
        return;
    }
    g_usb_frames += (uint32_t)frames;
    s_host_audio_us = time_us_64();

    static float audio_buf[512 * 2];
    static uint audio_buf_pos = 0;
    // 2. ch3/ch4 are the haptics: each 16 frames become one 3 kHz sample.
    static HapticDecimator decimator;
    static int8_t haptic_buf[SAMPLE_SIZE];
    static int haptic_buf_pos = 0;
    // A new stream: drop what the last one left (up to 511 speaker frames and
    // a partial haptic frame here, two blocks each way in the queues), so the
    // next sound doesn't start with a fragment of the last one.
    if (s_spk_stream_opened) {
        s_spk_stream_opened = false;
        audio_buf_pos = 0;
        haptic_buf_pos = 0;
        decimator = HapticDecimator{};
        // loop: HOT-PATH-OK — N ≤ 2 (queue depth), once per stream open
        while (queue_try_remove(&haptics_fifo, nullptr)) {}
#if !DISABLE_SPEAKER_PROC
        // loop: HOT-PATH-OK — N ≤ 2 (queue depth), once per stream open
        while (queue_try_remove(&audio_fifo, nullptr)) {}
        // loop: HOT-PATH-OK — N ≤ 2 (queue depth), once per stream open
        while (queue_try_remove(&audio_spk_fifo, nullptr)) {}
#endif
    }

    const float audio_gain = mute[0] ? 0.0f : powf(10.0f, get_config().speaker_volume / 20.0f);
    const float haptics_gain = get_config().haptics_gain;             // left grip
    const float haptics_gain_r = get_config().haptics_gain_right;     // right grip
    // This chunk's peaks: ch1/ch2 for the meter, ch3/ch4 for the meter and
    // the native-haptics detector below.
    uint16_t spk_max = 0, native_max = 0;

    // Audio Auto Haptics (src/auto_haptics.h). Mode 1 (Fallback) fires only
    // when native is silent, so it keeps native HD haptics in games that send
    // them; the default is 0, Off (config.cpp).
    const uint8_t auto_mode = get_config().auto_haptics_enable;
    const float auto_gain   = (auto_mode > 0) ? (get_config().auto_haptics_gain / 100.0f) : 0.0f;
    const float lp_a = AutoHaptics::kLowPass[get_config().auto_haptics_lowpass & 3];
    static AutoHaptics auto_haptics;
    static NativeHapticsPresence native_haptics;
    const bool fallback_active = (auto_mode == kAutoFallback) && native_haptics.silent();
#if !DISABLE_SPEAKER_PROC
    // No speaker encoder (yet, or core1 couldn't get its memory, issue #15):
    // nothing would take the PCM, so don't convert and queue it only to drop
    // it (upstream 4929996 skips this work when the speaker is off).
    const bool spk_ready = g_spk_ready && speaker_output_on();
    // Linux's mono Speaker device fills ch2 only: play it on both channels
    // (both headset ears). Stereo stays stereo on the headset and is mixed
    // down to (ch1 + ch2) / 2 for the speaker, which plays one channel only
    // (src/speaker_mono.h).
    static SpeakerMonoDetect speaker_mono;
    SpeakerMix spk_mix = SpeakerMix::Stereo;
    if (spk_ready) {
        bool l = false, r = false;
        // loop: HOT-PATH-OK — N = frames of one USB audio read (≤ 192, normally 48); speaker mono detect
        for (int i = 0; i < frames; i++) {
            l |= raw[i * INPUT_CHANNELS] != 0;
            r |= raw[i * INPUT_CHANNELS + 1] != 0;
        }
        spk_mix = speaker_mix_mode(speaker_mono.update(l, r, (uint32_t) frames), speaker_to_headset());
    }
#endif

    // loop: HOT-PATH-OK — N = frames of one USB audio read (≤ 192, normally 48); peaks + split, the audio path itself
    for (int i = 0; i < frames; i++) {
        // VU peak tracking
        {
            int16_t sl = raw[i * INPUT_CHANNELS];
            int16_t sr = raw[i * INPUT_CHANNELS + 1];
            int16_t hl = raw[i * INPUT_CHANNELS + 2];
            int16_t hr = raw[i * INPUT_CHANNELS + 3];
            uint16_t a = (uint16_t)(sl < 0 ? -sl : sl);
            uint16_t b = (uint16_t)(sr < 0 ? -sr : sr);
            if (a > spk_max) spk_max = a;
            if (b > spk_max) spk_max = b;
            a = (uint16_t)(hl < 0 ? -hl : hl);
            b = (uint16_t)(hr < 0 ? -hr : hr);
            if (a > native_max) native_max = a;
            if (b > native_max) native_max = b;
        }
 #if !DISABLE_SPEAKER_PROC
        // speaker_volume can boost up to +12 dB: speaker_frame clips at full
        // scale rather than hand the encoder samples past +-1.
        if (spk_ready) {
            speaker_frame(raw[i * INPUT_CHANNELS], raw[i * INPUT_CHANNELS + 1], spk_mix, audio_gain,
                          audio_buf[audio_buf_pos], audio_buf[audio_buf_pos + 1]);
            audio_buf_pos += 2;
            if (audio_buf_pos == 512 * 2) {
                static audio_raw_element element{};
                memcpy(element.data, audio_buf, 512 * 2 * 4);
                // Full: drop the oldest and add again. Testing queue_is_full
                // first raced core1: a block it took in between made the
                // remove drop a second, good one.
                // error: LOGGED-OK — counter g_drop_raw below; a full queue drops the oldest block
                if (!queue_try_add(&audio_fifo, &element)) {
                    queue_try_remove(&audio_fifo,NULL);
                    // error: LOGGED-OK — counter g_drop_raw (0xFB/0xF5, the app's audio counters); realtime audio drops the oldest block, nothing for the user to do
                    g_drop_raw = g_drop_raw + 1;
                    // error: LOGGED-OK — printf + g_drop_raw; only this core adds, so the slot just freed is still free
                    if (!queue_try_add(&audio_fifo, &element)) {
                        printf("[Audio] Warning: audio_fifo add failed\n");
                    }
                }
                g_raw_push = g_raw_push + 1;
                audio_buf_pos = 0;
            }
        }
#endif
        float h_l = raw[i * INPUT_CHANNELS + 2] / 32768.0f;
        float h_r = raw[i * INPUT_CHANNELS + 3] / 32768.0f;

        if (auto_mode > 0) {
            float al, ar;
            auto_haptics.next(raw[i * INPUT_CHANNELS] / 32768.0f, raw[i * INPUT_CHANNELS + 1] / 32768.0f,
                              lp_a, auto_gain, al, ar);
            auto_haptics.combine(auto_mode, fallback_active, al, ar, h_l, h_r);
        }

        // 3. 48 kHz -> 3 kHz, the mean of each 16 frames (src/haptics_decimate.h;
        // Auto Haptics above sees every frame). Gain after the rate change, as
        // upstream a55fd46.
        if (!decimator.push(h_l, h_r)) continue;
        if (s_grip.active()) {  // the haptic feedback test replaces both grips' samples
            s_grip.next(haptic_buf + haptic_buf_pos, haptics_gain, haptics_gain_r);
            haptic_buf_pos += 2;
        } else {
            haptic_buf[haptic_buf_pos++] = haptic_to_s8(h_l, haptics_gain);
            haptic_buf[haptic_buf_pos++] = haptic_to_s8(h_r, haptics_gain_r);
        }

        // 4. A full 64-byte frame (32 samples x 2 grips) goes to audio_bt_task.
        if (haptic_buf_pos != SAMPLE_SIZE) {
            continue;
        }
        static haptics_element element{};
        memcpy(element.data, haptic_buf, SAMPLE_SIZE);
        if (queue_is_full(&haptics_fifo)) {
            queue_try_remove(&haptics_fifo, NULL);
            // error: LOGGED-OK — counter g_drop_hap (0xFB/0xF5); realtime haptics drop the oldest block
            g_drop_hap = g_drop_hap + 1;
        }
        g_hap_push = g_hap_push + 1;
        // error: LOGGED-OK — counter g_drop_hap; room was made on the line above, so the add can't fail
        queue_try_add(&haptics_fifo, &element);
        haptic_buf_pos = 0;
    }
    g_peak_spk.record(spk_max, s_host_audio_us);
    g_peak_hap.record(native_max, s_host_audio_us);
    native_haptics.record(native_max, (uint32_t) frames);
    audio_bt_task();
}

void audio_init() {
    // lifetime: NOT-WORTH — boot, once per firmware run; the queue lives until reset
    queue_init(&haptics_fifo, sizeof(haptics_element), 2);
#if !DISABLE_SPEAKER_PROC
    // lifetime: NOT-WORTH — boot, once per firmware run; the queue lives until reset
    queue_init(&audio_fifo, sizeof(audio_raw_element), 2);
    // lifetime: NOT-WORTH — boot, once per firmware run; the queue lives until reset
    queue_init(&audio_spk_fifo, sizeof(audio_spk_element), 2);
#endif
    // Mic queues: core0 fills mic_fifo from BT, core1 decodes into the jitter
    // buffer, core0 drains it to USB. Mic Opus is mono / 48 kHz / 10 ms frames.
    // lifetime: NOT-WORTH — boot, once per firmware run; the queue lives until reset
    queue_init(&mic_fifo, sizeof(mic_element), MIC_DECODE_DEPTH);          // deeper: tolerate BT bursts
    // lifetime: NOT-WORTH — boot, once per firmware run; the queue lives until reset
    queue_init(&mic_decode_fifo, sizeof(mic_decoded_element), MIC_DECODE_DEPTH); // decoded-PCM jitter buffer
    // Core1 now always runs: it owns the mic decoder even in no-speaker builds.
    multicore_launch_core1_with_stack(core1_entry, audio_core1_stack, sizeof(audio_core1_stack));
}

#if !DISABLE_SPEAKER_PROC
    // The speaker resampler's filter table, before core1 runs (src/speaker_resample.h).
    speaker_fir_init();
static OpusEncoder *encoder;
static SpeakerResampler resampler_audio;  // core1 only

// Speaker path: USB OUT PCM (core0 audio_fifo) -> resample -> Opus encode ->
// audio_spk_fifo for audio_bt_task. Non-blocking so core1 can also run the mic.
static void __not_in_flash_func(speaker_proc)() {
    static audio_raw_element audio_element{};
    if (!queue_try_remove(&audio_fifo, &audio_element)) return;
    // 将 512 frames 重采样成 480 frames 以解决噪音问题。感谢 @Junhoo
    // A polyphase FIR (src/speaker_resample.h), no longer linear interpolation.
    static float out_buf[480 * 2];
    resampler_audio.process(audio_element.data, out_buf);

    static audio_spk_element spk{};
    // error: LOGGED-OK — the block is skipped (a 10 ms gap); an encode error needs a broken encoder, which core1 flags at start (g_c1_flags 0x02, shown in the app's Dongle dashboard)
    const int encoded = opus_encode_float(encoder, out_buf, 480, spk.data, SPEAKER_OPUS_SIZE);
    if (encoded <= 0) return;
    g_c1_enc = g_c1_enc + 1;
    if (encoded < SPEAKER_OPUS_SIZE) memset(spk.data + encoded, 0, SPEAKER_OPUS_SIZE - encoded);
    // Full: drop the oldest and add again (testing queue_is_full first raced
    // core0's audio_bt_task, which could take one in between).
    // error: LOGGED-OK — counter g_drop_enc below; a full queue drops the oldest packet
    if (!queue_try_add(&audio_spk_fifo, &spk)) {
        queue_try_remove(&audio_spk_fifo, NULL);
        // error: LOGGED-OK — counter g_drop_enc (0xF5); realtime speaker drops the oldest packet
        g_drop_enc = g_drop_enc + 1;
        // error: NOT-WORTH — only core1 adds, so the slot just freed is still free
        queue_try_add(&audio_spk_fifo, &spk);
    }
}
#endif

static OpusDecoder *mic_decoder = nullptr;

static void __not_in_flash_func(mic_push_decoded)(const mic_decoded_element &dec) {
    // Full: drop the oldest and add again (testing queue_is_full first raced
    // core0's mic_drain, which could take one in between).
    // error: LOGGED-OK — counter g_mic_buf_drops below; a full jitter buffer drops the oldest frame
    if (!queue_try_add(&mic_decode_fifo, &dec)) {
        queue_try_remove(&mic_decode_fifo, NULL);
        // error: LOGGED-OK — counter g_mic_buf_drops (0xFD, OLED Diagnostics "Mic b"); a drop is a glitch, not a user action
        g_mic_buf_drops = g_mic_buf_drops + 1;
        // error: NOT-WORTH — only core1 adds, so the slot just freed is still free
        queue_try_add(&mic_decode_fifo, &dec);
    }
}

// Mic path: Opus from the controller (mic_fifo) -> decode -> jitter buffer.
// If real packets stop mid-session, conceal one frame per missed 10 ms slot
// via Opus PLC; after mic_session::kSessionUs of silence, or once the mic is
// closed, the session is over.
static bool __not_in_flash_func(mic_proc)() {
    static mic_element pkt{};
    static mic_decoded_element dec{};
    static uint32_t last_out_us = 0;
    const uint32_t now = time_us_32();

    if (queue_try_remove(&mic_fifo, &pkt)) {
        // error: LOGGED-OK — g_mic_last_decoded in OLED Diag; a bad packet is one lost 10 ms frame, PLC covers it
        const int n = opus_decode(mic_decoder, pkt.data, MIC_OPUS_SIZE, dec.mono, MIC_FRAMES, 0);
        g_mic_last_decoded = n; // observed in OLED Diag
        if (n > 0) {
            mic_push_decoded(dec);
            g_mic_last_real_us = now;
            g_mic_session = true;
            g_mic_frames = g_mic_frames + 1;
            last_out_us = now;
        }
        return true;
    }
    // No packet: end the session (no real frame for 300 ms, or the mic was
    // closed), conceal a missed frame, or nothing (src/mic_session.h).
    const mic_session::Idle step = mic_session::idle_step(g_mic_session, g_mic_wanted, now, g_mic_last_real_us, last_out_us);
    if (step == mic_session::Idle::End) g_mic_session = false;
    // error: NOT-WORTH — nothing; false = no mic frame this pass (session idle or over), not an error
    if (step != mic_session::Idle::Conceal) return false;
    // error: LOGGED-OK — g_mic_last_decoded in OLED Diag; a bad packet is one lost 10 ms frame, PLC covers it
    const int n = opus_decode(mic_decoder, nullptr, 0, dec.mono, MIC_FRAMES, 0); // PLC
    if (n > 0) {
        mic_push_decoded(dec);
        g_mic_plc_frames = g_mic_plc_frames + 1;
    }
    last_out_us += mic_session::kFrameUs;
    return true;
}

void __not_in_flash_func(core1_entry)() {
    g_c1_stage = 1;
    // Let core0's flash_safe_execute() park this core in RAM during flash
    // erase/program (config, remap, slots, BT link keys) — see flash_safe.h.
    flash_safe_execute_core_init();
    g_c1_stage = 2;
    perf_core_init();  // core1's own cycle counter, for the 0xFC timings
    g_c1_stage = 3;
    g_c1_flags |= 0x01;
    g_c1_free_heap = free_heap_bytes();
    int error = 0;
    // Never return from here: a stopped core1 can't be parked for flash
    // writes, so every save would fail too (issue #15). Without an encoder
    // the speaker is silent, everything else keeps working.
#if !DISABLE_SPEAKER_PROC
    // CELT only by default (CMake OPUS_ENCODER_CELT_ONLY): the packets are the
    // same kind AUDIO sends at these settings, with half the encoder state.
    g_c1_stage = 4;
#if OPUS_ENCODER_CELT_ONLY
    // error: SHOWN — g_c1_flags 0x02 (CORE1_SPEAKER_FAILED) in 0xF5, the app's Dongle dashboard says the speaker path failed; printf with free heap
    // lifetime: NOT-WORTH — once at core1 start; the encoder lives as long as core1 (never restarted)
    encoder = opus_encoder_create(48000, 2, OPUS_APPLICATION_RESTRICTED_CELT, &error);
#else
    // error: SHOWN — g_c1_flags 0x02 (CORE1_SPEAKER_FAILED) in 0xF5, the app's Dongle dashboard says the speaker path failed; printf with free heap
    // lifetime: NOT-WORTH — once at core1 start; the encoder lives as long as core1 (never restarted)
    encoder = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &error);
#endif
    g_c1_stage = 5;
    if (error != 0 || encoder == nullptr) {
        printf("[Audio] OpusEncoder create failed (err=%d, free heap %lu): speaker off\n", error, (unsigned long) g_c1_free_heap);
        encoder = nullptr;
        g_c1_flags |= 0x02;
    } else {
        // error: NOT-WORTH — nothing; constant, valid encoder settings on an encoder that was just created
        opus_encoder_ctl(encoder,OPUS_SET_EXPERT_FRAME_DURATION(OPUS_FRAMESIZE_10_MS));
        // error: NOT-WORTH — nothing; constant, valid encoder settings on an encoder that was just created
        opus_encoder_ctl(encoder,OPUS_SET_BITRATE(200 * 8 * 100));
        // error: NOT-WORTH — nothing; constant, valid encoder settings on an encoder that was just created
        opus_encoder_ctl(encoder,OPUS_SET_VBR(false));
        // error: NOT-WORTH — nothing; constant, valid encoder settings on an encoder that was just created
        opus_encoder_ctl(encoder,OPUS_SET_COMPLEXITY(0)); // max 4
        g_spk_ready = true;
    }
    g_c1_stage = 6;
#endif
    g_c1_stage = 7;
    // error: SHOWN — g_c1_flags 0x04 (CORE1_MIC_FAILED) in 0xF5, the app's Dongle dashboard says the mic path failed; printf
    // lifetime: NOT-WORTH — once at core1 start; the decoder lives as long as core1 (never restarted)
    mic_decoder = opus_decoder_create(48000, MIC_CHANNELS, &error);
    g_c1_stage = 8;
    if (error != 0) {
        printf("[Audio] OpusDecoder create failed (err=%d)\n", error);
        mic_decoder = nullptr;
        g_c1_flags |= 0x04;
    }

    // loop: HOT-PATH-OK — core1 service loop: bounded work per item, sleep_us(10) when idle
    while (true) {
        g_c1_stage = 9;  // running: stage 9 and c1_loops moving
        g_c1_loops = g_c1_loops + 1;
        bool work = false;
#if !DISABLE_SPEAKER_PROC
        if (g_spk_ready && !queue_is_empty(&audio_fifo)) {
            const uint32_t t0 = perf_now();
            speaker_proc();
            perf_record(PERF_C1_SPEAKER, perf_now() - t0);
            work = true;
        }
#endif
        if (mic_decoder != nullptr) {
            const uint32_t t0 = perf_now();
            // error: NOT-WORTH — nothing; false = no mic frame this pass (session idle), not an error
            if (mic_proc()) {
                perf_record(PERF_C1_MIC, perf_now() - t0);
                work = true;
            }
        }
        // Idle without spinning on the queue spinlocks (starves core0's bus).
        if (!work) sleep_us(10);
    }
}
