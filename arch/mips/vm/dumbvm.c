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
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <atable.h>
#include <copyinout.h>

/*
 * Dumb MIPS-only "VM system" that is intended to only be just barely
 * enough to struggle off the ground. You should replace all of this
 * code while doing the VM assignment. In fact, starting in that
 * assignment, this file is not included in your kernel!
 *
 * NOTE: it's been found over the years that students often begin on
 * the VM assignment by copying dumbvm.c and trying to improve it.
 * This is not recommended. dumbvm is (more or less intentionally) not
 * a good design reference. The first recommendation would be: do not
 * look at dumbvm at all. The second recommendation would be: if you
 * do, be sure to review it from the perspective of comparing it to
 * what a VM system is supposed to do, and understanding what corners
 * it's cutting (there are many) and why, and more importantly, how.
 */

/* under dumbvm, always have 72k of user stack */
/* (this must be > 64K so argument blocks of size ARG_MAX will fit) */
#define DUMBVM_STACKPAGES    18

/*
 * Wrap ram memory acces in a spinlock.
 */
static struct spinlock mem_lock = SPINLOCK_INITIALIZER;

/*
* Allocation page table
*/
#if OPT_ALLOCATOR
static struct atable *atable = NULL;
#endif

void
vm_bootstrap(void)
{
#if OPT_ALLOCATOR
	spinlock_acquire(&mem_lock);

	KASSERT(atable == NULL);
	atable = atable_create();
	kprintf("vm initialized with: %d page frames available\n", atable_capacity(atable));

	spinlock_release(&mem_lock);
#endif // OPT_ALLOCATOR
}

/*
 * Check if we're in a context that can sleep. While most of the
 * operations in dumbvm don't in fact sleep, in a real VM system many
 * of them would. In those, assert that sleeping is ok. This helps
 * avoid the situation where syscall-layer code that works ok with
 * dumbvm starts blowing up during the VM assignment.
 */
static
void
dumbvm_can_sleep(void)
{
	if (CURCPU_EXISTS()) {
		/* must not hold spinlocks */
		KASSERT(curcpu->c_spinlocks == 0);

		/* must not be in an interrupt handler */
		if (curthread->t_in_interrupt != 0)
		{
			kprintf("non ealkdjsflkj");
		}
		KASSERT(curthread->t_in_interrupt == 0);
	}
}

static
paddr_t
getppages(unsigned long npages)
{
	paddr_t addr;

#if OPT_ALLOCATOR
	if (atable == NULL)
	{
		spinlock_acquire(&mem_lock);
		addr = ram_stealmem(npages);
		spinlock_release(&mem_lock);

		return addr;
	}
	else
	{
		spinlock_acquire(&mem_lock);
		addr = atable_getfreeppages(atable, npages);
		spinlock_release(&mem_lock);
	}
#else // OPT_ALLOCATOR
	spinlock_acquire(&mem_lock);
	addr = ram_stealmem(npages);
	spinlock_release(&mem_lock);
#endif // OPT_ALLOCATOR

	return addr;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(unsigned npages)
{
	paddr_t pa;

	dumbvm_can_sleep();
	pa = getppages(npages);
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

void
free_kpages(vaddr_t addr)
{
	paddr_t paddr;

	KASSERT(addr != 0);

#if OPT_ALLOCATOR
	paddr = addr - MIPS_KSEG0;
	spinlock_acquire(&mem_lock);
	atable_freeppages(atable, paddr);
	spinlock_release(&mem_lock);
#else
	(void)paddr;
#endif // OPT_ALLOCATOR
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* We always create pages read-write, so we can't get this */
		panic("dumbvm: got VM_FAULT_READONLY\n");
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = proc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_pbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_pbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT(as->as_stackpbase != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	KASSERT((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
	KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);

	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	if (faultaddress >= vbase1 && faultaddress < vtop1) {
		paddr = (faultaddress - vbase1) + as->as_pbase1;
	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		paddr = (faultaddress - vbase2) + as->as_pbase2;
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {
		paddr = (faultaddress - stackbase) + as->as_stackpbase;
	}
	else {
		return EFAULT;
	}

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}

	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);
	return EFAULT;
}

void
vm_kpages_stats(void)
{
	size_t tot;
	size_t ntaken;

#if OPT_ALLOCATOR
	spinlock_acquire(&mem_lock);
	tot = atable_capacity(atable);
	ntaken = atable_size(atable);
	spinlock_release(&mem_lock);

	kprintf("total pages:\t%8d\n", tot);
	kprintf("taken pages:\t%8d\n", ntaken);
#else
	(void)tot;
	(void)ntaken;
#endif // OPT_ALLOCATOR
}

struct addrspace *
as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}

	as->as_vbase1 = 0;
	as->as_pbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_pbase2 = 0;
	as->as_npages2 = 0;
	as->as_stackpbase = 0;

	return as;
}

void
as_destroy(struct addrspace *as)
{
	dumbvm_can_sleep();

	/* as_pbase2 could be 0 */
	KASSERT(as->as_pbase1 != 0);
	KASSERT(as->as_stackpbase != 0);

#if OPT_ALLOCATOR
	/* TODO: modifi in the future */
	spinlock_acquire(&mem_lock);
	atable_freeppages(atable, as->as_pbase1);
	atable_freeppages(atable, as->as_stackpbase);

	if (as->as_npages2 > 0)
		atable_freeppages(atable, as->as_pbase2);
	spinlock_release(&mem_lock);
#endif // OPT_ALLOCATOR

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
	/* nothing */
}

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	size_t npages;

	dumbvm_can_sleep();

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

	/* We don't use these - all pages are read-write */
	(void)readable;
	(void)writeable;
	(void)executable;

	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		return 0;
	}

	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return ENOSYS;
}

static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

int
as_prepare_load(struct addrspace *as)
{
	KASSERT(as->as_pbase1 == 0);
	KASSERT(as->as_pbase2 == 0);
	KASSERT(as->as_stackpbase == 0);

	dumbvm_can_sleep();

	as->as_pbase1 = getppages(as->as_npages1);
	if (as->as_pbase1 == 0) {
		return ENOMEM;
	}

	as->as_pbase2 = getppages(as->as_npages2);
	if (as->as_pbase2 == 0) {
		return ENOMEM;
	}

	as->as_stackpbase = getppages(DUMBVM_STACKPAGES);
	if (as->as_stackpbase == 0) {
		return ENOMEM;
	}

	as_zero_region(as->as_pbase1, as->as_npages1);
	as_zero_region(as->as_pbase2, as->as_npages2);
	as_zero_region(as->as_stackpbase, DUMBVM_STACKPAGES);

	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	dumbvm_can_sleep();
	(void)as;
	return 0;
}

#if OPT_ARGS
/**
 * @brief Sets up the user space that contains the args of the program.
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

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	KASSERT(as->as_stackpbase != 0);

#if OPT_ARGS
	*stackptr = as->start_arg;
#else // OPT_ARGS
	*stackptr = USERSTACK;
#endif // OPT_ARGS

	return 0;
}

static void
as_bad_prepare_load(struct addrspace *as)
{
	dumbvm_can_sleep();

	KASSERT(as != NULL);

#if OPT_ALLOCATOR
	/* TODO: modifi in the future */
	spinlock_acquire(&mem_lock);
	if (as->as_pbase1 != 0)
		atable_freeppages(atable, as->as_pbase1);

	if (as->as_stackpbase != 0)
		atable_freeppages(atable, as->as_stackpbase);

	if (as->as_npages2 > 0)
		atable_freeppages(atable, as->as_pbase2);
	spinlock_release(&mem_lock);
#endif // OPT_ALLOCATOR

	kfree(as);
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new;
	int retval;

	dumbvm_can_sleep();

	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}

	new->as_vbase1 = old->as_vbase1;
	new->as_npages1 = old->as_npages1;
	new->as_vbase2 = old->as_vbase2;
	new->as_npages2 = old->as_npages2;

	/* (Mis)use as_prepare_load to allocate some physical memory. */
	retval = as_prepare_load(new);
	if (retval) {
		/* 
		 * we cant use as_destroy because its
		 * not knows if the pbase where allocated
		 * correctly.
		 */
		as_bad_prepare_load(new);
		return ENOMEM;
	}

	KASSERT(new->as_pbase1 != 0);
	KASSERT(new->as_pbase2 != 0);
	KASSERT(new->as_stackpbase != 0);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase1),
		(const void *)PADDR_TO_KVADDR(old->as_pbase1),
		old->as_npages1*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase2),
		(const void *)PADDR_TO_KVADDR(old->as_pbase2),
		old->as_npages2*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_stackpbase),
		(const void *)PADDR_TO_KVADDR(old->as_stackpbase),
		DUMBVM_STACKPAGES*PAGE_SIZE);

	*ret = new;
	return 0;
}
