#!/usr/bin/env bash
# An RP2350 (Pico 2 W) image must carry its own flash (XIP) setup and run it
# at boot (CLAUDE.md, "The Pico 2 W runs at stock 150 MHz"; issue #15):
#   1. .boot2 is non-empty (at most 256 bytes), i.e. PICO_EMBED_XIP_SETUP=1
#      kept it from being garbage-collected;
#   2. crt0 copies and calls it (__boot2_entry_point, _copy_xip_setup,
#      _call_xip_setup);
#   3. its QMI M0_TIMING word decodes to a flash clock divider, printed, and
#      equal to EXPECT_CLKDIV (optional second argument) when given;
#   4. main() sets the core voltage first thing (vreg_set_voltage, its code
#      printed, and equal to EXPECT_VCODE when given: 11 = 1.10 V, the
#      default, for 150 MHz; 13 = 1.20 V for the 320 MHz fallback). The RP2350
#      keeps its regulator setting across every reset but power-on, so the
#      150 MHz build must lower it itself after an overclocked firmware.
# Without PICO_EMBED_XIP_SETUP the RP2350 linker script drops .boot2, so an
# image built without it fails 1. The RP2040 (Pico W) always has its boot2
# (the bootrom requires it) and only gets 1 checked.
#   Run by .github/workflows/build.yml, release.yml and tools/release.sh.
#   tools/check_xip_setup.sh path/to/ds5-bridge.elf [EXPECT_CLKDIV [EXPECT_VCODE]]
set -euo pipefail
ELF="${1:?usage: tools/check_xip_setup.sh ELF [EXPECT_CLKDIV [EXPECT_VCODE]]}"
EXPECT="${2:-}"
EXPECT_VCODE="${3:-}"
NM="${NM:-arm-none-eabi-nm}"
READELF="${READELF:-arm-none-eabi-readelf}"
OBJDUMP="${OBJDUMP:-arm-none-eabi-objdump}"
SYMS="$("$NM" "$ELF")"
has() { awk -v s="$1" '$3 == s {f=1} END {exit !f}' <<<"$SYMS"; }

# boot2's own size, from the linker's start/end symbols: the section's size can
# include alignment padding before .rodata (0x104 when .text ends 4 bytes off an
# 8-byte boundary), which isn't boot2 and isn't limited to 256 bytes.
b2s="$(awk '$3 == "__boot2_start__" {print $1; exit}' <<<"$SYMS")"
b2e="$(awk '$3 == "__boot2_end__" {print $1; exit}' <<<"$SYMS")"
size=""
[ -n "$b2s" ] && [ -n "$b2e" ] && size="$(printf '%x' $(( 16#$b2e - 16#$b2s )))"
if [ -z "$size" ] || (( 16#$size == 0 || 16#$size > 256 )); then
  echo "check_xip_setup: .boot2 is missing or empty (size '${size:-none}'): no embedded XIP setup" >&2
  exit 1
fi

# RP2350 only from here: its SRAM ends at 0x20082000, the RP2040's at 0x20042000.
top="$(awk '$3 == "__StackTop" {print $1; exit}' <<<"$SYMS")"
if [ -z "$top" ] || (( 16#$top <= 0x20042000 )); then
  echo "check_xip_setup: boot2 present (RP2040: the bootrom always runs it)"
  exit 0
fi
for s in __boot2_entry_point _copy_xip_setup _call_xip_setup; do
  if ! has "$s"; then
    echo "check_xip_setup: $s not found: the XIP setup is not run at boot" >&2
    exit 1
  fi
done

# boot2_w25q080.S: INIT_M0_TIMING = 1 << COOLDOWN(30) | RXDELAY << 8 | CLKDIV,
# loaded from its literal pool. Little-endian words of the section, in order.
clkdiv="$("$OBJDUMP" -s -j .boot2 "$ELF" | awk '
  function hex(h,   i, v) { v = 0; for (i = 1; i <= length(h); i++) v = v * 16 + index("0123456789abcdef", substr(h, i, 1)) - 1; return v }
  /^ [0-9a-f]+ / && d == "" { for (i = 2; i <= 5; i++) if (length($i) == 8 && d == "") {
      v = hex(substr($i,7,2) substr($i,5,2) substr($i,3,2) substr($i,1,2))
      # bits 31..11 must read 0x40000000 (COOLDOWN 1, nothing else), CLKDIV != 0
      if (int(v / 2048) == 0x40000000 / 2048 && v % 256 != 0) d = v % 256 } }
  END { print d }')"
if [ -z "$clkdiv" ]; then
  echo "check_xip_setup: no QMI M0_TIMING word found in .boot2" >&2
  exit 1
fi
if [ -n "$EXPECT" ] && [ "$clkdiv" != "$EXPECT" ]; then
  echo "check_xip_setup: boot2 flash clock divider is $clkdiv, expected $EXPECT" >&2
  exit 1
fi
# main()'s first call must be vreg_set_voltage(code), the code a movs to r0.
vcode="$("$OBJDUMP" -d --no-show-raw-insn "$ELF" | awk '
  /^[0-9a-f]+ <main>:/ { f = 1; next }
  f && /^$/ { f = 0 }
  f && /\tmovs\tr0, #[0-9]+/ { split($0, a, "#"); r0 = a[2] + 0 }
  f && /\tbl\t/ { if ($0 ~ /<vreg_set_voltage>/ && r0 != "") v = r0; f = 0 }
  END { print v }')"
if [ -z "$vcode" ]; then
  echo "check_xip_setup: main() does not start with vreg_set_voltage(<constant>)" >&2
  exit 1
fi
if [ -n "$EXPECT_VCODE" ] && [ "$vcode" != "$EXPECT_VCODE" ]; then
  echo "check_xip_setup: main() sets core voltage code $vcode, expected $EXPECT_VCODE" >&2
  exit 1
fi
echo "check_xip_setup: embedded XIP setup runs at boot, flash clock divider $clkdiv ($((16#$size)) bytes); main() sets core voltage code $vcode"
