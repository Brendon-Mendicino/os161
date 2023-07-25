#include <types.h>
#include <addrspace.h>
#include <vm.h>
#include <current.h>
#include <vm_tlb.h>
#include <kern/errno.h>


int vm_fault(int faulttype, vaddr_t faultaddress)
{
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl, retval;

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

	paddr = pt_get_pfn(&as->pt, faultaddress);
	/* try to load the page from the source file */
	if (paddr == 0) {
		retval = load_demand_page(as, faultaddress);
		if (retval)
			return retval;


		paddr = pt_get_pfn(&as->pt, faultaddress);
		if (paddr == 0)
			return EFAULT;
	}

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

    retval = vm_tlb_set_page(faultaddress, paddr);
    if (retval) 
        return retval;

    return 0;
}