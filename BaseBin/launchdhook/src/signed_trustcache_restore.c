#include "signed_trustcache_restore.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libjailbreak/info.h>
#include <libjailbreak/primitives.h>
#include <libjailbreak/log.h>

#define SIGNED_TRUSTCACHE_DIRECTORY \
	"/private/preboot/cryptex1/current"
#define SIGNED_TRUSTCACHE_OS_BASENAME "os.dmg.trustcache"
#define SIGNED_TRUSTCACHE_APP_BASENAME "app.dmg.trustcache"
#define SIGNED_TRUSTCACHE_TYPE_BOOT_OS UINT8_C(13)
#define SIGNED_TRUSTCACHE_TYPE_BOOT_APP UINT8_C(14)
#define SIGNED_TRUSTCACHE_AMFI_LOAD_CALL 0x65

extern int amfi_load_trust_cache(uint8_t type,
                                 const void *object,
                                 uint32_t objectLength,
                                 const void *manifest,
                                 uint32_t manifestLength,
                                 const void *auxManifest,
                                 uint32_t auxManifestLength) __attribute__((weak_import));

extern int __mac_syscall(const char *policyName, int call, void *argument);

typedef struct {
    uint8_t type;
    const void *object;
    uint32_t objectLength;
    const void *manifest;
    uint32_t manifestLength;
    const void *auxManifest;
    uint32_t auxManifestLength;
} signed_trustcache_load_arguments;

_Static_assert(sizeof(signed_trustcache_load_arguments) == 56, "unexpected AMFI trustcache load argument layout");

typedef struct {
    void *address;
    size_t size;
} signed_trustcache_mapping;

typedef struct {
    const char *basename;
    uint8_t type;
    uint64_t loadedFlagAddress;
} signed_trustcache_source;

static int signed_trustcache_load(uint8_t type,
                                  const void *object,
                                  uint32_t objectLength,
                                  const void *manifest,
                                  uint32_t manifestLength,
                                  const void *auxManifest,
                                  uint32_t auxManifestLength) {
    if (amfi_load_trust_cache) {
        return amfi_load_trust_cache(type,
                                     object,
                                     objectLength,
                                     manifest,
                                     manifestLength,
                                     auxManifest,
                                     auxManifestLength);
    }

    // iOS 16.5 keeps amfi_load_trust_cache as a local dyld symbol.
    // Reproduce its thin wrapper around the AMFI MAC syscall.
    signed_trustcache_load_arguments arguments;
    memset(&arguments, 0xAA, sizeof(arguments));
    arguments.type = type;
    arguments.object = object;
    arguments.objectLength = objectLength;
    arguments.manifest = manifest;
    arguments.manifestLength = manifestLength;
    arguments.auxManifest = auxManifest;
    arguments.auxManifestLength = auxManifestLength;
    return __mac_syscall("AMFI", SIGNED_TRUSTCACHE_AMFI_LOAD_CALL, &arguments);
}

static bool signed_trustcache_manifest_name(const char *name) {
    static const char prefix[] = "apticket.";
    static const char suffix[] = ".im4m";
    if (!name)
        return false;
    size_t length = strlen(name);
    return length > sizeof(prefix) - 1 + sizeof(suffix) - 1 && strncmp(name, prefix, sizeof(prefix) - 1) == 0
        && strcmp(name + length - (sizeof(suffix) - 1), suffix) == 0;
}

static int signed_trustcache_map_file(int directoryFD, const char *name, signed_trustcache_mapping *mapping) {
    *mapping = (signed_trustcache_mapping){0};
    int fileFD = openat(directoryFD, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fileFD < 0)
        return errno ? errno : EIO;

    struct stat st = {0};
    int status = fstat(fileFD, &st) == 0 ? 0 : (errno ? errno : EIO);
    if (status == 0 && (!S_ISREG(st.st_mode) || st.st_size <= 0 || (uint64_t)st.st_size > UINT32_MAX)) {
        status = EINVAL;
    }
    if (status == 0) {
        void *address = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fileFD, 0);
        if (address == MAP_FAILED) {
            status = errno ? errno : EIO;
        } else {
            mapping->address = address;
            mapping->size = (size_t)st.st_size;
        }
    }
    close(fileFD);
    return status;
}

static void signed_trustcache_unmap(signed_trustcache_mapping *mapping) {
    if (mapping->address && mapping->size) {
        munmap(mapping->address, mapping->size);
    }
    *mapping = (signed_trustcache_mapping){0};
}

static int signed_trustcache_find_manifest(int directoryFD, char manifestName[NAME_MAX + 1]) {
    int enumerationFD = fcntl(directoryFD, F_DUPFD_CLOEXEC, 0);
    if (enumerationFD < 0)
        return errno ? errno : EIO;
    DIR *directory = fdopendir(enumerationFD);
    if (!directory) {
        int status = errno ? errno : EIO;
        close(enumerationFD);
        return status;
    }

    int status = ENOENT;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(directory);
        if (!entry) {
            if (errno != 0)
                status = errno;
            break;
        }
        if (!signed_trustcache_manifest_name(entry->d_name))
            continue;
        if (status == 0) {
            status = EEXIST;
            break;
        }
        strlcpy(manifestName, entry->d_name, NAME_MAX + 1);
        status = 0;
    }
    closedir(directory);
    return status;
}

static int signed_trustcache_restore_source(int directoryFD,
                                            const char *manifestName,
                                            const signed_trustcache_source *source) {
    signed_trustcache_mapping object = {0};
    signed_trustcache_mapping manifest = {0};
    int status = signed_trustcache_map_file(directoryFD, source->basename, &object);
    if (status == 0) {
        status = signed_trustcache_map_file(directoryFD, manifestName, &manifest);
    }
    if (status != 0)
        goto out;

    uint8_t originalFlag = UINT8_MAX;
    uint8_t observedFlag = UINT8_MAX;
    uint8_t zero = 0;
    status = kreadbuf_protected(source->loadedFlagAddress, &originalFlag, sizeof(originalFlag));
    if (status == 0 && originalFlag != 1)
        status = EBUSY;
    if (status == 0) {
        status = kwritebuf_protected(source->loadedFlagAddress, &zero, sizeof(zero));
    }
    if (status == 0) {
        status = kreadbuf_protected(source->loadedFlagAddress, &observedFlag, sizeof(observedFlag));
        if (status == 0 && observedFlag != 0)
            status = EIO;
    }

    int rawStatus = -1;
    int savedErrno = 0;
    if (status == 0) {
        errno = 0;
        rawStatus = signed_trustcache_load(source->type,
                                           object.address,
                                           (uint32_t)object.size,
                                           manifest.address,
                                           (uint32_t)manifest.size,
                                           NULL,
                                           0);
        savedErrno = errno;
        status = rawStatus == 0 ? 0 : (savedErrno ? savedErrno : EIO);
    }
    if (status == 0) {
        status = kreadbuf_protected(source->loadedFlagAddress, &observedFlag, sizeof(observedFlag));
        if (status == 0 && observedFlag != 1)
            status = EPROTO;
    }
    if (status != 0 && originalFlag == 1) {
        int restoreStatus = kwritebuf_protected(source->loadedFlagAddress, &originalFlag, sizeof(originalFlag));
        if (restoreStatus != 0)
            status = restoreStatus;
    }

    if (status != 0) {
        JBLogError("signed trustcache restore failed file=%s type=%u gate=0x%llx "
                   "original=%u observed=%u raw=%d errno=%d status=%d",
                   source->basename,
                   source->type,
                   source->loadedFlagAddress,
                   originalFlag,
                   observedFlag,
                   rawStatus,
                   savedErrno,
                   status);
    } else {
        JBLogDebug("signed trustcache restore status=complete file=%s type=%u", source->basename, source->type);
    }

out:
    signed_trustcache_unmap(&manifest);
    signed_trustcache_unmap(&object);
    return status;
}

static int signed_trustcache_open_directory(int *directoryFDOut, char manifestName[NAME_MAX + 1]) {
    if (!directoryFDOut || !manifestName)
        return EINVAL;
    *directoryFDOut = -1;
    int directoryFD = open(SIGNED_TRUSTCACHE_DIRECTORY, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directoryFD < 0)
        return errno ? errno : EIO;
    int status = signed_trustcache_find_manifest(directoryFD, manifestName);
    if (status != 0) {
        close(directoryFD);
        return status;
    }
    *directoryFDOut = directoryFD;
    return 0;
}

static int signed_trustcache_resolve_source(uint8_t sourceType, signed_trustcache_source *sourceOut) {
    if (!sourceOut)
        return EINVAL;
    switch (sourceType) {
        case SIGNED_TRUSTCACHE_TYPE_BOOT_OS:
            *sourceOut = (signed_trustcache_source){
                .basename = SIGNED_TRUSTCACHE_OS_BASENAME,
                .type = SIGNED_TRUSTCACHE_TYPE_BOOT_OS,
                .loadedFlagAddress = ksymbol(boot_os_tc_loaded),
            };
            break;
        case SIGNED_TRUSTCACHE_TYPE_BOOT_APP:
            *sourceOut = (signed_trustcache_source){
                .basename = SIGNED_TRUSTCACHE_APP_BASENAME,
                .type = SIGNED_TRUSTCACHE_TYPE_BOOT_APP,
                .loadedFlagAddress = ksymbol(boot_app_tc_loaded),
            };
            break;
        default:
            return EINVAL;
    }
    return sourceOut->loadedFlagAddress ? 0 : ENOENT;
}

int launchd_reload_boot_trustcache(uint8_t sourceType) {
    signed_trustcache_source source = {0};
    int status = signed_trustcache_resolve_source(sourceType, &source);
    if (status != 0)
        return status;

    int directoryFD = -1;
    char manifestName[NAME_MAX + 1] = {0};
    status = signed_trustcache_open_directory(&directoryFD, manifestName);
    if (status == 0)
        status = signed_trustcache_restore_source(directoryFD, manifestName, &source);
    if (directoryFD >= 0)
        close(directoryFD);
    if (status != 0)
        JBLogError("signed trustcache reload failed type=%u status=%d", sourceType, status);
    else
        JBLogDebug("signed trustcache reload status=complete type=%u", sourceType);
    return status;
}

int launchd_restore_boot_trustcaches(void) {
    int directoryFD = -1;
    char manifestName[NAME_MAX + 1] = {0};
    int status = signed_trustcache_open_directory(&directoryFD, manifestName);
    if (status != 0)
        return status;

    const uint8_t sourceTypes[] = {
        SIGNED_TRUSTCACHE_TYPE_BOOT_OS,
        SIGNED_TRUSTCACHE_TYPE_BOOT_APP,
    };
    int firstError = 0;
    for (size_t index = 0; index < sizeof(sourceTypes) / sizeof(sourceTypes[0]); index++) {
        signed_trustcache_source source = {0};
        int sourceStatus = signed_trustcache_resolve_source(sourceTypes[index], &source);
        if (sourceStatus == 0) {
            sourceStatus = signed_trustcache_restore_source(directoryFD, manifestName, &source);
        }
        if (firstError == 0 && sourceStatus != 0) {
            firstError = sourceStatus;
        }
    }
    close(directoryFD);
    return firstError;
}
