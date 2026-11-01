//
// DualSense Edge profiles for Sony's PlayStation Accessories app (upstream
// awalol 8a2f576, issues #29 / #155).
//
// The Edge keeps up to four custom profiles. The app reads them back through
// feature reports 0x70-0x7B, but the controller fills those reports (a
// "snapshot") only after it has processed a SET 0x80 [0x70, 0x01], which takes
// about 3.5 s. A native Bluetooth host does that at connect; so does the
// dongle now:
//
//   connect (Edge found from report 0x20)
//     -> SET 0x65 = report 0x20 bytes 1..61 as is (no crc recompute)
//     -> SET 0x80 [0x70, 0x01] (crc32 added by set_feature_data)
//     -> 4 s later GET 0x70 .. 0x7B, one every 80 ms (a burst overflows the
//        control channel and drops requests: a profile shows "not assigned")
//     -> profile reads are answered from then on.
//   Until then a host GET of 0x70-0x7B is refused (stalled), so the app
//   retries instead of keeping an empty snapshot. USB isn't held back.
//
//   profile write (host SET 0x60-0x62)
//     -> 0.5 s later SET 0x80 [0x70, 0x01] again, then 6 GET 0x81 250 ms
//        apart from 0.5 s after it, as the app does itself
//     -> 5 s after the 0x80, GET 0x70 .. 0x7B again (80 ms apart), so the
//        saved profile shows without reopening the app. Reads stay answered.
//
// The 0x80 command and its 0x81 replies use the controller's single test
// result slot, which the configurator's colorway read and controller tests
// (cmd.cpp, ctrl_info.cpp) use too. dse_owns_test_slot() is true from the
// connect or the write until the refetch is done: those two wait for it, and
// dse_task() waits while they hold the slot. Main loop only, so no locking.
//

#ifndef DS5_BRIDGE_DSE_H
#define DS5_BRIDGE_DSE_H

#include <cstdint>

namespace dse {
constexpr uint8_t kFirstProfile = 0x70, kLastProfile = 0x7B;
constexpr uint64_t kUnlockWaitUs = 4000 * 1000;    // connect 0x80 -> fetch
constexpr uint64_t kFetchGapUs = 80 * 1000;        // between two profile GETs (and after the last)
constexpr uint64_t kSaveSettleUs = 500 * 1000;     // profile write -> 0x80
constexpr uint64_t kSavePollFirstUs = 500 * 1000;  // 0x80 -> first GET 0x81
constexpr uint64_t kSavePollGapUs = 250 * 1000;
constexpr uint8_t kSavePolls = 6;
constexpr uint64_t kSaveRefetchUs = 5000 * 1000;   // 0x80 -> refetch
}  // namespace dse

// An Edge connected (bt.cpp, on its report 0x20): the profile reads are
// refused from now until the connect fetch is done.
void dse_on_connect();

// Back to idle with reads allowed: a DualSense connected, or the link went.
// No reply counts as fresh until the controller sends one.
void dse_reset();

// A host SET was forwarded to the controller: 0x60-0x62 (a profile write) on
// an Edge starts the re-read.
void dse_on_profile_write(uint8_t report_id);

// 0x70-0x7B. Inline: bt.cpp's get_feature_data and the USB GET path run from RAM.
inline bool dse_is_profile_report(uint8_t report_id) {
    return report_id >= dse::kFirstProfile && report_id <= dse::kLastProfile;
}

// False while a host GET of a profile report must be refused.
bool dse_profiles_ready();

// Live reads (upstream 8c647e0, without its wait): a host GET of 0x81 (the
// test command's result) or of a profile report is answered only with a
// reply the controller sent after the PC's last SET 0x80 / 0x60-0x62, and a
// 0x81 reply answers one GET. Otherwise main.cpp asks the controller and the
// PC gets the 1-byte reply (only the report id) and asks again; the fresh
// reply is cached a few ms later. Upstream holds the USB request up to 250 ms
// while it polls Bluetooth; nothing here waits inside a TinyUSB callback.
inline bool dse_is_live_report(uint8_t report_id) {
    return report_id == 0x81 || dse_is_profile_report(report_id);
}
// bt.cpp: the controller's reply for report_id landed in the feature cache.
void dse_on_feature_reply(uint8_t report_id);
// main.cpp: a host SET of report_id went to the controller (0x80 makes 0x81
// and the profiles stale, 0x60-0x62 the profiles).
void dse_on_host_set(uint8_t report_id);
// A host GET of a live report may be answered from the cache now.
bool dse_live_reply_ready(uint8_t report_id);
// ...and was: a 0x81 reply answers one GET.
void dse_live_reply_taken(uint8_t report_id);

// The controller's 0x80/0x81 test slot is in use by this module.
bool dse_owns_test_slot();

// Main loop. may_use_test_slot = neither the colorway read nor a controller
// test holds the slot right now.
void dse_task(bool may_use_test_slot);

#endif //DS5_BRIDGE_DSE_H
