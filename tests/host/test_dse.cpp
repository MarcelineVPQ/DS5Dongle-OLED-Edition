// src/dse.cpp, DualSense Edge profiles for PS Accessories (upstream 8a2f576):
// the connect unlock (0x65 echo + SET 0x80) and the paced fetch of 0x70-0x7B
// in order and on time, profile reads refused until that fetch is done, the
// re-read after a profile save, and the controller's single 0x80/0x81 test
// slot shared with the colorway read and the app's controller tests (all
// driven through cmd_task, as in the firmware's main loop).
#include <cstring>
#include <vector>
#include "cmd.h"
#include "config.h"
#include "ctrl_info.h"
#include "dse.h"
#include "fakes.h"
#include "test.h"

namespace {
struct Ev {
    uint64_t t;
    char kind;  // 'G' GET to the controller, 'S' SET
    std::vector<uint8_t> data;  // GET: [id]; SET: [id, payload...]
};
std::vector<Ev> evs;
size_t s_sets_seen = 0;

void get_hook(uint8_t id) { evs.push_back({g_fake_time_us, 'G', {id}}); }

void tick(int n = 1) {
    for (int i = 0; i < n; i++) {
        g_fake_time_us += 10'000;
        cmd_task();
        for (; s_sets_seen < g_fake_feature_sets.size(); s_sets_seen++)
            evs.push_back({g_fake_time_us, 'S', g_fake_feature_sets[s_sets_seen]});
    }
}

void tick_ms(int ms) { tick(ms / 10); }

std::vector<uint8_t> fw_report() {
    std::vector<uint8_t> fw(64);
    for (size_t i = 0; i < fw.size(); i++) fw[i] = (uint8_t) (0x30 + i);
    fw[0] = 0x20;
    fw[22] = 0x44;  // Edge
    return fw;
}

// A clean link, then an Edge (or DualSense) whose 0x20 reply just arrived.
void connect(bool edge, bool with_fw = true) {
    g_fake_bt_connected = false;
    tick();
    g_fake_features.clear();
    g_fake_feature_sets.clear();
    g_fake_control_sends.clear();
    g_fake_feature_get_hook = get_hook;
    s_sets_seen = 0;
    evs.clear();
    if (with_fw) g_fake_features[0x20] = fw_report();
    is_dse = edge;
    g_fake_bt_connected = true;
    if (edge) dse_on_connect();
    else dse_reset();
}

bool is_unlock(const Ev &e) {
    return e.kind == 'S' && e.data.size() == 60 && e.data[0] == 0x80 && e.data[1] == 0x70 && e.data[2] == 0x01;
}
bool is_colorway(const Ev &e) { return e.kind == 'S' && e.data[0] == 0x80 && e.data[1] == 0x01 && e.data[2] == 0x13; }
bool is_test_cmd(const Ev &e) {  // ctrl_info's 0x80 [70 01]: same command, 63-byte body
    return e.kind == 'S' && e.data.size() == 64 && e.data[0] == 0x80 && e.data[1] == 0x70 && e.data[2] == 0x01;
}
bool is_get(const Ev &e, uint8_t id) { return e.kind == 'G' && e.data[0] == id; }

std::vector<Ev> select(bool (*pred)(const Ev &)) {
    std::vector<Ev> out;
    for (const Ev &e : evs)
        if (pred(e)) out.push_back(e);
    return out;
}
std::vector<Ev> profile_gets(uint64_t from = 0) {
    std::vector<Ev> out;
    for (const Ev &e : evs)
        if (e.kind == 'G' && dse_is_profile_report(e.data[0]) && e.t >= from) out.push_back(e);
    return out;
}

// GET 0x70..0x7B in order, the first at `start`, then every 80 ms.
void check_fetch(const std::vector<Ev> &g, uint64_t start) {
    CHECK_EQ(g.size(), 12u);
    for (size_t i = 0; i < g.size() && i < 12; i++) {
        CHECK_EQ(g[i].data[0], 0x70 + i);
        CHECK_EQ(g[i].t, start + i * dse::kFetchGapUs);
    }
}

void finish() {
    g_fake_bt_connected = false;
    tick();
    g_fake_feature_get_hook = nullptr;
    is_dse = false;
}
}  // namespace

TEST(dse_profile_report_range) {
    CHECK(!dse_is_profile_report(0x6F));
    CHECK(dse_is_profile_report(0x70));
    CHECK(dse_is_profile_report(0x7B));
    CHECK(!dse_is_profile_report(0x7C));
    CHECK(!dse_is_profile_report(0x80));
}

TEST(dse_connect_unlock_then_paced_fetch) {
    connect(true);
    CHECK(!dse_profiles_ready());
    CHECK(dse_owns_test_slot());
    tick();
    const uint64_t t_unlock = g_fake_time_us;
    // SET 0x65 = report 0x20 bytes 1..61, sent raw (our cache keeps the id at [0]).
    CHECK_EQ(g_fake_control_sends.size(), 1u);
    if (!g_fake_control_sends.empty()) {
        const auto &h = g_fake_control_sends[0];
        const auto fw = fw_report();
        CHECK_EQ(h.size(), 63u);
        CHECK_EQ(h[0], 0x53);
        CHECK_EQ(h[1], 0x65);
        CHECK(std::memcmp(h.data() + 2, fw.data() + 1, 61) == 0);
    }
    auto unlocks = select(is_unlock);
    CHECK_EQ(unlocks.size(), 1u);
    if (!unlocks.empty()) CHECK_EQ(unlocks[0].t, t_unlock);
    // Nothing of the profiles before the controller has had its 4 s.
    tick_ms(3990);
    CHECK(profile_gets().empty());
    CHECK(!dse_profiles_ready());
    tick_ms(2000);
    check_fetch(profile_gets(), t_unlock + dse::kUnlockWaitUs);
    // Reads open one gap after the last GET.
    CHECK(dse_profiles_ready());
    CHECK(!dse_owns_test_slot());
    CHECK_EQ(select(is_unlock).size(), 1u);  // once per connection
    finish();
}

TEST(dse_reads_refused_until_the_last_profile_reply_is_due) {
    connect(true);
    tick();
    const uint64_t last_get = g_fake_time_us + dse::kUnlockWaitUs + 11 * dse::kFetchGapUs;
    while (g_fake_time_us < last_get + dse::kFetchGapUs - 10'000) {
        tick();
        CHECK(!dse_profiles_ready());
    }
    tick();
    CHECK(dse_profiles_ready());
    finish();
}

TEST(dse_unlock_without_firmware_report_skips_only_the_echo) {
    connect(true, false);
    tick();
    CHECK(g_fake_control_sends.empty());
    CHECK_EQ(select(is_unlock).size(), 1u);
    finish();
}

TEST(dse_dualsense_untouched) {
    connect(false);
    CHECK(dse_profiles_ready());
    CHECK(!dse_owns_test_slot());
    dse_on_profile_write(0x60);  // a DualSense has no profiles
    CHECK(!dse_owns_test_slot());
    tick_ms(10000);
    CHECK(select(is_unlock).empty());
    CHECK(profile_gets().empty());
    CHECK(g_fake_control_sends.empty());
    finish();
}

TEST(dse_save_rereads_profiles) {
    connect(true);
    tick_ms(9000);  // connect fetch and the colorway read done
    CHECK(dse_profiles_ready());
    evs.clear();
    const uint64_t tw = g_fake_time_us;
    dse_on_profile_write(0x63);  // not a profile write
    CHECK(!dse_owns_test_slot());
    dse_on_profile_write(0x61);
    CHECK(dse_owns_test_slot());
    tick_ms(8000);
    const auto unlocks = select(is_unlock);
    CHECK_EQ(unlocks.size(), 1u);
    if (unlocks.empty()) return finish();
    const uint64_t t80 = unlocks[0].t;
    CHECK_EQ(t80, tw + dse::kSaveSettleUs);
    std::vector<Ev> polls;
    for (const Ev &e : evs)
        if (is_get(e, 0x81)) polls.push_back(e);
    CHECK_EQ(polls.size(), (size_t) dse::kSavePolls);
    for (size_t i = 0; i < polls.size(); i++)
        CHECK_EQ(polls[i].t, t80 + dse::kSavePollFirstUs + i * dse::kSavePollGapUs);
    check_fetch(profile_gets(), t80 + dse::kSaveRefetchUs);
    CHECK(dse_profiles_ready());  // reads were never refused around a save
    CHECK(!dse_owns_test_slot());
    finish();
}

TEST(dse_reads_stay_open_during_a_save) {
    connect(true);
    tick_ms(9000);
    dse_on_profile_write(0x60);
    for (int i = 0; i < 800; i++) {
        tick();
        CHECK(dse_profiles_ready());
    }
    finish();
}

TEST(dse_save_during_connect_runs_after_the_fetch) {
    connect(true);
    tick_ms(1000);
    dse_on_profile_write(0x62);
    tick_ms(15000);
    const auto unlocks = select(is_unlock);
    CHECK_EQ(unlocks.size(), 2u);
    const auto g = profile_gets();
    CHECK_EQ(g.size(), 24u);
    if (unlocks.size() == 2 && g.size() == 24) {
        CHECK(unlocks[1].t > g[11].t);  // after the connect fetch
        check_fetch(std::vector<Ev>(g.begin() + 12, g.end()), unlocks[1].t + dse::kSaveRefetchUs);
    }
    finish();
}

TEST(dse_save_during_refetch_starts_over) {
    connect(true);
    tick_ms(9000);
    evs.clear();
    dse_on_profile_write(0x60);
    tick_ms(5500 + 3 * 80);  // the refetch is a few reports in
    CHECK_EQ(profile_gets().size(), 4u);
    dse_on_profile_write(0x60);
    const uint64_t tw2 = g_fake_time_us;
    tick_ms(8000);
    const auto unlocks = select(is_unlock);
    CHECK_EQ(unlocks.size(), 2u);
    if (unlocks.size() == 2) {
        CHECK_EQ(unlocks[1].t, tw2 + dse::kSaveSettleUs);
        check_fetch(profile_gets(unlocks[1].t), unlocks[1].t + dse::kSaveRefetchUs);
    }
    finish();
}

TEST(dse_disconnect_mid_unlock_resets) {
    connect(true);
    tick_ms(1000);
    CHECK(!dse_profiles_ready());
    g_fake_bt_connected = false;
    tick();
    CHECK(dse_profiles_ready());
    CHECK(!dse_owns_test_slot());
    // The next controller (a DualSense: no dse_on_connect) gets none of it.
    evs.clear();
    is_dse = false;
    g_fake_bt_connected = true;
    tick_ms(10000);
    CHECK(select(is_unlock).empty());
    CHECK(profile_gets().empty());
    finish();
}

TEST(dse_disconnect_drops_a_pending_save) {
    connect(true);
    tick_ms(9000);
    dse_on_profile_write(0x60);
    tick_ms(100);
    g_fake_bt_connected = false;
    tick();
    CHECK(!dse_owns_test_slot());
    evs.clear();
    g_fake_bt_connected = true;
    tick_ms(10000);
    CHECK(select(is_unlock).empty());
    finish();
}

// The colorway read (SET 0x80 [01 13] ~1.5 s after connect) waits for the
// connect unlock and fetch: no other 0x80 while the controller builds the
// snapshot.
TEST(dse_colorway_read_waits_for_the_profile_fetch) {
    connect(true);
    tick_ms(12000);
    const auto colorway = select(is_colorway);
    const auto g = profile_gets();
    CHECK_EQ(colorway.size(), 1u);
    CHECK_EQ(g.size(), 12u);
    if (colorway.size() == 1 && g.size() == 12) CHECK(colorway[0].t >= g[11].t + dse::kFetchGapUs);
    // A DualSense keeps its 1.5 s colorway read.
    connect(false);
    tick_ms(3000);
    const auto c2 = select(is_colorway);
    CHECK_EQ(c2.size(), 1u);
    if (!c2.empty()) CHECK(c2[0].t <= 1600'000 + evs.front().t);
    finish();
}

// An app controller test asked for during a save cycle is queued until the
// refetch is done; its own 0x80 [70 01] is the only one after that.
TEST(dse_controller_test_waits_for_the_save_cycle) {
    connect(true);
    tick_ms(9000);
    evs.clear();
    dse_on_profile_write(0x60);
    tick_ms(1200);  // the save's 0x80 is out, polls running
    const uint8_t set[] = {0x16, kCtrlTestTelemetryDev, kCtrlTestTelemetryAction, 6};
    pico_cmd_set(PICO_CMD_SET, set, sizeof(set));
    uint8_t h[14];
    ctrl_info_get(kCtrlInfoSelHeader, h, sizeof(h));
    CHECK_EQ(h[3], CTRL_TEST_RUNNING);  // queued
    tick_ms(10000);
    const auto tests = select(is_test_cmd);
    const auto g = profile_gets();
    CHECK_EQ(select(is_unlock).size(), 1u);
    CHECK_EQ(tests.size(), 1u);
    CHECK_EQ(g.size(), 12u);
    if (tests.size() == 1 && g.size() == 12) CHECK(tests[0].t >= g[11].t + dse::kFetchGapUs);
    ctrl_info_get(kCtrlInfoSelHeader, h, sizeof(h));
    CHECK_EQ(h[3], CTRL_TEST_NO_ANSWER);  // this controller never answers it; the 3 s ran from its send
    finish();
}

// A save while the app's controller test holds the slot (sent, and the
// second after it for late replies): the re-unlock waits for it.
TEST(dse_save_waits_for_a_running_controller_test) {
    connect(true);
    tick_ms(9000);
    const uint8_t set[] = {0x16, kCtrlTestTelemetryDev, kCtrlTestTelemetryAction, 6};
    pico_cmd_set(PICO_CMD_SET, set, sizeof(set));
    tick();
    CHECK(ctrl_test_holds_slot());
    evs.clear();
    dse_on_profile_write(0x61);
    uint64_t freed = 0;
    for (int i = 0; i < 1000 && !select(is_unlock).size(); i++) {
        tick();
        if (!freed && !ctrl_test_holds_slot()) freed = g_fake_time_us;
    }
    const auto unlocks = select(is_unlock);
    CHECK_EQ(unlocks.size(), 1u);
    CHECK(freed != 0);
    if (!unlocks.empty() && freed) CHECK(unlocks[0].t >= freed);
    tick_ms(8000);
    check_fetch(profile_gets(), select(is_unlock)[0].t + dse::kSaveRefetchUs);
    finish();
}

// A test queued just as a save starts (before the save's own 0x80): the two
// don't wait on each other forever; the save goes first, the test after.
TEST(dse_save_and_queued_test_no_deadlock) {
    connect(true);
    tick_ms(9000);
    evs.clear();
    dse_on_profile_write(0x60);
    const uint8_t set[] = {0x16, kCtrlTestTelemetryDev, kCtrlTestTelemetryAction, 6};
    pico_cmd_set(PICO_CMD_SET, set, sizeof(set));
    tick_ms(12000);
    const auto unlocks = select(is_unlock);
    const auto tests = select(is_test_cmd);
    CHECK_EQ(unlocks.size(), 1u);
    CHECK_EQ(tests.size(), 1u);
    CHECK_EQ(profile_gets().size(), 12u);
    if (unlocks.size() == 1 && tests.size() == 1) CHECK(tests[0].t > unlocks[0].t);
    finish();
}

// Live reads (upstream 8c647e0, without its wait): 0x81 and the profiles are
// answered only with a reply that arrived after the PC's last SET 0x80 /
// 0x60-0x62, and a 0x81 reply answers one GET; other reports are untouched.
TEST(dse_live_reads_only_fresh_replies) {
    connect(false);
    CHECK(dse_is_live_report(0x81));
    CHECK(dse_is_live_report(0x70));
    CHECK(dse_is_live_report(0x7B));
    CHECK(!dse_is_live_report(0x7C));
    CHECK(!dse_is_live_report(0x20));
    CHECK(dse_live_reply_ready(0x20));   // not a live report: always the cache
    CHECK(!dse_live_reply_ready(0x81));  // nothing since the link came up
    CHECK(!dse_live_reply_ready(0x70));

    dse_on_feature_reply(0x81);
    dse_on_feature_reply(0x70);
    CHECK(dse_live_reply_ready(0x81));
    CHECK(dse_live_reply_ready(0x70));
    CHECK(!dse_live_reply_ready(0x71));
    dse_live_reply_taken(0x81);  // one GET per 0x81 reply
    dse_live_reply_taken(0x70);  // a profile stays answerable
    CHECK(!dse_live_reply_ready(0x81));
    CHECK(dse_live_reply_ready(0x70));

    dse_on_feature_reply(0x81);
    dse_on_host_set(0x61);  // a profile write: the profiles are stale, 0x81 isn't
    CHECK(dse_live_reply_ready(0x81));
    CHECK(!dse_live_reply_ready(0x70));
    dse_on_feature_reply(0x70);
    dse_on_host_set(0x80);  // a test command / unlock: both stale
    CHECK(!dse_live_reply_ready(0x81));
    CHECK(!dse_live_reply_ready(0x70));
    dse_on_host_set(0x02);  // anything else changes nothing
    dse_on_feature_reply(0x81);
    CHECK(dse_live_reply_ready(0x81));

    dse_reset();  // a new link
    CHECK(!dse_live_reply_ready(0x81));
    finish();
}
