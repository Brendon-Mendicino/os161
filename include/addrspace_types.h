#ifndef _ADDRSPACE_TYPES_H_
#define _ADDRSPACE_TYPES_H_

#include <machine/pt.h>
#include "opt-dumbvm.h"
#include "opt-paging.h"
#include "opt-args.h"


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

        /* points to a Page Miggle Diretory Table */
        pmd_t *pmd;

        vaddr_t start_code, end_code;
        vaddr_t start_data, end_data;
        vaddr_t start_stack, end_stack;
#endif

#if OPT_ARGS
        /* args are allocated inside the stack */
        vaddr_t start_arg, end_arg;
#endif // OPT_ARGS
};


#endif // _ADDRSPACE_TYPES_H_