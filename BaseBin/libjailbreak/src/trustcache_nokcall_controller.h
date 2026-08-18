#ifndef TRUSTCACHE_NOKCALL_CONTROLLER_H
#define TRUSTCACHE_NOKCALL_CONTROLLER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "trustcache_nokcall_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tcnc_controller tcnc_controller;

typedef struct {
    int (*read)(void *context, uint64_t address, void *output, size_t size);
    int (*protected_replace)(void *context, uint64_t address, const void *expected, const void *desired, size_t size);
    int (*reload_signed_source)(void *context, uint8_t sourceKind);
    void *context;
} tcnc_backend;

typedef struct {
    uint8_t sourceKind;
    const uint8_t *bytes;
    size_t size;
} tcnc_signed_source;

typedef int (*tcnc_nonce_provider)(void *context, uint8_t nonce[4]);

typedef struct {
    uint64_t listSlot;
    uint64_t pointerMask;
    uint64_t pointerMinimum;
    uint64_t pageSize;
    uint32_t maxNodes;
    uint8_t sharedType;
    const tcnc_signed_source *signedSources;
    uint32_t signedSourceCount;
    tcnc_nonce_provider nonce;
    void *nonceContext;
} tcnc_config;

/*
 * The backend, source bytes, and nonce context remain caller-owned and must
 * outlive the controller. The controller has no process-global state and does
 * not serialize callers; the single writer owner supplies that serialization.
 */
int tcnc_controller_create(const tcnc_config *config, const tcnc_backend *backend, tcnc_controller **controllerOut);

void tcnc_controller_destroy(tcnc_controller *controller);

/*
 * Before the signed loader exists, steals exactly one eligible signed source
 * into one unpaired READY carrier and publishes `entries`. Recovery can adopt
 * that stable singleton without a signed loader. Runtime publication is
 * enabled later by explicitly preparing a same-geometry peer.
 */
int tcnc_bootstrap_append(tcnc_controller *controller, const tcnm_entry *entries, uint32_t entryCount);

/*
 * Reobserves kernel memory after every ambiguous failure and repeats
 * idempotent recovery actions until no incomplete state remains.
 */
int tcnc_recover_to_fixed_point(tcnc_controller *controller);

/*
 * Boot-lifecycle operation run after the signed sources have been restored.
 * Converts the bootstrap singleton's exact same-kind source into its detached
 * peer, reloads that source, and recovers to a complete A/B pair.
 */
int tcnc_prepare_runtime_pair(tcnc_controller *controller);

/*
 * Append-only contract: existing hashes are never removed. A batch may be
 * committed in carrier-sized chunks; callers retry after an ambiguous failure
 * and already published chunks remain valid.
 */
int tcnc_append(tcnc_controller *controller, const tcnm_entry *entries, uint32_t entryCount);

/*
 * Zero hashes are rejected before the list slot is read. False is returned
 * only after every reachable node was read and validated successfully.
 */
int tcnc_query(tcnc_controller *controller, const uint8_t hash[TCNM_HASH_SIZE], bool *foundOut);

/* Returns one allocated, hash-deduplicated snapshot of live carrier entries. */
int tcnc_copy_entries(tcnc_controller *controller, tcnm_entry **entriesOut, uint32_t *entryCountOut);

/* Narrow boot-lifecycle postcondition; never invokes the signed loader. */
int tcnc_signed_sources_present(tcnc_controller *controller, bool *osPresentOut, bool *appPresentOut);

#ifdef __cplusplus
}
#endif

#endif
