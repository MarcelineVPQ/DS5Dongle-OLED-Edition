# Sourced by tools/dev_firmware.sh and tools/release.sh: picks the Pico SDK and
# the ARM toolchain for a local build and checks them against the pins in
# tools/toolchain.env (the same file CI builds with).
#
#   PICO_SDK_PATH        the Pico SDK (default ~/pico-sdk), its lib/tinyusb at
#                        TINYUSB_REF
#   PICO_TOOLCHAIN_PATH  an ARM GNU toolchain folder (the one holding bin/);
#                        unset = ~/.local/opt/arm-gnu-toolchain-<pin> if it
#                        exists (tools/install_toolchain.sh), else the
#                        arm-none-eabi-gcc on PATH
#
# e.g. PICO_SDK_PATH=~/sdk23/pico-sdk PICO_TOOLCHAIN_PATH=~/sdk23/arm-toolchain \
#        tools/dev_firmware.sh
#
# A mismatch is only a warning (CMake itself refuses a TinyUSB other than 0.21),
# but such a build isn't the binary CI would make.

toolchain_setup() {
  local root k v
  root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  while IFS='=' read -r k v; do
    [[ "$k" =~ ^[A-Z][A-Z0-9_]*$ ]] && printf -v "PIN_$k" '%s' "$v"
  done < "$root/toolchain.env"

  : "${PICO_SDK_PATH:=$HOME/pico-sdk}"
  export PICO_SDK_PATH
  [ -d "$PICO_SDK_PATH" ] || { echo "Pico SDK not found at $PICO_SDK_PATH (set PICO_SDK_PATH)" >&2; return 1; }
  # The pinned ARM toolchain where tools/install_toolchain.sh puts it, if present.
  if [ -z "${PICO_TOOLCHAIN_PATH:-}" ] && [ -x "$HOME/.local/opt/arm-gnu-toolchain-$PIN_ARM_TOOLCHAIN_VERSION/bin/arm-none-eabi-gcc" ]; then
    PICO_TOOLCHAIN_PATH="$HOME/.local/opt/arm-gnu-toolchain-$PIN_ARM_TOOLCHAIN_VERSION"
  fi
  if [ -n "${PICO_TOOLCHAIN_PATH:-}" ]; then
    export PICO_TOOLCHAIN_PATH
    TOOLCHAIN_CC="$PICO_TOOLCHAIN_PATH/bin/arm-none-eabi-gcc"
  else
    TOOLCHAIN_CC="$(command -v arm-none-eabi-gcc || true)"
  fi
  [ -x "$TOOLCHAIN_CC" ] || { echo "arm-none-eabi-gcc not found (set PICO_TOOLCHAIN_PATH)" >&2; return 1; }

  local sdk tusb gcc warn=()
  sdk="$(sed -nE 's/^ *set\(PICO_SDK_VERSION_(MAJOR|MINOR|REVISION) ([0-9]+)\).*/\2/p' \
    "$PICO_SDK_PATH/pico_sdk_version.cmake" | paste -sd.)"
  [ "$sdk" = "$PIN_PICO_SDK_REF" ] || warn+=("Pico SDK is ${sdk:-unknown}, CI uses $PIN_PICO_SDK_REF")
  sdk="$(git -C "$PICO_SDK_PATH" rev-parse HEAD 2>/dev/null || true)"
  [ "$sdk" = "$PIN_PICO_SDK_COMMIT" ] || warn+=("Pico SDK is at commit ${sdk:-unknown}, CI uses $PIN_PICO_SDK_COMMIT")
  tusb="$(git -C "$PICO_SDK_PATH/lib/tinyusb" rev-parse HEAD 2>/dev/null || true)"
  [ "$tusb" = "$PIN_TINYUSB_REF" ] || warn+=("TinyUSB is at ${tusb:-unknown}, CI uses $PIN_TINYUSB_REF")
  gcc="$("$TOOLCHAIN_CC" --version | head -n 1)"
  grep -qi -- "$PIN_ARM_TOOLCHAIN_VERSION" <<<"$gcc" \
    || warn+=("compiler is '$gcc', CI uses ARM GNU Toolchain $PIN_ARM_TOOLCHAIN_VERSION")
  local w
  for w in "${warn[@]}"; do echo "warning: $w (see tools/toolchain.sh)" >&2; done
  return 0
}

# A kept build folder configured with another SDK or compiler can't switch
# (CMake caches both), so start it afresh.
toolchain_fresh_build_dir() {
  local cache="$1/CMakeCache.txt" cc
  [ -f "$cache" ] || return 0
  cc="$(sed -n 's/^PICO_COMPILER_CC:FILEPATH=//p' "$cache")"
  if ! grep -qxF "PICO_SDK_PATH:PATH=$PICO_SDK_PATH" "$cache" \
      || [ "$(readlink -f "$cc")" != "$(readlink -f "$TOOLCHAIN_CC")" ]; then
    echo "== SDK or compiler changed: starting $1 afresh"
    rm -rf "$1"
  fi
}
