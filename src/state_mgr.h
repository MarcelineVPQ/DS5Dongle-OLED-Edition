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

#endif //DS5_BRIDGE_STATE_MGR_H
