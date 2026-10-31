//
// Configurator vendor interface — see vendor_itf.h for the wire protocol.
//

#include "vendor_itf.h"

#include <cstring>

#include "tusb.h"
#include "device/usbd_pvt.h"
#include "cmd.h"
#include "oled.h"
#include "ctrl_info.h"
#include "usb_power.h"

// {CD1F1B79-799E-44AC-ADFC-A3F2090D1D4A} — the configurator opens the WinUSB
// device by this interface GUID on Windows.
#define VENDOR_ITF_GUID u"{CD1F1B79-799E-44AC-ADFC-A3F2090D1D4A}"

static uint8_t s_buf[64];
// The screen mirror is the one reply longer than a HID-sized report: 1028
// bytes in a single control transfer (EP0 sends it in 64-byte packets).
static uint8_t s_mirror[kOledMirrorBytes];

//--------------------------------------------------------------------+
// Class driver: only claims the interface. TinyUSB routes every VENDOR-type
// request to tud_vendor_control_xfer_cb before recipient dispatch, so the
// driver's own control callback never sees our traffic.
//--------------------------------------------------------------------+

static void vi_init() {}

static bool vi_deinit() { return true; }

static void vi_reset(uint8_t rhport) { (void) rhport; }

static uint16_t vi_open(uint8_t rhport, tusb_desc_interface_t const *desc, uint16_t max_len) {
    (void) rhport;
    TU_VERIFY(desc->bInterfaceClass == TUSB_CLASS_VENDOR_SPECIFIC &&
              desc->bInterfaceNumber == VENDOR_ITF_NUM &&
              desc->bNumEndpoints == 0, 0);
    TU_VERIFY(max_len >= sizeof(tusb_desc_interface_t), 0);
    return sizeof(tusb_desc_interface_t);
}

static bool vi_control(uint8_t rhport, uint8_t stage, tusb_control_request_t const *req) {
    (void) rhport; (void) stage; (void) req;
    // error: NOT-WORTH — USB STALL for a request this interface doesn't take (wrong direction/recipient/id); the app only sends the documented ones and its transport reports a STALL as an error
    return false;
}

static bool vi_xfer(uint8_t rhport, uint8_t ep, xfer_result_t result, uint32_t n) {
    (void) rhport; (void) ep; (void) result; (void) n;
    return true;
}

static usbd_class_driver_t const s_driver = {
    .name = "DS5CFG",
    .init = vi_init,
    .deinit = vi_deinit,
    .reset = vi_reset,
    .open = vi_open,
    .control_xfer_cb = vi_control,
    .xfer_cb = vi_xfer,
    .xfer_isr = nullptr,
    .sof = nullptr,
};

usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count) {
    *driver_count = 1;
    return &s_driver;
}

//--------------------------------------------------------------------+
// Microsoft OS 1.0 descriptors (WinUSB auto-binding)
// https://learn.microsoft.com/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors
// 1.0 rather than 2.0: 2.0 needs bcdUSB >= 0x0201 and a BOS descriptor, which
// would change the device descriptor we byte-match against a real DualSense.
//--------------------------------------------------------------------+

uint16_t const *ms_os_string_descriptor() {
    static uint16_t desc[9];
    static const char sig[] = "MSFT100";
    desc[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | sizeof(desc));
    // loop: NOT-WORTH — 7 signature chars, USB enumeration only
    for (int i = 0; i < 7; ++i) desc[1 + i] = (uint16_t) sig[i];
    desc[8] = MS_OS_VENDOR_CODE; // bMS_VendorCode, bPad = 0
    return desc;
}

// Extended Compat ID (wIndex 4): WINUSB on our interface only.
static uint8_t const kCompatId[40] = {
    40, 0, 0, 0,            // dwLength
    0x00, 0x01,             // bcdVersion 1.00
    0x04, 0x00,             // wIndex: extended compat ID
    1,                      // bCount
    0, 0, 0, 0, 0, 0, 0,    // reserved
    VENDOR_ITF_NUM,         // bFirstInterfaceNumber
    0x01,                   // reserved
    'W', 'I', 'N', 'U', 'S', 'B', 0, 0, // compatibleID
    0, 0, 0, 0, 0, 0, 0, 0, // subCompatibleID
    0, 0, 0, 0, 0, 0,       // reserved
};

// Extended Properties (wIndex 5): DeviceInterfaceGUIDs (REG_MULTI_SZ).
static constexpr char16_t kPropName[] = u"DeviceInterfaceGUIDs";
static constexpr char16_t kPropData[] = VENDOR_ITF_GUID u"\0"; // multi-sz: extra terminator
static constexpr uint32_t kNameBytes = sizeof(kPropName);
static constexpr uint32_t kDataBytes = sizeof(kPropData);
static constexpr uint32_t kSectionBytes = 4 + 4 + 2 + kNameBytes + 4 + kDataBytes;
static constexpr uint32_t kExtPropBytes = 10 + kSectionBytes;
static uint8_t kExtProps[kExtPropBytes];

static void put32(uint8_t *&p, uint32_t v) {
    // loop: NOT-WORTH — 4 bytes, building MS OS descriptors once
    for (int i = 0; i < 4; ++i) *p++ = (uint8_t) (v >> (8 * i));
}

static void put16(uint8_t *&p, uint16_t v) {
    *p++ = (uint8_t) v;
    *p++ = (uint8_t) (v >> 8);
}

static void build_ext_props() {
    static bool built = false;
    if (built) return;
    uint8_t *p = kExtProps;
    put32(p, kExtPropBytes); // dwLength
    put16(p, 0x0100);        // bcdVersion
    put16(p, 0x0005);        // wIndex: extended properties
    put16(p, 1);             // wCount
    put32(p, kSectionBytes); // dwSize
    put32(p, 7);             // dwPropertyDataType: REG_MULTI_SZ
    put16(p, kNameBytes);    // wPropertyNameLength
    // loop: NOT-WORTH — property-name chars, built once (static flag)
    for (char16_t c : kPropName) put16(p, c);
    put32(p, kDataBytes);    // dwPropertyDataLength
    // loop: NOT-WORTH — property-data chars, built once (static flag)
    for (char16_t c : kPropData) put16(p, c);
    built = true;
}

//--------------------------------------------------------------------+
// Vendor control requests
//--------------------------------------------------------------------+

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *req) {
    // The wake keyboard has no configurator and no MS OS descriptors.
    // error: NOT-WORTH — USB STALL for a request this interface doesn't take (wrong direction/recipient/id); the app only sends the documented ones and its transport reports a STALL as an error
    if (usb_wake_keyboard()) return false;
    if (req->bRequest == MS_OS_VENDOR_CODE &&
        req->bmRequestType_bit.direction == TUSB_DIR_IN) {
        if (stage != CONTROL_STAGE_SETUP) return true;
        if (req->wIndex == 0x0004) {
            // error: NOT-WORTH — returned to TinyUSB (STALL on failure); queueing a ≤ 64-byte reply on endpoint 0 can't fail
            return tud_control_xfer(rhport, req, (void *) kCompatId,
                                    tu_min16(req->wLength, sizeof(kCompatId)));
        }
        if (req->wIndex == 0x0005) {
            build_ext_props();
            // error: NOT-WORTH — returned to TinyUSB (STALL on failure); queueing a ≤ 64-byte reply on endpoint 0 can't fail
            return tud_control_xfer(rhport, req, kExtProps,
                                    tu_min16(req->wLength, sizeof(kExtProps)));
        }
        // error: NOT-WORTH — USB STALL for a request this interface doesn't take (wrong direction/recipient/id); the app only sends the documented ones and its transport reports a STALL as an error
        return false;
    }

    if (req->bmRequestType_bit.recipient != TUSB_REQ_RCPT_INTERFACE ||
        tu_u16_low(req->wIndex) != VENDOR_ITF_NUM) {
        // error: NOT-WORTH — USB STALL for a request this interface doesn't take (wrong direction/recipient/id); the app only sends the documented ones and its transport reports a STALL as an error
        return false;
    }
    uint8_t const id = tu_u16_low(req->wValue);
    if (id == PICO_CMD_OLED_MIRROR) {
        // error: NOT-WORTH — USB STALL for a request this interface doesn't take (wrong direction/recipient/id); the app only sends the documented ones and its transport reports a STALL as an error
        if (req->bRequest != VENDOR_REQ_GET || req->bmRequestType_bit.direction != TUSB_DIR_IN) return false;
        if (stage != CONTROL_STAGE_SETUP) return true;
        uint16_t const n = oled_mirror(s_mirror, sizeof(s_mirror));
        // error: NOT-WORTH — returned to TinyUSB (STALL on failure); queueing a ≤ 64-byte reply on endpoint 0 can't fail
        return tud_control_xfer(rhport, req, s_mirror, tu_min16(n, req->wLength));
    }
    // Controller health & info (proto 1.12): wValue's high byte picks the
    // header, the firmware info or a result page (ctrl_info.cpp).
    if (id == PICO_CMD_CTRL_INFO) {
        // error: NOT-WORTH — USB STALL for a request this interface doesn't take (wrong direction/recipient/id); the app only sends the documented ones and its transport reports a STALL as an error
        if (req->bRequest != VENDOR_REQ_GET || req->bmRequestType_bit.direction != TUSB_DIR_IN) return false;
        if (stage != CONTROL_STAGE_SETUP) return true;
        uint16_t const reqlen = tu_min16(req->wLength, VENDOR_MAX_PAYLOAD);
        memset(s_buf, 0, sizeof(s_buf));
        uint16_t const n = ctrl_info_get(tu_u16_high(req->wValue), s_buf, reqlen);
        // error: NOT-WORTH — returned to TinyUSB (STALL on failure); queueing a ≤ 64-byte reply on endpoint 0 can't fail
        return tud_control_xfer(rhport, req, s_buf, tu_min16(n, reqlen));
    }
    // 0xF0/0xF1 exist only here (not on the HID path), and the 0xF2-0xF4 debug
    // reports only here in release builds; all are GET-only.
    bool const vendor_only = id == PICO_CMD_IDENTITY || id == PICO_CMD_LIVE_INPUT || id == PICO_CMD_CALIB || id == PICO_CMD_REMAP_EXTRA || id == PICO_CMD_CURVES || id == PICO_CMD_FW_IDENTITY || is_debug_cmd(id);
    // error: NOT-WORTH — USB STALL for a request this interface doesn't take (wrong direction/recipient/id); the app only sends the documented ones and its transport reports a STALL as an error
    if (!vendor_only && !is_pico_cmd(id)) return false;
    // error: NOT-WORTH — USB STALL for a request this interface doesn't take (wrong direction/recipient/id); the app only sends the documented ones and its transport reports a STALL as an error
    if (vendor_only && req->bRequest != VENDOR_REQ_GET) return false;

    switch (req->bRequest) {
        case VENDOR_REQ_GET: {
            if (stage != CONTROL_STAGE_SETUP) return true;
            TU_VERIFY(req->bmRequestType_bit.direction == TUSB_DIR_IN);
            uint16_t const reqlen = tu_min16(req->wLength, VENDOR_MAX_PAYLOAD);
            memset(s_buf, 0, sizeof(s_buf));
            uint16_t const n = pico_cmd_get(id, s_buf, reqlen);
            // error: NOT-WORTH — returned to TinyUSB (STALL on failure); queueing a ≤ 64-byte reply on endpoint 0 can't fail
            return tud_control_xfer(rhport, req, s_buf, tu_min16(n, reqlen));
        }
        case VENDOR_REQ_SET: {
            TU_VERIFY(req->bmRequestType_bit.direction == TUSB_DIR_OUT);
            TU_VERIFY(req->wLength >= 1 && req->wLength <= VENDOR_MAX_PAYLOAD);
            if (stage == CONTROL_STAGE_SETUP) {
                // error: NOT-WORTH — returned to TinyUSB (STALL on failure); queueing a ≤ 64-byte reply on endpoint 0 can't fail
                return tud_control_xfer(rhport, req, s_buf, req->wLength);
            }
            // Act after the status stage: 0x03 (reconnect) drops the bus, and the
            // host must already have its ACK by then.
            if (stage == CONTROL_STAGE_ACK) {
                pico_cmd_set(id, s_buf, req->wLength);
            }
            return true;
        }
        default:
            // error: NOT-WORTH — USB STALL for a request this interface doesn't take (wrong direction/recipient/id); the app only sends the documented ones and its transport reports a STALL as an error
            return false;
    }
}
