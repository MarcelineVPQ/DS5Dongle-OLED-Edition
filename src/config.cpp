//
// Created by awalol on 2026/5/4.
//

#include "config.h"

#include <cmath>
#include <cstddef>
#include <cstring>

#include "utils.h"
#include "status_gpio.h"
#include "state_override.h"
#include "hardware/flash.h"
#include "flash_safe.h"
#include "flash_layout.h"
#include "pico/btstack_flash_bank.h"
#include "hardware/sync.h"
#include "pico/cyw43_arch.h"

constexpr uint32_t CONFIG_MAGIC = 0x66ccff00;
constexpr uint16_t CONFIG_VERSION = 1;
static Config config{};
bool is_dse = false;

// 编译期保护
// 判断Config结构体是否能放进flash 256bytes
static_assert(sizeof(Config) <= FLASH_PAGE_SIZE);
// 配置区起始地址必须按 flash sector 对齐。
static_assert(CONFIG_FLASH_OFFSET % FLASH_SECTOR_SIZE == 0);

uint32_t calc_config_crc(const Config &con) {
    return crc32(reinterpret_cast<const uint8_t *>(&con.body), sizeof(Config_body));
}

// The start of saved state in flash, as an address, for tools/check_flash_reserved.py
// (flash_layout.h). Nothing reads it: CMakeLists.txt keeps it in the image.
extern "C" __attribute__((used)) const uint32_t ds5_reserved_flash_start = XIP_BASE + RESERVED_FLASH_OFFSET;

const Config *flash_config() {
    return reinterpret_cast<const Config *>(XIP_BASE + CONFIG_FLASH_OFFSET);
}

void config_valid() {
    // valid config and set default value
    // error: LOGGED-OK — printf; a blank or old sector gets defaults; RAM then differs from flash, which the app shows as unsaved settings (identity [19] bit0)
    if (config.magic != CONFIG_MAGIC) {
        config.magic = CONFIG_MAGIC;
        printf("[Config] Config Magic Header is invalid\n");
    }
    if (config.version != CONFIG_VERSION) {
        config.version = CONFIG_VERSION;
        printf("[Config] Config Version is invalid\n");
    }
    if (config.size != sizeof(Config_body)) {
        config.size = sizeof(Config_body);
        printf("[Config] Config Body size is invalid\n");
    }
    auto body = &config.body;
    if (std::isnan(body->haptics_gain) || body->haptics_gain < 1.0f || body->haptics_gain > 6.0f) {
        body->haptics_gain = 1.0f;
        printf("[Config] Haptics Gain value is invalid\n");
    }
    if (std::isnan(body->haptics_gain_right) || body->haptics_gain_right < 1.0f || body->haptics_gain_right > 6.0f) {
        body->haptics_gain_right = body->haptics_gain;
    }
    if (std::isnan(body->speaker_volume) || body->speaker_volume < -100 || body->speaker_volume > 12) {
        body->speaker_volume = 0;  // OLED Edition: 0 dB default (unity); -100 would be silent
        printf("[Config] Speaker Volume is invalid, defaulting to 0 dB\n");
    }
    if (body->inactive_time < 5 || body->inactive_time > 60) {
        body->inactive_time = 30;
        printf("[Config] Inactive time is invalid\n");
    }
    if (body->disable_inactive_disconnect > 1) {
        body->disable_inactive_disconnect = 0;
        printf("[Config] disable_auto_disconnect is invalid\n");
    }
    if (body->disable_pico_led > 1) {
        body->disable_pico_led = 0;
        printf("[Config] disable_pico_led is invalid\n");
    }
    if (body->polling_rate_mode > 2) {
        // Fresh / erased config: 500 Hz (upstream 501d435). A saved rate is
        // valid and never reaches this line, so nobody's choice changes.
        body->polling_rate_mode = 1;
        printf("[Config] polling_rate_mode is invalid\n");
    }
    if (body->audio_buffer_length < 16 || body->audio_buffer_length > kAudioBufferMax) {
        body->audio_buffer_length = 64;
        printf("[Config] haptics_buffer_length is invalid\n");
    } else if (body->audio_buffer_length < kAudioBufferMin) {
        // Upstream #252: below 28 the speaker garbles; keep the user's low
        // latency as close as works instead of jumping back to 64.
        body->audio_buffer_length = kAudioBufferMin;
    }
    if (body->controller_mode > 2) {
        body->controller_mode = 2;
        printf("[Config] controller_mode is invalid\n");
    }
    if (body->current_slot >= 4) {
        body->current_slot = 0;
        printf("[Config] current_slot is invalid\n");
    }
    if (body->auto_haptics_enable > 3) {
        body->auto_haptics_enable = 0; // Off: only vibrate when the game sends haptics
        printf("[Config] auto_haptics_enable invalid, defaulting to 0 (Off)\n");
    }
    if (body->auto_haptics_gain > 200) {
        body->auto_haptics_gain = 100;
        printf("[Config] auto_haptics_gain invalid, defaulting to 100\n");
    }
    if (body->auto_haptics_lowpass > 3) {
        body->auto_haptics_lowpass = 1; // 160 Hz
        printf("[Config] auto_haptics_lowpass invalid, defaulting to 1 (160 Hz)\n");
    }
    if (body->lightbar_mode > 8) { // 0..7 OLED modes + 8 = HOST passthrough (default)
        body->lightbar_mode = 8;
        printf("[Config] lightbar_mode invalid, defaulting to 8 (HOST passthrough)\n");
    }
    // lb_fav_{r,g,b} need no validation — any 0..255 is a legal color, and an
    // erased flash sector (0xFF) yields 4 white favorites, a usable default.
    if (body->screen_dim_timeout > 250) { // 0xFF erased / out of range → default
        body->screen_dim_timeout = 2;     // mirrors the original 2-min dim tier
        printf("[Config] screen_dim_timeout invalid, defaulting to 2 min\n");
    }
    if (body->screen_off_timeout > 250) {
        body->screen_off_timeout = 15;    // mirrors the original 15-min off tier
        printf("[Config] screen_off_timeout invalid, defaulting to 15 min\n");
    }
    if (body->bt_mic_enable > 1) {        // 0xFF erased / upgrade → default ON
        body->bt_mic_enable = 1;
        printf("[Config] bt_mic_enable invalid, defaulting to 1 (on)\n");
    }
    if (body->screen_brightness > 3) {    // kBrightLevels has 4 entries (0..3)
        body->screen_brightness = 0;      // full brightness
        printf("[Config] screen_brightness invalid, defaulting to 0 (full)\n");
    }
    if (body->controller_wakes_display > 1) { // 0xFF erased / upgrade → default ON
        body->controller_wakes_display = 1;
        printf("[Config] controller_wakes_display invalid, defaulting to 1 (on)\n");
    }
    if (body->narrator_voice < 1 || body->narrator_voice > 7) body->narrator_voice = 0xFF; // off
    // Protocol 1.13. 0xFF (erased flash, a body from older firmware) is out of
    // range for each, so they start at their defaults.
    if (body->trigger_reduce > 10) body->trigger_reduce = 0;   // the game's own strength
    if (body->speaker_gain > 7) body->speaker_gain = 0;         // the game's own
    if (body->lock_volume > 1) body->lock_volume = 0;
    if (body->mic_select > 2) body->mic_select = 0;             // auto
    if (body->speaker_select > 3) body->speaker_select = 0;     // auto
    if (!status_gpio_pin_valid(body->status_gpio_pin)) body->status_gpio_pin = kStatusGpioOff;
    if (body->status_gpio_mode > 1) body->status_gpio_mode = 0;
    if (body->mic_level > kMicLevelMax) body->mic_level = 0;    // protocol 1.16: follow the PC
    if (body->mic_echo_cancel > 1) body->mic_echo_cancel = 1;   // protocol 1.17: on
    if (config.power_flags & ~POWER_FLAGS_MASK) config.power_flags = POWER_FLAGS_DEFAULT; // 0xFF erased / older firmware
    if (body->config_version != CONFIG_VERSION) {
        body->config_version = CONFIG_VERSION;
        printf("[Config] Warning: Config may breaking change\n");
    }
}

// True when the settings in RAM differ from the ones saved in flash (or
// nothing valid is saved yet). Core 0 only, like every flash write, so the
// XIP read never races a write.
bool config_unsaved() {
    const Config *saved = flash_config();
    // error: NOT-WORTH — nothing; reads which copy to start from (no valid save yet = the RAM copy), not a failure
    return saved->magic != CONFIG_MAGIC || memcmp(&saved->body, &config.body, sizeof(Config_body)) != 0 ||
           saved->power_flags != config.power_flags;
}

// A saved page is intact when its magic is ours and its CRC matches the body
// its writer saved: `size` bytes (a body saved by older firmware is shorter,
// its CRC covers only its own length, and the flash after it reads 0xFF).
static bool config_page_intact(const Config *page) {
    // error: NOT-WORTH — nothing; reads which copy to start from (blank or another firmware's sector), not a failure
    if (page->magic != CONFIG_MAGIC) return false;
    // error: LOGGED-OK — the caller prints it and loads defaults (shown as unsaved settings in the app); a size past the page can't be ours
    if (page->size == 0 || page->size > FLASH_PAGE_SIZE - offsetof(Config, body)) return false;
    // error: LOGGED-OK — the caller prints a CRC mismatch and loads defaults (shown as unsaved settings in the app)
    return page->crc32 == crc32(reinterpret_cast<const uint8_t *>(page) + offsetof(Config, body), page->size);
}

// The sector config used before 31055a1 (the last one). On the Pico W that is
// inside BTstack's link-key bank (the last two sectors there), so it's never
// ours to erase; on the Pico 2 W the bank sits below it.
static constexpr bool kLegacyOutsideBank =
    LEGACY_CONFIG_FLASH_OFFSET >= PICO_FLASH_BANK_STORAGE_OFFSET + PICO_FLASH_BANK_TOTAL_SIZE ||
    LEGACY_CONFIG_FLASH_OFFSET + FLASH_SECTOR_SIZE <= PICO_FLASH_BANK_STORAGE_OFFSET;

static const Config *legacy_config() {
    return reinterpret_cast<const Config *>(XIP_BASE + LEGACY_CONFIG_FLASH_OFFSET);
}

// Erase the legacy copy once the new sector holds a verified one, so a later
// bad new copy (power lost during a save) can't bring the old settings back.
static void invalidate_legacy_config() {
    // error: NOT-WORTH — nothing; on the Pico W the legacy sector is BTstack's, never erased (compile-time)
    if (!kLegacyOutsideBank) return;
    alignas(4) uint8_t blank[FLASH_PAGE_SIZE];
    memset(blank, 0xff, sizeof(blank));
    // error: LOGGED-OK — printf; the legacy copy stays intact and the next boot erases it again (the new copy is the one loaded either way)
    if (!flash_sector_write(LEGACY_CONFIG_FLASH_OFFSET, blank)) printf("[Config] Legacy sector erase skipped\n");
}

void config_load() {
    // error: LOGGED-OK — checked below: a bad copy prints and loads defaults (or migrates), shown as unsaved settings in the app
    const bool intact = config_page_intact(flash_config());
    memcpy(&config, flash_config(), sizeof(Config));
    // error: NOT-WORTH — nothing; a bad or erased legacy copy is simply not migrated (the usual case)
    const bool legacy_intact = config_page_intact(legacy_config());

    if (!intact) {
        // error: LOGGED-OK — printf; a torn or corrupt save loads defaults (below), shown as unsaved settings in the app
        if (config.magic == CONFIG_MAGIC) printf("[Config] Saved config fails its CRC\n");
        // First boot after the sector move: carry saved settings over from the
        // old last-sector location if they're intact (magic + CRC). The legacy
        // copy is erased only after the new one is written and verified, so a
        // power loss at any point leaves one intact copy: an interrupted save
        // migrates again on the next boot.
        // error: LOGGED-OK — printf on migration; a bad legacy copy is ignored (defaults, shown as unsaved settings)
        if (legacy_intact) {
            printf("[Config] Migrating config from the legacy sector\n");
            memcpy(&config, legacy_config(), sizeof(Config));
            config_valid();
            // error: LOGGED-OK — printf; boot-time init save of the migrated settings: the RAM copy works now and the next boot retries the migration
            if (config_save()) invalidate_legacy_config();
            return;
        }
        // Blank, another firmware's or a corrupt sector: every setting at its
        // default (as erased flash), not whatever in-range bytes it holds.
        memset(&config.body, 0xFF, sizeof(config.body));
        config.power_flags = 0xFF;
    } else if (legacy_intact) {
        // A migration that lost power after the new copy was saved: finish it.
        invalidate_legacy_config();
    }

    config_valid();
}

// Reset RAM-resident config body to firmware defaults. Caller must
// config_save() to persist. Filling with 0xFF mirrors the byte pattern
// of a freshly-erased flash sector, so every field fails validity and
// gets re-initialized to its documented default by config_valid().
void config_default() {
    memset(&config.body, 0xFF, sizeof(config.body));
    config.power_flags = 0xFF;
    config_valid();
}

void config_default_keep_slot(uint8_t slot) {
    config_default();
    config.body.current_slot = slot;
    config_valid();  // an out-of-range slot reads as 0, as on load
}

static bool write_config(Config &con) {
    con.crc32 = calc_config_crc(con);
    alignas(4) uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xff, sizeof(page));
    memcpy(page, &con, sizeof(Config));

    // error: SHOWN — passed up; the callers report it (0xF1 CMD_RESULT_FAILED to the app, the OLED's "Save FAIL")
    if (!flash_sector_write(CONFIG_FLASH_OFFSET, page)) {
        printf("[Flash] core1 lockout timed out, write skipped\n");
        // error: SHOWN — passed up; the callers report it (0xF1 CMD_RESULT_FAILED to the app, the OLED's "Save FAIL")
        return false;
    }

    Config verify{};
    memcpy(&verify, flash_config(), sizeof(verify));
    const auto verify_crc32 = calc_config_crc(verify);
    // error: SHOWN — passed up; the callers report it (0xF1 CMD_RESULT_FAILED to the app, the OLED's "Save FAIL")
    if (verify_crc32 == con.crc32) {
        printf("[Config] Config write flash verify success\n");
        return true;
    }
    printf("[Config] Config write flash verify failed\n");
    // error: SHOWN — passed up; the callers report it (0xF1 CMD_RESULT_FAILED to the app, the OLED's "Save FAIL")
    return false;
}

// error: SHOWN — passed up; the callers report it (0xF1 CMD_RESULT_FAILED to the app, the OLED's "Save FAIL")
bool config_save() { return write_config(config); }

bool config_save_narrator(uint8_t value) {
    config.body.narrator_voice = value;
    config_valid();
    // Flash's copy with only this byte changed; nothing valid saved yet = RAM.
    const Config *saved = flash_config();
    // error: NOT-WORTH — nothing; reads which copy to start from (no valid save yet = the RAM copy), not a failure
    Config page = saved->magic == CONFIG_MAGIC ? *saved : config;
    page.magic = CONFIG_MAGIC;
    page.version = CONFIG_VERSION;
    page.size = sizeof(Config_body);  // an older, shorter body grows here
    page.body.narrator_voice = config.body.narrator_voice;
    if (page.power_flags & ~POWER_FLAGS_MASK) page.power_flags = POWER_FLAGS_DEFAULT;
    // error: SHOWN — passed up; the callers report it (0xF1 CMD_RESULT_FAILED to the app, the OLED's "Save FAIL")
    return write_config(page);
}

uint8_t config_power_flags() { return config.power_flags; }

void config_set_power_flags(uint8_t flags) {
    config.power_flags = flags;
    config_valid();
}

bool config_save_power_flags(uint8_t flags) {
    // error: SHOWN — CMD_RESULT_FAILED in 0xF1 (0x17 also checks the bits first and answers INVALID)
    if (flags & ~POWER_FLAGS_MASK) return false;
    config.power_flags = flags;
    // Flash's copy with only these bits changed; nothing valid saved yet = RAM.
    const Config *saved = flash_config();
    // error: NOT-WORTH — nothing; reads which copy to start from (no valid save yet = the RAM copy), not a failure
    Config page = saved->magic == CONFIG_MAGIC ? *saved : config;
    page.magic = CONFIG_MAGIC;
    page.version = CONFIG_VERSION;
    page.size = sizeof(Config_body);
    page.power_flags = flags;
    // error: SHOWN — passed up; the callers report it (0xF1 CMD_RESULT_FAILED to the app, the OLED's "Save FAIL")
    return write_config(page);
}

const Config_body& get_config() {
    return config.body;
}

void set_config(const uint8_t *new_config, const uint16_t len) {
    const auto copy_len = len < sizeof(Config_body) ? len : sizeof(Config_body);
    // The narrator has its own command (0x0B): a settings write never changes
    // it, so a body long enough to reach the fields after it is safe to send.
    const uint8_t narrator = config.body.narrator_voice;
    memcpy(&config.body, new_config, copy_len);
    config.body.narrator_voice = narrator;
    config_valid();
    if (config.body.disable_pico_led) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
    }else {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
    }
}

void set_config(const Config_body &new_config) {
    config.body = new_config;
    config_valid();
}
