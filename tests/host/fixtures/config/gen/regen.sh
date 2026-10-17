#!/usr/bin/env bash
# Regenerates tests/host/fixtures/config/<tag>/*.bin: each released config
# writer's own config.cpp (from the tag), host-built with writer.cpp, writes
# real pages (default, custom settings, and the narrator / PC-sleep saves
# where that firmware has them). One tag per distinct config.cpp + config.h +
# crc32 (see ../COVERAGE.txt). Scratch goes under ~/.cache/claude-scratch.
#   tests/host/fixtures/config/gen/regen.sh
set -euo pipefail
GEN="$(cd "$(dirname "$0")" && pwd)"
REPO="$(git -C "$GEN" rev-parse --show-toplevel)"
OUT="$GEN/.."
WORK="${XDG_CACHE_HOME:-$HOME/.cache}/claude-scratch/config-fixtures"
TAGS=(v0.6.0 v0.6.0-oled-edition v0.6.5-oled-edition v0.6.6-oled-edition v0.6.8-oled-edition
      v0.6.11-oled-edition v0.6.12-oled-edition v0.7.0 v0.7.2 v0.7.2-hotfix v0.7.0-oled-edition
      v0.8.0-oled-edition v0.8.1-oled-edition v0.9.0-oled-edition v0.9.5-dev.8-oled-edition v0.9.5-dev.9-oled-edition
      v0.9.5-dev.10-oled-edition v0.9.5-dev.14-oled-edition v0.9.5-dev.17-oled-edition)
for t in "${TAGS[@]}"; do
  d="$WORK/$t"; rm -rf "$d"; mkdir -p "$d/out"
  git -C "$REPO" archive "$t" src | tar -x -C "$d"
  # Upstream 0.7.x's config.cpp calls into state_mgr; nothing to do here.
  printf '#include <cstdint>\nvoid set_gain(uint8_t){}\nvoid set_trigger_reduce(uint8_t){}\nvoid set_volume(uint8_t,uint8_t){}\n' > "$d/extra.cpp"
  case "$t" in v0.7.0|v0.7.2|v0.7.2-hotfix) extra="$d/extra.cpp" ;; *) extra="" ;; esac
  g++ -std=c++20 -O0 -w -fsigned-char -DENABLE_SERIAL=0 -DENABLE_VERBOSE=0 -DPICO_RP2350=1 \
    -I"$GEN/stubs" -I"$REPO/tests/host/stubs" -I"$d/src" \
    "$d/src/config.cpp" "$GEN/writer.cpp" "$GEN/fakes_old.cpp" $extra -o "$d/writer"
  "$d/writer" "$d/out" >/dev/null
  mkdir -p "$OUT/$t"; cp "$d/out/"* "$OUT/$t/"
  echo "$t: $(cat "$d/out/layout.txt")"
done
