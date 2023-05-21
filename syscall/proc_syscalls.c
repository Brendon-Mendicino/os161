#include <syscall.h>
#include <lib.h>
#include <addrspace.h>
#include <proc.h>
#include <thread.h>
#include <current.h>
#include <list.h>
#include <thread.h>


void sys__exit(int status)
{
    struct proc *proc = curproc;
    
    // TODO: when this proc exits and has children
    // attach them to the init process, that will
    // periodically check for new children to clear
    // resources of

    /*
     * Remove running thread from
     * process, this allows us to call
     * signal on the wait_sem for the current
     * process, this would have not been
     * possible if thread_exit() was called
     */
	proc_remthread(curthread);

    proc_make_zombie(status, proc);

    thread_stop();

    panic("returned from thread_exit");
}

pid_t sys_waitpid(pid_t pid, int *wstatus, int options)
{
    pid_t result;

    if (options != 0)
        panic("sys_waitpid: options|wstatus not implemented yet\n");

    result = proc_check_zombie(pid, wstatus, options, curproc);

    return result;
}

pid_t sys_getpid(void)
{
    return curproc->pid;
}