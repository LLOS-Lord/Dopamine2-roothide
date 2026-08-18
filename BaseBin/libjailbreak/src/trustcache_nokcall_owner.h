#ifndef TRUSTCACHE_NOKCALL_OWNER_H
#define TRUSTCACHE_NOKCALL_OWNER_H

#include <stdbool.h>
#include <stdint.h>

#include "trustcache_nokcall_controller.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TCNO_STATE_RECOVERING = 1,
    TCNO_STATE_READY,
} tcno_state;

typedef void (*tcno_resources_destroy)(void *context);

/*
 * This function always consumes `resourcesContext`: after it returns, the
 * caller must never destroy that resource. Rejected/failed preparation calls
 * invoke `resourcesDestroy` synchronously. Once controller creation succeeds,
 * the launchd owner retains the resource for the process lifetime.
 *
 * Bootstrap is deliberately outside the owner: it is the one pre-loader
 * exception. Once the runtime loader is registered, a clean no-carrier state
 * is Ready because the controller can expand and pair carriers through its
 * normal append path.
 */
int tcno_prepare_and_recover(const tcnc_config *config,
                             const tcnc_backend *backend,
                             tcno_resources_destroy resourcesDestroy,
                             void *resourcesContext);

/*
 * Explicit initial/retry recovery is synchronous and PID-1-only. Admission
 * remains Ready-only; a mutating operation admitted while Ready may complete
 * its own recovery before returning. New calls see EAGAIN during that window.
 */
int tcno_recover(void);

/*
 * Explicit first-load capability preparation. Failure leaves a recovered
 * singleton owner Ready whenever kernel state is still unambiguous.
 */
int tcno_prepare_runtime_pair(void);

int tcno_append(const tcnm_entry *entries, uint32_t entryCount);
int tcno_query(const uint8_t hash[TCNM_HASH_SIZE], bool *foundOut);
int tcno_copy_entries(tcnm_entry **entriesOut, uint32_t *entryCountOut);
int tcno_signed_sources_present(bool *osPresentOut, bool *appPresentOut);

tcno_state tcno_state_get(void);

/*
 * Returns 0 only while Ready. Unavailable/recovering owners return EAGAIN.
 */
int tcno_status(void);

#ifdef __cplusplus
}
#endif

#endif
