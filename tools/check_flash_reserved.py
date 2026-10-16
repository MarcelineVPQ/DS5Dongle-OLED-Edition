#!/usr/bin/env python3
"""A firmware update must never write saved state: settings, pairing slots,
button assignments, calibration and BTstack's link keys live in the sectors from
RESERVED_FLASH_OFFSET (src/flash_layout.h) to the end of flash, and the app
writes only the sectors a UF2's blocks cover (app: src/flash/index.ts,
parseUf2Regions; tests/unit/uf2Regions.test.ts). So this checks a built
firmware for:

  1. ds5_reserved_flash_start (src/config.cpp: the start of saved state as an
     address, compiled from flash_layout.h, so the layout is written down
     once) is in the ELF; its value is read from the UF2 and must be sector
     aligned;
  2. the image ends below it (__flash_binary_end);
  3. every UF2 block that goes to flash, rounded up to the whole sector the app
     erases, ends at or below it, apart from the RP2350
     "absolute" family block at 0x10ffff00 (the bootrom's E10 workaround,
     outside the 4 MB flash, which the app and picotool skip).

  tools/check_flash_reserved.py path/to/ds5-bridge-oled.elf path/to/ds5-bridge-oled.uf2
  Run by .github/workflows/build.yml and release.yml after each build.
"""
import os
import struct
import subprocess
import sys

UF2_MAGIC = (0x0A324655, 0x9E5D5157, 0x0AB16F30)
FLAG_NOT_MAIN_FLASH = 0x00000001
FLAG_FAMILY_ID = 0x00002000
FAMILY_ABSOLUTE = 0xE48BFF57
SECTOR = 0x1000


def symbols(elf):
    nm = os.environ.get("NM", "arm-none-eabi-nm")
    out = subprocess.run([nm, elf], check=True, capture_output=True, text=True).stdout
    syms = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms[parts[2]] = int(parts[0], 16)
    return syms


def uf2_blocks(path):
    data = open(path, "rb").read()
    if len(data) % 512:
        sys.exit(f"check_flash_reserved: {path} is not a whole number of UF2 blocks")
    blocks = []
    for off in range(0, len(data), 512):
        m0, m1, flags, addr, size, _no, _n, family = struct.unpack_from("<8I", data, off)
        (m_end,) = struct.unpack_from("<I", data, off + 508)
        if (m0, m1, m_end) != UF2_MAGIC:
            sys.exit(f"check_flash_reserved: bad UF2 magic at offset {off}")
        if flags & FLAG_NOT_MAIN_FLASH:
            continue
        fam = family if flags & FLAG_FAMILY_ID else None
        blocks.append((addr, data[off + 32 : off + 32 + size], fam))
    return blocks


def read_word(blocks, addr):
    for start, payload, _ in blocks:
        if start <= addr and addr + 4 <= start + len(payload):
            return struct.unpack_from("<I", payload, addr - start)[0]
    return None


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    elf, uf2 = sys.argv[1], sys.argv[2]
    syms = symbols(elf)
    bad = []
    sym = syms.get("ds5_reserved_flash_start")
    end = syms.get("__flash_binary_end")
    if sym is None or end is None:
        sys.exit("check_flash_reserved: ds5_reserved_flash_start or __flash_binary_end missing from the ELF")
    blocks = uf2_blocks(uf2)
    reserved = read_word(blocks, sym)
    if reserved is None:
        sys.exit("check_flash_reserved: ds5_reserved_flash_start isn't in the UF2")
    if reserved % SECTOR:
        bad.append(f"the reserved start 0x{reserved:08x} isn't sector aligned")
    if end > reserved:
        bad.append(f"the image ends at 0x{end:08x}, inside saved state (from 0x{reserved:08x})")
    written = 0
    for start, payload, fam in blocks:
        if fam == FAMILY_ABSOLUTE:
            continue
        # The app erases whole sectors: the last sector a block touches must
        # end at or below the reserved start.
        last_sector_end = -(-(start + len(payload)) // SECTOR) * SECTOR
        if last_sector_end > reserved:
            bad.append(f"UF2 block at 0x{start:08x} (+{len(payload)}) reaches saved state (from 0x{reserved:08x})")
        written = max(written, last_sector_end)
    for line in bad:
        print(f"check_flash_reserved: {line}", file=sys.stderr)
    if bad:
        return 1
    print(f"check_flash_reserved: {os.path.basename(uf2)} writes up to 0x{written:08x}; "
          f"saved state starts at 0x{reserved:08x} ({(reserved - written) // 1024} KB clear)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
