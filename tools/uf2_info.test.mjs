// node --test tools/uf2_info.test.mjs — gate G2 on a UF2 built here, byte by
// byte, the way the Pico SDK lays out binary info (no firmware build needed).
import { test } from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { checkImage, FAMILY, uf2Info } from "./uf2_info.mjs";

const boards = JSON.parse(readFileSync(new URL("../boards.json", import.meta.url), "utf8")).boards;
const row = (id) => boards.find((b) => b.id === id);

const FLASH = 0x10000000;
const RAM = 0x20000000;

// A flash image with a binary info header at 0x110 (after an RP2350 vector
// table), the SDK's PICO_BOARD and version and the 'D','5' strings. The
// version string is stored in flash at 0x800 but referenced at its RAM
// address, through the address mapping table (as crt0's data copy). A picobin
// block loop as the SDK's crt0 lays it out (boot/picobin.h): a start block at
// 0x140 with the IMAGE_TYPE item, linked to an end block at 0x860 that holds
// picotool's HASH_DEF and HASH_VALUE when sealed (an ignored item when not),
// linked back.
function image({ picoBoard = "pico2_w", version = "v0.9.6-oled-edition", d5Board = "pico2_w", personas = "ds5", displays = "none,pico_oled_1_3", seal = true } = {}) {
  const img = Buffer.alloc(0x900);
  const w32 = (off, v) => img.writeUInt32LE(v >>> 0, off);
  let strAt = 0x600;
  const str = (s) => {
    const at = strAt;
    img.write(s + "\0", at, "latin1");
    strAt += s.length + 1;
    return FLASH + at;
  };
  img.write(version + "\0", 0x800, "latin1");
  const entries = [
    [0x5052, 0xb63cffbb, str(picoBoard)],
    [0x5052, 0x11a9bc3a, RAM], // mapped back to FLASH + 0x800
    [0x3544, 0x44350001, str(d5Board)],
    [0x3544, 0x44350002, str(personas)],
    [0x3544, 0x44350003, str(displays)],
    [0x3544, 0x44350099, str("ignored")],
  ];
  entries.forEach(([tag, id, value], i) => {
    const e = 0x400 + 12 * i;
    w32(e, (tag << 16) | 6);
    w32(e + 4, id);
    w32(e + 8, value);
    w32(0x200 + 4 * i, FLASH + e);
  });
  w32(0x300, FLASH + 0x800); // mapping: source, dest start, dest end; then 0
  w32(0x304, RAM);
  w32(0x308, RAM + 0x100);
  w32(0x110, 0x7188ebf2);
  w32(0x114, FLASH + 0x200);
  w32(0x118, FLASH + 0x200 + 4 * entries.length);
  w32(0x11c, FLASH + 0x300);
  w32(0x120, 0xe71aa390);
  // Start block: IMAGE_TYPE (1BS, 1 word), LAST (2BS, size 1), link, end.
  w32(0x140, 0xffffded3);
  w32(0x144, 0x42 | (1 << 8) | (0x1021 << 16));
  w32(0x148, 0xff | (1 << 8));
  w32(0x14c, 0x860 - 0x140);
  w32(0x150, 0xab123579);
  // End block: HASH_DEF (2 words) + HASH_VALUE (9 words), or IGNORED (2BS, 1 word).
  w32(0x860, 0xffffded3);
  let p = 0x864;
  if (seal) {
    w32(p, 0x47 | (2 << 8) | (1 << 24)); // SHA-256
    p += 8;
    w32(p, 0x4b | (9 << 8));
    p += 36;
  } else {
    w32(p, 0xfe | (1 << 8));
    p += 4;
  }
  w32(p, 0xff | (((p - 0x864) / 4) << 8));
  w32(p + 4, 0x140 - 0x860); // back to the start block (negative)
  w32(p + 8, 0xab123579);
  return img;
}

function uf2(img, family, { absoluteBlock = true } = {}) {
  const blocks = [];
  const block = (flags, addr, data, fam) => {
    const b = Buffer.alloc(512);
    b.writeUInt32LE(0x0a324655, 0);
    b.writeUInt32LE(0x9e5d5157, 4);
    b.writeUInt32LE(flags, 8);
    b.writeUInt32LE(addr, 12);
    b.writeUInt32LE(256, 16);
    b.writeUInt32LE(fam >>> 0, 28);
    data.copy(b, 32);
    b.writeUInt32LE(0x0ab16f30, 508);
    blocks.push(b);
  };
  if (absoluteBlock) block(0xa000, 0x10ffff00, Buffer.alloc(256, 0xef), 0xe48bff57); // picotool's RP2350-E10 block
  for (let o = 0; o < img.length; o += 256) block(0x2000, FLASH + o, img.subarray(o, o + 256), family);
  return Buffer.concat(blocks);
}

const sha = (b) => createHash("sha256").update(b).digest("hex");
const check = (buf, id, extra = {}) => checkImage({ info: uf2Info(buf), row: row(id), file: "f.uf2", sha256: sha(buf), ...extra });

test("reads the SDK's and the D5 binary info, through the address mapping", () => {
  const info = uf2Info(uf2(image(), FAMILY.rp2350a));
  assert.deepEqual(info, {
    families: [0xe48bff59],
    pico_board: "pico2_w",
    version: "v0.9.6-oled-edition",
    d5: { board: "pico2_w", personas: "ds5", displays: "none,pico_oled_1_3" },
    hash_sealed: true,
  });
});

test("an image that matches its row and entry passes", () => {
  const buf = uf2(image(), FAMILY.rp2350a);
  const entry = { board: "pico2_w", version: "v0.9.6-oled-edition", file: "f.uf2", sha256: sha(buf) };
  assert.deepEqual(check(buf, "pico2_w", { version: "v0.9.6-oled-edition", entry }), []);
  // The RP2040 has no hash check, so its image needs no seal.
  const picow = uf2(image({ picoBoard: "pico_w", d5Board: "pico_w", seal: false }), FAMILY.rp2040, { absoluteBlock: false });
  assert.deepEqual(check(picow, "pico_w"), []);
});

test("each mismatch fails, in plain words", () => {
  const ok = image();
  const fails = (buf, re, id = "pico2_w", extra) => assert.match(check(buf, id, extra).join("\n"), re);
  fails(uf2(image({ d5Board: "pico_w" }), FAMILY.rp2350a), /D5 board is "pico_w", boards.json says "pico2_w"/);
  fails(uf2(image({ picoBoard: "pico2" }), FAMILY.rp2350a), /PICO_BOARD is "pico2"/);
  fails(uf2(image({ personas: "ds5,ds4" }), FAMILY.rp2350a), /personas/);
  fails(uf2(image({ displays: "none" }), FAMILY.rp2350a), /displays/);
  fails(uf2(ok, FAMILY.rp2040), /UF2 families e48bff56 are not rp2350a's e48bff59/);
  // The Pico 2 W image offered to a Pico W row: board and family both wrong.
  assert.equal(check(uf2(ok, FAMILY.rp2350a), "pico_w").length, 3);
  fails(uf2(ok, FAMILY.rp2350a), /program version is "v0.9.6-oled-edition", expected "v0.9.7-oled-edition"/, "pico2_w", { version: "v0.9.7-oled-edition" });
  const buf = uf2(ok, FAMILY.rp2350a);
  const entry = { board: "pico2_w", version: "v0.9.6-oled-edition", file: "f.uf2", sha256: sha(buf) };
  fails(buf, /sha256/, "pico2_w", { entry: { ...entry, sha256: "0".repeat(64) } });
  fails(buf, /names g\.uf2/, "pico2_w", { entry: { ...entry, file: "g.uf2" } });
  fails(buf, /version v0\.9\.5/, "pico2_w", { entry: { ...entry, version: "v0.9.5-oled-edition" } });
  fails(buf, /no entry for pico2_w/, "pico2_w", { entry: null });
});

// Re-review G-S1: the CHANGELOG promises the boot ROM checks a hash, so an
// RP2350 image without one fails, as does a block loop that doesn't close.
test("an RP2350 image without the hash seal fails", () => {
  assert.equal(uf2Info(uf2(image({ seal: false }), FAMILY.rp2350a)).hash_sealed, false);
  assert.match(check(uf2(image({ seal: false }), FAMILY.rp2350a), "pico2_w").join("\n"), /no hash seal/);
  const broken = image();
  broken.writeUInt32LE(0, 0x86c + 36 + 8); // the end block's end marker
  assert.match(check(uf2(broken, FAMILY.rp2350a), "pico2_w").join("\n"), /no hash seal/);
  const unlinked = image();
  unlinked.writeUInt32LE(0x10, 0x14c); // the start block's link points nowhere
  assert.equal(uf2Info(uf2(unlinked, FAMILY.rp2350a)).hash_sealed, false);
});

test("not a UF2, or no binary info, is an error", () => {
  assert.throws(() => uf2Info(Buffer.alloc(100)), /not a UF2/);
  const bad = uf2(image(), FAMILY.rp2350a);
  bad.writeUInt32LE(0, 512 + 508);
  assert.throws(() => uf2Info(bad), /bad magic in block 1/);
  assert.throws(() => uf2Info(uf2(Buffer.alloc(0x400), FAMILY.rp2350a)), /no binary info header/);
});

test("boards.json rows are complete and the release names follow the design", () => {
  const ids = new Set();
  for (const b of boards) {
    assert.ok(!ids.has(b.id), `${b.id} twice`);
    ids.add(b.id);
    assert.ok(b.name.length <= 16, `${b.id}: name over 16 characters (DS5_BOARD_NAME)`);
    assert.ok(Number.isInteger(b.type_id) && b.type_id > 0);
    assert.ok(b.chip in FAMILY && Number.isInteger(b.flash_bytes) && Array.isArray(b.cmake_flags));
    assert.ok(b.personas.length && b.displays.length && typeof b.release === "boolean");
    // Old apps and manifest_lib find the Pico 2 W image by the plain name.
    assert.equal(b.asset, b.id === "pico2_w" ? "ds5-bridge-oled-{tag}.uf2" : `ds5-bridge-oled-{tag}-${b.id}.uf2`);
  }
  assert.equal(new Set(boards.map((b) => b.type_id)).size, boards.length);
  assert.deepEqual([row("pico2_w").type_id, row("pico_w").type_id], [1, 2]); // src/fw_identity.h kBoardPico2W / kBoardPicoW
});
