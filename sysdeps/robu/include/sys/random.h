#ifndef _ROBU_SYS_RANDOM_H
#define _ROBU_SYS_RANDOM_H

#include <abi-bits/random.h>
#include <bits/size_t.h>
#include <bits/ssize_t.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __MLIBC_ABI_ONLY

ssize_t getrandom(void *buffer, size_t length, unsigned int flags);

#endif

#ifdef __cplusplus
}
#endif

#endif
