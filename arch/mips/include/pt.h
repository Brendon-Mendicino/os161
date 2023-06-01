#ifndef _ASM_PT_H_
#define _ASM_PT_H_

#include <types.h>
#include <lib.h>
#include <machine/vm.h>


#define PAGE_PRESENT    0x00000001



typedef size_t pteval_t;
typedef size_t pteflags_t;

typedef size_t pmdval_t;
typedef size_t pmdflags_t;


/*
 * Page Middle Directory
 * First-Level of the PageTable
 */
typedef struct pmd {
    size_t pmdval;
} pmd_t;

/*
 * Page Table Entry
 * Second-Level of the PageTable
 */
typedef struct pte {
    size_t pteval;
} pte_t;




/*
 * PTE_SHIFT determines the area that the second-level
 * page table can map
 */
#define PTE_SHIFT  (PAGE_SHIFT)
#define PTRS_PER_PTE (PAGE_SIZE / sizeof(pte_t))
#define PTE_INDEX_MASK (~(PTRS_PER_PTE - 1))
#define PTE_FLAGS_MASK ((1 << PAGE_SHIFT) - 1)

#define PTE_ADDR_SIZE (1 << PTE_SHIFT)
#define PTE_ADDR_MASK (~(PTE_ADDR_SIZE - 1))

/*
 * PMD_SHIFT determines the area that the first-level
 * page table can map
 */
#define PMD_SHIFT  (PTE_SHIFT + 10)
#define PTRS_PER_PMD (PAGE_SIZE / sizeof(pmd_t))
#define PMD_MASK (~(PTRS_PER_PMD - 1))
#define PMD_FLAGS_MASK ((1 << PAGE_SHIFT) - 1)

#define PMD_ADDR_SIZE  (1 << PMD_SHIFT)
#define PMD_ADDR_MASK  (~(PMD_ADDR_SIZE - 1))


/**
 * @brief get the flags of the PTE
 * 
 * @param pte 
 * @return 
 */
static inline pteflags_t pte_flags(pte_t pte)
{
    return (pteflags_t)(pte.pteval & PTE_FLAGS_MASK);
}

/**
 * @brief return the value of the PTE
 * 
 * @param pte 
 * @return 
 */
static inline pteval_t pte_value(pte_t pte)
{
    return (pteval_t)(pte.pteval & ~PTE_FLAGS_MASK);
}

/**
 * @brief get the index of the PTE inside the table
 * 
 * @param addr 
 * @return size_t 
 */
static inline size_t pte_index(vaddr_t addr)
{
    return (size_t)((addr >> PTE_SHIFT) & PTE_INDEX_MASK);
}

static inline size_t pte_present(pte_t pte)
{
    return pte_flags(pte) & PAGE_PRESENT;
}

static inline void pte_clean_table(pte_t *pte)
{
    bzero((void *)pte, sizeof(pte_t) * PTRS_PER_PTE);
}





/**
 * @brief get the falgs of the PMD
 * 
 * @param pmd 
 * @return 
 */
static inline pmdflags_t pmd_flags(pmd_t pmd)
{
    return (pmdflags_t)(pmd.pmdval & PMD_FLAGS_MASK);
}

/**
 * @brief get the value of the PMD
 * which points to PTE Table
 * 
 * @param pmd 
 * @return pmdval_t 
 */
static inline pmdval_t pmd_value(pmd_t pmd) 
{
    return (pmdval_t)(pmd.pmdval & ~PMD_FLAGS_MASK);
}

/**
 * @brief get the index of the PMD inside the table
 * 
 * @param addr 
 * @return size_t 
 */
static inline size_t pmd_index(vaddr_t addr)
{
    return (size_t)((addr >> PMD_SHIFT) & PMD_MASK);
}

static inline size_t pmd_present(pmd_t pmd)
{
    return pmd_flags(pmd) & PAGE_PRESENT;
}

/**
 * @brief get the PTE Table from a PMD cell
 * 
 * @param pmd 
 * @return pte_t* 
 */
static inline pte_t *pmd_ptetable(pmd_t pmd)
{
    return (pte_t *)pmd_value(pmd);
}

static inline void pmd_clean_table(pmd_t *pmd)
{
    bzero((void *)pmd, sizeof(pmd_t) * PTRS_PER_PMD);
}


#endif // _ASM_PT_H_