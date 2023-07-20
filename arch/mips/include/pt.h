#ifndef _ASM_PT_H_
#define _ASM_PT_H_

#include <compiler_types.h>
#include <types.h>
#include <lib.h>
#include <machine/vm.h>

#define _PAGE_BIT_PRESENT   0    /* is present */
#define _PAGE_BIT_RW        1    /* writeable */
#define _PAGE_BIT_PWT       3    /* page write through */
#define _PAGE_BIT_ACCESSED  5    /* was accessed (raised by CPU) */
#define _PAGE_BIT_DIRTY     6    /* was written to (raised by CPU) */

#define PAGE_PRESENT    (1 << _PAGE_BIT_PRESENT)
#define PAGE_RW         (1 << _PAGE_BIT_RW)
#define PAGE_PWT        (1 << _PAGE_BIT_PWT)
#define PAGE_ACCESSED   (1 << _PAGE_BIT_ACCESSED)
#define PAGE_DIRTY      (1 << _PAGE_BIT_DIRTY)


/**
 * @brief used to set each bit of a page.
 */
struct page_flags {
    bool page_present ;    /* is present */                     
    bool page_rw      ;    /* writeable */
    bool page_pwt     ;    /* page write through */
    bool page_accessed;    /* was accessed (raised by CPU) */
    bool page_dirty   ;    /* was written to (raised by CPU) */
};


typedef size_t pteflags_t;
typedef size_t pmdflags_t;


/*
 * Page Middle Directory
 * First-Level of the PageTable
 */
typedef struct pmd_t {
    union {
        vaddr_t    pmdval;      /* value of the pmd entry */
        pmdflags_t pmdflags;    /* flags of the pmd entry */
    };
} pmd_t;

/*
 * Page Table Entry
 * Second-Level of the PageTable
 */
typedef struct pte_t {
    union {
        vaddr_t    pteval;      /* value of the pte entry */
        pteflags_t pteflags;    /* flags of the pte entry */
    };
} pte_t;


#define PMD_INIT  ((pmd_t) { .pmdval = 0 })

#define PTE_INIT  ((pte_t) { .pteval = 0})


/*
 * PTE_SHIFT determines the area that the second-level
 * page table can map
 */
#define PTE_SHIFT  (PAGE_SHIFT)
#define PTRS_PER_PTE (PAGE_SIZE / sizeof(pte_t))    /* number of pointers per PageTableEntry */
#define PTE_INDEX_BITS (10)                         /* number of bits of the pte index */
#define PTE_INDEX_MASK ((1 << PTE_INDEX_BITS) - 1)  /* mask for the index of the PTE */
#define PTE_FLAGS_MASK ((1 << PAGE_SHIFT) - 1)      /* mask for the PTE entry flags */

#define PTE_ADDR_SIZE (1 << PTE_SHIFT)
#define PTE_ADDR_MASK (~(PTE_ADDR_SIZE - 1))

#define PTE_TABLE_SIZE  (sizeof(pte_t) * PTRS_PER_PTE)  /* size of one PTE table */
#define PTE_TABLE_PAGES (PTE_TABLE_SIZE / PAGE_SIZE)    /* physical pages required to hold a PTE table */

/*
 * PMD_SHIFT determines the area that the first-level
 * page table can map
 */
#define PMD_SHIFT  (PTE_SHIFT + PTE_INDEX_BITS)
#define PTRS_PER_PMD (PAGE_SIZE / sizeof(pmd_t))    /* number of pointers per PageMiddleDirectory */
#define PMD_INDEX_BITS (10)                         /* number of bits of the pmd index */
#define PMD_INDEX_MASK ((1 << PMD_INDEX_BITS) - 1)  /* mask for the index of the PMD */
#define PMD_FLAGS_MASK ((1 << PAGE_SHIFT) - 1)      /* mask for the PMD entry falgs */

#define PMD_ADDR_SIZE  (1 << PMD_SHIFT)
#define PMD_ADDR_MASK  (~(PMD_ADDR_SIZE - 1))

#define PMD_TABLE_SIZE  (sizeof(pmd_t) * PTRS_PER_PMD)  /* size of one PMD table */
#define PMD_TABLE_PAGES (PMD_TABLE_SIZE / PAGE_SIZE)    /* physical pages required to hold a PMD table */



/**
 * @brief get the flags of the PTE
 * 
 * @param pte 
 * @return 
 */
static inline pteflags_t pte_flags(pte_t pte)
{
    return pte.pteflags & PTE_FLAGS_MASK;
}

/**
 * @brief return the value of the PTE
 * 
 * @param pte 
 * @return 
 */
static inline vaddr_t pte_value(pte_t pte)
{
    return pte.pteval & ~PTE_FLAGS_MASK;
}

/**
 * @brief set pointer to the PTE, the addr must be aligned with PTE_FLAGS_MASK
 * 
 * @param pmd pointer to the table
 * @param addr addres of the table
 */
static inline void pte_set_value(pte_t *pte, vaddr_t addr)
{
    KASSERT((addr & PTE_FLAGS_MASK) == 0);

    /* null-out the value of the entry */
    pte->pteval &= PTE_FLAGS_MASK;
    pte->pteval |= addr & (~PTE_FLAGS_MASK);
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

static inline void pte_set_flags(pte_t *pte, struct page_flags flags)
{
    pte->pteflags |= (pteflags_t)(flags.page_present * PAGE_PRESENT) |
        (flags.page_rw * PAGE_RW) |
        (flags.page_pwt * PAGE_PWT) |
        (flags.page_accessed * PAGE_ACCESSED) |
        (flags.page_dirty * PAGE_DIRTY);
}

static inline bool pte_present(pte_t pte)
{
    return pte_flags(pte) & PAGE_PRESENT ? true : false;
}

/**
 * @brief clears the PTE table, 
 * 
 * @param pte 
 */
static inline void pte_clean_table(pte_t *pte)
{
    bzero((void *)pte, PTE_TABLE_SIZE);
}

/**
 * @brief assigns a page to a pte entry given an page address and
 * his flags, all previus flags are set to 0.
 * 
 * @param pte PTE table
 * @param page_addr address of the physical page
 * @param addr virtual address
 * @param flags page flags
 */
static inline void pte_set_page(pte_t *pte_entry, vaddr_t page_addr, struct page_flags flags)
{ 
    KASSERT(!pte_present(*pte_entry));

    /* reset pte pointer */
    pte_entry->pteval &= PTE_FLAGS_MASK;
    /* assign the pte pointer */
    pte_entry->pteval |= page_addr & (~PTE_FLAGS_MASK);

    pte_entry->pteflags &= ~PTE_FLAGS_MASK;
    pte_set_flags(pte_entry, flags);
}

/**
 * @brief Return the Page Frame Number form a PTE entry
 * 
 * @param pte pte entry
 */
static inline paddr_t pte_pfn(pte_t pte)  
{
    return kvaddr_to_paddr(pte_value(pte));
}





/**
 * @brief get the falgs of the PMD
 * 
 * @param pmd 
 * @return 
 */
static inline pmdflags_t pmd_flags(pmd_t pmd)
{
    return pmd.pmdflags & PMD_FLAGS_MASK;
}

/**
 * @brief get the value of the PMD
 * which points to PTE Table
 * 
 * @param pmd 
 * @return pmdval_t 
 */
static inline vaddr_t pmd_value(pmd_t pmd) 
{
    return pmd.pmdval & ~PMD_FLAGS_MASK;
}

/**
 * @brief set pointer to the PMD table, the addr must be aligned with PMD_FLAGS_MASK
 * 
 * @param pmd pointer to the table
 * @param addr addres of the table
 */
static inline void pmd_set_value(pmd_t *pmd, vaddr_t addr)
{
    KASSERT((addr & PMD_FLAGS_MASK) == 0);

    /* set the value of the */
    pmd->pmdval &= PMD_FLAGS_MASK;
    pmd->pmdval |= addr & (~PMD_FLAGS_MASK);
}

/**
 * @brief get the index of the PMD inside the table
 * 
 * @param addr 
 * @return size_t 
 */
static inline size_t pmd_index(vaddr_t addr)
{
    return (size_t)((addr >> PMD_SHIFT) & PMD_INDEX_MASK);
}

/**
 * @brief only set page present
 * 
 * @param pmd 
 * @param flags 
 */
static inline void pmd_set_flags(pmd_t *pmd, struct page_flags flags)
{
    pmd->pmdval |= (pmdflags_t)flags.page_present * PAGE_PRESENT;
}

static inline size_t pmd_present(pmd_t pmd)
{
    return pmd_flags(pmd) & PAGE_PRESENT;
}

static inline void pmd_set_present(pmd_t *pmd)
{
    pmd->pmdflags |= PAGE_PRESENT;
}

/**
 * @brief assigns a pte to the pmd_entry and set the flag as present
 * 
 * @param pmd_entry pmd_entry
 * @param pte pte table
 */
static inline void pmd_set_pte(pmd_t *pmd_entry, pte_t *pte)
{
    KASSERT(!pmd_present(*pmd_entry));

    /* reset pte pointer */
    pmd_entry->pmdval &= PMD_FLAGS_MASK;
    /* assign the pte pointer */
    pmd_entry->pmdval |= (vaddr_t)pte & (~PMD_FLAGS_MASK);

    pmd_set_present(pmd_entry);
}

/**
 * @brief Get the PTE Table from a PMD entry
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
    bzero((void *)pmd, PMD_TABLE_SIZE);
}


#endif // _ASM_PT_H_