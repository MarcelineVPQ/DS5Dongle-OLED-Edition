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
vreg_voltage vreg_get_voltage() { return VREG_VOLTAGE_1_20; }
int g_fake_reboots = 0;
void watchdog_reboot(uint32_t, uint32_t, uint32_t) { g_fake_reboots++; }
void reset_usb_boot(uint32_t, uint32_t) { g_fake_reboots++; }
void cyw43_arch_gpio_put(uint32_t, bool) {}
bool tud_connect() { return true; }
bool tud_disconnect() { return true; }

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
void (*g_fake_feature_get_hook)(uint8_t id) = nullptr;
std::vector<uint8_t> get_feature_data(uint8_t id, uint16_t) {
    auto it = g_fake_features.find(id);
    std::vector<uint8_t> ret = it != g_fake_features.end() ? it->second : std::vector<uint8_t>{};
    g_fake_feature_gets++;
    if (g_fake_feature_get_hook) g_fake_feature_get_hook(id);
    return ret;
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
int bt_get_slot() { return g_fake_slot; }
void bt_set_slot(int slot) { g_fake_slot = slot; }
void bt_forget_slot(int) {}
void bt_wipe_all_slots() {}
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
uint8_t audio_peak_speaker() { return 12; }
uint8_t audio_peak_haptic() { return 13; }
#include "c1_diag.h"
C1DiagSnapshot g_fake_c1diag{};
void c1diag_snapshot(C1DiagSnapshot *out) { *out = g_fake_c1diag; }

// --- main.cpp / state_mgr / oled / voice ----------------------------------
#include "state_mgr.h"
#include "oled.h"
#include "voice.h"
void send_rumble(uint8_t, uint8_t) {}
uint8_t grip_test_level(bool) { return 64; }
void state_get_led(uint8_t *r, uint8_t *g, uint8_t *b) { *r = 1; *g = 2; *b = 3; }
uint8_t state_get_player_leds() { return 0x04; }
void oled_send_trigger_preset(int) {}
void voice_say_lang(VoiceLine, uint8_t) {}

volatile uint32_t g_bt_31_packets, g_bt_other_packets;
volatile uint8_t g_last_other_id, g_other_id_or, g_31_b2_or, g_last_31_b2;
volatile uint16_t g_31_len_min = 0xFFFF, g_31_len_max;
volatile uint8_t g_last_other_prefix[8];
volatile uint8_t g_last_any_prefix[16];
volatile uint16_t g_longest_len;
volatile uint8_t g_longest_frame[80];
volatile uint32_t g_host_out02_total, g_host_out02_trig_allow, g_host_out02_to_bt;
volatile uint32_t g_fse_calls, g_fse_fail, g_setfeat_count, g_setfeat_last;
uint8_t g_last_out02[47];
uint8_t g_get_ring[16][2];
volatile uint32_t g_get_count;
uint8_t g_last_out02_trig[47];
uint8_t interrupt_in_data[63];
