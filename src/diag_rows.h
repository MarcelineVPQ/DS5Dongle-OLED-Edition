// The OLED Diagnostics screen's rows (pure, host-tested in
// tests/host/test_diag_rows.cpp; oled.cpp's format_diag_row feeds them).
//
// A row has 19 glyphs before the scroll markers at x = 120. Exact counters of
// up to 10 digits didn't fit (the last one was cut off). A row with several
// counters shows them exact while the whole row fits, else compact: exact
// while a counter fits its room, then rounded DOWN to thousands (k), millions
// (M) or billions (G), so "12k" is 12000..12999. Where only three glyphs are
// free (the Drop row) a count of a tenth of the next unit and up reads ".3M"
// (300000..399999).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace diag_rows {

constexpr size_t kGlyphs = 19;  // per row, before the scroll markers

// A counter in at most W glyphs, NUL-terminated.
template <int W>
struct Compact {
    char s[W + 1];
};

// v in decimal at p, never past end (the callers check it fits).
inline char *put_uint(char *p, const char *end, uint32_t v) {
    char tmp[10];
    int n = 0;
    // loop: NOT-WORTH — at most 10 digits
    do {
        tmp[n++] = static_cast<char>('0' + v % 10);
        v /= 10;
    } while (v != 0);
    // loop: NOT-WORTH — at most 10 digits
    while (n > 0 && p < end) *p++ = tmp[--n];
    return p;
}

template <int W>
inline Compact<W> compact(uint32_t v) {
    static_assert(W >= 3 && W <= 10, "a compact counter needs 3..10 glyphs");
    constexpr uint32_t kUnit[] = {1u, 1000u, 1000000u, 1000000000u};
    constexpr char kSuffix[] = {'\0', 'k', 'M', 'G'};
    uint64_t lim = 1;  // 10^W
    // loop: NOT-WORTH — W ≤ 10 multiplications
    for (int i = 0; i < W; i++) lim *= 10;
    Compact<W> c{};
    char *p = c.s;
    const char *const end = c.s + W;  // s[W] stays the NUL
    if (v < lim) {
        put_uint(p, end, v);
        return c;
    }
    // loop: NOT-WORTH — three units (k, M, G)
    for (int u = 1; u < 4; u++) {
        const uint32_t q = v / kUnit[u];
        if (q == 0) {  // only for W = 3: a tenth of this unit, ".3M"
            *p++ = '.';
            *p++ = static_cast<char>('0' + v / (kUnit[u] / 10));
            *p = kSuffix[u];
            return c;
        }
        if (q < lim / 10) {
            p = put_uint(p, end - 1, q);  // q has at most W - 1 digits
            *p = kSuffix[u];
            return c;
        }
    }
    return c;  // not reached: 4G fits in any W ≥ 3
}

// The row as `exact` when it fits its 19 glyphs; false when it doesn't.
inline bool exact_fits(char *line, size_t n, const char *exact) {
    const size_t len = std::strlen(exact);
    // error: NOT-WORTH — not a failure: the row is too long for exact counters, the caller shows them compact
    if (len > kGlyphs || len >= n) return false;
    std::memcpy(line, exact, len + 1);
    return true;
}

// "Up:12h 05m 09s". Hours stop at 9999999 (1141 years).
inline void uptime(char *line, size_t n, uint64_t us) {
    const uint64_t s = us / 1000000u;
    const uint64_t h = s / 3600u;
    snprintf(line, n, "Up:%luh %02lum %02lus", static_cast<unsigned long>(h > 9999999u ? 9999999u : h),
             static_cast<unsigned long>((s / 60u) % 60u), static_cast<unsigned long>(s % 60u));
}

// "host02: 4294967295" / "Mic PLC: 4294967295": the label leaves room for all 10 digits.
inline void count(char *line, size_t n, const char *label, uint32_t v) {
    snprintf(line, n, "%s%lu", label, static_cast<unsigned long>(v));
}

// "trig 9999 / tx 12k": host 0x02 reports with trigger effects / forwarded to the controller.
inline void trig_tx(char *line, size_t n, uint32_t trig, uint32_t tx) {
    char exact[32];
    snprintf(exact, sizeof(exact), "trig %lu / tx %lu", static_cast<unsigned long>(trig), static_cast<unsigned long>(tx));
    // error: NOT-WORTH — not a failure: too long for exact counters, so the compact form
    if (!exact_fits(line, n, exact)) snprintf(line, n, "trig %s / tx %s", compact<4>(trig).s, compact<4>(tx).s);
}

// "Drop r999 e12k h.3M": raw PCM (core1 behind), encoded speaker (BT behind), haptic.
inline void drops(char *line, size_t n, uint32_t raw, uint32_t enc, uint32_t hap) {
    char exact[48];
    snprintf(exact, sizeof(exact), "Drop r%lu e%lu h%lu", static_cast<unsigned long>(raw),
             static_cast<unsigned long>(enc), static_cast<unsigned long>(hap));
    // error: NOT-WORTH — not a failure: too long for exact counters, so the compact form
    if (!exact_fits(line, n, exact))
        snprintf(line, n, "Drop r%s e%s h%s", compact<3>(raw).s, compact<3>(enc).s, compact<3>(hap).s);
}

// "BT32 out: 375/s": label of up to 10 glyphs, a per-second rate.
inline void rate(char *line, size_t n, const char *label, uint32_t per_s) {
    snprintf(line, n, "%s%s/s", label, compact<6>(per_s).s);
}

// "Mic dec=480 w=96": the last opus_decode return (negative = an Opus error) and
// the bytes the USB FIFO took.
inline void mic_dec(char *line, size_t n, int32_t decoded, uint32_t wrote) {
    char dec[5];  // "-999" or 4 glyphs
    if (decoded < 0) {
        const uint32_t mag = decoded == INT32_MIN ? 2147483648u : static_cast<uint32_t>(-decoded);
        snprintf(dec, sizeof(dec), "-%s", compact<3>(mag).s);
    } else {
        snprintf(dec, sizeof(dec), "%s", compact<4>(static_cast<uint32_t>(decoded)).s);
    }
    char exact[32];
    snprintf(exact, sizeof(exact), "Mic dec=%ld w=%lu", static_cast<long>(decoded), static_cast<unsigned long>(wrote));
    // error: NOT-WORTH — not a failure: too long for exact counters, so the compact form
    if (!exact_fits(line, n, exact)) snprintf(line, n, "Mic dec=%s w=%s", dec, compact<4>(wrote).s);
}

// "c1 4194303 e12345": core1's loops in 1024s (always all 7 digits, so it keeps
// moving while core1 runs) and its speaker encodes.
inline void c1(char *line, size_t n, uint32_t loops, uint32_t enc) {
    char exact[32];
    snprintf(exact, sizeof(exact), "c1 %lu e%lu", static_cast<unsigned long>(loops >> 10), static_cast<unsigned long>(enc));
    // error: NOT-WORTH — not a failure: too long for exact counters, so the compact form
    if (!exact_fits(line, n, exact))
        snprintf(line, n, "c1 %lu e%s", static_cast<unsigned long>(loops >> 10), compact<6>(enc).s);
}

// "raw123456 hap99999": speaker PCM chunks and haptic frames pushed.
inline void pushes(char *line, size_t n, uint32_t raw, uint32_t hap) {
    char exact[32];
    snprintf(exact, sizeof(exact), "raw%lu hap%lu", static_cast<unsigned long>(raw), static_cast<unsigned long>(hap));
    // error: NOT-WORTH — not a failure: too long for exact counters, so the compact form
    if (!exact_fits(line, n, exact)) snprintf(line, n, "raw%s hap%s", compact<6>(raw).s, compact<6>(hap).s);
}

}  // namespace diag_rows
