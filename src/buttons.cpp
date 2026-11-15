// Tension buttons (protocol 1.24): the board side of applying a layout. The
// layout itself (parse, validate, compile, apply) is pure, in buttons.h;
// tension.cpp decides which one runs (docs/TENSION_BUTTONS.md section 3.7)
// and hands it here. Core0 only: tension.cpp changes it from the main loop
// and TinyUSB callbacks, interrupt_loop reads it, all on core0's loop.

#include "buttons.h"

#include "core0_ram.h"
#include "remap.h"

namespace {
const buttons::Layout *g_layout = nullptr;  // the running set's buttons, or nullptr: the legacy table
}  // namespace

void buttons_run(const buttons::Layout *layout) { g_layout = layout; }

void CORE0_RAM_FUNC(buttons_apply)(uint8_t *report) {
    if (g_layout) buttons::apply(*g_layout, report);
    else remap_apply(report);  // levels 3-4: today's temporary or saved table, unchanged
}
