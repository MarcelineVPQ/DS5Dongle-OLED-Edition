//
// The RSSI refresh budget (bt.cpp): gap_read_rssi() answers asynchronously,
// so bt_get_signal_strength() returns the cached value and asks for a fresh
// one, at most every 100 ms (10 Hz) and with at most one request in flight
// (a lost reply frees the slot after 1 s). Only reads trigger it: the OLED BT
// Signal screen and the app's 0xF9, never a poll during gameplay. Pure, no
// Pico SDK, for the host tests. From CLF184's PR #18.
//
// What the value means: on a Classic (BR/EDR) link HCI Read RSSI is not dBm.
// It is dB relative to the receiver's Golden Receive Power Range: 0 = inside
// it (the ideal), negative = below it (weaker), positive = above it. A board
// near its controller reads 0 steadily, and that is a real reading.
//

#ifndef DS5_BRIDGE_RSSI_POLL_H
#define DS5_BRIDGE_RSSI_POLL_H

#include <cstdint>

struct RssiPoll {
    static constexpr uint64_t kMinGapUs = 100000;    // 10 Hz
    static constexpr uint64_t kReplyTimeoutUs = 1000000;
    bool pending = false, have_request = false;
    uint64_t last_us = 0;

    // May a request go out now?
    bool ready(uint64_t now_us) {
        if (pending && now_us - last_us >= kReplyTimeoutUs) pending = false;
        return !pending && (!have_request || now_us - last_us >= kMinGapUs);
    }
    void sent(uint64_t now_us) { pending = have_request = true; last_us = now_us; }
    void replied() { pending = false; }
    void reset() { *this = {}; }
};

// The link's cached reading and its poll, as bt.cpp wires them: a reading
// exists only after a successful Read RSSI on the current link, so "no reading
// yet", "the read failed" and a real 0 (in range) stay apart.
struct RssiReading {
    RssiPoll poll;
    int8_t value = 0;    // dB from the golden range; meaningful only while valid
    bool valid = false;

    // ACL connect and disconnect: no reading for the new (or no) link.
    void reset() { *this = {}; }

    // A reader wants a fresh value. queue() is gap_read_rssi(acl_handle):
    // BTstack returns 1 when the request is queued and 0 when there is no such
    // connection (hci.c), so a request counts as sent only on a non-zero return.
    template <typename Queue>
    void refresh(uint64_t now_us, Queue queue) {
        if (poll.ready(now_us) && queue() != 0) poll.sent(now_us);
    }

    // GAP_EVENT_RSSI_MEASUREMENT for the current link (BTstack sends it only
    // for a successful command complete).
    void measured(int8_t rssi) {
        poll.replied();
        value = rssi;
        valid = true;
    }

    // HCI_Read_RSSI command complete with an error status (or too short): no
    // reading, and the request slot is free again for the next reader.
    void failed() {
        poll.replied();
        valid = false;
    }
};

// 0xF9's byte. HCI allows -128..127; -128 is kept for "no reading", so a real
// -128 (far below the range; a link that weak is about to drop) goes out as
// -127, which every app shows as Weak anyway. Older apps show -128 as a very
// weak signal too.
constexpr int8_t kRssiNoReading = -128;
inline int8_t rssi_wire(bool valid, int8_t rssi) {
    if (!valid) return kRssiNoReading;
    return rssi == kRssiNoReading ? static_cast<int8_t>(-127) : rssi;
}

#endif  // DS5_BRIDGE_RSSI_POLL_H
