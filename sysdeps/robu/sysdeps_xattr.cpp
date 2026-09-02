#include <errno.h>
#include <mlibc/all-sysdeps.hpp>
#include <sys/xattr.h>

int setxattr(const char *path, const char *name, const void *value, size_t size, int flags) {
	if (int e = mlibc::sysdep_or_enosys<Setxattr>(path, name, value, size, flags); e) {
		errno = e;
		return -1;
	}
	return 0;
}

int lsetxattr(const char *, const char *, const void *, size_t, int) {
	errno = ENOTSUP;
	return -1;
}

int fsetxattr(int fd, const char *name, const void *value, size_t size, int flags) {
	if (int e = mlibc::sysdep_or_enosys<Fsetxattr>(fd, name, value, size, flags); e) {
		errno = e;
		return -1;
	}
	return 0;
}

ssize_t getxattr(const char *path, const char *name, void *value, size_t size) {
	ssize_t nread;
	if (int e = mlibc::sysdep_or_enosys<Getxattr>(path, name, value, size, &nread); e) {
		errno = e;
		return -1;
	}
	return nread;
}

ssize_t lgetxattr(const char *, const char *, void *, size_t) {
	errno = ENOTSUP;
	return -1;
}

ssize_t fgetxattr(int fd, const char *name, void *value, size_t size) {
	ssize_t nread;
	if (int e = mlibc::sysdep_or_enosys<Fgetxattr>(fd, name, value, size, &nread); e) {
		errno = e;
		return -1;
	}
	return nread;
}

ssize_t listxattr(const char *path, char *list, size_t size) {
	ssize_t nread;
	if (int e = mlibc::sysdep_or_enosys<Listxattr>(path, list, size, &nread); e) {
		errno = e;
		return -1;
	}
	return nread;
}

ssize_t llistxattr(const char *, char *, size_t) {
	errno = ENOTSUP;
	return -1;
}

ssize_t flistxattr(int fd, char *list, size_t size) {
	ssize_t nread;
	if (int e = mlibc::sysdep_or_enosys<Flistxattr>(fd, list, size, &nread); e) {
		errno = e;
		return -1;
	}
	return nread;
}

int removexattr(const char *path, const char *name) {
	if (int e = mlibc::sysdep_or_enosys<Removexattr>(path, name); e) {
		errno = e;
		return -1;
	}
	return 0;
}

int lremovexattr(const char *, const char *) {
	errno = ENOTSUP;
	return -1;
}

int fremovexattr(int fd, const char *name) {
	if (int e = mlibc::sysdep_or_enosys<Fremovexattr>(fd, name); e) {
		errno = e;
		return -1;
	}
	return 0;
}
