// The narrator's firmware lines: short voice cues the dongle itself plays on
// the controller's speaker (connected, pairing slot, battery low), so they
// work without the app. Off until the app turns them on (config
// narrator_voice), in the language the app is set to.
#ifndef DS5_BRIDGE_VOICE_H
#define DS5_BRIDGE_VOICE_H

#include <cstdint>

// Order = the bank's line order (tools/build_voice_bank.py).
enum VoiceLine : uint8_t {
    VOICE_CONNECTED = 0,
    VOICE_SLOT_0 = 1,  // + slot
    VOICE_BATTERY_LOW = 5,
    VOICE_NUM_LINES = 6,
};
constexpr uint8_t kVoiceNumLangs = 7;  // en de fr es pt-BR zh-CN ja

// Say a line in the configured language (nothing if the narrator is off or no
// controller is connected). voice_say_lang plays it regardless (app preview).
void voice_say(VoiceLine line);
void voice_say_lang(VoiceLine line, uint8_t lang);
bool voice_active();
// A slot switch: drop the controller once its line has played.
void voice_disconnect_after_line();

// The audio path: while a line plays, its frames replace the host's speaker
// frames. Fills two 200-byte Opus frames; false when nothing is playing.
bool voice_take_frames(uint8_t *frame0, uint8_t *frame1, uint16_t frame_size);
// The same, one frame (the one-frame 0x36 report, packet_mode 1).
bool voice_take_frame(uint8_t *frame, uint16_t frame_size);

// Main loop: the connect / battery cues, and the line's own audio reports
// while the host isn't streaming any.
void voice_task();

#endif // DS5_BRIDGE_VOICE_H
