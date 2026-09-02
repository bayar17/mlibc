#pragma once

namespace robu_detail {

enum FdKind {
	FD_NONE = 0,
	FD_RAMFS_DIR,
	FD_PIPE_READ,
	FD_PIPE_WRITE,
	FD_VFS,
	FD_SOCKET,
};

constexpr int MAX_FDS = 64;

struct FdEntry {
	FdKind kind = FD_NONE;
	uint64_t handle = 0;
	uint64_t size = 0;
	uint32_t server_tid = 0;
};

extern FdEntry g_fds[MAX_FDS];
extern uint64_t g_dir_cursors[MAX_FDS];

int alloc_fd();
bool fd_valid(int fd);
bool is_tty_handle(uint64_t handle);
int fd_to_vt(int fd);
void ensure_stdio_defaults();
bool export_std_fd(int fd, uint32_t *kind, uint64_t *handle, uint32_t *server_tid);

bool sig_check_interrupt();

}
