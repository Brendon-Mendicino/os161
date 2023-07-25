#include <pt.h>
#include <machine/pt.h>
#include <lib.h>
#include <kern/errno.h>
#include <addrspace.h>



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
    size_t freed_pages = 0;
    size_t i;

    // TODO: add check for page refcount
    /* free pages */
    for (i = 0; i < PTRS_PER_PTE; i++) {
        if (!pte_present(pte[i]))
            continue;

        free_kpages(pte_value(pte[i]));
        freed_pages += 1;
    }

    free_kpages((vaddr_t)pte);

    return freed_pages;
}

static int pte_alloc_page_range(pte_t *pte, vaddr_t start, vaddr_t end, struct pt_page_flags flags, size_t *alloc_pages)
{
    vaddr_t curr_addr;
    vaddr_t page;
    size_t pmd_curr_index;

    KASSERT(pte != NULL);
    KASSERT(alloc_pages != NULL);
    KASSERT(start <= end);

    /* setup the falgs for the page to allocate */
    struct page_flags page_flags = (struct page_flags){
        .page_present = true,
        .page_rw = flags.page_rw,
        .page_pwt = flags.page_pwt,
        .page_accessed = false,
        .page_dirty = false,
    };

    for (curr_addr = start, pmd_curr_index = pmd_index(start);
            curr_addr < end && pmd_curr_index == pmd_index(curr_addr);
            curr_addr += PAGE_SIZE)
    {
        if (pte_present(pte[pte_index(curr_addr)])) {
            pte_set_flags(&pte[pte_index(curr_addr)], page_flags);
            continue;
        }

        page = alloc_kpages(1);
        if (!page)
            return ENOMEM;

        *alloc_pages += 1;

        pte_set_page(&pte[pte_index(curr_addr)], page, page_flags);
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
 * @param pmd 
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
    }

    free_kpages((vaddr_t)pmd);

    return freed_pages;
}

/**
 * @brief Allocates a pte and it is assigned to the pmd
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

    // TODO: do a recursive delete and check for refcount
    //...

    pt->total_pages -= pmd_free_table(pt->pmd);

    KASSERT(pt->total_pages == 0);
}

int pt_get_or_alloc_pte(struct page_table *pt, vaddr_t addr, pte_t **pte_entry)
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
            return ENOMEM;

        /* assigns the PTE to a PMD entry */
        pmd_set_pte(&pt->pmd[pmd_index(addr)], pte);
    } else {
        pte = pmd_ptetable(*pmd_entry);
    }

    *pte_entry = &pte[pte_index(addr)];

    return 0;
}

int pt_alloc_page(struct page_table *pt, vaddr_t addr, struct pt_page_flags flags, paddr_t *paddr)
{
    pmd_t *pmd_entry;
    pte_t *pte, *pte_entry;
    vaddr_t page;

    KASSERT(pt != NULL);
    KASSERT(pt->pmd != NULL);


    pmd_entry = pmd_offset_pmd(pt->pmd, addr);
    /* get the pte if it exist or create a new one */
    if (!pmd_present(*pmd_entry)) {
        pte = pte_create_table();
        if (!pte)
            return ENOMEM;

        /* assigns the PTE to a PMD entry */
        pmd_set_pte(&pt->pmd[pmd_index(addr)], pte);
    } else {
        pte = pmd_ptetable(*pmd_entry);
    }

    /* setup the flags of the page */
    struct page_flags page_flags = (struct page_flags){
        .page_present = true,
        .page_rw = flags.page_rw,
        .page_pwt = flags.page_rw,
        .page_dirty = false,
        .page_accessed = false,
    };

    pte_entry = &pte[pte_index(addr)];
    /* allocate a page if it is not present */
    if (!pte_present(*pte_entry)) {
        page = alloc_kpages(1);
        if (!page)
            return ENOMEM;

        pt->total_pages += 1;

        pte_set_page(pte_entry, page, page_flags);
    } else {
        pte_set_flags(pte_entry, page_flags);
    }

    *paddr = pte_pfn(*pte_entry);

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

/**
 * @brief Get the Page Frame Number from a Virtual Address, if
 * the page is not present return 0.
 * 
 * @param pt 
 * @param addr 
 * @return paddr_t 
 */
paddr_t pt_get_pfn(struct page_table *pt, vaddr_t addr)
{
    pmd_t *pmd = pmd_offset(pt, addr);
    
    if (!pmd_present(*pmd))
        return 0;

    pte_t *pte = pte_offset(pmd, addr);
    if (!pte_present(*pte))
        return 0;

    return pte_pfn(*pte);
}

int pt_copy(struct page_table *new, struct page_table *old)
{
    pte_t *new_pte, *old_pte;
    vaddr_t new_page;
    size_t i, j;

    KASSERT(old->pmd != NULL);
    KASSERT(new->pmd != NULL);
    KASSERT(new->total_pages == 0);

    // TODO: implement COW of the pages
    // TODO: refactor, this is just temp, fix flags
    for (i = 0; i < PTRS_PER_PMD; i++) {
        if (!pmd_present(old->pmd[i]))
            continue;

        new_pte = pte_create_table();
        if (!new_pte)
            return ENOMEM;

        pmd_set_pte(&new->pmd[i], new_pte);

        old_pte = pmd_ptetable(old->pmd[i]);
        for (j = 0; j < PTRS_PER_PTE; j++) {
            if (!pte_present(old_pte[j]))
                continue;

            new_page = alloc_kpages(1);
            if (!new_page)
                return ENOMEM;

            new->total_pages += 1;

            /* copy the old page, this is just temporary */
            memmove((void *)new_page, (void *)pte_value(old_pte[j]), PAGE_SIZE);

            struct page_flags page_flags = (struct page_flags){
                .page_present = (pte_flags(old_pte[j]) & PAGE_PRESENT) == PAGE_PRESENT,
                .page_accessed = (pte_flags(old_pte[j]) & PAGE_ACCESSED) == PAGE_ACCESSED,
                .page_dirty = (pte_flags(old_pte[j]) & PAGE_DIRTY) == PAGE_DIRTY,
                .page_rw = (pte_flags(old_pte[j]) & PAGE_RW) == PAGE_RW,
                .page_pwt = (pte_flags(old_pte[j]) & PAGE_PWT) == PAGE_PWT,
            };
            pte_set_page(&new_pte[j], new_page, page_flags);
        }
    }

    KASSERT(new->total_pages == old->total_pages);
    
    return 0;
}
