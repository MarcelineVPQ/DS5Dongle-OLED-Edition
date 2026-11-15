// Tension (protocol 1.23): the board side. The engine, encodings, validation
// and the flash record are pure, in tension.h; this file holds the state, the
// flash sector, the 0xF6 functions and the 0xE9 replies. Core0 only (the main
// loop and TinyUSB / BTstack callbacks on it); nothing here blocks, and every
// flash write runs from cmd_task(), never inside a USB callback.

#include "tension.h"

#include <cstdio>
#include <cstring>

#include "bt.h"
#include "calib.h"
#include "cmd.h"
#include "flash_layout.h"
#include "flash_safe.h"
#include "pico/time.h"
#include "state_mgr.h"

extern uint8_t interrupt_in_data[63];  // main.cpp: the controller's latest input report
extern uint8_t reportSeqCounter;       // shared with main.cpp's 0x31 forwards

using namespace tension;

namespace {
static_assert(TENSION_A_FLASH_OFFSET % FLASH_SECTOR_SIZE == 0 && TENSION_B_FLASH_OFFSET % FLASH_SECTOR_SIZE == 0);
constexpr uint32_t kSector[2] = {TENSION_A_FLASH_OFFSET, TENSION_B_FLASH_OFFSET};
static_assert(kStoreBytes <= FLASH_SECTOR_SIZE && kStoreBytes % FLASH_PAGE_SIZE == 0);

union Image {
    Store s;
    uint8_t bytes[kStoreBytes];
};
alignas(4) Image g_img{};  // the saved sets as in flash (reloaded when a save fails)
bool g_newer = false;      // flash holds a newer layout: shown, never written over
int8_t g_from = -1;        // the sector holding the newest copy (0 A, 1 B, -1 none)
uint32_t g_seq = 0;        // its sequence number

uint8_t g_staged[sizeof(SetBody)]{};  // 0xF6 0x1C writes here; 0x1D / 0x1E take it
SetBody g_preview{};                  // Feel it: runs instead of the chosen set
bool g_preview_on = false;
uint8_t g_choice = kNone;       // the set chosen now (0..7, kNone)
bool g_choice_temp = false;     // ...for now only (a game's profile), not the saved one
const SetBody *g_running = nullptr;

Engine g_eng;
bool g_was_connected = false;
char g_name[kNameLen + 1]{};

uint32_t now_ms() { return (uint32_t) (time_us_64() / 1000); }
const uint8_t *flash_bytes(int i) { return reinterpret_cast<const uint8_t *>(XIP_BASE + kSector[i]); }
// The pull as the user's trigger calibration corrects it (what the PC sees).
uint8_t pull(int t) { return calib_trigger(t, interrupt_in_data[t == kL2 ? 4 : 5]); }

// Both sectors into RAM: the newest valid copy.
void load_store() {
    const Pick p = store_pick(flash_bytes(0), flash_bytes(1), g_img.s);
    g_newer = p.load == Load::Newer;
    g_from = p.from;
    g_seq = p.seq;
}

void programs(const Program *pg[2]) {
    pg[0] = g_running ? &g_running->trig[kL2] : nullptr;
    pg[1] = g_running ? &g_running->trig[kR2] : nullptr;
}

// What runs now, from scratch (a new set, a changed slot, Feel it on / off):
// the stage timers start over, and the first step runs at once so a merge
// before the next pass already has the right effect.
void restart() {
    if (g_preview_on) g_running = &g_preview;
    else if (g_choice < kSlots && g_img.s.slot[g_choice].used == 1) g_running = &g_img.s.slot[g_choice].body;
    else g_running = nullptr;
    const uint32_t now = now_ms();
    // loop: HOT-PATH-OK — N = 2 triggers, on a set change
    for (int t = 0; t < 2; t++) {
        run_reset(g_eng.run[t]);
        if (g_running) run_step(g_eng.run[t], g_running->trig[t], pull(t), now);
    }
}

// Seals the store with the next sequence number and writes it to the sector
// that doesn't hold the newest copy, so the previous copy stays until this
// one is complete. On a failed write the RAM copy goes back to the newest
// valid copy in flash, so RAM and flash never differ.
bool save_store() {
    const int to = g_from == 0 ? 1 : 0;
    g_img.s.h.seq = g_seq + 1;
    store_seal(g_img.s);
    // error: SHOWN — false = CMD_RESULT_FAILED in 0xF1 for 0x1D / 0x1F / 0x20 (their callers); counted in g_fse_fail
    const bool ok = flash_sector_write_pages(kSector[to], g_img.bytes, kStoreBytes) &&
                    memcmp(flash_bytes(to), g_img.bytes, sizeof(Store)) == 0;
    if (!ok) {
        printf("[Tension] save failed\n");
        // error: NOT-WORTH — reloading what flash holds after a failed save (the failure itself is reported above)
        load_store();
        // error: SHOWN — CMD_RESULT_FAILED in 0xF1 via the callers (0x1D / 0x1F / 0x20); counted in g_fse_fail
        return false;
    }
    g_from = (int8_t) to;
    g_seq = g_img.s.h.seq;
    return true;
}

bool staged_ok(const uint8_t *crc_le) {
    uint32_t crc;
    memcpy(&crc, crc_le, 4);
    SetBody b;
    memcpy(&b, g_staged, sizeof(b));
    // error: SHOWN — false = CMD_RESULT_INVALID in 0xF1 for 0x1D / 0x1E (a CRC that doesn't match or a set out of range), the app's command error
    return crc == crc32_ieee(g_staged, sizeof(g_staged)) && set_valid(b);
}
}  // namespace

void tension_load() {
    // As at boot: nothing temporary survives (Feel it, a game's choice).
    g_preview_on = false;
    g_choice_temp = false;
    g_eng = Engine{};
    g_was_connected = false;
    load_store();
    g_choice = g_img.s.h.active;
    restart();
}

void tension_task() {
    const bool connected = bt_is_connected();
    if (connected != g_was_connected) {
        g_was_connected = connected;
        forget_controller(g_eng);  // a new link starts with no effect; the connect-time state carries ours
    }
    // error: NOT-WORTH — nothing; no controller, nothing to run
    if (!connected) return;
    const uint32_t now = now_ms();
    if (g_running) {
        // loop: HOT-PATH-OK — N = 2 triggers, per main-loop pass
        for (int t = 0; t < 2; t++) run_step(g_eng.run[t], g_running->trig[t], pull(t), now);
    }
    const Program *pg[2];
    programs(pg);
    uint8_t pkt[78]{};
    // error: NOT-WORTH — nothing to send this pass (no change, rate limit, paused), not a failure
    if (!own_report(g_eng, pg, pkt + 3, now)) return;
    pkt[0] = 0x31;
    pkt[1] = reportSeqCounter << 4;
    reportSeqCounter = (reportSeqCounter + 1) & 0x0F;
    pkt[2] = 0x10;
    state_stamp_host_rumble(pkt + 3);  // the game's rumble stays as it is
    // error: LOGGED-OK — counter bt_send_drops (0xFD, OLED Diagnostics); a full output queue drops it and Tension sends it again next pass
    if (!bt_write(pkt, sizeof(pkt))) tension_report_lost((uint8_t) (((pkt[3] & kAllowBit[kL2]) ? 1 : 0) | ((pkt[3] & kAllowBit[kR2]) ? 2 : 0)));
}

uint8_t tension_on_state_report(uint8_t *ss) {
    const Program *pg[2];
    programs(pg);
    return merge(g_eng, pg, ss, now_ms());
}

void tension_report_lost(uint8_t mask) { report_lost(g_eng, mask); }

void tension_revert() {
    // error: NOT-WORTH — nothing temporary to end, not a failure
    if (!g_choice_temp) return;
    g_choice = g_img.s.h.active;
    g_choice_temp = false;
    restart();
}

void tension_pause(bool on) {
    g_eng.paused = on;
    if (!on) forget_controller(g_eng);  // the test left its own effect: ours goes out again
}

const char *tension_running_name() {
    if (g_running == nullptr) g_name[0] = 0;
    else if (g_preview_on) strcpy(g_name, "Feel it");
    else memcpy(g_name, g_running->name, kNameLen);  // g_name[kNameLen] stays 0
    return g_name;
}

// 0x1C [ver, offset, bytes...]: into the staging buffer, at once.
uint8_t tension_stage_write(const uint8_t *a, uint16_t n) {
    // error: SHOWN — CMD_RESULT_INVALID in the 0xF1 result byte: bad framing or past the record's end, the app's command error
    if (n < 3 || a[0] != kWireVer || a[1] + (n - 2) > (int) sizeof(g_staged)) return CMD_RESULT_INVALID;
    memcpy(g_staged + a[1], a + 2, n - 2);
    return CMD_RESULT_OK;
}

// 0x1D [ver, slot, crc32 LE]: checked now, saved from cmd_task().
uint8_t tension_save_check(const uint8_t *a, uint16_t n) {
    // error: SHOWN — CMD_RESULT_INVALID in the 0xF1 result byte: bad framing, slot, CRC or set, the app's command error
    if (n < 6 || a[0] != kWireVer || a[1] >= kSlots || !staged_ok(a + 2)) return CMD_RESULT_INVALID;
    // error: SHOWN — CMD_RESULT_FAILED in the 0xF1 result byte: the sets were saved by newer firmware (0xE9 status [3] bit3); the app says so
    if (g_newer) return CMD_RESULT_FAILED;
    return CMD_RESULT_OK;
}

uint8_t tension_save_staged(uint8_t slot) {
    SlotRec &r = g_img.s.slot[slot];
    r.used = 1;
    memcpy(&r.body, g_staged, sizeof(r.body));
    // error: SHOWN — false = CMD_RESULT_FAILED in 0xF1 (returned below), the app's command error
    const bool ok = save_store();
    restart();
    // error: SHOWN — CMD_RESULT_FAILED in the 0xF1 result byte: not saved (the slot keeps what it had), the app's command error
    return ok ? CMD_RESULT_OK : CMD_RESULT_FAILED;
}

// 0x1E [ver, 1, crc32 LE] = Feel it with the staged set; [ver, 0] = stop.
uint8_t tension_feel(const uint8_t *a, uint16_t n) {
    // error: SHOWN — CMD_RESULT_INVALID in the 0xF1 result byte: bad framing, the app's command error
    if (n < 2 || a[0] != kWireVer || a[1] > 1) return CMD_RESULT_INVALID;
    if (a[1] == 1) {
        // error: SHOWN — CMD_RESULT_INVALID in the 0xF1 result byte: bad CRC or set, the app's command error
        if (n < 6 || !staged_ok(a + 2)) return CMD_RESULT_INVALID;
        memcpy(&g_preview, g_staged, sizeof(g_preview));
    }
    g_preview_on = a[1] == 1;
    restart();
    return CMD_RESULT_OK;
}

// 0x1F [ver, slot]: checked now, the slot emptied from cmd_task().
uint8_t tension_delete_check(const uint8_t *a, uint16_t n) {
    // error: SHOWN — CMD_RESULT_INVALID in the 0xF1 result byte: bad framing or slot, the app's command error
    if (n < 2 || a[0] != kWireVer || a[1] >= kSlots) return CMD_RESULT_INVALID;
    // error: SHOWN — CMD_RESULT_FAILED in the 0xF1 result byte: the sets were saved by newer firmware (0xE9 status [3] bit3); the app says so
    if (g_newer) return CMD_RESULT_FAILED;
    return CMD_RESULT_OK;
}

uint8_t tension_delete(uint8_t slot) {
    memset(&g_img.s.slot[slot], 0, sizeof(SlotRec));
    if (g_img.s.h.active == slot) g_img.s.h.active = kNone;
    // error: SHOWN — false = CMD_RESULT_FAILED in 0xF1 (returned below), the app's command error
    const bool ok = save_store();
    if (ok && g_choice == slot) {
        g_choice = kNone;
        g_choice_temp = false;
    }
    restart();
    // error: SHOWN — CMD_RESULT_FAILED in the 0xF1 result byte: not deleted (the slot keeps its set), the app's command error
    return ok ? CMD_RESULT_OK : CMD_RESULT_FAILED;
}

// 0x20 [ver, slot, temporary]: slot 0..7 or kNone (no set); 0xFE = back to
// the saved choice (temporary ignored). Temporary = now, RAM only (a game's
// profile); saved = deferred (`defer_save`), cmd_task() calls select_save.
uint8_t tension_select(const uint8_t *a, uint16_t n, bool &defer_save) {
    defer_save = false;
    const uint8_t slot = n >= 2 ? a[1] : 0;
    const bool slot_ok = slot == kNone || slot == 0xFE || (slot < kSlots && g_img.s.slot[slot].used == 1);
    // error: SHOWN — CMD_RESULT_INVALID in the 0xF1 result byte: bad framing, or an empty slot, the app's command error
    if (n < 3 || a[0] != kWireVer || a[2] > 1 || !slot_ok) return CMD_RESULT_INVALID;
    if (slot == 0xFE) {
        g_choice = g_img.s.h.active;
        g_choice_temp = false;
    } else if (a[2] == 1) {
        g_choice = slot;
        g_choice_temp = true;
    } else {
        // error: SHOWN — CMD_RESULT_FAILED in the 0xF1 result byte: the sets were saved by newer firmware (0xE9 status [3] bit3); the app says so
        if (g_newer) return CMD_RESULT_FAILED;
        defer_save = true;
        return CMD_RESULT_OK;
    }
    restart();
    return CMD_RESULT_OK;
}

uint8_t tension_select_save(uint8_t slot) {
    // The slot may have been emptied since the SET; then nothing is chosen.
    if (slot < kSlots && g_img.s.slot[slot].used != 1) slot = kNone;
    g_img.s.h.active = slot;
    // error: SHOWN — false = CMD_RESULT_FAILED in 0xF1 (returned below), the app's command error
    const bool ok = save_store();
    if (ok) {
        g_choice = slot;
        g_choice_temp = false;
    }
    restart();
    // error: SHOWN — CMD_RESULT_FAILED in the 0xF1 result byte: the choice wasn't saved (the old one stays), the app's command error
    return ok ? CMD_RESULT_OK : CMD_RESULT_FAILED;
}

// GET 0xE9, wValue's high byte = selector (docs/TENSION.md, "Reading"):
//   0x00 status (38 bytes), 0x01 list (38 bytes),
//   0x40 | slot << 2 | chunk (chunk 0..2): a saved set, 56 bytes a chunk.
uint16_t tension_get(uint8_t sel, uint8_t *b, uint16_t reqlen) {
    const uint8_t flags = (uint8_t) ((g_running ? 0x01 : 0) | (g_preview_on ? 0x02 : 0) | (g_choice_temp ? 0x04 : 0) |
                                     (g_newer ? 0x08 : 0) | (g_eng.paused ? 0x10 : 0));
    if (sel == 0x00) {
        constexpr uint16_t kLen = 8 + 2 * (4 + kEffectBytes);
        // error: NOT-WORTH — no data; a request shorter than the reply (the app always asks the full length, its transport reports a short reply)
        if (reqlen < kLen) return 0;
        b[0] = 'T';
        b[1] = 'N';
        b[2] = kWireVer;
        b[3] = flags;
        b[4] = g_img.s.h.active;
        b[5] = g_choice;
        b[6] = kSlots;
        b[7] = kStages;
        const bool connected = bt_is_connected();
        // loop: HOT-PATH-OK — N = 2 triggers, per 0xE9 GET (~10 Hz while the app's live view is open)
        for (int t = 0; t < 2; t++) {
            uint8_t *o = b + 8 + t * (4 + kEffectBytes);
            const Run &r = g_eng.run[t];
            o[0] = g_running ? g_eng.own[t] : kOwnerNone;
            o[1] = g_running && r.stage >= 0 ? (uint8_t) r.stage : kNone;
            o[2] = connected ? pull(t) : 0;
            o[3] = g_running ? r.held_tenths : 0;
            if (g_running) memcpy(o + 4, r.want, kEffectBytes);
        }
        return kLen;
    }
    if (sel == 0x01) {
        constexpr uint16_t kLen = 6 + 4 * kSlots;
        // error: NOT-WORTH — no data; a request shorter than the reply (the app always asks the full length, its transport reports a short reply)
        if (reqlen < kLen) return 0;
        b[0] = kWireVer;
        b[1] = kSlots;
        b[2] = 0;
        // loop: NOT-WORTH — 8 slots, per 0xE9 list GET
        for (int i = 0; i < kSlots; i++) {
            const SlotRec &r = g_img.s.slot[i];
            if (r.used == 1) b[2] |= (uint8_t) (1u << i);
            const uint32_t crc = r.crc;  // a copy: r.crc is a packed member
            memcpy(b + 6 + 4 * i, &crc, 4);
        }
        b[3] = g_img.s.h.active;
        b[4] = g_choice;
        b[5] = flags;
        return kLen;
    }
    if ((sel & 0xE0) == 0x40 && (sel & 0x03) < 3) {
        constexpr uint16_t kChunk = 56;
        const uint8_t slot = (sel >> 2) & 0x07, chunk = sel & 0x03;
        const uint16_t at = chunk * kChunk;
        const uint16_t n = sizeof(SetBody) - at < kChunk ? sizeof(SetBody) - at : kChunk;
        // error: NOT-WORTH — no data; a request shorter than the reply (the app always asks the full length, its transport reports a short reply)
        if (reqlen < 4 + n) return 0;
        const SlotRec &r = g_img.s.slot[slot];
        b[0] = kWireVer;
        b[1] = slot;
        b[2] = chunk;
        b[3] = r.used == 1 ? 1 : 0;
        memcpy(b + 4, reinterpret_cast<const uint8_t *>(&r.body) + at, n);
        return (uint16_t) (4 + n);
    }
    // error: NOT-WORTH — no data; an unknown selector (the app only asks the documented ones)
    return 0;
}
