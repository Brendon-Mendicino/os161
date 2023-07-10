#include <kern/errno.h>
#include <types.h>
#include <spl.h>
#include <vm.h>
#include <current.h>
#include <addrspace.h>
#include <lib.h>
#include <proc.h>
#include <machine/tlb.h>

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	// vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	// faultaddress &= PAGE_FRAME;


	switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* We always create pages read-write, so we can't get this */
		// panic("dumbvm: got VM_FAULT_READONLY\n");
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
	// KASSERT(as->asp_vbase1 != 0);
	// KASSERT(as->asp_pbase1 != 0);
	// KASSERT(as->asp_npages1 != 0);
	// KASSERT(as->asp_vbase2 != 0);
	// KASSERT(as->asp_pbase2 != 0);
	// KASSERT(as->asp_npages2 != 0);
	KASSERT(as->asp_stackpbase != 0);
	// KASSERT((as->asp_vbase1 & PAGE_FRAME) == as->asp_vbase1);
	// KASSERT((as->asp_pbase1 & PAGE_FRAME) == as->asp_pbase1);
	// KASSERT((as->asp_vbase2 & PAGE_FRAME) == as->asp_vbase2);
	// KASSERT((as->asp_pbase2 & PAGE_FRAME) == as->asp_pbase2);
	KASSERT((as->asp_stackpbase & PAGE_FRAME) == as->asp_stackpbase);

	// vbase1 = as->asp_vbase1;
	// vtop1 = vbase1 + as->asp_npages1 * PAGE_SIZE;
	// vbase2 = as->asp_vbase2;
	// vtop2 = vbase2 + as->asp_npages2 * PAGE_SIZE;
	// stackbase = USERSTACK - as->asp_nstackpages * PAGE_SIZE;
	// stacktop = USERSTACK;

	// if (faultaddress >= vbase1 && faultaddress < vtop1) {
	// 	paddr = (faultaddress - vbase1) + as->asp_pbase1 - MIPS_KSEG0;
	// }
	// else if (faultaddress >= vbase2 && faultaddress < vtop2) {
	// 	paddr = (faultaddress - vbase2) + as->asp_pbase2 - MIPS_KSEG0;
	// }
	// else if (faultaddress >= stackbase && faultaddress < stacktop) {
	// 	paddr = (faultaddress - stackbase) + as->asp_stackpbase - MIPS_KSEG0;
	// }
	// else {
	// 	return EFAULT;
	// }

	paddr = pt_get_pfn(&as->pt, faultaddress);
	if (paddr == 0)
		return EFAULT;

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
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