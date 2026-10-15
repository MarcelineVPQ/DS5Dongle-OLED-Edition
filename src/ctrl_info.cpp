//
// Controller health & info (protocol 1.12, identity [46] bit 0). Vendor
// interface only, everything on core 0 in the main loop (USB callbacks and
// cmd_task), so no locking.
//
// SET 0xF6 func 0x16 [dev, action, pages] queues one of Sony's read-only test
// commands (kCtrlTest*: lifetime counters 0x70/1, battery voltage 0x04/3).
// ctrl_test_task() then sends feature report 0x80 [dev, action] to the
// controller and polls 0x81 one request at a time (a second request before
// the first answer could overwrite it in the one-entry feature cache), keeping
// each reply [0x81, dev, action, status, 56 bytes, crc32] with status 2
// (complete) or 3 (complete, more pages follow) as a page; a paged block ends
// at status 2, at `pages` pages, or when the controller stops echoing
// dev/action (it clears the header after the last page). 3 s without an
// answer = NO_ANSWER (controller firmware without that command).
//
// GET 0xEB, wValue = selector << 8 | 0xEB:
//   selector 0x00, header (14 bytes):
//     [0..1] 'C' 'H'  [2] layout version 1  [3] state (CTRL_TEST_*)
//     [4] dev  [5] action of the last request  [6] pages received
//     [7] request number (bumps on every accepted 0x16)
//     [8] model: 0 DualSense, 1 Edge, 0xFF not known yet
//     [9] flags: bit0 firmware report 0x20 cached, bit1 0x22 cached
//     [10..13] u32 Bluetooth patch version (report 0x22 [31..34]), 0 if unknown
//   selector 0x01, controller firmware (60 bytes): [0] bit0 valid, then
//     [1..59] = feature report 0x20 bytes 1..59 at their own offsets (build
//     date [1..11], time [12..19], fw type, sw series, hw info, main fw,
//     update version [44..45], SBL, DSP, MCU DSP), with the 12-byte device
//     info [32..43] zeroed: the app shows versions only.
//   selector 0x10 + p, page p (59 bytes): [0] p  [1] state  [2] request
//     number  [3..58] the page's 56 bytes (zeros past the pages received).
// The counters block starts with a status byte, then the controller's serial
// number (block [1..17]) and on the Edge the two stick modules' serials
// ([183..199], [220..236]): blanked as soon as the block is complete, so the
// app never sees them (the app promises the serial is dropped).
//

#include "ctrl_info.h"

#include <cstring>
#include <vector>

#include "bt.h"
#include "cmd.h"
#include "config.h"
#include "pico/time.h"

namespace {
constexpr uint64_t kAnswerTimeoutUs = 3000 * 1000;  // whole request, from the 0x80 SET
constexpr uint64_t kReaskUs = 150 * 1000;           // a GET 0x81 that got no reply
constexpr uint64_t kPollGapUs = 10 * 1000;          // between two GET 0x81
constexpr uint64_t kScrubUs = 1000 * 1000;          // late replies dropped after the end (ColorRead::Scrub)

enum : uint8_t { kStatusComplete = 2, kStatusMore = 3, kStatusTimeout = 0xFF };

struct Request {
    uint8_t state = CTRL_TEST_IDLE;
    uint8_t dev = 0, action = 0, max_pages = 0, pages = 0, number = 0;
    bool sent = false, asking = false;
    uint64_t deadline_us = 0, asked_us = 0, next_ask_us = 0;
};
Request s_req;
uint8_t s_block[kCtrlTestMaxPages * kCtrlTestPageBytes];
bool s_was_connected = false;
uint64_t s_scrub_until_us = 0;  // a late reply of ours (it may hold the serial) is dropped until then

bool allowed(uint8_t dev, uint8_t action, uint8_t pages) {
    if (dev == kCtrlTestTelemetryDev && action == kCtrlTestTelemetryAction) return pages >= 1 && pages <= kCtrlTestMaxPages;
    if (dev == kCtrlTestBatteryDev && action == kCtrlTestBatteryAction) return pages == 1;
    return false;
}

void blank(uint16_t from, uint16_t to) {  // block bytes [from, to)
    if (from < to && to <= sizeof(s_block)) memset(s_block + from, 0, to - from);
}

bool is_ours(const uint8_t *r, uint16_t n) {
    return n >= 4 && r[0] == 0x81 && r[1] == s_req.dev && r[2] == s_req.action;
}

void finish(uint8_t state) {
    s_req.state = state;
    s_req.asking = false;
    s_scrub_until_us = time_us_64() + kScrubUs;
    if (s_req.dev == kCtrlTestTelemetryDev) {
        blank(1, 1 + 17);  // controller serial
        if (is_dse) {
            blank(1 + 182, 1 + 199);  // left stick module serial
            blank(1 + 219, 1 + 236);  // right stick module serial
        }
    }
    if (state != CTRL_TEST_DONE) {
        memset(s_block, 0, sizeof(s_block));
        s_req.pages = 0;
    }
}

// One 0x81 reply of ours. Returns true once the request is over.
bool take_reply(const uint8_t *r, uint16_t len) {
    const uint8_t status = r[3];
    if (status == kStatusTimeout) {
        finish(s_req.pages > 0 ? CTRL_TEST_DONE : CTRL_TEST_NO_ANSWER);
        return true;
    }
    if (status != kStatusComplete && status != kStatusMore) return false;  // idle / running
    size_t n = (size_t) len - 4;
    if (n > kCtrlTestPageBytes) n = kCtrlTestPageBytes;  // the rest is the Bluetooth crc32
    // A re-asked GET can bring the last page again: it would land unblanked
    // at the next page's offset (page 0 holds the serial).
    if (s_req.pages > 0 && memcmp(s_block + (size_t) (s_req.pages - 1) * kCtrlTestPageBytes, r + 4, n) == 0) return false;
    memcpy(s_block + (size_t) s_req.pages * kCtrlTestPageBytes, r + 4, n);
    s_req.pages++;
    if (status == kStatusComplete || s_req.pages >= s_req.max_pages) {
        finish(CTRL_TEST_DONE);
        return true;
    }
    return false;
}
}  // namespace

uint8_t ctrl_test_start(const uint8_t *args, uint16_t len) {
    // error: SHOWN — CMD_RESULT_INVALID in the 0xF1 result byte: a test the dongle doesn't allow, the app's command error
    if (len < 3 || !allowed(args[0], args[1], args[2])) return CMD_RESULT_INVALID;
    // Not before ctrl_test_task has seen the connection: its connect edge
    // would turn this request into NO_CONTROLLER in the same loop pass.
    // error: SHOWN — CMD_RESULT_FAILED in the 0xF1 result byte: no controller, the app's command error
    if (!bt_is_connected() || !s_was_connected) return CMD_RESULT_FAILED;
    // error: SHOWN — CMD_RESULT_BUSY in the 0xF1 result byte: a test is running, the app's command error
    if (s_req.state == CTRL_TEST_RUNNING) return CMD_RESULT_BUSY;
    const uint8_t number = (uint8_t) (s_req.number + 1);
    s_req = Request{};
    s_req.state = CTRL_TEST_RUNNING;
    s_req.dev = args[0];
    s_req.action = args[1];
    s_req.max_pages = args[2];
    s_req.number = number;
    memset(s_block, 0, sizeof(s_block));
    s_scrub_until_us = 0;
    return CMD_RESULT_OK;
}

bool ctrl_test_owns_0x81() {
    return s_req.state == CTRL_TEST_RUNNING || (s_scrub_until_us && time_us_64() < s_scrub_until_us);
}

bool ctrl_test_holds_slot() {
    return (s_req.state == CTRL_TEST_RUNNING && s_req.sent) || (s_scrub_until_us && time_us_64() < s_scrub_until_us);
}

void ctrl_test_task(bool may_send) {
    const bool connected = bt_is_connected();
    if (connected != s_was_connected) {
        s_was_connected = connected;
        // Another controller may connect next: nothing of this one's stays.
        const uint8_t number = s_req.number;
        const bool was_running = s_req.state == CTRL_TEST_RUNNING;
        s_req = Request{};
        s_req.number = number;
        s_req.state = was_running ? CTRL_TEST_NO_CONTROLLER : CTRL_TEST_IDLE;
        memset(s_block, 0, sizeof(s_block));
        s_scrub_until_us = 0;
        return;
    }
    const uint64_t now = time_us_64();
    if (s_req.state != CTRL_TEST_RUNNING) {
        if (s_scrub_until_us && now >= s_scrub_until_us) s_scrub_until_us = 0;
        if (s_scrub_until_us) {
            uint8_t h[4];  // a stack copy of the header, no heap (lifetime pass batch 3)
            if (is_ours(h, bt_feature_copy(0x81, h, sizeof(h)))) bt_drop_feature(0x81);
        }
        return;
    }
    if (!s_req.sent) {
        // error: NOT-WORTH — nothing; scheduling (test slot busy / reply taken); the request's deadline ends it as NO_ANSWER, which GET 0xEB reports and the app shows
        if (!may_send) return;
        uint8_t cmd[63] = {s_req.dev, s_req.action};  // the crc32 is added by set_feature_data
        bt_drop_feature(0x81);
        set_feature_data(0x80, cmd, sizeof(cmd));
        s_req.sent = true;
        s_req.deadline_us = now + kAnswerTimeoutUs;
        s_req.next_ask_us = now + 2 * kPollGapUs;
        return;
    }
    uint8_t r[kBtFeatureMax];  // a stack copy, no heap (lifetime pass batch 3)
    const uint16_t rn = bt_feature_copy(0x81, r, sizeof(r));
    if (is_ours(r, rn)) {
        bt_drop_feature(0x81);  // each reply is one page: never read it twice
        s_req.asking = false;
        s_req.next_ask_us = now + kPollGapUs;
        // error: NOT-WORTH — nothing; scheduling (test slot busy / reply taken); the request's deadline ends it as NO_ANSWER, which GET 0xEB reports and the app shows
        if (take_reply(r, rn)) return;
    } else if (rn != 0 && s_req.pages > 0) {  // header cleared after the last page
        finish(CTRL_TEST_DONE);
        return;
    }  // anything else isn't ours to drop: the next GET replaces it
    if (now > s_req.deadline_us) {
        finish(s_req.pages > 0 ? CTRL_TEST_DONE : CTRL_TEST_NO_ANSWER);
        return;
    }
    if (s_req.asking && now - s_req.asked_us > kReaskUs) s_req.asking = false;  // that GET got lost
    if (!s_req.asking && now >= s_req.next_ask_us) {
        bt_request_feature(0x81);  // the answer lands in the feature cache
        s_req.asking = true;
        s_req.asked_us = now;
    }
}

uint16_t ctrl_info_get(uint8_t selector, uint8_t *buffer, uint16_t reqlen) {
    if (selector == kCtrlInfoSelHeader) {
        constexpr uint16_t kLen = 14;
        if (reqlen < kLen) return 0;
        memset(buffer, 0, kLen);
        // lifetime: NOT-WORTH — per app GET 0xEB (the Controller info card, a few per second at most)
        const std::vector<uint8_t> fw = bt_is_connected() ? bt_peek_feature(0x20) : std::vector<uint8_t>{};
        // lifetime: NOT-WORTH — per app GET 0xEB (the Controller info card, a few per second at most)
        const std::vector<uint8_t> patch = bt_is_connected() ? bt_peek_feature(0x22) : std::vector<uint8_t>{};
        buffer[0] = 'C';
        buffer[1] = 'H';
        buffer[2] = 1;
        buffer[3] = s_req.state;
        buffer[4] = s_req.dev;
        buffer[5] = s_req.action;
        buffer[6] = s_req.pages;
        buffer[7] = s_req.number;
        buffer[8] = cmd_controller_model();
        buffer[9] = (fw.size() >= 60 ? 0x01 : 0) | (patch.size() >= 35 ? 0x02 : 0);
        if (patch.size() >= 35) memcpy(buffer + 10, patch.data() + 31, 4);
        return kLen;
    }
    if (selector == kCtrlInfoSelFirmware) {
        constexpr uint16_t kLen = 60;
        if (reqlen < kLen) return 0;
        memset(buffer, 0, kLen);
        // lifetime: NOT-WORTH — per app GET 0xEB (the Controller info card, a few per second at most)
        const std::vector<uint8_t> fw = bt_is_connected() ? bt_peek_feature(0x20) : std::vector<uint8_t>{};
        if (fw.size() >= kLen && fw[0] == 0x20) {
            buffer[0] = 0x01;
            memcpy(buffer + 1, fw.data() + 1, kLen - 1);
            memset(buffer + 32, 0, 12);  // device info: not shown, not passed on
        }
        return kLen;
    }
    if (selector >= kCtrlInfoSelPage0 && selector < kCtrlInfoSelPage0 + kCtrlTestMaxPages) {
        constexpr uint16_t kLen = 3 + kCtrlTestPageBytes;
        if (reqlen < kLen) return 0;
        const uint8_t p = selector - kCtrlInfoSelPage0;
        buffer[0] = p;
        buffer[1] = s_req.state;
        buffer[2] = s_req.number;
        // Only a finished block: its serials are blanked by then.
        if (s_req.state == CTRL_TEST_DONE && p < s_req.pages) memcpy(buffer + 3, s_block + (size_t) p * kCtrlTestPageBytes, kCtrlTestPageBytes);
        else memset(buffer + 3, 0, kCtrlTestPageBytes);
        return kLen;
    }
    return 0;
}
