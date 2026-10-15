//
// Created by awalol on 2026/3/4.
//

#include <cstdio>
#include <cstring>
#include "bt.h"
#include "rssi_poll.h"
#include "core0_ram.h"
#include "voice.h"
#include "audio.h"
#include <queue>
#include <unordered_map>
#include <vector>
#include "btstack_event.h"
#include "gap.h"
#include "l2cap.h"
#include "pico/cyw43_arch.h"
#include "utils.h"
#include "bsp/board_api.h"
#include "classic/sdp_server.h"
#include "config.h"
#include "dse.h"
#include "fw_report.h"
#include "state_mgr.h"
#include "pico/util/queue.h"
#include "slots.h"
#include "usb_power.h"
#include "scan_policy.h"
#include "oled.h"
#include "oled_toast.h"
#if ENABLE_BATT_LED
#include "battery_led.h"
#endif

#define MTU_CONTROL 672
#define MTU_INTERRUPT 672

// Connection-attempt watchdog: if a connection commits to a device (inquiry
// found one / incoming request accepted) but doesn't reach USB-enumeration
// within this window, tear down and retry. Catches the silent stalls caused by
// USB 3.0 2.4 GHz RF interference on the CYW43 BT radio (DualSense stuck on the
// amber init lightbar, never enumerates) — see README troubleshooting. A
// healthy or slow re-pair finishes well under 6 s, so 10 s never trips a real
// connection but heals before the user reaches to replug.
#define CONNECT_WATCHDOG_TIMEOUT_US (10 * 1000 * 1000)

using std::unordered_map;
using std::vector;
using std::queue;

static void hci_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

static void l2cap_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

static btstack_packet_callback_registration_t hci_event_callback_registration, l2cap_event_callback_registration;
static bd_addr_t current_device_addr;
static bool device_found = false;
static bool new_pair = false; // 只有新匹配的设备才用创建channel，自动重连走的是service
static hci_con_handle_t acl_handle = HCI_CON_HANDLE_INVALID;
static uint16_t hid_control_cid;
static uint16_t hid_interrupt_cid;
static bt_data_callback_t bt_data_callback = nullptr;

// A neutral DualSense input report (sticks centred, nothing pressed), fed to
// the data callback on disconnect. Upstream 8c8b527's state_init_data.
static const uint8_t kNeutralInput[66] = {
    0xa2, 0x31, 0x01,
    0x7f, 0x7d, 0x7f, 0x7e, 0x00, 0x00, 0xa7,
    0x08, 0x00, 0x00, 0x00, 0x52, 0x43, 0x30, 0x41,
    0x01, 0x00, 0x0e, 0x00, 0xef, 0xff, 0x03, 0x03,
    0x7b, 0x1b, 0x18, 0xf0, 0xcc, 0x9c, 0x60, 0x00,
    0xfc, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x09, 0x09, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xa7, 0xad, 0x60, 0x00, 0x29, 0x18, 0x00,
    0x53, 0x9f, 0x28, 0x35, 0xa5, 0xa8, 0x0c, 0x8b
};
static bool check_dse = false;
static int8_t bt_rssi = 0;
// lifetime: OK — cleared on disconnect (feature_data.clear()); ≤ ~10 report ids × ≤ 64 B meanwhile
unordered_map<uint8_t, vector<uint8_t> > feature_data;
queue_t send_fifo;

// Sized to the interrupt MTU like upstream: the 0x39 audio report is 547 bytes
// (+1 for the 0xA2 header), which overflowed the old 512-byte element.
struct send_element {
    uint8_t data[MTU_INTERRUPT];
    size_t len;
};

absolute_time_t inactive_time = 0; // 手柄长时间静默

// Connection-attempt watchdog timestamp. 0 == not armed; armed == a connection
// attempt is in flight (committed to a device, not yet USB-enumerating). Set
// when an attempt begins, cleared the instant the controller type is identified
// (USB connects) and on every teardown. Checked by bt_connection_watchdog_tick().
static absolute_time_t connect_attempt_started = 0;

// Multi-slot pairing state. Modeled on zurce/DS5Dongle-OLED.
static int g_current_slot = 0;

bool bt_disconnect();  // fwd decl — defined further down

// Keep the dongle discoverable while at least one slot is empty (covers
// initial setup + partial-wipe states). Once all 4 slots are full, go
// non-discoverable so a stray phone can't try to pair.
// Page scanning: fast (continuous 11.25 ms, interlaced) while waiting for a
// controller so it reconnects quickly after PS (upstream ade9ea1), light
// (11.25 ms every 1.28 s, the Bluetooth default) while one is connected: the
// dongle stays connectable, and scanning all the time took radio time from
// the audio stream (crackling speaker).
static void page_scan_fast(bool fast) {
    gap_set_page_scan_activity(fast ? 0x0012 : 0x0800, 0x0012);
}

static void update_discoverable() {
    if (slots_any_empty()) {
        gap_discoverable_control(1);
    } else {
        gap_discoverable_control(0);
    }
}

// Inquiry (the active scan for a controller in pairing mode) runs only in a
// pairing window, never forever: src/scan_policy.h has the policy. A paired
// controller doesn't need it, it pages the dongle (page scan is always on).
static ScanPolicy g_scan;
static bool g_hci_working = false;  // BTstack is up (HCI_STATE_WORKING)
static bool g_inquiring = false;    // an inquiry is running (cleared on GAP_EVENT_INQUIRY_COMPLETE)

bool bt_any_slot_paired() {
    // loop: NOT-WORTH — kNumSlots = 4, once per main-loop pass only while no controller is connected and no inquiry runs
    for (int i = 0; i < kNumSlots; i++) {
        if (slot_occupied(i)) return true;
    }
    // error: NOT-WORTH — not a failure: false = every slot is empty (the idle back-off may scan)
    return false;
}

// One 30-unit inquiry (38.4 s), unless one is already running.
static void inquiry_start() {
    // error: NOT-WORTH — not a failure: one inquiry runs already, or BTstack isn't up yet (HCI_STATE_WORKING starts the boot one)
    if (g_inquiring || !g_hci_working) return;
    // error: NOT-WORTH — nothing; gap_inquiry_start only fails while an inquiry is still ending (the next pairing action / idle gap starts a new one) or the stack is off
    if (gap_inquiry_start(30) == ERROR_CODE_SUCCESS) g_inquiring = true;
}

// An inquiry ended, or a connection attempt failed, with no controller
// connected: scan on only inside the pairing window.
static void inquiry_continue() {
    if (g_scan.again(time_us_64())) inquiry_start();
}

// A pairing action (slot switch, forget, wipe): open the window and scan now
// if no controller is connected (else the disconnect that follows starts it).
static void pairing_window_open() {
    g_scan.open(time_us_64());
    if (acl_handle == HCI_CON_HANDLE_INVALID && !device_found) inquiry_start();
}

// "Pair now" on the board (KEY0 held 2 s on the OLED's Status screen): a
// pairing window for the current slot, when the boot / slot-action one ran out
// with nothing connected. With no controller the board is off USB, so the app
// can't ask for this; with one connected there is nothing to pair.
bool bt_pair_now() {
    // error: SHOWN — not a failure: the Status screen shows the controller (only that screen without one offers this)
    if (bt_is_connected()) return false;
    pairing_window_open();
    return true;
}

uint32_t bt_pairing_seconds_left() {
    if (bt_is_connected()) return 0;
    return g_scan.prompt_seconds_left(time_us_64());
}

void bt_register_data_callback(bt_data_callback_t callback) {
    bt_data_callback = callback;
}

// ---- OLED add-on + multi-slot accessors --------------------------------

bool bt_is_connected() { return hid_interrupt_cid != 0; }

void bt_get_addr(uint8_t out[6]) { memcpy(out, current_device_addr, 6); }

uint32_t bt_hci_err_count() { return 0; }  // stub for OLED Diagnostics

int bt_get_slot() { return g_current_slot; }

// Make `slot` the current one and save it (current_slot in the config);
// the link is left alone. false = not saved (the dongle boots in the old slot).
static bool make_slot_current(int slot) {
    g_current_slot = slot;
    Config_body cfg = get_config();
    cfg.current_slot = (uint8_t)slot;
    set_config(cfg);
    // error: SHOWN — returned to bt_set_slot / bt_forget_slot (0xF6 CMD_RESULT_FAILED, the OLED's "Save FAIL" / "Wipe FAIL"); the connect path marks its own call
    return config_save();
}

bool bt_set_slot(int slot) {
    // error: NOT-WORTH — false to the caller; every caller checks the slot first (0xF6 0x04/0x05 answer INVALID, the OLED cursor stays in 0..3)
    if (slot < 0 || slot >= kNumSlots) return false;
    if (slot == g_current_slot) return true;
    // error: SHOWN — returned: 0xF6 0x04 answers CMD_RESULT_FAILED (the app's command error), the OLED shows "Save FAIL"
    const bool saved = make_slot_current(slot);
    // A switch is how a new controller gets paired (into an empty slot), so
    // it opens a pairing window; a running inquiry keeps going under the new
    // slot's filter (applied per result).
    pairing_window_open();

    if (bt_is_connected()) {
        // The narrator names the new slot first (if she's on); then the drop.
        // DISCONNECTION_COMPLETE will restart inquiry under the new filter.
        voice_say(static_cast<VoiceLine>(VOICE_SLOT_0 + slot));
        if (voice_active()) voice_disconnect_after_line();
        // error: NOT-WORTH — nothing; false only when there is no ACL, i.e. already disconnected
        else bt_disconnect();
    }
    update_discoverable();
    return saved;
}

bool bt_slot_occupied(int slot) { return slot_occupied(slot); }
void bt_slot_get_addr(int slot, uint8_t out[6]) { slot_get_addr(slot, out); }

bool bt_forget_slot(int slot) {
    // error: NOT-WORTH — false to the caller; every caller checks the slot first (0xF6 0x04/0x05 answer INVALID, the OLED cursor stays in 0..3)
    if (slot < 0 || slot >= kNumSlots) return false;
    const bool had = slot_occupied(slot);
    uint8_t addr[6] = {};
    if (had) slot_get_addr(slot, addr);
    // error: SHOWN — returned: 0xF6 0x04/0x05/0x06 answer CMD_RESULT_FAILED (the app's command error), the OLED shows "Save FAIL" / "Wipe FAIL" (fixed in this pass: was dropped)
    bool saved = slot_forget(slot);
    // BTstack keeps one link key per address. Older firmware could put one
    // controller in two slots; forgetting one copy must not unpair the other.
    const int other = had ? slot_owner_of(addr) : -1;
    if (had && other < 0) gap_drop_link_key_for_bd_addr(addr);
    update_discoverable();
    pairing_window_open();  // room for a new controller: scan for one
    if (slot == g_current_slot && bt_is_connected()) {
        if (other >= 0 && memcmp(addr, current_device_addr, 6) == 0) {
            // The connected controller is still paired in `other`: go there.
            // error: SHOWN — returned with the forget: 0xF6 0x05 answers CMD_RESULT_FAILED, the OLED shows "Wipe FAIL"
            saved = make_slot_current(other) && saved;
        } else {
            // error: NOT-WORTH — nothing; false only when there is no ACL, i.e. already disconnected
            bt_disconnect();
        }
    }
    return saved;
}

bool bt_wipe_all_slots() {
    btstack_link_key_iterator_t it;
    // error: NOT-WORTH — nothing; no stored keys = nothing to drop, the slot table is still wiped
    if (gap_link_key_iterator_init(&it)) {
        bd_addr_t snapshot[16];
        int n = 0;
        bd_addr_t addr;
        link_key_t key;
        link_key_type_t type;
        // loop: NOT-WORTH — link-key snapshot, N ≤ 16; on a user wipe only
        while (n < 16 && gap_link_key_iterator_get_next(&it, addr, key, &type)) {
            bd_addr_copy(snapshot[n++], addr);
        }
        gap_link_key_iterator_done(&it);
        // loop: NOT-WORTH — drop each snapshotted key, N ≤ 16; on a user wipe only
        for (int i = 0; i < n; i++) {
            gap_drop_link_key_for_bd_addr(snapshot[i]);
        }
    }
    // error: SHOWN — returned: 0xF6 0x04/0x05/0x06 answer CMD_RESULT_FAILED (the app's command error), the OLED shows "Save FAIL" / "Wipe FAIL" (fixed in this pass: was dropped)
    const bool saved = slots_wipe_all();
    update_discoverable();
    pairing_window_open();  // nothing paired now: scan for a controller
    if (bt_is_connected()) {
        // error: NOT-WORTH — nothing; false only when there is no ACL, i.e. already disconnected
        bt_disconnect();
    }
    return saved;
}

void bt_send_packet(uint8_t *data, uint16_t len) {
    if (hid_interrupt_cid != 0) {
        // error: NOT-WORTH — unused (no caller); output goes through bt_write's FIFO
        l2cap_send(hid_interrupt_cid, data, len);
    }
}

void bt_send_control(uint8_t *data, uint16_t len) {
    if (hid_control_cid != 0) {
        // error: NOT-WORTH — nothing; one write on an open control channel (the Edge unlock handshake), fails only if the link just dropped, and everything restarts on reconnect
        l2cap_send(hid_control_cid, data, len);
    }
}

bool bt_disconnect() {
    if (acl_handle == HCI_CON_HANDLE_INVALID) {
        // error: NOT-WORTH — nothing; false only when there is no ACL, i.e. already disconnected
        return false;
    }

    // 0x13 = remote user terminated connection
    // error: LOGGED-OK — printf of the step + BTstack's own event; the connection watchdog (CONNECT_WATCHDOG_TIMEOUT_US) tears a stalled attempt down and inquiry restarts, the OLED shows not connected
    hci_send_cmd(&hci_disconnect, acl_handle, 0x13);
    return true;
}

// Called every main-loop iteration. If a connection attempt has stalled past
// the timeout, tear it down so the state machine retries instead of hanging
// (e.g. on the amber lightbar under USB 3.0 RF interference). Inert unless a
// connection attempt is in flight, so it never touches a healthy session.
void bt_connection_watchdog_tick() {
    if (connect_attempt_started == 0) {
        // Idle with nothing paired in any slot: one inquiry per idle gap
        // (src/scan_policy.h), instead of back-to-back ones.
        if (g_hci_working && !g_inquiring && acl_handle == HCI_CON_HANDLE_INVALID && !device_found) {
            // error: NOT-WORTH — not a failure: any_slot_paired() false only means nothing is paired
            const bool paired = bt_any_slot_paired();
            if (g_scan.idle_due(time_us_64(), paired)) inquiry_start();
        }
        return; // not armed
    }
    if (absolute_time_diff_us(connect_attempt_started, get_absolute_time())
            < CONNECT_WATCHDOG_TIMEOUT_US) {
        return;
    }
    printf("[BT] Connection watchdog: attempt stalled, recovering\n");
    connect_attempt_started = 0; // disarm; the next attempt re-arms

    if (acl_handle != HCI_CON_HANDLE_INVALID) {
        // ACL is up but setup stalled (auth/encryption/L2CAP/feature-wait).
        // Route through the proven HCI_EVENT_DISCONNECTION_COMPLETE teardown.
        // error: NOT-WORTH — nothing; false only when there is no ACL, i.e. already disconnected
        bt_disconnect();
    } else {
        // No ACL yet (stalled before/at create-connection) — reset by hand
        // and scan again if the pairing window is still open.
        device_found = false;
        new_pair = false;
        inquiry_continue();
        gap_connectable_control(1);
        update_discoverable();
    }
}

static RssiPoll rssi_poll;

void bt_get_signal_strength(int8_t *rssi) {
    // gap_read_rssi() completes asynchronously, so this function can only
    // return the last cached RSSI value. Ask for a refresh afterwards so a
    // later call sees the new value once the RSSI event arrives: at most
    // 10 Hz, one request in flight (src/rssi_poll.h), however many readers
    // (OLED BT Signal screen, app 0xF9). Nothing else polls it.
    if (rssi != nullptr) {
        *rssi = bt_rssi;
    }
    if (acl_handle == HCI_CON_HANDLE_INVALID) return;
    const uint64_t now = time_us_64();
    // error: NOT-WORTH — the RSSI poll retries after 1 s (rssi_poll.h); the OLED shows the last value
    if (rssi_poll.ready(now) && gap_read_rssi(acl_handle) == 0) rssi_poll.sent(now);
}

void bt_l2cap_init() {
    l2cap_event_callback_registration.callback = &l2cap_packet_handler;
    l2cap_add_event_handler(&l2cap_event_callback_registration);
    // 修复重连后自动断开的关键点
    sdp_init();
    // error: NOT-WORTH — boot: two fixed PSMs registered once on a fresh stack; can only fail out of memory, which the build sizes for
    // lifetime: NOT-WORTH — boot; BTstack keeps the service for the firmware's life
    l2cap_register_service(l2cap_packet_handler, PSM_HID_CONTROL, MTU_CONTROL, LEVEL_2);
    // error: NOT-WORTH — boot: two fixed PSMs registered once on a fresh stack; can only fail out of memory, which the build sizes for
    // lifetime: NOT-WORTH — boot; BTstack keeps the service for the firmware's life
    l2cap_register_service(l2cap_packet_handler, PSM_HID_INTERRUPT, MTU_INTERRUPT, LEVEL_2);

    l2cap_init();
}

int bt_init() {
    // lifetime: NOT-WORTH — boot, once per firmware run; the queue lives until reset
    queue_init(&send_fifo, sizeof(send_element), 10);

    // Load persistent slot table BEFORE HCI comes up so the inquiry filter
    // and discoverable-gating see the right state on the first event.
    slots_load();
    g_current_slot = get_config().current_slot;
    if (g_current_slot < 0 || g_current_slot >= kNumSlots) g_current_slot = 0;
    printf("[BT] Boot slot = %d\n", g_current_slot);

    bt_l2cap_init();

    // SSP (Secure Simple Pairing)
    gap_ssp_set_enable(true);
    gap_secure_connections_enable(true);
    gap_ssp_set_io_capability(SSP_IO_CAPABILITY_DISPLAY_YES_NO);
    gap_ssp_set_authentication_requirement(SSP_IO_AUTHREQ_MITM_PROTECTION_NOT_REQUIRED_GENERAL_BONDING);

    gap_connectable_control(1);
    update_discoverable();

    hci_event_callback_registration.callback = &hci_packet_handler;
    // lifetime: NOT-WORTH — boot; one registration for the firmware's life
    hci_add_event_handler(&hci_event_callback_registration);

    // error: NOT-WORTH — boot: power-on of the radio cyw43_arch_init already brought up; BTSTACK_EVENT_STATE never reaching WORKING leaves the OLED on its connecting screen
    hci_power_control(HCI_POWER_ON);
    return 0;
}

/*int main() {
    stdio_init_all();

    /*while (!stdio_usb_connected()) {
        sleep_ms(100);
    }
    printf("USB Serial connected!\n");#1#

    bt_init();

    while (1) {
        sleep_ms(10);
    }
}*/

static void CORE0_RAM_FUNC(hci_packet_handler)(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    (void) channel;

    const uint8_t event_type = hci_event_packet_get_type(packet);

    switch (event_type) {
        case BTSTACK_EVENT_STATE: {
            const uint8_t state = btstack_event_state_get_state(packet);
            printf("[BT] State: %u\n", state);
            if (state == HCI_STATE_WORKING) {
                gap_set_page_scan_type(PAGE_SCAN_MODE_INTERLACED);
                page_scan_fast(true);
                g_hci_working = true;
                // Boot opens a pairing window: the full one with an empty
                // slot, one inquiry when the slot holds a controller (it
                // reconnects on its own with PS; the inquiry only re-pairs it
                // if it lost its key).
                g_scan.open(time_us_64(), slot_occupied(g_current_slot) ? ScanPolicy::kBootWindowUs
                                                                       : ScanPolicy::kPairWindowUs);
                printf("[BT] Stack ready, start inquiry\n");
                inquiry_start();
            }
            break;
        }
        case HCI_EVENT_INQUIRY_RESULT:
        case HCI_EVENT_INQUIRY_RESULT_WITH_RSSI:
        case HCI_EVENT_EXTENDED_INQUIRY_RESPONSE: {
            bd_addr_t addr;
            uint32_t cod;

            if (event_type == HCI_EVENT_INQUIRY_RESULT) {
                cod = hci_event_inquiry_result_get_class_of_device(packet);
                hci_event_inquiry_result_get_bd_addr(packet, addr);
            } else if (event_type == HCI_EVENT_INQUIRY_RESULT_WITH_RSSI) {
                cod = hci_event_inquiry_result_with_rssi_get_class_of_device(packet);
                hci_event_inquiry_result_with_rssi_get_bd_addr(packet, addr);
            } else {
                cod = hci_event_extended_inquiry_response_get_class_of_device(packet);
                hci_event_extended_inquiry_response_get_bd_addr(packet, addr);
            }

            // CoD 0x002508 = Gamepad (Major: Peripheral, Minor: Gamepad)
            if ((cod & 0x000F00) == 0x000500) {
                // Slot-ownership filter: skip devices owned by a different slot;
                // if our slot is occupied, only accept its exact bd_addr.
                // Unowned devices pair into the current slot if it's empty.
                const int owner = slot_owner_of(addr);
                if (owner >= 0 && owner != g_current_slot) {
                    printf("[HCI] Gamepad %s belongs to slot %d, skip (cur=%d)\n",
                           bd_addr_to_str(addr), owner, g_current_slot);
                    break;
                }
                if (slot_occupied(g_current_slot)) {
                    uint8_t want[6];
                    slot_get_addr(g_current_slot, want);
                    if (memcmp(want, addr, 6) != 0) {
                        printf("[HCI] Slot %d wants different addr, skip %s\n",
                               g_current_slot, bd_addr_to_str(addr));
                        break;
                    }
                }
                printf("[HCI] Gamepad found: %s (CoD: 0x%06x)\n", bd_addr_to_str(addr), (unsigned int) cod);
                bd_addr_copy(current_device_addr, addr);
                device_found = true;
                // error: NOT-WORTH — nothing; gap_inquiry_* only fails while an inquiry is already in the wanted state or the stack is off, and INQUIRY_COMPLETE / the watchdog restart it
                gap_inquiry_stop();
            }
            break;
        }

        case GAP_EVENT_INQUIRY_COMPLETE:
        case HCI_EVENT_INQUIRY_COMPLETE: {
            printf("[HCI] Inquiry complete.\n");
            // BTstack sends GAP_EVENT_INQUIRY_COMPLETE for every end (also a
            // cancel), before the HCI event of a natural end.
            if (event_type == GAP_EVENT_INQUIRY_COMPLETE) g_inquiring = false;
            if (device_found) {
                printf("[HCI] Connecting to %s...\n", bd_addr_to_str(current_device_addr));
                new_pair = true;
                connect_attempt_started = get_absolute_time(); // arm connection watchdog
                // error: LOGGED-OK — printf of the step + BTstack's own event; the connection watchdog (CONNECT_WATCHDOG_TIMEOUT_US) tears a stalled attempt down and inquiry restarts, the OLED shows not connected
                hci_send_cmd(&hci_create_connection, current_device_addr,
                             hci_usable_acl_packet_types(), 0, 0, 0, 1);
                break;
            }
            if (event_type == HCI_EVENT_INQUIRY_COMPLETE) {
                printf("[HCI] Inquiry ended, scan on only in the pairing window\n");
                inquiry_continue();
                gap_connectable_control(1);
                update_discoverable();
            }
            break;
        }
        case HCI_EVENT_COMMAND_STATUS: {
            const uint8_t status = hci_event_command_status_get_status(packet);
            const uint16_t opcode = hci_event_command_status_get_command_opcode(packet);
            printf("[HCI] CmdStatus %s(0x%04X) status=0x%02X\n", opcode_to_str(opcode), opcode, status);
            if (opcode == HCI_OPCODE_HCI_CREATE_CONNECTION && status != ERROR_CODE_SUCCESS) {
                device_found = false;
                new_pair = false;
                connect_attempt_started = 0; // disarm; failed before an ACL existed
                printf("[HCI] Create connection rejected\n");
                inquiry_continue();
            }
            break;
        }

        case HCI_EVENT_COMMAND_COMPLETE: {
            const uint8_t status = hci_event_command_complete_get_return_parameters(packet)[0];
            const uint16_t opcode = hci_event_command_complete_get_command_opcode(packet);
            if (opcode != HCI_OPCODE_HCI_READ_RSSI) {
                printf("[HCI] CmdComplete %s(0x%04X) status=0x%02X\n", opcode_to_str(opcode), opcode, status);
            }
            if (opcode == HCI_OPCODE_HCI_READ_RSSI) {
                if (status != ERROR_CODE_SUCCESS || packet[1] < 7) {
                    printf("[HCI] RSSI complete failed status=0x%02X param_len=%u\n", status, packet[1]);
                }
            }
            break;
        }

        case HCI_EVENT_CONNECTION_COMPLETE: {
            const uint8_t status = hci_event_connection_complete_get_status(packet);
            if (status == 0) {
                const hci_con_handle_t handle = hci_event_connection_complete_get_connection_handle(packet);
                acl_handle = handle;
                bt_rssi = 0;
                rssi_poll.reset();
                hci_event_connection_complete_get_bd_addr(packet, current_device_addr);
                printf("[HCI] ACL connected handle=0x%04X\n", handle);
                printf("[HCI] Request authentication on handle=0x%04X\n", handle);
                // error: LOGGED-OK — printf of the step + BTstack's own event; the connection watchdog (CONNECT_WATCHDOG_TIMEOUT_US) tears a stalled attempt down and inquiry restarts, the OLED shows not connected
                hci_send_cmd(&hci_authentication_requested, handle);
            } else {
                device_found = false;
                new_pair = false;
                connect_attempt_started = 0; // disarm; no ACL was established
                printf("[HCI] ACL connect failed status=0x%02X\n", status);
                inquiry_continue();
            }
            break;
        }

        case HCI_EVENT_LINK_KEY_REQUEST: {
            bd_addr_t addr;
            hci_event_link_key_request_get_bd_addr(packet, addr);
            link_key_t link_key;
            link_key_type_t link_key_type;
            bool link = gap_get_link_key_for_bd_addr(addr, link_key, &link_key_type);
            printf("[HCI] Link key: ");
            // loop: NOT-WORTH — printf of the 16-byte key, on a link key request only
            for (int i = 0; i < sizeof(link_key_t); i++) {
                printf("%02X", link_key[i]);
            }
            printf("\n");
            if (link) {
                printf("[HCI] Link key request from %s, reply stored key type=%u\n", bd_addr_to_str(addr),
                       (unsigned int) link_key_type);
                // error: LOGGED-OK — printf of the step + BTstack's own event; the connection watchdog (CONNECT_WATCHDOG_TIMEOUT_US) tears a stalled attempt down and inquiry restarts, the OLED shows not connected
                hci_send_cmd(&hci_link_key_request_reply, addr, link_key);
            } else {
                printf("[HCI] Link key request from %s, no key, force re-pair\n", bd_addr_to_str(addr));
                // error: LOGGED-OK — printf of the step + BTstack's own event; the connection watchdog (CONNECT_WATCHDOG_TIMEOUT_US) tears a stalled attempt down and inquiry restarts, the OLED shows not connected
                hci_send_cmd(&hci_link_key_request_negative_reply, addr);
            }
            break;
        }

        case HCI_EVENT_USER_CONFIRMATION_REQUEST: {
            bd_addr_t addr;
            hci_event_user_confirmation_request_get_bd_addr(packet, addr);
            printf("[HCI] User confirmation request from %s, accept\n", bd_addr_to_str(addr));
            // error: LOGGED-OK — printf of the step + BTstack's own event; the connection watchdog (CONNECT_WATCHDOG_TIMEOUT_US) tears a stalled attempt down and inquiry restarts, the OLED shows not connected
            hci_send_cmd(&hci_user_confirmation_request_reply, addr);
            break;
        }

        case HCI_EVENT_PIN_CODE_REQUEST: {
            bd_addr_t addr;
            hci_event_pin_code_request_get_bd_addr(packet, addr);
            printf("[HCI] Legacy pin request from %s, reply 0000\n", bd_addr_to_str(addr));
            // error: LOGGED-OK — printf of the step + BTstack's own event; the connection watchdog (CONNECT_WATCHDOG_TIMEOUT_US) tears a stalled attempt down and inquiry restarts, the OLED shows not connected
            gap_pin_code_response(addr, "0000");
            break;
        }

        case HCI_EVENT_AUTHENTICATION_COMPLETE: {
            const uint8_t status = hci_event_authentication_complete_get_status(packet);
            const hci_con_handle_t handle = hci_event_authentication_complete_get_connection_handle(packet);
            printf("[HCI] Authentication complete handle=0x%04X status=0x%02X\n", handle, status);
            if (status != ERROR_CODE_SUCCESS) {
                printf("[HCI] Authentication failed, drop stored key for %s\n", bd_addr_to_str(current_device_addr));
                gap_drop_link_key_for_bd_addr(current_device_addr);
                connect_attempt_started = 0; // disarm; teardown below re-inquires
                // The controller has to be paired again (Create + PS): give it
                // a pairing window (the disconnect below starts the inquiry).
                g_scan.open(time_us_64());
                // ACL is still up — route through the clean disconnect path
                // (HCI_EVENT_DISCONNECTION_COMPLETE restarts inquiry) rather
                // than leaving a half-open ACL.
                // error: NOT-WORTH — nothing; false only when there is no ACL, i.e. already disconnected
                bt_disconnect();
            } else {
                // error: LOGGED-OK — printf of the step + BTstack's own event; the connection watchdog (CONNECT_WATCHDOG_TIMEOUT_US) tears a stalled attempt down and inquiry restarts, the OLED shows not connected
                hci_send_cmd(&hci_set_connection_encryption, handle, 1);
            }
            break;
        }

        case HCI_EVENT_ENCRYPTION_CHANGE: {
            const uint8_t status = hci_event_encryption_change_get_status(packet);
            const hci_con_handle_t handle = hci_event_encryption_change_get_connection_handle(packet);
            const uint8_t enabled = hci_event_encryption_change_get_encryption_enabled(packet);
            printf("[HCI] Encryption change handle=0x%04X status=0x%02X enabled=%u\n", handle, status, enabled);
            if (status == ERROR_CODE_SUCCESS && enabled) {
                printf("[L2CAP] Open HID channels\n");
                // A fresh pair opens control here and interrupt as soon as
                // control is open (below): waiting for a second encryption
                // change could stall the pairing until the watchdog retried.
                // Upstream 83b4df9.
                if (new_pair && hid_control_cid == 0) {
                    // error: LOGGED-OK — printf of the step + BTstack's own event; the connection watchdog (CONNECT_WATCHDOG_TIMEOUT_US) tears a stalled attempt down and inquiry restarts, the OLED shows not connected
                    l2cap_create_channel(l2cap_packet_handler, current_device_addr, PSM_HID_CONTROL, MTU_CONTROL,
                                         &hid_control_cid);
                }
            }
            break;
        }

        case HCI_EVENT_CONNECTION_REQUEST: {
            bd_addr_t addr;
            hci_event_connection_request_get_bd_addr(packet, addr);
            const uint32_t cod = hci_event_connection_request_get_class_of_device(packet);
            printf("[HCI] Incoming ACL request from %s cod=0x%06x\n", bd_addr_to_str(addr), (unsigned int) cod);
            if ((cod & 0x000F00) == 0x000500) {
                bd_addr_copy(current_device_addr, addr);
                // error: NOT-WORTH — nothing; gap_inquiry_* only fails while an inquiry is already in the wanted state or the stack is off, and INQUIRY_COMPLETE / the watchdog restart it
                gap_inquiry_stop();
                // error: LOGGED-OK — printf of the step + BTstack's own event; the connection watchdog (CONNECT_WATCHDOG_TIMEOUT_US) tears a stalled attempt down and inquiry restarts, the OLED shows not connected
                hci_send_cmd(&hci_accept_connection_request, addr, 0x01);
                connect_attempt_started = get_absolute_time(); // arm watchdog (incoming path)
            }
            break;
        }

        case HCI_EVENT_DISCONNECTION_COMPLETE: {
            // USB follows in usb_power_task(): hidden (or the wake keyboard),
            // but never while the PC sleeps (src/usb_power.h).
            usb_power_controller(false);
            gap_connectable_control(1);
            update_discoverable();
            const uint8_t reason = hci_event_disconnection_complete_get_reason(packet);
            device_found = false;
            new_pair = false;
            connect_attempt_started = 0; // disarm — every teardown clears here
            acl_handle = HCI_CON_HANDLE_INVALID;
            bt_rssi = 0;
            rssi_poll.reset();
            hid_control_cid = 0;
            hid_interrupt_cid = 0;
            feature_data.clear();
            dse_reset();
            // Packets still queued for the old link would leave every output
            // dead after a reconnect (bt_write only asks to send when the
            // queue goes from empty to one). Upstream a1492fd/697ade0.
            // loop: NOT-WORTH — drains send_fifo (≤ 10 entries) on disconnect
            while (queue_try_remove(&send_fifo, nullptr)) {}
            page_scan_fast(true);
            // The controller's last input mustn't linger (a held button or
            // stick showing on the OLED / first report). Upstream 8c8b527.
            if (bt_data_callback) bt_data_callback(INTERRUPT, const_cast<uint8_t *>(kNeutralInput), sizeof(kNeutralInput));
            state_clear_host_rumble();
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
#if ENABLE_BATT_LED
            battery_led_on_disconnect();
#endif
            printf("[HCI] Disconnected reason=0x%02X\n", reason);
            // A paired controller comes back by paging us (page scan is fast
            // again above); inquiry only inside a pairing window (a slot
            // switch's, a failed re-pair's), src/scan_policy.h.
            inquiry_continue();
            break;
        }

        case GAP_EVENT_RSSI_MEASUREMENT: {
            const hci_con_handle_t handle = gap_event_rssi_measurement_get_con_handle(packet);
            if (handle == acl_handle) {
                rssi_poll.replied();
                bt_rssi = static_cast<int8_t>(gap_event_rssi_measurement_get_rssi(packet));
            }
            break;
        }
    }
}

static void CORE0_RAM_FUNC(l2cap_packet_handler)(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    (void) channel;

    if (packet_type == L2CAP_DATA_PACKET) {
        if (channel == hid_interrupt_cid) {
            // printf("[L2CAP] HID Interrupt data len=%u\n", size);
            // printf_hexdump(packet, size);
            bt_data_callback(INTERRUPT, packet, size);

            // 静默检测. Only input reports count (packet[2] bit 0): the mic's
            // frames never look idle, so with the mic on the controller was
            // never disconnected. Upstream d7fb163.
            if (!(packet[2] & 1) || get_config().disable_inactive_disconnect) {
                return;
            }
            if (packet[3] < 120 || packet[3] > 140 ||
                packet[4] < 120 || packet[4] > 140 ||
                packet[5] < 120 || packet[5] > 140 ||
                packet[6] < 120 || packet[6] > 140 ||
                packet[7] > 0 || packet[8] > 0 ||
                packet[10] != 0x08 || packet[11] != 0x00 ||
                packet[12] != 0x00) {
                inactive_time = get_absolute_time();
            } else if (absolute_time_diff_us(inactive_time, get_absolute_time()) >
                       static_cast<int64_t>(get_config().inactive_time) * 60 * 1000 * 1000) {
                printf("disconnect when inactive\n");
                inactive_time = get_absolute_time();
                // error: NOT-WORTH — nothing; false only when there is no ACL, i.e. already disconnected
                bt_disconnect();
            }
        } else if (channel == hid_control_cid) {
            if (packet[0] == 0xA3) {
                uint8_t report_id = packet[1];
                // lifetime: OK — reuses the id's buffer (same size each reply); new ids only until disconnect clears
                feature_data[report_id].assign(packet + 1, packet + size);
#if ENABLE_VERBOSE
                printf("[L2CAP] Stored Feature Report 0x%02X, len=%u\n", report_id, size - 1);
#endif
                // The model comes from the firmware report (upstream 2efc855):
                // sw series 0x44 = Edge. It is init_feature()'s last request, so
                // 0x09/0x22/0x05 are cached before USB connects.
                if (check_dse && report_id == 0x20) {
                    check_dse = false;
                    const auto &fw = feature_data[0x20];
                    is_dse = fw_report::is_edge(fw.data(), fw.size());
                    printf("Connected %s Controller\n", is_dse ? "DSE" : "DS5");
                    // Edge: unlock and fetch its profiles for PS Accessories
                    // (dse.cpp; this replaces the old GET 0x70 at connect).
                    if (is_dse) dse_on_connect();
                    else dse_reset();
                    connect_attempt_started = 0; // fully up — disarm watchdog
                    // USB presents the DualSense from usb_power_task(); while
                    // the PC sleeps this is what may wake it (usb_power.h).
                    usb_power_controller(true);
                }
            }
#if ENABLE_VERBOSE
            printf("[L2CAP] HID Control data len=%u\n", size);
            printf_hexdump(packet, size);
#endif
            bt_data_callback(CONTROL, packet, size);
        } else {
            printf("[L2CAP] Data on unknown channel 0x%04X (Interrupt: 0x%04X, Control: 0x%04X)\n",
                   channel, hid_interrupt_cid, hid_control_cid);
        }
        return;
    }

    const uint8_t event_type = hci_event_packet_get_type(packet);
    switch (event_type) {
        case L2CAP_EVENT_CHANNEL_OPENED: {
            const uint8_t status = l2cap_event_channel_opened_get_status(packet);
            const uint16_t local_cid = l2cap_event_channel_opened_get_local_cid(packet);
            if (status == 0) {
                const uint16_t psm = l2cap_event_channel_opened_get_psm(packet);
                if (psm == PSM_HID_CONTROL) {
                    printf("[L2CAP] HID Control opened cid=0x%04X\n", local_cid);
                    hid_control_cid = local_cid;

                    // A controller already in another slot (it paged us: an
                    // incoming connection isn't filtered by slot) takes the
                    // dongle to its slot, never into a second one. Else a
                    // first-time pairing: assign it to the current slot.
                    const int own = slot_for_controller(g_current_slot, current_device_addr);
                    if (own != g_current_slot) {
                        printf("[Slots] %s is slot %d's controller, switch %d -> %d\n",
                               bd_addr_to_str(current_device_addr), own, g_current_slot, own);
                        // error: SHOWN — OLED status line "Slot: Save FAIL" on whichever screen is up (error pass F1; was SILENT-BUG: a failed save boots it in the old slot)
                        if (!make_slot_current(own)) oled_notice(oled_toast::kSlot);
                    } else if (!slot_occupied(g_current_slot)) {
                        // error: SHOWN — OLED status line "Pair: Save FAIL" on whichever screen is up (error pass F1; was SILENT-BUG: the pairing is lost at power-off)
                        if (!slot_assign(g_current_slot, current_device_addr)) oled_notice(oled_toast::kPair);
                        printf("[Slots] Assigned %s to slot %d\n",
                               bd_addr_to_str(current_device_addr), g_current_slot);
                        update_discoverable();
                    }

                    const auto mtu = l2cap_get_remote_mtu_for_local_cid(hid_control_cid);
                    printf("[L2CAP] Remote Control MTU: %d\n",mtu);

                    if (new_pair && hid_interrupt_cid == 0) {
                        printf("[L2CAP] Opening interrupt channel\n");
                        // error: LOGGED-OK — printf of the step + BTstack's own event; the connection watchdog (CONNECT_WATCHDOG_TIMEOUT_US) tears a stalled attempt down and inquiry restarts, the OLED shows not connected
                        l2cap_create_channel(l2cap_packet_handler, current_device_addr, PSM_HID_INTERRUPT,
                                             MTU_INTERRUPT, &hid_interrupt_cid);
                    }
                } else if (psm == PSM_HID_INTERRUPT) {
                    printf("[L2CAP] HID Interrupt opened cid=0x%04X\n", local_cid);
                    hid_interrupt_cid = local_cid;
                    page_scan_fast(false);
                    g_scan.close();  // connected: the pairing window is over

                    if (!get_config().disable_pico_led) {
                        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
                    }
                    inactive_time = get_absolute_time();

                    printf("Init DualSense\n");

                    init_feature();
                    // 初始化手柄状态
                    bt_send_state();
                    audio_on_bt_connected(); // re-send state + mic-stream state once USB is up

                    const auto mtu = l2cap_get_remote_mtu_for_local_cid(hid_interrupt_cid);
                    printf("[L2CAP] Remote Interrupt MTU: %d\n",mtu);

                    // OLED Edition: keep discoverable rule centralized — discoverable
                    // when any slot is empty, dark otherwise.
                    update_discoverable();
                    // tud_connect();
                } else {
                    printf("[L2CAP] Unknown Channel psm: 0x%02X", psm);
                }

                /*if (hid_control_cid != 0 && hid_interrupt_cid != 0) {
                    printf("[L2CAP] HID channels ready, request CAN_SEND_NOW for SET_PROTOCOL\n");
                    l2cap_request_can_send_now_event(hid_control_cid);
                }*/
            } else {
                const uint16_t psm = l2cap_event_channel_opened_get_psm(packet);
                hid_control_cid = 0;
                hid_interrupt_cid = 0;
                device_found = false;
                printf("[L2CAP] Open failed psm=0x%04X status=0x%02X\n", psm, status);
                // error: NOT-WORTH — nothing; false only when there is no ACL, i.e. already disconnected
                bt_disconnect();
            }
            break;
        }

        case L2CAP_EVENT_INCOMING_CONNECTION: {
            const uint16_t local_cid = l2cap_event_incoming_connection_get_local_cid(packet);
            const uint16_t psm = l2cap_event_incoming_connection_get_psm(packet);
            printf("[L2CAP] Incoming connection psm=0x%04X cid=0x%04X\n", psm, local_cid);
            l2cap_accept_connection(local_cid);
            break;
        }

        case L2CAP_EVENT_CHANNEL_CLOSED: {
            const uint16_t local_cid = l2cap_event_channel_closed_get_local_cid(packet);
            if (local_cid == hid_control_cid) {
                hid_control_cid = 0;
                printf("[L2CAP] HID Control closed cid=0x%04X\n", local_cid);
            } else if (local_cid == hid_interrupt_cid) {
                hid_interrupt_cid = 0;
                printf("[L2CAP] HID Interrupt closed cid=0x%04X\n", local_cid);
            } else {
                printf("[L2CAP] Channel closed cid=0x%04X\n", local_cid);
            }
            if (hid_control_cid == 0 && hid_interrupt_cid == 0) {
                // error: NOT-WORTH — nothing; false only when there is no ACL, i.e. already disconnected
                bt_disconnect();
            }
            break;
        }

        case L2CAP_EVENT_CAN_SEND_NOW: {
            // printf("[L2CAP] L2CAP_EVENT_CAN_SEND_NOW\n");

            static send_element send_packet{}; // 680 B: keep it off core0's small stack
            if (queue_try_remove(&send_fifo, &send_packet)) {
                // error: LOGGED-OK — printf of the L2CAP status; one output report lost, the game's next one (sent every frame) replaces it
                const uint8_t status = l2cap_send(hid_interrupt_cid, send_packet.data, send_packet.len);
                if (status != 0) {
                    printf("[L2CAP] L2CAP Send Error, Status: 0x%02X\n", status);
                }
            }
            if (!queue_is_empty(&send_fifo)) {
                // error: NOT-WORTH — nothing; asking for a CAN_SEND_NOW on an open channel only fails if the channel just closed, and DISCONNECTION clears the FIFO
                l2cap_request_can_send_now_event(hid_interrupt_cid);
            }
            break;
        }
    }
}

// Send the full controller state (lightbar, audio routing, volumes, rumble
// mode) as a 0x32 SetStateData report. Sent at L2CAP open and again once the
// host has enumerated us: the DS5 drops the first copy while it is still
// answering init_feature()'s requests (lightbar stayed on the player LED only).
void bt_send_state() {
    uint8_t report32[142]{};
    report32[0] = 0x32;
    report32[1] = 0x10; // reportSeqCounter
    report32[2] = 0x10 | 0 << 6 | 1 << 7;
    report32[3] = 0x3f; // 63 bytes
    state_set(report32 + 4,sizeof(SetStateData));
    state_stamp_host_rumble(report32 + 4);
    state_stamp_config(report32 + 4, false);  // trigger reduction, speaker pre-gain, mic choice (1.13), mic level (1.16), echo cancellation (1.17)
    bt_write(report32, sizeof(report32));
}

void CORE0_RAM_FUNC(bt_write)(const uint8_t *data, const uint16_t len) {
    // error: NOT-WORTH — nothing; no controller connected: the PC's output report has nowhere to go (the app and OLED already show no controller)
    if (hid_interrupt_cid == 0) return;
    if (len + 1u > sizeof(send_element::data)) {
        printf("[L2CAP bt_write] Error: %u-byte report exceeds send buffer\n", len);
        return;
    }
    static send_element packet{};
    // No memset (upstream 4929996): l2cap_send sends only packet.len bytes,
    // so whatever an earlier, longer report left past them never goes out.
    packet.len = len + 1;
    packet.data[0] = 0xA2;
    memcpy(packet.data + 1, data, len);
    fill_output_report_checksum(packet.data + 1, len);

    // error: LOGGED-OK — printf; the output FIFO is full, this report is dropped and the next state write (games send every frame, the app re-sends on change) replaces it
    if (!queue_try_add(&send_fifo, &packet)) {
        printf("[L2CAP bt_write] Error: Failed to add packet to send FIFO\n");
        return;
    }
    if (queue_get_level(&send_fifo) == 1) {
        // error: NOT-WORTH — nothing; asking for a CAN_SEND_NOW on an open channel only fails if the channel just closed, and DISCONNECTION clears the FIFO
        l2cap_request_can_send_now_event(hid_interrupt_cid);
    }
}

// Sends the GET for a feature report unless the cache can answer: 0x81 (test
// command results) and the Edge's profile reports are always asked again.
void bt_request_feature(uint8_t reportId) {
    // 若为0x81则会请求新内容，其他若有旧数据则不进行请求
    if (!feature_data.contains(reportId) ||
        // Get Test Command Result
        reportId == 0x81 ||
        // DSE: Set Profile Save?
        reportId == 0x63 ||
        reportId == 0x65 ||
        reportId == 0x64 ||
        // Edge profiles: answered from the cache, refreshed in the background
        // (PS Accessories reads them again after a save).
        dse_is_profile_report(reportId)
    ) {
        if (hid_control_cid != 0) {
            uint8_t get_feature[] = {0x43, reportId};
            // error: LOGGED-OK — nothing here; every caller polls for the answer with a deadline: ctrl tests end NO_ANSWER (shown by the app), the colorway read gives up (no color), the 0x09/0x20/0x05 host GETs answer from the cache
            l2cap_send(hid_control_cid, get_feature, sizeof(get_feature));
#if ENABLE_VERBOSE
            printf("[L2CAP] Requesting Get Feature Report 0x%02X\n", reportId);
#endif
        }
    }
}

// lifetime: NOT-WORTH — a ≤ 64 B copy per host GET feature report (the PC asks a few times per connect); fire-and-forget callers use bt_request_feature (lifetime pass batch 3)
vector<uint8_t> get_feature_data(uint8_t reportId, uint16_t len) {
    (void) len;
    auto it = feature_data.find(reportId);
    // lifetime: NOT-WORTH — the copy this function returns (see above)
    vector<uint8_t> ret = it != feature_data.end() ? it->second : vector<uint8_t>{};
    bt_request_feature(reportId);
    return ret;
}

uint16_t bt_feature_copy(uint8_t reportId, uint8_t *out, uint16_t cap) {
    auto it = feature_data.find(reportId);
    // error: NOT-WORTH — not a failure: 0 = the report hasn't arrived (the callers poll with a deadline)
    if (it == feature_data.end()) return 0;
    const size_t n = it->second.size() < cap ? it->second.size() : cap;
    memcpy(out, it->second.data(), n);
    return (uint16_t) n;
}

void bt_drop_feature(uint8_t reportId) {
    feature_data.erase(reportId);
}

// lifetime: NOT-WORTH — copies on every call; empty (no heap) until the report arrives; hot callers have rows
std::vector<uint8_t> bt_peek_feature(uint8_t reportId) {
    auto it = feature_data.find(reportId);
    return (it != feature_data.end()) ? it->second : std::vector<uint8_t>{};
}

void set_feature_data(uint8_t reportId, uint8_t *data, uint16_t len) {
    if (hid_control_cid != 0) {
        uint8_t get_feature[len + 2];
        get_feature[0] = 0x53;
        get_feature[1] = reportId;
        memcpy(get_feature + 2, data, len);
        fill_feature_report_checksum(get_feature + 1, len + 1);
        // error: LOGGED-OK — nothing here; every caller polls for the answer with a deadline: ctrl tests end NO_ANSWER (shown by the app), the colorway read gives up (no color), the 0x09/0x20/0x05 host GETs answer from the cache
        l2cap_send(hid_control_cid, get_feature, len + 2);
#if ENABLE_VERBOSE
        printf("[L2CAP] Requesting Set Feature Report 0x%02X\n", reportId);
        printf_hexdump(get_feature, len + 2);
#endif
    }
}

void init_feature() {
    bt_request_feature(0x09);
    bt_request_feature(0x22);
    bt_request_feature(0x05);
    // Last: its reply tells DualSense from Edge and connects USB, by when the
    // controller (which answers in order) has sent the three above.
    check_dse = true;
    bt_request_feature(0x20);
}
