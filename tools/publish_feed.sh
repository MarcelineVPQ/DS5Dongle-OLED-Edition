#!/usr/bin/env bash
# Rebuild, sign and publish the configurator app's update feeds (latest.json
# and latest-dev.json, each with its .sig) from the releases published on
# GitHub. Runs only on the developer's PC: the signing key never leaves it and
# CI never signs or writes a feed, so a release someone publishes with a
# stolen GitHub token never reaches installed apps.
#
#   tools/publish_feed.sh [--key PATH] [--app ../DS5Dongle-OLED-Config-Web] [--refresh]
#
# Run it:
#   - after every release (tools/release.sh), once GitHub shows the release
#     published (gh run watch -R MarcelineVPQ/DS5Dongle-OLED-Edition);
#   - after publishing a proton-* release: the newest one goes in both feeds.
# Every file in the feeds is hashed (tools/build_manifest.mjs), but a file
# already in ~/.cache/ds5-release/feed-hash-cache.json (same GitHub asset id,
# name, size and updated_at) is not downloaded again: only new release files
# are (the Proton build is ~565 MB). --refresh empties that cache first, so
# every file is downloaded once more (shared files once for both feeds).
# It shows what changed and asks before it commits and pushes to master.
set -euo pipefail

FW_DIR="$(cd "$(dirname "$0")/.." && pwd)"
KEY="$HOME/.config/ds5-release/manifest-signing-key.pem"
REFRESH=0
# the app repo sits next to the main checkout (a worktree lives deeper, so go from git's common dir)
APP_DIR="$(cd "$(git -C "$FW_DIR" rev-parse --path-format=absolute --git-common-dir)/../.." && pwd)/DS5Dongle-OLED-Config-Web"
while [ $# -gt 0 ]; do
  case "$1" in
    --key) KEY="${2:?--key needs a path}"; shift 2 ;;
    --app) APP_DIR="$(cd "${2:?--app needs a path}" && pwd)"; shift 2 ;;
    --refresh) REFRESH=1; shift ;;
    *) echo "publish_feed: unknown argument $1" >&2; exit 1 ;;
  esac
done
die() { echo "publish_feed: $*" >&2; exit 1; }
TRUSTED="$APP_DIR/electron/main/feedSignature.ts"

[ -f "$KEY" ] || die "no signing key at $KEY (use --key PATH)"
[ -f "$TRUSTED" ] || die "app repo not found at $APP_DIR (use --app PATH): needed to check the key is one the app trusts"
cd "$FW_DIR"
[ "$(git branch --show-current)" = master ] || die "$FW_DIR is not on master"
# The feeds are pushed to origin, which installed apps read through raw.githubusercontent.
[[ "$(git remote get-url origin)" =~ [:/]MarcelineVPQ/DS5Dongle-OLED-Edition(\.git)?$ ]] || die "origin is not MarcelineVPQ/DS5Dongle-OLED-Edition"
[ -z "$(git status --porcelain --untracked-files=no)" ] || die "$FW_DIR has uncommitted changes"
git pull --ff-only origin master

echo "== Feed selection and signing tests"
node --test tools/manifest_lib.test.mjs tools/sign_manifest.test.mjs tools/verify_feeds.test.mjs

echo "== Build the feeds from the published releases"
if [ "$REFRESH" = 1 ]; then rm -f "${XDG_CACHE_HOME:-$HOME/.cache}/ds5-release/feed-hash-cache.json"; fi
# Exit 2 = no release for that channel yet: nothing to write.
node tools/build_manifest.mjs --out latest.json || [ $? -eq 2 ]
node tools/build_manifest.mjs --channel dev --out latest-dev.json || [ $? -eq 2 ]
feeds=()
for f in latest.json latest-dev.json; do [ -f "$f" ] && feeds+=("$f"); done
[ ${#feeds[@]} -gt 0 ] || die "no feed was built"

echo "== Sign and verify"
# Only the key's path goes to the signer; it prints the public key alone.
out="$(node tools/sign_manifest.mjs --key "$KEY" "${feeds[@]}")"
echo "$out"
pub="$(sed -n 's/.*(key \(.*\))$/\1/p' <<<"$out" | head -n 1)"
[ -n "$pub" ] && grep -qF "\"$pub\"" "$TRUSTED" \
  || die "the key isn't in the app's TRUSTED_KEYS ($TRUSTED): installed apps would refuse this feed"
# Gate G1 (what CI and the pre-commit hook check): signature, channel, issued_at.
node tools/verify_feeds.mjs --trusted "$TRUSTED"

files=(latest.json latest.json.sig latest-dev.json latest-dev.json.sig)
if [ -z "$(git status --porcelain -- "${files[@]}")" ]; then
  echo "The feeds are unchanged."
  exit 0
fi
git --no-pager diff --stat -- "${files[@]}"
read -r -p "Commit and push the signed feeds to master? [y/N] " answer
if [ "$answer" != y ]; then
  git checkout -- "${files[@]}" 2>/dev/null || true
  echo "Not pushed; the feeds were put back."
  exit 0
fi
for f in "${files[@]}"; do [ -f "$f" ] && git add "$f"; done
git commit -m "chore(release): update latest.json / latest-dev.json"
git push origin master
