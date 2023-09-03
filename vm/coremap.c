#include <types.h>
#include <lib.h>
#include <vm.h>
#include <spinlock.h>
#include <addrspace.h>
#include <refcount.h>
#include <getorder.h>
#include <cpu.h>
#include <current.h>
#include <proc.h>
#include <page.h>
#include <swap.h>
#include <vm_tlb.h>
#include <kern/errno.h>

#include "opt-allocator.h"

#if OPT_ALLOCATOR
#include <atable.h>
#endif // OPT_ALLOCATOR

/*
 * Wrap ram memory acces in a spinlock.
 */
static struct spinlock mem_lock = SPINLOCK_INITIALIZER;

/*
 * List of system pages, each represent a physical page
 */
struct page *page_table = NULL;
size_t total_pages = 0;

/*
 * The only zone present in the system.
 */
static struct zone main_zone;

static inline bool above_page_swap_threshold(struct zone *zone)
{
	return SWAP_PAGE_THRESHOLD(zone->total_pages, zone->alloc_pages);
}

static inline bool vm_may_perform_swap(void)
{
	struct proc *curr = curproc;

	// Check if we are in a kernel process
	if (curr == NULL)
		return false;

	return above_page_swap_threshold(&main_zone);
}

static walk_action_t choose_victim_page(struct page_table *pt, pte_t *pte, vaddr_t page_addr)
{
	int retval;
	struct page *page;
	swap_entry_t entry;

	if (!pte_present(*pte) || pte_swap(*pte))
		return WALK_REPEAT;

	vm_tlb_flush_one(page_addr);

	if (pte_accessed(*pte)) {
		pte_clear_accessed(pte);
		return WALK_REPEAT;
	}

	page = pte_page(*pte);
	page = READ_ONCE(page);
	KASSERT(page->flags == PGF_USER);

	if (user_page_mapcount(page) > 1)
		return WALK_REPEAT;

	retval = swap_add_page(page, &entry);
	if (retval)
		panic("Could not add a page to the swap memory, page address: %08x\n", (unsigned)page);

	if (!user_page_put(page))
		panic("Page was not freed when moved to the swap memory!\n");

	pte_set_swap(pte, entry);

	pt_inc_page_count(pt, -1);

	return WALK_BREAK;
}

/**
 * @brief Gets the current address space and walk the page table
 * trying to move a page from a `zone` to the swap memory.
 * 
 * @return int
 */
static int vm_try_swapin_page(void)
{
	struct addrspace *as;
	struct proc *curr = curproc;

	// Check if we are in a kernel process
	if (curr == NULL)
		return EINVAL;

	as = proc_getas();
	if (as == NULL)
		return EINVAL;

	// TODO: refactor: use walk_addrspace
	return pt_walk_page_table(&as->pt, 0, USERSPACETOP, choose_victim_page);
}

/*
 * Locate the struct page for both the matching buddy in our
 * pair (buddy1) and the combined O(n+1) page they form (page).
 *
 * 1) Any buddy B1 will have an order O twin B2 which satisfies
 * the following equation:
 *     B2 = B1 ^ (1 << O)
 * For example, if the starting buddy (buddy2) is #8 its order
 * 1 buddy is #10:
 *     B2 = 8 ^ (1 << 1) = 8 ^ 2 = 10
 *
 * 2) Any buddy B will have an order O+1 parent P which
 * satisfies the following equation:
 *     P = B & ~(1 << O)
 */
static inline size_t find_buddy_pfn(size_t pfn, unsigned order)
{
	return pfn ^ (1 << order);
}

static inline bool page_is_buddy(struct page *page, struct page *buddy, unsigned order)
{
	if (buddy->flags != PGF_BUDDY)
		return false;

	if (page_get_order(buddy) != order)
		return false;

	if ((page_to_pfn(page) ^ page_to_pfn(buddy)) != (unsigned)(1 << order))
		return false;

	return true; 
}

/**
 * @brief Find the buddy of `page` and validate it.
 * If it does not exist returns NULL.
 * 
 * @param page page to find the buddy
 * @param order order of page
 * @returns returns the buddy of page if it exist
 */
static inline struct page *find_buddy_page(struct page *page, unsigned order)
{
	size_t page_pfn;
	struct page *buddy;
	
	page_pfn = page_to_pfn(page);

	buddy = pfn_to_page(find_buddy_pfn(page_pfn, order));

	if (page_is_buddy(page, buddy, order))
		return buddy;
	return NULL;
}

/**
 * @brief Create the page_table and initialize all the
 * pages.
 * 
 */
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

/**
 * @brief Tries to get a page from the free_list,
 * if none is available returns NULL.
 * 
 * @param area free_area of the free_list
 * @return returns a page or NULL if none is present
 */
static struct page *get_page_from_free_area(struct free_area *area)
{
	return list_first_entry_or_null(&area->free_list, struct page, buddy_list);
}

/**
 * @brief Adds a page to the free_list and
 * sets his order.
 * 
 * @param zone memory zone of the free_list
 * @param page page to add
 * @param order order of the page
 */
static void add_page_to_free_list(struct zone *zone, struct page *page, unsigned order)
{
	KASSERT(page->flags == PGF_BUDDY);
	/* assert that the page is aligned with the order fo the buddy allocator */
	KASSERT( (page_to_kvaddr(page) & ((1 << (PAGE_SHIFT + order)) - 1)) == 0 );

	page_set_order(page, order);

	list_add(&page->buddy_list, &zone->free_area[order].free_list);
	zone->free_area[order].n_free += 1;
}

static void del_page_from_free_list(struct zone *zone, struct page *page, unsigned order)	
{
	list_del_init(&page->buddy_list);
	zone->free_area[order].n_free -= 1;
}

/**
 * @brief Expands a page into it's buddy pages, from the high_order
 * to the low_order.
 * 
 * @param zone memory zone containing the free_list
 * @param page removed page to allocate it's buddy
 * @param low_order order of requested page size
 * @param high_order order of available page in the buddy system
 */
static void buddy_expand(struct zone *zone, struct page *page, unsigned low_order, unsigned high_order)
{
	size_t size = 1 << high_order;

	while (high_order > low_order) {
		high_order -= 1;
		size = size >> 1;

		KASSERT(page[size].flags == PGF_INIT);
		buddy_page_init(&page[size]);
		add_page_to_free_list(zone, &page[size], high_order);
	}
}

/**
 * @brief Allocates contiguos pages from the buddy
 * allocator system.
 * 
 * @param zone memory zone containing the free_list
 * @param order order of the allocated page
 * @return returns a struct page allocated in the buddy
 * system, or NULL if no memory is available.
 */
static struct page *get_free_pages(struct zone *zone, unsigned order)
{
	struct free_area *area;
	struct page *page;
	unsigned current_order;

	KASSERT(spinlock_do_i_hold(&mem_lock));

	for (current_order = order; current_order <= MAX_ORDER; current_order += 1) {
		area = &zone->free_area[current_order];

		page = get_page_from_free_area(area);
		if (!page)
			continue;

		KASSERT(page->flags == PGF_BUDDY);
		del_page_from_free_list(zone, page, current_order);
		buddy_expand(zone, page, order, current_order);

		zone->alloc_pages += 1 << order;

		page_set_order(page, order);
		page->flags = PGF_ALLOC;
		return page;
	}

	return NULL;
}

/**
 * @brief Free contiguos pages from the buddy
 * allocator system.
 * 
 * @param zone memory zone containing the free_list
 * @param page allocated page to free
 * @param order order of the allocated page
 */
static void free_alloc_pages(struct zone *zone, struct page *page, unsigned order)	
{
	struct page *buddy;

	KASSERT(spinlock_do_i_hold(&mem_lock));

	zone->alloc_pages -= 1 << order;

	while (order < MAX_ORDER) {
		buddy = find_buddy_page(page, order);
		if (!buddy)
			break;

		KASSERT(buddy->flags == PGF_BUDDY);
		KASSERT(buddy->buddy_order == order);

		del_page_from_free_list(zone, buddy, order);

		/*
		 * if buddy is less than page it means that on the upper
		 * level the buddy will be one that can be
		 * merged with another buddy
		 */
		if (buddy < page) {
			page_init(page);
			page = buddy;
		} else {
			page_init(buddy);
		}

		order += 1;
	}

	buddy_page_init(page);
	add_page_to_free_list(zone, page, order);
}

/**
 * @brief Bootstrap the memory zone.
 * 
 */
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
	zone->alloc_pages = 0;
	zone->total_pages = (zone->last_valid_addr - zone->first_valid_addr) / PAGE_SIZE;

	for_each_free_area(zone->free_area, area, order) {
		INIT_LIST_HEAD(&area->free_list);
	}

	/* Insert all the max order pages in the buddy */
	last = zone->last_valid_addr;
	first = zone->first_valid_addr;
	for (; first < last; first += PAGE_SIZE << MAX_ORDER) {
		page = kvaddr_to_page(first);
		buddy_page_init(page);
		add_page_to_free_list(zone, page, MAX_ORDER);
	}

	/* Assert that all the available pages are allocated in the free_list */
	KASSERT(zone->free_area[MAX_ORDER].n_free == (zone->last_valid_addr - zone->first_valid_addr) / (PAGE_SIZE << MAX_ORDER));
}

static void zone_print_info(void)
{
	kprintf("vm initiazed with:\n");
	kprintf("\t%10d: total physical pages\n", total_pages);
	kprintf("\t%10d: available physical pages\n", main_zone.total_pages);
	kprintf("\t0x%08x: first available address\n", main_zone.first_valid_addr);
	kprintf("\t0x%08x: last available address\n", main_zone.last_valid_addr);
	kprintf("\n");
}

/**
 * @brief Prints info about the state of
 * the buddy system.
 * 
 */
static void buddy_print_info(void)
{
	unsigned order;

	for (order = 0; order <= MAX_ORDER; order += 1) {
		unsigned n_free = main_zone.free_area[order].n_free;

		kprintf("order: %2d: free pages:\t%8d\n", order, n_free);
	}
}

static void page_print_info(void)
{
	size_t i;
	struct page *page;
	size_t alloc_pages = 0;
	size_t free_pages = 0;

	for (i = 0, page = &page_table[i]; i < total_pages; i += 1, page = &page_table[i]) {
		if (page->flags == PGF_BUDDY) {
			free_pages += 1 << page->buddy_order;
			i += (1 << page->buddy_order) - 1;
		} else if (page->flags == PGF_INIT) {
			free_pages += 1;
		} else {
			alloc_pages += 1 << page->buddy_order;
			i += (1 << page->buddy_order) - 1;
		}
	}

	kprintf("allocated pages:\t%8d\n", alloc_pages);
	kprintf("free pages:\t\t%8d\n", main_zone.total_pages - alloc_pages);

	if (alloc_pages != main_zone.alloc_pages)
		kprintf("[Warning] Calculated alloc pages are differnt from the ones stored in main_zone!\n");
}

/*
 * Check if we're in a context that can sleep. While most of the
 * operations in dumbvm don't in fact sleep, in a real VM system many
 * of them would. In those, assert that sleeping is ok. This helps
 * avoid the situation where syscall-layer code that works ok with
 * dumbvm starts blowing up during the VM assignment.
 */
static void
vm_can_sleep(void)
{
	if (CURCPU_EXISTS()) {
		/* must not hold spinlocks */
		KASSERT(curcpu->c_spinlocks == 0);

		/* must not be in an interrupt handler */
		KASSERT(curthread->t_in_interrupt == 0);
	}
}

void
vm_bootstrap(void)
{
	page_table_bootstrap();
	zone_bootstrap();
	zone_print_info();
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
	struct page *page;

	/* vm_bootstrap is not called yet */
	if (page_table == NULL)
		return PADDR_TO_KVADDR(ram_stealmem(npages));

	page = alloc_pages(npages);
	if (!page)
		return 0;

	kernel_page_init(page);

	return page_to_kvaddr(page);
}

/**
 * @brief Free previuosly allocated pages.
 * 
 * @param addr address returned by alloc_kpages
 */
void
free_kpages(vaddr_t addr)
{
	KASSERT(addr != 0);

	/* the page was allocated before bootstrap */
	if ((void *)addr < (void *)page_table)
		return;

	free_pages(kvaddr_to_page(addr));
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_kpages_stats(void)
{
	spinlock_acquire(&mem_lock);

	buddy_print_info();
	kprintf("\n");
	page_print_info();

	spinlock_release(&mem_lock);
}

/**
 * @brief Main part of the buddy allocator, 
 * this part of the code does not modify the
 * struct page itself, any modification like
 * assignement to a page table has to be done
 * by the caller.
 * 
 * @param npages number of requested pages
 * @return returns the page pointing to the begenning of the list,
 * or NULL if no memory is available
 */
struct page *alloc_pages(size_t npages)
{
	struct page *page;
	bool do_swap_page;

	vm_can_sleep();

	compiletime_assert(get_order(1) == 0, "Order of 1 is not 0!");
	unsigned order = get_order(npages);

	spinlock_acquire(&mem_lock);
	page = get_free_pages(&main_zone, order);
	do_swap_page = vm_may_perform_swap();
	spinlock_release(&mem_lock);

	/*
	 * If the memory is filling up and we are
	 * in a user process try to move some
	 * page to the swap memory.
	 */
	if (do_swap_page)
		vm_try_swapin_page();

	if (page)
		KASSERT(page->buddy_order == (unsigned)get_order(npages));
	return page;
}

/**
 * @brief Frees a previusly allocated batch of pages
 * 
 * @param page struct page returned by `alloc_pages`
 */
void free_pages(struct page *page)
{
	KASSERT(page_get_order(page) <= MAX_ORDER);

	// vm_can_sleep();

	spinlock_acquire(&mem_lock);
	free_alloc_pages(&main_zone, page, page_get_order(page));
	spinlock_release(&mem_lock);
}

/**
 * @brief Allocates a page for the user.
 * 
 * @return returns a page or NULL if there is no
 * memory available.
 */
struct page *alloc_user_page(void)
{
	struct page *page;
	
	page = alloc_pages(1);
	if (!page)
		return NULL;

	user_page_init(page);

	KASSERT(page->flags == PGF_USER);
	KASSERT(page->buddy_order == 0);

	return page;
}

/**
 * @brief Allocates a page for the user and zero-fills it.
 * 
 * @return returns a cleared page or NULL if there is no
 * memory available.
 */
struct page *alloc_user_zeroed_page(void)
{
	struct page *page;
	
	page = alloc_pages(1);
	if (!page)
		return NULL;

	user_page_init(page);
	clear_page(page);

	KASSERT(page->flags == PGF_USER);
	KASSERT(page->buddy_order == 0);

	return page;
}
