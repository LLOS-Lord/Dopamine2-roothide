#include "trustcache_nokcall_word32.h"

#include <errno.h>
#include <libkern/OSCacheControl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "info.h"
#include "primitives.h"
#include "translation.h"

#ifndef DEBUG
#define DEBUG 0
#endif

/*
 * Keep the commit instruction separately auditable. The host-side gate
 * verifies that this leaf contains exactly one 32-bit store and no call to
 * memcpy. It intentionally has hidden linkage rather than being public API.
 */
__attribute__((noinline, used, visibility("hidden"))) void tcn_word32_store_leaf(volatile uint32_t *target,
                                                                                 uint32_t value) {
    *target = value;
}

static int tcn_word32_status(int status) {
    return status == 0 ? 0 : (status > 0 && status <= ELAST ? status : EIO);
}

static int tcn_word32_validate_page_range(uint64_t address, uint64_t pageSize) {
    if (pageSize < sizeof(uint32_t) || (pageSize & (pageSize - 1)) != 0) {
        return ENOTSUP;
    }
    if (address > UINT64_MAX - (sizeof(uint32_t) - 1)) {
        return EOVERFLOW;
    }
    uint64_t pageOffset = address & (pageSize - 1);
    return pageOffset <= pageSize - sizeof(uint32_t) ? 0 : EINVAL;
}

static int tcn_word32_translate(uint64_t address, uint64_t pageSize, uint64_t *physicalAddressOut) {
    if (!physicalAddressOut)
        return EINVAL;
    *physicalAddressOut = 0;

    int status = tcn_word32_validate_page_range(address, pageSize);
    if (status != 0)
        return status;

    errno = 0;
    uint64_t physicalAddress = kvtophys(address);
    if (!physicalAddress)
        return errno ? errno : ENXIO;
    if ((physicalAddress & (sizeof(uint32_t) - 1)) != 0)
        return EFAULT;

    status = tcn_word32_validate_page_range(physicalAddress, pageSize);
    if (status != 0)
        return status;
    *physicalAddressOut = physicalAddress;
    return 0;
}

static int tcn_word32_read_physical(uint64_t physicalAddress, uint32_t *valueOut) {
    if (!valueOut || !gPrimitives.physreadbuf)
        return ENOTSUP;
    return tcn_word32_status(gPrimitives.physreadbuf(physicalAddress, valueOut, sizeof(*valueOut)));
}

static int tcn_word32_write_pte(uint64_t physicalAddress, uint32_t expected, uint32_t desired) {
    if (!gPrimitives.physaccess_mapped)
        return ENOTSUP;

    __block bool callbackRan = false;
    __block int callbackStatus = 0;
    int status = gPrimitives.physaccess_mapped(physicalAddress, sizeof(uint32_t), ^(void *mappedAddress) {
        callbackRan = true;
        volatile uint32_t *mappedWord = mappedAddress;
        uint32_t mappedValue = *mappedWord;
        if (mappedValue != expected) {
            callbackStatus = EAGAIN;
            return;
        }

        __asm__ volatile("dmb ish" ::: "memory");
        tcn_word32_store_leaf(mappedWord, desired);
        __asm__ volatile("dsb ishst" ::: "memory");
        sys_dcache_flush(mappedAddress, sizeof(uint32_t));
        __asm__ volatile("dsb sy" ::: "memory");
    });
    if (status != 0)
        return tcn_word32_status(status);
    if (!callbackRan)
        return EIO;
    return callbackStatus;
}

int tcn_word32_environment_status(void) {
    if (!gPrimitives.physreadbuf || (!gPrimitives.kvtophys && !gPrimitives.vtophys)
        || (!gPrimitives.protectedKwrite32 && !gPrimitives.physaccess_mapped)) {
        return ENOTSUP;
    }
    return 0;
}

int tcn_word32_replace(uint64_t address, uint32_t expected, uint32_t desired, uint32_t *observedOut) {
    if (!address || !observedOut || (address & (sizeof(uint32_t) - 1)) != 0) {
        return EINVAL;
    }
    *observedOut = 0;
    int status = tcn_word32_environment_status();
    if (status != 0)
        return status;

    uint64_t pageSize = vm_real_kernel_page_size;
    uint64_t initialPhysicalAddress = 0;
    status = tcn_word32_translate(address, pageSize, &initialPhysicalAddress);
    if (status != 0)
        return status;

    uint32_t observed = 0;
    status = tcn_word32_read_physical(initialPhysicalAddress, &observed);
    if (status != 0)
        return status;
    *observedOut = observed;
    if (observed != expected)
        return EAGAIN;

    int writeStatus = 0;
    if (expected != desired) {
        if (gPrimitives.protectedKwrite32) {
            writeStatus = tcn_word32_status(gPrimitives.protectedKwrite32(address, desired));
            __asm__ volatile("dsb sy" ::: "memory");
        } else {
            writeStatus = tcn_word32_write_pte(initialPhysicalAddress, expected, desired);
        }
    }

    uint64_t finalPhysicalAddress = 0;
    status = tcn_word32_translate(address, pageSize, &finalPhysicalAddress);
    if (status != 0)
        return status;

    status = tcn_word32_read_physical(finalPhysicalAddress, &observed);
    if (status != 0)
        return status;
    *observedOut = observed;

    if (finalPhysicalAddress != initialPhysicalAddress)
        return ESTALE;
    if (observed != expected && observed != desired)
        return EIO;
    if (writeStatus == 0 && observed == desired)
        return 0;
    if (writeStatus == EAGAIN)
        return EAGAIN;
#if DEBUG
    fprintf(
        stderr,
        "[trustcache_nokcall] phase=word32.ambiguous status=%d " "address=0x%llx physical=0x%llx expected=0x%x " "desired=0x%x observed=0x%x protected=%u mapped=%u\n",
        writeStatus,
        address,
        initialPhysicalAddress,
        expected,
        desired,
        observed,
        gPrimitives.protectedKwrite32 != NULL,
        gPrimitives.physaccess_mapped != NULL);
#endif
    return EINPROGRESS;
}
