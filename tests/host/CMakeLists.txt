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
        test_perf.cpp
        test_version_label.cpp
        test_title_scroll.cpp
        # The firmware sources under test, unmodified.
        ${FW}/src/calib.cpp
        ${FW}/src/remap.cpp
        ${FW}/src/config.cpp
        ${FW}/src/cmd.cpp
        ${FW}/src/perf.cpp
        ${FW}/src/ctrl_info.cpp
)
# stubs/ first so its SDK headers win.
target_include_directories(host_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs ${FW}/src)
target_compile_definitions(host_tests PRIVATE
        ENABLE_COMPANION_VENDOR=1
        ENABLE_SERIAL=0
        ENABLE_VERBOSE=0
        DS5_BOARD_NAME="Host"
        DS5_C1_DIAG=1  # issue #15 diagnostics: GET 0xEA (c1_diag.h)
        SYS_CLOCK_KHZ=320000
        FIRMWARE_VERSION="host-test"
        PICO_PROGRAM_VERSION_STRING="host-test"
)
# Same char signedness as the firmware build (CMakeLists.txt: -fsigned-char).
target_compile_options(host_tests PRIVATE -O1 -g -fsigned-char -Wall -Wextra -Wno-unused-parameter ${SAN})
target_link_options(host_tests PRIVATE ${SAN})
find_package(Threads REQUIRED)  # test_perf.cpp: a writer thread stands in for core1
target_link_libraries(host_tests PRIVATE Threads::Threads)
