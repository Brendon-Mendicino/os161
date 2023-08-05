#include <compiler_types.h>
#include <kern/errno.h>
#include <types.h>
#include <spl.h>
#include <vm.h>
#include <current.h>
#include <addrspace.h>
#include <lib.h>
#include <proc.h>
#include <vm_tlb.h>
#include <spinlock.h>
#include <fault_stat.h>
#include <machine/tlb.h>


static struct spinlock tlb_lock = SPINLOCK_INITIALIZER;

static inline unsigned tlb_select_victim(void)
{
	uint32_t ehi, elo;
	int i;

	for (i = 0; i < NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID)
			continue;

		fstat_tlb_faults_with_free();
		return i;
	}

	fstat_tlb_faults_with_replace();
	return i;
}

int vm_tlb_set_page(vaddr_t faultaddress, paddr_t paddr, bool writable)
{
	uint32_t ehi, elo;
	int index;

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	spinlock_acquire(&tlb_lock);
	/* Disable interrupts on this CPU while frobbing the TLB. */
	int spl = splhigh();

	index = tlb_probe(faultaddress & TLBHI_VPAGE, 0);
	if (index == -1) {
		index = tlb_select_victim();
	} else {
		fstat_tlb_faults_with_free();
	}

	ehi = faultaddress & TLBHI_VPAGE;
	elo = (paddr & TLBLO_PPAGE) | (writable * TLBLO_DIRTY) | TLBLO_VALID;

	tlb_write(ehi, elo, index);

	splx(spl);
	spinlock_release(&tlb_lock);
	return 0;
}

void vm_tlb_set_readonly(void)
{
	uint32_t ehi, elo;
	int i;

	spinlock_acquire(&tlb_lock);
	/* Disable interrupts on this CPU while frobbing the TLB. */
	int spl = splhigh();


	for (i = 0; i < NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID)
			continue;

		/* clear dirty bit */
		elo &= ~TLBLO_DIRTY;
		tlb_write(ehi, elo, i);
	}

	splx(spl);
	spinlock_release(&tlb_lock);
}

void vm_tlb_flush(void)
{
	int i;
	int spl;

	fstat_tlb_invalidations();

	spinlock_acquire(&tlb_lock);
	spl = splhigh();

	for (i = 0; i < NUM_TLB; i += 1) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
	spinlock_release(&tlb_lock);
}