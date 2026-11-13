// The bits of the C++ runtime Plaits' classes reference (the JieLi toolchain has no C++ library).
// Nothing is ever allocated or deleted: the stubs only satisfy the vtables.
#include <stddef.h>

#ifdef PLAT_WEB
// The browser's and the .rfm1's builds: libc++ compiles std::sort's float instance in its library (its header only
// declares it), which these builds do not link either. Plaits sorts the few notes of a chord (chord_bank.h).
extern "C" void quartz_sort_float(float* first, float* last, void*) __asm__("_ZNSt3__26__sortIRNS_6__lessIffEEPfEEvT0_S5_T_");
extern "C" void quartz_sort_float(float* first, float* last, void*) {
  for (float* i = first + 1; i < last; ++i) {
    float v = *i;
    float* j = i;
    for (; j > first && v < j[-1]; --j) *j = j[-1];
    *j = v;
  }
}
#endif

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
