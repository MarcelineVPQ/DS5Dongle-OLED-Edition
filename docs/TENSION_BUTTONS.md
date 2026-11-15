# Tension buttons: button layouts in Tension sets (protocol 1.24)

Status: build steps 1 and 2 (section 9) are built in the firmware, not yet
checked on hardware; the rest is design. Direction approved by the owner
2026-11-15 (scope: all four parts below; GUI: the research report's
recommendation with its four picks; the +1: live truth from the board).
This document is the contract for both repos: the firmware half, the protocol,
and the app's GUI. It builds on `docs/TENSION.md` (protocol 1.23), which stays
true except where section 3 below changes the store.

Conventions in this document:

- `file:line` cites the code as of `origin/master` 7df6c50 (firmware) and the
  app's `master` 2765e0e.
- **Tunable** marks a starting value that no one has measured. **Unverified**
  marks a claim about a host OS, a driver or a game that no one has tested with
  this board. **Estimate** marks a number worked out on paper.
- User-facing text uses Sony's names (`docs/GLOSSARY.md` in the app repo) and
  never says "dongle" (`{{device}}`).

## 1. What it does

A Tension set (`docs/TENSION.md` section 1) holds L2 and R2 today. From 1.24
it also holds a **button layout**: a set is the per-game unit, triggers and
buttons together, saved on the board, working without the app on every OS.

The four parts, built in steps (section 9):

1. **Button layouts in sets.** Any button can do another button's job, or
   nothing. A set can leave the buttons alone.
2. **Press types**, at most five rows per button: **Press**, **Hold** (with a
   time), **Double press**, **With PS** (a chord with the PS button), and the
   switches **Toggle** and **Turbo**.
3. **Keys and mouse sent by the board**: a button can press a keyboard key
   (with modifiers), a mouse button, or move the mouse or the wheel.
4. **Gyro as mouse** and **layers** (for example On foot / Vehicle): up to three
   named layers over the base, each button in a layer either "same as base" or
   its own.

The +1: **live truth from the board.** Press a real button and the drawing
lights the physical part, then shows what the PC actually received, through
which layer and which press type, with the hold timer filling. The board
reports what it really sent and the host really fetched (section 6.4), not what
the app expects.

## 2. What exists today (the starting point)

- **Remap table** (protocol 1.8/1.9): `src/remap.cpp`. Sixteen sources in the
  order of `enum RemapButton` (remap.cpp:19-23: L2, L1, Create, Up, Left, Down,
  Right, L3, R2, R1, Options, △, ○, ✕, □, R3); each maps to one of them or 0xFF
  (nothing). Stored as `RemapData{magic, table[16], extra[4]}` in the remap
  sector, bank −3 (remap.cpp:38-44, `src/flash_layout.h:20`). The four Edge
  extras (Fn left/right, left/right back button, input report byte 9 bits
  0x10/0x20/0x40/0x80) press a target and are cleared (remap.cpp:283-295).
- **Where it runs**: `interrupt_loop` (main.cpp:125, `CORE0_RAM_FUNC`) copies
  `interrupt_in_data` and calls `calib_apply` then `remap_apply` on the copy
  only (main.cpp:150-153 for the fixed-rate modes, 171-175 for real-time), so
  the PS+Mute reboot (main.cpp:127-141), the OLED and `0xF0` keep seeing raw
  input. `remap_apply` itself is in flash (no `CORE0_RAM_FUNC` in remap.cpp).
- **Temporary remaps**: `0x11` (table, RAM only) and `0x13` with temporary=1
  are a game's layout while it runs; `0x12` or a reboot goes back to the saved
  table (remap.cpp:174-198; firmware CLAUDE.md, protocol 1.8/1.9).
- **The app**: the Controller page's **Button assignments** section
  (`src/desktop/live/PartPanel.tsx:147-160`) lists all 16 (+4 Edge) buttons with
  a `<select>` each (PartPanel.tsx:224-320); changed parts get a `remapped`
  class on the drawing (`LiveController.tsx:227-233`, set from
  `ControllerView.tsx:132-137`). Named layouts live in the app's library
  (`librarySetLayout`, PartPanel.tsx:367), and a game's profile names one
  (`buttonLayout`, `src/desktop/library/profileDongle.ts:42`), sent as a
  temporary remap while the game runs. A profile also names a Tension set
  (`tensionSet`, profileDongle.ts:44).
- **Tension**: 8 sets on the board (`tension::kSlots`, `src/tension.h:44`), a
  134-byte `SetBody` (tension.h:89-95), store layout 1 (tension.h:548-572) in
  two alternating sectors A/B (flash_layout.h:25-26), 1128 bytes per copy.
  Tension shipped in v0.9.5-dev.21 (`git tag --contains 2457bd0`), so dev
  boards already hold layout-1 stores.
- **HID callbacks ignore the interface number**: `tud_hid_set_report_cb`
  `(void) itf` (main.cpp:535-537) and `tud_hid_get_report_cb` (main.cpp:295)
  treat every HID request as the DualSense's (or the wake keyboard's).
  `tud_hid_report_complete_cb` is empty (usb.cpp:219-222). `CFG_TUD_HID` is 1
  (tusb_config.h:99).

## 3. Data model

### 3.1 Where the layout lives

A set becomes **SetBody (unchanged, 134 bytes) + ButtonPart (360 bytes)**. The
SetBody keeps `ver` 1 and every offset of `docs/TENSION.md` section 2, so a 1.23
app reading or writing triggers keeps working on 1.24 firmware (the lockstep
rule lets the firmware know a newer minor than the app). The ButtonPart is a
separate field of the slot record, not appended to SetBody.

On the wire the staged buffer is 494 bytes: `[0..133]` SetBody, `[134..493]`
ButtonPart. Everything is little-endian and packed, and the wire bytes are the
flash bytes.

### 3.2 ButtonPart (360 bytes)

| Offset | Size | Field | Rule |
|---|---|---|---|
| 0 | 1 | `bver` | 1 when used, 0 when not |
| 1 | 1 | `bflags` | bit0 **used** (0 = this set leaves the buttons as they are: the whole part must then be zero); bit1 gyro block used; bits 2..7 zero |
| 2 | 1 | `layers` | extra layers in use, 0..3 (the base is always there) |
| 3 | 1 | `double_window` | Double press window in 10 ms, 15..50 (0.15–0.50 s); default 25 (**tunable**: 250 ms is upstream #278's window, not measured by us) |
| 4 | 2 | `pool_len` | bytes of `pool` in use, 0..308 |
| 6 | 3 × 12 | `layer[1..3]` | see Layer below; layers past `layers` all zero |
| 42 | 10 | `gyro` | see Gyro below; all zero unless bflags bit1 |
| 52 | 308 | `pool` | button entries (section 3.3); bytes past `pool_len` zero |

**Layer (12 bytes)**: `name[10]` ASCII 0x20..0x7E, NUL-padded, may be empty
(the app then shows its own localized "Layer 1/2/3"; preset names are filled
the same way, so a Japanese user's preset isn't stored in English); `colour`
0..5 (an index into six theme tokens the app defines, never a literal colour);
`lflags` bits 0-1 gyro in this layer (0 same as base, 1 on, 2 off), bits 2..7
zero.

**Gyro (10 bytes)**: `[0]` mode (1 mouse; 2 right stick, open question 5,
approved 2026-11-15; 0 is invalid when bit1 is set), `[1]` horizontal axis (0 yaw, 1 roll),
`[2]` flags (bit0 invert horizontal, bit1 invert vertical, bit2 needs a button:
gyro runs only while a Gyro target (3.4) holds or switches it on; else always
on in the layers where it is on), `[3]` horizontal speed 1..255, `[4]` vertical
speed 1..255 (multipliers, section 4.6), `[5]` steadiness
threshold 0..255 (section 4.6), `[6..9]` zero.

### 3.3 Button entries (variable length, in `pool`)

One entry per (layer, button) that differs. **Absent in the base = the button
is itself. Absent in a layer = same as base.** An entry present in a layer
replaces the base's whole button (all rows) in that layer.

| Byte | Field |
|---|---|
| 0 | bits 0..4 source (table below), bits 5..6 layer (0 base, 1..3), bit 7 zero |
| 1 | rows: bit0 Press, bit1 Hold, bit2 Double press, bit3 With PS, bit4 Toggle, bit5 Turbo, bits 6..7 zero |
| then | a 3-byte target for each of Press, Hold, Double press, With PS whose bit is set, in that order |
| then | if Hold: hold time in 10 ms, 15..200 (0.15–2.00 s; default 30 = 0.30 s, **tunable**; HID Remapper's default is 200 ms, `globals.cc:28`) |
| then | if Turbo: presses per second, 2..30 (default 10, **tunable**) |

A missing Press row means **Press = the button itself** (so "Cross: Hold → △"
is `[13, 0x02, target, 30]`, 6 bytes). Smallest entry 5 bytes (Press only),
largest 16. The pool holds 61 Press-only entries or 19 full ones; a full
keyboard-and-mouse base (all 21 sources that can take a Press) takes 105 bytes.

Entries are sorted by (layer, source), at most one per pair.

**Sources (22)**, 0..15 exactly the legacy order (remap.cpp:19-23), then:
16 Touch pad button (byte 9 bit 0x02, per SDL's SDL_hidapi_ps5.c (in SDL, not this repo) as remap.h
cites it), 17 PS button (byte 9 bit 0x01), 18 Left Fn button, 19 Right Fn
button, 20 Left back button, 21 Right back button (byte 9 bits 0x10..0x80, as
remap.cpp:283). The sticks are not sources (they are axes); the Mute button is
not a source (its light is managed by `state_config_task`, and Mute is half of
the PS+Mute reboot).

**The PS button as a source** may have Hold and Double press rows only; its
Press is always the PS button and can't be changed or emptied (a player can
always reach the system menu). With PS on the PS button is invalid.

### 3.4 Targets (3 bytes: kind, a, b)

| Kind | Meaning | a | b |
|---|---|---|---|
| 0 | Nothing ("Disable") | 0 | 0 |
| 1 | Controller button | 0..17: the 16 legacy indices, 16 Touch pad button, 17 PS button | 0 |
| 2 | Key | HID keyboard usage 0x04..0xA4, or 0 for modifiers only | modifier mask (bit0 Left Ctrl, 1 Left Shift, 2 Left Alt, 3 Left GUI, 4..7 the right ones, HID order); a=0 needs b≠0 |
| 3 | Mouse button | 1 left, 2 right, 3 middle, 4 back, 5 forward | 0 |
| 4 | Mouse motion | 0 up, 1 down, 2 left, 3 right, 4 wheel up, 5 wheel down | speed 1..255: pointer moves b × 25 counts per second (25..6375); wheel: b notches per second, 1..30 |
| 5 | Layer | 1..`layers` | 0 while held, 1 switch (press: on; press again: back to base) |
| 6 | Gyro | 0 on while held, 1 off while held, 2 switch on/off | 0; needs bflags bit1 |

Kinds 7..255 and any other value are invalid. The Edge's Fn and back buttons are
not targets (the PC only knows them in Edge mode, and nothing maps to them
there).

### 3.5 Validation (firmware and app, the same rules)

The board refuses (INVALID) a staged ButtonPart unless every rule holds; the app
runs the same checks and never sends one that fails:

1. bit0 clear → all 360 bytes zero. bit0 set → `bver` 1, reserved bits zero.
2. `layers` ≤ 3; `double_window` 15..50; `pool_len` ≤ 308; pool bytes past
   `pool_len` zero; layers past `layers` zero; names ASCII 0x20..0x7E then NULs;
   `colour` ≤ 5; `lflags` ≤ 2.
3. Entries parse exactly to `pool_len`; source < 22; entry layer ≤ `layers`;
   strictly increasing (layer, source); rows ≠ 0.
4. Targets per 3.4. Hold time 15..200; turbo rate 2..30.
5. Toggle and Turbo need a Press that is a controller button, a key or a mouse
   button (a missing Press counts: it is the button itself).
6. PS source: rows only Hold / Double press. With PS row: never on source 17.
7. A Layer target names an existing layer and never the entry's own layer. A
   button that **switches** into layer L has no entry in layer L (it keeps
   "switch back" there; otherwise a player could be trapped in L). Adapted from
   HID Remapper, which forces a layer key to stay mapped on the layer it
   enters (`remapper.cc:416-425`).
8. Gyro targets only with bflags bit1; gyro `mode` per 3.2.

Warnings that are not invalid (the app shows them, section 7.6): Cross or
Circle reachable by no button, duplicates, timing costs, keys without the
keys-and-mouse switch.

### 3.6 Store layout 2

`tension::Store` grows from layout 1 (1128 bytes) to layout 2:

| Offset | Size | Field |
|---|---|---|
| 0 | 16 | header as layout 1 (magic 'TENS', **layout 2**, slots 8, saved choice, body size 134, sequence, whole-copy CRC) |
| 16 + 499·i | 4 | slot i: CRC-32 of its SetBody **and** ButtonPart (494 bytes) |
| +4 | 1 | used: 1 |
| +5 | 134 | SetBody |
| +139 | 360 | ButtonPart |

Size 16 + 8 × 499 = **4008 bytes**, inside one 4096-byte sector (88 spare),
written as 16 pages instead of 5. Both sectors keep holding one whole copy
each, alternating, so the power-cut rule of TENSION.md section 6 is unchanged.

**Loading** (`store_pick`): a sector with layout 1 or 2, a valid CRC and the
right sizes holds a copy; the later sequence number wins across both layouts
(wrap-safe as today). A layout-1 winner is converted in RAM (every ButtonPart
zero, slot CRCs recomputed) and **nothing is written at boot**; the first save
writes layout 2 into the other sector. A layout above 2 in either sector = no
sets, nothing ever written (as today).

**Going back** to 1.23 firmware: it sees layout 2, shows no sets and never
writes (TENSION.md:266-268), so the sets survive until the newer firmware
returns. Firmware before 1.23 never reads either sector.

**RAM** (estimate): the store's page buffer grows from 1280 to 4096 bytes, the
staged and Feel it buffers from 2 × 134 to 2 × 494, the engine's compiled
layout and state about 1.7 KB (section 4.2): about **+5.2 KB** in all. Measure
with `tools/analyze.sh` at step 2.

**Flash time**: a save erases one sector and writes 16 pages instead of 5, from
`cmd_task()` with core1 parked, as today. Not measured.

### 3.7 Precedence: which buttons run

From the top, the first that has buttons wins (a set whose ButtonPart isn't used
falls through):

1. **Feel it** (the staged set, 0x1E).
2. **The set chosen now**: a temporary choice (0x20 temporary, or a set sent by
   the app for a game, 0x22) or the saved choice.
3. **The legacy temporary table** (0x11 / 0x13 temporary, protocol 1.8/1.9).
4. **The legacy saved table** (remap sector).

A set's buttons **replace** the legacy table completely (not on top of it).
Triggers keep TENSION.md section 5's precedence. A part that needs a piece this
firmware doesn't have (identity [58]; possible only in flash, saved by a later
build) keeps its bytes and falls through like a part that isn't used; the board
refuses such a part from the app (INVALID).

### 3.8 Migration from today's remap

- The remap sector, `RemapData`, `0x10`/`0x11`/`0x12`/`0x13`, `0xED` and the
  remap block in `0xF7` stay exactly as they are; older apps keep working.
- The firmware never converts the legacy table by itself. The app offers one
  click, **Make a set from these buttons** (section 7.4), which writes a set
  whose base has one Press entry per changed legacy button (and Edge extra) and
  makes it the saved choice. The legacy table stays in flash untouched, and
  shows again whenever no set with buttons runs.
- Game profiles: a profile that names a library layout keeps it (sent as today's
  temporary remap). When its Tension set has buttons, those win (3.7) and the
  game page says so (section 7.10).
- `0x12` (a game ended) keeps its meaning: back to the saved legacy table, the
  saved curves, and the saved set choice (TENSION.md section 5). It also ends a
  set sent by the app (0x22).

## 4. The firmware engine

### 4.1 Where it runs

Core0 only, inside `interrupt_loop` at the place of `remap_apply`
(main.cpp:153 and 174): `calib_apply(out); buttons_apply(out, now)`. When
nothing with buttons runs (3.7 levels 1-2 empty) `buttons_apply` calls today's
`remap_apply` unchanged, so a board without sets behaves byte for byte as
today.

Files: `src/buttons.h` (pure: entry parser, validator, compiler, the press-type
state machine, gyro math; no SDK, host-tested in `tests/host/test_buttons.cpp`
like `src/tension.h`) and `src/buttons.cpp` (glue: report bytes, time, USB
reports). The step function is `CORE0_RAM_FUNC` on RP2350 (core0's hot path is
in RAM, firmware CLAUDE.md), listed in `tools/check_ram_residency.sh`; on the
Pico W it stays in flash like the rest of core0. Its loops carry
`// loop: HOT-PATH-OK — N = 22 sources` verdicts; no heap, no flash access, no
printf.

**Nothing writes `interrupt_in_data`**: the engine reads the raw buttons from
it and writes only the outgoing copy, as `remap_apply` does. The PS+Mute
reboot, the OLED, the virtual OLED buttons and `0xF0` keep raw input.

**Real-time polling** (`polling_rate_mode` 2, `src/config.h:25`) sends a report
only when `report_dirty` is set (main.cpp:160-188). Timers (hold, double
window, turbo, tap end, mouse motion, gyro) change the output without a new
Bluetooth report, so the engine keeps `next_deadline_us` and `interrupt_loop`
sets `report_dirty` when it passes. The fixed-rate modes send every pass that
`tud_hid_ready()` allows, so they need nothing extra.

### 4.2 Compiling a layout

On every change of what runs (3.7) the board compiles the ButtonPart into RAM:
`Slot res[4][22]`, for each layer and source an index of its entry (or "itself"
for the base, "same as base" for a layer). 4 × 22 × 16 bytes ≈ 1.4 KB plus 22 ×
12 bytes of per-button state. Before switching layouts it **releases
everything** (4.8).

### 4.3 Layers

- One layer is active: the most recently **held** layer whose button is still
  down; else the **switched-on** layer; else the base. (HID Remapper: "if no
  layer is active then layer 0 is active", `remapper.cc:1162-1165`; it stacks
  layers as a mask, ours keeps one at a time, which is all the GUI shows.)
- **A press is resolved once, at its start**, against the layer active then,
  and keeps that action until release. A layer change never moves a held
  button to another action, so a key held in the base is released by the same
  finger that pressed it, never left stuck. (HID Remapper re-evaluates every
  frame; this is our change.)
- A **while held** layer ends when its button is released. A **switch** layer
  ends when its button is pressed again (rule 7 of 3.5 keeps that button free
  in the layer) or when another switch target chooses another layer.
- Layer entry by **hold**: a Hold row whose target is a Layer enters it once the
  hold time passes; the button's Press still does its own job on a tap.

### 4.4 The press-type state machine (per button)

Adapted from HID Remapper's tap-hold logic (`remapper.cc:1098-1109`: the time
of the press is kept on its rising edge; a tap is a release before the
threshold; a hold is still down at the threshold), with Double press, With PS,
Toggle and Turbo added. Times are `time_us_32()` differences (wrap-safe).

On the press's rising edge at t0 (resolved entry E in the active layer):

1. **With PS**: the PS button is down (raw) and E has a With PS row → its
   target goes on, held until this button is released. The PS button is then
   **used** for this PS press (4.5).
2. Else, E has neither Hold nor Double press → **Press fires at once** and
   follows the button (no delay, no cost).
3. Else **deciding**:
   - Still down at t0 + hold time (Hold row) → the **Hold** target goes on until
     release. Press never fires for this press.
   - Released before that, E has Double press → wait up to `double_window`
     after the release. A second press in it → the **Double press** target, held
     while that second press is held. The window ends without one → **Press**
     fires as a **tap**.
   - Released before that, no Double press → **Press** fires as a tap at once.
   - A **tap** holds the Press target for `kTapMs` = 50 ms (**tunable**: long
     enough for a game polling at 30 frames a second to see it; not measured).

**What a Hold costs the plain press**: the press no longer goes out when the
button goes down. It goes out when the button comes up (before the hold time),
and only as a short tap, so the plain press **can't be held** any more. The
delay is as long as the finger stays down, up to the hold time. A Double press
adds up to `double_window` after the release. The app says this in plain words
where it happens (7.6).

**Toggle** (on Press): each time Press fires, a latch flips; the target is on
while the latch is. With a Hold or Double press row, it flips on the Press
event (the tap). **Turbo** (on Press): while Press is on (held, or latched by
Toggle), the target pulses on for 1/(2r) and off for 1/(2r), starting on, r =
the rate. Toggle and Turbo together = a latched auto-fire.

**Several buttons, one target**: controller-button targets OR together; L2/R2
analog is the largest of the sources (as remap.cpp:277). L2 or R2 as a
source sends its analog pull to an L2/R2 target and its digital bit to anything
else. Every source whose action isn't "itself, now" is **cleared** from the
game's report, so the game sees only the target (as the Edge extras today,
remap.cpp:286-293), plus the D-pad hat rebuilt from the four directions
(remap.cpp:130-152).

### 4.5 With PS

Only when the active layer has at least one With PS row:

- The PS press is **held back**. A chord during it → PS never reaches the PC
  for that press. PS released with no chord → a PS tap (`kTapMs`). PS still
  down after `kPsPassMs` = 500 ms (**tunable**) with no chord → PS goes out as
  held from then until it is released, so a long PS press still works.
- With no With PS rows in the active layer, PS passes through untouched.
- The PS button's own Hold and Double press rows use 4.4 on top of this.
- The PS+Mute reboot reads raw input (main.cpp:130) and is never affected.

### 4.6 Gyro as mouse

Inputs: the raw gyro words at report offsets [15], [17], [19] (pitch, yaw,
roll; `src/calib.h:29-33`). Zero: the slot's calibrated rest reading when the
slot has a gyro calibration, else the factory bias from feature report 0x05
(`calib.cpp:59-62`, read per connection in `calib_service`, 168-186). Until
0x05 has arrived gyro-as-mouse stays off (the same rule as `calib_apply`'s gyro
step, calib.cpp:209). New accessor: `calib_gyro_zero(i)`.

Per input report, with dt = microseconds since the previous report (clamped to
20 ms so a gap never throws the cursor):

1. `w = raw − zero` for the horizontal axis (yaw, or roll when `[1]` = 1) and
   pitch, inverted per flags.
2. **Steadiness** (small-motion tightening, a common gyro-aim technique): with
   threshold T = `[5]` × 4 counts, if |w| < T then w = w × |w| / T (integer).
   0 = off. Default 8 (**tunable**).
3. `acc += w × dt_us × speed`, and `acc >> kGyroShift` whole counts leave the
   accumulator while the remainder stays (`kGyroShift` **tunable**, set in step 7
   so that speed 64 turns comfortably in one measured trial) (HID Remapper keeps relative motion in
   ×1000 accumulators the same way, `remapper.cc:91, 1242-1244`).
4. Clamped to int16 per report; summed with button mouse-motion targets.

**Unit**: speed is a plain multiplier on the raw counts (the raw count's °/s
scale is not ours to assume; SDL derives it from 0x05).
The app shows speed as a plain 1–255 slider with a live preview, not °/s.
**Unverified**: the controller's gyro rate and noise through this board.

On when: the base's gyro mode (3.2) and each layer's `lflags`; with flags bit2
only while a Gyro target holds or switches it on. Off at once (accumulator
cleared) when it stops.

**Right stick** (only if open question 5 is approved): the same w after step 2,
scaled to −128..127 and added to the right stick bytes [2],[3] of the outgoing
report, clamped. No USB change, so it works on Linux and with keys and mouse
off.

### 4.7 Interaction with calibration, curves and Tension

- `calib_apply` runs first (sticks, trigger lo/hi, gyro bias, stick curves), so
  an L2/R2 analog target carries the **calibrated** pull. Stick curves and
  calibration are untouched (sticks aren't sources).
- **Tension** reads the physical pull (`calib_trigger`, `tension.cpp:53`) and
  drives the physical trigger's motor. Moving L2's job elsewhere doesn't move
  its effect: the effect plays on the trigger the finger pulls. With L2 sent as
  a key, the game sees no L2 at all; Tension's Fill in then usually owns it
  (the game sends nothing for a trigger it doesn't see used; not verified).
- **A game's own trigger effects** go to the trigger the game addressed. With an
  exact L2↔R2 swap the game's R2 effect would land on the trigger that now
  does L2's job; open question 6 proposes swapping the game's two trigger
  blocks in the forwarded 0x02 report for an exact swap only.
- Trigger Effect Intensity, Fill in / Replace and Feel it are unchanged.

### 4.8 Release everything (never a stuck key)

All outputs (controller buttons, keys, mouse buttons, motion, turbo and toggle
latches, layers back to base) are released and one all-up report goes out on
each interface when: the controller disconnects; no input report has arrived
for `kStaleMs` = 500 ms (**tunable**); what runs changes (3.7); the keys and
mouse pause starts (5.5); USB suspends (nothing is sent while suspended: a
report then could trigger remote wakeup; `tud_suspended()`); the wake keyboard
comes up (`usb_wake_keyboard()`, main.cpp:144).

### 4.9 Cost (estimate, to be measured)

Per pass with a set's buttons running: 22 sources × a few compares, one
entry lookup per edge, two gyro multiplies per input report: a few microseconds
at 150 MHz (estimate). The `0xFC` "input" stage (stage 4) measures it: one A/B
reading on the owner's hardware at step 3, per the testing rule. With nothing
running: one flag test, then today's `remap_apply`.

## 5. USB: keys and mouse

### 5.1 The constraint

The DualSense interface (number 3) and its 289-byte report descriptor must stay
byte-identical to a real DualSense, or native game triggers break (firmware
CLAUDE.md, "The DualSense HID descriptor must stay byte-identical"). Keys and
mouse therefore need **another HID interface**: one USB port carries one device,
and the DualSense descriptor can't gain collections.

### 5.2 Options compared

| Option | What the PC sees | Risks | Verdict |
|---|---|---|---|
| A. Extra HID interface always present | A keyboard+mouse interface on every board, beside the DualSense, under Sony's VID/PID | Changes the configuration for every user, including those who never use keys; breaks the rule that with options off the configuration is byte-identical (usb_power rules, firmware CLAUDE.md); every risk of D for everyone | No |
| B. Extra interface only while the running set uses keys | The configuration changes whenever the set changes | Each change is a USB reconnect: the game loses the controller, Windows plays its disconnect sound, and a game's profile choosing a set at game start reconnects exactly when the game probes for a DualSense | No |
| C. Reuse the wake keyboard | Impossible beside the DualSense | The wake keyboard is a whole other device (its own VID/PID 2E8A:000A, device and configuration descriptors), presented only with no controller connected (usb_descriptors.cpp:138-168, 206-210); while it is up the DualSense isn't (main.cpp:144) | No |
| D. Opt-in interface behind a board setting (recommended) | Off (default): today's configuration byte for byte. On: one HID interface (keyboard + mouse) appended after the configurator interface | See 5.3; the "on" state is not proven safe on any OS | Yes, with a hardware probe first |
| E. The app sends the keys (uinput / SendInput / CGEvent) | Nothing new on USB | Needs the app running, against the owner's decision (2026-11-15) that the board sends them itself | Not in v1 |

Upstream (awalol #278, `05ac50a`) built the D shape: a keyboard and a consumer
interface appended after the others, included only while `enable_keyboard` (or
wake) is on, with `wTotalLength` and `bNumInterfaces` patched and a reconnect
to apply (05ac50a, usb_descriptors.cpp hunk at "Include the keyboard and
consumer interfaces"). It also grew the DualSense descriptor to 337 bytes for
its own feature reports, which ours must never do.

### 5.3 Risks of D, plainly

It **can't be called safe from the code**. What is known:

- **Linux (and Proton, which runs on it): likely broken.** HID drivers bind each
  HID interface by VID/PID; `hid-playstation` matches Sony's DualSense IDs, so
  it would also probe the keyboard interface and fail on it. That is the reason
  recorded for giving the wake keyboard its own USB ID: "Linux's hid-playstation
  binds by Sony's VID/PID and would claim (and fail on) a keyboard there"
  (usb_descriptors.cpp:141-143; CHANGELOG.md:192). It was reasoned, not
  measured. If true, the keyboard interface gets no driver on Linux (hid-generic
  doesn't take a device a special driver claims; **unverified**) while the
  DualSense interface binds as before.
- **Windows: unknown.** Windows opens keyboards and mice itself, so programs
  enumerating Sony's VID/PID see one more HID path they can't open. A game or
  library that picks the first matching path could pick it (**unverified**:
  SDL, Steam, Sony's PC ports).
- **macOS: likely a prompt.** A new keyboard usually opens the Keyboard Setup
  Assistant (**unverified** with this board).
- **Windows' per-device cache.** Windows keeps what it learned about a device
  (the MS OS descriptor answer, `usbflags`) per VID/PID/bcdDevice, and our
  bcdDevice stays 0x0100 with the switch on or off. Whether a PC that already
  knows the board picks up interface 5 after the switch is turned on is
  **unverified**; the probe checks it on a PC that has seen the board before.
  Prior art (ideas only, AGPL-3.0, no code taken): SundayMoments/DS5_Bridge
  presents a boot keyboard interface beside the DualSense under Sony's VID in
  one of its configurations and raises bcdDevice for it (0x0106) because of
  that cache; a third-party app (Kitsune Input 1.5) is reported to type keys
  through it on Windows (a field report, not checked by us). If the probe shows
  the cache keeps the old configuration, a bcdDevice change while the switch is
  on is the known fix, and it is the owner's call (it changes the device
  descriptor, so Windows sees a new device).
- **Firmware traps (fixable, must be fixed):** today's HID callbacks ignore the
  interface (main.cpp:295, 535-537). With a second instance, a request to it
  could reach the DualSense handlers: a feature GET could answer DualSense data
  (on Linux, hid-playstation could then create a **second controller**), and a
  SET could be forwarded to the controller. Rule: every `tud_hid_*_cb` returns
  at once (STALL / ignore) for instance 1, before anything else.

### 5.4 Recommendation: D, decided by a probe

1. **A board setting**, off by default: `Config_body.keys_mouse` at byte 53
   (body 54 bytes; 0 off, 1 on; 0xFF from older firmware = off). Saved by the
   save bar like every setting. It takes effect at the next USB connect; the app
   offers **Reconnect** (0xF6 0x03, `usb_power_reconnect`). OLED Settings row
   `Keys+mouse` (on/off) in `src/settings_menu.h`.
2. **The interface** (only while on): interface number `ITF_NUM_VENDOR + 1`
   (after everything, so `CONFIG_DESC_LEN_BASE`, the bInterval and HID-length
   patch offsets (usb_descriptors.cpp:532-542) and `VENDOR_ITF_NUM` don't move);
   class HID, subclass 0, protocol 0 (no boot keyboard), no string; one interrupt
   IN endpoint 0x87 (0x85/0x86 are the serial build's CDC, usb_descriptors.cpp:489;
   0x01, 0x82, 0x84, 0x03 are taken), wMaxPacketSize 16, bInterval 1. No OUT
   endpoint and no output report, so the host sends no LED reports. Off:
   `wTotalLength` and `bNumInterfaces` patched as upstream does, and a host test
   asserts the configuration descriptor is **byte-identical** to today's.
   `CFG_TUD_HID` 2 (tusb_config.h:99). Only with `ENABLE_COMPANION_VENDOR` (as
   it ships).
3. **The report descriptor** (129 bytes; pin with a `static_assert`):
   keyboard report id 1 (8 bytes: modifiers, reserved, 6 keys):
   `05 01 09 06 A1 01 85 01 05 07 19 E0 29 E7 15 00 25 01 75 01 95 08 81 02
   95 01 75 08 81 01 95 06 75 08 15 00 26 A4 00 05 07 19 00 29 A4 81 00 C0`;
   mouse report id 2 (7 bytes: 5 buttons, X, Y int16, wheel, pan int8):
   `05 01 09 02 A1 01 85 02 09 01 A1 00 05 09 19 01 29 05 15 00 25 01 95 05 75 01
   81 02 95 01 75 03 81 01 05 01 09 30 09 31 16 01 80 26 FF 7F 75 10 95 02 81 06
   09 38 15 81 25 7F 75 08 95 01 81 06 05 0C 0A 38 02 15 81 25 7F 75 08 95 01
   81 06 C0 C0`.
4. **Sending**: after the gamepad report in `interrupt_loop`, when
   `tud_hid_n_ready(1)`: the keyboard report if it changed, else the mouse
   report if motion is pending or its buttons changed (one endpoint, one report
   per 1 ms poll; motion keeps accumulating meanwhile, nothing is lost). More
   than six keys at once: the oldest six stay, the rest are counted (a 0xE8
   field), never sent wrong.
5. **Step 1 of the build is the probe** (section 9): a dev build with the
   interface behind the switch and sending nothing, checked on the owner's
   machines: Windows (a native DualSense game's adaptive triggers still fire,
   Steam sees one controller), Linux (`dmesg`: does hid-playstation fail on
   interface 5, is there one controller in `evtest`, does a key reach the
   desktop), a Proton game's triggers, macOS (the prompt). **If an OS fails, the
   app says so where keys are picked** ("Keys and mouse from the controller don't
   work on Linux.") and no fallback runs silently (open question 2).

### 5.5 Keys and mouse pause

While the app's window is in front and the Controller page is shown, a key from
the board would land in the app itself (Space or Enter activates the focused
control). With 0xF6 `0x21` (6.2) the app asks the board to **pause
keys and mouse** for N seconds and renews it every second while the condition
holds; the board releases everything (4.8) and sends no key or mouse report,
but the live-truth report still says what it would have sent. The pause ends by
itself, so a closed or crashed app never leaves keys paused. (Upstream pauses
key output while its tool learns a trigger, `22414b8`; idea only.)

## 6. Protocol 1.24

`PICO_PROTO_MINOR` 24 (`src/cmd.h:32`). One minor for the whole contract; the
pieces land in steps, each announced by a bit of a **fifth feature byte**,
identity **[58]** (answered when wLength ≥ 59; [49] is full, cmd.cpp:602, and
[50..57] is the board id, cmd.cpp:603-611). The app offers a piece only when
its bit is set. (The repo's habit is one minor per feature; if the owner wants
that, renumber 1.24..1.30 per step, nothing else changes.)

| [58] bit | Piece |
|---|---|
| 0 | Button layouts in sets: ButtonPart, store layout 2, 0x1C/0x1D/0x1E ver 2, 0xE9 sel 0x80 |
| 1 | Press types (Hold, Double press, With PS, Toggle, Turbo) |
| 2 | Layers |
| 3 | Live truth (GET 0xE8) |
| 4 | Keys and mouse: key / mouse-button targets and 0x21 (step 6). The `keys_mouse` setting alone (step 1) has no bit: it is the body's byte 53 (identity [6] ≥ 54), its state in [59] |
| 5 | Gyro as mouse (and right stick, if approved) |
| 6 | A set sent by the app for a game (0x22) |
| 7 | Reserved (switching sets on the controller, open question 7) |

Identity **[59]** (wLength ≥ 60): bit0 the keys-and-mouse interface is on the
bus now, bit1 keys and mouse are paused, bit2 `keys_mouse` saved on (so the app
can say "Reconnect to start keys and mouse"). [59] is answered from step 1, where
[58] has no bit yet: the switch itself is known from the body length (identity
[6] ≥ 54). Before a bit's piece lands, the
board accepts its bytes as invalid (INVALID) and the app never sends them.

### 6.1 Changed functions (all SETs to 0xF6, ≤ 63 bytes)

| Func | Bytes after the id | Change |
|---|---|---|
| `0x1C` stage | ver 1: as today (offset 0..133). **ver 2**: `2, offset lo, offset hi, data ≤ 59` into the 494-byte buffer | INVALID past byte 494. A whole set = 9 SETs |
| `0x1D` save | ver 1: CRC over [0..133]; saves the triggers and **keeps that slot's ButtonPart** (zero for an empty slot), so a 1.23 app never wipes buttons. **ver 2**: `2, slot, crc32 over [0..493]` saves both | deferred as today |
| `0x1E` Feel it | ver 1: as today, with no buttons. **ver 2**: `2, 1, crc32 over 494` runs triggers and buttons; `2, 0` stops | now |
| `0x20` choose | unchanged; the choice now also picks the buttons (3.7) | |
| `0x12` | unchanged meaning, plus: ends a set sent by the app (0x22) | |

### 6.2 New functions

| Func | Bytes after the id | Runs | Result |
|---|---|---|---|
| `0x21` keys and mouse pause | `1, seconds 0..10` (0 = end now) | now | OK; INVALID otherwise |
| `0x22` run a set for now | `2, 1, crc32 over 494` runs the staged buffer as the set chosen now (temporary, RAM only), shown as slot 0xFD; `2, 0` ends it (back to the saved choice) | now | OK; INVALID (CRC, rules); never written to flash |

### 6.3 Reading saved buttons: GET 0xE9 sel 0x80

`sel = 0x80 | slot << 3 | chunk`, chunk 0..6: `[0] ver 2, [1] slot, [2] chunk,
[3] used, then ButtonPart bytes 56·chunk onward` (56 bytes, the last 24): replies
of 60 bytes, 28 for chunk 6. The list (sel 0x01) slot CRCs now cover 494 bytes,
so a changed layout changes its cache key. Status (sel 0x00) adds nothing;
choice now may read 0xFD (a set sent by the app).

### 6.4 Live truth: GET 0xE8 (vendor interface only, 63 bytes)

What it promises: **every output field is what the board handed to USB and the
host then fetched.** The firmware copies the gamepad, keyboard and mouse bytes
in `tud_hid_report_complete_cb` (usb.cpp:219, empty today), which TinyUSB calls
after the host has taken the IN transfer; a report that never left is never
shown. The input fields are the same raw bytes `0xF0` serves.

| Byte | Field |
|---|---|
| 0..1 | 'B','T' |
| 2 | ver 1 |
| 3 | flags: bit0 a layout runs (a set's or the legacy table), bit1 it is a set's, bit2 Feel it, bit3 keys-and-mouse interface up, bit4 gyro mouse on now, bit5 keys and mouse paused (outputs below are then what would have gone out) |
| 4 | what runs: set slot 0..7, 0xFD the app's set, 0xFE the legacy table, 0xFF nothing |
| 5 | layer now 0..3 |
| 6..8 | u24: sources physically down (bit = source id) |
| 9..11 | u24: sources whose action is on now |
| 12 | last event's source (0xFF none yet) |
| 13 | its press type: 0 deciding, 1 Press, 2 Hold, 3 Double press, 4 With PS, 5 Toggle on, 6 Toggle off, 7 Turbo, 8 tap |
| 14 | its phase: 0 deciding (timer running), 1 on, 2 released |
| 15..16 | u16 ms it has been down (saturating) |
| 17..18 | u16 ms its Hold needs (0 none): the app fills the timer from these two |
| 19..21 | the target that went out (3.4) |
| 22 | the layer it was resolved in |
| 23..25 | gamepad bytes 7, 8, 9 the host fetched last |
| 26..27 | L2, R2 the host fetched last |
| 28 | keyboard modifiers fetched last |
| 29..34 | the six keys fetched last |
| 35 | mouse buttons fetched last |
| 36..41 | int16 X, Y, wheel moved since the previous 0xE8 (sums of fetched reports; reset by the read) |
| 42..45 | u32 count of gamepad reports fetched |
| 46 | keys dropped (more than six at once), saturating |
| 47..62 | zero (reserved) |

The last event stays until the next one, so a 50 ms tap between two polls is
never missed. Board cost: a few byte copies per fetched report (up to 1000/s)
and a 63-byte copy per GET; no flash, no printf, no heap.

### 6.5 What the app must do in lockstep

1. `PROTOCOL_MINOR_KNOWN` 24 (`src/protocol/bridgeClient.ts:116`); `FUNC`
   `buttonsPause 0x21`, `runSetForNow 0x22`; identity
   [58]/[59] as `FEATURES5`.
2. The ButtonPart codec, validator (3.5, rule for rule), target table and
   store-independent CRC, with shared test vectors generated from
   `src/buttons.h` in the host tests (as `stickCurve.ts` does with
   `stick_curve.h`). The vectors: `tests/host/fixtures/buttons/vectors.txt`
   in this repo (one line per part: name, ok/bad, the [58] bits it needs, its
   CRC-32, its 360 bytes in hex; written by `gen_vectors.py` next to it, with
   each expectation set by hand per rule), checked against `src/buttons.h` by
   `tests/host/test_buttons.cpp`.
3. Staging 494 bytes with 0x1C ver 2 (9 SETs) before 0x1D / 0x1E / 0x22 ver 2;
   reading sel 0x80 chunks; ver 1 to 1.23 firmware.
4. `simulatedDongle.ts` speaks all of it (the tests' fake).
5. The 0x21 pause while the window is in front (5.5); 0x22 at a game's start
   when its profile holds an app-side set (open question 4); 0x12 at its end.
6. `config.ts`: body 54 with `keys_mouse` at 53.
7. `tools/check_protocol_lockstep.py` gains [58]/[59] as feature tables.
8. Messages for every new INVALID / FAILED (error pass), the loop / lifetime /
   cache pass rows, the BACKLOG rows (section 8).

## 7. The GUI

The app's framework (app CLAUDE.md, "Page rules"): the drawing is the main view,
the accordion beside it holds settings and actions, one save bar saves board
settings, Sony's names, hints ≤ 60 characters or behind ⓘ, 7 locales, theme
tokens only, idle budgets. New keys are named here (English text given; the
six other locales follow the glossary).

### 7.1 Where things live: merge, not a second editor

**Button assignments stays the one place for buttons** (Sony's term, already
the section title `desktop.sections.buttons`). It edits the buttons of the set
being edited; with **Without a set** chosen it is today's list for the legacy
table. Replacing it with a second "Tension buttons" editor would put the same
buttons in two places (page rule 2).

Because a set is now triggers and buttons, the set-level controls leave the
Adaptive triggers section for a new first section:

- **Sets** (new Section `sets`, first in the panel; open question 1): Runs on the
  board (the saved choice, `desktop.tension.runs`), **Editing** (which set the
  drawing, Adaptive triggers and Button assignments show; `desktop.tension.edit`,
  plus `desktop.sets.without` "Without a set"), Name, Feel it, starters for an
  empty slot, and last Delete (`desktop.tension.*` keys move unchanged).
  Status: `desktop.sets.status` "Runs: {{name}}". Exception to page rule 10
  (drawing order): it decides what the part sections show, so it comes first.
- **Adaptive triggers** keeps Trigger Effect Intensity, calibration, the L2/R2
  programs of the edited set, its two live meters and the test.
- **Button assignments** (7.3).
- **Gyroscope** gains a group **Gyro as mouse** for the edited set (7.8).
- The Pico 2 W page's **Connection to the PC** section gains the
  **Keys and mouse from the controller** switch (7.9).

### 7.2 The main view: drawing, layer tabs, badges, live truth

**Layer tabs** above the drawing, only when a set is edited and its buttons are
used: `Base` (`desktop.layers.base`), each layer's name (empty name =
`desktop.layers.defaultName` "Layer {{n}}") on its colour token, and
`+ Layer` (`desktop.layers.add`, shown while fewer than three). Choosing a tab
is navigation (what the drawing and Button assignments show), never a setting.
A tab with changes carries a dot. Keyboard: the tabs are a `tablist` (arrows,
Home, End).

**Badges** on the drawing: only changed buttons get one, in the layer shown:
the new job as Sony's glyph (△ ○ ✕ □) or a short name (L1, Create, "F5",
"Mouse left", "Layer: Vehicle", "Off"), plus a dot when the button has Hold,
Double press, With PS, Toggle or Turbo. In a layer, a button that is "same as
base" shows the base's badge dimmed. The button that enters a layer wears that
layer's colour. Badges are a static SVG overlay drawn when the layout or the
tab changes: **0 frames per second idle**, nothing while hidden, the same on the
CPU fallback. They replace today's `remapped` class for sets.

**Picking a button**: click its part (as today, `onSelect`), or **press it on
the real controller** while Button assignments is open: a rising edge in the
`0xF0` input the drawing already polls selects it (no new poll). Keyboard: the
parts are buttons already (LiveController.tsx:145-160), and the section's
button picker (7.3) is a plain list.

**Live truth** (the +1), while Button assignments is open and the window shown
(firmware [58] bit3):

- The pressed part lights (as today, from `0xF0`).
- A one-line chip beside it, in the layer's colour: "✕ → △ Triangle", with the
  press type's icon. While deciding, a ring around the chip fills from the
  board's own `held ms / needs ms` (6.4 bytes 15-18), as a CSS animation
  started with the remaining time (no per-frame JavaScript), stepped in 10 steps
  so the CPU fallback repaints at most 10 times a second, and only during a
  hold.
- The part the PC received (a controller-button target) gets an outline pulse;
  a key or mouse target shows in the chip.
- The same sentence in the section's live line (7.3).
- After release the chip stays 1.5 s, then fades (one CSS transition).
- Paused (6.4 flag bit5): the chip says "would send F5" (`desktop.buttons.live.wouldSend`).

Cost: `0xE8` polled 30 times a second only while the section is open and the
window shown (`usePoll` pauses while hidden); an unchanged reply keeps the last
object, so nothing re-renders (the TensionLive pattern, Tension.tsx:509-523).
**0 frames per second idle**; during a hold one stepped animation (≤ 10
repaints/s); hidden: no polls, nothing drawn. A row in `docs/BUDGETS.md`.

### 7.3 Button assignments for a set, row by row

Header status: `desktop.buttons.status` "{{set}} · {{count}} changed", plus
" · {{layers}} layers" when there are any. Body, top to bottom:

1. **Button**: `desktop.buttons.pick` "Button" with a select of all sources
   (Sony names, `desktop.live.part.*`), and the hint
   `desktop.buttons.pickHint` "Click a part, or press it on the controller."
   (44 characters). With none chosen, the rest is the **Changed** list
   (`desktop.buttons.changed`): one sentence per changed button, each a button
   that selects it. Empty: `desktop.buttons.noneChanged`
   "Every button is itself in this layer."
2. **The chosen button**, as sentences (at most five rows):
   - **Press**: "Cross ✕ → [target]" (`desktop.buttons.row.press` "Press").
   - **Hold**: "Hold [0.30 s] → [target]" (`desktop.buttons.row.hold` "Hold",
     time stepper `desktop.buttons.holdTime` 0.15–2.00 s in 0.05 s).
   - **Double press** → [target] (`desktop.buttons.row.double` "Double press").
   - **With PS** → [target] (`desktop.buttons.row.withPs` "With PS").
   - **Toggle** switch · **Turbo** switch, and with Turbo
     `desktop.buttons.turboRate` "{{n}} a second" (2–30)
     (`desktop.buttons.row.toggle` "Toggle", `desktop.buttons.row.turbo` "Turbo").
   An unset row shows `desktop.buttons.notSet` "not set" as its target button
   (the row is real and acts: picking a target sets it; picking Nothing clears
   it), so nothing is greyed out (page rule 9). The PS button shows Press as
   "PS button (always)" (`desktop.buttons.psFixed`) with only Hold and Double
   press. Toggle and Turbo appear only when Press can take them (rule 5 of 3.5).
   - In a layer: a switch **Same as base** (`desktop.layers.sameAsBase`) first;
     on = the base's sentences shown dimmed, read-only; off = an editable copy.
   - Actions: **Swap with…** (`desktop.buttons.swap`) opens the button grid;
     picking B swaps A's and B's whole rows in this layer, in one draft step. A
     plain assignment never moves the other button. **Reset this button**
     (`desktop.buttons.resetButton`).
   - Warnings under the row they concern (7.6).
3. **Layer** (only on a layer tab): Name (`desktop.layers.name`, 10 characters,
   ASCII as set names are, with the set name's sub-hint `desktop.tension.nameSub`),
   Colour (six swatches from the theme,
   `desktop.layers.colour`), **Entered by** (`desktop.layers.enteredBy`: the
   button(s) whose target is this layer, each a link that selects it, or the
   warning `layerNoEntry`), Gyro in this layer (Same as base / On / Off,
   `desktop.layers.gyro`), then Reset this layer (`desktop.layers.reset`) and
   Delete layer (`desktop.layers.delete`, danger, last).
4. **Start from** (`desktop.buttons.presets`): One hand (left), One hand (right),
   Toggle instead of hold (7.7). Each is a draft change, revertable.
5. **Live from the board** (`desktop.buttons.live.title`; the live meter of page
   rule 5, Q2): `desktop.buttons.live.line`
   "{{from}} → {{layer}} · {{type}} → PC got {{to}}", e.g.
   "Cross ✕ → Base · Hold 0.4 s → PC got Triangle △"; before any press
   `desktop.buttons.live.idle` "Press a button on the controller.". Only with
   [58] bit3.
6. **Reset all buttons in this set** (`desktop.live.remapReset`, existing),
   danger zone, last.

Saving: the set draft (`tensionDraft.ts`) holds the ButtonPart too; the one
save bar writes it (0x1C ver 2 × 9, 0x1D ver 2). Feel it (Sets section) runs
triggers and buttons unsaved. No section has its own save (page rule 4).

### 7.4 Without a set (the legacy table)

Today's list, unchanged, plus one hint and one action:
`desktop.buttons.legacyHint` "Hold, double press and layers need a set."
(41) and **Make a set from these buttons** (`desktop.buttons.makeSet`): picks
the first empty slot (or asks which slot to replace when all 8 are used),
copies every changed button as a Press entry, names it "Buttons", makes it the
saved choice, as a draft the save bar saves. The legacy table is never changed
by it.

### 7.5 The target picker

One popover per row (top layer, Escape returns to the row, the `MenuButton`
keyboard rules), with four tabs: **Controller** · **Key** · **Mouse** ·
**Layer** (and **Gyro** when gyro is on; `desktop.buttons.target.*`).

- **Controller**: Sony's glyphs and names laid out like the controller (L2 L1 /
  R1 R2 on top, D-pad left, △ ○ ✕ □ right, Create, Options, PS, Touch pad, L3,
  R3), an arrow-key grid (roving tabindex), and **Disable**
  (`remap.disabled`, existing). The button itself is marked "(default)"
  (`desktop.live.remapDefault`).
- **Key**: a field **Press a key** (`desktop.buttons.target.pressKey`): focused
  and armed, the next key down is taken (with the modifiers held), Escape
  cancels; **Show all** (`desktop.buttons.target.showAll`) lists every key by
  group (letters, digits, F1–F24, arrows, editing, keypad) for keys the field
  can't catch (Tab, Escape, Print Screen); modifier switches Ctrl, Shift, Alt,
  Win/⌘ (`desktop.buttons.mod.*`). Key names come from the browser's layout map
  where it has one (`navigator.keyboard.getLayoutMap`, **unverified** in
  Electron on all three OSes), else the US name.
- **Mouse**: Left, Right, Middle, Back, Forward; Move up/down/left/right and
  Wheel up/down, each with a speed slider (`desktop.buttons.mouse.*`).
- **Layer**: each layer, with **While held** / **Switch**
  (`desktop.buttons.layerMode.held`, `.switch`).

With the keys-and-mouse switch off, the Key and Mouse tabs still work and show
the warning `needsKeys` with its one-click fix (7.6, page rule 7).

### 7.6 Warnings: exact words

All inline, under the row they concern, in plain words; ≤ 60 characters, the
detail behind ⓘ. Keys `desktop.buttons.warn.*`:

| Key | Text (en) | When | ⓘ |
|---|---|---|---|
| `holdCost` | "Hold on {{button}}: its press waits until you let go." | a Hold row | "A short press now goes out when you let go, as a quick tap, so it can't be held. Hold it {{time}} for the Hold action." |
| `doubleCost` | "Double press on {{button}} delays its press by {{time}}." | a Double press row | "The board waits {{time}} after you let go to see whether a second press comes." |
| `psCost` | "With PS holds back the PS button until you let go." | any With PS row in the layer | "The PS button goes out when you let go, or after half a second. Steam also uses PS with other buttons." |
| `noCross` / `noCircle` | "Nothing presses ✕ now: menus may be hard to use." / "Nothing presses ○ now: you may get stuck in menus." | no source's Press or Hold reaches it in the base | |
| `duplicate` | "{{other}} already does this." | two sources' Press with the same target | |
| `needsKeys` | "Keys and mouse are off on {{device}}." + **Turn on** | a key or mouse target, setting off | Turn on drafts the setting; after Save, `reconnect` |
| `reconnect` | "Reconnect to start keys and mouse." + **Reconnect** | saved on, interface not up ([59]) | |
| `noLinux` | "Keys and mouse from the controller don't work on Linux." | only if the probe fails there (5.4) | the reason, plainly |
| `paused` | "Keys and mouse pause while this window is in front." | live truth, flag bit5 | "So a key can't press buttons in this app. Switch to the game to use them." |
| `prompts` | (ⓘ only, on the section) | | "Games still show their own button icons: they don't know about this layout." |
| `layerNoEntry` | "No button enters this layer yet." | layer tab | "Choose a button and set Layer as one of its targets." |
| `full` | "This set has no room for more changes." | pool full | "A set holds about 60 simple changes, fewer with Hold and Double press." |
| `sixKeys` | "Only six keys can be held at once." | a layout that can hold 7+ keys | |
| `triggerEffect` | (ⓘ on L2/R2 rows) | L2 or R2 moved | "Trigger effects stay on the trigger you pull." |

### 7.7 Accessibility built in

- **An editor that needs single presses only**: every action is one click or
  Enter; nothing needs a drag, a chord, a long press or a timed action;
  pressing on the controller is optional.
- **Toggle instead of hold** preset: adds Toggle to L2, L1 and L3 (aim, block,
  sprint are commonly holds; a starting point to edit).
- **One hand (left)** preset: base: Up → △, Right → ○, Down → ✕, Left → □;
  L1 **Hold** (0.30 s) → layer 1 "Right side" while held, L1 Press → R1; layer 1:
  Up/Down/Left/Right are themselves, L2 → R2, L3 → R3, Create → Options.
  **One hand (right)**: the mirror (△ ○ ✕ □ → D-pad in a layer held by R1).
  The right stick can't move to the left hand in v1 (sticks aren't sources);
  gyro as right stick (open question 5) is the way to add it. Both presets are
  starting points to be tried by one-handed players, not tested designs.
- Undo: the save bar's Revert drops every unsaved change; per button, layer and
  set resets.

### 7.8 Gyro as mouse (Gyroscope section, for the edited set)

Group `desktop.gyroMouse.title` "Gyro as mouse": **Gyro** Off / On / With a button
(`desktop.gyroMouse.mode.*`; "With a button" = flags bit2, and the hint
`desktop.gyroMouse.buttonHint` "Give a button the Gyro target to use it."),
**Turn with** Yaw / Roll (`desktop.gyroMouse.axis.*`), **Speed** horizontal and
vertical (1–255), **Invert** horizontal / vertical, **Steadiness** (0–255),
and the warning `needsKeys` when keys and mouse are off. Only with [58] bit5;
static controls, 0 frames idle.

### 7.9 Keys and mouse from the controller (Pico 2 W page)

In **Connection to the PC** (`desktop.sections.usb`): a switch
`desktop.settings.keysMouse` "Keys and mouse from the controller", sub-hint
`desktop.settings.keysMouseSub` "Starts the next time {{device}} connects."
(≤ 60 with the board's name; ⓘ: what the PC will see, the per-OS result of the
probe). Saved by the save bar; after a save with a change, the row shows
`reconnect` with its button. Status in the section line.

### 7.10 Per game (a game page)

The game page's existing Tension row (`desktop.tension.gameLabel`) becomes
**Set** (`desktop.sets.gameLabel`) and picks a set by slot as today. Its
status says what the set holds: `desktop.sets.holds` "Triggers and buttons" /
"Triggers" / "Buttons". The game's **Button layout** row shows only when the
chosen set has no buttons; otherwise `desktop.sets.buttonsFromSet`
"Buttons come from the set {{name}}." "Edit on the Controller page" as
everywhere. With [58] bit6 and open question 4 approved, a game can also hold
its own set in the app's library, sent with 0x22 when it starts.

### 7.11 States

- **Older firmware** (no [58] bit0): Button assignments is today's editor plus
  `desktop.buttons.needsFirmware` "Hold, double press and layers need an update."
  (MUST-SHOW hint) and the Firmware link (the Tension pattern, Tension.tsx:66-77).
  A piece whose bit is missing is simply absent (no greyed-out rows).
- **First run** (a set whose buttons aren't used): `desktop.buttons.setLeaves`
  "This set leaves the buttons as they are." and **Change buttons in this set**
  (`desktop.buttons.useInSet`, starts from the legacy table's changes).
- **No controller**: editing works; live truth shows `desktop.buttons.live.noController`
  "Connect the controller to see it live."
- **Newer store** (read-only): the Sets section's existing `desktop.tension.readOnly`.
- **Narrow window** (900×650): the panel is the drawer (as today); the layer tabs
  become one select `desktop.layers.select` "Layer" when they don't fit; badges
  show glyphs only (names in their tooltips); the target popover fills the
  drawer's width.
- **Keyboard only**: Tab reaches the tabs, the parts, the section; every
  popover has a focus trap and Escape; the key field never captures Tab unless
  armed, and Show all covers it.

### 7.12 Glossary additions

Our own names (no Sony term), added to `docs/GLOSSARY.md` "Our settings with no
Sony name" in all 7 languages: **Set**, **Layer**, **Base**, **Press**,
**Hold**, **Double press**, **With PS**, **Toggle**, **Turbo**, **Keys and mouse
from the controller**, **Gyro as mouse**, **Same as base**, **Swap with**.
**Button assignments** stays Sony's.

## 8. Credits and licences

- **HID Remapper** (MIT, © 2023 Jacek Fedorynski, github.com/jfedor2/hid-remapper,
  read at 51ab8b3): the tap/hold timing (`remapper.cc:1098-1109`), momentary vs
  sticky layers with the layer key kept on its own layer (`remapper.cc:416-425,
  1131-1165`), and fixed-point accumulators for relative motion
  (`remapper.cc:91, 1242-1244`) are adapted as ideas and written fresh. If any
  code is copied, its MIT notice goes into `src/buttons.h`. Credited in About →
  Credits either way.
- **Upstream awalol #278** (MIT): the opt-in keyboard interface with a reconnect,
  chord preference, the learn pause; ideas, adapted.
- reWASD, Steam Input, DualSense Edge, Xbox Accessories, Input Remapper,
  OpenDS5: ideas only (the research report's sources).
- Backlog (app repo, the one list): the rows "Controller buttons as keyboard
  keys" and "Tension" move to In progress with a pointer to this document in the
  first commit that builds step 2.

## 9. Build steps

Each step ships something whole in a dev build, both repos where it applies, and
ends with the one check that proves it (host tests and `vitest related` run by
the agent within its budget; the hardware check by the owner, who holds the test
key).

| # | Ships | Firmware | App | Check that proves it |
|---|---|---|---|---|
| 1 | **USB probe** (dev build only, not a feature) | `keys_mouse` setting, the interface behind it sending nothing, instance checks in every HID callback | the switch on the Pico 2 W page | Host test: switch off = configuration descriptor byte-identical to today. Owner: the probe list of 5.4 on Windows, Linux, Proton, macOS; the result decides open question 2 |
| 2 | **Button layouts in sets, Press only** | ButtonPart, store layout 2 with migration, validation, 0x1C/0x1D/0x1E ver 2, 0xE9 sel 0x80, precedence 3.7, Press with controller-button targets and Disable ([58] bit0) | Sets section, Button assignments set editor (Press, Swap with…, resets), badges, pick by press, Without a set + Make a set | Host tests: layout-1 store loads and saves as layout 2, every rule of 3.5, Press-only output equals `remap_apply` for the same table. App: codec vectors from the host test. Owner: one set with ✕↔○ swapped, seen in a game |
| 3 | **Live truth** | 0xE8 from the complete callback ([58] bit3) | chip, outline, live line | Host test: 0xE8 fields after a scripted press. Owner: press ✕ and see "PC got ○" |
| 4 | **Press types** | 4.4, 4.5 ([58] bit1); `0xFC` stage 4 reading before/after | rows, timing warnings, hold ring | Host tests: the state machine on scripted timelines (tap, hold at the edge, double inside/outside the window, chord, PS pass-through, toggle, turbo phase, release-everything). Owner: Hold on ✕ in the live view |
| 5 | **Layers** | 4.3 ([58] bit2) | tabs, inheritance, layer rows, presets | Host tests: resolve-once rule, switch-back, no stuck output across a layer change. Owner: one layer held by L1 |
| 6 | **Keys and mouse buttons** (only on the OSes the probe passed) | key and mouse-button targets, 0x21 pause ([58] bit4) | Key and Mouse tabs, warnings, pause while in front | Host tests: keyboard report bytes, six-key limit, release-everything. Owner: R1 → F5 in a game on Windows |
| 7 | **Mouse motion and gyro as mouse** (and right stick if approved) | 4.6 ([58] bit5) | Gyroscope group, motion targets | Host tests: integer gyro math against vectors (accumulator remainder, steadiness, dt clamp). Owner: aim with gyro in one game |
| 8 | **Sets for a game from the app** (if approved) | 0x22 ([58] bit6) | game page Set row, app-side sets | Host tests: 0x22 never writes flash, 0x12 ends it. Owner: start and stop one game |

Each step updates CHANGELOG, the READMEs (7 languages) where users see it, the
loop / error / lifetime pass rows, and its BACKLOG row, in its own commit.

## 10. Open questions for the owner (recommendation first)

1. **A Sets section first in the panel** (Runs, Editing, Name, Feel it, Delete
   move out of Adaptive triggers). *Recommend yes*: a set now spans two
   sections, so its controls belong above both.
2. **Keys and mouse where the probe fails** (Linux is the likely one). *Recommend*:
   say plainly in the app that they don't work there, no app-side key sending in
   v1 (it would need the app running); revisit only if many Linux players ask.
3. **The legacy table and library layouts**: *recommend keeping both* for older
   apps and older game profiles, with Make a set from these buttons; retire them
   in a later release once sets carry everyone's layouts.
4. **8 sets on the board vs one per game**: *recommend* the board's 8 for what
   works without the app, plus unlimited sets in the app's library for game
   profiles, sent for now with 0x22 when the game starts (the board has no game
   detection, so per-game always needs the app).
5. **Gyro as right stick too**: *recommend yes* (step 7): it needs no USB change,
   so it works on Linux and with keys and mouse off, and it gives one-hand
   presets a right stick.
6. **An exact L2↔R2 swap moves the game's trigger effects with it**: *recommend
   yes*, only for the exact swap (the effect follows the job, as the DualSense
   Edge's own L2/R2 swap suggests); any other move leaves effects on the
   physical trigger.
7. **Switching sets on the controller**: *recommend not in v1*: switch from the
   app (and later the OLED). The Edge's Fn + face button is Sony's own profile
   switch inside the controller, and what it does through this board is
   unverified; every DualSense combo collides with something (PS with Steam,
   Mute with the mic light and the PS+Mute reboot). Identity [58] bit7 is kept
   for it.
