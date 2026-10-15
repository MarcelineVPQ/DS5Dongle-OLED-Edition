#!/usr/bin/env bash
# Mic-path host-side diagnostic for the DS5Dongle (OLED Edition).
#
# Subcommands:
#   status   — one-shot snapshot of dongle USB / ALSA / capture stream state.
#              Prints whether the dongle enumerated, what ALSA card # it
#              took, the capture stream's current alt setting + sync mode,
#              and whether a paired DualSense is reachable.
#   capture  — runs a 3-second arecord on the mic IN endpoint, reports
#              ALSA result code, captured byte count, and a non-silence
#              indicator (peak abs sample value via Python's wave module).
#              Tells us in one shot whether the firmware is producing
#              actual isoc-IN data and whether anything audio-like is
#              showing up.
#   watch    — loops `status` every 2 seconds, prints only on changes —
#              useful for catching the moment pairing completes or the
#              arecord stream opens/closes.
#
# Why a script: lets the assistant query mic-path state directly from
# the host rather than waiting for the user to relay OLED counters
# through chat, which dominated the early Phase-3 debugging time.
#
# Requirements (all already installed on the user's machine):
#   - arecord (alsa-utils)
#   - lsusb (usbutils)
#   - python3 (for wave-file stats)

set -u

VID=054c
PID=0ce6
DEV_NAME_RE='DualSense Wireless Controller'

find_card() {
    arecord -l 2>/dev/null | awk -v re="$DEV_NAME_RE" '
        $0 ~ re {
            for (i = 1; i <= NF; i++) {
                if ($i == "card") { gsub(":", "", $(i+1)); print $(i+1); exit }
            }
        }'
}

show_status() {
    local card
    card="$(find_card)"

    # USB layer — is the device visible?
    if lsusb -d "${VID}:${PID}" >/dev/null 2>&1; then
        printf 'usb: present (%s:%s)\n' "$VID" "$PID"
    else
        printf 'usb: NOT FOUND — is the dongle plugged in?\n'
        return 1
    fi

    if [[ -z "$card" ]]; then
        printf 'alsa: dongle is on USB but not exposed as an audio card\n'
        return 1
    fi
    printf 'alsa: card %s\n' "$card"

    # Capture stream details (interface 2 alt 1 mic-IN endpoint)
    if [[ -r "/proc/asound/card${card}/stream0" ]]; then
        # Grep just the Capture block so we see status + altset + endpoint
        awk '/^Capture:/,0' "/proc/asound/card${card}/stream0" | head -10 | sed 's/^/  /'
    else
        printf '  (no /proc/asound/card%s/stream0 — older kernel?)\n' "$card"
    fi
}

run_capture() {
    local card secs="${1:-3}"
    card="$(find_card)"
    if [[ -z "$card" ]]; then
        printf 'no dongle capture device found\n'
        return 1
    fi

    local tmp
    tmp="$(mktemp -t mic_diag.XXXXXX.wav)"
    printf 'capturing %ss from card %s into %s ...\n' "$secs" "$card" "$tmp"

    local err
    err="$(arecord -q -D "plughw:${card},0" -f S16_LE -c 2 -r 48000 -d "$secs" "$tmp" 2>&1)"
    local rc=$?
    if (( rc != 0 )); then
        printf 'arecord exit=%d: %s\n' "$rc" "$err"
        rm -f "$tmp"
        return "$rc"
    fi

    # Stats via Python — peak abs sample is enough to distinguish "stream
    # produced silence" from "stream produced actual audio".
    python3 - "$tmp" <<'PY'
import sys, wave, struct
path = sys.argv[1]
with wave.open(path, 'rb') as w:
    nframes = w.getnframes()
    sw = w.getsampwidth()
    ch = w.getnchannels()
    fr = w.getframerate()
    raw = w.readframes(nframes)
nsamples = nframes * ch
fmt = '<' + ('h' * nsamples)
data = struct.unpack(fmt, raw)
peak = max(abs(s) for s in data) if data else 0
nonzero = sum(1 for s in data if s != 0)
rms = (sum(s*s for s in data) / max(len(data), 1)) ** 0.5
print(f'wav: {nframes} frames, {ch} ch, {sw*8}-bit, {fr} Hz')
print(f'samples: nonzero={nonzero}/{nsamples}  peak={peak}  rms={rms:.1f}')
if peak == 0:
    print('verdict: STREAM IS SILENT — firmware not producing isoc-IN data')
elif peak < 100:
    print('verdict: extremely quiet — possibly DC offset only')
else:
    print('verdict: AUDIO PRESENT')
PY
    rm -f "$tmp"
}

watch_status() {
    local prev=""
    while :; do
        local now
        now="$(show_status 2>&1)"
        if [[ "$now" != "$prev" ]]; then
            printf '\n=== %s ===\n%s\n' "$(date '+%H:%M:%S')" "$now"
            prev="$now"
        fi
        sleep 2
    done
}

bt_trace() {
    # Query the firmware's 0xFD vendor feature report via /dev/hidraw.
    # 0xFD carries two sections:
    #   Section 1 (bytes 0..31)  — mic-investigation: BT 0x31 / non-0x31
    #     counts, byte[2] OR mask, frame prefixes. Originally used to
    #     locate the mic stream; kept for any future BT-input triage.
    #   Section 2 (bytes 32..43) — host -> dongle -> BT trigger flow
    #     counters (issue #3): host 0x02 OUT received total, of those
    #     where AllowRight/LeftTriggerFFB was set, and of those forwarded
    #     as BT 0x31 sub-0x10. Lets the user triage adaptive-trigger
    #     issues without needing an OLED in the loop.
    #   Section 3 (bytes 44..49, firmware 0.9.5+) — mic jitter buffer:
    #     frames its level control dropped, the level now and the highest
    #     this mic session (x 10 ms = the mic delay the buffer adds).
    # The ioctl buffer is 51 bytes (50 payload + 1 byte that the kernel
    # fills with the report ID); older firmware answers 44.
    python3 - <<'PY'
import fcntl, glob, struct, sys, time

VID, PID = 0x054c, 0x0ce6
IOCTL_SIZE = 51  # 1 byte report ID + 50 bytes firmware payload (44 before 0.9.5)

def find_dongle():
    for path in sorted(glob.glob('/dev/hidraw*')):
        try:
            f = open(path, 'rb+')
            buf = bytearray(IOCTL_SIZE); buf[0] = 0xFD
            ioctl_num = (3 << 30) | (IOCTL_SIZE << 16) | (ord('H') << 8) | 0x07
            try:
                fcntl.ioctl(f, ioctl_num, buf)
                return f
            except OSError:
                f.close()
        except (OSError, PermissionError):
            pass
    return None

f = find_dongle()
if f is None:
    print('no dongle found (or no /dev/hidraw permission)')
    sys.exit(1)

def query():
    buf = bytearray(IOCTL_SIZE); buf[0] = 0xFD
    ioctl_num = (3 << 30) | (IOCTL_SIZE << 16) | (ord('H') << 8) | 0x07
    n = fcntl.ioctl(f, ioctl_num, buf)
    # Kernel prepends the report ID at byte 0; firmware payload starts at byte 1.
    return bytes(buf[1:max(1, n)])

def decode(b):
    return {
        'bt31':       struct.unpack('<I', b[0:4])[0],
        'btoth':      struct.unpack('<I', b[4:8])[0],
        'other_id':   b[8],
        'other_or':   b[9],
        'b2_or':      b[10],
        'b2_last':    b[11],
        'lmin':       struct.unpack('<H', b[12:14])[0],
        'lmax':       struct.unpack('<H', b[14:16])[0],
        'othpfx':     b[16:24].hex(),
        'anypfx':     b[24:32].hex(),
        'host02':     struct.unpack('<I', b[32:36])[0] if len(b) >= 36 else 0,
        'host02_trig':struct.unpack('<I', b[36:40])[0] if len(b) >= 40 else 0,
        'host02_tx':  struct.unpack('<I', b[40:44])[0] if len(b) >= 44 else 0,
        'mic_lvl':    len(b) >= 50,
        'mic_drops':  struct.unpack('<I', b[44:48])[0] if len(b) >= 48 else 0,
        'mic_level':  b[48] if len(b) >= 50 else 0,
        'mic_max':    b[49] if len(b) >= 50 else 0,
    }

s1 = query(); time.sleep(1.0); s2 = query()
d1 = decode(s1); d2 = decode(s2)

# Mic-investigation section
bt31_rate  = d2['bt31']  - d1['bt31']
btoth_rate = d2['btoth'] - d1['btoth']
print('-- BT input (mic investigation legacy) --')
print(f'rates: 0x31={bt31_rate}/s, non-0x31={btoth_rate}/s')
print(f'len range: {d2["lmin"]}-{d2["lmax"]} bytes')
print(f'byte[2] OR mask across 0x31 frames: 0x{d2["b2_or"]:02X}  last=0x{d2["b2_last"]:02X}')
print(f'non-0x31 report IDs: OR mask=0x{d2["other_or"]:02X}  most recent=0x{d2["other_id"]:02X}')
print(f'last non-0x31 prefix (data[0..7]): {d2["othpfx"]}')
print(f'last ANY frame (data[0..7]):       {d2["anypfx"]}')

# Trigger-flow section
o02_rate  = d2['host02']      - d1['host02']
trig_rate = d2['host02_trig'] - d1['host02_trig']
tx_rate   = d2['host02_tx']   - d1['host02_tx']
print()
print('-- Host -> dongle -> BT trigger flow (issue #3) --')
print(f'host 0x02 OUT:       total={d2["host02"]}      ({o02_rate}/s)')
print(f'  w/ AllowTrigFFB:   total={d2["host02_trig"]} ({trig_rate}/s)')
print(f'  forwarded to BT:   total={d2["host02_tx"]}   ({tx_rate}/s)')
if d2['host02'] > 0 and d2['host02_trig'] == 0:
    print('verdict: host is sending 0x02 reports but never sets Allow*TriggerFFB.')
    print('         The host driver is not requesting adaptive trigger effects.')
elif d2['host02_trig'] > 0 and d2['host02_tx'] < d2['host02_trig']:
    print('verdict: trigger Allow bits are set but some reports are not reaching BT.')
    print('         Likely the speaker-active gate in main.cpp swallowed them.')
elif d2['host02_trig'] > 0:
    print('verdict: full chain reached the controller. Tension still missing -> Sony BT limit.')

# Mic jitter-buffer section (firmware 0.9.5+)
print()
print('-- Mic jitter buffer (level control) --')
if not d2['mic_lvl']:
    print('not reported by this firmware (before 0.9.5)')
else:
    print(f'level now: {d2["mic_level"]} frames (~{d2["mic_level"] * 10} ms)   '
          f'highest this session: {d2["mic_max"]} frames (~{d2["mic_max"] * 10} ms)')
    print(f'frames dropped by the level control: {d2["mic_drops"]} ({d2["mic_drops"] - d1["mic_drops"]}/s)')
PY
}

case "${1:-status}" in
    status)   show_status ;;
    capture)  shift; run_capture "${1:-3}" ;;
    watch)    watch_status ;;
    bt-trace) bt_trace ;;
    *)
        printf 'usage: %s {status|capture [secs]|watch|bt-trace}\n' "$0" >&2
        exit 2
        ;;
esac
