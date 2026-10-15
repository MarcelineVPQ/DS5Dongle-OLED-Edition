#pragma once
#include <cstdint>
#include <map>
#include <utility>
#include <vector>
#include "grip_wave.h"
void fake_flash_erase_all();
extern bool g_fake_flash_fail;
extern uint64_t g_fake_time_us;
extern bool g_fake_bt_connected;
extern int g_fake_slot;
extern int g_fake_reboots;
extern int g_fake_usb_reconnects;  // usb_power_reconnect() (0xF6 0x03)
extern uint32_t g_fake_c1_flags;      // audio_c1_flags()
extern uint32_t g_fake_c1_free_heap;  // audio_c1_free_heap(), bytes
extern uint32_t g_fake_c1_stage;      // audio_c1_stage(), 0..9
extern std::map<uint8_t, std::vector<uint8_t>> g_fake_features;
extern std::vector<std::vector<uint8_t>> g_fake_feature_sets;
extern int g_fake_feature_gets;
extern int g_fake_feature_copies;  // bt_feature_copy() calls (the no-heap 0x81 peek)
extern long g_heap_allocs;         // test_heap.cpp: every operator new
extern void (*g_fake_feature_get_hook)(uint8_t id);
extern std::vector<std::vector<uint8_t>> g_fake_control_sends;  // bt_send_control (raw, e.g. SET 0x65)
extern GripWave g_fake_grip;  // the audio_grip_test_* fakes play it (cmd 0xF6 0x0E)

// oled_key_inject (0xF6 0x18) pushes onto this injector.
#include "oled_keys.h"
extern oledkeys::Injector g_fake_key_inject;

// oled_pad_inject (0xF6 0x19) pushes onto this injector.
#include "oled_vpad.h"
extern oledvpad::Injector g_fake_pad_inject;
