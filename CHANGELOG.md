# Changelog

All notable changes to **Pico2W DualSense 5 Bridge — OLED Edition** are documented here. This fork tracks [awalol/DS5Dongle](https://github.com/awalol/DS5Dongle) (upstream) and adds an optional OLED status display plus a security/correctness audit pass on the core bridge.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versioning approximates [SemVer](https://semver.org/) with the upstream version stream — the fork shares a major.minor with whatever upstream tag it is rebased on.

---

## [Unreleased]

### Changed
- **OLED Settings has separate Hap Left and Hap Right controls** in place of the linked Hap Gain item. Each grip is adjusted independently with the D-pad, then applied and saved with Triangle. Saving or resetting settings also keeps the current screen brightness in sync with the stored setting.

## [0.9.5-dev.13-oled-edition] - 2026-10-15

### Added
- **Controller buttons for the OLED from the app** (protocol 1.19, identity [46] bit 7). 0xF6 function `0x19 [button, hold ms lo, hi]` presses the D-pad, △ ○ ✕ □ or R1 for the OLED screens only: Settings (▲▼ select, ◀▶ adjust, △ save, △ held 2 s for Reset / Wipe), Slots (▲▼, △ switch, □ held 1.5 s forget), Lightbar (R1 mode, △ ○ ✕ □ save a favorite), Trigger Test (△ next preset) and Diagnostics (▲▼ scroll). A hold lasts as long as asked (up to 5 s), a tap 100 ms; the press wakes the screen and acts once it's on. Games never see these presses: the controller's report to the PC is untouched. Settings, Slots and Diagnostics take them without a controller connected; Lightbar and Trigger Test need one. Up to four presses queue, a fifth answers BUSY, a bad button or hold answers INVALID. Host-tested (`tests/host/test_oled_vpad.cpp`).
- **Lightbar favorites:** a quick △ ○ ✕ □ tap on the Lightbar screen could fall between two 10 Hz frames and not save; the buttons are now read every pass, like R1.
- **Press KEY0 / KEY1 from the app** (protocol 1.18, identity [46] bit 6). 0xF6 function `0x18 [key, press]` (key 0 KEY0, 1 KEY1; press 0 short, 1 long) plays a press through the same logic as the board's keys: it debounces, wakes the screen and navigates exactly like a finger (KEY0 next screen; KEY1 short back, long next brightness level), with or without an OLED wired. It runs from the main loop, never inside the USB request; up to four presses queue, a fifth answers BUSY, and bad arguments answer INVALID. The key logic moved into `src/oled_keys.h` unchanged, so it is host-tested now (`tests/host/test_oled_keys.cpp`).

## [0.9.5-dev.12-oled-edition] - 2026-10-14

### Changed

- **OLED: the Settings menu is one table, and Diagnostics redraws only the rows that changed** (PR #20, thanks to CLF184). Every setting keeps its label, range, step and storage; adding a setting is now one row instead of three places. Coming back to Diagnostics from another screen or from the dim display redraws it in full. Test-flashed on a real board: Settings, Diagnostics scrolling, screen switching and dim/wake.

## [0.9.5-dev.11-oled-edition] - 2026-10-13

## [0.9.5-dev.10-oled-edition] - 2026-10-13

### Added
- **Echo cancellation: the controller's own, on by default, with a switch** (protocol 1.17, identity [46] bit 5). The DualSense has echo cancellation for its microphone (SetStateData AudioControl [7] bit 2, EchoCancelEnable; Apple's DualSense driver calls it `echoCancelEnable`), and nothing turned it on: the connect-time state sent AudioControl 0x08 (noise cancellation only), and on Linux hid-playstation sends AudioControl 0 or 0x30 (the output path only) on every headset plug or unplug. So the PC's own sound coming back into the microphone (a voice chat, the app's Mic Test playing you back) was heard again, louder the higher the volume. One byte appended to the config body (`mic_echo_cancel`, 51 bytes now): 1, the default, sets EchoCancelEnable in every report that sets AudioControl (a game's, the PC driver's, the connect-time state; the rest of the byte stays as the game set it) and sends it at once on a change; 0 leaves the bit as the game sets it (off unless a game turns it on). Older apps (50-byte body) never see or change it, and a config saved by older firmware starts it on. On the OLED: Settings → `Echo Cancel` (after `Mic Lvl`). `src/state_override.h`, host-tested.
- **Mic level: the controller's own microphone level, set from the app** (protocol 1.16, identity [46] bit 4). One byte appended to the config body (`mic_level`, 50 bytes now): 0, the default, follows the PC's mic level as before (the USB mic volume, 0..48 dB, as the controller's VolumeMic 0..0x40); 1..64 sets the controller's VolumeMic to that level whatever the PC's slider says, the same on Windows, macOS and Linux. The controller has exactly one mic level (VolumeMic, an absolute, non-linear byte that tops out at 0x40; no mic pre-gain like the speaker's), so the setting replaces the PC's level instead of stacking on it: a product of two levels could only make the mic quieter, which the PC's slider already does. The PC's mic mute still works. While it is set, a game report that sets the mic volume carries this level instead (Lock volume still drops the game's change altogether), the connect-time state carries it, and a change is sent at once; back at 0 the controller gets the PC's level again. Older apps (49-byte body) never see or change it, and a config saved by older firmware starts it at 0. Identity [48] reports the PC's mic level (as VolumeMic), so the app can warn when following the PC means a silent mic: on Linux the DualSense's hardware capture volume is whatever ALSA restored at boot (PipeWire only uses software volume), often 0. On the OLED: Settings → `Mic Lvl` (after `Mic In`; `PC` or 4..64 in steps of 4). `src/state_override.h`, host-tested.

### Fixed
- **The OLED Slots screen says when every pairing slot is taken**: with all 4 slots full the dongle stops being discoverable and only accepts the current slot's controller, so a new controller doing Create + PS just blinked until it gave up, with nothing saying why. With all slots full and no controller connected, the footer now reads "All full: forget one" instead of "Press PS to connect" (forget one with Square held on a connected controller, or from the app). `src/slots_footer.h`, host-tested.
- **Forgetting one pairing slot unpaired the controller in another**: one controller could end up in two slots. A controller connecting by itself (pressing PS pages the dongle, and an incoming connection isn't filtered by slot) while the current slot was empty was paired into that slot too, though another slot already held it. BTstack keeps one link key per Bluetooth address, so forgetting either copy dropped the key both shared: the other slot's controller could no longer authenticate and dropped out, and switching back to its slot couldn't bring it back without pairing it again (Create + PS). Now a controller that is already in a slot takes the dongle to its own slot when it connects (the app and the OLED show that slot as current), so it is never paired twice; forgetting a slot keeps the link key while another slot still holds the same controller; and forgetting the current slot while that controller is connected moves to its other slot instead of disconnecting it. Forgetting a slot never disconnects a controller on a different slot. The OLED Slots screen says `Press PS to connect` instead of `Tri=sw Sq hold=wipe` while no controller is connected (Triangle and Square are the controller's buttons). `src/slots.cpp` `slot_for_controller()`, host-tested.
- **A slot switch, forget or wipe that failed to save said it worked**: when the flash save failed (it can, when core1 doesn't let go of flash in time), switching, forgetting or wiping a pairing slot still answered OK to the app, and the OLED still said "Switched!" / "Wiped!" / "Slots wiped!". The change was then lost at power-off. Now the app gets `CMD_RESULT_FAILED` and the OLED says "Save FAIL" / "Wipe FAIL". Saving the Edge's extra buttons when the flash save fails now answers `CMD_RESULT_FAILED`; it used to answer `CMD_RESULT_INVALID`, so the app said "rejected" though the buttons were applied. Found by the error pass (`tools/list_errors.py`; every failure path in `src/` now has a verdict, gated in CI). Host-tested.
- **The narrator's "Connected, let's play" lost its start on every connect** (and her lines dropped pieces): the line started a fixed 2.5 s after the link, straight into a speaker path that had carried no audio, with her first word 100 ms into the clip, and the dongle sent her reports every 20 ms while the controller plays one per 21.33 ms (6.7 % too fast for its buffer). Now the connected line waits until the path is ready (the controller's input steady for 0.3 s, the connect-time state sent, and the PC's speaker volume set and quiet for 0.3 s; never before 0.8 s, and at 2.5 s at the latest, the old fixed start), a cold path gets 0.3 s of silent Opus frames before a line and 85 ms after it, and her reports go out at the controller's own rate (`src/voice_cue.h`, `src/report_clock.h`, shared with the haptic feedback test). The line still plays straight from the flash bank, pre-encoded; nothing is decoded on the dongle.
- **The speaker dropped to about half volume after a headset plug or unplug** (Linux): the kernel's DualSense driver sends the controller's audio routing (AudioControl 0 on plug, 0x30 on unplug) and the dongle forwarded it as it was, which cleared NoiseCancelEnable (bit 3), the bit without which the speaker plays at about half level (upstream awalol b821623, #263). The dongle now keeps that bit set in every report that sets AudioControl, a game's included (no known game turns it off; SDL never sends the section), and in its own mic-choice push. Echo cancellation's bit is unchanged.
- **The headset never got full volume**: the PC's volume reached the controller's headphone amp as the speaker amp's number (0..100), but the headphone amp goes to 127, so 0 dB on the PC gave 100/127 (79 %; 63 % at Linux's restored −20 dB). The headphone amp now gets the same position scaled to 0..127; the speaker amp is unchanged (0..0x64). `src/amp_volume.h`, host-tested.
- **Mono speaker audio played in the right ear only on a headset** (Linux): the DualSense's "Speaker" output (the PipeWire/PulseAudio profile "HiFi (Mic, Speaker)", alsa-ucm-conf) writes its mono signal to USB channel 2 only and leaves channel 1 silent, and the dongle sends channels 1/2 as the speaker stream's left/right. With Audio output on Auto and a headset plugged in (or on Headset), the app's speaker test, the narrator's host-side lines and anything else on that output came out of the right ear only. Now, while the speaker stream goes to the headset and channel 2 has carried sound for 100 ms with channel 1 exactly silent, channel 2 is played in both ears; the first non-zero sample on channel 1 ends it at once, so stereo from the "Headphones" output or a game stays stereo. The speaker itself is unchanged. `src/speaker_mono.h`, host-tested.

## [0.9.5-dev.9-oled-edition] - 2026-10-13

### Added
- **DualSense Edge profiles in PlayStation Accessories over the dongle** (upstream awalol 8a2f576, issues #29, #155): the app showed the Edge's custom profiles empty ("not assigned"), and a saved profile only appeared after reopening it. The controller fills its profile reports 0x70–0x7B only after a SET 0x80 [0x70, 0x01], which a native Bluetooth host sends at connect and the dongle didn't. Now, when an Edge connects, the dongle sends what a native host does (SET 0x65 = the firmware report 0x20 echoed, then the 0x80 unlock), waits the 4 s the controller needs, and reads 0x70–0x7B one every 80 ms (all at once overflows the controller's control channel and drops some). Until that is done the app's profile reads are refused, so it retries instead of keeping an empty list; USB isn't held back. After a profile is written (0x60–0x62) the dongle repeats the unlock, polls 0x81 six times as the app does, and reads the profiles again 5 s later, so the save shows right away. The configurator's colorway read and controller tests use the same controller command slot (0x80/0x81) and now wait for this sequence (and it for them): on an Edge the model and colorway arrive about 5 s after connect instead of 1.5 s. Upstream's 0x65 offsets were adapted to our feature cache, which keeps the report id. A DualSense is untouched. `src/dse.cpp`, host-tested (order, timing, refusal, re-read after a save, and the shared slot).
- **The controller turns off when the PC sleeps** (upstream awalol 9923ce3, caaff10, 9ddb181; on by default, as upstream): once the USB bus has stayed suspended for 3 s the dongle drops the controller's Bluetooth link and the DualSense turns itself off, instead of staying on until its inactivity timeout. The 3 s ride out USB hubs that idle the bus while the PC is awake (upstream #148, #174), and a USB reconnect the dongle does itself (0xF6 0x03, a mode switch) never counts as a sleep. While the PC sleeps the dongle never leaves or rejoins the bus, so a controller coming and going can't hot-plug-wake it; after the PC wakes with no controller, the leftover DualSense disappears after 2 s (upstream edec7f7) unless a controller comes back first.
- **The PS button can wake the PC** (upstream awalol edec7f7 / c67c7f6; off by default, as upstream): with "PS wakes the PC" on and no controller connected, the dongle shows up as a wake-only keyboard, which the PC arms for USB remote wakeup while it sleeps. Pressing PS (the controller reconnecting) or any button wakes the PC, and the keyboard then presses F15 twice so Windows turns the screen on and stays awake; then the dongle becomes the DualSense again. With a controller connected the dongle is the DualSense exactly as before (same 289-byte HID descriptor, same interfaces); the only difference with the option on is the remote-wakeup bit in its configuration descriptor, so a PC that allows it can also be woken by a connected controller. Unlike upstream, the wake keyboard has its own USB ID (2E8A:000A, "DS5 Dongle Wake Keyboard") instead of Sony's, so Linux's hid-playstation, Steam and games never take it for a DualSense and Windows keeps it apart from the DualSense (the configurator's WinUSB binding is not affected); and there is no BOS / Microsoft OS 2.0 descriptor and no forced remote wakeup: the dongle signals wakeup only when the PC enabled it, never in the first 50 ms of a suspend, at most 3 times per sleep, and every wait has a time limit. Not in the serial (debug) build, which stays on the bus for its console.
- **Settings for both** (protocol 1.14): identity [46] bit 2 and [47] (bit 0 controller off when the PC sleeps, bit 1 PS wakes the PC), set and saved with 0xF6 0x17 [bits] on their own (other unsaved edits stay unsaved). They are kept after the config body (`Config::power_flags`), not in it, so the 0xF7 config read and older apps are unchanged. On the OLED: Settings → "Sleep CtrlOff" and "PS Wakes PC" (after `Mic In`, before Reset).
- **Upstream's settings batch ("batch 3/5", awalol), protocol 1.13.** Seven settings appended to the config body (49 bytes now; a config saved by older firmware keeps every setting and starts these at their defaults, which change nothing):
  - **Trigger effect intensity** (upstream a109481, 8f42593, 0fba807, a7824d9): adaptive trigger resistance weakened in 10 steps (`trigger_reduce`, the controller's TriggerMotorPowerReduction). 0, the default, is the game's own strength: the game's report goes out exactly as it sent it. That is upstream's fix for its #228 regression (v0.7.2 re-sent a latched controller state with every section allowed on each game report, so triggers came out weak or gone); our forward already sent the game's own report (#11), and the setting only changes that one nibble. Unlike upstream, the rumble reduction next to it stays the game's.
  - **Speaker boost** (upstream cc9be13): the controller amp's pre-gain 1..7 (`speaker_gain`); 0, the default, leaves it to the game. Our speaker volume (digital gain in dB) is unchanged and separate. Upstream defaults to 2; ours stays at the game's so nobody's sound changes.
  - **Lock volume** (upstream ca1af99): games can't change the controller's speaker, headset or mic volume, mute or mute light; the PC's own volume and mute still apply.
  - **Microphone and speaker choice** (upstream ea93fad): the mic as Auto / Built-in / Headset (`mic_select`; off stays the existing BT Mic switch, so there's one off switch), the PC's audio on Auto (headset when one is plugged in) / Speaker / Headset / Off (`speaker_select`; Off also skips converting and encoding it, as with no encoder; the narrator still speaks). At connect the dongle used to force the built-in mic; Auto, the new default, lets the controller use a headset's mic when one is plugged in, as on a PS5. The noise-cancel bit the speaker needs is kept (we never had upstream's b821623 bug).
  - **Status pin** (upstream 86da98c, #225): a GPIO that is high while a controller is connected, or gives a 200 ms pulse when one connects (e.g. an LED or a PC's power header). Only pins nothing else uses: GP2-7, 13, 14, 16, 18-22, 26-28 (not the UART, the OLED HAT's pins or the radio's). The main loop follows the connection and the setting, so a pin that's changed or turned off is released.
  - The OLED Settings screen has `TrigEff`, `SpkBoost`, `VolLock`, `SPK Out` and `Mic In` (Reset and Wipe moved down). The status pin is set from the app only.
- **The mute button's light follows the controller's mic mute** (upstream a7824d9), as on a wired DualSense.
- **The PC's mute switches and mic volume reach the controller** (upstream cfca2a5): mic mute, speaker/headset mute, and the mic level (0..48 dB as the controller's 0..0x40, the level it had before; upstream sends the dB number, max 48). Speaker mute still also zeroes the speaker audio, as before.
- **The app's haptic feedback test vibrates steadily for its whole 3 s** (protocol 1.15, identity [46] bit 3): 0xF6 0x0E takes an optional duration, `[side, tenths]` (0.1–5 s; 0 stops; without it 0.5 s as before, so older apps work unchanged). The test is now an HD haptics vibration the dongle generates itself, a steady 125 Hz wave on the chosen grip(s) at each grip's haptics strength (strength 1 a quarter, 4 and up full), sent in the haptic channels of the 0x39 audio report the way games' HD haptics are: in place of the host's haptics while the PC streams audio, otherwise in the dongle's own reports at the haptic packet rate (the narrator's line rides along if she is speaking). It runs for exactly that long, then stops; a new test replaces the running one with no gap. Before, the app repeated 0.5 s pulses, which felt like three little bumps, and the classic-rumble version that followed still pulsed.
- **Debug builds (`ENABLE_SERIAL`) wait up to 5 s for a serial terminal at boot** (upstream 356b69c waits for ever), so the boot log isn't lost.

### Changed
- **A fresh or reset config uses 500 Hz polling** (upstream 501d435) instead of 250 Hz. Only an erased or invalid setting takes the default: a saved rate is never changed.
- **Protocol 1.13** (identity [46] bit 1): a config GET (0xF7) of the full 63 bytes still answers the 42-byte 1.6 body with the remap block at 42, so older apps keep finding their remap; a shorter GET answers the whole 49-byte body (apps from 1.13 ask for `sizeof(Config_body)`). A config write may carry all 49 bytes; older apps' 42-byte writes leave the new fields alone.
- **Haptics are cut from 48 kHz to the controller's 3 kHz by taking every 16th sample** (upstream awalol 4929996) instead of running them through the WDL resampler. The resampler was set to plain linear interpolation at an exact 16:1 ratio, so it was already doing exactly that: the bytes sent to the controller are the same (a host test checks the new code against WDL itself over ten minutes of audio, and over short USB reads), with less work on core0 for every 1 ms USB audio packet. Audio Auto Haptics, the per-grip gains and the narrator are unchanged. Upstream put the resampler back an hour later (91b3a28) without giving a reason; the test shows the output is identical either way.
- **Built with Pico SDK 2.3.0, a newer TinyUSB and ARM's GNU toolchain 15.2.Rel1** (upstream awalol 994f310, 898c352 and 76690ab), instead of SDK 2.2.0, TinyUSB 0.20.0 and 14.2.Rel1. Upstream made this change to fix DualSense V2 controllers (CFI-ZCT2W / BDM-060) that failed to reconnect or put the dongle into watchdog reboot loops (upstream #235). TinyUSB is pinned to the same commit as upstream: 0.21.0 plus later fixes (hathach/tinyusb#3758). The USB descriptors are byte-for-byte unchanged (the 289-byte DualSense HID descriptor, UAC1 audio and the configurator interface), and the Bluetooth/USB hot path is still in RAM: TinyUSB 0.21 renamed its FIFO copy functions, so the RAM placement now uses the new names and `tools/check_ram_residency.sh` checks them. Building from source: the pins are in `tools/toolchain.env`, `tools/install_toolchain.sh` sets up the pinned toolchain, SDK and TinyUSB in one go, and the build refuses a TinyUSB other than 0.21.
- **`bt_write` no longer clears its 672-byte send buffer before every report** (upstream awalol 4929996): only the report's own bytes are ever sent, so this was wasted work on core0 for every 0x39 audio report and every forwarded output report.
- **Without a speaker encoder, core0 no longer prepares speaker audio** (adapted from upstream awalol 4929996, which skips that work when the speaker is off; we have no speaker-off setting): until core1 has its encoder, or for the whole session if it couldn't get the memory for one (issue #15), the speaker PCM was converted and queued only to be thrown away. Haptics and the narrator are sent exactly as before. In that state the Diag `Drop r` count and `raw_push` now stay at 0 instead of climbing; the encoder-failed flag (0xF5 `c1_flags` bit 1) still reports it.

### Fixed
- **DualSense Edge detection no longer depends on a GET 0x70 probe** (upstream awalol 2efc855): the dongle tells an Edge from a DualSense by the firmware report 0x20 it already reads at connect (software series byte [22] = 0x44 on the Edge). Before, it waited for a DualSense to refuse report 0x70 with a handshake, so a DualSense firmware answering differently would never have connected to USB. 0x20 is now read last at connect, so 0x09/0x22/0x05 are still cached before USB connects; an Edge then gets its profiles read (below).
- **An Edge in DS5 mode no longer gets a failing "firmware update available" in PS Accessories** (upstream ae83907, issues #194, #197, #198): the host now reads a DualSense firmware report 0x20 (the latest known DualSense firmware, pinned in `src/fw_report.h`) instead of the Edge's, which under the DualSense ID made PS Accessories offer a DualSense update that then failed (FW-000007-5). Only an Edge presented as a DualSense gets it: a DualSense in any mode and an Edge in Auto or DSE mode still get their real report (native game triggers), and the app's Controller info still shows the Edge's real firmware.
- **The 0xFE mic-debug dump fits the 63-byte reply cap**: it wanted 82 bytes (length + 80 frame bytes), so its last 19 bytes were silently cut off. It now answers 63: the longest 0x31 frame's length, then its first 61 bytes (`MIC_DEBUG_FRAME_BYTES`, also the capture size). No script or app reads it; the host test now checks that no GET answers more than 63 bytes, with no exceptions.
- **Quick taps on the OLED's controller-driven screens are no longer missed** (CLF184, #19): Slots, Lightbar, Trigger Test, Diagnostics and Settings read the controller's buttons only when they redrew (10 times a second), so a tap shorter than a frame could be lost. They now read them on every main-loop pass. The Slots screen's header, addresses and help line fit the 20-character display, and "Switched!" / "Wiped!" show in the footer instead of over the header.
- **Audio Auto Haptics' Fallback mode engages again after a game's own haptics stop** (CLF184, #18): it decided "the game sends HD haptics" from the Audio Meters' held peak, which only fell while the OLED meter or the app read it; with neither, one loud haptic moment kept Fallback off. It now looks only at the haptics in the current USB audio packet (threshold and 100-packet delay unchanged).
- **The audio meters read the same however often they're read** (CLF184, #18): each read used to lower the shared peak, so the app's diagnostics polling made the OLED meter fall faster, and with nobody reading a peak stayed up. The level now falls with time since the peak, at the rate the OLED meter fell before (about -20 dB in 1.7 s), and a full-scale sample shows full instead of 0.
- **Signal strength reads no longer flood the controller link** (CLF184, #18): every read of the RSSI (OLED BT Signal screen, app 0xF9) sent a new Bluetooth RSSI request. Reads still return the last value, and now ask for a new one at most 10 times a second with one request outstanding. Nothing polls it during gameplay.
- **The app's performance report never shows a half-written reading** (CLF184, #18): if a timing stage was mid-update through every retry, the report used the torn values; it now repeats that stage's last complete reading.
- **OLED Settings' "Saved!" / "Reset!" messages show in the footer for 2 s** (CLF184, #18) instead of over the title until the next message; the (*) unsaved marker clears only when the save succeeded.

## [0.9.5-dev.8-oled-edition] - 2026-10-13

### Changed
- **Core0's Bluetooth/USB hot path now runs from RAM on the Pico 2 W** (ported from upstream awalol B.1–B.4, a2772a9, 57f185b and the rest of A.1): our Bluetooth handlers, `bt_write`, `on_bt_data`, `interrupt_loop`, `audio_loop`, `audio_bt_task`, `mic_add_queue` and the CRC32 table, plus BTstack's HCI/L2CAP dispatch and TX pumps, the CYW43 SPI transport and SDPCM pump, and TinyUSB's FIFO, task, HID report and audio class data paths, so none of them wait on the flash cache that the OLED and BTstack keep evicting. Costs ~39 KB of SRAM (core1 still has room for the speaker encoder and mic decoder). `tools/check_ram_residency.sh` checks every one in CI and releases. The original Pico W keeps them in flash (its RP2040 has no RAM to spare).
- **The dongle now runs the Pico 2 W at its rated 150 MHz and default voltage, like upstream** (awalol v0.7.0: a023371, 99da904, e5d99f0): cooler and within the chip's specs, instead of the 320 MHz / 1.20 V overclock inherited from upstream (1.20 V is above the RP2350's 1.16 V maximum). The core voltage is set back to the default 1.10 V at boot, including on dongles upgraded from the 320 MHz firmware (the RP2350 keeps the old voltage until it is unplugged). The flash is now set up explicitly at boot, which fixes core1 crashes on some boards (issue #15). The original Pico W is unchanged (200 MHz). Building from source: `-DDS5_CLOCK_KHZ=320000` still builds the old overclock as a fallback for this release; the build refuses a Pico 2 W setup without the boot-time flash setup, and `tools/check_xip_setup.sh` checks every CI and release build for it.
- **CI builds with ARM's official GNU toolchain 14.2.Rel1** (as upstream 425061f) instead of Ubuntu's `gcc-arm-none-eabi` package, so every build uses the same compiler.

## [0.9.5-dev.7-oled-edition] - 2026-10-13

### Changed

- **Release and CI gates (quality pass F41-F43):** `tools/check_gitlinks.sh` fails the build when `lib/opus` or `lib/WDL` stops being a submodule gitlink (a committed symlink broke every fresh checkout once, 5d869f1); `tools/release.sh` and the release workflow now run the host unit tests before building the UF2; and `tools/check_ram_residency.sh` checks every CI and release ELF for core1's RAM rule (no `__memcpy_chk` / `__memset_chk` / `__memmove_chk`, and core1's entry, the libopus encode / decode entry points, the resampler's hot methods, the queue ops and our `memcpy` / `memset` / `memmove` in SRAM), which before was only visible on hardware as the OLED's `Drop r` count.

### Fixed

- **Controller health test (protocol 1.12) hardening:** a page the controller answers twice (a re-asked GET) is kept once, where it used to land unblanked at the next page's offset; for 1 s after a test ends, a late reply of ours (it may hold the serial) is dropped from the feature cache instead of being served to a host GET 0x81; while a test runs and during that second, a host SET 0x80 is not forwarded and a host GET 0x81 is refused (stalled), so neither takes the test's pages, and a 0x81 reply that isn't the test's is no longer thrown away; and a 0xF6 0x16 sent in the loop pass the controller connects in answers FAILED instead of being accepted and then reported as "no controller".
- **Per-stage timings (0xFC) could read half the real average:** reading a stage while core1 halved its sum and count could pair the new sum with the old count. Each stage now has a sequence number the reader checks (a seqlock); a host test with a writer thread checks it.
- **The Status title's scroll froze for up to ~36 minutes** (local builds, whose version label is longer than the title's space) when the Status screen had been away for more than ~35 minutes: its timer was 32-bit microseconds. It is 64-bit now (`src/title_scroll.h`, host-tested).

## [0.9.5-dev.6-oled-edition] - 2026-10-12

### Added

- **Core1 start-up stage in the diagnostics (issue #15):** when the dongle's second core never starts its loop, the 0xF5 audio counters now say which start-up step it stopped in (registering for flash lockout, starting its cycle counter, reading the free memory, creating or setting up the speaker encoder and resampler, creating the mic decoder), in bits 3..6 of the core1 flags byte [60] (9 = running). Older apps read the flags and counters as before; `scripts/audio_diag.py` prints the stage.

- **The DualSense Edge on the OLED, for the first time:** with an Edge connected, the Status screen shows its Fn buttons and back buttons (LB / RB) and lights them as you press them.

  ![The OLED Status screen: DualSense, DualSense Edge, and the Edge with its buttons pressed](https://raw.githubusercontent.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web/master/docs/screenshots/oled-status-dualsense-edge.png)

- **Controller health & info (protocol 1.12):** the configurator can show the connected controller's own firmware (build date, hardware, main / SBL / DSP versions, Bluetooth patch), its lifetime counters (play and charge time, haptics and trigger time, charge cycles, connections, button presses, stick travel) and its battery voltage. GET 0xEB (vendor interface only; wValue's high byte picks the header, the firmware info or a result page) and 0xF6 0x16 [dev, action, pages], which queues one of two read-only Sony test commands (counters 0x70/1, battery voltage 0x04/3; anything else is refused) and runs it from the main loop after the connect-time colorway read, one 0x81 request at a time, never inside a USB transfer. The controller's serial number and the Edge stick modules' serials in the counters block are blanked before the app can read them, and the device-info bytes of report 0x20 are withheld. Identity [46] bit 0 announces it. Counter layout from daidr/dualsense-tester (MIT).

### Changed

- **Redrawn controller on the OLED Status screen:** the drawing is now mirror-symmetric (sticks, L2/R2 bars, L1/R1 and the d-pad / face buttons sit at mirrored positions; the face buttons were 7 px off-center against the d-pad and the right stick box 6 px further out than the left), the face buttons are rounded so they read apart from the d-pad, a stick at full tilt no longer merges into its box's frame, and **Create and Options** now show between L1 and R1 and light when pressed. On a **DualSense Edge** a strip under the d-pad and face buttons adds its four extra buttons, mirrored like the rest: Fn L / Fn R under Create / Options, the back buttons LB / RB further out under the d-pad and face buttons (filled while pressed; shown once the connect-time model read says Edge, a standard DualSense looks exactly as before). The **battery** now spans exactly the gap between the two stick boxes (outline and nub x 36..91, mirrored; was 36..90), and the charge time moved one pixel left so it mirrors the level text. A host test (`tests/host/test_oled_pad.cpp`) keeps it symmetric and the standard DualSense drawing pixel-identical.

### Fixed

- **Core1's health never reached the app:** the 0xF5 audio counters wanted 68 bytes, but both the configurator interface and the HID path answer at most 63, so the speaker-encoder failure flag and the free heap added in 0.9.4 (issue #15) were cut off. 0xF5 is now 63 bytes: the 15 counters stay where they were, [60] holds core1's flags (bit 7 set marks this layout) and [61..62] the free heap at core1 start in KiB. Older apps read the counters as before; `scripts/audio_diag.py` reads the new layout. A host test checks that no reply needs more than 63 bytes (it found 0xFE, the mic-debug frame dump, wants 82; left as is for now).

## [0.9.5-dev.5-oled-edition] - 2026-10-12

### Added

- **Per-stage timings (protocol 1.11):** the dongle times its own work: each pass of the main loop and its parts (Bluetooth, USB, audio, input reports, OLED) and, on core1, every speaker frame encoded and every mic frame decoded, keeping the average and the worst case. Read with 0xFC (63 bytes from a host that asks for them; 11 as before otherwise), reset with 0xF6 0x15; identity [29] bit 7 announces it. For the app's dashboard and for comparing builds on real hardware.
- **Host unit tests** (`tests/host/run.sh`, also in CI): calibration, stick curves (checked against the app's sample values), button remapping, the config codec, the configurator command parser (every report at every length stays inside its buffer) and the OLED transfer, built on a PC with AddressSanitizer and UndefinedBehaviorSanitizer. `tools/analyze.sh` reports firmware size, RAM use, stack depth and clang-tidy / cppcheck findings.

### Changed

- **Lighter OLED refresh:** a frame goes to the display in 129 SPI transfers instead of one per byte (~1,150), with exactly the same bytes, and an unchanged frame isn't sent at all (still refreshed every 2 s), so the main loop spends far less time on the screen.
- **The whole version on the OLED Status screen:** the title is now "DS5 B. " and the version without "-oled-edition" ("DS5 B. v0.9.5-dev.4" fits exactly; it used to be cut off after "DS5 Bridge v0.9.5-d"). A longer local build version scrolls slowly back and forth, never over the link icon. The boot splash shows the real version and board too (it said "v0.6.0" and "Pico2W").
- **Leaner speaker encoder:** libopus is built without its desktop hardening (its fortified `memcpy`/`memset` ran from flash on core1 every frame, the stall the RAM relocation exists to avoid; ~17 KB less RAM and ~13 KB less flash), and the speaker encoder runs CELT-only (`OPUS_APPLICATION_RESTRICTED_CELT`): the same packets the controller got before at these settings, from half the encoder memory (~24 KB more free heap on core1) and 4 ms sooner. `-DOPUS_ENCODER_CELT_ONLY=OFF` brings back the previous encoder mode.

## [0.9.5-dev.4-oled-edition] - 2026-10-12

### Added

- **Stick response curves (protocol 1.10):** a response curve per stick, per pairing slot, applied in the dongle to the report games receive (after calibration, so it works in every game): Linear (default), Precise, Steady, Aggressive, Digital, or Custom with four points. The tilt goes through the curve, the direction stays. Stored with the calibration (older saves read as Linear, and older firmware still reads the calibration). Read with 0xEC, set with 0xF6 0x14 (saved, or temporary for a game's profile: RAM only, ended by 0x12, a reboot or a saved set); identity [29] bit 6 announces it, [19] bit 2 says temporary curves are on.
- **Remap the DualSense Edge's Fn buttons and back buttons (LB / RB) (protocol 1.9):** four more remap entries (Fn L/R, LB/RB): each can act as any standard button, stay itself (default) or do nothing; a remapped one presses its target instead of itself. Read with 0xED, set with 0xF6 0x13 (saved or temporary, like game layouts). Older saved remaps keep working (the new entries read as "itself").

## [0.9.5-dev.3-oled-edition] - 2026-10-12

## [0.9.5-dev.2-oled-edition] - 2026-10-12

## [0.9.5-dev.1-oled-edition] - 2026-10-12

## [0.9.4-oled-edition] - 2026-10-12

### Fixed

- **No haptics and failing saves when the speaker encoder couldn't start (issue #15):** if core1 couldn't get memory for the Opus encoder at startup it returned and stopped for good, so audio frames were never encoded, haptics waited forever for speaker frames, and every flash write (which parks core1) timed out (`Dongle command 0x02 failed`). Core1 now keeps running without the encoder, haptics go out alone (the Pico W layout; the narrator still speaks), saving works, and 0xF5 reports core1's health ([15] flags: started, speaker encoder failed, mic decoder failed; [16] free heap at core1 start). The diag scripts also find a DualSense Edge dongle (054C:0DF2).

### Added

- **Temporary remaps (protocol 1.8):** 0xF6 func `0x11` applies a remap table in RAM only (a game's button layout from the app, so starting and stopping games never rewrites flash), `0x12` goes back to the saved table (as does a reboot); identity [19] bit 1 says a temporary table is active, [29] bit 4 announces the feature. A normal `0x10` set still saves and ends the temporary state.
- **Dev builds are stamped from Git:** `tools/dev_firmware.sh [--flash]` and any build without `-DVERSION` report e.g. `v0.9.3-oled-edition+3.a1b2c3d[.dirty]` (OLED, app, picotool) instead of a stale "dev" version kept in an old build folder. `tools/bootsel.py` puts the dongle into BOOTSEL over HID.

## [0.9.3-oled-edition] - 2026-10-12

### Added

- **Board name in the identity report (protocol 1.7):** identity [29] bit 3, then [30..45] the board's name as ASCII ("Pico 2 W", "Pico W"), set per build from `PICO_BOARD` (`DS5_BOARD_NAME` in CMake), so the app names the hardware without an app update for each new board.

## [0.9.2-oled-edition] - 2026-10-12

The firmware is unchanged from 0.9.0. This release fixes the app on Windows PCs where a real DualSense was plugged in by USB before: the dongle never connected, and the app now offers **Repair USB driver** (details in the app's CHANGELOG).

## [0.9.1-oled-edition] - 2026-10-11

The firmware is unchanged from 0.9.0. This release brings the app's opt-in **Development update channel**.

### Added

- **Development builds** (`vX.Y.Z-dev.N-oled-edition`): published as GitHub pre-releases and listed only in the new `latest-dev.json` feed, which the app's Development channel reads. The stable feed (`latest.json`) never lists them.

## [0.9.0-oled-edition] — 2026-10-11

### Added

- **Left and right grip haptics strength (protocol 1.6).** `haptics_gain` is now the left grip and a new `haptics_gain_right` the right one (older saved settings start with both equal, so nothing changes until you set them apart). A host settings write never changes the narrator setting. The OLED Hap Gain setting moves both; it shows L / R when they differ. Classic rumble (the vibration most games send) now follows the grip strengths too, not just HD haptics. A grip test (0xF6 0x0E) buzzes one grip at its strength for half a second.
- **Calibration in the dongle (protocol 1.5), per pairing slot.** Stick center offset, deadzone and full-tilt radius (each half-axis scaled to its own reach, so full tilt reaches both ends even when the center leans), trigger travel and gyro zero, one set per pairing slot in their own flash sector, applied to the report games receive (the live input the app reads stays raw). Untouched sticks and triggers pass straight through. The gyro zero removes only what's left after the controller's factory bias (which games already subtract). Nothing is written to the controller. Set with 0xF6 function 0x0D ['C','L',2, slot, 18-byte body], read with 0xEE ([slot] + body); identity [29] bit 1 announces it.
- **Screen mirror for the app (protocol 1.4).** A read-only vendor request (`0xEF`) returns the OLED image exactly as last sent to the panel, with the current screen, contrast and power state, so the app shows the dongle's screen pixel for pixel. Works without the OLED add-on fitted (the firmware draws the same frames either way). Announced by a new feature byte in the identity report ([29] bit 0).

### Fixed

- The OLED brightness saved in Settings now applies from the first frame (the panel started at a fixed contrast the screen mirror didn't report).

## [0.8.1-oled-edition] — 2026-10-11

### Fixed

- **Rumble no longer cancelled by the dongle's own reports.** The lightbar colour (sent ~10× a second while charging or on a non-HOST lightbar mode), volume changes, the trigger preview and the connect state now carry the game's current rumble instead of "no rumble", which could stop rumble in some games (reported in Crimson Desert) while haptics and triggers kept working. The game's rumble is forgotten on disconnect so a reconnect never resumes it.
- **Output could stay dead after a reconnect.** Packets still queued when the controller dropped are now discarded (upstream a1492fd/697ade0); before, rumble, triggers, lightbar and speaker could all stay silent until the dongle restarted.
- The controller's last input no longer lingers after it disconnects (a neutral report, upstream 8c8b527).
- Idle disconnect works with the mic on (only input reports count, upstream d7fb163).
- SetStateData: the motor power nibbles were swapped, and `UseRumbleNotHaptics2` (byte 38 bit 3, Ninja Gaiden 4) is declared (upstream 8d8255c).
- **Faster, steadier connecting.** A first-time pair opens its second Bluetooth channel as soon as the first is up instead of waiting for a later event, which could stall pairing until the watchdog retried (upstream 83b4df9); a faster interlaced page scan while no controller is connected makes a known controller reconnect quicker after PS (upstream ade9ea1; back to the normal scan once connected, so it doesn't take radio time from the audio).
- Pico W SMPS in PWM mode: no coil whine (upstream 23a55da, #207); a short delay before the boot-time USB disconnect (upstream 4f9f485).

## [0.8.0-oled-edition] — 2026-10-10

### Changed

- **Configurator: only `0xF6` runs commands.** A SET to any other configurator id (e.g. `0xF2`, `0xF4`, which are also real DualSense feature reports) no longer runs `0xF6` functions such as wipe, BOOTSEL or factory reset. While a command is pending, every other one answers `BUSY`. The `0xF2`–`0xF4` debug reports are on the HID path only in serial (debug) builds; release builds pass `0xF2`/`0xF4` through to the controller like a real DualSense.
- **The controller's serial number is not kept.** The cached reply that carries it is dropped as soon as the colorway is read, and the read only happens in builds with the configurator interface.
- A config write or factory reset from the app no longer changes the active pairing slot (factory reset used to switch to slot 0 at the next boot).

- **Releases now ship the configurator app with the firmware.** Each `vX.Y.Z-oled-edition` release has the app for Linux, Windows and macOS (with this firmware built in) plus the UF2. Release builds turn on the configurator USB interface. Built by `.github/workflows/release.yml` from one tag pushed by `tools/release.sh`; other releases (Proton builds) are no longer touched by it. The update feed (`latest.json`) is signed (`tools/sign_manifest.mjs`, the app refuses an unsigned one), every release file carries a GitHub build-provenance attestation, and a manual dry run of the release workflow builds everything without publishing. The app is tested on Linux; its Windows and macOS builds are included but untested.
- The app's update feed (`latest.json`) now lives in this repo (`tools/build_manifest.mjs`).

### Added

- **Narrator on the dongle.** Short voice lines on the controller's speaker, in 7 languages: "Connected" when the controller connects, the new pairing slot when you switch (before it drops), and a battery warning at 10 % (once per connection). They work without the app, which turns them on and sets the language (off by default). Pre-encoded Opus in flash (`src/voice_bank.bin`, ~600 KB); while a line plays it takes the place of the game's speaker audio for a second or two. Configurator protocol 1.3: feature bit `0x80`, `0xF6` functions `0x0B` (narrator, saved on its own) and `0x0C` (say a line), config byte 37 `narrator_voice`.
- **Configurator interface (build flag `ENABLE_COMPANION_VENDOR`, off by default).** A control-only vendor USB interface for the upcoming desktop configurator. Linux's `hid_playstation` leaves it alone, and Windows binds WinUSB to it with no driver install. The gamepad descriptor stays byte-identical to a real DualSense.
- **Speaker boost up to +12 dB.** The speaker level (app, OLED Settings) now goes above 0 dB, as digital gain on the game audio before encoding; anything past full scale is clipped cleanly. +12 dB was the loudest clean setting on a mastered music test with the desktop volume at 85 %. The UAC volume reported to the PC stays capped at 0 dB.
- **Controller model, colorway and player indicator (protocol 1.2).** The identity report adds which controller is connected (DualSense or Edge), its colorway code (read once per connection from the controller's factory serial with Sony's read-only test command; only the two colorway characters are kept) and the player-indicator LEDs the PC set.
- **Live input report (protocol 1.2).** `0xF0` on the configurator interface returns the controller's current input (buttons, sticks, triggers, touchpad, motion, battery) for the desktop app's live controller view.
- **Identity report 1.1.** `0xF1` also carries charging state, the controller's current lightbar color and battery level, for the desktop app's tray icon.
- **Configurator commands.** An identity report (`0xF1`) with protocol version and last-command result; switch/forget/wipe pairing slots, reboot, reboot into BOOTSEL, factory reset and trigger preset preview over `0xF6`. Saving now reports success or failure.

### Fixed

- The OLED's speaker level steps on a 5 dB grid again, so it can come back down from +12 dB to 0 dB.
- The identity report no longer shows the previous controller's model, battery or input for the first moments after a different controller connects.
- An empty USB output transfer is ignored instead of re-sending stale data to the controller.

- **Host config changes now show on the dongle without a reboot.** Lightbar mode and favourites, and OLED brightness, were only read at boot; a dongle-side Settings save could also write its stale snapshot over changes made from the PC.

---

## [0.7.0-oled-edition] — 2026-10-10

> **After updating, re-pair your controller(s)** (hold Create + PS). The config, pairing-slot and remap flash sectors moved (see Fixed); saved settings are migrated automatically, but pairing slots and button remaps start fresh.

Headline: **the controller speaker is clean over the dongle.** The residual crackle left after the 0.6.x RX-buffer fix traced to core1 missing its Opus-encode deadline: libopus ran from flash, and core0's BT/USB/OLED code kept evicting the shared XIP cache, so ~7 % of 10.7 ms speaker chunks were dropped (measured with the new `Drop` diagnostic row). This release ports upstream awalol's audio-path work (RAM-resident audio, packed `0x39` audio reports, core1 mic decode) and keeps the OLED Edition's own additions on top. A/B'd by ear against upstream's current build on the same controller.

### Fixed

- **Speaker crackle.** Core1's audio path now runs from RAM: libopus `.text`/`.rodata`, the two per-frame `WDL_Resampler` methods, `core1_entry` and its speaker/mic routines, the inter-core queue ops, and RAM `memcpy`/`memset`/`memmove` (upstream Phases A.1–A.3, B.4, B.5; `cmake/relocate_to_ram.cmake`, `src/ram_mem.c`). ~380 KB of 520 KB RAM in use; clock unchanged at 320 MHz.
- **Speaker no longer pinned at full amp volume.** Every audio frame forced the DS5 amp to `VolumeSpeaker=0x64` regardless of the host slider. The host's UAC1 speaker volume now drives the amp (0 dB → 0x64 … −100 dB → 0), as upstream (`aa5e012`).
- **Mic stutter with the mic on (#13).** Mic Opus decode moved from core0's USB/BT pump to core1. Mic PCM now goes to USB in 1 ms slices as the IN FIFO frees up, following the host clock — the old 1920-byte frame writes never fit the ~784-byte FIFO and were silently truncated. The mic only streams while the host has the mic interface open (upstream `370032e`, `8d25a38`). Packet-loss concealment (Opus PLC) is kept and now runs on core1.
- **Constant trigger buzz (#11).** Host output reports are forwarded to the controller as-is, with the host's own allow bits, instead of a latched copy of trigger state re-sent at the audio rate.
- **Saving settings during audio could crash the dongle.** Config, remap and slot flash writes now park core1 via `flash_safe_execute()` (upstream `6a52b5e`); BTstack's link-key storage gets the same protection. Required now that core1's audio path runs from RAM: without it, core1 died on the first flash write after pairing.
- **Flash sectors overlapped Bluetooth's link-key storage.** On RP2350 BTstack keeps pairing keys in the two sectors below the last one, exactly where the pairing slots and button remap lived, so each side overwrote the other (flaky pairing, BTstack rewriting its storage at every boot). Config, slots and remap now sit below BTstack's bank (`src/flash_layout.h`), as upstream did for its config (#244). Saved config is migrated from the old location once. Flash writes made before core1 starts no longer get silently refused.
- **Lightbar stayed on the player indicator after connecting.** The controller ignores the startup state sent while it is still answering the dongle's feature requests; the state is now sent again once the host has enumerated the dongle.

### Changed

- **Audio goes out as upstream's packed `0x39` report** — two haptic + two speaker frames per BT packet, half the packet rate of one `0x36` per frame, with no controller state inside (upstream `8eb8612`). Controller state reaches the DS5 as `0x31` output reports (every host report, forwarded immediately), and volume / mic-stream changes go out as one-off reports. The OLED lightbar now pushes its color during audio too.
- **Audio Auto Haptics defaults to Off.** In Fallback mode the actuators followed any speaker audio when a game sent no haptics, so music and desktop sound made the controller vibrate constantly. Haptics now come only from the game; Fallback / Mix / Replace stay available in the OLED Settings menu. Existing saved settings are kept — set `AutoHap` to Off on the OLED if yours is on Fallback.
- Haptics gain is applied after the 48 kHz → 3 kHz resampler, as upstream (`a55fd46`).
- **Haptics gain now goes up to 6.0** (was 2.0), in 0.5 steps on the OLED `Gain` item, for when HD haptics feel weaker than wired.

### Known issues

- Saving a setting while audio is playing causes one short pop: core1 is paused for the flash erase and one 10 ms audio frame is lost. (Before this release, saving during audio could crash the dongle.)
- After the controller stops charging, the lightbar keeps the last amber frame of the charging pulse until a lightbar mode is chosen or the game sets a color.

### Added

- **Audio pipeline diagnostics over USB:** new `0xF5` feature report (USB-in / BT-out rates, drops, core1 liveness, queue counts, mic frames, flash writes / failures, last host feature report), read with `scripts/audio_diag.py` (one snapshot, or `scripts/audio_diag.py 5` for rates over 5 s). The Diagnostics screen gains matching `c1` and `raw/hap` rows.
- **`Drop r/e/h` row on the OLED Diagnostics screen** — audio chunks dropped because core1 fell behind (`r`), because Bluetooth fell behind (`e`), or while a haptic frame waited for its speaker pair (`h`). Replaces the `trig fold` row, which no longer applies.

---

## [0.6.12-oled-edition] — 2026-06-07

> **Supersedes the withdrawn v0.6.11.** v0.6.11 bundled the two OLED quality-of-life features below with two regressions (constant haptics, issue #11; an audio retiming change, issue #12) and is not recommended. v0.6.12 is built on the verified-working native-trigger firmware and folds in **only** the two separable good features — `CtrlWake` and brightness persistence — leaving the trigger and audio code paths byte-identical to the tested build. The v0.6.11 regression changes (the `state_mgr.cpp` trigger-FFB allow-bit mirror and the `audio.cpp` resampler retiming) are intentionally **not** carried over.

Headline: **native DualSense adaptive triggers now fire in real PC games on Linux/Proton — through the dongle, 1:1 with a wired controller.** For weeks the triggers only ever worked in the on-dongle OLED self-test, never in a game: the dongle was *recognised*, but games fell back to a generic/Xbox pad and never sent trigger effects. The root cause was three ways the dongle's USB presentation differed from genuine Sony hardware — each one accepted by Linux's `hid_playstation` (which only checks size + CRC) but **rejected by a game's native DualSense detection, which validates the actual content**. Closing all three makes the dongle byte-for-byte indistinguishable from a real DS5 to the game.

**Host-side recipe (required alongside this firmware):** run the game on a Proton that carries the Wine `winebus.sys` **#9034** fix (Wine 11 / current Proton-GE — the bug suppresses the SDL gamepad device when a hidraw device exists for the same VID/PID, so the pad shows up everywhere *except* in-game) and **disable Steam Input** (native path). **No launch option is needed** — Wine 11 enables the hidraw native path by default. Works on **both Steam and Heroic** (on Heroic, fully quit Steam so it can't grab the pad, and keep any global `PROTON_PREFER_SDL` off). The game must natively support DualSense — XInput-only titles give rumble but no adaptive triggers. With a real DS5 this "just works"; the firmware changes below make the dongle match.

### Added

- **Native adaptive triggers & haptics in games on Linux/Proton, through the dongle.** Games with native DualSense support (Cyberpunk 2077, etc.) now drive the controller's adaptive triggers via the dongle, identical to a directly-wired DualSense — verified 1:1 by A/B against the same controller plugged in over USB-C. No Steam Input, no per-game hacks beyond the host recipe above. The firmware already proxies the host's trigger output reports to the controller (unchanged); the missing piece was getting games to *recognise* the dongle as a genuine DS5 in the first place (below).
- **`CtrlWake` setting — let the OLED sleep while you play.** New Settings toggle (default **on**, preserving the old behaviour). With it **off**, controller input no longer keeps the panel awake — only the OLED's own KEY0/KEY1 do — so the auto-dim / auto-off timers actually count down during gameplay and the screen can sleep while the controller is in active use. (folded from v0.6.11; issues #8/#9)
- **OLED brightness now persists across a power cycle.** The KEY1-long-press brightness choice is saved to config and restored on boot, instead of resetting to full every power-on. (folded from v0.6.11; issue #9)

### Changed

- **The DualSense HID report descriptor is now byte-identical to a real DS5 (289 bytes, was 321).** The OLED Edition declared four extra feature reports — `0xF6`–`0xF9`, the WebHID config/remap IDs — inside the gamepad collection, making the descriptor 32 bytes longer than genuine hardware. Games' native DualSense parser rejects that mismatch and falls back to a generic pad (no triggers); `hid_playstation` didn't care, which is why the OLED self-test always worked but games never did. The four declarations are removed (plus the runtime `wDescriptorLength` patch that overrode the static array, `0x41`→`0x21`). The firmware **still handles** `0xF6`–`0xF9`; they're simply undeclared now and work over Linux `hidraw` exactly like the existing `0xFD` diagnostic report.
- **Feature reports `0x09` (pairing), `0x20` (firmware) and `0x05` (calibration) now return the controller's real cached data, not zeros.** The earlier native-recognition fix answered these with zeros + a valid CRC — enough for `hid_playstation` to bind, but games validate the *content*: Cyberpunk GET-read `0x20`/`0x09` ~156× in a retry storm rejecting the zeros, never completed the DualSense handshake, and showed no controller. The dongle already fetches and caches the genuine reports from the connected controller over BT (`init_feature()`); `tud_hid_get_report_cb` now serves that cache (real firmware string / pairing / calibration), keeping the CRC-valid synthetic stub only as a fallback for the USB-enumeration probe before the BT link is up (so `hid_playstation` still binds then). Builds on the `hid_playstation` binding fix and the serial-=-MAC identity fix.

### Known tradeoff

- **The browser (WebHID) Config and Remap tabs no longer reach the dongle.** They drove config over reports `0xF6`/`0xF7`, which are no longer *declared* in the HID descriptor — and WebHID refuses undeclared report IDs (declaring them is exactly what broke game triggers). Mitigations, no firmware change needed: **on-dongle OLED config still works**, and because undeclared IDs are fine over Linux `hidraw`, a small `hidraw` CLI/script can read & write all of `0xF6`–`0xF9` (the same mechanism `scripts/mic_diag.sh` uses for `0xFD`). Restoring the *browser* tool would require moving the custom reports into a separate vendor HID collection — which risks the descriptor diverging from a real DS5 again, re-breaking triggers, so it's deliberately not done here.

---

## [0.6.10-oled-edition] — 2026-05-25

Headline: the **Gyro Tilt screen** is now actually usable — it applies the controller's per-unit factory **IMU calibration**, the dot **centres when the controller lies flat**, and tilt **tracks the direction** you move it. Also adds an **L3 / R3 stick-click indicator** on the Status screen and makes the **charge-ETA** robust so it no longer over-reports off a single slow charge step. Everything here is on-dongle display only — **what games receive is unchanged** (the full gyro/accel stream is still forwarded byte-for-byte). UF2s attached to [the GitHub release](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/v0.6.10-oled-edition) (built by `.github/workflows/release.yml`).

### Added

- **L3 / R3 click indicator on the OLED Status screen.** Clicking a stick in now flashes its analog-stick box inverse (white box, black dot) for as long as it's held — previously the stick clicks had no on-screen feedback. Mirrored in the web config tool's OLED Preview.

### Changed

- **Gyro Tilt screen reworked: per-unit IMU calibration, a tilt dot that centres when flat, and intuitive direction.** Three things, all display-only — the host input report (all gyro + accel axes) is still forwarded byte-for-byte, so in-game motion is unaffected:
  - **Calibration.** The DualSense ships factory gyro/accel bias + sensitivity in feature report `0x05` (which the dongle already fetches and caches at connect). Previously the tilt visuals used raw accel counts; now the cached `0x05` is parsed once per connection (re-read per controller, so it stays correct across the 4 pairing slots) and applied as `(raw − bias) × sensitivity`, keeping the same ±8192 ≈ 1 g scale. A bad/short read is rejected (same sanity gate SDL uses) and it falls back to raw — no regression when calibration is unavailable. The tilt→RGB lightbar mode uses the corrected accel too. Parse/apply mirror SDL's `SDL_hidapi_ps5.c` (zlib-licensed; credit).
  - **Centred when flat.** The dot is now driven by the X (roll) and **Z** (pitch) axes — the two that read ~0 when the controller lies flat — instead of X/Y. Gravity rests on Y when flat, so the old Y mapping pegged the dot to the bottom edge at rest; it now sits centred.
  - **Direction follows the controller.** Both axes are negated so tilting left moves the dot left and tilting forward moves it up, instead of mirrored.
  - The dot centring + direction are mirrored in the web config tool's OLED Preview (its mock IMU now also rests gravity on Y to match real hardware).

### Fixed

- **Charge-ETA no longer balloons off a single slow 10% step.** The Status-screen `~Nm` charge estimate timed each 10% battery step and projected the rest, but one anomalously slow step (e.g. ~47 min — observed reading `~222m` at 70% on the dock) used to drag the whole projection up because the rate was a mean over only 3 steps. Each timed step's bulk-equivalent is now clamped to a 30-min ceiling and the rate is taken as the **median** over the last 5 steps, so a single under-load/anomalous reading can't dominate. Mirrored in the web OLED Preview emulator.

---

## [0.6.9-oled-edition] — 2026-05-24

Headline feature: **on-dongle button remapping** — reassign any of the 16 digital controls, stored on the dongle so it works in every game and on every OS with no host-side software, edited visually in the web config tool's new **Remap** tab (a click-the-controller diagram built on Zacksly's CC BY 3.0 DualSense art). Also ships **packet-loss concealment** for the BT microphone. UF2s attached to [the GitHub release](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/v0.6.9-oled-edition) (built by `.github/workflows/release.yml`); this is the first release without the debug UF2 as a download.

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

## Post-0.5.4 work before the 0.6.0 rebase — 2026-05-16

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
