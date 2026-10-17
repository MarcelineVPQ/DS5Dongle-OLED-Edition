// src/rssi_poll.h: the RSSI refresh budget (<= 10 Hz, one request in flight).
#include "rssi_poll.h"
#include "test.h"

TEST(rssi_poll_first_read_asks_at_once) {
    RssiPoll p;
    CHECK(p.ready(0));
    CHECK(p.ready(123456));
}

TEST(rssi_poll_one_request_in_flight) {
    RssiPoll p;
    p.sent(1'000'000);
    CHECK(!p.ready(1'000'000 + 500'000));  // no reply yet: wait, even past 100 ms
    p.replied();
    CHECK(p.ready(1'000'000 + 500'000));
}

TEST(rssi_poll_at_most_10_hz) {
    RssiPoll p;
    uint64_t now = 2'000'000;
    int sent = 0;
    for (int i = 0; i < 1000; i++, now += 1000) {  // a reader every 1 ms for 1 s
        if (p.ready(now)) { p.sent(now); sent++; }
        p.replied();                               // replies arrive at once
    }
    CHECK_EQ(sent, 10);
}

TEST(rssi_poll_lost_reply_frees_the_slot_after_1_s) {
    RssiPoll p;
    p.sent(5'000'000);
    CHECK(!p.ready(5'000'000 + 999'999));
    CHECK(p.ready(5'000'000 + 1'000'000));
}

TEST(rssi_poll_reset_on_link_change) {
    RssiPoll p;
    p.sent(7'000'000);
    p.reset();
    CHECK(p.ready(7'000'001));
}

// RssiReading: the wiring bt.cpp uses (gap_read_rssi's return, the RSSI event,
// a failed command complete, connect / disconnect).

TEST(rssi_reading_counts_a_request_only_when_btstack_queued_it) {
    RssiReading r;
    int calls = 0;
    // gap_read_rssi() returns 1 when queued (BTstack hci.c): the request is in flight.
    r.refresh(1'000'000, [&] { calls++; return 1; });
    CHECK_EQ(calls, 1);
    CHECK(r.poll.pending);
    r.refresh(1'050'000, [&] { calls++; return 1; });  // one in flight: no second request
    CHECK_EQ(calls, 1);
    // 0 = no such connection: nothing went out, so the next reader may ask at once.
    RssiReading n;
    n.refresh(2'000'000, [&] { calls++; return 0; });
    CHECK(!n.poll.pending);
    n.refresh(2'000'001, [&] { calls++; return 0; });
    CHECK_EQ(calls, 3);
}

TEST(rssi_reading_measured_is_valid_including_zero) {
    RssiReading r;
    CHECK(!r.valid);  // nothing read yet: no reading, not 0
    r.refresh(1'000'000, [] { return 1; });
    r.measured(0);    // inside the golden range: a real reading
    CHECK(r.valid);
    CHECK_EQ(r.value, 0);
    CHECK(!r.poll.pending);  // replied() on success
    CHECK(r.poll.ready(1'000'000 + RssiPoll::kMinGapUs));
    r.refresh(1'200'000, [] { return 1; });
    r.measured(-12);
    CHECK(r.valid);
    CHECK_EQ(r.value, -12);
}

TEST(rssi_reading_failed_command_clears_it_and_frees_the_slot) {
    RssiReading r;
    r.refresh(1'000'000, [] { return 1; });
    r.measured(-3);
    r.refresh(1'200'000, [] { return 1; });
    CHECK(r.poll.pending);
    r.failed();       // HCI_Read_RSSI command complete with an error status
    CHECK(!r.valid);
    CHECK(!r.poll.pending);  // replied() on failure too: no 1 s wait for the next try
    int calls = 0;
    r.refresh(1'200'000 + RssiPoll::kMinGapUs, [&] { calls++; return 1; });
    CHECK_EQ(calls, 1);
}

TEST(rssi_reading_reset_on_connect_and_disconnect) {
    RssiReading r;
    r.refresh(1'000'000, [] { return 1; });
    r.measured(-7);
    r.reset();
    CHECK(!r.valid);
    CHECK(!r.poll.pending);
    CHECK(r.poll.ready(1'000'001));
}

TEST(rssi_wire_byte) {
    CHECK_EQ(rssi_wire(false, 0), kRssiNoReading);
    CHECK_EQ(rssi_wire(false, -12), -128);
    CHECK_EQ(rssi_wire(true, 0), 0);
    CHECK_EQ(rssi_wire(true, -8), -8);
    CHECK_EQ(rssi_wire(true, 5), 5);
    CHECK_EQ(rssi_wire(true, 127), 127);
    CHECK_EQ(rssi_wire(true, -127), -127);
    CHECK_EQ(rssi_wire(true, -128), -127);  // a real -128 never reads as "no reading"
}
