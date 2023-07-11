#ifndef _PT_H_
#define _PT_H_

#include <machine/pt.h>
#include <types.h>

typedef struct pmd_t pmd_t;

struct page_table {
    pmd_t *pmd;     /* pointer to the PageMiddleDirectory */
    size_t total_pages;    /* number of allocated pages */
};

struct pt_page_flags {
    bool page_rw;       /* page is writable */
    bool page_pwt;      /* page is write through */
};

extern int pt_init(struct page_table *pt);

extern void pt_destroy(struct page_table *pt);

extern int pt_alloc_page(struct page_table *pt, vaddr_t addr, struct pt_page_flags page_flags);

extern int pt_alloc_page_range(struct page_table *pt, vaddr_t start, vaddr_t end, struct pt_page_flags flags);

extern paddr_t pt_get_pfn(struct page_table *pt, vaddr_t addr);

extern int pt_copy(struct page_table *new, struct page_table *old);

#endif // _PT_H_