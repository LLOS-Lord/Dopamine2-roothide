#include "trustcache_nokcall.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>

#include "primitives.h"
#include "info.h"
#include "jbclient_xpc.h"
#include "trustcache_nokcall_controller.h"
#include "trustcache_nokcall_kernel.h"
#include "trustcache_nokcall_model.h"
#include "trustcache_nokcall_owner.h"

#ifndef DEBUG
#define DEBUG 0
#endif

_Static_assert(sizeof(trustcache_entry_v1) == sizeof(tcnm_entry),
               "public and controller trust-cache entries must match");
_Static_assert(offsetof(trustcache_entry_v1, hash) == offsetof(tcnm_entry, hash),
               "trust-cache hash offsets must match");
_Static_assert(offsetof(trustcache_entry_v1, hash_type) == offsetof(tcnm_entry, hashType),
               "trust-cache hash type offsets must match");
_Static_assert(offsetof(trustcache_entry_v1, flags) == offsetof(tcnm_entry, flags),
               "trust-cache flag offsets must match");

static int trustcache_nokcall_debug_status(const char *phase, int status) {
#if DEBUG
    if (status != 0) {
        fprintf(stderr, "[trustcache_nokcall] phase=%s status=%d\n", phase, status);
    }
#else
    (void)phase;
#endif
    return status;
}

static int trustcache_nokcall_validate_entries(const trustcache_entry_v1 *entries, uint32_t entryCount) {
    if (!entries || entryCount == 0)
        return EINVAL;
    for (uint32_t index = 0; index < entryCount; index++) {
        if (tcnm_hash_is_zero(entries[index].hash))
            return EINVAL;
    }
    return 0;
}

static int trustcache_nokcall_readback(tcnc_controller *controller,
                                       const trustcache_entry_v1 *entries,
                                       uint32_t entryCount) {
    for (uint32_t index = 0; index < entryCount; index++) {
        bool found = false;
        int status = tcnc_query(controller, entries[index].hash, &found);
        if (status != 0)
            return status > 0 ? status : EIO;
        if (!found)
            return ENOENT;
    }
    return 0;
}

bool trustcache_nokcall_is_required(void) {
    if (system_info_uses_sptm() || (kconstant(base) && !is_kcall_available())) {
        return true;
    }
    if (getpid() == 1)
        return false;

    /*
	 * Most injected clients deserialize sysinfo, but routing must not depend
	 * on that process-local cache. A well-formed owner-probe reply makes PID
	 * 1 authoritative even while recovery is not Ready.
	 */
    bool available = false;
    int ownerStatus = EIO;
    return jbclient_platform_trustcache_owner_probe(&available, &ownerStatus) == 0 && available;
}

int trustcache_nokcall_owner_prepare_and_recover(trustcache_nokcall_signed_loader loader, void *loaderContext) {
    if (!trustcache_nokcall_is_required())
        return ENOTSUP;
    if (getpid() != 1)
        return EPERM;
    if (!loader)
        return EINVAL;

    tcnk_kernel *kernel = NULL;
    int status = tcnk_kernel_create(loader, loaderContext, &kernel);
    if (status != 0)
        return status > 0 ? status : EIO;

    tcnc_config config = {0};
    tcnc_backend backend = {0};
    status = tcnk_kernel_prepare(kernel, &config, &backend);
    if (status != 0) {
        tcnk_kernel_destroy(kernel);
        return status > 0 ? status : EIO;
    }

    /*
	 * tcno_prepare_and_recover always consumes kernel after this call,
	 * including every rejected or controller-creation failure path.
	 */
    return tcno_prepare_and_recover(&config, &backend, tcnk_kernel_destroy_context, kernel);
}

int trustcache_nokcall_owner_recover(void) {
    if (!trustcache_nokcall_is_required())
        return ENOTSUP;
    if (getpid() != 1)
        return EPERM;
    return tcno_recover();
}

int trustcache_nokcall_owner_prepare_runtime_pair(void) {
    if (!trustcache_nokcall_is_required())
        return ENOTSUP;
    if (getpid() != 1)
        return EPERM;
    return trustcache_nokcall_debug_status("owner.prepare_runtime_pair", tcno_prepare_runtime_pair());
}

int trustcache_nokcall_owner_signed_sources_present(bool *osPresentOut, bool *appPresentOut) {
    if (osPresentOut)
        *osPresentOut = false;
    if (appPresentOut)
        *appPresentOut = false;
    if (!trustcache_nokcall_is_required())
        return ENOTSUP;
    if (getpid() != 1)
        return EPERM;
    return tcno_signed_sources_present(osPresentOut, appPresentOut);
}

int trustcache_nokcall_bootstrap_append_entries(const trustcache_entry_v1 *entries, uint32_t entryCount) {
    if (!trustcache_nokcall_is_required())
        return ENOTSUP;
    if (getpid() == 1)
        return EPERM;
    int status = trustcache_nokcall_validate_entries(entries, entryCount);
    if (status != 0) {
        return trustcache_nokcall_debug_status("bootstrap.validate_entries", status);
    }

    tcnk_kernel *kernel = NULL;
    status = tcnk_kernel_create(NULL, NULL, &kernel);
    if (status != 0) {
        status = status > 0 ? status : EIO;
        return trustcache_nokcall_debug_status("bootstrap.kernel_create", status);
    }

    tcnc_config config = {0};
    tcnc_backend backend = {0};
    status = tcnk_kernel_prepare(kernel, &config, &backend);
    if (status != 0) {
        tcnk_kernel_destroy(kernel);
        status = status > 0 ? status : EIO;
        return trustcache_nokcall_debug_status("bootstrap.kernel_prepare", status);
    }

    tcnc_controller *controller = NULL;
    status = tcnc_controller_create(&config, &backend, &controller);
    if (status != 0) {
        tcnk_kernel_destroy(kernel);
        status = status > 0 ? status : EIO;
        return trustcache_nokcall_debug_status("bootstrap.controller_create", status);
    }
    if (!controller) {
        tcnk_kernel_destroy(kernel);
        return trustcache_nokcall_debug_status("bootstrap.controller_create", EPROTO);
    }

    int operationStatus = tcnc_bootstrap_append(controller, (const tcnm_entry *)entries, entryCount);
    if (operationStatus != 0) {
        trustcache_nokcall_debug_status("bootstrap.controller_append", operationStatus);
        status = tcnc_recover_to_fixed_point(controller);
        if (status != 0) {
            status = status > 0 ? status : EIO;
            trustcache_nokcall_debug_status("bootstrap.recovery", status);
            goto out;
        }
    }

    status = trustcache_nokcall_readback(controller, entries, entryCount);
    trustcache_nokcall_debug_status("bootstrap.readback", status);
    if (status != 0 && operationStatus != 0) {
        status = operationStatus > 0 ? operationStatus : EIO;
    }

out:
    tcnc_controller_destroy(controller);
    tcnk_kernel_destroy(kernel);
    return status;
}

int trustcache_nokcall_append_entries(const trustcache_entry_v1 *entries, uint32_t entryCount) {
    if (!trustcache_nokcall_is_required())
        return ENOTSUP;
    int status = trustcache_nokcall_validate_entries(entries, entryCount);
    if (status != 0)
        return status;

    if (getpid() == 1) {
        status = tcno_append((const tcnm_entry *)entries, entryCount);
        return trustcache_nokcall_debug_status("runtime.append.owner", status);
    }
    status = jbclient_root_trustcache_append_entries(entries, entryCount);
    return trustcache_nokcall_debug_status("runtime.append.ipc", status);
}

int trustcache_nokcall_query_cdhash(const cdhash_t hash, bool *foundOut) {
    if (foundOut)
        *foundOut = false;
    if (!trustcache_nokcall_is_required())
        return ENOTSUP;
    if (!hash || !foundOut || tcnm_hash_is_zero(hash))
        return EINVAL;

    int status = getpid() == 1 ? tcno_query(hash, foundOut) : jbclient_platform_trustcache_query_cdhash(hash, foundOut);
    return trustcache_nokcall_debug_status(getpid() == 1 ? "runtime.query.owner" : "runtime.query.ipc", status);
}
