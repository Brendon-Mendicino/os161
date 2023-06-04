#include <types.h>
#include <file.h>
#include <refcount.h>
#include <spinlock.h>
#include <lib.h>
#include <limits.h>
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

    file->file_lock = lock_create("file_lock");
    if (!file->file_lock) {
        kfree(file);
        return NULL;
    }

    file->vnode = NULL;

    file->fd = -1;

    file->offset = 0;

    INIT_REFCOUNT(&file->refcount, 1);
    
    return file;
}

void file_destroy(struct file *file)
{
    bool destroy;
    bool success;

    KASSERT(file != NULL);

    lock_acquire(file->file_lock);

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

    lock_release(file->file_lock);

    if (!destroy)
        return;

    lock_destroy(file->file_lock);

    vfs_close(file->vnode);

    kfree(file);
}

int file_copy(struct file *file, struct file **copy)
{
    struct file *new;

    lock_acquire(file->file_lock);
    new = file_create();
    if (!new) {
        lock_release(file->file_lock);
        return ENOMEM;
    }

    new->fd = file->fd;
    new->offset = file->offset;

    vnode_incref(file->vnode);
    new->vnode = file->vnode;

    *copy = new;
    lock_acquire(file->file_lock);

    return 0;
}

void file_add_offset(struct file *file, off_t offset)
{
    lock_acquire(file->file_lock);
    file->offset += offset;
    lock_release(file->file_lock);
}

off_t file_read_offset(struct file *file)
{
    off_t offset;

    lock_acquire(file->file_lock);
    offset = file->offset;
    lock_release(file->file_lock);

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
    struct file *file;
    int fd = 0, i;

    KASSERT(head != NULL);
    KASSERT(head->fd_array != NULL);
    KASSERT(head->open_files > 0);

    lock_acquire(head->table_lock);

    for (i = 0; i < OPEN_MAX; i++) {
        file = head->fd_array[i];
        if (!file)
            continue;

        if (file->fd > fd)
            fd = file->fd;
    }

    lock_release(head->table_lock);

    return fd + 1;
}

struct file_table *file_table_create(void)
{
    struct file_table *ftable;
    int fd;

    ftable = kmalloc(sizeof(struct file_table));
    if (!ftable)
        return NULL;

    ftable->table_lock = lock_create("ftable_lock");
    if (!ftable->table_lock) {
        kfree(ftable);
        return NULL;
    }

    for (fd = 0; fd < OPEN_MAX; fd++)
        ftable->fd_array[fd] = NULL;

    ftable->open_files = 0;

    return ftable;
}

void file_table_destroy(struct file_table *ftable)
{
    KASSERT(ftable != NULL);
    KASSERT(ftable->open_files == 0);

    lock_destroy(ftable->table_lock);
    kfree(ftable);
}

int file_table_add(struct file *file, struct file_table *head)
{
    KASSERT(file != NULL);
    KASSERT(head != NULL);
    /* file is still uninitialized */
    KASSERT(file->fd >= 0);
    KASSERT(refcount_read(&file->refcount) > 0);

    lock_acquire(head->table_lock);

    if (head->open_files == OPEN_MAX) {
        lock_release(head->table_lock);
        return EMFILE;
    }

    KASSERT(head->fd_array[file->fd] == NULL);
    head->fd_array[file->fd] = file;
    head->open_files += 1;

    lock_release(head->table_lock);

    return 0;
}

int file_table_remove(struct file_table *ftable, int fd)
{
    struct file *file;

    KASSERT(ftable != NULL);

    lock_acquire(ftable->table_lock);

    file = ftable->fd_array[fd];
    if (!file) {
        lock_release(ftable->table_lock);
        return ENOENT;
    }

    ftable->fd_array[fd] = NULL;
    ftable->open_files -= 1;

    lock_release(ftable->table_lock);

    file_destroy(file);

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

    KASSERT(ftable->fd_array != NULL);
    KASSERT(ftable->open_files == 0);

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

    KASSERT(ftable->open_files == 3);

    return 0;

bad_init_cleanup_file:
    file_destroy(console_file);
out:
    file_table_clear(ftable);
    return retval;
}

struct file *file_table_get(struct file_table *head, int fd)
{
    struct file *file;

    lock_acquire(head->table_lock);
    file = head->fd_array[fd];
    lock_release(head->table_lock);

    return file;
}

/**
 * @brief removes all the files from the file
 * table calling file_destroy on them
 * 
 * @param ftable 
 */
void file_table_clear(struct file_table *ftable)
{
    int fd;
    struct file *file;

    lock_acquire(ftable->table_lock);

    for (fd = 0; fd < OPEN_MAX && ftable->open_files > 0; fd++) {
        file = ftable->fd_array[fd];
        if (!file)
            continue;

        file_destroy(file);
        ftable->fd_array[fd] = NULL;
        ftable->open_files -= 1;
    }

    lock_release(ftable->table_lock);

    KASSERT(ftable->open_files == 0);
}

/**
 * @brief copies the a file table to a new one
 * increasing the refcount of the inner files,
 * the inner files are not copyied only a reference
 * is kept inside the table.
 * 
 * @param ftable the table to copy from
 * @param copy the file table has to uninitialized
 * @return return error if any
 */
int file_table_copy(struct file_table *ftable, struct file_table *copy)
{
    struct file *file;
    int retval;
    int fd;


    lock_acquire(ftable->table_lock);

    for (fd = 0; fd < OPEN_MAX; fd++) {
        file = ftable->fd_array[fd];
        if (!file)
            continue;
        
        /* increase reference count */
        lock_acquire(file->file_lock);
        retval = refcount_inc_not_zero(&file->refcount);
        if (!retval)
            panic("file_table_copy: tryied to increase 0 reference count\n");
        lock_release(file->file_lock);

        retval = file_table_add(file, copy);
        if (retval)
            goto out;
    }

    lock_release(ftable->table_lock);

    KASSERT(copy->open_files == ftable->open_files);

    return 0;

/* cleanup bad initialization */
out:
    lock_release(ftable->table_lock);
    file_table_clear(copy);
    return retval;
}