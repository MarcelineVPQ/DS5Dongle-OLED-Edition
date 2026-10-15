//
// Created by awalol on 2026/3/4.
//

#ifndef DS5_BRIDGE_BT_H
#define DS5_BRIDGE_BT_H

#include <cstdint>
#include <vector>

enum CHANNEL_TYPE {
    INTERRUPT,
    CONTROL
};

typedef void (*bt_data_callback_t)(CHANNEL_TYPE channel, uint8_t *data, uint16_t len);

int bt_init();
void bt_register_data_callback(bt_data_callback_t callback);
void bt_send_packet(uint8_t *data, uint16_t len);
void bt_send_control(uint8_t *data, uint16_t len);
void bt_send_state();
void bt_write(const uint8_t *data, uint16_t len);
void bt_get_signal_strength(int8_t *rssi); // cached; refresh <= 10 Hz, one in flight
std::vector<uint8_t> get_feature_data(uint8_t reportId,uint16_t len);
// get_feature_data() for callers that only want the request sent (the answer
// lands in the feature cache): no copy of the cached report.
void bt_request_feature(uint8_t reportId);
// Copies the cached feature report (report id at [0]) into out, at most cap
// bytes; returns the bytes copied, 0 if it hasn't arrived. No heap: for the
// callers that poll every main-loop pass (the 0x81 test replies).
uint16_t bt_feature_copy(uint8_t reportId, uint8_t *out, uint16_t cap);
constexpr uint16_t kBtFeatureMax = 64;  // a DualSense feature report over BT, id + crc32 included
// Side-effect-free read of an already-cached feature report (empty vector if it
// hasn't arrived yet). Unlike get_feature_data(), never issues an L2CAP request,
// so it is safe to poll every frame — used by the OLED IMU-calibration parse.
std::vector<uint8_t> bt_peek_feature(uint8_t reportId);
void bt_drop_feature(uint8_t reportId);
void init_feature();
void set_feature_data(uint8_t reportId, uint8_t* data,uint16_t len);

// Connection-attempt watchdog: call once per main-loop iteration. Recovers a
// stalled connection (auto re-inquiry) so a transient RF glitch — e.g. USB 3.0
// 2.4 GHz interference — doesn't hang the dongle on the amber lightbar.
void bt_connection_watchdog_tick();

// OLED add-on accessors.
bool bt_is_connected();
bool bt_disconnect();
void bt_get_addr(uint8_t out[6]);
uint32_t bt_hci_err_count();

// Multi-slot persistent pairing (Phase G). Modeled on zurce/DS5Dongle-OLED.
int  bt_get_slot();
// false = the change is made but not saved (flash write failed): the caller
// tells the user (0xF1 CMD_RESULT_FAILED, the OLED's "Save FAIL").
bool bt_set_slot(int slot);
bool bt_forget_slot(int slot);
bool bt_wipe_all_slots();
bool bt_slot_occupied(int slot);
void bt_slot_get_addr(int slot, uint8_t out[6]);
bool bt_any_slot_paired();

// Pairing window (src/scan_policy.h). bt_pair_now() opens one for the current
// slot (KEY0 held 2 s on the OLED's Status screen): false with a controller
// connected (nothing to do). bt_pairing_seconds_left() = whole seconds left in
// a pairing window the Status screen counts down, 0 when there is none.
bool bt_pair_now();
uint32_t bt_pairing_seconds_left();

#endif //DS5_BRIDGE_BT_H