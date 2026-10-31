// node --test tools/verify_feeds.test.mjs
// Gate G1 with a throwaway key made here (never the real one).
import assert from "node:assert/strict";
import { execFileSync, spawnSync } from "node:child_process";
import { generateKeyPairSync, sign } from "node:crypto";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";
import { checkFeed, trustedKeys } from "./verify_feeds.mjs";

const { privateKey, publicKey } = generateKeyPairSync("ed25519");
const pub = publicKey.export({ format: "der", type: "spki" }).toString("base64");
const keys = [pub];
const feed = (fields) => Buffer.from(JSON.stringify({ channel: "stable", app: { version: "0.9.5" }, ...fields }, null, 2) + "\n");
const sigOf = (data) => Buffer.from(sign(null, data, privateKey).toString("base64") + "\n");
const check = (data, extra = {}) => checkFeed({ name: "latest.json", data, sig: sigOf(data), prev: null, keys, ...extra });

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
