#include "trustcache_nokcall_model.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define TCNM_MARKER_REQUIRES_CLONE UINT8_C(1)

static bool tcnm_stride_is_supported(size_t stride) {
    return stride == TCNM_HASH_SIZE || stride == TCNM_ENTRY_V1_SIZE || stride == TCNM_ENTRY_V2_SIZE;
}

size_t tcnm_entry_stride(uint32_t version) {
    switch (version) {
        case 0:
            return TCNM_HASH_SIZE;
        case 1:
            return TCNM_ENTRY_V1_SIZE;
        case 2:
            return TCNM_ENTRY_V2_SIZE;
        default:
            return 0;
    }
}

bool tcnm_hash_is_zero(const uint8_t hash[TCNM_HASH_SIZE]) {
    if (!hash)
        return false;
    uint8_t combined = 0;
    for (size_t index = 0; index < TCNM_HASH_SIZE; index++) {
        combined |= hash[index];
    }
    return combined == 0;
}

int tcnm_entries_validate(const uint8_t *raw, uint32_t capacity, size_t stride) {
    if (!raw || !capacity || !tcnm_stride_is_supported(stride)) {
        return EINVAL;
    }
    bool sawNonzero = false;
    const uint8_t *previous = NULL;
    for (uint32_t index = 0; index < capacity; index++) {
        const uint8_t *entry = raw + (size_t)index * stride;
        if (tcnm_hash_is_zero(entry)) {
            if (sawNonzero)
                return EPROTO;
            continue;
        }
        if (previous && memcmp(previous, entry, TCNM_HASH_SIZE) > 0) {
            return EPROTO;
        }
        sawNonzero = true;
        previous = entry;
    }
    return 0;
}

int tcnm_entries_decode(const uint8_t *raw,
                        uint32_t capacity,
                        size_t stride,
                        tcnm_entry *entries,
                        uint32_t entriesCapacity,
                        uint32_t *usedOut) {
    if (!entries || !usedOut)
        return EINVAL;
    int status = tcnm_entries_validate(raw, capacity, stride);
    if (status != 0)
        return status;

    uint32_t used = 0;
    for (uint32_t index = 0; index < capacity; index++) {
        const uint8_t *rawEntry = raw + (size_t)index * stride;
        if (tcnm_hash_is_zero(rawEntry))
            continue;
        if (used >= entriesCapacity)
            return ENOSPC;
        memcpy(entries[used].hash, rawEntry, TCNM_HASH_SIZE);
        entries[used].hashType = stride >= TCNM_ENTRY_V1_SIZE ? rawEntry[TCNM_HASH_SIZE] : 0;
        entries[used].flags = stride >= TCNM_ENTRY_V1_SIZE ? rawEntry[TCNM_HASH_SIZE + 1] : 0;
        used++;
    }
    *usedOut = used;
    return 0;
}

static int tcnm_entry_compare(const void *leftValue, const void *rightValue) {
    const tcnm_entry *left = leftValue;
    const tcnm_entry *right = rightValue;
    int comparison = memcmp(left->hash, right->hash, TCNM_HASH_SIZE);
    if (comparison != 0)
        return comparison;
    if (left->hashType != right->hashType) {
        return left->hashType < right->hashType ? -1 : 1;
    }
    if (left->flags != right->flags) {
        return left->flags < right->flags ? -1 : 1;
    }
    return 0;
}

static void tcnm_raw_entry_encode(uint8_t *raw, size_t stride, const tcnm_entry *entry) {
    memcpy(raw, entry->hash, TCNM_HASH_SIZE);
    if (stride >= TCNM_ENTRY_V1_SIZE) {
        raw[TCNM_HASH_SIZE] = entry->hashType;
        raw[TCNM_HASH_SIZE + 1] = entry->flags;
    }
}

typedef struct {
    uint8_t bytes[TCNM_ENTRY_V2_SIZE];
} tcnm_raw_entry;

static void tcnm_raw_entry_from_addition(tcnm_raw_entry *raw, size_t stride, const tcnm_entry *entry) {
    memset(raw, 0, sizeof(*raw));
    tcnm_raw_entry_encode(raw->bytes, stride, entry);
}

int tcnm_entries_merge(const uint8_t *currentRaw,
                       uint32_t capacity,
                       size_t stride,
                       const tcnm_entry *additions,
                       uint32_t additionCount,
                       uint8_t *targetRaw,
                       uint32_t *usedOut) {
    if (!currentRaw || !capacity || !targetRaw || !usedOut || (additionCount && !additions)
        || !tcnm_stride_is_supported(stride) || (size_t)capacity > SIZE_MAX / stride) {
        return EINVAL;
    }
    int status = tcnm_entries_validate(currentRaw, capacity, stride);
    if (status != 0)
        return status;

    tcnm_raw_entry *current = calloc(capacity, sizeof(*current));
    tcnm_entry *sortedAdditions = additionCount ? malloc((size_t)additionCount * sizeof(*additions)) : NULL;
    tcnm_raw_entry *merged = calloc(capacity, sizeof(*merged));
    if (!current || (additionCount && !sortedAdditions) || !merged) {
        free(merged);
        free(sortedAdditions);
        free(current);
        return ENOMEM;
    }

    uint32_t currentCount = 0;
    for (uint32_t index = 0; index < capacity; index++) {
        const uint8_t *rawEntry = currentRaw + (size_t)index * stride;
        if (tcnm_hash_is_zero(rawEntry))
            continue;
        memcpy(current[currentCount++].bytes, rawEntry, stride);
    }
    if (additionCount) {
        memcpy(sortedAdditions, additions, (size_t)additionCount * sizeof(*additions));
        qsort(sortedAdditions, additionCount, sizeof(*sortedAdditions), tcnm_entry_compare);
        for (uint32_t index = 0; index < additionCount; index++) {
            if (tcnm_hash_is_zero(sortedAdditions[index].hash)) {
                status = EINVAL;
                goto out;
            }
        }
    }

    uint32_t currentIndex = 0;
    uint32_t additionIndex = 0;
    uint32_t mergedCount = 0;
    while (currentIndex < currentCount || additionIndex < additionCount) {
        tcnm_raw_entry additionRaw = {0};
        const tcnm_raw_entry *next = NULL;
        if (currentIndex >= currentCount) {
            tcnm_raw_entry_from_addition(&additionRaw, stride, &sortedAdditions[additionIndex++]);
            next = &additionRaw;
        } else if (additionIndex >= additionCount) {
            next = &current[currentIndex++];
        } else {
            int comparison = memcmp(current[currentIndex].bytes, sortedAdditions[additionIndex].hash, TCNM_HASH_SIZE);
            if (comparison <= 0) {
                next = &current[currentIndex++];
                if (comparison == 0)
                    additionIndex++;
            } else {
                tcnm_raw_entry_from_addition(&additionRaw, stride, &sortedAdditions[additionIndex++]);
                next = &additionRaw;
            }
        }

        while (additionIndex < additionCount
               && memcmp(next->bytes, sortedAdditions[additionIndex].hash, TCNM_HASH_SIZE) == 0) {
            additionIndex++;
        }
        if (mergedCount && memcmp(merged[mergedCount - 1].bytes, next->bytes, TCNM_HASH_SIZE) == 0) {
            continue;
        }
        if (mergedCount >= capacity) {
            status = ENOSPC;
            goto out;
        }
        memcpy(merged[mergedCount++].bytes, next->bytes, stride);
    }

    memset(targetRaw, 0, (size_t)capacity * stride);
    uint32_t first = capacity - mergedCount;
    for (uint32_t index = 0; index < mergedCount; index++) {
        memcpy(targetRaw + (size_t)(first + index) * stride, merged[index].bytes, stride);
    }
    status = tcnm_entries_validate(targetRaw, capacity, stride);
    if (status == 0)
        *usedOut = mergedCount;

out:
    free(merged);
    free(sortedAdditions);
    free(current);
    return status;
}

int tcnm_entries_query(const uint8_t *raw,
                       uint32_t capacity,
                       size_t stride,
                       const uint8_t hash[TCNM_HASH_SIZE],
                       bool *foundOut) {
    if (!hash || !foundOut || tcnm_hash_is_zero(hash))
        return EINVAL;
    int status = tcnm_entries_validate(raw, capacity, stride);
    if (status != 0)
        return status;

    *foundOut = false;
    int64_t left = 0;
    int64_t right = (int64_t)capacity - 1;
    while (left <= right) {
        int64_t middle = left + (right - left) / 2;
        const uint8_t *observed = raw + (size_t)middle * stride;
        int comparison = memcmp(hash, observed, TCNM_HASH_SIZE);
        if (comparison == 0) {
            *foundOut = true;
            break;
        }
        if (comparison < 0)
            right = middle - 1;
        else
            left = middle + 1;
    }
    return 0;
}

static uint32_t tcnm_crc32_update(uint32_t crc, const void *bytes, size_t size) {
    const uint8_t *cursor = bytes;
    for (size_t index = 0; index < size; index++) {
        crc ^= cursor[index];
        for (uint32_t bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (UINT32_C(0xEDB88320) & (uint32_t)-(int32_t)(crc & 1));
        }
    }
    return crc;
}

uint32_t tcnm_crc32(const void *bytes, size_t size) {
    if (size && !bytes)
        return 0;
    return ~tcnm_crc32_update(UINT32_MAX, bytes, size);
}

static void tcnm_store_u32_le(uint8_t output[4], uint32_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

static uint32_t tcnm_crc32_u32(uint32_t crc, uint32_t value) {
    uint8_t encoded[4] = {0};
    tcnm_store_u32_le(encoded, value);
    return tcnm_crc32_update(crc, encoded, sizeof(encoded));
}

static uint32_t tcnm_crc32_u64(uint32_t crc, uint64_t value) {
    uint8_t encoded[8] = {0};
    for (size_t index = 0; index < sizeof(encoded); index++) {
        encoded[index] = (uint8_t)(value >> (index * 8));
    }
    return tcnm_crc32_update(crc, encoded, sizeof(encoded));
}

static uint32_t tcnm_marker_crc(const uint8_t marker[TCNM_MARKER_SIZE], const tcnm_marker_binding *binding) {
    uint32_t crc = tcnm_crc32_update(UINT32_MAX, marker, TCNM_MARKER_HEADER_SIZE);
    crc = tcnm_crc32_u32(crc, binding->selfModuleLow32);
    crc = tcnm_crc32_u32(crc, binding->peerModuleLow32);
    crc = tcnm_crc32_u64(crc, binding->selfModuleSize);
    crc = tcnm_crc32_u64(crc, binding->peerModuleSize);
    crc = tcnm_crc32_u32(crc, binding->selfVersion);
    crc = tcnm_crc32_u32(crc, binding->peerVersion);
    crc = tcnm_crc32_u32(crc, binding->selfCapacity);
    crc = tcnm_crc32_u32(crc, binding->peerCapacity);
    crc = tcnm_crc32_update(crc, binding->payload, binding->payloadSize);
    return ~crc;
}

static uint32_t tcnm_load_u32_le(const uint8_t input[4]) {
    return (uint32_t)input[0] | (uint32_t)input[1] << 8 | (uint32_t)input[2] << 16 | (uint32_t)input[3] << 24;
}

static bool tcnm_marker_binding_is_valid(const tcnm_marker_binding *binding) {
    if (!binding || !binding->payload || !binding->selfCapacity || !binding->peerCapacity
        || (binding->peerModuleLow32 != 0 && binding->selfModuleLow32 == binding->peerModuleLow32)
        || binding->selfVersion != binding->peerVersion || binding->selfCapacity != binding->peerCapacity
        || binding->selfModuleSize != binding->peerModuleSize) {
        return false;
    }
    size_t stride = tcnm_entry_stride(binding->selfVersion);
    if (!stride || (size_t)binding->selfCapacity > SIZE_MAX / stride) {
        return false;
    }
    size_t payloadSize = (size_t)binding->selfCapacity * stride;
    if (binding->payloadSize != payloadSize || payloadSize > UINT64_MAX - TCNM_FILE_HEADER_SIZE
        || binding->selfModuleSize < TCNM_FILE_HEADER_SIZE + (uint64_t)payloadSize) {
        return false;
    }
    return true;
}

static bool tcnm_marker_phase_is_valid(tcnm_marker_phase phase) {
    return phase == TCNM_MARKER_PHASE_PREPARED_SOURCE || phase == TCNM_MARKER_PHASE_PREPARED_FILL
        || phase == TCNM_MARKER_PHASE_READY;
}

static bool tcnm_source_kind_is_valid(uint8_t sourceKind) {
    return sourceKind == TCNM_SOURCE_OS || sourceKind == TCNM_SOURCE_APP;
}

int tcnm_marker_encode(const tcnm_marker_fields *fields,
                       const tcnm_marker_binding *binding,
                       uint8_t marker[TCNM_MARKER_SIZE]) {
    if (!fields || !marker || !tcnm_marker_binding_is_valid(binding)
        || fields->peerModuleLow32 != binding->peerModuleLow32 || !tcnm_marker_phase_is_valid(fields->phase)
        || !tcnm_source_kind_is_valid(fields->sourceKind)) {
        return EINVAL;
    }
    memset(marker, 0, TCNM_MARKER_SIZE);
    memcpy(marker, fields->nonce, sizeof(fields->nonce));
    tcnm_store_u32_le(marker + sizeof(fields->nonce), fields->peerModuleLow32);
    marker[TCNM_MARKER_VERSION_OFFSET] = TCNM_MARKER_VERSION;
    marker[TCNM_MARKER_PHASE_OFFSET] = (uint8_t)fields->phase;
    marker[TCNM_MARKER_SOURCE_OFFSET] = fields->sourceKind;
    marker[TCNM_MARKER_FLAGS_OFFSET] = fields->requiresClone ? TCNM_MARKER_REQUIRES_CLONE : 0;
    tcnm_store_u32_le(marker + TCNM_MARKER_CRC_OFFSET, tcnm_marker_crc(marker, binding));
    return 0;
}

int tcnm_marker_decode(const uint8_t marker[TCNM_MARKER_SIZE],
                       const tcnm_marker_binding *binding,
                       tcnm_marker_fields *fieldsOut) {
    if (!marker || !fieldsOut || !tcnm_marker_binding_is_valid(binding)) {
        return EINVAL;
    }
    tcnm_marker_phase phase = (tcnm_marker_phase)marker[TCNM_MARKER_PHASE_OFFSET];
    uint8_t flags = marker[TCNM_MARKER_FLAGS_OFFSET];
    if (marker[TCNM_MARKER_VERSION_OFFSET] != TCNM_MARKER_VERSION || !tcnm_marker_phase_is_valid(phase)
        || !tcnm_source_kind_is_valid(marker[TCNM_MARKER_SOURCE_OFFSET])
        || tcnm_load_u32_le(marker + sizeof(fieldsOut->nonce)) != binding->peerModuleLow32
        || (flags & ~TCNM_MARKER_REQUIRES_CLONE) != 0) {
        return EPROTO;
    }
    uint32_t expected = tcnm_marker_crc(marker, binding);
    uint32_t observed = tcnm_load_u32_le(marker + TCNM_MARKER_CRC_OFFSET);
    if (expected != observed)
        return EBADMSG;

    memcpy(fieldsOut->nonce, marker, sizeof(fieldsOut->nonce));
    fieldsOut->peerModuleLow32 = tcnm_load_u32_le(marker + sizeof(fieldsOut->nonce));
    fieldsOut->phase = phase;
    fieldsOut->sourceKind = marker[TCNM_MARKER_SOURCE_OFFSET];
    fieldsOut->requiresClone = (flags & TCNM_MARKER_REQUIRES_CLONE) != 0;
    return 0;
}

tcnm_type_observation tcnm_type_classify(uint8_t observedType, uint8_t sourceKind, uint8_t carrierType) {
    if (!tcnm_source_kind_is_valid(sourceKind) || sourceKind == carrierType) {
        return TCNM_TYPE_OBSERVED_FOREIGN;
    }
    if (observedType == sourceKind)
        return TCNM_TYPE_OBSERVED_SOURCE;
    if (observedType == carrierType)
        return TCNM_TYPE_OBSERVED_CARRIER;
    return TCNM_TYPE_OBSERVED_FOREIGN;
}

typedef enum {
    TCNM_PAYLOAD_CANONICAL_SOURCE,
    TCNM_PAYLOAD_VALID,
    TCNM_PAYLOAD_INVALID,
    TCNM_PAYLOAD_UNREADABLE,
} tcnm_payload_evidence;

static tcnm_payload_evidence tcnm_payload_classify(const uint8_t *observedRaw,
                                                   const uint8_t *canonicalRaw,
                                                   uint32_t capacity,
                                                   size_t stride) {
    if (!observedRaw)
        return TCNM_PAYLOAD_UNREADABLE;
    if (!capacity || !tcnm_stride_is_supported(stride) || (size_t)capacity > SIZE_MAX / stride
        || tcnm_entries_validate(observedRaw, capacity, stride) != 0) {
        return TCNM_PAYLOAD_INVALID;
    }
    size_t rawSize = (size_t)capacity * stride;
    if (canonicalRaw && memcmp(observedRaw, canonicalRaw, rawSize) == 0) {
        return TCNM_PAYLOAD_CANONICAL_SOURCE;
    }
    return TCNM_PAYLOAD_VALID;
}

typedef enum {
    TCNM_SOURCE_ABSENT,
    TCNM_SOURCE_EXACT_ONE,
    TCNM_SOURCE_UNREADABLE,
} tcnm_source_evidence;

static tcnm_source_evidence tcnm_source_classify(int scanStatus, uint32_t exactCloneCount) {
    if (scanStatus != 0)
        return TCNM_SOURCE_UNREADABLE;
    return exactCloneCount != 0 ? TCNM_SOURCE_EXACT_ONE : TCNM_SOURCE_ABSENT;
}

tcnm_observed_state tcnm_observed_classify(const tcnm_observed_evidence *evidence) {
    if (!evidence)
        return TCNM_OBSERVED_CONFLICT;

    tcnm_type_observation type = tcnm_type_classify(evidence->observedType,
                                                    evidence->sourceKind,
                                                    evidence->carrierType);
    tcnm_payload_evidence payload = tcnm_payload_classify(evidence->observedPayload,
                                                          evidence->canonicalPayload,
                                                          evidence->capacity,
                                                          evidence->stride);
    tcnm_source_evidence source = tcnm_source_classify(evidence->cloneScanStatus, evidence->exactCloneCount);

    if (payload == TCNM_PAYLOAD_UNREADABLE || source == TCNM_SOURCE_UNREADABLE) {
        return TCNM_OBSERVED_UNREADABLE;
    }
    if (type == TCNM_TYPE_OBSERVED_FOREIGN || !evidence->canonicalPayload
        || (evidence->markerValid && !evidence->markerHeaderKnown)) {
        return TCNM_OBSERVED_CONFLICT;
    }

    if (type == TCNM_TYPE_OBSERVED_SOURCE) {
        /*
		 * This node still owns the globally constrained 13/14 Type. A
		 * second exact source is a conflict; otherwise canonical payload
		 * is sufficient to roll any marker tear back to the signed bytes.
		 */
        if (source != TCNM_SOURCE_ABSENT) {
            return TCNM_OBSERVED_CONFLICT;
        }
        if (evidence->exactSource) {
            return payload == TCNM_PAYLOAD_CANONICAL_SOURCE ? TCNM_OBSERVED_EXACT_ORIGINAL : TCNM_OBSERVED_CONFLICT;
        }
        if (payload == TCNM_PAYLOAD_CANONICAL_SOURCE
            || (evidence->markerHeaderKnown && evidence->markerSourceKind == evidence->sourceKind)) {
            return TCNM_OBSERVED_ROLLBACK_ONLY;
        }
        return TCNM_OBSERVED_CONFLICT;
    }

    if (payload == TCNM_PAYLOAD_INVALID) {
        return TCNM_OBSERVED_CONFLICT;
    }

    if (!evidence->markerHeaderKnown) {
        return payload == TCNM_PAYLOAD_CANONICAL_SOURCE && source == TCNM_SOURCE_ABSENT ? TCNM_OBSERVED_ROLLBACK_ONLY
                                                                                        : TCNM_OBSERVED_CONFLICT;
    }

    if (evidence->markerPhase == TCNM_MARKER_PHASE_READY) {
        /*
		 * READY is publishable only with a valid CRC. A non-cloning
		 * bootstrap singleton is stable by itself; a cloning carrier
		 * with no exact source must reload it before pair recovery.
		 */
        if (!evidence->markerValid) {
            return payload == TCNM_PAYLOAD_CANONICAL_SOURCE && source == TCNM_SOURCE_ABSENT
                ? TCNM_OBSERVED_ROLLBACK_ONLY
                : TCNM_OBSERVED_CONFLICT;
        }
        if (!evidence->requiresClone) {
            return evidence->peerDeclared ? TCNM_OBSERVED_CONFLICT : TCNM_OBSERVED_STABLE_READY;
        }
        return source == TCNM_SOURCE_EXACT_ONE ? TCNM_OBSERVED_CLONED_READY : TCNM_OBSERVED_READY_WITHOUT_CLONE;
    }

    bool prepared = evidence->markerPhase == TCNM_MARKER_PHASE_PREPARED_SOURCE
        || evidence->markerPhase == TCNM_MARKER_PHASE_PREPARED_FILL;
    if (!prepared)
        return TCNM_OBSERVED_CONFLICT;

    /*
	 * A PREPARED marker may have a stale CRC while its payload is being
	 * replaced. It is never accepted as READY: source presence determines
	 * whether recovery completes forward or rolls back to signed bytes.
	 */
    if (!evidence->requiresClone) {
        return source == TCNM_SOURCE_ABSENT ? TCNM_OBSERVED_ROLLBACK_ONLY : TCNM_OBSERVED_CONFLICT;
    }
    return source == TCNM_SOURCE_EXACT_ONE ? TCNM_OBSERVED_PREPARED_WITH_CLONE : TCNM_OBSERVED_PREPARED_WITHOUT_CLONE;
}

tcnm_recovery_action tcnm_recovery_decide(tcnm_observed_state state) {
    switch (state) {
        case TCNM_OBSERVED_EXACT_ORIGINAL:
            return TCNM_RECOVERY_ACCEPT_ORIGINAL;
        case TCNM_OBSERVED_STABLE_READY:
        case TCNM_OBSERVED_CLONED_READY:
            return TCNM_RECOVERY_ACCEPT_READY;
        case TCNM_OBSERVED_READY_WITHOUT_CLONE:
            return TCNM_RECOVERY_RELOAD_SOURCE;
        case TCNM_OBSERVED_PREPARED_WITHOUT_CLONE:
        case TCNM_OBSERVED_ROLLBACK_ONLY:
            return TCNM_RECOVERY_RESTORE_ORIGINAL;
        case TCNM_OBSERVED_PREPARED_WITH_CLONE:
            return TCNM_RECOVERY_RESTORE_EMPTY_READY;
        case TCNM_OBSERVED_UNREADABLE:
            return TCNM_RECOVERY_RETRY;
        case TCNM_OBSERVED_CONFLICT:
            break;
    }
    return TCNM_RECOVERY_FAIL_CLOSED;
}

int tcnm_query_reduce(const tcnm_query_observation *observations, uint32_t observationCount, bool *foundOut) {
    if (!foundOut || (observationCount && !observations))
        return EINVAL;
    *foundOut = false;
    for (uint32_t index = 0; index < observationCount; index++) {
        if (observations[index].status != 0) {
            return observations[index].status;
        }
        if (observations[index].found) {
            *foundOut = true;
            return 0;
        }
    }
    return 0;
}

static uint32_t tcnm_first_nonzero_entry(const uint8_t *raw, uint32_t capacity, size_t stride) {
    uint32_t index = 0;
    while (index < capacity && tcnm_hash_is_zero(raw + (size_t)index * stride)) {
        index++;
    }
    return index;
}

static uint32_t tcnm_hash_group_end(const uint8_t *raw, uint32_t start, uint32_t capacity, size_t stride) {
    const uint8_t *hash = raw + (size_t)start * stride;
    uint32_t end = start + 1;
    while (end < capacity && memcmp(hash, raw + (size_t)end * stride, TCNM_HASH_SIZE) == 0) {
        end++;
    }
    return end;
}

static bool tcnm_group_contains(const uint8_t *raw, uint32_t start, uint32_t end, size_t stride, const uint8_t *entry) {
    for (uint32_t index = start; index < end; index++) {
        if (memcmp(raw + (size_t)index * stride, entry, stride) == 0) {
            return true;
        }
    }
    return false;
}

static bool tcnm_group_has_extra(const uint8_t *candidate,
                                 uint32_t candidateStart,
                                 uint32_t candidateEnd,
                                 const uint8_t *container,
                                 uint32_t containerStart,
                                 uint32_t containerEnd,
                                 size_t stride) {
    for (uint32_t index = candidateStart; index < candidateEnd; index++) {
        if (!tcnm_group_contains(container, containerStart, containerEnd, stride, candidate + (size_t)index * stride)) {
            return true;
        }
    }
    return false;
}

tcnm_table_relation tcnm_entries_relation(const uint8_t *leftRaw,
                                          const uint8_t *rightRaw,
                                          uint32_t capacity,
                                          size_t stride) {
    if (!leftRaw || !rightRaw)
        return TCNM_TABLE_RELATION_UNREADABLE;
    int leftStatus = tcnm_entries_validate(leftRaw, capacity, stride);
    int rightStatus = tcnm_entries_validate(rightRaw, capacity, stride);
    if (leftStatus != 0 && rightStatus != 0) {
        return TCNM_TABLE_RELATION_BOTH_INVALID;
    }
    if (leftStatus != 0)
        return TCNM_TABLE_RELATION_LEFT_INVALID;
    if (rightStatus != 0)
        return TCNM_TABLE_RELATION_RIGHT_INVALID;

    uint32_t leftIndex = tcnm_first_nonzero_entry(leftRaw, capacity, stride);
    uint32_t rightIndex = tcnm_first_nonzero_entry(rightRaw, capacity, stride);
    bool leftHasExtra = false;
    bool rightHasExtra = false;
    while (leftIndex < capacity && rightIndex < capacity) {
        const uint8_t *leftEntry = leftRaw + (size_t)leftIndex * stride;
        const uint8_t *rightEntry = rightRaw + (size_t)rightIndex * stride;
        int comparison = memcmp(leftEntry, rightEntry, TCNM_HASH_SIZE);
        uint32_t leftEnd = tcnm_hash_group_end(leftRaw, leftIndex, capacity, stride);
        uint32_t rightEnd = tcnm_hash_group_end(rightRaw, rightIndex, capacity, stride);
        if (comparison < 0) {
            leftHasExtra = true;
            leftIndex = leftEnd;
            continue;
        }
        if (comparison > 0) {
            rightHasExtra = true;
            rightIndex = rightEnd;
            continue;
        }
        leftHasExtra |= tcnm_group_has_extra(leftRaw, leftIndex, leftEnd, rightRaw, rightIndex, rightEnd, stride);
        rightHasExtra |= tcnm_group_has_extra(rightRaw, rightIndex, rightEnd, leftRaw, leftIndex, leftEnd, stride);
        leftIndex = leftEnd;
        rightIndex = rightEnd;
    }
    if (leftIndex < capacity)
        leftHasExtra = true;
    if (rightIndex < capacity)
        rightHasExtra = true;
    if (!leftHasExtra && !rightHasExtra) {
        return TCNM_TABLE_RELATION_EQUAL;
    }
    if (leftHasExtra && !rightHasExtra) {
        return TCNM_TABLE_RELATION_LEFT_STRICT_SUPERSET;
    }
    if (!leftHasExtra && rightHasExtra) {
        return TCNM_TABLE_RELATION_RIGHT_STRICT_SUPERSET;
    }
    return TCNM_TABLE_RELATION_NONCOMPARABLE;
}

tcnm_bank_pointer tcnm_bank_pointer_classify(int readStatus,
                                             uint64_t observedModule,
                                             uint64_t bank0Module,
                                             uint64_t bank1Module) {
    if (readStatus != 0)
        return TCNM_BANK_POINTER_UNREADABLE;
    if (!bank0Module || !bank1Module || bank0Module == bank1Module) {
        return TCNM_BANK_POINTER_FOREIGN;
    }
    if (observedModule == bank0Module)
        return TCNM_BANK_POINTER_BANK0;
    if (observedModule == bank1Module)
        return TCNM_BANK_POINTER_BANK1;
    return TCNM_BANK_POINTER_FOREIGN;
}

static bool tcnm_pointer_is_bank(tcnm_bank_pointer pointer) {
    return pointer == TCNM_BANK_POINTER_BANK0 || pointer == TCNM_BANK_POINTER_BANK1;
}

static bool tcnm_relation_is_comparable(tcnm_table_relation relation) {
    return relation == TCNM_TABLE_RELATION_EQUAL || relation == TCNM_TABLE_RELATION_LEFT_STRICT_SUPERSET
        || relation == TCNM_TABLE_RELATION_RIGHT_STRICT_SUPERSET;
}

static tcnm_ab_recovery_action tcnm_recover_collapsed_bank0(const tcnm_ab_observed_state *state) {
    if (!state->bank0Ready || state->relation == TCNM_TABLE_RELATION_LEFT_INVALID
        || state->relation == TCNM_TABLE_RELATION_BOTH_INVALID) {
        return TCNM_AB_RECOVERY_FAIL_CLOSED;
    }
    if (!state->bank1Ready || state->relation == TCNM_TABLE_RELATION_RIGHT_INVALID
        || state->relation == TCNM_TABLE_RELATION_LEFT_STRICT_SUPERSET) {
        return TCNM_AB_RECOVERY_REBUILD_BANK1_FROM_BANK0;
    }
    if (state->relation == TCNM_TABLE_RELATION_EQUAL || state->relation == TCNM_TABLE_RELATION_RIGHT_STRICT_SUPERSET) {
        return TCNM_AB_RECOVERY_PUBLISH_BANK1;
    }
    return TCNM_AB_RECOVERY_FAIL_CLOSED;
}

static tcnm_ab_recovery_action tcnm_recover_collapsed_bank1(const tcnm_ab_observed_state *state) {
    if (!state->bank1Ready || state->relation == TCNM_TABLE_RELATION_RIGHT_INVALID
        || state->relation == TCNM_TABLE_RELATION_BOTH_INVALID) {
        return TCNM_AB_RECOVERY_FAIL_CLOSED;
    }
    if (!state->bank0Ready || state->relation == TCNM_TABLE_RELATION_LEFT_INVALID
        || state->relation == TCNM_TABLE_RELATION_RIGHT_STRICT_SUPERSET) {
        return TCNM_AB_RECOVERY_REBUILD_BANK0_FROM_BANK1;
    }
    if (state->relation == TCNM_TABLE_RELATION_EQUAL || state->relation == TCNM_TABLE_RELATION_LEFT_STRICT_SUPERSET) {
        return TCNM_AB_RECOVERY_PUBLISH_BANK0;
    }
    return TCNM_AB_RECOVERY_FAIL_CLOSED;
}

tcnm_ab_recovery_action tcnm_ab_recovery_decide(const tcnm_ab_observed_state *state) {
    if (!state)
        return TCNM_AB_RECOVERY_FAIL_CLOSED;
    if (!state->readComplete || state->nodeA == TCNM_BANK_POINTER_UNREADABLE
        || state->nodeB == TCNM_BANK_POINTER_UNREADABLE || state->relation == TCNM_TABLE_RELATION_UNREADABLE) {
        return TCNM_AB_RECOVERY_RETRY;
    }
    if (!state->geometryExact || !state->typesShared || !tcnm_pointer_is_bank(state->nodeA)
        || !tcnm_pointer_is_bank(state->nodeB)) {
        return TCNM_AB_RECOVERY_FAIL_CLOSED;
    }

    if (state->nodeA != state->nodeB) {
        if (!state->bank0Ready || !state->bank1Ready || !tcnm_relation_is_comparable(state->relation)) {
            return TCNM_AB_RECOVERY_FAIL_CLOSED;
        }
        return TCNM_AB_RECOVERY_ACCEPT_READY;
    }

    if (!state->detachedBankKnown) {
        return TCNM_AB_RECOVERY_FAIL_CLOSED;
    }
    return state->nodeA == TCNM_BANK_POINTER_BANK0 ? tcnm_recover_collapsed_bank0(state)
                                                   : tcnm_recover_collapsed_bank1(state);
}
