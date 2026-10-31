// Runs a released tag's own config.cpp to read a page saved by the current
// firmware (stable/current_custom.bin): does going back to that release keep
// the settings? Prints "ok" when it loads exactly what that release would
// hold after the same settings were set on it (nondefault.h), else "diff".
#include <cstdio>
#include <cstring>
#include "config.h"
#include "hardware/flash.h"
#include "nondefault.h"
extern uint8_t g_fake_flash[];

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    // What this release holds with the same settings set on it.
    std::memset(g_fake_flash, 0xFF, PICO_FLASH_SIZE_BYTES);
    config_load();
    Config_body expect = get_config();
    nondefault(expect);
    set_config(expect);
    expect = get_config();
    // The current firmware's page, where it saved it; then this release loads it.
    std::FILE *f = std::fopen(argv[1], "rb");
    uint32_t from_end = 0;
    uint8_t page[FLASH_PAGE_SIZE];
    if (!f || std::fread(&from_end, 4, 1, f) != 1 || std::fread(page, 1, sizeof(page), f) != sizeof(page)) return 2;
    std::fclose(f);
    std::memset(g_fake_flash, 0xFF, PICO_FLASH_SIZE_BYTES);
    std::memcpy(g_fake_flash + PICO_FLASH_SIZE_BYTES - from_end, page, sizeof(page));
    config_load();
    const Config_body got = get_config();
    const bool same = std::memcmp(&got, &expect, sizeof(got)) == 0;
    std::printf("%s\n", same ? "ok" : "diff");
    return same ? 0 : 1;
}
