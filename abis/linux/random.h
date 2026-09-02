#ifndef _ABIBITS_RANDOM_H
#define _ABIBITS_RANDOM_H

#include <mlibc-config.h>

#if !__MLIBC_LINUX_OPTION && !__MLIBC_ROBU_RANDOM_OPTION
#  error "getrandom() requires Linux random ABI support."
#endif

#define GRND_NONBLOCK 0x0001
#define GRND_RANDOM 0x0002
#define GRND_INSECURE 0x0004

#endif /* _ABIBITS_RANDOM_H */
