#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <choma/MachO.h>
#include <choma/Fat.h>
#include <choma/MemoryStream.h>
#include <choma/FileStream.h>
#include <choma/CSBlob.h>
#include <choma/CodeDirectory.h>
#include <choma/Util.h>
#include <choma/Host.h>
#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <libkern/OSByteOrder.h>
#include "signatures.h"
#include "trustcache.h"
#include "util.h"
#include "kernel.h"
#include "primitives.h"
#include "codesign.h"

#include "roothider.h"

extern CS_DecodedBlob *csd_superblob_find_best_code_directory(CS_DecodedSuperBlob *decodedSuperblob);

bool macho_is_mappable(MachO *macho)
{
	// Determine if there is any case in which the macho could be mapped

	struct mach_header *header = macho_get_mach_header(macho);

	cpu_type_t cputype = header->cputype;
	cpu_subtype_t cpusubtype = header->cpusubtype;
	bool isLibrary = (header->filetype == MH_EXECUTE);

	if (cputype != CPU_TYPE_ARM64) return false;

	if (host_is_arm64e()) {
		if (cpusubtype == (CPU_SUBTYPE_ARM64E | CPU_SUBTYPE_ARM64E_ABI_V2)) {
			// New arm64e ABI always mappable on arm64e.
			return true;
		}
		else if (cpusubtype == CPU_SUBTYPE_ARM64E && isLibrary) {
			// Old arm64e ABI only mappable for libraries on arm64e iOS 14.6+.
			return true;
		}
	}

	// Anything arm64 is always mappable on all dvices
	if ((cpusubtype == CPU_SUBTYPE_ARM64_V8) || (cpusubtype == CPU_SUBTYPE_ARM64_ALL)) return true;

	return false;
}

bool csd_superblob_is_adhoc_signed(CS_DecodedSuperBlob *superblob)
{
	CS_DecodedBlob *wrapperBlob = csd_superblob_find_blob(superblob, CSSLOT_SIGNATURESLOT, NULL);
	if (wrapperBlob) {
		if (csd_blob_get_size(wrapperBlob) > 8) {
			return false;
		}
	}
	return true;
}

bool code_signature_calculate_adhoc_cdhash(CS_SuperBlob *superblob, cdhash_t cdhashOut)
{
	bool isAdhocSigned = false;

	CS_DecodedSuperBlob *decodedSuperblob = csd_superblob_decode(superblob);
	if (decodedSuperblob) {
		if (csd_superblob_is_adhoc_signed(decodedSuperblob)) {
			if (csd_superblob_calculate_best_cdhash(decodedSuperblob, cdhashOut, NULL) == 0) {
				isAdhocSigned = true;
			}
		}
		csd_superblob_free(decodedSuperblob);
	}

	return isAdhocSigned;
}

bool macho_parse_code_signature(MachO *macho, cdhash_t cdhashOut)
{
	bool isAdhocSigned = false;

	CS_SuperBlob *superblob = macho_read_code_signature(macho);
	if (superblob) {
		isAdhocSigned = code_signature_calculate_adhoc_cdhash(superblob, cdhashOut);
		free(superblob);
	}

	return isAdhocSigned;
}

void file_collect_untrusted_cdhashes(int fd, cdhash_t **cdhashesOut, uint32_t *cdhashCountOut)
{
/*************************************** roothide specfic *************************************/
static char __thread filepath[PATH_MAX] = {0};
if(fcntl(fd, F_GETPATH, filepath) != 0) {
	JBLogError("Failed to get file path for fd %d", fd);
	return;
}
if(string_has_prefix(filepath, "/private/preboot/Cryptexes/")) {
	JBLogDebug("Skipping Cryptexes file: %s", filepath);
	return;
}
if(isRemovableBundlePath(filepath) && !hasTrollstoreLiteMarker(filepath)) {
	// ignore adhoc signed apps(removable system apps or other stuffs) which is not installed via tslite
	JBLogDebug("ignoring addhoc signed app: %s\n", filepath);
	return;
}
/*************************************** roothide specfic *************************************/


	MemoryStream *s = file_stream_init_from_file_descriptor(fd, 0, FILE_STREAM_SIZE_AUTO, 0);
	if (!s) return;

	Fat *fat = fat_init_from_memory_stream(s);
	if (!fat) {
		memory_stream_free(s);
		return;
	}

	__block cdhash_t *cdhashes = NULL;
	__block uint32_t cdhashCount = 0;
	fat_enumerate_slices(fat, ^(MachO *macho, bool *stop) {
		if (macho_is_mappable(macho)) {
			cdhash_t cdhash;
			if (macho_parse_code_signature(macho, cdhash)) {
				if (!is_cdhash_trustcached(cdhash)) {


/*************************************** roothide specfic *************************************/
if(ensure_randomized_cdhash_for_slice(filepath, macho->archDescriptor.offset, cdhash) != 0) {
	JBLogError("Failed to ensure randomized cdhash for %s", filepath);
	return;
}
/**************************************** roothide specfic *************************************/


					cdhashCount++;
					cdhashes = realloc(cdhashes, cdhashCount * sizeof(cdhash_t));
					memcpy(cdhashes[cdhashCount-1], cdhash, sizeof(cdhash));
				}
			}
		}
	});

	fat_free(fat);

	*cdhashesOut = cdhashes;
	*cdhashCountOut = cdhashCount;
}

void file_collect_untrusted_cdhashes_by_path(const char *path, cdhash_t **cdhashesOut, uint32_t *cdhashCountOut)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) return;
	file_collect_untrusted_cdhashes(fd, cdhashesOut, cdhashCountOut);
	close(fd);
}

void fat_collect_untrusted_cdhashes(Fat *fat, cdhash_t **cdhashesOut, uint32_t *cdhashCountOut)
{
	if (!fat) return;
	__block cdhash_t *cdhashes = NULL;
	__block uint32_t cdhashCount = 0;
	fat_enumerate_slices(fat, ^(MachO *macho, bool *stop) {
		if (!macho_is_mappable(macho)) return;
		cdhash_t cdhash;
		if (macho_parse_code_signature(macho, cdhash) && !is_cdhash_trustcached(cdhash)) {
			cdhash_t *newHashes = realloc(cdhashes, (cdhashCount + 1) * sizeof(cdhash_t));
			if (!newHashes) return;
			cdhashes = newHashes;
			memcpy(cdhashes[cdhashCount++], cdhash, sizeof(cdhash_t));
		}
	});
	if (cdhashesOut) *cdhashesOut = cdhashes;
	if (cdhashCountOut) *cdhashCountOut = cdhashCount;
}

void fat_collect_signatures(Fat *fat, struct siginfo **sigInfosOut, uint32_t *sigInfoCountOut)
{
	if (!fat) return;
	__block struct siginfo *sigInfos = NULL;
	__block uint32_t sigInfoCount = 0;
	fat_enumerate_slices(fat, ^(MachO *macho, bool *stop) {
		if (!macho_is_mappable(macho)) return;
		CS_SuperBlob *superblob = macho_read_code_signature(macho);
		if (!superblob) return;
		struct siginfo *newInfos = realloc(sigInfos, (sigInfoCount + 1) * sizeof(struct siginfo));
		if (!newInfos) {
			free(superblob);
			return;
		}
		sigInfos = newInfos;
		struct siginfo *cur = &sigInfos[sigInfoCount++];
		cur->source = SIGNATURE_SOURCE_ALLOCATION;
		cur->signature.fs_file_start = macho->archDescriptor.offset;
		cur->signature.fs_blob_start = superblob;
		cur->signature.fs_blob_size = OSSwapBigToHostInt32(superblob->length);
	});
	if (sigInfosOut) *sigInfosOut = sigInfos;
	if (sigInfoCountOut) *sigInfoCountOut = sigInfoCount;
}

void file_collect_signatures(int fd, struct siginfo **sigInfosOut, uint32_t *sigInfoCountOut)
{
	MemoryStream *s = file_stream_init_from_file_descriptor(fd, 0, FILE_STREAM_SIZE_AUTO, 0);
	if (!s) return;
	Fat *fat = fat_init_from_memory_stream(s);
	if (!fat) {
		memory_stream_free(s);
		return;
	}
	fat_collect_signatures(fat, sigInfosOut, sigInfoCountOut);
	fat_free(fat);
}

CS_SuperBlob *siginfo_resolve_superblob(struct siginfo *siginfo, int pid, int fd)
{
	if (!siginfo || siginfo->signature.fs_blob_size == 0) return NULL;
	size_t superblobSize = siginfo->signature.fs_blob_size;
	CS_SuperBlob *superblob = malloc(superblobSize);
	if (!superblob) return NULL;
	bool success = false;

	switch (siginfo->source) {
		case SIGNATURE_SOURCE_ALLOCATION:
			memcpy(superblob, siginfo->signature.fs_blob_start, superblobSize);
			success = true;
			break;
		case SIGNATURE_SOURCE_FILE: {
			uintptr_t start = siginfo->signature.fs_file_start + (uintptr_t)siginfo->signature.fs_blob_start;
			uintptr_t end = start + superblobSize;
			struct stat st = { 0 };
			if (fstat(fd, &st) != 0 || end > (uintptr_t)st.st_size) break;
			if (lseek(fd, (off_t)start, SEEK_SET) != (off_t)start) break;
			if (read(fd, superblob, superblobSize) != (ssize_t)superblobSize) break;
			success = true;
			break;
		}
		case SIGNATURE_SOURCE_PROC: {
			uint64_t proc = proc_find(pid);
			if (!proc || proc_vreadbuf(proc, siginfo->signature.fs_blob_start, superblob, superblobSize) != 0) break;
			success = true;
			break;
		}
		default:
			break;
	}
	if (!success) {
		free(superblob);
		return NULL;
	}
	return superblob;
}

int trust_signatures(int pid, int fd, struct siginfo *sigInfos, uint32_t sigInfoCount)
{
	if (!sigInfos || sigInfoCount == 0) return 0;
	cdhash_t *cdhashes = calloc(sigInfoCount, sizeof(cdhash_t));
	struct siginfo **toAttach = calloc(sigInfoCount, sizeof(struct siginfo *));
	if (!cdhashes || !toAttach) {
		free(cdhashes);
		free(toAttach);
		return -2;
	}
	uint32_t cdhashCount = 0;
	uint32_t attachCount = 0;
	int result = 0;

	for (uint32_t i = 0; i < sigInfoCount; i++) {
		struct siginfo *cur = &sigInfos[i];
		CS_SuperBlob *superblob = siginfo_resolve_superblob(cur, pid, fd);
		if (!superblob) continue;
		CS_DecodedSuperBlob *decoded = csd_superblob_decode(superblob);
		free(superblob);
		if (!decoded) continue;
		if (!csd_superblob_is_adhoc_signed(decoded)) {
			csd_superblob_free(decoded);
			continue;
		}

		CS_DecodedBlob *best = csd_superblob_find_best_code_directory(decoded);
		if (best) {
			if (ksymbol(SPTMArgs)) {
				uint32_t flags = csd_code_directory_get_flags(best);
				char *teamId = csd_code_directory_copy_team_id(best, NULL);
				bool hasTeamId = (teamId != NULL);
				free(teamId);
				if (!!(flags & CS_ADHOC) == hasTeamId) {
					if (cur->source != SIGNATURE_SOURCE_ALLOCATION) {
						csd_superblob_free(decoded);
						free(cdhashes);
						free(toAttach);
						return -1;
					}
					csd_code_directory_set_flags(best, hasTeamId ? (flags & ~CS_ADHOC) : (flags | CS_ADHOC));
					free(cur->signature.fs_blob_start);
					CS_SuperBlob *updated = csd_superblob_encode(decoded);
					if (!updated) {
						csd_superblob_free(decoded);
						continue;
					}
					cur->signature.fs_blob_start = updated;
					cur->signature.fs_blob_size = OSSwapBigToHostInt32(updated->length);
					toAttach[attachCount++] = cur;
				}
			}
			cdhash_t cdhash;
			csd_code_directory_calculate_hash(best, &cdhash);
			if (!is_cdhash_trustcached(cdhash)) memcpy(cdhashes[cdhashCount++], cdhash, sizeof(cdhash_t));
		}
		csd_superblob_free(decoded);
	}

	if (cdhashCount > 0) jb_trustcache_add_cdhashes(cdhashes, cdhashCount);
	for (uint32_t i = 0; i < attachCount; i++) {
		int r = fd_attach_signature(fd, &toAttach[i]->signature);
		if (r != 0) result = r;
	}
	free(toAttach);
	free(cdhashes);
	return result;
}
