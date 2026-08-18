#ifndef TRUSTCACHE_NOKCALL_MODEL_H
#define TRUSTCACHE_NOKCALL_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TCNM_HASH_SIZE 20U
#define TCNM_ENTRY_V1_SIZE 22U
#define TCNM_ENTRY_V2_SIZE 24U
#define TCNM_MARKER_SIZE 16U
#define TCNM_MARKER_VERSION UINT8_C(3)
#define TCNM_MARKER_HEADER_SIZE 12U
#define TCNM_MARKER_VERSION_OFFSET 8U
#define TCNM_MARKER_PHASE_OFFSET 9U
#define TCNM_MARKER_SOURCE_OFFSET 10U
#define TCNM_MARKER_FLAGS_OFFSET 11U
#define TCNM_MARKER_CRC_OFFSET 12U
#define TCNM_SOURCE_OS UINT8_C(13)
#define TCNM_SOURCE_APP UINT8_C(14)
#define TCNM_FILE_HEADER_SIZE 24U

typedef struct {
    uint8_t hash[TCNM_HASH_SIZE];
    uint8_t hashType;
    uint8_t flags;
} tcnm_entry;

_Static_assert(sizeof(tcnm_entry) == TCNM_ENTRY_V1_SIZE, "trust cache v1 entries must stay packed");

bool tcnm_hash_is_zero(const uint8_t hash[TCNM_HASH_SIZE]);

size_t tcnm_entry_stride(uint32_t version);

int tcnm_entries_validate(const uint8_t *raw, uint32_t capacity, size_t stride);

int tcnm_entries_decode(const uint8_t *raw,
                        uint32_t capacity,
                        size_t stride,
                        tcnm_entry *entries,
                        uint32_t entriesCapacity,
                        uint32_t *usedOut);

int tcnm_entries_merge(const uint8_t *currentRaw,
                       uint32_t capacity,
                       size_t stride,
                       const tcnm_entry *additions,
                       uint32_t additionCount,
                       uint8_t *targetRaw,
                       uint32_t *usedOut);

int tcnm_entries_query(const uint8_t *raw,
                       uint32_t capacity,
                       size_t stride,
                       const uint8_t hash[TCNM_HASH_SIZE],
                       bool *foundOut);

typedef enum {
    TCNM_MARKER_PHASE_PREPARED_SOURCE = 1,
    TCNM_MARKER_PHASE_PREPARED_FILL = 2,
    TCNM_MARKER_PHASE_READY = 3,
} tcnm_marker_phase;

typedef struct {
    uint8_t nonce[4];
    uint32_t peerModuleLow32;
    tcnm_marker_phase phase;
    uint8_t sourceKind;
    bool requiresClone;
} tcnm_marker_fields;

typedef struct {
    uint32_t selfModuleLow32;
    uint32_t peerModuleLow32;
    uint64_t selfModuleSize;
    uint64_t peerModuleSize;
    uint32_t selfVersion;
    uint32_t peerVersion;
    uint32_t selfCapacity;
    uint32_t peerCapacity;
    const uint8_t *payload;
    size_t payloadSize;
} tcnm_marker_binding;

/*
 * Marker CRC serialization is owned by the model. It covers every binding
 * field in little-endian order followed by the complete self payload.
 */
uint32_t tcnm_crc32(const void *bytes, size_t size);

int tcnm_marker_encode(const tcnm_marker_fields *fields,
                       const tcnm_marker_binding *binding,
                       uint8_t marker[TCNM_MARKER_SIZE]);

int tcnm_marker_decode(const uint8_t marker[TCNM_MARKER_SIZE],
                       const tcnm_marker_binding *binding,
                       tcnm_marker_fields *fieldsOut);

typedef enum {
    TCNM_TYPE_OBSERVED_SOURCE,
    TCNM_TYPE_OBSERVED_CARRIER,
    TCNM_TYPE_OBSERVED_FOREIGN,
} tcnm_type_observation;

tcnm_type_observation tcnm_type_classify(uint8_t observedType, uint8_t sourceKind, uint8_t carrierType);

typedef struct {
    uint8_t observedType;
    uint8_t sourceKind;
    uint8_t carrierType;
    bool exactSource;
    bool markerHeaderKnown;
    bool markerValid;
    tcnm_marker_phase markerPhase;
    uint8_t markerSourceKind;
    bool requiresClone;
    bool peerDeclared;
    const uint8_t *observedPayload;
    const uint8_t *canonicalPayload;
    uint32_t capacity;
    size_t stride;
    int cloneScanStatus;
    uint32_t exactCloneCount;
} tcnm_observed_evidence;

/*
 * These are the only node states that lead to distinct recovery behavior.
 * Marker CRC validity, payload validity, Type, and signed-source evidence are
 * aggregated here so production and host tests consume one policy table.
 */
typedef enum {
    TCNM_OBSERVED_EXACT_ORIGINAL,
    TCNM_OBSERVED_STABLE_READY,
    TCNM_OBSERVED_CLONED_READY,
    TCNM_OBSERVED_READY_WITHOUT_CLONE,
    TCNM_OBSERVED_PREPARED_WITHOUT_CLONE,
    TCNM_OBSERVED_PREPARED_WITH_CLONE,
    TCNM_OBSERVED_ROLLBACK_ONLY,
    TCNM_OBSERVED_UNREADABLE,
    TCNM_OBSERVED_CONFLICT,
} tcnm_observed_state;

tcnm_observed_state tcnm_observed_classify(const tcnm_observed_evidence *evidence);

typedef enum {
    TCNM_RECOVERY_ACCEPT_ORIGINAL,
    TCNM_RECOVERY_ACCEPT_READY,
    TCNM_RECOVERY_RELOAD_SOURCE,
    TCNM_RECOVERY_RESTORE_ORIGINAL,
    TCNM_RECOVERY_RESTORE_EMPTY_READY,
    TCNM_RECOVERY_RETRY,
    TCNM_RECOVERY_FAIL_CLOSED,
} tcnm_recovery_action;

tcnm_recovery_action tcnm_recovery_decide(tcnm_observed_state state);

typedef struct {
    int status;
    bool found;
} tcnm_query_observation;

int tcnm_query_reduce(const tcnm_query_observation *observations, uint32_t observationCount, bool *foundOut);

typedef enum {
    TCNM_TABLE_RELATION_EQUAL,
    TCNM_TABLE_RELATION_LEFT_STRICT_SUPERSET,
    TCNM_TABLE_RELATION_RIGHT_STRICT_SUPERSET,
    TCNM_TABLE_RELATION_NONCOMPARABLE,
    TCNM_TABLE_RELATION_LEFT_INVALID,
    TCNM_TABLE_RELATION_RIGHT_INVALID,
    TCNM_TABLE_RELATION_BOTH_INVALID,
    TCNM_TABLE_RELATION_UNREADABLE,
} tcnm_table_relation;

/* Nonzero entries are compared as an exact set of full-stride records. */
tcnm_table_relation tcnm_entries_relation(const uint8_t *leftRaw,
                                          const uint8_t *rightRaw,
                                          uint32_t capacity,
                                          size_t stride);

typedef enum {
    TCNM_BANK_POINTER_BANK0,
    TCNM_BANK_POINTER_BANK1,
    TCNM_BANK_POINTER_FOREIGN,
    TCNM_BANK_POINTER_UNREADABLE,
} tcnm_bank_pointer;

tcnm_bank_pointer tcnm_bank_pointer_classify(int readStatus,
                                             uint64_t observedModule,
                                             uint64_t bank0Module,
                                             uint64_t bank1Module);

typedef struct {
    tcnm_bank_pointer nodeA;
    tcnm_bank_pointer nodeB;
    tcnm_table_relation relation;
    bool readComplete;
    bool geometryExact;
    bool typesShared;
    bool detachedBankKnown;
    bool bank0Ready;
    bool bank1Ready;
} tcnm_ab_observed_state;

typedef enum {
    TCNM_AB_RECOVERY_ACCEPT_READY,
    /* Copy bank1 into detached bank0; leave both node pointers unchanged. */
    TCNM_AB_RECOVERY_REBUILD_BANK0_FROM_BANK1,
    /* Copy bank0 into detached bank1; leave both node pointers unchanged. */
    TCNM_AB_RECOVERY_REBUILD_BANK1_FROM_BANK0,
    /* Both nodes point at bank1; repoint nodeA to detached bank0. */
    TCNM_AB_RECOVERY_PUBLISH_BANK0,
    /* Both nodes point at bank0; repoint nodeB to detached bank1. */
    TCNM_AB_RECOVERY_PUBLISH_BANK1,
    TCNM_AB_RECOVERY_RETRY,
    TCNM_AB_RECOVERY_FAIL_CLOSED,
} tcnm_ab_recovery_action;

tcnm_ab_recovery_action tcnm_ab_recovery_decide(const tcnm_ab_observed_state *state);

#endif
