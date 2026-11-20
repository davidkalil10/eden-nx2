#pragma once

#ifdef __SWITCH2__
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _STRUCT_IOVEC_DEFINED
#define _STRUCT_IOVEC_DEFINED
struct iovec {
    void* iov_base;
    size_t iov_len;
};
#endif

#ifdef __cplusplus
}
#endif
#endif
