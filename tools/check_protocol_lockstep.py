#!/usr/bin/env python3
"""The firmware and the app agree on the configurator protocol (rule 10).

    tools/check_protocol_lockstep.py APP_REPO

Reads this repo's src/config.h (Config_body, packed), src/cmd.h
(PICO_PROTO_MAJOR / MINOR) and src/cmd.cpp (the 0xF6 function ids it
handles, the identity report's feature bytes), and the app's
src/protocol/config.ts (its "offset N type name" layout table and its
encoder's view.setUint8 / setFloat32 calls at literal offsets) and
src/protocol/bridgeClient.ts (SUPPORTED_PROTO_MAJOR, PROTOCOL_MINOR_KNOWN,
FUNC, the IDENTITY_FEATURES / FEATURES2..4 bit tables and the identity
bytes it reads them from). Fails when:

  - a field the app knows sits at another offset or has another type here,
    or the app knows a field this firmware doesn't have;
  - the app encodes a field at an offset that isn't that field's here;
  - the major versions differ, or the app knows a newer minor than this
    firmware speaks;
  - the app sends a 0xF6 function id (FUNC) this firmware doesn't handle;
  - the app reads a feature table from an identity byte this firmware
    doesn't fill with feature bits, or expects a bit there it never sets.

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


def strip_comments(text):
    return re.sub(r"//[^\n]*", "", text)


def app_table(text, name):
    """An app `export const NAME = { key: 0xNN, ... }` as {key: value}."""
    m = re.search(rf"export const {name}\s*=\s*\{{(.*?)\}}", strip_comments(text), re.S)
    if not m:
        return None
    return {k: int(v, 16) for k, v in re.findall(r"(\w+):\s*(0x[0-9A-Fa-f]+)", m.group(1))}


def firmware_function_ids(cmd_cpp):
    """The 0xF6 function ids cmd.cpp switches on or compares buffer[0] / func with."""
    found = re.findall(r"(?:case\s+|buffer\[0\]\s*==\s*|func\s*==\s*)(0x[0-9A-Fa-f]{2})\b", strip_comments(cmd_cpp))
    return {int(v, 16) for v in found if int(v, 16) < 0x80}


def firmware_feature_bits(cmd_cpp, index):
    """The bits cmd.cpp may set in identity byte `index` (every hex literal of
    its `buffer[index] = ...;`, through a named constant), or None."""
    code = strip_comments(cmd_cpp)
    m = re.search(rf"buffer\[{index}\]\s*=\s*([^;]*);", code)
    if not m:
        return None
    expr = m.group(1).strip()
    if re.fullmatch(r"\w+", expr):
        c = re.search(rf"constexpr\s+uint8_t\s+{expr}\s*=\s*([^;]*);", code)
        if not c:
            return None
        expr = c.group(1)
    bits = 0
    # loop: NOT-WORTH — tool, the literals of one expression
    for v in re.findall(r"0x[0-9A-Fa-f]+", expr):
        bits |= int(v, 16)
    return bits


# The app's feature tables and the BridgeIdentity field each is read into.
FEATURE_TABLES = {"IDENTITY_FEATURES": "features", "FEATURES2": "features2",
                  "FEATURES3": "features3", "FEATURES4": "features4"}


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

    # 0xF6 function ids: every one the app sends, the firmware handles.
    cmd_cpp = (FW / "src" / "cmd.cpp").read_text()
    fw_funcs = firmware_function_ids(cmd_cpp)
    func = app_table(bridge_ts, "FUNC")
    if not func:
        problems.append("bridgeClient.ts: no FUNC table found")
    else:
        # loop: NOT-WORTH — tool, one entry per function
        for key, fid in func.items():
            if fid not in fw_funcs:
                problems.append(f"the app's FUNC.{key} is 0x{fid:02X}, cmd.cpp handles no such 0xF6 function")
        # loop: NOT-WORTH — tool, one entry per function
        for fid in sorted(fw_funcs - set(func.values())):
            notes.append(f"0xF6 function 0x{fid:02X} is not in the app's FUNC (firmware first, or sent from elsewhere)")

    # Identity feature bytes: where the app reads each table, and its bits.
    checked_bits = 0
    # loop: NOT-WORTH — tool, four tables
    for table_name, field in FEATURE_TABLES.items():
        bits = app_table(bridge_ts, table_name)
        at = re.search(rf"\b{field}:[^\n]*?bytes\[(\d+)\]\s*(?::\s*0\s*)?,", bridge_ts)
        if bits is None or not at:
            problems.append(f"bridgeClient.ts: {table_name} or where {field} is read not found")
            continue
        index = int(at.group(1))
        fw_bits = firmware_feature_bits(cmd_cpp, index)
        if fw_bits is None:
            problems.append(f"the app reads {table_name} from identity [{index}], cmd.cpp sets no feature bits there")
            continue
        # loop: NOT-WORTH — tool, one entry per feature bit
        for key, bit in bits.items():
            checked_bits += 1
            if bit & ~fw_bits:
                problems.append(f"the app's {table_name}.{key} is bit 0x{bit:02X} of identity [{index}], "
                                f"which this firmware never sets (it sets 0x{fw_bits:02X})")

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
    print(f"check_protocol_lockstep: {len(table)} config fields, {len(enc)} encoder writes, "
          f"{len(func or {})} function ids, {checked_bits} feature bits and protocol "
          f"{fw_major}.{fw_minor} agree ({size}-byte body)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
