#!/usr/bin/env python3
"""Pack the narrator's firmware lines into src/voice_bank.bin.

The lines are recorded and mastered in the app repo
(DS5Dongle-OLED-Config-Web/assets-src/voice/<lang>-speaker/fw_*.wav, 48 kHz
mono, approved by ear on the controller). Here they are encoded the way the
DualSense's speaker expects them: Opus, 48 kHz stereo, 480-sample frames, CELT
only (as the firmware's own encoder). They're stored compact; the firmware
pads each frame to the 0x39 report's 200-byte slot when it sends it.

The controller plays a 480-sample frame in 10.67 ms, not 10 ms: two frames
per 0x39 report, one report per 64 haptic samples at 3 kHz (src/report_clock.h,
the rate the host path sends; audio.cpp squeezes 512 host samples into each
480-sample frame). So each frame must carry 10.67 ms of her voice: the WAV is
resampled to 45 kHz and the result labelled 48 kHz for the encoder. Encoded
at 48 kHz as is, a line played 6.25 % slow and 1.1 semitones flat.

Bank layout (little endian):
  "DS5V" u8 version=1, u8 langs, u8 lines, u8 0
  langs*lines entries: u32 offset (from the start of the bank), u16 frames, u16 0
  frame data: u8 length + Opus packet, repeated

Usage: tools/build_voice_bank.py ../DS5Dongle-OLED-Config-Web/assets-src/voice
"""
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

# Order matters: the firmware indexes by these (config voice byte = lang + 1).
LANGS = ["en", "de", "fr", "es", "pt-BR", "zh-CN", "ja"]
LINES = ["fw_connected", "fw_slot_0", "fw_slot_1", "fw_slot_2", "fw_slot_3", "fw_battery_low"]
BITRATE = "48k"
MAX_PACKET = 200
FRAME_SAMPLES = 480
FRAME_S = 64 / 3000 / 2                   # half a report period (src/report_clock.h)
PLAY_RATE = round(FRAME_SAMPLES / FRAME_S)  # 45000 Hz: the rate the controller really plays at
assert PLAY_RATE == 48000 * 480 // 512 == 45000


def ogg_packets(data: bytes) -> list[bytes]:
    """The Opus packets in an Ogg stream, minus the two header packets."""
    packets, partial, pos = [], b"", 0
    while pos < len(data):
        assert data[pos : pos + 4] == b"OggS", "not an Ogg page"
        nseg = data[pos + 26]
        lacing = data[pos + 27 : pos + 27 + nseg]
        body = pos + 27 + nseg
        for lace in lacing:
            partial += data[body : body + lace]
            body += lace
            if lace < 255:
                packets.append(partial)
                partial = b""
        pos = body
    return packets[2:]


def encode(wav: Path) -> list[bytes]:
    with tempfile.NamedTemporaryFile(suffix=".ogg") as out:
        subprocess.run(
            ["ffmpeg", "-loglevel", "error", "-y", "-i", str(wav),
             "-af", f"aresample={PLAY_RATE}:resampler=soxr,asetrate=48000", "-ac", "2", "-ar", "48000",
             "-c:a", "libopus", "-application", "lowdelay", "-frame_duration", "10",
             "-b:a", BITRATE, "-vbr", "on", str(out.name)],
            check=True,
        )
        frames = ogg_packets(Path(out.name).read_bytes())
    if len(frames) % 2:
        frames.append(frames[-1])  # even: two frames per report (the tail is silence)
    for f in frames:
        assert 0 < len(f) <= MAX_PACKET - 3, f"frame too big to pad: {len(f)}"
    return frames


def main() -> None:
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("../DS5Dongle-OLED-Config-Web/assets-src/voice")
    dest = Path(__file__).resolve().parent.parent / "src" / "voice_bank.bin"
    header = bytearray(b"DS5V" + bytes([1, len(LANGS), len(LINES), 0]))
    table_size = len(LANGS) * len(LINES) * 8
    data = bytearray()
    entries = []
    total_s = 0.0
    for lang in LANGS:
        for line in LINES:
            frames = encode(src / f"{lang}-speaker" / f"{line}.wav")
            entries.append((len(header) + table_size + len(data), len(frames)))
            total_s += len(frames) * FRAME_S
            for f in frames:
                data += bytes([len(f)]) + f
    for offset, count in entries:
        header += struct.pack("<IHH", offset, count, 0)
    dest.write_bytes(bytes(header) + bytes(data))
    print(f"{dest.name}: {len(LANGS)} languages x {len(LINES)} lines, {total_s:.1f} s, {(len(header) + len(data)) // 1024} KB")


if __name__ == "__main__":
    main()
