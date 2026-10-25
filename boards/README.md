# Waveshare RP2350B-Plus-W

This port builds the OLED Edition for the Waveshare RP2350B-Plus-W. Its
firmware base is `v0.9.5-dev.20-oled-edition` (commit `3a85382`); the fork's
next commit only updates the app's release manifests.

The board header is copied from awalol/DS5Dongle at commit
`daa35cca841c8a70a627d1ce9a65b9d3f3209265`:
RP2350B (`PICO_RP2350A=0`), 16 MiB flash, RM2/CYW43 on GPIO36 (REG_ON),
GPIO37 (DATA/HOST_WAKE), GPIO38 (CS), GPIO39 (CLOCK).
The configurator reports the board as **RP2350B-Plus-W**; its identity field
has a 16-byte limit, so the full CMake board identifier cannot be used there.

## Build

Use the pins in `tools/toolchain.env`: Pico SDK 2.3.0, TinyUSB
`2d56dc533e45e4e91b15e93fdab5e22e964f328d`, ARM GNU 15.2.Rel1.
The newer SDK/TinyUSB pins currently used by awalol are not required by
this OLED release.

From a fresh build directory:

```bash
cmake -S . -B build/waveshare -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPICO_SDK_PATH="$PICO_SDK_PATH" \
  -DWAVESHARE_RP2350B_PLUS_W_BUILD=ON \
  -DENABLE_COMPANION_VENDOR=ON \
  -DVERSION=v0.9.5-dev.20-oled-edition+waveshare
cmake --build build/waveshare --target ds5-bridge
tools/check_ram_residency.sh build/waveshare/ds5-bridge-oled.elf
tools/check_xip_setup.sh build/waveshare/ds5-bridge-oled.elf 2 11
python3 tools/check_flash_reserved.py build/waveshare/ds5-bridge-oled.elf build/waveshare/ds5-bridge-oled.uf2
```

The output is `build/waveshare/ds5-bridge-oled.uf2`.
For diagnostic USB CDC logging add `-DENABLE_SERIAL=ON -DENABLE_VERBOSE=ON`.
The normal build retains the stock 150 MHz clock and full RP2350 audio path.
`PICO_W_BUILD` and the Waveshare option are mutually exclusive.

GitHub Actions builds both Waveshare variants with the configurator interface,
runs the host tests and the RAM/XIP/flash reservation checks, and uploads
**DS5-OLED-Waveshare-RP2350B-Plus-W**, containing:

- `ds5-bridge-oled-waveshare.uf2`: normal firmware.
- `ds5-bridge-oled-waveshare-debug.uf2`: USB CDC and verbose logs.
- `SHA256SUMS.txt`.

## OLED wiring

The optional display is Waveshare Pico-OLED-1.3 (SH1107, SPI1).
Wire by GPIO number; do not assume the board's connector matches a Pico header.

| OLED signal | Waveshare GPIO |
| --- | --- |
| DIN / MOSI | GP11 |
| CLK | GP10 |
| CS | GP9 |
| DC | GP8 |
| RST | GP12 |
| KEY0 | GP15 (button to GND) |
| KEY1 | GP17 (button to GND) |
| VCC | 3V3 |
| GND | GND |

The existing OLED firmware also boots without a physical display.
BOOT and RESET are not KEY0 and KEY1.

## Flash and verify

Hold BOOT while connecting USB, release it when the RP2350 drive appears,
and copy the **Waveshare** UF2 to that drive. The board reboots automatically.
Use the normal file first; the debug variant is for investigating problems.

Pair using Create + PS on the controller and verify controller input.
For a DualSense / DualSense Edge, also verify the configurator, audio and
haptics on the actual board. Compilation and static checks cannot establish
hardware behavior.

Use manual Waveshare firmware updates. The upstream configurator's bundled
Pico 2 W UF2 uses different radio pins and must not be installed on this board.
The firmware intentionally keeps a distinct board name; this port does not
add Waveshare firmware to the configurator's update feed.
