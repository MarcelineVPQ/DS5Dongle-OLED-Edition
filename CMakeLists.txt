# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if (WIN32)
    set(USERHOME $ENV{USERPROFILE})
else ()
    set(USERHOME $ENV{HOME})
endif ()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif ()
# ====================================================================================

if (PICO_W_BUILD)
        set(PICO_BOARD pico_w CACHE STRING "Board type")
        set(PICO_RP2350 OFF CACHE BOOL "Not using RP2350")
else ()
        set(PICO_BOARD pico2_w CACHE STRING "Board type")
endif ()
#set(PICO_CYW43_SUPPORTED 1)
#set(PICO_CYW43_ARCH_THREADSAFE_BACKGROUND 1)

option(ENABLE_SERIAL "Enable USB CDC serial printf output" OFF)
if (ENABLE_SERIAL)
        set(ENABLE_SERIAL_VALUE 1)
else ()
        set(ENABLE_SERIAL_VALUE 0)
endif ()
option(ENABLE_VERBOSE "Enable Log Verbose" OFF)
if(ENABLE_VERBOSE)
    set(ENABLE_VERBOSE_VALUE 1)
else()
    set(ENABLE_VERBOSE_VALUE 0)
endif ()
option(ENABLE_BATT_LED "Blink Pico LED when DualSense battery is low (<=10%)" ON)
if (ENABLE_BATT_LED)
        set(ENABLE_BATT_LED_VALUE 1)
else ()
        set(ENABLE_BATT_LED_VALUE 0)
endif ()
option(DISABLE_SPEAKER_PROC "Disable Speaker in controller" OFF)
if (DISABLE_SPEAKER_PROC)
        set(DISABLE_SPEAKER_PROC_VALUE 1)
else ()
        set(DISABLE_SPEAKER_PROC_VALUE 0)
endif ()
option(PICO_W_BUILD "Build for Rasberry Pi Pico W, audio is missing" OFF)
# Issue #15 diagnostic build selector, declared here because F2 changes the
# clock below; the variants are described further down.
set(ISSUE15_DIAG "" CACHE STRING "Issue #15 diagnostic build: empty (off), D, F1, F2 or F3")
if (PICO_W_BUILD)
        set(CYW43_PIO_CLOCK_DIV_VALUE 3)
        set(SYS_CLOCK_KHZ_VALUE 200000)
        set(DISABLE_SPEAKER_PROC_VALUE 1)
elseif (ISSUE15_DIAG STREQUAL "F2")
        # awalol upstream v0.7.0's Pico 2 W clock (a023371): stock 150 MHz,
        # CYW43 PIO divider 2; main() then leaves clock and voltage at the
        # boot defaults, as upstream does.
        set(CYW43_PIO_CLOCK_DIV_VALUE 2)
        set(SYS_CLOCK_KHZ_VALUE 150000)
else ()
        set(CYW43_PIO_CLOCK_DIV_VALUE 5)
        set(SYS_CLOCK_KHZ_VALUE 320000)
endif ()



# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(ds5-bridge C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(ds5-bridge
        src/usb_descriptors.cpp
        src/main.cpp
        src/usb.cpp
        src/audio.cpp
        src/bt.cpp
        src/config.cpp
        src/cmd.cpp
        src/state_mgr.cpp
        src/oled.cpp
        src/calib.cpp
        src/slots.cpp
        src/remap.cpp
        src/voice.cpp
        src/perf.cpp
        src/ctrl_info.cpp
        src/ram_mem.c
)
# The narrator's lines (tools/build_voice_bank.py), .incbin'd by voice.cpp.
target_compile_definitions(ds5-bridge PRIVATE VOICE_BANK_FILE="${CMAKE_CURRENT_SOURCE_DIR}/src/voice_bank.bin")
set_source_files_properties(src/voice.cpp PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/voice_bank.bin)

if (ENABLE_BATT_LED)
        target_sources(ds5-bridge PRIVATE src/battery_led.cpp)
endif ()

# Cockos WDLResampler
add_library(wdl_resampler STATIC
        lib/WDL/WDL/resample.cpp
)
target_include_directories(wdl_resampler PUBLIC
        lib/WDL/WDL
)

# Opus
#set(OPUS_FIXED_POINT ON)
# libopus' desktop hardening, on by default upstream, costs core1 time every
# frame: _FORTIFY_SOURCE turns its memcpy/memset into __memcpy_chk /
# __memset_chk, which live in flash (so core1 leaves RAM for them, the XIP
# cache stall the RAM relocation below exists to avoid), stack protection adds
# a guard check to most functions, and ENABLE_HARDENING turns celt_assert into
# runtime checks that abort (a panic here). Off, before the subdirectory reads them.
set(OPUS_HARDENING OFF CACHE BOOL "libopus runtime hardening checks" FORCE)
set(OPUS_STACK_PROTECTOR OFF CACHE BOOL "libopus stack protection" FORCE)
set(OPUS_FORTIFY_SOURCE OFF CACHE BOOL "libopus _FORTIFY_SOURCE" FORCE)
add_subdirectory(lib/opus)

# The speaker encoder's Opus application. ON = OPUS_APPLICATION_RESTRICTED_CELT
# (libopus 1.6): CELT only, no SILK encoder state and no 4 ms SILK look-ahead
# buffer. At our settings (48 kHz stereo, 10 ms, 160 kbps CBR, complexity 0)
# OPUS_APPLICATION_AUDIO already sends only CELT full-band 10 ms stereo packets
# (TOC 0xF4, 200 bytes; checked on a PC over silence, tones, sweeps, noise,
# speech-like bursts and on/off), so the controller gets the same kind of
# packet. What changes: the encoder needs ~24 KB less heap on core1 (48.7 ->
# 24.4 KB) and the speaker is 4 ms earlier. OFF = OPUS_APPLICATION_AUDIO.
option(OPUS_ENCODER_CELT_ONLY "Speaker encoder: Opus CELT-only application (RESTRICTED_CELT)" ON)
if (OPUS_ENCODER_CELT_ONLY)
        set(OPUS_ENCODER_CELT_ONLY_VALUE 1)
else ()
        set(OPUS_ENCODER_CELT_ONLY_VALUE 0)
endif ()

# -fstack-usage on our sources: writes a .su file next to every object, which
# tools/analyze.sh sums up (deepest frames). Off for normal builds.
option(ENABLE_STACK_USAGE "Write -fstack-usage .su files for tools/analyze.sh" OFF)

# --- Core1 audio path in RAM (ported from upstream awalol Phases A.1-A.3, B.4
# queue ops, B.5). Running libopus and the resampler from flash left core1
# stalled on the XIP cache, which core0 (BTstack, TinyUSB, OLED) keeps evicting;
# the OLED Diagnostics "Drop r" counter showed ~7% of speaker chunks dropped
# because core1 couldn't encode in time. The Pico SDK linker places
# .time_critical.* in RAM (loaded from flash at boot).

# A.3: rename libopus .text/.rodata to .time_critical.* (~220 KB of RAM).
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/opus_ram_relocate.stamp
    COMMAND ${CMAKE_OBJCOPY} --rename-section .text=.time_critical.opus_text --rename-section .rodata=.time_critical.opus_rodata --rename-section .rodata.str1.4=.time_critical.opus_strings $<TARGET_FILE:opus>
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_BINARY_DIR}/opus_ram_relocate.stamp
    DEPENDS $<TARGET_FILE:opus>
    COMMENT "Relocating libopus to .time_critical.* sections"
    VERBATIM
)
add_custom_target(opus_ram_relocate ALL DEPENDS ${CMAKE_BINARY_DIR}/opus_ram_relocate.stamp)
add_dependencies(ds5-bridge opus_ram_relocate)

# A.2: the two WDL_Resampler methods called every audio frame.
target_compile_options(wdl_resampler PRIVATE -ffunction-sections)
add_custom_command(TARGET wdl_resampler POST_BUILD
    COMMAND ${CMAKE_OBJCOPY}
        --rename-section .text._ZN13WDL_Resampler11ResampleOutEPfiii=.time_critical.WDL_ResampleOut
        --rename-section .text._ZN13WDL_Resampler15ResamplePrepareEiiPPf=.time_critical.WDL_ResamplePrepare
        $<TARGET_FILE:wdl_resampler>
    COMMENT "Relocating WDL_Resampler hot-path methods to .time_critical.*"
)

# Rename hot .text.<fn> sections of an SDK object (located by path suffix) at
# PRE_LINK; see cmake/relocate_to_ram.cmake.
function(relocate_to_ram _suffix _renames _comment)
    add_custom_command(TARGET ds5-bridge PRE_LINK
        COMMAND ${CMAKE_COMMAND}
            "-DOBJROOT=${CMAKE_BINARY_DIR}/CMakeFiles/ds5-bridge.dir"
            "-DOBJCOPY=${CMAKE_OBJCOPY}"
            "-DSUFFIX=${_suffix}"
            "-DRENAMES=${_renames}"
            -P "${CMAKE_SOURCE_DIR}/cmake/relocate_to_ram.cmake"
        COMMENT "${_comment}"
        VERBATIM)
endfunction()

# B.4 (queue part): queue add/remove back the inter-core audio FIFOs.
relocate_to_ram(src/common/pico_util/queue.c.o
    ".text.queue_try_add=.time_critical.queue_try_add|.text.queue_try_remove=.time_critical.queue_try_remove"
    "pico_util queue add/remove -> RAM")

# B.5: RAM memcpy/memset/memmove (newlib's live in flash; libopus calls them
# every frame). Must not be lowered back into calls to themselves.
set_source_files_properties(src/ram_mem.c PROPERTIES COMPILE_OPTIONS
        "-fno-builtin;-fno-tree-loop-distribute-patterns")

target_compile_options(wdl_resampler PRIVATE -fsigned-char)
target_compile_options(ds5-bridge PRIVATE -fsigned-char)
target_compile_definitions(wdl_resampler PRIVATE
        WDL_RESAMPLE_TYPE=float
)
# The board's name as hosts show it (identity [30..45], protocol 1.7).
if (PICO_BOARD STREQUAL "pico_w")
        set(DS5_BOARD_NAME "Pico W")
elseif (PICO_BOARD STREQUAL "pico2_w")
        set(DS5_BOARD_NAME "Pico 2 W")
else ()
        set(DS5_BOARD_NAME "${PICO_BOARD}")
endif ()

target_compile_definitions(ds5-bridge PRIVATE
        DS5_BOARD_NAME="${DS5_BOARD_NAME}"
        CYW43_LWIP=0
        ENABLE_SERIAL=${ENABLE_SERIAL_VALUE}
        PICO_FLASH_ASSUME_CORE1_SAFE=0 # core1 registers for flash lockout (flash_safe.h)
        WDL_RESAMPLE_TYPE=float
        CYW43_PIO_CLOCK_DIV_INT=${CYW43_PIO_CLOCK_DIV_VALUE}
        PICO_STDIO_USB_USE_DEFAULT_DESCRIPTORS=0
        PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE=0
        PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=0
        ENABLE_VERBOSE=${ENABLE_VERBOSE_VALUE}
        ENABLE_BATT_LED=${ENABLE_BATT_LED_VALUE}
        SYS_CLOCK_KHZ=${SYS_CLOCK_KHZ_VALUE}
        DISABLE_SPEAKER_PROC=${DISABLE_SPEAKER_PROC_VALUE}
        OPUS_ENCODER_CELT_ONLY=${OPUS_ENCODER_CELT_ONLY_VALUE}
)
if (ENABLE_STACK_USAGE)
        target_compile_options(ds5-bridge PRIVATE -fstack-usage)
        target_compile_options(opus PRIVATE -fstack-usage)
        target_compile_options(wdl_resampler PRIVATE -fstack-usage)
endif ()

# Control-only vendor-class USB interface for the desktop configurator
# (docs: CLAUDE.md "Companion vendor interface"). Linux hid_playstation only
# binds HID, so unlike the June 0xFF5D HID companion this one is not hijacked;
# Windows binds WinUSB via MS OS 1.0 descriptors with no driver install.
option(ENABLE_COMPANION_VENDOR "Add the configurator's vendor USB interface" OFF)
if (ENABLE_COMPANION_VENDOR)
        set(ENABLE_COMPANION_VENDOR_VALUE 1)
        target_sources(ds5-bridge PRIVATE src/vendor_itf.cpp)
else ()
        set(ENABLE_COMPANION_VENDOR_VALUE 0)
endif ()
target_compile_definitions(ds5-bridge PRIVATE
        ENABLE_COMPANION_VENDOR=${ENABLE_COMPANION_VENDOR_VALUE}
)

# Issue #15 diagnostic builds (branch debug/issue15), never in a release:
#   D   core1 crash diagnostics: finer loop stages in 0xF5, a core1 HardFault
#       recorder, a stack guard + high-water mark, the loop heartbeat and the
#       board's flash (QMI) setup, all read with GET 0xEA (src/c1_diag.h)
#   F1  D + the XIP setup (boot2) embedded and run at boot with flash clock
#       divider 4, as awalol's upstream does (99da904, e5d99f0); still 320 MHz
#   F2  D + upstream v0.7.0's whole Pico 2 W clock and flash setup: 150 MHz
#       at the default core voltage, CYW43 PIO divider 2 (a023371), XIP setup
#       embedded with flash clock divider 2 (99da904, e5d99f0: 2 below 266 MHz)
#   F3  D + core1 idles with a RAM busy-wait instead of sleep_us
# Empty (the default) builds exactly what master builds. The version gets
# "+diag-<variant>.<commit>" so the OLED and the app show which one runs.
# (ISSUE15_DIAG itself is declared with the clock settings above.)
set(DS5_C1_DIAG_VALUE 0)
set(DS5_C1_BUSY_IDLE_VALUE 0)
if (NOT ISSUE15_DIAG STREQUAL "")
        if (NOT ISSUE15_DIAG MATCHES "^(D|F1|F2|F3)$")
                message(FATAL_ERROR "ISSUE15_DIAG must be empty, D, F1, F2 or F3 (got '${ISSUE15_DIAG}')")
        endif ()
        set(DS5_C1_DIAG_VALUE 1)
        target_sources(ds5-bridge PRIVATE src/c1_diag.h)
        target_link_libraries(ds5-bridge hardware_exception)
        if (ISSUE15_DIAG MATCHES "^F[12]$")
                if (PICO_W_BUILD)
                        message(FATAL_ERROR "ISSUE15_DIAG=${ISSUE15_DIAG} is RP2350 only (no XIP setup to embed on the Pico W)")
                endif ()
                # Upstream's rule (e5d99f0): divider 4 above 266 MHz, else 2.
                if (SYS_CLOCK_KHZ_VALUE GREATER 266000)
                        set(ISSUE15_FLASH_CLKDIV 4)
                else ()
                        set(ISSUE15_FLASH_CLKDIV 2)
                endif ()
                target_compile_definitions(bs2_default PRIVATE PICO_FLASH_SPI_CLKDIV=${ISSUE15_FLASH_CLKDIV})
                target_compile_definitions(ds5-bridge PRIVATE PICO_EMBED_XIP_SETUP=1)
                message(STATUS "Issue #15 ${ISSUE15_DIAG}: ${SYS_CLOCK_KHZ_VALUE} kHz, boot2 flash clock divider ${ISSUE15_FLASH_CLKDIV}, CYW43 PIO divider ${CYW43_PIO_CLOCK_DIV_VALUE}")
        elseif (ISSUE15_DIAG STREQUAL "F3")
                set(DS5_C1_BUSY_IDLE_VALUE 1)
        endif ()
        message(STATUS "Issue #15 diagnostic build: ${ISSUE15_DIAG}")
endif ()
target_compile_definitions(ds5-bridge PRIVATE
        DS5_C1_DIAG=${DS5_C1_DIAG_VALUE}
        DS5_C1_BUSY_IDLE=${DS5_C1_BUSY_IDLE_VALUE}
)

option(ENABLE_WAKE_HID "Wake the Windows host on PS-button press" OFF)
if(ENABLE_WAKE_HID)
    target_compile_definitions(ds5-bridge PRIVATE
            ENABLE_WAKE_HID
    )
    target_sources(ds5-bridge PRIVATE
            src/wake.cpp
    )
endif()

option(WAKE_DEBUG "Print wake-FSM transitions to UART (developer use)" OFF)
if(WAKE_DEBUG)
    target_compile_definitions(ds5-bridge PRIVATE
            WAKE_DEBUG
    )
endif()
# The firmware's version string (OLED Status header, the app's firmware
# version, picotool's program version). Releases pass the tag
# (release.yml / release.sh: -DVERSION=vX.Y.Z-oled-edition). Anything else is
# a dev build and says exactly what it was built from, from Git, worked out
# at every configure (never a stale value kept in an old build folder):
#   v0.9.3-oled-edition+3.a1b2c3d[.dirty]   3 commits after the tag, at a1b2c3d
# tools/dev_firmware.sh passes the same string explicitly on every build.
set(VERSION "" CACHE STRING "Release tag (vX.Y.Z-oled-edition); empty = a dev build, stamped from Git")
if (VERSION STREQUAL "" OR VERSION STREQUAL "dev")
        execute_process(
                COMMAND git describe --tags --match "v*-oled-edition" --long --dirty
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                OUTPUT_VARIABLE GIT_DESCRIBE OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if (GIT_DESCRIBE MATCHES "^(v.+-oled-edition)-([0-9]+)-g([0-9a-f]+)(-dirty)?$")
                set(FW_VERSION "${CMAKE_MATCH_1}+${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
                if (CMAKE_MATCH_4)
                        string(APPEND FW_VERSION ".dirty")
                endif ()
        else ()
                set(FW_VERSION "v0.0.0-oled-edition+unknown")
        endif ()
else ()
        set(FW_VERSION "${VERSION}")
endif ()
if (NOT ISSUE15_DIAG STREQUAL "")
        # v0.9.5-dev.7-oled-edition+3.a1b2c3d -> v0.9.5-dev.7-oled-edition+diag-D.a1b2c3d
        execute_process(
                COMMAND git rev-parse --short=7 HEAD
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                OUTPUT_VARIABLE GIT_SHORT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        string(REGEX REPLACE "\\+.*$" "" FW_VERSION "${FW_VERSION}")
        string(APPEND FW_VERSION "+diag-${ISSUE15_DIAG}")
        if (GIT_SHORT)
                string(APPEND FW_VERSION ".${GIT_SHORT}")
        endif ()
endif ()
message(STATUS "Firmware version: ${FW_VERSION}")
target_compile_definitions(ds5-bridge PRIVATE
        FIRMWARE_VERSION="${FW_VERSION}"
)

set_target_properties(ds5-bridge PROPERTIES OUTPUT_NAME "ds5-bridge-oled")
pico_set_program_name(ds5-bridge "ds5-bridge-oled")
pico_set_program_version(ds5-bridge "${FW_VERSION}")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(ds5-bridge 1)
pico_enable_stdio_usb(ds5-bridge ${ENABLE_SERIAL_VALUE})

# Add the standard include files to the build
#target_include_directories(ds5-bridge PRIVATE
#        ${CMAKE_CURRENT_LIST_DIR}
#)

target_include_directories(${PROJECT_NAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Add any user requested libraries
target_link_libraries(ds5-bridge
        pico_stdlib
        pico_multicore
        pico_flash
        hardware_watchdog
        hardware_timer
        hardware_flash
        hardware_spi
        hardware_adc
        pico_btstack_classic
#        pico_cyw43_arch_threadsafe_background
        pico_cyw43_arch_poll
        pico_btstack_cyw43
        tinyusb_device
        tinyusb_board
        wdl_resampler
        opus
)

# Count every flash_safe_execute() (ours and BTstack's link-key storage) for
# the 0xF5 diagnostics; see __wrap_flash_safe_execute in src/main.cpp.
target_link_options(ds5-bridge PRIVATE -Wl,--wrap=flash_safe_execute)

pico_add_extra_outputs(ds5-bridge)

