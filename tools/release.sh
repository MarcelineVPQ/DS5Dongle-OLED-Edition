#!/usr/bin/env bash
# Release the DS5 Dongle OLED Edition: firmware X.Y.Z + configurator app X.Y.Z,
# one tag (vX.Y.Z-oled-edition) on both repos. The tag push starts
# .github/workflows/release.yml here, which builds the firmware, builds the
# app for every OS with that firmware bundled, publishes one release with
# everything attached, and refreshes latest.json.
#
#   tools/release.sh 0.8.0 [--app ../DS5Dongle-OLED-Config-Web]
#   tools/release.sh 0.9.1-dev.1      a development build: a GitHub pre-release,
#                                     offered only on the app's Development channel
#   tools/release.sh 0.9.5-dev.20 --label "Release Candidate 1"
#                                     a release name: in both CHANGELOG headings
#                                     ("## [X] — LABEL — date") and the GitHub
#                                     release's title (release.yml reads it there)
#
# Before running a stable release: both CHANGELOGs have a section for the
# version (firmware: "## [0.8.0-oled-edition]", app: "## [0.8.0]"). For a
# stable release after development builds (e.g. 0.9.5 after 0.9.5-dev.N), that
# section is a summary on top; the dev sections stay below it.
#
# A development build gets its own CHANGELOG section: this script renames
# "## [Unreleased]" to "## [X.Y.Z-dev.N-oled-edition] - YYYY-MM-DD" (firmware)
# and "## [X.Y.Z-dev.N] - YYYY-MM-DD" (app), puts a fresh empty [Unreleased]
# above it, and commits that in both repos with the release ("chore(release):
# TAG"). The release notes are those sections. It refuses a development build
# whose [Unreleased] is empty in both CHANGELOGs.
set -euo pipefail

VERSION="${1:?usage: tools/release.sh X.Y.Z[-dev.N] [--app PATH] [--label TEXT]}"
APP_DIR="$(cd "$(dirname "$0")/../.." && pwd)/DS5Dongle-OLED-Config-Web"
LABEL=""
shift
while [ $# -gt 0 ]; do
  case "$1" in
    --app) APP_DIR="$(cd "${2:?--app needs a path}" && pwd)"; shift 2 ;;
    --label) LABEL="${2:?--label needs a text}"; shift 2 ;;
    *) echo "release: unknown argument $1" >&2; exit 1 ;;
  esac
done
FW_DIR="$(cd "$(dirname "$0")/.." && pwd)"
TAG="v$VERSION-oled-edition"

die() { echo "release: $*" >&2; exit 1; }
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-dev\.[0-9]+)?$ ]] || die "version must be X.Y.Z or X.Y.Z-dev.N"
DEV=false
[[ "$VERSION" == *-dev.* ]] && DEV=true
[[ "$LABEL" =~ ^[A-Za-z0-9][A-Za-z0-9\ .]*$ ]] || [ -z "$LABEL" ] || die "--label: letters, digits, spaces and dots only"
[ -z "$LABEL" ] || [ "$DEV" = true ] || die "--label is for development builds (a stable release's section is written by hand)"
# PICO_SDK_PATH / PICO_TOOLCHAIN_PATH pick the SDK and compiler for the local
# build check (tools/toolchain.sh); CI builds the release itself.
. "$FW_DIR/tools/toolchain.sh"
toolchain_setup || die "no usable Pico SDK / ARM toolchain"
[ -f "$APP_DIR/package.json" ] || die "app repo not found at $APP_DIR (use --app PATH)"

for dir in "$FW_DIR" "$APP_DIR"; do
  [ "$(git -C "$dir" branch --show-current)" = master ] || die "$dir is not on master"
  [ -z "$(git -C "$dir" status --porcelain --untracked-files=no)" ] || die "$dir has uncommitted changes"
  git -C "$dir" rev-parse -q --verify "refs/tags/$TAG" >/dev/null && die "$TAG already exists in $dir"
done
if [ "$DEV" = false ]; then
  grep -q "^## \[$VERSION-oled-edition\]" "$FW_DIR/CHANGELOG.md" || die "firmware CHANGELOG.md has no '## [$VERSION-oled-edition]' section"
  grep -q "^## \[$VERSION\]" "$APP_DIR/CHANGELOG.md" || die "app CHANGELOG.md has no '## [$VERSION]' section"
fi

# Does CHANGELOG $1 have anything under its (first) [Unreleased] heading?
unreleased_has_entries() {
  awk '/^## \[Unreleased\]/ {on=1; next} on && /^## / {exit} on && NF && !/^### / {found=1} END {exit !found}' "$1"
}
# Rename CHANGELOG $1's [Unreleased] to "## [$2] - today" (with --label:
# "## [$2] — LABEL — today") under a fresh, empty [Unreleased].
cut_dev_section() {
  grep -q "^## \[Unreleased\]" "$1" || die "$1 has no '## [Unreleased]' section"
  grep -q "^## \[$2\]" "$1" && die "$1 already has a '## [$2]' section"
  local head="## [$2] - $(date +%F)"
  [ -z "$LABEL" ] || head="## [$2] — $LABEL — $(date +%F)"
  awk -v head="$head" '!done && /^## \[Unreleased\]/ {print; print ""; print head; done=1; next} {print}' "$1" > "$1.tmp"
  mv "$1.tmp" "$1"
}
if [ "$DEV" = true ]; then
  unreleased_has_entries "$FW_DIR/CHANGELOG.md" || unreleased_has_entries "$APP_DIR/CHANGELOG.md" \
    || die "[Unreleased] is empty in both CHANGELOGs: nothing to release"
fi

echo "== App checks"
(cd "$APP_DIR" && npm run check)
echo "== Firmware checks: submodule gitlinks, the 7 READMEs, host unit tests"
"$FW_DIR/tools/check_gitlinks.sh" "$FW_DIR"
python3 "$FW_DIR/tools/check_readmes.py" "$FW_DIR"
(cd "$APP_DIR" && node tools/check_identical_files.mjs "$FW_DIR")
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT
"$FW_DIR/tests/host/run.sh" "$BUILD_DIR/host-tests"
echo "== Firmware build check (release configuration)"
cmake -S "$FW_DIR" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_COMPANION_VENDOR=ON -DVERSION="$TAG" >/dev/null
cmake --build "$BUILD_DIR" --target ds5-bridge >/dev/null
"$FW_DIR/tools/check_ram_residency.sh" "$BUILD_DIR/ds5-bridge-oled.elf"
"$FW_DIR/tools/check_xip_setup.sh" "$BUILD_DIR/ds5-bridge-oled.elf" 2 11

if [ "$DEV" = true ]; then
  echo "== CHANGELOGs: [Unreleased] becomes [$VERSION]"
  cut_dev_section "$FW_DIR/CHANGELOG.md" "$VERSION-oled-edition"
  cut_dev_section "$APP_DIR/CHANGELOG.md" "$VERSION"
fi

echo "== App version $VERSION"
(cd "$APP_DIR" && node tools/update_version.mjs "$VERSION" && node tools/update_version.mjs --verify)

cat <<MSG

Ready to release $TAG:
  - app repo:      commit "chore(release): $TAG", tag $TAG, push master + tag
  - firmware repo: $([ "$DEV" = true ] && echo "commit \"chore(release): $TAG\" (CHANGELOG.md), ")tag $TAG, push master + tag
GitHub Actions then builds and publishes the release on MarcelineVPQ/DS5Dongle-OLED-Edition.
MSG
read -r -p "Push now? [y/N] " answer
if [ "$answer" != y ]; then
  # Leave nothing behind, so the next run starts from a clean tree.
  git -C "$APP_DIR" checkout -- package.json package-lock.json CHANGELOG.md
  git -C "$FW_DIR" checkout -- CHANGELOG.md
  echo "Not pushed; the app version bump and the CHANGELOG changes were undone."
  exit 0
fi

git -C "$APP_DIR" commit -am "chore(release): $TAG"
git -C "$APP_DIR" tag -a "$TAG" -m "DS5 Dongle OLED Edition $TAG"
git -C "$APP_DIR" push origin master "$TAG"
[ "$DEV" = true ] && git -C "$FW_DIR" commit -m "chore(release): $TAG" -- CHANGELOG.md
git -C "$FW_DIR" tag -a "$TAG" -m "DS5 Dongle OLED Edition $TAG"
git -C "$FW_DIR" push origin master "$TAG"

echo "Pushed. Watch: gh run watch -R MarcelineVPQ/DS5Dongle-OLED-Edition"
