#!/usr/bin/env bash
# A dev firmware build, stamped with exactly what it was built from, and
# optionally flashed onto the dongle. Releases are tools/release.sh (CI).
#
#   tools/dev_firmware.sh            build -> prints the .uf2 path
#   tools/dev_firmware.sh --flash    build, put the dongle in BOOTSEL, copy it
#   tools/dev_firmware.sh --picow    build for the original Pico W instead
#   tools/dev_firmware.sh --diag D   an issue #15 diagnostic build (D, F1, F2 or F3;
#                                    CMakeLists.txt ISSUE15_DIAG), own build folder
#
# The version is "<last tag>+<commits since>.<commit>[.dirty]", e.g.
# v0.9.3-oled-edition+3.a1b2c3d: the OLED, the app and picotool all show it,
# so a dev build is never mistaken for a release. Build folders live in
# ~/.cache/ds5dongle/ (one per board), kept between builds for speed.
set -euo pipefail
FW="$(cd "$(dirname "$0")/.." && pwd)"
FLASH=false
BOARD=pico2_w
DIAG=""
while [ $# -gt 0 ]; do
  case "$1" in
    --flash) FLASH=true ;;
    --picow) BOARD=pico_w ;;
    --diag) DIAG="${2:-}"; shift ;;
    *) echo "usage: tools/dev_firmware.sh [--flash] [--picow] [--diag D|F1|F2|F3]" >&2; exit 2 ;;
  esac
  shift
done
case "$DIAG" in ""|D|F1|F2|F3) ;; *) echo "--diag takes D, F1, F2 or F3" >&2; exit 2 ;; esac
: "${PICO_SDK_PATH:=$HOME/pico-sdk}"
export PICO_SDK_PATH
[ -d "$PICO_SDK_PATH" ] || { echo "Pico SDK not found at $PICO_SDK_PATH (set PICO_SDK_PATH)" >&2; exit 1; }

desc="$(git -C "$FW" describe --tags --match 'v*-oled-edition' --long --dirty)"
# v0.9.3-oled-edition-3-ga1b2c3d[-dirty] -> v0.9.3-oled-edition+3.a1b2c3d[.dirty]
VERSION="$(sed -E 's/-([0-9]+)-g([0-9a-f]+)(-dirty)?$/+\1.\2\3/; s/-dirty$/.dirty/' <<<"$desc")"
BUILD="${XDG_CACHE_HOME:-$HOME/.cache}/ds5dongle/build-$BOARD${DIAG:+-diag-$DIAG}"
EXTRA=()
[ "$BOARD" = pico_w ] && EXTRA=(-DPICO_W_BUILD=ON)

echo "== Firmware $VERSION ($BOARD${DIAG:+, issue #15 diag $DIAG})"
cmake -S "$FW" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_COMPANION_VENDOR=ON "-DVERSION=$VERSION" "-DISSUE15_DIAG=$DIAG" "${EXTRA[@]}" >/dev/null
cmake --build "$BUILD" --target ds5-bridge >/dev/null
UF2="$BUILD/ds5-bridge-oled.uf2"
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
