#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <mlibc/all-sysdeps.hpp>

#include "robu-abi.hpp"
#include "sysdeps_internal.hpp"

extern "C" uint64_t __robu_heap_base;

using namespace robu_detail;

namespace {
uint8_t *g_anon_cursor;
}

namespace mlibc {

int Sysdeps<TcbSet>::operator()(void *pointer) {
	robu::msg_regs m{};
	m.word[0] = (uint64_t)pointer;
	int64_t rc = robu::ipc_raw(0, 0, robu::IPC_FLAG_SET_FSBASE, &m, nullptr);
	return rc == robu::IPC_ERR_NONE ? 0 : EINVAL;
}

int Sysdeps<AnonAllocate>::operator()(size_t size, void **pointer) {
	if (!g_anon_cursor) {
		g_anon_cursor = (uint8_t *)__robu_heap_base;
	}
	size = (size + 0xFFF) & ~(size_t)0xFFF;
	*pointer = g_anon_cursor;
	g_anon_cursor += size;
	return 0;
}

int Sysdeps<AnonFree>::operator()(void *, size_t) {
	return 0;
}

int Sysdeps<VmMap>::operator()(void *, size_t size, int, int, int fd, off_t, void **window) {
	if (fd != -1) {
		return ENOSYS;
	}
	void *p;
	Sysdeps<AnonAllocate>::operator()(size, &p);
	*window = p;
	return 0;
}

int Sysdeps<VmUnmap>::operator()(void *, size_t) {
	return 0;
}

int Sysdeps<Shmget>::operator()(int *shm_id, key_t key, size_t size, int shmflg) {
	int id = -1;
	int64_t rc = robu::shmget_raw((int)key, (uint64_t)size, shmflg, &id);
	if (rc == robu::IPC_ERR_NONE) {
		*shm_id = id;
		return 0;
	}
	if (rc == robu::IPC_ERR_NO_MEM) return ENOMEM;
	if (rc == robu::IPC_ERR_NOT_FOUND) return ENOENT;
	if (rc == robu::IPC_ERR_EXISTS) return EEXIST;
	if (rc == robu::IPC_ERR_NO_SPACE) return ENOSPC;
	return EINVAL;
}

int Sysdeps<Shmat>::operator()(void **seg_start, int shmid, const void *shmaddr, int shmflg) {
	if (shmaddr != nullptr) {
		return EINVAL;
	}
	uint64_t va = 0;
	int64_t rc = robu::shmat_raw(shmid, 0, shmflg, &va);
	if (rc == robu::IPC_ERR_NONE) {
		*seg_start = (void *)va;
		return 0;
	}
	if (rc == robu::IPC_ERR_NO_MEM || rc == robu::IPC_ERR_NO_SPACE) return ENOMEM;
	if (rc == robu::IPC_ERR_NOT_FOUND) return EINVAL;
	if (rc == robu::IPC_ERR_NO_CAP) return EACCES;
	return EINVAL;
}

int Sysdeps<Shmdt>::operator()(const void *shmaddr) {
	int64_t rc = robu::shmdt_raw((uint64_t)shmaddr);
	return rc == robu::IPC_ERR_NONE ? 0 : EINVAL;
}

int Sysdeps<Shmctl>::operator()(int *idx, int shmid, int cmd, struct shmid_ds *buf) {
	robu::msg_regs reply{};
	int64_t rc = robu::shmctl_raw(shmid, cmd, &reply);
	if (rc != robu::IPC_ERR_NONE) {
		return EINVAL;
	}
	if (cmd == IPC_STAT && buf) {
		memset(buf, 0, sizeof(*buf));
		buf->shm_segsz = (size_t)reply.word[0];
		buf->shm_nattch = (shmatt_t)reply.word[1];
		buf->shm_cpid = (pid_t)(reply.word[2] & 0xFFFFFFFFu);
		buf->shm_lpid = (pid_t)(reply.word[2] >> 32);
		buf->shm_perm.mode = (mode_t)(reply.word[3] & 0xFFF);
		buf->shm_atime = (time_t)reply.word[4];
		buf->shm_dtime = (time_t)reply.word[5];
	}
	if (idx) {
		*idx = 0;
	}
	return 0;
}

}

namespace {

int extract_unix_path(const struct sockaddr *addr_ptr, socklen_t addr_length, char out[robu::SOCK_PATH_MAX]) {
	if (!addr_ptr || addr_length < sizeof(sa_family_t)) {
		return EINVAL;
	}
	const struct sockaddr_un *un = (const struct sockaddr_un *)addr_ptr;
	if (un->sun_family != AF_UNIX) {
		return EAFNOSUPPORT;
	}
	size_t path_len = addr_length - offsetof(struct sockaddr_un, sun_path);
	size_t n = strnlen(un->sun_path, path_len);
	if (n >= (size_t)robu::SOCK_PATH_MAX) {
		return ENAMETOOLONG;
	}
	memcpy(out, un->sun_path, n);
	out[n] = '\0';
	return 0;
}

}

namespace mlibc {

int Sysdeps<Socket>::operator()(int family, int type, int protocol, int *fd) {
	(void)protocol;
	if (family != AF_UNIX) {
		return EAFNOSUPPORT;
	}
	if ((type & 0xFF) != SOCK_STREAM) {
		return EPROTONOSUPPORT;
	}
	int sockid = -1;
	int64_t rc = robu::sock_create_raw(family, type & 0xFF, &sockid);
	if (rc != robu::IPC_ERR_NONE) {
		return rc == robu::IPC_ERR_NO_SPACE ? EMFILE : EINVAL;
	}
	int newfd = alloc_fd();
	if (newfd < 0) {
		robu::sock_close_raw(sockid);
		return EMFILE;
	}
	g_fds[newfd] = { FD_SOCKET, (uint64_t)(int64_t)sockid, 0, 0 };
	*fd = newfd;
	return 0;
}

int Sysdeps<Bind>::operator()(int fd, const struct sockaddr *addr_ptr, socklen_t addr_length) {
	if (!fd_valid(fd) || g_fds[fd].kind != FD_SOCKET) {
		return EBADF;
	}
	char path[robu::SOCK_PATH_MAX];
	int e = extract_unix_path(addr_ptr, addr_length, path);
	if (e) {
		return e;
	}
	int64_t rc = robu::sock_bind_raw((int)g_fds[fd].handle, path);
	if (rc == robu::IPC_ERR_NONE) {
		return 0;
	}
	if (rc == robu::IPC_ERR_EXISTS) {
		return EADDRINUSE;
	}
	return EINVAL;
}

int Sysdeps<Listen>::operator()(int fd, int backlog) {
	if (!fd_valid(fd) || g_fds[fd].kind != FD_SOCKET) {
		return EBADF;
	}
	int64_t rc = robu::sock_listen_raw((int)g_fds[fd].handle, backlog);
	return rc == robu::IPC_ERR_NONE ? 0 : EINVAL;
}

int Sysdeps<Connect>::operator()(int fd, const struct sockaddr *addr_ptr, socklen_t addr_length) {
	if (!fd_valid(fd) || g_fds[fd].kind != FD_SOCKET) {
		return EBADF;
	}
	char path[robu::SOCK_PATH_MAX];
	int e = extract_unix_path(addr_ptr, addr_length, path);
	if (e) {
		return e;
	}
	int64_t rc = robu::sock_connect_raw((int)g_fds[fd].handle, path);
	if (rc == robu::IPC_ERR_WOULDBLOCK) {
		while (rc == robu::IPC_ERR_WOULDBLOCK) {
			robu::ipc_raw(0, 1, robu::IPC_FLAG_NONE, nullptr, nullptr);
			rc = robu::sock_connect_raw((int)g_fds[fd].handle, path);
		}
	}
	if (rc == robu::IPC_ERR_NONE) {
		return 0;
	}
	if (rc == robu::IPC_ERR_NOT_FOUND) {
		return ECONNREFUSED;
	}
	return EINVAL;
}

int Sysdeps<Accept>::operator()(int fd, int *newfd, struct sockaddr *addr_ptr, socklen_t *addr_length, int flags) {
	(void)flags;
	if (!fd_valid(fd) || g_fds[fd].kind != FD_SOCKET) {
		return EBADF;
	}
	int new_sockid = -1;
	int64_t rc = robu::sock_accept_raw((int)g_fds[fd].handle, &new_sockid);
	while (rc == robu::IPC_ERR_WOULDBLOCK) {
		robu::ipc_raw(0, 1, robu::IPC_FLAG_NONE, nullptr, nullptr);
		rc = robu::sock_accept_raw((int)g_fds[fd].handle, &new_sockid);
	}
	if (rc != robu::IPC_ERR_NONE) {
		return EINVAL;
	}
	int fdslot = alloc_fd();
	if (fdslot < 0) {
		robu::sock_close_raw(new_sockid);
		return EMFILE;
	}
	g_fds[fdslot] = { FD_SOCKET, (uint64_t)(int64_t)new_sockid, 0, 0 };
	*newfd = fdslot;
	if (addr_ptr && addr_length) {
		socklen_t avail = *addr_length;
		struct sockaddr_un un{};
		un.sun_family = AF_UNIX;
		socklen_t actual = (socklen_t)offsetof(struct sockaddr_un, sun_path);
		socklen_t to_copy = avail < actual ? avail : actual;
		memcpy(addr_ptr, &un, to_copy);
		*addr_length = actual;
	}
	return 0;
}

int Sysdeps<FutexWait>::operator()(int *, int, const struct timespec *) {
	return ENOSYS;
}

int Sysdeps<FutexWake>::operator()(int *, bool) {
	return ENOSYS;
}

int Sysdeps<ClockGet>::operator()(int, time_t *secs, long *nanos) {
	uint64_t ticks = robu::kinfo_ticks();
	uint64_t hz = robu::kinfo()->clock_hz ? robu::kinfo()->clock_hz : 1;
	*secs = (time_t)(ticks / hz);
	*nanos = (long)((ticks % hz) * (1000000000ull / hz));
	return 0;
}

int Sysdeps<ClockGetres>::operator()(int, time_t *secs, long *nanos) {
	uint64_t hz = robu::kinfo()->clock_hz ? robu::kinfo()->clock_hz : 100;
	*secs = 0;
	*nanos = (long)(1000000000ull / hz);
	return 0;
}

int Sysdeps<GetSockopt>::operator()(int, int layer, int number, void *__restrict buffer, socklen_t *__restrict size) {
	if (layer == SOL_SOCKET && number == SO_ERROR) {
		if (*size >= sizeof(int)) {
			*(int *)buffer = 0;
			*size = sizeof(int);
		}
		return 0;
	}
	if (*size >= sizeof(int)) {
		memset(buffer, 0, sizeof(int));
		*size = sizeof(int);
	}
	return 0;
}

}
