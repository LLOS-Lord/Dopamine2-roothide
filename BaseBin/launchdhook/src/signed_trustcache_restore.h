#ifndef SIGNED_TRUSTCACHE_RESTORE_H
#define SIGNED_TRUSTCACHE_RESTORE_H

#include <stdint.h>

int launchd_restore_boot_trustcaches(void);
int launchd_reload_boot_trustcache(uint8_t sourceType);

#endif
