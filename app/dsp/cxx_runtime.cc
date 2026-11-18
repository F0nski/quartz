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

extern "C" void __cxa_pure_virtual() {
  for (;;) {
  }
}

void operator delete(void*) {}
void operator delete(void*, size_t) {}
