//
// Created by awalol on 2026/3/5.
//

#include <malloc.h>
#include <unistd.h>
#include "audio.h"
#include "bt.h"
#include "resample.h"
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
#if DS5_C1_DIAG
#include "c1_diag.h"
#include "hardware/sync.h"
#if PICO_RP2350
#include "hardware/exception.h"
#include "hardware/structs/qmi.h"
#endif
#endif

#define INPUT_CHANNELS    4
#define OUTPUT_CHANNELS   2
#define SAMPLE_SIZE       64
// Upstream's packed audio report (awalol 8eb8612): two haptic + two speaker
// frames per BT packet, no SetStateData. Controller state now travels in its own
// 0x31 reports (main.cpp forwards every host output report), so it isn't
// re-broadcast at the audio rate any more.
#define REPORT_SIZE       547
#define REPORT_ID         0x39
// #define VOLUME_GAIN       2
// #define BUFFER_LENGTH     48

// DualSense microphone, ported from awalol/DS5Dongle's `mic` branch.
// The DS5 sends mic audio as Opus packets embedded in BT input report
// 0x31 when bit 1 of byte 2 is set; payload is 71 bytes of Opus at
// offset 4, decoded to mono 48 kHz 10 ms frames (480 samples).
#define MIC_CHANNELS      1
#define MIC_FRAMES        480
#define MIC_OPUS_SIZE     71

using std::clamp;
using std::max;

static WDL_Resampler resampler;
extern uint8_t reportSeqCounter; // shared with main.cpp's 0x31 forwards
static uint8_t packetCounter = 0;
static bool plug_headset = false;
alignas(8) static uint32_t audio_core1_stack[8192];
queue_t audio_fifo;

// Core1 -> core0 hand-off of encoded speaker frames and core0's finished
// haptic frames. Ported from upstream (awalol 8eb8612): a queue per stream
// instead of one shared opus_buf that core1 overwrote and core0 re-read —
// which could send the same speaker frame twice or skip one (crackle).
// audio_bt_task() pairs one haptic frame with one speaker frame per 0x36.
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
static constexpr uint32_t MIC_FRAME_US      = 10000;  // 10 ms per Opus frame @ 48 kHz
static constexpr uint32_t MIC_SESSION_US    = 300000; // no real frame this long → session over
// 1 ms of 48 kHz stereo int16 = 48 frames = 192 bytes. The mic IN FIFO is only
// 4 USB packets deep (~784 B), so the old whole-frame 1920 B write was always
// truncated by TinyUSB; feeding 1 ms slices as space frees up slaves the mic to
// the host clock (upstream 370032e, written for macOS CoreAudio).
static constexpr uint32_t MIC_SLICE_FRAMES  = 48;
static constexpr uint32_t MIC_SLICE_BYTES   = MIC_SLICE_FRAMES * 2 * sizeof(int16_t);
static volatile uint32_t g_mic_last_real_us = 0;      // core1 writes, core0 reads (32-bit: no tearing)
static volatile bool     g_mic_session      = false;  // real frames arrived within MIC_SESSION_US
static volatile uint32_t g_mic_plc_frames = 0;        // concealed frames generated (Diag)
uint32_t audio_mic_plc_frames() { return g_mic_plc_frames; }

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

// Issue #15 diagnostic builds (CMake ISSUE15_DIAG, DS5_C1_DIAG=1): finer
// stages inside core1's loop, in the same 4 bits of 0xF5 [60]: 10 before the
// speaker queue read, 11 resample, 12 Opus encode, 13 queued, 14 mic, 15 idle
// wait. Normal builds keep 9 for the whole loop. GET 0xEA (c1_diag.h) has
// the rest: a HardFault record, the loop heartbeat, the stack high-water mark.
#if DS5_C1_DIAG
#define C1_STAGE(n) (g_c1_stage = (n))
#else
#define C1_STAGE(n) ((void) 0)
#endif
uint32_t audio_c1_flags() { return g_c1_flags; }
uint32_t audio_c1_free_heap() { return g_c1_free_heap; }
uint32_t audio_c1_enc() { return g_c1_enc; }
uint32_t audio_raw_push() { return g_raw_push; }
uint32_t audio_hap_push() { return g_hap_push; }

// Monotonic byte-flow counters for the OLED Diagnostics screen and the web
// emulator's USB / BT rate display. Updated below.
static volatile uint32_t g_usb_frames = 0;
static volatile uint32_t g_bt_packets = 0;
uint32_t audio_usb_frames() { return g_usb_frames; }
uint32_t audio_bt_packets() { return g_bt_packets; }

// Rolling-peak meters for the OLED VU screen. Updated during audio_loop's
// per-sample iteration, decayed 12.5 % on each read (so the bar falls back
// over a few frames if the signal goes quiet).
static volatile uint16_t g_peak_spk = 0;
static volatile uint16_t g_peak_hap = 0;
uint8_t audio_peak_speaker() {
    const uint16_t v = g_peak_spk;
    g_peak_spk = (uint16_t)((v * 7) / 8);
    return (uint8_t)(v >> 7);
}
uint8_t audio_peak_haptic() {
    const uint16_t v = g_peak_hap;
    g_peak_hap = (uint16_t)((v * 7) / 8);
    return (uint8_t)(v >> 7);
}

// Most-recent Opus TOC byte (first byte of the packet). Used by the OLED
// Diagnostics screen to decode the frame's bandwidth + duration config
// without serial.
static volatile uint8_t g_mic_toc = 0;
uint8_t audio_mic_last_toc() { return g_mic_toc; }

// Push a 71-byte Opus mic packet from the BT handler into the mic_fifo.
// Called from src/main.cpp's on_bt_data() when the DS5 sends a mic-tagged
// 0x31 input report. Drops the oldest queued packet if the FIFO is full —
// preferring fresh audio over backlog on overload.
void mic_add_queue(const uint8_t *data) {
    if (!audio_mic_wanted()) return;
    static mic_element packet{};
    memcpy(packet.data, data, MIC_OPUS_SIZE);
    g_mic_toc = data[0]; // first byte of the Opus packet
    if (queue_is_full(&mic_fifo)) queue_try_remove(&mic_fifo, NULL);
    queue_try_add(&mic_fifo, &packet);
}

// Tell the DS5 whether to stream its mic (0x32 audio-control report, as
// upstream's update_mic_status). Sent on every change of audio_mic_wanted()
// and on each new BT link; the setting is sticky on the controller, so the
// old ~4 Hz keepalive isn't needed.
static void update_mic_status(bool enable) {
    uint8_t pkt[142]{};
    pkt[0] = 0x32;
    pkt[1] = reportSeqCounter << 4;
    reportSeqCounter = (reportSeqCounter + 1) & 0x0F;
    pkt[2] = 0x11 | 0 << 6 | 1 << 7;
    pkt[3] = 1;
    pkt[4] = enable ? 0b00000011 : 0b00000010;
    bt_write(pkt, sizeof(pkt));
}

static volatile bool mic_status_dirty = true;
void audio_on_bt_connected() { mic_status_dirty = true; }

static void mic_status_task() {
    static bool sent = false;
    const bool want = audio_mic_wanted();
    if (!bt_is_connected()) return;
    if (mic_status_dirty) bt_send_state(); // new link: re-apply state now the DS5 is ready
    if (mic_status_dirty || want != sent) {
        update_mic_status(want);
        sent = want;
        mic_status_dirty = false;
    }
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

    if (!mic_active) { started = false; have = false; return; }
    tu_fifo_t *ff = tud_audio_get_ep_in_ff();
    if (ff == nullptr) return;

    if (!started && queue_get_level(&mic_decode_fifo) >= MIC_PLAYOUT_START) started = true;
    if (started && !g_mic_session && queue_is_empty(&mic_decode_fifo) && !have) {
        started = false; // session over — re-buffer when the mic comes back
    }

    while (tu_fifo_remaining(ff) >= MIC_SLICE_BYTES) {
        static int16_t slice[MIC_SLICE_FRAMES * 2];
        if (!have) {
            if (started && queue_try_remove(&mic_decode_fifo, &frame)) {
                have = true;
                offset = 0;
            } else {
                memset(slice, 0, sizeof(slice));
                tud_audio_write(slice, MIC_SLICE_BYTES);
                break;
            }
        }
        for (uint32_t i = 0; i < MIC_SLICE_FRAMES; i++) {
            const int16_t s = frame.mono[offset + i];
            slice[i * 2]     = s; // mono duplicated to L/R — our UAC1 mic is 2-ch like a real DS5
            slice[i * 2 + 1] = s;
        }
        g_mic_last_wrote = tud_audio_write(slice, MIC_SLICE_BYTES);
        g_mic_last_want  = MIC_SLICE_BYTES;
        offset += MIC_SLICE_FRAMES;
        if (offset >= MIC_FRAMES) have = false;
    }
}

// Send one 0x39 once two haptic frames and two speaker frames are queued
// (upstream audio_bt_task). Both streams come from the same USB audio at the
// same rate, so they stay paired; this halves the BT packet rate vs one 0x36
// per frame.
// When the host last sent audio through us: while it streams, the narrator's
// frames ride in its reports; otherwise voice_task() sends them on its own.
static uint64_t s_host_audio_us = 0;
bool audio_host_streaming() { return time_us_64() - s_host_audio_us < 60 * 1000; }

static void audio_bt_task() {
    if (queue_get_level(&haptics_fifo) < 2) return;
#if !DISABLE_SPEAKER_PROC
    const bool spk_ready = g_spk_ready;
    if (spk_ready && queue_get_level(&audio_spk_fifo) < 2) return;
#endif

    uint8_t pkt[REPORT_SIZE]{};
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
    static haptics_element hap{};
    queue_try_remove(&haptics_fifo, &hap);
    memcpy(pkt + 12, hap.data, SAMPLE_SIZE);
    queue_try_remove(&haptics_fifo, &hap);
    memcpy(pkt + 12 + SAMPLE_SIZE, hap.data, SAMPLE_SIZE);
#if !DISABLE_SPEAKER_PROC
    // No speaker encoder (core1 couldn't get its memory, issue #15): haptics
    // alone, as the Pico W build sends them; the narrator's pre-encoded lines
    // can still take the speaker block.
    if (!spk_ready) {
        if (voice_take_frames(pkt + 142, pkt + 142 + SPEAKER_OPUS_SIZE, SPEAKER_OPUS_SIZE)) {
            pkt[140] = (plug_headset ? 0x16 : 0x13) | 1 << 6 | 1 << 7;
            pkt[141] = SPEAKER_OPUS_SIZE;
        }
        bt_write(pkt, sizeof(pkt));
        g_bt_packets++;
        return;
    }
    // Speaker Audio Data (2 frames)
    pkt[140] = (plug_headset ? 0x16 : 0x13) | 1 << 6 | 1 << 7; // Speaker: 0x13
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

    bt_write(pkt, sizeof(pkt));
    g_bt_packets++;
}

// A report carrying only the narrator: silent haptics, her two frames. For
// when the host isn't streaming audio (voice_task paces it, 20 ms apart).
void audio_send_voice_report() {
#if !DISABLE_SPEAKER_PROC
    uint8_t pkt[REPORT_SIZE]{};
    pkt[0] = REPORT_ID;
    pkt[1] = reportSeqCounter << 4;
    reportSeqCounter = (reportSeqCounter + 1) & 0x0F;
    pkt[2] = 0x11 | 0 << 6 | 1 << 7;
    pkt[3] = 6;
    pkt[4] = audio_mic_wanted() ? 0b01111111 : 0b01111110;
    const auto buf_len = get_config().audio_buffer_length;
    pkt[5] = buf_len;
    pkt[6] = buf_len;
    pkt[7] = buf_len;
    pkt[8] = buf_len;
    pkt[9] = packetCounter += 2;
    pkt[10] = 0x12 | 1 << 6 | 1 << 7;
    pkt[11] = SAMPLE_SIZE;  // haptics: 2 frames of silence
    pkt[140] = (plug_headset ? 0x16 : 0x13) | 1 << 6 | 1 << 7;
    pkt[141] = SPEAKER_OPUS_SIZE;
    if (!voice_take_frames(pkt + 142, pkt + 142 + SPEAKER_OPUS_SIZE, SPEAKER_OPUS_SIZE)) return;
    bt_write(pkt, sizeof(pkt));
    g_bt_packets++;
#endif
}

void audio_loop() {
    // Only after enumeration: BT traffic during the fresh-pair feature handshake
    // delays controller detection past the connection watchdog (~10-15 s drop).
    if (tud_mounted()) mic_status_task();
    mic_drain();
    audio_bt_task();

    // 1. 读取 USB 音频数据
    if (!tud_audio_available()) return;

    int16_t raw[192];
    uint32_t bytes_read = tud_audio_read(raw, sizeof(raw)); // 每次读入 384 bytes
    int frames = bytes_read / (INPUT_CHANNELS * sizeof(int16_t));
    if (frames == 0) {
        return;
    }
    g_usb_frames += (uint32_t)frames;
    s_host_audio_us = time_us_64();

    static float audio_buf[512 * 2];
    static uint audio_buf_pos = 0;
    // 2. 从4ch中提取ch3/ch4，转换为float输入重采样器
    WDL_ResampleSample *in_buf;
    int nframes = resampler.ResamplePrepare(frames, OUTPUT_CHANNELS, &in_buf);

    const float audio_gain = mute[0] ? 0.0f : powf(10.0f, get_config().speaker_volume / 20.0f);
    const float haptics_gain = get_config().haptics_gain;             // left grip
    const float haptics_gain_r = get_config().haptics_gain_right;     // right grip
    uint16_t spk_max = g_peak_spk;
    uint16_t hap_max = g_peak_hap;

    // ---- Audio Auto Haptics (borrowed from loteran/DS5Dongle 5d6bc2f) ----
    // Derives a haptic-feedback waveform from the speaker audio so games that
    // never write haptic data (e.g. Ghost of Tsushima on Linux+Steam) still
    // produce rumble. Mode 1 (Fallback, default) fires only when native is
    // silent → preserves native HD haptics in games that do send them.
    const uint8_t auto_mode = get_config().auto_haptics_enable;
    const float auto_gain   = (auto_mode > 0) ? (get_config().auto_haptics_gain / 100.0f) : 0.0f;
    static const float LP_COEFF[4] = { 0.01039f, 0.02074f, 0.03095f, 0.05123f };
    const float lp_a = LP_COEFF[get_config().auto_haptics_lowpass & 3];
    static float lp_l = 0.0f, lp_r = 0.0f;
    static float env_l = 0.0f, env_r = 0.0f;
    constexpr float ENV_ATK = 0.40f;
    constexpr float ENV_REL = 0.025f;
    constexpr int     NATIVE_SILENT_TIMEOUT = 100;
    constexpr uint16_t NATIVE_THRESHOLD     = 256;
    static int native_silent_count = NATIVE_SILENT_TIMEOUT * 2;
    const bool fallback_active = (auto_mode == 1) && (native_silent_count >= NATIVE_SILENT_TIMEOUT);

    for (int i = 0; i < nframes; i++) {
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
            if (a > hap_max) hap_max = a;
            if (b > hap_max) hap_max = b;
        }
 #if !DISABLE_SPEAKER_PROC
        // speaker_volume can boost up to +12 dB: clip at full scale rather than
        // hand the encoder samples past +-1.
        audio_buf[audio_buf_pos++] = std::clamp(raw[i * INPUT_CHANNELS] / 32768.0f * audio_gain, -1.0f, 1.0f);
        audio_buf[audio_buf_pos++] = std::clamp(raw[i * INPUT_CHANNELS + 1] / 32768.0f * audio_gain, -1.0f, 1.0f);
        if (audio_buf_pos == 512 * 2) {
            static audio_raw_element element{};
            memcpy(element.data, audio_buf, 512 * 2 * 4);
            if (queue_is_full(&audio_fifo)) {
                queue_try_remove(&audio_fifo,NULL);
                g_drop_raw++;
            }
            g_raw_push++;
            if (!queue_try_add(&audio_fifo, &element)) {
                printf("[Audio] Warning: audio_fifo add failed\n");
            }
            audio_buf_pos = 0;
        }
#endif
        float h_l = raw[i * INPUT_CHANNELS + 2] / 32768.0f;
        float h_r = raw[i * INPUT_CHANNELS + 3] / 32768.0f;

        if (auto_mode > 0) {
            const float spk_l = raw[i * INPUT_CHANNELS    ] / 32768.0f;
            const float spk_r = raw[i * INPUT_CHANNELS + 1] / 32768.0f;
            lp_l += lp_a * (spk_l - lp_l);
            lp_r += lp_a * (spk_r - lp_r);
            const float abs_l = lp_l < 0.0f ? -lp_l : lp_l;
            const float abs_r = lp_r < 0.0f ? -lp_r : lp_r;
            env_l = (abs_l > env_l) ? env_l + ENV_ATK * (abs_l - env_l)
                                    : env_l + ENV_REL * (abs_l - env_l);
            env_r = (abs_r > env_r) ? env_r + ENV_ATK * (abs_r - env_r)
                                    : env_r + ENV_REL * (abs_r - env_r);
            float al = lp_l * (1.0f + 3.0f * env_l) * auto_gain;
            float ar = lp_r * (1.0f + 3.0f * env_r) * auto_gain;
            al = al / (1.0f + (al < 0.0f ? -al : al));
            ar = ar / (1.0f + (ar < 0.0f ? -ar : ar));

            if (auto_mode == 3) {              // Replace
                h_l = al; h_r = ar;
            } else if (auto_mode == 2) {       // Mix
                float m_l = h_l + al, m_r = h_r + ar;
                h_l = m_l / (1.0f + (m_l < 0.0f ? -m_l : m_l));
                h_r = m_r / (1.0f + (m_r < 0.0f ? -m_r : m_r));
            } else if (auto_mode == 1 && fallback_active) {  // Fallback (default)
                h_l = al; h_r = ar;
            }
        }

        in_buf[i * 2]     = static_cast<WDL_ResampleSample>(clamp(h_l, -1.0f, 1.0f));
        in_buf[i * 2 + 1] = static_cast<WDL_ResampleSample>(clamp(h_r, -1.0f, 1.0f));
    }
    g_peak_spk = spk_max;
    g_peak_hap = hap_max;
    if (hap_max > NATIVE_THRESHOLD) {
        native_silent_count = 0;
    } else if (native_silent_count < NATIVE_SILENT_TIMEOUT * 2) {
        native_silent_count++;
    }

    // 3. 48kHz -> 3kHz 重采样
    static WDL_ResampleSample out_buf[SAMPLE_SIZE]; // 64 floats = 32帧 × 2ch
    const int out_frames = resampler.ResampleOut(out_buf, nframes, nframes / 4, OUTPUT_CHANNELS);

    static int8_t haptic_buf[SAMPLE_SIZE];
    static int haptic_buf_pos = 0;

    // 4. 转换为int8并缓冲，满64字节即组包发送
    for (int i = 0; i < out_frames; i++) {
        // Gain after the resampler, as upstream (a55fd46).
        int val_l = static_cast<int>(out_buf[i * 2] * 127.0f * haptics_gain);
        int val_r = static_cast<int>(out_buf[i * 2 + 1] * 127.0f * haptics_gain_r);
        haptic_buf[haptic_buf_pos++] = (int8_t) clamp(val_l, -128, 127); // 似乎clamp有点多余？还是以防万一吧
        haptic_buf[haptic_buf_pos++] = (int8_t) clamp(val_r, -128, 127);

        if (haptic_buf_pos != SAMPLE_SIZE) {
            continue;
        }
        static haptics_element element{};
        memcpy(element.data, haptic_buf, SAMPLE_SIZE);
        if (queue_is_full(&haptics_fifo)) {
            queue_try_remove(&haptics_fifo, NULL);
            g_drop_hap++;
        }
        g_hap_push++;
        queue_try_add(&haptics_fifo, &element);
        haptic_buf_pos = 0;
    }
    audio_bt_task();
}

#if DS5_C1_DIAG
// Core1's loop heartbeat: time_us_32() at the top of every pass.
static volatile uint32_t g_c1_heartbeat_us = 0;
static volatile bool g_c1_heartbeat_set = false;
// Stack paint: core0 fills core1's stack with this before launching it; the
// lowest word that still holds it is the high-water mark.
static constexpr uint32_t kC1StackPaint = 0xC1C1C1C1u;
// MSPLIM sits this far above the stack base so the HardFault handler still
// has room to run after a stack overflow (the SDK puts it at the base itself).
static constexpr uint32_t kC1StackGuardBytes = 64;
static volatile bool g_c1_guard_armed = false;

#if PICO_RP2350
// Core1 HardFault record, written once by the handler below (in RAM).
struct C1FaultRecord {
    uint32_t magic, pc, lr, xpsr, cfsr, hfsr, mmfar, bfar, sfsr, sp, exc_return, stage, us;
};
static constexpr uint32_t kC1FaultMagic = 0xC1FA0017u;
static volatile C1FaultRecord g_c1_fault{};
extern "C" {
// Read by the naked handler's asm by name.
uint32_t c1diag_prev_hardfault = 0;  // core0's (the SDK's default) handler
uint32_t c1diag_stack_base = 0;      // core1's real stack base, for MSPLIM
}

// Core1 only: record the stacked frame and the fault status registers, then
// park in RAM for good. Core0 keeps running, so 0xEA can report it.
extern "C" void __not_in_flash_func(c1diag_fault_record)(const uint32_t *frame, uint32_t exc_return)
    __attribute__((noreturn, used, noinline));
extern "C" void __not_in_flash_func(c1diag_fault_record)(const uint32_t *frame, uint32_t exc_return) {
    const uint32_t f = (uint32_t) frame;
    g_c1_fault.sp = f;
    g_c1_fault.exc_return = exc_return;
    // Only read a frame that lies in SRAM (after a stack overflow it may be
    // partial, but it is still readable there).
    if ((f & 3u) == 0 && f >= SRAM_BASE && f + 32 <= SRAM_END) {
        g_c1_fault.lr = frame[5];
        g_c1_fault.pc = frame[6];
        g_c1_fault.xpsr = frame[7];
    }
    g_c1_fault.cfsr = *(volatile uint32_t *) 0xE000ED28u;
    g_c1_fault.hfsr = *(volatile uint32_t *) 0xE000ED2Cu;
    g_c1_fault.mmfar = *(volatile uint32_t *) 0xE000ED34u;
    g_c1_fault.bfar = *(volatile uint32_t *) 0xE000ED38u;
    g_c1_fault.sfsr = *(volatile uint32_t *) 0xE000EDE4u;
    g_c1_fault.stage = g_c1_stage;
    g_c1_fault.us = time_us_32();
    __dmb();
    g_c1_fault.magic = kC1FaultMagic;
    __dmb();
    for (;;) __wfe();
}

// HardFault on either core (the vector table is shared). Core0 goes on to the
// SDK's default handler as before (so the watchdog still reboots); core1
// gives its MSPLIM headroom back, picks the frame's stack and records.
extern "C" void __not_in_flash_func(c1diag_hardfault_handler)(void) __attribute__((naked, used));
extern "C" void __not_in_flash_func(c1diag_hardfault_handler)(void) {
    pico_default_asm_volatile(
        "ldr r0, =0xd0000000\n"          // SIO CPUID: 0 core0, 1 core1
        "ldr r0, [r0]\n"
        "cmp r0, #0\n"
        "bne 1f\n"
        "ldr r0, =c1diag_prev_hardfault\n"
        "ldr r0, [r0]\n"
        "bx r0\n"
        "1:\n"
        "ldr r1, =c1diag_stack_base\n"
        "ldr r1, [r1]\n"
        "msr msplim, r1\n"                // the 64 guard bytes are ours now
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "mov r1, lr\n"
        "b c1diag_fault_record\n"
        ".ltorg\n");
}
#endif  // PICO_RP2350

void c1diag_snapshot(C1DiagSnapshot *s) {
    *s = C1DiagSnapshot{};
    s->stage_now = g_c1_stage;
    if (g_c1_heartbeat_set) s->heartbeat_age_us = time_us_32() - g_c1_heartbeat_us;
    if (g_c1_guard_armed) s->flags |= C1DIAG_STACK_GUARD;
    uint32_t untouched = 0;
    for (uint32_t i = 0; i < count_of(audio_core1_stack) && audio_core1_stack[i] == kC1StackPaint; i++) untouched++;
    s->stack_free_bytes = untouched * 4;
    s->rom_version = *(volatile const uint8_t *) 0x13;
#if defined(PICO_EMBED_XIP_SETUP) && PICO_EMBED_XIP_SETUP
    s->flags |= C1DIAG_F1_XIP_SETUP;
#endif
#if DS5_C1_BUSY_IDLE
    s->flags |= C1DIAG_F3_BUSY_IDLE;
#endif
#if PICO_RP2350
    s->flags |= C1DIAG_RP2350;
    s->chip_version = rp2350_chip_version();
    if (c1diag_prev_hardfault != 0) s->flags |= C1DIAG_RECORDER;
    s->qmi_timing = qmi_hw->m[0].timing;
    s->qmi_rfmt = qmi_hw->m[0].rfmt;
    s->qmi_rcmd = qmi_hw->m[0].rcmd;
    if (g_c1_fault.magic == kC1FaultMagic) {
        s->flags |= C1DIAG_FAULTED;
        s->stage_at_fault = (uint8_t) g_c1_fault.stage;
        s->pc = g_c1_fault.pc;
        s->lr = g_c1_fault.lr;
        s->xpsr = g_c1_fault.xpsr;
        s->cfsr = g_c1_fault.cfsr;
        s->hfsr = g_c1_fault.hfsr;
        s->mmfar = g_c1_fault.mmfar;
        s->bfar = g_c1_fault.bfar;
        s->sp = g_c1_fault.sp;
        s->fault_us = g_c1_fault.us;
        s->exc_return = (uint8_t) g_c1_fault.exc_return;
        s->sfsr = (uint8_t) g_c1_fault.sfsr;
    }
#else
    s->chip_version = rp2040_chip_version();
#endif
}
#endif  // DS5_C1_DIAG

void audio_init() {
    resampler.SetMode(true, 0, false);
    resampler.SetRates(48000, 3000);
    resampler.SetFeedMode(true);
    resampler.Prealloc(2, 48, 4);
    queue_init(&haptics_fifo, sizeof(haptics_element), 2);
#if !DISABLE_SPEAKER_PROC
    queue_init(&audio_fifo, sizeof(audio_raw_element), 2);
    queue_init(&audio_spk_fifo, sizeof(audio_spk_element), 2);
#endif
    // Mic queues: core0 fills mic_fifo from BT, core1 decodes into the jitter
    // buffer, core0 drains it to USB. Mic Opus is mono / 48 kHz / 10 ms frames.
    queue_init(&mic_fifo, sizeof(mic_element), MIC_DECODE_DEPTH);          // deeper: tolerate BT bursts
    queue_init(&mic_decode_fifo, sizeof(mic_decoded_element), MIC_DECODE_DEPTH); // decoded-PCM jitter buffer
#if DS5_C1_DIAG
    for (auto &w : audio_core1_stack) w = kC1StackPaint;
#if PICO_RP2350
    c1diag_stack_base = (uint32_t) audio_core1_stack;
    c1diag_prev_hardfault = (uint32_t) exception_set_exclusive_handler(HARDFAULT_EXCEPTION, c1diag_hardfault_handler);
#endif
#endif
    // Core1 now always runs: it owns the mic decoder even in no-speaker builds.
    multicore_launch_core1_with_stack(core1_entry, audio_core1_stack, sizeof(audio_core1_stack));
}

#if !DISABLE_SPEAKER_PROC
static OpusEncoder *encoder;
static WDL_Resampler resampler_audio;

// Speaker path: USB OUT PCM (core0 audio_fifo) -> resample -> Opus encode ->
// audio_spk_fifo for audio_bt_task. Non-blocking so core1 can also run the mic.
static void __not_in_flash_func(speaker_proc)() {
    static audio_raw_element audio_element{};
    C1_STAGE(10);
    if (!queue_try_remove(&audio_fifo, &audio_element)) return;
    C1_STAGE(11);
    // 将 512 frames 重采样成 480 frames 以解决噪音问题。感谢 @Junhoo
    WDL_ResampleSample *in_buf;
    int nframes = resampler_audio.ResamplePrepare(512, 2, &in_buf);
    for (int i = 0; i < nframes * 2; i++) {
        in_buf[i] = audio_element.data[i];
    }
    static WDL_ResampleSample out_buf[480 * 2];
    resampler_audio.ResampleOut(out_buf, nframes, 480, 2);

    static audio_spk_element spk{};
    C1_STAGE(12);
    const int encoded = opus_encode_float(encoder, out_buf, 480, spk.data, SPEAKER_OPUS_SIZE);
    if (encoded <= 0) return;
    g_c1_enc++;
    if (encoded < SPEAKER_OPUS_SIZE) memset(spk.data + encoded, 0, SPEAKER_OPUS_SIZE - encoded);
    if (queue_is_full(&audio_spk_fifo)) {
        queue_try_remove(&audio_spk_fifo, NULL);
        g_drop_enc++;
    }
    queue_try_add(&audio_spk_fifo, &spk);
    C1_STAGE(13);
}
#endif

static OpusDecoder *mic_decoder = nullptr;

static void __not_in_flash_func(mic_push_decoded)(const mic_decoded_element &dec) {
    if (queue_is_full(&mic_decode_fifo)) queue_try_remove(&mic_decode_fifo, NULL);
    queue_try_add(&mic_decode_fifo, &dec);
}

// Mic path: Opus from the controller (mic_fifo) -> decode -> jitter buffer.
// If real packets stop mid-session, conceal one frame per missed 10 ms slot
// via Opus PLC; after MIC_SESSION_US of silence the session is over.
static bool __not_in_flash_func(mic_proc)() {
    static mic_element pkt{};
    static mic_decoded_element dec{};
    static uint32_t last_out_us = 0;
    const uint32_t now = time_us_32();

    if (queue_try_remove(&mic_fifo, &pkt)) {
        const int n = opus_decode(mic_decoder, pkt.data, MIC_OPUS_SIZE, dec.mono, MIC_FRAMES, 0);
        g_mic_last_decoded = n; // observed in OLED Diag
        if (n > 0) {
            mic_push_decoded(dec);
            g_mic_last_real_us = now;
            g_mic_session = true;
            g_mic_frames++;
            last_out_us = now;
        }
        return true;
    }
    if (!g_mic_session) return false;
    if (now - g_mic_last_real_us >= MIC_SESSION_US) {
        g_mic_session = false;
        return false;
    }
    // Half a frame of slack before concealing, so normal BT jitter isn't
    // mistaken for loss.
    if (now - last_out_us >= MIC_FRAME_US + MIC_FRAME_US / 2) {
        const int n = opus_decode(mic_decoder, nullptr, 0, dec.mono, MIC_FRAMES, 0); // PLC
        if (n > 0) {
            mic_push_decoded(dec);
            g_mic_plc_frames++;
        }
        last_out_us += MIC_FRAME_US;
        return true;
    }
    return false;
}

void __not_in_flash_func(core1_entry)() {
    g_c1_stage = 1;
#if DS5_C1_DIAG && PICO_RP2350
    // Stack guard 64 bytes above the base (the SDK set it at the base): an
    // overflow faults with room left for the HardFault recorder.
    pico_default_asm_volatile("msr msplim, %0" : : "r"((uint32_t) audio_core1_stack + kC1StackGuardBytes));
    g_c1_guard_armed = true;
#endif
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
    encoder = opus_encoder_create(48000, 2, OPUS_APPLICATION_RESTRICTED_CELT, &error);
#else
    encoder = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &error);
#endif
    g_c1_stage = 5;
    if (error != 0 || encoder == nullptr) {
        printf("[Audio] OpusEncoder create failed (err=%d, free heap %lu): speaker off\n", error, (unsigned long) g_c1_free_heap);
        encoder = nullptr;
        g_c1_flags |= 0x02;
    } else {
        opus_encoder_ctl(encoder,OPUS_SET_EXPERT_FRAME_DURATION(OPUS_FRAMESIZE_10_MS));
        opus_encoder_ctl(encoder,OPUS_SET_BITRATE(200 * 8 * 100));
        opus_encoder_ctl(encoder,OPUS_SET_VBR(false));
        opus_encoder_ctl(encoder,OPUS_SET_COMPLEXITY(0)); // max 4
        resampler_audio.SetMode(true, 0, false);
        resampler_audio.SetRates(51200, 48000);
        resampler_audio.SetFeedMode(true);
        resampler_audio.Prealloc(2, 512, 480);
        g_spk_ready = true;
    }
    g_c1_stage = 6;
#endif
    g_c1_stage = 7;
    mic_decoder = opus_decoder_create(48000, MIC_CHANNELS, &error);
    g_c1_stage = 8;
    if (error != 0) {
        printf("[Audio] OpusDecoder create failed (err=%d)\n", error);
        mic_decoder = nullptr;
        g_c1_flags |= 0x04;
    }

    while (true) {
        g_c1_stage = 9;  // running: stage 9 and c1_loops moving
        g_c1_loops++;
#if DS5_C1_DIAG
        g_c1_heartbeat_us = time_us_32();
        g_c1_heartbeat_set = true;
#endif
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
            C1_STAGE(14);
            const uint32_t t0 = perf_now();
            if (mic_proc()) {
                perf_record(PERF_C1_MIC, perf_now() - t0);
                work = true;
            }
        }
        // Idle without spinning on the queue spinlocks (starves core0's bus).
        if (!work) {
            C1_STAGE(15);
#if DS5_C1_BUSY_IDLE
            // Issue #15 candidate F3: wait in RAM (sleep_us runs from flash
            // and goes through the alarm pool).
            busy_wait_at_least_cycles(SYS_CLOCK_KHZ * 10u / 1000u);
#else
            sleep_us(10);
#endif
        }
    }
}
