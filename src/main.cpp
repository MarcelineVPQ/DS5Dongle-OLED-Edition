//
// Created by awalol on 2026/3/4.
//

#include <cstdio>
#include "bsp/board_api.h"
#include "bt.h"
#include "core0_ram.h"
#include "utils.h"
#include "resample.h"
#include "audio.h"
#include "voice.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/watchdog.h"
#include "pico/cyw43_arch.h"
#include "state_mgr.h"
#include "status_gpio.h"
#if ENABLE_SERIAL
#include "pico/stdio_usb.h"
#endif
#include "config.h"
#include "cmd.h"
#if ENABLE_BATT_LED
#include "battery_led.h"
#endif
#include "oled.h"
#include "remap.h"
#include "calib.h"
#include "perf.h"
#include "ctrl_info.h"
#include "dse.h"
#include "fw_report.h"
#include "usb_power.h"

// Pico SDK speciifically for waiting on conditions
#include "pico/critical_section.h"
#include "hardware/sync.h"
#include "pico/multicore.h"

uint8_t reportSeqCounter = 0; // 4-bit BT output sequence, shared with audio.cpp

// A rumble motor value times a grip haptics strength [1,6], clamped.
static uint8_t scale_rumble(uint8_t v, float gain) {
    const int s = (int) ((float) v * gain + 0.5f);
    return (uint8_t) (s > 255 ? 255 : s);
}

uint8_t packetCounter = 0;
bool spk_active = false;
extern bool g_lightbar_override; // oled.cpp: OLED owns the lightbar

// Mic-debug instrumentation: count every 0x31 BT input report regardless
// of mic-tag bit, accumulate OR-mask of every byte-2 value seen (tells us
// which bits ever fire) and remember the last byte-2 value. Also track
// observed frame-length range. Surfaced on the OLED Diagnostics screen.
volatile uint32_t g_bt_31_packets = 0;
volatile uint32_t g_bt_other_packets = 0;
volatile uint8_t  g_last_other_id = 0;
volatile uint8_t  g_other_id_or = 0;
volatile uint8_t  g_last_31_b2 = 0;
volatile uint8_t  g_31_b2_or = 0;
volatile uint16_t g_31_len_min = 0xFFFF;
volatile uint16_t g_31_len_max = 0;
volatile uint8_t  g_mic_prefix[6] = {0};
volatile uint8_t  g_last_other_prefix[8] = {0};
volatile uint8_t  g_last_any_prefix[16] = {0};
volatile uint16_t g_longest_len = 0;
volatile uint8_t  g_longest_frame[MIC_DEBUG_FRAME_BYTES] = {0};
uint32_t bt_31_packet_count() { return g_bt_31_packets; }
uint8_t  bt_31_last_byte2()  { return g_last_31_b2; }
uint8_t  bt_31_b2_or_mask()  { return g_31_b2_or; }
uint16_t bt_31_len_min()     { return g_31_len_min == 0xFFFF ? 0 : g_31_len_min; }
uint16_t bt_31_len_max()     { return g_31_len_max; }
void bt_31_mic_prefix(uint8_t out[6]) {
    // loop: HOT-PATH-OK — 6 bytes, for the OLED diagnostics screen
    for (int i = 0; i < 6; i++) out[i] = g_mic_prefix[i];
}

// Trigger-flow diagnostics. Counts host → dongle → BT path for adaptive
// trigger effects. Lets us tell which link in the chain breaks when games
// like Death Stranding 2 don't produce trigger tension via the dongle:
//   out02_total     - every 0x02 HID OUT report received from host
//   out02_trig_allow - of those, how many set AllowRight/LeftTriggerFFB
//                     (valid_flag0 bits 2 & 3) — i.e. the host actually
//                     told us "apply trigger FFB"
//   out02_to_bt     - 0x02 reports that we forwarded to the controller as
//                     a BT 0x31 sub-0x10 packet (gated off when speaker is
//                     active; audio.cpp's 0x36 path carries state then)
//   out02_trig_folded - of the trig_allow reports, how many arrived while the
//                     speaker stream was active and were therefore NOT sent as
//                     a standalone 0x31 — their trigger FFB was folded into the
//                     0x36 audio frames via state[]. So trig_allow == to_bt's
//                     trigger share + this, proving the "missing" forwards
//                     (issue #6) aren't drops. Surfaced on the Diag screen.
// Surfaced on the OLED Diagnostics screen.
volatile uint32_t g_host_out02_total = 0;
volatile uint32_t g_host_out02_trig_allow = 0;
volatile uint32_t g_host_out02_to_bt = 0;
volatile uint32_t g_host_out02_trig_folded = 0;
uint32_t host_out02_total()       { return g_host_out02_total; }
uint32_t host_out02_trig_allow()  { return g_host_out02_trig_allow; }
uint32_t host_out02_to_bt()       { return g_host_out02_to_bt; }
uint32_t host_out02_trig_folded() { return g_host_out02_trig_folded; }

uint8_t interrupt_in_data[63] = {
    0x7f, 0x7d, 0x7f, 0x7e, 0x00, 0x00, 0xa7,
    0x08, 0x00, 0x00, 0x00, 0x52, 0x43, 0x30, 0x41,
    0x01, 0x00, 0x0e, 0x00, 0xef, 0xff, 0x03, 0x03,
    0x7b, 0x1b, 0x18, 0xf0, 0xcc, 0x9c, 0x60, 0x00,
    0xfc, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x09, 0x09, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xa7, 0xad, 0x60, 0x00, 0x29, 0x18, 0x00,
    0x53, 0x9f, 0x28, 0x35, 0xa5, 0xa8, 0x0c, 0x8b
};

critical_section_t report_cs;
volatile bool report_dirty = false;

void CORE0_RAM_FUNC(interrupt_loop)() {
    // OLED Edition: hold PS + Mute for 2 seconds to soft-reboot the dongle.
    // Works whether or not the OLED add-on is present. PS+Mute is uncommon
    // during gameplay and the long hold avoids accidental triggers.
    {
        static uint32_t combo_first_us = 0;
        constexpr uint8_t kComboBits = 0x05;       // byte 9: PS (bit 0) + Mute (bit 2)
        constexpr uint32_t kComboHoldUs = 2000000; // 2 seconds
        const bool held = (interrupt_in_data[9] & kComboBits) == kComboBits;
        if (held) {
            const uint32_t now = time_us_32();
            if (combo_first_us == 0) combo_first_us = now;
            else if ((now - combo_first_us) > kComboHoldUs) watchdog_reboot(0, 0, 0);
        } else {
            combo_first_us = 0;
        }
    }

    // Only the wake keyboard on the bus: HID instance 0 isn't the DualSense.
    if (usb_wake_keyboard() || !tud_hid_ready()) return;

    // TODO: Refactor for better code reuse
    if (get_config().polling_rate_mode != 2) {
        // Remap acts on the OUTGOING copy only — interrupt_in_data stays raw so
        // the reboot combo above and every OLED screen keep seeing physical input.
        uint8_t out[63];
        memcpy(out, interrupt_in_data, 63);
        calib_apply(out);
        remap_apply(out);
        // error: LOGGED-OK — printf; the HID IN endpoint is busy, this input frame is skipped and the next one (~1 ms) replaces it
        if (!tud_hid_report(0x01, out, 63)) {
            printf("[USBHID] tud_hid_report error\n");
        }
        return;
    }

    bool should_send = false;
    // Local buffer to hold the report data while we prepare it to send. 
    uint8_t safe_report[63];


    critical_section_enter_blocking(&report_cs);
    if (report_dirty) {
        memcpy(safe_report, interrupt_in_data, 63);
        report_dirty = false;
        should_send = true;
    }
    critical_section_exit(&report_cs);

    // Remap the snapshot, not interrupt_in_data (outgoing copy only — see above).
    if (should_send) {
        calib_apply(safe_report);
        remap_apply(safe_report);
    }

    // Only send to TinyUSB if we actually grabbed fresh data
    if (should_send) {
        // error: LOGGED-OK — printf; the HID IN endpoint is busy, this input frame is skipped and the next one (~1 ms) replaces it
        if (!tud_hid_report(0x01, safe_report, 63)) {
            printf("[USBHID] tud_hid_report error\n");

            // If the report failed to queue, restore the dirty flag 
            // so we try again on the next loop iteration.
            critical_section_enter_blocking(&report_cs);
            report_dirty = true;
            critical_section_exit(&report_cs);
        }
    }
}

void CORE0_RAM_FUNC(on_bt_data)(CHANNEL_TYPE channel, uint8_t *data, uint16_t len) {
    // printf("[Main] BT data callback: channel=%u len=%u\n", channel, len);
    // Track ALL INTERRUPT input reports, not just 0x31. The mic stream
    // may live on a different report ID — confirmed 2026-05-19 that data[2]
    // bit 0 (and bit 1) is NOT a mic flag, just the report-type indicator;
    // every "mic-tagged" frame turned out to be standard input.
    if (channel == INTERRUPT && len > 1) {
        if (data[1] == 0x31) g_bt_31_packets++;
        else {
            g_bt_other_packets++;
            g_last_other_id = data[1];
            g_other_id_or = (uint8_t)(g_other_id_or | data[1]);
            // loop: HOT-PATH-OK — ≤ 8 bytes per non-0x31 BT frame (debug counters)
            for (uint16_t i = 0; i < 8 && i < len; i++) {
                g_last_other_prefix[i] = data[i];
            }
        }
        if (len > 2) {
            g_last_31_b2 = data[2];
            g_31_b2_or = (uint8_t)(g_31_b2_or | data[2]);
        }
        if (len < g_31_len_min) g_31_len_min = len;
        if (len > g_31_len_max) g_31_len_max = len;
        // loop: HOT-PATH-OK — ≤ 16 bytes per BT frame (debug counters)
        for (uint16_t i = 0; i < 16 && i < len; i++) {
            g_last_any_prefix[i] = data[i];
        }

        // Capture the start of the longest 0x31 frame we've seen (0xFE
        // replies with its length and first MIC_DEBUG_FRAME_BYTES bytes). Long frames almost certainly carry the mic audio appended
        // after the standard 63-byte input report — this lets us look
        // at the trailing bytes directly via 0xFD diagnostic.
        if (data[1] == 0x31 && len > g_longest_len) {
            g_longest_len = len;
            // loop: NOT-WORTH — ≤ 61 bytes, only when a new longest 0x31 frame appears
            for (uint16_t i = 0; i < MIC_DEBUG_FRAME_BYTES && i < len; i++) {
                g_longest_frame[i] = data[i];
            }
        }
    }

    // Mic-in tap (TEST): once the dongle asserts the mic-enable bit in the
    // outgoing 0x36 audio report (pkt[4] bit 0, see audio.cpp — awalol
    // confirmed this is the key), the DS5 streams its mic as a 71-byte Opus
    // packet at data+4 of a 0x31 report with bit 1 of data[2] set. Route those
    // to the mic decoder instead of treating them as a standard input report.
    // The length guard (4-byte header + 71-byte Opus) keeps a stray short
    // frame from over-reading. The diagnostic counters above still observe
    // these frames, so the Diag screen's data[2] OR-mask will show bit 1 set
    // once the enable bit takes effect.
    // A mic-tagged 0x31 frame carries Opus audio at data+4, NOT a standard input
    // report — so it must ALWAYS be diverted here (decoded when mic is on, dropped
    // when off), never fall through to the input handler below. Letting it through
    // would copy Opus bytes into interrupt_in_data and corrupt sticks/buttons.
    if (channel == INTERRUPT && data[1] == 0x31 && ((data[2] >> 1) & 1)
        && len >= 75) {
        mic_add_queue(data + 4); // drops it unless the host is recording
        return;
    }

    if (channel == INTERRUPT && data[1] == 0x31) {
        if ((data[56] & 1) != (interrupt_in_data[53] & 1)) {
            set_headset(data[56] & 1);
        }

        if (get_config().polling_rate_mode != 2) {
            memcpy(interrupt_in_data, data + 3, 63);
#if ENABLE_BATT_LED
            battery_led_note_report();
#endif
            return;
        }

        critical_section_enter_blocking(&report_cs);
        memcpy(interrupt_in_data, data + 3, 63);
        report_dirty = true;
        critical_section_exit(&report_cs);
#if ENABLE_BATT_LED
        battery_led_note_report();
#endif
    }
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
static uint16_t get_report_impl(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer,
                                uint16_t reqlen);

// Ring of the last 16 host GET_REPORT requests (report id, bytes answered) for
// the 0xF2 diagnostic — shows what a game polls right before it gives up.
uint8_t g_get_ring[16][2]{};
volatile uint32_t g_get_count = 0;

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
    if (usb_wake_keyboard()) {  // the wake keyboard: no keys down
        // error: NOT-WORTH — USB STALL; a zero/short GET from the host OS, which retries with the right length
        if (reqlen < 8) return 0;
        memset(buffer, 0, 8);
        return 8;
    }
    const uint16_t n = get_report_impl(itf, report_id, report_type, buffer, reqlen);
    if (report_id < 0xF2) { // don't record our own diagnostic reads
        const uint32_t i = g_get_count++ % 16;
        g_get_ring[i][0] = report_id;
        g_get_ring[i][1] = n > 255 ? 255 : static_cast<uint8_t>(n);
    }
    return n;
}

static uint16_t get_report_impl(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer,
                                uint16_t reqlen) {
    (void) itf;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;

    // --- DualSense feature reports that Linux's hid_playstation reads at probe ---
    // Without valid answers the kernel never creates a gamepad device, so games
    // outside Steam Input (Heroic/Proton/native) see no controller. The host asks
    // only for the report DATA (reqlen = report_size - 1); usbhid prepends the
    // report-id byte itself. The two CRC'd reports validate crc32 over
    // [0xA3 feature-seed, report_id, data...] in the last 4 bytes.
    // hid_playstation (kernel) AND the game's native DualSense detection both read
    // 0x09 (pairing), 0x20 (firmware) and 0x05 (calibration). The KERNEL only checks
    // size + crc, but the GAME validates the actual CONTENT — so synthesized zeros
    // pass the kernel yet get rejected by the game (a ~156x GET retry storm, and no
    // native adaptive triggers). Serve the REAL controller data, which init_feature()
    // caches from the controller over BT (get_feature_data returns it incl. the
    // report-id at [0]). Fall back to a crc-valid synthetic answer ONLY when the
    // controller isn't linked yet (USB-enumeration probe before the BT link), so the
    // kernel still binds at that moment.
    if (report_id == 0x09 || report_id == 0x20 || report_id == 0x05) {
        // error: NOT-WORTH — USB STALL; a zero/short GET from the host OS, which retries with the right length
        if (reqlen == 0) return 0;
        // lifetime: NOT-WORTH — per host GET feature report (the PC asks a few times per connect)
        std::vector<uint8_t> real = get_feature_data(report_id, reqlen);
        if (report_id == 0x20 && real.size() > 1) {
            // An Edge presented as a DualSense gets a DualSense firmware report
            // (no failing PS Accessories update); everything else its real one.
            return fw_report::host_report(real.data(), real.size(), is_dse, ds_mode(), buffer, reqlen);
        }
        if (real.size() > 1) {                     // real cached response present
            uint16_t n = (uint16_t)(real.size() - 1);
            if (n > reqlen) n = reqlen;
            memcpy(buffer, real.data() + 1, n);
            return n;
        }
        memset(buffer, 0, reqlen);
        if (report_id == 0x09) {                   // not linked yet: MAC-only stub
            if (reqlen >= 6) bt_get_addr(buffer);
            return reqlen;
        }
        // error: NOT-WORTH — USB STALL; a zero/short GET from the host OS, which retries with the right length
        if (reqlen < 5) return 0;                  // 0x20 / 0x05 stub: zeros + valid crc32
        uint8_t tmp[2 + 64];
        tmp[0] = 0xA3; tmp[1] = report_id;
        memcpy(tmp + 2, buffer, reqlen - 4);
        uint32_t crc = crc32_seeded(tmp, (size_t)(2 + (reqlen - 4)), 0);
        buffer[reqlen - 4] = (uint8_t)(crc);
        buffer[reqlen - 3] = (uint8_t)(crc >> 8);
        buffer[reqlen - 2] = (uint8_t)(crc >> 16);
        buffer[reqlen - 1] = (uint8_t)(crc >> 24);
        return reqlen;
    }

    if (is_pico_cmd(report_id)) {
        return pico_cmd_get(report_id, buffer, reqlen);
    }

    // The dongle's controller test owns the 0x81 result slot: busy (stall).
    // error: NOT-WORTH — USB STALL on purpose: the app's controller test owns 0x81 for a few seconds; PC tools retry
    if (report_id == 0x81 && ctrl_test_owns_0x81()) return 0;

    // Edge profiles before the connect fetch is done: refused (stall), so PS
    // Accessories retries rather than keeping an empty snapshot. The GET still
    // goes to the controller, as upstream 8a2f576 does.
    if (dse_is_profile_report(report_id) && !dse_profiles_ready()) {
        bt_request_feature(report_id);
        // error: NOT-WORTH — USB STALL on purpose until the Edge profiles are cached (PS Accessories retries); the read is started here
        return 0;
    }

    // lifetime: NOT-WORTH — per host GET feature report (the PC asks a few times per connect)
    std::vector<uint8_t> feature_data = get_feature_data(report_id, reqlen);
    if (!feature_data.empty()) {
        memcpy(buffer, feature_data.data() + 1, feature_data.size() - 1);
    }

    return feature_data.empty() ? 0 : feature_data.size() - 1;
}

// Unplug / host reset: no SET_INTERFACE alt 0 arrives, so close the mic here.
void tud_umount_cb(void) {
    audio_set_mic_active(false);
}

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
    (void) rhport;
    uint8_t const itf = tu_u16_low(p_request->wIndex); // wInterface
    uint8_t const alt = tu_u16_low(p_request->wValue); // bAlternateSetting

    if (itf == 1) {
        printf("[AUDIO] Set interface Speaker to alternate setting %d\n", alt);
        spk_active = alt;
    }
    if (itf == 2) { // ITF_NUM_AUDIO_STREAMING_IN (microphone)
        printf("[AUDIO] Set interface Microphone to alternate setting %d\n", alt);
        audio_set_mic_active(alt != 0);
    }

    return true;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
// Flash-write and host feature-report counters for the 0xF5 diagnostics.
volatile uint32_t g_fse_calls = 0;     // flash_safe_execute() calls (any caller)
volatile uint32_t g_fse_fail = 0;      // ...that returned non-PICO_OK
volatile uint32_t g_setfeat_count = 0; // SET_REPORT with a non-zero report ID
volatile uint32_t g_setfeat_last = 0;  // last one: report_id | buffer[0] << 8 | bufsize << 16
// Copies of host 0x02 output reports (SetStateData, 47 bytes) for 0xF3/0xF4.
uint8_t g_last_out02[47]{};       // most recent
uint8_t g_last_out02_trig[47]{};  // most recent with an Allow*TriggerFFB bit

extern "C" int __real_flash_safe_execute(void (*func)(void *), void *param, uint32_t enter_exit_timeout_ms);
extern "C" int __wrap_flash_safe_execute(void (*func)(void *), void *param, uint32_t enter_exit_timeout_ms) {
    g_fse_calls++;
    // Before core1 registers for lockout (boot: BTstack TLV init, *_load
    // repairs) the SDK refuses with PICO_ERROR_NOT_PERMITTED and the write is
    // silently lost. Core1 isn't running audio yet, so masking core0's IRQs is
    // enough.
    if (!multicore_lockout_victim_is_initialized(1)) {
        const uint32_t irq = save_and_disable_interrupts();
        func(param);
        restore_interrupts(irq);
        return PICO_OK;
    }
    const int rc = __real_flash_safe_execute(func, param, enter_exit_timeout_ms);
    // error: LOGGED-OK — counter g_fse_fail (OLED Diag); the failed write itself is reported by its caller
    if (rc != PICO_OK) g_fse_fail++;
    return rc;
}

// A USB output report 0x02 (id at [0]) from the host to the controller as
// BT 0x31. Main-loop context (TinyUSB callbacks). False: too short, not sent.
static bool host_out02_forward(const uint8_t *buffer, uint16_t bufsize) {
    // error: NOT-WORTH — nothing; a malformed/short host report isn't forwarded (counted only when forwarded, g_host_out02_to_bt); games send well-formed ones every frame
    if (bufsize < 2) return false;
    // state[] still tracks the host's state for the OLED (LED readout,
    // trigger screens) and the BT-connect init.
    state_update(buffer + 1, bufsize - 1);
    // error: NOT-WORTH — nothing; a malformed/short host report isn't forwarded (counted only when forwarded, g_host_out02_to_bt); games send well-formed ones every frame
    if ((size_t) bufsize - 1 < sizeof(SetStateData)) return false;
    // Forward the host's report as-is, every time, as upstream does.
    // Audio frames (0x39) no longer carry state, so this is the only
    // path to the controller; sending the host's own allow bits (not a
    // latched copy) also ends the trigger re-fire buzz (#11).
    uint8_t outputData[78]{};
    outputData[0] = 0x31;
    outputData[1] = reportSeqCounter << 4;
    reportSeqCounter = (reportSeqCounter + 1) & 0x0F;
    outputData[2] = 0x10;
    memcpy(outputData + 3, buffer + 1, sizeof(SetStateData));
    // Classic rumble follows the grip haptics strengths too
    // (SetStateData [2] right / light motor, [3] left / heavy).
    outputData[3 + 2] = scale_rumble(outputData[3 + 2], get_config().haptics_gain_right);
    outputData[3 + 3] = scale_rumble(outputData[3 + 3], get_config().haptics_gain);
    if (g_lightbar_override) {
        outputData[3 + 1] &= ~0x04; // OLED owns the lightbar: drop AllowLedColor
    }
    // Protocol 1.13 settings on the game's own report: trigger
    // reduction, speaker pre-gain, mic choice, volume lock. With
    // all of them at "the game's own" the report goes out as the
    // game sent it (upstream #228's fix).
    state_stamp_config(outputData + 3, true);
    bt_write(outputData, sizeof(outputData));
    return true;
}

void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize) {
    (void) itf;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) bufsize;

    if (usb_wake_keyboard()) return;  // the wake keyboard's LED report: nothing to do

    if (report_id != 0) {
        g_setfeat_count++;
        g_setfeat_last = report_id | (bufsize ? buffer[0] : 0) << 8 | (uint32_t) bufsize << 16;
    }

    if (is_pico_cmd(report_id)) {
        printf("[HID] Receive 0xf6 setting config, funcid:0x%02X\n", buffer[0]);
        pico_cmd_set(report_id, buffer, bufsize);
        return;
    }

    // INTERRUPT OUT
    if (report_id == 0) {
        // Every report below starts with its id; a zero-length transfer (or a
        // lone id) carries nothing, and `bufsize - 1` would wrap.
        if (bufsize < 2) return;
        switch (buffer[0]) {
            case 0x02: {
                g_host_out02_total++;
                // valid_flag0 lives at buffer[1] (right after the 0x02 report id).
                // Bits 2 & 3 are AllowRight/LeftTriggerFFB.
                if (bufsize > 1 && (buffer[1] & 0x0C)) {
                    g_host_out02_trig_allow++;
                }
                if ((size_t) bufsize - 1 >= sizeof(g_last_out02)) {
                    memcpy(g_last_out02, buffer + 1, sizeof(g_last_out02));
                    if (buffer[1] & 0x0C) memcpy(g_last_out02_trig, buffer + 1, sizeof(g_last_out02_trig));
                }
                // error: NOT-WORTH — nothing; a malformed/short host report isn't forwarded (counted only when forwarded, g_host_out02_to_bt); games send well-formed ones every frame
                if (host_out02_forward(buffer, bufsize)) g_host_out02_to_bt++;
                break;
            }
        }
    }
    if (report_id == 0x80 && ctrl_test_owns_0x81()) return;  // busy: the dongle's test is running
    if (report_id == 0x80 ||
        // DSE: Write Profile Block
        report_id == 0x60 ||
        report_id == 0x62 ||
        report_id == 0x61) {
        set_feature_data(report_id, const_cast<uint8_t *>(buffer), bufsize);
        dse_on_profile_write(report_id);  // 0x60-0x62: re-read the profiles
        return;
    }
}

#if PICO_RP2350 && SYS_CLOCK_KHZ == 150000 && SYS_CLK_KHZ != 150000
#error "the Pico 2 W build assumes the SDK boots clk_sys at 150 MHz"
#endif

int main() {
    // The Pico 2 W runs at the clock it boots with (150 MHz), as upstream
    // awalol v0.7.0 does, at the default core voltage (1.10 V). That voltage
    // is set explicitly: the RP2350 keeps its regulator setting across every
    // reset but power-on, so a dongle that ran the old 320 MHz firmware would
    // otherwise stay at 1.20 V after this one is flashed. Lowering it at the
    // boot clock is safe; vreg_set_voltage() waits for the change to finish.
    // The Pico W (200 MHz) and the DS5_CLOCK_KHZ=320000 fallback raise
    // voltage and clock instead.
#if SYS_CLOCK_KHZ == 150000
    vreg_set_voltage(VREG_VOLTAGE_DEFAULT);
#else
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(1000);
    set_sys_clock_khz(SYS_CLOCK_KHZ, true);
#endif

    board_init();
    tusb_rhport_init_t dev_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_FULL
    };
    // error: NOT-WORTH — boot: TinyUSB init with a fixed, valid config; failure would leave no USB device at all (no app to tell), the OLED still runs
    tusb_init(BOARD_TUD_RHPORT, &dev_init);
#if !ENABLE_SERIAL
    // Give the host a moment to see us before the disconnect (upstream 4f9f485).
    sleep_ms(150);
    // error: NOT-WORTH — nothing; boot: disconnect of a device that was just initialized
    tud_disconnect();
#endif
    board_init_after_tusb();
#if ENABLE_SERIAL
    stdio_usb_init();
    // Debug builds: wait for a serial terminal so the boot log isn't lost
    // (upstream 356b69c), but not for ever: a debug build with nothing
    // attached still starts after 5 s.
    // loop: SERIAL-REQUIRED — debug build: wait for a terminal until a 5 s deadline
    for (const uint64_t until = time_us_64() + 5000 * 1000; !stdio_usb_connected() && time_us_64() < until;) {
        tud_task();
    }
    if (stdio_usb_connected()) sleep_ms(150);
#endif

    // error: LOGGED-OK — printf and main() returns: no radio, nothing can run; the board shows no OLED/USB activity, a hardware fault
    if (cyw43_arch_init()) {
        printf("Failed to initialize CYW43\n");
        return 1;
    }
#ifdef CYW43_WL_GPIO_SMPS_PIN
    // The Pico W's SMPS in PWM mode: no coil whine under light load (upstream 23a55da, #207).
    cyw43_arch_gpio_put(CYW43_WL_GPIO_SMPS_PIN, true);
#endif
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);

#if ENABLE_BATT_LED
    battery_led_init();
#endif

#if !ENABLE_SERIAL
    if (watchdog_caused_reboot()) {
        printf("Rebooted by Watchdog!\n");
        // 当崩溃重启以后，闪三下灯
        // loop: NOT-WORTH — 6 LED blinks after a watchdog reboot, boot-only
        for (int i = 0; i < 6; i++) {
            if (i % 2 == 0) {
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
            } else {
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
            }
            sleep_ms(500);
        }
    } else {
        printf("Clean boot\n");
    }
#endif

    // Initialize the critical section for the report buffer
    critical_section_init(&report_cs);

    config_load();
    remap_load();
    calib_load();

    bt_init();
    bt_register_data_callback(on_bt_data);

    audio_init();
    state_init();
    oled_init();

#if !ENABLE_SERIAL
    watchdog_enable(1000, true);
#endif

    // Sleep / wake handling and what the dongle presents on USB (usb_power.h).
    usb_power_init();

    // Per-stage timing for 0xFC (perf.h): a few cycle-counter reads a pass.
    perf_core_init();
    uint32_t loop_start = perf_now();
    // loop: SERIAL-REQUIRED — core0 main loop: stage order is the design, each stage perf-timed
    while (1) {
        const uint32_t t0 = perf_now();
        perf_record(PERF_LOOP, t0 - loop_start);
        loop_start = t0;
#if !ENABLE_SERIAL
        watchdog_update();
#endif
        cyw43_arch_poll();
        bt_connection_watchdog_tick();
        const uint32_t t1 = perf_now();
        perf_record(PERF_CYW43, t1 - t0);
        tud_task();
        usb_power_task();
        const uint32_t t2 = perf_now();
        perf_record(PERF_TUD, t2 - t1);
        cmd_task();
        state_config_task();
        status_gpio_task();
        const uint32_t t2c = perf_now();
        audio_loop();
        voice_task();
        const uint32_t t3 = perf_now();
        perf_record(PERF_AUDIO, t3 - t2c);
        interrupt_loop();
        calib_service();
        const uint32_t t4 = perf_now();
        perf_record(PERF_INPUT, (t4 - t3) + (t2c - t2));
        oled_loop();
        perf_record(PERF_OLED, perf_now() - t4);
#if ENABLE_BATT_LED
        battery_led_tick();
#endif
    }
}
