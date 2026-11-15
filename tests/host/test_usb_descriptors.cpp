// src/usb_descriptors.cpp: the configuration descriptor with the keys and
// mouse switch (protocol 1.24, docs/TENSION_BUTTONS.md section 5.4).
//
// kBefore is the configuration descriptor of firmware 7df6c50 (before the
// switch existed), for this host build (configurator interface on, no serial),
// taken from that commit's source with its constants resolved
// (wTotalLength 0xEC, 5 interfaces, vendor interface 4, string 4). With the
// switch off the host must get exactly these bytes; the DualSense's 289-byte
// report descriptor is pinned by the static_assert in usb_descriptors.cpp.
#include <cstring>
#include <vector>
#include "config.h"
#include "fakes.h"
#include "test.h"
#include "usb_keys_mouse.h"

uint8_t const *tud_descriptor_configuration_cb(uint8_t index);
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance);

namespace {
const uint8_t kBefore[] = {
    0x09, 0x02, 0xEC, 0x00, 0x05, 0x01, 0x00, 0xC0, 0xFA, 0x09, 0x04, 0x00,
    0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x0A, 0x24, 0x01, 0x00, 0x01, 0x49,
    0x00, 0x02, 0x01, 0x02, 0x0C, 0x24, 0x02, 0x01, 0x01, 0x01, 0x06, 0x04,
    0x33, 0x00, 0x00, 0x00, 0x0C, 0x24, 0x06, 0x02, 0x01, 0x01, 0x03, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x09, 0x24, 0x03, 0x03, 0x01, 0x03, 0x04, 0x02,
    0x00, 0x0C, 0x24, 0x02, 0x04, 0x02, 0x04, 0x03, 0x02, 0x03, 0x00, 0x00,
    0x00, 0x09, 0x24, 0x06, 0x05, 0x04, 0x01, 0x03, 0x00, 0x00, 0x09, 0x24,
    0x03, 0x06, 0x01, 0x01, 0x01, 0x05, 0x00, 0x09, 0x04, 0x01, 0x00, 0x00,
    0x01, 0x02, 0x00, 0x00, 0x09, 0x04, 0x01, 0x01, 0x01, 0x01, 0x02, 0x00,
    0x00, 0x07, 0x24, 0x01, 0x01, 0x01, 0x01, 0x00, 0x0B, 0x24, 0x02, 0x01,
    0x04, 0x02, 0x10, 0x01, 0x80, 0xBB, 0x00, 0x09, 0x05, 0x01, 0x09, 0x88,
    0x01, 0x01, 0x00, 0x00, 0x07, 0x25, 0x01, 0x00, 0x00, 0x00, 0x00, 0x09,
    0x04, 0x02, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x09, 0x04, 0x02, 0x01,
    0x01, 0x01, 0x02, 0x00, 0x00, 0x07, 0x24, 0x01, 0x06, 0x01, 0x01, 0x00,
    0x0B, 0x24, 0x02, 0x01, 0x02, 0x02, 0x10, 0x01, 0x80, 0xBB, 0x00, 0x09,
    0x05, 0x82, 0x05, 0xC4, 0x00, 0x01, 0x00, 0x00, 0x07, 0x25, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x09, 0x04, 0x03, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x21, 0x01, 0x07, 0x05, 0x84,
    0x03, 0x40, 0x00, 0x01, 0x07, 0x05, 0x03, 0x03, 0x40, 0x00, 0x01, 0x09,
    0x04, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x04,
};
static_assert(sizeof(kBefore) == 0xEC, "7df6c50's wTotalLength for this build");

// The bytes the host reads: wTotalLength of them.
std::vector<uint8_t> config_desc() {
    const uint8_t *d = tud_descriptor_configuration_cb(0);
    const uint16_t total = (uint16_t) (d[2] | d[3] << 8);
    return std::vector<uint8_t>(d, d + total);
}

// Settings that make 7df6c50's patches land on the literal's own bytes:
// bInterval 1 (1000 Hz), the DualSense's report length (controller mode DS5),
// no remote wakeup ("PS wakes the PC" off).
void settings(uint8_t keys_mouse) {
    fake_flash_erase_all();
    g_fake_flash_fail = false;
    config_load();
    Config_body b = get_config();
    b.polling_rate_mode = 3;
    b.controller_mode = 0;
    b.keys_mouse = keys_mouse;
    set_config(b);
    config_set_power_flags(POWER_SLEEP_CONTROLLER_OFF);
    g_usb_wake_keyboard = false;
}
}  // namespace

TEST(usb_config_descriptor_off_is_byte_identical_to_before) {
    settings(0);
    g_fake_tud_mounted = false;
    const std::vector<uint8_t> d = config_desc();
    CHECK_EQ(d.size(), sizeof(kBefore));
    CHECK(d == std::vector<uint8_t>(kBefore, kBefore + sizeof(kBefore)));
    g_fake_tud_mounted = true;
    CHECK(!usb_keys_mouse_on());
    g_fake_tud_mounted = false;
}

TEST(usb_config_descriptor_on_appends_the_keys_and_mouse_interface) {
    settings(1);
    g_fake_tud_mounted = false;  // enumerating: the switch is read now
    const std::vector<uint8_t> d = config_desc();
    CHECK_EQ(d.size(), sizeof(kBefore) + 25);
    CHECK_EQ(d[4], 6);  // bNumInterfaces
    // Everything before it unchanged but the two counts.
    CHECK(std::memcmp(d.data() + 5, kBefore + 5, sizeof(kBefore) - 5) == 0);
    CHECK(std::memcmp(d.data(), kBefore, 2) == 0);
    const uint8_t want[25] = {
        0x09, 0x04, 0x05, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,  // interface 5: HID, 1 endpoint, no boot protocol, no string
        0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x81, 0x00,  // HID 1.11, one report descriptor of 129 bytes
        0x07, 0x05, 0x87, 0x03, 0x10, 0x00, 0x01,              // interrupt IN 0x87, 16 bytes, every 1 ms
    };
    CHECK(std::memcmp(d.data() + sizeof(kBefore), want, sizeof(want)) == 0);
    // Its report descriptor is instance 1's; instance 0 stays the DualSense's.
    const uint8_t *km = tud_hid_descriptor_report_cb(KEYS_MOUSE_HID_INSTANCE);
    CHECK(km != tud_hid_descriptor_report_cb(0));
    const uint8_t kbd_head[] = {0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01};
    CHECK(std::memcmp(km, kbd_head, sizeof(kbd_head)) == 0);
    CHECK_EQ(km[128], 0xC0);
    g_fake_tud_mounted = true;  // configured
    CHECK(usb_keys_mouse_on());
    g_usb_wake_keyboard = true;  // the wake keyboard instead: not on the bus
    CHECK(!usb_keys_mouse_on());
    g_usb_wake_keyboard = false;
    g_fake_tud_mounted = false;
}

// A change after enumeration waits for the next connect: a configured host
// never sees the descriptor change under it.
TEST(usb_config_descriptor_switch_waits_for_the_next_connect) {
    settings(0);
    g_fake_tud_mounted = false;
    CHECK_EQ(config_desc().size(), sizeof(kBefore));
    g_fake_tud_mounted = true;
    Config_body b = get_config();
    b.keys_mouse = 1;
    set_config(b);
    CHECK_EQ(config_desc().size(), sizeof(kBefore));  // still off
    CHECK(!usb_keys_mouse_on());
    g_fake_tud_mounted = false;                       // the next connect
    CHECK_EQ(config_desc().size(), sizeof(kBefore) + 25);
    g_fake_tud_mounted = true;
    CHECK(usb_keys_mouse_on());
    b.keys_mouse = 0;  // and off again: the bytes go back to exactly the old ones
    set_config(b);
    g_fake_tud_mounted = false;
    CHECK(config_desc() == std::vector<uint8_t>(kBefore, kBefore + sizeof(kBefore)));
}
