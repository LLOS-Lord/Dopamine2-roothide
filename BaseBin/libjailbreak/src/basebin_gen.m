#import "libjailbreak.h"
#import "carboncopy.h"
#import "codesign.h"
#import <Foundation/Foundation.h>
#import <sys/sysctl.h>
#import <errno.h>
#import <string.h>

int apply_dyld_patch(NSString *dyldPath, const char *newUUIDPrefix)
{
        MachO *dyldMacho = macho_init_for_writing(dyldPath.fileSystemRepresentation);
        if (!dyldMacho) return -1;

        __block int r = 0;

        // Make AMFI flags always be `0xff`, allows DYLD_* variables to always work
        __block uint64_t getAMFIAddr = 0;
        macho_enumerate_symbols(dyldMacho, ^(const char *name, uint8_t type, uint64_t vmaddr, bool *stop){
                if (!strcmp(name, "__ZN5dyld413ProcessConfig8Security7getAMFIERKNS0_7ProcessERNS_15SyscallDelegateE")) {
                        getAMFIAddr = vmaddr;
                }
        });
        uint32_t getAMFIPatch[] = {
                0xd2801fe0, // mov x0, 0xff
                0xd65f03c0  // ret
        };

        if (getAMFIAddr == 0) {
        printf("Error: Failed patchfinding getAMFI\n");
        return -1;
    }

        macho_write_at_vmaddr(dyldMacho, getAMFIAddr, sizeof(getAMFIPatch), getAMFIPatch);

        // iOS 16+: Change LC_UUID to prevent the kernel from using the in-cache dyld
        macho_enumerate_load_commands(dyldMacho, ^(struct load_command loadCommand, uint64_t offset, void *cmd, bool *stop) {
                if (loadCommand.cmd == LC_UUID) {
            // The new UUID will look like this:
            // DOPA<dopamine version>\0<rest of original UUID>
            // This way we ensure:
            // - The version it was patched on and it being patched by Dopamine is identifiable later
            // - The UUID is still unique based on the source dyld that was patched

            size_t newUUIDPrefixLen = strlen(newUUIDPrefix) + 1;
            if (newUUIDPrefixLen <= sizeof(uuid_t)) {
                // Also write null byte here, because otherwise it's impossible to know where the version string ends
                macho_write_at_offset(dyldMacho, offset + offsetof(struct uuid_command, uuid), newUUIDPrefixLen, newUUIDPrefix);
            }
            else {
                                r = -1;
                printf("Error: Failed to write identifier to LC_UUID, too long (%zu)\n", newUUIDPrefixLen);
            }
                        *stop = true;
                }
        });

        macho_free(dyldMacho);
        return r;
}

NSString *dyldhook_dylib_for_platform(void)
{
        cpu_subtype_t cpusubtype = 0;
        size_t len = sizeof(cpusubtype);
        if (sysctlbyname("hw.cpusubtype", &cpusubtype, &len, NULL, 0) == -1) { return nil; }
        if ((cpusubtype & ~CPU_SUBTYPE_MASK) == CPU_SUBTYPE_ARM64E) {
                if (@available(iOS 16.0, *)) {
                        return @"dyldhook_merge.arm64e.dylib"; 
                }
                else {
                        return @"dyldhook_merge.arm64e.iOS15.dylib"; 
                }
        }
        else {
                if (@available(iOS 16.0, *)) {
                        return @"dyldhook_merge.arm64.dylib"; 
                }
                else {
                        return @"dyldhook_merge.arm64.iOS15.dylib"; 
                }
        }
}

int merge_dyldhook(NSString *originalDyldPath, NSString *outPath)
{
        NSString *dyldhookMergeDylibName = dyldhook_dylib_for_platform();
        if (!dyldhookMergeDylibName) {
                printf("Error: Failed to locate dyldhook.dylib\n");
                return -1;
        }

        NSString *dyldhookMergeDylibPath = [JBROOT_PATH(@"/basebin") stringByAppendingPathComponent:dyldhookMergeDylibName];
        int r = exec_cmd(JBROOT_PATH("/basebin/MachOMerger"), originalDyldPath.fileSystemRepresentation, dyldhookMergeDylibPath.fileSystemRepresentation, outPath.fileSystemRepresentation, NULL);
        if (r == 0) {
                r = chmod(outPath.fileSystemRepresentation, 0755);
        }
        return r;
}

int basebin_generate(bool comingFromJBUpdate)
{
        NSString *basebinPath    = JBROOT_PATH(@"/basebin");
        NSString *genPath        = JBROOT_PATH(@"/basebin/gen");
        NSString *fakelibPath    = JBROOT_PATH(@"/basebin/.fakelib");
        NSString *systemhookPath = JBROOT_PATH(@"/basebin/systemhook.dylib");

        // Make sure the gen/ directory exists. On iOS 17/18+, the previous basebin directory
        // can be left in a half-cleaned state after a reboot-loop wipe, so always recreate it.
        [[NSFileManager defaultManager] removeItemAtPath:genPath error:nil];
        if (![[NSFileManager defaultManager] createDirectoryAtPath:genPath withIntermediateDirectories:YES attributes:nil error:nil]) {
                printf("Error: Failed to create gen directory at %s\n", genPath.UTF8String);
                return 5;
        }

        NSString *fakelibDyldPath        = [fakelibPath stringByAppendingPathComponent:@"dyld"];
        NSString *fakelibSystemHookPath  = [fakelibPath stringByAppendingPathComponent:@"systemhook.dylib"];

        NSString *dyldOrigPath     = [genPath stringByAppendingPathComponent:@"dyld.orig"];
        NSString *dyldInflightPath = [genPath stringByAppendingPathComponent:@"dyld.inflight"];
        NSString *dyldOldPath      = [genPath stringByAppendingPathComponent:@"dyld.old"];
        NSString *dyldPatchedPath  = [genPath stringByAppendingPathComponent:@"dyld"];

        NSString *dopamineVersion = [NSString stringWithContentsOfFile:JBROOT_PATH(@"/basebin/.version") encoding:NSUTF8StringEncoding error:nil];
        if (!dopamineVersion) {
                printf("Error: Failed to read dopamine version from %s\n", JBROOT_PATH(@"/basebin/.version"));
                return 1;
        }

        if (!comingFromJBUpdate) {
                // Copy /usr/lib to /var/jb/basebin/.fakelib
                [[NSFileManager defaultManager] removeItemAtPath:fakelibPath error:nil];
                if (![[NSFileManager defaultManager] createDirectoryAtPath:fakelibPath withIntermediateDirectories:YES attributes:nil error:nil]) {
                        printf("Error: Failed to create fakelib directory at %s\n", fakelibPath.UTF8String);
                        return 6;
                }
                if (carbonCopy(@"/usr/lib", fakelibPath) != 0) {
                        printf("Error: carbonCopy /usr/lib -> %s failed\n", fakelibPath.UTF8String);
                        return 7;
                }

                // Delete the dyld inside .fakelib
                [[NSFileManager defaultManager] removeItemAtPath:fakelibDyldPath error:nil];

                // Symlink .fakelib/dyld -> /var/jb/basebin/gen/dyld
                if (![[NSFileManager defaultManager] createSymbolicLinkAtPath:fakelibDyldPath withDestinationPath:dyldPatchedPath error:nil]) {
                        printf("Error: Failed to symlink %s -> %s\n", fakelibDyldPath.UTF8String, dyldPatchedPath.UTF8String);
                        return 8;
                }

                // Symlink .fakelib/systemhook.dylib -> /var/jb/basebin/systemhook.dylib
                if (![[NSFileManager defaultManager] createSymbolicLinkAtPath:fakelibSystemHookPath withDestinationPath:systemhookPath error:nil]) {
                        printf("Error: Failed to symlink %s -> %s\n", fakelibSystemHookPath.UTF8String, systemhookPath.UTF8String);
                        return 9;
                }

                // Backup original dyld. On iOS 18+, /usr/lib/dyld is the in-cache dyld, which
                // may not be readable as a regular file (it lives inside the shared cache).
                // carbonCopy will fail in that case, so we fall back to reading the on-disk copy
                // via /rootfs/usr/lib/dyld (the roothide Procursus bootstrap ships a /rootfs -> /
                // symlink, which lets us reach the real rootfs path even when chdir()'d into jbroot).
                if (carbonCopy(@"/usr/lib/dyld", dyldOrigPath) != 0) {
                        printf("Warning: carbonCopy /usr/lib/dyld -> %s failed; trying /rootfs/usr/lib/dyld\n", dyldOrigPath.UTF8String);
                        if (carbonCopy(@"/rootfs/usr/lib/dyld", dyldOrigPath) != 0) {
                                printf("Error: Failed to back up original dyld to %s\n", dyldOrigPath.UTF8String);
                                return 10;
                        }
                }
        }
        else {
                // On JB update, dyldOrigPath should already exist from the previous basebin_generate.
                // If it doesn't (e.g., because the user wiped basebin manually after a reboot-loop),
                // re-create it from the in-cache dyld. Without this, dyld.inflight would never be
                // created and MachOMerger would crash with "Fatal error: ... dyld.inflight couldn't be opened".
                if (![[NSFileManager defaultManager] fileExistsAtPath:dyldOrigPath]) {
                        if (carbonCopy(@"/usr/lib/dyld", dyldOrigPath) != 0) {
                                if (carbonCopy(@"/rootfs/usr/lib/dyld", dyldOrigPath) != 0) {
                                        printf("Error: dyld.orig missing and could not be re-created for JB update\n");
                                        return 10;
                                }
                        }
                }
        }

        // Create dyld.inflight as a fresh copy of dyld.orig. carbonCopy silently returning
        // non-zero here was the root cause of the "dyld.inflight couldn't be opened" Swift
        // fatal error reported during finalizeBootstrap on iOS 17/18+. We must propagate the
        // failure so DOJailbreaker.m surfaces a meaningful error instead of letting
        // MachOMerger crash at the top level.
        if (carbonCopy(dyldOrigPath, dyldInflightPath) != 0) {
                printf("Error: carbonCopy %s -> %s failed (errno hints: %s)\n",
                       dyldOrigPath.UTF8String, dyldInflightPath.UTF8String, strerror(errno));
                return 11;
        }
        if (![[NSFileManager defaultManager] fileExistsAtPath:dyldInflightPath]) {
                printf("Error: dyld.inflight was not created at %s\n", dyldInflightPath.UTF8String);
                return 12;
        }

        NSString *dyldUUIDPrefix = [@"DOPA" stringByAppendingString:dopamineVersion];
        if (apply_dyld_patch(dyldInflightPath, dyldUUIDPrefix.UTF8String) != 0) return 2;
        if (merge_dyldhook(dyldInflightPath, dyldInflightPath) != 0) return 3;
        if (resign_file(dyldInflightPath, @"com.apple.dyld", YES) != 0) return 4;

        if (comingFromJBUpdate) {
                // We cannot delete dyld as this point because it's still in use
                // If we did this, we'd panic the system
                // So we will move the past patched dyld to dyld.old to keep the vnode alive
                // If there is another dyld.old at this point, we will remove it now
                // since it is guaranteed to not be in use at this point
                if ([[NSFileManager defaultManager] fileExistsAtPath:dyldOldPath]) {
                        [[NSFileManager defaultManager] removeItemAtPath:dyldOldPath error:nil];
                }
                [[NSFileManager defaultManager] moveItemAtPath:dyldPatchedPath toPath:dyldOldPath error:nil];
        }

        // Move dyld.inflight -> dyld (the final patched dyld). If a stale dyld already exists
        // (e.g., from a previous failed basebin_generate), the move will fail silently and we
        // will end up with an out-of-date patched dyld. Remove any stale dyld first.
        if ([[NSFileManager defaultManager] fileExistsAtPath:dyldPatchedPath] &&
            ![[NSFileManager defaultManager] isDeletableFileAtPath:dyldPatchedPath]) {
                printf("Warning: stale dyld at %s is not removable; patched dyld may be out of date\n", dyldPatchedPath.UTF8String);
        }
        if (!comingFromJBUpdate && [[NSFileManager defaultManager] fileExistsAtPath:dyldPatchedPath]) {
                [[NSFileManager defaultManager] removeItemAtPath:dyldPatchedPath error:nil];
        }
        if (![[NSFileManager defaultManager] moveItemAtPath:dyldInflightPath toPath:dyldPatchedPath error:nil]) {
                printf("Error: Failed to move %s -> %s\n", dyldInflightPath.UTF8String, dyldPatchedPath.UTF8String);
                return 13;
        }
        return 0;
}