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
#include <vm_tlb.h>

static struct addrspace_area *
as_create_area(vaddr_t start, vaddr_t end, area_flags_t flags, area_type_t type)
{
	struct  addrspace_area *area;

	KASSERT(start < end);

	area = kmalloc(sizeof(struct addrspace_area));
	if (!area)
		return NULL;

	area->area_start = start;
	area->area_end = end;
	area->area_flags = flags;
	area->area_type = type;

	INIT_LIST_HEAD(&area->next_area);

	return area;
}

static void
as_destroy_area(struct addrspace_area *as_area)
{
	KASSERT(list_empty(&as_area->next_area));

	kfree(as_area);
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
		/* set AS_AREA_MAY_WRITE for COW mapping */
		old_area->area_flags |= asa_write(old_area) * AS_AREA_MAY_WRITE;

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
	if (as==NULL)
		return NULL;

	as->as_file_lock = lock_create("as_file_lock");
	if (!as->as_file_lock)
		goto out;

	retval = pt_init(&as->pt);
	if (retval)
		goto bad_lock_cleanup;

	INIT_LIST_HEAD(&as->addrspace_area_list);

	as->source_file = NULL;

	/* initialize stack region */
	as->start_stack = 0;
	as->end_stack = 0;

	/* initialize args region */
	as->start_arg = 0;
	as->end_arg = 0;

	return as;

bad_lock_cleanup:
	lock_destroy(as->as_file_lock);

out:
	kfree(as);
	return NULL;
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

	VOP_INCREF(old->source_file);
	new->source_file = old->source_file;

	retval = as_copy_area(new, old);
	if (retval)
		goto bad_as_copy_area_cleanup;

	retval = pt_copy(&new->pt, &old->pt);
	if (retval)
		goto bad_pt_copy_cleanup;

	vm_tlb_flush();
	
	*ret = new;
	return 0;

bad_pt_copy_cleanup:
	// TODO: handle cow copy
	kprintf("Remember to handle COW copy of the old page!\n");

bad_as_copy_area_cleanup:
	as_destroy(new);
	return retval;
}

void
as_destroy(struct addrspace *as)
{
	struct addrspace_area *area, *temp;

	as_for_each_area_safe(as, area, temp) {
		list_del_init(&area->next_area);
		as_destroy_area(area);
	}

	KASSERT(list_empty(&as->addrspace_area_list));

	pt_destroy(&as->pt);

	lock_destroy(as->as_file_lock);

	vfs_close(as->source_file);

	kfree(as);
}

void
as_activate(void)
{
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		return;
	}

	vm_tlb_flush();
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

	area_flags_t flags =
			(readable ? AS_AREA_READ : 0) |
			(writeable ? AS_AREA_WRITE : 0) |
			(executable ? AS_AREA_EXEC : 0);

	area = as_create_area(vaddr,
			vaddr + memsize,
			flags,
			ASA_TYPE_FILE);
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
					.page_rw = (area->area_flags & AS_AREA_READ) ? true : false,
					.page_pwt = false,
				});

		if (retval)
			return retval;
	}

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
	struct addrspace_area *area;
	int retval;

	KASSERT(as != NULL);

	KASSERT(as->start_arg != 0);
	KASSERT(as->end_arg != 0);

	KASSERT(as->start_stack == 0);
	KASSERT(as->end_stack == 0);


	as->end_stack = as->start_arg & PAGE_FRAME;
	as->start_stack = as->end_stack - AS_STACKPAGES * PAGE_SIZE;

	retval = pt_alloc_page_range(&as->pt, as->start_stack, as->end_stack, (struct pt_page_flags){
		.page_rw = true,
		.page_pwt = false,
	});
	if (retval)
		return retval;

	area = as_create_area(as->start_stack, as->end_stack, AS_AREA_READ | AS_AREA_WRITE, ASA_TYPE_STACK);
	if (!area)
		return ENOMEM;

	retval = as_add_area(as, area);
	if (retval)
		return retval;

	*stackptr = as->end_stack;

	return 0;
#else // OPT_ARGS
	(void)as;
	KASSERT(as->asp_stackpbase != 0);

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
#endif // OPT_ARGS
}

struct addrspace_area *as_find_area(struct addrspace *as, vaddr_t addr)
{
	struct addrspace_area *area;

	as_for_each_area(as, area) {
		if (addr >= area->area_start && addr < area->area_end)
			return area;
	}

	return NULL;
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
	struct addrspace_area *area;
	size_t arg_map_size = 0;
	size_t offset = 0;
	char **user_argv;
	int retval;

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

	/* allocate the page for the required space */
	retval = pt_alloc_page_range(&as->pt, as->start_arg, as->end_arg, (struct pt_page_flags){
		.page_rw = true,
		.page_pwt = false,
	});
	if (retval)
		return retval;

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

	area = as_create_area(as->start_arg, as->end_arg, AS_AREA_READ, ASA_TYPE_ARGS);
	if (!area)
		return ENOMEM;

	retval = as_add_area(as, area);
	if (retval)
		return retval;

	/* set the user argv pointer */
	*uargv = (userptr_t)as->start_arg;

	return 0;
}
#endif // OPT_ARGS