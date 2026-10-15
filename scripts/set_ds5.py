#!/usr/bin/env python3
"""
DS5Dongle (OLED Edition) configuration tool.

Reads and writes the firmware's persistent config over USB HID feature
reports — no browser, no WebHID, works in any terminal. Ported from
loteran/DS5Dongle's scripts/set_ds5.py and extended for this fork's
Config_body layout (adds `config_version` at the start and `current_slot`
between `controller_mode` and `auto_haptics_*`).

Requires EITHER `cython-hidapi` (preferred — `pip install hidapi`) OR
the `hid` package (`pip install hid`). The script auto-detects which is
installed; both expose `hid.device` somewhere with slightly different
constructor APIs, and we handle both.

Quick usage:
  scripts/set_ds5.py                       # print current config
  scripts/set_ds5.py --auto-haptics fallback --auto-haptics-gain 120
  scripts/set_ds5.py --haptics-gain 1.5 --speaker-volume -10
  scripts/set_ds5.py --slot 2              # switch active pairing slot
  scripts/set_ds5.py --version             # print firmware version
  scripts/set_ds5.py --rssi                # print live BT RSSI in dBm

Credit: protocol + script structure from loteran/DS5Dongle commit 5d6bc2f.
"""

import argparse
import struct
import sys

try:
    import hid
except ImportError:
    print("[ERROR] Missing dependency: install with  pip install hidapi", file=sys.stderr)
    sys.exit(1)


def _open_hid(vid, pid):
    """Open a HID device, abstracting over cython-hidapi vs apmorton/pyhidapi."""
    if hasattr(hid, 'Device'):                   # apmorton/pyhidapi: hid.Device(vid, pid)
        return hid.Device(vid, pid)
    if hasattr(hid, 'device'):                   # cython-hidapi: hid.device() + .open(vid, pid)
        d = hid.device()
        d.open(vid, pid)
        return d
    raise RuntimeError("installed `hid` module has neither Device nor device — unknown variant")

SONY_VID = 0x054C
DS5_PID  = 0x0CE6
DSE_PID  = 0x0DF2

# Our Config_body wire layout — 19 bytes, little-endian, packed struct.
#   uint8  config_version          [0]      (firmware-set, ignored on write)
#   float  haptics_gain            [1:5]
#   float  speaker_volume          [5:9]
#   uint8  inactive_time           [9]
#   uint8  disable_inactive_disc   [10]
#   uint8  disable_pico_led        [11]
#   uint8  polling_rate_mode       [12]
#   uint8  audio_buffer_length     [13]
#   uint8  controller_mode         [14]
#   uint8  current_slot            [15]      (fork-specific: multi-slot pairing)
#   uint8  auto_haptics_enable     [16]      0=off 1=fallback 2=mix 3=replace
#   uint8  auto_haptics_gain       [17]      [0..200] percent
#   uint8  auto_haptics_lowpass    [18]      0=80Hz 1=160Hz 2=250Hz 3=400Hz
CONFIG_FMT  = '<BffBBBBBBBBBB'
CONFIG_SIZE = struct.calcsize(CONFIG_FMT)
assert CONFIG_SIZE == 19, f"unexpected CONFIG_SIZE {CONFIG_SIZE}"

POLLING_MODES    = {0: "250 Hz", 1: "500 Hz", 2: "Real-time (1000 Hz)"}
CONTROLLER_MODES = {0: "DS5", 1: "DSE (Edge)", 2: "Auto"}
AUTO_HAP_MODES   = {
    0: "Off",
    1: "Fallback (only when game sends no native haptic)",
    2: "Mix (native + audio-derived)",
    3: "Replace (audio-derived only, override native)",
}
LOWPASS_MODES = {0: "80 Hz", 1: "160 Hz", 2: "250 Hz", 3: "400 Hz"}


def open_device():
    last_err = None
    for pid, label in [(DS5_PID, "DualSense"), (DSE_PID, "DualSense Edge")]:
        try:
            d = _open_hid(SONY_VID, pid)
            print(f"[INFO] Connected to {label} (VID:PID 0x{SONY_VID:04X}:0x{pid:04X})")
            return d
        except Exception as e:
            last_err = e
            continue
    print("[ERROR] No DS5/DSE device found. Pair the controller with the Pico first.", file=sys.stderr)
    if last_err is not None:
        print(f"        (last open() error: {type(last_err).__name__}: {last_err})", file=sys.stderr)
    sys.exit(1)


def get_config(device):
    raw = bytes(device.get_feature_report(0xF7, 64))
    # raw[0] is the report ID echo; the actual Config_body starts at raw[1].
    body = raw[1:1 + CONFIG_SIZE]
    if len(body) < CONFIG_SIZE:
        print(f"[ERROR] Config too short ({len(body)} bytes, expected {CONFIG_SIZE}). "
              "Flash a newer firmware.", file=sys.stderr)
        sys.exit(1)
    (
        config_version,
        haptics_gain, speaker_volume,
        inactive_time, disable_inactive_disconnect, disable_pico_led,
        polling_rate_mode, audio_buffer_length, controller_mode,
        current_slot,
        auto_haptics_enable, auto_haptics_gain, auto_haptics_lowpass,
    ) = struct.unpack(CONFIG_FMT, body)
    return {
        'config_version':              config_version,
        'haptics_gain':                haptics_gain,
        'speaker_volume':              speaker_volume,
        'inactive_time':               inactive_time,
        'disable_inactive_disconnect': disable_inactive_disconnect,
        'disable_pico_led':            disable_pico_led,
        'polling_rate_mode':           polling_rate_mode,
        'audio_buffer_length':         audio_buffer_length,
        'controller_mode':             controller_mode,
        'current_slot':                current_slot,
        'auto_haptics_enable':         auto_haptics_enable,
        'auto_haptics_gain':           auto_haptics_gain,
        'auto_haptics_lowpass':        auto_haptics_lowpass,
    }


def write_config(device, cfg):
    body = struct.pack(
        CONFIG_FMT,
        cfg['config_version'] & 0xFF,
        cfg['haptics_gain'], cfg['speaker_volume'],
        cfg['inactive_time'] & 0xFF, cfg['disable_inactive_disconnect'] & 0xFF,
        cfg['disable_pico_led'] & 0xFF,
        cfg['polling_rate_mode'] & 0xFF, cfg['audio_buffer_length'] & 0xFF,
        cfg['controller_mode'] & 0xFF,
        cfg['current_slot'] & 0xFF,
        cfg['auto_haptics_enable'] & 0xFF, cfg['auto_haptics_gain'] & 0xFF,
        cfg['auto_haptics_lowpass'] & 0xFF,
    )
    # 0xF6 set protocol:  [0x01, ...body...] = update in-memory  →  [0x02] = persist to flash.
    device.send_feature_report(b'\xf6\x01' + body)
    device.send_feature_report(b'\xf6\x02')


def get_version(device):
    raw = bytes(device.get_feature_report(0xF8, 64))
    return raw[1:].rstrip(b'\x00').decode('ascii', errors='replace')


def get_rssi(device):
    raw = bytes(device.get_feature_report(0xF9, 64))
    if len(raw) < 2:
        return None
    val = raw[1]
    return val - 256 if val >= 128 else val   # int8


def fmt_cfg(c):
    return (
        f"  config_version     {c['config_version']}\n"
        f"  haptics_gain       {c['haptics_gain']:.3f}\n"
        f"  speaker_volume     {c['speaker_volume']:.1f} dB\n"
        f"  inactive_time      {c['inactive_time']} min\n"
        f"  inactive_disc      {'disabled' if c['disable_inactive_disconnect'] else 'enabled'}\n"
        f"  pico_led           {'off' if c['disable_pico_led'] else 'on'}\n"
        f"  polling_rate       {c['polling_rate_mode']} ({POLLING_MODES.get(c['polling_rate_mode'], '?')})\n"
        f"  audio_buffer       {c['audio_buffer_length']}\n"
        f"  controller_mode    {c['controller_mode']} ({CONTROLLER_MODES.get(c['controller_mode'], '?')})\n"
        f"  current_slot       {c['current_slot']}\n"
        f"  auto_haptics       {c['auto_haptics_enable']} ({AUTO_HAP_MODES.get(c['auto_haptics_enable'], '?')})\n"
        f"  auto_haptics_gain  {c['auto_haptics_gain']}%\n"
        f"  auto_haptics_lp    {c['auto_haptics_lowpass']} ({LOWPASS_MODES.get(c['auto_haptics_lowpass'], '?')})"
    )


AUTO_HAP_ARG = {'off': 0, 'fallback': 1, 'mix': 2, 'replace': 3}
LOWPASS_ARG  = {'80': 0, '160': 1, '250': 2, '400': 3}
CTRL_MODE_ARG = {'ds5': 0, 'dse': 1, 'auto': 2}
POLL_ARG     = {'250': 0, '500': 1, 'realtime': 2, 'rt': 2}


def build_parser():
    p = argparse.ArgumentParser(description="DS5Dongle (OLED Edition) config tool.")
    p.add_argument('--version', action='store_true', help="print firmware version and exit")
    p.add_argument('--rssi', action='store_true', help="print live BT RSSI (dBm) and exit")
    p.add_argument('--haptics-gain', type=float, help="float [1.0, 2.0]")
    p.add_argument('--speaker-volume', type=float, help="dB [-100, 0]")
    p.add_argument('--inactive-time', type=int, help="minutes [10, 60]")
    p.add_argument('--inactive-disc', choices=['on', 'off'], help="silent disconnect on idle")
    p.add_argument('--pico-led', choices=['on', 'off'])
    p.add_argument('--polling', choices=POLL_ARG.keys(), help="USB HID polling rate")
    p.add_argument('--audio-buffer', type=int, help="audio buffer length [32, 128] (16..31 read as 32, upstream #252)")
    p.add_argument('--controller-mode', choices=CTRL_MODE_ARG.keys())
    p.add_argument('--slot', type=int, choices=[0, 1, 2, 3], help="active multi-pairing slot")
    p.add_argument('--auto-haptics', choices=AUTO_HAP_ARG.keys(), help="Auto Haptics mode")
    p.add_argument('--auto-haptics-gain', type=int, help="percent [0, 200]")
    p.add_argument('--auto-haptics-lp', choices=LOWPASS_ARG.keys(), help="LP cutoff Hz")
    return p


def main():
    args = build_parser().parse_args()
    device = open_device()

    if args.version:
        print(f"Firmware: {get_version(device)}")
        return
    if args.rssi:
        rssi = get_rssi(device)
        print(f"BT RSSI: {rssi} dBm" if rssi is not None else "RSSI unavailable")
        return

    cfg = get_config(device)
    changes = []

    def set_kv(key, val, label=None):
        if cfg[key] != val:
            changes.append(f"  {label or key}: {cfg[key]} → {val}")
            cfg[key] = val

    if args.haptics_gain is not None:    set_kv('haptics_gain', args.haptics_gain)
    if args.speaker_volume is not None:  set_kv('speaker_volume', args.speaker_volume)
    if args.inactive_time is not None:   set_kv('inactive_time', args.inactive_time)
    if args.inactive_disc is not None:
        set_kv('disable_inactive_disconnect', 1 if args.inactive_disc == 'off' else 0,
               label='inactive_disc')
    if args.pico_led is not None:
        set_kv('disable_pico_led', 1 if args.pico_led == 'off' else 0, label='pico_led')
    if args.polling is not None:         set_kv('polling_rate_mode', POLL_ARG[args.polling])
    if args.audio_buffer is not None:    set_kv('audio_buffer_length', args.audio_buffer)
    if args.controller_mode is not None: set_kv('controller_mode', CTRL_MODE_ARG[args.controller_mode])
    if args.slot is not None:            set_kv('current_slot', args.slot)
    if args.auto_haptics is not None:    set_kv('auto_haptics_enable', AUTO_HAP_ARG[args.auto_haptics])
    if args.auto_haptics_gain is not None: set_kv('auto_haptics_gain', args.auto_haptics_gain)
    if args.auto_haptics_lp is not None: set_kv('auto_haptics_lowpass', LOWPASS_ARG[args.auto_haptics_lp])

    if changes:
        print("Updating config:")
        print("\n".join(changes))
        write_config(device, cfg)
        print("Saved to flash.")
        # Re-read to show what stuck after firmware validation
        cfg = get_config(device)
        print("\nNew config:")
    else:
        print("Current config:")
    print(fmt_cfg(cfg))


if __name__ == '__main__':
    main()
