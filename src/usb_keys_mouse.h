// The keyboard + mouse HID interface (protocol 1.24, docs/TENSION_BUTTONS.md
// section 5): a second HID interface after the configurator interface, on the
// bus only while Config_body.keys_mouse is on (from the next USB connect) and
// only in the configurator build (ENABLE_COMPANION_VENDOR). The DualSense
// interface and its 289-byte report descriptor never change; with the switch
// off the configuration descriptor is byte-identical to before.
//
// Step 1 of the build (the USB probe): the interface is there and sends
// nothing. Every TinyUSB HID callback checks the instance first, so a request
// to this interface never reaches the DualSense's handlers.
#ifndef DS5_BRIDGE_USB_KEYS_MOUSE_H
#define DS5_BRIDGE_USB_KEYS_MOUSE_H

// TinyUSB numbers HID instances in descriptor order: 0 the DualSense (or the
// wake keyboard, which then is the only interface), 1 keys and mouse.
#define KEYS_MOUSE_HID_INSTANCE 1

// The interface is on the bus now: enumerated with the switch on, configured
// by the host, and not the wake keyboard (identity [59] bit0).
bool usb_keys_mouse_on();

#endif  // DS5_BRIDGE_USB_KEYS_MOUSE_H
