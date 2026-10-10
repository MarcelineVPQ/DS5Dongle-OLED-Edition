//
// Created by awalol on 2026/5/15.
//

#ifndef DS5_BRIDGE_STATE_MGR_H
#define DS5_BRIDGE_STATE_MGR_H

#include <cstdint>

void state_init();
void state_set(uint8_t *data, const uint8_t size);
void state_update(const uint8_t *data, const uint8_t size);

// Lightbar RGB lives in the persistent state[] block (SetStateData LedRed/
// Green/Blue) that gets stamped into every outbound BT packet. The OLED
// lightbar service writes it directly so a firmware-chosen color rides every
// host/audio frame instead of only the transient send_lightbar_color() packet.
void state_set_led(uint8_t r, uint8_t g, uint8_t b);
void state_get_led(uint8_t *r, uint8_t *g, uint8_t *b);

// Motor RumbleEmulation (heavy/light) lives in the persistent state[] block too
// (SetStateData RumbleEmulationRight/Left). audio.cpp drives these from the
// haptic-audio (ch3/4) envelope so games that author haptics ONLY as audio
// (Cyberpunk, DS2) still fire the strong motor actuators — the 0x36 audio-
// haptic actuator path alone is far too weak. EnableImprovedRumbleEmulation is
// already asserted in state_init_data, so this gets full (un-halved) strength.
void state_set_rumble(uint8_t right, uint8_t left);

#endif //DS5_BRIDGE_STATE_MGR_H
