// src/ctrl_info.cpp, controller health & info (protocol 1.12): the test
// command runs only from the main loop and only after the colorway read, pages
// are collected one GET at a time, serial numbers never reach the app, only
// the two allowed read-only commands run, and GET 0xEB stays inside its buffer.
#include <cstring>
#include <memory>
#include <vector>
#include "cmd.h"
#include "config.h"
#include "ctrl_info.h"
#include "fakes.h"
#include "test.h"

namespace {
// The simulated controller's side of test commands 0x80 / 0x81.
struct SimController {
    uint8_t dev = 0, action = 0;
    int running_first = 0;           // "running" replies before the first page
    std::vector<std::vector<uint8_t>> pages;  // 56 bytes each
    std::vector<uint8_t> statuses;   // per page
    bool clear_after = false;        // header cleared after the last page
    bool repeat_first = false;       // page 0 served twice (a re-asked GET answered again)
    size_t next = 0;
    bool answers = true;
};
SimController sim;

std::vector<uint8_t> reply(uint8_t dev, uint8_t action, uint8_t status, const std::vector<uint8_t> &data) {
    std::vector<uint8_t> r(64, 0);  // [0x81, dev, action, status, 56 bytes, crc32]
    r[0] = 0x81;
    r[1] = dev;
    r[2] = action;
    r[3] = status;
    if (!data.empty()) std::memcpy(r.data() + 4, data.data(), data.size() < 56 ? data.size() : 56);
    return r;
}

void controller_hook(uint8_t id) {
    if (id != 0x81 || !sim.answers || g_fake_feature_sets.empty()) return;
    const auto &last = g_fake_feature_sets.back();
    if (last[0] != 0x80 || last[1] != sim.dev || last[2] != sim.action) return;
    if (sim.running_first > 0) {
        sim.running_first--;
        g_fake_features[0x81] = reply(sim.dev, sim.action, 1, {});
    } else if (sim.next < sim.pages.size()) {
        g_fake_features[0x81] = reply(sim.dev, sim.action, sim.statuses[sim.next], sim.pages[sim.next]);
        if (sim.repeat_first && sim.next == 0) sim.repeat_first = false;
        else sim.next++;
    } else if (sim.clear_after) {
        g_fake_features[0x81] = reply(0, 0, 0, {});
    }
}

void tick(int n = 1) {
    for (int i = 0; i < n; i++) {
        g_fake_time_us += 10'000;
        cmd_task();
    }
}

// A controller that just connected, past the dongle's own colorway read
// (which this simulated controller never answers: ~3.5 s, then done).
void connect_fresh(bool edge) {
    g_fake_bt_connected = false;
    tick();
    g_fake_features.clear();
    g_fake_feature_sets.clear();
    g_fake_feature_get_hook = controller_hook;
    sim = SimController{};
    is_dse = edge;
    g_fake_bt_connected = true;
    tick(600);
}

uint8_t run(uint8_t dev, uint8_t action, uint8_t pages) {
    const uint8_t set[] = {0x16, dev, action, pages};
    pico_cmd_set(PICO_CMD_SET, set, sizeof(set));
    uint8_t id[47];
    pico_cmd_get(PICO_CMD_IDENTITY, id, sizeof(id));
    return id[11];
}

uint8_t state() {
    uint8_t h[14];
    CHECK_EQ(ctrl_info_get(kCtrlInfoSelHeader, h, sizeof(h)), 14);
    return h[3];
}

void wait_done() {
    for (int i = 0; i < 500 && state() == CTRL_TEST_RUNNING; i++) tick();
}

// The counters block (with its leading status byte) as the test controller holds it.
std::vector<uint8_t> block(size_t pages) {
    std::vector<uint8_t> b(pages * 56);
    for (size_t i = 0; i < b.size(); i++) b[i] = (uint8_t) (i * 7 + 3);
    std::memcpy(b.data() + 1, "SERIALSERIAL12345", 17);
    return b;
}

void serve(const std::vector<uint8_t> &b, uint8_t last_status) {
    sim.dev = kCtrlTestTelemetryDev;
    sim.action = kCtrlTestTelemetryAction;
    for (size_t p = 0; p * 56 < b.size(); p++) {
        sim.pages.emplace_back(b.begin() + p * 56, b.begin() + (p + 1) * 56);
        sim.statuses.push_back(p * 56 + 56 >= b.size() ? last_status : 3);
    }
}

std::vector<uint8_t> read_block(size_t pages) {
    std::vector<uint8_t> out;
    for (size_t p = 0; p < pages; p++) {
        uint8_t r[59];
        CHECK_EQ(ctrl_info_get(kCtrlInfoSelPage0 + p, r, sizeof(r)), 59);
        CHECK_EQ(r[0], p);
        out.insert(out.end(), r + 3, r + 59);
    }
    return out;
}

bool zeros(const std::vector<uint8_t> &b, size_t from, size_t to) {
    for (size_t i = from; i < to; i++)
        if (b[i]) return false;
    return true;
}
}  // namespace

TEST(ctrl_identity_feature_bit) {
    connect_fresh(false);
    uint8_t id[47]{};
    CHECK_EQ(pico_cmd_get(PICO_CMD_IDENTITY, id, 46), 46);  // apps before 1.12 get what they asked for
    CHECK_EQ(pico_cmd_get(PICO_CMD_IDENTITY, id, 47), 47);
    CHECK_EQ(id[5], PICO_PROTO_MINOR);
    CHECK(id[46] & 0x01);
}

TEST(ctrl_telemetry_dualsense_pages_and_serial_blanked) {
    connect_fresh(false);
    const std::vector<uint8_t> b = block(4);
    serve(b, 3);
    sim.clear_after = true;  // as the controller does: COMPLETE_2 pages, then a cleared header
    sim.running_first = 2;
    CHECK_EQ(run(0x70, 1, 6), CMD_RESULT_OK);
    // Nothing goes to the controller inside the USB request.
    CHECK(g_fake_feature_sets.empty() || g_fake_feature_sets.back()[0] != 0x80 || g_fake_feature_sets.back()[1] != 0x70);
    wait_done();
    CHECK_EQ(state(), CTRL_TEST_DONE);
    uint8_t h[14];
    ctrl_info_get(kCtrlInfoSelHeader, h, sizeof(h));
    CHECK_EQ(h[0], 'C');
    CHECK_EQ(h[1], 'H');
    CHECK_EQ(h[4], 0x70);
    CHECK_EQ(h[6], 4);
    CHECK_EQ(h[8], 0);  // DualSense
    const std::vector<uint8_t> got = read_block(6);
    CHECK(zeros(got, 1, 18));                                            // serial gone
    CHECK(std::memcmp(got.data() + 18, b.data() + 18, b.size() - 18) == 0);  // counters intact, incl. PS / mute at [183..190]
    CHECK(zeros(got, 4 * 56, 6 * 56));                                   // pages never received
}

TEST(ctrl_telemetry_edge_module_serials_blanked) {
    connect_fresh(true);
    std::vector<uint8_t> b = block(6);
    std::memcpy(b.data() + 183, "LEFTMODULESERIAL1", 17);
    std::memcpy(b.data() + 220, "RIGHTMODULESERIAL", 17);
    serve(b, 3);
    CHECK_EQ(run(0x70, 1, 6), CMD_RESULT_OK);
    wait_done();
    CHECK_EQ(state(), CTRL_TEST_DONE);
    uint8_t h[14];
    ctrl_info_get(kCtrlInfoSelHeader, h, sizeof(h));
    CHECK_EQ(h[6], 6);  // stops at the page limit
    CHECK_EQ(h[8], 1);  // Edge
    const std::vector<uint8_t> got = read_block(6);
    CHECK(zeros(got, 1, 18));
    CHECK(zeros(got, 183, 200));
    CHECK(zeros(got, 220, 237));
    CHECK(std::memcmp(got.data() + 200, b.data() + 200, 20) == 0);  // module counters between them
    CHECK_EQ(got[237], b[237]);
    is_dse = false;
}

TEST(ctrl_battery_voltage_single_reply) {
    connect_fresh(false);
    sim.dev = kCtrlTestBatteryDev;
    sim.action = kCtrlTestBatteryAction;
    sim.running_first = 3;
    sim.pages.push_back({0x1c, 0x0f});  // 3868 mV
    sim.statuses.push_back(2);
    CHECK_EQ(run(4, 3, 1), CMD_RESULT_OK);
    wait_done();
    CHECK_EQ(state(), CTRL_TEST_DONE);
    const std::vector<uint8_t> got = read_block(1);
    CHECK_EQ(got[0] | got[1] << 8, 3868);
}

TEST(ctrl_no_answer_times_out) {
    connect_fresh(false);
    sim.answers = false;
    CHECK_EQ(run(0x70, 1, 4), CMD_RESULT_OK);
    tick(200);
    CHECK_EQ(state(), CTRL_TEST_RUNNING);
    // One GET at a time: ~3 s of polling, re-asked only after 150 ms of silence.
    const int gets = g_fake_feature_gets;
    tick(200);
    CHECK_EQ(state(), CTRL_TEST_NO_ANSWER);
    CHECK(g_fake_feature_gets - gets <= 16);
}

TEST(ctrl_refuses_other_commands) {
    connect_fresh(false);
    const size_t sets = g_fake_feature_sets.size();
    CHECK_EQ(run(0x01, 0x13, 1), CMD_RESULT_INVALID);  // READ_SERIAL_NUMBER
    CHECK_EQ(run(0x01, 0x01, 1), CMD_RESULT_INVALID);
    CHECK_EQ(run(0x70, 1, 0), CMD_RESULT_INVALID);
    CHECK_EQ(run(0x70, 1, 7), CMD_RESULT_INVALID);
    CHECK_EQ(run(4, 3, 2), CMD_RESULT_INVALID);
    const uint8_t short_set[] = {0x16, 0x70, 1};
    pico_cmd_set(PICO_CMD_SET, short_set, sizeof(short_set));
    uint8_t id[47];
    pico_cmd_get(PICO_CMD_IDENTITY, id, sizeof(id));
    CHECK_EQ(id[11], CMD_RESULT_INVALID);
    tick(50);
    CHECK_EQ(g_fake_feature_sets.size(), sets);
    sim.answers = false;
    CHECK_EQ(run(0x70, 1, 4), CMD_RESULT_OK);
    CHECK_EQ(run(4, 3, 1), CMD_RESULT_BUSY);
    g_fake_bt_connected = false;
    tick();
    CHECK_EQ(state(), CTRL_TEST_NO_CONTROLLER);
    CHECK_EQ(run(4, 3, 1), CMD_RESULT_FAILED);
}

TEST(ctrl_waits_for_the_colorway_read) {
    g_fake_bt_connected = false;
    tick();
    g_fake_feature_sets.clear();
    g_fake_feature_get_hook = controller_hook;
    sim = SimController{};
    sim.answers = false;
    g_fake_bt_connected = true;
    tick(160);  // colorway read sent (1.5 s), still polling
    CHECK_EQ(run(0x70, 1, 4), CMD_RESULT_OK);
    tick(50);
    for (const auto &s : g_fake_feature_sets) CHECK(!(s[0] == 0x80 && s[1] == 0x70));
    tick(400);
    bool sent = false;
    for (const auto &s : g_fake_feature_sets) sent |= s[0] == 0x80 && s[1] == 0x70;
    CHECK(sent);
}

TEST(ctrl_disconnect_clears_results) {
    connect_fresh(false);
    serve(block(4), 2);
    CHECK_EQ(run(0x70, 1, 4), CMD_RESULT_OK);
    wait_done();
    CHECK_EQ(state(), CTRL_TEST_DONE);
    g_fake_bt_connected = false;
    tick();
    CHECK_EQ(state(), CTRL_TEST_IDLE);
    CHECK(zeros(read_block(4), 0, 4 * 56));
}

TEST(ctrl_firmware_info_from_cached_reports) {
    connect_fresh(false);
    uint8_t r[60];
    CHECK_EQ(ctrl_info_get(kCtrlInfoSelFirmware, r, sizeof(r)), 60);
    CHECK_EQ(r[0], 0);  // nothing cached yet
    std::vector<uint8_t> fw(64);
    for (size_t i = 0; i < fw.size(); i++) fw[i] = (uint8_t) (i + 0x40);
    fw[0] = 0x20;
    g_fake_features[0x20] = fw;
    std::vector<uint8_t> patch(64, 0);
    patch[0] = 0x22;
    patch[31] = 0x78;
    patch[32] = 0x56;
    patch[33] = 0x34;
    patch[34] = 0x12;
    g_fake_features[0x22] = patch;
    CHECK_EQ(ctrl_info_get(kCtrlInfoSelFirmware, r, sizeof(r)), 60);
    CHECK_EQ(r[0], 1);
    CHECK(std::memcmp(r + 1, fw.data() + 1, 31) == 0);
    for (int i = 32; i < 44; i++) CHECK_EQ(r[i], 0);  // device info withheld
    CHECK(std::memcmp(r + 44, fw.data() + 44, 16) == 0);
    uint8_t h[14];
    ctrl_info_get(kCtrlInfoSelHeader, h, sizeof(h));
    CHECK_EQ(h[9], 3);
    uint32_t v;
    std::memcpy(&v, h + 10, 4);
    CHECK_EQ(v, 0x12345678u);
    g_fake_bt_connected = false;  // a controller that left shows nothing
    CHECK_EQ(ctrl_info_get(kCtrlInfoSelFirmware, r, sizeof(r)), 60);
    CHECK_EQ(r[0], 0);
    g_fake_features.clear();
}

TEST(ctrl_every_selector_stays_in_bounds) {
    connect_fresh(false);
    for (int sel = 0; sel < 256; sel++) {
        for (uint16_t len = 0; len <= 63; len++) {
            std::unique_ptr<uint8_t[]> buf(new uint8_t[64]);
            std::memset(buf.get(), 0xA5, 64);
            const uint16_t n = ctrl_info_get((uint8_t) sel, buf.get(), len);
            CHECK(n <= len);
            bool over = false;
            for (uint16_t i = len; i < 64; i++) over |= buf[i] != 0xA5;
            CHECK(!over);
        }
    }
    g_fake_feature_get_hook = nullptr;
}

// F14: a request in the loop pass that sees the connection come up used to be
// accepted and then reported as "no controller" by the connect edge.
TEST(ctrl_start_before_the_connect_is_seen_fails) {
    g_fake_bt_connected = false;
    tick();
    g_fake_bt_connected = true;
    CHECK_EQ(run(4, 3, 1), CMD_RESULT_FAILED);
    tick();
    CHECK_EQ(state(), CTRL_TEST_IDLE);
}

// F12: a page answered twice is kept once (it would land unblanked at the next
// offset), and a late reply after the end is dropped from the cache for ~1 s.
TEST(ctrl_duplicate_page_and_late_reply_dropped) {
    connect_fresh(false);
    const std::vector<uint8_t> b = block(3);
    serve(b, 2);
    sim.repeat_first = true;
    CHECK_EQ(run(0x70, 1, 6), CMD_RESULT_OK);
    wait_done();
    CHECK_EQ(state(), CTRL_TEST_DONE);
    uint8_t h[14];
    ctrl_info_get(kCtrlInfoSelHeader, h, sizeof(h));
    CHECK_EQ(h[6], 3);
    const std::vector<uint8_t> got = read_block(3);
    CHECK(zeros(got, 1, 18));
    CHECK(std::memcmp(got.data() + 18, b.data() + 18, b.size() - 18) == 0);
    // A late answer to one of our GETs, holding the serial page.
    CHECK(ctrl_test_owns_0x81());
    g_fake_features[0x81] = reply(0x70, 1, 3, sim.pages[0]);
    tick();
    CHECK(g_fake_features.find(0x81) == g_fake_features.end());
    tick(100);
    CHECK(!ctrl_test_owns_0x81());
    g_fake_features[0x81] = reply(0x70, 1, 3, sim.pages[0]);  // past the window: the host's business
    tick();
    CHECK(g_fake_features.find(0x81) != g_fake_features.end());
    g_fake_features.erase(0x81);
}

// F13: while a test runs the 0x81 slot is the dongle's (main.cpp refuses host
// SET 0x80 / GET 0x81), and a reply that isn't the test's is left alone.
TEST(ctrl_running_owns_0x81_and_leaves_other_replies) {
    connect_fresh(false);
    CHECK(!ctrl_test_owns_0x81());
    sim.answers = false;
    CHECK_EQ(run(0x70, 1, 4), CMD_RESULT_OK);
    CHECK(ctrl_test_owns_0x81());
    tick(5);
    const std::vector<uint8_t> other = reply(0x01, 0x13, 2, {1, 2, 3});
    g_fake_features[0x81] = other;
    tick(20);
    CHECK_EQ(state(), CTRL_TEST_RUNNING);
    CHECK(g_fake_features[0x81] == other);
    tick(400);
    CHECK_EQ(state(), CTRL_TEST_NO_ANSWER);
    g_fake_features.erase(0x81);
}

// Lifetime pass batch 3: the 0x81 pollers read the cached reply into a stack
// buffer, so polling it every main-loop pass never touches the heap (they
// used to copy a std::vector each pass): a running test with a foreign reply
// cached, the scrub after it, and the colorway read's polling and scrub.
TEST(ctrl_0x81_polling_never_allocates) {
    connect_fresh(false);
    sim.answers = false;
    CHECK_EQ(run(0x70, 1, 4), CMD_RESULT_OK);
    tick(5);  // the test's SET 0x80 went out
    g_fake_features[0x81] = reply(0x01, 0x13, 2, {1, 2, 3});  // not ours: stays cached
    const int copies = g_fake_feature_copies;
    const long allocs = g_heap_allocs;
    tick(200);  // 2 s of passes while the test runs
    CHECK_EQ(g_heap_allocs, allocs);
    CHECK(g_fake_feature_copies - copies >= 200);
    CHECK_EQ(state(), CTRL_TEST_RUNNING);
    g_fake_features[0x81] = reply(0x70, 1, 0, {});  // ours, "idle": the scrub drops it
    const long allocs2 = g_heap_allocs;
    tick(200);  // the test times out, then the scrub window
    CHECK_EQ(state(), CTRL_TEST_NO_ANSWER);
    CHECK_EQ(g_heap_allocs, allocs2);
    g_fake_features.erase(0x81);
}

TEST(ctrl_colorway_read_polling_never_allocates) {
    g_fake_bt_connected = false;
    tick();
    g_fake_features.clear();
    g_fake_feature_sets.clear();
    g_fake_feature_get_hook = nullptr;
    g_fake_bt_connected = true;
    tick(160);  // 1.5 s: the colorway read's SET 0x80 goes out
    g_fake_features[0x81] = reply(0x70, 1, 2, {});  // someone else's reply: never taken
    const long allocs = g_heap_allocs;
    const int copies = g_fake_feature_copies;
    tick(350);  // polling until its 2 s deadline, then 1 s of scrub
    CHECK_EQ(g_heap_allocs, allocs);
    CHECK(g_fake_feature_copies > copies);
    g_fake_features.erase(0x81);
}
