#pragma once

#include_next <sys/resource.h>

#ifdef __SWITCH2__
#include <stdint.h>
#include <stddef.h>

#ifndef RLIM_INFINITY
typedef uint64_t rlim_t;
#define RLIM_INFINITY ((rlim_t)-1)

struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};

#ifndef RLIMIT_STACK
#define RLIMIT_STACK 3
#endif
#ifndef RLIMIT_NOFILE
#define RLIMIT_NOFILE 7
#endif

static inline int getrlimit(int resource, struct rlimit* rlim) {
    if (!rlim) {
        return -1;
    }
    if (resource == RLIMIT_STACK) {
        rlim->rlim_cur = 8u * 1024u * 1024u;
        rlim->rlim_max = 8u * 1024u * 1024u;
        return 0;
    }
    rlim->rlim_cur = 1024;
    rlim->rlim_max = 1024;
    return 0;
}

static inline int setrlimit(int resource, const struct rlimit* rlim) {
    (void)resource;
    (void)rlim;
    return 0;
}
#endif
#endif
