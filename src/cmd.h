//
// Created by awalol on 2026/5/4.
//

#ifndef DS5_BRIDGE_CMD_H
#define DS5_BRIDGE_CMD_H

#include <stdint.h>

bool is_pico_cmd(uint8_t report_id);
bool is_debug_cmd(uint8_t report_id);  // 0xF2-0xF4, read-only
uint16_t pico_cmd_get(uint8_t report_id, uint8_t *buffer,uint16_t reqlen);
void pico_cmd_set(uint8_t report_id, uint8_t const *buffer,uint16_t bufsize);

// Configurator protocol (identity report 0xF1, vendor interface only).
#define PICO_CMD_IDENTITY 0xf1
#define PICO_CMD_SET 0xf6        // the only id that carries commands (SET)
#define PICO_CMD_LIVE_INPUT 0xf0   // vendor interface only (a real DS5 uses F0-F2 for firmware update)
#define PICO_PROTO_MAJOR 1
#define PICO_CMD_CURVES 0xec      // vendor interface only, GET, stick response curves (proto 1.10)
#define PICO_CMD_REMAP_EXTRA 0xed // vendor interface only, GET, Edge extra-button remap (proto 1.9)
#define PICO_CMD_CALIB 0xee       // vendor interface only, GET, calibration body (proto 1.5)
#define PICO_CMD_OLED_MIRROR 0xef // vendor interface only, GET, 1028 bytes (proto 1.4)
#define PICO_PROTO_MINOR 19

// 0xFE mic-debug dump: frame bytes kept of the longest 0x31 seen, so the
// reply (2 length bytes + these) fits the 63-byte cap.
#define MIC_DEBUG_FRAME_BYTES 61

// Result of the last 0xF6 SET, reported in 0xF1.
enum : uint8_t {
    CMD_RESULT_OK = 0,
    CMD_RESULT_PENDING = 1, // deferred to cmd_task(); poll 0xF1
    CMD_RESULT_INVALID = 2, // unknown function or bad argument
    CMD_RESULT_FAILED = 3,  // ran but failed (flash write, BT not connected)
    CMD_RESULT_BUSY = 4,    // a deferred command is still pending
};

// Runs deferred host commands (save, slot ops, reboot, BOOTSEL...) from the
// main loop, outside the USB control transfer that requested them.
void cmd_task();

// Bumped on every host-originated config change, so the OLED can re-read the
// fields it caches (lightbar, brightness, Settings snapshot).
uint32_t cmd_host_config_gen();

// The connected controller: 0 DualSense, 1 DualSense Edge, 0xFF not known yet
// (identity [26], 0xEB header [8]).
uint8_t cmd_controller_model();

// Smoothed RP2350 on-die temperature sensor reading (ADC input 4, 12-bit
// raw). A single sample is very noisy; this averages a large block and runs
// a slow EMA so the value converges to the true die temperature instead of
// chasing per-sample noise. Single source of truth — the OLED CPU screen and
// the 0xfc web telemetry both call this so device and web always agree.
uint16_t cpu_temp_raw_smoothed();
// clk_sys as the on-chip frequency counter measures it, in kHz. Measured once
// (the counter busy-waits a few ms on core0; clk_sys is fixed at boot), then
// the same value for the CPU screen and 0xFC (cache pass batch 5).
uint32_t measured_clk_khz();

#endif //DS5_BRIDGE_CMD_H
