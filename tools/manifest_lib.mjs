import { createHash } from "node:crypto";

// Pure part of tools/build_manifest.mjs: picks the release(s) and asset files
// that latest.json describes. Kept separate so it can be tested on fixture
// release lists without touching the network.

// Unified releases: firmware X.Y.Z + the configurator app X.Y.Z; development
// builds (X.Y.Z-dev.N, GitHub pre-releases) only for the dev channel.
export const RELEASE_TAG = /^v(\d+\.\d+\.\d+)-oled-edition$/;
export const DEV_TAG = /^v(\d+\.\d+\.\d+-dev\.\d+)-oled-edition$/;
const V = String.raw`\d+\.\d+\.\d+(?:-dev\.\d+)?`;
export const APP_FILES = {
  linux: new RegExp(`^DS5Dongle-OLED-Edition-Configurator-${V}-linux-x86_64\\.AppImage$`),
  // The per-user installer (app 0.9.5-dev.12 on); older releases shipped a portable exe.
  windows: new RegExp(`^DS5Dongle-OLED-Edition-Configurator-${V}-win-x64(?:-setup)?\\.exe$`),
  macos: new RegExp(`^DS5Dongle-OLED-Edition-Configurator-${V}-mac-universal\\.zip$`),
};

// X.Y.Z or X.Y.Z-dev.N -> sortable [X, Y, Z, N] (a release sorts after its dev builds).
export function versionKey(v) {
  const m = /^(\d+)\.(\d+)\.(\d+)(?:-dev\.(\d+))?$/.exec(v);
  return m ? [Number(m[1]), Number(m[2]), Number(m[3]), m[4] === undefined ? Infinity : Number(m[4])] : null;
}
const newer = (a, b) => {
  const x = versionKey(a), y = versionKey(b);
  for (let i = 0; i < 4; i++) if (x[i] !== y[i]) return x[i] > y[i];
  return false;
};
// Same naming rule as the app (PROTON_NAME in the app's src/update/manifest.ts).
export const PROTON_TARBALL = /^GE-Proton[\w.-]*DualSense\.tar\.gz$/;

export const pick = (release, pattern) => release?.assets.find((a) => pattern.test(a.name)) ?? null;
const published = (r) => !r.draft && !r.prerelease;

// The newest published unified release that carries the app. Older
// firmware-only releases (v0.7.0 and before) are skipped: the feed always
// pairs an app with the firmware it was built and tested with.
// channel "stable": only published stable releases. "dev": the newest of the
// development pre-releases and the stable releases (dev users get those too).
export function selectRelease(releases, channel = "stable") {
  let best = null;
  for (const r of releases) {
    if (r.draft) continue;
    const stable = !r.prerelease ? RELEASE_TAG.exec(r.tag_name) : null;
    const dev = channel === "dev" && r.prerelease ? DEV_TAG.exec(r.tag_name) : null;
    const m = stable ?? dev;
    if (!m) continue;
    const platforms = Object.fromEntries(Object.entries(APP_FILES).flatMap(([os, re]) => (pick(r, re) ? [[os, pick(r, re)]] : [])));
    if (Object.keys(platforms).length === 0) continue;
    const uf2 = pick(r, new RegExp(`^ds5-bridge-oled-${r.tag_name.replace(/[.]/g, "\\.")}\\.uf2$`));
    const found = { release: r, version: m[1], platforms, uf2 };
    // By version, not list order: the list is paged and may be read in any order.
    if (!best || newer(found.version, best.version)) best = found;
  }
  return best;
}

export function selectProton(releases) {
  const r = releases.filter(published).find((rel) => rel.tag_name.startsWith("proton-")) ?? null;
  const tarball = pick(r, PROTON_TARBALL);
  return r && tarball ? { release: r, tarball, sums: pick(r, /^SHA256SUMS\.txt$/) } : null;
}

// The hash SHA256SUMS.txt lists for `name`, or null.
export function sumFor(sumsText, name) {
  const line = sumsText.split("\n").find((l) => l.trim().split(/\s+/)[1]?.replace(/^\*/, "") === name);
  return line ? line.trim().split(/\s+/)[0] : null;
}

// Every page of a paged list: getPage(n) (n from 1) returns one page's array;
// a page shorter than perPage is the last.
export async function allPages(getPage, perPage) {
  const all = [];
  for (let page = 1; ; page++) {
    const items = await getPage(page);
    all.push(...items);
    if (items.length < perPage) return all;
  }
}

// What the only image published so far is built for (design
// DESIGN_FIRMWARE_IDENTITY §2): every release UF2 is the Pico 2 W (RP2350A,
// 4 MB) DualSense image, OLED add-on optional. Releases from step 7 on list
// their images in firmware-images.json (boards.json rows, releaseImages);
// this stays for the releases made before. Personas switch at run time.
export const PICO2_W_IMAGE = {
  board: "pico2_w",
  chip: "rp2350a",
  flash_min: 4 * 1024 * 1024,
  personas: ["ds5"],
  displays: ["none", "pico_oled_1_3"],
};

// ISO UTC to the second ("2026-11-02T18:04:11Z").
export const isoSeconds = (date) => date.toISOString().replace(/\.\d{3}Z$/, "Z");

// The feed, schema 2. Installed apps read `app`, `firmware` and `proton` as
// before (unknown keys are dropped); `firmware` stays the Pico 2 W image.
// New apps read `firmwares[]`, check `channel` and `issued_at`, and go "Back
// to stable" only to `newest_stable`.
// `firmwares` (from the release's firmware-images.json, releaseImages below)
// replaces the Pico 2 W-only list of releases made before it existed.
export function buildFeed({ channel, issuedAt, newestStable, app, firmware, firmwares, proton }) {
  if (!newestStable) throw new Error("no stable release: newest_stable is required in both feeds");
  const { version, notes_url, uf2 } = firmware ?? {};
  return {
    schema: 2,
    channel,
    issued_at: issuedAt,
    newest_stable: newestStable,
    app,
    ...(firmware ? { firmware } : {}),
    firmwares: firmwares ?? (firmware ? [{ version, ...PICO2_W_IMAGE, notes_url, uf2 }] : []),
    ...(proton ? { proton } : {}),
  };
}

// A boards.json row as the feed describes the image (the PICO2_W_IMAGE fields).
export const imageFields = (row) => ({ board: row.id, chip: row.chip, flash_min: row.flash_bytes, personas: row.personas, displays: row.displays });

// The images a release lists in its firmware-images.json (release.yml writes
// it after gate G2: [{ board, version, file, sha256 }]), each with its
// boards.json row and its release asset. Throws when an entry names a board
// that isn't a release row, another version, or a file the release lacks.
export function releaseImages(listing, release, boards) {
  if (!Array.isArray(listing)) throw new Error(`${release.tag_name}: firmware-images.json is not a list`);
  // loop: NOT-WORTH — one entry per release board
  return listing.map((entry) => {
    const row = boards.find((b) => b.id === entry.board);
    if (!row?.release) throw new Error(`${release.tag_name}: firmware-images.json lists ${entry.board}, not a release board in boards.json`);
    if (entry.version !== release.tag_name) throw new Error(`${release.tag_name}: firmware-images.json says ${entry.board} is ${entry.version}`);
    const asset = release.assets.find((a) => a.name === entry.file);
    if (!asset) throw new Error(`${release.tag_name}: firmware-images.json names ${entry.file}, which the release doesn't have`);
    return { entry, row, asset };
  });
}

// Keep the previous feed's issued_at when nothing else changed, so an
// unchanged feed stays byte-identical (publish_feed.sh then has nothing to push).
export function keepIssuedAt(feed, previousText) {
  let prev;
  try {
    prev = JSON.parse(previousText);
  } catch {
    return feed;
  }
  if (typeof prev?.issued_at !== "string") return feed;
  const same = JSON.stringify({ ...prev, issued_at: "" }) === JSON.stringify({ ...feed, issued_at: "" });
  return same ? { ...feed, issued_at: prev.issued_at } : feed;
}

// Size + sha256 of release files, without downloading one twice. `entries` is
// the hash cache (feed-hash-cache.json, keyed by GitHub asset id): a file
// whose id, name, size and updated_at all match its entry is not downloaded
// again. In one run each asset is downloaded at most once. noCache ignores the
// stored entries (re-downloads and replaces them). When GitHub lists a
// `digest` ("sha256:…") it must equal the cached or computed sha256, or this
// throws: neither side is trusted silently. save(entries) runs after each new
// hash, so an interrupted publish keeps what it already hashed.
// measure(asset, gate): gate = { key, check(bytes, sha256) } runs on the
// downloaded bytes and throws to refuse the file (gate G2 on a UF2); the
// entry records `gate: key`, and a cached entry without that key is
// downloaded and checked again.
export function assetHasher({ fetch, entries = {}, noCache = false, save = () => {} }) {
  const inRun = new Map();
  const confirm = (asset, sha256, where) => {
    if (!asset.digest) return;
    const d = /^sha256:([0-9a-f]{64})$/.exec(asset.digest);
    if (d && d[1] !== sha256) throw new Error(`${asset.name}: GitHub's digest says ${d[1]}, ${where} says ${sha256}`);
  };
  async function hash(asset, gate) {
    const hit = entries[asset.id];
    if (!noCache && hit && hit.name === asset.name && hit.size === asset.size && hit.updated_at === asset.updated_at && (!gate || hit.gate === gate.key)) {
      confirm(asset, hit.sha256, "the hash cache");
      return { url: asset.browser_download_url, size: hit.size, sha256: hit.sha256 };
    }
    const r = await fetch(asset.browser_download_url, { redirect: "follow" });
    if (!r.ok || !r.body) throw new Error(`${asset.name}: download returned ${r.status}`);
    const h = createHash("sha256");
    let size = 0;
    const chunks = [];
    for await (const chunk of r.body) {
      size += chunk.byteLength;
      h.update(chunk);
      if (gate) chunks.push(chunk);
    }
    if (size !== asset.size) throw new Error(`${asset.name}: size ${size} != ${asset.size}`);
    const sha256 = h.digest("hex");
    confirm(asset, sha256, "the download");
    if (gate) gate.check(Buffer.concat(chunks), sha256);
    entries[asset.id] = { name: asset.name, size, updated_at: asset.updated_at, sha256, ...(gate ? { gate: gate.key } : {}) };
    save(entries);
    return { url: asset.browser_download_url, size, sha256 };
  }
  return (asset, gate) => {
    const k = `${asset.id} ${gate?.key ?? ""}`;
    if (!inRun.has(k)) inRun.set(k, hash(asset, gate));
    return inRun.get(k);
  };
}
