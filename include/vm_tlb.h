#ifndef _VM_TLB_H_
#define _VM_TLB_H_

#include <types.h>

extern int vm_tlb_set_page(vaddr_t fault_address, paddr_t paddr);

#endif // _VM_TLB_H_