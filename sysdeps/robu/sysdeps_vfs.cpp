#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <robu/vfs.h>
#include <mlibc/all-sysdeps.hpp>
#include <mlibc/fsfd_target.hpp>

#include "robu-abi.hpp"
#include "sysdeps_internal.hpp"

using namespace robu_detail;

extern "C" int robu_vfs_endpoint_capabilities(uint32_t endpoint, uint64_t *abi,
		uint64_t *features) {
	if(!abi || !features)
		return EINVAL;
	int64_t status = robu::vfs_caps(endpoint, abi, features);
	if(status == robu::IPC_ERR_NONE)
		return 0;
	return status == robu::VFS_ERR_NOT_SUPPORTED ? ENOTSUP : EIO;
}

extern "C" int robu_vfs_root_capabilities(uint64_t *abi, uint64_t *features) {
	int matched_len = 0;
	uint32_t endpoint = robu::resolve_mount("/", &matched_len);
	if(endpoint == 0 || matched_len != 1)
		return ENOENT;
	return robu_vfs_endpoint_capabilities(endpoint, abi, features);
}

namespace {

constexpr int CWD_MAX = 256;
char g_cwd[CWD_MAX] = "/";

void resolve_path(const char *path, char *out, size_t out_size) {
	char raw[CWD_MAX];
	if (path[0] == '/') {
		strncpy(raw, path, sizeof(raw) - 1);
		raw[sizeof(raw) - 1] = '\0';
	} else {
		size_t cwdlen = strlen(g_cwd);
		if (cwdlen >= sizeof(raw)) cwdlen = sizeof(raw) - 1;
		memcpy(raw, g_cwd, cwdlen);
		raw[cwdlen] = '\0';
		if (cwdlen > 0 && raw[cwdlen - 1] != '/') {
			strncat(raw, "/", sizeof(raw) - strlen(raw) - 1);
		}
		strncat(raw, path, sizeof(raw) - strlen(raw) - 1);
	}
	char *segs[16];
	int nseg = 0;
	char *save = nullptr;
	for (char *tok = strtok_r(raw, "/", &save); tok; tok = strtok_r(nullptr, "/", &save)) {
		if (strcmp(tok, ".") == 0) continue;
		if (strcmp(tok, "..") == 0) {
			if (nseg > 0) nseg--;
			continue;
		}
		if (nseg < (int)(sizeof(segs) / sizeof(segs[0]))) {
			segs[nseg++] = tok;
		}
	}
	size_t pos = 0;
	if (out_size > 1) out[pos++] = '/';
	out[pos] = '\0';
	for (int i = 0; i < nseg; i++) {
		size_t len = strlen(segs[i]);
		if (pos > 1) {
			if (pos + 1 >= out_size) break;
			out[pos++] = '/';
		}
		if (pos + len >= out_size) break;
		memcpy(out + pos, segs[i], len);
		pos += len;
		out[pos] = '\0';
	}
}

uint32_t resolve_mount_for_dir(const char *resolved, const char **rel_out) {
	size_t rlen = strlen(resolved);
	char match_buf[CWD_MAX];
	if (rlen + 2 > sizeof(match_buf)) {
		*rel_out = resolved + rlen;
		return 0;
	}
	memcpy(match_buf, resolved, rlen);
	match_buf[rlen] = '/';
	match_buf[rlen + 1] = '\0';
	int matched_len = 0;
	uint32_t tid = robu::resolve_mount(match_buf, &matched_len);
	*rel_out = ((size_t)matched_len <= rlen) ? resolved + matched_len : resolved + rlen;
	return tid;
}

void fill_stat(struct stat *buf, uint64_t size, int node_type, dev_t dev, ino_t ino,
		time_t atime = 0, time_t mtime = 0) {
	memset(buf, 0, sizeof(*buf));
	buf->st_mode = node_type == robu::VFS_NODE_DIR ? (S_IFDIR | 0755) : (S_IFREG | 0644);
	buf->st_size = (off_t)size;
	buf->st_nlink = 1;
	buf->st_blksize = 512;
	buf->st_blocks = (blkcnt_t)((size + 511) / 512);
	buf->st_dev = dev;
	buf->st_ino = ino;
	buf->st_atim.tv_sec = atime;
	buf->st_mtim.tv_sec = mtime;
	buf->st_ctim.tv_sec = mtime;
}

constexpr dev_t ROBU_STDEV_DEVFS = 1;
constexpr dev_t ROBU_STDEV_RAMFS = 2;
constexpr dev_t ROBU_STDEV_VFS = 5;

bool vfs_has_timestamps(uint32_t tid) {
	uint64_t abi = 0;
	uint64_t features = 0;
	return robu::vfs_caps(tid, &abi, &features) == 0 &&
		(features & robu::VFS_FEATURE_TIMESTAMPS) != 0;
}

int stat_path(const char *resolved, struct stat *buf) {
	const char *rel;
	uint32_t mount_tid = resolve_mount_for_dir(resolved, &rel);
	if (mount_tid != 0) {
		uint64_t size = 0;
		int is_dir = 0;
		uint64_t ino = 0;
		int64_t atime = 0;
		int64_t mtime = 0;
		int have_timestamps = vfs_has_timestamps(mount_tid);
		if (robu::vfs_stat(mount_tid, rel, &size, &is_dir, &ino,
				have_timestamps ? &atime : nullptr,
				have_timestamps ? &mtime : nullptr) != 0) {
			return ENOENT;
		}
		if (mount_tid == robu::devfs_tid()) {
			fill_stat(buf, size, is_dir, ROBU_STDEV_DEVFS, (ino_t)ino, atime, mtime);
			if (is_dir != robu::VFS_NODE_DIR) {
				if (is_dir == robu::VFS_NODE_BLOCK) {
					buf->st_mode = S_IFBLK | 0660;
					buf->st_rdev = (dev_t)ino;
				} else {
					buf->st_mode = S_IFCHR | 0666;
				}
			}
			return 0;
		}
		if (mount_tid == robu::ramfs_tid()) {
			fill_stat(buf, size, is_dir, ROBU_STDEV_RAMFS, (ino_t)ino, atime, mtime);
			return 0;
		}
		fill_stat(buf, size, is_dir, ROBU_STDEV_VFS, (ino_t)ino, atime, mtime);
		return 0;
	}
	return ENOENT;
}

int vfs_err_to_errno(int64_t status) {
	switch (status) {
	case 0: return 0;
	case robu::VFS_ERR_NOT_FOUND: return ENOENT;
	case robu::VFS_ERR_EXISTS: return EEXIST;
	case robu::VFS_ERR_IS_DIR: return EISDIR;
	case robu::VFS_ERR_NOT_DIR: return ENOTDIR;
	case robu::VFS_ERR_NOT_EMPTY: return ENOTEMPTY;
	case robu::VFS_ERR_NOT_SUPPORTED: return ENOTSUP;
	case robu::VFS_ERR_NO_SPACE: return ENOSPC;
	default: return EIO;
	}
}

int xattr_err_to_errno(int64_t status) {
	switch (status) {
	case 0: return 0;
	case robu::VFS_ERR_NOT_FOUND: return ENODATA;
	case robu::VFS_ERR_EXISTS: return EEXIST;
	case robu::VFS_ERR_NO_SPACE: return ERANGE;
	case robu::VFS_ERR_NOT_SUPPORTED: return ENOTSUP;
	case robu::VFS_ERR_INVALID: return EINVAL;
	case robu::VFS_ERR_BAD_HANDLE: return EBADF;
	default: return EIO;
	}
}

int xattr_fd(int fd, uint64_t command, const char *name, const void *input,
		size_t input_size, void *output, size_t output_size, int flags,
		ssize_t *nread) {
	if (!fd_valid(fd) || g_fds[fd].kind != FD_VFS) {
		return EBADF;
	}
	if ((input_size != 0 && !input) || (output_size != 0 && !output)) {
		return EFAULT;
	}
	uint64_t name_len = 0;
	if (command != robu::VFS_XATTR_LIST) {
		if (!name) return EFAULT;
		while (name[name_len] && name_len <= (uint64_t)robu::VFS_XATTR_NAME_MAX) name_len++;
		if (name_len == 0 || name_len > (uint64_t)robu::VFS_XATTR_NAME_MAX) return ERANGE;
	}
	if (input_size > (size_t)robu::VFS_XATTR_VALUE_MAX ||
		output_size > (size_t)robu::VFS_XATTR_VALUE_MAX) {
		return ERANGE;
	}
	uint64_t value_size = command == robu::VFS_XATTR_SET ? input_size : output_size;
	uint64_t segment_size = command == robu::VFS_XATTR_LIST ? value_size : name_len + value_size;
	if (segment_size == 0) segment_size = 1;
	int shmid = -1;
	int64_t rc = robu::shmget_raw(0, segment_size, 0, &shmid);
	if (rc != robu::IPC_ERR_NONE) return rc == robu::IPC_ERR_NO_MEM ? ENOMEM : ENOSPC;
	uint64_t va = 0;
	rc = robu::shmat_raw(shmid, 0, 0, &va);
	if (rc != robu::IPC_ERR_NONE) {
		robu::shmctl_raw(shmid, 0, nullptr);
		return EIO;
	}
	uint8_t *shared = (uint8_t *)va;
	for (uint64_t i = 0; i < name_len; i++) shared[i] = (uint8_t)name[i];
	if (command == robu::VFS_XATTR_SET) {
		const uint8_t *value = (const uint8_t *)input;
		for (uint64_t i = 0; i < input_size; i++) shared[name_len + i] = value[i];
	}
	uint64_t returned_size = 0;
	uint64_t wire_command = command | ((uint64_t)(uint32_t)flags << robu::VFS_XATTR_FLAGS_SHIFT);
	rc = robu::vfs_xattr(g_fds[fd].server_tid, wire_command, g_fds[fd].handle, shmid,
		name_len, value_size, &returned_size);
	if (rc == robu::IPC_ERR_NONE && output_size != 0) {
		uint8_t *value = (uint8_t *)output;
		uint64_t offset = command == robu::VFS_XATTR_LIST ? 0 : name_len;
		for (uint64_t i = 0; i < returned_size; i++) value[i] = shared[offset + i];
	}
	robu::shmdt_raw(va);
	robu::shmctl_raw(shmid, 0, nullptr);
	if (rc != robu::IPC_ERR_NONE) return xattr_err_to_errno(rc);
	if (nread) *nread = (ssize_t)returned_size;
	return 0;
}

int xattr_path(const char *path, uint64_t command, const char *name, const void *input,
		size_t input_size, void *output, size_t output_size, int flags, ssize_t *nread) {
	int fd = -1;
	int e = mlibc::Sysdeps<::Open>{}(path, O_RDONLY, 0, &fd);
	if (e) return e;
	e = xattr_fd(fd, command, name, input, input_size, output, output_size, flags, nread);
	mlibc::Sysdeps<::Close>{}(fd);
	return e;
}

}

namespace mlibc {

int Sysdeps<Open>::operator()(const char *path, int flags, mode_t, int *fd_out) {
	ensure_stdio_defaults();
	char resolved[CWD_MAX];
	resolve_path(path, resolved, sizeof(resolved));

	const char *rel;
	uint32_t mount_tid = resolve_mount_for_dir(resolved, &rel);
	if (mount_tid != 0) {
		{
			uint64_t size;
			int is_dir;
			uint64_t ino;
			if (robu::vfs_stat(mount_tid, rel, &size, &is_dir, &ino) == 0 &&
					is_dir == robu::VFS_NODE_DIR) {
				if ((flags & O_ACCMODE) != O_RDONLY) return EISDIR;
				int fd = alloc_fd();
				if (fd < 0) return EMFILE;
				g_fds[fd] = { FD_RAMFS_DIR, ino, 0, mount_tid };
				g_dir_cursors[fd] = 0;
				*fd_out = fd;
				return 0;
			}
		}
		uint64_t vflags = 0;
		if (flags & O_CREAT) vflags |= robu::VFS_O_CREAT;
		if (flags & O_TRUNC) vflags |= robu::VFS_O_TRUNC;
		if (flags & O_APPEND) vflags |= robu::VFS_O_APPEND;
		int64_t h = robu::vfs_open(mount_tid, rel, vflags);
		if (h < 0) return h == robu::VFS_ERR_NOT_FOUND ? ENOENT : EIO;
		int fd = alloc_fd();
		if (fd < 0) { robu::vfs_close(mount_tid, (uint64_t)h); return EMFILE; }
		g_fds[fd] = { FD_VFS, (uint64_t)h, 0, mount_tid };
		*fd_out = fd;
		return 0;
	}
	return ENOENT;
}

int Sysdeps<Stat>::operator()(fsfd_target fsfdt, int fd, const char *path, int, struct stat *statbuf) {
	ensure_stdio_defaults();
	if (fsfdt == fsfd_target::fd) {
		if (!fd_valid(fd)) return EBADF;
		switch (g_fds[fd].kind) {
		case FD_RAMFS_DIR:
			fill_stat(statbuf, 0, 1, ROBU_STDEV_RAMFS, (ino_t)g_fds[fd].handle);
			return 0;
		case FD_VFS: {
			uint64_t size, ino;
			int is_dir;
			int64_t atime = 0;
			int64_t mtime = 0;
			int have_timestamps = vfs_has_timestamps(g_fds[fd].server_tid);
			if (robu::vfs_fstat(g_fds[fd].server_tid, g_fds[fd].handle, &size, &is_dir, &ino,
					have_timestamps ? &atime : nullptr,
					have_timestamps ? &mtime : nullptr) != 0) return EBADF;
			if (g_fds[fd].server_tid == robu::devfs_tid()) {
				fill_stat(statbuf, size, is_dir, ROBU_STDEV_DEVFS, (ino_t)ino, atime, mtime);
				if (is_dir != robu::VFS_NODE_DIR) {
					if (is_dir == robu::VFS_NODE_BLOCK) {
						statbuf->st_mode = S_IFBLK | 0660;
						statbuf->st_rdev = (dev_t)ino;
					} else {
						statbuf->st_mode = S_IFCHR | 0666;
					}
				}
				return 0;
			}
			if (g_fds[fd].server_tid == robu::ramfs_tid()) {
				fill_stat(statbuf, size, is_dir, ROBU_STDEV_RAMFS, (ino_t)ino, atime, mtime);
				return 0;
			}
			fill_stat(statbuf, size, is_dir, ROBU_STDEV_VFS, (ino_t)ino, atime, mtime);
			return 0;
		}
		default:
			return EBADF;
		}
	}
	char resolved[CWD_MAX];
	resolve_path(path, resolved, sizeof(resolved));
	return stat_path(resolved, statbuf);
}

int Sysdeps<Access>::operator()(const char *path, int) {
	ensure_stdio_defaults();
	char resolved[CWD_MAX];
	resolve_path(path, resolved, sizeof(resolved));
	struct stat statbuf;
	return stat_path(resolved, &statbuf);
}

int Sysdeps<Fchmod>::operator()(int, mode_t) {
	return 0;
}

int Sysdeps<Chmod>::operator()(const char *, mode_t) {
	return 0;
}

namespace {
mode_t g_umask = 022;
}

int Sysdeps<Umask>::operator()(mode_t mode, mode_t *old) {
	*old = g_umask;
	g_umask = mode;
	return 0;
}

int Sysdeps<OpenDir>::operator()(const char *path, int *handle) {
	ensure_stdio_defaults();
	char resolved[CWD_MAX];
	resolve_path(path, resolved, sizeof(resolved));
	uint64_t dir_ino;
	uint32_t server_tid;
	{
		const char *rel;
		server_tid = resolve_mount_for_dir(resolved, &rel);
		if (server_tid == 0) return ENOENT;
		uint64_t size, ino;
		int is_dir;
		if (robu::vfs_stat(server_tid, rel, &size, &is_dir, &ino) != 0 ||
				is_dir != robu::VFS_NODE_DIR) {
			return ENOTDIR;
		}
		dir_ino = ino;
	}
	int fd = alloc_fd();
	if (fd < 0) return EMFILE;
	g_fds[fd] = { FD_RAMFS_DIR, dir_ino, 0, server_tid };
	g_dir_cursors[fd] = 0;
	*handle = fd;
	return 0;
}

int Sysdeps<ReadEntries>::operator()(int handle, void *buffer, size_t max_size, size_t *bytes_read) {
	if (!fd_valid(handle) || g_fds[handle].kind != FD_RAMFS_DIR) {
		return EBADF;
	}
	char name[32];
	int is_dir;
	int64_t rc = robu::vfs_readdir(g_fds[handle].server_tid, g_fds[handle].handle, g_dir_cursors[handle], name, &is_dir);
	if (rc != 0) {
		*bytes_read = 0;
		return 0;
	}
	g_dir_cursors[handle]++;
	struct dirent *de = (struct dirent *)buffer;
	if (max_size < sizeof(*de)) {
		return EINVAL;
	}
	memset(de, 0, sizeof(*de));
	de->d_ino = 1;
	de->d_off = 0;
	de->d_reclen = sizeof(*de);
	de->d_type = is_dir == robu::VFS_NODE_DIR ? DT_DIR :
		is_dir == robu::VFS_NODE_BLOCK ? DT_BLK :
		is_dir == robu::VFS_NODE_CHAR ? DT_CHR : DT_REG;
	strncpy(de->d_name, name, sizeof(de->d_name) - 1);
	*bytes_read = sizeof(*de);
	return 0;
}

int Sysdeps<GetCwd>::operator()(char *buffer, size_t size) {
	size_t len = strlen(g_cwd);
	if (len + 1 > size) {
		return ERANGE;
	}
	memcpy(buffer, g_cwd, len + 1);
	return 0;
}

int Sysdeps<Readlink>::operator()(const char *, void *, size_t, ssize_t *) {
	return ENOSYS;
}

int Sysdeps<Chdir>::operator()(const char *path) {
	char resolved[CWD_MAX];
	resolve_path(path, resolved, sizeof(resolved));
	bool valid = false;
	const char *rel;
	uint32_t mount_tid = resolve_mount_for_dir(resolved, &rel);
	if (mount_tid != 0) {
		uint64_t size, ino;
		int is_dir;
		valid = robu::vfs_stat(mount_tid, rel, &size, &is_dir, &ino) == 0 &&
			is_dir == robu::VFS_NODE_DIR;
	}
	if (!valid) {
		return ENOENT;
	}
	strncpy(g_cwd, resolved, sizeof(g_cwd) - 1);
	g_cwd[sizeof(g_cwd) - 1] = '\0';
	return 0;
}

int Sysdeps<Utimensat>::operator()(int dirfd, const char *path, const struct timespec *times, int flags) {
	if (dirfd != AT_FDCWD || !path || flags != 0) {
		return ENOTSUP;
	}
	char resolved[CWD_MAX];
	resolve_path(path, resolved, sizeof(resolved));
	const char *rel;
	uint32_t mount_tid = resolve_mount_for_dir(resolved, &rel);
	if (mount_tid == 0) {
		return ENOENT;
	}
	if (!vfs_has_timestamps(mount_tid)) {
		return ENOTSUP;
	}
	int64_t handle = robu::vfs_open(mount_tid, rel, 0);
	if (handle < 0) {
		return vfs_err_to_errno(handle);
	}
	time_t now_sec = 0;
	long now_nsec = 0;
	int clock_error = Sysdeps<ClockGet>{}(CLOCK_REALTIME, &now_sec, &now_nsec);
	if (clock_error != 0) {
		robu::vfs_close(mount_tid, (uint64_t)handle);
		return clock_error;
	}
	int64_t atime = now_sec;
	int64_t mtime = now_sec;
	uint64_t utime_flags = 0;
	if (times) {
		for (int i = 0; i < 2; i++) {
			long nsec = times[i].tv_nsec;
			if (nsec != UTIME_NOW && nsec != UTIME_OMIT && (nsec < 0 || nsec >= 1000000000L)) {
				robu::vfs_close(mount_tid, (uint64_t)handle);
				return EINVAL;
			}
			if (i == 0) {
				if (nsec == UTIME_OMIT) utime_flags |= robu::VFS_UTIME_OMIT_ATIME;
				else atime = nsec == UTIME_NOW ? now_sec : times[i].tv_sec;
			} else {
				if (nsec == UTIME_OMIT) utime_flags |= robu::VFS_UTIME_OMIT_MTIME;
				else mtime = nsec == UTIME_NOW ? now_sec : times[i].tv_sec;
			}
		}
	}
	int64_t status = robu::vfs_utimens(mount_tid, (uint64_t)handle, utime_flags,
		atime, mtime, now_sec);
	robu::vfs_close(mount_tid, (uint64_t)handle);
	return vfs_err_to_errno(status);
}

int Sysdeps<Mkdir>::operator()(const char *path, mode_t) {
	char resolved[CWD_MAX];
	resolve_path(path, resolved, sizeof(resolved));
	const char *rel;
	uint32_t mount_tid = resolve_mount_for_dir(resolved, &rel);
	if (mount_tid == 0) return ENOENT;
	return vfs_err_to_errno(robu::vfs_mkdir(mount_tid, rel));
}

int Sysdeps<Rmdir>::operator()(const char *path) {
	char resolved[CWD_MAX];
	resolve_path(path, resolved, sizeof(resolved));
	const char *rel;
	uint32_t mount_tid = resolve_mount_for_dir(resolved, &rel);
	if (mount_tid == 0) return ENOENT;
	return vfs_err_to_errno(robu::vfs_rmdir(mount_tid, rel));
}

int Sysdeps<Unlinkat>::operator()(int, const char *path, int) {
	char resolved[CWD_MAX];
	resolve_path(path, resolved, sizeof(resolved));
	const char *rel;
	uint32_t mount_tid = resolve_mount_for_dir(resolved, &rel);
	if (mount_tid == 0) return ENOENT;
	return vfs_err_to_errno(robu::vfs_unlink(mount_tid, rel));
}

int Sysdeps<Link>::operator()(const char *old_path, const char *new_path) {
	char old_resolved[CWD_MAX], new_resolved[CWD_MAX];
	resolve_path(old_path, old_resolved, sizeof(old_resolved));
	resolve_path(new_path, new_resolved, sizeof(new_resolved));
	const char *old_rel;
	const char *new_rel;
	uint32_t old_tid = resolve_mount_for_dir(old_resolved, &old_rel);
	uint32_t new_tid = resolve_mount_for_dir(new_resolved, &new_rel);
	if (old_tid == 0 || new_tid == 0) return ENOENT;
	if (old_tid != new_tid) return EXDEV;
	return vfs_err_to_errno(robu::vfs_link(old_tid, old_rel, new_rel));
}

int Sysdeps<Mknodat>::operator()(int, const char *path, int mode, int dev) {
	char resolved[CWD_MAX];
	resolve_path(path, resolved, sizeof(resolved));
	const char *rel;
	uint32_t mount_tid = resolve_mount_for_dir(resolved, &rel);
	if (mount_tid == 0) return ENOENT;
	return vfs_err_to_errno(robu::vfs_mknod(mount_tid, rel, (uint64_t)mode, (uint64_t)dev));
}

void Sysdeps<Sync>::operator()() {
	robu::vfs_quiesce_disk();
}

int Sysdeps<Setxattr>::operator()(const char *path, const char *name, const void *value,
		size_t size, int flags) {
	return xattr_path(path, robu::VFS_XATTR_SET, name, value, size, nullptr, 0, flags, nullptr);
}

int Sysdeps<Lsetxattr>::operator()(const char *, const char *, const void *, size_t, int) {
	return ENOTSUP;
}

int Sysdeps<Fsetxattr>::operator()(int fd, const char *name, const void *value, size_t size,
		int flags) {
	return xattr_fd(fd, robu::VFS_XATTR_SET, name, value, size, nullptr, 0, flags, nullptr);
}

int Sysdeps<Getxattr>::operator()(const char *path, const char *name, void *value, size_t size,
		ssize_t *nread) {
	return xattr_path(path, robu::VFS_XATTR_GET, name, nullptr, 0, value, size, 0, nread);
}

int Sysdeps<Lgetxattr>::operator()(const char *, const char *, void *, size_t, ssize_t *) {
	return ENOTSUP;
}

int Sysdeps<Fgetxattr>::operator()(int fd, const char *name, void *value, size_t size,
		ssize_t *nread) {
	return xattr_fd(fd, robu::VFS_XATTR_GET, name, nullptr, 0, value, size, 0, nread);
}

int Sysdeps<Listxattr>::operator()(const char *path, char *list, size_t size, ssize_t *nread) {
	return xattr_path(path, robu::VFS_XATTR_LIST, nullptr, nullptr, 0, list, size, 0, nread);
}

int Sysdeps<Llistxattr>::operator()(const char *, char *, size_t, ssize_t *) {
	return ENOTSUP;
}

int Sysdeps<Flistxattr>::operator()(int fd, char *list, size_t size, ssize_t *nread) {
	return xattr_fd(fd, robu::VFS_XATTR_LIST, nullptr, nullptr, 0, list, size, 0, nread);
}

int Sysdeps<Removexattr>::operator()(const char *path, const char *name) {
	return xattr_path(path, robu::VFS_XATTR_REMOVE, name, nullptr, 0, nullptr, 0, 0, nullptr);
}

int Sysdeps<Lremovexattr>::operator()(const char *, const char *) {
	return ENOTSUP;
}

int Sysdeps<Fremovexattr>::operator()(int fd, const char *name) {
	return xattr_fd(fd, robu::VFS_XATTR_REMOVE, name, nullptr, 0, nullptr, 0, 0, nullptr);
}

}
