#pragma once

#ifdef __SWITCH2__
#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 108
#endif

struct sockaddr_un {
    sa_family_t sun_family;
    char sun_path[UNIX_PATH_MAX];
};

#ifdef __cplusplus
}
#endif
#endif
