// A 128x64 1-bit canvas for the host tests and the PC renderer: the same
// primitives as oled.cpp's FbCanvas / draw_text, plus a count of pixels drawn
// off the screen (a fit check).
#pragma once
#include <cstdint>
#include <cstring>
#include "oled_font.h"

struct BitCanvas {
    static constexpr int kW = 128, kH = 64;
    bool p[kH][kW] = {};
    int off_screen = 0;

    void clear() { std::memset(p, 0, sizeof(p)); off_screen = 0; }
    void px(int x, int y, bool on = true) {
        if (x < 0 || x >= kW || y < 0 || y >= kH) { off_screen++; return; }
        p[y][x] = on;
    }
    void text(int x, int y, const char *s) {
        // loop: NOT-WORTH — host test canvas
        for (; *s; s++, x += 6) {
            if (*s < 0x20 || *s > 0x7E) continue;
            const uint8_t *g = kFont5x7[*s - 0x20];
            // loop: NOT-WORTH — host test canvas
            for (int c = 0; c < kFontW; c++)
                // loop: NOT-WORTH — host test canvas
                for (int r = 0; r < kFontH; r++)
                    if (g[c] & (1 << r)) px(x + c, y + r);
        }
    }
    void outline(int x, int y, int w, int h) {
        // loop: NOT-WORTH — host test canvas
        for (int i = 0; i < w; i++) { px(x + i, y); px(x + i, y + h - 1); }
        // loop: NOT-WORTH — host test canvas
        for (int i = 0; i < h; i++) { px(x, y + i); px(x + w - 1, y + i); }
    }
    void filled(int x, int y, int w, int h) {
        // loop: NOT-WORTH — host test canvas
        for (int j = 0; j < h; j++)
            // loop: NOT-WORTH — host test canvas
            for (int i = 0; i < w; i++) px(x + i, y + j);
    }
    void clear_rect(int x, int y, int w, int h) {
        // loop: NOT-WORTH — host test canvas
        for (int j = 0; j < h; j++)
            // loop: NOT-WORTH — host test canvas
            for (int i = 0; i < w; i++) px(x + i, y + j, false);
    }
    void invert(int x, int y, int w, int h) {
        // loop: NOT-WORTH — host test canvas
        for (int j = 0; j < h; j++)
            // loop: NOT-WORTH — host test canvas
            for (int i = 0; i < w; i++) {
                if (x + i < 0 || x + i >= kW || y + j < 0 || y + j >= kH) { off_screen++; continue; }
                p[y + j][x + i] = !p[y + j][x + i];
            }
    }
    // Lit-pixel bounds: false when nothing is lit.
    bool bounds(int &x0, int &y0, int &x1, int &y1) const {
        x0 = kW; y0 = kH; x1 = -1; y1 = -1;
        // loop: NOT-WORTH — host test canvas
        for (int y = 0; y < kH; y++)
            // loop: NOT-WORTH — host test canvas
            for (int x = 0; x < kW; x++)
                if (p[y][x]) {
                    if (x < x0) x0 = x;
                    if (x > x1) x1 = x;
                    if (y < y0) y0 = y;
                    if (y > y1) y1 = y;
                }
        return x1 >= 0;
    }
    bool same(const BitCanvas &o) const { return std::memcmp(p, o.p, sizeof(p)) == 0; }
};
