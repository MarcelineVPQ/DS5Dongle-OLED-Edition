// Runs a released tag's own config.cpp to write real config pages.
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <string>
#include "config.h"
#include "hardware/flash.h"
extern uint8_t g_fake_flash[];
bool config_save_narrator(uint8_t value) __attribute__((weak));
bool config_save_power_flags(uint8_t flags) __attribute__((weak));

static void erase_all() { std::memset(g_fake_flash, 0xFF, PICO_FLASH_SIZE_BYTES); }
// Dump every sector whose first word is the config magic: offset + 256-byte page.
static void dump(const std::string &dir, const char *scen) {
    int found = 0;
    for (uint32_t off = 0; off < PICO_FLASH_SIZE_BYTES; off += FLASH_SECTOR_SIZE) {
        uint32_t m; std::memcpy(&m, g_fake_flash + off, 4);
        if (m != 0x66ccff00u) continue;
        found++;
        FILE *f = fopen((dir + "/" + scen + ".bin").c_str(), "wb");
        const uint32_t from_end = PICO_FLASH_SIZE_BYTES - off;
        fwrite(&from_end, 4, 1, f);
        fwrite(g_fake_flash + off, 1, FLASH_PAGE_SIZE, f);
        fclose(f);
    }
    fprintf(stderr, "%s: %d page(s)\n", scen, found);
}
#include "nondefault.h"
int main(int argc, char **argv) {
    const std::string dir = argv[1];
    erase_all(); config_load(); config_save(); dump(dir, "default");
    erase_all(); config_load();
    Config_body b = get_config(); nondefault(b); set_config(b); config_save(); dump(dir, "custom");
    if (config_save_narrator) { config_save_narrator(3); dump(dir, "custom_narrator"); }
    if (config_save_power_flags) { config_save_power_flags(0x03); dump(dir, "custom_power"); }
    // Layout facts for the test: where the fields sit in this tag's body.
    FILE *f = fopen((dir + "/layout.txt").c_str(), "w");
    fprintf(f, "body_size %zu inactive_time %zu haptics_gain %zu\n", sizeof(Config_body),
            offsetof(Config_body, inactive_time), offsetof(Config_body, haptics_gain));
    fclose(f);
}
