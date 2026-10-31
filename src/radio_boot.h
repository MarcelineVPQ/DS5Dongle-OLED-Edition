//
// The radio-missing stop (DESIGN_FIRMWARE_IDENTITY §1.2): a board without the
// wireless chip (a Pico 2 flashed with the Pico 2 W image) failed
// cyw43_arch_init() and rebooted for ever, so it never showed anything. Failed
// starts are counted in a watchdog scratch register (kept across a watchdog
// reboot, cleared at power-on); the third one stops and says why on the OLED.
// Pure, host-tested (tests/host/test_fw_identity.cpp).
//

#ifndef DS5_BRIDGE_RADIO_BOOT_H
#define DS5_BRIDGE_RADIO_BOOT_H

#include <cstdint>

namespace radio_boot {

// watchdog_hw->scratch[0]. The SDK's watchdog_reboot uses scratch 4..7
// (hardware_watchdog/watchdog.c); the RP2350 boot ROM's reboot passes its
// parameters in 2..3 (RP2350 datasheet, reboot; not re-checked here).
constexpr unsigned kScratch = 0;
constexpr uint32_t kMagic = 0xD5520000u;  // high half; low byte = failed starts
constexpr uint32_t kMagicMask = 0xFFFF0000u;
constexpr uint8_t kStopAfter = 3;

// The failed starts so far (0 when the register holds anything else).
constexpr uint8_t failures(uint32_t scratch) { return (scratch & kMagicMask) == kMagic ? (uint8_t) (scratch & 0xFF) : 0; }

// The register's value after one more failed start.
constexpr uint32_t after_failure(uint32_t scratch) {
    const uint8_t n = failures(scratch);
    return kMagic | (uint32_t) (n < 0xFF ? n + 1 : n);
}

// After this failed start: stop (true) or reboot and try again (false).
constexpr bool should_stop(uint32_t scratch_after) { return failures(scratch_after) >= kStopAfter; }

}  // namespace radio_boot

#endif  // DS5_BRIDGE_RADIO_BOOT_H
