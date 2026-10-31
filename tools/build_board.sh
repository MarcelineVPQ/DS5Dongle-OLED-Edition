#!/usr/bin/env bash
# Build one board's image from its boards.json row (design
# DESIGN_FIRMWARE_IDENTITY §5), run the image checks and gate G2.
#   tools/build_board.sh BOARD VERSION [extra cmake -D flags...]
# Builds build/BOARD with the row's cmake_flags and -DVERSION=VERSION, runs
# check_ram_residency, check_xip_setup (150 MHz: flash divider 2, 1.10 V; an
# RP2040 image only gets its boot2 checked) and check_flash_reserved, copies
# the UF2 to artifacts/ under the row's asset name ({tag} = VERSION), writes
# artifacts/firmware-image-BOARD.json ([{board, version, file, sha256}], what
# release.yml merges into firmware-images.json), then runs tools/uf2_info.mjs
# against the row and that entry. Used by build.yml and release.yml.
set -euo pipefail
board="${1:?usage: tools/build_board.sh BOARD VERSION [cmake flags...]}"
version="${2:?usage: tools/build_board.sh BOARD VERSION [cmake flags...]}"
shift 2
cd "$(dirname "$0")/.."
field() {
  node -e 'const [id, k] = process.argv.slice(1); const r = require("./boards.json").boards.find((b) => b.id === id);
    if (!r) { console.error(`${id} is not in boards.json`); process.exit(1); }
    console.log(Array.isArray(r[k]) ? r[k].join("\n") : r[k]);' "$board" "$1"
}
mapfile -t flags < <(field cmake_flags | sed "/^$/d")
asset="$(field asset)"
asset="${asset//\{tag\}/$version}"
dir="build/$board"
cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_SDK_PATH="$PICO_SDK_PATH" "${flags[@]}" "$@" -DVERSION="$version"
cmake --build "$dir" --target ds5-bridge
tools/check_ram_residency.sh "$dir/ds5-bridge-oled.elf"
tools/check_xip_setup.sh "$dir/ds5-bridge-oled.elf" 2 11
python3 tools/check_flash_reserved.py "$dir/ds5-bridge-oled.elf" "$dir/ds5-bridge-oled.uf2"
mkdir -p artifacts
cp "$dir/ds5-bridge-oled.uf2" "artifacts/$asset"
printf '[{"board":"%s","version":"%s","file":"%s","sha256":"%s"}]\n' \
  "$board" "$version" "$asset" "$(sha256sum "artifacts/$asset" | cut -d' ' -f1)" > "artifacts/firmware-image-$board.json"
node tools/uf2_info.mjs "artifacts/$asset" --board "$board" --version "$version" --images "artifacts/firmware-image-$board.json"
