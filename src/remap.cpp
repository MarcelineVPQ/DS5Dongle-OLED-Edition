// Button remapping. See remap.h. Flash-sector pattern mirrors slots.cpp;
// the apply logic + button set are ported from SundayMoments/DS5_Bridge.

#include "remap.h"

#include <cstring>
#include <cstdio>
#include <algorithm>

#include "hardware/flash.h"
#include "flash_safe.h"
#include "flash_layout.h"
#include "hardware/sync.h"

namespace {

// Source/target button indices. Order is arbitrary but MUST match the web
// editor's expectation (DS5Dongle-OLED-Config-Web src/protocol/remap.ts).
enum RemapButton : uint8_t {
    RemapL2, RemapL1, RemapCreate, RemapDpadUp, RemapDpadLeft, RemapDpadDown,
    RemapDpadRight, RemapL3, RemapR2, RemapR1, RemapOptions, RemapTriangle,
    RemapCircle, RemapCross, RemapSquare, RemapR3, RemapButtonCount,
};
static_assert(RemapButtonCount == kRemapCount, "kRemapCount must match RemapButton");

// interrupt_in_data[8]: shoulders / sticks / system bits.
constexpr uint8_t kL1Bit = 0x01, kR1Bit = 0x02, kL2Bit = 0x04, kR2Bit = 0x08,
                  kCreateBit = 0x10, kOptionsBit = 0x20, kL3Bit = 0x40, kR3Bit = 0x80;
// interrupt_in_data[7]: D-pad hat (low nibble) + face buttons (high nibble).
constexpr uint8_t kSquareBit = 0x10, kCrossBit = 0x20, kCircleBit = 0x40,
                  kTriangleBit = 0x80, kDpadMask = 0x0F;
// D-pad hat values.
constexpr uint8_t kUp = 0, kUpRight = 1, kRight = 2, kDownRight = 3, kDown = 4,
                  kDownLeft = 5, kLeft = 6, kUpLeft = 7, kNeutral = 8;

constexpr uint32_t REMAP_MAGIC = 0x44533503u; // "DS5\x03"

struct __attribute__((packed)) RemapData {
    uint32_t magic;
    uint8_t  table[kRemapCount];
    // proto 1.9: after the table, so older saves (0xFF here, erased) read as
    // "each extra button is itself".
    uint8_t  extra[kRemapExtraCount];
};
static_assert(sizeof(RemapData) <= FLASH_PAGE_SIZE);
static_assert(REMAP_FLASH_OFFSET % FLASH_SECTOR_SIZE == 0);

RemapData g_remap{};
bool      g_active   = false; // false = identity → remap_apply() fast-returns
uint16_t  g_revision = 0;
bool      g_temporary = false; // the table in RAM isn't the saved one (proto 1.8)

const RemapData *flash_remap() {
    return reinterpret_cast<const RemapData *>(XIP_BASE + REMAP_FLASH_OFFSET);
}

void set_identity() {
    // loop: NOT-WORTH — 16 table entries, on a reset / invalid load
    for (int i = 0; i < kRemapCount; i++) g_remap.table[i] = (uint8_t) i;
}

void recompute_active() {
    g_active = false;
    // loop: NOT-WORTH — 16 entries, after a host remap set
    for (int i = 0; i < kRemapCount; i++) {
        if (g_remap.table[i] != i) { g_active = true; return; }
    }
    // loop: NOT-WORTH — 4 extra buttons, after a host remap set
    for (int i = 0; i < kRemapExtraCount; i++) {
        if (g_remap.extra[i] != kRemapExtraSelf) { g_active = true; return; }
    }
}

bool valid_extra(const uint8_t *e) {
    // loop: NOT-WORTH — 4 extra buttons, validating a host set / load
    for (int i = 0; i < kRemapExtraCount; i++) {
        if (e[i] >= kRemapCount && e[i] != kRemapExtraSelf && e[i] != kRemapExtraOff) return false;
    }
    return true;
}

// The extra entries saved in flash (all "itself" if missing or invalid).
void flash_extra(uint8_t out[kRemapExtraCount]) {
    const RemapData *f = flash_remap();
    // error: NOT-WORTH — nothing; picks the saved copy when it's valid, else the identity default (not a failure)
    if (f->magic == REMAP_MAGIC && valid_extra(f->extra)) memcpy(out, f->extra, kRemapExtraCount);
    else memset(out, kRemapExtraSelf, kRemapExtraCount);
}

bool valid_table(const uint8_t *t) {
    // loop: NOT-WORTH — 16 entries, validating a host set / load
    for (int i = 0; i < kRemapCount; i++) {
        if (t[i] >= kRemapCount && t[i] != 0xFF) return false; // <16 or disabled
    }
    return true;
}

// Saves a table + extra entries (not necessarily what's in RAM: a temporary
// table must never reach flash).
bool save_with(const uint8_t *table, const uint8_t *extra) {
    RemapData data{};
    data.magic = REMAP_MAGIC;
    memcpy(data.table, table, kRemapCount);
    memcpy(data.extra, extra, kRemapExtraCount);
    alignas(4) uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xff, sizeof(page));
    memcpy(page, &data, sizeof(data));

    // error: SHOWN — passed up as NotSaved: CMD_RESULT_FAILED in 0xF1, the app's command error
    if (!flash_sector_write(REMAP_FLASH_OFFSET, page)) {
        printf("[Flash] core1 lockout timed out, write skipped\n");
        // error: SHOWN — passed up as NotSaved: CMD_RESULT_FAILED in 0xF1, the app's command error
        return false;
    }

    RemapData verify{};
    memcpy(&verify, flash_remap(), sizeof(verify));
    if (memcmp(&verify, &data, sizeof(data)) == 0) {
        printf("[Remap] flash write verified\n");
        return true;
    }
    printf("[Remap] flash write VERIFY FAILED\n");
    // error: SHOWN — passed up as NotSaved: CMD_RESULT_FAILED in 0xF1, the app's command error
    return false;
}

// error: SHOWN — passed up as NotSaved: CMD_RESULT_FAILED in 0xF1, the app's command error
bool save_to_flash() { return save_with(g_remap.table, g_remap.extra); }

bool dpad_has(uint8_t dir, RemapButton b) {
    switch (b) {
        case RemapDpadUp:    return dir == kUp || dir == kUpRight || dir == kUpLeft;
        case RemapDpadRight: return dir == kRight || dir == kUpRight || dir == kDownRight;
        case RemapDpadDown:  return dir == kDown || dir == kDownRight || dir == kDownLeft;
        case RemapDpadLeft:  return dir == kLeft || dir == kUpLeft || dir == kDownLeft;
        default:             return false;
    }
}

uint8_t dpad_from(bool up, bool right, bool down, bool left) {
    if (up && right && !down && !left) return kUpRight;
    if (right && down && !up && !left) return kDownRight;
    if (down && left && !up && !right) return kDownLeft;
    if (left && up && !right && !down) return kUpLeft;
    if (up && !down)    return kUp;
    if (right && !left) return kRight;
    if (down && !up)    return kDown;
    if (left && !right) return kLeft;
    return kNeutral;
}

} // namespace

void remap_load() {
    memcpy(&g_remap, flash_remap(), sizeof(g_remap));
    // error: LOGGED-OK — printf; a blank/invalid remap sector means no remap (identity), the app shows the default layout
    if (g_remap.magic != REMAP_MAGIC || !valid_table(g_remap.table)) {
        printf("[Remap] flash sector empty/invalid, defaulting to identity\n");
        g_remap.magic = REMAP_MAGIC;
        set_identity();
        memset(g_remap.extra, kRemapExtraSelf, kRemapExtraCount);
        // error: LOGGED-OK — printf; boot-time init save of a blank sector: the RAM copy works now and the next boot retries the save
        save_to_flash();
    }
    if (!valid_extra(g_remap.extra)) memset(g_remap.extra, kRemapExtraSelf, kRemapExtraCount);
    recompute_active();
    printf("[Remap] loaded (active=%d)\n", g_active);
}

void remap_get(uint8_t out[kRemapCount]) { memcpy(out, g_remap.table, kRemapCount); }

uint16_t remap_revision() { return g_revision; }

bool remap_set_temporary(const uint8_t *table) {
    if (!valid_table(table)) return false;
    memcpy(g_remap.table, table, kRemapCount);
    g_remap.magic = REMAP_MAGIC;
    recompute_active();
    g_temporary = true;
    g_revision++;
    return true;
}

void remap_revert() {
    const RemapData *saved = flash_remap();
    // error: NOT-WORTH — nothing; picks the saved copy when it's valid, else the identity default (not a failure)
    if (saved->magic == REMAP_MAGIC && valid_table(saved->table)) memcpy(g_remap.table, saved->table, kRemapCount);
    else set_identity();
    flash_extra(g_remap.extra);
    g_remap.magic = REMAP_MAGIC;
    recompute_active();
    g_temporary = false;
    g_revision++;
}

bool remap_temporary() { return g_temporary; }

bool remap_active() { return g_active; }

RemapSet remap_set_extra(const uint8_t *extra, bool temporary) {
    // error: SHOWN — CMD_RESULT_INVALID in 0xF1, the app's command error
    if (!valid_extra(extra)) return RemapSet::Invalid;
    memcpy(g_remap.extra, extra, kRemapExtraCount);
    g_remap.magic = REMAP_MAGIC;
    recompute_active();
    g_revision++;
    if (temporary) {
        g_temporary = true;
        return RemapSet::Ok;
    }
    // Saved: while a game's table is on (temporary), keep the saved table in
    // flash, never the temporary one.
    if (g_temporary) {
        alignas(4) uint8_t saved_table[kRemapCount];
        const RemapData *f = flash_remap();
        // error: NOT-WORTH — nothing; picks the saved copy when it's valid, else the identity default (not a failure)
        if (f->magic == REMAP_MAGIC && valid_table(f->table)) memcpy(saved_table, f->table, kRemapCount);
        // loop: NOT-WORTH — 16 entries, a host extra-button set while temporary
        else for (int i = 0; i < kRemapCount; i++) saved_table[i] = (uint8_t) i;
        // error: SHOWN — passed up as NotSaved: CMD_RESULT_FAILED in 0xF1, the app's command error
        return save_with(saved_table, g_remap.extra) ? RemapSet::Ok : RemapSet::NotSaved;
    }
    // error: SHOWN — passed up as NotSaved: CMD_RESULT_FAILED in 0xF1, the app's command error
    return save_to_flash() ? RemapSet::Ok : RemapSet::NotSaved;
}

void remap_get_extra(uint8_t out[kRemapExtraCount]) { memcpy(out, g_remap.extra, kRemapExtraCount); }

RemapSet remap_set(const uint8_t *table) {
    // error: SHOWN — CMD_RESULT_INVALID in 0xF1, the app's command error
    if (!valid_table(table)) return RemapSet::Invalid;
    memcpy(g_remap.table, table, kRemapCount);
    // A game's extra-button entries end with its temporary state too.
    if (g_temporary) flash_extra(g_remap.extra);
    g_temporary = false;
    g_remap.magic = REMAP_MAGIC;
    recompute_active();
    g_revision++;
    // error: SHOWN — NotSaved: CMD_RESULT_FAILED in 0xF1, the app's command error
    return save_to_flash() ? RemapSet::Ok : RemapSet::NotSaved;
}

void remap_apply(uint8_t *report) {
    if (!g_active) return; // identity → no-op (hot-path fast return)

    bool    src[kRemapCount]{};
    uint8_t src_analog[kRemapCount]{};
    const uint8_t dir = report[7] & kDpadMask;

    src[RemapL2]        = (report[8] & kL2Bit) != 0;
    src[RemapL1]        = (report[8] & kL1Bit) != 0;
    src[RemapCreate]    = (report[8] & kCreateBit) != 0;
    src[RemapDpadUp]    = dpad_has(dir, RemapDpadUp);
    src[RemapDpadLeft]  = dpad_has(dir, RemapDpadLeft);
    src[RemapDpadDown]  = dpad_has(dir, RemapDpadDown);
    src[RemapDpadRight] = dpad_has(dir, RemapDpadRight);
    src[RemapL3]        = (report[8] & kL3Bit) != 0;
    src[RemapR2]        = (report[8] & kR2Bit) != 0;
    src[RemapR1]        = (report[8] & kR1Bit) != 0;
    src[RemapOptions]   = (report[8] & kOptionsBit) != 0;
    src[RemapTriangle]  = (report[7] & kTriangleBit) != 0;
    src[RemapCircle]    = (report[7] & kCircleBit) != 0;
    src[RemapCross]     = (report[7] & kCrossBit) != 0;
    src[RemapSquare]    = (report[7] & kSquareBit) != 0;
    src[RemapR3]        = (report[8] & kR3Bit) != 0;

    // loop: HOT-PATH-OK — 16 sources per input report, only with a remap on (fast return)
    for (int i = 0; i < kRemapCount; i++) src_analog[i] = src[i] ? 0xFF : 0;
    src_analog[RemapL2] = report[4]; // L2 analog
    src_analog[RemapR2] = report[5]; // R2 analog

    bool    tgt[kRemapCount]{};
    uint8_t tgt_analog[kRemapCount]{};
    // loop: HOT-PATH-OK — 16 table entries per input report, only with a remap on
    for (int s = 0; s < kRemapCount; s++) {
        const uint8_t t = g_remap.table[s];
        if (t >= kRemapCount) continue; // 0xFF = disabled (source produces nothing)
        if (src[s]) tgt[t] = true;
        tgt_analog[t] = std::max(tgt_analog[t], src_analog[s]); // OR digital / max analog
    }

    // proto 1.9: the Edge's Fn buttons and paddles (byte 9). Remapped ones
    // press their target instead and are cleared, so the game sees only the
    // button chosen; "itself" leaves the bit alone.
    static constexpr uint8_t kExtraBits[kRemapExtraCount] = {0x10, 0x20, 0x40, 0x80};
    uint8_t b9 = report[9];
    // loop: HOT-PATH-OK — 4 Edge extra buttons per input report, only with a remap on
    for (int i = 0; i < kRemapExtraCount; i++) {
        const uint8_t t = g_remap.extra[i];
        if (t == kRemapExtraSelf || !(b9 & kExtraBits[i])) continue;
        b9 &= (uint8_t) ~kExtraBits[i];
        if (t < kRemapCount) {
            tgt[t] = true;
            tgt_analog[t] = 0xFF;
        }
    }
    report[9] = b9;

    report[4] = tgt_analog[RemapL2];
    report[5] = tgt_analog[RemapR2];

    // Byte 7 is entirely D-pad + face (all 8 bits) — rebuild it.
    report[7] = dpad_from(tgt[RemapDpadUp], tgt[RemapDpadRight],
                          tgt[RemapDpadDown], tgt[RemapDpadLeft]);
    if (tgt[RemapSquare])   report[7] |= kSquareBit;
    if (tgt[RemapCross])    report[7] |= kCrossBit;
    if (tgt[RemapCircle])   report[7] |= kCircleBit;
    if (tgt[RemapTriangle]) report[7] |= kTriangleBit;

    // Byte 8 is entirely shoulders/sticks/Create/Options (all 8 bits) — rebuild it.
    report[8] = 0;
    if (tgt[RemapL1])      report[8] |= kL1Bit;
    if (tgt[RemapR1])      report[8] |= kR1Bit;
    if (tgt[RemapL2])      report[8] |= kL2Bit;
    if (tgt[RemapR2])      report[8] |= kR2Bit;
    if (tgt[RemapCreate])  report[8] |= kCreateBit;
    if (tgt[RemapOptions]) report[8] |= kOptionsBit;
    if (tgt[RemapL3])      report[8] |= kL3Bit;
    if (tgt[RemapR3])      report[8] |= kR3Bit;
}
