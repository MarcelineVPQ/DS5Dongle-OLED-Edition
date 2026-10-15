// The rest of the firmware, faked for the host tests: flash in RAM, a settable
// clock, and the Bluetooth / audio / OLED / voice functions src/cmd.cpp calls.
#include <cstring>
#include <map>
#include <vector>
#include "hardware/flash.h"
#include "hardware/timer.h"
#include "pico/flash.h"
#include "fakes.h"

uint8_t g_fake_flash[PICO_FLASH_SIZE_BYTES];
bool g_fake_flash_fail = false;
uint64_t g_fake_time_us = 0;
static fake_timer_hw_t s_timer;
fake_timer_hw_t *timer_hw = &s_timer;

void flash_range_erase(uint32_t offset, size_t count) { std::memset(g_fake_flash + offset, 0xFF, count); }
void flash_range_program(uint32_t offset, const uint8_t *data, size_t count) {
    for (size_t i = 0; i < count; i++) g_fake_flash[offset + i] &= data[i];  // NOR: program only clears bits
}
int flash_safe_execute(void (*fn)(void *), void *param, uint32_t) {
    if (g_fake_flash_fail) return -1;
    fn(param);
    return PICO_OK;
}
void fake_flash_erase_all() { std::memset(g_fake_flash, 0xFF, sizeof(g_fake_flash)); }

// --- Hardware the 0xF5/0xFC status reports read ---------------------------
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/cyw43_arch.h"
#include "device/usbd.h"
void adc_init() {}
void adc_set_temp_sensor_enabled(bool) {}
void adc_select_input(uint32_t) {}
uint16_t adc_read() { return 876; }
uint32_t frequency_count_khz(uint32_t) { return SYS_CLOCK_KHZ; }
vreg_voltage vreg_get_voltage() { return VREG_VOLTAGE_DEFAULT; }
int g_fake_reboots = 0;
void watchdog_reboot(uint32_t, uint32_t, uint32_t) { g_fake_reboots++; }
void reset_usb_boot(uint32_t, uint32_t) { g_fake_reboots++; }
void cyw43_arch_gpio_put(uint32_t, bool) {}
bool tud_connect() { return true; }
bool tud_disconnect() { return true; }
int g_fake_usb_reconnects = 0;
void usb_power_reconnect() { g_fake_usb_reconnects++; }

// --- Bluetooth (bt.cpp) ----------------------------------------------------
#include "bt.h"
bool g_fake_bt_connected = false;
int g_fake_slot = 0;
bool bt_is_connected() { return g_fake_bt_connected; }
uint32_t bt_hci_err_count() { return 0; }
void bt_get_signal_strength(int8_t *rssi) { *rssi = -50; }
// The feature-report cache (bt.cpp feature_data: [id, data..., crc32]), what
// was SET to the controller, and a hook standing in for its replies to GETs.
std::map<uint8_t, std::vector<uint8_t>> g_fake_features;
std::vector<std::vector<uint8_t>> g_fake_feature_sets;
int g_fake_feature_gets = 0;
int g_fake_feature_copies = 0;
void (*g_fake_feature_get_hook)(uint8_t id) = nullptr;
std::vector<uint8_t> get_feature_data(uint8_t id, uint16_t) {
    auto it = g_fake_features.find(id);
    std::vector<uint8_t> ret = it != g_fake_features.end() ? it->second : std::vector<uint8_t>{};
    g_fake_feature_gets++;
    if (g_fake_feature_get_hook) g_fake_feature_get_hook(id);
    return ret;
}
void bt_request_feature(uint8_t id) {
    g_fake_feature_gets++;
    if (g_fake_feature_get_hook) g_fake_feature_get_hook(id);
}
uint16_t bt_feature_copy(uint8_t id, uint8_t *out, uint16_t cap) {
    auto it = g_fake_features.find(id);
    if (it == g_fake_features.end()) return 0;
    const size_t n = it->second.size() < cap ? it->second.size() : cap;
    memcpy(out, it->second.data(), n);
    g_fake_feature_copies++;
    return (uint16_t) n;
}
std::vector<uint8_t> bt_peek_feature(uint8_t id) {
    auto it = g_fake_features.find(id);
    return it != g_fake_features.end() ? it->second : std::vector<uint8_t>{};
}
void bt_drop_feature(uint8_t id) { g_fake_features.erase(id); }
void set_feature_data(uint8_t id, uint8_t *data, uint16_t len) {
    std::vector<uint8_t> v{id};
    v.insert(v.end(), data, data + len);
    g_fake_feature_sets.push_back(v);
}
std::vector<std::vector<uint8_t>> g_fake_control_sends;
void bt_send_control(uint8_t *data, uint16_t len) { g_fake_control_sends.emplace_back(data, data + len); }
int bt_get_slot() { return g_fake_slot; }
// Like bt.cpp: the change is made, then saved (false = the flash write failed).
bool bt_set_slot(int slot) { g_fake_slot = slot; return !g_fake_flash_fail; }
bool bt_forget_slot(int) { return !g_fake_flash_fail; }
bool bt_wipe_all_slots() { return !g_fake_flash_fail; }
bool bt_slot_occupied(int slot) { return slot == 0; }
void bt_slot_get_addr(int, uint8_t out[6]) { std::memset(out, 0, 6); }

// --- Audio (audio.cpp) -----------------------------------------------------
#include "audio.h"
uint32_t audio_fifo_drops() { return 1; }
uint32_t opus_fifo_drops() { return 2; }
uint32_t audio_hap_drops() { return 3; }
uint32_t audio_c1_loops() { return 4; }
uint32_t audio_c1_enc() { return 5; }
uint32_t g_fake_c1_flags = 1;
uint32_t g_fake_c1_free_heap = 100000;
uint32_t audio_c1_flags() { return g_fake_c1_flags; }
uint32_t audio_c1_free_heap() { return g_fake_c1_free_heap; }
uint32_t g_fake_c1_stage = 9;
uint32_t audio_c1_stage() { return g_fake_c1_stage; }
uint32_t audio_raw_push() { return 6; }
uint32_t audio_hap_push() { return 7; }
uint32_t audio_usb_frames() { return 8; }
uint32_t audio_bt_packets() { return 9; }
uint32_t audio_mic_frames() { return 10; }
uint32_t audio_mic_plc_frames() { return 11; }
uint32_t audio_mic_level_drops() { return 0x01020304; }
uint8_t audio_mic_level() { return 4; }
uint8_t audio_mic_level_max() { return 6; }
uint8_t audio_peak_speaker() { return 12; }
uint8_t audio_peak_haptic() { return 13; }
GripWave g_fake_grip;
void audio_grip_test_start(bool left, bool right, uint32_t samples) { g_fake_grip.start(left, right, samples); }
void audio_grip_test_stop() { g_fake_grip.stop(); }
bool audio_grip_test_running() { return g_fake_grip.active(); }
bool audio_grip_test_sending() { return false; }

// --- main.cpp / state_mgr / oled / voice ----------------------------------
#include "state_mgr.h"
#include "oled.h"
#include "oled_keys.h"
#include "oled_vpad.h"
#include "voice.h"
void state_get_led(uint8_t *r, uint8_t *g, uint8_t *b) { *r = 1; *g = 2; *b = 3; }
uint8_t state_get_player_leds() { return 0x04; }
uint8_t state_get_pc_mic_volume() { return 0x15; }
void oled_send_trigger_preset(int) {}
// oled.cpp's oled_key_inject is a one-line push onto its injector; this is the same.
oledkeys::Injector g_fake_key_inject;
bool oled_key_inject(uint8_t key, bool long_press) { return oledkeys::push(g_fake_key_inject, key, long_press); }
// oled.cpp's oled_pad_inject is a one-line push onto its injector; this is the same.
oledvpad::Injector g_fake_pad_inject;
bool oled_pad_inject(uint8_t button, uint16_t hold_ms) { return oledvpad::push(g_fake_pad_inject, button, hold_ms); }
void voice_say_lang(VoiceLine, uint8_t) {}

#include "cmd.h"
volatile uint32_t g_bt_31_packets, g_bt_other_packets;
volatile uint8_t g_last_other_id, g_other_id_or, g_31_b2_or, g_last_31_b2;
volatile uint16_t g_31_len_min = 0xFFFF, g_31_len_max;
volatile uint8_t g_last_other_prefix[8];
volatile uint8_t g_last_any_prefix[16];
volatile uint16_t g_longest_len;
volatile uint8_t g_longest_frame[MIC_DEBUG_FRAME_BYTES];
volatile uint32_t g_host_out02_total, g_host_out02_trig_allow, g_host_out02_to_bt;
volatile uint32_t g_fse_calls, g_fse_fail, g_setfeat_count, g_setfeat_last;
uint8_t g_last_out02[47];
uint8_t g_get_ring[16][2];
volatile uint32_t g_get_count;
uint8_t g_last_out02_trig[47];
uint8_t interrupt_in_data[63];
