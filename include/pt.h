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

typedef enum walk_action_t {
    WALK_CONTINUE,
    WALK_BREAK,
    WALK_REPEAT,
} walk_action_t;

typedef walk_action_t (*walk_ops_t)(struct page_table *pt, pte_t *pte, vaddr_t page_addr);


#define pt_for_each_pte_entry(pte, pte_entry, start, end, pmd_curr_index)   \
    for (pmd_curr_index = pmd_index(start),                                 \
            pte_entry = &pte[pte_index(start)];                             \
            (start) < (end) && (pmd_curr_index == pmd_index(start));        \
            start += PAGE_SIZE,                                             \
            pte_entry = &pte[pte_index(start)])


static inline void pt_inc_page_count(struct page_table *pt, int count)
{
    pt->total_pages += count;
}

extern int pt_init(struct page_table *pt);

extern void pt_destroy(struct page_table *pt);

extern pte_t *pt_get_or_alloc_pte(struct page_table *pt, vaddr_t addr);

extern int pt_alloc_page(struct page_table *pt, vaddr_t addr, struct pt_page_flags page_flags, paddr_t *paddr);

extern int pt_alloc_page_range(struct page_table *pt, vaddr_t start, vaddr_t end, struct pt_page_flags flags);

extern int pt_walk_page_table(struct page_table *pt, vaddr_t start, vaddr_t end, walk_ops_t f);

extern paddr_t pt_get_paddr(struct page_table *pt, vaddr_t addr);

extern int pt_copy(struct page_table *new, struct page_table *old);

#endif // _PT_H_