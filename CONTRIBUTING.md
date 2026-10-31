# Contributing

Thank you for wanting to help. DS5 Dongle OLED Edition is free firmware for the Raspberry Pi Pico 2 W (and the Pico W) that brings a DualSense to the PC over Bluetooth, with an optional OLED status display. Its companion app is [DS5Dongle-OLED-Config-Web](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web) (its own [CONTRIBUTING](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web/blob/master/docs/CONTRIBUTING.md)).

- **Bugs:** open an issue with the bug form (the firmware version, your board, and the OLED Diagnostics screen or the app's diagnostics help most).
- **Questions, ideas, results:** [Discussions](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/discussions).
- **Security problems:** privately, see [SECURITY.md](SECURITY.md).
- **Upstream:** this is a fork of [awalol/DS5Dongle](https://github.com/awalol/DS5Dongle). A fix to the core bridge (Bluetooth, USB, audio) that applies there too is welcome upstream as well.

## Building

`tools/install_toolchain.sh` sets up the pinned Pico SDK, TinyUSB and ARM toolchain (`tools/toolchain.env`), the same as CI. `tools/dev_firmware.sh` builds a development firmware (`--picow` for the Pico W, `--flash` puts your board into BOOTSEL and copies the UF2). CLAUDE.md "Build" has the manual cmake steps and the build flags.

## Pull requests

Small and focused, one change each, against `master`. Before opening one, these must pass (CI runs them all, plus five firmware build variants):

```bash
tests/host/run.sh build/host-tests      # host unit tests (ASan + UBSan), no hardware
tools/check_gitlinks.sh                 # lib/opus and lib/WDL stay submodule gitlinks
python3 tools/check_readmes.py          # the 7 READMEs in step
python3 tools/check_community.py        # the community files are present
python3 tools/list_loops.py --check     # every loop has a verdict
python3 tools/list_errors.py --check    # every failure path has a verdict
python3 tools/list_lifetimes.py --check # every acquisition has a verdict
node --test tools/*.test.mjs             # update-feed selection and signing, the UF2 check (gate G2)
python3 tools/check_ci_pins.py          # CI actions pinned by commit, exact Node and toolchain pins
```

CI also runs the app repo's doc gates (CHANGELOG, file references, privacy) and its identical-files check on this repo, and `python3 tools/check_protocol_lockstep.py <app checkout>` (the app's config layout and protocol version against `src/config.h` / `src/cmd.h`). `git config core.hooksPath tools/hooks` runs the quick ones before each commit and checks the message is a Conventional Commit.

- **A fix comes with a check:** a host test in `tests/host/` that fails without it when the logic is pure, otherwise a description of the hardware check in the pull request (board, controller, operating system, what you did and what you saw, e.g. the OLED Diagnostics counters or `scripts/audio_diag.py`).
- **README changes go to all 7 languages** (`README.md` and its `CN`, `DE`, `ES`, `FR`, `JA`, `PT-BR` mirrors) in the same commit; `tools/check_readmes.py` checks them. Other documents are English.
- **User-facing changes** to `src/`, `lib/` or `CMakeLists.txt` add an entry under `[Unreleased]` in `CHANGELOG.md`.
- **The firmware and the app share a protocol.** The app reads `Config_body` (`src/config.h`) byte for byte (`src/protocol/config.ts` in the app repo): grow it only by appending, keep it shorter than 63 bytes, and bump `PICO_PROTO_MINOR` (`src/cmd.h`) for additions or `PICO_PROTO_MAJOR` for layout breaks. Such a change needs a matching pull request in the app repo; CLAUDE.md "Configurator protocol" describes every field.
- **Never stage anything under `lib/`** (submodules), and don't use `git commit -a` over them.
- Commit messages follow [Conventional Commits](https://www.conventionalcommits.org/) (`fix(bt): …`, `feat(oled): …`, `docs(readme): …`).
- The project's conventions and the non-obvious parts of the design are in CLAUDE.md (RAM-resident audio path, flash layout, the DualSense HID descriptor that must stay byte-identical).
- Reviews can take some days. Changes that don't fit the project's direction may be declined, with a reason.

By contributing you agree that your contribution is released under this repository's licence (MIT, [LICENSE](LICENSE)).
