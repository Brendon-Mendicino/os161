#ifndef _ADDRSPACE_TYPES_H_
#define _ADDRSPACE_TYPES_H_

#include <types.h>
#include <vnode.h>
#include <pt.h>
#include <refcount.h>
#include <list.h>
#include "opt-dumbvm.h"
#include "opt-paging.h"
#include "opt-args.h"



#if OPT_PAGING
/**
 * @brief This are the struct page flags,
 * they identify in which state a page is in the system.
 * 
 */
typedef enum page_flags_t {
        PGF_INIT,       /* The page has been just initialized */
        PGF_BUDDY,      /* The page is inside the buddy allocator */
        PGF_KERN,
        PGF_USER,
} page_flags_t;


/*
 * Each physical page in the system has a struct page associated with
 * it to keep track of whatever it is we are using the page for at the
 * moment. Note that we have no way to track which tasks are using
 * a page.
 *
 */
struct page {
        page_flags_t    flags;

        union {
                struct {
                        struct list_head buddy_list;
                };

                struct {
                        refcount_t      _mapcount;    /* User usage count, increased when a page becomes COW */
                };
        };

        /*
         * Order of the buddy systme allocator,
         * this is set when the page gets assigned to an order
         * or is removed from the buddy system.
         * 
         * Manipulated inside alloc_pages() / free_pages().
         */
        unsigned buddy_order;
        vaddr_t         virtual;        /* Kernel virtual address (NULL if not kmapped) */
};


typedef enum area_flags_t {
        AS_AREA_WRITE        = 1 << 0,
        AS_AREA_READ         = 1 << 1,
        AS_AREA_EXEC         = 1 << 2,
        AS_AREA_MAY_WRITE    = 1 << 3,
        AS_AREA_MAY_READ     = 1 << 4,
        AS_AREA_MAY_EXEC     = 1 << 5,
} area_flags_t;

/*
 * Address Space Area (ASA) types
 */
typedef enum area_type_t {
        ASA_TYPE_FILE,          /* the area is mapped from a file */
        ASA_TYPE_MMAP,          /* the area is mapped in memory */
        ASA_TYPE_ARGS,
        ASA_TYPE_STACK,
} area_type_t;

/**
 * @brief Represent an area of the address space. The flags
 * allows to handle the area during vm_fault.
 * 
 */
struct addrspace_area {
        area_flags_t area_flags;        /* flags of the area */
        area_type_t  area_type;         /* type of the area */
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

        struct lock  *as_file_lock;

        struct vnode *source_file;

        vaddr_t start_stack, end_stack;
#endif // OPT_PAGING

#if OPT_ARGS
        /* args are allocated before the stack */
        vaddr_t start_arg, end_arg;
#endif // OPT_ARGS
};

#endif // _ADDRSPACE_TYPES_H_