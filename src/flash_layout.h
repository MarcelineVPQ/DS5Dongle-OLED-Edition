//
// Flash sectors owned by this firmware, all below BTstack's link-key bank.
//

#ifndef DS5_BRIDGE_FLASH_LAYOUT_H
#define DS5_BRIDGE_FLASH_LAYOUT_H

#include <cstdint>
#include "hardware/flash.h"
#include "pico/btstack_flash_bank.h"

// BTstack keeps its TLV (link keys) in PICO_FLASH_BANK_TOTAL_SIZE bytes at
// PICO_FLASH_BANK_STORAGE_OFFSET; on RP2350 that is the 2 sectors just below the
// last one. Config, slots and remap used to sit at -1/-2/-3 sectors, so slots
// and remap overlapped the bank and each side clobbered the other (flaky
// pairing, BTstack rewriting its bank at boot). They now sit directly below
// the bank, as upstream moved its config (awalol #244).
constexpr uint32_t CONFIG_FLASH_OFFSET = PICO_FLASH_BANK_STORAGE_OFFSET - 1u * FLASH_SECTOR_SIZE;
constexpr uint32_t SLOTS_FLASH_OFFSET  = PICO_FLASH_BANK_STORAGE_OFFSET - 2u * FLASH_SECTOR_SIZE;
constexpr uint32_t REMAP_FLASH_OFFSET  = PICO_FLASH_BANK_STORAGE_OFFSET - 3u * FLASH_SECTOR_SIZE;
constexpr uint32_t CALIB_FLASH_OFFSET  = PICO_FLASH_BANK_STORAGE_OFFSET - 4u * FLASH_SECTOR_SIZE;
// Everything from here to the end of flash belongs to saved state (calibration,
// remap, slots, config, BTstack's bank, the legacy config sector), never to the
// firmware image. tools/check_flash_reserved.py reads it from the built ELF
// (ds5_reserved_flash_start, config.cpp) and fails if any UF2 block or the
// image's end reaches it, so a firmware update can't touch saved state.
constexpr uint32_t RESERVED_FLASH_OFFSET = CALIB_FLASH_OFFSET;
static_assert(RESERVED_FLASH_OFFSET < REMAP_FLASH_OFFSET && REMAP_FLASH_OFFSET < SLOTS_FLASH_OFFSET &&
              SLOTS_FLASH_OFFSET < CONFIG_FLASH_OFFSET && CONFIG_FLASH_OFFSET < PICO_FLASH_BANK_STORAGE_OFFSET,
              "RESERVED_FLASH_OFFSET must be the lowest saved-state sector");
// Where config lived before; read once to migrate saved settings.
constexpr uint32_t LEGACY_CONFIG_FLASH_OFFSET = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;

static_assert(PICO_FLASH_BANK_STORAGE_OFFSET % FLASH_SECTOR_SIZE == 0);
static_assert(CONFIG_FLASH_OFFSET + FLASH_SECTOR_SIZE <= PICO_FLASH_BANK_STORAGE_OFFSET);
static_assert(LEGACY_CONFIG_FLASH_OFFSET >= RESERVED_FLASH_OFFSET);

#endif //DS5_BRIDGE_FLASH_LAYOUT_H
