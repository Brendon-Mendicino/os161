#ifndef _PT_H_
#define _PT_H_

#include <machine/pt.h>
#include <addrspace_types.h>
#include <types.h>


/**
 * @brief get the relative PTE to the address from the PMD entry
 * 
 * @param pmd PMD page entry
 * @param addr relative address
 * @return pte_t* 
 */
static inline pte_t *pte_offset(pmd_t *pmd, vaddr_t addr)
{
    return (pte_t *)(pmd_ptetable(*pmd) + pte_index(addr));
}

static inline pmd_t *pmd_offset_pmd(pmd_t *pmd, vaddr_t addr)
{
    return (pmd + pmd_index(addr));
}

/**
 * @brief get the relative PMD entry from the vaddr inside the PMD table.
 * 
 * @param as address space to take the pmd_t pointer from
 * @param addr realative virtual address
 * @return pmd_t* 
 */
static inline pmd_t *pmd_offset(struct addrspace *as, vaddr_t addr)
{
    return pmd_offset_pmd(as->pmd, addr);
}



/*
 * Creates a PTE table and initialize all entry to invalid
 */
extern pte_t *pte_create_table(void);

/*
 * Creates a PMD table and initialize all entry to invalid
 */
extern pmd_t *pmd_create_table(void);

extern void pmd_destroy_table(struct addrspace *as);

extern int pmd_create_range(struct addrspace *as, vaddr_t addr, size_t size);


#endif // _PT_H_