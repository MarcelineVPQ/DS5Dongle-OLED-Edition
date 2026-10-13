#pragma once
#include <cstdint>
#include <map>
#include <vector>
void fake_flash_erase_all();
extern bool g_fake_flash_fail;
extern uint64_t g_fake_time_us;
extern bool g_fake_bt_connected;
extern int g_fake_slot;
extern int g_fake_reboots;
extern uint32_t g_fake_c1_flags;      // audio_c1_flags()
extern uint32_t g_fake_c1_free_heap;  // audio_c1_free_heap(), bytes
extern uint32_t g_fake_c1_stage;      // audio_c1_stage(), 0..15
struct C1DiagSnapshot;
extern C1DiagSnapshot g_fake_c1diag;  // c1diag_snapshot() (GET 0xEA)
extern std::map<uint8_t, std::vector<uint8_t>> g_fake_features;
extern std::vector<std::vector<uint8_t>> g_fake_feature_sets;
extern int g_fake_feature_gets;
extern void (*g_fake_feature_get_hook)(uint8_t id);
