#include "trustcache_nokcall_kernel.h"

#include <errno.h>
#include <fcntl.h>
#include <mach-o/loader.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "info.h"
#include "primitives.h"
#include "translation.h"
#include "trustcache_nokcall_word32.h"

#ifndef DEBUG
#define DEBUG 0
#endif

#define TCNK_MAX_NODES UINT32_C(4096)
#define TCNK_MAX_MODULE_SIZE UINT64_C(0x04000000)
#define TCNK_RUNTIME_CONFIG_MASK UINT64_C(0x00010101)
#define TCNK_MAX_LOAD_COMMAND_BYTES UINT64_C(0x10000)
#define TCNK_MAX_DATA_SEGMENT_SIZE UINT64_C(0x2000000)

#define TCNK_SPTM_ARGS_DEBUG_HEADER_OFFSET UINT64_C(0x60)
#define TCNK_DEBUG_HEADER_COUNT_OFFSET UINT64_C(0x8)
#define TCNK_DEBUG_HEADER_TXM_IMAGE_OFFSET UINT64_C(0x20)

#define TCNK_POINTER_MINIMUM UINT64_C(0xFFFF000000000000)
#define TCNK_POINTER_SIGN_BIT UINT64_C(0x0080000000000000)
#define TCNK_SHARED_CARRIER_TYPE UINT8_C(2)
#define TCNK_NODE_TYPE_LAST UINT8_C(0x17)

#define TCNK_APP_SOURCE_PATH \
	"/private/preboot/cryptex1/current/app.dmg.trustcache"
#define TCNK_OS_SOURCE_PATH \
	"/private/preboot/cryptex1/current/os.dmg.trustcache"

typedef struct {
    uint64_t next;
    uint64_t previous;
    uint8_t type;
    uint8_t reserved[7];
    uint64_t moduleSize;
    uint64_t module;
} tcnk_node;

typedef struct {
    uint32_t version;
    uint8_t uuid[TCNM_MARKER_SIZE];
    uint32_t length;
} __attribute__((packed)) tcnk_file_header;

typedef struct {
    const char *path;
    uint8_t sourceKind;
} tcnk_source_spec;

struct tcnk_kernel {
    uint64_t listSlot;
    tcnc_signed_source signedSources[2];
    uint32_t signedSourceCount;
    tcnk_reload_signed_source reload;
    void *reloadContext;
};

_Static_assert(sizeof(tcnk_node) == 40U, "SPTM loadable trust-cache node layout changed");
_Static_assert(sizeof(tcnk_file_header) == TCNM_FILE_HEADER_SIZE, "trust-cache file header layout changed");

static int tcnk_backend_read(void *context, uint64_t address, void *output, size_t size);
static int tcnk_backend_protected_replace(void *context,
                                          uint64_t address,
                                          const void *expected,
                                          const void *desired,
                                          size_t size);
static int tcnk_backend_reload_signed_source(void *context, uint8_t sourceKind);
static int tcnk_validate_environment(void);
static bool tcnk_power_of_two(uint64_t value);
static int tcnk_status(int status);
static int tcnk_nonce(void *context, uint8_t nonce[4]);
static uint64_t tcnk_canonical_pointer(uint64_t rawPointer);
static bool tcnk_pointer(uint64_t rawPointer, size_t alignment, uint64_t *pointerOut);
static int tcnk_read_kernel(uint64_t address, void *output, size_t size);
static int tcnk_read_pointer(uint64_t address, uint64_t *pointerOut);
static int tcnk_validate_file(uint64_t moduleAddress, uint64_t moduleSize);
static int tcnk_validate_node(uint64_t nodeAddress);
static bool tcnk_runtime_shape_is_valid(const uint64_t runtime[5]);
static int tcnk_runtime_candidate(uint64_t runtimeAddress, uint64_t *listSlotOut);
static int tcnk_resolve_list_slot(uint64_t *listSlotOut);
static int tcnk_resolve_list_slot_from_txm(uint64_t *listSlotOut);
static int tcnk_scan_txm_image(uint64_t loadAddress, uint64_t *listSlotOut);
static int tcnk_validate_load_commands(const struct mach_header_64 *header,
                                       const uint8_t *commands,
                                       uint64_t *textAddressOut);
static int tcnk_scan_data_segments(const struct mach_header_64 *header,
                                   const uint8_t *commands,
                                   uint64_t slide,
                                   uint64_t *listSlotOut);
static int tcnk_scan_runtime_region(uint64_t address, uint64_t size, uint64_t *listSlotOut);
static int tcnk_map_signed_sources(tcnk_kernel *kernel);
static int tcnk_map_source(const char *path, const uint8_t **bytesOut, size_t *sizeOut);

static int tcnk_debug_status(const char *phase, int status) {
#if DEBUG
    if (status != 0) {
        fprintf(stderr, "[trustcache_nokcall] phase=%s status=%d\n", phase, status);
    }
#else
    (void)phase;
#endif
    return status;
}

static void tcnk_debug_pointer_rejection(const char *phase, uint64_t rawPointer, size_t alignment) {
#if DEBUG
    fprintf(stderr,
            "[trustcache_nokcall] phase=%s status=%d " "raw=0x%llx canonical=0x%llx mask=0x%llx alignment=%zu\n",
            phase,
            EFAULT,
            (unsigned long long)rawPointer,
            (unsigned long long)tcnk_canonical_pointer(rawPointer),
            (unsigned long long)kconstant(pointer_mask),
            alignment);
#else
    (void)phase;
    (void)rawPointer;
    (void)alignment;
#endif
}

int tcnk_kernel_create(tcnk_reload_signed_source reload, void *reloadContext, tcnk_kernel **kernelOut) {
    if (!kernelOut)
        return EINVAL;
    *kernelOut = NULL;

    int status = tcnk_validate_environment();
    if (status != 0) {
        return tcnk_debug_status("kernel.environment", status);
    }

    tcnk_kernel *kernel = calloc(1, sizeof(*kernel));
    if (!kernel)
        return ENOMEM;
    kernel->reload = reload;
    kernel->reloadContext = reloadContext;

    status = tcnk_resolve_list_slot(&kernel->listSlot);
    if (status == 0)
        status = tcnk_map_signed_sources(kernel);
    if (status != 0) {
        tcnk_debug_status(kernel->listSlot ? "kernel.map_signed_sources" : "kernel.resolve_list_slot", status);
        tcnk_kernel_destroy(kernel);
        return status;
    }

    *kernelOut = kernel;
    return 0;
}

int tcnk_kernel_prepare(tcnk_kernel *kernel, tcnc_config *configOut, tcnc_backend *backendOut) {
    if (!configOut || !backendOut)
        return EINVAL;
    memset(configOut, 0, sizeof(*configOut));
    memset(backendOut, 0, sizeof(*backendOut));
    if (!kernel || !kernel->listSlot || !kernel->signedSourceCount) {
        return EINVAL;
    }

    uint64_t pageSize = vm_real_kernel_page_size;
    uint64_t pointerMask = kconstant(pointer_mask);
    if (!tcnk_power_of_two(pageSize) || !pointerMask)
        return EPROTO;

    *configOut = (tcnc_config){
        .listSlot = kernel->listSlot,
        .pointerMask = pointerMask,
        .pointerMinimum = TCNK_POINTER_MINIMUM,
        .pageSize = pageSize,
        .maxNodes = TCNK_MAX_NODES,
        .sharedType = TCNK_SHARED_CARRIER_TYPE,
        .signedSources = kernel->signedSources,
        .signedSourceCount = kernel->signedSourceCount,
        .nonce = tcnk_nonce,
        .nonceContext = kernel,
    };
    *backendOut = (tcnc_backend){
        .read = tcnk_backend_read,
        .protected_replace = tcnk_backend_protected_replace,
        .reload_signed_source = kernel->reload ? tcnk_backend_reload_signed_source : NULL,
        .context = kernel,
    };
    return 0;
}

void tcnk_kernel_destroy(tcnk_kernel *kernel) {
    if (!kernel)
        return;
    for (uint32_t index = 0; index < kernel->signedSourceCount; index++) {
        const tcnc_signed_source *source = &kernel->signedSources[index];
        if (source->bytes && source->size) {
            munmap((void *)source->bytes, source->size);
        }
    }
    free(kernel);
}

void tcnk_kernel_destroy_context(void *context) {
    tcnk_kernel_destroy(context);
}

static int tcnk_validate_environment(void) {
    uint64_t pageSize = vm_real_kernel_page_size;
    if (!tcnk_power_of_two(pageSize) || pageSize < sizeof(uint64_t) || !kconstant(pointer_mask)) {
        return EPROTO;
    }
    return tcn_word32_environment_status();
}

static bool tcnk_power_of_two(uint64_t value) {
    return value && (value & (value - 1)) == 0;
}

static int tcnk_status(int status) {
    return status == 0 ? 0 : (status > 0 && status <= ELAST ? status : EIO);
}

static int tcnk_nonce(void *context, uint8_t nonce[4]) {
    if (!context || !nonce)
        return EINVAL;
    arc4random_buf(nonce, 4);
    return 0;
}

static uint64_t tcnk_canonical_pointer(uint64_t rawPointer) {
    uint64_t pointerMask = kconstant(pointer_mask);
    return (rawPointer & TCNK_POINTER_SIGN_BIT) ? rawPointer | pointerMask : rawPointer & ~pointerMask;
}

static bool tcnk_pointer(uint64_t rawPointer, size_t alignment, uint64_t *pointerOut) {
    if (!rawPointer || !alignment || (alignment & (alignment - 1)) != 0) {
        return false;
    }
    uint64_t pointer = tcnk_canonical_pointer(rawPointer);
    if (pointer < TCNK_POINTER_MINIMUM || (pointer & (alignment - 1)) != 0) {
        return false;
    }
    if (pointerOut)
        *pointerOut = pointer;
    return true;
}

static int tcnk_read_kernel(uint64_t address, void *output, size_t size) {
    if (!address || !output || !size)
        return EINVAL;
    if (address > UINT64_MAX - (size - 1))
        return EOVERFLOW;
    memset(output, 0, size);

    if (gPrimitives.kreadbuf) {
        return tcnk_status(gPrimitives.kreadbuf(address, output, size));
    }
    if (!gPrimitives.physreadbuf || (!gPrimitives.kvtophys && !gPrimitives.vtophys)) {
        return ENOTSUP;
    }

    uint64_t pageSize = vm_real_kernel_page_size;
    size_t completed = 0;
    while (completed < size) {
        uint64_t currentAddress = address + completed;
        size_t pageRemaining = (size_t)(pageSize - (currentAddress & (pageSize - 1)));
        size_t chunk = size - completed;
        if (chunk > pageRemaining)
            chunk = pageRemaining;

        errno = 0;
        uint64_t physicalAddress = kvtophys(currentAddress);
        if (!physicalAddress)
            return errno ? errno : ENXIO;
        int status = tcnk_status(gPrimitives.physreadbuf(physicalAddress, (uint8_t *)output + completed, chunk));
        if (status != 0)
            return status;
        completed += chunk;
    }
    return 0;
}

static int tcnk_read_pointer(uint64_t address, uint64_t *pointerOut) {
    if (!pointerOut)
        return EINVAL;
    *pointerOut = 0;
    uint64_t rawPointer = 0;
    int status = tcnk_read_kernel(address, &rawPointer, sizeof(rawPointer));
    if (status != 0)
        return status;
    if (!tcnk_pointer(rawPointer, sizeof(uint64_t), pointerOut)) {
        tcnk_debug_pointer_rejection("kernel.read_pointer", rawPointer, sizeof(uint64_t));
        return EFAULT;
    }
    return 0;
}

static int tcnk_validate_file(uint64_t moduleAddress, uint64_t moduleSize) {
    if (moduleSize < sizeof(tcnk_file_header) || moduleSize > TCNK_MAX_MODULE_SIZE) {
        return EPROTO;
    }
    tcnk_file_header file = {0};
    int status = tcnk_read_kernel(moduleAddress, &file, sizeof(file));
    if (status != 0)
        return status;

    size_t stride = tcnm_entry_stride(file.version);
    if (!stride || !file.length || (uint64_t)file.length > (moduleSize - sizeof(file)) / stride) {
        return EPROTO;
    }
    return 0;
}

static int tcnk_validate_node(uint64_t nodeAddress) {
    tcnk_node node = {0};
    int status = tcnk_read_kernel(nodeAddress, &node, sizeof(node));
    if (status != 0)
        return status;
    if (node.type < TCNK_SHARED_CARRIER_TYPE || node.type > TCNK_NODE_TYPE_LAST) {
        return EPROTO;
    }

    uint64_t moduleAddress = 0;
    if (!tcnk_pointer(node.module, 1, &moduleAddress)) {
        tcnk_debug_pointer_rejection("kernel.node_module", node.module, 1);
        return EFAULT;
    }
    return tcnk_validate_file(moduleAddress, node.moduleSize);
}

/*
 * runtime[3] is an optional engineering node, not a reserved zero field.
 * A populated slot is validated as a pointer in tcnk_runtime_candidate().
 */
static bool tcnk_runtime_shape_is_valid(const uint64_t runtime[5]) {
    return runtime && runtime[0] && (runtime[1] & ~TCNK_RUNTIME_CONFIG_MASK) == 0 && runtime[2] && runtime[4];
}

static int tcnk_runtime_candidate(uint64_t runtimeAddress, uint64_t *listSlotOut) {
    if (!runtimeAddress || !listSlotOut)
        return EINVAL;
    uint64_t runtime[5] = {0};
    int status = tcnk_read_kernel(runtimeAddress, runtime, sizeof(runtime));
    if (status != 0)
        return status;
    if (!tcnk_runtime_shape_is_valid(runtime))
        return ENOENT;

    uint64_t staticNodeAddress = 0;
    uint64_t listSlot = 0;
    bool staticNodeValid = tcnk_pointer(runtime[2], sizeof(uint64_t), &staticNodeAddress);
    bool engineeringNodeValid = !runtime[3] || tcnk_pointer(runtime[3], sizeof(uint64_t), NULL);
    bool listSlotValid = tcnk_pointer(runtime[4], sizeof(uint64_t), &listSlot);
    if (!staticNodeValid || !engineeringNodeValid || !listSlotValid) {
        if (!staticNodeValid) {
            tcnk_debug_pointer_rejection("kernel.runtime_static_node", runtime[2], sizeof(uint64_t));
        }
        if (!engineeringNodeValid) {
            tcnk_debug_pointer_rejection("kernel.runtime_engineering_node", runtime[3], sizeof(uint64_t));
        }
        if (!listSlotValid) {
            tcnk_debug_pointer_rejection("kernel.runtime_list_slot", runtime[4], sizeof(uint64_t));
        }
        return EFAULT;
    }

    tcnk_node staticNode = {0};
    status = tcnk_read_kernel(staticNodeAddress, &staticNode, sizeof(staticNode));
    if (status != 0)
        return status;
    if (staticNode.type != 0)
        return EPROTO;

    uint64_t staticModuleAddress = 0;
    if (!tcnk_pointer(staticNode.module, 1, &staticModuleAddress)) {
        tcnk_debug_pointer_rejection("kernel.static_module", staticNode.module, 1);
        return EFAULT;
    }
    status = tcnk_validate_file(staticModuleAddress, staticNode.moduleSize);
    if (status != 0)
        return status;

    uint64_t headRaw = 0;
    status = tcnk_read_kernel(listSlot, &headRaw, sizeof(headRaw));
    if (status != 0)
        return status;
    if (headRaw) {
        uint64_t headAddress = 0;
        if (!tcnk_pointer(headRaw, sizeof(uint64_t), &headAddress)) {
            tcnk_debug_pointer_rejection("kernel.list_head", headRaw, sizeof(uint64_t));
            return EFAULT;
        }
        status = tcnk_validate_node(headAddress);
        if (status != 0)
            return status;
    }

    *listSlotOut = listSlot;
    return 0;
}

static int tcnk_resolve_list_slot(uint64_t *listSlotOut) {
    if (!listSlotOut)
        return EINVAL;
    *listSlotOut = 0;

    uint64_t runtimeSymbol = ksymbol(ppl_trust_cache_rt);
    if (runtimeSymbol) {
        /*
		 * XPF resolves ppl_trust_cache_rt to the runtime structure itself;
		 * its word at +0x20 owns the loadable trust-cache list slot.
		 */
        int status = tcnk_runtime_candidate(runtimeSymbol, listSlotOut);
        if (status == 0)
            return 0;
        tcnk_debug_status("kernel.direct_runtime_anchor", status);
    }
    int status = tcnk_resolve_list_slot_from_txm(listSlotOut);
    return tcnk_debug_status("kernel.txm_image_anchor", status);
}

static int tcnk_resolve_list_slot_from_txm(uint64_t *listSlotOut) {
    uint64_t sptmArgsSymbol = rlx_ksymbol(sptm_args);
    uint64_t sptmArgs = 0;
    uint64_t debugHeader = 0;
    if (!sptmArgsSymbol) {
        return tcnk_debug_status("kernel.txm.sptm_args_symbol", ENOENT);
    }
    int status = tcnk_read_pointer(sptmArgsSymbol, &sptmArgs);
    if (status != 0) {
        return tcnk_debug_status("kernel.txm.sptm_args_pointer", status);
    }
    if (sptmArgs > UINT64_MAX - TCNK_SPTM_ARGS_DEBUG_HEADER_OFFSET) {
        return EOVERFLOW;
    }
    status = tcnk_read_pointer(sptmArgs + TCNK_SPTM_ARGS_DEBUG_HEADER_OFFSET, &debugHeader);
    if (status != 0) {
        return tcnk_debug_status("kernel.txm.debug_header_pointer", status);
    }
    if (debugHeader > UINT64_MAX - TCNK_DEBUG_HEADER_TXM_IMAGE_OFFSET) {
        return EOVERFLOW;
    }

    uint32_t imageCount = 0;
    status = tcnk_read_kernel(debugHeader + TCNK_DEBUG_HEADER_COUNT_OFFSET, &imageCount, sizeof(imageCount));
    if (status != 0) {
        return tcnk_debug_status("kernel.txm.image_count_read", status);
    }
    if (imageCount <= 2) {
        return tcnk_debug_status("kernel.txm.image_count", ENOENT);
    }

    uint64_t txmLoadAddress = 0;
    status = tcnk_read_pointer(debugHeader + TCNK_DEBUG_HEADER_TXM_IMAGE_OFFSET, &txmLoadAddress);
    if (status != 0) {
        return tcnk_debug_status("kernel.txm.load_address_pointer", status);
    }
    status = tcnk_scan_txm_image(txmLoadAddress, listSlotOut);
    return tcnk_debug_status("kernel.txm.scan_image", status);
}

static int tcnk_scan_txm_image(uint64_t loadAddress, uint64_t *listSlotOut) {
    struct mach_header_64 header = {0};
    int status = tcnk_read_kernel(loadAddress, &header, sizeof(header));
    if (status != 0)
        return status;
    if (header.magic != MH_MAGIC_64 || !header.ncmds || !header.sizeofcmds
        || header.sizeofcmds > TCNK_MAX_LOAD_COMMAND_BYTES
        || header.ncmds > header.sizeofcmds / sizeof(struct load_command)) {
        return EPROTO;
    }
    if (loadAddress > UINT64_MAX - sizeof(header) - header.sizeofcmds) {
        return EOVERFLOW;
    }

    uint8_t *commands = malloc(header.sizeofcmds);
    if (!commands)
        return ENOMEM;
    status = tcnk_read_kernel(loadAddress + sizeof(header), commands, header.sizeofcmds);
    if (status != 0) {
        free(commands);
        return status;
    }

    uint64_t textAddress = 0;
    status = tcnk_validate_load_commands(&header, commands, &textAddress);
    if (status != 0 || !textAddress || loadAddress < textAddress) {
        free(commands);
        return status != 0 ? status : EPROTO;
    }

    uint64_t slide = loadAddress - textAddress;
    status = tcnk_scan_data_segments(&header, commands, slide, listSlotOut);
    free(commands);
    return status;
}

static int tcnk_validate_load_commands(const struct mach_header_64 *header,
                                       const uint8_t *commands,
                                       uint64_t *textAddressOut) {
    if (!header || !commands || !textAddressOut)
        return EINVAL;
    *textAddressOut = 0;

    const uint8_t *cursor = commands;
    const uint8_t *end = commands + header->sizeofcmds;
    for (uint32_t index = 0; index < header->ncmds; index++) {
        if ((size_t)(end - cursor) < sizeof(struct load_command)) {
            return EPROTO;
        }
        const struct load_command *command = (const struct load_command *)cursor;
        if (command->cmdsize < sizeof(*command) || command->cmdsize > (size_t)(end - cursor)) {
            return EPROTO;
        }
        if (command->cmd == LC_SEGMENT_64) {
            if (command->cmdsize < sizeof(struct segment_command_64)) {
                return EPROTO;
            }
            const struct segment_command_64 *segment = (const struct segment_command_64 *)command;
            if (strncmp(segment->segname, "__TEXT", sizeof(segment->segname)) == 0) {
                if (*textAddressOut && *textAddressOut != segment->vmaddr) {
                    return EPROTO;
                }
                *textAddressOut = segment->vmaddr;
            }
        }
        cursor += command->cmdsize;
    }
    return cursor == end ? 0 : EPROTO;
}

static int tcnk_scan_data_segments(const struct mach_header_64 *header,
                                   const uint8_t *commands,
                                   uint64_t slide,
                                   uint64_t *listSlotOut) {
    const uint8_t *cursor = commands;
    for (uint32_t index = 0; index < header->ncmds; index++) {
        const struct load_command *command = (const struct load_command *)cursor;
        if (command->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *segment = (const struct segment_command_64 *)command;
            if (strncmp(segment->segname, "__DATA", 6) == 0 && segment->vmsize
                && segment->vmaddr <= UINT64_MAX - slide) {
                uint64_t size = segment->vmsize;
                if (size > TCNK_MAX_DATA_SEGMENT_SIZE) {
                    size = TCNK_MAX_DATA_SEGMENT_SIZE;
                }
                int status = tcnk_scan_runtime_region(segment->vmaddr + slide, size, listSlotOut);
                if (status == 0)
                    return 0;
                if (status != ENOENT)
                    return status;
            }
        }
        cursor += command->cmdsize;
    }
    return ENOENT;
}

static int tcnk_scan_runtime_region(uint64_t address, uint64_t size, uint64_t *listSlotOut) {
    if (!address || !size || !listSlotOut)
        return EINVAL;
    uint64_t end = address <= UINT64_MAX - size ? address + size : UINT64_MAX;
    uint64_t pageSize = vm_real_kernel_page_size;
    uint64_t pageAddress = address & ~(pageSize - 1);
    uint8_t *page = malloc((size_t)pageSize);
    if (!page)
        return ENOMEM;

    int result = ENOENT;
    while (pageAddress < end) {
        if (tcnk_read_kernel(pageAddress, page, (size_t)pageSize) == 0) {
            uint64_t first = pageAddress < address ? address - pageAddress : 0;
            first = (first + 7) & ~UINT64_C(7);
            uint64_t last = end - pageAddress;
            if (last > pageSize)
                last = pageSize;

            for (uint64_t offset = first; offset + sizeof(uint64_t) * 5 <= last; offset += sizeof(uint64_t)) {
                uint64_t words[5] = {0};
                memcpy(words, page + offset, sizeof(words));
                if (!tcnk_runtime_shape_is_valid(words))
                    continue;
                result = tcnk_runtime_candidate(pageAddress + offset, listSlotOut);
                if (result == 0)
                    break;
                /* Heuristic candidates may contain arbitrary pointers. */
                result = ENOENT;
            }
        }
        if (result != ENOENT)
            break;
        if (pageAddress > UINT64_MAX - pageSize)
            break;
        pageAddress += pageSize;
    }

    free(page);
    return result;
}

static int tcnk_map_signed_sources(tcnk_kernel *kernel) {
    static const tcnk_source_spec sourceSpecs[] = {
        {
            .path = TCNK_APP_SOURCE_PATH,
            .sourceKind = TCNM_SOURCE_APP,
        },
        {
            .path = TCNK_OS_SOURCE_PATH,
            .sourceKind = TCNM_SOURCE_OS,
        },
    };
    int lastStatus = ENOENT;

    for (size_t index = 0; index < sizeof(sourceSpecs) / sizeof(sourceSpecs[0]); index++) {
        const uint8_t *bytes = NULL;
        size_t size = 0;
        int status = tcnk_map_source(sourceSpecs[index].path, &bytes, &size);
        if (status != 0) {
            lastStatus = status;
            continue;
        }
        kernel->signedSources[kernel->signedSourceCount++] = (tcnc_signed_source){
            .sourceKind = sourceSpecs[index].sourceKind,
            .bytes = bytes,
            .size = size,
        };
    }
    return kernel->signedSourceCount ? 0 : lastStatus;
}

static int tcnk_map_source(const char *path, const uint8_t **bytesOut, size_t *sizeOut) {
    if (!path || !bytesOut || !sizeOut)
        return EINVAL;
    *bytesOut = NULL;
    *sizeOut = 0;

    int descriptor = open(path, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0)
        return errno;

    struct stat attributes = {0};
    if (fstat(descriptor, &attributes) != 0) {
        int status = errno;
        close(descriptor);
        return status;
    }
    if (!S_ISREG(attributes.st_mode) || attributes.st_size < (off_t)TCNM_FILE_HEADER_SIZE
        || (uint64_t)attributes.st_size > SIZE_MAX) {
        close(descriptor);
        return EINVAL;
    }

    size_t size = (size_t)attributes.st_size;
    void *mapping = mmap(NULL, size, PROT_READ, MAP_PRIVATE, descriptor, 0);
    int mapStatus = mapping == MAP_FAILED ? errno : 0;
    close(descriptor);
    if (mapStatus != 0)
        return mapStatus;

    *bytesOut = mapping;
    *sizeOut = size;
    return 0;
}

static int tcnk_backend_read(void *context, uint64_t address, void *output, size_t size) {
    if (!context)
        return EINVAL;
    return tcnk_read_kernel(address, output, size);
}

static int tcnk_backend_protected_replace(void *context,
                                          uint64_t address,
                                          const void *expected,
                                          const void *desired,
                                          size_t size) {
    if (!context || !address || !expected || !desired) {
        return EINVAL;
    }
    if (size != sizeof(uint32_t) || (address & (sizeof(uint32_t) - 1)) != 0) {
        return EINVAL;
    }

    uint32_t expectedWord = 0;
    uint32_t desiredWord = 0;
    uint32_t observedWord = 0;
    memcpy(&expectedWord, expected, sizeof(expectedWord));
    memcpy(&desiredWord, desired, sizeof(desiredWord));
    int status = tcn_word32_replace(address, expectedWord, desiredWord, &observedWord);
    if (status == 0)
        return 0;
    if (status == EAGAIN)
        return EAGAIN;
    if (status == EINPROGRESS) {
        if (observedWord == expectedWord || observedWord == desiredWord) {
            return EINPROGRESS;
        }
        return EIO;
    }
    return status;
}

static int tcnk_backend_reload_signed_source(void *context, uint8_t sourceKind) {
    tcnk_kernel *kernel = context;
    if (!kernel)
        return EINVAL;
    if (sourceKind != TCNM_SOURCE_APP && sourceKind != TCNM_SOURCE_OS) {
        return EINVAL;
    }
    if (!kernel->reload)
        return ENOTSUP;
    return tcnk_status(kernel->reload(kernel->reloadContext, sourceKind));
}
