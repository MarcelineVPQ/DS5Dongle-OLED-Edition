// Protocol 1.21 (DESIGN_FIRMWARE_IDENTITY §1.1, §7): the 0xEA firmware
// identity byte by byte, identity [49] bits 2 and 3, the flash checks behind
// it (SFDP density, the saved-state lock, the chip package), the display
// choice (Config::display_id: migration, saving, 0xF6 0x1B, "none" leaves the
// keys alone) and the radio-missing stop's boot count.
#include <cstddef>
#include <cstring>
#include "cmd.h"
#include "config.h"
#include "display_table.h"
#include "fakes.h"
#include "flash_layout.h"
#include "fw_identity.h"
#include "radio_boot.h"
#include "remap.h"
#include "calib.h"
#include "test.h"
#include "utils.h"

namespace {
void fresh() {
    fake_flash_erase_all();
    g_fake_flash_fail = false;
    g_fake_bt_connected = true;
    g_fake_display_active = display::kPicoOled13;
    g_fake_flash_measured = 0x0A;
    g_fake_chip = fwid::kChipRp2350A;
    config_load();
    remap_load();
    calib_load();
}
uint8_t last_result() {
    uint8_t id[20];
    pico_cmd_get(PICO_CMD_IDENTITY, id, sizeof(id));
    return id[11];
}
void settle() {
    // loop: NOT-WORTH — host test
    for (int i = 0; i < 3; i++) {
        g_fake_time_us += 100'000;
        cmd_task();
    }
}
}  // namespace

TEST(fwid_identity_announces_0xea_in_byte_49) {
    fresh();
    uint8_t id[63]{};
    CHECK_EQ(pico_cmd_get(PICO_CMD_IDENTITY, id, sizeof(id)), 58);
    CHECK_EQ(id[4], 1);
    CHECK_EQ(id[5], 22);
    CHECK_EQ(id[49], 0x7F);  // board id, identify, 0xEA (bit2), display choice (bit3), 1.22's bits 4..6
}

TEST(fwid_0xea_every_byte) {
    fresh();
    uint8_t r[63];
    std::memset(r, 0xCC, sizeof(r));
    CHECK_EQ(pico_cmd_get(PICO_CMD_FW_IDENTITY, r, sizeof(r)), 14);
    CHECK_EQ(r[0], 'F');
    CHECK_EQ(r[1], 'I');
    CHECK_EQ(r[2], 1);     // layout version
    CHECK_EQ(r[3], 1);     // board type pico2_w (host build: DS5_BOARD_TYPE_ID=1), u16 LE
    CHECK_EQ(r[4], 0);
    CHECK_EQ(r[5], 0);     // DualSense
    CHECK_EQ(r[6], 0x01);  // personas: DualSense only
    CHECK_EQ(r[7], 0x0A);  // built for 4 MB (the stub's PICO_FLASH_SIZE_BYTES)
    CHECK_EQ(r[8], 0x0A);  // measured 4 MB
    CHECK_EQ(r[9], 1);     // RP2350A
    CHECK_EQ(r[10], 0x01 | 0x04);  // radio found, saved state on, display running
    CHECK_EQ(r[11], 1);    // Pico-OLED-1.3 active
    CHECK_EQ(r[12], 0x03); // none + Pico-OLED-1.3
    CHECK_EQ(r[13], 1);    // saved choice: Pico-OLED-1.3 (erased flash)
    CHECK_EQ(r[14], 0xCC); // nothing written past the layout

    // A short request gets nothing (and writes nothing).
    std::memset(r, 0xCC, sizeof(r));
    CHECK_EQ(pico_cmd_get(PICO_CMD_FW_IDENTITY, r, 13), 0);
    CHECK_EQ(r[0], 0xCC);
    CHECK_EQ(pico_cmd_get(PICO_CMD_FW_IDENTITY, r, 14), 14);
}

TEST(fwid_0xea_small_flash_and_no_display) {
    fresh();
    g_fake_flash_measured = 0x09;  // a 2 MB chip under a 4 MB image
    g_fake_display_active = display::kNone;
    g_fake_chip = fwid::kChipRp2350B;
    uint8_t r[14];
    CHECK_EQ(pico_cmd_get(PICO_CMD_FW_IDENTITY, r, sizeof(r)), 14);
    CHECK_EQ(r[8], 0x09);
    CHECK_EQ(r[9], 2);
    CHECK_EQ(r[10], 0x01 | 0x02);  // radio, saved state off, no display driver
    CHECK_EQ(r[11], 0);
    g_fake_flash_measured = 0;  // unknown: saved state stays on
    CHECK_EQ(pico_cmd_get(PICO_CMD_FW_IDENTITY, r, sizeof(r)), 14);
    CHECK_EQ(r[8], 0);
    CHECK_EQ(r[10] & 0x02, 0);
}

// The layout's positions with every field distinct (board type above 255
// checks the byte order).
TEST(fwid_write_positions) {
    const fwid::Facts f{0x0302, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    uint8_t r[16]{};
    CHECK_EQ(fwid::write(f, r, sizeof(r)), 14);
    const uint8_t want[14] = {'F', 'I', 1, 0x02, 0x03, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    CHECK(std::memcmp(r, want, sizeof(want)) == 0);
    CHECK_EQ(r[14], 0);
}

TEST(fwid_flash_codes_and_sfdp) {
    CHECK_EQ(fwid::flash_code(2u << 20), 0x9);
    CHECK_EQ(fwid::flash_code(4u << 20), 0xA);
    CHECK_EQ(fwid::flash_code(16u << 20), 0xC);
    CHECK_EQ(fwid::flash_code(3u << 20), 0);  // not a power of two
    CHECK_EQ(fwid::flash_code(4096), 0);      // 4 KiB would read as "unknown"
    CHECK_EQ(fwid::flash_code(0), 0);

    // A JESD216 header and the basic table's parameter header (id 0x00/0xFF,
    // 16 DWORDs at 0x000080), as Winbond's W25Q parts publish them.
    const uint8_t hdr[16] = {'S', 'F', 'D', 'P', 0x06, 0x01, 0x00, 0xFF, 0x00, 0x06, 0x01, 0x10, 0x80, 0x00, 0x00, 0xFF};
    uint32_t ptr = 0;
    CHECK(fwid::sfdp_bfpt_ptr(hdr, ptr));
    CHECK_EQ(ptr, 0x80u);
    uint8_t bad[16];
    std::memcpy(bad, hdr, 16);
    bad[0] = 0xFF;  // no SFDP (a blank answer)
    CHECK(!fwid::sfdp_bfpt_ptr(bad, ptr));
    std::memcpy(bad, hdr, 16);
    bad[8] = 0x84;  // the first table isn't the basic one
    CHECK(!fwid::sfdp_bfpt_ptr(bad, ptr));
    std::memcpy(bad, hdr, 16);
    bad[11] = 1;  // too short to hold the density
    CHECK(!fwid::sfdp_bfpt_ptr(bad, ptr));

    // Density, bit 31 clear: bits - 1.
    CHECK_EQ(fwid::sfdp_density_code(0x00FFFFFF), 0x9);  // 16 Mbit = 2 MB
    CHECK_EQ(fwid::sfdp_density_code(0x01FFFFFF), 0xA);  // 32 Mbit = 4 MB
    CHECK_EQ(fwid::sfdp_density_code(0x07FFFFFF), 0xC);  // 128 Mbit = 16 MB
    // Bit 31 set: 2^N bits.
    CHECK_EQ(fwid::sfdp_density_code(0x80000019), 0xA);  // 2^25 bits = 4 MB
    CHECK_EQ(fwid::sfdp_density_code(0x80000002), 0);    // nonsense: unknown
    CHECK_EQ(fwid::sfdp_density_code(0xFFFFFFFF), 0);    // blank answer: unknown
    CHECK_EQ(fwid::sfdp_density_code(0x00000000), 0);
}

TEST(fwid_saved_state_lock) {
    CHECK(fwid::saved_state_off(0xA, 0x9));   // 2 MB chip, 4 MB image: locked
    CHECK(!fwid::saved_state_off(0xA, 0xA));  // as built
    CHECK(!fwid::saved_state_off(0xA, 0xC));  // bigger is fine
    CHECK(!fwid::saved_state_off(0xA, 0));    // unknown: stays on
}

TEST(fwid_rp2350_package) {
    // A3 and later: SYSINFO_PACKAGE_SEL (1 = QFN60).
    CHECK_EQ(fwid::rp2350_package(3, 1, 0), fwid::kChipRp2350A);
    CHECK_EQ(fwid::rp2350_package(4, 0, 0), fwid::kChipRp2350B);
    CHECK_EQ(fwid::rp2350_package(3, 0, 30), fwid::kChipRp2350B);  // OTP not used after A2
    // A2: OTP NUM_GPIOS, whatever the package select says.
    CHECK_EQ(fwid::rp2350_package(2, 0, 30), fwid::kChipRp2350A);
    CHECK_EQ(fwid::rp2350_package(2, 1, 48), fwid::kChipRp2350B);
    CHECK_EQ(fwid::rp2350_package(2, 1, 0), fwid::kChipUnknown);
}

TEST(fwid_display_table) {
    CHECK_EQ(display::resolve(0xFF), display::kPicoOled13);  // firmware before 1.21
    CHECK_EQ(display::resolve(0), display::kNone);
    CHECK_EQ(display::resolve(1), display::kPicoOled13);
    CHECK_EQ(display::resolve(7), display::kPicoOled13);     // a driver this image hasn't
    CHECK(display::is_supported(0) && display::is_supported(1) && !display::is_supported(2) && !display::is_supported(0xFF));
    CHECK(!display::has_panel(display::kNone));
    CHECK(display::has_panel(display::kPicoOled13));
    // "none": a key pin left at reset reads low, which must not be a press
    // (both low = the KEY0 + KEY1 reboot chord).
    CHECK(!display::is_key_down(display::kNone, false));
    CHECK(!display::is_key_down(display::kNone, true));
    CHECK(display::is_key_down(display::kPicoOled13, false));
    CHECK(!display::is_key_down(display::kPicoOled13, true));
}

// Config::display_id: after the power flags, outside the body and its CRC.
TEST(fwid_config_display_migration_and_save) {
    fresh();
    CHECK_EQ(config_display_id(), display::kPicoOled13);  // erased flash
    CHECK_EQ(offsetof(Config, display_id), offsetof(Config, power_flags) + 1);
    CHECK_EQ(sizeof(Config_body), 53u);  // the display choice isn't in the body (protocol 1.22 grew it, not 1.21)
    CHECK(config_save());
    CHECK(!config_unsaved());

    // A page saved by firmware before 1.21: 0xFF there. Reads as the
    // Pico-OLED-1.3, and isn't an unsaved change.
    g_fake_flash[CONFIG_FLASH_OFFSET + offsetof(Config, display_id)] = 0xFF;
    config_load();
    CHECK_EQ(config_display_id(), display::kPicoOled13);
    CHECK(!config_unsaved());

    // Saved on its own: an unsaved body edit stays unsaved, the choice survives a reload.
    Config_body b = get_config();
    b.inactive_time = 44;
    set_config(b);
    CHECK(config_save_display_id(display::kNone));
    CHECK(config_unsaved());
    config_load();
    CHECK_EQ(config_display_id(), display::kNone);
    CHECK_EQ(get_config().inactive_time, 30);
    CHECK(!config_save_display_id(2));  // no driver in this image
    CHECK_EQ(config_display_id(), display::kNone);

    // The other own-savers and a factory reset keep it (it says what's fitted).
    CHECK(config_save_narrator(2));
    CHECK(config_save_power_flags(0x03));
    config_load();
    CHECK_EQ(config_display_id(), display::kNone);
    config_default_keep_slot(0);
    CHECK(config_save());
    config_load();
    CHECK_EQ(config_display_id(), display::kNone);

    // A failed write says so.
    g_fake_flash_fail = true;
    CHECK(!config_save_display_id(display::kPicoOled13));
    g_fake_flash_fail = false;
}

// 0.9.5-dev.9's 49-byte body kept its power flags right after it; the byte
// after those is 0xFF, so the display takes the default.
TEST(fwid_config_short_body_display_default) {
    fresh();
    CHECK(config_save());
    uint8_t *raw = g_fake_flash + CONFIG_FLASH_OFFSET;
    std::memset(raw + offsetof(Config, body) + 49, 0xFF, FLASH_PAGE_SIZE - offsetof(Config, body) - 49);
    raw[offsetof(Config, body) + 49] = 0x03;
    const uint16_t size = 49;
    std::memcpy(raw + offsetof(Config, size), &size, sizeof(size));
    const uint32_t crc = crc32(raw + offsetof(Config, body), size);  // what that firmware sealed
    std::memcpy(raw + offsetof(Config, crc32), &crc, sizeof(crc));
    config_load();
    CHECK_EQ(config_power_flags(), 0x03);
    CHECK_EQ(config_display_id(), display::kPicoOled13);
}

TEST(fwid_cmd_display_choice) {
    fresh();
    const uint8_t none[] = {0x1B, 0};
    pico_cmd_set(PICO_CMD_SET, none, sizeof(none));
    CHECK_EQ(last_result(), CMD_RESULT_PENDING);
    settle();
    CHECK_EQ(last_result(), CMD_RESULT_OK);
    uint8_t r[14];
    CHECK_EQ(pico_cmd_get(PICO_CMD_FW_IDENTITY, r, sizeof(r)), 14);
    CHECK_EQ(r[11], 1);  // still driving the Pico-OLED-1.3 until the next start
    CHECK_EQ(r[13], 0);  // saved: none
    config_load();
    CHECK_EQ(config_display_id(), display::kNone);

    const uint8_t bad[] = {0x1B, 2};
    pico_cmd_set(PICO_CMD_SET, bad, sizeof(bad));
    CHECK_EQ(last_result(), CMD_RESULT_INVALID);
    const uint8_t missing[] = {0x1B};
    pico_cmd_set(PICO_CMD_SET, missing, sizeof(missing));
    CHECK_EQ(last_result(), CMD_RESULT_INVALID);

    g_fake_flash_fail = true;
    const uint8_t oled[] = {0x1B, 1};
    pico_cmd_set(PICO_CMD_SET, oled, sizeof(oled));
    settle();
    CHECK_EQ(last_result(), CMD_RESULT_FAILED);
    g_fake_flash_fail = false;
}

TEST(fwid_radio_boot_stops_on_the_third_failed_start) {
    uint32_t s = 0;  // power-on
    s = radio_boot::after_failure(s);
    CHECK(!radio_boot::should_stop(s));
    s = radio_boot::after_failure(s);
    CHECK(!radio_boot::should_stop(s));
    s = radio_boot::after_failure(s);
    CHECK(radio_boot::should_stop(s));
    CHECK_EQ(radio_boot::failures(s), 3);
    // Anything else in the register (not our magic) counts from zero.
    CHECK_EQ(radio_boot::failures(0x12345603u), 0);
    CHECK_EQ(radio_boot::failures(radio_boot::after_failure(0x12345603u)), 1);
    // The count saturates instead of wrapping.
    CHECK_EQ(radio_boot::failures(radio_boot::after_failure(radio_boot::kMagic | 0xFF)), 0xFF);
}
