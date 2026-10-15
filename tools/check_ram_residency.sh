#!/usr/bin/env bash
# Core1's audio path must run from RAM (CLAUDE.md, "Core1's audio path must
# stay in RAM"): checks a built ELF for the two ways that broke before.
#   1. No __memcpy_chk / __memset_chk / __memmove_chk (libopus fortify put
#      them in flash, so core1 left RAM every frame).
#   2. Core1's entry, its frame functions, the libopus encode/decode entry
#      points, the two WDL_Resampler hot methods, the SDK queue ops and our
#      memcpy/memset/memmove all sit in SRAM (0x20000000-0x20080000).
#      A symbol the build doesn't have (the Pico W build has no speaker
#      encoder) is skipped; the always-present ones must exist.
#   3. Core0's Bluetooth/USB hot path (upstream Phases B.1-B.4, the A.1
#      audio_loop remainder) sits in SRAM too. Most of it is renamed into
#      .time_critical.* by relocate_to_ram() in CMakeLists.txt, where a wrong
#      section name silently does nothing (upstream 76690ab), so every name
#      is checked here. "file.c:sym" picks a file-local symbol by its source
#      file (btstack has several static packet_handler functions). RP2350
#      builds only: the Pico W (RP2040) keeps core0 in flash (src/core0_ram.h).
#      Inline per-frame helpers (src/haptics_decimate.h, src/grip_wave.h) are
#      checked only if the compiler kept an out-of-line copy.
#   Run by .github/workflows/build.yml and release.yml after each build.
#   tools/check_ram_residency.sh path/to/ds5-bridge.elf
set -euo pipefail
ELF="${1:?usage: tools/check_ram_residency.sh ELF}"
NM="${NM:-arm-none-eabi-nm}"
READELF="${READELF:-arm-none-eabi-readelf}"
SYMS="$("$NM" "$ELF")"
bad=0
chk="$(awk '$3 ~ /^__mem(cpy|set|move)_chk$/ {print $3}' <<<"$SYMS" | sort -u)"
if [ -n "$chk" ]; then
  echo "check_ram_residency: fortify helpers linked (they run from flash): $chk" >&2
  bad=1
fi
in_ram() { local a=$((16#$1)); (( a >= 0x20000000 && a < 0x20080000 )); }
# speaker_proc / mic_proc are inlined into core1_entry. memcpy / memset are
# the SDK's ROM wrappers on RP2040 (no symbol of that name), ours on RP2350.
REQUIRED="_Z11core1_entryv memmove queue_try_add queue_try_remove opus_decode celt_decode_with_ec"
OPTIONAL="memcpy memset opus_encode_float opus_encode_native celt_encode_with_ec
  _ZN13WDL_Resampler11ResampleOutEPfiii _ZN13WDL_Resampler15ResamplePrepareEiiPPf"
for sym in $REQUIRED $OPTIONAL; do
  addr="$(awk -v s="$sym" '$3 == s && $2 ~ /^[tTwW]$/ {print $1; exit}' <<<"$SYMS")"
  if [ -z "$addr" ]; then
    case " $REQUIRED " in *" $sym "*) echo "check_ram_residency: $sym not found" >&2; bad=1 ;; esac
    continue
  fi
  if ! in_ram "$addr"; then
    echo "check_ram_residency: $sym is at 0x$addr, not in RAM" >&2
    bad=1
  fi
done

# Core0 hot path (RP2350 only; its SRAM ends at 0x20082000, the RP2040's at
# 0x20042000): every one must exist.
top="$(awk '$3 == "__StackTop" {print $1; exit}' <<<"$SYMS")"
CORE0=""
[ -n "$top" ] && (( 16#$top > 0x20042000 )) && CORE0="
  bt.cpp:_ZL18hci_packet_handlerhtPht bt.cpp:_ZL20l2cap_packet_handlerhtPht
  _Z8bt_writePKht _Z14interrupt_loopv _Z10on_bt_data12CHANNEL_TYPEPht
  hci.c:packet_handler l2cap.c:l2cap_acl_handler
  tu_fifo_read_n_access_mode tu_fifo_write_n_access_mode
  cyw43_spi_transfer cyw43_read_bytes cyw43_write_bytes
  cyw43_read_reg_u32 cyw43_read_reg_u16 cyw43_read_reg_u8
  cyw43_write_reg_u32 cyw43_write_reg_u16 cyw43_write_reg_u8
  read_reg_u32_swap write_reg_u32_swap cyw43_bus_pio_spi.c:ns_delay.constprop.0
  cyw43_ll.c:cyw43_ll_sdpcm_poll_device cyw43_ll_process_packets
  cyw43_ll.c:cyw43_sdpcm_send_common cyw43_ll.c:cyw43_do_ioctl.part.0
  cyw43_ll_bt_has_work cyw43_ll.c:cyw43_ll_parse_async_event
  cyw43_ctrl.c:cyw43_poll_func cyw43_bluetooth_hci_read cyw43_bluetooth_hci_write
  hci.c:hci_run hci.c:hci_send_acl_packet_fragments hci_send_acl_packet_buffer
  l2cap.c:l2cap_run l2cap_send l2cap_send_prepared l2cap.c:l2cap_notify_channel_can_send
  cyw43_ll_write_backplane_mem cyw43_ll_read_backplane_mem
  cyw43_ll.c:cyw43_write_backplane cyw43_ll.c:cyw43_read_backplane.constprop.0
  tud_task_ext usbd_edpt_xfer dcd_edpt_xfer dcd_edpt_iso_activate
  tud_audio_n_read tud_audio_n_write audio_device.c:audiod_calc_tx_packet_sz.isra.0 audiod_xfer_isr
  tud_hid_n_report tud_hid_n_ready hidd_xfer_cb cyw43_arch_poll
  btstack_run_loop_base_poll_data_sources btstack_run_loop_poll_data_sources_from_irq
  l2cap_request_can_send_now_event btstack_linked_list_iterator_init
  btstack_linked_list_iterator_has_next btstack_linked_list_iterator_next
  crc32_lookup_table _Z10audio_loopv audio.cpp:_ZL13audio_bt_taskv _Z13mic_add_queuePKh
"
# Inline helpers audio_loop calls per frame (src/haptics_decimate.h,
# src/audio_levels.h, src/mic_level.h, and the haptic feedback test's grip_test_task with
# src/grip_wave.h): normally folded into audio_loop, but an out-of-line copy
# must be in RAM too. TinyUSB 0.21's single-item tu_fifo_read / tu_fifo_write
# have no caller in this firmware (the linker drops them); renamed anyway, so checked if linked.
CORE0_OPTIONAL=""
[ -n "$CORE0" ] && CORE0_OPTIONAL="_ZN15HapticDecimator4takeEv _Z12haptic_to_s8ff tu_fifo_read tu_fifo_write
  audio.cpp:_ZL14grip_test_taskv _ZN8GripWave4nextEPaff _ZN8GripWave4fillEPaiff _ZNK8GripWave6activeEv
  _ZN16AudioReportClock3dueEy _ZN16AudioReportClock4sentEy _ZN16AudioReportClock5resetEv
  _ZNK21NativeHapticsPresence6silentEv _ZN21NativeHapticsPresence6recordEt
  _ZNK9AudioPeak5levelEy _ZN9AudioPeak6recordEty
  _ZN8MicLevel6sampleEji _ZN8MicLevel4dropEji _Z14mic_frame_peakPKsi"
# FUNC/OBJECT symbols as "file:name addr" (locals, grouped after their FILE
# entry) and "name addr" (globals and weak), one per line.
TABLE="$("$READELF" -sW "$ELF" | awk '
  $4 == "FILE" { file = $8; next }
  ($4 == "FUNC" || $4 == "OBJECT") && $8 != "" {
    if ($5 == "LOCAL") print file ":" $8, $2; else print $8, $2 }')"
for sym in $CORE0; do
  addr="$(awk -v s="$sym" '$1 == s {print $2; exit}' <<<"$TABLE")"
  if [ -z "$addr" ]; then
    echo "check_ram_residency: core0 $sym not found" >&2
    bad=1
  elif ! in_ram "$addr"; then
    echo "check_ram_residency: core0 $sym is at 0x$addr, not in RAM" >&2
    bad=1
  fi
done
for sym in $CORE0_OPTIONAL; do
  addr="$(awk -v s="$sym" '$1 == s {print $2; exit}' <<<"$TABLE")"
  if [ -n "$addr" ] && ! in_ram "$addr"; then
    echo "check_ram_residency: core0 $sym is at 0x$addr, not in RAM" >&2
    bad=1
  fi
done
if [ "$bad" = 0 ]; then
  if [ -n "$CORE0" ]; then
    echo "check_ram_residency: core1's audio path and core0's BT/USB hot path are RAM-resident"
  else
    echo "check_ram_residency: core1's audio path is RAM-resident (RP2040: core0 stays in flash)"
  fi
fi
exit "$bad"
