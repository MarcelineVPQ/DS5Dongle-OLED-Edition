//
// Button remapping — a 16-entry table persisted in its own flash sector,
// applied to the OUTGOING host HID report only (never the raw interrupt_in_data
// the OLED / reboot-combo logic reads). Edited from the web config tool over the
// already-declared 0xF6/0xF7 vendor reports. Apply logic + button set ported
// from SundayMoments/DS5_Bridge (credit).
//

#ifndef DS5_BRIDGE_REMAP_H
#define DS5_BRIDGE_REMAP_H

#include <cstdint>

// Number of remappable buttons (see RemapButton in remap.cpp). The table maps
// source index -> target index; 0xFF means "disabled" (source does nothing).
constexpr int kRemapCount = 16;

// Wire-protocol version for the remap get/set framing carried over the existing
// 0xF6/0xF7 vendor reports (see cmd.cpp). Bump only on incompatible layout
// changes so the web tool can refuse a mismatched firmware.
constexpr uint8_t kRemapProtoVer = 1;

// proto 1.9: the DualSense Edge's extra buttons as remap sources, in input
// report byte 9 (without the report id): Fn left 0x10, Fn right 0x20, left
// paddle 0x40, right paddle 0x80 (SDL_hidapi_ps5.c, daidr/dualsense-tester).
// Each entry: a target 0..15 (a standard button), kRemapExtraOff (nothing),
// or kRemapExtraSelf (the button itself, the default; erased flash reads so).
constexpr int kRemapExtraCount = 4;
constexpr uint8_t kRemapExtraSelf = 0xFF, kRemapExtraOff = 0xFE;
// Ok / Invalid (a bad entry, nothing changed) / NotSaved (applied, but the
// flash write failed, so it's lost at power-off).
enum class RemapSet : uint8_t { Ok, Invalid, NotSaved };
// Set the four entries, saved (temporary = false) or for now only.
RemapSet remap_set_extra(const uint8_t *extra, bool temporary);
void remap_get_extra(uint8_t out[kRemapExtraCount]);

// Load the table from its dedicated flash sector. Call once at boot, after
// config_load(). A fresh/invalid sector defaults to identity (no remap).
void remap_load();

// Remap a 63-byte DS5 input-report COPY in place (buttons live in report[4,5,7,8]).
// No-op fast path when the table is identity. Must only ever touch the outgoing
// host report, not the raw interrupt_in_data.
void remap_apply(uint8_t *report);

// Validate + store + persist a new 16-entry table (each entry < 16, or 0xFF =
// disabled). Bumps the revision whenever the table is applied.
// Applies a table (in RAM at once) and saves it. NotSaved: applied, but the
// flash write failed, so it's lost at power-off.
RemapSet remap_set(const uint8_t *table);

// proto 1.8: a table for now only (a game's layout, set by the app while the
// game runs): applied in RAM, never written to flash, so a reboot or
// remap_revert() brings the saved table back. Same validation as remap_set.
bool remap_set_temporary(const uint8_t *table);
// Back to the table saved in flash (ends a temporary one).
void remap_revert();
// A temporary table is active (identity [19] bit 1).
bool remap_temporary();
// The table in use changes something (not the identity): GET 0xE8 [3] bit0.
bool remap_active();

// Copy the current 16-entry table out.
void remap_get(uint8_t out[kRemapCount]);

// Monotonic counter bumped on each successful remap_set — the web polls it to
// confirm a write landed. Runtime only (not persisted).
uint16_t remap_revision();

#endif // DS5_BRIDGE_REMAP_H
