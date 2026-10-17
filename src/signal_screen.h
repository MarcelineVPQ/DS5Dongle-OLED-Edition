// The OLED BT Signal screen's body (oled.cpp render_screen_rssi draws the
// title and the chrome; this draws the rest while a controller is linked).
//
// The value is HCI Read RSSI on a Classic link: dB from the receiver's Golden
// Receive Power Range, not dBm (src/rssi_poll.h). 0 = in the range (the
// ideal; a board near its controller reads 0 steadily), negative = weaker,
// positive = above the range (strong). The words and thresholds are the app's
// (src/desktop/format.ts signalQuality): >= -5 Excellent, >= -15 Good,
// >= -25 Fair, else Weak.
//
//   Signal: -8 dB          (y 12)    Signal: 0 dB   /  Signal: --
//   in range               (y 22, only at 0)
//   [##########......]     (y 34, -30..0 dB = empty..full, above 0 full)
//   Link: Good             (y 48)    Link: unknown without a reading
//
// Pure, no Pico SDK: the host tests and the PC renderer draw this exact code.
#ifndef DS5_BRIDGE_SIGNAL_SCREEN_H
#define DS5_BRIDGE_SIGNAL_SCREEN_H

#include <stdint.h>
#include <stdio.h>

namespace signal_screen {

constexpr int kX = 6;           // oled.cpp kContentX
constexpr int kMaxChars = 20;   // 6 + 20 * 6 - 1 = 125 < 128
constexpr int kValueY = 12;
constexpr int kRangeY = 22;
constexpr int kBarY = 34;       // outline 34..43, fill 36..41
constexpr int kBarH = 10;
constexpr int kBarW = 122;      // x 6..127, the content area's full width
constexpr int kBarInner = kBarW - 4;  // the fill, 2 px inside the outline on each side
constexpr int kLinkY = 48;
constexpr int kFloorDb = -30;   // an empty bar

constexpr char kInRange[] = "in range";
static_assert(sizeof(kInRange) - 1 <= kMaxChars, "BT Signal line too wide");

// The app's words (signalQuality), the same thresholds.
inline const char *label(int8_t db) {
    if (db >= -5) return "Excellent";
    if (db >= -15) return "Good";
    if (db >= -25) return "Fair";
    return "Weak";
}

// The bar's filled width: -30 dB (or lower) empty, 0 dB (or above) full.
inline int bar_fill(int8_t db) {
    int v = (int) db - kFloorDb;
    if (v < 0) v = 0;
    if (v > -kFloorDb) v = -kFloorDb;
    return v * kBarInner / -kFloorDb;
}

// Line 1. 24 bytes hold the longest, "Signal: -128 dB" (15 characters).
inline void value_text(bool valid, int8_t db, char out[24]) {
    if (valid) snprintf(out, 24, "Signal: %d dB", (int) db);
    else snprintf(out, 24, "Signal: --");
}

// Line 4.
inline void link_text(bool valid, int8_t db, char out[24]) {
    snprintf(out, 24, "Link: %s", valid ? label(db) : "unknown");
}

// Draws the body below the title. C: text(x, y, s), outline(x, y, w, h),
// filled(x, y, w, h) (oled.cpp's FbCanvas, or the host renderer's).
template <typename C>
void draw(C &c, bool valid, int8_t db) {
    char buf[24];
    value_text(valid, db, buf);
    c.text(kX, kValueY, buf);
    if (valid && db == 0) c.text(kX, kRangeY, kInRange);
    c.outline(kX, kBarY, kBarW, kBarH);
    const int fill = valid ? bar_fill(db) : 0;
    if (fill > 0) c.filled(kX + 2, kBarY + 2, fill, kBarH - 4);
    link_text(valid, db, buf);
    c.text(kX, kLinkY, buf);
}

}  // namespace signal_screen

#endif  // DS5_BRIDGE_SIGNAL_SCREEN_H
