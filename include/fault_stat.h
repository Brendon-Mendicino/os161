#ifndef _FAULT_STAT_H_
#define _FAULT_STAT_H_

#include <machine/atomic.h>


struct fault_stat {
    /**
     * The number of TLB misses that have occurred 
     * (not including faults that cause a program to crash).
     */
    atomic_t    tlb_faults;
    /**
     * Number of TLB misses with a free space inside the TLB.
     */
    atomic_t    tlb_faults_with_free;
    /**
     * Number of TLB misses with all the entry
     * unavailable inside the TLB.
     */
    atomic_t    tlb_faults_with_replace;
    /**
     * Number of times the whole TLB is invalidated.
     */
    atomic_t    tlb_invalidations;
    /**
     * Number of TLB misses for pages already in memory.
     */
    atomic_t    tlb_realoads;
    /**
     * Number of TLB misses that require a zero-filled page.
     */
    atomic_t    page_faults_zero;
    /**
     * Number of TLB misses that require a page to be loded
     * from the disk.
     */
    atomic_t    page_faults_disk;
    /**
     * Number of page faults that require a page to be loded
     * from an ELF file.
     */
    atomic_t    page_faults_elf;
    /**
     * Number of page faults that require a page to be loded
     * from the swap partition.
     */
    atomic_t    page_faults_swap;
    /**
     * Number of page faults that require a page to be written
     * to the swap partition.
     */
    atomic_t    swap_writes;
};


extern struct fault_stat sys_fault_stat;


static inline void fstat_tlb_faults(void)
{
    atomic_add(&sys_fault_stat.tlb_faults, 1);
}

static inline void fstat_tlb_faults_with_free(void)
{
    atomic_add(&sys_fault_stat.tlb_faults_with_free, 1);
}

static inline void fstat_tlb_faults_with_replace(void)
{
    atomic_add(&sys_fault_stat.tlb_faults_with_replace, 1);
}

static inline void fstat_tlb_invalidations(void)
{
    atomic_add(&sys_fault_stat.tlb_invalidations, 1);
}

static inline void fstat_tlb_realoads(void)
{
    atomic_add(&sys_fault_stat.tlb_realoads, 1);
}

static inline void fstat_page_faults_zero(void)
{
    atomic_add(&sys_fault_stat.page_faults_zero, 1);
}

static inline void fstat_page_faults_disk(void)
{
    atomic_add(&sys_fault_stat.page_faults_disk, 1);
}

static inline void fstat_page_faults_elf(void)
{
    atomic_add(&sys_fault_stat.page_faults_elf, 1);
}

static inline void fstat_page_faults_swap(void)
{
    atomic_add(&sys_fault_stat.page_faults_swap, 1);
}

static inline void fstat_swap_writes(void)
{
    atomic_add(&sys_fault_stat.swap_writes, 1);
}

extern void fault_stat_print_info(void);

#endif // _FAULT_STAT_H_