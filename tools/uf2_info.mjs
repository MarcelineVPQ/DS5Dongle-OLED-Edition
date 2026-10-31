#!/usr/bin/env node
// Gate G2: an image is what its boards.json row and its firmware-images.json
// entry say (design DESIGN_FIRMWARE_IDENTITY §4).
//   node tools/uf2_info.mjs FILE.uf2                    prints what the UF2 says
//   node tools/uf2_info.mjs FILE.uf2 --board ID [--version V] [--images firmware-images.json] [--boards boards.json]
// Reads the UF2's binary info (Pico SDK pico/binary_info: the SDK's PICO_BOARD
// and program version, and the firmware's own 'D','5' strings from
// src/fw_identity.h: board, personas, displays) and its UF2 family ids, then
// checks them against the board's row: D5 board = id, PICO_BOARD = pico_board,
// personas and displays equal, every image block in the chip's family. With
// --version the program version must equal it; with --images the board's entry
// must name this file, its sha256 and the same version. Exit 1 on any problem.
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { basename, dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

// pico-sdk 2.3.0 boot/uf2.h, pico/binary_info/{defs,structure}.h
const UF2_MAGIC = [0x0a324655, 0x9e5d5157, 0x0ab16f30];
const UF2_FLAG_NOT_MAIN_FLASH = 0x1;
const UF2_FLAG_FAMILY_ID = 0x2000;
export const FAMILY = { rp2040: 0xe48bff56, rp2350a: 0xe48bff59, rp2350b: 0xe48bff59 }; // RP2350: ARM secure
const ABSOLUTE_FAMILY = 0xe48bff57; // picotool's RP2350-E10 workaround block
const BI_MARKER_START = 0x7188ebf2;
const BI_MARKER_END = 0xe71aa390;
const BI_TYPE_ID_AND_STRING = 6;
const TAG_RP = 0x5052; // BINARY_INFO_MAKE_TAG('R','P')
const TAG_D5 = 0x3544; // BINARY_INFO_MAKE_TAG('D','5'), src/fw_identity.h
const ID_PICO_BOARD = 0xb63cffbb;
const ID_PROGRAM_VERSION = 0x11a9bc3a;
const D5 = { 0x44350001: "board", 0x44350002: "personas", 0x44350003: "displays" };

// The UF2's blocks as an address -> bytes image, plus the family ids its
// image blocks carry.
export function readUf2(buf) {
  const pages = new Map();
  const families = new Set();
  if (buf.length === 0 || buf.length % 512) throw new Error(`not a UF2: ${buf.length} bytes is not a whole number of 512-byte blocks`);
  // loop: NOT-WORTH — host tool, one pass over the file
  for (let o = 0; o < buf.length; o += 512) {
    if (buf.readUInt32LE(o) !== UF2_MAGIC[0] || buf.readUInt32LE(o + 4) !== UF2_MAGIC[1] || buf.readUInt32LE(o + 508) !== UF2_MAGIC[2])
      throw new Error(`not a UF2: bad magic in block ${o / 512}`);
    const flags = buf.readUInt32LE(o + 8);
    const addr = buf.readUInt32LE(o + 12);
    const size = buf.readUInt32LE(o + 16);
    const family = flags & UF2_FLAG_FAMILY_ID ? buf.readUInt32LE(o + 28) : null;
    if (flags & UF2_FLAG_NOT_MAIN_FLASH || family === ABSOLUTE_FAMILY) continue;
    if (addr & 0xff || size > 256) throw new Error(`block ${o / 512}: ${size} bytes at 0x${addr.toString(16)} is not one aligned 256-byte page`);
    families.add(family);
    pages.set(addr, buf.subarray(o + 32, o + 32 + size));
  }
  return { pages, families: [...families] };
}

function reader(pages) {
  // UF2 payloads are 256-byte pages at 256-byte aligned addresses (picotool, the SDK's elf2uf2).
  const byte = (a) => {
    const base = (a & ~0xff) >>> 0;
    return pages.get(base)?.[a - base];
  };
  const u32 = (a) => {
    const b = [0, 1, 2, 3].map((i) => byte(a + i));
    return b.includes(undefined) ? undefined : (b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24)) >>> 0;
  };
  return { byte, u32 };
}

// The binary info strings: { pico_board, version, d5: { board, personas, displays } }.
export function binaryInfo(pages) {
  const { byte, u32: raw32 } = reader(pages);
  const start = Math.min(...pages.keys());
  // The header sits in the image's first 256 bytes (defs.h; after the vector
  // table on RP2350, after boot2 on RP2040): search the first 1 KB.
  let header = null;
  // loop: NOT-WORTH — 256 words
  for (let a = start; a < start + 1024 && !header; a += 4)
    if (raw32(a) === BI_MARKER_START && raw32(a + 16) === BI_MARKER_END) header = a;
  if (header === null) throw new Error("no binary info header in the image");
  // Runtime addresses (data copied to RAM) map back to where they are stored.
  const map = [];
  // loop: NOT-WORTH — the data copy table, a few entries
  for (let m = raw32(header + 12); m !== undefined && raw32(m); m += 12) map.push([raw32(m), raw32(m + 4), raw32(m + 8)]);
  const at = (a) => {
    const hit = map.find(([, d0, d1]) => a >= d0 && a < d1);
    return hit ? hit[0] + (a - hit[1]) : a;
  };
  const u32 = (a) => raw32(at(a));
  const str = (a) => {
    let s = "";
    // loop: NOT-WORTH — one string, capped
    for (let c = byte(at(a)), i = 0; c && i < 256; c = byte(at(a + ++i))) s += String.fromCharCode(c);
    return s;
  };
  const out = { pico_board: null, version: null, d5: {} };
  // loop: NOT-WORTH — one entry per bi_decl
  for (let p = raw32(header + 4); p < raw32(header + 8); p += 4) {
    const e = u32(p);
    if (e === undefined) continue;
    const typeTag = u32(e);
    if ((typeTag & 0xffff) !== BI_TYPE_ID_AND_STRING) continue;
    const tag = typeTag >>> 16, id = u32(e + 4), value = str(u32(e + 8));
    if (tag === TAG_RP && id === ID_PICO_BOARD) out.pico_board = value;
    if (tag === TAG_RP && id === ID_PROGRAM_VERSION) out.version = value;
    if (tag === TAG_D5 && D5[id]) out.d5[D5[id]] = value;
  }
  return out;
}

export function uf2Info(buf) {
  const { pages, families } = readUf2(buf);
  return { families, ...binaryInfo(pages) };
}

// Problems, in plain words; [] = the image is what its row and entry say.
export function checkImage({ info, row, version, entry, file, sha256 }) {
  const p = [];
  const want = (what, have, should) => have !== should && p.push(`${what} is ${JSON.stringify(have)}, boards.json says ${JSON.stringify(should)}`);
  want("binary info D5 board", info.d5.board ?? null, row.id);
  want("binary info PICO_BOARD", info.pico_board, row.pico_board);
  want("binary info personas", info.d5.personas ?? null, row.personas.join(","));
  want("binary info displays", info.d5.displays ?? null, row.displays.join(","));
  const fam = FAMILY[row.chip];
  if (fam === undefined) p.push(`boards.json chip ${JSON.stringify(row.chip)} has no UF2 family here`);
  else if (info.families.length !== 1 || info.families[0] !== fam)
    p.push(`UF2 families ${info.families.map((f) => f?.toString(16) ?? "none").join(",")} are not ${row.chip}'s ${fam.toString(16)}`);
  if (version !== undefined && info.version !== version) p.push(`program version is ${JSON.stringify(info.version)}, expected ${JSON.stringify(version)}`);
  if (entry !== undefined) {
    if (!entry) return [...p, `firmware-images.json has no entry for ${row.id}`];
    if (entry.file !== file) p.push(`firmware-images.json names ${entry.file}, this file is ${file}`);
    if (entry.sha256 !== sha256) p.push(`firmware-images.json sha256 ${entry.sha256} != the file's ${sha256}`);
    if (entry.version !== info.version) p.push(`firmware-images.json version ${entry.version} != the image's ${info.version}`);
  }
  return p;
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  const args = process.argv.slice(2);
  const opt = (k) => (args.includes(k) ? args[args.indexOf(k) + 1] : undefined);
  const path = args[0];
  if (!path || path.startsWith("--")) {
    console.error("usage: node tools/uf2_info.mjs FILE.uf2 [--board ID [--version V] [--images firmware-images.json] [--boards boards.json]]");
    process.exit(2);
  }
  const buf = readFileSync(path);
  const info = uf2Info(buf);
  console.log(JSON.stringify({ file: basename(path), ...info, families: info.families.map((f) => f?.toString(16) ?? null) }));
  const board = opt("--board");
  if (board) {
    const boardsPath = opt("--boards") ?? join(dirname(fileURLToPath(import.meta.url)), "..", "boards.json");
    const row = JSON.parse(readFileSync(boardsPath, "utf8")).boards.find((b) => b.id === board);
    if (!row) {
      console.error(`G2: ${board} is not in ${boardsPath}`);
      process.exit(1);
    }
    const images = opt("--images");
    const entry = images ? (JSON.parse(readFileSync(images, "utf8")).find((e) => e.board === board) ?? null) : undefined;
    const problems = checkImage({ info, row, version: opt("--version"), entry, file: basename(path), sha256: createHash("sha256").update(buf).digest("hex") });
    for (const m of problems) console.error(`G2 ${basename(path)}: ${m}`);
    if (problems.length) process.exit(1);
    console.log(`G2 ${basename(path)}: matches boards.json ${board}${images ? ` and ${basename(images)}` : ""}`);
  }
}
