#ifndef TRUSTCACHE_NOKCALL_KERNEL_H
#define TRUSTCACHE_NOKCALL_KERNEL_H

#include <stdint.h>

#include "trustcache_nokcall_controller.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tcnk_kernel tcnk_kernel;

typedef int (*tcnk_reload_signed_source)(void *context, uint8_t sourceKind);

/*
 * Resolves the loadable trust-cache list and maps the current cryptex app/os
 * signed sources. This operation reads kernel state but never changes it.
 *
 * `reload` may be NULL while the pre-PID1 bootstrap owner is active. In that
 * case the backend reports ENOTSUP for expansion until a launchd-owned adapter
 * is created with a reload callback.
 */
int tcnk_kernel_create(tcnk_reload_signed_source reload, void *reloadContext, tcnk_kernel **kernelOut);

/*
 * Produces caller-owned value views into `kernel`. The signed-source byte
 * mappings and backend context remain valid until tcnk_kernel_destroy().
 */
int tcnk_kernel_prepare(tcnk_kernel *kernel, tcnc_config *configOut, tcnc_backend *backendOut);

void tcnk_kernel_destroy(tcnk_kernel *kernel);

/* Exact tcno_resources_destroy-compatible adapter for owner composition. */
void tcnk_kernel_destroy_context(void *context);

#ifdef __cplusplus
}
#endif

#endif
