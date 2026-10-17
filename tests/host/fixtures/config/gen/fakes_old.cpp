// Minimal fakes so an old tag's config.cpp links on the PC.
#include <cstdint>
#include <cstring>
#include <cstddef>
#include "hardware/flash.h"
#include "pico.h"
uint8_t g_fake_flash[PICO_FLASH_SIZE_BYTES];
void flash_range_erase(uint32_t offset, size_t count) { std::memset(g_fake_flash + offset, 0xFF, count); }
void flash_range_program(uint32_t offset, const uint8_t *data, size_t count) {
    for (size_t i = 0; i < count; i++) g_fake_flash[offset + i] &= data[i];
}
extern "C" uint32_t save_and_disable_interrupts() { return 0; }
extern "C" void restore_interrupts(uint32_t) {}
extern "C" void restore_interrupts_from_disabled(uint32_t) {}
int flash_safe_execute(void (*fn)(void *), void *param, uint32_t) { fn(param); return 0; }
void cyw43_arch_gpio_put(uint32_t, bool) {}
