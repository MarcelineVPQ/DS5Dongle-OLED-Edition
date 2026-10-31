// Sign the configurator app's update feeds: an Ed25519 signature over each
// file's exact bytes, base64, written to <file>.sig. The app refuses a feed no
// trusted key signed (DS5Dongle-OLED-Config-Web electron/main/feedSignature.ts
// holds the public keys).
//   node tools/sign_manifest.mjs --key ~/.config/ds5-release/manifest-signing-key.pem latest.json [latest-dev.json]
// Feeds are signed only on the developer's PC (tools/publish_feed.sh runs
// this): CI never has the key, so a release published with a stolen GitHub
// token is never signed for the app. The key is read from its file here and
// never printed; only its public half is.
import { createPrivateKey, createPublicKey, sign, verify } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";

const args = process.argv.slice(2);
const k = args.indexOf("--key");
const files = args.filter((_, i) => k < 0 || (i !== k && i !== k + 1));
if (k < 0 || !args[k + 1] || files.length === 0) {
  console.error("usage: node tools/sign_manifest.mjs --key KEY.pem FEED.json [FEED.json ...]");
  process.exit(1);
}
const key = createPrivateKey(readFileSync(args[k + 1]));
const pub = createPublicKey(key).export({ format: "der", type: "spki" }).toString("base64");
for (const file of files) {
  const data = readFileSync(file);
  const signature = sign(null, data, key);
  if (!verify(null, data, createPublicKey(key), signature)) throw new Error(`${file}: signature self-check failed`);
  writeFileSync(`${file}.sig`, signature.toString("base64") + "\n");
  console.log(`${file}.sig written (key ${pub})`);
}
