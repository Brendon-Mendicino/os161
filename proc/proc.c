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



/*
 * The process for the kernel; this holds all the kernel-only threads.
 * 
 * ftable will be initialized in proc_bootstrap
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
 * Current next greater value of PID among the procs.
 */
static pid_t max_pid = PID_MIN;

/*
 * Lock for pid manipulation:
 * - `max_pid`
 * - `proc_table`
 * 
 */
static struct spinlock pid_lock = SPINLOCK_INITIALIZER;

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
	list_del_init(&child->siblings);
	spinlock_release(&child->parent->p_lock);
}

static inline
void add_new_child_proc(struct proc *new_child, struct proc *parent)
{
	spinlock_acquire(&parent->p_lock);
	list_add_tail(&new_child->siblings, &parent->children);
	spinlock_release(&parent->p_lock);
}

// /**
//  * @brief Set the new parent for a proc. The requirements to
//  * call this funcitons are to already own the p_lock for the
//  * new parent and for the old parent.
//  * 
//  * @param child 
//  * @param parent 
//  */
// static void proc_set_parent(struct proc *child, struct proc *parent)
// {
// 	KASSERT(spinlock_do_i_hold(&parent->p_lock));
// 	KASSERT(spinlock_do_i_hold(&child->parent->p_lock));

// 	spinlock_acquire(&child->p_lock);

// 	/* Parent is owned by child */
// 	child->parent = parent;

// 	/* List owned by current parent */
// 	list_del_init(&child->siblings);

// 	/* List owned by new parent */
// 	list_add_tail(&child->siblings, &parent->children);

// 	spinlock_release(&child->p_lock);
// }

// static struct proc *proc_get_parent(struct proc *proc)
// {
// 	spinlock_acquire(&proc->p_lock);
// 	return proc->parent;
// }

// static void proc_release_parent(struct proc *proc)
// {
// 	KASSERT(spinlock_do_i_hold(&proc->p_lock));
// 	spinlock_release(&proc->p_lock);
// }

// static void proc_wait_one_child(struct proc *proc)
// {

// }

static void proc_orphanize_childeren(struct proc *proc)
{
	// struct proc *child;
	// struct proc *temp;

	// spinlock_acquire(&kproc.p_lock);
	// spinlock_acquire(&proc->p_lock);

	// proc_for_each_child(child, temp, proc) {
	// 	proc_set_parent(child, &kproc);
	// }

	// spinlock_release(&proc->p_lock);
	// spinlock_release(&kproc.p_lock);

	// KASSERT(list_empty(&proc->children));

	spinlock_acquire(&proc->p_lock);
	list_del_init(&proc->children);
	spinlock_release(&proc->p_lock);
}

void proc_make_zombie(int exit_code, struct proc *proc)
{
	proc_orphanize_childeren(proc);

	lock_acquire(proc->wait_lock);
	proc->exit_state = PROC_ZOMBIE;
	proc->exit_code = exit_code;
	cv_broadcast(proc->wait_cv, proc->wait_lock);
	lock_release(proc->wait_lock);

	/**
	 * Without this signal the father can kill the current proc
	 * when this hasn't come out of this funcion yet, thus causing
	 * references to dangling pointers.
	 */
	V(proc->wait_sem);
}

static struct proc *
proc_get_child(pid_t pid, struct proc *proc)
{
	struct proc *found = NULL;
	struct proc *child;

	spinlock_acquire(&pid_lock);
	hash_for_each_possible(proc_table, child, pid_link, pid) {
		if (child->pid != pid || child->parent != proc)
			continue;

		found = child;
		break;
	}
	spinlock_release(&pid_lock);

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

	/**
	 * Without this signal the father can kill the child proc
	 * when it hasn't exited yet, thus causing
	 * references to dangling pointers.
	 */
	P(child->wait_sem);

	if (wstatus) {
		*wstatus = child->exit_code;
	}

	// TODO: move this
    proc_destroy(child);

	return 0;
}

static struct proc *proc_get_from_pid(pid_t pid)
{
	struct proc *proc;

	KASSERT(spinlock_do_i_hold(&pid_lock));

	hash_for_each_possible(proc_table, proc, pid_link, pid) {
		if (proc->pid == pid)
			return proc;
	}

	return NULL;
}

static inline void free_pid(struct proc *proc)
{
	spinlock_acquire(&pid_lock);
	hash_del(&proc->pid_link);
	spinlock_release(&pid_lock);

	proc->pid = -1;
}

/**
 * Get the next greater pid.
*/
static inline pid_t __must_check alloc_pid(void) 
{
    pid_t pid;
	pid_t next_max;
    
	spinlock_acquire(&pid_lock);
	pid = max_pid;

	/* Fail if the pid already exist */
	if (proc_get_from_pid(pid)) {
		pid = -1;
		goto out;
	}

	next_max = pid + 1;
    if (next_max >= PID_MAX || next_max < PID_MIN)
        next_max = PID_MIN;
	max_pid = next_max;

out:
	spinlock_release(&pid_lock);

    return pid;
}

/**
 * Insert a new proc to the list of processes
 * and to the PID table.
 */
static inline void insert_proc(struct proc *new)
{
	spinlock_acquire(&pid_lock);
	hash_add(proc_table, &new->pid_link, new->pid);
	spinlock_release(&pid_lock);
}
#endif // OPT_SYSCALLS

/*
 * This is an internal function that is the
 * mirror of `proc_create`, we need this because there
 * are some cases where the structure inside proc may still
 * not be unitialized, we will take care of that in error
 * handling and in `proc_destroy` which uninitialize
 * everything.
 */
static void
__proc_destroy(struct proc *proc)
{
	KASSERT(proc->p_cwd == NULL);
	KASSERT(proc->p_addrspace == NULL);

	KASSERT(proc->p_numthreads == 0);
	spinlock_cleanup(&proc->p_lock);

#if OPT_SYSCALLS
	KASSERT(proc->pid == -1);

	// TODO: implemet this when the init proc will receive zombie children
	KASSERT(list_empty(&proc->children));
	KASSERT(list_empty(&proc->siblings));

	cv_destroy(proc->wait_cv);
	lock_destroy(proc->wait_lock);
	sem_destroy(proc->wait_sem);
#endif // OPT_SYSCALLS

#if OPT_SYSFS
	/* clear the file left unclosed */
	file_table_clear(proc->ftable);
	file_table_destroy(proc->ftable);
#endif // OPT_SYSFS

	kfree(proc->p_name);
	kfree(proc);
}

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

#if OPT_SYSFS
	proc->ftable = file_table_create();
	if (!proc->ftable)
		goto bad_create_cleanup_sem;
#endif // OPT_SYSFS

	proc->pid = -1;
	INIT_HLIST_NODE(&proc->pid_link);

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
#if OPT_SYSFS
bad_create_cleanup_sem:
	sem_destroy(proc->wait_sem);
#endif // OPT_SYSFS

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

#if OPT_SYSCALLS
	/* remove from proc_table */
	free_pid(proc);

	del_child_proc(proc);
#endif // OPT_SYSCALLS

	__proc_destroy(proc);
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

#if OPT_SYSCALLS
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
	pid_t pid;
	struct proc *newproc;

	newproc = proc_create(name);
	if (newproc == NULL) {
		return NULL;
	}

	/* VM fields */

	newproc->p_addrspace = NULL;

#if OPT_SYSCALLS
	pid = alloc_pid();
	if (pid == -1)
		return NULL;

	newproc->pid = pid;
	insert_proc(newproc);

	add_new_child_proc(newproc, curproc);
#endif // OPT_SYSCALLS

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

#if OPT_SYSFS
	file_table_init(newproc->ftable);
#endif // OPT_SYSFS


	return newproc;
}

/*
 * Actual fork implementation.
 */
struct proc *
proc_copy(void)
{
	struct proc *curr, *new_proc;
	pid_t pid;
	int err;

	KASSERT(curproc != NULL);
	curr = curproc;

	new_proc = proc_create((const char *)curr->p_name);
	if (!new_proc)
		return NULL;

	err = as_copy(curr->p_addrspace, &new_proc->p_addrspace);
	if (err)
		goto fork_out;

#if OPT_SYSCALLS
	new_proc->parent = curr;
	add_new_child_proc(new_proc, curr);

	pid = alloc_pid();
	if (pid == -1)
		goto bad_as_cleanup;

	new_proc->pid = pid;
	insert_proc(new_proc);
#endif // OPT_SYSCALLS

#if OPT_SYSFS
	err = file_table_copy(curr->ftable, new_proc->ftable);
	if (err)
		goto bad_pid_cleanup;
#endif // OPT_SYSFS

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

bad_pid_cleanup:
#if OPT_SYSCALLS
	free_pid(new_proc);
	del_child_proc(new_proc);
#endif // OPT_SYSCALL

bad_as_cleanup:
	as_destroy(new_proc->p_addrspace);

fork_out:
	__proc_destroy(new_proc);

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

#if OPT_SYSFS
/**
 * @brief adds a new file to the file table inside
 * the process
 * 
 * @param proc 
 * @param file 
 * @return int 
 */
int proc_add_new_file(struct proc *proc, struct file *file)
{
	struct file_table *ftable = proc->ftable;
	int fd;

	// TOOD: modify fd assignment
	fd = file_next_fd(ftable);
	file->fd = fd;
	// TODO: aggiungere gestione errore
	file_table_add(file, ftable);

	return fd;
}

/**
 * @brief removed a file from the process file
 * table.
 * 
 * @param proc 
 * @param fd 
 * @return int 
 */
int proc_removed_file(struct proc *proc, int fd)
{
	return file_table_remove(proc->ftable, fd);
}

/**
 * @brief return the file from it's file descriptor
 * 
 * @param fd 
 * @return struct file* return the file it exist
 * otherwise NULL
 */
struct file *proc_get_file(struct proc *proc, int fd)
{
	return file_table_get(proc->ftable, fd);
}
#endif // OPT_SYSFS