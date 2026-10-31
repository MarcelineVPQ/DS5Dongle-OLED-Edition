//
// Created by awalol on 2026/5/15.
//

#include <cstddef>
#include <cstring>

#include "utils.h"
#include "state_mgr.h"
#include "state_override.h"
#include "config.h"
#include "bt.h"
#include "device/usbd.h"

extern uint8_t reportSeqCounter;

// Set by the OLED lightbar service (src/oled.cpp). While true, the firmware
// owns the lightbar (an OLED mode or the charging pulse) and the host's
// AllowLedColor writes are suppressed below so they can't stomp it.
extern bool g_lightbar_override;

namespace {
    constexpr size_t kAudioControlOffset = offsetof(SetStateData, MuteLightMode) - sizeof(uint8_t);
    constexpr size_t kMuteControlOffset = offsetof(SetStateData, RightTriggerFFB) - sizeof(uint8_t);
    constexpr size_t kMotorPowerLevelOffset = offsetof(SetStateData, HostTimestamp) + sizeof(uint32_t);
    constexpr size_t kAudioControl2Offset = kMotorPowerLevelOffset + sizeof(uint8_t);
    constexpr size_t kHapticLowPassFilterOffset = offsetof(SetStateData, LightFadeAnimation) - 2 * sizeof(uint8_t);
    constexpr size_t kPlayerIndicatorsOffset = offsetof(SetStateData, LedRed) - sizeof(uint8_t);
}

// Headphones/Speaker volume start at max until the host sets its USB volume,
// which state_set_volume() then maps onto the controller's amp (see usb.cpp).
// With the allow bits clear the DS5 amp stays silent, so they must stay set.
static constexpr uint8_t state_init_data[63] = {
    0xfd, 0xf7, 0x0, 0x0,
    0x7f, 0x64, // Headphones, Speaker
    0x40, 0x9, 0x0, 0x00, 0x0, 0x0, 0x0, 0x0, // VolumeMic=64, MuteControl all clear (no PowerSave)
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xa,
    0x7, 0x0, 0x0, 0x2, 0x1,
    0x00,
    0xff, 0xd7, 0x00 // RGB LED: R, G, B (Nijika Color!)✨
};

uint8_t state[63]{};

void state_init() {
    memcpy(state, state_init_data, sizeof(state));
}

void state_set(uint8_t *data, const uint8_t size) {
    if (size > 63) {
        printf("[StateMgr] Warning: State Set over 63 bytes\n");
    }
    memcpy(data, state, size);
}

void state_set_led(uint8_t r, uint8_t g, uint8_t b) {
    state[offsetof(SetStateData, LedRed) + 0] = r;
    state[offsetof(SetStateData, LedRed) + 1] = g;
    state[offsetof(SetStateData, LedRed) + 2] = b;
}

// The sections a game last set that the 1.13 settings can override
// (state_override.h), so a forced value that's turned off again goes back to
// the game's.
static HostAudioBytes s_host_audio = kHostAudioDefaults;

// One 0x31 with just these SetStateData bytes (their allow bits say what the
// controller applies), carrying the game's current rumble.
static void send_state_report(const uint8_t *ss) {
    uint8_t pkt[78]{};
    pkt[0] = 0x31;
    pkt[1] = reportSeqCounter << 4;
    reportSeqCounter = (reportSeqCounter + 1) & 0x0F;
    pkt[2] = 0x10;
    memcpy(pkt + 3, ss, sizeof(SetStateData));
    state_stamp_host_rumble(pkt + 3);
    // error: LOGGED-OK — counter bt_send_drops (0xFD, OLED Diagnostics); a full output queue drops it, the next state change sends the whole state again
    bt_write(pkt, sizeof(pkt));
}

void state_set_volume(uint8_t headphones, uint8_t speaker) {
    state[0] |= 0x30; // AllowHeadphoneVolume | AllowSpeakerVolume
    state[offsetof(SetStateData, VolumeHeadphones)] = headphones;
    state[offsetof(SetStateData, VolumeSpeaker)] = speaker;

    // Push it now (upstream update_state): audio frames don't carry state.
    uint8_t ss[sizeof(SetStateData)]{};
    ss[0] = 0x30; // valid_flag0: headphone + speaker volume only
    ss[offsetof(SetStateData, VolumeHeadphones)] = headphones;
    ss[offsetof(SetStateData, VolumeSpeaker)] = speaker;
    send_state_report(ss);
}

void state_set_audio_mute(bool mic, bool speaker) {
    // MuteControl [9]: bit4 MicMute, bit5 SpeakerMute, bit6 HeadphoneMute; the
    // power-save bits stay clear, as at connect (upstream cfca2a5).
    const uint8_t mute = (uint8_t) ((mic ? 0x10 : 0) | (speaker ? 0x60 : 0));
    state[kMuteControlOffset] = mute;  // the connect-time state carries it too
    uint8_t ss[sizeof(SetStateData)]{};
    ss[1] = 0x02;  // AllowAudioMute
    ss[kMuteControlOffset] = mute;
    send_state_report(ss);
}

void state_set_mic_volume(uint8_t volume) {
    // Kept as the PC's level (the connect-time state carries it, stamped with
    // a fixed mic level when one is set), but with a fixed level (1.16) the
    // controller keeps that one; Auto puts this back (state_config_task).
    state[offsetof(SetStateData, VolumeMic)] = volume;
    if (get_config().mic_level) return;
    uint8_t ss[sizeof(SetStateData)]{};
    ss[0] = 0x40;  // AllowMicVolume
    ss[offsetof(SetStateData, VolumeMic)] = volume;
    send_state_report(ss);
}

static StateOverrides overrides_from_config() {
    const Config_body &c = get_config();
    return {c.trigger_reduce, c.speaker_gain, c.lock_volume, c.mic_select, c.mic_level, c.mic_echo_cancel};
}

void state_stamp_config(uint8_t *ss, bool from_host) {
    stamp_overrides(ss, overrides_from_config(), s_host_audio, from_host);
}

extern uint8_t interrupt_in_data[63]; // main.cpp: the controller's latest input report

void state_config_task() {
    static bool s_was_connected = false;
    static StateOverrides s_sent{};
    static int8_t s_muted = -1;  // the controller's mic mute as last shown on its light; -1 unknown
    // After USB is up: by then the connect-time feature reads are done (BT
    // traffic during them delays controller detection, see audio_loop).
    const bool connected = bt_is_connected() && tud_mounted();
    const StateOverrides now = overrides_from_config();
    if (!connected || !s_was_connected) {
        // The connect-time state (bt_send_state) already carries the settings.
        s_was_connected = connected;
        s_sent = now;
        s_muted = -1;
        return;
    }
    const bool trigger = now.trigger_reduce != s_sent.trigger_reduce;
    const bool gain = now.speaker_gain != s_sent.speaker_gain;
    // The mic choice and the echo cancellation (1.17) share AudioControl.
    const bool mic = now.mic_select != s_sent.mic_select || now.echo_cancel != s_sent.echo_cancel;
    const bool level = now.mic_level != s_sent.mic_level;
    s_sent = now;
    if (trigger || gain || mic || level) {
        uint8_t ss[sizeof(SetStateData)]{};
        build_override_push(ss, now, s_host_audio, trigger, gain, mic, level, state[offsetof(SetStateData, VolumeMic)]);
        send_state_report(ss);
    }
    // The mute button's light follows the controller's mic mute (input report
    // [53] bit 2), as on a wired DualSense (upstream a7824d9).
    const int8_t muted = (interrupt_in_data[53] >> 2) & 1;
    if (muted != s_muted) {
        s_muted = muted;
        uint8_t ss[sizeof(SetStateData)]{};
        ss[1] = 0x01;  // AllowMuteLight
        ss[offsetof(SetStateData, MuteLightMode)] = muted ? MuteLight::On : MuteLight::Off;
        send_state_report(ss);
    }
}

// The host's most recent 0x02 output report (SetStateData), kept by main.cpp.
extern uint8_t g_last_out02[47];

void state_stamp_host_rumble(uint8_t *ss) {
    const uint8_t *host = g_last_out02;
    // flags0 bit 0 EnableRumbleEmulation, bit 1 UseRumbleNotHaptics
    ss[0] = (uint8_t) ((ss[0] & ~0x03) | (host[0] & 0x03));
    ss[offsetof(SetStateData, RumbleEmulationRight)] = host[offsetof(SetStateData, RumbleEmulationRight)];
    ss[offsetof(SetStateData, RumbleEmulationLeft)] = host[offsetof(SetStateData, RumbleEmulationLeft)];
    // byte 38 bit 2 EnableImprovedRumbleEmulation, bit 3 UseRumbleNotHaptics2
    ss[38] = (uint8_t) ((ss[38] & ~0x0C) | (host[38] & 0x0C));
}

void state_clear_host_rumble() {
    memset(g_last_out02, 0, sizeof(g_last_out02));
    s_host_audio = kHostAudioDefaults;
}

void state_get_led(uint8_t *r, uint8_t *g, uint8_t *b) {
    *r = state[offsetof(SetStateData, LedRed) + 0];
    *g = state[offsetof(SetStateData, LedRed) + 1];
    *b = state[offsetof(SetStateData, LedRed) + 2];
}

uint8_t state_get_pc_mic_volume() {
    return state[offsetof(SetStateData, VolumeMic)];
}

uint8_t state_get_player_leds() {
    return state[offsetof(SetStateData, LedRed) - 1] & 0x1F;
}

void state_update(const uint8_t *data, const uint8_t size) {
    if (size < sizeof(SetStateData)) {
        printf(
            "[StateMgr] Error: SetStateData at least %u bytes\n",
            static_cast<unsigned>(sizeof(SetStateData))
        );
        return;
    }

    SetStateData update{};
    memcpy(&update, data, sizeof(update));
    record_host_audio(s_host_audio, data);

    const auto copy_if_allowed = [&](const bool allowed, const size_t offset, const size_t length) {
        if (allowed) {
            memcpy(state + offset, data + offset, length);
        }
    };
    auto set_bit = [](uint8_t &byte, const int bit, const bool value) {
        byte = (byte & ~(1 << bit)) | (value << bit);
    };

    set_bit(state[0], 0, update.EnableRumbleEmulation);
    set_bit(state[0], 1, update.UseRumbleNotHaptics);
    set_bit(state[38], 2, update.EnableImprovedRumbleEmulation);
    copy_if_allowed(
        update.UseRumbleNotHaptics || update.EnableRumbleEmulation,
        offsetof(SetStateData, RumbleEmulationRight),
        2
    );

    /*copy_if_allowed(
        update.AllowHeadphoneVolume,
        offsetof(SetStateData, VolumeHeadphones),
        sizeof(update.VolumeHeadphones)
    );*/
    /*copy_if_allowed(
        update.AllowSpeakerVolume,
        offsetof(SetStateData, VolumeSpeaker),
        sizeof(update.VolumeSpeaker)
    );*/
    /*copy_if_allowed(
        update.AllowMicVolume,
        offsetof(SetStateData, VolumeMic),
        sizeof(update.VolumeMic)
    );*/
    /*copy_if_allowed(
        update.AllowAudioControl,
        kAudioControlOffset,
        sizeof(uint8_t)
    );*/

    copy_if_allowed(
        update.AllowMuteLight,
        offsetof(SetStateData, MuteLightMode),
        sizeof(update.MuteLightMode)
    );

    /*copy_if_allowed(
        update.AllowAudioMute,
        kMuteControlOffset,
        sizeof(uint8_t)
    );*/

    copy_if_allowed(
        update.AllowRightTriggerFFB,
        offsetof(SetStateData, RightTriggerFFB),
        sizeof(update.RightTriggerFFB)
    );
    copy_if_allowed(
        update.AllowLeftTriggerFFB,
        offsetof(SetStateData, LeftTriggerFFB),
        sizeof(update.LeftTriggerFFB)
    );

    /*copy_if_allowed(
        update.AllowMotorPowerLevel,
        kMotorPowerLevelOffset,
        sizeof(uint8_t)
    );*/
    /*copy_if_allowed(
        update.AllowAudioControl2,
        kAudioControl2Offset,
        sizeof(uint8_t)
    );*/
    /*copy_if_allowed(
        update.AllowHapticLowPassFilter,
        kHapticLowPassFilterOffset,
        sizeof(uint8_t)
    );*/

    copy_if_allowed(
        update.AllowColorLightFadeAnimation,
        offsetof(SetStateData, LightFadeAnimation),
        sizeof(update.LightFadeAnimation)
    );
    copy_if_allowed(
        update.AllowLightBrightnessChange,
        offsetof(SetStateData, LightBrightness),
        sizeof(update.LightBrightness)
    );
    copy_if_allowed(
        update.AllowPlayerIndicators,
        kPlayerIndicatorsOffset,
        sizeof(uint8_t)
    );
    copy_if_allowed(
        update.AllowLedColor && !g_lightbar_override,
        offsetof(SetStateData, LedRed),
        sizeof(update.LedRed) * 3
    );
}
