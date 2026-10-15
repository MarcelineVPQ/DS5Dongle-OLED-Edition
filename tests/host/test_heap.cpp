// Counts heap allocations (every operator new), so a test can check that a
// main-loop path doesn't touch the heap (lifetime pass batch 3: the 0x81
// pollers). malloc/free underneath, so ASan still sees every block.
#include <cstdlib>
#include <new>
#include "fakes.h"

long g_heap_allocs = 0;

void *operator new(std::size_t n) {
    g_heap_allocs++;
    if (void *p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void *operator new[](std::size_t n) { return operator new(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
