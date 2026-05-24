#!/usr/bin/env python3
"""
Host-side dev helper to exercise the firmware button-remap path over the
already-declared 0xF6 (SET) / 0xF7 (GET) vendor feature reports — no web tool
needed. Linux only (reads /dev/hidraw directly), same role as mic_diag.sh.

The remap rides 0xF6/0xF7 with a magic+version frame (see src/cmd.cpp):
  SET 0xF6: [func=0x10]['R']['M'][ver][table[16]]
  GET 0xF7: <Config_body(35)> ['R']['M'][ver][rev_lo][rev_hi][table[16]]

Usage:
  remap_test.py get                 # show revision + current table
  remap_test.py swap CIRCLE CROSS   # swap two buttons (and persist)
  remap_test.py set SRC TGT         # route SRC -> TGT (one-way)
  remap_test.py off SRC             # disable SRC (0xFF)
  remap_test.py reset               # identity (no remap)

Run with sudo if /dev/hidraw needs root.
"""
import fcntl, glob, struct, sys

VID, PID = 0x054c, 0x0ce6
CONFIG_LEN = 35          # sizeof(Config_body), see src/config.h
PROTO_VER = 1            # kRemapProtoVer
COUNT = 16               # kRemapCount

# Must match RemapButton order in src/remap.cpp.
NAMES = ["L2", "L1", "CREATE", "DPAD_UP", "DPAD_LEFT", "DPAD_DOWN",
         "DPAD_RIGHT", "L3", "R2", "R1", "OPTIONS", "TRIANGLE",
         "CIRCLE", "CROSS", "SQUARE", "R3"]
IDX = {n: i for i, n in enumerate(NAMES)}


def hidiocg(size):  # HIDIOCGFEATURE(size)
    return (3 << 30) | (size << 16) | (ord('H') << 8) | 0x07


def hidiocs(size):  # HIDIOCSFEATURE(size)
    return (3 << 30) | (size << 16) | (ord('H') << 8) | 0x06


def read_f7(f):
    """Return (rev, table[16]) or None if the response lacks the remap block."""
    size = 64  # 1 report-id byte + up to 63 payload
    buf = bytearray(size)
    buf[0] = 0xF7
    fcntl.ioctl(f, hidiocg(size), buf)
    payload = bytes(buf[1:])  # kernel prepends report id at byte 0
    blk = payload[CONFIG_LEN:CONFIG_LEN + 5 + COUNT]
    if len(blk) < 5 + COUNT or blk[0] != ord('R') or blk[1] != ord('M'):
        return None
    ver = blk[2]
    rev = blk[3] | (blk[4] << 8)
    table = list(blk[5:5 + COUNT])
    return ver, rev, table


def find_dongle():
    for path in sorted(glob.glob('/dev/hidraw*')):
        try:
            f = open(path, 'rb+', buffering=0)
        except (OSError, PermissionError):
            continue
        try:
            if read_f7(f) is not None:
                return f, path
        except OSError:
            pass
        f.close()
    return None, None


def write_table(f, table):
    assert len(table) == COUNT
    payload = bytes([0xF6, 0x10, ord('R'), ord('M'), PROTO_VER]) + bytes(table)
    buf = bytearray(payload)
    fcntl.ioctl(f, hidiocs(len(buf)), buf)


def show(label, ver, rev, table):
    print(f"{label}: proto v{ver}, revision {rev}")
    active = [(s, t) for s, t in enumerate(table) if t != s]
    if not active:
        print("  identity (no remap active)")
        return
    for s, t in active:
        tn = "DISABLED" if t == 0xFF else NAMES[t]
        print(f"  {NAMES[s]:<10} -> {tn}")


def parse_button(arg):
    key = arg.upper()
    if key not in IDX:
        sys.exit(f"unknown button '{arg}'. choices: {', '.join(NAMES)}")
    return IDX[key]


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    cmd = sys.argv[1].lower()

    f, path = find_dongle()
    if f is None:
        sys.exit("no DS5 dongle with remap support found "
                 "(check /dev/hidraw permissions and firmware version)")
    print(f"dongle: {path}")
    ver, rev, table = read_f7(f)

    if cmd == "get":
        show("current", ver, rev, table)
        return

    if cmd == "reset":
        table = list(range(COUNT))
    elif cmd == "off" and len(sys.argv) == 3:
        table[parse_button(sys.argv[2])] = 0xFF
    elif cmd == "set" and len(sys.argv) == 4:
        table[parse_button(sys.argv[2])] = parse_button(sys.argv[3])
    elif cmd == "swap" and len(sys.argv) == 4:
        a, b = parse_button(sys.argv[2]), parse_button(sys.argv[3])
        table[a], table[b] = b, a
    else:
        print(__doc__)
        sys.exit(2)

    show("writing", ver, rev, table)
    write_table(f, table)
    nver, nrev, ntable = read_f7(f)
    show("read-back", nver, nrev, ntable)
    if ntable == table and nrev != rev:
        print("OK: table applied and revision bumped")
    else:
        print("WARNING: read-back mismatch or revision did not change")


if __name__ == "__main__":
    main()
