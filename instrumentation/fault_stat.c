#include <lib.h>
#include <machine/atomic.h>
#include <fault_stat.h>

struct fault_stat sys_fault_stat = {
    .tlb_faults                 = ATOMIC_INIT(0),
    .tlb_faults_with_free       = ATOMIC_INIT(0),
    .tlb_faults_with_replace    = ATOMIC_INIT(0),
    .tlb_invalidations          = ATOMIC_INIT(0),
    .tlb_realoads               = ATOMIC_INIT(0),
    .page_faults_zero           = ATOMIC_INIT(0),
    .page_faults_disk           = ATOMIC_INIT(0),
    .page_faults_elf            = ATOMIC_INIT(0),
    .page_faults_swap           = ATOMIC_INIT(0),
    .swap_writes                = ATOMIC_INIT(0),
};

void fault_stat_print_info(void)
{
    kprintf("TLB fautls statistics.\n\n");
    kprintf("TLB faults:\t\t%10d\n", atomic_read(&sys_fault_stat.tlb_faults));
    kprintf("TLB faults with free:\t%10d\n", atomic_read(&sys_fault_stat.tlb_faults_with_free));
    kprintf("TLB faults replace:\t%10d\n", atomic_read(&sys_fault_stat.tlb_faults_with_replace));
    kprintf("TLB invalidations:\t%10d\n", atomic_read(&sys_fault_stat.tlb_invalidations));
    kprintf("TLB reloads:\t\t%10d\n", atomic_read(&sys_fault_stat.tlb_realoads));
    kprintf("Page faults zero page:\t%10d\n", atomic_read(&sys_fault_stat.page_faults_zero));
    kprintf("Page faults from disk:\t%10d\n", atomic_read(&sys_fault_stat.page_faults_disk));
    kprintf("Page fualts from ELF:\t%10d\n", atomic_read(&sys_fault_stat.page_faults_elf));
    kprintf("Page faults from swap:\t%10d\n", atomic_read(&sys_fault_stat.page_faults_swap));
    kprintf("Swap writes:\t\t%10d\n", atomic_read(&sys_fault_stat.swap_writes));

    if (atomic_read(&sys_fault_stat.tlb_faults) !=
        atomic_read(&sys_fault_stat.tlb_faults_with_free) +
        atomic_read(&sys_fault_stat.tlb_faults_with_replace))
        kprintf("Warning: free + replace faults don't sum up to TLB faults!\n");

    if (atomic_read(&sys_fault_stat.tlb_faults) !=
        atomic_read(&sys_fault_stat.tlb_realoads) +
        atomic_read(&sys_fault_stat.page_faults_disk) +
        atomic_read(&sys_fault_stat.page_faults_zero))
        kprintf("Warning: reaload + disk + zeroed faults don't sum up to TLB faults!\n");

    if (atomic_read(&sys_fault_stat.page_faults_disk) !=
        atomic_read(&sys_fault_stat.page_faults_elf) +
        atomic_read(&sys_fault_stat.page_faults_swap))
        kprintf("Warning: swap + ELF faults don't sum up to Disk faults!\n");
}