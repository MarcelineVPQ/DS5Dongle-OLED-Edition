//
// The displays this image can drive (DESIGN_FIRMWARE_IDENTITY §7): one image,
// a driver table and a saved choice (Config::display_id, protocol 1.21).
// Pure, host-tested (tests/host/test_fw_identity.cpp).
//

#ifndef DS5_BRIDGE_DISPLAY_TABLE_H
#define DS5_BRIDGE_DISPLAY_TABLE_H

#include <cstdint>

namespace display {

// Wire ids (0xEA [11..13], 0xF6 0x1B). The app's and the feed's names are in
// kNames (tools/manifest_lib.mjs, the app's src/update/boardIdentity.ts).
enum : uint8_t {
    kNone = 0,         // nothing driven: no SPI, the OLED pins and keys left alone
    kPicoOled13 = 1,   // Waveshare Pico-OLED-1.3 (SH1107, SPI1, KEY0 / KEY1)
};
// Bit n = display id n is in this image (0xEA [12]).
constexpr uint8_t kSupported = (1u << kNone) | (1u << kPicoOled13);
// The UF2's binary info (bi_decl, main.cpp) lists the same set by name.
#define DS5_DISPLAYS_BI "none,pico_oled_1_3"

constexpr bool is_supported(uint8_t id) { return id < 8 && (kSupported & (1u << id)) != 0; }

// The saved choice as the firmware uses it. 0xFF (a config page saved by
// firmware before 1.21) and an id this image has no driver for take the
// Pico-OLED-1.3, which every earlier image drove (owner's decision, §9.5).
constexpr uint8_t resolve(uint8_t saved) { return is_supported(saved) ? saved : static_cast<uint8_t>(kPicoOled13); }

// Whether the OLED code talks to a panel at all.
constexpr bool has_panel(uint8_t id) { return id == kPicoOled13; }

// A board key is down only when a panel with keys is driven and its pin reads
// low (pull-up). With "none" the pins are never set up, and a pin left at its
// reset state can read low (not checked on hardware), which would look like
// both keys held: the KEY0 + KEY1 reboot chord.
constexpr bool is_key_down(uint8_t active, bool pin_high) { return has_panel(active) && !pin_high; }

}  // namespace display

#endif  // DS5_BRIDGE_DISPLAY_TABLE_H
