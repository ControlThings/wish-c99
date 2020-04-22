#include <stddef.h>
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>

#ifdef _WIN32
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wincrypt.h>
#include "helper.h"
#else
#include <sys/select.h>
#endif

#include "port_select.h"

/* This variable holds the largest socket fd + 1. It must be
 * updated every time new fd is added to either of the sets */
static int max_fd;

static void update_max_fd(int fd) {
    if (fd >= max_fd) {
        max_fd = fd + 1;
    }
}

/* The file descriptors to be polled for reading */
fd_set rfds;
/* The file descriptors to be polled for writing */
fd_set wfds;

/** 
 * Reset the file descriptor sets. 
 * This function is to be called before the first call to port_select(), and after processing the results of a call to port_select() 
 */
void port_select_reset(void) {
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    max_fd = 0;
}

void port_select_fd_set_readable(int fd) {
    FD_SET(fd, &rfds);
    update_max_fd(fd);
}

void port_select_fd_set_writable(int fd) {
    FD_SET(fd, &wfds);
    update_max_fd(fd);
}

int port_select(void) {
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    
    int select_ret =  select(max_fd, &rfds, &wfds, NULL, &tv); /* Note: exceptfds is NULL, because we do not expect to handle TCP out-of-band data from the sockets */

    if (select_ret == -1) {
        if (errno == EBADF) {
            /* Knowing max_fd's value could be interesing since select(2) return EBADF if an fd larger than FD_SETSIZE was included in any of the fd sets */
            fprintf(stderr, "select(2) returned EBADF, current max_fd is %i\n", max_fd);
        }
    }
    return select_ret;
}

/** 
 * Test if fd is readable (after select)
 * 
 */
bool port_select_fd_is_readable(int fd) {
    return FD_ISSET(fd, &rfds);
}

/**
 * test if fd is writable after select
 * @param fd
 */
bool port_select_fd_is_writable(int fd) {
    return FD_ISSET(fd, &wfds);
}

