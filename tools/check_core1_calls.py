#!/usr/bin/env python3
"""Core1's RAM code must not call into flash, except for the calls listed here.

check_ram_residency.sh checks that core1's functions sit in RAM, by name. This
checks what they call: it disassembles the ELF, walks the direct calls and
branches from core1_entry through every RAM function it reaches, and fails on
any call into flash (0x10000000-0x11000000, directly or through a linker
veneer) whose target isn't in ALLOWED below. A new flash call on core1's path
is exactly what made the speaker crackle before (CLAUDE.md, "Core1's audio
path must stay in RAM"): core0's traffic evicts the XIP cache.

Indirect calls (function pointers, e.g. libopus's per-arch tables) aren't
followed: a target only ever called through a pointer isn't seen.

  tools/check_core1_calls.py path/to/ds5-bridge.elf   (OBJDUMP=... to override)

Run by tools/check_ram_residency.sh.
"""
import fnmatch
import os
import re
import subprocess
import sys

ENTRY = "_Z11core1_entryv"

# Flash functions core1 may call today, each with the reason it's tolerated.
# Add one only with its reason; better, move the function to RAM.
ALLOWED = {
    # Once, at core1's start (before its loop).
    "flash_safe_execute_core_init": "start-up: registers core1 for flash lockout",
    "mallinfo": "start-up: free heap for the 0xF5 health bytes",
    "sbrk": "start-up: free heap for the 0xF5 health bytes",
    "__wrap_printf": "start-up failure messages (encoder/decoder create); no serial in release builds",
    "_ZN13WDL_Resampler7SetModeEbibii": "start-up: resampler set-up",
    "_ZN13WDL_Resampler8SetRatesEdd": "start-up: resampler set-up",
    "_ZN13WDL_Resampler8PreallocEiii": "start-up: resampler buffers, sized so they never grow",
    "_ZN13WDL_Resampler12BuildLowPassEPb": "only with the resampler's IIR filter, which is off (SetMode(true, 0, false))",
    "_ZL26wdl_rs_reinterleave_bufferPfiii*": "resampler: only when its channel count changes (first frame)",
    "__wrap_malloc": "opus_*_create at start-up; the resampler only when a buffer grows (Prealloc'd)",
    "__wrap_free": "opus_*_create at start-up; the resampler only when a buffer grows (Prealloc'd)",
    "__wrap_realloc": "the resampler only when a buffer grows (Prealloc'd)",
    "_Znwj": "the resampler only when a buffer grows (Prealloc'd)",
    # Every frame, known (CLAUDE.md "Core1 must stay registered for flash lockout").
    "sleep_us": "core1's idle wait (10 us when there is no work)",
    "__wrap___aeabi_*": "soft-float/double helpers: WDL's double phase, libopus float paths",
    "__wrap___clzsi2": "RP2040: count-leading-zeros helper",
    "__wrap_memcpy": "RP2040: the SDK's wrapper around the ROM memcpy",
    "__wrap_memset": "RP2040: the SDK's wrapper around the ROM memset",
    "__wrap_acos": "libm (libopus)", "__wrap_cos": "libm (libopus, resampler)",
    "__wrap_sin": "libm (resampler)", "__wrap_exp": "libm (libopus)",
    "__wrap_floor": "libm (libopus, resampler)", "__wrap_log": "libm (libopus)",
    "__wrap_log10": "libm (libopus)", "__wrap_pow": "libm (libopus)",
    "__wrap_sqrt": "libm (libopus)",
}

HDR = re.compile(r"^([0-9a-f]{8}) <([^>]+)>:$")
INS = re.compile(r"^\s*[0-9a-f]+:\s+(b\S*)\s+[0-9a-f]+ <([^>+]+)(?:\+0x[0-9a-f]+)?>")
VENEER = re.compile(r"^__(.+)_veneer$")


def in_ram(a):
    return 0x20000000 <= a < 0x20082000


def in_flash(a):
    return 0x10000000 <= a < 0x11000000


def main():
    if len(sys.argv) != 2:
        print("usage: tools/check_core1_calls.py ELF", file=sys.stderr)
        return 2
    objdump = os.environ.get("OBJDUMP", "arm-none-eabi-objdump")
    dis = subprocess.run([objdump, "-d", "--no-show-raw-insn", sys.argv[1]], check=True,
                         capture_output=True, text=True).stdout
    addr, calls, fn = {}, {}, None
    for line in dis.splitlines():
        m = HDR.match(line)
        if m:
            fn = m.group(2)
            addr[fn] = int(m.group(1), 16)
            calls.setdefault(fn, set())
            continue
        m = INS.match(line)
        if m and fn and m.group(2) != fn:
            calls[fn].add(m.group(2))
    if ENTRY not in addr:
        print(f"check_core1_calls: {ENTRY} not found", file=sys.stderr)
        return 1
    seen, todo, flash = set(), [ENTRY], {}
    while todo:
        f = todo.pop()
        if f in seen:
            continue
        seen.add(f)
        for t in calls.get(f, ()):
            v = VENEER.match(t)
            if v:  # a veneer in RAM jumping to its target (flash when it's needed at all)
                target = v.group(1)
                if target in addr and in_ram(addr[target]):
                    todo.append(target)
                else:
                    flash.setdefault(target, set()).add(f)
                continue
            a = addr.get(t)
            if a is None:
                continue
            if in_ram(a):
                todo.append(t)
            elif in_flash(a):
                flash.setdefault(t, set()).add(f)
    bad = [t for t in sorted(flash) if not any(fnmatch.fnmatchcase(t, p) for p in ALLOWED)]
    for t in bad:
        print(f"check_core1_calls: core1 calls {t} in flash (from {', '.join(sorted(flash[t]))}); "
              "move it to RAM, or add it to ALLOWED in tools/check_core1_calls.py with its reason",
              file=sys.stderr)
    if not bad:
        print(f"check_core1_calls: {len(seen)} RAM functions reached from core1_entry; "
              f"{len(flash)} flash calls, all on the allowed list")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
