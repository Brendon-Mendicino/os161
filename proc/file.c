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
    
    spinlock_init(&file->file_lock);

    return file;
}

static struct file_table_entry *__table_entry_create(void)
{
    struct file_table_entry *entry;

    entry = kmalloc(sizeof(struct file_table_entry));
    if (!entry)
        return NULL;

    entry->file = NULL;
    INIT_LIST_HEAD(&entry->file_head);

    return entry;
}

// static void __table_entry_destroy(struct file_table_entry *entry)
// {
//     KASSERT(entry != NULL);

//     list_del(&entry->file_head);

//     file_destroy(entry->file);

//     kfree(entry);
// }

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

int file_copy(struct file *file, struct file **copy)
{
    struct file *new;

    new = file_create();
    if (!new)
        return ENOMEM;

    new->fd = file->fd;
    new->offset = file->offset;

    vnode_incref(file->vnode);
    new->vnode = file->vnode;

    *copy = new;

    return 0;
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

    struct file_table_entry *entry = list_last_entry(&head->file_head, struct file_table_entry, file_head);
    struct file *file = entry->file;

    return file->fd + 1;
}

int file_table_add(struct file *file, struct file_table *head)
{
    KASSERT(file != NULL);
    KASSERT(head != NULL);
    /* file is still uninitialized */
    KASSERT(file->fd >= 0);

    struct file_table_entry *entry = __table_entry_create();
    if (!entry)
        return ENOMEM;

    entry->file = file;

    list_add_tail(&entry->file_head, &head->file_head);

    return 0;
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
        if (!console) {
            retval = ENOMEM;
            goto out;
        }

        retval = vfs_open(console, openflag[fd], 0, &console_vnode);
        kfree(console);
        if (retval)
            goto out;

        console_file = file_create();
        if (!console_file) {
            vfs_close(console_vnode);
            retval =  ENOMEM;
            goto out;
        }

        console_file->fd = fd;
        console_file->vnode = console_vnode;

        retval = file_table_add(console_file, ftable);
        if (retval)
            goto bad_init_cleanup_file;
    }

    return 0;

bad_init_cleanup_file:
    file_destroy(console_file);
out:
    file_table_clear(ftable);
    return retval;
}

struct file *file_table_get(struct file_table *head, int fd)
{
    struct file_table_entry *entry;
    bool found = false;

    list_for_each_entry(entry, &head->file_head, file_head) {
        if (entry->file->fd == fd) {
            found = true;
            break;
        }
    }

    return (found) ? entry->file : NULL;
}

void file_table_clear(struct file_table *ftable)
{
    struct file_table_entry *file_entry, *n;

    list_for_each_entry_safe(file_entry, n, &ftable->file_head, file_head) {
        list_del(&file_entry->file_head);
        file_destroy(file_entry->file);
        kfree(file_entry);
    }
}