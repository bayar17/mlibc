#include <errno.h>
#include <string.h>
#include <termios.h>
#include <poll.h>
#include <sys/select.h>
#include <bits/winsize.h>
#include <mlibc/all-sysdeps.hpp>

#include "robu-abi.hpp"
#include "sysdeps_internal.hpp"

using namespace robu_detail;

namespace mlibc {

int Sysdeps<Tcgetattr>::operator()(int fd, struct termios *attr) {
	ensure_stdio_defaults();
	int vt = fd_to_vt(fd);
	memset(attr, 0, sizeof(*attr));
	attr->c_cflag = CREAD | CS8;
	attr->c_lflag = ISIG | ECHOE | ECHOK | ECHO;
	if (!robu::console_mode_query_raw(vt)) {
		attr->c_lflag |= ICANON;
	}
	attr->c_cc[VMIN] = 1;
	attr->c_cc[VTIME] = 0;
	return 0;
}

int Sysdeps<Tcsetattr>::operator()(int fd, int when, const struct termios *attr) {
	(void)when;
	ensure_stdio_defaults();
	int vt = fd_to_vt(fd);
	int raw = !(attr->c_lflag & ICANON);
	robu::console_mode_set_raw(vt, raw);
	return 0;
}

int Sysdeps<Tcgetwinsize>::operator()(int fd, struct winsize *winsz) {
	(void)fd;
	winsz->ws_row = 25;
	winsz->ws_col = 80;
	winsz->ws_xpixel = 0;
	winsz->ws_ypixel = 0;
	return 0;
}

int Sysdeps<Tcsetwinsize>::operator()(int fd, const struct winsize *winsz) {
	(void)fd;
	(void)winsz;
	return 0;
}

int Sysdeps<Tcflow>::operator()(int fd, int action) {
	(void)fd;
	(void)action;
	return 0;
}

int Sysdeps<Ioctl>::operator()(int fd, unsigned long request, void *arg, int *result) {
	if (result) {
		*result = 0;
	}
	if (request == robu::ROBU_TIOCGWINSZ) {
		struct winsize *ws = (struct winsize *)arg;
		ws->ws_row = 25;
		ws->ws_col = 80;
		ws->ws_xpixel = 0;
		ws->ws_ypixel = 0;
		return 0;
	}
	if (request == robu::ROBU_TIOCGPGRP) {
		*(int *)arg = (int)robu::tcgetpgrp_raw(fd_to_vt(fd));
		return 0;
	}
	if (request == robu::ROBU_TIOCSPGRP) {
		robu::tcsetpgrp_raw(fd_to_vt(fd), (uint64_t)(*(int *)arg));
		return 0;
	}
	if (request == robu::ROBU_TIOCSCTTY) {
		return 0;
	}
	if (request == robu::ROBU_BLKGETSIZE64) {
		if (!arg || !fd_valid(fd) || g_fds[fd].kind != FD_VFS ||
				g_fds[fd].server_tid != robu::devfs_tid()) {
			return ENOTTY;
		}
		if (g_fds[fd].handle < robu::DEVFS_BLOCK_HANDLE_BASE) {
			return ENOTTY;
		}
		uint64_t size = 0;
		uint64_t ino = 0;
		int node_type = robu::VFS_NODE_REG;
		if (robu::vfs_fstat(g_fds[fd].server_tid, g_fds[fd].handle, &size, &node_type, &ino) != 0) {
			return ENOTTY;
		}
		*(uint64_t *)arg = size;
		return 0;
	}
	return ENOTTY;
}

int Sysdeps<SetPgid>::operator()(pid_t pid, pid_t pgid) {
	int64_t rc = robu::setpgid_raw((uint64_t)pid, (uint64_t)pgid);
	return rc == robu::IPC_ERR_NONE ? 0 : ESRCH;
}

int Sysdeps<GetPgid>::operator()(pid_t pid, pid_t *pgid) {
	uint64_t g = 0;
	int64_t rc = robu::getpgid_raw((uint64_t)pid, &g);
	if (rc != robu::IPC_ERR_NONE) {
		return ESRCH;
	}
	*pgid = (pid_t)g;
	return 0;
}

int Sysdeps<SetSid>::operator()(pid_t *sid) {
	uint64_t s = 0;
	robu::setsid_raw(&s);
	if (sid) {
		*sid = (pid_t)s;
	}
	return 0;
}

int Sysdeps<Pselect>::operator()(int num_fds, fd_set *read_set, fd_set *write_set, fd_set *except_set,
		const struct timespec *timeout, const sigset_t *, int *num_events) {
	if (write_set) {
		FD_ZERO(write_set);
	}
	if (except_set) {
		FD_ZERO(except_set);
	}
	if (!read_set) {
		*num_events = 0;
		return 0;
	}

	int fd = -1;
	for (int i = 0; i < num_fds; i++) {
		if (FD_ISSET(i, read_set)) {
			fd = i;
			break;
		}
	}
	FD_ZERO(read_set);
	if (fd < 0) {
		*num_events = 0;
		return 0;
	}

	ensure_stdio_defaults();
	if (!fd_valid(fd)) {
		*num_events = 0;
		return 0;
	}

	bool is_console = g_fds[fd].kind == FD_VFS && g_fds[fd].server_tid == robu::devfs_tid() &&
	                   is_tty_handle(g_fds[fd].handle);

	bool has_timeout = timeout != nullptr;
	uint64_t ticks_left = has_timeout
		? (uint64_t)timeout->tv_sec * 100 + (uint64_t)timeout->tv_nsec / 10000000
		: 0;

	if (is_console && sig_check_interrupt()) {
		return EINTR;
	}
	for (;;) {
		bool ready = is_console ? robu::vfs_peek(robu::devfs_tid(), g_fds[fd].handle) > 0 : true;
		if (ready) {
			FD_SET(fd, read_set);
			*num_events = 1;
			return 0;
		}
		if (has_timeout) {
			if (ticks_left == 0) {
				*num_events = 0;
				return 0;
			}
			ticks_left--;
		}
		if (is_console && sig_check_interrupt()) {
			return EINTR;
		}
		robu::sleep_raw(1);
	}
}

int Sysdeps<Poll>::operator()(struct pollfd *fds, nfds_t count, int timeout, int *num_events) {
	ensure_stdio_defaults();
	bool has_timeout = timeout >= 0;
	uint64_t ticks_left = has_timeout ? (uint64_t)timeout / 10 : 0;
	for (;;) {
		int ready = 0;
		for (nfds_t i = 0; i < count; i++) {
			fds[i].revents = 0;
			int fd = fds[i].fd;
			if (fd < 0) {
				continue;
			}
			if (!fd_valid(fd)) {
				fds[i].revents = POLLNVAL;
				ready++;
				continue;
			}
			bool readable = true;
			switch (g_fds[fd].kind) {
			case FD_VFS:
				readable = robu::vfs_peek(g_fds[fd].server_tid, g_fds[fd].handle) > 0;
				break;
			case FD_SOCKET: {
				uint8_t dummy;
				uint64_t n = 0;
				readable = robu::sock_read_raw((int)g_fds[fd].handle, &dummy, 0, &n) == robu::IPC_ERR_NONE;
				break;
			}
			default:
				readable = true;
				break;
			}
			if ((fds[i].events & POLLIN) && readable) {
				fds[i].revents |= POLLIN;
			}
			if (fds[i].events & POLLOUT) {
				fds[i].revents |= POLLOUT;
			}
			if (fds[i].revents) {
				ready++;
			}
		}
		if (ready > 0) {
			*num_events = ready;
			return 0;
		}
		if (has_timeout) {
			if (ticks_left == 0) {
				*num_events = 0;
				return 0;
			}
			ticks_left--;
		}
		if (sig_check_interrupt()) {
			return EINTR;
		}
		robu::sleep_raw(1);
	}
}

}
