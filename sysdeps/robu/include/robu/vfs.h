#ifndef _ROBU_VFS_H
#define _ROBU_VFS_H

#include <stdint.h>

#define ROBU_VFS_ABI_MAJOR 1
#define ROBU_VFS_ABI_MINOR 2

#define ROBU_VFS_FEATURE_OPEN      (1ULL << 0)
#define ROBU_VFS_FEATURE_READ      (1ULL << 1)
#define ROBU_VFS_FEATURE_WRITE     (1ULL << 2)
#define ROBU_VFS_FEATURE_STAT      (1ULL << 3)
#define ROBU_VFS_FEATURE_READDIR   (1ULL << 4)
#define ROBU_VFS_FEATURE_MUTATE    (1ULL << 5)
#define ROBU_VFS_FEATURE_SYMLINK   (1ULL << 6)
#define ROBU_VFS_FEATURE_XATTR     (1ULL << 7)
#define ROBU_VFS_FEATURE_LINUX_VFS (1ULL << 8)
#define ROBU_VFS_FEATURE_TIMESTAMPS (1ULL << 9)

#ifdef __cplusplus
extern "C" {
#endif

int robu_vfs_endpoint_capabilities(uint32_t endpoint, uint64_t *abi,
        uint64_t *features);
int robu_vfs_root_capabilities(uint64_t *abi, uint64_t *features);

#ifdef __cplusplus
}
#endif

#endif
