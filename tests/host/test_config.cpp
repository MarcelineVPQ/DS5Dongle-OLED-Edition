// src/config.cpp: defaults and clamping (config_valid), the flash round trip
// with CRC, the legacy-sector migration, and set_config keeping the narrator.
#include <cmath>
#include <cstddef>
#include <cstring>
#include "config.h"
#include <cstddef>
#include "status_gpio.h"
#include "fakes.h"
#include "flash_layout.h"
#include "hardware/flash.h"
#include "test.h"
#include "utils.h"

namespace {
// What the firmware that wrote a page stored: the CRC over its own body
// length (`size`), which config_load() checks.
void reseal_config_page(uint16_t size) {
    uint8_t *raw = g_fake_flash + CONFIG_FLASH_OFFSET;
    const uint32_t crc = crc32(raw + offsetof(Config, body), size);
    std::memcpy(raw + offsetof(Config, crc32), &crc, sizeof(crc));
}
}  // namespace

TEST(config_erased_flash_gives_documented_defaults) {
    fake_flash_erase_all();
    config_load();
    const Config_body &c = get_config();
    CHECK(c.haptics_gain == 1.0f);
    CHECK(c.haptics_gain_right == 1.0f);
    CHECK(c.speaker_volume == 0.0f);
    CHECK_EQ(c.inactive_time, 30);
    CHECK_EQ(c.lightbar_mode, 8);
    CHECK_EQ(c.current_slot, 0);
    CHECK_EQ(c.narrator_voice, 0xFF);
    CHECK_EQ(c.screen_dim_timeout, 2);
    CHECK_EQ(c.screen_off_timeout, 15);
    CHECK_EQ(c.polling_rate_mode, 1);  // 500 Hz on a fresh config (upstream 501d435)
    CHECK_EQ(c.trigger_reduce, 0);
    CHECK_EQ(c.speaker_gain, 0);
    CHECK_EQ(c.lock_volume, 0);
    CHECK_EQ(c.mic_select, 0);
    CHECK_EQ(c.speaker_select, 0);
    CHECK_EQ(c.status_gpio_pin, kStatusGpioOff);
    CHECK_EQ(c.status_gpio_mode, 0);
    CHECK_EQ(c.mic_level, 0);  // follow the PC (1.16)
    CHECK_EQ(c.mic_echo_cancel, 1);  // on (1.17)
    CHECK(config_unsaved());  // nothing valid saved yet
}

// A config saved by 1.6-1.12 firmware: a 42-byte body, the page 0xFF after
// it. Its settings stay (a saved 250 Hz too), the 1.13 fields take defaults.
TEST(config_older_body_keeps_settings_and_defaults_the_new_fields) {
    fake_flash_erase_all();
    g_fake_flash_fail = false;
    config_load();
    Config_body b = get_config();
    b.polling_rate_mode = 0;  // 250 Hz, saved on purpose
    b.inactive_time = 40;
    set_config(b);
    CHECK(config_save());
    Config page{};
    std::memcpy(&page, g_fake_flash + CONFIG_FLASH_OFFSET, sizeof(page));
    uint8_t *raw = g_fake_flash + CONFIG_FLASH_OFFSET;
    std::memset(raw + offsetof(Config, body) + kConfigBodyV16, 0xFF, sizeof(Config_body) - kConfigBodyV16);
    page.size = kConfigBodyV16;
    std::memcpy(raw + offsetof(Config, size), &page.size, sizeof(page.size));
    reseal_config_page(kConfigBodyV16);
    config_default();
    config_load();
    const Config_body &c = get_config();
    CHECK_EQ(c.polling_rate_mode, 0);
    CHECK_EQ(c.inactive_time, 40);
    CHECK_EQ(c.trigger_reduce, 0);
    CHECK_EQ(c.speaker_select, 0);
    CHECK_EQ(c.status_gpio_pin, kStatusGpioOff);
}

TEST(config_clamps_the_1_13_fields) {
    fake_flash_erase_all();
    config_load();
    Config_body b = get_config();
    b.trigger_reduce = 10;
    b.speaker_gain = 7;
    b.lock_volume = 1;
    b.mic_select = 2;
    b.speaker_select = 3;
    b.status_gpio_pin = 22;
    b.status_gpio_mode = 1;
    set_config(b);
    CHECK_EQ(get_config().trigger_reduce, 10);
    CHECK_EQ(get_config().speaker_gain, 7);
    CHECK_EQ(get_config().lock_volume, 1);
    CHECK_EQ(get_config().mic_select, 2);
    CHECK_EQ(get_config().speaker_select, 3);
    CHECK_EQ(get_config().status_gpio_pin, 22);
    CHECK_EQ(get_config().status_gpio_mode, 1);
    b.trigger_reduce = 11;
    b.speaker_gain = 8;
    b.lock_volume = 2;
    b.mic_select = 3;
    b.speaker_select = 4;
    b.status_gpio_pin = 9;  // the OLED's chip select
    b.status_gpio_mode = 2;
    set_config(b);
    CHECK_EQ(get_config().trigger_reduce, 0);
    CHECK_EQ(get_config().speaker_gain, 0);
    CHECK_EQ(get_config().lock_volume, 0);
    CHECK_EQ(get_config().mic_select, 0);
    CHECK_EQ(get_config().speaker_select, 0);
    CHECK_EQ(get_config().status_gpio_pin, kStatusGpioOff);
    CHECK_EQ(get_config().status_gpio_mode, 0);
}

// A config saved by 1.13..1.15 firmware: a 49-byte body, 0xFF after it. Its
// 1.13 settings stay, the 1.16 mic level follows the PC.
TEST(config_1_13_body_keeps_settings_and_defaults_the_mic_level) {
    fake_flash_erase_all();
    g_fake_flash_fail = false;
    config_load();
    Config_body b = get_config();
    b.trigger_reduce = 3;
    b.status_gpio_mode = 1;
    set_config(b);
    CHECK(config_save());
    uint8_t *raw = g_fake_flash + CONFIG_FLASH_OFFSET;
    raw[offsetof(Config, body) + offsetof(Config_body, mic_level)] = 0xFF;
    const uint16_t size = 49;
    std::memcpy(raw + offsetof(Config, size), &size, sizeof(size));
    reseal_config_page(size);
    config_default();
    config_load();
    CHECK_EQ(get_config().trigger_reduce, 3);
    CHECK_EQ(get_config().status_gpio_mode, 1);
    CHECK_EQ(get_config().mic_level, 0);
}

// A config saved by 1.16 firmware: a 50-byte body, 0xFF after it. Its mic
// level stays, the 1.17 echo cancellation is on.
TEST(config_1_16_body_keeps_the_mic_level_and_turns_echo_cancel_on) {
    fake_flash_erase_all();
    g_fake_flash_fail = false;
    config_load();
    Config_body b = get_config();
    b.mic_level = 20;
    b.mic_echo_cancel = 0;
    set_config(b);
    CHECK(config_save());
    uint8_t *raw = g_fake_flash + CONFIG_FLASH_OFFSET;
    raw[offsetof(Config, body) + offsetof(Config_body, mic_echo_cancel)] = 0xFF;
    const uint16_t size = 50;
    std::memcpy(raw + offsetof(Config, size), &size, sizeof(size));
    reseal_config_page(size);
    config_default();
    config_load();
    CHECK_EQ(get_config().mic_level, 20);
    CHECK_EQ(get_config().mic_echo_cancel, 1);
}

TEST(config_clamps_the_echo_cancel) {
    fake_flash_erase_all();
    config_load();
    Config_body b = get_config();
    for (int v : {0, 1}) {
        b.mic_echo_cancel = (uint8_t) v;
        set_config(b);
        CHECK_EQ(get_config().mic_echo_cancel, v);
    }
    for (int v : {2, 0x80, 0xFF}) {
        b.mic_echo_cancel = (uint8_t) v;
        set_config(b);
        CHECK_EQ(get_config().mic_echo_cancel, 1);
    }
}

TEST(config_clamps_the_mic_level) {
    fake_flash_erase_all();
    config_load();
    Config_body b = get_config();
    for (int v : {0, 1, 32, 64}) {
        b.mic_level = (uint8_t) v;
        set_config(b);
        CHECK_EQ(get_config().mic_level, v);
    }
    for (int v : {65, 128, 0xFF}) {
        b.mic_level = (uint8_t) v;
        set_config(b);
        CHECK_EQ(get_config().mic_level, 0);
    }
}

// An older app writes the 42-byte body: the 1.13 fields stay as they are.
TEST(config_short_host_write_keeps_the_1_13_fields) {
    fake_flash_erase_all();
    config_load();
    Config_body b = get_config();
    b.trigger_reduce = 4;
    set_config(reinterpret_cast<const uint8_t *>(&b), sizeof(b));
    b.trigger_reduce = 0;
    b.inactive_time = 50;
    set_config(reinterpret_cast<const uint8_t *>(&b), kConfigBodyV16);
    CHECK_EQ(get_config().inactive_time, 50);
    CHECK_EQ(get_config().trigger_reduce, 4);
}

TEST(status_gpio_pins) {
    for (int pin : {2, 3, 4, 5, 6, 7, 13, 14, 16, 18, 19, 20, 21, 22, 26, 27, 28, 0xFF}) CHECK(status_gpio_pin_valid((uint8_t) pin));
    for (int pin : {0, 1, 8, 9, 10, 11, 12, 15, 17, 23, 24, 25, 29, 30, 47, 0xFE}) CHECK(!status_gpio_pin_valid((uint8_t) pin));
}

TEST(config_save_load_round_trip) {
    fake_flash_erase_all();
    g_fake_flash_fail = false;
    config_load();
    Config_body b = get_config();
    b.haptics_gain = 2.5f;
    b.inactive_time = 45;
    b.lb_fav_r[2] = 7;
    set_config(b);
    CHECK(config_unsaved());
    CHECK(config_save());
    CHECK(!config_unsaved());
    config_default();
    config_load();
    CHECK(get_config().haptics_gain == 2.5f);
    CHECK_EQ(get_config().inactive_time, 45);
    CHECK_EQ(get_config().lb_fav_r[2], 7);
}

TEST(config_clamps_bad_values) {
    fake_flash_erase_all();
    config_load();
    Config_body b = get_config();
    b.haptics_gain = NAN;
    b.haptics_gain_right = 9.0f;
    b.speaker_volume = 50.0f;
    b.inactive_time = 2;
    b.polling_rate_mode = 9;
    b.current_slot = 7;
    set_config(b);
    const Config_body &c = get_config();
    CHECK(c.haptics_gain == 1.0f);
    CHECK(c.haptics_gain_right == 1.0f);  // falls back to the left grip's
    CHECK(c.speaker_volume == 0.0f);
    CHECK_EQ(c.inactive_time, 30);
    CHECK_EQ(c.polling_rate_mode, 1);  // an invalid rate takes the 500 Hz default
    CHECK_EQ(c.current_slot, 0);
}

TEST(config_host_write_keeps_narrator) {
    fake_flash_erase_all();
    config_load();
    CHECK(config_save_narrator(3));
    Config_body b = get_config();
    b.narrator_voice = 6;
    set_config(reinterpret_cast<const uint8_t *>(&b), sizeof(b));
    CHECK_EQ(get_config().narrator_voice, 3);
    // A short (older app) body: only its bytes change.
    uint8_t shortb[5];
    std::memcpy(shortb, &b, sizeof(shortb));
    set_config(shortb, sizeof(shortb));
    CHECK_EQ(get_config().narrator_voice, 3);
}

TEST(config_migrates_legacy_sector) {
    fake_flash_erase_all();
    config_load();
    Config_body b = get_config();
    b.inactive_time = 12;
    set_config(b);
    CHECK(config_save());
    // Move the saved page to the old last-sector location, erase the new one.
    std::memcpy(g_fake_flash + LEGACY_CONFIG_FLASH_OFFSET, g_fake_flash + CONFIG_FLASH_OFFSET, FLASH_PAGE_SIZE);
    std::memset(g_fake_flash + CONFIG_FLASH_OFFSET, 0xFF, FLASH_SECTOR_SIZE);
    config_default();
    config_load();
    CHECK_EQ(get_config().inactive_time, 12);
    CHECK(!config_unsaved());  // written to the new sector
}

// Load checks the CRC: a corrupt or torn save loads defaults, not whatever
// in-range bytes it holds.
TEST(config_load_rejects_a_bad_crc) {
    fake_flash_erase_all();
    g_fake_flash_fail = false;
    config_load();
    Config_body b = get_config();
    b.inactive_time = 45;
    set_config(b);
    CHECK(config_save());
    config_load();
    CHECK_EQ(get_config().inactive_time, 45);  // intact
    // One flipped byte in the body (still in range for config_valid).
    g_fake_flash[CONFIG_FLASH_OFFSET + offsetof(Config, body) + offsetof(Config_body, inactive_time)] = 44;
    config_load();
    CHECK_EQ(get_config().inactive_time, 30);  // the default, not 44
    CHECK(config_unsaved());
    // A size past the page can't be one of ours.
    b.inactive_time = 45;
    set_config(b);
    CHECK(config_save());
    const uint16_t huge = 0xFFF0;
    std::memcpy(g_fake_flash + CONFIG_FLASH_OFFSET + offsetof(Config, size), &huge, sizeof(huge));
    config_load();
    CHECK_EQ(get_config().inactive_time, 30);
}

namespace {
// A config saved before the sector move: at the legacy (last) sector only.
void legacy_only(uint8_t inactive_time) {
    fake_flash_erase_all();
    g_fake_flash_fail = false;
    config_load();
    Config_body b = get_config();
    b.inactive_time = inactive_time;
    set_config(b);
    CHECK(config_save());
    std::memcpy(g_fake_flash + LEGACY_CONFIG_FLASH_OFFSET, g_fake_flash + CONFIG_FLASH_OFFSET, FLASH_PAGE_SIZE);
    std::memset(g_fake_flash + CONFIG_FLASH_OFFSET, 0xFF, FLASH_SECTOR_SIZE);
    config_default();
}
bool legacy_erased() {
    // loop: NOT-WORTH — host test, one sector
    for (uint32_t i = 0; i < FLASH_SECTOR_SIZE; i++)
        if (g_fake_flash[LEGACY_CONFIG_FLASH_OFFSET + i] != 0xFF) return false;
    return true;
}
}  // namespace

// After a migration the legacy copy is erased, so a later bad save in the new
// sector loads defaults instead of bringing the years-old settings back.
TEST(config_migration_erases_the_legacy_copy) {
    legacy_only(12);
    config_load();
    CHECK_EQ(get_config().inactive_time, 12);
    CHECK(!config_unsaved());
    CHECK(legacy_erased());
    // Power lost mid-save later: the new sector erased, nothing programmed.
    std::memset(g_fake_flash + CONFIG_FLASH_OFFSET, 0xFF, FLASH_SECTOR_SIZE);
    config_load();
    CHECK_EQ(get_config().inactive_time, 30);  // defaults, not the legacy 12
}

// Power-loss order: the legacy copy goes only after the new one is written
// and verified. A failed migration save keeps it, and the next boot migrates.
TEST(config_migration_keeps_the_legacy_copy_until_the_new_one_is_saved) {
    legacy_only(12);
    g_fake_flash_fail = true;  // the save fails (as a power loss before it)
    config_load();
    CHECK_EQ(get_config().inactive_time, 12);  // in RAM for this boot
    CHECK(!legacy_erased());
    g_fake_flash_fail = false;
    config_default();
    config_load();  // next boot
    CHECK_EQ(get_config().inactive_time, 12);
    CHECK(!config_unsaved());
    CHECK(legacy_erased());
}

// Power lost after the new copy was saved but before the legacy erase: both
// are intact; the next boot loads the new one and finishes the erase.
TEST(config_load_finishes_an_interrupted_migration) {
    legacy_only(12);
    // The new sector got the migrated copy, then a later save changed it.
    Config_body b = get_config();
    b.inactive_time = 20;
    set_config(b);
    CHECK(config_save());
    config_default();
    config_load();
    CHECK_EQ(get_config().inactive_time, 20);  // the new copy wins
    CHECK(legacy_erased());
}

// 0.9.5-dev.9 (protocol 1.14, 49-byte body) kept power_flags right after its
// body, where mic_level is now: they load as its power flags, and mic_level
// takes its default instead of the power-flags byte.
TEST(config_dev9_body_keeps_its_power_flags) {
    fake_flash_erase_all();
    g_fake_flash_fail = false;
    config_load();
    Config_body b = get_config();
    b.inactive_time = 41;
    set_config(b);
    CHECK(config_save());
    uint8_t *raw = g_fake_flash + CONFIG_FLASH_OFFSET;
    std::memset(raw + offsetof(Config, body) + 49, 0xFF, FLASH_PAGE_SIZE - offsetof(Config, body) - 49);
    raw[offsetof(Config, body) + 49] &= 0x03;  // its power_flags: both options on
    const uint16_t size = 49;
    std::memcpy(raw + offsetof(Config, size), &size, sizeof(size));
    reseal_config_page(size);
    config_default();
    config_load();
    CHECK_EQ(get_config().inactive_time, 41);
    CHECK_EQ(config_power_flags(), 0x03);
    CHECK_EQ(get_config().mic_level, 0);        // follow the PC (default), not 3
    CHECK_EQ(get_config().mic_echo_cancel, 1);  // default
}

// PC sleep options (Config::power_flags, after the body, protocol 1.14).
TEST(config_power_flags_defaults_and_round_trip) {
    fake_flash_erase_all();
    g_fake_flash_fail = false;
    config_load();
    CHECK_EQ(config_power_flags(), POWER_FLAGS_DEFAULT);  // erased flash
    CHECK_EQ(POWER_FLAGS_DEFAULT, POWER_SLEEP_CONTROLLER_OFF);  // upstream: power-off on, wake off
    CHECK(config_save());
    // A page older firmware wrote: the byte after its Config is 0xFF.
    CHECK_EQ(g_fake_flash[CONFIG_FLASH_OFFSET + offsetof(Config, power_flags)], POWER_FLAGS_DEFAULT);
    g_fake_flash[CONFIG_FLASH_OFFSET + offsetof(Config, power_flags)] = 0xFF;  // (the fake flash is RAM)
    config_load();
    CHECK_EQ(config_power_flags(), POWER_FLAGS_DEFAULT);
    // Not part of the body (the 49-byte 1.13 body, CRC'd), right after it.
    CHECK_EQ(offsetof(Config, power_flags), offsetof(Config, body) + sizeof(Config_body));

    // Saved on their own: an unsaved body edit stays unsaved.
    CHECK(config_save());
    Config_body b = get_config();
    b.inactive_time = 44;
    set_config(b);
    CHECK(config_save_power_flags(POWER_PS_WAKES_PC));
    CHECK(config_unsaved());
    config_load();
    CHECK_EQ(config_power_flags(), POWER_PS_WAKES_PC);
    CHECK_EQ(get_config().inactive_time, 30);
    CHECK(!config_save_power_flags(0x80));  // invalid bits
    CHECK_EQ(config_power_flags(), POWER_PS_WAKES_PC);

    // The narrator's own save keeps them, and they count as unsaved changes.
    CHECK(config_save_narrator(2));
    config_load();
    CHECK_EQ(config_power_flags(), POWER_PS_WAKES_PC);
    config_set_power_flags(0x03);
    CHECK(config_unsaved());
    CHECK(config_save());
    CHECK(!config_unsaved());
    config_load();
    CHECK_EQ(config_power_flags(), 0x03);
    config_set_power_flags(0x40);  // out of range -> default
    CHECK_EQ(config_power_flags(), POWER_FLAGS_DEFAULT);

    // A factory reset brings back the default.
    config_set_power_flags(0x02);
    config_default();
    CHECK_EQ(config_power_flags(), POWER_FLAGS_DEFAULT);
}

// Upstream #252: the speaker garbles below an AudioBufferLength of 28, so the
// floor is 32. 16..31 (older firmware or app) read as 32, not as the default;
// out of range and erased flash still take 64; 32..128 stay as they are.
TEST(config_audio_buffer_floor) {
    fake_flash_erase_all();
    config_load();
    CHECK_EQ(get_config().audio_buffer_length, 64);
    Config_body b = get_config();
    const struct { int in, out; } cases[] = {
        {16, 32}, {20, 32}, {27, 32}, {28, 32}, {31, 32}, {32, 32}, {33, 33}, {64, 64},
        {128, 128}, {129, 64}, {15, 64}, {0, 64}, {0xFF, 64},
    };
    // loop: NOT-WORTH — host test, 13 cases
    for (const auto &c : cases) {
        b.audio_buffer_length = (uint8_t) c.in;
        set_config(reinterpret_cast<const uint8_t *>(&b), sizeof(b));
        CHECK_EQ(get_config().audio_buffer_length, c.out);
        CHECK(get_config().audio_buffer_length >= kAudioBufferMin);
    }
    // A config saved by older firmware with 16 loads as 32.
    g_fake_flash_fail = false;
    CHECK(config_save());
    g_fake_flash[CONFIG_FLASH_OFFSET + offsetof(Config, body) + offsetof(Config_body, audio_buffer_length)] = 16;
    reseal_config_page(sizeof(Config_body));
    config_default();
    config_load();
    CHECK_EQ(get_config().audio_buffer_length, 32);
}
