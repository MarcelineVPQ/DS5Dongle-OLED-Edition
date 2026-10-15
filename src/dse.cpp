//
// DualSense Edge profiles for PS Accessories. See dse.h for the protocol.
//

#include "dse.h"

#include <cstdio>
#include <vector>

#include "bt.h"
#include "config.h"
#include "pico/time.h"

using namespace dse;

namespace {
enum class Phase : uint8_t {
    Idle,
    Unlock,      // connect: 0x65 + 0x80 to send once the test slot is free
    UnlockWait,  // connect: waiting for the controller's snapshot
    SaveSettle,  // write seen: 0x80 to send kSaveSettleUs after it
    SavePoll,    // 0x80 sent: kSavePolls GET 0x81
    SaveWait,    // until kSaveRefetchUs after the 0x80
    Fetch,       // GET 0x70..0x7B, paced
};
Phase s_phase = Phase::Idle;
bool s_ready = true;         // host profile reads answered
bool s_fetch_readies = false;  // this fetch is the connect one: ends the refusal
bool s_save_pending = false;   // a write arrived during the connect sequence
uint64_t s_written_us = 0;     // the last profile write
uint64_t s_t0_us = 0;          // the phase's 0x80 SET (UnlockWait, SavePoll, SaveWait)
uint64_t s_last_get_us = 0;
uint8_t s_next = 0;            // Fetch: next report id (kLastProfile + 1 = all asked)
uint8_t s_polls = 0;

void send_unlock() {
    uint8_t unlock[59]{};  // the last 4 bytes become the crc32
    unlock[0] = 0x70;
    unlock[1] = 0x01;
    set_feature_data(0x80, unlock, sizeof(unlock));
}

void start_fetch(bool readies, uint64_t now) {
    s_phase = Phase::Fetch;
    s_fetch_readies = readies;
    s_next = kFirstProfile;
    s_last_get_us = now;
    bt_request_feature(s_next++);  // the reply lands in the feature cache
}

void start_save(uint64_t written_us) {
    s_phase = Phase::SaveSettle;
    s_written_us = written_us;
    s_save_pending = false;
}
}  // namespace

bool dse_profiles_ready() { return s_ready; }

bool dse_owns_test_slot() { return s_phase != Phase::Idle; }

void dse_reset() {
    s_phase = Phase::Idle;
    s_ready = true;
    s_fetch_readies = false;
    s_save_pending = false;
}

void dse_on_connect() {
    dse_reset();
    s_phase = Phase::Unlock;
    s_ready = false;
}

void dse_on_profile_write(uint8_t report_id) {
    if (!is_dse || report_id < 0x60 || report_id > 0x62) return;
    const uint64_t now = time_us_64();
    switch (s_phase) {
        case Phase::Unlock:
        case Phase::UnlockWait:
            s_save_pending = true;  // after the connect fetch
            s_written_us = now;
            break;
        case Phase::Fetch:
            if (s_fetch_readies) {
                s_save_pending = true;
                s_written_us = now;
                break;
            }
            [[fallthrough]];  // a save's own refetch: start over, it would miss this write
        default:
            start_save(now);
            break;
    }
}

void dse_task(bool may_use_test_slot) {
    if (!bt_is_connected()) {
        if (s_phase != Phase::Idle || !s_ready || s_save_pending) dse_reset();
        return;
    }
    const uint64_t now = time_us_64();
    switch (s_phase) {
        case Phase::Idle:
            return;
        case Phase::Unlock: {
            // error: NOT-WORTH — nothing; scheduling of the Edge profile unlock/fetch (test slot busy, settle and gap timers), retried next pass
            if (!may_use_test_slot) return;
            // SET 0x65: the firmware report's body as is, like a native host.
            // lifetime: NOT-WORTH — once per Edge unlock
            const std::vector<uint8_t> fw = bt_peek_feature(0x20);  // [0x20, data..., crc32]
            if (fw.size() >= 62) {
                uint8_t handshake[63];
                handshake[0] = 0x53;  // SET_REPORT | feature
                handshake[1] = 0x65;
                // loop: NOT-WORTH — 61-byte handshake copy, once per DSE unlock
                for (int i = 0; i < 61; i++) handshake[2 + i] = fw[1 + i];
                bt_send_control(handshake, sizeof(handshake));
            }
            send_unlock();
            s_t0_us = now;
            s_phase = Phase::UnlockWait;
            printf("[DSE] Unlock sent, profiles in %u ms\n", (unsigned) (kUnlockWaitUs / 1000));
            return;
        }
        case Phase::UnlockWait:
            if (now - s_t0_us >= kUnlockWaitUs) start_fetch(true, now);
            return;
        case Phase::SaveSettle:
            // error: NOT-WORTH — nothing; scheduling of the Edge profile unlock/fetch (test slot busy, settle and gap timers), retried next pass
            if (now - s_written_us < kSaveSettleUs || !may_use_test_slot) return;
            send_unlock();
            s_t0_us = now;
            s_polls = 0;
            s_phase = Phase::SavePoll;
            printf("[DSE] Profile saved: unlock re-sent\n");
            return;
        case Phase::SavePoll:
            if (now - s_t0_us >= kSavePollFirstUs + kSavePollGapUs * s_polls) {
                bt_request_feature(0x81);  // status poll, as the app does
                if (++s_polls >= kSavePolls) s_phase = Phase::SaveWait;
            }
            return;
        case Phase::SaveWait:
            if (now - s_t0_us >= kSaveRefetchUs) start_fetch(false, now);
            return;
        case Phase::Fetch:
            // error: NOT-WORTH — nothing; scheduling of the Edge profile unlock/fetch (test slot busy, settle and gap timers), retried next pass
            if (now - s_last_get_us < kFetchGapUs) return;
            s_last_get_us = now;
            if (s_next <= kLastProfile) {
                bt_request_feature(s_next++);
                return;
            }
            // One gap after the last GET: its reply is in by now.
            if (s_fetch_readies && !s_ready) {
                s_ready = true;
                printf("[DSE] Profiles ready\n");
            }
            s_fetch_readies = false;
            s_phase = Phase::Idle;
            if (s_save_pending) start_save(s_written_us);
            return;
    }
}
