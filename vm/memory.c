#include <types.h>
#include <addrspace.h>
#include <vm.h>
#include <current.h>
#include <proc.h>
#include <vm_tlb.h>
#include <kern/errno.h>

static int page_not_present_fault(
	struct addrspace *as,
	struct addrspace_area *area,
	pte_t *pte,
	vaddr_t fault_address,
	int fault_type,
	paddr_t *paddr)
{
	struct page *page;
	int retval;

	page = alloc_user_zeroed_page();
	if (!page)
		return ENOMEM;

	/* load page from memory if file mapped */
	if (asa_file_mapped(area)) {
		retval = load_demand_page(as, fault_address, page_to_paddr(page));
		if (retval)
			goto cleanup_page;
	}

	bool page_write = (area->area_flags & AS_AREA_WRITE) == AS_AREA_WRITE;
	bool page_dirty = page_write && (fault_type & VM_FAULT_READ);

	pteflags_t flags = PAGE_PRESENT |
					   PAGE_ACCESSED |
					   (page_dirty * PAGE_DIRTY) |
					   (page_write * PAGE_RW);

	pte_set_page(pte, page_to_kvaddr(page), flags);
	pt_inc_page_count(&as->pt, 1);

	*paddr = page_to_paddr(page);
	
	return 0;

cleanup_page:
	free_pages(page);
	return retval;
}

static int readonly_fault(void)
{
	return 0;
}

static int vm_handle_fault(struct addrspace *as, vaddr_t fault_address, int fault_type, paddr_t *paddr)
{
	struct addrspace_area *area;
	pte_t *pte, pte_entry;

	area = as_find_area(as, fault_address);

	pte = pt_get_or_alloc_pte(&as->pt, fault_address);
	if (!pte)
		return ENOMEM;

	pte_entry = *pte;

	if (!pte_present(pte_entry)) {
		return page_not_present_fault(as, area, pte, fault_address, fault_type, paddr);
	}

	*paddr = pte_paddr(pte_entry);

	if (fault_type & VM_FAULT_READONLY) {
		if (!pte_write(pte_entry)) {
			readonly_fault();
		}
	}

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

    retval = vm_handle_fault(as, faultaddress, faulttype, &paddr);
    if (retval)
        return retval;

    retval = vm_tlb_set_page(faultaddress, paddr);
    if (retval) 
        return retval;
        
    return 0;
}