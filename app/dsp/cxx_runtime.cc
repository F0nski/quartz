// The bits of the C++ runtime Plaits' classes reference (the JieLi toolchain has no C++ library).
// Nothing is ever allocated or deleted: the stubs only satisfy the vtables.
#include <stddef.h>

// The offset of the audio's code in the instruction cache's 4 KiB ways (fm1.toml o2_first: the DSP linked first, then the
// platform's and the render's code, the screen's last): the size measured best on the device (docs/cpu.md). pq_init
// reads it so that the link keeps it.
extern "C" __attribute__((section(".text.pq_place"))) const unsigned char pq_text_pad[512] = {0};

extern "C" void __cxa_pure_virtual() {
  for (;;) {
  }
}

void operator delete(void*) {}
void operator delete(void*, size_t) {}
