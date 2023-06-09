#ifndef _ADDRSPACE_TYPES_H_
#define _ADDRSPACE_TYPES_H_

#include <types.h>
#include <pt.h>
#include "opt-dumbvm.h"
#include "opt-paging.h"
#include "opt-args.h"



#if OPT_PAGING
typedef enum area_flags_t {
        AS_AREA_WRITE        = 1 << 0,
        AS_AREA_READ         = 1 << 1,
        AS_AREA_EXEC         = 1 << 2,
        AS_AREA_MAY_WRITE    = 1 << 3,
        AS_AREA_MAY_READ     = 1 << 4,
        AS_AREA_MAY_EXEC     = 1 << 5,
} area_flags_t;

/**
 * @brief Represent an area of the address space. The flags
 * allows to handle the area during vm_fault.
 * 
 */
struct addrspace_area {
        area_flags_t area_flags;        /* flags of the area */
        struct list_head next_area;  
        /*
         * Borders of the area, the end is not included
         * in the interval [area_start, area_end)
         */
        vaddr_t area_start, area_end;
};
#endif // OPT_PAGING


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
        struct page_table pt;

        struct list_head addrspace_area_list;

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