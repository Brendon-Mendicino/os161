#ifndef _VM_TLB_H_
#define _VM_TLB_H_

#include <types.h>

extern int vm_tlb_set_page(vaddr_t fault_address, paddr_t paddr, bool writable);

extern void vm_tlb_set_readonly(void);

extern int vm_tlb_set_readable(vaddr_t addr, paddr_t paddr);

extern void vm_tlb_flush(void);

#endif // _VM_TLB_H_