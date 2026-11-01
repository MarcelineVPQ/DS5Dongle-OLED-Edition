// src/bt_policy.h: which incoming connections get in, when an inquiry's end
// connects, and which disconnects count as HCI errors (0xFB).
#include "bt_policy.h"
#include "scan_policy.h"
#include "test.h"

using namespace btpolicy;

TEST(bt_policy_incoming_only_known_or_in_a_window) {
    CHECK(accept_incoming(0, false));      // PS reconnect of a paired controller
    CHECK(accept_incoming(3, false));      // another slot's controller (it moves the board to its slot)
    CHECK(accept_incoming(1, true));
    CHECK(accept_incoming(-1, true));      // a new controller while pairing
    CHECK(!accept_incoming(-1, false));    // a phone, any other time; also one with a
                                           // link key older firmware stored (no slot)
}

// B-S1: at start-up only keys a slot owns stay.
TEST(bt_policy_start_up_keeps_only_slot_owned_keys) {
    CHECK(keep_link_key(0));
    CHECK(keep_link_key(3));
    CHECK(!keep_link_key(-1));
}

TEST(bt_policy_pairing_window_open_now) {
    ScanPolicy p;
    CHECK(!p.open_now(0));
    p.open(1000);
    CHECK(p.open_now(1000));
    CHECK(p.open_now(1000 + ScanPolicy::kPairWindowUs - 1));
    CHECK(!p.open_now(1000 + ScanPolicy::kPairWindowUs));
    p.close();
    CHECK(!p.open_now(2000));
}

// N2: GAP_EVENT_INQUIRY_COMPLETE then HCI_EVENT_INQUIRY_COMPLETE, both with
// a controller found: only the first sends create-connection.
TEST(bt_policy_inquiry_end_connects_once) {
    bool attempt = false;
    int creates = 0;
    // loop: NOT-WORTH — host test, the two end events
    for (int ev = 0; ev < 2; ev++) {
        if (inquiry_end_connects(true, attempt)) {
            creates++;
            attempt = true;  // bt.cpp arms connect_attempt_started with the create
        }
    }
    CHECK_EQ(creates, 1);
    CHECK(!inquiry_end_connects(false, false));
    // B-N1: the HCI end that follows doesn't start a new inquiry during the
    // page the GAP end just sent; with nothing in flight it scans on.
    CHECK(!inquiry_end_scans_on(attempt));
    CHECK(inquiry_end_scans_on(false));
}

TEST(bt_policy_disconnect_reasons) {
    CHECK(!disconnect_is_error(0x13));  // remote user terminated (PS held)
    CHECK(!disconnect_is_error(0x15));  // remote power off
    CHECK(!disconnect_is_error(0x16));  // the board's own disconnect
    CHECK(disconnect_is_error(0x08));   // connection timeout (link lost)
    CHECK(disconnect_is_error(0x05));   // authentication failure
    CHECK(disconnect_is_error(0x22));   // LMP response timeout
}

// Slot routing of an inquiry result (bt.cpp HCI_EVENT_INQUIRY_RESULT).
TEST(bt_policy_inquiry_result_slot_routing) {
    CHECK(inquiry_result_pairs(-1, 0, false, false));  // new controller, empty slot
    CHECK(!inquiry_result_pairs(-1, 0, true, false));  // new controller, slot taken
    CHECK(inquiry_result_pairs(0, 0, true, true));     // the slot's own controller
    CHECK(!inquiry_result_pairs(2, 0, false, false));  // another slot's controller
    CHECK(!inquiry_result_pairs(2, 0, true, false));
}

// Upstream 83fcb19: only names starting "DualSense" (the DualSense and the
// Edge) pair; another gamepad's name, a short or missing name don't.
TEST(bt_policy_inquiry_name_is_dualsense) {
    auto name = [](const char *s) { return inquiry_name_is_dualsense(reinterpret_cast<const uint8_t *>(s), static_cast<uint8_t>(std::strlen(s))); };
    CHECK(name("DualSense Wireless Controller"));
    CHECK(name("DualSense Edge Wireless Controller"));
    CHECK(name("DualSense"));
    CHECK(!name("Wireless Controller"));  // a DualShock 4
    CHECK(!name("Xbox Wireless Controller"));
    CHECK(!name("DualSens"));
    CHECK(!name("dualsense"));
    CHECK(!name(""));
    CHECK(!inquiry_name_is_dualsense(nullptr, 0));
    const uint8_t not_terminated[9] = {'D', 'u', 'a', 'l', 'S', 'e', 'n', 's', 'e'};
    CHECK(inquiry_name_is_dualsense(not_terminated, sizeof(not_terminated)));
}

// Upstream 2e79733 + 3fa663e: one disconnect per dock event, only when on.
TEST(bt_policy_dock_disconnect_once_per_dock) {
    DockDisconnect d;
    CHECK(!d.due(false, true));
    CHECK(!d.due(true, false));  // off: nothing
    CHECK(d.due(true, true));    // docked with the setting on
    CHECK(d.due(true, true));    // the disconnect didn't go out: try again
    d.sent();
    CHECK(!d.due(true, true));   // still docked (or switched on again on the dock): stays
    CHECK(!d.due(false, true));  // lifted
    CHECK(d.due(true, true));    // docked again
}
