// The bits of the C++ runtime Plaits' classes reference (the JieLi toolchain has no C++ library).
// Nothing is ever allocated or deleted: the stubs only satisfy the vtables.
#include <stddef.h>

extern "C" void __cxa_pure_virtual() {
  for (;;) {
  }
}

void operator delete(void*) {}
void operator delete(void*, size_t) {}
