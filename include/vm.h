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


struct zone {
    vaddr_t first_valid_addr;
    vaddr_t last_valid_addr;
    struct free_area    free_area[MAX_ORDER + 1];
};

#define for_each_free_area(free_area_list, free_area, order)    \
        for (order = 0, free_area = &free_area_list[order];     \
            order <= MAX_ORDER;                                 \
            order += 1, free_area = &free_area_list[order]) 



/* Initialization function */
void vm_bootstrap(void);

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress);

/* Allocate/free kernel heap pages (called by kmalloc/kfree) */

vaddr_t alloc_kpages(unsigned npages);
void free_kpages(vaddr_t addr);

/* TLB shootdown handling called from interprocessor_interrupt */
void vm_tlbshootdown(const struct tlbshootdown *);

void vm_kpages_stats(size_t *tot, size_t *ntaken);

extern struct page *alloc_pages(size_t npages);

extern void free_pages(struct page *page);

#endif /* _VM_H_ */
