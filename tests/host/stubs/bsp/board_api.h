// Host-test stub: the board API src/usb_descriptors.cpp uses (fakes.cpp).
#pragma once
#include <cstddef>
#include <cstdint>
size_t board_usb_get_serial(uint16_t desc_str1[], size_t max_chars);
