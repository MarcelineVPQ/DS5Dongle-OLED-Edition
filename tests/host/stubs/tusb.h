// Host-test stub: just the TinyUSB names src/usb_descriptors.cpp uses, with
// TinyUSB 0.21's values (src/common/tusb_types.h, src/device/usbd.h,
// src/class/hid/hid.h), so the configuration descriptor can be checked byte
// for byte on a PC (tests/host/test_usb_descriptors.cpp).
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE 64
#endif

#define TU_BIT(n) (1UL << (n))
#define TU_U16_HIGH(_u16) ((uint8_t) (((uint16_t) (_u16) >> 8) & 0x00ffu))
#define TU_U16_LOW(_u16) ((uint8_t) ((uint16_t) (_u16) & 0x00ffu))
#define U16_TO_U8S_LE(_u16) TU_U16_LOW(_u16), TU_U16_HIGH(_u16)

enum {
    TUSB_DESC_DEVICE = 0x01,
    TUSB_DESC_CONFIGURATION = 0x02,
    TUSB_DESC_STRING = 0x03,
    TUSB_DESC_INTERFACE = 0x04,
    TUSB_DESC_ENDPOINT = 0x05,
    TUSB_DESC_INTERFACE_ASSOCIATION = 0x0B,
};
enum { TUSB_CLASS_HID = 3, TUSB_CLASS_MISC = 0xEF, TUSB_CLASS_VENDOR_SPECIFIC = 0xFF };
enum { MISC_SUBCLASS_COMMON = 2, MISC_PROTOCOL_IAD = 1 };
enum { TUSB_XFER_INTERRUPT = 3 };
enum { TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP = 1u << 5, TUSB_DESC_CONFIG_ATT_SELF_POWERED = 1u << 6 };
enum { HID_SUBCLASS_BOOT = 1 };
enum { HID_ITF_PROTOCOL_NONE = 0, HID_ITF_PROTOCOL_KEYBOARD = 1 };
enum { HID_DESC_TYPE_HID = 0x21, HID_DESC_TYPE_REPORT = 0x22 };

#define TUD_CONFIG_DESC_LEN (9)
#define TUD_CONFIG_DESCRIPTOR(config_num, _itfcount, _stridx, _total_len, _attribute, _power_ma) \
    9, TUSB_DESC_CONFIGURATION, U16_TO_U8S_LE(_total_len), _itfcount, config_num, _stridx, TU_BIT(7) | _attribute, (_power_ma) / 2
#define TUD_HID_DESC_LEN (9 + 9 + 7)
#define TUD_HID_DESCRIPTOR(_itfnum, _stridx, _boot_protocol, _report_desc_len, _epin, _epsize, _ep_interval)              \
    9, TUSB_DESC_INTERFACE, _itfnum, 0, 1, TUSB_CLASS_HID,                                                              \
        (uint8_t) ((_boot_protocol != HID_ITF_PROTOCOL_NONE) ? (uint8_t) HID_SUBCLASS_BOOT : 0u), _boot_protocol, _stridx, \
        9, HID_DESC_TYPE_HID, U16_TO_U8S_LE(0x0111), 0, 1, HID_DESC_TYPE_REPORT, U16_TO_U8S_LE(_report_desc_len), 7,       \
        TUSB_DESC_ENDPOINT, _epin, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(_epsize), _ep_interval

typedef struct __attribute__((packed)) {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
} tusb_desc_device_t;

// fakes.cpp: g_fake_tud_mounted (the host has configured us).
bool tud_mounted();
