// What the OLED Settings screen saves (pure, host-tested in
// tests/host/test_settings_merge.cpp).
//
// The screen edits its own copy of the config, taken on the first visit and
// refreshed only when the app writes the config. Other paths change the live
// config meanwhile without touching that copy: the Lightbar screen (mode and
// favourites), a slot switch / forget / auto-switch (current_slot), KEY1's
// brightness, the narrator. Saving the whole copy wrote their old values back
// to flash. A save now starts from the live config and takes only the fields
// the screen itself shows from its copy.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "config.h"

namespace settings_merge {

// One Config_body field the Settings screen edits: its offset and size.
struct Field {
    uint8_t off;
    uint8_t size;
};

inline Config_body apply(const Config_body &live, const Config_body &local, const Field *fields, size_t n) {
    Config_body out = live;
    auto *dst = reinterpret_cast<uint8_t *>(&out);
    const auto *src = reinterpret_cast<const uint8_t *>(&local);
    // loop: NOT-WORTH — one pass over the Settings items (~25), once per Triangle save
    for (size_t i = 0; i < n; i++) {
        if (static_cast<size_t>(fields[i].off) + fields[i].size > sizeof(Config_body)) continue;
        std::memcpy(dst + fields[i].off, src + fields[i].off, fields[i].size);
    }
    return out;
}

}  // namespace settings_merge
