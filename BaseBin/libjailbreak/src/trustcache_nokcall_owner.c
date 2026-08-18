#include "trustcache_nokcall_owner.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>

#ifdef TCNO_OWNER_TEST_PROCESS_ID
#define TCNO_OWNER_PROCESS_ID TCNO_OWNER_TEST_PROCESS_ID
#else
#define TCNO_OWNER_PROCESS_ID getpid()
#endif

typedef struct {
    pthread_mutex_t lock;
    atomic_int state;
    tcnc_controller *controller;
    tcno_resources_destroy resourcesDestroy;
    void *resourcesContext;
} tcno_owner;

static tcno_owner gOwner = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .state = ATOMIC_VAR_INIT(TCNO_STATE_RECOVERING),
};

static int tcno_normalize_error(int status) {
    if (status == 0)
        return 0;
    return status > 0 ? status : EIO;
}

static void tcno_destroy_resources(tcno_resources_destroy resourcesDestroy, void *resourcesContext) {
    if (resourcesDestroy)
        resourcesDestroy(resourcesContext);
}

static int tcno_state_status(tcno_state state) {
    return state == TCNO_STATE_READY ? 0 : EAGAIN;
}

static void tcno_publish_state_locked(tcno_state state) {
    atomic_store_explicit(&gOwner.state, state, memory_order_release);
}

static int tcno_recover_locked(void) {
    if (!gOwner.controller)
        return ENXIO;

    int status = tcno_normalize_error(tcnc_recover_to_fixed_point(gOwner.controller));
    if (status != 0) {
        tcno_publish_state_locked(TCNO_STATE_RECOVERING);
        return status;
    }
    tcno_publish_state_locked(TCNO_STATE_READY);
    return 0;
}

static int tcno_lock_ready(void) {
    int status = tcno_status();
    if (status != 0)
        return status;

    status = pthread_mutex_lock(&gOwner.lock);
    if (status != 0)
        return status;

    status = tcno_status();
    if (status != 0) {
        pthread_mutex_unlock(&gOwner.lock);
        return status;
    }
    if (!gOwner.controller) {
        pthread_mutex_unlock(&gOwner.lock);
        return ENXIO;
    }
    return 0;
}

static int tcno_lock_for_mutation(void) {
    int status = pthread_mutex_lock(&gOwner.lock);
    if (status != 0)
        return status;

    if (!gOwner.controller) {
        status = tcno_status();
        pthread_mutex_unlock(&gOwner.lock);
        return status;
    }
    if (tcno_status() != 0) {
        status = tcno_recover_locked();
        if (status != 0) {
            status = tcno_status();
            pthread_mutex_unlock(&gOwner.lock);
            return status;
        }
    }
    return 0;
}

static int tcno_entries_present_locked(const tcnm_entry *entries, uint32_t entryCount, bool *presentOut) {
    *presentOut = false;
    for (uint32_t i = 0; i < entryCount; i++) {
        bool found = false;
        int status = tcno_normalize_error(tcnc_query(gOwner.controller, entries[i].hash, &found));
        if (status != 0)
            return status;
        if (!found)
            return 0;
    }
    *presentOut = true;
    return 0;
}

int tcno_prepare_and_recover(const tcnc_config *config,
                             const tcnc_backend *backend,
                             tcno_resources_destroy resourcesDestroy,
                             void *resourcesContext) {
    if (TCNO_OWNER_PROCESS_ID != 1) {
        tcno_destroy_resources(resourcesDestroy, resourcesContext);
        return EPERM;
    }
    if (!config || !backend) {
        tcno_destroy_resources(resourcesDestroy, resourcesContext);
        return EINVAL;
    }

    int status = pthread_mutex_lock(&gOwner.lock);
    if (status != 0) {
        tcno_destroy_resources(resourcesDestroy, resourcesContext);
        return status;
    }

    if (gOwner.controller) {
        pthread_mutex_unlock(&gOwner.lock);
        tcno_destroy_resources(resourcesDestroy, resourcesContext);
        return EALREADY;
    }

    tcnc_controller *controller = NULL;
    status = tcno_normalize_error(tcnc_controller_create(config, backend, &controller));
    if (status != 0) {
        if (controller)
            tcnc_controller_destroy(controller);
        tcno_publish_state_locked(TCNO_STATE_RECOVERING);
        pthread_mutex_unlock(&gOwner.lock);
        tcno_destroy_resources(resourcesDestroy, resourcesContext);
        return status;
    }
    if (!controller) {
        tcno_publish_state_locked(TCNO_STATE_RECOVERING);
        pthread_mutex_unlock(&gOwner.lock);
        tcno_destroy_resources(resourcesDestroy, resourcesContext);
        return EPROTO;
    }

    gOwner.controller = controller;
    gOwner.resourcesDestroy = resourcesDestroy;
    gOwner.resourcesContext = resourcesContext;
    tcno_publish_state_locked(TCNO_STATE_RECOVERING);

    status = tcno_recover_locked();
    pthread_mutex_unlock(&gOwner.lock);
    return status;
}

int tcno_recover(void) {
    if (TCNO_OWNER_PROCESS_ID != 1)
        return EPERM;

    tcno_state state = tcno_state_get();
    if (state == TCNO_STATE_READY)
        return 0;
    if (state != TCNO_STATE_RECOVERING)
        return tcno_state_status(state);

    int status = pthread_mutex_lock(&gOwner.lock);
    if (status != 0)
        return status;

    state = tcno_state_get();
    if (state == TCNO_STATE_READY) {
        pthread_mutex_unlock(&gOwner.lock);
        return 0;
    }
    if (state != TCNO_STATE_RECOVERING) {
        status = tcno_state_status(state);
        pthread_mutex_unlock(&gOwner.lock);
        return status;
    }

    status = tcno_recover_locked();
    pthread_mutex_unlock(&gOwner.lock);
    return status;
}

int tcno_prepare_runtime_pair(void) {
    if (TCNO_OWNER_PROCESS_ID != 1)
        return EPERM;

    int status = tcno_lock_for_mutation();
    if (status != 0)
        return status;

    status = tcno_normalize_error(tcnc_prepare_runtime_pair(gOwner.controller));
    if (status != 0) {
        int operationStatus = status;
        int recoveryStatus = tcno_recover_locked();
        status = recoveryStatus == 0 ? operationStatus : tcno_state_status(tcno_state_get());
    }

    pthread_mutex_unlock(&gOwner.lock);
    return status;
}

int tcno_append(const tcnm_entry *entries, uint32_t entryCount) {
    if (TCNO_OWNER_PROCESS_ID != 1)
        return EPERM;

    if (!entries || entryCount == 0)
        return EINVAL;
    for (uint32_t i = 0; i < entryCount; i++) {
        if (tcnm_hash_is_zero(entries[i].hash))
            return EINVAL;
    }

    int status = tcno_lock_for_mutation();
    if (status != 0)
        return status;

    status = tcno_normalize_error(tcnc_append(gOwner.controller, entries, entryCount));

    if (status != 0) {
        int operationStatus = status;
        tcno_publish_state_locked(TCNO_STATE_RECOVERING);
        int recoveryStatus = tcno_recover_locked();
        if (recoveryStatus == 0) {
            bool present = false;
            int queryStatus = tcno_entries_present_locked(entries, entryCount, &present);
            if (queryStatus == 0 && present) {
                status = 0;
            } else {
                status = operationStatus;
            }
        } else {
            status = tcno_state_status(tcno_state_get());
        }
    }

    pthread_mutex_unlock(&gOwner.lock);
    return status;
}

int tcno_query(const uint8_t hash[TCNM_HASH_SIZE], bool *foundOut) {
    if (foundOut)
        *foundOut = false;
    if (TCNO_OWNER_PROCESS_ID != 1)
        return EPERM;

    int status = tcno_status();
    if (status != 0)
        return status;
    if (!hash || !foundOut)
        return EINVAL;
    if (tcnm_hash_is_zero(hash))
        return EINVAL;

    status = tcno_lock_ready();
    if (status != 0)
        return status;

    status = tcno_normalize_error(tcnc_query(gOwner.controller, hash, foundOut));
    if (status != 0) {
        *foundOut = false;
    }
    pthread_mutex_unlock(&gOwner.lock);
    return status;
}

int tcno_copy_entries(tcnm_entry **entriesOut, uint32_t *entryCountOut) {
    if (entriesOut)
        *entriesOut = NULL;
    if (entryCountOut)
        *entryCountOut = 0;
    if (TCNO_OWNER_PROCESS_ID != 1)
        return EPERM;
    if (!entriesOut || !entryCountOut)
        return EINVAL;

    int status = tcno_lock_ready();
    if (status != 0)
        return status;
    status = tcno_normalize_error(tcnc_copy_entries(gOwner.controller, entriesOut, entryCountOut));
    pthread_mutex_unlock(&gOwner.lock);
    return status;
}

int tcno_signed_sources_present(bool *osPresentOut, bool *appPresentOut) {
    if (osPresentOut)
        *osPresentOut = false;
    if (appPresentOut)
        *appPresentOut = false;
    if (TCNO_OWNER_PROCESS_ID != 1)
        return EPERM;
    if (!osPresentOut || !appPresentOut)
        return EINVAL;

    int status = pthread_mutex_lock(&gOwner.lock);
    if (status != 0)
        return status;
    status = gOwner.controller
        ? tcno_normalize_error(tcnc_signed_sources_present(gOwner.controller, osPresentOut, appPresentOut))
        : ENXIO;
    pthread_mutex_unlock(&gOwner.lock);
    return status;
}

tcno_state tcno_state_get(void) {
    return (tcno_state)atomic_load_explicit(&gOwner.state, memory_order_acquire);
}

int tcno_status(void) {
    return tcno_state_status(tcno_state_get());
}
