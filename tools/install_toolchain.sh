#!/usr/bin/env bash
# Set up a local build like CI's: the pinned ARM GNU toolchain into
# ~/.local/opt/arm-gnu-toolchain-<version> (tools/toolchain.sh finds it there;
# the tarball's SHA-256 checked first), and ~/pico-sdk (or $PICO_SDK_PATH) at
# the pinned SDK and TinyUSB commits.
#   tools/install_toolchain.sh            both
#   tools/install_toolchain.sh --sdk-only / --gcc-only
set -euo pipefail
root="$(cd "$(dirname "$0")" && pwd)"
while IFS='=' read -r k v; do [[ "$k" =~ ^[A-Z][A-Z0-9_]*$ ]] && printf -v "$k" '%s' "$v"; done < "$root/toolchain.env"
sdk="${PICO_SDK_PATH:-$HOME/pico-sdk}"
gcc_dir="$HOME/.local/opt/arm-gnu-toolchain-$ARM_TOOLCHAIN_VERSION"
if [ "${1:-}" != "--sdk-only" ]; then
  if [ -x "$gcc_dir/bin/arm-none-eabi-gcc" ]; then echo "toolchain: already at $gcc_dir"
  else
    echo "toolchain: downloading $ARM_TOOLCHAIN_VERSION"
    tarball="$(mktemp --suffix=.tar.xz)"
    trap 'rm -f "$tarball"' EXIT
    curl -fL "$ARM_TOOLCHAIN_URL" -o "$tarball"
    # The SHA-256 pinned in tools/toolchain.env: a changed download stops here.
    echo "$ARM_TOOLCHAIN_SHA256  $tarball" | sha256sum -c - >/dev/null \
      || { echo "toolchain: the download's SHA-256 is not ARM_TOOLCHAIN_SHA256 in tools/toolchain.env; nothing installed" >&2; exit 1; }
    mkdir -p "$gcc_dir"
    tar -xJf "$tarball" --strip-components=1 -C "$gcc_dir"
  fi
fi
if [ "${1:-}" != "--gcc-only" ]; then
  [ -d "$sdk/.git" ] || git clone -q https://github.com/raspberrypi/pico-sdk "$sdk"
  git -C "$sdk" fetch -q --tags origin && git -C "$sdk" checkout -q "$PICO_SDK_REF"
  [ "$(git -C "$sdk" rev-parse HEAD)" = "$PICO_SDK_COMMIT" ] \
    || { echo "sdk: tag $PICO_SDK_REF is not PICO_SDK_COMMIT $PICO_SDK_COMMIT (tools/toolchain.env): the tag moved" >&2; exit 1; }
  git -C "$sdk" submodule update -q --init
  git -C "$sdk/lib/tinyusb" fetch -q origin "$TINYUSB_REF" && git -C "$sdk/lib/tinyusb" checkout -q "$TINYUSB_REF"
  echo "sdk: $sdk at $PICO_SDK_REF, TinyUSB ${TINYUSB_REF:0:12}"
fi
