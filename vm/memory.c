#include <types.h>
#include <addrspace.h>
#include <vm.h>
#include <current.h>
#include <proc.h>
#include <vm_tlb.h>
#include <page.h>
#include <fault_stat.h>
#include <swap.h>
#include <kern/errno.h>

static inline bool is_cow_mapping(area_flags_t flags)
{
	return (flags & AS_AREA_MAY_WRITE) == AS_AREA_MAY_WRITE;
}

static int page_not_present_fault(
	struct addrspace *as,
	struct addrspace_area *area,
	pte_t *pte,
	vaddr_t fault_address,
	int fault_type)
{
	struct page *page;
	int retval;

	page = alloc_user_zeroed_page();
	if (!page)
		return ENOMEM;

	/* load page from swap memory */
	if (pte_swap_mapped(*pte)) {
		retval = swap_get_page(page, pte_swap_entry(*pte));
		if (retval)
			goto cleanup_page;

		fstat_page_faults_swap();
	}
	/* load page from memory if file mapped */
	else if (pte_none(*pte) && asa_file_mapped(area)) {
		retval = load_demand_page(as, fault_address, page_to_paddr(page));
		if (retval)
			goto cleanup_page;

		fstat_page_faults_elf();
	} else {
		panic("Don't know what kind of pte faulted!\n");
	}

	bool page_write = asa_write(area);
	bool page_dirty = page_write && (fault_type & VM_FAULT_READ);

	pteflags_t flags = PAGE_PRESENT |
					   PAGE_ACCESSED |
					   (page_dirty * PAGE_DIRTY) |
					   (page_write * PAGE_RW);

	pte_clear(pte);
	pte_set_page(pte, page_to_kvaddr(page), flags);
	pt_inc_page_count(&as->pt, 1);

	fstat_page_faults_disk();
	vm_tlb_set_page(fault_address, page_to_paddr(page), page_write);
	
	return 0;

cleanup_page:
	free_pages(page);
	return retval;
}

static int readonly_fault(
	struct addrspace *as,
	struct addrspace_area *area,
	pte_t *pte,
	vaddr_t fault_address,
	int fault_type)
{
	struct page *page;
	int retval;

	// TODO: temp
	(void)as;
	(void)fault_type;

	if (asa_readonly(area))
		return EFAULT;

	page = pte_page(*pte);

	/*
	 * Clear the entry from the pte before checking
	 * if this is a COW mapped page, this is because
	 * in the process if the refcount is decreased 
	 * the page will not be owned anymore by this process
	 * and the pte will reference an invalid page.
	 */
	pte_clear(pte);

	if (is_cow_mapping(area->area_flags)) {
		page = user_page_copy(page);
	}

	if (!page) {
		pt_inc_page_count(&as->pt, -1);
		vm_tlb_flush_one(fault_address);
		return ENOMEM;
	}

	pte_set_page(pte, page_to_kvaddr(page), PAGE_PRESENT | PAGE_RW | PAGE_ACCESSED | PAGE_DIRTY);

	retval = vm_tlb_set_page(fault_address & PAGE_FRAME, page_to_paddr(page), true);
	if (retval)
		return retval;

	return 0;
}

static int vm_handle_fault(struct addrspace *as, vaddr_t fault_address, int fault_type)
{
	struct addrspace_area *area;
	pte_t *pte, pte_entry;

	area = as_find_area(as, fault_address);
	if (!area)
		return EFAULT;

	pte = pt_get_or_alloc_pte(&as->pt, fault_address);
	if (!pte)
		return ENOMEM;

	pte_entry = *pte;

	if (!pte_present(pte_entry)) {
		return page_not_present_fault(as, area, pte, fault_address, fault_type);
	}

	fstat_tlb_realoads();

	if (fault_type & VM_FAULT_READONLY) {
		if (!pte_write(pte_entry)) {
			return readonly_fault(as, area, pte, fault_address, fault_type);
		}
		return EFAULT;
	}

	vm_tlb_set_page(fault_address, pte_paddr(pte_entry), pte_write(pte_entry));

    return 0;
}

int vm_fault(int faulttype, vaddr_t faultaddress)
{
	struct addrspace *as;
	int retval;

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

	if (faultaddress == 0)
		return EFAULT;

    retval = vm_handle_fault(as, faultaddress, faulttype);
    if (retval)
        return retval;

	fstat_tlb_faults();
        
    return 0;
}