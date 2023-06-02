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
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <spl.h>
#include <pt.h>
#include <machine/tlb.h>

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

	as->pmd = pmd_create_table();
	if (!as->pmd)
		return NULL;

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
	KASSERT(as->asp_pbase1 != 0);
	KASSERT(as->asp_stackpbase != 0);

	/* TODO: modify in the future */
	free_kpages(as->asp_pbase1);
	free_kpages(as->asp_stackpbase);

	pmd_destroy_table(as);

	if (as->asp_npages2 > 0)
		free_kpages(as->asp_pbase2);

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
	size_t npages;

	/* Align the region. First, the base... */
	memsize += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = memsize / PAGE_SIZE;

	/* We don't use these - all pages are read-write */
	(void)readable;
	(void)writeable;
	(void)executable;

	if (as->asp_vbase1 == 0) {
		as->asp_vbase1 = vaddr;
		as->asp_npages1 = npages;
		return 0;
	}

	if (as->asp_vbase2 == 0) {
		as->asp_vbase2 = vaddr;
		as->asp_npages2 = npages;
		return 0;
	}

	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return ENOSYS;
}

static void
as_zero_region(vaddr_t addr, size_t npages)
{
	bzero((void *)addr, npages);
}

int
as_prepare_load(struct addrspace *as)
{
	KASSERT(as->asp_pbase1 == 0);
	KASSERT(as->asp_pbase2 == 0);
	KASSERT(as->asp_stackpbase == 0);

	as->asp_pbase1 = alloc_kpages(as->asp_npages1);
	if (as->asp_pbase1 == 0) {
		return ENOMEM;
	}

	as->asp_pbase2 = alloc_kpages(as->asp_npages2);
	if (as->asp_pbase2 == 0) {
		return ENOMEM;
	}

	as->asp_stackpbase = alloc_kpages(as->asp_nstackpages);
	if (as->asp_stackpbase == 0) {
		return ENOMEM;
	}

	as_zero_region(as->asp_pbase1, as->asp_npages1);
	as_zero_region(as->asp_pbase2, as->asp_npages2);
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
	/*
	 * Write this.
	 */

	(void)as;
	KASSERT(as->asp_stackpbase != 0);

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}

