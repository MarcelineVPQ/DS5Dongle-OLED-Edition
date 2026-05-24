# Changelog

All notable changes to **Pico2W DualSense 5 Bridge — OLED Edition** are documented here. This fork tracks [awalol/DS5Dongle](https://github.com/awalol/DS5Dongle) (upstream) and adds an optional OLED status display plus a security/correctness audit pass on the core bridge.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versioning approximates [SemVer](https://semver.org/) with the upstream version stream — the fork shares a major.minor with whatever upstream tag it is rebased on.

---

## [Unreleased]

### Added

- **Button remapping.** Any of the 16 digital controls (face buttons, D-pad, shoulders/triggers, stick clicks, Create/Options) can be remapped to any other — stored on the dongle, applied transparently before the host sees the report, so it works on every OS and every game with no host-side software. The remap table lives in its own dedicated flash sector (`PICO_FLASH_SIZE_BYTES - 3·FLASH_SECTOR_SIZE`, magic `DS5\x03`, below the slots sector) and survives reboot; identity (no remap) is the default. Multiple sources mapping to one target OR together (analog L2/R2 take the max); a source can be set to *disabled* (`0xFF`). The remap acts on the **outgoing host report copy only** — the raw input the OLED screens and the PS+Mute reboot combo read is untouched. Edited over the existing `0xF6`/`0xF7` vendor reports with a hardened `RM`+version frame (**no HID-descriptor change**, so Windows enumeration is unaffected) and a revision counter the host polls to confirm a write landed. New `src/remap.{h,cpp}`; apply logic + button set ported from [SundayMoments/DS5_Bridge](https://github.com/SundayMoments/DS5_Bridge) (credit). Dev helper `scripts/remap_test.py` exercises the path over `/dev/hidraw` without the web tool.

### Changed

- **BT microphone now has packet-loss concealment (PLC).** The mic decode path gained a small decoded-frame jitter buffer (8 frames) drained at a steady 10 ms playout cadence: bursty BT delivery is smoothed, and a dropped mic frame during an active session is concealed with an Opus PLC frame (`opus_decode(decoder, NULL, 0, …)`) instead of leaving a hole the host hears as a click/dropout. Playout pre-buffers 3 frames and stops after 300 ms of no real frames (so it never emits comfort noise when the mic is idle). A new **`Mic PLC:`** counter on the Diagnostics screen climbs only when concealment fires — effectively a live BT link-quality gauge. Verified: forced BT loss kept the captured audio gap-free (longest zero-run ~0 ms) while the counter climbed. Design ported from [SundayMoments/DS5_Bridge](https://github.com/SundayMoments/DS5_Bridge) (credit). Adds ~30 ms mic latency (the pre-buffer).

### Companion web tool

- **Visual button-remapping editor (new Remap tab).** `DS5Dongle-OLED-Config-Web` gains a dedicated **Remap** tab built on the new firmware remap protocol: click a button on a live, theme-aware DualSense diagram to reassign it, with the shoulders/triggers (L1/L2/R1/R2) floated to the corners as labeled glyphs + leader lines (they have no target in a front view). Remapped/selected buttons glow; a collapsible full dropdown list is kept as a fallback. Reads the remap block appended to the `0xF7` config response and writes over `0xF6` (func `0x10`), independent of `Config_body`. Controller outline + button glyphs are [Zacksly's "PS5 Button Icons and Controls"](https://zacksly.itch.io/ps5-button-icons-and-controls) (CC BY 3.0, recolored to `currentColor` and cropped), credited in the footer, each asset file, and a bundled license. Strings translated across all 7 locales.

---

## [0.6.8-oled-edition] — 2026-05-24

Two big items: **DualSense microphone over Bluetooth** (long believed impossible — turned out to be a single enable bit; credit [awalol](https://github.com/awalol/DS5Dongle) upstream) and a **USB 3.0 connection-interference watchdog**. UF2s attached to [the GitHub release](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/v0.6.8-oled-edition) (built by `.github/workflows/release.yml`). The companion `DS5Dongle-OLED-Config-Web` config tool gains a **BT microphone** toggle.

### Added

- **DualSense microphone over Bluetooth.** The controller's built-in mic now works over the dongle's BT pairing — decoded from the DS5's Opus stream and presented to the host as the standard DualSense USB capture device, usable by any app (Discord, OBS, in-game voice). This fork had previously documented BT mic as a hard Sony-firmware limitation (likely encrypted); that conclusion was **wrong** — it hinged on a single enable bit (`pkt[4]` bit 0 in the outbound `0x36` audio report). Credit to **[awalol](https://github.com/awalol/DS5Dongle)** (upstream) for identifying it. The DS5 streams mic as 71-byte Opus packets tagged in `0x31` reports (`(data[2]>>1)&1`); `src/audio.cpp` decodes mono→stereo to the UAC1 endpoint. **Always-on:** the enable is sticky once streaming, so a control-only `0x36` keep-alive asserts it at ~4 Hz only until frames arrive, then backs off — mic works with no game audio, at minimal BT traffic. **Toggle:** new `bt_mic_enable` config field (default on; off saves DS5 battery since always-on keeps its audio subsystem awake) — OLED **Settings → BT Mic** and the web config tool's **BT microphone** switch. `BLUETOOTH_AUDIO_NOTES.md` rewritten from "dead end" to the working mechanism; README gains a user-facing **"DualSense Microphone over Bluetooth"** section.

### Fixed

- **Connection no longer hangs permanently on the amber lightbar (USB 3.0 interference recovery).** Users reported the DualSense getting stuck mid-connect (solid amber/yellow, never enumerates) on USB 3.0 host ports while USB 2.0 worked — caused by USB 3.0's broadband ~2.4 GHz RF noise desensitizing the CYW43 Bluetooth radio. The firmware's connection flow had dead-end states (ACL-fail and auth-fail re-inquiry were commented out; the controller-type feature-packet wait had no timeout), so a single lost packet stalled forever until a replug. Added a **connection-attempt watchdog** (`src/bt.cpp`): a 10 s timeout armed when a connection commits to a device and cleared when it reaches USB enumeration; on expiry it tears down via the existing `HCI_EVENT_DISCONNECTION_COMPLETE` path and restarts inquiry, so a stalled connect auto-retries instead of hanging. Re-enabled the ACL-fail / create-connection-reject / auth-fail recovery paths for faster recovery when the controller *does* report a failure. The watchdog is inert during a healthy established session (no effect on normal play, slot-switching, or idle-disconnect). Helps any marginal-RF setup, not just USB 3.0.

### Documentation

- New README section **"USB 3.0 ports & Bluetooth interference"** + a Known Issues bullet: explains the 2.4 GHz RFI cause (referencing Intel's white paper) and lists mitigations (USB 2.0 port, short USB 2.0 extension cable, powered USB 2.0 hub, ferrite bead, distance/line-of-sight).

---

## [0.6.7-oled-edition] — 2026-05-23

UF2s attached to [the GitHub release](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/v0.6.7-oled-edition) (built by `.github/workflows/release.yml`).

### Changed

- **Charge ETA now shows a provisional estimate immediately on plug-in.** Instead of sitting on `~--m` for the ~15-20 min until the first 10% step is timed, the Status screen shows a default-rate estimate `~Nm?` (the trailing `?` marks it provisional) the moment charging starts. The `?` drops and the number switches to the measured rate once a clean 10% step completes. Default is ~15 min per 10% step (`kDefaultStepUs`), taper-weighted exactly like the measured path, so the provisional figure is in the right ballpark and self-corrects.

### Companion web tool

- `DS5Dongle-OLED-Config-Web` gains **lightbar controls** (mode dropdown + four favorite-color pickers) in the config view, the provisional charge-ETA token in the OLED preview to match this firmware, and translations for two preview notes that were English-only. Build housekeeping: `tsconfig.tsbuildinfo` is no longer tracked.

---

## [0.6.6-oled-edition] — 2026-05-23

Community-issue follow-ups: configurable OLED idle-ladder thresholds (#5) and a diagnostic counter clarifying the trigger-flow numbers (#6). UF2s attached to [the GitHub release](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/v0.6.6-oled-edition) (built by `.github/workflows/release.yml`). The companion `DS5Dongle-OLED-Config-Web` config tool gains matching screen-timeout controls and is synced to the v0.6.5+ `Config_body` layout (fixes a latent issue where saving via the old web tool would zero the lightbar fields).

### Added

- **Configurable OLED idle-ladder thresholds (issue #5, requested by @TerryFrench).** The dim and off tiers are no longer hardcoded at 2 / 15 min — two new `Config_body` fields `screen_dim_timeout` / `screen_off_timeout` (minutes, `0 = that tier disabled`, range `[0,250]`) are editable on the Settings screen (`ScrDim`/`ScrOff`) and persist to flash. Defaults preserve the previous 2 / 15 ladder; on upgrade the unset fields read as those defaults via the `config_valid()` clamp. The idle timer moved from `time_us_32()` to 64-bit µs so the full 250-min range is representable without the ~71-min wrap. Power users with always-on dongles can bias shorter; status-watchers can bias longer or set `0` to keep a tier lit.
- **`trig fold` counter on the Diagnostics screen (issue #6).** Counts trigger-bearing `0x02` host reports that arrived while the speaker stream was active and were therefore folded into the `0x36` audio frames (via `state[]`) instead of sent as a standalone `0x31`. Makes `trig_allow == to_bt(trig) + fold` visible, confirming the apparent `trig`/`tx` gap is audio-path folding, not dropped trigger reports.

---

## [0.6.5-oled-edition] — 2026-05-23

Charging UX (Status-screen battery ETA + amber lightbar pulse), persistent and screen-sticky lightbar control, and a charging-aware idle power ladder. UF2s attached to [the GitHub release](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/v0.6.5-oled-edition) (built by `.github/workflows/release.yml`).

### Added

- **Charge ETA on the OLED Status screen.** While the DualSense is charging, the battery line shows an estimated time-to-full (`~43m`) to the right of the battery icon. The DS5 only reports battery in 10 % steps over BT (`interrupt_in_data[52]` low nibble, 0–10; high nibble is power-state, 1 = charging), so a smooth countdown is impossible — instead `sample_charge_eta()` times how long each 10 % step takes and extrapolates the remaining steps. It shows `~--m` while calibrating (the first estimate can't appear until one full step has been timed, ~15–20 min after plug-in), then refines on each subsequent notch. The partial step in progress at plug-in is discarded so the first estimate isn't skewed by a half-measured step; a 3-entry moving average smooths the rest. **Li-ion taper correction:** a flat "time-per-step × steps-left" runs optimistic in the constant-voltage tail, so each measured step is normalised to a bulk-equivalent duration (divide out a per-step weight: 1.0× in the bulk region, 1.5× for 80→90 %, 2.2× for 90→100 %) and the remaining steps are re-weighted — keeping the estimate consistent whether the user plugs in near-empty or near-full. Sampled once per frame from `oled_loop` ahead of the idle power-ladder early-returns, so step timing stays correct even while the panel is dimmed/off or the user is on another screen.
- **Lightbar settings persist across reboot and stick across every screen.** The selected lightbar mode and the four favorite colors are now saved to the config flash sector (new `Config_body` fields `lightbar_mode` + `lb_fav_{r,g,b}[4]`), so a chosen mode/color survives a power cycle. A new **HOST** mode (the default) hands the LED back to the host/game so the dongle doesn't hijack player-indicator LEDs out of the box; on upgrade from ≤0.6.4 the unset field reads as HOST, preserving prior behavior. Mode/favorite edits made on the Lightbar screen are batched into a single flash write when you navigate away (tracked by a dirty flag) to spare flash endurance.
- **Lightbar pulses amber-orange while charging.** A slow ~4.6 s breathing pulse (base `(255,100,0)`, sine-enveloped from dim to bright via the existing 32-step LUT) shows charging at a glance from any screen. Implemented in the unified `lightbar_service()` (below) as the top-priority owner of the LED, so it overrides the selected mode while charging and reverts to it when unplugged.

### Changed

- **The OLED no longer fully sleeps while the controller is charging.** The idle power ladder is capped at the Dim tier (the low-power breathing dot) instead of advancing to full Off (`cmd(0xAE)`) when `g_charge_eta.charging` is true. The charge-ETA tracker already runs while the panel is off, but users were unplugging the controller to "wake" the dongle — which reset the ETA calibration and restarted the wait-for-the-next-10%-notch. Capping at the dot tier (which draws ~no current) removes the reason to unplug. Normal Active→Dim→Off behavior resumes once charging stops.
- **A single `lightbar_service()` now owns the controller LED, every frame, on every screen.** Previously the OLED only drove the lightbar via a transient `0x31` packet sent from inside `render_screen_lightbar()` — so the color was only asserted while that screen was open. The service (run from `oled_loop` ahead of the power-ladder early-returns) instead writes the chosen color into the persistent `state[]` block (`SetStateData` `LedRed/Green/Blue`, via new `state_set_led()`), so it rides every outbound host/audio packet, and also actively pushes a `0x31` when audio is idle so animations keep moving. A new `g_lightbar_override` flag gates `state_update()` so the host's `AllowLedColor` writes can't stomp a firmware-chosen mode. During audio the active `0x31` push is suppressed — the `0x36` frames already carry `state[]`'s LED, and slipping a `0x31` between them would intrude on the load-bearing audio path.

### Fixed

- **OLED idle dim/dot tier now actually engages while a controller is connected.** The activity detector hashed `interrupt_in_data[0..9]` with an exact compare, but the analog sticks jitter by ±1 LSB at rest, so the hash changed every few frames and reset the idle timer — meaning the breathing-dot/dim tier only ever kicked in when no controller was paired. Now it mirrors `bt.cpp`'s inactivity heuristic: the stick bytes' rest band `[120,140]` is collapsed to a constant and the volatile counter byte (`idata[6]`) is skipped, so a resting controller reads as idle. Confirmed against a live `/dev/hidraw` capture (only the left-stick X byte was flickering 129↔128).
- **Lightbar no longer reverts the instant you leave the Lightbar screen.** Root cause: the OLED's `send_lightbar_color()` wrote a one-off `0x31` packet and never touched the persistent `state[]` block, while the host's `0x02` output reports, every audio frame, and reconnect all re-stamp `state[]` (incl. the LED) into the controller. Off the Lightbar screen the OLED stopped pushing, so the next `state[]`-based packet overwrote the color — which is why saved favorites and animated modes (Rainbow/Breathing/Fade) never "stuck." Now that the lightbar is owned through `state[]` with a host override gate (see Changed), the selected mode holds across screens and through active gameplay/audio.

---

## [0.6.4-oled-edition] — 2026-05-19

Trigger-flow diagnostics (in response to issue #3) + the OLED idle power ladder. UF2s attached to [the GitHub release](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/v0.6.4-oled-edition) (built by `.github/workflows/release.yml`).

### Added

- **OLED idle power ladder.** Replaces the single-tier 5-min auto-dim with a three-stage state machine: at 2 min idle the panel wipes black and a 2×2 "breathing dot" (1 s on / 1 s off) walks through 8 evenly-spaced positions every 30 s; at 15 min idle the SH1107 is sent `cmd(0xAE)` (display off) entirely. Wakes instantly on KEY0/KEY1, controller pair (BT-connect rising edge), or any input-report change. Why this shape: on the Waveshare panel, bench-testing `kDimContrast = 0x10` and `0x02` both produced only ~10 % perceptual reduction (SH1107's contrast register vs apparent brightness is heavily non-linear on this hardware), so the only reliable per-pixel dim available is *rendering fewer pixels*. The breathing dot lights ~4 of 8 192 pixels half the time — roughly a 1 000× drop in cumulative current — while still indicating "the dongle is alive," and the rotating position spreads OLED wear across the panel.
- **Trigger-flow diagnostic counters on the Diagnostics screen.** `host02` (total `0x02` HID OUT reports from host) / `trig` (those where the host set `AllowRight|LeftTriggerFFB` in `valid_flag0`) / `tx` (forwarded as BT `0x31` sub-`0x10`). Added in response to issue #3 ("trigger tension missing in Death Stranding 2"). Lets the user triage in one game session whether the dongle, the host driver, or the controller is the source of the missing adaptive-trigger effect — without a UART or BT sniffer.
- **Diagnostics screen now scrolls with the controller D-pad.** Refactored to a row-list (10 rows currently: Uptime / BT state / host02 / trig+tx / BT31 in/s / USB aud/s / BT32 out/s / Mic in/s / Mic dec=&w= / Mic prefix). 5 rows visible at a time; ▲/▼ glyphs at the right edge mark "more above/below." Read-only — no cursor, unlike Settings, since there's nothing to select.
- **Host-side trigger-flow triage via `scripts/mic_diag.sh bt-trace`.** The firmware's `0xFD` vendor feature report grew a second section (bytes 32–43) with the trigger counters; `bt-trace`'s Python decoder now reads them and prints a one-line verdict — "host driver isn't setting Allow*TriggerFFB" / "trigger Allow bits set but speaker path stole the BT pipe" / "full chain reached the controller". Lets the user diagnose issue #3 without a UART cable or OLED-relay-per-flash.
- **`README.md` "Diagnostics & debug tooling" section** documents `scripts/mic_diag.sh` and its subcommands. The script existed but was only mentioned inside `BLUETOOTH_AUDIO_NOTES.md` — invisible to anyone who hadn't already read the parked-mic notes.

### Changed

- **`flush_fb()` split.** Internal refactor: `flush_fb_raw()` writes just the framebuffer; `flush_fb()` is now `draw_button_chrome() + flush_fb_raw()`. Lets the dim-tier renderer push the breathing dot without the K0/K1 chrome arrows (no navigation target while the panel is asleep).
- **Diagnostics row order re-prioritized.** The first 5 rows (always visible without scrolling) cover the most common triage path: Uptime / BT state / `host02` / `trig`+`tx` / `BT31 in/s`. Audio + parked-mic-investigation counters live below the fold.

---

## [0.6.3-oled-edition] — 2026-05-18

Small follow-up to v0.6.2. UF2s attached to [the GitHub release](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/v0.6.3-oled-edition) (built by `.github/workflows/release.yml`).

### Fixed

- **OLED Status header was stuck on `"DS5 Bridge v0.6.0"`.** The string was hardcoded in `src/oled.cpp` and never got bumped per release, so v0.6.1 and v0.6.2 both shipped with stale text on the Status screen. Now driven by a compile-time `FIRMWARE_VERSION` macro set from `CMakeLists.txt`'s `${VERSION}` (which `release.yml` already passes as `-DVERSION="$FIRMWARE_VERSION"`). Single source of truth: the release tag. Local builds without `-DVERSION` show `"dev"` so an untagged build is obvious at a glance.
- **Web preview's Status header had the same bug.** `src/oled/screens.ts` hardcoded `"v0.5.4"`. Now reads `firmware-latest.json` (already CI-bundled from the GitHub API) at runtime in `OledEmulator.tsx` and writes the short tag (suffix `-oled-edition` stripped) into `state.firmwareVersionLabel`, which `renderStatus()` consumes.

### Documentation

- New `CLAUDE.md` "Versioning — single source of truth" section documents the release ritual (CHANGELOG bump → tag → push → `gh release create`) and the single-source-of-truth flow from tag → CMake → C++ macro → web `firmware-latest.json`. Includes a note about the still-pending `WEB_REPO_DISPATCH_PAT` secret on the firmware repo.

---

## [0.6.2-oled-edition] — 2026-05-18

OLED button-model + visual chrome refactor on top of v0.6.1. UF2s attached to [the GitHub release](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/v0.6.2-oled-edition) (built by `.github/workflows/release.yml`).

### Changed (button model)

- **KEY0 / KEY1 are now strictly navigation on every screen.** KEY0 short-press = next screen, KEY1 short-press = previous screen. KEY1 long-press still cycles OLED brightness (unchanged). The old contextual K1=cycle behavior on Trigger Test (cycle trigger preset) and Lightbar (cycle lightbar mode) moved to **DualSense controller buttons** — Triangle on Trigger Test, R1 on Lightbar. Source of "the Mode label didn't change when I clicked K1" confusion eliminated.
- **KEY0 double-click reboot → KEY0 + KEY1 simultaneous hold (≥ 1 s).** Rapid forward-navigation kept tripping the double-click timer by accident, soft-rebooting the dongle mid-session. The new two-button chord can't be fat-fingered. `kDoubleClickUs` + `key0_pending_single` state removed; new `chord_held_since_us` + `kChordHoldUs = 1 s`. DS5 PS+Mute hold-2 s remains the headless backup.
- **Per-screen contextual actions on the controller** mirror the existing Slots / Settings conventions (where Triangle has always meant "commit / switch / save"):
  - **Trigger Test** — △ rising edge cycles `trigger_preset` and re-applies via `send_trigger_effect()`.
  - **Lightbar** — R1 rising edge cycles `lb_mode`. (Triangle stays as "save current RGB to favorite slot 0" — the existing favorite-save UX.)

### Changed (visual chrome)

- **Arrow chrome on the left edge of every screen.** `flush_fb()` now paints `>` at `(0, 8)` and `<` at `(0, 49)` so the on-screen labels physically pair with the KEY0 (top) and KEY1 (bottom) buttons. The horizontal `"K0=next K1=back"` footer at y=56 is removed from all 11 screens. Trigger Test footer = `"Tri=cycle"`; Lightbar footer = `"R1=mode"`; Slots and Settings keep their existing contextual hints (`"Tri=switch Sq hold=wipe"`, `"DP nav/adj Tri=save"`). New `kContentX = 6` shifts every screen's content right by 6 px to clear the chrome strip; rectangles, sticks, and the L1/L2 column on Status all repositioned to avoid the chrome `<` glyph painting inside the live left-stick area.

### Added (web preview parity)

- **`src/protocol/ds5BridgeHid.ts` `sendTriggerPreset(preset)`** — builds the DS5 SetStateData payload byte-for-byte from `src/oled.cpp send_trigger_effect()` and pushes via `device.sendReport(0x02, ...)`. The dongle relays it over BT to the paired controller, so cycling Trigger Test in the web preview actually drives the real adaptive triggers.
- **Web preview mirrors the firmware refactor.** `key1Action()` collapsed to back-nav-only. New rising-edge handlers in `OledEmulator.tsx` detect Triangle / R1 / D-pad from the live controller's input report and dispatch to the appropriate per-screen action. `drawButtonChrome(fb)` paints the `>` / `<` arrows after every render. `flush()` accepts an optional tint color: Slots / Diagnostics / CPU/Clock render in **orange** (`#f59e0b`) when a controller is connected — Chrome WebHID can't expose those reports on a stock DualSense descriptor, so the orange tint + an explanatory paragraph below the canvas flag the values as mock. KEY0/KEY1 buttons in the UI moved to sit visually next to the rendered Pico-OLED-1.3, mirroring the physical board.
- **Settings cursor on the web** — new `settingsSel` state, `>` cursor mark on the selected row; D-pad up/down on the connected controller moves the cursor (web-preview-only — actual edits + save happen via the dedicated Config tab on the website).
- **Mock-data temperature tweak** — web Preview's CPU/Clock screen no longer drifts 41–47 °C; jitter is now ±0.4 °C around 33.6 °C (realistic Pico 2 W idle).

### Documentation

- **README "Web Config Tool" section** added near the top, linking https://marcelinevpq.github.io/DS5Dongle-OLED-Config-Web/#config and explaining the three tabs (Flash / Config / OLED Preview). Includes a BOOTSEL mode primer for first-time flashers.
- **OLED Display Add-on section rewritten.** Screen count 10 → 11 (CPU/Clock added). Cycle order updated. New "Button reference" table covers the strict K0/K1 nav, K1 long-press brightness, and K0+K1 chord reboot. ASCII mockups dropped in favor of consistent web-preview screenshots under `assets/oled/`.
- **Performance / Overclocking section reworded** to lead with "you don't need to do anything — the overclock is baked into the firmware". The "raise voltage / lower clock if it fails to boot" line is scoped to users compiling from source.

---

## [0.6.1-oled-edition] — 2026-05-18

Tagged release of the v0.6.0-oled-edition follow-up. UF2s attached to [the GitHub release](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/v0.6.1-oled-edition) (built by `.github/workflows/release.yml`).

### Added

- **CPU / Clock diagnostics screen** (`kScreenCpu`, inserted between Diagnostics and BT Signal in the K0 cycle). Shows the configured system clock (`SYS_CLOCK_KHZ` — the overclock target), the *actually running* `clk_sys` measured live by the RP2350 on-chip frequency counter against the crystal reference, the core voltage read back from the regulator (`vreg_get_voltage()`, not the compile-time constant), and the RP2350 on-die temperature (ADC input 4). Pure read-only instrumentation; one-time ADC bring-up and no other code path uses the ADC, so it is conflict-free. `render_screen_cpu()` is `noinline` like the other render functions (Thumb literal-pool reach). Adds `hardware_adc` to `target_link_libraries`. The frequency-counter measurement (a multi-ms busy-wait) runs **once on screen entry** and is cached — `clk_sys` is fixed at boot, so only the temperature refreshes per frame, avoiding a per-frame BT/audio hitch while the screen is visible. `oled_loop()` gained a generic `screen_entered` flag for this. Hardware-verified on Pico 2 W + OLED. Also exported over a new HID feature report **`0xfc`** (`src/cmd.cpp`) — 11 bytes: set_khz, cached real_khz, vreg code, ADC ch4 raw — so the web config emulator can show live CPU telemetry (volts/temp math done web-side to keep the firmware HID path float-free).

### Fixed (web telemetry — latent since the slots/diag reports landed)

- **CPU/Clock temperature was a single noisy ADC sample.** The RP2350 temp sensor has a shallow slope (−1.721 mV/°C, ~1 LSB ≈ 0.47 °C) so a lone 12-bit reading swings several tenths of a degree per frame — the displayed value just mirrored the latest noisy sample instead of the true die temperature. New `cpu_temp_raw_smoothed()` in `src/cmd.cpp` averages a 256-sample block then runs a slow EMA (α=0.15, seeded on first call). It is the **single source of truth**: both `render_screen_cpu()` and the `0xfc` web telemetry call it, and the duplicated per-site ADC bring-up was removed (ADC now initialised in exactly one place). `oled.cpp` no longer touches the ADC directly (drops `hardware/adc.h`, adds `cmd.h`).
- **Live web telemetry over WebHID: not feasible on the target setup; abandoned.** A browser-side read-only diagnostic proved Chrome WebHID returns `NotAllowedError` for any report ID **not declared** in the parsed HID report descriptor (declared `0xF7`/`0xF8`/`0xF9` read fine; undeclared `0xFA`/`0xFB`/`0xFC` fail). Declaring them is therefore mandatory for the web read — but doing so (even applied atomically with a matching `wDescriptorLength`, correct bytes identical in shape to the working `0xF6`–`0xF9`, and a `bcdDevice` cache-bust bump) made the device fail to enumerate as a usable HID device on the user's real Windows machine in **two** independent attempts (Device Manager showed it; WebHID and the PlayStation Accessories app did not). The cloned DualSense HID report descriptor cannot be safely extended on this environment. Reverted to the original descriptor (`0x0141`/`0x01B5`, no vendor feature reports, `bcdDevice 0x0100`). Retained with **no USB impact**: the `0xfc` firmware handler and the temperature smoothing/`cpu_temp_raw_smoothed()`. The on-device CPU/Clock OLED screen is fully working and hardware-verified; the web preview's CPU screen stays on representative mock values (the slots/diagnostics web screens were never readable for the same root cause and are likewise mock-only when connected).

### Fixed

- **Low-battery LED keeps blinking after controller disconnect** (`fb68ea5`). When the DualSense's battery dropped low enough to trigger `battery_led_tick`'s blink and the controller subsequently disconnected (typically: battery fully depletes and the BT link drops), the Pico's onboard LED stayed frozen in whichever half-cycle it was in at the moment of disconnect; reconnect-retry windows could even briefly resume blinking. New `battery_led_on_disconnect()` clears blink state, forces LED off, and zeros `last_report_us` so the stale-check early-return blocks any new blink until a fresh 0x31 report arrives on the next connection. Stale-check in the tick also now forces LED off when it fires mid-blink (defense in depth for ungraceful disconnects). Reported by Sura Academy on Discord. Same bug present in upstream — sent back as [awalol/DS5Dongle#101](https://github.com/awalol/DS5Dongle/pull/101).

---

## [0.6.0-oled-edition] — 2026-05-17

Tagged release of the rebase below. UF2s attached to [the GitHub release](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/v0.6.0-oled-edition) (built by `.github/workflows/release.yml`). No code changes vs the rebase; tag exists so users can install from a stable artifact.

---

## [0.6.0-rebase] — 2026-05-17

Rebased onto upstream `awalol/DS5Dongle` `v0.6.0-hotfix`. All OLED Edition features preserved with no user-visible regression.

### Changed

- **Adopted upstream's `state_mgr` refactor** (`awalol/DS5Dongle#93`) as the new base for controller-state and audio-packet construction. The local speaker / HD-haptic regression fix we shipped in `bff65d6` (re-introducing a 63-byte `state_data` block into every audio packet) is **dropped** — upstream's `state_mgr.cpp` is the proper architectural fix and supersedes the hack. Speaker, HD haptics, and basic rumble all verified working on the rebased firmware. Same fix loteran/DS5Dongle shipped as `c7a8d3c` ~6 h before ours.
- All upstream commits between our prior base (`a2e3a33`) and `v0.6.0-hotfix` are now in our tree: `0a33aae` PR#93 merge, `b1019fb` README update, `54a4b69` config struct comment typo, `f882ff1` adjust default state-init volume, `77bbee5` rumble transfer fix, `c2e0d84` state init after connected, `7bbe37b` state_mgr refactor itself, `9a2c2b4` discoverable / connectable off when connected, `508a841` DISABLE_SPEAKER_PROC option, `b308545` audio.cpp comment, `0ed05d3` rumble hotfix.
- Our `update_discoverable()` helper (gates `gap_discoverable_control` on `slots_any_empty()`) replaces upstream's stricter `gap_*_control(false)` pair on L2CAP connect. Effect: dongle stays discoverable while at least one slot is empty (needed for slot-1/2/3 pairing); only goes dark when all 4 are full. Strictly looser than upstream's rule, but correct for the multi-slot use case.

### Fixed

- **`awalol/DS5Dongle#100` "(v0.6.0) Dongle is detected as a new device on Windows when using different USB ports"** — upstream's `e79c762 remove usb serialnumber #32` zeroed the device descriptor's `iSerialNumber` to work around a SpecialK compat issue, but that broke Windows device identity: users lost per-device volume / app settings every time they moved the dongle to a different USB port. The OLED Edition restores `iSerialNumber = STRID_SERIAL` (per-board unique serial from flash chip ID via `board_usb_get_serial`). Trade-off documented in `src/usb_descriptors.cpp`: re-introduces SpecialK incompatibility (`#32`) for the narrower set of Windows users with that specific tool, in exchange for stable device identity for the broader Windows population.

### Verification

End-to-end on user's hardware after the rebase:
- DS5 pairs cleanly.
- Speaker audio via `scripts/test_speaker.sh --tone 440 3` — audible (load-bearing check that upstream's state_mgr does what we expect).
- Basic rumble works in games (validates we didn't disturb upstream's `0ed05d3` hotfix).
- All 10 OLED screens render correctly with live data.
- Multi-slot pairing: switch between slots, slot_assign on empty, wipe-from-Settings all work.
- PS + Mute hold-2s combo reboot triggers `watchdog_reboot` (validates our `interrupt_loop` addition coexists with upstream's `state_update` flow).

### Unchanged

The full prior CHANGELOG history for `[0.5.4]` and earlier sessions remains accurate and below.

## [Unreleased]

### Fixed

- **DualSense speaker + HD haptic actuator regression** — upstream commit `3a31bd7` (2026-05-12, "refactor: add SetStateData and audio send priority") moved the `0x10` SetStateData sub-report out of every `0x36` audio packet and into a one-time L2CAP-open setup. The DS5 hardware requires that sub-report to be re-asserted on every audio frame (the `0x7f 0x7f` Headphones+Speaker volume bytes specifically) or the speaker and HD haptic actuators silently stop producing output. Restored in `src/audio.cpp` with the pre-3a31bd7 packet layout (state_data at `pkt[11..75]`, haptic at `pkt[76..141]`, speaker at `pkt[142..343]`). Same fix shipped independently by loteran/DS5Dongle as commit `c7a8d3c` ~6 h before ours; credit to loteran for the clearer hardware-side explanation in their commit message.
- **USB UAC1 SET_CUR Volume request no longer overrides flash-persisted speaker_volume.** PipeWire / PulseAudio re-apply their last-known UAC1 volume on every device reconnect, which had silently overridden whatever the user had saved in the OLED Settings menu. The in-memory `volume[]` array still tracks live host volume; only the flash sync was removed. Fix borrowed from loteran/DS5Dongle commit `03fa1e4`.

### Added

- **Audio Auto Haptics** — derive haptic feedback from the speaker audio (UAC channels 0/1) for games that send sound but no per-frame haptic data, e.g. Ghost of Tsushima on Linux + Steam. DSP is a 1-pole low-pass + envelope follower + modulation + soft-clip (`x / (1 + |x|)`, avoids `tanhf` on Cortex-M33), borrowed from loteran/DS5Dongle commit `5d6bc2f`. Four modes selectable from the OLED Settings menu: **Off** / **Fallback** / **Mix** / **Replace**. Default is **Fallback** — derived rumble fires only after the game has been silent on the native haptic path (channels 2/3) for ~1 s, so games that send native HD haptics (Spider-Man Remastered) are not overridden, while games that don't (Ghost of Tsushima) get derived haptics out of the box. Gain (0–200 %) and LP cutoff (80 / 160 / 250 / 400 Hz) are also tunable from the Settings menu.
- **Audio Diagnostics counters** on the OLED Diagnostics screen: `USB aud N/s` (UAC1 frames per second arriving from the host) and `BT 0x32 N/s` (audio packets emitted to the DS5 per second). Lets the user verify the speaker/haptic path is actually moving bytes without needing a UART cable. Used to triage the speaker regression above.

---

## [0.5.4] — 2026-05-16

First full OLED Edition release. Includes upstream's v0.5.4 base plus the audit pass and the entire OLED add-on feature set.

### Added — OLED display add-on

Requires a Waveshare [Pico-OLED-1.3](https://www.waveshare.com/wiki/Pico-OLED-1.3) (128×64 SH1107). Firmware drives it automatically when present and no-ops gracefully when absent.

- **Boot splash** (1.5 s) showing firmware version on power-on.
- **10 screens**, cycled with KEY0 (forward) / KEY1 (back, except where contextual):
  1. **Status** — connection state, paired BD address, battery % with pixel-icon battery, live stick / D-pad / face-button / L1-R1 / L2-R2 trigger visualization.
  2. **Slots** — persistent 4-slot multi-controller pairing (see below).
  3. **Lightbar Color Picker** — tilt-to-RGB live preview on the controller's lightbar, with 4 user-savable favorite slots (△ ○ ✕ □) and three effect presets (Breathing, Rainbow, Fade).
  4. **Trigger Test** — cycles 7 DS5 adaptive trigger effects (Off / Feedback / Weapon / Vibration / Bow / Galloping / Machine Gun) on both L2 and R2, bitpacked per [dualsensectl](https://github.com/nowrep/dualsensectl)'s reverse-engineering.
  5. **Gyro Tilt** — live X/Y/Z accelerometer values + 40×40 crosshair box that tracks tilt in real time.
  6. **Touchpad** — live render of finger positions on the touchpad surface, with a finger count.
  7. **Diagnostics** — uptime, BT state, HCI / audio-FIFO / opus-FIFO counter stubs (kept for future wiring).
  8. **RSSI** — live BT signal strength of the active link, dBm + bar.
  9. **VU Meters** — live peak meters for the speaker + haptic audio paths.
  10. **Settings** — persistent on-device editor for the 8 firmware config fields, plus "Reset to defaults" (hold △ 2 s) and "Wipe all slots" (hold △ 2 s).
- **OLED brightness control** — KEY1 long-press cycles brightness levels.
- **Auto-dim** after 5 minutes of input idle (lifespan / burn-in protection).
- **Soft-reboot recovery** without unplugging USB:
  - OLED KEY0 **double-click** (~400 ms window) → `watchdog_reboot`.
  - DS5 `PS + Mute` held for 2 seconds → `watchdog_reboot` (works without the OLED).
- **Pixel-art icons** in the Status screen header (link indicator + battery icon).

### Added — 4-slot persistent multi-controller pairing

- Bond up to 4 DualSenses; switch between them from the **Slots** OLED screen.
- Active slot persisted in the existing flash-backed config; dongle reconnects to the last-used controller on boot.
- D-pad ▲▼ to move cursor, △ to switch to the cursor slot (disconnect current ACL, restart inquiry filtered to the new slot's bd_addr), □ hold 1.5 s to wipe a single slot.
- "Wipe all slots" item in the Settings menu drops all 4 stored bd_addrs + all BTstack link keys in one shot.
- Inquiry filter on `HCI_EVENT_INQUIRY_RESULT` enforces slot ownership: devices stored in other slots are skipped; empty slots auto-assign on the first L2CAP HID_CONTROL channel open.
- Dongle stops being BT-discoverable once all 4 slots are full (security tightening; was permanently discoverable in upstream).
- Storage: BTstack's TLV link-key DB holds the keys (`NVM_NUM_LINK_KEYS=4`, unchanged from upstream); a new dedicated flash sector holds the 4 bd_addrs + occupancy bits + magic word.
- Multi-slot UX modeled on [zurce/DS5Dongle-OLED](https://github.com/zurce/DS5Dongle-OLED). Credit to zurce.

### Added — security / correctness audit pass

Critical and high-severity fixes on the core bridge code. Many of these have since landed upstream independently; this changelog captures what this fork shipped.

- **C1**: 4.8× stack-overflow in `core1_entry`'s `out_buf` (200 floats vs. 960 floats the resampler/encoder actually writes/reads). Root cause of the long-standing "audio may experience slight stuttering" known issue. Buffer resized and Opus return value now checked.
- **C2 + H5**: Variable-length stack array in `set_feature_data` sized by host-controlled length (potential stack blowup). Replaced with a bounded fixed buffer + length validation; CRC bounds tightened so `len < 4` no longer wraps to a huge `size_t`.
- **C3**: Unbounded `memcpy` into a 78-byte stack buffer in `tud_hid_set_report_cb` for HID report 0x02 (overflow if `bufsize > 76`). Bounded.
- **H1**: `tud_hid_get_report_cb` wrote `feature_data.size() - 1` bytes into the host-supplied buffer without clamping to `reqlen` (host-buffer overflow). Clamped.
- **H2**: OOB read in `on_bt_data` (`data[56]` / `memcpy(.., data+3, 63)` with no length check on the incoming BT frame). Bounded.
- **H3**: Same pattern in `l2cap_packet_handler` (`packet[3]` read with no size check). Bounded.
- **H4**: UAC1 volume parsed as `uint16` instead of signed `int16` — non-conformant hosts could crank haptic gain to 256× and saturate everything. Fixed sign extension.
- **H7**: BT report sequence counter in `main.cpp` cycled with period 16 (incremented as `int`, written as `(c<<4)`). Replaced with the correct `(c+1) & 0x0F` wrap used in `audio.cpp`.
- **N1**: Pairing-failure recovery: three `gap_inquiry_start(30)` calls were commented out in `bt.cpp`'s error paths (HCI command status, connection complete, authentication complete). Dongle would soft-brick after any pairing failure that didn't end in a disconnect. Uncommented.
- **N2**: Removed printf of the bonding link-key to UART on every reconnect.
- **N3**: CRC-helper bounds: `fill_output_report_checksum` / `fill_feature_report_checksum` underflowed `size_t` and wrote at negative offsets on `len < 4`. Guarded.
- **N4**: HCI command-send return values logged so a future stuck-dongle report has observability.
- **N5**: Watchdog enabled (8 s). Hangs now recover automatically instead of needing a power-cycle.

### Added — pairing posture hardening

- Tightened `gap_discoverable_control(1)` — the dongle now only advertises as pairable when at least one slot is empty. Once all 4 slots are full, it goes non-discoverable (still connectable to bonded controllers).
- (Carried over from upstream's own hardening; documented here for completeness.)

### Changed

- Project rebranded as **OLED Edition** to differentiate from upstream. UF2 artifact renamed: `ds5-bridge.uf2` → `ds5-bridge-oled.uf2`.
- README rewritten with full hardware section (Pico 2 W + Pico-OLED-1.3 SKUs, vendor links, prices), all 10 OLED screen mockups in the new cycle order, and explicit `TINYUSB 0.20.0` pin requirement (the 0.18.0 bundled with Pico SDK 2.2.0 lacks the 4-arg form of `TUD_AUDIO_EP_SIZE` used by this project's `tusb_config.h`).
- KEY1 short-press: was a 250 ms test-rumble burst on most screens; now cycles **backward** through screens (mirror of KEY0). Still cycles trigger preset on Trigger Test and lightbar mode on Lightbar (their primary in-screen interactions).
- Screen cycle order reorganized: **Status → Slots → Lightbar → Trigger Test → Gyro Tilt → Touchpad → Diagnostics → RSSI → VU Meters → Settings**. Settings last; the three "test" stimulus screens grouped together; diagnostic screens grouped together. Screen indices are symbolic constants (`kScreenStatus`, `kScreenSlots`, …) so future reorders are a one-block edit.

### Fixed

- `config_default()` was declared in `config.h` but never defined upstream (latent "undefined symbol" → linker "dangerous relocation" if anything called it). Implemented (fills body with `0xFF`, runs `config_valid()` — same path as a freshly-erased flash sector).
- GitHub Actions workflow `cp` paths corrected to match the `ds5-bridge-oled.uf2` produced after the CMake `OUTPUT_NAME` rename. (Workflows had failed on every push since the rebrand commit.)

### Build

- TinyUSB pinned to `0.20.0` (required; see Build Instructions in README).
- Pico SDK 2.2.0 + toolchain `14_2_Rel1` validated. Pico 2 W (RP2350) is the primary target.
- `CMakeLists.txt` adds `src/oled.cpp` + `src/slots.cpp` to the executable target, links `hardware_spi`.
- `OUTPUT_NAME` set to `ds5-bridge-oled` so the UF2 reflects the project name.

### Acknowledgements

- **[awalol/DS5Dongle](https://github.com/awalol/DS5Dongle)** — upstream base. This fork is a strict superset that tracks upstream and layers add-on features.
- **[zurce/DS5Dongle-OLED](https://github.com/zurce/DS5Dongle-OLED)** — pixel-art icon approach, the "hold for factory reset" UX pattern, and the multi-slot persistent pairing model.

---

[0.5.4]: https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/v0.5.4
