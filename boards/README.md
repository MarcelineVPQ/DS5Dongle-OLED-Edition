# Waveshare RP2350B-Plus-W

Enable this optional board target with
`-DWAVESHARE_RP2350B_PLUS_W_BUILD=ON` (default OFF).
The default build continues to target Raspberry Pi Pico 2 W.

The board header is copied from awalol/DS5Dongle at commit
`daa35cca841c8a70a627d1ce9a65b9d3f3209265`:
RP2350B (`PICO_RP2350A=0`), 16 MiB flash, RM2/CYW43 on GPIO36 (REG_ON),
GPIO37 (DATA/HOST_WAKE), GPIO38 (CS), GPIO39 (CLOCK).
The configurator reports the board as **RP2350B-Plus-W**; its identity field
has a 16-byte limit, so the full CMake board identifier cannot be used there.

## Build

`boards.json` has the board's row (`waveshare_rp2350b_plus_w`, board type 3), so the
board script builds it with the pinned toolchain (`tools/toolchain.env`) and runs
every image check (RAM residency, XIP setup, reserved flash, gate G2):

```bash
tools/build_board.sh waveshare_rp2350b_plus_w v0.9.5-dev.21-oled-edition -DENABLE_COMPANION_VENDOR=ON
```

The output is `artifacts/ds5-bridge-oled-<version>-waveshare_rp2350b_plus_w.uf2`
(the build directory is `build/waveshare_rp2350b_plus_w`). By hand, in a fresh build
directory: `-DWAVESHARE_RP2350B_PLUS_W_BUILD=ON`. For USB CDC logging add
`-DENABLE_SERIAL=ON -DENABLE_VERBOSE=ON`. The board runs at the RP2350's stock
150 MHz with the full audio path. `PICO_W_BUILD` and the Waveshare option are
mutually exclusive.

GitHub Actions builds this image on every push, with the configurator interface,
like every row of `boards.json`, and uploads it in the
**ds5-bridge-oled-<commit>-boards** artifact.

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

Install Waveshare firmware by hand for now. The board is a `release: false` row:
its image is not in releases or the update feeds yet, so the configurator offers
no update for it. The configurator refuses to write another board's firmware to
it (it reads the chip in BOOTSEL mode: the Pico 2 W image is for an RP2350A and
uses different radio pins).
