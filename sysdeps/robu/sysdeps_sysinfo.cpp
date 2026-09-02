#include <errno.h>
#include <string.h>
#include <sys/sysinfo.h>

#include "robu-abi.hpp"

extern "C" int sysinfo(struct sysinfo *info) {
	if (!info) {
		errno = EFAULT;
		return -1;
	}
	robu::msg_regs m{};
	m.word[0] = 0;
	if (robu::ipc_raw(0, 0, robu::IPC_FLAG_SYS_INFO, &m, nullptr) != robu::IPC_ERR_NONE) {
		errno = EIO;
		return -1;
	}
	memset(info, 0, sizeof(*info));
	info->totalram = m.word[0];
	info->freeram = m.word[1];
	info->mem_unit = 4096;
	return 0;
}

extern "C" int get_nprocs(void) {
	return 1;
}

extern "C" int get_nprocs_conf(void) {
	return 1;
}
