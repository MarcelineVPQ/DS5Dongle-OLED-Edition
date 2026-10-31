//
// Firmware identity, GET 0xEA (protocol 1.21, identity [49] bit 2;
// DESIGN_FIRMWARE_IDENTITY §1.1): which board type, chip, flash, persona and
// display this image is and runs on, so the app matches an update to the
// board instead of guessing from the board's name. Plus the pure pieces of the
// boot-time checks behind it: the flash's SFDP density (JESD216) and the
// saved-state lock, and the chip's package. Pure, host-tested
// (tests/host/test_fw_identity.cpp); board_facts.cpp reads the hardware.
//
//   [0..1]  'F' 'I'
//   [2]     layout version (1); grows only by appending
//   [3..4]  u16 board type id (1 pico2_w, 2 pico_w, 0 not in the table)
//   [5]     active persona (0 DualSense, incl. the Edge mode)
//   [6]     personas this image can switch to, bit n = persona n (0x01)
//   [7]     flash size the image was built for, code n = 4 KiB << n
//           (OTP FLASH_DEVINFO's encoding: 0x9 2 MB, 0xA 4 MB, 0xC 16 MB)
//   [8]     flash size measured at boot, same code, 0 = unknown
//   [9]     chip: 0 RP2040, 1 RP2350A (QFN60), 2 RP2350B (QFN80), 0xFF unknown
//   [10]    flags: bit0 radio found, bit1 saved state off (flash smaller than
//           the build: settings stay in RAM), bit2 display driver running
//   [11]    active display id (display_table.h; 0 none)
//   [12]    displays this image drives, bit n = display id n
//   [13]    saved display id: what runs from the next start (0xF6 0x1B)
//

#ifndef DS5_BRIDGE_FW_IDENTITY_H
#define DS5_BRIDGE_FW_IDENTITY_H

#include <cstdint>

namespace fwid {

constexpr uint8_t kLayout = 1;
constexpr uint16_t kLen = 14;

// Board type ids (the firmware repo's boards.json from step 7; the app's
// src/update/boardIdentity.ts). 0 = a board not in the table.
enum : uint16_t { kBoardUnknown = 0, kBoardPico2W = 1, kBoardPicoW = 2 };
enum : uint8_t { kPersonaDs5 = 0 };
enum : uint8_t { kChipRp2040 = 0, kChipRp2350A = 1, kChipRp2350B = 2, kChipUnknown = 0xFF };
enum : uint8_t { kFlagRadio = 0x01, kFlagSavedStateOff = 0x02, kFlagDisplay = 0x04 };

// The UF2's binary info (bi_decl in main.cpp), for the release gate G2
// (tools/uf2_info.mjs, step 7): bi_string entries under this tag.
#define DS5_BI_TAG_C1 'D'
#define DS5_BI_TAG_C2 '5'
enum : uint32_t {
    kBiBoard = 0x44350001,     // board type, e.g. "pico2_w"
    kBiPersonas = 0x44350002,  // personas, e.g. "ds5"
    kBiDisplays = 0x44350003,  // displays, e.g. "none,pico_oled_1_3"
};

struct Facts {
    uint16_t board_type;
    uint8_t persona;
    uint8_t personas;
    uint8_t flash_built;     // code
    uint8_t flash_measured;  // code, 0 unknown
    uint8_t chip;
    uint8_t flags;
    uint8_t display_active;
    uint8_t displays;
    uint8_t display_saved;
};

// The 0xEA reply; 0 when the request is shorter than the layout.
inline uint16_t write(const Facts &f, uint8_t *out, uint16_t reqlen) {
    // error: NOT-WORTH — no data (not a STALL: the vendor interface answers a zero-length reply); a request shorter than the reply (the app asks the full length, its decoder bounds-checks)
    if (reqlen < kLen) return 0;
    out[0] = 'F';
    out[1] = 'I';
    out[2] = kLayout;
    out[3] = (uint8_t) (f.board_type & 0xFF);
    out[4] = (uint8_t) (f.board_type >> 8);
    out[5] = f.persona;
    out[6] = f.personas;
    out[7] = f.flash_built;
    out[8] = f.flash_measured;
    out[9] = f.chip;
    out[10] = f.flags;
    out[11] = f.display_active;
    out[12] = f.displays;
    out[13] = f.display_saved;
    return kLen;
}

// Flash size code for a byte count (4 KiB << n); 0 for a size that isn't a
// power of two of at least 8 KiB (code 0, 4 KiB, means "unknown" here).
constexpr uint8_t flash_code(uint64_t bytes) {
    if (bytes < 8192 || (bytes & (bytes - 1)) != 0) return 0;
    uint8_t n = 0;
    // loop: NOT-WORTH — at most ~40 shifts, boot / compile time only
    while ((4096ull << n) < bytes) n++;
    return n;
}

// SFDP (JESD216): the 8-byte header then parameter header 0, which must be
// the JEDEC Basic Flash Parameter Table (id LSB 0x00, MSB 0xFF) with at least
// two DWORDs. Gives the table's address in the SFDP space, false otherwise.
inline bool sfdp_bfpt_ptr(const uint8_t hdr[16], uint32_t &ptr) {
    // error: NOT-WORTH — the measured size stays unknown (0xEA [8] = 0), saved state stays on; a flash without SFDP
    if (hdr[0] != 'S' || hdr[1] != 'F' || hdr[2] != 'D' || hdr[3] != 'P') return false;
    // error: NOT-WORTH — the measured size stays unknown (0xEA [8] = 0), saved state stays on; not a basic table first
    if (hdr[8] != 0x00 || hdr[15] != 0xFF || hdr[11] < 2) return false;
    ptr = (uint32_t) hdr[12] | (uint32_t) hdr[13] << 8 | (uint32_t) hdr[14] << 16;
    return true;
}

// The basic table's DWORD 2, flash memory density: bit 31 clear = size in
// bits minus 1, set = 2^N bits. As a size code, 0 when it can't be one.
constexpr uint8_t sfdp_density_code(uint32_t dword2) {
    if (dword2 & 0x80000000u) {
        const uint32_t n = dword2 & 0x7FFFFFFFu;
        return n >= 16 && n <= 63 ? flash_code(1ull << (n - 3)) : 0;
    }
    return flash_code(((uint64_t) dword2 + 1) / 8);
}

// Saved state (config, slots, remap, calibration, BTstack's link keys) sits
// at the top of the flash the image was built for. On a smaller chip it lands
// elsewhere (or nowhere), so it is never written: settings work until the
// board restarts. An unknown size (0) leaves saved state on.
constexpr bool saved_state_off(uint8_t built, uint8_t measured) { return measured != 0 && measured < built; }

// RP2350 package: SYSINFO_PACKAGE_SEL bit 0 (1 QFN60, 0 QFN80), except on A2
// silicon (chip version <= 2), whose package select picotool doesn't trust:
// there OTP NUM_GPIOS (row 0x18) says it, 30 = QFN60, 48 = QFN80, as picotool
// does (main.cpp 2344-2380). Anything else on A2 is unknown.
constexpr uint8_t rp2350_package(uint8_t chip_version, uint8_t package_sel, uint16_t otp_num_gpios) {
    if (chip_version > 2) return (package_sel & 1) ? kChipRp2350A : kChipRp2350B;
    if (otp_num_gpios == 30) return kChipRp2350A;
    if (otp_num_gpios == 48) return kChipRp2350B;
    return kChipUnknown;
}

}  // namespace fwid

#endif  // DS5_BRIDGE_FW_IDENTITY_H
