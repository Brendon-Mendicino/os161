#include <types.h>
#include <lib.h>
#include <vm.h>
#include <spinlock.h>
#include <atable.h>


#include "opt-allocator.h"


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

static paddr_t
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

vaddr_t
alloc_kpages(unsigned npages)
{
	paddr_t pa;

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

void
vm_kpages_stats(size_t *tot, size_t *ntaken)
{
#if OPT_ALLOCATOR
	spinlock_acquire(&mem_lock);
	*tot = atable_capacity(atable);
	*ntaken = atable_size(atable);
	spinlock_release(&mem_lock);
#else
	(void)tot;
	(void)ntaken;
#endif // OPT_ALLOCATOR
}
