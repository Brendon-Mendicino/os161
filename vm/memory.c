#include <types.h>
#include <addrspace.h>
#include <vm.h>
#include <current.h>
#include <proc.h>
#include <vm_tlb.h>
#include <kern/errno.h>


static int vm_handle_fault(struct addrspace *as, vaddr_t fault_address, paddr_t *paddr)
{
    int retval;

    *paddr = pt_get_pfn(&as->pt, fault_address);
    if (*paddr != 0)
        return 0;

    // TODO: chage this allocation, use vm_area
    retval = pt_alloc_page(&as->pt,
            fault_address,
            (struct pt_page_flags){
                .page_pwt = false,
                .page_rw = true,
            },
            paddr);
    if (retval)
        return retval;

    // TODO: for now just load the page form the disk, update to handle swap
    retval = load_demand_page(as, fault_address, *paddr);
    if (retval)
        return retval;


    return 0;
}

int vm_fault(int faulttype, vaddr_t faultaddress)
{
	paddr_t paddr;
	struct addrspace *as;
	int retval;

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

    retval = vm_handle_fault(as, faultaddress, &paddr);
    if (retval)
        return retval;

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

    retval = vm_tlb_set_page(faultaddress, paddr);
    if (retval) 
        return retval;
        
    return 0;
}