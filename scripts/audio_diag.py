#!/usr/bin/env python3
"""Read the dongle's audio pipeline counters (0xF5 feature report) over hidraw.

  scripts/audio_diag.py            one snapshot
  scripts/audio_diag.py SECONDS    two snapshots SECONDS apart, plus deltas

Issue #15 diagnostic firmware (+diag-D / -F1 / -F3) also answers 0xEA (core1
crash record, heartbeat, stack high-water, flash setup); it is printed after
the counters when the firmware has it.
"""
import fcntl, glob, os, struct, sys, time

FIELDS = ["usb_frames", "bt_out", "drop_raw", "drop_enc", "drop_hap",
          "c1_loops", "c1_enc", "raw_push", "hap_push", "mic_frames", "mic_plc",
          "flash_wr", "flash_fail", "setfeat_n", "setfeat_last"]
# Then core1's health: [60] flags (1 started, 2 speaker encoder failed, 4 mic
# decoder failed, bits 3..6 core1's start-up stage, 0x80 = this layout),
# [61..62] u16 free heap at core1 start in KiB. Firmware 0.9.4 to 0.9.5-dev.5
# had no 0x80 bit, no stage and no heap (it fell off the 63-byte reply).
# The stage is the last start-up step core1 reached (src/audio.cpp); a core1
# stuck at stage N stopped in the step after it.
STAGES = {
    0: "never entered core1_entry",
    1: "entered; stopped registering for flash lockout",
    2: "flash lockout registered; stopped starting its cycle counter",
    3: "cycle counter on; stopped reading the free heap",
    4: "stopped creating the speaker encoder",
    5: "speaker encoder created; stopped setting it / the resampler up",
    6: "speaker ready; stopped before the mic decoder",
    7: "stopped creating the mic decoder",
    8: "mic decoder created; stopped before its loop",
    9: "running its loop",
    # Issue #15 diagnostic builds only: where in its loop core1 is (or stopped).
    10: "in its loop: reading the speaker queue",
    11: "in its loop: resampling a speaker chunk",
    12: "in its loop: Opus-encoding a speaker chunk",
    13: "in its loop: speaker chunk queued",
    14: "in its loop: mic decode / concealment",
    15: "in its loop: idle wait",
}
SIZE = 1 + 63

def stage_text(st):
    return "n/a" if st is None else "%d (%s)" % (st, STAGES.get(st, "unknown"))

def find_dongle():
    for d in sorted(glob.glob("/sys/class/hidraw/hidraw*")):
        try:
            ue = open(os.path.join(d, "device/uevent")).read().upper()
        except OSError:
            continue
        if "054C" in ue and ("0CE6" in ue or "0DF2" in ue):  # DualSense or Edge
            return "/dev/" + os.path.basename(d)
    sys.exit("dongle hidraw not found")

def query(dev):
    buf = bytearray(SIZE); buf[0] = 0xF5
    hidiocgfeature = 0xC0004807 | (SIZE << 16)  # _IOC(RW, 'H', 0x07, SIZE)
    with open(dev, "rb+", buffering=0) as f:
        fcntl.ioctl(f, hidiocgfeature, buf)
    out = dict(zip(FIELDS, struct.unpack_from("<%dI" % len(FIELDS), buf, 1)))
    flags = buf[61]
    out["c1_flags"] = flags & 0x07
    out["c1_stage"] = (flags >> 3) & 0x0F if flags & 0x80 else None
    out["c1_free_kib"] = struct.unpack_from("<H", buf, 62)[0] if flags & 0x80 else None
    return out

# GET 0xEA, issue #15 diagnostic builds (src/c1_diag.h).
CFSR_BITS = [  # (bit, name)
    (0, "IACCVIOL"), (1, "DACCVIOL"), (3, "MUNSTKERR"), (4, "MSTKERR"), (5, "MLSPERR"), (7, "MMARVALID"),
    (8, "IBUSERR"), (9, "PRECISERR"), (10, "IMPRECISERR"), (11, "UNSTKERR"), (12, "STKERR"),
    (13, "LSPERR"), (15, "BFARVALID"),
    (16, "UNDEFINSTR"), (17, "INVSTATE"), (18, "INVPC"), (19, "NOCP"), (20, "STKOF"),
    (24, "UNALIGNED"), (25, "DIVBYZERO"),
]
HFSR_BITS = [(1, "VECTTBL"), (30, "FORCED"), (31, "DEBUGEVT")]
SFSR_BITS = [(0, "INVEP"), (1, "INVIS"), (2, "INVER"), (3, "AUVIOL"), (4, "INVTRAN"),
             (5, "LSPERR"), (6, "SFARVALID"), (7, "LSERR")]
DIAG_FLAGS = [(0, "core1 faulted"), (1, "fault recorder"), (2, "stack guard"),
              (3, "F1 XIP setup"), (4, "F3 busy idle"), (5, "RP2350")]

def bits(v, table):
    on = [n for b, n in table if v >> b & 1]
    return "%#010x%s" % (v, (" " + " ".join(on)) if on else "")

def query_c1diag(dev):
    buf = bytearray(SIZE); buf[0] = 0xEA
    hidiocgfeature = 0xC0004807 | (SIZE << 16)
    try:
        with open(dev, "rb+", buffering=0) as f:
            n = fcntl.ioctl(f, hidiocgfeature, buf)
    except OSError:
        return None
    r = bytes(buf[1:])
    if r[0:2] != b"CF":
        return None  # firmware without the issue #15 diagnostics
    u32 = lambda at: struct.unpack_from("<I", r, at)[0]
    return dict(ver=r[2], flags=r[3], stage_now=r[4], stage_fault=r[5], chip=r[6], rom=r[7],
                pc=u32(8), lr=u32(12), xpsr=u32(16), cfsr=u32(20), hfsr=u32(24), mmfar=u32(28),
                bfar=u32(32), sp=u32(36), fault_us=u32(40), hb_age=u32(44),
                stack_free=struct.unpack_from("<H", r, 48)[0], qmi_timing=u32(50), qmi_rfmt=u32(54),
                qmi_rcmd=struct.unpack_from("<H", r, 58)[0], exc_return=r[60], sfsr=r[61])

def print_c1diag(d, sys_khz=320000):
    print("-- core1 diagnostics (0xEA, layout %d)" % d["ver"])
    print(f"{'flags':11} {d['flags']:#04x} " + ", ".join(n for b, n in DIAG_FLAGS if d['flags'] >> b & 1))
    print(f"{'stage_now':11} {stage_text(d['stage_now'])}")
    hb = d["hb_age"]
    print(f"{'heartbeat':11} " + ("never" if hb == 0xFFFFFFFF else "%d us ago (%.3f s)" % (hb, hb / 1e6)))
    print(f"{'stack_free':11} {d['stack_free']} bytes never touched (of 32768)")
    rp2350 = d["flags"] & 0x20
    print(f"{'chip':11} {'RP2350' if rp2350 else 'RP2040'} chip version {d['chip']}, boot ROM version {d['rom']}")
    if rp2350:
        t = d["qmi_timing"]
        clkdiv = t & 0xFF
        rxdelay = (t >> 8) & 0x7
        cooldown = t >> 30
        mhz = (sys_khz / 1000 / clkdiv) if clkdiv else 0
        print(f"{'qmi_timing':11} {t:#010x} (CLKDIV {clkdiv} = {mhz:.1f} MHz flash at {sys_khz // 1000} MHz, RXDELAY {rxdelay}, COOLDOWN {cooldown})")
        print(f"{'qmi_rfmt':11} {d['qmi_rfmt']:#010x}")
        rc = d["qmi_rcmd"]
        print(f"{'qmi_rcmd':11} {rc:#06x} (read cmd {rc & 0xFF:#04x}, suffix {rc >> 8:#04x})")
    if not d["flags"] & 0x01:
        print(f"{'fault':11} none recorded")
        return
    print(f"{'fault':11} HardFault on core1 at stage {stage_text(d['stage_fault'])}, {d['fault_us'] / 1e6:.3f} s after boot")
    print(f"{'pc':11} {d['pc']:#010x}   (arm-none-eabi-addr2line -e ds5-bridge-oled.elf -f -C {d['pc']:#x})")
    print(f"{'lr':11} {d['lr']:#010x}")
    print(f"{'xpsr':11} {d['xpsr']:#010x} (exception {d['xpsr'] & 0x1FF})")
    print(f"{'sp':11} {d['sp']:#010x}")
    print(f"{'exc_return':11} {d['exc_return']:#04x}")
    print(f"{'cfsr':11} {bits(d['cfsr'], CFSR_BITS)}")
    print(f"{'hfsr':11} {bits(d['hfsr'], HFSR_BITS)}")
    print(f"{'mmfar':11} {d['mmfar']:#010x}" + ("" if d['cfsr'] >> 7 & 1 else " (not valid)"))
    print(f"{'bfar':11} {d['bfar']:#010x}" + ("" if d['cfsr'] >> 15 & 1 else " (not valid)"))
    print(f"{'sfsr':11} {bits(d['sfsr'], SFSR_BITS)}")

dev = find_dongle()
a = query(dev)
if len(sys.argv) < 2:
    for k in FIELDS: print(f"{k:11} {a[k]:#x}" if k == "setfeat_last" else f"{k:11} {a[k]}")
    print(f"{'c1_flags':11} {a['c1_flags']:#x}")
    print(f"{'c1_free_kib':11} {'n/a' if a['c1_free_kib'] is None else a['c1_free_kib']}")
    print(f"{'c1_stage':11} {stage_text(a['c1_stage'])}")
    d = query_c1diag(dev)
    if d: print_c1diag(d)
    sys.exit()
secs = float(sys.argv[1]); time.sleep(secs); b = query(dev)
print(f"{'field':11} {'before':>10} {'after':>10} {'per s':>9}")
for k in FIELDS:
    if k == "setfeat_last": print(f"{k:11} {a[k]:>#10x} {b[k]:>#10x}"); continue
    print(f"{k:11} {a[k]:>10} {b[k]:>10} {(b[k]-a[k])/secs:>9.1f}")
print(f"{'c1_flags':11} {a['c1_flags']:>#10x} {b['c1_flags']:>#10x}")
print(f"{'c1_stage':11} {stage_text(b['c1_stage'])}")
d = query_c1diag(dev)
if d: print_c1diag(d)
