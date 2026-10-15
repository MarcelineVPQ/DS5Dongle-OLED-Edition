#ifndef DS5_BRIDGE_OLED_H
#define DS5_BRIDGE_OLED_H

#include <stdint.h>

void oled_init();
void oled_loop();
void oled_send_trigger_preset(int preset); // 0..6, the Trigger Test presets

// The app's remote KEY0 / KEY1 press (0xF6 0x18, protocol 1.18): queues a
// press (key 0 KEY0, 1 KEY1; long = held past the long-press time) that
// handle_buttons() plays through the same logic as the board's keys, so it
// debounces, wakes the screen and navigates like a finger. Main loop only.
// False when four presses are already waiting.
bool oled_key_inject(uint8_t key, bool long_press);

// The app's virtual controller buttons for the OLED screens (0xF6 0x19,
// protocol 1.19): queues a press of button (oled_vpad.h: 0 up, 1 down,
// 2 left, 3 right, 4 triangle, 5 circle, 6 cross, 7 square, 8 R1) held
// hold_ms (0 = a tap, up to 5000) that only the OLED's input handling sees,
// never the PC's report. Main loop only. False for a bad button / hold or
// when four presses are already waiting.
bool oled_pad_inject(uint8_t button, uint16_t hold_ms);

// A status line on whatever screen is up (oled_toast.h, ~3 s, the bottom row
// inverted): for a save that fails outside the Settings / Slots screens.
// msg must outlive the toast (a string literal). Main loop only.
void oled_notice(const char *msg);

// Screen mirror for the app (vendor 0xEF, protocol 1.4): a 4-byte header
// (power state 0 on / 1 dimmed / 2 off, current screen, contrast, frame
// counter) and the 128x64 image exactly as last sent to the panel, row-major,
// 16 bytes per row, MSB = leftmost pixel. Returns 0 if `cap` is too small.
constexpr uint16_t kOledMirrorBytes = 4 + 128 * 64 / 8;
uint16_t oled_mirror(uint8_t *out, uint16_t cap);

#endif // DS5_BRIDGE_OLED_H
