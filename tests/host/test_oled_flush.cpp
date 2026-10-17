// The OLED flush (src/oled_flush.h) must put exactly the bytes on the wire
// that the old one-byte-per-transaction flush did, D/C level included.
#include <cstring>
#include <random>
#include <vector>
#include "oled_flush.h"
#include "test.h"

namespace {
struct Wire {
    std::vector<uint16_t> bytes;  // (dc << 8) | byte, in send order
    int transactions = 0;
    void cmd(const uint8_t *p, int n) { put(0, p, n); }
    void data(const uint8_t *p, int n) { put(1, p, n); }
    void put(int dc, const uint8_t *p, int n) {
        transactions++;
        for (int i = 0; i < n; i++) bytes.push_back((uint16_t) (dc << 8 | p[i]));
    }
};

// The flush as it was before (src/oled.cpp up to v0.9.5-dev.4), verbatim
// apart from recording the bytes instead of driving the SPI pins.
uint8_t reverse_byte(uint8_t b) {
    b = ((b & 0x55) << 1) | ((b & 0xAA) >> 1);
    b = ((b & 0x33) << 2) | ((b & 0xCC) >> 2);
    b = ((b & 0x0F) << 4) | ((b & 0xF0) >> 4);
    return b;
}
void old_flush(const uint8_t *fb, Wire &w) {
    constexpr int kH = 64, kRowBytes = 16;
    auto cmd = [&](uint8_t c) { w.cmd(&c, 1); };
    auto data_byte = [&](uint8_t d) { w.data(&d, 1); };
    cmd(0xB0);
    for (int j = 0; j < kH; j++) {
        const uint8_t col = kH - 1 - j;
        cmd(0x00 + (col & 0x0F));
        cmd(0x10 + (col >> 4));
        for (int i = 0; i < kRowBytes; i++) {
            data_byte(reverse_byte(fb[j * kRowBytes + i]));
        }
    }
}

void compare(const uint8_t *fb) {
    Wire a, b;
    old_flush(fb, a);
    oled_flush::stream_frame(fb, b);
    CHECK(a.bytes == b.bytes);
    CHECK_EQ(a.transactions, 1 + 64 * 18);  // 1153
    CHECK_EQ(b.transactions, 1 + 64 * 2);   // 129
}
}  // namespace

TEST(oled_flush_bit_reverse_table) {
    for (int i = 0; i < 256; i++) CHECK_EQ(oled_flush::kBitReverse.v[i], reverse_byte((uint8_t) i));
}

TEST(oled_flush_same_bytes_as_before) {
    uint8_t fb[oled_flush::kFbBytes];
    std::memset(fb, 0, sizeof(fb));
    compare(fb);
    std::memset(fb, 0xFF, sizeof(fb));
    compare(fb);
    for (int i = 0; i < oled_flush::kFbBytes; i++) fb[i] = (uint8_t) i;
    compare(fb);
    std::mt19937 rng(1234);
    for (int n = 0; n < 200; n++) {
        for (auto &b : fb) b = (uint8_t) rng();
        compare(fb);
    }
}

// PR #21: a partial flush (previous frame given) sends, after the page
// command, exactly the changed rows, each with the same column address and
// bytes as the full frame; an unchanged frame sends nothing. So the panel
// ends up as if the full frame had been sent.
namespace {
struct RowChunks {
    bool page_first = false;
    std::vector<std::vector<uint16_t>> rows;  // 2 address bytes + 16 data bytes each
};
RowChunks split(const Wire &w) {
    RowChunks c;
    if (w.bytes.empty()) return c;
    c.page_first = w.bytes[0] == 0xB0;
    for (size_t i = 1; i + 18 <= w.bytes.size(); i += 18)
        c.rows.emplace_back(w.bytes.begin() + (long) i, w.bytes.begin() + (long) i + 18);
    CHECK_EQ((w.bytes.size() - 1) % 18, 0u);
    return c;
}
}  // namespace

TEST(oled_flush_partial_sends_changed_rows_only) {
    std::mt19937 rng(21);
    uint8_t prev[oled_flush::kFbBytes], fb[oled_flush::kFbBytes];
    for (auto &b : prev) b = (uint8_t) rng();
    std::memcpy(fb, prev, sizeof(fb));
    Wire none;
    oled_flush::stream_frame(fb, none, prev);
    CHECK_EQ(none.transactions, 0);
    for (int n = 0; n < 300; n++) {
        std::memcpy(prev, fb, sizeof(fb));
        const int changes = (int) (rng() % 12);
        for (int k = 0; k < changes; k++) fb[rng() % oled_flush::kFbBytes] = (uint8_t) rng();
        Wire full, part;
        oled_flush::stream_frame(fb, full);
        oled_flush::stream_frame(fb, part, prev);
        const RowChunks f = split(full), p = split(part);
        std::vector<std::vector<uint16_t>> want;
        for (int j = 0; j < oled_flush::kH; j++)
            if (std::memcmp(fb + j * oled_flush::kRowBytes, prev + j * oled_flush::kRowBytes,
                            oled_flush::kRowBytes) != 0)
                want.push_back(f.rows[(size_t) j]);
        CHECK(p.rows == want);
        if (!want.empty()) CHECK(p.page_first);
        CHECK_EQ(part.transactions, want.empty() ? 0 : 1 + 2 * (int) want.size());
    }
}
