#include <syscall.h>
#include <lib.h>
#include <addrspace.h>
#include <proc.h>
#include <thread.h>


void _exit(int status)
{
    struct addrspace *curr_as;
    
    kprintf("Exit status: %d\n", status);

    curr_as = proc_getas();
    as_destroy(curr_as);

    thread_exit();

    panic("returned from thread_exit");
}