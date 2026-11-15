//
// Shared flash-sector writer for config / remap / slots.
//

#ifndef DS5_BRIDGE_FLASH_SAFE_H
#define DS5_BRIDGE_FLASH_SAFE_H

#include <cstdint>
#include "hardware/flash.h"
#include "pico/flash.h"
#include "pico/multicore.h"
#include "hardware/sync.h"

// Erase one sector and program its first page, with core1 parked in RAM for
// the duration. save_and_disable_interrupts() alone only stops core0 — core1
// (audio: Opus encode, mic decode) kept fetching from XIP while flash was
// being erased and could fault or read garbage. flash_safe_execute() uses the
// multicore lockout that core1 registers for in core1_entry (upstream 6a52b5e).
// BTstack's link-key TLV goes through flash_safe_execute too, so it gains the
// same protection. Returns false if core1 couldn't be parked in time.
struct FlashSectorWrite {
    uint32_t offset;
    const uint8_t *page;
};

inline void flash_sector_write_cb(void *param) {
    const auto *w = static_cast<const FlashSectorWrite *>(param);
    flash_range_erase(w->offset, FLASH_SECTOR_SIZE);
    flash_range_program(w->offset, w->page, FLASH_PAGE_SIZE);
}

inline bool flash_sector_write(uint32_t offset, const uint8_t *page) {
    FlashSectorWrite w{offset, page};
    // Writes before core1 registers for lockout are handled inside the
    // flash_safe_execute wrapper (src/main.cpp).
    // error: SHOWN — returned false to every save (config/remap/calib/slots), which report it; counted in g_fse_fail
    return flash_safe_execute(flash_sector_write_cb, &w, 100) == PICO_OK;
}

// The same for a record longer than a page (Tension's sets, 5 pages): erase
// the sector, program `len` bytes (whole pages, at most a sector) from `data`.
struct FlashSectorWritePages {
    uint32_t offset;
    const uint8_t *data;
    uint32_t len;
};

inline void flash_sector_write_pages_cb(void *param) {
    const auto *w = static_cast<const FlashSectorWritePages *>(param);
    flash_range_erase(w->offset, FLASH_SECTOR_SIZE);
    flash_range_program(w->offset, w->data, w->len);
}

inline bool flash_sector_write_pages(uint32_t offset, const uint8_t *data, uint32_t len) {
    FlashSectorWritePages w{offset, data, len};
    // error: SHOWN — returned false to Tension's saves, which report CMD_RESULT_FAILED; counted in g_fse_fail
    return flash_safe_execute(flash_sector_write_pages_cb, &w, 100) == PICO_OK;
}

#endif //DS5_BRIDGE_FLASH_SAFE_H
