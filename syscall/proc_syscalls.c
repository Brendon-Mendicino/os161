#include <syscall.h>
#include <lib.h>


void sys_exit(int status)
{
    kprintf("Exit status: %d\n", status);
}