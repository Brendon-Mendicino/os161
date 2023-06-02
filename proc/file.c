#include <types.h>
#include <file.h>
#include <refcount.h>
#include <spinlock.h>
#include <lib.h>
#include <kern/fcntl.h>
#include <kern/errno.h>

/**
 * @brief return a newly created struct file
 * the vnode and the fd must be after the
 * file creation.
 * 
 * @return struct file* 
 */
struct file *file_create(void)
{
    struct file *file;

    file = kmalloc(sizeof(struct file));
    if (!file)
        return NULL;

    file->vnode = NULL;

    file->fd = -1;

    file->offset = 0;

    INIT_REFCOUNT(&file->refcount, 1);
    
    INIT_LIST_HEAD(&file->file_head);
    
    spinlock_init(&file->file_lock);

    return file;
}

void file_destroy(struct file *file)
{
    bool destroy;
    bool success;

    KASSERT(file != NULL);

    spinlock_acquire(&file->file_lock);

    success = refcount_dec_not_zero(&file->refcount);
    if (!success) {
        panic("file_destroy: Decreased non-zero refcount\n");
    }

    if (refcount_read(&file->refcount) > 0) {
        destroy = false;
    }
    else {
        destroy = true;
    }

    spinlock_release(&file->file_lock);

    if (!destroy)
        return;

    vfs_close(file->vnode);

    kfree(file);
}

void file_add_offset(struct file *file, off_t offset)
{
    spinlock_acquire(&file->file_lock);
    file->offset += offset;
    spinlock_release(&file->file_lock);
}

off_t file_read_offset(struct file *file)
{
    off_t offset;

    spinlock_acquire(&file->file_lock);
    offset = file->offset;
    spinlock_release(&file->file_lock);

    return offset;
}

// static int __file_next_fd(struct file *head) 
// {
//     KASSERT(head != NULL);
//     KASSERT(!list_empty(&head->file_head));

//     struct file *last = list_last_entry(&head->file_head, struct file, file_head);

//     return last->fd;
// }

int file_next_fd(struct file_table *head)
{
    KASSERT(head != NULL);
    KASSERT(!list_empty(&head->file_head));

    struct file *file = list_last_entry(&head->file_head, struct file, file_head);

    return file->fd + 1;
}

void file_table_add(struct file *file, struct file_table *head)
{
    KASSERT(file != NULL);
    KASSERT(head != NULL);
    /* file is still uninitialized */
    KASSERT(file->fd >= 0);

    list_add_tail(&file->file_head, &head->file_head);
}

/**
 * @brief initialized the file table with
 * the basic 3 file descriptor of stdin,
 * stdout, stderr.
 * 
 * @param ftable 
 * @return int 
 */
int file_table_init(struct file_table *ftable)
{
    int fd;
    int retval;
    struct vnode *console_vnode;
    struct file *console_file;
    int openflag[3] = { O_RDONLY, O_WRONLY, O_WRONLY };


    INIT_LIST_HEAD(&ftable->file_head);

    for (fd = 0; fd < 3; fd++) {
        /*
         * The "con:" string when calling
         * vfs_open represnt the console device
         */
        char *console = kstrdup("con:");
        if (!console)
            return ENOMEM;

        retval = vfs_open(console, openflag[fd], 0, &console_vnode);
        kfree(console);
        if (retval)
            return retval;

        console_file = file_create();
        if (!console_file) {
            vfs_close(console_vnode);
            return ENOMEM;
        }

        console_file->fd = fd;
        console_file->vnode = console_vnode;

        file_table_add(console_file, ftable);
    }

    return 0;
}

struct file *file_table_get(struct file_table *head,int fd)
{
    struct file *file;
    bool found = false;

    list_for_each_entry(file, &head->file_head, file_head) {
        if (file->fd == fd) {
            found = true;;
            break;
        }
    }

    return (found) ? file : NULL;
}

void file_table_clear(struct file_table *ftable)
{
    struct file *file, *n;

    list_for_each_entry_safe(file, n, &ftable->file_head, file_head) {
        list_del(&file->file_head);
        file_destroy(file);
    }
}