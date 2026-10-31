#!/usr/bin/env node
// Build latest.json, the update feed the DS5 Dongle OLED Edition Configurator
// reads (app ADR-0003/0005), from what is ALREADY published on this repo:
//   - the newest unified release vX.Y.Z-oled-edition: the app for each OS and
//     the Pico 2 W firmware UF2 it was built with
//   - the newest proton-* release: the GE-Proton DualSense build the app's
//     Games page installs (its SHA256SUMS.txt must list the tarball)
// Every file is downloaded and hashed here, so the feed can never point at a
// file that doesn't exist or doesn't match; the hashes are kept in
// ~/.cache/ds5-release/feed-hash-cache.json (per GitHub asset id, name, size
// and updated_at), so an unchanged file is never downloaded again, and
// GitHub's own sha256 digest, when listed, must agree (manifest_lib
// assetHasher). Writes feed schema 2 (manifest_lib buildFeed): issued_at
// (now, UTC), newest_stable, firmwares[] beside the legacy firmware key
// (from the release's firmware-images.json and boards.json when it has one).
//
//   node tools/build_manifest.mjs [--out latest.json] [--check] [--channel stable|dev] [--no-cache]
//
// --no-cache downloads and hashes every file again (and refreshes the cache).
//
// Exit 2: no unified release yet (nothing to write). --check exits 1 if the
// result differs from the existing file. Uses GITHUB_TOKEN when set.
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, renameSync, writeFileSync } from "node:fs";
import { homedir } from "node:os";
import { dirname, join } from "node:path";
import { allPages, assetHasher, buildFeed, imageFields, isoSeconds, keepIssuedAt, pick, releaseImages, selectProton, selectRelease, sumFor } from "./manifest_lib.mjs";
import { checkImage, uf2Info } from "./uf2_info.mjs";

const REPO = "MarcelineVPQ/DS5Dongle-OLED-Edition";

const args = process.argv.slice(2);
const out = args.includes("--out") ? args[args.indexOf("--out") + 1] : "latest.json";
const checkOnly = args.includes("--check");
const noCache = args.includes("--no-cache");
const channel = args.includes("--channel") ? args[args.indexOf("--channel") + 1] : "stable";
if (channel !== "stable" && channel !== "dev") throw new Error(`unknown channel ${channel}`);
const headers = { Accept: "application/vnd.github+json", "User-Agent": "ds5-manifest-builder" };
if (process.env.GITHUB_TOKEN) headers.Authorization = `Bearer ${process.env.GITHUB_TOKEN}`;

// All releases, every page (newest_stable may be older than the first page).
const releases = () =>
  allPages(async (page) => {
    const r = await fetch(`https://api.github.com/repos/${REPO}/releases?per_page=100&page=${page}`, { headers });
    if (!r.ok) throw new Error(`${REPO}: GitHub API returned ${r.status}`);
    return r.json();
  }, 100);

// Download and hash an asset, streamed (the Proton build is ~565 MB), unless
// the hash cache already has it.
const CACHE = join(process.env.XDG_CACHE_HOME || join(homedir(), ".cache"), "ds5-release", "feed-hash-cache.json");
const measure = assetHasher({
  fetch,
  entries: existsSync(CACHE) ? JSON.parse(readFileSync(CACHE, "utf8")) : {},
  noCache,
  save: (entries) => {
    mkdirSync(dirname(CACHE), { recursive: true });
    writeFileSync(`${CACHE}.tmp`, JSON.stringify(entries, null, 2) + "\n");
    renameSync(`${CACHE}.tmp`, CACHE);
  },
});

const all = await releases();
const unified = selectRelease(all, channel);
if (!unified) {
  console.error("No unified app + firmware release yet; not writing a manifest.");
  process.exit(checkOnly ? 0 : 2);
}

const parts = { channel };
const platforms = {};
for (const [os, asset] of Object.entries(unified.platforms)) platforms[os] = await measure(asset);
// The app's part of the release notes (the "## Configurator app" section,
// i.e. its CHANGELOG entry), so the app can show what's in a build before
// someone joins the Development channel. Older apps ignore the field.
const appNotes = (body) => {
  const at = body.indexOf("## Configurator app");
  if (at < 0) return undefined;
  let text = body.slice(body.indexOf("\n", at) + 1);
  const end = text.indexOf("**Verify a download**");
  if (end >= 0) text = text.slice(0, end);
  text = text.trim().slice(0, 20000);
  return text || undefined;
};
const notes = appNotes(unified.release.body ?? "");
parts.app = { version: unified.version, notes_url: unified.release.html_url, ...(notes ? { notes } : {}), platforms };
// Releases from step 7 on list every board image in firmware-images.json
// (release.yml, after gate G2); each file's hash must match its entry, and
// gate G2 (tools/uf2_info.mjs) runs again on each downloaded UF2 against its
// boards.json row and entry. The hash cache records the pass (keyed by the
// row and entry), so a cached image is checked again only when either
// changes. Older releases: firmwares[] is the Pico 2 W image alone (buildFeed).
const imagesAsset = pick(unified.release, /^firmware-images\.json$/);
if (imagesAsset) {
  const r = await fetch(imagesAsset.browser_download_url, { redirect: "follow" });
  if (!r.ok) throw new Error(`firmware-images.json: download returned ${r.status}`);
  const boards = JSON.parse(readFileSync(new URL("../boards.json", import.meta.url), "utf8")).boards;
  parts.firmwares = [];
  for (const { entry, row, asset } of releaseImages(await r.json(), unified.release, boards)) {
    const key = createHash("sha256").update(JSON.stringify(["G2", row, entry])).digest("hex");
    const check = (bytes, sha256) => {
      const problems = checkImage({ info: uf2Info(bytes), row, version: entry.version, entry, file: asset.name, sha256 });
      if (problems.length) throw new Error(`G2 ${asset.name}: ${problems.join("; ")}`);
    };
    const uf2 = await measure(asset, { key, check });
    if (uf2.sha256 !== entry.sha256) throw new Error(`${asset.name}: firmware-images.json says ${entry.sha256}, the file hashes to ${uf2.sha256}`);
    parts.firmwares.push({ version: entry.version, ...imageFields(row), notes_url: unified.release.html_url, uf2 });
  }
}
// After the G2 pass above, so the Pico 2 W image comes from the hash cache.
if (unified.uf2) parts.firmware = { version: unified.release.tag_name, notes_url: unified.release.html_url, uf2: await measure(unified.uf2) };

const proton = selectProton(all);
if (proton) {
  if (!proton.sums) throw new Error(`${proton.release.tag_name}: no SHA256SUMS.txt in the release`);
  const measured = await measure(proton.tarball);
  const r = await fetch(proton.sums.browser_download_url, { redirect: "follow" });
  if (!r.ok) throw new Error(`SHA256SUMS.txt: download returned ${r.status}`);
  const listed = sumFor(await r.text(), proton.tarball.name);
  if (listed !== measured.sha256) throw new Error(`${proton.tarball.name}: SHA256SUMS.txt says ${listed ?? "nothing"}, download hashes to ${measured.sha256}`);
  parts.proton = { name: proton.tarball.name.replace(/\.tar\.gz$/, ""), notes_url: proton.release.html_url, tarball: measured };
}

const current = existsSync(out) ? readFileSync(out, "utf8") : "";
const newestStable = selectRelease(all, "stable")?.version;
const manifest = keepIssuedAt(buildFeed({ ...parts, issuedAt: isoSeconds(new Date()), newestStable }), current);
const text = JSON.stringify(manifest, null, 2) + "\n";
if (checkOnly) {
  console.log(text === current ? "latest.json is current." : "latest.json is out of date.");
  process.exit(text === current ? 0 : 1);
}
writeFileSync(out, text);
console.log(`Wrote ${out}: schema 2, app ${manifest.app.version} (${Object.keys(platforms).join(", ")}), firmware ${manifest.firmware?.version ?? "none"}, proton ${manifest.proton?.name ?? "none"}, newest stable ${manifest.newest_stable}`);
