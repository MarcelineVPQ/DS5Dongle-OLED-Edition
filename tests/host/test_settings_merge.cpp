// The OLED Settings save (src/settings_merge.h) and Reset to defaults
// (config_default_keep_slot, shared with 0xF6 0x09): fields the screen doesn't
// edit keep their live values, and a reset keeps the current slot.
#include <cstddef>
#include <cstring>
#include "cmd.h"
#include "config.h"
#include "fakes.h"
#include "settings_merge.h"
#include "test.h"

#define FIELD(f) settings_merge::Field{static_cast<uint8_t>(offsetof(Config_body, f)), sizeof(Config_body::f)}

TEST(settings_save_keeps_fields_changed_elsewhere) {
    fake_flash_erase_all();
    g_fake_flash_fail = false;
    config_load();
    // The Settings screen's copy, taken on its first visit.
    const Config_body local_at_visit = get_config();

    // Since then: the Lightbar screen, a slot switch, KEY1 brightness and the
    // narrator changed the live config (none bumps the host generation).
    Config_body live = get_config();
    live.lightbar_mode = 3;
    for (int i = 0; i < 4; i++) {
        live.lb_fav_r[i] = (uint8_t) (10 + i);
        live.lb_fav_g[i] = (uint8_t) (20 + i);
        live.lb_fav_b[i] = (uint8_t) (30 + i);
    }
    live.current_slot = 2;
    live.screen_brightness = 2;
    live.narrator_voice = 4;
    set_config(live);

    // The user edits two settings on the screen and saves.
    Config_body local = local_at_visit;
    local.haptics_gain = 3.5f;
    local.inactive_time = 45;
    local.mic_echo_cancel = 0;
    const settings_merge::Field fields[] = {FIELD(haptics_gain), FIELD(inactive_time), FIELD(mic_echo_cancel),
                                            FIELD(speaker_volume), FIELD(polling_rate_mode)};
    set_config(settings_merge::apply(get_config(), local, fields, sizeof(fields) / sizeof(fields[0])));
    CHECK(config_save());

    config_load();  // what the next boot reads
    const Config_body &c = get_config();
    CHECK(c.haptics_gain == 3.5f);
    CHECK_EQ(c.inactive_time, 45);
    CHECK_EQ(c.mic_echo_cancel, 0);
    CHECK(c.speaker_volume == local_at_visit.speaker_volume);
    CHECK_EQ(c.lightbar_mode, 3);
    CHECK_EQ(c.lb_fav_r[3], 13);
    CHECK_EQ(c.lb_fav_g[0], 20);
    CHECK_EQ(c.lb_fav_b[2], 32);
    CHECK_EQ(c.current_slot, 2);
    CHECK_EQ(c.screen_brightness, 2);
    CHECK_EQ(c.narrator_voice, 4);
}

TEST(settings_merge_ignores_a_field_past_the_body) {
    Config_body live{}, local{};
    std::memset(&live, 0x11, sizeof(live));
    std::memset(&local, 0x22, sizeof(local));
    const settings_merge::Field bad[] = {{static_cast<uint8_t>(sizeof(Config_body) - 1), 4}};
    const Config_body out = settings_merge::apply(live, local, bad, 1);
    CHECK(std::memcmp(&out, &live, sizeof(out)) == 0);
}

TEST(reset_to_defaults_keeps_the_current_slot) {
    fake_flash_erase_all();
    g_fake_flash_fail = false;
    config_load();
    Config_body b = get_config();
    b.current_slot = 3;
    b.inactive_time = 50;
    b.lightbar_mode = 1;
    set_config(b);
    config_default_keep_slot(3);
    CHECK_EQ(get_config().current_slot, 3);
    CHECK_EQ(get_config().inactive_time, 30);
    CHECK_EQ(get_config().lightbar_mode, 8);
    config_default_keep_slot(9);  // out of range reads as 0, as on load
    CHECK_EQ(get_config().current_slot, 0);
}

TEST(cmd_factory_reset_keeps_the_current_slot) {
    fake_flash_erase_all();
    g_fake_flash_fail = false;
    g_fake_bt_connected = true;
    config_load();
    g_fake_slot = 2;
    Config_body b = get_config();
    b.current_slot = 2;
    b.inactive_time = 50;
    set_config(b);
    const uint8_t msg[] = {0x09};
    pico_cmd_set(PICO_CMD_SET, msg, sizeof(msg));
    for (int i = 0; i < 3; i++) {
        g_fake_time_us += 100'000;
        cmd_task();
    }
    CHECK_EQ(get_config().current_slot, 2);
    CHECK_EQ(get_config().inactive_time, 30);
    CHECK(!config_unsaved());
    g_fake_slot = 0;
}
