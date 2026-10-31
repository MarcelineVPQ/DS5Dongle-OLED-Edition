#include "perf.h"

#include <string.h>
#include "pico/time.h"

PerfStat g_perf[PERF_NUM_STAGES];
volatile uint32_t g_perf_gen = 0;
volatile bool g_perf_timer_fallback = false;
static uint64_t s_window_start_us = 0;
// The sole reader is core0. Keep its last complete sample so exhausting the
// retry budget never publishes a writer's unfinished update.
struct PerfSnapshot { uint32_t sum, count, max, gen; };
static PerfSnapshot s_snapshot[PERF_NUM_STAGES]{};

void perf_reset() {
    s_window_start_us = time_us_64();
    g_perf_gen = g_perf_gen + 1;
}

size_t perf_serialize(uint8_t *out, size_t cap) {
    if (cap < kPerfReportBytes) return 0;
    const uint64_t secs = (time_us_64() - s_window_start_us) / 1000000u;
    const uint16_t window = secs > 0xFFFF ? 0xFFFF : (uint16_t) secs;
    out[0] = 1 | (g_perf_timer_fallback ? 0x80 : 0);  // layout version, bit 7 timer fallback
    out[1] = PERF_NUM_STAGES;
    memcpy(out + 2, &window, 2);
    const uint32_t gen = g_perf_gen;
    // loop: HOT-PATH-OK — N = PERF_NUM_STAGES = 8 rows, per 0xFC telemetry GET
    for (int i = 0; i < PERF_NUM_STAGES; i++) {
        const PerfStat &s = g_perf[i];
        uint32_t sum = 0, count = 0, max = 0, sgen = 0;
        bool coherent = false;
        // The writer may be another core: retry while it is mid-update or a
        // sample landed mid-read (an update is a few stores, so this ends
        // fast; bounded so a writer stopped mid-update can't hang core0).
        // loop: SERIAL-REQUIRED — seqlock retry against the other core's writer, bounded to 1000 tries
        for (int tries = 0; tries < 1000; tries++) {
            const uint32_t seq = s.seq;
            __dmb();
            count = s.count;
            sum = s.sum;
            max = s.max;
            sgen = s.gen;
            __dmb();
            if (!(seq & 1) && s.seq == seq) {
                coherent = true;
                break;
            }
        }
        if (coherent) s_snapshot[i] = {sum, count, max, sgen};
        const PerfSnapshot &last = s_snapshot[i];
        sum = last.sum;
        count = last.count;
        max = last.max;
        sgen = last.gen;
        if (sgen != gen) sum = count = max = 0;  // nothing since the reset
        const uint32_t avg_ns = count ? (uint32_t) ((uint64_t) sum * 1000u / PERF_TICKS_PER_US / count) : 0;
        const uint32_t max_us_full = (max + PERF_TICKS_PER_US - 1) / PERF_TICKS_PER_US;
        const uint16_t max_us = max_us_full > 0xFFFF ? 0xFFFF : (uint16_t) max_us_full;
        memcpy(out + 4 + i * 6, &avg_ns, 4);
        memcpy(out + 4 + i * 6 + 4, &max_us, 2);
    }
    return kPerfReportBytes;
}
