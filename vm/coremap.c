#include <types.h>
#include <lib.h>
#include <vm.h>
#include <spinlock.h>
#include <addrspace.h>
#include <refcount.h>
#include <getorder.h>

#include "opt-allocator.h"

#if OPT_ALLOCATOR
#include <atable.h>
#endif // OPT_ALLOCATOR

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

/*
 * List of system pages, each represent a physical page
 */
struct page *page_table = NULL;
size_t total_pages = 0;

/*
 * The only zone present in the system.
 */
struct zone main_zone;



// /**
//  * @brief Given an address it gets translated to the relative
//  * buddy allocator order Page Frame Number, which can
//  * retrieve the relative buddy page from the page table
//  * 
//  * @param kaddr kernel address
//  * @param order order of the buddy system
//  * @return size_t 
//  */
// inline static size_t
// kvaddr_to_order_pfn(vaddr_t addr, unsigned int order)
// {
//     KASSERT(addr >= MIPS_KSEG0);
//     return kvaddr_to_paddr(addr) >> (PAGE_SHIFT);
// 	kvaddr_to_paddr
// }

inline static struct page *
get_page_by_addr(vaddr_t addr)
{
	// size_t pfn = kvaddr_to_order_pfn(addr, order);
	// KASSERT(pfn < total_pages);
	return &page_table[kvaddr_to_pfn(addr)];
}

inline static vaddr_t
page_to_kvaddr(struct page *page)
{
	return PADDR_TO_KVADDR((paddr_t)(page - page_table) * PAGE_SIZE);
}

static void
page_init(struct page *page)	
{
	page->flags = PGF_INIT;
	INIT_LIST_HEAD(&page->buddy_list);
	page->virtual = 0;
	page->_mapcount = REFCOUNT_INIT(0);
}

static void
page_table_bootstrap(void) 
{
	size_t ram_size = (size_t)ram_getsize();
	size_t npage = DIVROUNDUP(ram_size, PAGE_SIZE);
	total_pages = npage;

	paddr_t paddr = ram_stealmem(DIVROUNDUP(npage * sizeof(struct page), PAGE_SIZE));
	page_table = (void *)PADDR_TO_KVADDR(paddr);

	for (size_t i = 0; i < npage; i++)
		page_init(&page_table[i]);
}

static struct page *get_page_from_free_area(struct free_area *area)
{
	return list_first_entry_or_null(&area->free_list, struct page, buddy_list);
}

static void add_page_to_free_list(struct zone *zone, struct page *page, unsigned order)
{
	/* assert that the page is aligned with the order fo the buddy allocator */
	KASSERT( (page_to_kvaddr(page) & ((1 << (PAGE_SHIFT + order)) - 1)) == 0 );

	page->flags = PGF_BUDDY;
	list_add(&page->buddy_list, &zone->free_area[order].free_list);
	zone->free_area[order].n_free += 1;
}

static void del_page_from_free_list(struct zone *zone, struct page *page, unsigned order)	
{
	list_del_init(&page->buddy_list);
	zone->free_area[order].n_free -= 1;
}

static void expand(struct zone *zone, struct page *page, unsigned low_order, unsigned high_order)
{
	size_t size = 1 << high_order;

	while (high_order > low_order) {
		high_order -= 1;
		size = size >> 1;

		add_page_to_free_list(zone, &page[size], high_order);
	}
}

static struct page *get_free_pages(struct zone *zone, unsigned order)
{
	struct free_area *area;
	struct page *page;
	unsigned current_order;

	for (current_order = order; current_order <= MAX_ORDER; current_order += 1) {
		area = &zone->free_area[order];

		page = get_page_from_free_area(area);
		if (!page)
			continue;

		del_page_from_free_list(zone, page, current_order);
		expand(zone, page, order, current_order);
		return page;
	}

	return NULL;
}

static void
zone_bootstrap(void)
{
	struct free_area *area;
	unsigned order;
	vaddr_t first, last;
	struct page *page;
	struct zone *zone;
	
	zone = &main_zone;
	
	zone->last_valid_addr = PADDR_TO_KVADDR(ram_getsize());
	zone->first_valid_addr = ROUNDUP(PADDR_TO_KVADDR(ram_getfirstfree()), PAGE_SIZE << MAX_ORDER);

	for_each_free_area(zone->free_area, area, order) {
		INIT_LIST_HEAD(&area->free_list);
	}

	/* Insert all the max order pages in the buddy */
	last = zone->last_valid_addr;
	first = zone->first_valid_addr;
	for (; first < last; first += PAGE_SIZE << MAX_ORDER) {
		page = get_page_by_addr(first);
		add_page_to_free_list(zone, page, MAX_ORDER);
	}

	/* Assert that all the available pages are allocated in the free_list */
	KASSERT(zone->free_area[MAX_ORDER].n_free == (zone->last_valid_addr - zone->first_valid_addr) / (PAGE_SIZE << MAX_ORDER));
}

static void zone_print_info(void)
{
	kprintf("vm initiazed with:\n");
	kprintf("\t%8d: total physical pages\n", total_pages);
	kprintf("\t%8d: available physical pages\n", (main_zone.last_valid_addr - main_zone.first_valid_addr) / PAGE_SIZE);
	kprintf("\n");
}

void
vm_bootstrap(void)
{
	page_table_bootstrap();
	zone_bootstrap();
	zone_print_info();
// #if OPT_ALLOCATOR
// 	spinlock_acquire(&mem_lock);

// 	KASSERT(atable == NULL);
// 	atable = atable_create();
// 	kprintf("vm initialized with: %d page frames available\n", atable_capacity(atable));

// 	spinlock_release(&mem_lock);
// #endif // OPT_ALLOCATOR

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

/**
 * @brief Allocates npages contiguosly for the kernel
 * 
 * @param npages number contiguos pages to allocate
 * @return vaddr_t returns 0 if no page was allocated
 */
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

__UNUSED struct page *alloc_pages(size_t npages)
{
	unsigned order = get_order(npages);
	return get_free_pages(&main_zone, order);
}