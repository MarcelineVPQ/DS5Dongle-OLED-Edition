// node --test tools/sign_manifest.test.mjs
import assert from "node:assert/strict";
import { execFileSync, spawnSync } from "node:child_process";
import { createPublicKey, generateKeyPairSync, verify } from "node:crypto";
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

const script = fileURLToPath(new URL("./sign_manifest.mjs", import.meta.url));

test("signs each feed with the key file and never prints the private key", () => {
  const dir = mkdtempSync(join(tmpdir(), "ds5-sign-"));
  try {
    const { privateKey } = generateKeyPairSync("ed25519");
    const pem = String(privateKey.export({ format: "pem", type: "pkcs8" }));
    const keyPath = join(dir, "key.pem");
    writeFileSync(keyPath, pem);
    const feeds = ["latest.json", "latest-dev.json"].map((f) => join(dir, f));
    feeds.forEach((f, i) => writeFileSync(f, `{"channel":"${i ? "dev" : "stable"}"}\n`));
    const out = execFileSync(process.execPath, [script, "--key", keyPath, ...feeds], { encoding: "utf8" });
    assert.ok(!out.includes("PRIVATE KEY") && !out.includes(pem.split("\n")[1]), "the private key never reaches the output");
    for (const f of feeds) {
      const sig = Buffer.from(readFileSync(`${f}.sig`, "utf8").trim(), "base64");
      assert.ok(verify(null, readFileSync(f), createPublicKey(privateKey), sig));
    }
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("refuses to run without a key file", () => {
  const r = spawnSync(process.execPath, [script, "latest.json"], { encoding: "utf8" });
  assert.equal(r.status, 1);
});
