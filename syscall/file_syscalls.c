#include <syscall.h>
#include <lib.h>
#include <kern/unistd.h>

ssize_t sys_write(int fd, const void *buf, size_t nbytes)
{
    ssize_t wrote_bytes = 0;

    // to be implemented
    if (fd != STDOUT_FILENO && fd != STDERR_FILENO)
    {
        kprintf("Error: writing to a file!\n");
        return -1;
    }

    for (size_t i = 0; i < nbytes; i++) {
        putch(((char *)buf)[i]);
        wrote_bytes++;
    }

    return wrote_bytes;
}
