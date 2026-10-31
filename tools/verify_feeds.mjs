// Gate G1: the committed update feeds are the ones the owner signed.
//   node tools/verify_feeds.mjs [--staged] [--base REF] [--trusted feedSignature.ts]
// Checks latest.json and latest-dev.json: each has a .sig that verifies against
// a public key in the app's TRUSTED_KEYS (DS5Dongle-OLED-Config-Web
// electron/main/feedSignature.ts; public keys only, no secret), its `channel`
// matches its file, and its `issued_at` is not older than the one at --base
// (default HEAD). --staged checks what is about to be committed (the
// pre-commit hook, tools/hooks/pre-commit); CI checks HEAD against HEAD~1.
// A hand edit fails here: feeds change only through tools/publish_feed.sh.
import { createPublicKey, verify } from "node:crypto";
import { execFileSync } from "node:child_process";
import { existsSync, readFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

export const FEEDS = { "latest.json": "stable", "latest-dev.json": "dev" };

// The base64 SPKI keys inside `TRUSTED_KEYS = [ ... ]`.
export function trustedKeys(source) {
  const block = /TRUSTED_KEYS\s*=\s*\[([\s\S]*?)\]/.exec(source);
  return block ? [...block[1].matchAll(/"([A-Za-z0-9+/]+=*)"/g)].map((m) => m[1]) : [];
}

function signedBy(data, sigText, keys) {
  const signature = Buffer.from(sigText.trim(), "base64");
  if (signature.length !== 64) return false;
  // loop: NOT-WORTH — N = 1 trusted key (2 during a rotation)
  return keys.some((k) => {
    try {
      return verify(null, data, createPublicKey({ key: Buffer.from(k, "base64"), format: "der", type: "spki" }), signature);
    } catch {
      return false;
    }
  });
}

// Schema 2's own fields (tools/manifest_lib.mjs buildFeed). Board names are
// checked against boards.json once it exists (design step 7), not here.
function checkSchema2(name, feed) {
  const problems = [];
  if (!/^\d+\.\d+\.\d+$/.test(feed.newest_stable ?? "")) problems.push(`${name}: newest_stable ${JSON.stringify(feed.newest_stable)} is not a stable version`);
  if (!Array.isArray(feed.firmwares)) return [...problems, `${name}: schema 2 needs firmwares[]`];
  for (const [i, f] of feed.firmwares.entries()) {
    const ok =
      typeof f?.version === "string" && typeof f.board === "string" && typeof f.chip === "string" && Number.isInteger(f.flash_min) &&
      Array.isArray(f.personas) && Array.isArray(f.displays) && typeof f.uf2?.url === "string" && /^[0-9a-f]{64}$/.test(f.uf2?.sha256 ?? "") && Number.isInteger(f.uf2?.size);
    if (!ok) problems.push(`${name}: firmwares[${i}] is missing version, board, chip, flash_min, personas, displays or uf2 (url, size, sha256)`);
  }
  // Installed apps offer `firmware`: it must be the Pico 2 W entry, byte for byte.
  if (feed.firmware) {
    const p2w = feed.firmwares.find((f) => f?.board === "pico2_w");
    if (!p2w || p2w.version !== feed.firmware.version || JSON.stringify(p2w.uf2) !== JSON.stringify(feed.firmware.uf2))
      problems.push(`${name}: firmware (what installed apps offer) is not the pico2_w entry of firmwares[]`);
  }
  return problems;
}

// Problems with one feed, in plain words; [] = it passes.
// data/sig/prev: Buffers or null (missing).
export function checkFeed({ name, data, sig, prev, keys }) {
  if (!data) return sig ? [`${name}.sig exists without ${name}`] : [];
  if (!sig) return [`${name} has no ${name}.sig`];
  if (!signedBy(data, sig.toString("utf8"), keys)) return [`${name}: the signature doesn't match (edited by hand, or signed with a key the app doesn't trust)`];
  let feed;
  try {
    feed = JSON.parse(data.toString("utf8"));
  } catch {
    return [`${name} is not valid JSON`];
  }
  const problems = [];
  if (feed.schema !== undefined && feed.schema !== 1 && feed.schema !== 2) problems.push(`${name}: unknown schema ${JSON.stringify(feed.schema)}`);
  if (feed.channel !== FEEDS[name]) problems.push(`${name}: channel is ${JSON.stringify(feed.channel)}, must be "${FEEDS[name]}"`);
  const issued = feed.issued_at === undefined ? null : Date.parse(feed.issued_at);
  if (feed.schema === 2 && issued === null) problems.push(`${name}: schema 2 needs issued_at`);
  if (Number.isNaN(issued)) problems.push(`${name}: issued_at ${JSON.stringify(feed.issued_at)} is not a date`);
  if (feed.schema === 2) problems.push(...checkSchema2(name, feed));
  let before = null;
  try {
    before = prev ? JSON.parse(prev.toString("utf8")).issued_at : null;
  } catch {
    // The previous version wasn't JSON: nothing to compare against.
  }
  if (before !== undefined && before !== null && !Number.isNaN(Date.parse(before))) {
    if (issued === null) problems.push(`${name}: issued_at was removed (the previous version has ${before})`);
    else if (issued < Date.parse(before)) problems.push(`${name}: issued_at ${feed.issued_at} is older than the previous version's ${before}`);
  }
  return problems;
}

function main(args) {
  const opt = (name) => {
    const i = args.indexOf(name);
    return i < 0 ? undefined : args[i + 1];
  };
  const staged = args.includes("--staged");
  const base = opt("--base") ?? "HEAD";
  const git = (...a) => execFileSync("git", a, { stdio: ["ignore", "pipe", "ignore"] });
  const root = git("rev-parse", "--show-toplevel").toString().trim();
  const show = (spec) => {
    try {
      return execFileSync("git", ["cat-file", "blob", spec], { cwd: root, stdio: ["ignore", "pipe", "ignore"] });
    } catch {
      return null;
    }
  };
  const current = (file) => (staged ? show(`:${file}`) : existsSync(join(root, file)) ? readFileSync(join(root, file)) : null);
  // Default: the app repo next to this one (next to the main checkout when run in a worktree), as tools/publish_feed.sh.
  const mainRepo = dirname(resolve(root, git("-C", root, "rev-parse", "--git-common-dir").toString().trim()));
  const trusted = opt("--trusted") ?? join(dirname(mainRepo), "DS5Dongle-OLED-Config-Web", "electron/main/feedSignature.ts");
  if (!existsSync(trusted)) {
    console.error(`verify_feeds: the app's public keys weren't found at ${trusted} (use --trusted PATH to feedSignature.ts)`);
    return 1;
  }
  const keys = trustedKeys(readFileSync(trusted, "utf8"));
  if (keys.length === 0) {
    console.error(`verify_feeds: no TRUSTED_KEYS in ${trusted}`);
    return 1;
  }
  const problems = [];
  let found = 0;
  for (const name of Object.keys(FEEDS)) {
    const data = current(name);
    if (data) found++;
    problems.push(...checkFeed({ name, data, sig: current(`${name}.sig`), prev: show(`${base}:${name}`), keys }));
  }
  if (found === 0) problems.push("no feed found (latest.json, latest-dev.json)");
  if (problems.length) {
    console.error(`verify_feeds: ${problems.join("\nverify_feeds: ")}`);
    console.error("verify_feeds: feeds change only through tools/publish_feed.sh (signed on the owner's PC); installed apps refuse a feed that fails this.");
    return 1;
  }
  console.log(`verify_feeds: ${found} feed(s) signed by a trusted key, channels match, issued_at not older than ${base}`);
  return 0;
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) process.exit(main(process.argv.slice(2)));
