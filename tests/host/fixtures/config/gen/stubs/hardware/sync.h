#pragma once
#include "pico.h"
static inline void __dmb() {}
extern "C" uint32_t save_and_disable_interrupts();
extern "C" void restore_interrupts(uint32_t);
extern "C" void restore_interrupts_from_disabled(uint32_t);
