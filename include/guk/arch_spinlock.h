#if defined(__i386__) || defined(__x86_64__)
#include <x86/arch_spinlock.h>
#else
#error architecture not supported
#endif
