#include <syscall.h>
#include <lib.h>
#include <addrspace.h>
#include <proc.h>
#include <thread.h>
#include <current.h>
#include <list.h>
#include <thread.h>
#include <copyinout.h>
#include <kern/wait.h>
#include <kern/errno.h>

static bool check_options(int options) 
{
    return (options == 0) || ((options & ~(WNOHANG | WUNTRACED)) == 0);
}

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

int sys_waitpid(pid_t pid, userptr_t wstatus, int options, pid_t *exit_pid)
{
    int retval;
    int sys_wstatus;

    KASSERT(exit_pid != NULL);

    if (!check_options(options))
        return EINVAL;

    if (options != 0)
        panic("sys_waitpid: options|wstatus not implemented yet\n");

    retval = proc_check_zombie(pid, &sys_wstatus, options, curproc);
    if (retval)
        return retval;

    if (wstatus != NULL)
        retval = copyout(&sys_wstatus, wstatus, sizeof(int));

    // TODO: cambiare
    *exit_pid = pid;

    return retval;
}

pid_t sys_getpid(void)
{
    return curproc->pid;
}