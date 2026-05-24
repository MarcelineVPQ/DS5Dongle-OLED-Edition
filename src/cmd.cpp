//
// Created by awalol on 2026/5/4.
//

#include "cmd.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "audio.h"
#include "bt.h"
#include "config.h"
#include "device/usbd.h"
#include "pico/time.h"
#include "slots.h"
#include "remap.h"
#include "hardware/clocks.h"
#include "hardware/adc.h"
#include "hardware/vreg.h"

uint16_t cpu_temp_raw_smoothed() {
    // One-time ADC bring-up. This is the only place the ADC is initialised
    // now (oled.cpp's CPU screen calls through here too). Runs on core0
    // under the cooperative main loop; adc_select_input(4) is set before
    // every read, so the shared ADC needs no locking.
    static bool adc_ready = false;
    if (!adc_ready) {
        adc_init();
        adc_set_temp_sensor_enabled(true);
        adc_ready = true;
    }
    adc_select_input(4);

    // The temp sensor has a shallow slope (-1.721 mV/C) and ~1 LSB ≈ 0.47 C,
    // so a lone 12-bit sample swings several tenths of a degree frame to
    // frame. Average a big block to kill that...
    constexpr int kSamples = 256;
    uint32_t acc = 0;
    for (int i = 0; i < kSamples; i++) acc += adc_read();
    const float mean = (float)acc / (float)kSamples;

    // ...then a slow EMA so the displayed value glides to the true die
    // temperature rather than mirroring the latest block. Seeded on the
    // first call so it doesn't ramp up from zero.
    static float ema = -1.0f;
    if (ema < 0.0f) ema = mean;
    else            ema += (mean - ema) * 0.15f;
    return (uint16_t)(ema + 0.5f);
}

// Mic-debug globals (defined in main.cpp). File-scope extern so the
// linker resolves them once and cmd.cpp's 0xFD handler reads the same
// memory main.cpp writes to.
extern volatile uint32_t g_bt_31_packets;
extern volatile uint32_t g_bt_other_packets;
extern volatile uint8_t  g_last_other_id;
extern volatile uint8_t  g_other_id_or;
extern volatile uint8_t  g_31_b2_or;
extern volatile uint8_t  g_last_31_b2;
extern volatile uint16_t g_31_len_min;
extern volatile uint16_t g_31_len_max;
extern volatile uint8_t  g_last_other_prefix[8];
extern volatile uint8_t  g_last_any_prefix[16];
extern volatile uint16_t g_longest_len;
extern volatile uint8_t  g_longest_frame[80];
extern volatile uint32_t g_host_out02_total;
extern volatile uint32_t g_host_out02_trig_allow;
extern volatile uint32_t g_host_out02_to_bt;

bool is_pico_cmd(uint8_t report_id) {
    if (report_id == 0xf6 ||
        report_id == 0xf7 ||
        report_id == 0xf8 ||
        report_id == 0xf9 ||
        report_id == 0xfa ||
        report_id == 0xfb ||
        report_id == 0xfc ||
        report_id == 0xfd ||  // mic-debug counters
        report_id == 0xfe     // mic-debug longest-frame dump
    ) {
        return true;
    }
    return false;
}

uint16_t pico_cmd_get(uint8_t report_id, uint8_t *buffer, uint16_t reqlen) {
    if (report_id == 0xf7) {
        printf("[HID] Receive 0xf7 getting config\n");
        const size_t cfg_len = sizeof(Config_body);
        if (cfg_len > reqlen) {
            printf("[Config] Warning: Config_body overflow\n");
        }
        const auto len = std::min(cfg_len, static_cast<size_t>(reqlen));
        memcpy(buffer, &get_config(), len);

        // OLED Edition: append the button-remap block right after Config_body
        // when the host asked for enough room. Old clients request exactly
        // sizeof(Config_body) and never see it; new web tools read config +
        // remap in one GET (the 0xF6/0xF7 reports are 63 bytes, plenty).
        //   [+0]      'R'
        //   [+1]      'M'
        //   [+2]      protocol version (kRemapProtoVer)
        //   [+3..+4]  revision uint16 LE (bumps on each successful set)
        //   [+5..+20] 16-byte remap table (source idx -> target idx, 0xFF=off)
        constexpr size_t kRemapBlock = 5 + kRemapCount;
        if (reqlen >= cfg_len + kRemapBlock) {
            uint8_t *p = buffer + cfg_len;
            p[0] = 'R';
            p[1] = 'M';
            p[2] = kRemapProtoVer;
            const uint16_t rev = remap_revision();
            p[3] = (uint8_t)(rev & 0xFF);
            p[4] = (uint8_t)((rev >> 8) & 0xFF);
            remap_get(p + 5);
            return cfg_len + kRemapBlock;
        }
        return len;
    }
    if (report_id == 0xf8) {
        printf("[HID] Receive 0xf8 getting firmware version\n");
        const auto len = std::min(strlen(PICO_PROGRAM_VERSION_STRING), static_cast<size_t>(reqlen));
        memcpy(buffer, PICO_PROGRAM_VERSION_STRING, len);
        return len;
    }
    if (report_id == 0xf9) {
        // [-128,0]
        int8_t rssi = 0;
        bt_get_signal_strength(&rssi);
        if (reqlen == 0) {
            return 0;
        }
        buffer[0] = rssi;
#if ENABLE_VERBOSE
        printf("[HID] 0xf9 RSSI=%d raw=0x%02X\n", rssi, buffer[0]);
#endif
        return 1;
    }
    if (report_id == 0xfa) {
        // OLED Edition: 4 x bd_addr (6 bytes each) + 4 x occupied flag = 28 bytes.
        constexpr uint16_t want = 28;
        if (reqlen < want) {
            printf("[HID] 0xfa reqlen=%u too small for slots payload (%u)\n", reqlen, want);
            return 0;
        }
        for (int i = 0; i < 4; i++) {
            uint8_t addr[6];
            bt_slot_get_addr(i, addr);
            memcpy(buffer + i * 6, addr, 6);
        }
        for (int i = 0; i < 4; i++) {
            buffer[24 + i] = bt_slot_occupied(i) ? 1 : 0;
        }
        return want;
    }
    if (report_id == 0xfb) {
        // OLED Edition: diagnostics + audio meters for the web emulator.
        constexpr uint16_t want = 18;
        if (reqlen < want) {
            printf("[HID] 0xfb reqlen=%u too small for diag payload (%u)\n", reqlen, want);
            return 0;
        }
        const uint32_t uptime_s   = time_us_32() / 1000000u;
        const uint32_t usb_frames = audio_usb_frames();
        const uint32_t bt_packets = audio_bt_packets();
        const uint32_t hci_errs   = bt_hci_err_count();
        memcpy(buffer + 0,  &uptime_s,   4);
        memcpy(buffer + 4,  &usb_frames, 4);
        memcpy(buffer + 8,  &bt_packets, 4);
        buffer[12] = audio_peak_speaker();
        buffer[13] = audio_peak_haptic();
        memcpy(buffer + 14, &hci_errs,   4);
        return want;
    }
    if (report_id == 0xfc) {
        // OLED Edition: CPU / Clock telemetry for the web emulator. 11 bytes:
        //   [0..3]  set_khz  uint32  configured clk_sys (SYS_CLOCK_KHZ)
        //   [4..7]  real_khz uint32  measured clk_sys (cached, see below)
        //   [8]     vcode    uint8   vreg_get_voltage() raw enum code
        //   [9..10] temp_raw uint16  ADC ch4 12-bit reading
        // The web side does the volts/temperature math (same formulas as
        // render_screen_cpu) so the firmware HID path stays float-free.
        constexpr uint16_t want = 11;
        if (reqlen < want) {
            printf("[HID] 0xfc reqlen=%u too small for cpu payload (%u)\n", reqlen, want);
            return 0;
        }
        const uint32_t set_khz = (uint32_t)SYS_CLOCK_KHZ;

        // clk_sys is fixed at boot and frequency_count_khz() busy-waits a few
        // ms — measure exactly once (lazily) and cache. Doing it here on the
        // first poll keeps it off the boot path; one ~ms stall in a single
        // GET_REPORT is acceptable.
        static uint32_t cached_real_khz = 0;
        if (cached_real_khz == 0) {
            cached_real_khz = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);
        }

        const uint16_t temp_raw = cpu_temp_raw_smoothed();

        const uint8_t vcode = (uint8_t)vreg_get_voltage();

        memcpy(buffer + 0, &set_khz,         4);
        memcpy(buffer + 4, &cached_real_khz, 4);
        buffer[8] = vcode;
        memcpy(buffer + 9, &temp_raw,        2);
        return want;
    }
    if (report_id == 0xfd) {
        // Bridge-diagnostics feature report. 44-byte payload.
        // Section 1: mic-investigation counters (original 0..31).
        //   [0..3]   uint32  BT 0x31 input report count
        //   [4..7]   uint32  BT non-0x31 input report count
        //   [8]      uint8   last non-0x31 report ID seen
        //   [9]      uint8   OR mask of all non-0x31 report IDs seen
        //   [10]     uint8   OR mask of byte[2] across all 0x31 frames
        //   [11]     uint8   last value of byte[2] in a 0x31 frame
        //   [12..13] uint16  min frame length seen
        //   [14..15] uint16  max frame length seen
        //   [16..23] uint8[8]  first 8 bytes of last non-0x31 frame
        //   [24..31] uint8[8]  first 8 bytes of most recent ANY frame
        // Section 2: trigger-flow counters (issue #3 triage).
        //   [32..35] uint32  host 0x02 OUT reports received total
        //   [36..39] uint32  ...of those, with Allow*TriggerFFB set
        //   [40..43] uint32  ...forwarded as BT 0x31 sub-0x10
        constexpr uint16_t want = 44;
        for (uint16_t i = 0; i < want && i < reqlen; i++) buffer[i] = 0;

        const uint32_t bt31    = g_bt_31_packets;
        const uint32_t btother = g_bt_other_packets;
        const uint16_t lmin    = g_31_len_min == 0xFFFF ? 0 : g_31_len_min;
        const uint16_t lmax    = g_31_len_max;

        memcpy(buffer + 0,  &bt31, 4);
        memcpy(buffer + 4,  &btother, 4);
        buffer[8]  = g_last_other_id;
        buffer[9]  = g_other_id_or;
        buffer[10] = g_31_b2_or;
        buffer[11] = g_last_31_b2;
        memcpy(buffer + 12, &lmin, 2);
        memcpy(buffer + 14, &lmax, 2);
        for (int i = 0; i < 8 && (16 + i) < reqlen; i++) buffer[16 + i] = g_last_other_prefix[i];
        for (int i = 0; i < 8 && (24 + i) < reqlen; i++) buffer[24 + i] = g_last_any_prefix[i];

        const uint32_t out02   = g_host_out02_total;
        const uint32_t out02_t = g_host_out02_trig_allow;
        const uint32_t out02_b = g_host_out02_to_bt;
        if ((32 + 4) <= reqlen) memcpy(buffer + 32, &out02,   4);
        if ((36 + 4) <= reqlen) memcpy(buffer + 36, &out02_t, 4);
        if ((40 + 4) <= reqlen) memcpy(buffer + 40, &out02_b, 4);
        return (reqlen < want) ? reqlen : want;
    }
    if (report_id == 0xfe) {
        // 0xFE: full content of the LONGEST 0x31 frame seen. Bytes 0-1
        // = length (uint16 LE), bytes 2+ = the captured frame bytes.
        constexpr uint16_t want = 82;  // 2 length + 80 frame bytes
        const uint16_t lim = (reqlen < want) ? reqlen : want;
        for (uint16_t i = 0; i < lim; i++) buffer[i] = 0;
        const uint16_t llen = g_longest_len;
        if (lim >= 2) {
            buffer[0] = (uint8_t)(llen & 0xFF);
            buffer[1] = (uint8_t)((llen >> 8) & 0xFF);
        }
        for (uint16_t i = 0; i < 80 && (i + 2) < lim; i++) {
            buffer[2 + i] = g_longest_frame[i];
        }
        return lim;
    }
    return 0;
}

void pico_cmd_set(uint8_t report_id, uint8_t const *buffer, uint16_t bufsize) {
    (void) report_id;
    if (bufsize == 0) {
        return;
    }

    // 0x01 update config in variable
    // 0x02 write config to flash
    // 0x03 reconnect tinyusb device;
    if (buffer[0] == 0x01) {
        printf("[CMD] Enter config set func\n");
        set_config(buffer + 1, bufsize - 1);
    }
    if (buffer[0] == 0x02) {
        printf("[CMD] Enter config save func\n");
        config_save();
    }
    if (buffer[0] == 0x03) {
        printf("[CMD] Enter tud reconnect func\n");
        tud_disconnect();
        sleep_ms(150);
        tud_connect();
    }
    // 0x10 set button-remap table (OLED Edition). Hardened framing so a stray
    // write to 0xF6 can't corrupt the map: magic 'R''M' + protocol version gate
    // before remap_set() (which itself validates each entry <16 or 0xFF=off).
    //   [0]      0x10  func-id
    //   [1]      'R'
    //   [2]      'M'
    //   [3]      protocol version (must == kRemapProtoVer)
    //   [4..19]  16-byte remap table
    if (buffer[0] == 0x10) {
        constexpr uint16_t kNeed = 4 + kRemapCount;
        if (bufsize < kNeed) {
            printf("[CMD] 0x10 remap-set too short (%u<%u)\n", bufsize, kNeed);
            return;
        }
        if (buffer[1] != 'R' || buffer[2] != 'M' || buffer[3] != kRemapProtoVer) {
            printf("[CMD] 0x10 remap-set bad magic/version\n");
            return;
        }
        if (remap_set(buffer + 4)) printf("[CMD] remap set ok (rev=%u)\n", remap_revision());
        else                       printf("[CMD] remap set rejected (invalid table)\n");
    }
}
