#ifndef _SWAP_H_
#define _SWAP_H_

#include <types.h>
#include <file.h>
#include <spinlock.h>
#include <synch.h>
#include <swap_types.h>
#include <page.h>

#define SWAP_SIZE  (9 * (1 << 20))
#define SWAP_ENTRIES (SWAP_SIZE / PAGE_SIZE)


struct swap_entry {
    unsigned refcount;
};

struct swap_memory {
    size_t swap_pages;
    size_t swap_size;

    struct spinlock swap_lock;
    struct lock *swap_file_lock;

    struct vnode *swap_file;
    
    struct swap_entry swap_page_list[SWAP_ENTRIES];
};


extern void swap_bootsrap(void);

extern int swap_add_page(struct page *page, swap_entry_t *entry);

extern int swap_get_page(struct page *page, swap_entry_t swap_entry);

extern int swap_inc_page(swap_entry_t entry);

extern int swap_dec_page(swap_entry_t entry);

extern void swap_print_info(void);

extern void swap_print_all(void);

extern void swap_print_range(size_t start, size_t end);

#endif // _SWAP_H_
