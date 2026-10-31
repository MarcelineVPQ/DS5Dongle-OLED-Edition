#!/usr/bin/env bash
# "Back to stable" keeps the settings (review update-flash N7): the last stable
# release's own config.cpp (STABLE below) loads the page the current firmware
# saves with the custom settings (stable/current_custom.bin, written by the
# host test config_stable_release_reads_the_current_page with
# DS5_STABLE_PAGE_OUT set) and must hold exactly those settings. Writes
# stable/RESULT.txt, which that host test checks. Rerun it after a change to
# the saved page (the host test says when) or a new stable release.
#   tests/host/fixtures/config/gen/stable_reads.sh
set -euo pipefail
STABLE=v0.9.4-oled-edition
GEN="$(cd "$(dirname "$0")" && pwd)"
REPO="$(git -C "$GEN" rev-parse --show-toplevel)"
OUT="$GEN/../stable"
WORK="${XDG_CACHE_HOME:-$HOME/.cache}/claude-scratch/config-fixtures/stable-$STABLE"
rm -rf "$WORK"; mkdir -p "$WORK"
git -C "$REPO" archive "$STABLE" src | tar -x -C "$WORK"
g++ -std=c++20 -O0 -w -fsigned-char -DENABLE_SERIAL=0 -DENABLE_VERBOSE=0 -DPICO_RP2350=1 \
  -I"$GEN" -I"$GEN/stubs" -I"$REPO/tests/host/stubs" -I"$WORK/src" \
  "$WORK/src/config.cpp" "$GEN/reader.cpp" "$GEN/fakes_old.cpp" -o "$WORK/reader"
result="$("$WORK/reader" "$OUT/current_custom.bin" | tail -n 1 || true)"  # its config.cpp prints too; the verdict is last
printf '%s %s\n' "$result" "$STABLE" > "$OUT/RESULT.txt"
echo "$STABLE reads the current page: $result"
rm -rf "$WORK"
[ "$result" = ok ]
