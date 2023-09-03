#ifndef _VM_TLB_H_
#define _VM_TLB_H_

#include <types.h>

typedef enum tlb_state_t {
    TLB_ENTRY_PRESENT,
    TLB_ENTRY_NOT_PRESENT,
} tlb_state_t;

extern tlb_state_t vm_tlb_set_page(vaddr_t fault_address, paddr_t paddr, bool writable);

extern void vm_tlb_set_readonly(void);

extern void vm_tlb_flush(void);

extern void vm_tlb_flush_one(vaddr_t addr);

#endif // _VM_TLB_H_