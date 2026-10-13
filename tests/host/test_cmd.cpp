// src/cmd.cpp, the configurator protocol: every GET id at every request length
// stays inside the caller's buffer (ASan), random SETs never crash, and the
// protocol 1.11 additions (0xFC timings, 0xF6 0x15) have the documented layout.
#include <cstring>
#include <memory>
#include <random>
#include "cmd.h"
#include "config.h"
#include "c1_diag.h"
#include "calib.h"
#include "fakes.h"
#include "perf.h"
#include "remap.h"
#include "test.h"

namespace {
void fresh() {
    fake_flash_erase_all();
    g_fake_flash_fail = false;
    g_fake_bt_connected = true;
    config_load();
    remap_load();
    calib_load();
}
uint8_t last_result() {
    uint8_t id[46];
    pico_cmd_get(PICO_CMD_IDENTITY, id, sizeof(id));
    return id[11];
}
void settle() {  // let a deferred command run (reboot/BOOTSEL wait 50 ms)
    for (int i = 0; i < 3; i++) {
        g_fake_time_us += 100'000;
        cmd_task();
    }
}
}  // namespace

TEST(cmd_identity_reports_protocol_1_12) {
    fresh();
    uint8_t id[46]{};
    CHECK_EQ(pico_cmd_get(PICO_CMD_IDENTITY, id, sizeof(id)), 46);
    CHECK(std::memcmp(id, "DS5O", 4) == 0);
    CHECK_EQ(id[4], 1);
    CHECK_EQ(id[5], 12);
    CHECK_EQ(id[6], sizeof(Config_body));
    CHECK(id[29] & 0x80);  // timings in 0xFC
    CHECK(std::memcmp(id + 30, "Host", 5) == 0);
}

TEST(cmd_every_get_stays_in_bounds) {
    fresh();
    // Both transports hand pico_cmd_get a 64-byte buffer (vendor_itf.cpp s_buf,
    // TinyUSB's control buffer) with reqlen = wLength capped to 63: ASan guards
    // the 64 bytes, and a canary shows bytes written past reqlen. Writing there
    // is harmless with those buffers but is a bug waiting for a smaller one;
    // the ids known to do it are listed, a new one fails.
    constexpr uint16_t kBuf = 64;
    const uint8_t known_overreach[] = {0xFD};  // writes [0..15] before checking reqlen
    for (int rid = 0; rid < 256; rid++) {
        bool overreach = false;
        for (uint16_t len = 0; len <= 63; len++) {
            std::unique_ptr<uint8_t[]> buf(new uint8_t[kBuf]);
            std::memset(buf.get(), 0xA5, kBuf);
            const uint16_t n = pico_cmd_get((uint8_t) rid, buf.get(), len);
            if (n > len) {
                std::fprintf(stderr, "  GET 0x%02X with %u bytes answered %u\n", rid, len, n);
                CHECK(n <= len);
            }
            for (uint16_t i = len; i < kBuf; i++) overreach |= buf[i] != 0xA5;
        }
        bool known = false;
        for (uint8_t k : known_overreach) known |= k == rid;
        if (overreach != known) std::fprintf(stderr, "  GET 0x%02X writes past reqlen: %d (known %d)\n", rid, overreach, known);
        CHECK(overreach == known);
    }
}

TEST(cmd_fc_timings_layout_and_reset) {
    fresh();
    uint8_t r[63]{};
    CHECK_EQ(pico_cmd_get(0xFC, r, 11), 11);  // what apps before 1.11 ask for
    perf_reset();
    perf_record(PERF_TUD, 40);
    CHECK_EQ(pico_cmd_get(0xFC, r, 63), 63);
    uint32_t khz;
    std::memcpy(&khz, r, 4);
    CHECK_EQ(khz, 320000u);
    CHECK_EQ(r[11], 1);
    CHECK_EQ(r[12], 8);
    uint32_t avg;
    uint16_t mx;
    std::memcpy(&avg, r + 15 + 6 * PERF_TUD, 4);
    std::memcpy(&mx, r + 19 + 6 * PERF_TUD, 2);
    CHECK_EQ(avg, 40000u);
    CHECK_EQ(mx, 40);
    const uint8_t reset[] = {0x15};
    pico_cmd_set(PICO_CMD_SET, reset, sizeof(reset));
    CHECK_EQ(last_result(), CMD_RESULT_OK);
    pico_cmd_get(0xFC, r, 63);
    std::memcpy(&avg, r + 15 + 6 * PERF_TUD, 4);
    CHECK_EQ(avg, 0u);
}

TEST(cmd_no_reply_needs_more_than_63_bytes) {
    // Both transports cap a reply at 63 bytes: a reply that wants more loses
    // its tail without any error (0xF5 lost core1's health that way). Ask
    // every id with room to spare and check nothing is cut off. The ids known
    // to do it are listed (0xFE, the mic-debug frame dump, wants 82: its last
    // 19 frame bytes never arrive); a new one fails.
    fresh();
    const uint8_t known_long[] = {0xFE};
    for (int rid = 0; rid < 256; rid++) {
        std::unique_ptr<uint8_t[]> buf(new uint8_t[1024]);
        const uint16_t n = pico_cmd_get((uint8_t) rid, buf.get(), 1024);
        bool known = false;
        for (uint8_t k : known_long) known |= k == rid;
        if ((n > 63) != known) std::fprintf(stderr, "  GET 0x%02X wants %u bytes (known %d)\n", rid, n, known);
        CHECK((n > 63) == known);
    }
}

TEST(cmd_f5_layout) {
    fresh();
    for (uint16_t len = 0; len <= 200; len++) {  // every length: min(len, 63)
        std::unique_ptr<uint8_t[]> buf(new uint8_t[200]);
        CHECK_EQ(pico_cmd_get(0xF5, buf.get(), len), len < 63 ? len : 63);
    }
    uint8_t r[63]{};
    auto u32 = [&](int i) {
        uint32_t x;
        std::memcpy(&x, r + 4 * i, 4);
        return x;
    };
    auto heap_kib = [&] { return (uint16_t) (r[61] | r[62] << 8); };
    g_fake_c1_flags = 0x01;
    g_fake_c1_free_heap = 100000;
    g_fake_c1_stage = 0;
    CHECK_EQ(pico_cmd_get(0xF5, r, 63), 63);
    // The 15 counters keep their offsets (fakes.cpp values).
    CHECK_EQ(u32(0), 8u);   // USB frames
    CHECK_EQ(u32(1), 9u);   // BT out
    CHECK_EQ(u32(2), 1u);   // drop raw
    CHECK_EQ(u32(5), 4u);   // core1 loops
    CHECK_EQ(u32(10), 11u); // mic PLC
    CHECK_EQ(r[60], 0x81);  // started + this layout
    CHECK_EQ(heap_kib(), 97);
    g_fake_c1_flags = 0x01 | 0x02 | 0x04 | 0x10;  // unknown bits stay out
    g_fake_c1_free_heap = 12 * 1024 + 1023;
    pico_cmd_get(0xF5, r, 63);
    CHECK_EQ(r[60], 0x87);
    CHECK_EQ(heap_kib(), 12);
    g_fake_c1_free_heap = 500;  // under 1 KiB still reads as known
    pico_cmd_get(0xF5, r, 63);
    CHECK_EQ(heap_kib(), 1);
    g_fake_c1_free_heap = 0xFFFFFFFFu;
    pico_cmd_get(0xF5, r, 63);
    CHECK_EQ(heap_kib(), 0xFFFF);
    g_fake_c1_flags = 0;  // before core1 starts
    g_fake_c1_free_heap = 0;
    pico_cmd_get(0xF5, r, 63);
    CHECK_EQ(r[60], 0x80);
    CHECK_EQ(heap_kib(), 0);
    // Core1's start-up stage in bits 3..6, beside the flags (issue #15).
    g_fake_c1_flags = 0x01 | 0x02;
    g_fake_c1_free_heap = 100000;
    for (uint32_t stage = 0; stage <= 9; stage++) {
        g_fake_c1_stage = stage;
        pico_cmd_get(0xF5, r, 63);
        CHECK_EQ(r[60], 0x80 | stage << 3 | 0x03);
        CHECK_EQ((r[60] >> 3) & 0x0F, stage);
        CHECK_EQ(r[60] & 0x07, 0x03);  // the flags old apps read are unchanged
        CHECK_EQ(heap_kib(), 97);
        CHECK_EQ(u32(5), 4u);  // and the counters
    }
    g_fake_c1_stage = 9;
    g_fake_c1_flags = 0x01;
    pico_cmd_get(0xF5, r, 63);
    CHECK_EQ(r[60], 0xC9);  // running: started, stage 9, this layout
    g_fake_c1_stage = 0x1F;  // never more than 4 bits: bit7 stays the marker
    g_fake_c1_flags = 0;
    pico_cmd_get(0xF5, r, 63);
    CHECK_EQ(r[60], 0xF8);
    g_fake_c1_flags = 1;
    g_fake_c1_free_heap = 100000;
    g_fake_c1_stage = 9;
}

TEST(cmd_ea_c1diag_layout) {
    // Issue #15 core1 diagnostics: 63 bytes at most on every length, read-only.
    fresh();
    for (uint16_t len = 0; len <= 200; len++) {
        std::unique_ptr<uint8_t[]> buf(new uint8_t[200]);
        CHECK_EQ(pico_cmd_get(PICO_CMD_C1_DIAG, buf.get(), len), len < 63 ? len : 63);
    }
    CHECK(is_pico_cmd(PICO_CMD_C1_DIAG));  // the HID feature path too
    C1DiagSnapshot s;
    s.flags = C1DIAG_FAULTED | C1DIAG_RECORDER | C1DIAG_STACK_GUARD | C1DIAG_RP2350;
    s.stage_now = 15;
    s.stage_at_fault = 12;
    s.chip_version = 2;
    s.rom_version = 3;
    s.pc = 0x10001234u;
    s.lr = 0x20001235u;
    s.xpsr = 0x61000000u;
    s.cfsr = 0x00008200u;
    s.hfsr = 0x40000000u;
    s.mmfar = 0xE000EDF8u;
    s.bfar = 0x10ABCDEFu;
    s.sp = 0x20040F00u;
    s.fault_us = 123456789u;
    s.heartbeat_age_us = 0xFFFFFFFFu;
    s.stack_free_bytes = 70000;  // saturates at 65535
    s.qmi_timing = 0x60007203u;
    s.qmi_rfmt = 0x00009154u;
    s.qmi_rcmd = 0x000000BBu;
    s.exc_return = 0xF9;
    s.sfsr = 0x48;
    g_fake_c1diag = s;
    uint8_t r[64];
    std::memset(r, 0xA5, sizeof(r));
    CHECK_EQ(pico_cmd_get(PICO_CMD_C1_DIAG, r, 63), 63);
    auto u32 = [&](int at) { uint32_t x; std::memcpy(&x, r + at, 4); return x; };
    CHECK_EQ(r[0], 'C');
    CHECK_EQ(r[1], 'F');
    CHECK_EQ(r[2], 1);
    CHECK_EQ(r[3], 0x27);
    CHECK_EQ(r[4], 15);
    CHECK_EQ(r[5], 12);
    CHECK_EQ(r[6], 2);
    CHECK_EQ(r[7], 3);
    CHECK_EQ(u32(8), 0x10001234u);
    CHECK_EQ(u32(12), 0x20001235u);
    CHECK_EQ(u32(16), 0x61000000u);
    CHECK_EQ(u32(20), 0x00008200u);
    CHECK_EQ(u32(24), 0x40000000u);
    CHECK_EQ(u32(28), 0xE000EDF8u);
    CHECK_EQ(u32(32), 0x10ABCDEFu);
    CHECK_EQ(u32(36), 0x20040F00u);
    CHECK_EQ(u32(40), 123456789u);
    CHECK_EQ(u32(44), 0xFFFFFFFFu);
    CHECK_EQ(r[48] | r[49] << 8, 0xFFFF);
    CHECK_EQ(u32(50), 0x60007203u);
    CHECK_EQ(u32(54), 0x00009154u);
    CHECK_EQ(r[58] | r[59] << 8, 0xBB);
    CHECK_EQ(r[60], 0xF9);
    CHECK_EQ(r[61], 0x48);
    CHECK_EQ(r[62], 0);
    CHECK_EQ(r[63], 0xA5);  // nothing past 63
    // A SET to 0xEA does nothing (read-only).
    const uint32_t gen = cmd_host_config_gen();
    const uint8_t junk[] = {0x02, 0x09, 0x07};
    pico_cmd_set(PICO_CMD_C1_DIAG, junk, sizeof(junk));
    CHECK_EQ(cmd_host_config_gen(), gen);
    g_fake_c1diag = C1DiagSnapshot{};
}

TEST(cmd_random_sets_never_crash) {
    fresh();
    std::mt19937 rng(42);
    for (int n = 0; n < 20000; n++) {
        const uint16_t len = 1 + rng() % 63;
        std::unique_ptr<uint8_t[]> buf(new uint8_t[len]);
        for (uint16_t i = 0; i < len; i++) buf[i] = (uint8_t) rng();
        buf[0] = (uint8_t) (rng() % 0x18);  // mostly real function ids
        if (buf[0] == 0x03) buf[0] = 0x01;  // USB reconnect sleeps 150 ms
        if (len > 2 && (rng() & 1)) {       // and valid-looking framing half the time
            const uint8_t magic[][2] = {{'R', 'M'}, {'R', 'X'}, {'C', 'L'}, {'C', 'V'}};
            const auto &m = magic[rng() % 4];
            buf[1] = m[0];
            buf[2] = m[1];
        }
        pico_cmd_set(PICO_CMD_SET, buf.get(), len);
        settle();
    }
    // Still answering, and still the same protocol.
    uint8_t id[46];
    CHECK_EQ(pico_cmd_get(PICO_CMD_IDENTITY, id, sizeof(id)), 46);
    CHECK_EQ(id[5], 12);
}

TEST(cmd_only_f6_runs_functions) {
    fresh();
    const int reboots = g_fake_reboots;
    const uint8_t reboot[] = {0x07};
    for (int rid = 0; rid < 256; rid++) {
        if (rid == PICO_CMD_SET) continue;
        pico_cmd_set((uint8_t) rid, reboot, sizeof(reboot));
    }
    settle();
    CHECK_EQ(g_fake_reboots, reboots);
    pico_cmd_set(PICO_CMD_SET, reboot, sizeof(reboot));
    settle();
    CHECK_EQ(g_fake_reboots, reboots + 1);
}
