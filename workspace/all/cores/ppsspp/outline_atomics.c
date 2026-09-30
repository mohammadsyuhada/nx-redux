/* GCC 10+ outline-atomics helpers referenced by PPSSPP's prebuilt aarch64 ffmpeg
 * (ffmpeg/linux/aarch64/lib/*.a). tg5040's GCC 8.3 libgcc lacks them; tg5050's
 * GCC 10 provides them. Same semantics: return the old value. Hidden so the
 * core does not export them. */
#include <stdint.h>
#define HIDDEN __attribute__((visibility("hidden")))
HIDDEN uint64_t __aarch64_cas8_acq_rel(uint64_t expected, uint64_t desired, uint64_t* ptr) {
	__atomic_compare_exchange_n(ptr, &expected, desired, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
	return expected;
}
HIDDEN uint32_t __aarch64_ldadd4_acq_rel(uint32_t val, uint32_t* ptr) {
	return __atomic_fetch_add(ptr, val, __ATOMIC_ACQ_REL);
}
