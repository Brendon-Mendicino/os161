#include <syscall.h>
#include <lib.h>
#include <kern/unistd.h>

ssize_t sys_write(int fd, const void *buf, size_t nbyte)
{
    ssize_t wrote_bytes = 0;

    // to be implemented
    if (fd != STDOUT_FILENO && fd != STDERR_FILENO)
    {
        kprintf("Error: writing to a file!\n");
        return -1;
    }

    for (size_t i = 0; i < nbyte; i++)
    {
        putch(((uint8_t *)buf)[i]);
        wrote_bytes++;
    }

    return wrote_bytes;
}

ssize_t sys_read(int fd, const void *buf, size_t nbyte)
{
    ssize_t read_bytes = 0;

    if (fd != STDOUT_FILENO && fd != STDERR_FILENO)
    {
        kprintf("Error: reading from a file!\n");
        return -1;
    }

    for (size_t i = 0; i < nbyte; i++)
    {
        ((uint8_t *)buf)[i] = (uint8_t)getch();
        read_bytes++;
    }

    return read_bytes;
}
