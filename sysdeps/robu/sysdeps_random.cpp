#include <errno.h>
#include <mlibc/all-sysdeps.hpp>
#include <sys/random.h>

ssize_t getrandom(void *buffer, size_t length, unsigned int flags) {
	if (flags & ~(GRND_RANDOM | GRND_NONBLOCK | GRND_INSECURE)) {
		errno = EINVAL;
		return -1;
	}
	if (int e = mlibc::sysdep_or_enosys<GetEntropy>(buffer, length); e) {
		errno = e;
		return -1;
	}
	return (ssize_t)length;
}
