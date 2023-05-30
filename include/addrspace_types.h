#ifndef _ADDRSPACE_TYPES_H_
#define _ADDRSPACE_TYPES_H_

#include <machine/pt.h>
#include "opt-dumbvm.h"
#include "opt-paging.h"


/*
 * Address space - data structure associated with the virtual memory
 * space of a process.
 *
 * You write this.
 */
struct addrspace {
#if OPT_DUMBVM
        vaddr_t as_vbase1;
        paddr_t as_pbase1;
        size_t as_npages1;
        vaddr_t as_vbase2;
        paddr_t as_pbase2;
        size_t as_npages2;
        paddr_t as_stackpbase;
        pmd_t *pmd;
#elif OPT_PAGING
        vaddr_t asp_vbase1;
        vaddr_t asp_pbase1;
        size_t asp_npages1;
        vaddr_t asp_vbase2;
        vaddr_t asp_pbase2;
        size_t asp_npages2;
        vaddr_t asp_stackpbase;
        size_t asp_nstackpages;
        pmd_t *pmd;
        /* Put stuff here for your VM system */
#endif
};


#endif // _ADDRSPACE_TYPES_H_