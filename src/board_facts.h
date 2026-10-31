//
// What the board itself is, read once at boot (DESIGN_FIRMWARE_IDENTITY §1.1):
// the flash size from the chip's SFDP table and the RP2350 package, for the
// 0xEA report (fw_identity.h) and the saved-state lock.
//

#ifndef DS5_BRIDGE_BOARD_FACTS_H
#define DS5_BRIDGE_BOARD_FACTS_H

#include <cstdint>

// Reads the flash's SFDP density and the chip package. Boot only: first thing
// in main(), before any flash write (config migration, BTstack's bank) and
// before core1 starts, with interrupts off around the flash command.
void board_facts_probe();

uint8_t board_flash_built();     // size code the image was built for (4 KiB << n)
uint8_t board_flash_measured();  // size code read at boot, 0 = unknown
uint8_t board_chip();            // fwid::kChip*
// True when the measured flash is smaller than the build: nothing may write
// saved state (__wrap_flash_safe_execute refuses every flash write).
bool board_saved_state_off();

#endif  // DS5_BRIDGE_BOARD_FACTS_H
