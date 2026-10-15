# Host-side unit tests for the firmware's pure logic, with ASan + UBSan.
# No hardware and no Pico SDK: stubs/ stands in for the SDK headers, fakes.cpp
# for the rest of the firmware (flash is a RAM array). Run: tests/host/run.sh
cmake_minimum_required(VERSION 3.16)
project(ds5_host_tests CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(FW ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(SAN -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)

add_executable(host_tests
        main.cpp
        fakes.cpp
        test_oled_flush.cpp
        test_oled_pad.cpp
        test_stick_curve.cpp
        test_calib.cpp
        test_remap.cpp
        test_config.cpp
        test_cmd.cpp
        test_ctrl_info.cpp
        test_dse.cpp
        test_perf.cpp
        test_version_label.cpp
        test_title_scroll.cpp
        test_fw_report.cpp
        test_haptics_decimate.cpp
        test_grip_wave.cpp
        test_usb_power.cpp
        test_oled_keys.cpp
        test_oled_vpad.cpp
        test_state_override.cpp
        test_audio_levels.cpp
        test_speaker_mono.cpp
        test_rssi_poll.cpp
        test_amp_volume.cpp
        test_voice_cue.cpp
        test_slots.cpp
        test_scan_policy.cpp
        test_mic_level.cpp
        test_pair_prompt.cpp
        test_oled_toast.cpp
        test_heap.cpp
        # The firmware sources under test, unmodified.
        ${FW}/src/calib.cpp
        ${FW}/src/remap.cpp
        ${FW}/src/config.cpp
        ${FW}/src/cmd.cpp
        ${FW}/src/perf.cpp
        ${FW}/src/ctrl_info.cpp
        ${FW}/src/dse.cpp
        ${FW}/src/slots.cpp
        # The reference the haptics decimator is checked against (lib/WDL submodule).
        ${FW}/lib/WDL/WDL/resample.cpp
)
# stubs/ first so its SDK headers win.
target_include_directories(host_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs ${FW}/src ${FW}/lib/WDL/WDL)
target_compile_definitions(host_tests PRIVATE
        ENABLE_COMPANION_VENDOR=1
        ENABLE_SERIAL=0
        ENABLE_VERBOSE=0
        DS5_BOARD_NAME="Host"
        SYS_CLOCK_KHZ=150000
        FIRMWARE_VERSION="host-test"
        PICO_PROGRAM_VERSION_STRING="host-test"
        WDL_RESAMPLE_TYPE=float  # as the firmware build
)
# Same char signedness as the firmware build (CMakeLists.txt: -fsigned-char).
target_compile_options(host_tests PRIVATE -O1 -g -fsigned-char -Wall -Wextra -Wno-unused-parameter ${SAN})
target_link_options(host_tests PRIVATE ${SAN})
find_package(Threads REQUIRED)  # test_perf.cpp: a writer thread stands in for core1
target_link_libraries(host_tests PRIVATE Threads::Threads)
# Third-party: no warnings from WDL itself.
set_source_files_properties(${FW}/lib/WDL/WDL/resample.cpp PROPERTIES COMPILE_OPTIONS "-w")
