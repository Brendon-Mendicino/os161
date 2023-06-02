#include <pt.h>
#include <lib.h>


/*
 * Creates a PTE table and initialize all entry to invalid
 */
pte_t *pte_create_table(void)
{
    pte_t *pte = kmalloc(sizeof(pte_t) * PTRS_PER_PTE);
    if (!pte)
        return NULL;

    pte_clean_table(pte);

    return pte;
}

/*
 * Creates a PMD table and initialize all entry to invalid
 */
pmd_t *pmd_create_table(void)
{
    pmd_t *pmd = kmalloc(sizeof(pmd_t) * PTRS_PER_PMD);
    if (!pmd)
        return NULL;

    pmd_clean_table(pmd);

    return pmd;
}

void pmd_destroy_table(struct addrspace *as)
{
    // TODO: finire
    kfree(as->pmd);
}

// int pte_create_range(pmd_t *pmd, vaddr_t start, vaddr_t end)
// {
//     for (; start <  end - PAGE_SIZE; start += PAGE_SIZE) {

//     }
//     return 0;
// }

// int pmd_create_range(struct addrspace *as, vaddr_t start, size_t memsize)
// {
//     size_t offset = 0;
//     size_t pages_to_create = DIVROUNDUP(memsize, PAGE_SIZE);


//     return 0;
// }