# How it's built and tested

English only. The short version, in 7 languages, is in the [README](../README.md#-how-its-built-and-tested).

Two repositories make up the project: this one (firmware, releases and the update feed) and the app, [MarcelineVPQ/DS5Dongle-OLED-Config-Web](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web). Paths below starting with `app:` are in the app repository.

## The rule behind it

A rule that matters gets a gate (a script or test that fails the build), not just a sentence in a document. Quality passes are mechanical: a script lists every candidate (every loop, cache, error path…), each gets one written verdict, and new code adds its verdict in the same commit.

## Every change

### App (`app:.github/workflows/ci.yml`, on every push to master and every pull request)

| Check | Runs on | Where |
| --- | --- | --- |
| Unit, integration and security tests (1,620 tests in 134 files, with coverage) | Linux | `app:tests/` (Vitest) |
| The same tests, plus a desktop build | Windows, macOS | `test-windows`, `test-macos` jobs |
| Typecheck | Linux | `tsc -b` |
| ESLint, including no floating or misused promises and no empty `catch` | Linux | `app:eslint.config.js` |
| Prettier | Linux | |
| Electron security settings | Linux | `app:.hooks/check_electron_security.sh` |
| No unsafe DOM sinks (`innerHTML`, `eval`, `new Function`) | Linux | `app:.hooks/check_dom_sinks.sh` |
| One theme system: no literal colors outside the palettes | Linux | `app:.hooks/check_theme_colors.mjs` |
| Every duplicate candidate has a verdict | Linux | `app:tools/list_duplicates.mjs`, `app:docs/DEDUPE_AUDIT.md` |
| No byte-identical files | Linux | `app:tools/check_identical_files.mjs` |
| Every loop that waits or sits on a hot path has a verdict | Linux | `app:tools/list_loops.mjs`, `app:docs/LOOP_PASS_2026-10.md` |
| Every cache, memo and mirror has a verdict (key, invalidation, bound) | Linux | `app:tools/list_caches.mjs`, `app:docs/CACHE_PASS_2026-10.md` |
| Every error path has a verdict, no silent failure | Linux | `app:tools/list_errors.mjs`, `app:docs/ERROR_PASS_2026-10.md` |
| Every setting is a row inside a section | Linux | `app:tools/list_settings.mjs` |
| Long hints sit behind the ⓘ unless they must stay visible | Linux | `app:tools/list_hints.mjs` |

`npm run check` runs all of the above locally (typecheck, lint, format, every gate, the tests) and adds the lifetime gate: every listener, timer, subscription, stream, device, process and observer names its release point (`app:tools/list_lifetimes.mjs`, `app:docs/LIFETIME_PASS_2026-10.md`). A pre-commit hook runs most gates before a commit is made.

Tests worth naming: every locale has exactly the strings English has (`app:tests/unit/i18n.test.ts`); tile words fit in every language (`app:tests/unit/i18nFit.test.ts`); dependencies are exact versions (`app:tests/unit/dependencies.test.ts`); every inline lint suppression is listed (`app:tests/unit/lintSuppressions.test.ts`); with automatic update checks off nothing goes online (`app:tests/integration/updateConsent.test.ts`); the window, session, CSP and USB allowlists can't drift (`app:tests/security/`). The integration tests drive the app's protocol client against a fake board that follows the firmware's command rules (`app:tests/support/fakeDongle.ts`).

### Security scans (app)

- Semgrep (security-audit, secrets, TypeScript, React, JavaScript, OWASP Top 10) and Gitleaks over the full history: every push and weekly (`app:.github/workflows/security-scans.yml`).
- npm audit: fails on a high advisory in shipped dependencies; every push and weekly (`app:.github/workflows/npm-audit.yml`).

### Firmware (`.github/workflows/build.yml`, on every push to any branch)

| Check | Where |
| --- | --- |
| 185 host tests of the firmware's pure logic (calibration, button assignments, config, commands, stick curves, OLED, USB sleep and wake, haptics, slots…), built with AddressSanitizer and UndefinedBehaviorSanitizer, no hardware | `tests/host/` |
| Every loop that waits or sits on a hot path has a verdict | `tools/list_loops.py` |
| Every failure path has a verdict | `tools/list_errors.py` |
| Every acquisition has a verdict | `tools/list_lifetimes.py` |
| Five firmware builds: standard (Pico 2 W), debug, without the battery LED, the 320 MHz fallback, Pico W | |
| Time-critical code stays in RAM | `tools/check_ram_residency.sh` |
| The flash (XIP) setup is the expected one | `tools/check_xip_setup.sh` |
| The 7 READMEs match README.md (headings, links, language bar, dev version) | `tools/check_readmes.py` |
| `lib/*` stay submodule links | `tools/check_gitlinks.sh` |

Toolchain, Pico SDK and TinyUSB are pinned in `tools/toolchain.env`, so CI and releases build the same binary. `tools/analyze.sh` (local) adds section sizes, the biggest RAM and flash symbols, the deepest stack frames, clang-tidy and cppcheck.

## Every development build (X.Y.Z-dev.N)

- **UI checker, fast plan** (`app:tools/check_ui.sh`): its own app copy (own settings, virtual display, private D-Bus, no USB) with a simulated board. A pairwise covering array of language × window size (1100×900, 900×650) × theme × page group × controller model, every view at every size, and de, es, fr and pt-BR on every view at the smallest size. It fails on clipped or spilling text, contrast under WCAG AA, or a control without a name.
- **Resource budgets, quick run** (`app:tools/check_budgets.sh --quick`) on Linux, and the same measurement on the installed app in the Windows 11 VM (`app:tools/check_budgets_vm.sh`).
- **Windows**: the installed app in a Windows 11 VM (`app:tools/windows_vm_test.py`), on the real user path.
- **macOS smoke test** on a GitHub-hosted Apple Silicon Mac on every release tag (`app:.github/workflows/mac-smoke.yml`): builds the universal app as the release does, installs it as a quarantined download into /Applications, records what Gatekeeper says, starts it and drives it.
- **Release workflow** (`.github/workflows/release.yml`): the firmware checks above again, then the app's tests on Linux, Windows and macOS before each platform's build. The app version must match the tag.

## Before a stable release

- **UI checker, full plan** (`app:tools/check_ui.sh --plan full`): every screen and panel in all 7 languages at both sizes, every theme and style, every colorway of both controller models.
- **Resource budgets, long run** (`--long`, about 55 minutes): adds a 30-minute soak for memory growth. Linux and the Windows VM.
- **Hardware-in-the-loop** on the release firmware with a paired DualSense (`app:docs/TEST_PLAN.md`): auto-connect, live view of every input, haptic feedback strength saved across a power cycle, light bar, button assignments in a game, pairing slots, every trigger preset, audio counters with zero drops, restart and BOOTSEL, first-time setup of a blank Pico, the Windows USB repair, the narrator, the updater (refuses older versions, bad checksums and unsigned feeds), games with adaptive triggers and haptics.
- **Owner's QC:** a stable release ships after the planned set of features is done and the owner has signed it off after everyday use. Development builds reach the Development channel first.

## Efficiency

The limits live in `app:tools/budgets.json`, explained with the last measurement in [`app:docs/BUDGETS.md`](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web/blob/master/docs/BUDGETS.md):

| Budget | Limit |
| --- | --- |
| Idle frames, window shown, nothing changing | ≤ 10 per second on every page |
| CPU, shown and idle | < 2% of one core |
| CPU, hidden in the tray | < 0.5% of one core, every process |
| Memory after 10 minutes (without the GPU process) | ≤ 275 MB |
| Memory growth over a 30-minute soak | < 5 MB |

A failing budget is fixed, not raised; a limit changes only with the owner's OK and a written reason. Visual effects render on the GPU (WebGL2 shaders, compositor animations) with a cheap fallback when acceleration is off; nothing draws while the window is hidden. Every new visual states its idle cost.

Firmware: the audio path and other time-critical code run from RAM (checked in CI), the OLED skips unchanged frames, and the app's Pico 2 W page shows how long each part of the firmware's main loop takes.

## Security & trust

- **Signed update feed:** `latest.json` and `latest-dev.json` carry an Ed25519 signature made in CI (`tools/sign_manifest.mjs`); the app refuses a feed no trusted key signed (`app:electron/main/feedSignature.ts`). Each download is checked for size and SHA-256 before it is installed or written to the board, and the updater never installs an older version on its own.
- **Build provenance:** every release file gets a signed attestation from the release workflow. Check a download with `gh attestation verify FILE -R MarcelineVPQ/DS5Dongle-OLED-Edition`.
- **The app** ([`app:docs/SECURITY_AUDIT.md`](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web/blob/master/docs/SECURITY_AUDIT.md)): sandboxed renderer with context isolation and no Node access; a strict Content Security Policy (`script-src 'self'`, no remote origins); one fixed IPC table where every message checks its sender and is validated with zod; WebUSB limited to the board and the RP2350 bootloader; navigation locked to the app's own page; privileged actions only through fixed programs and arguments, after the user agreed. Dependencies are exact versions chosen at least two weeks after release.
- **No network unless allowed:** with *Check for updates automatically* off, the app fetches nothing on its own.
- **Faithful to the controller:** the board's DualSense HID descriptor stays byte-identical to a real DualSense (289 bytes) and its feature reports return the controller's real data, so games use their native DualSense path. The configurator talks over a separate USB interface that games never see.

## Accessibility & languages

- 7 languages (English, Chinese, German, Spanish, French, Japanese, Brazilian Portuguese) in the app and the README, kept complete by tests and `tools/check_readmes.py`.
- Controller features use Sony's official terms per language (`app:docs/GLOSSARY.md`).
- The UI checker judges contrast (WCAG AA) and control names in every language and theme.
- Color vision modes, Reduce motion, and keyboard use in every popup menu.
