# DualSense adaptive-trigger games — owned + tested through the dongle

Cross-referenced across Steam + Epic + GOG (~1,271 owned games). Native adaptive triggers work
on **both Steam and Heroic**, flag-free, with **runner = GE-Proton11-7-DualSense (Wine 11)**. Verified
2026-06-07 by running Cyberpunk on *both* launchers — identical trigger behaviour (on the earlier
GE-Proton10-based build); re-verified on GE-Proton11-7-DualSense with Uncharted on 2026-09-27.
The current build, GE-Proton11-7.2-DualSense, keeps the same native path (it adds the standard-gamepad
route for XInput-only games with `PROTON_PREFER_SDL=1`).

- **Steam:** GE-Proton11-7-DualSense + **Steam Input off**. No launch option needed (Wine 11 enables
  the hidraw native path by default).
- **Heroic (Epic / GOG):** GE-Proton11-7-DualSense + **Steam fully quit** + **no `PROTON_PREFER_SDL`**.
  No launch option needed. (Earlier belief that Heroic needs `PROTON_ENABLE_HIDRAW` was wrong —
  it was masked by the two gotchas below.)

### ⚠️ Linux gotchas that masquerade as "the dongle is broken" (all host-side)

- **Steam running in the background** grabs the pad from non-Steam (Heroic) games. Fully quit Steam,
  or disable Settings → Controller → PlayStation controller support.
- **A pro-audio profile rule** for the DualSense (WirePlumber/PulseAudio) breaks GE-Proton 11's haptic
  routing: games send zero-force triggers and no HD haptics. Leave the card on its default HiFi profile.
  (Found 2026-10-10 with Avatar: zero force on pro-audio, full triggers + haptics on HiFi.)
- **A global `PROTON_PREFER_SDL=1`** forces the SDL/Xbox path and *suppresses* native triggers. Keep
  it off (or per-game only). It's how you get a generic Xbox pad when a game lacks native DualSense.
- **XInput-only games can't do adaptive triggers — period.** XInput has no trigger-resistance API, so
  titles that read the pad via XInput (e.g. Ghostrunner, Control on PC) give rumble only, on any OS,
  through any tool. Not a dongle limit.

Legend: ✅ = confirmed working **flag-free** through the dongle (this session, 2026-06-07).

## 🎯 Full adaptive triggers

| Game | Where you own it | Status |
|---|---|---|
| Cyberpunk 2077 | Steam · GOG | ✅ tested flag-free — **confirmed on BOTH Steam and Heroic/GOG** |
| The Last of Us Part I | Steam | ✅ tested flag-free |
| Marvel's Spider-Man Remastered | Steam | ✅ tested flag-free |
| Uncharted: Legacy of Thieves Collection | Steam | ✅ tested flag-free |
| Ghost of Tsushima Director's Cut | Steam | recognized — bow draw 🏹 (earlier) |
| Hogwarts Legacy | Steam · Epic | ✅ tested flag-free (native recog + rumble; triggers moderate, in spellcasting) |
| Ghostrunner | Epic · GOG | ⚠️ XInput-only on PC — rumble, **no adaptive triggers** (not the dongle) |
| Avatar: Frontiers of Pandora | Steam | ✅ tested flag-free |
| Assassin's Creed Shadows | Steam | |
| Indiana Jones and the Great Circle | Steam | ✅ tested flag-free |
| Star Wars: Galactic Racer | Steam | ✅ tested flag-free — full HD haptics + adaptive triggers (UE 5.7; verified 2026-10-10) |
| Star Wars Jedi: Fallen Order | Steam | |
| LEGO Star Wars: The Skywalker Saga | Steam | |
| Metro Exodus (Enhanced) | Steam | |
| Marvel's Guardians of the Galaxy | Epic | |
| F.I.S.T.: Forged In Shadow Torch | Epic | |
| Dakar Desert Rally | Epic | racing-trigger feel |

## 〰️ Lighter / haptics-leaning (subtle trigger use)

- Baldur's Gate 3 — Steam — ✅ great experience; haptics-forward, light trigger use (dice rolls / ranged)
- Sifu — Epic — haptics only; combat is O/Triangle face-buttons, **no adaptive triggers** (not a trigger test)
- The Witcher 3: Wild Hunt (next-gen) — Steam · GOG
- Control Ultimate Edition — GOG — XInput-only on PC (rumble, no adaptive triggers)
- Forza Horizon 5 — Steam
- Hellblade: Senua's Sacrifice — Steam
- Star Wars: Squadrons — Epic
- Maneater — Epic

**Caveats:** matched the *well-known* trigger titles across a 600+ game library — there may be a
few more not flagged. ~18 games with real triggers, all owned, all working through the dongle.
