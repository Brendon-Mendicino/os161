/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
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

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace_types.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <spl.h>
#include <pt.h>
#include <copyinout.h>
#include <machine/tlb.h>

static struct addrspace_area *
as_create_area(vaddr_t start, vaddr_t end, bool read, bool write, bool exec)
{
	struct  addrspace_area *area;

	KASSERT(start < end);

	area = kmalloc(sizeof(struct addrspace_area));
	if (!area)
		return NULL;

	area->area_start = start;
	area->area_end = end;
	/* we use AS_AREA_MAY_WRITE for COW pages */
	area->area_flags =
		exec * AS_AREA_EXEC |
		read * (AS_AREA_WRITE | AS_AREA_MAY_WRITE) |
		write * AS_AREA_WRITE;

	INIT_LIST_HEAD(&area->next_area);

	return area;
}


static int
as_add_area(struct addrspace *as, struct addrspace_area *area)
{
	struct addrspace_area *entry;

	KASSERT(as != NULL);
	KASSERT(area != NULL);

	/* check that the interval is unique */
	as_for_each_area(as, entry) {
		if (area->area_start < entry->area_end && area->area_end > entry->area_start)
			return EINVAL;
	}

	list_add_tail(&area->next_area, &as->addrspace_area_list);

	return 0;
}

static int
as_copy_area(struct addrspace *new, struct addrspace *old)
{
	struct addrspace_area *old_area, *new_area;

	KASSERT(list_empty(&new->addrspace_area_list));

	as_for_each_area(old, old_area) {
		new_area = kmalloc(sizeof(struct addrspace_area));
		if (!new_area)
			return ENOMEM;

		memmove(new_area, old_area, sizeof(struct addrspace_area));

		/* adjust the list pointer */
		INIT_LIST_HEAD(&new_area->next_area);

		/* add it to new list addrspace */
		as_add_area(new, new_area);
	}

	return 0;
}

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

/* temp variable */
#define AS_STACKPAGES 16

struct addrspace *
as_create(void)
{
	int retval;
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}

	retval = pt_init(&as->pt);
	if (retval)
		return NULL;

	INIT_LIST_HEAD(&as->addrspace_area_list);

	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new;
	int retval;

	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}

	new->start_arg = old->start_arg;
	new->end_arg = old->end_arg;
	new->start_stack = old->start_stack;
	new->end_stack = old->end_stack;

	retval = as_copy_area(new, old);
	if (retval)
		goto bad_as_copy_area_cleanup;

	retval = pt_copy(&new->pt, &old->pt);
	if (retval)
		goto bad_pt_copy_cleanup;


	*ret = new;
	return 0;

bad_pt_copy_cleanup:
	kprintf("Remember to handle COW copy of the old page!\n");

bad_as_copy_area_cleanup:
	as_destroy(new);
	return retval;
}

void
as_destroy(struct addrspace *as)
{
	struct addrspace_area *area, *temp;

	// TODO: refactor
	as_for_each_area_safe(as, area, temp) {
		kfree(area);
	}

	pt_destroy(&as->pt);

	kfree(as);
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
		 int readable, int writeable, int executable)
{
	int retval;
	struct addrspace_area *area;

	area = as_create_area(vaddr,
			vaddr + memsize,
			readable ? true : false,
			writeable ? true : false,
			executable ? true : false);
	if (!area)
		return ENOMEM;

	retval = as_add_area(as, area);
	if (retval)
		return retval;

	return 0;
}

int
as_prepare_load(struct addrspace *as)
{
	int retval;
	struct addrspace_area *area;

	as_for_each_area(as, area) {
		retval = pt_alloc_page_range(&as->pt,
				area->area_start,
				area->area_end,
				(struct pt_page_flags){
					.page_rw = area->area_flags & AS_AREA_READ ? true : false,
					.page_pwt = false,
				});

		if (retval)
			return retval;
	}

	// TODO: use start_stack, end_stack.
	/* alloc stack range */
	retval = pt_alloc_page_range(&as->pt,
		USERSTACK - AS_STACKPAGES * PAGE_SIZE,
		USERSTACK,
		(struct pt_page_flags){ .page_rw = true, .page_pwt = false });
	if (retval)
		return retval;

	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
#if OPT_ARGS
	KASSERT(as != NULL);
	KASSERT(as->start_arg != 0);

	/* start_arg must be 8byte aligned */
	*stackptr = as->start_arg;

	return 0;
#else // OPT_ARGS
	(void)as;
	KASSERT(as->asp_stackpbase != 0);

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
#endif // OPT_ARGS
}


#if OPT_ARGS
/**
 * @brief sets up the user space that contains the args of the program.
 * The args space is confined between the `USER_TOP` and the begenning of the
 * user stack.
 * 
 * @param as 
 * @param argc 
 * @param argv 
 * @param uargv 
 * @return error code
 */
int as_define_args(struct addrspace *as, int argc, char **argv, userptr_t *uargv)
{
	int i;
	size_t arg_map_size = 0;
	size_t offset = 0;
	char **user_argv;

	KASSERT(as != NULL);

	/*
	 * When allocating the space for the args
	 * we need two things to place in memory:
	 * - the array of pointers to the args
	 * - the actual strings for the args
	 */

	/* we need to add one beacuse the vec is NULL-terminated */
	arg_map_size += (argc + 1) * sizeof(char *);

	for (i = 0; i < argc; i++) {
		arg_map_size += strlen(argv[i]) + 1;
	}

	/* stack pointer must be 8byte aligned */

	/* last aligned address is not usable */
	arg_map_size = ROUNDUP(arg_map_size, 8) + 8;

	as->start_arg = USERSPACETOP - arg_map_size;
	/* end is not inclusive */
	as->end_arg = as->start_arg + arg_map_size;

	KASSERT(as->start_arg < as->end_arg);


	/* create the strcture to be copyied in userspace */
	user_argv = kmalloc((argc + 1) * sizeof(char **));
	if (!user_argv)
		return ENOMEM;

	/* setup the args array */
	user_argv[0] = (char *)as->start_arg + (argc + 1) * sizeof(char **);
	for (i = 1; i < argc; i++) {
		user_argv[i] = (char *)user_argv[i-1] + strlen(argv[i-1]) + 1;
	}
	user_argv[i] = NULL;
	
	/* copy the args array to the user space */
	copyout(user_argv, (userptr_t)as->start_arg, (argc + 1) * sizeof(char **));
	kfree(user_argv);


	offset = (argc + 1) * sizeof(char **);

	/* copy the args to the user space */
	for (i = 0; i < argc; i++) {
		KASSERT(as->start_arg + offset < USERSPACETOP);

		copyoutstr(argv[i], (userptr_t)as->start_arg + offset, strlen(argv[i]) + 1, NULL);

		offset += strlen(argv[i]) + 1;
	}

	/* set the user argv pointer */
	*uargv = (userptr_t)as->start_arg;

	return 0;
}
#endif // OPT_ARGS