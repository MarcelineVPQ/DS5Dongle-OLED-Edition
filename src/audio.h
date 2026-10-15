//
// Created by awalol on 2026/3/5.
//

#ifndef DS5_BRIDGE_AUDIO_H
#define DS5_BRIDGE_AUDIO_H

#include <cstdint>

void audio_init();
void audio_loop();
void core1_entry();
void set_headset(bool state);

// Accessors used by the optional OLED add-on (diag + VU meter screens).
uint32_t audio_fifo_drops();
uint32_t opus_fifo_drops();
uint32_t audio_hap_drops();
uint32_t audio_c1_loops();
uint32_t audio_c1_enc();
uint32_t audio_c1_flags();      // bit0 started, bit1 speaker encoder failed, bit2 mic decoder failed
uint32_t audio_c1_free_heap();  // bytes free when core1 started
uint32_t audio_c1_stage();      // core1 start-up stage 0..9 (9 = in its loop), see audio.cpp
uint32_t audio_raw_push();
uint32_t audio_hap_push();
uint8_t  audio_peak_speaker();   // 0..255, falls with elapsed time, read-only
uint8_t  audio_peak_haptic();    // 0..255, falls with elapsed time, read-only

// Byte-flow counters for the Diagnostics screen + web emulator.
uint32_t audio_usb_frames();
uint32_t audio_bt_packets();
uint32_t audio_mic_frames();   // count of mic Opus frames decoded + written
int32_t  audio_mic_last_decoded(); // last opus_decode return — neg = error, 480 = OK
uint16_t audio_mic_last_want();    // bytes asked of tud_audio_write
uint16_t audio_mic_last_wrote();   // bytes TinyUSB FIFO actually accepted
uint8_t  audio_mic_last_toc();     // first byte of last Opus packet (frame config)
uint32_t audio_mic_plc_frames();   // count of packet-loss-concealment frames generated
uint32_t audio_mic_level_drops();  // frames the jitter buffer's level control dropped (src/mic_level.h)
uint8_t audio_mic_level();         // jitter-buffer frames queued at the last frame boundary
uint8_t audio_mic_level_max();     // ...the highest this mic session

// Called from on_bt_data() in main.cpp when the DS5 sends a mic-tagged
// 0x31 input report. Buffer must point at MIC_OPUS_SIZE (71) bytes of
// Opus payload.
void mic_add_queue(const uint8_t *data);

// Host opened/closed the mic IN interface (tud_audio_set_itf_cb / unmount).
void audio_set_mic_active(bool active);
// True while the host is recording AND BT Mic is enabled — gates the DS5
// mic-enable bit and the mic decode path.
bool audio_mic_wanted();
// New BT link: re-send the mic-stream state to the controller.
void audio_on_bt_connected();
// The connect-time state has gone out again since USB came up (voice.cpp
// waits for it before the connected line).
bool audio_connect_state_sent();

// The narrator (voice.cpp): is the host streaming audio through us, and
// send one report carrying only her frames.
bool audio_host_streaming();
void audio_send_voice_report();

// The haptic feedback test (0xF6 0x0E, src/grip_wave.h): a steady wave on the
// left and/or right grip for `samples` 3 kHz haptic samples, in the 0x39
// haptic stream. Start replaces a running test; stop ends it at once.
void audio_grip_test_start(bool left, bool right, uint32_t samples);
void audio_grip_test_stop();
bool audio_grip_test_running();
// The test is sending its own 0x39 reports (no host audio): the narrator's
// frames ride in them, so voice_task sends none of its own.
bool audio_grip_test_sending();
#endif //DS5_BRIDGE_AUDIO_H