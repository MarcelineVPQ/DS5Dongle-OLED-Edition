// node --test tools/verify_feeds.test.mjs
// Gate G1 with a throwaway key made here (never the real one).
import assert from "node:assert/strict";
import { execFileSync, spawnSync } from "node:child_process";
import { generateKeyPairSync, sign } from "node:crypto";
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";
import { buildFeed } from "./manifest_lib.mjs";
import { checkFeed, trustedKeys } from "./verify_feeds.mjs";

const { privateKey, publicKey } = generateKeyPairSync("ed25519");
const pub = publicKey.export({ format: "der", type: "spki" }).toString("base64");
const keys = [pub];
const feed = (fields) => Buffer.from(JSON.stringify({ channel: "stable", app: { version: "0.9.5" }, ...fields }, null, 2) + "\n");
const sigOf = (data) => Buffer.from(sign(null, data, privateKey).toString("base64") + "\n");
const boards = JSON.parse(readFileSync(new URL("../boards.json", import.meta.url), "utf8")).boards;
const check = (data, extra = {}) => checkFeed({ name: "latest.json", data, sig: sigOf(data), prev: null, keys, boards, ...extra });

test("reads the keys out of the app's TRUSTED_KEYS", () => {
  const src = `export const TRUSTED_KEYS = [\n  "${pub}", // note\n];\nconst other = "AAAA";\n`;
  assert.deepEqual(trustedKeys(src), [pub]);
});

test("a signed feed with its own channel passes", () => {
  assert.deepEqual(check(feed({ issued_at: "2026-11-02T18:04:11Z" })), []);
});

test("a byte edit fails", () => {
  const data = feed({});
  const edited = Buffer.from(data.toString().replace("0.9.5", "0.9.6"));
  assert.match(checkFeed({ name: "latest.json", data: edited, sig: sigOf(data), prev: null, keys }).join(), /signature/);
});

test("another channel's feed in this file fails", () => {
  assert.match(check(feed({ channel: "dev" })).join(), /channel is "dev", must be "stable"/);
});

test("an issued_at older than the previous version fails", () => {
  const prev = feed({ issued_at: "2026-11-02T18:04:11Z" });
  assert.match(check(feed({ issued_at: "2026-11-01T00:00:00Z" }), { prev }).join(), /older than/);
  assert.match(check(feed({}), { prev }).join(), /removed/);
  assert.deepEqual(check(feed({ issued_at: "2026-11-02T18:04:11Z" }), { prev }), []);
});

test("a missing .sig fails", () => {
  assert.match(checkFeed({ name: "latest.json", data: feed({}), sig: null, prev: null, keys }).join(), /no latest\.json\.sig/);
});

test("the command checks staged feeds against HEAD in a git repo", () => {
  const dir = mkdtempSync(join(tmpdir(), "ds5-verify-"));
  try {
    const git = (...a) => execFileSync("git", ["-c", "user.name=t", "-c", "user.email=t@t", "-c", "commit.gpgsign=false", "-c", "core.hooksPath=/dev/null", ...a], { cwd: dir });
    const trusted = join(dir, "feedSignature.ts");
    writeFileSync(trusted, `export const TRUSTED_KEYS = [\n  "${pub}",\n];\n`);
    const put = (name, data) => {
      writeFileSync(join(dir, name), data);
      writeFileSync(join(dir, `${name}.sig`), sigOf(data));
    };
    git("init", "-q");
    put("latest.json", feed({ issued_at: "2026-11-02T00:00:00Z" }));
    put("latest-dev.json", feed({ channel: "dev", issued_at: "2026-11-02T00:00:00Z" }));
    git("add", ".");
    git("commit", "-qm", "feeds");
    const run = () =>
      spawnSync(process.execPath, [fileURLToPath(new URL("./verify_feeds.mjs", import.meta.url)), "--staged", "--trusted", trusted], { cwd: dir, encoding: "utf8" });
    assert.equal(run().status, 0, run().stderr);
    put("latest.json", feed({ issued_at: "2026-11-01T00:00:00Z" }));
    git("add", "latest.json", "latest.json.sig");
    const r = run();
    assert.equal(r.status, 1);
    assert.match(r.stderr, /older than/);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

// A schema-2 feed as tools/build_manifest.mjs writes it.
const uf2 = { url: "https://github.com/x/ds5-bridge-oled-v0.9.4-oled-edition.uf2", size: 2745856, sha256: "2f".repeat(32) };
const schema2 = (over = {}) => ({
  ...buildFeed({
    channel: "stable",
    issuedAt: "2026-11-02T18:04:11Z",
    newestStable: "0.9.4",
    app: { version: "0.9.4" },
    firmware: { version: "v0.9.4-oled-edition", notes_url: "n", uf2 },
  }),
  ...over,
});
const json = (o) => Buffer.from(JSON.stringify(o, null, 2) + "\n");

test("a signed schema-2 feed passes", () => {
  assert.deepEqual(check(json(schema2())), []);
});

test("schema 2 needs newest_stable, a complete firmwares[] and firmware = its pico2_w entry", () => {
  assert.match(check(json(schema2({ newest_stable: undefined }))).join(), /newest_stable/);
  assert.match(check(json(schema2({ newest_stable: "0.9.5-dev.20" }))).join(), /newest_stable/);
  assert.match(check(json(schema2({ firmwares: undefined }))).join(), /firmwares\[\]/);
  const f = schema2().firmwares[0];
  assert.match(check(json(schema2({ firmwares: [{ ...f, chip: undefined }] }))).join(), /firmwares\[0\]/);
  assert.match(check(json(schema2({ firmwares: [{ ...f, uf2: { ...uf2, sha256: "3f".repeat(32) } }] }))).join(), /pico2_w entry/);
});

test("schema 2: every firmwares[] board is a release row of boards.json with its chip, flash_min, personas and displays", () => {
  const f = schema2().firmwares[0];
  const one = (over) => check(json(schema2({ firmware: undefined, firmwares: [{ ...f, ...over }] }))).join();
  assert.equal(one({}), "");
  assert.match(one({ board: "pico_w" }), /"pico_w" is not a release board/);
  assert.match(one({ board: "nonesuch" }), /"nonesuch" is not a release board/);
  assert.match(one({ chip: "rp2350b" }), /differ from boards\.json/);
  assert.match(one({ flash_min: 2097152 }), /differ from boards\.json/);
  assert.match(one({ personas: ["ds5", "x"] }), /differ from boards\.json/);
  assert.match(one({ displays: ["none"] }), /differ from boards\.json/);
});
