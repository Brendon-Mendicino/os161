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
	area->area_flags =
		AS_AREA_EXEC * exec |
		AS_AREA_READ * read |
		AS_AREA_WRITE * write;

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

	as->asp_vbase1 = 0;
	as->asp_pbase1 = 0;
	as->asp_npages1 = 0;
	as->asp_vbase2 = 0;
	as->asp_pbase2 = 0;
	as->asp_npages2 = 0;
	as->asp_stackpbase = 0;
	as->asp_nstackpages = AS_STACKPAGES;

	retval = pt_init(&as->pt);
	if (retval)
		return NULL;

	INIT_LIST_HEAD(&as->addrspace_area_list);

	return as;
}

static void
as_bad_prepare_load(struct addrspace *as)
{
	KASSERT(as != NULL);

	if (as->asp_pbase1)
		free_kpages(as->asp_pbase1);

	if (as->asp_pbase2)
		free_kpages(as->asp_pbase2);

	if (as->asp_stackpbase)
		free_kpages(as->asp_stackpbase);
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

	new->asp_vbase1 = old->asp_vbase1;
	new->asp_npages1 = old->asp_npages1;
	new->asp_vbase2 = old->asp_vbase2;
	new->asp_npages2 = old->asp_npages2;
	new->asp_nstackpages = old->asp_nstackpages;

	/* (Mis)use asp_prepare_load to allocate some physical memory. */
	retval = as_prepare_load(new);
	if (retval) {
		/* 
		 * we cant use asp_destroy because its
		 * not knows if the pbase where allocated
		 * correctly.
		 */
		as_bad_prepare_load(new);
		return ENOMEM;
	}

	KASSERT(new->asp_pbase1 != 0);
	KASSERT(new->asp_pbase2 != 0);
	KASSERT(new->asp_stackpbase != 0);

	memmove((void *)new->asp_pbase1,
		(const void *)old->asp_pbase1,
		old->asp_npages1*PAGE_SIZE);

	memmove((void *)new->asp_pbase2,
		(const void *)old->asp_pbase2,
		old->asp_npages2*PAGE_SIZE);

	memmove((void *)new->asp_stackpbase,
		(const void *)old->asp_stackpbase,
		old->asp_nstackpages*PAGE_SIZE);

	*ret = new;
	return 0;
}

void
as_destroy(struct addrspace *as)
{

	/* as_pbase2 could be 0 */
	// KASSERT(as->asp_pbase1 != 0);
	KASSERT(as->asp_stackpbase != 0);

	/* TODO: modify in the future */
	// free_kpages(as->asp_pbase1);
	free_kpages(as->asp_stackpbase);

	pt_destroy(&as->pt);

	// if (as->asp_npages2 > 0)
	// 	free_kpages(as->asp_pbase2);

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

static void
as_zero_region(vaddr_t addr, size_t npages)
{
	bzero((void *)addr, npages);
}

int
as_prepare_load(struct addrspace *as)
{
	int retval;
	struct addrspace_area *area;

	KASSERT(as->asp_pbase1 == 0);
	KASSERT(as->asp_pbase2 == 0);
	KASSERT(as->asp_stackpbase == 0);


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

	as->asp_stackpbase = alloc_kpages(as->asp_nstackpages);
	if (as->asp_stackpbase == 0) {
		return ENOMEM;
	}

	// as_zero_region(as->asp_pbase1, as->asp_npages1);
	// as_zero_region(as->asp_pbase2, as->asp_npages2);
	as_zero_region(as->asp_stackpbase, as->asp_nstackpages);

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
	KASSERT(as->asp_stackpbase != 0);

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
	KASSERT(as->asp_nstackpages > 0);
	KASSERT(as->asp_stackpbase != 0);

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