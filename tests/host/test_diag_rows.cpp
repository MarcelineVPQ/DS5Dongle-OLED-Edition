// src/diag_rows.h: every OLED Diagnostics row fits its 19 glyphs at any
// counter value, a row that fits stays exact, and compact counters keep their
// meaning (rounded down).
#include <cstdint>
#include <cstring>
#include <string>
#include "diag_rows.h"
#include "test.h"

namespace {
template <int W>
std::string c(uint32_t v) { return diag_rows::compact<W>(v).s; }

constexpr size_t kLine = diag_rows::kGlyphs + 1;  // oled.cpp's line buffer

// The longest each row gets, with every counter at its worst.
void check_fits(const char *what, const char *line) {
    if (std::strlen(line) > diag_rows::kGlyphs) {
        std::fprintf(stderr, "  %s: \"%s\" is %zu glyphs\n", what, line, std::strlen(line));
        CHECK(std::strlen(line) <= diag_rows::kGlyphs);
    }
}
}  // namespace

TEST(diag_compact_is_exact_while_it_fits_then_rounds_down) {
    CHECK(c<4>(0) == "0");
    CHECK(c<4>(9999) == "9999");
    CHECK(c<4>(10000) == "10k");
    CHECK(c<4>(12999) == "12k");
    CHECK(c<4>(999999) == "999k");
    CHECK(c<4>(1000000) == "1M");
    CHECK(c<4>(999999999) == "999M");
    CHECK(c<4>(4294967295u) == "4G");
    CHECK(c<6>(999999) == "999999");
    CHECK(c<6>(1000000) == "1000k");
    CHECK(c<6>(4294967295u) == "4294M");
    // Three glyphs: a tenth of the next unit where a whole number wouldn't fit.
    CHECK(c<3>(999) == "999");
    CHECK(c<3>(1000) == "1k");
    CHECK(c<3>(99999) == "99k");
    CHECK(c<3>(100000) == ".1M");
    CHECK(c<3>(399999) == ".3M");
    CHECK(c<3>(999999) == ".9M");
    CHECK(c<3>(1000000) == "1M");
    CHECK(c<3>(99999999) == "99M");
    CHECK(c<3>(100000000) == ".1G");
    CHECK(c<3>(4294967295u) == "4G");
}

TEST(diag_compact_never_exceeds_its_width) {
    // Every power-of-ten boundary and its neighbours, and the extremes.
    uint64_t p = 1;
    for (int i = 0; i <= 10; i++, p *= 10) {
        for (int64_t d = -1; d <= 1; d++) {
            const int64_t v = (int64_t) p + d;
            if (v < 0 || v > 4294967295ll) continue;
            CHECK(c<3>((uint32_t) v).size() <= 3);
            CHECK(c<4>((uint32_t) v).size() <= 4);
            CHECK(c<6>((uint32_t) v).size() <= 6);
        }
    }
    CHECK(c<3>(4294967295u).size() <= 3);
}

TEST(diag_every_row_fits_19_glyphs_at_its_worst) {
    char line[kLine];
    diag_rows::uptime(line, sizeof(line), UINT64_MAX);
    check_fits("uptime", line);
    diag_rows::uptime(line, sizeof(line), 3ull * 3600 * 1000000 + 5 * 60 * 1000000 + 9 * 1000000);
    CHECK(std::string(line) == "Up:3h 05m 09s");
    diag_rows::count(line, sizeof(line), "host02: ", UINT32_MAX);
    check_fits("host02", line);
    CHECK(std::string(line) == "host02: 4294967295");
    diag_rows::count(line, sizeof(line), "Mic PLC: ", UINT32_MAX);
    check_fits("Mic PLC", line);
    CHECK(std::string(line) == "Mic PLC: 4294967295");
    diag_rows::trig_tx(line, sizeof(line), UINT32_MAX, UINT32_MAX);
    check_fits("trig/tx", line);
    diag_rows::trig_tx(line, sizeof(line), 812, 123456);
    CHECK(std::string(line) == "trig 812 / tx 123k");
    diag_rows::drops(line, sizeof(line), UINT32_MAX, 150000, 99999);
    check_fits("drops", line);
    CHECK(std::string(line) == "Drop r4G e.1M h99k");
    diag_rows::drops(line, sizeof(line), 0, 3, 17);
    CHECK(std::string(line) == "Drop r0 e3 h17");
    diag_rows::drops(line, sizeof(line), 1234, 456, 17);  // 19 glyphs: exact
    CHECK(std::string(line) == "Drop r1234 e456 h17");
    diag_rows::drops(line, sizeof(line), 123456, 4567, 17);  // 22: compact
    CHECK(std::string(line) == "Drop r.1M e4k h17");
    diag_rows::trig_tx(line, sizeof(line), 812, 51234);  // fits: exact
    CHECK(std::string(line) == "trig 812 / tx 51234");
    const char *labels[] = {"BT31 in: ", "USB aud: ", "BT32 out: ", "Mic in: "};
    for (const char *l : labels) {
        diag_rows::rate(line, sizeof(line), l, UINT32_MAX);
        check_fits(l, line);
    }
    diag_rows::rate(line, sizeof(line), "BT32 out: ", 375);
    CHECK(std::string(line) == "BT32 out: 375/s");
    diag_rows::mic_dec(line, sizeof(line), INT32_MIN, UINT16_MAX);
    check_fits("mic dec min", line);
    diag_rows::mic_dec(line, sizeof(line), INT32_MAX, UINT32_MAX);
    check_fits("mic dec max", line);
    diag_rows::mic_dec(line, sizeof(line), 480, 96);
    CHECK(std::string(line) == "Mic dec=480 w=96");
    diag_rows::mic_dec(line, sizeof(line), -4, 0);
    CHECK(std::string(line) == "Mic dec=-4 w=0");
    diag_rows::c1(line, sizeof(line), UINT32_MAX, UINT32_MAX);
    check_fits("c1", line);
    CHECK(std::string(line) == "c1 4194303 e4294M");
    diag_rows::pushes(line, sizeof(line), UINT32_MAX, UINT32_MAX);
    check_fits("pushes", line);
    diag_rows::pushes(line, sizeof(line), 123456, 99);
    CHECK(std::string(line) == "raw123456 hap99");
}
