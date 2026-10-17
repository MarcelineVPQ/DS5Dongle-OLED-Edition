// src/scan_policy.h: Bluetooth inquiry only in a pairing window, never forever.
#include "scan_policy.h"
#include "test.h"

namespace {
constexpr uint64_t kInquiryUs = 30 * 1'280'000ull;  // gap_inquiry_start(30)
constexpr uint64_t kS = 1'000'000ull;

// Run back-to-back inquiries from `now` the way bt.cpp does (start, end,
// again?) for `span_us`; returns how many started.
int inquiries_in(ScanPolicy &p, uint64_t now, uint64_t span_us, bool any_paired) {
    const uint64_t stop = now + span_us;
    int n = 0;
    bool running = true;  // the caller started one at `now`
    n++;
    uint64_t ends = now + kInquiryUs;
    // loop: NOT-WORTH — host test, 1 s steps over a bounded span
    for (uint64_t t = now; t < stop; t += kS) {
        if (running && t >= ends) {
            running = false;
            if (p.again(t)) { running = true; ends = t + kInquiryUs; n++; }
        }
        if (!running && p.idle_due(t, any_paired)) { running = true; ends = t + kInquiryUs; n++; }
    }
    return n;
}
}  // namespace

TEST(scan_policy_window_bounds_the_inquiry) {
    ScanPolicy p;
    p.open(0);
    // 2 min window: inquiries start at 0, 38.4, 76.8 and 115.2 s, none after.
    CHECK_EQ(inquiries_in(p, 0, 30 * 60 * kS, /*any_paired=*/true), 4);
    CHECK(!p.window);
}

// Every boot opens the full 2-min window, also with a paired slot (owner
// decision 2026-10-15; was one 38 s inquiry). With a paired slot it is
// silent: no countdown, so the Status screen stays "Press PS to connect".
// PS from the paired controller connects during it (page scan; close() on
// connect) and nothing scans after.
TEST(scan_policy_boot_with_a_paired_slot_is_a_silent_full_window) {
    ScanPolicy p;
    p.open(0, ScanPolicy::kPairWindowUs, /*shown=*/false);  // bt.cpp HCI_STATE_WORKING, paired slot
    CHECK_EQ(p.prompt_seconds_left(0), 0u);
    CHECK_EQ(p.prompt_seconds_left(60 * kS), 0u);
    CHECK_EQ(inquiries_in(p, 0, 10 * 60 * kS, true), 4);
    CHECK(!p.window);
    // Empty slot: the same window, with the countdown (unchanged).
    ScanPolicy e;
    e.open(0, ScanPolicy::kPairWindowUs, /*shown=*/true);
    CHECK_EQ(e.prompt_seconds_left(0), 120u);
    // KEY0 pair-now during a silent boot window shows the countdown.
    ScanPolicy k;
    k.open(0, ScanPolicy::kPairWindowUs, false);
    k.open(30 * kS);
    CHECK_EQ(k.prompt_seconds_left(30 * kS), 120u);
    // PS 10 s after boot: the controller connects, the window closes at once.
    ScanPolicy q;
    q.open(0, ScanPolicy::kPairWindowUs, false);
    q.close();
    CHECK_EQ(q.prompt_seconds_left(10 * kS), 0u);
    CHECK(!q.again(48 * kS));  // the running inquiry's end starts no other
    CHECK(!q.idle_due(48 * kS + 3600 * kS, true));
}

TEST(scan_policy_nothing_paired_backs_off) {
    ScanPolicy p;
    p.open(0);
    // The window's 4, then one per (38.4 s + 60 s quiet) for the rest of the hour.
    const int n = inquiries_in(p, 0, 60 * 60 * kS, /*any_paired=*/false);
    CHECK(n > 4);
    CHECK(n <= 4 + (60 * 60 - 150) / 98 + 1);
    // Never back to back: a full idle gap of quiet first.
    ScanPolicy q;
    CHECK(!q.again(500 * kS));  // an inquiry ended at 500 s outside any window
    CHECK(!q.idle_due(500 * kS + ScanPolicy::kIdleGapUs - 1, false));
    CHECK(q.idle_due(500 * kS + ScanPolicy::kIdleGapUs, false));
}

TEST(scan_policy_paired_and_idle_never_scans) {
    ScanPolicy p;
    CHECK(!p.again(10 * kS));  // e.g. the controller turned off: no window open
    CHECK(!p.idle_due(10 * kS + 24 * 3600 * kS, /*any_paired=*/true));
}

TEST(scan_policy_connect_closes_and_action_reopens) {
    ScanPolicy p;
    p.open(0);
    p.close();                 // a controller connected
    CHECK(!p.again(5 * kS));   // its later disconnect doesn't scan
    p.open(100 * kS);          // slot switch / forget / wipe / failed re-pair
    CHECK(p.again(100 * kS + 1));
    CHECK(p.again(100 * kS + ScanPolicy::kPairWindowUs - 1));
    CHECK(!p.again(100 * kS + ScanPolicy::kPairWindowUs));
}

TEST(scan_policy_open_never_shortens_a_window) {
    ScanPolicy p;
    p.open(0);                                 // ends at 120 s
    p.open(10 * kS, 30 * kS);                  // would end at 40 s
    CHECK(p.again(100 * kS));
    p.open(110 * kS);                          // extends to 230 s
    CHECK(p.again(200 * kS));
}

TEST(scan_policy_no_idle_scan_while_a_window_is_open) {
    ScanPolicy p;
    p.open(0);
    CHECK(!p.idle_due(1000 * kS, false));  // again() decides inside the window
}

// The Status screen's countdown (oled.cpp via bt_pairing_seconds_left): whole
// seconds rounded up, only for a full pairing window, gone on connect.
TEST(scan_policy_prompt_counts_down_whole_seconds) {
    ScanPolicy p;
    CHECK_EQ(p.prompt_seconds_left(0), 0u);
    p.open(10 * kS);
    CHECK_EQ(p.prompt_seconds_left(10 * kS), 120u);
    CHECK_EQ(p.prompt_seconds_left(10 * kS + 1), 120u);
    CHECK_EQ(p.prompt_seconds_left(11 * kS), 119u);
    CHECK_EQ(p.prompt_seconds_left(129 * kS + 999'999), 1u);
    CHECK_EQ(p.prompt_seconds_left(130 * kS), 0u);
    // Never more than one change per second over the whole window.
    uint32_t last = p.prompt_seconds_left(10 * kS);
    int changes = 0;
    // loop: NOT-WORTH — host test, 10 ms steps over 2 min
    for (uint64_t t = 10 * kS; t <= 131 * kS; t += 10'000) {
        const uint32_t v = p.prompt_seconds_left(t);
        if (v != last) {
            CHECK_EQ(v + 1, last);
            changes++;
            last = v;
        }
    }
    CHECK_EQ(changes, 120);
}

TEST(scan_policy_short_window_is_no_prompt_but_pair_now_is) {
    ScanPolicy p;
    p.open(0, 30 * kS);                    // a window shorter than 2 min shows no countdown
    CHECK_EQ(p.prompt_seconds_left(kS), 0u);
    p.open(5 * kS);                        // KEY0 held 2 s: pair now
    CHECK_EQ(p.prompt_seconds_left(5 * kS), 120u);
    p.close();                             // a controller connected
    CHECK_EQ(p.prompt_seconds_left(6 * kS), 0u);
    CHECK(!p.window);
}

TEST(scan_policy_pair_now_after_the_window_ran_out) {
    ScanPolicy p;
    p.open(0);
    CHECK(!p.again(121 * kS));  // the window's last inquiry ended
    CHECK_EQ(p.prompt_seconds_left(200 * kS), 0u);
    p.open(300 * kS);           // pair now, minutes later
    CHECK(p.again(301 * kS));   // inquiries run again
    CHECK_EQ(p.prompt_seconds_left(301 * kS), 119u);
    // Again while one is open: extended to 2 min from now, never shortened.
    p.open(360 * kS);
    CHECK_EQ(p.prompt_seconds_left(360 * kS), 120u);
}

// Only a rejected or missing key drops the pairing; a timeout or a dropped
// link during authentication keeps it (the controller reconnects with PS).
TEST(scan_auth_failure_drops_the_key_only_for_key_errors) {
    CHECK(auth_failure_drops_key(0x05));   // Authentication Failure
    CHECK(auth_failure_drops_key(0x06));   // PIN or Key Missing
    CHECK(!auth_failure_drops_key(0x00));  // success
    CHECK(!auth_failure_drops_key(0x08));  // Connection Timeout
    CHECK(!auth_failure_drops_key(0x13));  // Remote User Terminated Connection
    CHECK(!auth_failure_drops_key(0x16));  // Connection Terminated by Local Host
    CHECK(!auth_failure_drops_key(0x22));  // LMP Response Timeout
}
