<!-- Thank you! One focused change per pull request. CONTRIBUTING.md has the details. -->

## What this changes

<!-- What it does and why, in a few lines. Link the issue or discussion if there is one (Fixes #123). -->

## How it was checked

- [ ] `tests/host/run.sh build/host-tests` passes, and a fix to pure logic comes with a host test that fails without it
- [ ] `tests/host/fixtures/config/gen/stable_reads.sh` passes (the last stable release reads the settings saved now)
- [ ] `tools/check_gitlinks.sh`, `python3 tools/check_readmes.py`, `python3 tools/check_community.py` and `python3 tools/check_ci_pins.py` pass
- [ ] `python3 tools/list_loops.py --check`, `python3 tools/list_errors.py --check` and `python3 tools/list_lifetimes.py --check` pass
- [ ] Update-feed, `boards.json` or UF2-check code changed: `node --test tools/*.test.mjs` passes
- [ ] The firmware builds (`tools/dev_firmware.sh`, plus `--picow` if it touches the Pico W)
- [ ] User-facing change: an entry under `[Unreleased]` in `CHANGELOG.md`; a README change is in all 7 languages
- [ ] Protocol or `Config_body` change: `PICO_PROTO_*` bumped, a matching pull request in DS5Dongle-OLED-Config-Web, and `python3 tools/check_protocol_lockstep.py <app checkout>` passes

## Hardware tested on

<!-- Board (Pico 2 W / Pico W), OLED fitted or not, controller (DualSense / Edge), and what you did and saw. -->

## Systems tried

<!-- Linux (distribution) / SteamOS / Windows / macOS, and the game or app if it matters. -->

By opening this pull request you agree that your contribution is released under this repository's licence (MIT, LICENSE).
