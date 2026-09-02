#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <mlibc/all-sysdeps.hpp>

#include "robu-abi.hpp"
#include "sysdeps_internal.hpp"

using namespace robu_detail;

namespace mlibc {

int Sysdeps<Sleep>::operator()(time_t *secs, long *nanos) {
	uint64_t hz = robu::kinfo()->clock_hz;
	if (hz == 0) hz = 100;
	uint64_t ticks = (uint64_t)(*secs) * hz + ((uint64_t)(*nanos) * hz) / 1000000000ull;
	if (ticks == 0 && (*secs != 0 || *nanos != 0)) ticks = 1;
	robu::sleep_raw(ticks);
	*secs = 0;
	*nanos = 0;
	return 0;
}

[[noreturn]] void Sysdeps<Exit>::operator()(int status) {
	robu::exit_raw(status);
}

int Sysdeps<Fork>::operator()(pid_t *child) {
	uint64_t child_or_zero = 0;
	int64_t rc = robu::fork_raw(&child_or_zero);
	if (rc != robu::IPC_ERR_NONE) {
		return rc == robu::IPC_ERR_NO_MEM ? ENOMEM : EAGAIN;
	}
	*child = (pid_t)child_or_zero;
	return 0;
}

pid_t Sysdeps<FutexTid>::operator()() {
	return (pid_t)robu::self_tid();
}

pid_t Sysdeps<GetPid>::operator()() {
	return (pid_t)robu::self_tid();
}

pid_t Sysdeps<GetPpid>::operator()() {
	robu::msg_regs q{};
	q.word[0] = robu::self_tid();
	int64_t rc = robu::ipc_raw(0, 0, robu::IPC_FLAG_THREAD_INFO, &q, nullptr);
	if (rc != 0) {
		return 1;
	}
	return (pid_t)q.word[3];
}

int Sysdeps<Ttyname>::operator()(int fd, char *buf, size_t size) {
	if (!fd_valid(fd) || g_fds[fd].kind != FD_VFS || g_fds[fd].server_tid != robu::devfs_tid()) {
		return ENOTTY;
	}
	const char *name = "/dev/console";
	size_t len = strlen(name);
	if (len + 1 > size) {
		return ERANGE;
	}
	memcpy(buf, name, len + 1);
	return 0;
}

int Sysdeps<GetResuid>::operator()(uid_t *ruid, uid_t *euid, uid_t *suid) {
	*ruid = 0;
	*euid = 0;
	*suid = 0;
	return 0;
}

int Sysdeps<GetResgid>::operator()(gid_t *rgid, gid_t *egid, gid_t *sgid) {
	*rgid = 0;
	*egid = 0;
	*sgid = 0;
	return 0;
}

int Sysdeps<GetHostname>::operator()(char *buffer, size_t bufsize) {
	const char *name = "robu";
	size_t len = strlen(name);
	if (len + 1 > bufsize) {
		return ENAMETOOLONG;
	}
	memcpy(buffer, name, len + 1);
	return 0;
}

int Sysdeps<GetGroups>::operator()(size_t size, gid_t *list, int *ret) {
	(void)size;
	(void)list;
	*ret = 0;
	return 0;
}

int Sysdeps<Pipe>::operator()(int *fds, int) {
	uint64_t handle = 0;
	int64_t rc = robu::pipe_create(&handle);
	if (rc != robu::IPC_ERR_NONE) {
		return rc == robu::IPC_ERR_NO_MEM ? ENOMEM : EMFILE;
	}
	int rfd = alloc_fd();
	if (rfd < 0) {
		robu::pipe_close_raw(handle, 0);
		robu::pipe_close_raw(handle, 1);
		return EMFILE;
	}
	g_fds[rfd] = { FD_PIPE_READ, handle, 0 };
	int wfd = alloc_fd();
	if (wfd < 0) {
		g_fds[rfd] = FdEntry{};
		robu::pipe_close_raw(handle, 0);
		robu::pipe_close_raw(handle, 1);
		return EMFILE;
	}
	g_fds[wfd] = { FD_PIPE_WRITE, handle, 0 };
	fds[0] = rfd;
	fds[1] = wfd;
	return 0;
}

int Sysdeps<Execve>::operator()(const char *path, char *const argv[], char *const envp[]) {
	int64_t rc = robu::exec_raw(path, argv, envp, export_std_fd);
	if (rc == robu::IPC_ERR_NOT_FOUND) {
		return ENOENT;
	}
	if (rc == robu::IPC_ERR_NO_MEM) {
		return ENOMEM;
	}
	if (rc != robu::IPC_ERR_NONE) {
		return EINVAL;
	}
	return 0;
}

int Sysdeps<Waitpid>::operator()(pid_t pid, int *status, int flags, struct rusage *, pid_t *ret_pid) {
	bool nohang = (flags & WNOHANG) != 0;
	int64_t rc = robu::robu_waitpid(pid, status, nohang);
	if (rc == 0 && nohang) {
		*ret_pid = 0;
		return 0;
	}
	if (rc < 0) {
		return ECHILD;
	}
	*ret_pid = (pid_t)rc;
	return 0;
}

uid_t Sysdeps<GetUid>::operator()() {
	return 0;
}

uid_t Sysdeps<GetEuid>::operator()() {
	return 0;
}

gid_t Sysdeps<GetGid>::operator()() {
	return 0;
}

gid_t Sysdeps<GetEgid>::operator()() {
	return 0;
}

int Sysdeps<SetUid>::operator()(uid_t) {
	return 0;
}

int Sysdeps<SetGid>::operator()(gid_t) {
	return 0;
}

}

extern "C" int __libc_spawn(const char *name, char *const argv[], char *const envp[]) {
	int64_t rc = robu::robu_spawn(name, argv, envp, export_std_fd);
	if (rc < 0) {
		errno = ENOENT;
		return -1;
	}
	return (int)rc;
}
