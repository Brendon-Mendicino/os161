#include <kern/errno.h>
#include <syscall.h>
#include <proc.h>
#include <addrspace.h>
#include <thread.h>
#include <list.h>
#include <types.h>
#include <current.h>
#include <spl.h>
#include <machine/trapframe.h>



static void prepare_forked_process(struct trapframe *tf, unsigned long none) 
{
    (void)none;
    enter_forked_process(tf);
    panic("returned from mips_usermode\n");
}

int sys_fork(pid_t *pid, struct trapframe *tf)
{
    struct trapframe *tf_copy;
    struct proc *new;
    int retval;

    new = proc_copy();
    if (!new)
        return ENOMEM;

    tf_copy = kmalloc(sizeof(struct trapframe));
    if (!tf_copy)
        goto fork_out;

    memmove(tf_copy, tf, sizeof(struct trapframe));
    
    retval = thread_fork("sys_fork", 
        new, 
        (void (*)(void *, unsigned long))prepare_forked_process, 
        (void *)tf_copy, 
        0);
    if (retval)
        goto bad_fork_cleanup_tf;

    *pid = new->pid;

    return 0;

bad_fork_cleanup_tf:
    kfree(tf_copy);
fork_out:
    proc_destroy(new);
    return ENOMEM;
}