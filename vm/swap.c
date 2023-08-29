#include <compiler_attributes.h>
#include <swap.h>
#include <vm.h>
#include <lib.h>
#include <uio.h>
#include <page.h>
#include <fault_stat.h>
#include <kern/errno.h>
#include <kern/fcntl.h>


struct swap_memory swap_mem;


static int __must_check handle_swap_inc_page(struct swap_memory *swap, swap_entry_t entry)
{
    bool valid = true;
    size_t index;

    KASSERT(PAGE_ALIGNED(entry.val));

    index = entry.val / PAGE_SIZE;

    spinlock_acquire(&swap->swap_lock);
    if (swap->swap_page_list[index].refcount == 0)
        valid = false;
    else
        swap->swap_page_list[index].refcount += 1;
    spinlock_release(&swap->swap_lock);

    return valid ? 0 : EINVAL;
}

static int __must_check handle_swap_dec_page(struct swap_memory *swap, swap_entry_t entry)
{
    bool valid = true;
    size_t index = entry.val / PAGE_SIZE;

    spinlock_acquire(&swap->swap_lock);
    if (swap->swap_page_list[index].refcount == 0) {
        valid = false;
    } else {
        swap->swap_page_list[index].refcount -= 1;
        
        if (swap->swap_page_list[index].refcount == 0)
            swap->swap_pages -= 1;
    }
    spinlock_release(&swap->swap_lock);

    return valid ? 0 : EINVAL;
}

static bool swap_check_page(struct page *page)
{
    if (page->flags != PGF_USER)
        return false;

    if (user_page_mapcount(page) > 1)
        return false;

    if (page->buddy_order != 0)
        return false;

    return true;
}

static size_t swap_get_first_free(struct swap_memory *swap)
{
    for (size_t i = 0; i < SWAP_ENTRIES; i += 1) {
        if (swap->swap_page_list[i].refcount != 0)
            continue;

        return i;
    }

    panic("Out of swap space!\n");
}

static int handle_swap_add_page(struct swap_memory *swap, struct page *page, swap_entry_t *entry)
{
    off_t file_offset;
    size_t first_free;
    struct uio uio;
    struct iovec iovec;
    int retval;

    spinlock_acquire(&swap->swap_lock);

    first_free = swap_get_first_free(swap);

    swap->swap_page_list[first_free].refcount += 1;
    swap->swap_pages += 1;

    KASSERT(swap->swap_page_list[first_free].refcount == 1);

    spinlock_release(&swap->swap_lock);

    file_offset = first_free * PAGE_SIZE;
    uio_kinit(&iovec, &uio, (void *)page_to_kvaddr(page), PAGE_SIZE, file_offset, UIO_WRITE);

    lock_acquire(swap->swap_file_lock);
    retval = VOP_WRITE(swap->swap_file, &uio);
    lock_release(swap->swap_file_lock);
    if (retval)
        goto bad_write_cleanup;

    entry->val = (size_t)file_offset;
    KASSERT(PAGE_ALIGNED(entry->val));
    fstat_swap_writes();

    return 0;

bad_write_cleanup:
    spinlock_acquire(&swap->swap_lock);
    swap->swap_page_list[first_free].refcount -= 1;
    swap->swap_pages -= 1;
    spinlock_release(&swap->swap_lock);
    
    return retval;
}

static int handle_swap_get_page(struct swap_memory *swap, struct page *page, swap_entry_t entry)
{
    struct uio uio;
    struct iovec iovec;
    off_t file_offset;
    int retval;

    KASSERT(page != NULL);
    KASSERT(PAGE_ALIGNED(entry.val));

    file_offset = (off_t)entry.val;
    uio_kinit(&iovec, &uio, (void *)page_to_kvaddr(page), PAGE_SIZE, file_offset, UIO_READ);

    lock_acquire(swap->swap_file_lock);
    retval = VOP_READ(swap->swap_file, &uio);
    lock_release(swap->swap_file_lock);
    if (retval)
        return retval;

    retval = handle_swap_dec_page(swap, entry);
    if (retval)
        return retval;

    return 0;
}

static void write_at_end_swap_file(struct vnode *swap)
{
    struct uio uio;
    struct iovec iovec;
    char buff[PAGE_SIZE] = { 0 };
    int retval;

    uio_kinit(&iovec, &uio, (void *)buff, PAGE_SIZE, SWAP_SIZE, UIO_WRITE);
    retval = VOP_WRITE(swap, &uio);
    if (retval)
        panic("Swap bootstrap failed: %s\n", strerror(retval));
}

/**
 * @brief Bootstraps the swap file.
 * 
 * @return error if any
 */
void swap_bootsrap(void)
{
    int retval;

    char swap_file[10] = "/swap";
    retval = vfs_open(swap_file, O_CREAT | O_RDWR | O_TRUNC, 0, &swap_mem.swap_file);
    if (retval)
        goto bad_swap_boot;

    write_at_end_swap_file(swap_mem.swap_file);

    swap_mem.swap_file_lock = lock_create("swap_lock");
    if (!swap_mem.swap_file_lock) {
        retval = ENOMEM;
        goto bad_swap_boot;
    }

    swap_mem.swap_size = SWAP_SIZE;
    swap_mem.swap_pages = 0;

    spinlock_init(&swap_mem.swap_lock);

    for (int i = 0; i < SWAP_ENTRIES; i += 1) {
        swap_mem.swap_page_list[i] = (struct swap_entry){
            .refcount = 0,
        };
    }

    return;

bad_swap_boot:
    panic("Could not initialize swap memory: %s\n", strerror(retval));
}

/**
 * @brief Add a page to the swap file and
 * sets `entry` to the swap entry to page was se to.
 * 
 * This will return an error if the page is not owned
 * by a user process, it is shared by more than
 * one process and it's not a single page allocated
 * by the buddy system.
 * 
 * @param page page to swap
 * @param entry swap entry the page will be on
 * @return error if any
 */
int swap_add_page(struct page *page, swap_entry_t *entry)
{
    int retval;

    if (!swap_check_page(page))
        return EINVAL;

    retval = handle_swap_add_page(&swap_mem, page, entry);
    if (retval)
        return retval;

    return 0;
}

int swap_get_page(struct page *page, swap_entry_t swap_entry)
{
    if (!swap_check_page(page))
        return EINVAL;

    return handle_swap_get_page(&swap_mem, page, swap_entry);
}

int swap_inc_page(swap_entry_t entry)
{
    return handle_swap_inc_page(&swap_mem, entry);
}

int swap_dec_page(swap_entry_t entry)
{
    return handle_swap_dec_page(&swap_mem, entry);
}
