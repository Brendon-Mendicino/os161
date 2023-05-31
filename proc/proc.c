/*
 * Copyright (c) 2013
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Process support.
 *
 * There is (intentionally) not much here; you will need to add stuff
 * and maybe change around what's already present.
 *
 * p_lock is intended to be held when manipulating the pointers in the
 * proc structure, not while doing any significant work with the
 * things they point to. Rearrange this (and/or change it to be a
 * regular lock) as needed.
 *
 * Unless you're implementing multithreaded user processes, the only
 * process that will have more than one thread is the kernel process.
 */

#include <kern/errno.h>
#include <kern/wait.h>
#include <list.h>
#include <hashtable.h>
#include <types.h>
#include <spl.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>



#define PROC_RUNNING   0x00000000
#define PROC_NEW       0x00000001
#define PROC_ZOMBIE    0x00000002



/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc kproc = {
	.p_name           = (char *)"[kernel]",
	.p_lock           = SPINLOCK_INITIALIZER,
	.p_numthreads     = 0,
	.p_addrspace      = NULL,
	.p_cwd            = NULL,
#if OPT_SYSCALLS   
	.wait_cv          = NULL,
	.wait_lock        = NULL,
	.wait_sem         = NULL,
    .state            = PROC_RUNNING,
    .exit_state       = PROC_RUNNING,
    .children         = LIST_HEAD_INIT(kproc.children),
	.siblings         = LIST_HEAD_INIT(kproc.siblings),
	.parent           = NULL,
    .pid              = 0,
	.pid_link         = { .next = NULL, .pprev = NULL },
#endif // OPT_SYSCALLS
};

#if OPT_SYSCALLS
/**
 * Construct a new define hashtable object
 */
DEFINE_HASHTABLE(proc_table, 5);

/*
 * This value is locked by
 * p_lock of kproc.
 */
static pid_t max_pid = 0;

/*
 * Removes a child from the children
 * list of the parent, the list is locked
 * with the parent p_lock.
 */
static inline
void del_child_proc(struct proc *child)
{
	KASSERT(child != NULL);
	KASSERT(child->parent != NULL);

	spinlock_acquire(&child->parent->p_lock);
	list_del(&child->siblings);
	spinlock_release(&child->parent->p_lock);
}

static inline
void add_new_child_proc(struct proc *new, struct proc *head)
{
	spinlock_acquire(&head->p_lock);
	list_add_tail(&new->siblings, &head->children);
	spinlock_release(&head->p_lock);
}

void proc_make_zombie(int exit_code, struct proc *proc)
{
	lock_acquire(proc->wait_lock);
	proc->exit_state = PROC_ZOMBIE;
	proc->exit_code = exit_code;
	cv_broadcast(proc->wait_cv, proc->wait_lock);
	lock_release(proc->wait_lock);

	V(proc->wait_sem);
}

static struct proc *
proc_get_child(pid_t pid, struct proc *proc)
{
	struct proc *found = NULL;
	struct proc *child;

	spinlock_acquire(&kproc.p_lock);
	hash_for_each_possible(proc_table, child, pid_link, pid) {
		if (child->pid != pid || child->parent != proc)
			continue;

		found = child;
		break;
	}
	spinlock_release(&kproc.p_lock);

	return found;
}

int proc_check_zombie(pid_t pid, int *wstatus, int options, struct proc *proc)
{
	struct proc *child;
	(void)options;

	child = proc_get_child(pid, proc);

	if (!child)
		return ESRCH;

	lock_acquire(child->wait_lock);
	while (child->exit_state != PROC_ZOMBIE) {
		cv_wait(child->wait_cv, child->wait_lock);
	}
	lock_release(child->wait_lock);

	P(child->wait_sem);

	if (wstatus)
	{
		*wstatus = _MKWVAL(child->exit_code);
		*wstatus = _MKWAIT_EXIT(*wstatus);
	}

	// TODO: move this
    proc_destroy(child);

	return 0;
}

static inline void free_pid(struct proc *proc)
{
	spinlock_acquire(&kproc.p_lock);
	hash_del(&proc->pid_link);
	spinlock_release(&kproc.p_lock);
}

/**
 * Get the next greater pid.
*/
static inline pid_t alloc_pid(void) 
{
    pid_t pid;
    
	spinlock_acquire(&kproc.p_lock);
	max_pid += 1;
	pid = max_pid;
	spinlock_release(&kproc.p_lock);

    if (pid >= PID_MAX || pid < PID_MIN)
        pid = PID_MIN;

    return pid;
}

/**
 * Insert a new proc to the list of processes
 * and to the PID table.
 */
static inline void insert_proc(struct proc *new)
{
	spinlock_acquire(&kproc.p_lock);
	hash_add(proc_table, &new->pid_link, new->pid);
	spinlock_release(&kproc.p_lock);
}
#endif // OPT_SYSCALLS

/*
 * Create a proc structure.
 * This function only initialized the
 * fields, the remainings configuration
 * needs to be the caller.
 */
static
struct proc *
proc_create(const char *name)
{
	struct proc *proc;

	proc = kmalloc(sizeof(*proc));
	if (proc == NULL)
		return NULL;

	proc->p_name = kstrdup(name);
	if (proc->p_name == NULL)
		goto create_out;

#if OPT_SYSCALLS
	proc->wait_cv = cv_create("wait_cv");
	if (!proc->wait_cv)
		goto bad_create_cleanup_name;

	proc->wait_lock = lock_create("wait_lock");
	if (!proc->wait_lock)
		goto bad_create_cleanup_cv;

	proc->wait_sem = sem_create("wait_sem", 0);
	if (!proc->wait_sem)
		goto bad_create_cleanup_lock;

	/*
	 * The new process is not running yet, this
	 * is why there is a PROC_NEW state, the
	 * caller will handle when the
	 * process will need to run.
	 */
	proc->state = PROC_NEW;
	proc->exit_state = PROC_NEW;
	proc->exit_code = 0;

	proc->parent = curproc;

	INIT_LIST_HEAD(&proc->children);
	INIT_LIST_HEAD(&proc->siblings);
#endif // OPT_SYSCALLS

	proc->p_numthreads = 0;
	spinlock_init(&proc->p_lock);

	/* VM fields */
	proc->p_addrspace = NULL;

	/* VFS fields */
	proc->p_cwd = NULL;

	return proc;

#if OPT_SYSCALLS
bad_create_cleanup_lock:
	lock_destroy(proc->wait_lock);
bad_create_cleanup_cv:
	cv_destroy(proc->wait_cv);
#endif // OPT_SYSCALLS
bad_create_cleanup_name:
	kfree(proc->p_name);
create_out:
	kfree(proc);
	return NULL;
}

/*
 * Destroy a proc structure.
 *
 * Note: nothing currently calls this. Your wait/exit code will
 * probably want to do so.
 */
void
proc_destroy(struct proc *proc)
{
	/*
	 * You probably want to destroy and null out much of the
	 * process (particularly the address space) at exit time if
	 * your wait/exit design calls for the process structure to
	 * hang around beyond process exit. Some wait/exit designs
	 * do, some don't.
	 */

	KASSERT(proc != NULL);
	KASSERT(proc != &kproc);

	/*
	 * We don't take p_lock in here because we must have the only
	 * reference to this structure. (Otherwise it would be
	 * incorrect to destroy it.)
	 */

	/* VFS fields */
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}

	/* VM fields */
	if (proc->p_addrspace) {
		/*
		 * If p is the current process, remove it safely from
		 * p_addrspace before destroying it. This makes sure
		 * we don't try to activate the address space while
		 * it's being destroyed.
		 *
		 * Also explicitly deactivate, because setting the
		 * address space to NULL won't necessarily do that.
		 *
		 * (When the address space is NULL, it means the
		 * process is kernel-only; in that case it is normally
		 * ok if the MMU and MMU- related data structures
		 * still refer to the address space of the last
		 * process that had one. Then you save work if that
		 * process is the next one to run, which isn't
		 * uncommon. However, here we're going to destroy the
		 * address space, so we need to make sure that nothing
		 * in the VM system still refers to it.)
		 *
		 * The call to as_deactivate() must come after we
		 * clear the address space, or a timer interrupt might
		 * reactivate the old address space again behind our
		 * back.
		 *
		 * If p is not the current process, still remove it
		 * from p_addrspace before destroying it as a
		 * precaution. Note that if p is not the current
		 * process, in order to be here p must either have
		 * never run (e.g. cleaning up after fork failed) or
		 * have finished running and exited. It is quite
		 * incorrect to destroy the proc structure of some
		 * random other process while it's still running...
		 */
		struct addrspace *as;

		if (proc == curproc) {
			as = proc_setas(NULL);
			as_deactivate();
		}
		else {
			as = proc->p_addrspace;
			proc->p_addrspace = NULL;
		}
		as_destroy(as);
	}

	KASSERT(proc->p_numthreads == 0);
	spinlock_cleanup(&proc->p_lock);

#ifdef OPT_SYSCALLS
	cv_destroy(proc->wait_cv);
	lock_destroy(proc->wait_lock);
	sem_destroy(proc->wait_sem);

	/* remove from proc_table */
	free_pid(proc);

	del_child_proc(proc);
#endif // OPT_SYSCALLS

	kfree(proc->p_name);
	kfree(proc);
}

/*
 * Create the process structure for the kernel.
 */
void
proc_bootstrap(void)
{
	/*
	 * kproc is statically initialized
	 * we only need to add it to PID table
	 */

#ifdef OPT_SYSCALLS
	// TODO: make synch static
	kproc.wait_cv = cv_create("wait_cv");
	kproc.wait_lock = lock_create("wait_lock");
	kproc.wait_sem = sem_create("wait_sem", 0);
	hash_add(proc_table, &kproc.pid_link, kproc.pid);
#endif // OPT_SYSCALLS
}

/*
 * Create a fresh proc for use by runprogram.
 *
 * It will have no address space and will inherit the current
 * process's (that is, the kernel menu's) current directory.
 */
struct proc *
proc_create_runprogram(const char *name)
{
	struct proc *newproc;

	newproc = proc_create(name);
	if (newproc == NULL) {
		return NULL;
	}

	/* VM fields */

	newproc->p_addrspace = NULL;

	/* VFS fields */

	/*
	 * Lock the current process to copy its current directory.
	 * (We don't need to lock the new process, though, as we have
	 * the only reference to it.)
	 */
	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		newproc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);

#if OPT_SYSCALLS
	newproc->pid = alloc_pid();
	insert_proc(newproc);

	add_new_child_proc(newproc, curproc);
#endif // OPT_SYSCALLS


	return newproc;
}

/*
 * Actual fork implementation.
 */
struct proc *
proc_copy(void)
{
	struct proc *new_proc;
	int err;

	KASSERT(curproc != NULL);

	new_proc = proc_create("proc_copy");
	if (!new_proc)
		return NULL;

	err = as_copy(curproc->p_addrspace, &new_proc->p_addrspace);
	if (err)
		goto fork_out;

#ifdef OPT_SYSCALLS
	new_proc->parent = curproc;

	// TODO: fix this
	new_proc->pid = alloc_pid();
	insert_proc(new_proc);

	add_new_child_proc(new_proc, curproc);
#endif // OPT_SYSCALLS

	/*
	 * Lock the current process to copy its current directory.
	 * (We don't need to lock the new process, though, as we have
	 * the only reference to it.)
	 */
	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		new_proc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);

	return new_proc;

fork_out:
	kfree(new_proc);
	return NULL;
}

/*
 * Add a thread to a process. Either the thread or the process might
 * or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
int
proc_addthread(struct proc *proc, struct thread *t)
{
	int spl;

	KASSERT(t->t_proc == NULL);

	spinlock_acquire(&proc->p_lock);
	proc->p_numthreads++;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = proc;
	splx(spl);

	return 0;
}

/*
 * Remove a thread from its process. Either the thread or the process
 * might or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
void
proc_remthread(struct thread *t)
{
	struct proc *proc;
	int spl;

	proc = t->t_proc;
	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	KASSERT(proc->p_numthreads > 0);
	proc->p_numthreads--;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = NULL;
	splx(spl);
}

/*
 * Fetch the address space of (the current) process.
 *
 * Caution: address spaces aren't refcounted. If you implement
 * multithreaded processes, make sure to set up a refcount scheme or
 * some other method to make this safe. Otherwise the returned address
 * space might disappear under you.
 */
struct addrspace *
proc_getas(void)
{
	struct addrspace *as;
	struct proc *proc = curproc;

	if (proc == NULL) {
		return NULL;
	}

	spinlock_acquire(&proc->p_lock);
	as = proc->p_addrspace;
	spinlock_release(&proc->p_lock);
	return as;
}

/*
 * Change the address space of (the current) process. Return the old
 * one for later restoration or disposal.
 */
struct addrspace *
proc_setas(struct addrspace *newas)
{
	struct addrspace *oldas;
	struct proc *proc = curproc;

	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	oldas = proc->p_addrspace;
	proc->p_addrspace = newas;
	spinlock_release(&proc->p_lock);
	return oldas;
}
