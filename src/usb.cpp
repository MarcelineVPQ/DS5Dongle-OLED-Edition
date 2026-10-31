//
// Created by awalol on 2026/3/4.
//

#include "tusb.h"
#include "bsp/board_api.h"
#include "config.h"
#include "state_mgr.h"
#include "amp_volume.h"
#include <algorithm>
#include "pico/time.h"
#include "usb.h"

uint8_t mute[2]; // 0: SPEAKER(0x02) 1: MIC(0x05)
// The mic starts at its top (48 dB, the controller's full VolumeMic) until the
// PC sets it (upstream a7824d9 starts it at 48 too).
float volume[2] = {-100.0f,48.0f}; // 0: SPEAKER(0x02) 1: MIC(0x05)
static volatile uint64_t s_host_spk_set_us = 0;
static bool s_spk_vol_from_host = false;  // the host has set the speaker volume (GET_CUR echoes it)
uint64_t usb_host_speaker_set_us() { return s_host_spk_set_us; }

#define UAC1_ENTITY_SPK_FEATURE_UNIT    0x02
#define UAC1_ENTITY_MIC_FEATURE_UNIT    0x05

/*int main() {
    board_init();

    tusb_rhport_init_t dev_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO
    };
    tusb_init(BOARD_TUD_RHPORT, &dev_init);

    board_init_after_tusb();

    while (1) {
        tud_task();
    }
}*/

//--------------------------------------------------------------------+
// Audio Callback Functions
//--------------------------------------------------------------------+

//--------------------------------------------------------------------+
// UAC1 Helper Functions
//--------------------------------------------------------------------+

static bool audio10_set_req_entity(tusb_control_request_t const *p_request, uint8_t *pBuff) {
    uint8_t channelNum = TU_U16_LOW(p_request->wValue);
    uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
    uint8_t entityID = TU_U16_HIGH(p_request->wIndex);
    uint8_t index = entityID == UAC1_ENTITY_SPK_FEATURE_UNIT ? 0 : 1;

    // If request is for our speaker feature unit
    if (entityID == UAC1_ENTITY_SPK_FEATURE_UNIT || entityID == UAC1_ENTITY_MIC_FEATURE_UNIT) {
        switch (ctrlSel) {
            case AUDIO10_FU_CTRL_MUTE:
                switch (p_request->bRequest) {
                    case AUDIO10_CS_REQ_SET_CUR:
                        // Only 1st form is supported
                        TU_VERIFY(p_request->wLength == 1);

                        mute[index] = pBuff[0];
                        if (index == 0) s_host_spk_set_us = time_us_64();
                        // The PC's mute switches reach the controller too
                        // (upstream cfca2a5); the speaker's PCM is also zeroed
                        // in audio_loop, as before.
                        state_set_audio_mute(mute[1] != 0, mute[0] != 0);

                        TU_LOG2("    Set Mute: %d of entity: %u\r\n", mute[index], entityID);
                        return true;

                    default:
                        // error: NOT-WORTH — USB STALL: the host asked for an audio control we don't have; that's USB's answer, the OS moves on
                        return false; // not supported
                }

            case AUDIO10_FU_CTRL_VOLUME:
                switch (p_request->bRequest) {
                    case AUDIO10_CS_REQ_SET_CUR:
                        // Only 1st form is supported
                        TU_VERIFY(p_request->wLength == 2);

                        volume[index] = static_cast<float>(*reinterpret_cast<int16_t const *>(pBuff)) / 256;
                        // Do NOT sync the USB-side UAC1 volume control into our
                        // flash-persisted config. The host (PipeWire/Pulse) re-applies
                        // its last-known UAC1 volume on every device reconnect, which
                        // would silently override the user's saved speaker_volume.
                        // Fix borrowed from loteran/DS5Dongle commit 03fa1e4.
                        //
                        // Instead the host volume drives the controller's amp,
                        // 0 dB -> 100 (0x64, top of the speaker range) down to
                        // -100 dB -> 0, as upstream does (aa5e012). Before, the
                        // amp was pinned at 0x64 whatever the host slider said,
                        // which overdrove the speaker into crackle. The headphone
                        // amp's range is 0..0x7F, so it gets the same position
                        // scaled to 127 (amp_volume.h).
                        if (entityID == UAC1_ENTITY_SPK_FEATURE_UNIT) {
                            s_host_spk_set_us = time_us_64();
                            s_spk_vol_from_host = true;
                            state_set_volume(headphone_amp_from_db(volume[index]), speaker_amp_from_db(volume[index]));
                        }
                        // The PC's mic volume, 0..48 dB (our range below), as
                        // the controller's VolumeMic 0..0x40 (it tops out at
                        // 0x40, the level it had before). Upstream cfca2a5
                        // sends the dB number itself (max 48).
                        if (entityID == UAC1_ENTITY_MIC_FEATURE_UNIT) {
                            const float v = std::clamp(volume[index], 0.0f, 48.0f);
                            state_set_mic_volume(static_cast<uint8_t>(v * 64.0f / 48.0f + 0.5f));
                        }

                        TU_LOG2("    Set Volume: %d dB of entity: %u\r\n", volume[index], entityID);
                        return true;

                    default:
                        // error: NOT-WORTH — USB STALL: the host asked for an audio control we don't have; that's USB's answer, the OS moves on
                        return false; // not supported
                }

            // Unknown/Unsupported control
            default:
                TU_BREAKPOINT();
                // error: NOT-WORTH — USB STALL: the host asked for an audio control we don't have; that's USB's answer, the OS moves on
                return false;
        }
    }

    // error: NOT-WORTH — USB STALL: the host asked for an audio control we don't have; that's USB's answer, the OS moves on
    return false;
}

static bool audio10_get_req_entity(uint8_t rhport, tusb_control_request_t const *p_request) {
    uint8_t channelNum = TU_U16_LOW(p_request->wValue);
    uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
    uint8_t entityID = TU_U16_HIGH(p_request->wIndex);
    uint8_t index = entityID == UAC1_ENTITY_SPK_FEATURE_UNIT ? 0 : 1;

    // If request is for our speaker feature unit
    if (entityID == UAC1_ENTITY_SPK_FEATURE_UNIT || entityID == UAC1_ENTITY_MIC_FEATURE_UNIT) {
        switch (ctrlSel) {
            case AUDIO10_FU_CTRL_MUTE:
                // Audio control mute cur parameter block consists of only one byte - we thus can send it right away
                // There does not exist a range parameter block for mute
                TU_LOG2("    Get Mute of entity: %u\r\n", entityID);
                // error: NOT-WORTH — returned to TinyUSB (STALL on failure); a 1-2 byte reply into TinyUSB's control buffer can't fail
                return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &mute[index], 1);

            case AUDIO10_FU_CTRL_VOLUME:
                switch (p_request->bRequest) {
                    case AUDIO10_CS_REQ_GET_CUR:
                        TU_LOG2("    Get Volume of entity: %u\r\n", entityID); {
                            if (entityID == UAC1_ENTITY_SPK_FEATURE_UNIT && !s_spk_vol_from_host) {
                                // Until the host sets one: the saved speaker volume.
                                // UAC1's advertised range tops out at 0 dB; a boost
                                // (speaker_volume > 0) reports as 0 to the host.
                                // After a SET_CUR, the host reads back what it set
                                // (it used to read the saved gain, so its slider
                                // could jump; review fw-usb-bt N1).
                                volume[index] = std::min(get_config().speaker_volume, 0.0f);
                            }
                            int16_t vol = volume[index] * 256; // convert to 1/256 dB units
                            // error: NOT-WORTH — returned to TinyUSB (STALL on failure); a 1-2 byte reply into TinyUSB's control buffer can't fail
                            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &vol, sizeof(vol));
                        }

                    case AUDIO10_CS_REQ_GET_MIN:
                        TU_LOG2("    Get Volume min of entity: %u\r\n", entityID); {
                            uint8_t min[2];
                            if (entityID == UAC1_ENTITY_SPK_FEATURE_UNIT) {
                                min[0] = 0x00;
                                min[1] = 0x9c;
                            }else {
                                min[0] = 0x00;
                                min[1] = 0x00;
                            }
                            // error: NOT-WORTH — returned to TinyUSB (STALL on failure); a 1-2 byte reply into TinyUSB's control buffer can't fail
                            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &min, sizeof(min));
                        }

                    case AUDIO10_CS_REQ_GET_MAX:
                        TU_LOG2("    Get Volume max of entity: %u\r\n", entityID); {
                            uint8_t max[2];
                            if (entityID == UAC1_ENTITY_SPK_FEATURE_UNIT) {
                                max[0] = 0x00;
                                max[1] = 0x00;
                            }else {
                                max[0] = 0x00;
                                max[1] = 0x30;
                            }
                            // error: NOT-WORTH — returned to TinyUSB (STALL on failure); a 1-2 byte reply into TinyUSB's control buffer can't fail
                            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &max, sizeof(max));
                        }

                    case AUDIO10_CS_REQ_GET_RES:
                        TU_LOG2("    Get Volume res of entity: %u\r\n", entityID); {
                            uint8_t res[2];
                            if (entityID == UAC1_ENTITY_SPK_FEATURE_UNIT) {
                                res[0] = 0x00;
                                res[1] = 0x01;
                            }else {
                                res[0] = 0x7a;
                                res[1] = 0x00;
                            }
                            // error: NOT-WORTH — returned to TinyUSB (STALL on failure); a 1-2 byte reply into TinyUSB's control buffer can't fail
                            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &res, sizeof(res));
                        }
                    // Unknown/Unsupported control
                    default:
                        TU_BREAKPOINT();
                        return false;
                }
                break;

            // Unknown/Unsupported control
            default:
                TU_BREAKPOINT();
                return false;
        }
    }

    return false;
}

// Invoked when audio class specific get request received for an entity
bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
    (void) rhport;

    return audio10_get_req_entity(rhport, p_request);
}

// Invoked when audio class specific set request received for an entity
bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf) {
    (void) rhport;

    // error: NOT-WORTH — USB STALL: the host asked for an audio control we don't have; that's USB's answer, the OS moves on
    return audio10_set_req_entity(p_request, buf);
}

void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len) {
    (void) instance;
    (void) len;
}
