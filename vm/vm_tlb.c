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
#include <machine/tlb.h>


int vm_tlb_set_page(vaddr_t faultaddress, paddr_t paddr)
{
	uint32_t ehi, elo;
	int i;

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	int spl = splhigh();

	for (i = 0; i < NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID)
			continue;

		ehi = faultaddress & TLBHI_VPAGE;
		elo = (paddr & TLBLO_PPAGE) | TLBLO_DIRTY | TLBLO_VALID;
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);

		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}

	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);
	return EFAULT;
}
