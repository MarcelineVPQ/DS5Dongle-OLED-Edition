#!/usr/bin/env bash
# A dev firmware build, stamped with exactly what it was built from, and
# optionally flashed onto the dongle. Releases are tools/release.sh (CI).
#
#   tools/dev_firmware.sh            build -> prints the .uf2 path
#   tools/dev_firmware.sh --flash    build, put the dongle in BOOTSEL, copy it
#   tools/dev_firmware.sh --picow    build for the original Pico W instead
#
# The version is "<last tag>+<commits since>.<commit>[.dirty]", e.g.
# v0.9.3-oled-edition+3.a1b2c3d: the OLED, the app and picotool all show it,
# so a dev build is never mistaken for a release. Built like a release image
# (tools/build_board.sh: boards.json flags, image checks, gate G2) in
# ~/.cache/ds5dongle/build/<board>, kept between builds for speed.
# PICO_SDK_PATH / PICO_TOOLCHAIN_PATH pick the SDK and compiler (tools/toolchain.sh).
set -euo pipefail
FW="$(cd "$(dirname "$0")/.." && pwd)"
FLASH=false
BOARD=pico2_w
for arg in "$@"; do
  case "$arg" in
    --flash) FLASH=true ;;
    --picow) BOARD=pico_w ;;
    *) echo "usage: tools/dev_firmware.sh [--flash] [--picow]" >&2; exit 2 ;;
  esac
done
. "$FW/tools/toolchain.sh"
toolchain_setup || exit 1

desc="$(git -C "$FW" describe --tags --match 'v*-oled-edition' --long --dirty)"
# v0.9.3-oled-edition-3-ga1b2c3d[-dirty] -> v0.9.3-oled-edition+3.a1b2c3d[.dirty]
VERSION="$(sed -E 's/-([0-9]+)-g([0-9a-f]+)(-dirty)?$/+\1.\2\3/; s/-dirty$/.dirty/' <<<"$desc")"
OUT="${XDG_CACHE_HOME:-$HOME/.cache}/ds5dongle"

echo "== Firmware $VERSION ($BOARD)"
toolchain_fresh_build_dir "$OUT/build/$BOARD"
rm -rf "$OUT/artifacts"  # only the newest build's named copy is kept
DS5_OUT_DIR="$OUT" "$FW/tools/build_board.sh" "$BOARD" "$VERSION" -DENABLE_COMPANION_VENDOR=ON >/dev/null
UF2="$(ls "$OUT"/artifacts/*.uf2)"
echo "Built: $UF2"
$FLASH || exit 0

echo "== Flashing (the dongle restarts into BOOTSEL; press PS afterwards)"
python3 "$FW/tools/bootsel.py"
for _ in $(seq 1 30); do
  DEV="$(lsblk -nrpo NAME,LABEL | awk '$2=="RP2350"{print $1; exit}')"
  [ -n "$DEV" ] && break
  sleep 1
done
[ -n "${DEV:-}" ] || { echo "The RP2350 drive didn't appear" >&2; exit 1; }
MNT="$(findmnt -rn -o TARGET -S "$DEV" | head -1 || true)"  # not mounted yet is fine
[ -n "$MNT" ] || MNT="$(udisksctl mount -b "$DEV" | sed -E 's/.* at (.*)\.?$/\1/')"
cp "$UF2" "$MNT/" && sync
echo "Flashed $VERSION. Press PS on the controller."
