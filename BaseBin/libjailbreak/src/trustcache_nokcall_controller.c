#include "trustcache_nokcall_controller.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef DEBUG
#define DEBUG 0
#endif

#define TCNC_FILE_HEADER_SIZE 24U
#define TCNC_NODE_SIZE 40U
#define TCNC_NODE_PREVIOUS_OFFSET 8U
#define TCNC_NODE_TYPE_OFFSET 16U
#define TCNC_NODE_MODULE_OFFSET 32U
#define TCNC_MARKER_OFFSET 4U
#define TCNC_POINTER_SIGN_BIT UINT64_C(0x0080000000000000)
#define TCNC_QUARANTINED_TYPE UINT8_C(3)
#define TCNC_MAX_MODULE_SIZE UINT64_C(0x01000000)
#define TCNC_MAX_SNAPSHOT_BYTES UINT64_C(0x04000000)
#define TCNC_MAX_NODE_LIMIT UINT32_C(4096)

typedef struct {
    uint64_t next;
    uint64_t previous;
    uint8_t type;
    uint8_t reserved[7];
    uint64_t moduleSize;
    uint64_t module;
} tcnc_raw_node;

_Static_assert(sizeof(tcnc_raw_node) == TCNC_NODE_SIZE, "trust cache node geometry changed");

typedef struct {
    uint32_t version;
    uint8_t marker[TCNM_MARKER_SIZE];
    uint32_t capacity;
} __attribute__((packed)) tcnc_file_header;

_Static_assert(sizeof(tcnc_file_header) == TCNC_FILE_HEADER_SIZE, "trust cache file geometry changed");

typedef struct {
    uint64_t address;
    uint64_t moduleAddress;
    tcnc_raw_node raw;
    uint8_t *moduleBytes;
    size_t stride;
    uint32_t capacity;
    const tcnc_signed_source *canonicalSource;
    const uint8_t *canonicalModule;
    bool exactSource;
    bool canonicalExceptMarker;
    bool payloadValid;
    bool markerHeaderKnown;
    bool markerValid;
    tcnm_marker_fields marker;
} tcnc_node;

typedef struct {
    tcnc_node *nodes;
    uint32_t count;
} tcnc_snapshot;

typedef struct {
    tcnc_node *left;
    tcnc_node *right;
    tcnm_table_relation relation;
} tcnc_pair;

typedef enum {
    TCNC_MARKER_COMMIT_VERSION,
    TCNC_MARKER_COMMIT_PHASE,
} tcnc_marker_commit;

struct tcnc_controller {
    tcnc_config config;
    tcnc_backend backend;
};

static int tcnc_status(int status) {
    return status == 0 ? 0 : (status > 0 ? status : EIO);
}

static bool tcnc_power_of_two(uint64_t value) {
    return value && (value & (value - 1)) == 0;
}

static uint32_t tcnc_load_u32(const uint8_t bytes[4]) {
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static uint64_t tcnc_canonical_pointer(const tcnc_controller *controller, uint64_t rawPointer) {
    return (rawPointer & TCNC_POINTER_SIGN_BIT) ? rawPointer | controller->config.pointerMask
                                                : rawPointer & ~controller->config.pointerMask;
}

static void tcnc_debug_pointer_rejection(const tcnc_controller *controller,
                                         const char *phase,
                                         uint64_t nodeAddress,
                                         uint64_t rawPointer,
                                         size_t alignment) {
#if DEBUG
    fprintf(
        stderr,
        "[trustcache_nokcall] phase=%s status=%d " "node=0x%llx raw=0x%llx canonical=0x%llx " "mask=0x%llx alignment=%zu\n",
        phase,
        EFAULT,
        (unsigned long long)nodeAddress,
        (unsigned long long)rawPointer,
        (unsigned long long)tcnc_canonical_pointer(controller, rawPointer),
        (unsigned long long)controller->config.pointerMask,
        alignment);
#else
    (void)controller;
    (void)phase;
    (void)nodeAddress;
    (void)rawPointer;
    (void)alignment;
#endif
}

static bool tcnc_pointer(const tcnc_controller *controller,
                         uint64_t rawPointer,
                         size_t alignment,
                         uint64_t *pointerOut) {
    if (!controller || !rawPointer || !alignment || (alignment & (alignment - 1)) != 0) {
        return false;
    }
    uint64_t pointer = tcnc_canonical_pointer(controller, rawPointer);
    if (pointer < controller->config.pointerMinimum || (pointer & (alignment - 1)) != 0) {
        return false;
    }
    if (pointerOut)
        *pointerOut = pointer;
    return true;
}

static void tcnc_debug_backend_failure(const char *operation, int status, uint64_t address, size_t size) {
#if DEBUG
    fprintf(stderr,
            "[trustcache_nokcall] phase=controller.%s status=%d " "address=0x%llx size=%zu\n",
            operation,
            status,
            (unsigned long long)address,
            size);
#else
    (void)operation;
    (void)status;
    (void)address;
    (void)size;
#endif
}

static int tcnc_read(const tcnc_controller *controller, uint64_t address, void *output, size_t size) {
    if (!controller || !address || !output || !size || address > UINT64_MAX - (size - 1)) {
        return EINVAL;
    }
    memset(output, 0, size);
    int status = tcnc_status(controller->backend.read(controller->backend.context, address, output, size));
    if (status == EFAULT) {
        tcnc_debug_backend_failure("read", status, address, size);
    }
    return status;
}

static int tcnc_replace(const tcnc_controller *controller,
                        uint64_t address,
                        const void *expected,
                        const void *desired,
                        size_t size) {
    if (!controller || !address || !expected || !desired || !size || address > UINT64_MAX - (size - 1)) {
        return EINVAL;
    }
    if (memcmp(expected, desired, size) == 0)
        return 0;
    int status = tcnc_status(
        controller->backend.protected_replace(controller->backend.context, address, expected, desired, size));
    if (status == EFAULT) {
        tcnc_debug_backend_failure("protected_replace", status, address, size);
    }
    return status;
}

static const tcnc_signed_source *tcnc_source_for_kind(const tcnc_controller *controller, uint8_t sourceKind) {
    for (uint32_t index = 0; index < controller->config.signedSourceCount; index++) {
        const tcnc_signed_source *source = &controller->config.signedSources[index];
        if (source->sourceKind == sourceKind)
            return source;
    }
    return NULL;
}

static bool tcnc_source_kind(uint8_t kind) {
    return kind == TCNM_SOURCE_OS || kind == TCNM_SOURCE_APP;
}

static bool tcnc_carrier_compatible(const tcnc_node *node) {
    return node && node->stride >= TCNM_ENTRY_V1_SIZE && node->capacity != 0 && node->payloadValid;
}

static bool tcnc_source_candidate_geometry(const uint8_t *candidate,
                                           size_t moduleSize,
                                           uint32_t version,
                                           uint32_t capacity) {
    if (!candidate || moduleSize < TCNC_FILE_HEADER_SIZE)
        return false;
    tcnc_file_header header = {0};
    memcpy(&header, candidate, sizeof(header));
    if (header.version != version || header.capacity != capacity) {
        return false;
    }
    size_t stride = tcnm_entry_stride(version);
    return stride && (size_t)capacity <= (moduleSize - TCNC_FILE_HEADER_SIZE) / stride;
}

/*
 * A mapped signed source may contain framing around its trust cache. Geometry
 * must identify exactly one module-sized slice; ambiguity is fail-closed.
 */
static int tcnc_find_canonical_module(const tcnc_signed_source *source,
                                      size_t moduleSize,
                                      uint32_t version,
                                      uint32_t capacity,
                                      const uint8_t **moduleOut) {
    if (!source || !source->bytes || !source->size || !moduleOut || moduleSize < TCNC_FILE_HEADER_SIZE) {
        return EINVAL;
    }
    *moduleOut = NULL;
    if (moduleSize > source->size)
        return ENOENT;
    for (size_t offset = 0; offset <= source->size - moduleSize; offset++) {
        const uint8_t *candidate = source->bytes + offset;
        if (!tcnc_source_candidate_geometry(candidate, moduleSize, version, capacity)) {
            continue;
        }
        if (*moduleOut)
            return EEXIST;
        *moduleOut = candidate;
    }
    return *moduleOut ? 0 : ENOENT;
}

static bool tcnc_equal_except_marker(const uint8_t *observed, const uint8_t *canonical, size_t moduleSize) {
    if (!observed || !canonical || moduleSize < TCNC_FILE_HEADER_SIZE) {
        return false;
    }
    return memcmp(observed, canonical, TCNC_MARKER_OFFSET) == 0
        && memcmp(observed + TCNC_MARKER_OFFSET + TCNM_MARKER_SIZE,
                  canonical + TCNC_MARKER_OFFSET + TCNM_MARKER_SIZE,
                  moduleSize - TCNC_MARKER_OFFSET - TCNM_MARKER_SIZE)
        == 0;
}

static void tcnc_node_dispose(tcnc_node *node) {
    if (!node)
        return;
    free(node->moduleBytes);
    memset(node, 0, sizeof(*node));
}

static void tcnc_snapshot_dispose(tcnc_snapshot *snapshot) {
    if (!snapshot)
        return;
    for (uint32_t index = 0; index < snapshot->count; index++) {
        tcnc_node_dispose(&snapshot->nodes[index]);
    }
    free(snapshot->nodes);
    memset(snapshot, 0, sizeof(*snapshot));
}

static int tcnc_marker_decode_node(tcnc_node *node) {
    const tcnc_file_header *header = (const tcnc_file_header *)node->moduleBytes;
    const uint8_t *payload = node->moduleBytes + TCNC_FILE_HEADER_SIZE;
    const uint8_t *raw = header->marker;
    uint32_t peerLow = tcnc_load_u32(raw + 4);
    tcnm_marker_binding binding = {
        .selfModuleLow32 = (uint32_t)node->moduleAddress,
        .peerModuleLow32 = peerLow,
        .selfModuleSize = node->raw.moduleSize,
        .peerModuleSize = node->raw.moduleSize,
        .selfVersion = header->version,
        .peerVersion = header->version,
        .selfCapacity = header->capacity,
        .peerCapacity = header->capacity,
        .payload = payload,
        .payloadSize = (size_t)header->capacity * node->stride,
    };
    int status = tcnm_marker_decode(raw, &binding, &node->marker);
    node->markerValid = status == 0;

    uint8_t version = raw[8];
    uint8_t phase = raw[9];
    uint8_t sourceKind = raw[10];
    uint8_t flags = raw[11];
    node->markerHeaderKnown = version == TCNM_MARKER_VERSION
        && (phase == TCNM_MARKER_PHASE_PREPARED_SOURCE || phase == TCNM_MARKER_PHASE_PREPARED_FILL
            || phase == TCNM_MARKER_PHASE_READY)
        && tcnc_source_kind(sourceKind) && (flags & ~UINT8_C(1)) == 0;
    if (node->markerHeaderKnown && !node->markerValid) {
        memcpy(node->marker.nonce, raw, sizeof(node->marker.nonce));
        node->marker.peerModuleLow32 = peerLow;
        node->marker.phase = (tcnm_marker_phase)phase;
        node->marker.sourceKind = sourceKind;
        node->marker.requiresClone = (flags & 1) != 0;
    }
    return status;
}

static int tcnc_read_node(const tcnc_controller *controller, uint64_t address, tcnc_node *nodeOut) {
    if (!controller || !address || !nodeOut)
        return EINVAL;
    memset(nodeOut, 0, sizeof(*nodeOut));
    nodeOut->address = address;
    int status = tcnc_read(controller, address, &nodeOut->raw, sizeof(nodeOut->raw));
    if (status != 0)
        return status;

    if (nodeOut->raw.moduleSize < TCNC_FILE_HEADER_SIZE || nodeOut->raw.moduleSize > TCNC_MAX_MODULE_SIZE
        || nodeOut->raw.moduleSize > SIZE_MAX) {
        return EPROTO;
    }
    if (!tcnc_pointer(controller, nodeOut->raw.module, 1, &nodeOut->moduleAddress)) {
        tcnc_debug_pointer_rejection(controller, "controller.node_module", address, nodeOut->raw.module, 1);
        return EFAULT;
    }
    nodeOut->moduleBytes = malloc((size_t)nodeOut->raw.moduleSize);
    if (!nodeOut->moduleBytes)
        return ENOMEM;
    status = tcnc_read(controller, nodeOut->moduleAddress, nodeOut->moduleBytes, (size_t)nodeOut->raw.moduleSize);
    if (status != 0)
        return status;

    const tcnc_file_header *header = (const tcnc_file_header *)nodeOut->moduleBytes;
    nodeOut->stride = tcnm_entry_stride(header->version);
    nodeOut->capacity = header->capacity;
    if (!nodeOut->stride || !nodeOut->capacity
        || (size_t)nodeOut->capacity > ((size_t)nodeOut->raw.moduleSize - TCNC_FILE_HEADER_SIZE) / nodeOut->stride) {
        return EPROTO;
    }
    const uint8_t *payload = nodeOut->moduleBytes + TCNC_FILE_HEADER_SIZE;
    nodeOut->payloadValid = tcnm_entries_validate(payload, nodeOut->capacity, nodeOut->stride) == 0;

    (void)tcnc_marker_decode_node(nodeOut);

    uint8_t possibleKind = 0;
    if (tcnc_source_kind(nodeOut->raw.type)) {
        possibleKind = nodeOut->raw.type;
    } else if (nodeOut->markerHeaderKnown) {
        possibleKind = nodeOut->marker.sourceKind;
    }
    if (possibleKind) {
        nodeOut->canonicalSource = tcnc_source_for_kind(controller, possibleKind);
        if (nodeOut->canonicalSource) {
            status = tcnc_find_canonical_module(nodeOut->canonicalSource,
                                                (size_t)nodeOut->raw.moduleSize,
                                                header->version,
                                                header->capacity,
                                                &nodeOut->canonicalModule);
            if (status != 0 && status != ENOENT)
                return status;
            nodeOut->exactSource = nodeOut->canonicalModule
                && memcmp(nodeOut->canonicalModule, nodeOut->moduleBytes, (size_t)nodeOut->raw.moduleSize) == 0;
            nodeOut->canonicalExceptMarker = nodeOut->canonicalModule
                && tcnc_equal_except_marker(nodeOut->moduleBytes,
                                            nodeOut->canonicalModule,
                                            (size_t)nodeOut->raw.moduleSize);
        }
    } else if (nodeOut->raw.type == controller->config.sharedType) {
        for (uint32_t sourceIndex = 0; sourceIndex < controller->config.signedSourceCount; sourceIndex++) {
            const tcnc_signed_source *source = &controller->config.signedSources[sourceIndex];
            const uint8_t *candidate = NULL;
            status = tcnc_find_canonical_module(source,
                                                (size_t)nodeOut->raw.moduleSize,
                                                header->version,
                                                header->capacity,
                                                &candidate);
            if (status == ENOENT)
                continue;
            if (status != 0)
                return status;
            if (!tcnc_equal_except_marker(nodeOut->moduleBytes, candidate, (size_t)nodeOut->raw.moduleSize)) {
                continue;
            }
            if (nodeOut->canonicalModule)
                return EEXIST;
            nodeOut->canonicalSource = source;
            nodeOut->canonicalModule = candidate;
            nodeOut->canonicalExceptMarker = true;
            nodeOut->exactSource = memcmp(candidate, nodeOut->moduleBytes, (size_t)nodeOut->raw.moduleSize) == 0;
        }
    }
    return 0;
}

static bool tcnc_snapshot_contains_address(const tcnc_snapshot *snapshot, uint64_t address) {
    for (uint32_t index = 0; index < snapshot->count; index++) {
        if (snapshot->nodes[index].address == address)
            return true;
    }
    return false;
}

static int tcnc_scan(const tcnc_controller *controller, tcnc_snapshot *snapshotOut) {
    if (!controller || !snapshotOut)
        return EINVAL;
    memset(snapshotOut, 0, sizeof(*snapshotOut));
    uint64_t headRaw = 0;
    int status = tcnc_read(controller, controller->config.listSlot, &headRaw, sizeof(headRaw));
    if (status != 0)
        return status;
    if (!headRaw)
        return 0;

    uint64_t address = 0;
    if (!tcnc_pointer(controller, headRaw, sizeof(uint64_t), &address)) {
        tcnc_debug_pointer_rejection(controller, "controller.list_head", 0, headRaw, sizeof(uint64_t));
        return EFAULT;
    }
    snapshotOut->nodes = calloc(controller->config.maxNodes, sizeof(*snapshotOut->nodes));
    if (!snapshotOut->nodes)
        return ENOMEM;

    uint64_t moduleBytes = 0;
    while (address) {
        if (snapshotOut->count >= controller->config.maxNodes) {
            status = ELOOP;
            goto fail;
        }
        if (tcnc_snapshot_contains_address(snapshotOut, address)) {
            status = ELOOP;
            goto fail;
        }
        tcnc_node *node = &snapshotOut->nodes[snapshotOut->count];
        status = tcnc_read_node(controller, address, node);
        if (status != 0) {
            tcnc_node_dispose(node);
            goto fail;
        }
        if (node->raw.moduleSize > TCNC_MAX_SNAPSHOT_BYTES - moduleBytes) {
            tcnc_node_dispose(node);
            status = E2BIG;
            goto fail;
        }
        moduleBytes += node->raw.moduleSize;

        snapshotOut->count++;
        if (!node->raw.next)
            break;
        if (!tcnc_pointer(controller, node->raw.next, sizeof(uint64_t), &address)) {
            tcnc_debug_pointer_rejection(controller, "controller.node_next", address, node->raw.next, sizeof(uint64_t));
            status = EFAULT;
            goto fail;
        }
    }
    return 0;

fail:
    tcnc_snapshot_dispose(snapshotOut);
    return status;
}

static bool tcnc_validated_node_contains_hash(const tcnc_node *node, const uint8_t hash[TCNM_HASH_SIZE]) {
    const uint8_t *payload = node->moduleBytes + TCNC_FILE_HEADER_SIZE;
    int64_t left = 0;
    int64_t right = (int64_t)node->capacity - 1;
    while (left <= right) {
        int64_t middle = left + (right - left) / 2;
        const uint8_t *observed = payload + (size_t)middle * node->stride;
        int comparison = memcmp(hash, observed, TCNM_HASH_SIZE);
        if (comparison == 0)
            return true;
        if (comparison < 0)
            right = middle - 1;
        else
            left = middle + 1;
    }
    return false;
}

static int tcnc_snapshot_query(const tcnc_snapshot *snapshot, const uint8_t hash[TCNM_HASH_SIZE], bool *foundOut) {
    if (!snapshot || !hash || !foundOut || tcnm_hash_is_zero(hash)) {
        return EINVAL;
    }
    *foundOut = false;
    for (uint32_t index = 0; index < snapshot->count; index++) {
        const tcnc_node *node = &snapshot->nodes[index];
        if (!node->payloadValid)
            return EPROTO;
        if (tcnc_validated_node_contains_hash(node, hash)) {
            *foundOut = true;
            return 0;
        }
    }
    return 0;
}

static uint32_t tcnc_count_exact_source(const tcnc_snapshot *snapshot, uint8_t sourceKind, const tcnc_node *excluding) {
    uint32_t count = 0;
    for (uint32_t index = 0; index < snapshot->count; index++) {
        const tcnc_node *node = &snapshot->nodes[index];
        if (node == excluding)
            continue;
        if (node->raw.type == sourceKind && node->exactSource)
            count++;
    }
    return count;
}

static bool tcnc_same_geometry(const tcnc_node *left, const tcnc_node *right) {
    if (!left || !right)
        return false;
    const tcnc_file_header *leftHeader = (const tcnc_file_header *)left->moduleBytes;
    const tcnc_file_header *rightHeader = (const tcnc_file_header *)right->moduleBytes;
    return left->raw.moduleSize == right->raw.moduleSize && leftHeader->version == rightHeader->version
        && leftHeader->capacity == rightHeader->capacity && (left->raw.module >> 32) == (right->raw.module >> 32);
}

static int tcnc_marker_build(const tcnc_node *node,
                             uint32_t peerModuleLow32,
                             tcnm_marker_phase phase,
                             uint8_t sourceKind,
                             bool requiresClone,
                             const uint8_t nonce[4],
                             const uint8_t *payload,
                             uint8_t markerOut[TCNM_MARKER_SIZE]) {
    if (!node || !nonce || !payload || !markerOut)
        return EINVAL;
    const tcnc_file_header *header = (const tcnc_file_header *)node->moduleBytes;
    tcnm_marker_fields fields = {
        .peerModuleLow32 = peerModuleLow32,
        .phase = phase,
        .sourceKind = sourceKind,
        .requiresClone = requiresClone,
    };
    memcpy(fields.nonce, nonce, sizeof(fields.nonce));
    tcnm_marker_binding binding = {
        .selfModuleLow32 = (uint32_t)node->moduleAddress,
        .peerModuleLow32 = peerModuleLow32,
        .selfModuleSize = node->raw.moduleSize,
        .peerModuleSize = node->raw.moduleSize,
        .selfVersion = header->version,
        .peerVersion = header->version,
        .selfCapacity = header->capacity,
        .peerCapacity = header->capacity,
        .payload = payload,
        .payloadSize = (size_t)header->capacity * node->stride,
    };
    return tcnm_marker_encode(&fields, &binding, markerOut);
}

static int tcnc_replace_marker_word(const tcnc_controller *controller,
                                    const tcnc_node *node,
                                    const uint8_t desired[TCNM_MARKER_SIZE],
                                    uint64_t address) {
    uint64_t markerStart = node->moduleAddress + TCNC_MARKER_OFFSET;
    uint64_t markerEnd = markerStart + TCNM_MARKER_SIZE;
    uint8_t observed[sizeof(uint32_t)] = {0};
    uint8_t replacement[sizeof(uint32_t)] = {0};
    int status = tcnc_read(controller, address, observed, sizeof(observed));
    if (status != 0)
        return status;
    memcpy(replacement, observed, sizeof(replacement));
    for (size_t byte = 0; byte < sizeof(replacement); byte++) {
        uint64_t location = address + byte;
        if (location >= markerStart && location < markerEnd) {
            replacement[byte] = desired[location - markerStart];
        }
    }
    return tcnc_replace(controller, address, observed, replacement, sizeof(replacement));
}

static int tcnc_replace_marker(const tcnc_controller *controller,
                               const tcnc_node *node,
                               const uint8_t desired[TCNM_MARKER_SIZE],
                               tcnc_marker_commit commit) {
    if (!controller || !node || !desired || node->moduleAddress > UINT64_MAX - TCNC_MARKER_OFFSET - TCNM_MARKER_SIZE) {
        return EINVAL;
    }

    uint64_t markerStart = node->moduleAddress + TCNC_MARKER_OFFSET;
    uint64_t markerEnd = markerStart + TCNM_MARKER_SIZE;
    uint64_t firstWord = markerStart & ~UINT64_C(3);
    uint64_t finalWord = (markerEnd - 1) & ~UINT64_C(3);
    uint64_t versionWord = (markerStart + TCNM_MARKER_VERSION_OFFSET) & ~UINT64_C(3);
    uint64_t phaseWord = (markerStart + TCNM_MARKER_PHASE_OFFSET) & ~UINT64_C(3);
    uint64_t order[5] = {0};
    size_t orderCount = 0;

    /*
	 * A signed trust-cache module is packed and may start at any byte
	 * address. Initial preparation commits with version after every other
	 * byte is present. PREPARED->READY commits with phase after the complete
	 * READY CRC is present.
	 */
    for (uint64_t address = firstWord;; address += sizeof(uint32_t)) {
        uint64_t commitWord = commit == TCNC_MARKER_COMMIT_VERSION ? versionWord : phaseWord;
        if (address != commitWord && (commit != TCNC_MARKER_COMMIT_VERSION || address != phaseWord)) {
            order[orderCount++] = address;
        }
        if (address == finalWord)
            break;
    }
    if (commit == TCNC_MARKER_COMMIT_VERSION && phaseWord != versionWord) {
        order[orderCount++] = phaseWord;
    }
    order[orderCount++] = commit == TCNC_MARKER_COMMIT_VERSION ? versionWord : phaseWord;

    for (size_t index = 0; index < orderCount; index++) {
        int status = tcnc_replace_marker_word(controller, node, desired, order[index]);
        if (status != 0)
            return status;
    }
    return 0;
}

static int tcnc_replace_marker_phase(const tcnc_controller *controller,
                                     const tcnc_node *node,
                                     tcnm_marker_phase desiredPhase) {
    if (!controller || !node || node->moduleAddress > UINT64_MAX - TCNC_MARKER_OFFSET - TCNM_MARKER_SIZE) {
        return EINVAL;
    }
    uint64_t phaseAddress = node->moduleAddress + TCNC_MARKER_OFFSET + TCNM_MARKER_PHASE_OFFSET;
    uint64_t wordAddress = phaseAddress & ~UINT64_C(3);
    uint8_t observed[sizeof(uint32_t)] = {0};
    uint8_t replacement[sizeof(uint32_t)] = {0};
    int status = tcnc_read(controller, wordAddress, observed, sizeof(observed));
    if (status != 0)
        return status;
    memcpy(replacement, observed, sizeof(replacement));
    replacement[phaseAddress - wordAddress] = (uint8_t)desiredPhase;
    return tcnc_replace(controller, wordAddress, observed, replacement, sizeof(replacement));
}

static int tcnc_replace_type(const tcnc_controller *controller, const tcnc_node *node, uint8_t desiredType) {
    uint32_t observed = 0;
    uint64_t address = node->address + TCNC_NODE_TYPE_OFFSET;
    int status = tcnc_read(controller, address, &observed, sizeof(observed));
    if (status != 0)
        return status;
    uint32_t desired = (observed & UINT32_C(0xffffff00)) | desiredType;
    return tcnc_replace(controller, address, &observed, &desired, sizeof(desired));
}

static int tcnc_quarantine_unmanaged_shared_nodes(const tcnc_controller *controller, tcnc_snapshot *snapshot) {
    if (!controller || !snapshot)
        return EINVAL;
    for (uint32_t index = 0; index < snapshot->count; index++) {
        tcnc_node *node = &snapshot->nodes[index];
        if (node->raw.type != controller->config.sharedType || node->markerHeaderKnown) {
            continue;
        }

        int replaceStatus = tcnc_replace_type(controller, node, TCNC_QUARANTINED_TYPE);
        uint32_t readback = 0;
        int readStatus = tcnc_read(controller, node->address + TCNC_NODE_TYPE_OFFSET, &readback, sizeof(readback));
        if (readStatus != 0)
            return readStatus;
        if ((uint8_t)readback != TCNC_QUARANTINED_TYPE) {
            return replaceStatus != 0 ? replaceStatus : EIO;
        }
        node->raw.type = TCNC_QUARANTINED_TYPE;
    }
    return 0;
}

static void tcnc_repair_previous_links_best_effort(const tcnc_controller *controller, const tcnc_snapshot *snapshot) {
    if (!controller || !snapshot)
        return;
    for (uint32_t index = 0; index < snapshot->count; index++) {
        const tcnc_node *node = &snapshot->nodes[index];
        uint64_t desiredPrevious = index ? snapshot->nodes[index - 1].address : 0;
        uint64_t observedPrevious = 0;
        if (node->raw.previous && tcnc_pointer(controller, node->raw.previous, sizeof(uint64_t), &observedPrevious)
            && observedPrevious == desiredPrevious) {
            continue;
        }
        if (!node->raw.previous && !desiredPrevious)
            continue;

        uint32_t desiredWords[2] = {
            (uint32_t)desiredPrevious,
            (uint32_t)(desiredPrevious >> 32),
        };
        for (uint32_t word = 0; word < 2; word++) {
            uint64_t address = node->address + TCNC_NODE_PREVIOUS_OFFSET + (uint64_t)word * sizeof(uint32_t);
            uint32_t observed = 0;
            if (tcnc_read(controller, address, &observed, sizeof(observed)) != 0) {
                continue;
            }
            if (observed == desiredWords[word])
                continue;
            (void)tcnc_replace(controller, address, &observed, &desiredWords[word], sizeof(observed));
            uint32_t readback = 0;
            (void)tcnc_read(controller, address, &readback, sizeof(readback));
        }

        uint64_t readback = 0;
        (void)tcnc_read(controller, node->address + TCNC_NODE_PREVIOUS_OFFSET, &readback, sizeof(readback));
    }
}

static int tcnc_restore_marker_and_type(const tcnc_controller *controller,
                                        const tcnc_node *node,
                                        const uint8_t desired[TCNM_MARKER_SIZE],
                                        uint8_t sourceKind) {
    if (!controller || !node || !desired || !tcnc_source_kind(sourceKind)
        || node->moduleAddress > UINT64_MAX - TCNC_MARKER_OFFSET - TCNM_MARKER_SIZE) {
        return EINVAL;
    }

    uint64_t markerStart = node->moduleAddress + TCNC_MARKER_OFFSET;
    uint64_t markerEnd = markerStart + TCNM_MARKER_SIZE;
    uint64_t firstWord = markerStart & ~UINT64_C(3);
    uint64_t finalWord = (markerEnd - 1) & ~UINT64_C(3);
    uint64_t versionWord = (markerStart + TCNM_MARKER_VERSION_OFFSET) & ~UINT64_C(3);
    uint8_t invalid[TCNM_MARKER_SIZE] = {0};
    memcpy(invalid, desired, sizeof(invalid));
    invalid[TCNM_MARKER_VERSION_OFFSET] = 0;

    /*
	 * Restore is the reverse publication direction. Clear marker identity
	 * first, restore every non-version word, restore source Type, and only
	 * then expose the canonical UUID byte. Thus no torn canonical UUID can
	 * be interpreted as a different PREPARED transaction.
	 */
    int status = tcnc_replace_marker_word(controller, node, invalid, versionWord);
    if (status != 0)
        return status;
    for (uint64_t address = firstWord;; address += sizeof(uint32_t)) {
        if (address != versionWord) {
            status = tcnc_replace_marker_word(controller, node, desired, address);
            if (status != 0)
                return status;
        }
        if (address == finalWord)
            break;
    }
    if (node->raw.type != sourceKind) {
        status = tcnc_replace_type(controller, node, sourceKind);
        if (status != 0)
            return status;
    }
    return tcnc_replace_marker_word(controller, node, desired, versionWord);
}

static int tcnc_payload_clear(const tcnc_controller *controller, const tcnc_node *node) {
    uint64_t start = node->moduleAddress + TCNC_FILE_HEADER_SIZE;
    size_t payloadSize = (size_t)node->capacity * node->stride;
    uint64_t end = start + payloadSize;
    uint64_t firstWord = start & ~UINT64_C(3);
    uint64_t finalWord = (end - 1) & ~UINT64_C(3);
    for (uint64_t address = firstWord;; address += sizeof(uint32_t)) {
        uint8_t observed[4] = {0};
        uint8_t desired[4] = {0};
        int status = tcnc_read(controller, address, observed, sizeof(observed));
        if (status != 0)
            return status;
        memcpy(desired, observed, sizeof(desired));
        for (size_t byte = 0; byte < sizeof(desired); byte++) {
            uint64_t location = address + byte;
            if (location >= start && location < end)
                desired[byte] = 0;
        }
        status = tcnc_replace(controller, address, observed, desired, sizeof(desired));
        if (status != 0)
            return status;
        if (address == finalWord)
            break;
    }
    return 0;
}

static int tcnc_payload_fill(const tcnc_controller *controller, const tcnc_node *node, const uint8_t *target) {
    if (!target)
        return EINVAL;
    uint64_t start = node->moduleAddress + TCNC_FILE_HEADER_SIZE;
    size_t payloadSize = (size_t)node->capacity * node->stride;
    uint64_t end = start + payloadSize;
    uint64_t address = (end - 1) & ~UINT64_C(3);
    uint64_t firstWord = start & ~UINT64_C(3);
    for (;;) {
        uint8_t observed[4] = {0};
        uint8_t desired[4] = {0};
        int status = tcnc_read(controller, address, observed, sizeof(observed));
        if (status != 0)
            return status;
        memcpy(desired, observed, sizeof(desired));
        for (size_t byte = 0; byte < sizeof(desired); byte++) {
            uint64_t location = address + byte;
            if (location >= start && location < end) {
                desired[byte] = target[location - start];
            }
        }
        status = tcnc_replace(controller, address, observed, desired, sizeof(desired));
        if (status != 0)
            return status;
        if (address == firstWord)
            break;
        address -= sizeof(uint32_t);
    }
    return 0;
}

static int tcnc_write_payload(const tcnc_controller *controller, const tcnc_node *node, const uint8_t *target) {
    int status = tcnc_payload_clear(controller, node);
    if (status != 0)
        return status;
    status = tcnc_payload_fill(controller, node, target);
    if (status != 0)
        return status;

    size_t size = (size_t)node->capacity * node->stride;
    uint8_t *observed = malloc(size);
    if (!observed)
        return ENOMEM;
    status = tcnc_read(controller, node->moduleAddress + TCNC_FILE_HEADER_SIZE, observed, size);
    if (status == 0 && memcmp(observed, target, size) != 0) {
        status = EIO;
    }
    if (status == 0) {
        status = tcnm_entries_validate(observed, node->capacity, node->stride);
    }
    free(observed);
    return status;
}

static int tcnc_restore_original(const tcnc_controller *controller, const tcnc_node *node) {
    if (!node->canonicalModule || !node->canonicalSource)
        return EPROTO;
    const uint8_t *payload = node->canonicalModule + TCNC_FILE_HEADER_SIZE;
    int status = tcnc_write_payload(controller, node, payload);
    if (status != 0)
        return status;
    return tcnc_restore_marker_and_type(controller,
                                        node,
                                        node->canonicalModule + TCNC_MARKER_OFFSET,
                                        node->canonicalSource->sourceKind);
}

static int tcnc_publish_module_low(const tcnc_controller *controller,
                                   const tcnc_node *node,
                                   uint64_t desiredRawModule) {
    if ((node->raw.module >> 32) != (desiredRawModule >> 32)) {
        return EXDEV;
    }
    uint32_t observed = 0;
    uint64_t address = node->address + TCNC_NODE_MODULE_OFFSET;
    int status = tcnc_read(controller, address, &observed, sizeof(observed));
    if (status != 0)
        return status;
    uint32_t desired = (uint32_t)desiredRawModule;
    return tcnc_replace(controller, address, &observed, &desired, sizeof(desired));
}

static int tcnc_prepare_and_finish(const tcnc_controller *controller,
                                   const tcnc_node *node,
                                   const uint8_t *targetPayload,
                                   uint32_t peerModuleLow32,
                                   uint8_t sourceKind,
                                   bool requiresClone,
                                   bool writePrepared,
                                   bool generateNonce) {
    uint8_t nonce[4] = {0};
    if (generateNonce) {
        int status = tcnc_status(controller->config.nonce(controller->config.nonceContext, nonce));
        if (status != 0)
            return status;
    } else {
        if (!node->markerHeaderKnown)
            return EPROTO;
        memcpy(nonce, node->marker.nonce, sizeof(nonce));
    }

    uint8_t prepared[TCNM_MARKER_SIZE] = {0};
    int status = tcnc_marker_build(node,
                                   peerModuleLow32,
                                   TCNM_MARKER_PHASE_PREPARED_SOURCE,
                                   sourceKind,
                                   requiresClone,
                                   nonce,
                                   node->moduleBytes + TCNC_FILE_HEADER_SIZE,
                                   prepared);
    if (status != 0)
        return status;
    if (writePrepared) {
        status = tcnc_replace_marker(controller, node, prepared, TCNC_MARKER_COMMIT_VERSION);
        if (status != 0)
            return status;
    }

    status = tcnc_write_payload(controller, node, targetPayload);
    if (status != 0)
        return status;

    uint8_t ready[TCNM_MARKER_SIZE] = {0};
    status = tcnc_marker_build(node,
                               peerModuleLow32,
                               TCNM_MARKER_PHASE_READY,
                               sourceKind,
                               requiresClone,
                               nonce,
                               targetPayload,
                               ready);
    if (status != 0)
        return status;
    return tcnc_replace_marker(controller, node, ready, TCNC_MARKER_COMMIT_PHASE);
}

static int tcnc_rebind_ready_marker(const tcnc_controller *controller,
                                    const tcnc_node *node,
                                    uint32_t peerModuleLow32,
                                    uint8_t sourceKind,
                                    const uint8_t nonce[4]) {
    if (!controller || !node || !node->markerHeaderKnown || !peerModuleLow32 || !tcnc_source_kind(sourceKind)
        || !nonce) {
        return EINVAL;
    }

    uint8_t prepared[TCNM_MARKER_SIZE] = {0};
    int status = tcnc_marker_build(node,
                                   peerModuleLow32,
                                   TCNM_MARKER_PHASE_PREPARED_FILL,
                                   sourceKind,
                                   true,
                                   nonce,
                                   node->moduleBytes + TCNC_FILE_HEADER_SIZE,
                                   prepared);
    if (status != 0)
        return status;
    uint8_t ready[TCNM_MARKER_SIZE] = {0};
    status = tcnc_marker_build(node,
                               peerModuleLow32,
                               TCNM_MARKER_PHASE_READY,
                               sourceKind,
                               true,
                               nonce,
                               node->moduleBytes + TCNC_FILE_HEADER_SIZE,
                               ready);
    if (status != 0)
        return status;

    /*
	 * PREPARED_FILL is the gate for rebinding a live READY carrier. Once
	 * phase changes, recovery derives the intended peer from the unique
	 * reciprocal READY anchor and ignores any partially replaced peer/CRC
	 * bytes in this marker.
	 */
    status = tcnc_replace_marker_phase(controller, node, TCNM_MARKER_PHASE_PREPARED_FILL);
    if (status != 0)
        return status;
    status = tcnc_replace_marker(controller, node, prepared, TCNC_MARKER_COMMIT_VERSION);
    if (status != 0)
        return status;
    return tcnc_replace_marker(controller, node, ready, TCNC_MARKER_COMMIT_PHASE);
}

static int tcnc_finish_singleton_fill(const tcnc_controller *controller, const tcnc_node *node) {
    if (!controller || !node || node->raw.type != controller->config.sharedType || !node->markerHeaderKnown
        || node->marker.phase != TCNM_MARKER_PHASE_PREPARED_FILL || node->marker.requiresClone
        || node->marker.peerModuleLow32 != 0 || !node->payloadValid) {
        return EINVAL;
    }

    uint8_t ready[TCNM_MARKER_SIZE] = {0};
    int status = tcnc_marker_build(node,
                                   0,
                                   TCNM_MARKER_PHASE_READY,
                                   node->marker.sourceKind,
                                   false,
                                   node->marker.nonce,
                                   node->moduleBytes + TCNC_FILE_HEADER_SIZE,
                                   ready);
    if (status != 0)
        return status;
    return tcnc_replace_marker(controller, node, ready, TCNC_MARKER_COMMIT_PHASE);
}

static int tcnc_bootstrap_candidate(const tcnc_controller *controller,
                                    const tcnc_node *node,
                                    const tcnm_entry *entries,
                                    uint32_t entryCount) {
    if (!node->exactSource || !tcnc_source_kind(node->raw.type) || !tcnc_carrier_compatible(node)) {
        return ENOENT;
    }
    size_t payloadSize = (size_t)node->capacity * node->stride;
    uint8_t *empty = calloc(1, payloadSize);
    uint8_t *target = malloc(payloadSize);
    if (!empty || !target) {
        free(target);
        free(empty);
        return ENOMEM;
    }
    uint32_t used = 0;
    int status = tcnm_entries_merge(empty, node->capacity, node->stride, entries, entryCount, target, &used);
    if (status != 0) {
        free(target);
        free(empty);
        return status;
    }

    status = tcnc_prepare_and_finish(controller, node, target, 0, node->raw.type, false, true, true);
    free(target);
    free(empty);
    if (status != 0)
        return status;

    /*
	 * Bootstrap's only commit is last. Until this word changes, recovery
	 * chooses the exact signed source.
	 */
    return tcnc_replace_type(controller, node, controller->config.sharedType);
}

static bool tcnc_ready_carrier(const tcnc_controller *controller, const tcnc_node *node) {
    return node->raw.type == controller->config.sharedType && tcnc_carrier_compatible(node) && node->markerValid
        && node->marker.phase == TCNM_MARKER_PHASE_READY && node->payloadValid;
}

static bool tcnc_stable_bootstrap_singleton(const tcnc_controller *controller, const tcnc_node *node) {
    return tcnc_ready_carrier(controller, node) && !node->marker.requiresClone && node->marker.peerModuleLow32 == 0;
}

static bool tcnc_marker_points_to(const tcnc_node *left, const tcnc_node *right) {
    if (!left || !right || !left->marker.peerModuleLow32)
        return false;
    uint64_t peerAddress = (left->moduleAddress & UINT64_C(0xffffffff00000000)) | left->marker.peerModuleLow32;
    return peerAddress == right->moduleAddress;
}

static bool tcnc_nodes_form_pair(const tcnc_controller *controller, const tcnc_node *left, const tcnc_node *right) {
    if (!tcnc_ready_carrier(controller, left) || !tcnc_ready_carrier(controller, right) || !left->marker.requiresClone
        || !right->marker.requiresClone || left->marker.sourceKind != right->marker.sourceKind
        || !tcnc_same_geometry(left, right)) {
        return false;
    }
    return tcnc_marker_points_to(left, right) || tcnc_marker_points_to(right, left);
}

static int tcnc_pair_relation(tcnc_pair *pair) {
    pair->relation = tcnm_entries_relation(pair->left->moduleBytes + TCNC_FILE_HEADER_SIZE,
                                           pair->right->moduleBytes + TCNC_FILE_HEADER_SIZE,
                                           pair->left->capacity,
                                           pair->left->stride);
    switch (pair->relation) {
        case TCNM_TABLE_RELATION_EQUAL:
        case TCNM_TABLE_RELATION_LEFT_STRICT_SUPERSET:
        case TCNM_TABLE_RELATION_RIGHT_STRICT_SUPERSET:
            return 0;
        default:
            return EPROTO;
    }
}

static int tcnc_find_pairs(const tcnc_controller *controller,
                           tcnc_snapshot *snapshot,
                           tcnc_pair **pairsOut,
                           uint32_t *pairCountOut,
                           bool *pairedOut) {
    if (!pairsOut || !pairCountOut || !pairedOut)
        return EINVAL;
    *pairsOut = NULL;
    *pairCountOut = 0;
    memset(pairedOut, 0, snapshot->count * sizeof(*pairedOut));
    tcnc_pair *pairs = calloc(snapshot->count / 2 + 1, sizeof(*pairs));
    if (!pairs)
        return ENOMEM;

    for (uint32_t left = 0; left < snapshot->count; left++) {
        if (pairedOut[left] || !tcnc_ready_carrier(controller, &snapshot->nodes[left])) {
            continue;
        }
        uint32_t match = UINT32_MAX;
        for (uint32_t right = left + 1; right < snapshot->count; right++) {
            if (pairedOut[right]
                || !tcnc_nodes_form_pair(controller, &snapshot->nodes[left], &snapshot->nodes[right])) {
                continue;
            }
            if (match != UINT32_MAX) {
                free(pairs);
                return EPROTO;
            }
            match = right;
        }
        if (match == UINT32_MAX)
            continue;
        tcnc_pair pair = {
            .left = &snapshot->nodes[left],
            .right = &snapshot->nodes[match],
        };
        int status = tcnc_pair_relation(&pair);
        if (status != 0) {
            free(pairs);
            return status;
        }
        pairs[*pairCountOut] = pair;
        (*pairCountOut)++;
        pairedOut[left] = true;
        pairedOut[match] = true;
    }
    *pairsOut = pairs;
    return 0;
}

/*
 * A newly expanded carrier initially points at the existing carrier, whose
 * marker still describes its pre-pair state. Finish that one-way publication
 * before generic marker recovery sees it. The reciprocal READY anchor owns all
 * target metadata while PREPARED_FILL makes the peer's partial marker ignorable.
 */
static int tcnc_normalize_pair_markers(const tcnc_controller *controller, tcnc_snapshot *snapshot) {
    for (uint32_t anchorIndex = 0; anchorIndex < snapshot->count; anchorIndex++) {
        tcnc_node *anchor = &snapshot->nodes[anchorIndex];
        if (!tcnc_ready_carrier(controller, anchor) || !anchor->marker.requiresClone
            || !anchor->marker.peerModuleLow32) {
            continue;
        }

        uint64_t peerAddress = (anchor->moduleAddress & UINT64_C(0xffffffff00000000)) | anchor->marker.peerModuleLow32;
        tcnc_node *peer = NULL;
        for (uint32_t peerIndex = 0; peerIndex < snapshot->count; peerIndex++) {
            tcnc_node *candidate = &snapshot->nodes[peerIndex];
            if (candidate->moduleAddress != peerAddress)
                continue;
            if (candidate == anchor || peer)
                return EPROTO;
            peer = candidate;
        }
        if (!peer)
            return EPROTO;

        uint32_t reverseAnchorCount = 0;
        for (uint32_t candidateIndex = 0; candidateIndex < snapshot->count; candidateIndex++) {
            tcnc_node *candidate = &snapshot->nodes[candidateIndex];
            if (candidate == peer || !tcnc_ready_carrier(controller, candidate) || !candidate->marker.requiresClone
                || !tcnc_marker_points_to(candidate, peer)) {
                continue;
            }
            reverseAnchorCount++;
        }
        if (reverseAnchorCount != 1)
            return EPROTO;

        if (tcnc_ready_carrier(controller, peer) && peer->marker.requiresClone && tcnc_marker_points_to(peer, anchor)) {
            continue;
        }
        bool peerIsReady = peer->markerHeaderKnown && peer->marker.phase == TCNM_MARKER_PHASE_READY;
        bool peerIsPreparedFill = peer->markerHeaderKnown && peer->marker.phase == TCNM_MARKER_PHASE_PREPARED_FILL;
        if (peer->raw.type != controller->config.sharedType || !tcnc_carrier_compatible(peer)
            || (!peerIsReady && !peerIsPreparedFill) || peer->marker.sourceKind != anchor->marker.sourceKind
            || (peerIsReady && peer->marker.peerModuleLow32
                && peer->marker.peerModuleLow32 != (uint32_t)anchor->moduleAddress)
            || !tcnc_same_geometry(anchor, peer)) {
            return EPROTO;
        }
        tcnm_table_relation relation = tcnm_entries_relation(anchor->moduleBytes + TCNC_FILE_HEADER_SIZE,
                                                             peer->moduleBytes + TCNC_FILE_HEADER_SIZE,
                                                             anchor->capacity,
                                                             anchor->stride);
        if (relation != TCNM_TABLE_RELATION_EQUAL && relation != TCNM_TABLE_RELATION_LEFT_STRICT_SUPERSET
            && relation != TCNM_TABLE_RELATION_RIGHT_STRICT_SUPERSET) {
            return EPROTO;
        }

        int status = tcnc_rebind_ready_marker(controller,
                                              peer,
                                              (uint32_t)anchor->moduleAddress,
                                              anchor->marker.sourceKind,
                                              anchor->marker.nonce);
        return status == 0 ? EINPROGRESS : status;
    }
    return 0;
}

static tcnc_node *tcnc_exact_source_for_peer(tcnc_snapshot *snapshot, const tcnc_node *peer) {
    tcnc_node *match = NULL;
    for (uint32_t index = 0; index < snapshot->count; index++) {
        tcnc_node *node = &snapshot->nodes[index];
        if (node->raw.type != peer->marker.sourceKind || !node->exactSource || !tcnc_same_geometry(node, peer)) {
            continue;
        }
        if (match)
            return NULL;
        match = node;
    }
    return match;
}

static int tcnc_reload(const tcnc_controller *controller, uint8_t sourceKind) {
    if (!controller->backend.reload_signed_source)
        return ENOTSUP;
    return tcnc_status(controller->backend.reload_signed_source(controller->backend.context, sourceKind));
}

static const tcnc_node *tcnc_snapshot_node_at(const tcnc_snapshot *snapshot, uint64_t address) {
    for (uint32_t index = 0; index < snapshot->count; index++) {
        if (snapshot->nodes[index].address == address) {
            return &snapshot->nodes[index];
        }
    }
    return NULL;
}

static int tcnc_reload_validate(const tcnc_snapshot *before,
                                const tcnc_snapshot *after,
                                uint8_t sourceKind,
                                const tcnc_node *geometry) {
    if (!before || !after || !geometry || after->count != before->count + 1) {
        return EPROTO;
    }
    for (uint32_t index = 0; index < before->count; index++) {
        if (!tcnc_snapshot_node_at(after, before->nodes[index].address)) {
            return EPROTO;
        }
    }
    const tcnc_node *newNode = NULL;
    for (uint32_t index = 0; index < after->count; index++) {
        const tcnc_node *candidate = &after->nodes[index];
        if (tcnc_snapshot_node_at(before, candidate->address))
            continue;
        if (newNode)
            return EPROTO;
        newNode = candidate;
    }
    if (!newNode || newNode->raw.type != sourceKind || !newNode->exactSource || !tcnc_same_geometry(newNode, geometry)
        || tcnc_count_exact_source(after, sourceKind, NULL) != 1) {
        return EPROTO;
    }
    return 0;
}

static int tcnc_reload_for_peer(const tcnc_controller *controller, const tcnc_snapshot *before, const tcnc_node *peer) {
    int reloadStatus = tcnc_reload(controller, peer->marker.sourceKind);
    tcnc_snapshot after = {0};
    int status = tcnc_scan(controller, &after);
    if (status == 0) {
        int validationStatus = tcnc_reload_validate(before, &after, peer->marker.sourceKind, peer);
        if (validationStatus == 0) {
            status = 0;
        } else if (reloadStatus != 0 && after.count == before->count) {
            status = reloadStatus;
        } else {
            status = validationStatus;
        }
    }
    tcnc_snapshot_dispose(&after);
    return status;
}

static int tcnc_convert_source(const tcnc_controller *controller,
                               const tcnc_node *source,
                               const uint8_t *targetPayload,
                               uint32_t peerModuleLow32,
                               bool requiresClone) {
    if (!source || !source->exactSource || !tcnc_source_kind(source->raw.type) || !tcnc_carrier_compatible(source)) {
        return EINVAL;
    }
    int status = 0;
    uint8_t nonce[4] = {0};
    status = tcnc_status(controller->config.nonce(controller->config.nonceContext, nonce));
    if (status != 0)
        return status;
    uint8_t prepared[TCNM_MARKER_SIZE] = {0};
    status = tcnc_marker_build(source,
                               peerModuleLow32,
                               TCNM_MARKER_PHASE_PREPARED_SOURCE,
                               source->raw.type,
                               requiresClone,
                               nonce,
                               source->moduleBytes + TCNC_FILE_HEADER_SIZE,
                               prepared);
    if (status != 0)
        return status;
    status = tcnc_replace_marker(controller, source, prepared, TCNC_MARKER_COMMIT_VERSION);
    if (status != 0)
        return status;
    status = tcnc_replace_type(controller, source, controller->config.sharedType);
    if (status != 0)
        return status;
    if (requiresClone) {
        tcnc_snapshot beforeReload = {0};
        tcnc_snapshot verification = {0};
        status = tcnc_scan(controller, &beforeReload);
        if (status != 0)
            return status;
        int reloadStatus = tcnc_reload(controller, source->raw.type);
        int scanStatus = tcnc_scan(controller, &verification);
        if (scanStatus == 0) {
            int validationStatus = tcnc_reload_validate(&beforeReload, &verification, source->raw.type, source);
            if (validationStatus == 0) {
                status = 0;
            } else if (reloadStatus != 0 && verification.count == beforeReload.count) {
                status = reloadStatus;
            } else {
                status = validationStatus;
            }
        } else {
            status = scanStatus;
        }
        tcnc_snapshot_dispose(&beforeReload);
        tcnc_snapshot_dispose(&verification);
        if (status != 0)
            return status;
    }

    /*
	 * Re-read after the Type/reload transaction so expected bytes never come
	 * from a stale userspace snapshot.
	 */
    tcnc_node current = {0};
    status = tcnc_read_node(controller, source->address, &current);
    if (status == 0) {
        current.marker = (tcnm_marker_fields){
            .peerModuleLow32 = peerModuleLow32,
            .phase = TCNM_MARKER_PHASE_PREPARED_SOURCE,
            .sourceKind = source->raw.type,
            .requiresClone = requiresClone,
        };
        memcpy(current.marker.nonce, nonce, sizeof(nonce));
        status = tcnc_prepare_and_finish(controller,
                                         &current,
                                         targetPayload,
                                         peerModuleLow32,
                                         source->raw.type,
                                         requiresClone,
                                         false,
                                         false);
    }
    tcnc_node_dispose(&current);
    return status;
}

static int tcnc_expand_for_unpaired(const tcnc_controller *controller, tcnc_snapshot *snapshot, const tcnc_node *peer) {
    tcnc_node *source = tcnc_exact_source_for_peer(snapshot, peer);
    if (!source)
        return ENOENT;
    return tcnc_convert_source(controller,
                               source,
                               peer->moduleBytes + TCNC_FILE_HEADER_SIZE,
                               (uint32_t)peer->moduleAddress,
                               true);
}

static int tcnc_make_empty_unpaired(const tcnc_controller *controller, tcnc_snapshot *snapshot) {
    tcnc_node *source = NULL;
    for (uint32_t index = 0; index < snapshot->count; index++) {
        tcnc_node *candidate = &snapshot->nodes[index];
        if (!candidate->exactSource || !tcnc_source_kind(candidate->raw.type) || !tcnc_carrier_compatible(candidate)) {
            continue;
        }
        if (!source || candidate->capacity > source->capacity) {
            source = candidate;
        }
    }
    if (!source)
        return ENOENT;
    size_t size = (size_t)source->capacity * source->stride;
    uint8_t *empty = calloc(1, size);
    if (!empty)
        return ENOMEM;
    int status = tcnc_convert_source(controller, source, empty, 0, true);
    free(empty);
    return status;
}

static int tcnc_recover_collapsed(const tcnc_controller *controller, tcnc_snapshot *snapshot) {
    for (uint32_t left = 0; left < snapshot->count; left++) {
        tcnc_node *live = &snapshot->nodes[left];
        if (!tcnc_ready_carrier(controller, live) || !live->marker.peerModuleLow32) {
            continue;
        }
        if (!live->marker.requiresClone)
            return EPROTO;
        uint32_t duplicateCount = 0;
        for (uint32_t candidate = 0; candidate < snapshot->count; candidate++) {
            if (snapshot->nodes[candidate].moduleAddress == live->moduleAddress
                && snapshot->nodes[candidate].raw.type == controller->config.sharedType) {
                duplicateCount++;
            }
        }
        if (duplicateCount > 2)
            return EPROTO;
        for (uint32_t right = left + 1; right < snapshot->count; right++) {
            tcnc_node *duplicate = &snapshot->nodes[right];
            if (duplicate->moduleAddress != live->moduleAddress
                || duplicate->raw.type != controller->config.sharedType) {
                continue;
            }
            uint64_t detachedAddress = (live->moduleAddress & UINT64_C(0xffffffff00000000))
                | live->marker.peerModuleLow32;
            uint64_t detachedRaw = (live->raw.module & UINT64_C(0xffffffff00000000)) | live->marker.peerModuleLow32;
            if (detachedAddress == live->moduleAddress)
                return EPROTO;

            tcnc_node detached = *live;
            detached.address = 0;
            detached.moduleAddress = detachedAddress;
            detached.raw.module = detachedRaw;
            detached.moduleBytes = NULL;
            int status = tcnc_read_node(controller, duplicate->address, &detached);
            /*
			 * tcnc_read_node reads the node's current pointer, so read the
			 * detached module explicitly with inherited geometry.
			 */
            if (status == 0) {
                free(detached.moduleBytes);
                detached.moduleBytes = malloc((size_t)live->raw.moduleSize);
                if (!detached.moduleBytes)
                    status = ENOMEM;
            }
            if (status == 0) {
                detached.moduleAddress = detachedAddress;
                detached.raw.module = detachedRaw;
                status = tcnc_read(controller, detachedAddress, detached.moduleBytes, (size_t)live->raw.moduleSize);
            }
            if (status == 0) {
                detached.stride = live->stride;
                detached.capacity = live->capacity;
                detached.payloadValid = tcnm_entries_validate(detached.moduleBytes + TCNC_FILE_HEADER_SIZE,
                                                              detached.capacity,
                                                              detached.stride)
                    == 0;
                (void)tcnc_marker_decode_node(&detached);
            }
            if (status != 0) {
                tcnc_node_dispose(&detached);
                return status;
            }

            tcnm_table_relation relation = tcnm_entries_relation(live->moduleBytes + TCNC_FILE_HEADER_SIZE,
                                                                 detached.moduleBytes + TCNC_FILE_HEADER_SIZE,
                                                                 live->capacity,
                                                                 live->stride);
            bool geometryExact = tcnc_same_geometry(live, &detached);
            bool detachedReady = detached.markerValid && detached.marker.phase == TCNM_MARKER_PHASE_READY
                && detached.payloadValid && detached.marker.requiresClone
                && detached.marker.sourceKind == live->marker.sourceKind
                && detached.marker.peerModuleLow32 == (uint32_t)live->moduleAddress && geometryExact;
            bool detachedBootstrapReady = detached.markerValid && detached.marker.phase == TCNM_MARKER_PHASE_READY
                && detached.payloadValid && !detached.marker.requiresClone && detached.marker.peerModuleLow32 == 0
                && detached.marker.sourceKind == live->marker.sourceKind && geometryExact;
            /*
			 * A collapsed pair has lost the node->bank association. Restore
			 * it by the controller's stable canonical ordering: the lower
			 * node address owns the lower module address. Expansion and every
			 * completed append preserve this association.
			 */
            bool liveOwnsDetached = (detachedAddress < live->moduleAddress) == (live->address < duplicate->address);
            tcnc_node *detachedOwner = liveOwnsDetached ? live : duplicate;
            if (detached.markerValid && detached.marker.phase == TCNM_MARKER_PHASE_READY && detached.payloadValid
                && !detachedReady && !detachedBootstrapReady) {
                tcnc_node_dispose(&detached);
                return EPROTO;
            }
            tcnm_ab_observed_state observed = {
                .nodeA = TCNM_BANK_POINTER_BANK0,
                .nodeB = TCNM_BANK_POINTER_BANK0,
                .relation = relation,
                .readComplete = true,
                .geometryExact = geometryExact,
                .typesShared = true,
                .detachedBankKnown = true,
                .bank0Ready = true,
                .bank1Ready = detachedReady || detachedBootstrapReady,
            };
            tcnm_ab_recovery_action action = tcnm_ab_recovery_decide(&observed);
            if (action == TCNM_AB_RECOVERY_REBUILD_BANK1_FROM_BANK0) {
                status = tcnc_prepare_and_finish(controller,
                                                 &detached,
                                                 live->moduleBytes + TCNC_FILE_HEADER_SIZE,
                                                 (uint32_t)live->moduleAddress,
                                                 live->marker.sourceKind,
                                                 true,
                                                 true,
                                                 false);
                if (status == 0) {
                    status = tcnc_publish_module_low(controller, detachedOwner, detachedRaw);
                }
            } else if (action == TCNM_AB_RECOVERY_PUBLISH_BANK1) {
                status = tcnc_publish_module_low(controller, detachedOwner, detachedRaw);
            } else if (action == TCNM_AB_RECOVERY_RETRY) {
                status = EAGAIN;
            } else if (action != TCNM_AB_RECOVERY_ACCEPT_READY) {
                status = EPROTO;
            }
            tcnc_node_dispose(&detached);
            return status == 0 ? EINPROGRESS : status;
        }
    }
    return 0;
}

static int tcnc_recover_one_node(const tcnc_controller *controller, tcnc_snapshot *snapshot, tcnc_node *node) {
    if (!tcnc_source_kind(node->raw.type) && node->raw.type != controller->config.sharedType) {
        return 0;
    }
    if (node->raw.type == controller->config.sharedType && node->markerHeaderKnown
        && node->marker.phase == TCNM_MARKER_PHASE_PREPARED_FILL && !node->marker.requiresClone
        && node->marker.peerModuleLow32 == 0 && node->payloadValid) {
        int status = tcnc_finish_singleton_fill(controller, node);
        return status == 0 ? EINPROGRESS : status;
    }

    uint8_t sourceKind = 0;
    if (tcnc_source_kind(node->raw.type)) {
        sourceKind = node->raw.type;
    } else if (node->markerHeaderKnown) {
        sourceKind = node->marker.sourceKind;
    } else if (node->canonicalSource) {
        sourceKind = node->canonicalSource->sourceKind;
    }

    const tcnc_node *excluding = tcnc_source_kind(node->raw.type) ? node : NULL;
    tcnm_observed_evidence evidence = {
        .observedType = node->raw.type,
        .sourceKind = sourceKind,
        .carrierType = controller->config.sharedType,
        .exactSource = node->exactSource,
        .markerHeaderKnown = node->markerHeaderKnown,
        .markerValid = node->markerValid,
        .markerPhase = node->marker.phase,
        .markerSourceKind = node->marker.sourceKind,
        .requiresClone = node->markerHeaderKnown && node->marker.requiresClone,
        .peerDeclared = node->markerHeaderKnown && node->marker.peerModuleLow32 != 0,
        .observedPayload = node->moduleBytes + TCNC_FILE_HEADER_SIZE,
        .canonicalPayload = node->canonicalModule ? node->canonicalModule + TCNC_FILE_HEADER_SIZE : NULL,
        .capacity = node->capacity,
        .stride = node->stride,
        .cloneScanStatus = 0,
        .exactCloneCount = sourceKind ? tcnc_count_exact_source(snapshot, sourceKind, excluding) : 0,
    };
    tcnm_observed_state observed = tcnm_observed_classify(&evidence);
    tcnm_recovery_action action = tcnm_recovery_decide(observed);
    if (action == TCNM_RECOVERY_ACCEPT_ORIGINAL || action == TCNM_RECOVERY_ACCEPT_READY) {
        return 0;
    }
    if (action == TCNM_RECOVERY_RELOAD_SOURCE) {
        int status = tcnc_reload_for_peer(controller, snapshot, node);
        return status == 0 ? EINPROGRESS : status;
    }
    if (action == TCNM_RECOVERY_RESTORE_ORIGINAL) {
        int status = tcnc_restore_original(controller, node);
        return status == 0 ? EINPROGRESS : status;
    }
    if (action == TCNM_RECOVERY_RESTORE_EMPTY_READY) {
        if (!node->marker.peerModuleLow32) {
            size_t payloadSize = (size_t)node->capacity * node->stride;
            uint8_t *empty = calloc(1, payloadSize);
            if (!empty)
                return ENOMEM;
            int status = tcnc_prepare_and_finish(controller,
                                                 node,
                                                 empty,
                                                 0,
                                                 node->marker.sourceKind,
                                                 true,
                                                 false,
                                                 false);
            free(empty);
            return status == 0 ? EINPROGRESS : status;
        }
        uint64_t peerAddress = (node->moduleAddress & UINT64_C(0xffffffff00000000)) | node->marker.peerModuleLow32;
        tcnc_node peer = {0};
        peer.raw = node->raw;
        peer.raw.module = peerAddress;
        peer.moduleAddress = peerAddress;
        peer.moduleBytes = malloc((size_t)peer.raw.moduleSize);
        if (!peer.moduleBytes)
            return ENOMEM;
        int status = tcnc_read(controller, peerAddress, peer.moduleBytes, (size_t)peer.raw.moduleSize);
        if (status == 0) {
            peer.stride = node->stride;
            peer.capacity = node->capacity;
            peer.payloadValid = tcnm_entries_validate(peer.moduleBytes + TCNC_FILE_HEADER_SIZE,
                                                      peer.capacity,
                                                      peer.stride)
                == 0;
            if (!peer.payloadValid)
                status = EPROTO;
        }
        if (status == 0) {
            status = tcnc_prepare_and_finish(controller,
                                             node,
                                             peer.moduleBytes + TCNC_FILE_HEADER_SIZE,
                                             node->marker.peerModuleLow32,
                                             node->marker.sourceKind,
                                             true,
                                             false,
                                             false);
        }
        tcnc_node_dispose(&peer);
        return status == 0 ? EINPROGRESS : status;
    }
    if (action == TCNM_RECOVERY_RETRY)
        return EAGAIN;
    return EPROTO;
}

static int tcnc_normalize_pair_payloads(const tcnc_controller *controller, tcnc_pair *pairs, uint32_t pairCount);

static int tcnc_recover_step(tcnc_controller *controller) {
    tcnc_snapshot snapshot = {0};
    int status = tcnc_scan(controller, &snapshot);
    if (status != 0)
        return status;
    status = tcnc_recover_collapsed(controller, &snapshot);
    if (status != 0) {
        tcnc_snapshot_dispose(&snapshot);
        return status;
    }
    status = tcnc_normalize_pair_markers(controller, &snapshot);
    if (status != 0) {
        tcnc_snapshot_dispose(&snapshot);
        return status;
    }
    /*
	 * Restore/finish observable transactions before deciding that a READY
	 * carrier has lost its source clone. Otherwise a torn source marker can
	 * be mistaken for an absent source and an unnecessary reload creates a
	 * second exact 13/14 node.
	 */
    for (uint32_t pass = 0; pass < 2; pass++) {
        for (uint32_t index = 0; index < snapshot.count; index++) {
            tcnc_node *node = &snapshot.nodes[index];
            bool ready = tcnc_ready_carrier(controller, node);
            if ((pass == 0 && ready) || (pass == 1 && !ready)) {
                continue;
            }
            status = tcnc_recover_one_node(controller, &snapshot, node);
            if (status != 0) {
                tcnc_snapshot_dispose(&snapshot);
                return status;
            }
        }
    }

    bool *paired = calloc(snapshot.count ? snapshot.count : 1, sizeof(*paired));
    tcnc_pair *pairs = NULL;
    uint32_t pairCount = 0;
    if (!paired)
        status = ENOMEM;
    if (status == 0) {
        status = tcnc_find_pairs(controller, &snapshot, &pairs, &pairCount, paired);
    }
    if (status == 0) {
        status = tcnc_normalize_pair_payloads(controller, pairs, pairCount);
    }
    if (status == 0 && controller->backend.reload_signed_source) {
        for (uint32_t index = 0; index < snapshot.count; index++) {
            tcnc_node *node = &snapshot.nodes[index];
            if (paired[index] || !tcnc_ready_carrier(controller, node) || node->marker.peerModuleLow32 != 0
                || !node->marker.requiresClone) {
                continue;
            }
            uint32_t sourceCount = tcnc_count_exact_source(&snapshot, node->marker.sourceKind, NULL);
            if (sourceCount == 0) {
                status = tcnc_reload_for_peer(controller, &snapshot, node);
            } else if (sourceCount == 1) {
                status = tcnc_expand_for_unpaired(controller, &snapshot, node);
            } else {
                status = EPROTO;
            }
            if (status == 0)
                status = EINPROGRESS;
            break;
        }
    }
    free(pairs);
    free(paired);
    tcnc_snapshot_dispose(&snapshot);
    return status;
}

static int tcnc_normalize_pair_payloads(const tcnc_controller *controller, tcnc_pair *pairs, uint32_t pairCount) {
    for (uint32_t index = 0; index < pairCount; index++) {
        tcnc_pair *pair = &pairs[index];
        if (pair->relation == TCNM_TABLE_RELATION_EQUAL)
            continue;

        tcnc_node *latest = NULL;
        tcnc_node *target = NULL;
        if (pair->relation == TCNM_TABLE_RELATION_LEFT_STRICT_SUPERSET) {
            latest = pair->left;
            target = pair->right;
        } else if (pair->relation == TCNM_TABLE_RELATION_RIGHT_STRICT_SUPERSET) {
            latest = pair->right;
            target = pair->left;
        } else {
            return EPROTO;
        }
        if (!latest->marker.requiresClone || !target->marker.requiresClone || !tcnc_marker_points_to(latest, target)
            || !tcnc_marker_points_to(target, latest)) {
            return EPROTO;
        }

        /*
		 * Keep one complete live generation while rebuilding the detached
		 * bank. A crash before READY rebuilds from latest; a crash after
		 * READY publishes this bank and converges to the same equal pair.
		 */
        int status = tcnc_publish_module_low(controller, target, latest->raw.module);
        if (status == 0) {
            status = tcnc_prepare_and_finish(controller,
                                             target,
                                             latest->moduleBytes + TCNC_FILE_HEADER_SIZE,
                                             (uint32_t)latest->moduleAddress,
                                             latest->marker.sourceKind,
                                             true,
                                             true,
                                             false);
        }
        if (status == 0) {
            status = tcnc_publish_module_low(controller, target, target->raw.module);
        }
        return status == 0 ? EINPROGRESS : status;
    }
    return 0;
}

static int tcnc_append_pair(const tcnc_controller *controller,
                            tcnc_pair *pair,
                            const tcnm_entry *entries,
                            uint32_t entryCount) {
    tcnc_node *latest = NULL;
    tcnc_node *target = NULL;
    switch (pair->relation) {
        case TCNM_TABLE_RELATION_LEFT_STRICT_SUPERSET:
            latest = pair->left;
            target = pair->right;
            break;
        case TCNM_TABLE_RELATION_RIGHT_STRICT_SUPERSET:
            latest = pair->right;
            target = pair->left;
            break;
        case TCNM_TABLE_RELATION_EQUAL:
            if (tcnc_marker_points_to(pair->left, pair->right)) {
                latest = pair->left;
                target = pair->right;
            } else if (tcnc_marker_points_to(pair->right, pair->left)) {
                latest = pair->right;
                target = pair->left;
            } else {
                return EPROTO;
            }
            break;
        default:
            return EPROTO;
    }
    if (!tcnc_marker_points_to(latest, target))
        return EPROTO;

    size_t payloadSize = (size_t)latest->capacity * latest->stride;
    uint8_t *desired = malloc(payloadSize);
    if (!desired)
        return ENOMEM;
    uint32_t used = 0;
    int status = tcnm_entries_merge(latest->moduleBytes + TCNC_FILE_HEADER_SIZE,
                                    latest->capacity,
                                    latest->stride,
                                    entries,
                                    entryCount,
                                    desired,
                                    &used);
    if (status != 0) {
        free(desired);
        return status;
    }

    /* First collapse the older node onto the latest complete generation. */
    status = tcnc_publish_module_low(controller, target, latest->raw.module);
    if (status == 0) {
        /* The old bank is now detached and may be invalidated/mutated. */
        status = tcnc_prepare_and_finish(controller,
                                         target,
                                         desired,
                                         (uint32_t)latest->moduleAddress,
                                         latest->marker.sourceKind,
                                         true,
                                         true,
                                         false);
    }
    if (status == 0) {
        /* The only publication of the new generation is one low word. */
        status = tcnc_publish_module_low(controller, target, target->raw.module);
    }
    free(desired);
    return status;
}

int tcnc_controller_create(const tcnc_config *config, const tcnc_backend *backend, tcnc_controller **controllerOut) {
    if (!config || !backend || !controllerOut || !backend->read || !backend->protected_replace || !config->listSlot
        || !config->pointerMask || !config->pointerMinimum || !tcnc_power_of_two(config->pageSize)
        || config->pageSize < sizeof(uint64_t) || !config->maxNodes || config->maxNodes > TCNC_MAX_NODE_LIMIT
        || !config->sharedType || config->sharedType == TCNM_SOURCE_OS || config->sharedType == TCNM_SOURCE_APP
        || !config->signedSources || !config->signedSourceCount || !config->nonce) {
        return EINVAL;
    }
    for (uint32_t index = 0; index < config->signedSourceCount; index++) {
        const tcnc_signed_source *source = &config->signedSources[index];
        if (!tcnc_source_kind(source->sourceKind) || !source->bytes || source->size < TCNC_FILE_HEADER_SIZE) {
            return EINVAL;
        }
        for (uint32_t other = 0; other < index; other++) {
            if (config->signedSources[other].sourceKind == source->sourceKind) {
                return EINVAL;
            }
        }
    }
    tcnc_controller *controller = calloc(1, sizeof(*controller));
    if (!controller)
        return ENOMEM;
    controller->config = *config;
    controller->backend = *backend;
    *controllerOut = controller;
    return 0;
}

void tcnc_controller_destroy(tcnc_controller *controller) {
    if (!controller)
        return;
    memset(controller, 0, sizeof(*controller));
    free(controller);
}

int tcnc_bootstrap_append(tcnc_controller *controller, const tcnm_entry *entries, uint32_t entryCount) {
    if (!controller || !entries || !entryCount)
        return EINVAL;
    tcnc_snapshot snapshot = {0};
    int status = tcnc_scan(controller, &snapshot);
    if (status != 0)
        return status;
    status = tcnc_quarantine_unmanaged_shared_nodes(controller, &snapshot);
    if (status != 0)
        goto out;
    tcnc_repair_previous_links_best_effort(controller, &snapshot);
    for (uint32_t index = 0; index < snapshot.count; index++) {
        if (snapshot.nodes[index].raw.type == controller->config.sharedType
            && snapshot.nodes[index].markerHeaderKnown) {
            status = EALREADY;
            goto out;
        }
    }

    /*
	 * Task07 consumes only one source. Prefer the largest compatible source
	 * so the same-kind runtime peer provides the most useful A/B capacity.
	 */
    tcnc_node *candidate = NULL;
    for (uint32_t index = 0; index < snapshot.count; index++) {
        tcnc_node *node = &snapshot.nodes[index];
        if (!node->exactSource || !tcnc_source_kind(node->raw.type) || !tcnc_carrier_compatible(node)) {
            continue;
        }
        if (!candidate || node->capacity > candidate->capacity) {
            candidate = node;
        }
    }
    status = candidate ? tcnc_bootstrap_candidate(controller, candidate, entries, entryCount) : ENOSPC;
out:
    tcnc_snapshot_dispose(&snapshot);
    return status;
}

int tcnc_recover_to_fixed_point(tcnc_controller *controller) {
    if (!controller)
        return EINVAL;
    tcnc_snapshot initial = {0};
    int status = tcnc_scan(controller, &initial);
    if (status != 0)
        return status;
    uint64_t budget64 = (uint64_t)initial.count * 16 + (uint64_t)controller->config.signedSourceCount * 4 + 1;
    tcnc_snapshot_dispose(&initial);
    uint32_t budget = budget64 > UINT32_MAX ? UINT32_MAX : (uint32_t)budget64;

    for (uint32_t step = 0; step < budget; step++) {
        int status = tcnc_recover_step(controller);
        if (status == 0)
            return 0;
        if (status != EINPROGRESS)
            return status;
    }
    return ELOOP;
}

int tcnc_prepare_runtime_pair(tcnc_controller *controller) {
    if (!controller)
        return EINVAL;
    int status = tcnc_recover_to_fixed_point(controller);
    if (status != 0)
        return status;

    /*
	 * At most two actions are needed: restore the singleton's exact source
	 * when absent, then convert it into the detached peer. The final pass
	 * only verifies the pair produced by recovery.
	 */
    for (uint32_t step = 0; step < 3; step++) {
        tcnc_snapshot snapshot = {0};
        status = tcnc_scan(controller, &snapshot);
        if (status != 0)
            return status;

        bool *paired = calloc(snapshot.count ? snapshot.count : 1, sizeof(*paired));
        tcnc_pair *pairs = NULL;
        uint32_t pairCount = 0;
        if (!paired)
            status = ENOMEM;
        if (status == 0) {
            status = tcnc_find_pairs(controller, &snapshot, &pairs, &pairCount, paired);
        }

        tcnc_node *singleton = NULL;
        uint32_t singletonCount = 0;
        if (status == 0) {
            for (uint32_t index = 0; index < snapshot.count; index++) {
                if (!tcnc_stable_bootstrap_singleton(controller, &snapshot.nodes[index])) {
                    continue;
                }
                singleton = &snapshot.nodes[index];
                singletonCount++;
            }
        }

        if (status == 0 && singletonCount == 0) {
            status = pairCount > 0 ? 0 : ENOENT;
            free(pairs);
            free(paired);
            tcnc_snapshot_dispose(&snapshot);
            return status;
        }
        if (status == 0 && singletonCount != 1)
            status = EPROTO;

        if (status == 0) {
            uint32_t sourceCount = tcnc_count_exact_source(&snapshot, singleton->marker.sourceKind, NULL);
            if (sourceCount == 0) {
                status = tcnc_reload_for_peer(controller, &snapshot, singleton);
            } else if (sourceCount == 1) {
                status = tcnc_expand_for_unpaired(controller, &snapshot, singleton);
            } else {
                status = EPROTO;
            }
        }

        free(pairs);
        free(paired);
        tcnc_snapshot_dispose(&snapshot);
        if (status != 0) {
            int recoveryStatus = tcnc_recover_to_fixed_point(controller);
            return recoveryStatus == 0 ? status : recoveryStatus;
        }
        status = tcnc_recover_to_fixed_point(controller);
        if (status != 0)
            return status;
    }
    return ELOOP;
}

static int tcnc_collect_missing_entries(tcnc_controller *controller,
                                        const tcnm_entry *entries,
                                        uint32_t entryCount,
                                        tcnm_entry **missingOut,
                                        uint32_t *missingCountOut) {
    if (!missingOut || !missingCountOut)
        return EINVAL;
    *missingOut = NULL;
    *missingCountOut = 0;
    tcnm_entry *missing = malloc((size_t)entryCount * sizeof(*missing));
    if (!missing)
        return ENOMEM;

    tcnc_snapshot snapshot = {0};
    bool snapshotReady = false;
    int status = 0;
    for (uint32_t index = 0; index < entryCount; index++) {
        if (tcnm_hash_is_zero(entries[index].hash)) {
            status = EINVAL;
            goto out;
        }
        bool duplicate = false;
        for (uint32_t prior = 0; prior < *missingCountOut; prior++) {
            if (memcmp(missing[prior].hash, entries[index].hash, TCNM_HASH_SIZE) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
            continue;

        if (!snapshotReady) {
            status = tcnc_scan(controller, &snapshot);
            if (status != 0)
                goto out;
            snapshotReady = true;
        }
        bool found = false;
        status = tcnc_snapshot_query(&snapshot, entries[index].hash, &found);
        if (status != 0)
            goto out;
        if (!found) {
            missing[(*missingCountOut)++] = entries[index];
        }
    }
out:
    tcnc_snapshot_dispose(&snapshot);
    if (status != 0) {
        free(missing);
        *missingCountOut = 0;
        return status;
    }
    if (*missingCountOut == 0) {
        free(missing);
        return 0;
    }
    *missingOut = missing;
    return 0;
}

static uint32_t tcnc_node_used(const tcnc_node *node) {
    uint32_t used = 0;
    const uint8_t *payload = node->moduleBytes + TCNC_FILE_HEADER_SIZE;
    for (uint32_t index = 0; index < node->capacity; index++) {
        if (!tcnm_hash_is_zero(payload + (size_t)index * node->stride)) {
            used++;
        }
    }
    return used;
}

static int tcnc_entry_hash_compare(const void *leftValue, const void *rightValue) {
    const tcnm_entry *left = leftValue;
    const tcnm_entry *right = rightValue;
    return memcmp(left->hash, right->hash, TCNM_HASH_SIZE);
}

int tcnc_copy_entries(tcnc_controller *controller, tcnm_entry **entriesOut, uint32_t *entryCountOut) {
    if (!controller || !entriesOut || !entryCountOut)
        return EINVAL;
    *entriesOut = NULL;
    *entryCountOut = 0;

    tcnc_snapshot snapshot = {0};
    int status = tcnc_scan(controller, &snapshot);
    if (status != 0)
        return status;

    uint64_t capacity = 0;
    for (uint32_t index = 0; index < snapshot.count; index++) {
        tcnc_node *node = &snapshot.nodes[index];
        if (!tcnc_ready_carrier(controller, node))
            continue;
        capacity += tcnc_node_used(node);
        if (capacity > UINT32_MAX || capacity > SIZE_MAX / sizeof(tcnm_entry)) {
            status = EOVERFLOW;
            goto out;
        }
    }
    if (capacity == 0)
        goto out;

    tcnm_entry *entries = malloc((size_t)capacity * sizeof(*entries));
    if (!entries) {
        status = ENOMEM;
        goto out;
    }

    uint32_t entryCount = 0;
    for (uint32_t index = 0; index < snapshot.count; index++) {
        tcnc_node *node = &snapshot.nodes[index];
        if (!tcnc_ready_carrier(controller, node))
            continue;
        uint32_t decoded = 0;
        status = tcnm_entries_decode(node->moduleBytes + TCNC_FILE_HEADER_SIZE,
                                     node->capacity,
                                     node->stride,
                                     entries + entryCount,
                                     (uint32_t)capacity - entryCount,
                                     &decoded);
        if (status != 0) {
            free(entries);
            goto out;
        }
        entryCount += decoded;
    }

    qsort(entries, entryCount, sizeof(*entries), tcnc_entry_hash_compare);
    uint32_t uniqueCount = 0;
    for (uint32_t index = 0; index < entryCount; index++) {
        if (uniqueCount != 0 && memcmp(entries[uniqueCount - 1].hash, entries[index].hash, TCNM_HASH_SIZE) == 0) {
            continue;
        }
        entries[uniqueCount++] = entries[index];
    }
    *entriesOut = entries;
    *entryCountOut = uniqueCount;

out:
    tcnc_snapshot_dispose(&snapshot);
    return status;
}

int tcnc_append(tcnc_controller *controller, const tcnm_entry *entries, uint32_t entryCount) {
    if (!controller || !entries || !entryCount)
        return EINVAL;
    int status = tcnc_recover_to_fixed_point(controller);
    if (status != 0)
        return status;

    tcnm_entry *missing = NULL;
    uint32_t missingCount = 0;
    status = tcnc_collect_missing_entries(controller, entries, entryCount, &missing, &missingCount);
    if (status != 0 || missingCount == 0) {
        free(missing);
        return status;
    }

    uint32_t offset = 0;
    while (offset < missingCount) {
        tcnc_snapshot snapshot = {0};
        status = tcnc_scan(controller, &snapshot);
        if (status != 0)
            break;
        bool *paired = calloc(snapshot.count ? snapshot.count : 1, sizeof(*paired));
        tcnc_pair *pairs = NULL;
        uint32_t pairCount = 0;
        if (!paired)
            status = ENOMEM;
        if (status == 0) {
            status = tcnc_find_pairs(controller, &snapshot, &pairs, &pairCount, paired);
        }

        bool appended = false;
        bool bootstrapSingleton = false;
        if (status == 0) {
            for (uint32_t index = 0; index < snapshot.count; index++) {
                if (tcnc_stable_bootstrap_singleton(controller, &snapshot.nodes[index])) {
                    bootstrapSingleton = true;
                    break;
                }
            }
            for (uint32_t index = 0; index < pairCount; index++) {
                tcnc_node *node = pairs[index].left;
                uint32_t used = tcnc_node_used(node);
                uint32_t freeCount = node->capacity - used;
                if (!freeCount)
                    continue;
                uint32_t chunk = missingCount - offset;
                if (chunk > freeCount)
                    chunk = freeCount;
                status = tcnc_append_pair(controller, &pairs[index], missing + offset, chunk);
                if (status != 0)
                    break;
                offset += chunk;
                appended = true;
                break;
            }
        }

        if (status == 0 && !appended) {
            if (bootstrapSingleton) {
                /*
				 * A live singleton is never rewritten in place. Promote it
				 * through the canonical detached-peer transaction, then
				 * retry the pending append against the recovered pair.
				 */
                status = tcnc_prepare_runtime_pair(controller);
            } else if (snapshot.count >= controller->config.maxNodes) {
                status = ENOSPC;
            } else {
                /* No live carrier has space: this is real expansion. */
                status = tcnc_make_empty_unpaired(controller, &snapshot);
            }
        }

        free(pairs);
        free(paired);
        tcnc_snapshot_dispose(&snapshot);
        if (status != 0)
            break;
        status = tcnc_recover_to_fixed_point(controller);
        if (status != 0)
            break;
    }
    free(missing);
    return status;
}

int tcnc_query(tcnc_controller *controller, const uint8_t hash[TCNM_HASH_SIZE], bool *foundOut) {
    if (!controller || !hash || !foundOut || tcnm_hash_is_zero(hash)) {
        return EINVAL;
    }
    *foundOut = false;
    tcnc_snapshot snapshot = {0};
    int status = tcnc_scan(controller, &snapshot);
    if (status != 0)
        return status;
    status = tcnc_snapshot_query(&snapshot, hash, foundOut);
    tcnc_snapshot_dispose(&snapshot);
    return status;
}

int tcnc_signed_sources_present(tcnc_controller *controller, bool *osPresentOut, bool *appPresentOut) {
    if (!controller || !osPresentOut || !appPresentOut)
        return EINVAL;
    *osPresentOut = false;
    *appPresentOut = false;
    tcnc_snapshot snapshot = {0};
    int status = tcnc_scan(controller, &snapshot);
    if (status != 0)
        return status;
    *osPresentOut = tcnc_count_exact_source(&snapshot, TCNM_SOURCE_OS, NULL) != 0;
    *appPresentOut = tcnc_count_exact_source(&snapshot, TCNM_SOURCE_APP, NULL) != 0;
    tcnc_snapshot_dispose(&snapshot);
    return 0;
}
