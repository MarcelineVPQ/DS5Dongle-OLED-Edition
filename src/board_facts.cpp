//
// Boot-time board facts (board_facts.h). The pure decisions are in
// fw_identity.h (host-tested); this file only talks to the hardware.
//

#include "board_facts.h"

#include <cstdio>
#include "fw_identity.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#if PICO_RP2350
#include "hardware/structs/sysinfo.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/otp_data.h"
#include "pico/platform.h"
#endif

static uint8_t s_measured = 0;
static uint8_t s_chip = fwid::kChipUnknown;

// SFDP read (JESD216, command 0x5A): 3 address bytes, 8 dummy clocks, then
// data, in plain SPI. flash_do_cmd leaves XIP and puts it back itself; it runs
// from RAM, and nothing else may run from flash meanwhile (interrupts off,
// core1 not started yet).
static void sfdp_read(uint32_t addr, uint8_t *out, uint32_t n) {
    uint8_t buf[5 + 16] = {0x5A, (uint8_t) (addr >> 16), (uint8_t) (addr >> 8), (uint8_t) addr, 0};
    const uint32_t irq = save_and_disable_interrupts();
    flash_do_cmd(buf, buf, 5 + n);
    restore_interrupts(irq);
    // loop: NOT-WORTH — at most 16 bytes, twice at boot
    for (uint32_t i = 0; i < n; i++) out[i] = buf[5 + i];
}

void board_facts_probe() {
    uint8_t hdr[16];
    sfdp_read(0, hdr, sizeof(hdr));
    uint32_t ptr = 0;
    // error: LOGGED-OK — printf; a flash without SFDP reports its size as unknown (0xEA [8] = 0) and keeps saved state on, as before this check
    if (fwid::sfdp_bfpt_ptr(hdr, ptr)) {
        uint8_t dw[4];
        sfdp_read(ptr + 4, dw, sizeof(dw));
        s_measured = fwid::sfdp_density_code((uint32_t) dw[0] | (uint32_t) dw[1] << 8 | (uint32_t) dw[2] << 16 | (uint32_t) dw[3] << 24);
    } else {
        printf("[Board] No SFDP table: flash size unknown\n");
    }
#if PICO_RP2350
    const uint8_t version = rp2350_chip_version();
    // OTP rows read ECC-corrected, 16 bits each, from OTP_DATA_BASE (as the
    // SDK's powman.c does); only needed on A2 silicon.
    const uint16_t gpios = version <= 2 ? ((const volatile uint16_t *) OTP_DATA_BASE)[OTP_DATA_NUM_GPIOS_ROW] : 0;
    s_chip = fwid::rp2350_package(version, (uint8_t) (sysinfo_hw->package_sel & 1u), gpios);
#else
    s_chip = fwid::kChipRp2040;
#endif
    printf("[Board] flash built 0x%x measured 0x%x, chip %u%s\n", board_flash_built(), s_measured, s_chip,
           board_saved_state_off() ? ": saved state off" : "");
}

uint8_t board_flash_built() { return fwid::flash_code(PICO_FLASH_SIZE_BYTES); }
uint8_t board_flash_measured() { return s_measured; }
uint8_t board_chip() { return s_chip; }
bool board_saved_state_off() { return fwid::saved_state_off(board_flash_built(), s_measured); }
