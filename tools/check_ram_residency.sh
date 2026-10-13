#!/usr/bin/env bash
# Core1's audio path must run from RAM (CLAUDE.md, "Core1's audio path must
# stay in RAM"): checks a built ELF for the two ways that broke before.
#   1. No __memcpy_chk / __memset_chk / __memmove_chk (libopus fortify put
#      them in flash, so core1 left RAM every frame).
#   2. Core1's entry, its frame functions, the libopus encode/decode entry
#      points, the two WDL_Resampler hot methods, the SDK queue ops and our
#      memcpy/memset/memmove all sit in SRAM (0x20000000-0x20080000).
#      A symbol the build doesn't have (the Pico W build has no speaker
#      encoder) is skipped; the always-present ones must exist.
#   3. Issue #15 diagnostic builds (CMake ISSUE15_DIAG, which link
#      c1diag_snapshot) on RP2350: core1's HardFault recorder (the handler and
#      the function it records with) must be in RAM too, since it runs when
#      flash may be what failed.
#   Run by .github/workflows/build.yml and release.yml after each build.
#   tools/check_ram_residency.sh path/to/ds5-bridge.elf
set -euo pipefail
ELF="${1:?usage: tools/check_ram_residency.sh ELF}"
NM="${NM:-arm-none-eabi-nm}"
SYMS="$("$NM" "$ELF")"
bad=0
chk="$(awk '$3 ~ /^__mem(cpy|set|move)_chk$/ {print $3}' <<<"$SYMS" | sort -u)"
if [ -n "$chk" ]; then
  echo "check_ram_residency: fortify helpers linked (they run from flash): $chk" >&2
  bad=1
fi
# speaker_proc / mic_proc are inlined into core1_entry. memcpy / memset are
# the SDK's ROM wrappers on RP2040 (no symbol of that name), ours on RP2350.
REQUIRED="_Z11core1_entryv memmove queue_try_add queue_try_remove opus_decode celt_decode_with_ec"
OPTIONAL="memcpy memset opus_encode_float opus_encode_native celt_encode_with_ec
  _ZN13WDL_Resampler11ResampleOutEPfiii _ZN13WDL_Resampler15ResamplePrepareEiiPPf"
ATTRS="$("${READELF:-arm-none-eabi-readelf}" -A "$ELF")"
if grep -q ' _Z15c1diag_snapshotP14C1DiagSnapshot$' <<<"$SYMS" && grep -q 'Tag_CPU_arch: v8-M' <<<"$ATTRS"; then
  REQUIRED="$REQUIRED c1diag_hardfault_handler c1diag_fault_record"
fi
for sym in $REQUIRED $OPTIONAL; do
  addr="$(awk -v s="$sym" '$3 == s && $2 ~ /^[tTwW]$/ {print $1; exit}' <<<"$SYMS")"
  if [ -z "$addr" ]; then
    case " $REQUIRED " in *" $sym "*) echo "check_ram_residency: $sym not found" >&2; bad=1 ;; esac
    continue
  fi
  a=$((16#$addr))
  if (( a < 0x20000000 || a >= 0x20080000 )); then
    echo "check_ram_residency: $sym is at 0x$addr, not in RAM" >&2
    bad=1
  fi
done
[ "$bad" = 0 ] && echo "check_ram_residency: core1's audio path is RAM-resident"
exit "$bad"
