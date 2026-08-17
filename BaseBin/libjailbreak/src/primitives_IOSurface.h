#ifndef PRIMITIVES_IOSURFACE_H
#define PRIMITIVES_IOSURFACE_H

#include <stdint.h>

int IOSurface_map(uint64_t phys, uint64_t size, void **uaddr);
int IOSurface_map_withCacheMode(uint64_t phys, uint64_t size, void **uaddr, uint32_t cacheMode);
void IOSurface_map_cleanup(void);
uint64_t IOSurface_kalloc(uint64_t size, bool leak);
int IOSurface_kalloc_global(uint64_t *addr, uint64_t size);
int IOSurface_kalloc_local(uint64_t *addr, uint64_t size);
void libjailbreak_IOSurface_primitives_init(void);

#endif
