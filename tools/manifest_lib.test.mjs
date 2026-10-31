// node --test tools/manifest_lib.test.mjs
import assert from "node:assert/strict";
import { test } from "node:test";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { allPages, assetHasher, buildFeed, imageFields, isoSeconds, keepIssuedAt, PICO2_W_IMAGE, releaseImages, selectProton, selectRelease, sumFor, versionKey } from "./manifest_lib.mjs";

const asset = (name) => ({ name, size: 1, browser_download_url: `https://github.com/x/releases/download/t/${name}` });
const rel = (tag_name, names, extra = {}) => ({ tag_name, html_url: `https://github.com/x/releases/tag/${tag_name}`, draft: false, prerelease: false, assets: names.map(asset), ...extra });

const unified = rel("v0.8.0-oled-edition", [
  "DS5Dongle-OLED-Edition-Configurator-0.8.0-linux-x86_64.AppImage",
  "DS5Dongle-OLED-Edition-Configurator-0.8.0-win-x64.exe",
  "DS5Dongle-OLED-Edition-Configurator-0.8.0-mac-universal.zip",
  "ds5-bridge-oled-v0.8.0-oled-edition.uf2",
  "SHA256SUMS.txt",
]);
const fwOnly = rel("v0.7.0-oled-edition", ["ds5-bridge-oled-v0.7.0-oled-edition.uf2", "SHA256SUMS.txt"]);
const proton = rel("proton-GE-Proton11-7-DualSense", ["GE-Proton11-7-DualSense.tar.gz", "SHA256SUMS.txt", "dualsense-serial-colon-sanitize.patch"]);

test("picks the newest unified release and pairs its app with its own firmware", () => {
  const s = selectRelease([proton, unified, fwOnly]);
  assert.equal(s.version, "0.8.0");
  assert.deepEqual(Object.keys(s.platforms).sort(), ["linux", "macos", "windows"]);
  assert.equal(s.uf2.name, "ds5-bridge-oled-v0.8.0-oled-edition.uf2");
});

test("skips firmware-only, draft and prerelease releases", () => {
  assert.equal(selectRelease([fwOnly, proton]), null);
  assert.equal(selectRelease([{ ...unified, draft: true }, fwOnly]), null);
  assert.equal(selectRelease([{ ...unified, prerelease: true }]), null);
});

test("never treats a proton release as the app/firmware release", () => {
  const strayUf2 = rel("proton-GE-Proton11-7-DualSense", ["ds5-bridge-oled-proton-GE-Proton11-7-DualSense.uf2"]);
  assert.equal(selectRelease([strayUf2]), null);
});

test("finds the proton tarball and its checksum line", () => {
  const p = selectProton([unified, proton]);
  assert.equal(p.tarball.name, "GE-Proton11-7-DualSense.tar.gz");
  const sums = "5c73  GE-Proton11-7-DualSense.tar.gz\ne2f9  dualsense-serial-colon-sanitize.patch\n";
  assert.equal(sumFor(sums, "GE-Proton11-7-DualSense.tar.gz"), "5c73");
  assert.equal(sumFor(sums, "missing.tar.gz"), null);
  assert.equal(sumFor("aa *GE-Proton11-7-DualSense.tar.gz\n", "GE-Proton11-7-DualSense.tar.gz"), "aa");
});

const devBuild = (v) =>
  rel(`v${v}-oled-edition`, [`DS5Dongle-OLED-Edition-Configurator-${v}-linux-x86_64.AppImage`, `ds5-bridge-oled-v${v}-oled-edition.uf2`], { prerelease: true });
const stable090 = rel("v0.9.0-oled-edition", ["DS5Dongle-OLED-Edition-Configurator-0.9.0-linux-x86_64.AppImage", "ds5-bridge-oled-v0.9.0-oled-edition.uf2"]);

test("orders development builds before their release", () => {
  assert.deepEqual(versionKey("0.9.1-dev.2"), [0, 9, 1, 2]);
  assert.equal(versionKey("0.9.1")[3], Infinity);
  assert.equal(versionKey("nope"), null);
});

test("the stable feed never offers a development build", () => {
  assert.equal(selectRelease([devBuild("0.9.1-dev.1"), stable090]).version, "0.9.0");
});

test("the dev feed offers the newest of development and stable releases", () => {
  const s = selectRelease([devBuild("0.9.1-dev.2"), devBuild("0.9.1-dev.1"), stable090], "dev");
  assert.equal(s.version, "0.9.1-dev.2");
  assert.equal(s.uf2.name, "ds5-bridge-oled-v0.9.1-dev.2-oled-edition.uf2");
  // Once 0.9.1 ships, it wins over its own development builds.
  const stable091 = rel("v0.9.1-oled-edition", ["DS5Dongle-OLED-Edition-Configurator-0.9.1-linux-x86_64.AppImage"]);
  assert.equal(selectRelease([stable091, devBuild("0.9.1-dev.2"), stable090], "dev").version, "0.9.1");
  // No development build yet: the dev feed is the stable release.
  assert.equal(selectRelease([stable090], "dev").version, "0.9.0");
});

test("the Windows entry is the per-user installer (or an older release's portable exe)", () => {
  const installer = rel("v0.9.6-oled-edition", [
    "DS5Dongle-OLED-Edition-Configurator-0.9.6-linux-x86_64.AppImage",
    "DS5Dongle-OLED-Edition-Configurator-0.9.6-win-x64-setup.exe",
    "ds5-bridge-oled-v0.9.6-oled-edition.uf2",
  ]);
  assert.equal(selectRelease([installer]).platforms.windows.name, "DS5Dongle-OLED-Edition-Configurator-0.9.6-win-x64-setup.exe");
  assert.equal(selectRelease([unified]).platforms.windows.name, "DS5Dongle-OLED-Edition-Configurator-0.8.0-win-x64.exe");
  const stray = rel("v0.9.7-oled-edition", ["DS5Dongle-OLED-Edition-Configurator-0.9.7-win-x64-setup.exe.blockmap", "ds5-bridge-oled-v0.9.7-oled-edition.uf2"]);
  assert.equal(selectRelease([stray]), null);
});

// Feed schema 2 (DESIGN_FIRMWARE_IDENTITY §2).
const uf2 = { url: "https://github.com/x/releases/download/v0.9.4-oled-edition/ds5-bridge-oled-v0.9.4-oled-edition.uf2", size: 2745856, sha256: "2f".repeat(32) };
const firmware = { version: "v0.9.4-oled-edition", notes_url: "https://github.com/x/releases/tag/v0.9.4-oled-edition", uf2 };
const app = { version: "0.9.4", notes_url: firmware.notes_url, platforms: { linux: { url: "u", size: 1, sha256: "a".repeat(64) } } };
const protonEntry = { name: "GE-Proton11-7-DualSense", notes_url: "n", tarball: { url: "t", size: 1, sha256: "b".repeat(64) } };

test("schema 2 has the signed fields first and the old keys unchanged", () => {
  const feed = buildFeed({ channel: "dev", issuedAt: "2026-11-02T18:04:11Z", newestStable: "0.9.4", app, firmware, proton: protonEntry });
  assert.deepEqual(Object.keys(feed), ["schema", "channel", "issued_at", "newest_stable", "app", "firmware", "firmwares", "proton"]);
  assert.equal(feed.schema, 2);
  assert.equal(feed.channel, "dev");
  assert.equal(feed.issued_at, "2026-11-02T18:04:11Z");
  assert.equal(feed.newest_stable, "0.9.4");
  assert.equal(feed.app, app);
  assert.equal(feed.firmware, firmware);
  assert.equal(feed.proton, protonEntry);
});

test("firmwares[] is exactly the Pico 2 W image, the same file as the legacy firmware key", () => {
  const feed = buildFeed({ channel: "stable", issuedAt: "2026-11-02T18:04:11Z", newestStable: "0.9.4", app, firmware });
  assert.deepEqual(feed.firmwares, [
    { version: "v0.9.4-oled-edition", board: "pico2_w", chip: "rp2350a", flash_min: 4194304, personas: ["ds5"], displays: ["none", "pico_oled_1_3"], notes_url: firmware.notes_url, uf2 },
  ]);
  assert.deepEqual(feed.firmwares[0].uf2, feed.firmware.uf2);
  assert.equal(PICO2_W_IMAGE.flash_min, 4 * 1024 * 1024);
  // No UF2 in the release: no firmware either way.
  const none = buildFeed({ channel: "stable", issuedAt: "2026-11-02T18:04:11Z", newestStable: "0.9.4", app });
  assert.equal(none.firmware, undefined);
  assert.deepEqual(none.firmwares, []);
});

test("newest_stable is required in both feeds", () => {
  assert.throws(() => buildFeed({ channel: "dev", issuedAt: "2026-11-02T18:04:11Z", app }), /newest_stable/);
});

test("newest_stable is the newest non-prerelease, whatever the list order", () => {
  const stable094 = rel("v0.9.4-oled-edition", ["DS5Dongle-OLED-Edition-Configurator-0.9.4-linux-x86_64.AppImage"]);
  const list = [stable090, devBuild("0.9.5-dev.20"), stable094, { ...rel("v0.9.6-oled-edition", ["DS5Dongle-OLED-Edition-Configurator-0.9.6-linux-x86_64.AppImage"]), draft: true }];
  assert.equal(selectRelease(list, "stable").version, "0.9.4");
  assert.equal(selectRelease(list, "dev").version, "0.9.5-dev.20");
});

test("issued_at is ISO UTC to the second", () => {
  assert.equal(isoSeconds(new Date(Date.UTC(2026, 10, 2, 18, 4, 11, 789))), "2026-11-02T18:04:11Z");
});

test("an unchanged feed keeps its issued_at, a changed one takes the new one", () => {
  const feed = (issuedAt, a = app) => buildFeed({ channel: "stable", issuedAt, newestStable: "0.9.4", app: a, firmware });
  const prev = JSON.stringify(feed("2026-11-01T00:00:00Z"), null, 2) + "\n";
  assert.equal(keepIssuedAt(feed("2026-11-02T00:00:00Z"), prev).issued_at, "2026-11-01T00:00:00Z");
  assert.equal(keepIssuedAt(feed("2026-11-02T00:00:00Z", { ...app, version: "0.9.5" }), prev).issued_at, "2026-11-02T00:00:00Z");
  // A schema-1 feed (no issued_at) or no file: the new time.
  assert.equal(keepIssuedAt(feed("2026-11-02T00:00:00Z"), JSON.stringify({ channel: "stable" })).issued_at, "2026-11-02T00:00:00Z");
  assert.equal(keepIssuedAt(feed("2026-11-02T00:00:00Z"), "").issued_at, "2026-11-02T00:00:00Z");
});

test("reads every page of the release list", async () => {
  const pages = { 1: [1, 2], 2: [3, 4], 3: [5] };
  const asked = [];
  assert.deepEqual(await allPages(async (n) => (asked.push(n), pages[n] ?? []), 2), [1, 2, 3, 4, 5]);
  assert.deepEqual(asked, [1, 2, 3]);
  assert.deepEqual(await allPages(async (n) => (n === 1 ? [1, 2] : []), 2), [1, 2]);
});

// The hash cache (assetHasher): a fake fetch counts downloads.
const sha = (text) => createHash("sha256").update(text).digest("hex");
const fakeFetch = (files) => {
  const calls = [];
  const fetch = async (url) => (calls.push(url), new Response(files[url]));
  return { fetch, calls };
};
const file = (id, name, body, extra = {}) => ({ id, name, size: body.length, updated_at: "2026-10-31T10:00:00Z", browser_download_url: `https://github.com/x/d/${name}`, ...extra });
const tar = file(7, "GE-Proton11-7-DualSense.tar.gz", "proton bytes");
const files = { [tar.browser_download_url]: "proton bytes" };

test("hash cache: a cached file with the same id, name, size and updated_at is not downloaded", async () => {
  const entries = {};
  const first = fakeFetch(files);
  assert.deepEqual(await assetHasher({ fetch: first.fetch, entries })(tar), { url: tar.browser_download_url, size: 12, sha256: sha("proton bytes") });
  assert.equal(first.calls.length, 1);
  assert.deepEqual(entries[7], { name: tar.name, size: 12, updated_at: tar.updated_at, sha256: sha("proton bytes") });
  const again = fakeFetch(files);
  assert.deepEqual(await assetHasher({ fetch: again.fetch, entries })(tar), { url: tar.browser_download_url, size: 12, sha256: sha("proton bytes") });
  assert.equal(again.calls.length, 0);
});

test("hash cache: a changed updated_at or size downloads again", async () => {
  const entries = { 7: { name: tar.name, size: 12, updated_at: "2026-10-01T00:00:00Z", sha256: "0".repeat(64) } };
  const f = fakeFetch(files);
  assert.equal((await assetHasher({ fetch: f.fetch, entries })(tar)).sha256, sha("proton bytes"));
  assert.equal(f.calls.length, 1);
  assert.equal(entries[7].updated_at, tar.updated_at);
  const replaced = file(7, tar.name, "proton bytes v2");
  const g = fakeFetch({ [replaced.browser_download_url]: "proton bytes v2" });
  assert.equal((await assetHasher({ fetch: g.fetch, entries })(replaced)).sha256, sha("proton bytes v2"));
  assert.equal(g.calls.length, 1);
});

test("hash cache: GitHub's digest must match the cache and the download", async () => {
  const good = { ...tar, digest: `sha256:${sha("proton bytes")}` };
  const bad = { ...tar, digest: `sha256:${"1".repeat(64)}` };
  const f = fakeFetch(files);
  assert.equal((await assetHasher({ fetch: f.fetch })(good)).sha256, sha("proton bytes"));
  await assert.rejects(assetHasher({ fetch: f.fetch })(bad), /digest says 1{64}, the download says/);
  const entries = { 7: { name: tar.name, size: 12, updated_at: tar.updated_at, sha256: "2".repeat(64) } };
  await assert.rejects(assetHasher({ fetch: f.fetch, entries })(good), /the hash cache says 2{64}/);
  assert.equal(f.calls.length, 2);
});

test("hash cache: the same file in both feeds downloads once; --no-cache downloads again", async () => {
  const f = fakeFetch(files);
  const saved = [];
  const entries = {};
  const measure = assetHasher({ fetch: f.fetch, entries, save: (e) => saved.push(JSON.stringify(e)) });
  const [a, b] = await Promise.all([measure(tar), measure({ ...tar })]);
  assert.deepEqual(a, b);
  assert.equal(f.calls.length, 1);
  assert.equal(saved.length, 1);
  const forced = fakeFetch(files);
  assert.equal((await assetHasher({ fetch: forced.fetch, entries, noCache: true })(tar)).sha256, sha("proton bytes"));
  assert.equal(forced.calls.length, 1);
  // A short download is still refused.
  const short = fakeFetch({ [tar.browser_download_url]: "proton" });
  await assert.rejects(assetHasher({ fetch: short.fetch, noCache: true })(tar), /size 6 != 12/);
});

test("hash cache with a gate (G2): checked on download, the pass cached per key, a refused file not cached", async () => {
  const entries = {};
  const seen = [];
  const gate = (key, ok = true) => ({ key, check: (bytes, sha256) => { seen.push([key, bytes.toString(), sha256]); if (!ok) throw new Error("G2 refused"); } });
  const f = fakeFetch(files);
  assert.equal((await assetHasher({ fetch: f.fetch, entries })(tar, gate("k1"))).sha256, sha("proton bytes"));
  assert.deepEqual(seen, [["k1", "proton bytes", sha("proton bytes")]]);
  assert.equal(entries[7].gate, "k1");
  // Same key: no download, no check. Without a gate: the cached hash.
  const g = fakeFetch(files);
  await assetHasher({ fetch: g.fetch, entries })(tar, gate("k1"));
  await assetHasher({ fetch: g.fetch, entries })(tar);
  assert.equal(g.calls.length, 0);
  assert.equal(seen.length, 1);
  // Another key (boards.json row or entry changed): downloaded and checked again; a refusal throws and keeps the old entry.
  const h = fakeFetch(files);
  await assert.rejects(assetHasher({ fetch: h.fetch, entries })(tar, gate("k2", false)), /G2 refused/);
  assert.equal(h.calls.length, 1);
  assert.equal(entries[7].gate, "k1");
});

const boards = JSON.parse(readFileSync(new URL("../boards.json", import.meta.url), "utf8")).boards;

// src/fw_identity.h keeps its own copy of the type ids (kBoard<Id in
// PascalCase>, e.g. pico2_w -> kBoardPico2W): it must be boards.json's.
test("src/fw_identity.h board type ids equal boards.json type_id, row for row", () => {
  const header = readFileSync(new URL("../src/fw_identity.h", import.meta.url), "utf8");
  const inHeader = Object.fromEntries([...header.matchAll(/\b(kBoard(?!Unknown)\w+)\s*=\s*(\d+)/g)].map((m) => [m[1], Number(m[2])]));
  const pascal = (id) => id.split("_").map((w) => w[0].toUpperCase() + w.slice(1)).join("");
  assert.deepEqual(inHeader, Object.fromEntries(boards.map((b) => [`kBoard${pascal(b.id)}`, b.type_id])));
});

test("boards.json's pico2_w row describes the image every release so far published", () => {
  assert.deepEqual(imageFields(boards.find((b) => b.id === "pico2_w")), PICO2_W_IMAGE);
});

test("firmware-images.json: each entry is a release board, this version and a file of the release", () => {
  const r = rel("v0.9.6-oled-edition", ["ds5-bridge-oled-v0.9.6-oled-edition.uf2", "firmware-images.json"]);
  const entry = { board: "pico2_w", version: "v0.9.6-oled-edition", file: "ds5-bridge-oled-v0.9.6-oled-edition.uf2", sha256: "2f".repeat(32) };
  const [img] = releaseImages([entry], r, boards);
  assert.equal(img.row.id, "pico2_w");
  assert.equal(img.asset.name, entry.file);
  assert.throws(() => releaseImages([{ ...entry, board: "pico_w" }], r, boards), /not a release board/);
  assert.throws(() => releaseImages([{ ...entry, board: "nope" }], r, boards), /not a release board/);
  assert.throws(() => releaseImages([{ ...entry, version: "v0.9.5-oled-edition" }], r, boards), /says pico2_w is v0\.9\.5/);
  assert.throws(() => releaseImages([{ ...entry, file: "other.uf2" }], r, boards), /doesn't have/);
  assert.throws(() => releaseImages({}, r, boards), /not a list/);
});

test("firmwares[] from firmware-images.json is byte-identical to the Pico 2 W fallback for the same file", () => {
  const listed = [{ version: firmware.version, ...imageFields(boards.find((b) => b.id === "pico2_w")), notes_url: firmware.notes_url, uf2 }];
  const a = buildFeed({ channel: "stable", issuedAt: "2026-11-02T18:04:11Z", newestStable: "0.9.4", app, firmware });
  const b = buildFeed({ channel: "stable", issuedAt: "2026-11-02T18:04:11Z", newestStable: "0.9.4", app, firmware, firmwares: listed });
  assert.equal(JSON.stringify(b), JSON.stringify(a));
});
