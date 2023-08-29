#include <pt.h>
#include <machine/pt.h>
#include <lib.h>
#include <kern/errno.h>
#include <addrspace.h>
#include <page.h>
#include <swap.h>


static inline vaddr_t pmd_addr_end(vaddr_t addr, vaddr_t end)
{
    vaddr_t boundary = (addr + PMD_ADDR_SIZE) & PMD_ADDR_MASK;
    return (boundary - 1 < end - 1) ? boundary : end;
}



/**
 * @brief Find an entry in the PTE from a PMD entry
 * 
 * @param pmd PMD entry
 * @param addr relative address
 * @return pte_t* 
 */
static inline pte_t *pte_offset(pmd_t *pmd, vaddr_t addr)
{
    return ((pte_t *)pmd_value(*pmd)) + pte_index(addr);
}

/**
 * @brief Find an entry in the PMD given his pointer
 * 
 * @param pmd 
 * @param addr 
 * @return pmd_t* 
 */
static inline pmd_t *pmd_offset_pmd(pmd_t *pmd, vaddr_t addr)
{
    return pmd + pmd_index(addr);
}

/**
 * @brief get the relative PMD entry from the vaddr inside the PMD table.
 * 
 * @param as address space to take the pmd_t pointer from
 * @param addr realative virtual address
 * @return pmd_t* 
 */
static inline pmd_t *pmd_offset(struct page_table *pt, vaddr_t addr)
{
    return pmd_offset_pmd(pt->pmd, addr);
}


/**
 * @brief Creates a PTE and initialize all entry to invalid
 * 
 * @return return the address of the newly allocated PTE,
 * or NULL if no memory is available.
 */
static pte_t *pte_create_table(void)
{
    pte_t *pte = NULL;

    compiletime_assert(PTE_TABLE_PAGES == 1, "The size of a PTE table must be equal to one page");

    vaddr_t pte_address = alloc_kpages(PTE_TABLE_PAGES);
    if (!pte_address)
        return NULL;

    KASSERT((pte_address & PAGE_FRAME) == pte_address);

    pte = (pte_t *)pte_address;
    pte_clean_table(pte);

    return pte;
}

/**
 * @brief freed the pte and return the number of freed
 * pages
 * 
 * @param pte 
 * @return number freed pages
 */
static size_t pte_free_table(pte_t *pte)
{
    struct page *page;
    size_t freed_pages = 0;
    size_t i;

    // TODO: add check for swap page
    /* free pages */
    for (i = 0; i < PTRS_PER_PTE; i++) {
        if (pte_none(pte[i]))
            continue;

        if (pte_swap(pte[i])) {
            swap_dec_page(pte_swap_entry(pte[i]));
            continue;
        }

        if (!pte_present(pte[i]))
            continue;

        page = pte_page(pte[i]);
        page = READ_ONCE(page);
        user_page_put(page);

        pte_clear(&pte[i]);
        freed_pages += 1;
    }

    free_kpages((vaddr_t)pte);

    return freed_pages;
}

static int pte_alloc_page_range(pte_t *pte, vaddr_t start, vaddr_t end, struct pt_page_flags flags, size_t *alloc_pages)
{
    struct page *page;
    size_t pmd_curr_index;
    pte_t *pte_entry;

    KASSERT(pte != NULL);
    KASSERT(alloc_pages != NULL);
    KASSERT(start <= end);

    /* setup the falgs for the page to allocate */
    pteflags_t page_flags = PAGE_PRESENT |
            (flags.page_rw * PAGE_RW) |
            (flags.page_pwt * PAGE_PWT); 

    for (pmd_curr_index = pmd_index(start),
            pte_entry = &pte[pte_index(start)];
            start < end && pmd_curr_index == pmd_index(start);
            start += PAGE_SIZE,
            pte_entry = &pte[pte_index(start)])
    {
        if (!pte_none(*pte_entry)) {
            // pte_clear_flags(pte_entry);
            // pte_set_flags(pte_entry, page_flags);
            continue;
        }

        page = alloc_user_zeroed_page();
        if (!page)
            return ENOMEM;

        *alloc_pages += 1;

        pte_set_page(pte_entry, page_to_kvaddr(page), page_flags);
    }

    return 0;
}




/**
 * @brief Creates a PMD and initialize all entry to invalid
 * 
 * @return return the address of the newly allocated PMD table,
 * or NULL if no memory is available.
 */
static pmd_t *pmd_create_table(void)
{
    pmd_t *pmd = NULL;

    compiletime_assert(PMD_TABLE_PAGES == 1, "The size of a PMD table must be equal to one page");

    vaddr_t pmd_address = alloc_kpages(PMD_TABLE_PAGES);
    if (!pmd_address)
        return NULL;

    KASSERT((pmd_address & PAGE_FRAME) == pmd_address);

    pmd = (pmd_t *)pmd_address;
    pmd_clean_table(pmd);

    return pmd;
}

/**
 * @brief freed the pmd table and return the number of freed
 * pages
 * 
 * @param pmd table
 * @return size_t number of freed pages
 */
static size_t pmd_free_table(pmd_t *pmd)
{
    size_t freed_pages = 0;
    size_t i;

    KASSERT(pmd != NULL);

    for (i = 0; i < PTRS_PER_PMD; i++) {
        if (!pmd_present(pmd[i]))
            continue;

        freed_pages += pte_free_table(pmd_ptetable(pmd[i]));
        pmd_clear(&pmd[i]);
    }

    free_kpages((vaddr_t)pmd);

    return freed_pages;
}

/**
 * @brief Allocates a pte and it get assigned to the pmd table
 * 
 * @param pmd pmd table
 * @param addr address
 * @return int error value, if any
 */
static int pmd_alloc_pte(pmd_t *pmd, vaddr_t addr)
{
    KASSERT(PMD_TABLE_PAGES == 1);

    pte_t *pte = pte_create_table();
    if (!pte)   
        return ENOMEM;

    pmd_set_pte(&pmd[pmd_index(addr)], pte);

    return 0;
}

static int pmd_alloc_page_range(pmd_t *pmd, vaddr_t start, vaddr_t end, struct pt_page_flags flags, size_t *alloc_pages)
{
    int retval;
    vaddr_t next;
    pmd_t *pmd_entry;
    pte_t *pte;

    KASSERT(pmd != NULL);
    KASSERT(alloc_pages != NULL);
    KASSERT(start <= end);

    do {
        next = pmd_addr_end(start, end);

        pmd_entry = pmd_offset_pmd(pmd, start);
        /* allocate a new pte if not present */
        if (!pmd_present(*pmd_entry)) {
            retval = pmd_alloc_pte(pmd, start);
            if (retval)
                return retval;
        }

        pte = pmd_ptetable(*pmd_entry);

        retval = pte_alloc_page_range(pte, start, end, flags, alloc_pages);
        if (retval)
            return retval;

    } while (start = next, start < end);
    
    return 0;
}

/**
 * @brief Initialized a `page_table` allocating the first level of the page
 * 
 * @param pt 
 * @return error code
 */
int pt_init(struct page_table *pt)
{
    KASSERT(pt != NULL);

    pt->total_pages = 0;

    pt->pmd = pmd_create_table();
    if (!pt->pmd)
        return ENOMEM;

    return 0;
}

void pt_destroy(struct page_table *pt)
{
    KASSERT(pt != NULL);
    KASSERT(pt->pmd != NULL);

    pt->total_pages -= pmd_free_table(pt->pmd);

    KASSERT(pt->total_pages == 0);
}

/**
 * @brief get the current pte to a specific address if present,
 * otherwise allocate a new table in the process.
 * 
 * @param pt page table
 * @param addr address to the pte
 * @return returns the pte_entry or NULL no memory is available
 */
pte_t *pt_get_or_alloc_pte(struct page_table *pt, vaddr_t addr)
{
    pmd_t *pmd_entry;
    pte_t *pte;

    KASSERT(pt != NULL);
    KASSERT(pt->pmd != NULL);


    pmd_entry = pmd_offset_pmd(pt->pmd, addr);
    /* get the pte if it exist or create a new one */
    if (!pmd_present(*pmd_entry)) {
        pte = pte_create_table();
        if (!pte)
            return NULL;

        /* assigns the PTE to a PMD entry */
        pmd_set_pte(pmd_entry, pte);
    }

    return pte_offset(pmd_entry, addr);
}

int pt_alloc_page(struct page_table *pt, vaddr_t addr, struct pt_page_flags flags, paddr_t *paddr)
{
    pmd_t *pmd_entry;
    pte_t *pte, *pte_entry;
    struct page *page;

    KASSERT(pt != NULL);
    KASSERT(pt->pmd != NULL);

    pteflags_t page_flags = PAGE_PRESENT | 
            (flags.page_rw * PAGE_RW) |
            (flags.page_pwt * PAGE_PWT);

    pmd_entry = pmd_offset(pt, addr);
    /* get the pte if it exist or create a new one */
    if (!pmd_present(*pmd_entry)) {
        pte = pte_create_table();
        if (!pte)
            return ENOMEM;

        /* assigns the PTE to a PMD entry */
        pmd_set_pte(pmd_entry, pte);
    } 

    pte_entry = pte_offset(pmd_entry, addr);
    /* allocate a page if it is not present */
    if (pte_none(*pte_entry)) {
        page = alloc_user_zeroed_page();
        if (!page)
            return ENOMEM;

        pt->total_pages += 1;

        pte_set_page(pte_entry, page_to_kvaddr(page), page_flags);
    } else {
        pte_clear_flags(pte_entry);
        pte_set_flags(pte_entry, page_flags);
    }

    *paddr = pte_paddr(*pte_entry);

    return 0;
}

int pt_alloc_page_range(struct page_table *pt, vaddr_t start, vaddr_t end, struct pt_page_flags flags)
{
    int retval;
    size_t alloc_pages = 0;

    KASSERT(pt != NULL);
    KASSERT(pt->pmd != NULL);

    retval = pmd_alloc_page_range(pt->pmd, start, end, flags, &alloc_pages);
    if (retval)
        return retval;

    pt->total_pages += alloc_pages;

    return 0;
}

static walk_action_t pt_walk_pte(struct page_table *pt, pte_t *pte, vaddr_t start, vaddr_t end, walk_ops_t f)
{
    size_t pmd_curr_index;
    pte_t *pte_entry;
    walk_action_t action = WALK_CONTINUE;

    KASSERT(pte != NULL);
    KASSERT(start <= end);

    pt_for_each_pte_entry(pte, pte_entry, start, end, pmd_curr_index) {
        if (pte_none(*pte_entry))
            continue;

        action = f(pt, pte_entry, start);

        if (action == WALK_BREAK)
            return WALK_BREAK;
    }

    return action;
}

int pt_walk_page_table(struct page_table *pt, vaddr_t start, vaddr_t end, walk_ops_t f)
{
    vaddr_t next;
    pmd_t *pmd_entry;
    pte_t *pte;
    walk_action_t action = WALK_CONTINUE;
    // vaddr_t initial_start = start;
    // unsigned int n_walks = 0;

    KASSERT(pt != NULL);
    KASSERT(pt->pmd != NULL);
    KASSERT(start <= end);

// repeat_walk:
//     n_walks += 1;
//     // TODO: refactor retrun value
//     if (n_walks > 2)
//         return 1;

    do {
        next = pmd_addr_end(start, end);

        pmd_entry = pmd_offset(pt, start);
        if (!pmd_present(*pmd_entry))
            continue;

        pte = pmd_ptetable(*pmd_entry);

        action = pt_walk_pte(pt, pte, start, end, f);
        if (action == WALK_BREAK)
            break;

    } while (start = next, start < end);

    // if (action == WALK_REPEAT) {
    //     start = initial_start;
    //     goto repeat_walk;
    // }

    return 0;
}

/**
 * @brief Get the Page Frame Number from a Virtual Address, if
 * the page is not present return 0.
 * 
 * @param pt 
 * @param addr 
 * @return paddr_t 
 */
paddr_t pt_get_paddr(struct page_table *pt, vaddr_t addr)
{
    pmd_t *pmd;
    pte_t *pte;
    
    pmd = pmd_offset(pt, addr);
    if (!pmd_present(*pmd))
        return 0;

    pte = pte_offset(pmd, addr);
    if (!pte_present(*pte))
        return 0;

    return pte_paddr(*pte);
}

int pt_copy(struct page_table *new, struct page_table *old)
{
    pte_t *new_pte, *old_pte;
    struct page *page;
    size_t i, j;

    KASSERT(old->pmd != NULL);
    KASSERT(new->pmd != NULL);
    KASSERT(new->total_pages == 0);

    for (i = 0; i < PTRS_PER_PMD; i++) {
        if (!pmd_present(old->pmd[i]))
            continue;

        new_pte = pte_create_table();
        if (!new_pte)
            return ENOMEM;

        pmd_set_pte(&new->pmd[i], new_pte);

        old_pte = pmd_ptetable(old->pmd[i]);
        for (j = 0; j < PTRS_PER_PTE; j++) {
            if (pte_none(old_pte[j]))
                continue;

            if (pte_swap(old_pte[j])) {
                swap_inc_page(pte_swap_entry(old_pte[j]));
                new_pte[j] = old_pte[j];
                continue;
            }

            KASSERT(pte_present(old_pte[j]));

            page = pte_page(old_pte[j]);
            user_page_get(page);
            pte_set_cow(&old_pte[j]);

            /* copy the flags */
            pte_set_page(&new_pte[j], page_to_kvaddr(page), pte_flags(old_pte[j]));

            new->total_pages += 1;
        }
    }

    KASSERT(new->total_pages == old->total_pages);
    
    return 0;
}
