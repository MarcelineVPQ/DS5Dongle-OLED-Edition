#!/usr/bin/env python3
"""ButtonPart test vectors shared by the firmware and the app (protocol 1.24).

Writes vectors.txt next to this file: one line per vector,
    name expect needs crc32 part
expect  "ok" or "bad": what docs/TENSION_BUTTONS.md section 3.5 says of the
        part, set by hand per vector below (each "bad" one breaks the rule its
        name gives), not computed by any validator;
needs   the identity [58] bits the part needs (section 6), hex, by hand ("-"
        for a bad part);
crc32   zlib's CRC-32 of the 360 part bytes, hex (the codec check);
part    the 360 bytes, hex.
The firmware's tests/host/test_buttons.cpp checks src/buttons.h against every
line; the app copies the file for its codec and validator tests.
Run: python3 tests/host/fixtures/buttons/gen_vectors.py
"""
import os
import zlib

POOL = 308


def part(used=True, bver=1, bflags=None, layers=0, dw=25, pool=b"", pool_len=None,
         layer=None, gyro=None, tail=None):
    """The 360 bytes. `layer`: up to 3 (name, colour, lflags); `tail`:
    (offset, byte) pokes after building, for the broken ones."""
    if not used:
        b = bytearray(360)
    else:
        b = bytearray(360)
        b[0] = bver
        b[1] = (0x01 | (0x02 if gyro else 0)) if bflags is None else bflags
        b[2] = layers
        b[3] = dw
        n = len(pool) if pool_len is None else pool_len
        b[4], b[5] = n & 0xFF, n >> 8
        for i, (name, colour, lflags) in enumerate(layer or []):
            o = 6 + 12 * i
            nm = name.encode("ascii") if isinstance(name, str) else name
            b[o:o + len(nm)] = nm
            b[o + 10], b[o + 11] = colour, lflags
        if gyro:
            b[42:42 + len(gyro)] = bytes(gyro)
        b[52:52 + len(pool)] = pool
    for off, v in tail or []:
        b[off] = v
    return bytes(b)


def e(src, rows, *targets, hold=None, turbo=None, layer=0):
    """One pool entry: [src | layer << 5, rows, targets..., hold?, turbo?]."""
    out = bytes([src | layer << 5, rows])
    for t in targets:
        out += bytes(t)
    if hold is not None:
        out += bytes([hold])
    if turbo is not None:
        out += bytes([turbo])
    return out


BTN = lambda a: (1, a, 0)
OFF = (0, 0, 0)
KEY = lambda a, mods=0: (2, a, mods)
MB = lambda a: (3, a, 0)
MOVE = lambda a, speed: (4, a, speed)
LAYER = lambda a, switch=0: (5, a, switch)
GYRO = lambda a: (6, a, 0)
P, H, D, W, T, U = 0x01, 0x02, 0x04, 0x08, 0x10, 0x20  # Press, Hold, Double press, With PS, Toggle, Turbo
# Sources: 0 L2, 1 L1, 2 Create, 3 Up, 4 Left, 5 Down, 6 Right, 7 L3, 8 R2, 9 R1,
# 10 Options, 11 Triangle, 12 Circle, 13 Cross, 14 Square, 15 R3, 16 Touch pad,
# 17 PS, 18 Fn left, 19 Fn right, 20 left back, 21 right back.
GY = [1, 0, 0, 64, 64, 8]

V = [
    # --- valid ---------------------------------------------------------
    ("not_used_all_zero", "ok", 0x00, part(used=False)),
    ("used_no_changes", "ok", 0x01, part()),
    ("cross_circle_swap", "ok", 0x01, part(pool=e(12, P, BTN(13)) + e(13, P, BTN(12)))),
    ("square_disabled", "ok", 0x01, part(pool=e(14, P, OFF))),
    ("left_back_button_is_cross", "ok", 0x01, part(pool=e(20, P, BTN(13)))),
    ("touch_pad_is_ps", "ok", 0x01, part(pool=e(16, P, BTN(17)))),
    ("every_legacy_button_moved", "ok", 0x01, part(pool=b"".join(e(s, P, BTN(15 - s)) for s in range(16)))),
    ("cross_hold_is_triangle", "ok", 0x03, part(pool=e(13, H, BTN(11), hold=30))),
    ("cross_toggle_turbo", "ok", 0x03, part(pool=e(13, P | T | U, BTN(13), turbo=10))),
    ("toggle_without_press_row", "ok", 0x03, part(pool=e(13, T))),
    ("ps_hold_and_double", "ok", 0x03, part(pool=e(17, H | D, BTN(11), BTN(12), hold=30))),
    ("r1_is_f5", "ok", 0x11, part(pool=e(9, P, KEY(0x3E)))),
    ("r1_is_ctrl_only", "ok", 0x11, part(pool=e(9, P, KEY(0, 0x01)))),
    ("r1_is_mouse_left", "ok", 0x11, part(pool=e(9, P, MB(1)))),
    ("l2_moves_mouse_up", "ok", 0x31, part(pool=e(0, P, MOVE(0, 10)))),
    ("wheel_down_30", "ok", 0x31, part(pool=e(0, P, MOVE(5, 30)))),
    ("l1_holds_layer_1", "ok", 0x05, part(layers=1, layer=[("Vehicle", 2, 0)],
                                          pool=e(1, P, LAYER(1)) + e(13, P, BTN(12), layer=1))),
    ("l1_switches_layer_1_and_back", "ok", 0x05, part(layers=1, layer=[("", 0, 0)],
                                                      pool=e(1, P, LAYER(1, 1)) + e(13, P, BTN(12), layer=1))),
    ("l1_hold_enters_layer", "ok", 0x07, part(layers=1, layer=[("Right side", 5, 0)],
                                              pool=e(1, P | H, BTN(9), LAYER(1), hold=30))),
    ("gyro_mouse", "ok", 0x21, part(gyro=GY)),
    ("gyro_right_stick_with_button", "ok", 0x21, part(gyro=[2, 1, 0x07, 1, 255, 0], pool=e(15, P, GYRO(0)))),
    ("three_layers_gyro_off_in_one", "ok", 0x25, part(gyro=GY, layers=3,
                                                      layer=[("A", 0, 0), ("B", 1, 1), ("C", 2, 2)],
                                                      pool=e(1, P, LAYER(1)) + e(7, P, LAYER(3)) + e(9, P, LAYER(2)))),
    ("with_ps_on_cross", "ok", 0x03, part(pool=e(13, W, BTN(10)))),
    ("hold_edges_15_and_200", "ok", 0x03, part(pool=e(12, H, BTN(11), hold=15) + e(13, H, BTN(11), hold=200))),
    ("pool_61_press_entries", "ok", 0x05, part(layers=2, layer=[("A", 0, 0), ("B", 1, 0)],
                                               pool=b"".join(e(x, P, BTN(0)) for x in range(22) if x != 17) +
                                               b"".join(e(x, P, BTN(0), layer=1) for x in range(22) if x != 17) +
                                               b"".join(e(x, P, BTN(0), layer=2) for x in range(20) if x != 17))),
    # --- invalid, one rule each ------------------------------------------
    ("r1_not_used_but_bytes", "bad", None, part(used=False, tail=[(3, 25)])),
    ("r1_bver_2", "bad", None, part(bver=2)),
    ("r1_bver_0", "bad", None, part(bver=0)),
    ("r1_reserved_flag", "bad", None, part(bflags=0x05)),
    ("r2_layers_4", "bad", None, part(layers=4)),
    ("r2_double_window_14", "bad", None, part(dw=14)),
    ("r2_double_window_51", "bad", None, part(dw=51)),
    ("r2_pool_len_309", "bad", None, part(pool_len=309)),
    ("r2_byte_past_pool_len", "bad", None, part(pool=e(13, P, BTN(12)), tail=[(52 + 5, 1)])),
    ("r2_unused_layer_not_zero", "bad", None, part(layers=0, layer=[("", 1, 0)])),
    ("r2_layer_name_0x7f", "bad", None, part(layers=1, layer=[(b"A\x7f", 0, 0)], pool=e(1, P, LAYER(1)))),
    ("r2_layer_name_after_nul", "bad", None, part(layers=1, layer=[(b"A\x00B", 0, 0)], pool=e(1, P, LAYER(1)))),
    ("r2_layer_colour_6", "bad", None, part(layers=1, layer=[("A", 6, 0)], pool=e(1, P, LAYER(1)))),
    ("r2_layer_lflags_3", "bad", None, part(layers=1, layer=[("A", 0, 3)], pool=e(1, P, LAYER(1)))),
    ("r2_gyro_bytes_without_bit", "bad", None, part(bflags=0x01, tail=[(42, 1)])),
    ("r2_gyro_mode_0", "bad", None, part(gyro=[0, 0, 0, 64, 64, 8], bflags=0x03)),
    ("r2_gyro_speed_0", "bad", None, part(gyro=[1, 0, 0, 0, 64, 8])),
    ("r2_gyro_axis_2", "bad", None, part(gyro=[1, 2, 0, 64, 64, 8])),
    ("r2_gyro_flag_bit3", "bad", None, part(gyro=[1, 0, 0x08, 64, 64, 8])),
    ("r3_entry_runs_past_pool_len", "bad", None, part(pool=e(13, P, BTN(12)), pool_len=4)),
    ("r3_source_22", "bad", None, part(pool=e(22, P, BTN(12)))),
    ("r3_layer_not_in_use", "bad", None, part(pool=e(13, P, BTN(12), layer=1))),
    ("r3_not_sorted", "bad", None, part(pool=e(13, P, BTN(12)) + e(12, P, BTN(13)))),
    ("r3_duplicate", "bad", None, part(pool=e(13, P, BTN(12)) + e(13, P, BTN(11)))),
    ("r3_rows_0", "bad", None, part(pool=bytes([13, 0]))),
    ("r3_rows_bit6", "bad", None, part(pool=e(13, P | 0x40, BTN(12)))),
    ("r3_byte0_bit7", "bad", None, part(pool=bytes([13 | 0x80, P, 1, 12, 0]))),
    ("r4_kind_7", "bad", None, part(pool=e(13, P, (7, 0, 0)))),
    ("r4_button_18", "bad", None, part(pool=e(13, P, BTN(18)))),
    ("r4_button_b_not_0", "bad", None, part(pool=e(13, P, (1, 12, 1)))),
    ("r4_nothing_not_zero", "bad", None, part(pool=e(13, P, (0, 1, 0)))),
    ("r4_key_0x03", "bad", None, part(pool=e(9, P, KEY(0x03)))),
    ("r4_key_0xa5", "bad", None, part(pool=e(9, P, KEY(0xA5)))),
    ("r4_key_nothing", "bad", None, part(pool=e(9, P, KEY(0, 0)))),
    ("r4_mouse_button_6", "bad", None, part(pool=e(9, P, MB(6)))),
    ("r4_mouse_button_0", "bad", None, part(pool=e(9, P, MB(0)))),
    ("r4_move_speed_0", "bad", None, part(pool=e(0, P, MOVE(0, 0)))),
    ("r4_move_6", "bad", None, part(pool=e(0, P, MOVE(6, 10)))),
    ("r4_wheel_31", "bad", None, part(pool=e(0, P, MOVE(4, 31)))),
    ("r4_hold_14", "bad", None, part(pool=e(13, H, BTN(11), hold=14))),
    ("r4_hold_201", "bad", None, part(pool=e(13, H, BTN(11), hold=201))),
    ("r4_turbo_1", "bad", None, part(pool=e(13, U, turbo=1))),
    ("r4_turbo_31", "bad", None, part(pool=e(13, U, turbo=31))),
    ("r5_toggle_on_disable", "bad", None, part(pool=e(13, P | T, OFF))),
    ("r5_turbo_on_mouse_move", "bad", None, part(pool=e(13, P | U, MOVE(0, 10), turbo=10))),
    ("r6_ps_press_row", "bad", None, part(pool=e(17, P, BTN(13)))),
    ("r6_ps_with_ps", "bad", None, part(pool=e(17, W, BTN(13)))),
    ("r6_ps_toggle", "bad", None, part(pool=e(17, T))),
    ("r7_layer_target_not_in_use", "bad", None, part(layers=1, layer=[("A", 0, 0)], pool=e(1, P, LAYER(2)))),
    ("r7_layer_target_0", "bad", None, part(layers=1, layer=[("A", 0, 0)], pool=e(1, P, LAYER(0)))),
    ("r7_layer_target_own_layer", "bad", None, part(layers=1, layer=[("A", 0, 0)],
                                                     pool=e(1, P, LAYER(1)) + e(9, P, LAYER(1), layer=1))),
    ("r7_layer_mode_2", "bad", None, part(layers=1, layer=[("A", 0, 0)], pool=e(1, P, LAYER(1, 2)))),
    ("r7_switch_key_mapped_in_its_layer", "bad", None, part(layers=1, layer=[("A", 0, 0)],
                                                             pool=e(1, P, LAYER(1, 1)) + e(1, P, BTN(9), layer=1))),
    ("r8_gyro_target_without_gyro", "bad", None, part(pool=e(15, P, GYRO(0)))),
    ("r8_gyro_target_3", "bad", None, part(gyro=GY, pool=e(15, P, GYRO(3)))),
]


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    names = set()
    lines = [
        "# ButtonPart vectors (protocol 1.24, docs/TENSION_BUTTONS.md sections 3.2-3.5 and 6).",
        "# Written by gen_vectors.py; expectations set by hand per rule, not by a validator.",
        "# name expect needs crc32 part(360 bytes hex)",
    ]
    for name, expect, needs, b in V:
        assert len(b) == 360 and name not in names, name
        names.add(name)
        n = "-" if needs is None else "%02x" % needs
        lines.append("%s %s %s %08x %s" % (name, expect, n, zlib.crc32(b) & 0xFFFFFFFF, b.hex()))
    with open(os.path.join(here, "vectors.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print("%d vectors" % len(V))


if __name__ == "__main__":
    main()
