#include <file.h>
#include <refcount.h>
#include <spinlock.h>

struct file *file_create(void)
{
    struct file *file;

    file = kmalloc(sizeof(struct file));
    if (!file)
        return NULL;

    file->vnode = NULL;

    file->fd = -1;

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
}

static int __file_next_fd(struct file *head) 
{
    KASSERT(head != NULL);
    KASSERT(!list_empty(&head->file_head));

    struct file *last = list_last_entry(&head->file_head, struct file, file_head);

    return last->fd;
}

int file_next_fd(struct file_table *head)
{
    KASSERT(head != NULL);
    KASSERT(!list_empty(&head->file_head));

    struct file *file = list_last_entry(&head->file_head, struct file, file_head);

    return file->fd;
}

void file_table_add(struct file *file, struct file_table *head)
{
    list_add_tail(&file->file_head, &head->file_head);
}