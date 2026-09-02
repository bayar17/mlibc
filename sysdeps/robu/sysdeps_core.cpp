#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <mlibc/all-sysdeps.hpp>
#include <bits/ensure.h>

#include "robu-abi.hpp"
#include "sysdeps_internal.hpp"

namespace robu_detail {

FdEntry g_fds[MAX_FDS];
uint64_t g_dir_cursors[MAX_FDS];

int alloc_fd() {
	for (int i = 0; i < MAX_FDS; i++) {
		if (g_fds[i].kind == FD_NONE) {
			return i;
		}
	}
	return -1;
}

bool fd_valid(int fd) {
	return fd >= 0 && fd < MAX_FDS && g_fds[fd].kind != FD_NONE;
}

bool is_tty_handle(uint64_t handle) {
	return handle == robu::DEV_CONSOLE || (handle >= robu::DEV_RSTTY1 && handle <= robu::DEV_RSTTY6);
}

int fd_to_vt(int fd) {
	if (!fd_valid(fd) || g_fds[fd].kind != FD_VFS || g_fds[fd].server_tid != robu::devfs_tid()) {
		return 0;
	}
	uint64_t h = g_fds[fd].handle;
	if (h >= robu::DEV_RSTTY1 && h <= robu::DEV_RSTTY6) {
		return (int)(h - robu::DEV_RSTTY1);
	}
	return 0;
}

namespace {

int64_t console_handle_cached = -1;
int64_t console_handle() {
	if (console_handle_cached < 0) {
		const char *tty_dev = getenv("ROBU_TTY_DEV");
		console_handle_cached = robu::vfs_open(robu::devfs_tid(), tty_dev && tty_dev[0] ? tty_dev : "console", 0);
	}
	return console_handle_cached;
}

void console_write_all(const char *buf, size_t len) {
	int64_t h = console_handle();
	if (h < 0) {
		return;
	}
	size_t off = 0;
	while (off < len) {
		size_t chunk = len - off;
		if (chunk > (size_t)robu::VFS_WRITE_MAX) {
			chunk = robu::VFS_WRITE_MAX;
		}
		robu::vfs_write(robu::devfs_tid(), (uint64_t)h, buf + off, chunk);
		off += chunk;
	}
}

}

void ensure_stdio_defaults() {
	for (int fd = 0; fd <= 2; fd++) {
		if (g_fds[fd].kind == FD_NONE) {
			g_fds[fd] = { FD_VFS, (uint64_t)console_handle(), 0, robu::devfs_tid() };
		}
	}
}

bool export_std_fd(int fd, uint32_t *kind, uint64_t *handle, uint32_t *server_tid) {
	ensure_stdio_defaults();
	if (!fd_valid(fd)) return false;
	if (g_fds[fd].kind == FD_PIPE_READ) {
		*kind = robu::SPAWN_FD_KIND_PIPE_READ;
	} else if (g_fds[fd].kind == FD_PIPE_WRITE) {
		*kind = robu::SPAWN_FD_KIND_PIPE_WRITE;
	} else {
		*kind = (uint32_t)g_fds[fd].kind;
	}
	*handle = g_fds[fd].handle;
	*server_tid = g_fds[fd].server_tid;
	return true;
}

namespace {
bool g_fd_inherit_done;
}

}

extern "C" void __robu_fd_inherit(uint64_t spawn_info) {
	using namespace robu_detail;
	g_fd_inherit_done = true;
	if (!spawn_info) {
		return;
	}
	const robu::robu_spawn_info *info = reinterpret_cast<const robu::robu_spawn_info *>(spawn_info);
	if (info->magic != robu::SPAWN_INFO_MAGIC) {
		return;
	}
	const robu::robu_spawn_fd *fds = reinterpret_cast<const robu::robu_spawn_fd *>(info + 1);
	uint32_t nfds = info->nfds;
	if (nfds > (uint32_t)robu::SPAWN_FD_INFO_MAX) {
		nfds = robu::SPAWN_FD_INFO_MAX;
	}
	for (uint32_t i = 0; i < nfds; i++) {
		int fd = (int)fds[i].fd;
		if (fd < 0 || fd > 2) continue;
		if (fds[i].kind == robu::SPAWN_FD_KIND_PIPE_READ) {
			g_fds[fd].kind = FD_PIPE_READ;
		} else if (fds[i].kind == robu::SPAWN_FD_KIND_PIPE_WRITE) {
			g_fds[fd].kind = FD_PIPE_WRITE;
		} else {
			g_fds[fd].kind = (FdKind)fds[i].kind;
		}
		g_fds[fd].handle = fds[i].handle;
		g_fds[fd].server_tid = fds[i].server_tid;
	}
}

using namespace robu_detail;

namespace mlibc {

[[noreturn]] void Sysdeps<LibcPanic>::operator()() {
	sysdep<LibcLog>("!!! mlibc panic !!!");
	sysdep<Exit>(-1);
	__builtin_trap();
}

void Sysdeps<LibcLog>::operator()(const char *msg) {
	robu_detail::console_write_all(msg, strlen(msg));
	robu_detail::console_write_all("\n", 1);
}

int Sysdeps<Isatty>::operator()(int fd) {
	ensure_stdio_defaults();
	if (fd_valid(fd) && g_fds[fd].kind == FD_VFS && g_fds[fd].server_tid == robu::devfs_tid() &&
			is_tty_handle(g_fds[fd].handle)) {
		return 0;
	}
	return ENOTTY;
}

int Sysdeps<Dup>::operator()(int fd, int, int *newfd_out) {
	ensure_stdio_defaults();
	if (!fd_valid(fd)) {
		return EBADF;
	}
	int newfd = alloc_fd();
	if (newfd < 0) {
		return EMFILE;
	}
	g_fds[newfd] = g_fds[fd];
	*newfd_out = newfd;
	return 0;
}

int Sysdeps<Dup2>::operator()(int fd, int, int newfd) {
	ensure_stdio_defaults();
	if (!fd_valid(fd)) {
		return EBADF;
	}
	if (newfd < 0 || newfd >= MAX_FDS) {
		return EBADF;
	}
	if (newfd == fd) {
		return 0;
	}
	if (fd_valid(newfd)) {
		Sysdeps<Close>{}(newfd);
	}
	g_fds[newfd] = g_fds[fd];
	return 0;
}

int Sysdeps<Close>::operator()(int fd) {
	ensure_stdio_defaults();
	if (!fd_valid(fd)) {
		return EBADF;
	}
	bool shared = false;
	for (int i = 0; i < MAX_FDS; i++) {
		if (i != fd && g_fds[i].kind == g_fds[fd].kind && g_fds[i].handle == g_fds[fd].handle &&
		    g_fds[i].server_tid == g_fds[fd].server_tid) {
			shared = true;
			break;
		}
	}
	if (!shared) {
		switch (g_fds[fd].kind) {
		case FD_PIPE_READ: robu::pipe_close_raw(g_fds[fd].handle, 0); break;
		case FD_PIPE_WRITE: robu::pipe_close_raw(g_fds[fd].handle, 1); break;
		case FD_VFS: robu::vfs_close(g_fds[fd].server_tid, g_fds[fd].handle); break;
		case FD_SOCKET: robu::sock_close_raw((int)g_fds[fd].handle); break;
		default: break;
		}
	}
	g_fds[fd] = FdEntry{};
	return 0;
}

int Sysdeps<Write>::operator()(int fd, const void *buf, size_t count, ssize_t *bytes_written) {
	ensure_stdio_defaults();
	if (!fd_valid(fd)) {
		return EBADF;
	}
	const uint8_t *p = (const uint8_t *)buf;
	size_t total = 0;
	if (g_fds[fd].kind == FD_PIPE_WRITE) {
		while (total < count) {
			uint64_t n = 0;
			int64_t rc = robu::pipe_write_raw(g_fds[fd].handle, p + total, count - total, &n);
			if (rc == robu::IPC_ERR_NOT_FOUND) {
				return total > 0 ? 0 : EPIPE;
			}
			if (rc != robu::IPC_ERR_NONE) {
				return total > 0 ? 0 : EIO;
			}
			if (n == 0) {
				robu::ipc_raw(0, 1, robu::IPC_FLAG_NONE, nullptr, nullptr);
				continue;
			}
			total += (size_t)n;
		}
		*bytes_written = (ssize_t)total;
		return 0;
	}
	if (g_fds[fd].kind == FD_SOCKET) {
		while (total < count) {
			uint64_t n = 0;
			int64_t rc = robu::sock_write_raw((int)g_fds[fd].handle, p + total, count - total, &n);
			if (rc == robu::IPC_ERR_NOT_FOUND) {
				return total > 0 ? 0 : EPIPE;
			}
			if (rc == robu::IPC_ERR_INVALID) {
				return total > 0 ? 0 : ENOTCONN;
			}
			if (rc != robu::IPC_ERR_NONE) {
				return total > 0 ? 0 : EIO;
			}
			if (n == 0) {
				robu::ipc_raw(0, 1, robu::IPC_FLAG_NONE, nullptr, nullptr);
				continue;
			}
			total += (size_t)n;
		}
		*bytes_written = (ssize_t)total;
		return 0;
	}
	while (total < count) {
		size_t chunk = count - total;
		int64_t n;
		switch (g_fds[fd].kind) {
		case FD_VFS: n = robu::vfs_write(g_fds[fd].server_tid, g_fds[fd].handle, p + total, chunk); break;
		default: return EBADF;
		}
		if (n <= 0) {
			if (total > 0) break;
			return EIO;
		}
		total += (size_t)n;
	}
	*bytes_written = (ssize_t)total;
	return 0;
}

int Sysdeps<Read>::operator()(int fd, void *buf, size_t count, ssize_t *bytes_read) {
	ensure_stdio_defaults();
	if (!fd_valid(fd)) {
		return EBADF;
	}
	uint8_t *p = (uint8_t *)buf;
	size_t total = 0;
	if (g_fds[fd].kind == FD_PIPE_READ) {
		uint64_t n = 0;
		int64_t rc = robu::pipe_read_raw(g_fds[fd].handle, p, count, &n);
		while (rc == robu::IPC_ERR_NONE && n == 0) {
			robu::ipc_raw(0, 1, robu::IPC_FLAG_NONE, nullptr, nullptr);
			rc = robu::pipe_read_raw(g_fds[fd].handle, p, count, &n);
		}
		if (rc == robu::IPC_ERR_NOT_FOUND) {
			*bytes_read = 0;
			return 0;
		}
		if (rc != robu::IPC_ERR_NONE) {
			return EIO;
		}
		*bytes_read = (ssize_t)n;
		return 0;
	}
	if (g_fds[fd].kind == FD_SOCKET) {
		uint64_t n = 0;
		int64_t rc = robu::sock_read_raw((int)g_fds[fd].handle, p, count, &n);
		while (rc == robu::IPC_ERR_WOULDBLOCK) {
			robu::ipc_raw(0, 1, robu::IPC_FLAG_NONE, nullptr, nullptr);
			rc = robu::sock_read_raw((int)g_fds[fd].handle, p, count, &n);
		}
		if (rc == robu::IPC_ERR_NOT_FOUND) {
			*bytes_read = 0;
			return 0;
		}
		if (rc == robu::IPC_ERR_INVALID) {
			return ENOTCONN;
		}
		if (rc != robu::IPC_ERR_NONE) {
			return EIO;
		}
		*bytes_read = (ssize_t)n;
		return 0;
	}
	bool is_console = g_fds[fd].kind == FD_VFS && g_fds[fd].server_tid == robu::devfs_tid() &&
	                  is_tty_handle(g_fds[fd].handle);
	if (is_console && sig_check_interrupt()) {
		return EINTR;
	}
	while (total < count) {
		size_t chunk = count - total;
		int64_t n;
		switch (g_fds[fd].kind) {
		case FD_VFS: n = robu::vfs_read(g_fds[fd].server_tid, g_fds[fd].handle, p + total, chunk); break;
		default: return EBADF;
		}
		if (is_console && n == robu::VFS_ERR_WOULDBLOCK && total == 0) {
			robu::ipc_raw(0, 1, robu::IPC_FLAG_NONE, nullptr, nullptr);
			if (sig_check_interrupt()) {
				return EINTR;
			}
			continue;
		}
		if (n < 0) return EIO;
		if (n == 0) {
			break;
		}
		total += (size_t)n;
		break;
	}
	*bytes_read = (ssize_t)total;
    return 0;
}

int Sysdeps<GetEntropy>::operator()(void *buffer, size_t length) {
	uint8_t *bytes = (uint8_t *)buffer;
	if (length != 0 && !bytes) {
		return EFAULT;
	}
	while (length != 0) {
		size_t chunk = length > 256 ? 256 : length;
		int64_t rc = robu::getentropy_raw(bytes, chunk);
		if (rc == robu::IPC_ERR_NONE) {
			bytes += chunk;
			length -= chunk;
			continue;
		}
		if (rc == robu::IPC_ERR_NOT_SUPPORTED) {
			return ENOSYS;
		}
		return EIO;
	}
	return 0;
}

int Sysdeps<Seek>::operator()(int fd, off_t offset, int whence, off_t *new_offset) {
	ensure_stdio_defaults();
	if (!fd_valid(fd) || g_fds[fd].kind != FD_VFS || g_fds[fd].server_tid != robu::devfs_tid()) {
		return ESPIPE;
	}
	uint64_t result = 0;
	int64_t status = robu::vfs_seek(g_fds[fd].server_tid, g_fds[fd].handle, (int64_t)offset,
			(uint64_t)whence, &result);
	if (status == robu::VFS_ERR_NOT_SUPPORTED) return ESPIPE;
	if (status != 0 || result > (uint64_t)INT64_MAX) return EINVAL;
	if (new_offset) *new_offset = (off_t)result;
	return 0;
}

int Sysdeps<Fcntl>::operator()(int fd, int request, va_list args, int *result) {
	if (!fd_valid(fd)) {
		return EBADF;
	}
	switch (request) {
	case F_GETFD:
		*result = 0;
		return 0;
	case F_SETFD:
		(void)va_arg(args, int);
		*result = 0;
		return 0;
	case F_GETFL:
		*result = 0;
		return 0;
	case F_SETFL:
		(void)va_arg(args, int);
		*result = 0;
		return 0;
	case F_DUPFD:
	case F_DUPFD_CLOEXEC: {
		int min_fd = va_arg(args, int);
		if (min_fd < 0) {
			return EINVAL;
		}
		int newfd = -1;
		for (int i = min_fd; i < MAX_FDS; i++) {
			if (g_fds[i].kind == FD_NONE) {
				newfd = i;
				break;
			}
		}
		if (newfd < 0) {
			return EMFILE;
		}
		g_fds[newfd] = g_fds[fd];
		*result = newfd;
		return 0;
	}
	default:
		return EINVAL;
	}
}

}
