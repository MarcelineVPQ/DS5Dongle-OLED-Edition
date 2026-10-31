#!/usr/bin/env python3
"""The firmware and the app agree on the configurator protocol (rule 10).

    tools/check_protocol_lockstep.py APP_REPO

Reads this repo's src/config.h (Config_body, packed) and src/cmd.h
(PICO_PROTO_MAJOR / MINOR), and the app's src/protocol/config.ts (its
"offset N type name" layout table and its encoder's view.setUint8 /
setFloat32 calls at literal offsets) and src/protocol/bridgeClient.ts
(SUPPORTED_PROTO_MAJOR, PROTOCOL_MINOR_KNOWN). Fails when:

  - a field the app knows sits at another offset or has another type here,
    or the app knows a field this firmware doesn't have;
  - the app encodes a field at an offset that isn't that field's here;
  - the major versions differ, or the app knows a newer minor than this
    firmware speaks.

Protocol changes land in the firmware first, so fields and a minor the app
doesn't know yet are listed but pass; the app's own test
(tests/unit/protocolLockstep.test.ts, with DS5_FIRMWARE_REPO) requires the
minors to be equal. Standard library only. Exit 0 = agree, 1 = not, 2 = usage.
"""
import re
import sys
from pathlib import Path

FW = Path(__file__).resolve().parent.parent
SIZES = {"uint8_t": (1, "uint8"), "float": (4, "float32")}


def firmware_body():
    text = (FW / "src" / "config.h").read_text()
    m = re.search(r"struct\s+__attribute__\(\(packed\)\)\s+Config_body\s*\{(.*?)\n\};", text, re.S)
    if not m:
        sys.exit("config.h: no packed Config_body")
    fields, off = {}, 0
    # loop: NOT-WORTH — tool, ~40 struct lines
    for line in m.group(1).splitlines():
        line = line.split("//")[0].strip()
        if not line:
            continue
        d = re.fullmatch(r"(uint8_t|float)\s+(\w+)(?:\[(\d+)\])?;", line)
        if not d:
            sys.exit(f"config.h: Config_body line not understood: {line!r}")
        size, kind = SIZES[d.group(1)]
        n = int(d.group(3) or 1)
        fields[d.group(2)] = (off, kind + (f"[{n}]" if d.group(3) else ""))
        off += size * n
    return fields, off


def define(text, name):
    m = re.search(rf"^#define\s+{name}\s+(\d+)\b", text, re.M)
    return int(m.group(1)) if m else None


def const(text, name):
    m = re.search(rf"export const {name}\s*=\s*(\d+)\b", text)
    return int(m.group(1)) if m else None


def camel_to_snake(name):
    return re.sub(r"(?<=[a-z0-9])([A-Z])", r"_\1", name).lower()


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2
    app = Path(argv[1])
    config_ts = (app / "src" / "protocol" / "config.ts").read_text()
    bridge_ts = (app / "src" / "protocol" / "bridgeClient.ts").read_text()
    cmd_h = (FW / "src" / "cmd.h").read_text()
    fields, size = firmware_body()
    by_offset = {off: name for name, (off, _) in fields.items()}
    problems, notes = [], []

    table = re.findall(r"^//\s+offset\s+(\d+)\s+(\S+)\s+(\w+)", config_ts, re.M)
    if not table:
        problems.append("config.ts: no 'offset N type name' table found")
    app_names = set()
    # loop: NOT-WORTH — tool, one row per config field
    for off, kind, name in table:
        app_names.add(name)
        if name not in fields:
            problems.append(f"config.ts lists {name} (offset {off}), Config_body has no such field")
        elif fields[name] != (int(off), kind):
            problems.append(f"{name}: app offset {off} {kind}, firmware offset {fields[name][0]} {fields[name][1]}")
    # loop: NOT-WORTH — tool, one row per config field
    for name in fields:
        if name not in app_names:
            notes.append(f"Config_body.{name} is not in the app's table yet (firmware first)")

    # The encoder: view.setX(<literal offset>, config.<camelName> ...)
    enc = re.findall(r"view\.set(Uint8|Float32)\((\d+),\s*config\.(\w+)", config_ts)
    # loop: NOT-WORTH — tool, one line per encoded field
    for kind, off, camel in enc:
        name = camel_to_snake(camel)
        if name in fields and fields[name][0] != int(off):
            problems.append(f"config.ts encodes {camel} at {off}, firmware {name} is at {fields[name][0]}")
        elif name not in fields and by_offset.get(int(off)) is None:
            problems.append(f"config.ts encodes {camel} at {off}, no firmware field starts there")

    fw_major, fw_minor = define(cmd_h, "PICO_PROTO_MAJOR"), define(cmd_h, "PICO_PROTO_MINOR")
    app_major, app_minor = const(bridge_ts, "SUPPORTED_PROTO_MAJOR"), const(bridge_ts, "PROTOCOL_MINOR_KNOWN")
    if None in (fw_major, fw_minor, app_major, app_minor):
        problems.append(f"protocol version not found (cmd.h {fw_major}.{fw_minor}, app {app_major}.{app_minor})")
    else:
        if fw_major != app_major:
            problems.append(f"protocol major: firmware {fw_major}, app {app_major}")
        if app_minor > fw_minor:
            problems.append(f"the app knows protocol 1.{app_minor}, this firmware speaks 1.{fw_minor}")
        elif app_minor < fw_minor:
            notes.append(f"the app knows protocol 1.{app_minor}, the firmware speaks 1.{fw_minor} (firmware first)")

    # loop: NOT-WORTH — tool, a few lines of output
    for n in notes:
        print(f"  note: {n}")
    if problems:
        print(f"check_protocol_lockstep: {len(problems)} disagreement(s) between the firmware and the app:")
        # loop: NOT-WORTH — tool, a few lines of output
        for p in problems:
            print(f"  {p}")
        return 1
    print(f"check_protocol_lockstep: {len(table)} config fields, {len(enc)} encoder writes and protocol "
          f"{fw_major}.{fw_minor} agree ({size}-byte body)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
