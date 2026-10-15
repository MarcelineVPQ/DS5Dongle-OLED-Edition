//
// When the dongle runs a Bluetooth inquiry (bt.cpp): the active scan that finds
// a DualSense in pairing mode (Create + PS). A controller that is already paired
// never needs it: it pages the dongle itself when PS is pressed, and page scan
// stays on the whole time (fast while no controller is connected). So the
// inquiry runs only in a pairing window, never forever:
//
//   - at boot: kPairWindowUs when the current slot is empty, kBootWindowUs
//     (one inquiry) when it holds a controller (re-pairing a controller that
//     lost its key, e.g. after a PS5, still works after a replug);
//   - on a pairing action: switching slot, forgetting a slot, wiping all
//     slots (OLED Slots screen or the app's 0xF6 0x04/0x05/0x06), and after a
//     failed authentication (the controller has to be paired again): kPairWindowUs;
//   - with no controller paired in any slot, after the window one inquiry
//     every kIdleGapUs of quiet (the dongle has nothing else to do, but no
//     longer scans back to back).
//
// A controller connecting closes the window. Before (to 0.9.5-dev): a 30-unit
// inquiry (38.4 s) restarted forever while no controller was connected: radio
// time and heat while idle, slower reconnects (page scan competes with the
// inquiry), and the dongle grabbed other hosts' pads in pairing mode (upstream
// awalol #158, #162, #196; upstream now scans at boot and on a BOOTSEL click,
// PR #171 for the reconnect side). Pure, no Pico SDK, for the host tests.
//

#ifndef DS5_BRIDGE_SCAN_POLICY_H
#define DS5_BRIDGE_SCAN_POLICY_H

#include <cstdint>

struct ScanPolicy {
    static constexpr uint64_t kPairWindowUs = 120000000;  // inquiries may start for 2 min
    static constexpr uint64_t kBootWindowUs = 30000000;   // boot with a paired slot: one inquiry
    static constexpr uint64_t kIdleGapUs = 60000000;      // nothing paired: quiet between inquiries

    bool window = false;       // a pairing window is open
    uint64_t window_end = 0;   // inquiries may start until then
    uint64_t quiet_since = 0;  // the last inquiry ended (the idle gap's clock)

    // Open (or extend) a pairing window of `len_us` from now.
    void open(uint64_t now, uint64_t len_us = kPairWindowUs) {
        const uint64_t end = now + len_us;
        if (!window || end > window_end) window_end = end;
        window = true;
    }
    // A controller connected: no more inquiries until the next window.
    void close() { window = false; }
    // An inquiry ended (or a connection attempt failed) with no controller
    // connected: start another one? Only inside the window; outside it the
    // quiet clock starts.
    bool again(uint64_t now) {
        if (window && now < window_end) return true;
        window = false;
        quiet_since = now;
        // error: NOT-WORTH — not a failure: false = the pairing window is over, stop scanning
        return false;
    }
    // Nothing paired in any slot and no inquiry running: time for the next
    // spaced-out inquiry? (Never while a window is open: again() owns that.)
    bool idle_due(uint64_t now, bool any_paired) const {
        return !window && !any_paired && now - quiet_since >= kIdleGapUs;
    }
};

#endif  // DS5_BRIDGE_SCAN_POLICY_H
