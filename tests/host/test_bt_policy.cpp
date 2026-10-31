// src/bt_policy.h: which incoming connections get in, when an inquiry's end
// connects, and which disconnects count as HCI errors (0xFB).
#include "bt_policy.h"
#include "scan_policy.h"
#include "test.h"

using namespace btpolicy;

TEST(bt_policy_incoming_only_known_or_in_a_window) {
    CHECK(accept_incoming(true, false));   // PS reconnect of a paired controller
    CHECK(accept_incoming(true, true));
    CHECK(accept_incoming(false, true));   // a new controller while pairing
    CHECK(!accept_incoming(false, false)); // a phone, any other time
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
