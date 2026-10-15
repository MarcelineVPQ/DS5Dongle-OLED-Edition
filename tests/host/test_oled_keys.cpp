// src/oled_keys.h: KEY0 / KEY1 press logic and the app's remote presses
// (0xF6 0x18, protocol 1.18). A remote press goes through the same step() as
// the board's keys: short KEY0 = next, short KEY1 = back, long KEY1 =
// brightness, each wakes the screen; plus the 0x18 framing in cmd.cpp.
#include <cstdint>
#include "cmd.h"
#include "config.h"
#include "fakes.h"
#include "oled_keys.h"
#include "test.h"

using namespace oledkeys;

namespace {
// Runs handle_buttons() like oled_loop does (every 1 ms here) for `us`,
// with the pins released unless told otherwise; returns every event seen.
struct Board {
    Buttons b;
    Injector in;
    uint32_t now = 5'000'000;
    int next = 0, back = 0, bright = 0, reboot = 0, activity = 0, hold0 = 0;
    bool hold = false;  // the screen has a KEY0 hold action (Status, no controller)
    void run(uint32_t us, bool pin0 = false, bool pin1 = false) {
        const uint32_t end = now + us;
        // loop: NOT-WORTH — host test only
        while (now < end) {
            bool v0, v1;
            levels(in, now, v0, v1);
            const uint8_t ev = step(b, pin0 || v0, pin1 || v1, now, hold);
            next += (ev & kNext) != 0;
            back += (ev & kBack) != 0;
            bright += (ev & kBrightness) != 0;
            reboot += (ev & kReboot) != 0;
            activity += (ev & kActivity) != 0;
            hold0 += (ev & kHold0) != 0;
            now += 1000;
        }
    }
};
}  // namespace

TEST(oled_keys_physical_presses_unchanged) {
    Board k;
    k.run(100'000, true);  // KEY0 held 0.1 s
    k.run(100'000);
    CHECK_EQ(k.next, 1);
    k.run(200'000, false, true);  // KEY1 short
    k.run(100'000);
    CHECK_EQ(k.back, 1);
    k.run(kLongPressUs + 50'000, false, true);  // KEY1 long
    k.run(100'000);
    CHECK_EQ(k.bright, 1);
    CHECK_EQ(k.back, 1);
    k.run(kChordHoldUs + 10'000, true, true);  // chord: reboot, no navigation after
    CHECK(k.reboot >= 1);
    k.run(100'000);
    CHECK_EQ(k.next, 1);
    CHECK_EQ(k.back, 1);
}

TEST(oled_keys_remote_short_and_long) {
    Board k;
    CHECK(push(k.in, 0, false));
    k.run(300'000);
    CHECK_EQ(k.next, 1);
    CHECK_EQ(k.activity, 2);  // press + release: wakes the screen
    CHECK(!busy(k.in));
    CHECK(push(k.in, 1, false));
    k.run(300'000);
    CHECK_EQ(k.back, 1);
    CHECK_EQ(k.bright, 0);
    CHECK(push(k.in, 1, true));
    k.run(kInjectLongHoldUs + 200'000);
    CHECK_EQ(k.bright, 1);
    CHECK_EQ(k.back, 1);
    CHECK(push(k.in, 0, true));  // KEY0 long = next, like the board
    k.run(kInjectLongHoldUs + 200'000);
    CHECK_EQ(k.next, 2);
    CHECK_EQ(k.reboot, 0);
}

TEST(oled_keys_remote_queue_each_press_counts) {
    Board k;
    CHECK(push(k.in, 0, false));
    CHECK(push(k.in, 0, false));
    CHECK(push(k.in, 0, false));
    CHECK(push(k.in, 1, false));
    CHECK(!push(k.in, 0, false));  // full: the 0xF6 0x18 answers BUSY
    k.run(1'000'000);
    CHECK_EQ(k.next, 3);  // the gap outlasts the debounce: none swallowed
    CHECK_EQ(k.back, 1);
    CHECK(!busy(k.in));
}

TEST(oled_keys_remote_during_real_hold_adds_nothing) {
    Board k;
    CHECK(push(k.in, 0, false));
    k.run(300'000, true);  // the finger already holds KEY0: no new edge
    CHECK_EQ(k.next, 0);
    k.run(100'000);
    CHECK_EQ(k.next, 1);  // one press: the finger's
}

TEST(cmd_0x18_oled_key_press) {
    fake_flash_erase_all();
    config_load();
    g_fake_key_inject = Injector{};
    auto result = [] {
        uint8_t id[47];
        pico_cmd_get(PICO_CMD_IDENTITY, id, sizeof(id));
        CHECK(id[46] & 0x40);  // feature bit (proto 1.18)
        CHECK_EQ(id[5], 19);
        CHECK_EQ(id[10], 0x18);  // last function
        return id[11];
    };
    auto run = [] {
        // loop: NOT-WORTH — host test only
        for (int i = 0; i < 3; i++) {
            g_fake_time_us += 1000;
            cmd_task();
        }
    };
    const uint8_t bad[][3] = {{0x18, 2, 0}, {0x18, 0, 2}};
    // loop: NOT-WORTH — host test only
    for (const auto &b : bad) {
        pico_cmd_set(PICO_CMD_SET, b, 3);
        CHECK_EQ(result(), CMD_RESULT_INVALID);
    }
    const uint8_t short_cmd[] = {0x18, 0};
    pico_cmd_set(PICO_CMD_SET, short_cmd, sizeof(short_cmd));
    CHECK_EQ(result(), CMD_RESULT_INVALID);
    CHECK(!busy(g_fake_key_inject));

    const uint8_t key1_long[] = {0x18, 1, 1};
    pico_cmd_set(PICO_CMD_SET, key1_long, sizeof(key1_long));
    CHECK_EQ(result(), CMD_RESULT_PENDING);  // deferred to cmd_task, not run in the USB callback
    CHECK(!busy(g_fake_key_inject));
    run();
    CHECK_EQ(result(), CMD_RESULT_OK);
    CHECK_EQ(g_fake_key_inject.count, 1);
    CHECK_EQ(g_fake_key_inject.q[g_fake_key_inject.head], 0x03);  // KEY1, long

    const uint8_t key0[] = {0x18, 0, 0};
    // loop: NOT-WORTH — host test only
    for (int i = 0; i < 3; i++) {
        pico_cmd_set(PICO_CMD_SET, key0, sizeof(key0));
        run();
        CHECK_EQ(result(), CMD_RESULT_OK);
    }
    pico_cmd_set(PICO_CMD_SET, key0, sizeof(key0));  // fifth while four wait
    run();
    CHECK_EQ(result(), CMD_RESULT_BUSY);
    g_fake_key_inject = Injector{};
}

// "Pair now": KEY0 held 2 s on the Status screen with no controller fires once
// while still held, and its release doesn't also go to the next screen.
TEST(oled_keys_key0_hold_pairs_without_navigating) {
    Board k;
    k.hold = true;
    k.run(1'990'000, true);  // just short of 2 s: nothing yet
    CHECK_EQ(k.hold0, 0);
    k.run(20'000, true);     // 2 s reached while held
    CHECK_EQ(k.hold0, 1);
    k.run(3'000'000, true);  // keeps holding: once only
    CHECK_EQ(k.hold0, 1);
    k.run(100'000);          // release
    CHECK_EQ(k.next, 0);
    CHECK_EQ(k.back, 0);
    CHECK_EQ(k.bright, 0);
    CHECK(k.activity >= 2);  // the press woke the screen, the hold too
    // A short press there still goes to the next screen.
    k.run(100'000, true);
    k.run(100'000);
    CHECK_EQ(k.next, 1);
    CHECK_EQ(k.hold0, 1);
}

// Every other screen (and Status with a controller): a KEY0 hold of any length
// is "next screen" on release, as before.
TEST(oled_keys_key0_hold_elsewhere_still_navigates) {
    Board k;
    k.run(5'000'000, true);
    k.run(100'000);
    CHECK_EQ(k.hold0, 0);
    CHECK_EQ(k.next, 1);
}

// The app's remote long KEY0 press (1.6 s) is shorter than the hold, so it
// navigates even on the Status screen; KEY1 long is still brightness there.
TEST(oled_keys_key0_hold_not_reachable_by_remote_or_key1) {
    Board k;
    k.hold = true;
    CHECK(push(k.in, 0, true));
    k.run(2'000'000);
    CHECK_EQ(k.hold0, 0);
    CHECK_EQ(k.next, 1);
    k.run(1'700'000, false, true);  // KEY1 held 1.7 s
    k.run(100'000);
    CHECK_EQ(k.bright, 1);
    CHECK_EQ(k.hold0, 0);
}

// KEY0 + KEY1 (the reboot chord) never counts as a KEY0 hold.
TEST(oled_keys_chord_is_not_a_hold) {
    Board k;
    k.hold = true;
    k.run(500'000, true);
    k.run(2'000'000, true, true);
    CHECK(k.reboot >= 1);
    CHECK_EQ(k.hold0, 0);
}
