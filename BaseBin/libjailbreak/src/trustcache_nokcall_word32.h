#ifndef TRUSTCACHE_NOKCALL_WORD32_H
#define TRUSTCACHE_NOKCALL_WORD32_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Returns 0 when the current primitive set can execute and physically verify
 * one protected word replacement. A direct protected writer owns its cache
 * coherency; mapped physical access is required only for the single-store
 * fallback.
 */
int tcn_word32_environment_status(void);

/*
 * Replaces one aligned protected 32-bit word.
 *
 * Success means the post-write physical readback was exactly `desired`.
 * On every nonzero result, callers must reobserve and classify the complete
 * owning object before deciding whether to retry, continue, or recover.
 *
 * `observedOut` is authoritative only when this function completed a read.
 * In particular:
 *   - EAGAIN means a pre-write observation was not `expected`;
 *   - EINPROGRESS means a write-side step was ambiguous, but the final
 *     readback was exactly `expected` or `desired`;
 *   - EIO means the final readback was neither `expected` nor `desired`.
 *
 * The caller owns higher-level serialization. This function never retries a
 * write and never rolls one back.
 */
int tcn_word32_replace(uint64_t address, uint32_t expected, uint32_t desired, uint32_t *observedOut);

#ifdef __cplusplus
}
#endif

#endif
