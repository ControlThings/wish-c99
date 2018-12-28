#pragma once

void port_select_reset(void);

/** 
 * Set fd to the set of file descriptors to be watched for readability.
 * @param fd the file descriptor
 */
void port_select_fd_set_readable(int fd);

/** 
 * Set fd to the set of file descriptors to be watched for writability.
 * @param fd the file descriptor
 */
void port_select_fd_set_writable(int fd);

/**
 * Perform select, yeilding control the OS until something interesting happens with the watched file descriptors, or at most 100000 µs.
 * @return The return value directly from select(). If greater than 0, something of interest happened, and you can use port_select_fd_is_readable() and port_select_fd_is_writable() to test the filedescriptors.
 * If return value is 0, then a timeout occurred before anything interesting happened. A return value less than 0 indicates an error, and the global errno is set.
 */
int port_select(void);

/** 
 * Test if fd is readable (after select)
 *  @param fd the file descriptor
 */
bool port_select_fd_is_readable(int fd);

/**
 * test if fd is writable after select
 *  @param fd the file descriptor
 */
bool port_select_fd_is_writable(int fd);