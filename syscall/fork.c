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
    // TODO: move this when implementing TLB fault handling
    as_activate();
    enter_forked_process(tf);
}

int sys_fork(pid_t *pid, struct trapframe *tf)
{
    struct trapframe *tf_copy;
    struct proc *new;

    new = proc_copy();
    if (!new)
        return ENOMEM;

    tf_copy = kmalloc(sizeof(struct trapframe));
    if (!tf_copy)
        goto fork_out;

    memmove(tf_copy, tf, sizeof(struct trapframe));
    
    thread_fork("sys_fork", 
        new, 
        (void (*)(void *, unsigned long))prepare_forked_process, 
        (void *)tf_copy, 
        0);

    *pid = new->pid;

    return 0;

fork_out:
    proc_destroy(new);
    return ENOMEM;
}