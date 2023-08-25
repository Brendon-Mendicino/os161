/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _VM_H_
#define _VM_H_

/*
 * VM system-related definitions.
 *
 * You'll probably want to add stuff here.
 */

#include <addrspace_types.h>
#include <machine/vm.h>

/* Fault-type arguments to vm_fault() */
#define VM_FAULT_READ        0    /* A read was attempted */
#define VM_FAULT_WRITE       1    /* A write was attempted */
#define VM_FAULT_READONLY    2    /* A write to a readonly page was attempted*/


typedef enum fault_value_t {
    FAULT_OK,
    FAULT_NOMEM,
} fault_value_t ;

#define SWAP_PAGE_THRESHOLD(max, curr) (curr > ((90 * (max)) / 100))

/*
 * In OS161 the RAM size is very limited, so we keep the size
 * of the buddy allocator order low to not loose many pages
 * in the process, this is due to the fact that is more complicated
 * to manage the last pages, that will not be aligned with the
 * last order, and further cheks are required to to not place
 * pages out of memory.
 */
#define MAX_ORDER       (6)    /* Max order (power of 2) of the buddy allocator, included */


/**
 * Represent a level in the buddy allocator,
 * every level contais a list of free pages that can
 * be: merged, expaned or removed.
 */
struct free_area {
    /*
     * List of pages, each page in the list represent the buddy
     * the relative order, this done by looking at the bits in
     * the address, i.e. in the 0th level we only consider
     * the page_number, at the 1st level the page_number >> 1
     * and so on...
     */
    struct list_head    free_list;
    size_t              n_free;         /* Number of pages in the free_list */
};

/**
 * Memory zone of the RAM. In OS161 there is no distinction,
 * not being NUMA mapped or other mappings, so there
 * is only one which maps the whole memory.
 * 
 */
struct zone {
    vaddr_t             first_valid_addr;
    vaddr_t             last_valid_addr;
    size_t              alloc_pages;
    size_t              total_pages;
    struct free_area    free_area[MAX_ORDER + 1];
};

#define for_each_free_area(free_area_list, free_area, order)    \
        for (order = 0, free_area = &free_area_list[order];     \
            order <= MAX_ORDER;                                 \
            order += 1, free_area = &free_area_list[order]) 


extern struct page *page_table;


static inline struct page *
kvaddr_to_page(vaddr_t addr)
{
	return &page_table[kvaddr_to_pfn(addr)];
}

static inline struct page *
pfn_to_page(size_t pfn)
{
	return &page_table[pfn];
}

static inline vaddr_t
page_to_kvaddr(struct page *page)
{
	return PADDR_TO_KVADDR((paddr_t)(page - page_table) * PAGE_SIZE);
}

static inline paddr_t
page_to_paddr(struct page *page)
{
    return ((paddr_t)(page - page_table) * PAGE_SIZE);
}

static inline size_t
page_to_pfn(struct page *page)
{
	return kvaddr_to_pfn(page_to_kvaddr(page));
}

static inline void
clear_page(struct page *page)
{
	KASSERT(page != NULL);

	memset((void *)page_to_kvaddr(page), 0, PAGE_SIZE);
}

static inline void
page_init(struct page *page)	
{
	page->flags = PGF_INIT;
	page->virtual = 0;
}

static inline void
buddy_page_init(struct page *page)
{
	page->flags = PGF_BUDDY;
	INIT_LIST_HEAD(&page->buddy_list);
	page->buddy_order = -1;
	page->virtual = 0;
}

static inline void
user_page_init(struct page *page)
{
    page->flags = PGF_USER;
    page->_mapcount = REFCOUNT_INIT(1);
    page->virtual = 0;
}

static inline void
kernel_page_init(struct page *page)
{
    // TODO: rivedere
    page->flags = PGF_KERN;
}

static inline void
page_set_order(struct page *page, unsigned order)
{
	page->buddy_order = order;
}

static inline unsigned
page_get_order(struct page *page)
{
	return page->buddy_order;
}



/* Initialization function */
void vm_bootstrap(void);

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress);

/* Allocate/free kernel heap pages (called by kmalloc/kfree) */

vaddr_t alloc_kpages(unsigned npages);
void free_kpages(vaddr_t addr);

/* TLB shootdown handling called from interprocessor_interrupt */
void vm_tlbshootdown(const struct tlbshootdown *);

void vm_kpages_stats(void);

extern struct page *alloc_pages(size_t npages);

extern void free_pages(struct page *page);

extern struct page *alloc_user_zeroed_page(void);

#endif /* _VM_H_ */
