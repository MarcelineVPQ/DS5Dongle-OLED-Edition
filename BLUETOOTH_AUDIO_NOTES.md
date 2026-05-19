# Bluetooth microphone investigation — current status

**TL;DR:** The DualSense's built-in microphone does **not** work when the controller is paired to this dongle over Bluetooth. It works fine when the controller is connected directly to a host over USB. This is a Sony / DS5-firmware-side limitation we currently can't work around without reverse engineering or BT-sniffer access to PS5 ↔ DS5 traffic. The same limitation is documented in the upstream Linux kernel driver (`drivers/hid/hid-playstation.c` line ~1509: *"Bluetooth audio is currently not supported"*).

This file is a hand-off / research log for the next person who tries.

## What does work

- **Direct USB-C from DS5 → host:** mic enumerates as a UAC1 IN endpoint at 48 kHz / 16-bit / 2 channels on `EP 0x82`, max packet 196 bytes. ALSA recognizes it as `card N: Controller [DualSense Wireless Controller]`. `arecord` captures real audio after raising the `Headset Capture Volume` mixer control (it defaults to 0 dB).
- **Our dongle's USB descriptor** correctly mirrors the DS5's UAC1 layout — same interfaces, same alt settings, same endpoint addresses, same packet sizes. Verified with `lsusb -v` against a real DS5.
- **All the firmware-side decode infrastructure for BT mic is in place** (Opus decoder, `mic_fifo` queue, `tud_audio_write` to the IN endpoint, mono → stereo duplication) — see `src/audio.cpp`. It's currently gated behind `if (false)` in `src/main.cpp`'s `on_bt_data()` because we have nothing to feed it.

## What doesn't, and why

The DS5 firmware on the test controller (build date `Jul 4 2025`, queried via feature report 0x20) **does not stream microphone audio over the standard BT-HID L2CAP channels** (PSM 0x11 control + 0x13 interrupt).

What we tried:

1. **Upstream `awalol/DS5Dongle` `mic` branch as reference.** That branch claims to extract a 71-byte Opus packet at `data + 4` of any BT input report where `(data[2] >> 1) & 1` is set. On our DS5 firmware, **bit 1 of byte 2 is never set** (verified across thousands of frames via the `g_31_b2_or` OR mask). The upstream RE was likely done on a different (older) DS5 firmware revision.
2. **Bit 0 of byte 2** matches roughly all standard input reports — confirmed by reading the supposed "mic prefix" via `0xFD` feature report and seeing live stick X/Y values (not Opus data). Not a mic flag.
3. **Frame length sweep.** Longest BT 0x31 frame we ever see is 79 bytes — a fully-decoded standard DS5 input report (sticks + IMU + touchpad + battery + sensor timestamp + trailing zeros). No audio bytes appended anywhere.
4. **Other report IDs.** Counted ALL incoming BT input reports by report ID. Only `0x01` (rare) and `0x31` (common). No 0x33 / 0x35 / 0x36 / 0x39 / etc. The DS5 isn't sending anything mic-shaped on a different ID.
5. **State configuration matching the kernel.** Set `AllowAudioControl=1`, `AllowMicVolume=1`, `AllowAudioMute=1`, `MicSelect=Internal`, `VolumeMic=0x40`, `MicMute=0`, `AudioPowerSave=0` — exactly what `hid-playstation.c` sets when calling its "Enable microphone" path. DS5 still doesn't stream.
6. **Bidirectional audio session hypothesis.** Maybe the DS5 only streams mic when there's also active speaker audio (`0x36` packets) flowing. Tested: ran `aplay /dev/zero` simultaneously with `arecord`. No change in BT-side counters, no new report IDs, no longer frames. Disproved.
7. **State refresh on host UAC1 alt-setting change.** Considered hooking `tud_audio_set_itf_cb(itf=2, alt=1)` to send the DS5 a fresh "enable mic" state update. Not implemented — given the kernel comment and our state matching the kernel's own "enable" sequence, this wouldn't have helped.

What we **did not** try (real next steps if anyone picks this up):

- **SDP browse the DS5** over BT after pairing. Discover what L2CAP PSMs / services it advertises beyond HID. If there's a Sony proprietary audio PSM we haven't subscribed to, that's where mic traffic might live.
- **Open additional L2CAP channels** (proprietary audio PSM if found, or standard ones like A2DP=0x19 / RFCOMM=0x03) and watch for unsolicited inbound data.
- **Compare DS5 firmware revisions.** Test with an older DS5 (pre-2024 manufacture) and see if it streams mic over BT — that would tell us whether Sony removed the feature or just nobody documented the protocol. (We only have one DS5; can't test.)
- **BT sniffer** between a PS5 console and a DS5 during voice chat. Tells us exactly what L2CAP channels and bytes Sony uses for mic. Equipment-intensive (~$50–200 for an Ubertooth or commercial sniffer).
- **DS5 firmware disassembly.** Legally fraught, almost certainly EULA-violating.

## What we built that's useful regardless

These all stay shipped — they're general-purpose audio-debug infrastructure now:

- **`scripts/mic_diag.sh`** with subcommands `status`, `capture [secs]`, `watch`, `bt-trace`. Drives the entire diagnostic loop from the host without needing OLED-relay-through-the-user; reads vendor feature reports via `/dev/hidraw`.
- **Vendor HID feature report `0xFD`** (32 bytes): BT input-report counter, non-0x31 counter, last seen non-0x31 report ID, OR mask of byte 2 across 0x31 frames, length range, hex prefix of last frame.
- **Vendor HID feature report `0xFE`** (82 bytes): full content of the longest 0x31 frame seen, for byte-level inspection.
- **OLED Diagnostics screen** carries BT31/Mic rate + recent frame prefix + opus dec/wrote bytes — useful for any future audio-path debugging at the bench.
- **`src/audio.cpp`** mic-decode infrastructure (Opus decoder on core0, FIFO, mono → stereo duplication, `tud_audio_write` to IN endpoint). Disabled at the `mic_add_queue` call site, ready to re-enable the moment a real mic trigger is identified.
- **`src/state_mgr.cpp`** initial state corrected — `VolumeMic` was `0xff` (out of valid range per spec; max is `0x40`), `MuteControl` had all `*PowerSave` bits set which would have power-gated the audio DSP. These corrections don't enable BT mic but they're the right defaults regardless.

## References

- Linux kernel `drivers/hid/hid-playstation.c`. Quoted lines: ~1407–1420 (mic enable/disable), ~1509 (*"Bluetooth audio is currently not supported"*). [Raw source on GitHub mirror](https://raw.githubusercontent.com/torvalds/linux/master/drivers/hid/hid-playstation.c).
- PSDevWiki [DualSense HID Commands](https://www.psdevwiki.com/ps5/DualSense_HID_Commands) — has factory/manufacturer commands (report IDs 128, 129, 160, 164, 165) for BT patches and audio codec selection, but explicitly notes most "do not work with retail controllers". Not a path forward.
- Upstream `awalol/DS5Dongle` branch `mic` (commits `9c197fc feat: mic work`, `3829163 mic mono channel`). RE'd a working mic path for an older DS5 firmware revision; we ported the data plumbing but the BT-side trigger differs on current firmware.
- dualsensectl: `command_microphone on/off` sets `valid_flag0 |= DS_OUTPUT_VALID_FLAG0_AUDIO_CONTROL_ENABLE` and clears `DS_OUTPUT_POWER_SAVE_CONTROL_MIC_MUTE`. Same as what we already do on connect.

## For users asking about the mic

When users report "the mic doesn't work":

- **Plug the DS5 into the host via USB.** Mic works out of the box. You may need to raise the `Headset Capture Volume` mixer control if it defaults to 0 dB.
- **Over the dongle's Bluetooth pairing, the mic is currently a known limitation** — not something a firmware update on our side can fix without further reverse engineering of the DS5's proprietary BT audio path.
- The diagnostic tools in `scripts/mic_diag.sh` are available if you want to help reverse engineer this; PRs welcome.
