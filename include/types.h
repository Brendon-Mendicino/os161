/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2007, 2008
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

#ifndef _TYPES_H_
#define _TYPES_H_

/*
 * Master kernel header file.
 *
 * The model for the include files in the kernel is as follows:
 *
 *     - Every source file includes this file, <types.h>, first.
 *
 *     - Every other header file may assume this file has been
 *       included, but should explicitly include any other headers it
 *       uses to compile.
 *
 *     - Some exceptions to the previous rules exist among the headers
 *       exported to userland; those files should be included in the
 *       kernel only indirectly via other, non-exported, headers, as
 *       described in comments therein.
 *
 *     - Every source or header file should include each file it
 *       directly uses, even if that header is included via some other
 *       header. This helps to prevent build failures when unrelated
 *       dependencies are changed around.
 *
 *     - As a matter of convention, the ordering of include files in
 *       the base system is in order of subsystem dependence. That is,
 *       lower-level code like <spinlock.h> should come before
 *       higher-level code like <addrspace.h> or <vfs.h>. This
 *       convention helps one to keep keep track of (and learn) the
 *       organization of the system.
 *
 *       The general ordering is as follows:
 *           1. <types.h>
 *           2. Kernel ABI definitions, e.g. <kern/errno.h>.
 *           3. Support code: <lib.h>, arrays, queues, etc.
 *           4. Low-level code: locks, trapframes, etc.
 *           5. Kernel subsystems: threads, VM, VFS, etc.
 *           6. System call layer, e.g. <elf.h>, <syscall.h>.
 *
 *       Subsystem-private headers (the only extant example is
 *       switchframe.h) and then kernel option headers generated by
 *       config come last.
 *
 *       There is no one perfect ordering, because the kernel is not
 *       composed of perfectly nested layers. But for the most part
 *       this principle produces a workable result.
 */


/* Get types visible to userland, both MI and MD. */
#include <kern/types.h>

/* Get machine-dependent types not visible to userland. */
#include <machine/types.h>

/*
 * Define userptr_t as a pointer to a one-byte struct, so it won't mix
 * with other pointers.
 */

struct __userptr { char _dummy; };
typedef struct __userptr *userptr_t;
typedef const struct __userptr *const_userptr_t;

/*
 * Proper (non-underscore) names for the types that are exposed to
 * userland.
 */

/* machine-dependent from <kern/machine/types.h>... */
typedef __i8 int8_t;
typedef __i16 int16_t;
typedef __i32 int32_t;
typedef __i64 int64_t;
typedef __u8 uint8_t;
typedef __u16 uint16_t;
typedef __u32 uint32_t;
typedef __u64 uint64_t;
typedef __size_t size_t;
typedef __ssize_t ssize_t;
typedef __intptr_t intptr_t;
typedef __uintptr_t uintptr_t;
typedef __ptrdiff_t ptrdiff_t;

/* ...and machine-independent from <kern/types.h>. */
typedef __blkcnt_t blkcnt_t;
typedef __blksize_t blksize_t;
typedef __daddr_t daddr_t;
typedef __dev_t dev_t;
typedef __fsid_t fsid_t;
typedef __gid_t gid_t;
typedef __in_addr_t in_addr_t;
typedef __in_port_t in_port_t;
typedef __ino_t ino_t;
typedef __mode_t mode_t;
typedef __nlink_t nlink_t;
typedef __off_t off_t;
typedef __pid_t pid_t;
typedef __rlim_t rlim_t;
typedef __sa_family_t sa_family_t;
typedef __time_t time_t;
typedef __uid_t uid_t;

typedef __nfds_t nfds_t;
typedef __socklen_t socklen_t;

/*
 * Number of bits per byte.
 */

#define CHAR_BIT __CHAR_BIT

/*
 * Null pointer.
 */

#define NULL ((void *)0)

/*
 * Boolean.
 */
typedef _Bool bool;
#define true  1
#define false 0

/*
 * Circular doubly linked list
 */
struct list_head {
	struct list_head *next, *prev;
};

/*
 * Double linked lists with a single pointer list head.
 * Mostly useful for hash tables where the two pointer list head is
 * too wasteful.
 * You lose the ability to access the tail in O(1).
 */
struct hlist_head {
	struct hlist_node *first;
};

struct hlist_node {
	struct hlist_node *next, **pprev;
};


#endif /* _TYPES_H_ */
