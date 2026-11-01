//
// Bluetooth connection decisions taken in bt.cpp's HCI event handler, kept
// here as pure functions so the host tests can check them (no Pico SDK, no
// BTstack). The inquiry policy itself is src/scan_policy.h.
//

#ifndef DS5_BRIDGE_BT_POLICY_H
#define DS5_BRIDGE_BT_POLICY_H

#include <cstdint>

namespace btpolicy {

// An incoming Bluetooth connection (another device paging the board). A
// controller in a pairing slot (`slot_owner` >= 0, slot_owner_of()) always
// gets in: that is the PS-button reconnect. Anything else only while a
// pairing window is open (src/scan_policy.h). Before, every device was
// accepted and the board was discoverable whenever a slot was empty, so a
// phone could pair with it, push a controller's key out of the 4-key store
// and hold the only connection (review fw-usb-bt S3). A stored link key alone
// doesn't count: a phone paired by older firmware still has one (re-review
// B-S1); bt.cpp drops such keys at start-up (keep_link_key).
constexpr bool accept_incoming(int slot_owner, bool pairing_window_open) {
    return slot_owner >= 0 || pairing_window_open;
}

// At start-up, a stored link key stays only when a pairing slot holds its
// address. Every controller the board pairs is put in a slot when it
// connects; a key without one is a phone or other device older firmware let
// pair, or a pairing whose slot save failed ("Pair: Save FAIL", which needs
// a new pairing anyway). Left in place it takes one of the 4 key places.
constexpr bool keep_link_key(int slot_owner) {
    return slot_owner >= 0;
}

// A controller in pairing mode an inquiry found (gamepad class of device):
// pair it into the current slot? Not when another slot owns it (`owner`, -1 =
// none); when the current slot already holds a controller, only that one.
// Unowned controllers pair into the current slot when it is empty.
constexpr bool inquiry_result_pairs(int owner, int current_slot, bool current_occupied, bool is_current_slots_addr) {
    // error: NOT-WORTH — a yes/no decision, not a failure; bt.cpp prints the skip
    if (owner >= 0 && owner != current_slot) return false;
    return !current_occupied || is_current_slots_addr;
}

// An inquiry ended (BTstack reports GAP_EVENT_INQUIRY_COMPLETE for every end
// and HCI_EVENT_INQUIRY_COMPLETE after a natural one): connect to the
// controller it found, unless a connection attempt is already on its way.
// Without that check a natural end racing the cancel sent both events with a
// controller found, so two create-connection commands went out; the second
// one's failure tore the first attempt's state down (review fw-usb-bt N2).
constexpr bool inquiry_end_connects(bool device_found, bool attempt_in_flight) {
    return device_found && !attempt_in_flight;
}

// After a natural end (HCI_EVENT_INQUIRY_COMPLETE) with no connect: scan on
// in the pairing window, but not while a connection attempt is on its way
// (the GAP event before it may just have sent the create): a new inquiry then
// ran during the page, slowing it or failing (re-review B-N1).
constexpr bool inquiry_end_scans_on(bool attempt_in_flight) {
    return !attempt_in_flight;
}

// A disconnect reason that is a fault (counted in 0xFB "HCI errors"). The
// normal ends are not: Remote User Terminated (0x13, e.g. the controller
// switched off with PS held), Remote Power Off (0x15) and Local Host
// Terminated (0x16, the board's own idle / sleep / slot disconnect).
constexpr bool disconnect_is_error(uint8_t reason) {
    return reason != 0x13 && reason != 0x15 && reason != 0x16;
}

}  // namespace btpolicy

#endif  // DS5_BRIDGE_BT_POLICY_H
