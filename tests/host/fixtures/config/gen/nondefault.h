// The non-default settings the fixture writers save ("custom" pages) and the
// stable-loader check reads back: every field a tag has gets a value that is
// not its default. Shared by writer.cpp, reader.cpp and the host tests.
#pragma once
template <class B> inline void nondefault(B &b) {
    b.haptics_gain = 2.5f;
    b.inactive_time = 42;
    if constexpr (requires { b.speaker_volume; }) b.speaker_volume = -6.0f;
    if constexpr (requires { b.polling_rate_mode; }) b.polling_rate_mode = 0;
    if constexpr (requires { b.current_slot; }) b.current_slot = 2;
    if constexpr (requires { b.lightbar_mode; }) b.lightbar_mode = 3;
    if constexpr (requires { b.lb_fav_r; }) b.lb_fav_r[1] = 0x12;
    if constexpr (requires { b.screen_dim_timeout; }) b.screen_dim_timeout = 7;
    if constexpr (requires { b.screen_brightness; }) b.screen_brightness = 2;
    if constexpr (requires { b.haptics_gain_right; }) b.haptics_gain_right = 3.5f;
    if constexpr (requires { b.trigger_reduce; }) b.trigger_reduce = 4;
    if constexpr (requires { b.mic_level; }) b.mic_level = 20;
    if constexpr (requires { b.mic_echo_cancel; }) b.mic_echo_cancel = 0;
}
