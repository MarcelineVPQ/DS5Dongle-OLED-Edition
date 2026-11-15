# Tension: your own adaptive trigger effects (protocol 1.23)

Design of the firmware half, and the exact contract the app builds against.
Approved by the owner 2026-11-15. Code: `src/tension.h` (pure logic, host-tested
in `tests/host/test_tension.cpp`) and `src/tension.cpp` (board glue).

User-facing wording: the feature lives under Sony's **Adaptive triggers** and is
called **Tension** ("Tension: your own adaptive trigger effects"). Effect names
are Sony's (Feedback, Weapon, Vibration, Multiple Position Feedback, Slope
Feedback, Multiple Position Vibration); Bow, Galloping and Machine are shown as
unofficial. Never "dongle" in user text.

## 1. What it does

- A **set** (for example "Bow") holds one small program for L2 and one for R2,
  saved together under a name, **on the board**: it runs without the app, on
  Windows, Linux and macOS alike.
- A program is up to **4 stages**. Each stage is one of the DualSense's trigger
  effects plus a condition: the pull position is in the stage's window, and has
  been there at least the stage's hold time. The board reads the pull from every
  input report and switches effects mid-pull (the trigger runs one effect at a
  time).
- A stage can **ramp** one parameter of its effect, either across its pull
  window or over the time held, so effects build with pull position **and** with
  hold time (the +1).
- Per trigger, per set: **Fill in** (ours only while the game has no effect on
  that trigger) or **Replace** (always ours).

Out of scope for v1: assignable buttons / PS long-press, decoding captured game
effects into sliders, a DSX-compatible UDP API, community sharing.

## 2. Data model (wire = flash, little-endian, packed)

The bytes the app sends, the bytes in flash and the bytes it reads back are the
same `SetBody`.

### SetBody (134 bytes)

| Offset | Size | Field | Rule |
|---|---|---|---|
| 0 | 1 | `ver` | 1 |
| 1 | 1 | `flags` | 0 (reserved) |
| 2 | 16 | `name` | ASCII 0x20..0x7E, at least 1 character, NUL-padded (nothing after the first NUL) |
| 18 | 58 | `trig[0]` | L2's Program |
| 76 | 58 | `trig[1]` | R2's Program |

### Program (58 bytes)

| Offset | Size | Field | Rule |
|---|---|---|---|
| 0 | 1 | `count` | 0..4 stages in use. **0 = this set leaves the trigger to the game** |
| 1 | 1 | `policy` | 0 Fill in, 1 Replace |
| 2 | 4 × 14 | `stage[4]` | stages at or past `count` must be all zero |

### Stage (14 bytes)

| Offset | Field | Rule |
|---|---|---|
| 0 | `mode` | effect id, table below |
| 1..7 | `p[0..6]` | the effect's parameters (table below); unused ones must be 0 |
| 8 | `pull_min` | the stage's pull window, 0..255 as the user's trigger calibration corrects it (section 3) |
| 9 | `pull_max` | `pull_min <= pull_max` |
| 10 | `hold_min` | tenths of a second the pull must have been in the window (0..25.5 s) |
| 11 | `ramp` | bits 0..2: parameter index + 1 (0 = no ramp); bits 3..6: 0; bit 7: source (0 = pull across the window, 1 = time held) |
| 12 | `ramp_to` | the ramped parameter's value at the end of the ramp (its start value is `p[i]`) |
| 13 | `ramp_time` | tenths of a second for a ramp by hold (1..255); must be 0 for a ramp by pull or no ramp |

Ramp rules: no ramp = `ramp`, `ramp_to`, `ramp_time` all 0. A ramp by pull needs
`pull_max > pull_min`. Only scalar parameters ramp (not the packed zone levels
of the two Multiple Position modes; Multiple Position Vibration may ramp its
frequency `p[0]`). The effect with `p[i] = ramp_to` must be valid too; every
value in between then is.

### Effects (Sony's API ranges, bytes by Nielk1 rev 6)

| id | Effect | Wire mode | Status | Parameters | Rampable |
|---|---|---|---|---|---|
| 0 | Off | 0x05 | official | none | none |
| 1 | Feedback | 0x21 | official | p0 position 0–9, p1 strength 0–8 | p0, p1 |
| 2 | Weapon | 0x25 | official | p0 start 2–7, p1 end start+1..8, p2 strength 0–8 | p0..p2 |
| 3 | Vibration | 0x26 | official | p0 position 0–9, p1 amplitude 0–8, p2 frequency 0–255 Hz | p0..p2 |
| 4 | Multiple Position Feedback | 0x21 | official | p0..p4: the strength 0–8 of zones 0..9, zone 2i in the low nibble of p[i], zone 2i+1 in the high nibble | none |
| 5 | Slope Feedback | 0x21 | official | p0 start 0–8, p1 end start+1..9, p2 start strength 1–8, p3 end strength 1–8 | p0..p3 |
| 6 | Multiple Position Vibration | 0x26 | official | p0 frequency 0–255 Hz, p1..p5: amplitudes 0–8 of zones 0..9 (nibbles as above) | p0 |
| 7 | Bow | 0x22 | **unofficial** | p0 start 0–8, p1 end start+1..8, p2 strength 0–8, p3 snap 0–8 | p0..p3 |
| 8 | Galloping | 0x23 | **unofficial** | p0 start 0–8, p1 end start+1..9, p2 first foot 0–6, p3 second foot first+1..7, p4 frequency Hz | p0..p4 |
| 9 | Machine | 0x27 | **unofficial** | p0 start 0–8, p1 end start+1..9, p2 amplitude A 0–7, p3 amplitude B 0–7, p4 frequency Hz, p5 period in tenths of a second | p0..p5 |

"Unofficial": left in the controller's firmware, not in Sony's API; Nielk1:
"might be removed in the future". The app must label them so.

**Encoding** (`tension::encode`): Nielk1's TriggerEffectGenerator revision 6
(gist 6d54cc2c00d2201ccb8c2720ad7538db, newest revision f73a299f, 2022-10-07;
MIT), byte for byte. A zone mask is a little-endian u16 at bytes 1–2; zone
levels are 3-bit fields `level - 1` at bit 3·zone of a little-endian u32 at
bytes 3–6; Vibration and Multiple Position Vibration put the frequency at byte 9
(rev 6 fixed MPV writing 0 there). An effect with no force (strength 0,
amplitude 0, frequency 0, every zone 0) encodes as Off, as Nielk1 does. Slope
Feedback rounds like C#'s `Math.Round` (halves to even); the firmware does this
on exact integers. Nielk1 computes in 32-bit floats; the only exact halves
possible on its inputs come from windows of 2, 4, 6 or 8 zones, where the float
result rounds back to the exact half (checked by hand for 1/6, 5/6 and 7/6 × 3),
so both give the same bytes. The host test vectors came from a separate
line-by-line Python transliteration of the C# with 32-bit floats emulated, not
from this C++ and not from running the C#.

The firmware never sends the legacy modes 0x01/0x02/0x06/0x11/0x12 or the debug
modes 0xFC–0xFE (Nielk1: they corrupt the trigger state until reset).

### Limits

8 saved sets; 4 stages per trigger; names 16 ASCII characters; strength and
amplitude are the controller's real resolution (10 zones × 8 levels).

## 3. The stage engine

Every main-loop pass while a controller is linked (`tension_task`, after
`interrupt_loop`), per trigger:

1. **Windows.** For each stage, the pull (input report byte [4] for L2, [5] for
   R2, corrected by the user's trigger calibration exactly as the PC gets it,
   `calib_trigger()`; the raw value while that trigger isn't calibrated) is in
   its window or not. Entering
   starts that stage's own timer. Once inside, the pull may go **3 counts** past
   the window's edges before the stage counts as left (hysteresis, so a finger
   resting on an edge doesn't flip effects; tunable).
2. **Pick.** The first stage, in list order, that is inside its window and has
   been there at least `hold_min` runs. Put the longer-held stages first. No
   stage matches = Off.
3. **Ramp.** By pull: `value = p + round((ramp_to − p) × (pull − pull_min) /
   (pull_max − pull_min))`, pull clamped to the window. By hold: the same with
   `(held − hold_min) / ramp_time`, clamped at the end. Rounding: halves away from
   zero.
4. **Encode** the effect (section 2): that is what the program wants on the
   trigger now.

### The bow (the owner's example)

R2 resistance builds to full by 95 % of the pull (242 of 255), releases at full
pull, then a vibration slowly grows the longer it's held. R2's Program, Replace:

| Stage | Effect | Window | Hold | Ramp | Bytes |
|---|---|---|---|---|---|
| 0 | Vibration pos 0, amp 1, 30 Hz | 242..255 | 1.5 s | amplitude → 8 over 5 s (by hold) | `03 00 01 1E 00 00 00 00 F2 FF 0F 82 08 32` |
| 1 | Off | 242..255 | 0 | none | `00 00 00 00 00 00 00 00 F2 FF 00 00 00 00` |
| 2 | Feedback pos 0, strength 1 | 0..241 | 0 | strength → 8 across the window (by pull) | `01 00 01 00 00 00 00 00 00 F1 00 02 08 00` |

Program bytes: `03 01` + the three stages + 14 zero bytes. Feel: at pull 0
strength 1, at 120 strength 4, at 241 strength 8; at 242 the tension lets go;
after 1.5 s at full pull the vibration starts at 1 and reaches 8 five seconds
later; letting go to 238 or less goes back to the draw (strength 8 there).
Slope Feedback (0 → 9, 1 → 8) gives a similar draw done by the controller itself
in one effect; the app may offer both.

## 4. Fill in and Replace

The board judges what the game does from the host's output reports (0x02,
`SetStateData`): `valid_flag0` [0] bit 0x04 = the game sets R2's block [10..20],
bit 0x08 = L2's block [21..31].

- **The game's state per trigger** is what the controller holds from the game:
  the last block the game sent **with** the allow bit (a report without it
  changes nothing on the controller, so the game's effect stays). It is "an
  effect" unless its mode byte is 0x05 (Off) or 0x00. 0x00 is not one of the
  controller's modes; games and drivers send a zeroed block to mean nothing.
  Treating it as "no effect" is a judgement, not something we measured.
- **Replace**: ours, always.
- **Fill in**: the game wins **at once** when it sets an effect (its own report
  carries it, untouched). Ours only once the game's state has been "no effect"
  for **500 ms** (a tunable starting value). A game that never set the trigger
  at all gets ours at once. A game that clears and sets effects (a weapon
  change, rapid toggling) never hands the trigger back and forth: the hold-off
  is ~8× the ~60 ms gaps of a game toggling 16 times a second. That rate is an
  unverified claim (ApexSenseBridge), so 500 ms is a starting value.
- **Merging** (`tension::merge`, on every forwarded 0x02 and on the board's
  connect-time state, `bt_send_state`): where ours owns the trigger, our block
  goes into the report (allow bit on) when the game's report sets that trigger
  (it would overwrite ours) or when ours changed. A report that doesn't touch
  the trigger and finds ours already on the controller goes out untouched: no
  re-sending the same effect on every report (the re-fire buzz, issue #11).
- **Own reports**: when no game report comes (no game, or a game that sends
  nothing), `tension_task` sends a 0x31 with only the trigger blocks that need to
  change (and the game's current rumble), at most one every **20 ms** (tunable).
- **Giving back**: when a trigger stops being ours (the game sets an effect in
  Fill in, the set ends, another set is chosen), the game's last effect goes back
  on it (or Off if the game never set one).
- **Nothing runs**: the report goes out byte for byte as the game sent it.
- Lost reports (a full Bluetooth queue) and a new link make the board send ours
  again. The OLED Trigger Test and the app's preset preview (0xF6 0x0A) pause
  Tension while a preset other than Off is on; the Off preset (leaving the OLED
  screen, or the app's Triggers page sending preset 0 when it closes) resumes it.
- The controller-wide Trigger Effect Intensity (`trigger_reduce`, protocol 1.13)
  applies to ours too.

## 5. Which set runs

- **The saved choice** (flash, with the sets): what runs from power-on, with or
  without the app.
- **For now** (RAM only, 0x20 with temporary = 1): a game's profile in the app
  selects a set by slot when the game starts. When it ends, the app's 0xF6 0x12
  (back to the saved remap and stick curves, which it already sends) also goes
  back to the saved choice; 0x20 with slot 0xFE does only that. Starting and
  stopping games never writes flash (as temporary remaps since 1.8). A restart
  forgets it.
- **Feel it** (0x1E): runs the staged, unsaved set instead of either until it is
  stopped or the board restarts. It keeps the set's own Fill in / Replace; with
  no game running a Fill in trigger is ours at once.
- Precedence: Feel it, then the choice now (temporary or saved).
- Per-game profiles live in the app (they pick a set by slot id). The board has
  no game detection.

## 6. Flash

Two sectors, A and B, holding two copies of the store (owner 2026-11-15):
`TENSION_A_FLASH_OFFSET` = `PICO_FLASH_BANK_STORAGE_OFFSET − 5 × FLASH_SECTOR_SIZE`
and `TENSION_B_FLASH_OFFSET` = `− 6 ×` (`src/flash_layout.h`): directly below
calibration (−4), below remap (−3), slots (−2) and config (−1), all below
BTstack's link-key bank. On a 4 MB Pico 2 W with the bank at 0x3FD000: A is
**0x3F8000–0x3F8FFF**, B **0x3F7000–0x3F7FFF**. Nothing existing moved or shrank.
`RESERVED_FLASH_OFFSET` (the start of saved state, which
`tools/check_flash_reserved.py` holds every image and UF2 block below, read
from the built ELF) now starts at B, 8 KB lower than before.

Why it is safe:

- **Not the image.** The CI gate fails any build whose image or UF2 blocks
  reach B. 0.9.4's UF2 is 2,745,856 bytes (about 1.37 MB of image) on a 4 MB
  chip; B starts 36 KB below the end.
- **Not settings or pairing keys.** Their own sectors; `static_assert`s pin A
  directly below calibration and B directly below A.
- **Updates and Back to stable.** The app's updater (and a UF2 copy) writes only
  the sectors a UF2's blocks cover, so the sets survive every update. Firmware
  without Tension (0.9.4 stable, and anything before 1.23) never reads or writes
  A or B, and its image ends about 2.6 MB below them. Going back to stable and
  forward again finds the sets as they were. A factory reset (0x09) resets
  settings only and keeps the sets.
- **A power cut during a save.** Each save writes the sector that doesn't hold
  the newest copy, with the next sequence number, and the other sector is left
  alone. A save cut short leaves an erased or half-written sector that fails its
  CRC, so the previous copy is used at the next start: at most that one save is
  lost.

From protocol 1.24 the store is layout 2 (each slot also holds its button layout, 4008 bytes, 16 pages; `docs/TENSION_BUTTONS.md` section 3.6); layout 1 is still read and converted. Layout 1 (`tension::Store` in 1.23, 1128 bytes, written as 5 pages, the same in A and B):

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic 'T','E','N','S' |
| 4 | 1 | layout (1) |
| 5 | 1 | slots (8) |
| 6 | 1 | saved choice: 0..7, 0xFF none |
| 7 | 1 | body size (134) |
| 8 | 4 | sequence number (bumps on every save, wraps) |
| 12 | 4 | CRC-32 of the whole 1128-byte copy with these 4 bytes as 0 |
| 16 + 139·i | 4 | slot i: CRC-32 of its body (the app's cache key in the list) |
| +4 | 1 | used: 1, anything else empty |
| +5 | 134 | SetBody (zeros when empty) |

CRC-32 = IEEE 802.3 / zlib (reflected 0xEDB88320, init and final xor
0xFFFFFFFF; "123456789" → 0xCBF43926), the same the app computes for 0x1D / 0x1E.

Loading (`store_pick`): a sector without the magic, with other sizes, or whose
copy fails its CRC holds no copy. Of the valid copies the one with the later
sequence number wins (compared wrap-safe: `(int32)(a − b) > 0`, so 0 follows
0xFFFFFFFF). Neither valid = no sets. Within the winning copy a slot whose own
CRC or contents fail reads as empty, and a saved choice naming an empty slot is
dropped. A layout newer than 1 in **either** sector = no sets shown and nothing
**ever written** (saves and deletes answer FAILED, 0xE9 status [3] bit 3), so a
later firmware's sets survive a trip back. Every save writes the whole copy from
RAM through `flash_safe_execute` (core1 parked), from `cmd_task()` (never inside
a USB callback), and verifies it. On failure RAM goes back to the newest valid
copy in flash.

Future layouts: grow `SetBody` or `Store` by bumping the layout; this firmware
then shows no sets and protects them.

## 7. Protocol 1.23

`PICO_PROTO_MINOR` 23. **Identity [49] bit 7** (`0x80`, the last bit of the
fourth feature byte) = Tension. All SETs go to 0xF6 (both interfaces); every
SET is at most 63 bytes. Results in 0xF1 [11]: OK, PENDING (deferred: poll 0xF1
until it leaves PENDING; every SET meanwhile answers BUSY), INVALID, FAILED.
`ver` below is 1 (`tension::kWireVer`).

| Func | Bytes after the id | Runs | Result |
|---|---|---|---|
| `0x1C` stage | `ver, offset, data…` (≤ 60 bytes) | now | OK; INVALID for bad framing or past byte 134 |
| `0x1D` save staged | `ver, slot 0..7, crc32 LE (4)` | deferred (flash) | PENDING → OK / FAILED; INVALID (bad CRC, set, slot); FAILED at once on a newer store |
| `0x1E` Feel it | `ver, 1, crc32 LE (4)` start; `ver, 0` stop | now | OK; INVALID |
| `0x1F` delete | `ver, slot 0..7` | deferred (flash) | PENDING → OK / FAILED; INVALID; FAILED on a newer store. Deleting the chosen set chooses none |
| `0x20` choose | `ver, slot, temporary 0/1`: slot 0..7 (a used one), 0xFF none, 0xFE back to the saved choice (temporary ignored but must be 0/1) | temporary / 0xFE: now; saved: deferred (flash) | OK / PENDING; INVALID for an empty slot or bad framing |

`0x12` (existing: back to the saved remap and stick curves) also ends a
temporary Tension choice from 1.23 (back to the saved one); Feel it is not
touched.

The staged buffer is 134 bytes in RAM; the app writes the whole `SetBody` (three
SETs: offsets 0, 60, 120) and then 0x1D or 0x1E with its CRC-32. The board checks
CRC and every rule of section 2 before using it.

On the game-facing HID interface (any program with the hidraw node open), 0x1D
and 0x1F are refused (INVALID) in release builds, like forgetting slots: only
the app's vendor interface can overwrite or delete a set.

### Reading: GET 0xE9 (vendor interface only)

Like 0xEB, the **selector is wValue's high byte** (wValue = sel << 8 | 0xE9).
A request shorter than the reply gets nothing.

**sel 0x00, status (38 bytes)**, for the app's live view (poll ~10 Hz):

| Byte | Field |
|---|---|
| 0..1 | 'T','N' |
| 2 | ver (1) |
| 3 | flags: bit0 a set runs, bit1 Feel it on, bit2 the choice is temporary, bit3 the store was saved by newer firmware (read-only), bit4 paused (Trigger Test / preset preview) |
| 4 | saved choice (0..7, 0xFF) |
| 5 | choice now (0..7, 0xFF; Feel it runs over it when bit1) |
| 6 | slots (8) |
| 7 | stages per trigger (4) |
| 8 + 15·t | trigger t (0 L2, 1 R2): owner (0 none, 1 Tension, 2 the game, 3 Fill in waiting for the hold-off) |
| +1 | the stage running (0..3, 0xFF none) |
| +2 | pull 0..255, calibrated as the stages read it (0 without a controller) |
| +3 | tenths held in that stage's window (255 = 25.5 s or more) |
| +4..+14 | the 11 bytes the program wants on the trigger now (zeros when no set runs) |

**sel 0x01, list (38 bytes)**: [0] ver, [1] slots (8), [2] used mask (bit i =
slot i), [3] saved choice, [4] choice now, [5] flags (as status [3]), [6 + 4i]
slot i's CRC-32 LE (0 when empty). The app caches names by CRC.

**sel 0x40 | slot << 2 | chunk** (chunk 0..2): a saved set in 56-byte chunks:
[0] ver, [1] slot, [2] chunk, [3] used (1/0), then the SetBody bytes 56·chunk
onward (56, 56, 22 bytes: replies of 60, 60, 26 bytes).

## 8. What the app must add (lockstep)

`tools/check_protocol_lockstep.py` passes before the app knows 1.23 (firmware
first); the app's `protocolLockstep.test.ts` needs equal minors. The app adds:

1. `PROTO_MINOR` 23 (`bridgeClient.ts`).
2. `FUNC`: `tensionStage: 0x1c`, `tensionSave: 0x1d`, `tensionFeel: 0x1e`,
   `tensionDelete: 0x1f`, `tensionChoose: 0x20` (any names; the ids must match).
3. `FEATURES4.tension: 0x80` (identity [49]); offer Tension only when set.
4. The GET id 0xE9 with a wValue selector, as it reads 0xEB.
5. The `SetBody` encoder/decoder (section 2, byte for byte), the CRC-32, the
   effect table with Sony's ranges, and the same validation (the board refuses
   anything else with INVALID). Unofficial effects labelled.
6. The flow: stage (3 SETs) → save (0x1D, poll 0xF1) or Feel it (0x1E); stop
   Feel it when leaving the editor; choose temporary on a game's start; at its
   end the existing 0x12 goes back to the saved choice; the saved choice from
   the Tension section.
7. Per-game profiles: a set slot (or none) per game.
8. Messages for INVALID / FAILED, the read-only store (status bit 3), and the
   paused state (bit 4).
9. The loop / error / lifetime pass tables (`docs/LOOP_PASS_*.md`,
   `ERROR_PASS_*`, `LIFETIME_PASS_*` in the app repo) get the new firmware rows,
   and the backlog row is updated.

## 9. Tunable starting values (not measured)

| Value | Where | Why this number |
|---|---|---|
| Fill in hold-off 500 ms | `kFillHoldOffMs` | ~8× the gap of a game toggling ~16×/s (unverified claim); short enough that a menu's clear shows ours within half a second |
| Own report gap 20 ms | `kOwnReportGapMs` | a ramp can change the effect every pass; 50 reports/s is well under the game's own rate |
| Pull hysteresis 3 counts | `kPullHysteresis` | ~1.2 % of travel, above the raw value's jitter of a count or two (not measured) |

## 10. Runtime cost and where it runs

Core0 only. `tension_task` (main loop, in the 0xFC "input" stage): two triggers
× ≤ 4 stages and one 11-byte encode each, plus at most one 78-byte report per
20 ms. The merge runs inside the TinyUSB output callback and in `bt_send_state`:
O(1), two 11-byte compares. Not RAM-resident (`host_out02_forward`, its caller,
isn't either). No heap; RAM: the store (1280 bytes, one copy), the staged and Feel-it
bodies (268 bytes) and the engine (~120 bytes). Flash writes only from
`cmd_task()`.

## 11. Credits

- Effect encodings: Nielk1's TriggerEffectGenerator rev 6 (MIT, © 2021-2022
  John "Nielk1" Klein); the licence notice is in `src/tension.h`.
- Staging effects by pull position on the board: an idea seen in
  artzox/DS5Dongle-Studio (MIT); no code taken.
- OpenDS5, DS5_Bridge, PadForge: ideas only (named L2+R2 states, per-game
  selection), no code.

## 12. Decided by the owner (2026-11-15)

1. Two alternating sectors, so a power cut during a save keeps the previous copy
   (section 6).
2. Stages read the calibrated trigger value (section 3).
3. 0xF6 0x12 (a game ended) also ends the temporary set choice (section 5).
