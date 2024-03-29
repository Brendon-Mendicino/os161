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

#ifndef _PROC_H_
#define _PROC_H_

/*
 * Definition of a process.
 *
 * Note: curproc is defined by <current.h>.
 */

#include <spinlock.h>
#include <types.h>
#include <limits.h>
#include <synch.h>
#include <file.h>
#include "opt-syscalls.h"
#include "opt-sysfs.h"

typedef enum proc_state_t {
	PROC_NEW,
	PROC_RUNNING,
	PROC_ZOMBIE,
} proc_state_t;

struct addrspace;
struct thread;
struct vnode;

/*
 * Process structure.
 *
 * Note that we only count the number of threads in each process.
 * (And, unless you implement multithreaded user processes, this
 * number will not exceed 1 except in kproc.) If you want to know
 * exactly which threads are in the process, e.g. for debugging, add
 * an array and a sleeplock to protect it. (You can't use a spinlock
 * to protect an array because arrays need to be able to call
 * kmalloc.)
 *
 * You will most likely be adding stuff to this structure, so you may
 * find you need a sleeplock in here for other reasons as well.
 * However, note that p_addrspace must be protected by a spinlock:
 * thread_switch needs to be able to fetch the current address space
 * without sleeping.
 */
struct proc {
	char *p_name;			/* Name of this process */
	struct spinlock p_lock;		/* Lock for this structure */
	unsigned p_numthreads;		/* Number of threads in this process */

	/* VM */
	struct addrspace *p_addrspace;	/* virtual address space */

	/* VFS */
	struct vnode *p_cwd;		/* current working directory */

#if OPT_SYSCALLS
	/* waitpid cv */
	struct cv *wait_cv;
	/* waitpid lock */
	struct lock *wait_lock;
	/*
	 * Used after the the proc becomes zombie,
	 * if it doesn't call this the proces
	 * may be free to early by sys_waitpid
	 */
	struct semaphore *wait_sem;

	proc_state_t state;
	proc_state_t exit_state;
	int exit_code;

	/* 
	 * childred are parent property
	 * they should be locked with parent->p_lock
	 */
	struct list_head children;
	struct list_head siblings;

	/* Recipient of SIGCHLD */
	struct proc *parent;

	pid_t pid;

	/* PID hash table linkage */
	struct hlist_node pid_link;
#endif // OPT_SYSCALLS

#ifdef OPT_SYSFS
	struct file_table *ftable;       /* open file table */
#endif // OPT_SYSFS
};

/* This is the process structure for the kernel and for kernel-only threads. */
extern struct proc kproc;

extern struct proc orphanage;

#define proc_for_each_child(child, temp, parent) \
		list_for_each_entry_safe(child, temp, &parent->children, siblings)

#if OPT_SYSCALLS
extern void proc_make_zombie(int exit_code, struct proc *proc);

extern pid_t proc_check_zombie(struct proc *child, int *wstatus, int options, struct proc *proc);

extern struct proc *proc_get_child(pid_t pid, struct proc *proc);

extern struct proc *proc_copy(void);
#endif

/* Call once during system startup to allocate data structures. */
void proc_bootstrap(void);

extern void kproc_bootstrap(void);

/* Create a fresh process for use by runprogram(). */
struct proc *proc_create_runprogram(const char *name);

/* Destroy a process. */
void proc_destroy(struct proc *proc);

/* Attach a thread to a process. Must not already have a process. */
int proc_addthread(struct proc *proc, struct thread *t);

/* Detach a thread from its process. */
void proc_remthread(struct thread *t);

/* Fetch the address space of the current process. */
struct addrspace *proc_getas(void);

/* Change the address space of the current process, and return the old one. */
struct addrspace *proc_setas(struct addrspace *);

#ifdef OPT_SYSFS
extern int proc_add_new_file(struct proc *proc, struct file *file);

extern int proc_removed_file(struct proc *proc, int fd);

extern struct file *proc_get_file(struct proc *proc, int fd);
#endif // OPT_SYSFS

#endif /* _PROC_H_ */
