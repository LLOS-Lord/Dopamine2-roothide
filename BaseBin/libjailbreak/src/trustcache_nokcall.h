#ifndef TRUSTCACHE_NOKCALL_H
#define TRUSTCACHE_NOKCALL_H

#include <stdbool.h>
#include <stdint.h>

#include "trustcache_structs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*trustcache_nokcall_signed_loader)(void *context, uint8_t sourceKind);

bool trustcache_nokcall_is_required(void);

/*
 * PID 1 owns all runtime mutation. The loader and loaderContext must remain
 * valid for the launchd process lifetime.
 */
int trustcache_nokcall_owner_prepare_and_recover(trustcache_nokcall_signed_loader loader, void *loaderContext);
/*
 * Retry an already prepared owner after a transient dependency failure.
 * The controller and loader registered by prepare remain authoritative.
 */
int trustcache_nokcall_owner_recover(void);
int trustcache_nokcall_owner_prepare_runtime_pair(void);
int trustcache_nokcall_owner_signed_sources_present(bool *osPresentOut, bool *appPresentOut);

/*
 * The sole pre-owner mutation API. It creates a transient controller without
 * a signed loader, publishes one unpaired bootstrap carrier, and verifies
 * every requested hash before returning success.
 */
int trustcache_nokcall_bootstrap_append_entries(const trustcache_entry_v1 *entries, uint32_t entryCount);

/*
 * Append-only runtime API. PID 1 calls the owner directly; every other process
 * delegates to the PID-1 Root XPC domain.
 */
int trustcache_nokcall_append_entries(const trustcache_entry_v1 *entries, uint32_t entryCount);
int trustcache_nokcall_query_cdhash(const cdhash_t hash, bool *foundOut);

#ifdef __cplusplus
}
#endif

#endif
