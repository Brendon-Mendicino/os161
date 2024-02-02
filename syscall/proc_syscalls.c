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
    int retval = 0;
    pid_t ret_pid;
    int sys_wstatus;
    struct proc *curr = curproc;

    KASSERT(exit_pid != NULL);

    if (options & WUNTRACED)
        panic("sys_waitpid: WUNTRACED not implemented yet\n");

    if (!check_options(options))
        return EINVAL;

	struct proc *child = proc_get_child(pid, curr);
	if (!child)
		return ESRCH;

    ret_pid = proc_check_zombie(child, &sys_wstatus, options, curr);
    // TODO: cambiare
    *exit_pid = ret_pid;

    if (wstatus != NULL)
        retval = copyout(&sys_wstatus, wstatus, sizeof(int));

    return retval;
}

pid_t sys_getpid(void)
{
    return curproc->pid;
}