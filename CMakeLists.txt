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
set(sdkVersion 2.3.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.0)
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
# Pico 2 W clock. The default is upstream awalol v0.7.0's (a023371): the
# RP2350's rated 150 MHz at the boot-default core voltage, CYW43 PIO clock
# divider 2; main() leaves clock and voltage as the bootrom/SDK set them.
# DS5_CLOCK_KHZ=320000 builds the old overclock (1.20 V, PIO divider 5) as a
# fallback for one release; it goes away after that.
set(DS5_CLOCK_KHZ 150000 CACHE STRING "Pico 2 W clk_sys in kHz: 150000 (default) or 320000 (old overclock, fallback)")
if (PICO_W_BUILD)
        set(CYW43_PIO_CLOCK_DIV_VALUE 3)
        set(SYS_CLOCK_KHZ_VALUE 200000)
        set(DISABLE_SPEAKER_PROC_VALUE 1)
elseif (DS5_CLOCK_KHZ STREQUAL "150000")
        set(CYW43_PIO_CLOCK_DIV_VALUE 2)
        set(SYS_CLOCK_KHZ_VALUE 150000)
elseif (DS5_CLOCK_KHZ STREQUAL "320000")
        set(CYW43_PIO_CLOCK_DIV_VALUE 5)
        set(SYS_CLOCK_KHZ_VALUE 320000)
else ()
        message(FATAL_ERROR "DS5_CLOCK_KHZ must be 150000 or 320000 (got '${DS5_CLOCK_KHZ}')")
endif ()



# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(ds5-bridge C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# The pins are in tools/toolchain.env (Pico SDK 2.3.0, TinyUSB 0.21 at upstream's
# commit). The relocate_to_ram() section names below are TinyUSB 0.21's, and a
# name that matches nothing silently leaves that code in flash, so refuse any
# other TinyUSB minor version rather than build a slower firmware.
# (The SDK resolves PICO_TINYUSB_PATH in its own directory scope, the same way.)
set(_tusb_dir "${PICO_TINYUSB_PATH}")
if (NOT _tusb_dir)
    set(_tusb_dir "$ENV{PICO_TINYUSB_PATH}")
endif ()
if (NOT _tusb_dir)
    set(_tusb_dir "${PICO_SDK_PATH}/lib/tinyusb")
endif ()
file(STRINGS "${_tusb_dir}/src/tusb_option.h" _tusb_ver
     REGEX "^#define TUSB_VERSION_(MAJOR|MINOR) +[0-9]+")
string(REGEX REPLACE ".*MAJOR +([0-9]+).*MINOR +([0-9]+).*" "\\1.\\2" _tusb_ver "${_tusb_ver}")
if (NOT _tusb_ver STREQUAL "0.21")
    message(FATAL_ERROR "TinyUSB at ${_tusb_dir} is ${_tusb_ver}; this firmware needs 0.21 "
        "(tools/toolchain.env: Pico SDK 2.3.0 with lib/tinyusb at TINYUSB_REF; see CLAUDE.md, Build)")
endif ()

# Add executable. Default name is the project name, version 0.1

add_executable(ds5-bridge
        src/usb_descriptors.cpp
        src/main.cpp
        src/usb.cpp
        src/usb_power.cpp
        src/audio.cpp
        src/bt.cpp
        src/config.cpp
        src/cmd.cpp
        src/state_mgr.cpp
        src/status_gpio.cpp
        src/oled.cpp
        src/calib.cpp
        src/slots.cpp
        src/remap.cpp
        src/voice.cpp
        src/perf.cpp
        src/ctrl_info.cpp
        src/dse.cpp
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

# --- Core0's Bluetooth/USB hot path in RAM (upstream awalol Phases B.2-B.4),
# RP2350 only (see src/core0_ram.h: the Pico W's RP2040 can't spare the RAM).
# Section names are TinyUSB 0.21's (pico-sdk 2.3.0; the configure check above
# refuses another TinyUSB). A rename that matches nothing does nothing: 0.21's
# FIFO bulk movers are tu_fifo_{read,write}_n_access_mode, and tu_fifo_read_n /
# write_n / count became always-inline wrappers with no section of their own
# (upstream 76690ab). tools/check_ram_residency.sh checks every one of these
# landed in SRAM.
if (PICO_RP2350)
# B.2: per-packet BT/USB transport.
relocate_to_ram(lib/btstack/src/hci.c.o
    ".text.packet_handler=.time_critical.bt_packet_handler"
    "B.2: btstack hci.c packet_handler (HCI event/ACL dispatch) -> RAM")
relocate_to_ram(lib/btstack/src/l2cap.c.o
    ".text.l2cap_acl_handler=.time_critical.l2cap_acl_handler"
    "B.2: btstack l2cap.c l2cap_acl_handler (inbound demux) -> RAM")
relocate_to_ram(lib/tinyusb/src/common/tusb_fifo.c.o
    ".text.tu_fifo_read=.time_critical.tu_fifo_read|.text.tu_fifo_read_n_access_mode=.time_critical.tu_fifo_read_n_access_mode|.text.tu_fifo_write=.time_critical.tu_fifo_write|.text.tu_fifo_write_n_access_mode=.time_critical.tu_fifo_write_n_access_mode"
    "B.2: tinyusb FIFO data path -> RAM")
relocate_to_ram(src/rp2_common/pico_cyw43_driver/cyw43_bus_pio_spi.c.o
    ".text.cyw43_spi_transfer=.time_critical.cyw43_spi_transfer|.text.cyw43_read_bytes=.time_critical.cyw43_read_bytes|.text.cyw43_write_bytes=.time_critical.cyw43_write_bytes|.text.cyw43_read_reg_u32=.time_critical.cyw43_read_reg_u32|.text.cyw43_read_reg_u16=.time_critical.cyw43_read_reg_u16|.text.cyw43_read_reg_u8=.time_critical.cyw43_read_reg_u8|.text.cyw43_write_reg_u32=.time_critical.cyw43_write_reg_u32|.text.cyw43_write_reg_u16=.time_critical.cyw43_write_reg_u16|.text.cyw43_write_reg_u8=.time_critical.cyw43_write_reg_u8|.text.read_reg_u32_swap=.time_critical.read_reg_u32_swap|.text.write_reg_u32_swap=.time_critical.write_reg_u32_swap|.text.ns_delay.constprop.0=.time_critical.cyw43_ns_delay"
    "B.2: cyw43 PIO-SPI transport -> RAM")
relocate_to_ram(lib/cyw43-driver/src/cyw43_ll.c.o
    ".text.cyw43_ll_sdpcm_poll_device=.time_critical.cyw43_ll_sdpcm_poll_device|.text.cyw43_ll_process_packets=.time_critical.cyw43_ll_process_packets|.text.cyw43_sdpcm_send_common=.time_critical.cyw43_sdpcm_send_common|.text.cyw43_do_ioctl.part.0=.time_critical.cyw43_do_ioctl|.text.cyw43_ll_bt_has_work=.time_critical.cyw43_ll_bt_has_work|.text.cyw43_ll_parse_async_event=.time_critical.cyw43_ll_parse_async_event"
    "B.2: cyw43 SDPCM pump -> RAM")
relocate_to_ram(lib/cyw43-driver/src/cyw43_ctrl.c.o
    ".text.cyw43_poll_func=.time_critical.cyw43_poll_func|.text.cyw43_bluetooth_hci_read=.time_critical.cyw43_bluetooth_hci_read|.text.cyw43_bluetooth_hci_write=.time_critical.cyw43_bluetooth_hci_write"
    "B.2: cyw43 poll + BT HCI bus -> RAM")
# B.3: run-loop TX pumps and per-transfer submit paths.
relocate_to_ram(lib/btstack/src/hci.c.o
    ".text.hci_run=.time_critical.hci_run|.text.hci_send_acl_packet_fragments=.time_critical.hci_send_acl_packet_fragments|.text.hci_send_acl_packet_buffer=.time_critical.hci_send_acl_packet_buffer"
    "B.3: btstack hci.c TX pump + ACL out -> RAM")
relocate_to_ram(lib/btstack/src/l2cap.c.o
    ".text.l2cap_run=.time_critical.l2cap_run|.text.l2cap_send=.time_critical.l2cap_send|.text.l2cap_send_prepared=.time_critical.l2cap_send_prepared|.text.l2cap_notify_channel_can_send=.time_critical.l2cap_notify_channel_can_send"
    "B.3: btstack l2cap.c TX pump + sends -> RAM")
relocate_to_ram(lib/cyw43-driver/src/cyw43_ll.c.o
    ".text.cyw43_ll_write_backplane_mem=.time_critical.cyw43_ll_write_backplane_mem|.text.cyw43_ll_read_backplane_mem=.time_critical.cyw43_ll_read_backplane_mem|.text.cyw43_write_backplane=.time_critical.cyw43_write_backplane|.text.cyw43_read_backplane.constprop.0=.time_critical.cyw43_read_backplane"
    "B.3: cyw43 backplane accessors -> RAM")
relocate_to_ram(lib/tinyusb/src/device/usbd.c.o
    ".text.tud_task_ext=.time_critical.tud_task_ext|.text.usbd_edpt_xfer=.time_critical.usbd_edpt_xfer"
    "B.3: tinyusb usbd.c task pump + xfer -> RAM")
relocate_to_ram(lib/tinyusb/src/portable/raspberrypi/rp2040/dcd_rp2040.c.o
    ".text.dcd_edpt_xfer=.time_critical.dcd_edpt_xfer|.text.dcd_edpt_iso_activate=.time_critical.dcd_edpt_iso_activate"
    "B.3: tinyusb dcd_rp2040.c submit + iso -> RAM")
relocate_to_ram(lib/tinyusb/src/class/audio/audio_device.c.o
    ".text.tud_audio_n_read=.time_critical.tud_audio_n_read|.text.tud_audio_n_write=.time_critical.tud_audio_n_write|.text.audiod_calc_tx_packet_sz.isra.0=.time_critical.audiod_calc_tx_packet_sz|.text.audiod_xfer_isr=.time_critical.audiod_xfer_isr"
    "B.3: tinyusb audio class data path -> RAM")
# B.4 (the rest; the queue ops above are on every board): input-report tail.
relocate_to_ram(lib/tinyusb/src/class/hid/hid_device.c.o
    ".text.tud_hid_n_report=.time_critical.tud_hid_n_report|.text.tud_hid_n_ready=.time_critical.tud_hid_n_ready|.text.hidd_xfer_cb=.time_critical.hidd_xfer_cb"
    "B.4: tinyusb HID report path -> RAM")
relocate_to_ram(src/rp2_common/pico_cyw43_arch/cyw43_arch.c.o
    ".text.cyw43_arch_poll=.time_critical.cyw43_arch_poll"
    "B.4: cyw43_arch_poll -> RAM")
relocate_to_ram(lib/btstack/src/btstack_run_loop.c.o
    ".text.btstack_run_loop_base_poll_data_sources=.time_critical.btstack_run_loop_base_poll_data_sources|.text.btstack_run_loop_poll_data_sources_from_irq=.time_critical.btstack_run_loop_poll_data_sources_from_irq"
    "B.4: btstack run-loop data-source poll -> RAM")
# a2772a9: bt_write's send request, and the linked-list iterators BTstack's
# run loops walk every pass.
relocate_to_ram(lib/btstack/src/l2cap.c.o
    ".text.l2cap_request_can_send_now_event=.time_critical.l2cap_request_can_send_now_event"
    "a2772a9: btstack l2cap_request_can_send_now_event -> RAM")
relocate_to_ram(lib/btstack/src/btstack_linked_list.c.o
    ".text.btstack_linked_list_iterator_init=.time_critical.btstack_linked_list_iterator_init|.text.btstack_linked_list_iterator_has_next=.time_critical.btstack_linked_list_iterator_has_next|.text.btstack_linked_list_iterator_next=.time_critical.btstack_linked_list_iterator_next"
    "a2772a9: btstack linked-list iterators -> RAM")
endif ()

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

# Flash (XIP) setup, RP2350 only. The bootrom leaves the QMI flash interface
# at a slow, conservative setting unless the image carries its own XIP setup
# (boot2); crt0 then runs it at boot (PICO_EMBED_XIP_SETUP, SDK 2.x) with
# PICO_FLASH_SPI_CLKDIV. Upstream awalol does the same (99da904, e5d99f0):
# divider 4 above 266 MHz, else 2. Without it core1 crashed on some boards
# (issue #15). The RP2040 (Pico W) always runs its boot2, at the board's
# divider 2, as upstream leaves it.
if (PICO_PLATFORM MATCHES "^rp2350")
        if (SYS_CLOCK_KHZ_VALUE GREATER 266000)
                set(DS5_FLASH_CLKDIV 4)
        else ()
                set(DS5_FLASH_CLKDIV 2)
        endif ()
        target_compile_definitions(bs2_default PRIVATE PICO_FLASH_SPI_CLKDIV=${DS5_FLASH_CLKDIV})
        target_compile_definitions(ds5-bridge PRIVATE PICO_EMBED_XIP_SETUP=1)
endif ()
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
# Keep the start of saved state in the image (src/config.cpp; nothing reads it)
# for tools/check_flash_reserved.py: a required symbol is a --gc-sections root.
target_link_options(ds5-bridge PRIVATE -Wl,--require-defined=ds5_reserved_flash_start)

pico_add_extra_outputs(ds5-bridge)

# Gate (issue #15): an RP2350 build must embed its XIP setup, and the flash
# clock it sets (clk_sys / divider) must stay within ~100 MHz. Checked on the
# final target properties, so a later edit that drops either fails here.
# tools/check_xip_setup.sh checks the built ELF for the same thing.
if (PICO_PLATFORM MATCHES "^rp2350")
        get_target_property(_ds5_defs ds5-bridge COMPILE_DEFINITIONS)
        if (NOT "PICO_EMBED_XIP_SETUP=1" IN_LIST _ds5_defs)
                message(FATAL_ERROR "RP2350 build without PICO_EMBED_XIP_SETUP=1 on ds5-bridge: the flash keeps the bootrom's setup (issue #15)")
        endif ()
        get_target_property(_bs2_defs bs2_default COMPILE_DEFINITIONS)
        set(_ds5_clkdiv "")
        foreach (_d IN LISTS _bs2_defs)
                if (_d MATCHES "^PICO_FLASH_SPI_CLKDIV=([0-9]+)$")
                        set(_ds5_clkdiv ${CMAKE_MATCH_1})
                endif ()
        endforeach ()
        if (_ds5_clkdiv STREQUAL "" OR _ds5_clkdiv EQUAL 0)
                message(FATAL_ERROR "RP2350 build without PICO_FLASH_SPI_CLKDIV on bs2_default (issue #15)")
        endif ()
        math(EXPR _ds5_flash_khz "${SYS_CLOCK_KHZ_VALUE} / ${_ds5_clkdiv}")
        if (_ds5_flash_khz GREATER 100000)
                message(FATAL_ERROR "Flash clock ${_ds5_flash_khz} kHz (clk_sys ${SYS_CLOCK_KHZ_VALUE} kHz / divider ${_ds5_clkdiv}) is above 100 MHz")
        endif ()
        message(STATUS "Pico 2 W: clk_sys ${SYS_CLOCK_KHZ_VALUE} kHz, flash ${_ds5_flash_khz} kHz (divider ${_ds5_clkdiv}, embedded XIP setup), CYW43 PIO divider ${CYW43_PIO_CLOCK_DIV_VALUE}")
endif ()

