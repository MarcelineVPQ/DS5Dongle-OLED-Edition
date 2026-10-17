// src/oled_vpad.h: the app's virtual controller buttons for the OLED screens
// (0xF6 0x19, protocol 1.19). Each button lands on the bit the screens read,
// holds last as long as asked (Settings Reset / Wipe need △ 2 s), queued
// presses each make an edge, a press on a dark screen waits for it to come
// on, and nothing is ever written into the controller's report (the PC's);
// plus the 0x19 framing in cmd.cpp.
#include <cstdint>
#include <cstring>
#include "cmd.h"
#include "config.h"
#include "fakes.h"
#include "oled_vpad.h"
#include "test.h"

using namespace oledvpad;

namespace {
// Runs vpad_tick() + a screen's edge detector like oled_loop (every 1 ms
// here): counts rising edges per input bit and the longest hold of each.
struct Screen {
    Injector in;
    uint32_t now = 7'000'000;
    bool connected = true;
    bool screen_on = true;
    uint8_t report[63]{0, 0, 0, 0, 0, 0, 0, 0x08};  // the controller's report as the PC gets it (hat released)
    uint8_t last7 = 0x08, last8 = 0;
    int dpad_edges[9]{};          // per hat value 0..7
    int face_edges[4]{};          // △ ○ ✕ □
    int r1_edges = 0, wakes = 0;
    uint32_t tri_held_us = 0, tri_longest_us = 0;
    void run(uint32_t us) {
        const uint32_t end = now + us;
        // loop: NOT-WORTH — host test only
        while (now < end) {
            uint8_t held = kNumButtons;
            if (tick(in, now, screen_on, held) & kWake) wakes++;
            const View v = view(connected, report[7], report[8], held);
            const uint8_t hat = v.b7 & 0x0F;
            if (hat != (last7 & 0x0F) && hat != 8) dpad_edges[hat]++;
            const uint8_t up = (uint8_t) (v.b7 & ~last7 & 0xF0);
            // loop: NOT-WORTH — host test only
            for (int i = 0; i < 4; i++) face_edges[i] += (up & (0x80 >> i)) != 0;
            r1_edges += (v.b8 & 0x02) && !(last8 & 0x02);
            if (v.b7 & 0x80) { tri_held_us += 1000; if (tri_held_us > tri_longest_us) tri_longest_us = tri_held_us; }
            else tri_held_us = 0;
            last7 = v.b7;
            last8 = v.b8;
            now += 1000;
        }
    }
};
}  // namespace

TEST(oled_vpad_each_button_lands_on_its_bit) {
    // up, down, left, right = hat 0, 4, 6, 2; △ ○ ✕ □ = 0x80 .. 0x10; R1 = byte 8 bit 1
    const int hat[4] = {0, 4, 6, 2};
    // loop: NOT-WORTH — host test only
    for (uint8_t b = 0; b < kNumButtons; b++) {
        Screen s;
        CHECK(push(s.in, b, 0));
        s.run(400'000);
        CHECK(!busy(s.in));
        int total = s.r1_edges;
        // loop: NOT-WORTH — host test only
        for (int i = 0; i < 9; i++) total += s.dpad_edges[i];
        // loop: NOT-WORTH — host test only
        for (int i = 0; i < 4; i++) total += s.face_edges[i];
        CHECK_EQ(total, 1);  // one press, one edge, nothing else
        if (b < 4) CHECK_EQ(s.dpad_edges[hat[b]], 1);
        else if (b < 8) CHECK_EQ(s.face_edges[b - 4], 1);
        else CHECK_EQ(s.r1_edges, 1);
    }
}

TEST(oled_vpad_tap_and_hold_times) {
    Screen s;
    CHECK(push(s.in, kTriangle, 0));  // a tap: 100 ms, one render frame sees it
    s.run(500'000);
    CHECK_EQ(s.tri_longest_us, kShortHoldUs);
    s.tri_longest_us = 0;
    CHECK(push(s.in, kTriangle, 2100));  // Settings Reset / Wipe: hold △ 2 s
    s.run(3'000'000);
    CHECK_EQ(s.tri_longest_us, 2'100'000u);
    CHECK_EQ(s.face_edges[0], 2);
    s.tri_longest_us = 0;
    CHECK(push(s.in, kTriangle, 30));  // shorter than a tap counts as a tap
    s.run(500'000);
    CHECK_EQ(s.tri_longest_us, kShortHoldUs);
    CHECK(!push(s.in, kTriangle, kMaxHoldMs + 1));
    CHECK(push(s.in, kTriangle, kMaxHoldMs));
    CHECK(!push(s.in, kNumButtons, 0));
}

TEST(oled_vpad_queue_each_press_is_an_edge) {
    Screen s;
    // loop: NOT-WORTH — host test only
    for (int i = 0; i < 4; i++) CHECK(push(s.in, kDown, 0));
    CHECK(!push(s.in, kDown, 0));  // full: the 0xF6 0x19 answers BUSY
    s.run(1'500'000);
    CHECK_EQ(s.dpad_edges[4], 4);  // the gap releases the hat between presses
    CHECK(!busy(s.in));
}

TEST(oled_vpad_never_touches_the_report) {
    Screen s;
    // A real controller: holding ✕ and R1, hat released.
    s.report[7] = 0x28;
    s.report[8] = 0x02;
    s.report[9] = 0x05;
    uint8_t before[63];
    std::memcpy(before, s.report, sizeof(before));
    // loop: NOT-WORTH — host test only
    for (uint8_t b = 0; b < kNumButtons; b++) {
        CHECK(push(s.in, b, 300));
        s.run(600'000);
    }
    CHECK(std::memcmp(before, s.report, sizeof(before)) == 0);  // the PC's report never changes
    // The real buttons stay down in the OLED's view while a virtual one is
    // pressed (face bits add; a virtual D-pad direction replaces the hat).
    const View v = view(true, 0x28, 0x02, kTriangle);
    CHECK_EQ(v.b7, 0xA8);
    CHECK_EQ(v.b8, 0x02);
    CHECK_EQ(view(true, 0x23, 0, kLeft).b7, 0x26);
    // No controller: its stale bytes are ignored, the virtual press still works.
    CHECK_EQ(view(false, 0x23, 0x02, kNumButtons).b7, 0x08);
    CHECK_EQ(view(false, 0x23, 0x02, kNumButtons).b8, 0);
    CHECK_EQ(view(false, 0x23, 0x02, kUp).b7, 0x00);
}

TEST(oled_vpad_dark_screen_wakes_first) {
    Screen s;
    s.screen_on = false;
    CHECK(push(s.in, kUp, 0));
    s.run(50'000);
    CHECK_EQ(s.dpad_edges[0], 0);  // not pressed while the panel is dark
    CHECK(s.wakes > 0);             // but asks the OLED to wake
    s.screen_on = true;
    s.run(400'000);
    CHECK_EQ(s.dpad_edges[0], 1);  // then does what it says
    // A screen that never comes on (a panel-less board stays "on" anyway):
    // the press still plays after kWakeWaitUs.
    s.screen_on = false;
    CHECK(push(s.in, kUp, 0));
    s.run(kWakeWaitUs + 300'000);
    CHECK_EQ(s.dpad_edges[0], 2);
}

TEST(cmd_0x19_oled_pad_press) {
    fake_flash_erase_all();
    config_load();
    g_fake_pad_inject = Injector{};
    auto result = [] {
        uint8_t id[47];
        pico_cmd_get(PICO_CMD_IDENTITY, id, sizeof(id));
        CHECK(id[46] & 0x80);  // feature bit (proto 1.19)
        CHECK(id[5] >= 19);  // 1.19 or later
        CHECK_EQ(id[10], 0x19);  // last function
        return id[11];
    };
    auto run = [] {
        // loop: NOT-WORTH — host test only
        for (int i = 0; i < 3; i++) {
            g_fake_time_us += 1000;
            cmd_task();
        }
    };
    const uint8_t bad[][4] = {{0x19, 9, 0, 0}, {0x19, 0xFF, 0, 0}, {0x19, 4, 0x89, 0x13}};  // button 9, 0xFF, 5001 ms
    // loop: NOT-WORTH — host test only
    for (const auto &b : bad) {
        pico_cmd_set(PICO_CMD_SET, b, 4);
        CHECK_EQ(result(), CMD_RESULT_INVALID);
    }
    const uint8_t no_button[] = {0x19};
    pico_cmd_set(PICO_CMD_SET, no_button, sizeof(no_button));
    CHECK_EQ(result(), CMD_RESULT_INVALID);
    CHECK(!busy(g_fake_pad_inject));

    const uint8_t tri_hold[] = {0x19, 4, 0xD0, 0x07};  // △ held 2000 ms
    pico_cmd_set(PICO_CMD_SET, tri_hold, sizeof(tri_hold));
    CHECK_EQ(result(), CMD_RESULT_PENDING);  // deferred to cmd_task, not run in the USB callback
    CHECK(!busy(g_fake_pad_inject));
    run();
    CHECK_EQ(result(), CMD_RESULT_OK);
    CHECK_EQ(g_fake_pad_inject.count, 1);
    CHECK_EQ(g_fake_pad_inject.q[g_fake_pad_inject.head].button, kTriangle);
    CHECK_EQ(g_fake_pad_inject.q[g_fake_pad_inject.head].hold_us, 2'000'000u);

    const uint8_t tap[] = {0x19, 1};  // hold bytes left out = a tap
    // loop: NOT-WORTH — host test only
    for (int i = 0; i < 3; i++) {
        pico_cmd_set(PICO_CMD_SET, tap, sizeof(tap));
        run();
        CHECK_EQ(result(), CMD_RESULT_OK);
    }
    CHECK_EQ(g_fake_pad_inject.q[(g_fake_pad_inject.head + 1) % kQueue].hold_us, kShortHoldUs);
    pico_cmd_set(PICO_CMD_SET, tap, sizeof(tap));  // fifth while four wait
    run();
    CHECK_EQ(result(), CMD_RESULT_BUSY);
    g_fake_pad_inject = Injector{};
}
