// Core1 crash diagnostics for issue #15 (built only with CMake ISSUE15_DIAG,
// which defines DS5_C1_DIAG=1): what core1 was doing, whether it took a
// HardFault (and where), how much of its stack it ever used, and the board's
// flash (QMI) setup. Read with GET 0xEA, on the same paths as 0xF5 (vendor
// interface and HID feature report); scripts/audio_diag.py decodes it.
//
// Reply (63 bytes, little-endian, the cap of both transports):
//   [0..1]  'C' 'F'        [2] layout version (1)
//   [3]     flags: bit0 core1 took a HardFault (the fault fields are valid),
//           bit1 fault recorder installed (RP2350 only), bit2 core1 stack
//           guard armed (MSPLIM = stack base + 64), bit3 F1 build (XIP setup
//           embedded, flash clock divider 4), bit4 F3 build (core1 idle is a
//           RAM busy-wait, no sleep_us), bit5 RP2350
//   [4]     core1 stage now (as 0xF5: 0..9 start-up, 10..15 in its loop)
//   [5]     core1 stage when it faulted (0 = no fault)
//   [6]     chip version (RP2350: 0 A0, 1 A1, 2 A2; RP2040: B0..B2 = 1..3)
//   [7]     boot ROM version (byte at 0x13)
//   [8]  PC     [12] LR     [16] xPSR     (from the stacked exception frame)
//   [20] CFSR   [24] HFSR   [28] MMFAR    [32] BFAR
//   [36] SP (the stacked frame's address)
//   [40] time_us_32() when it faulted
//   [44] µs since core1 last passed the top of its loop (0xFFFFFFFF never)
//   [48..49] u16 bytes of core1's stack never touched (high-water mark)
//   [50] QMI M0 TIMING   [54] QMI M0 RFMT   [58..59] QMI M0 RCMD (zeros on RP2040)
//   [60] EXC_RETURN low byte   [61] SFSR low byte   [62] reserved (0)
// Pure (no SDK): cmd.cpp serializes, the host tests check it.
#ifndef DS5_BRIDGE_C1_DIAG_H
#define DS5_BRIDGE_C1_DIAG_H

#include <cstdint>
#include <cstring>

#define PICO_CMD_C1_DIAG 0xea  // GET only, DS5_C1_DIAG builds

struct C1DiagSnapshot {
    uint8_t flags = 0;
    uint8_t stage_now = 0;
    uint8_t stage_at_fault = 0;
    uint8_t chip_version = 0;
    uint8_t rom_version = 0;
    uint32_t pc = 0, lr = 0, xpsr = 0;
    uint32_t cfsr = 0, hfsr = 0, mmfar = 0, bfar = 0;
    uint32_t sp = 0;
    uint32_t fault_us = 0;
    uint32_t heartbeat_age_us = 0xFFFFFFFFu;
    uint32_t stack_free_bytes = 0;
    uint32_t qmi_timing = 0, qmi_rfmt = 0, qmi_rcmd = 0;
    uint8_t exc_return = 0;
    uint8_t sfsr = 0;
};

enum : uint8_t {
    C1DIAG_FAULTED      = 0x01,
    C1DIAG_RECORDER     = 0x02,
    C1DIAG_STACK_GUARD  = 0x04,
    C1DIAG_F1_XIP_SETUP = 0x08,
    C1DIAG_F3_BUSY_IDLE = 0x10,
    C1DIAG_RP2350       = 0x20,
};

constexpr uint16_t kC1DiagLen = 63;
constexpr uint8_t kC1DiagVersion = 1;

// Writes min(cap, 63) bytes of the reply; returns that count.
inline uint16_t c1diag_serialize(const C1DiagSnapshot &s, uint8_t *buffer, uint16_t cap) {
    uint8_t out[kC1DiagLen]{};
    auto put32 = [&](int at, uint32_t v) {
        out[at] = (uint8_t) v;
        out[at + 1] = (uint8_t) (v >> 8);
        out[at + 2] = (uint8_t) (v >> 16);
        out[at + 3] = (uint8_t) (v >> 24);
    };
    out[0] = 'C';
    out[1] = 'F';
    out[2] = kC1DiagVersion;
    out[3] = s.flags;
    out[4] = s.stage_now;
    out[5] = s.stage_at_fault;
    out[6] = s.chip_version;
    out[7] = s.rom_version;
    put32(8, s.pc);
    put32(12, s.lr);
    put32(16, s.xpsr);
    put32(20, s.cfsr);
    put32(24, s.hfsr);
    put32(28, s.mmfar);
    put32(32, s.bfar);
    put32(36, s.sp);
    put32(40, s.fault_us);
    put32(44, s.heartbeat_age_us);
    const uint32_t free_b = s.stack_free_bytes > 0xFFFFu ? 0xFFFFu : s.stack_free_bytes;
    out[48] = (uint8_t) free_b;
    out[49] = (uint8_t) (free_b >> 8);
    put32(50, s.qmi_timing);
    put32(54, s.qmi_rfmt);
    out[58] = (uint8_t) s.qmi_rcmd;
    out[59] = (uint8_t) (s.qmi_rcmd >> 8);
    out[60] = s.exc_return;
    out[61] = s.sfsr;
    out[62] = 0;
    const uint16_t n = cap < kC1DiagLen ? cap : kC1DiagLen;
    std::memcpy(buffer, out, n);
    return n;
}

// Firmware: audio.cpp (DS5_C1_DIAG builds); host tests: fakes.cpp.
void c1diag_snapshot(C1DiagSnapshot *out);

#endif  // DS5_BRIDGE_C1_DIAG_H
