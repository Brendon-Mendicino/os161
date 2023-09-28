#include <types.h>
#include <file.h>
#include <refcount.h>
#include <spinlock.h>
#include <lib.h>
#include <limits.h>
#include <uio.h>
#include <stat.h>
#include <kern/seek.h>
#include <kern/fcntl.h>
#include <kern/errno.h>

static bool check_fd(int fd)
{
    if (fd < 0)
        return false;

    if (fd >= OPEN_MAX)
        return false;

    return true;
}

/**
 * @brief return a newly created struct file
 * the vnode and the fd must be set after the
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

    file->refcount = REFCOUNT_INIT(1);
    
    return file;
}

void file_destroy(struct file *file)
{
    bool destroy;

    KASSERT(file != NULL);

    /* destroy the file if refcount is 0 */
    destroy = refcount_dec(&file->refcount) == 0;

    if (!destroy)
        return;

    lock_destroy(file->file_lock);

    vfs_close(file->vnode);

    kfree(file);
}

int file_read(struct file *file, void *kbuf, size_t nbyte, size_t *byte_read)
{
    struct iovec iovec;
    struct uio uio;
    int retval;

    KASSERT(file != NULL);

    lock_acquire(file->file_lock);
    uio_kinit(&iovec, &uio, kbuf, nbyte, file->offset, UIO_READ);

    retval = VOP_READ(file->vnode, &uio);
    if (retval) {
        lock_release(file->file_lock);
        return retval;
    }

    /* check if all the bytes were read */
    *byte_read = nbyte - uio.uio_resid;
    file->offset += (off_t)*byte_read;
    lock_release(file->file_lock);

    return 0;
}

int file_write(struct file *file, void *kbuf, size_t nbyte, size_t *byte_wrote)
{
    struct iovec iovec;
    struct uio uio;
    int retval;

    lock_acquire(file->file_lock);
    uio_kinit(&iovec, &uio, kbuf, nbyte, file->offset, UIO_WRITE);

    retval = VOP_WRITE(file->vnode, &uio);
    if (retval) {
        lock_release(file->file_lock);
        return retval;
    }

    /* check if all the bytes were written */
    *byte_wrote = nbyte - uio.uio_resid;
    file->offset += (off_t)*byte_wrote;
    lock_release(file->file_lock);

    return 0;
}

int file_lseek(struct file *file, off_t offset, int whence, off_t *offset_location)
{
    struct stat file_stat;

    lock_acquire(file->file_lock);

    VOP_STAT(file->vnode, &file_stat);

    if (whence == SEEK_SET) {
        if (offset < 0)
            return EINVAL;        

        file->offset = offset;
    } else if (whence == SEEK_CUR) {
        if (file->offset + offset < 0)
            return EINVAL;

        file->offset += offset;
    } else if (whence == SEEK_END) {
        if (file_stat.st_size + offset < 0)
            return EINVAL;

        file->offset = file_stat.st_size + offset;
    } else {
        return EINVAL;
    }

    *offset_location = file->offset;

    lock_release(file->file_lock);

    return 0;
}

int file_copy(struct file *file, struct file **copy)
{
    struct file *new;

    new = file_create();
    if (!new)
        return ENOMEM;

    lock_acquire(file->file_lock);
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

    // TODO: remove kmalloc
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

    if (!check_fd(fd))
        return EBADF;

    lock_acquire(ftable->table_lock);

    file = ftable->fd_array[fd];
    if (!file) {
        lock_release(ftable->table_lock);
        return EBADF;
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

    if (fd < 0 || fd >= OPEN_MAX)
        return NULL;

    lock_acquire(head->table_lock);
    file = head->fd_array[fd];
    lock_release(head->table_lock);

    // TODO: remember to increase refcount of file
    return file;
}

int file_table_dup2(struct file_table *ftable, int oldfd, int newfd)
{
    if (!check_fd(oldfd) || !check_fd(newfd))
        return EBADF;

    lock_acquire(ftable->table_lock);

    struct file *old_file = ftable->fd_array[oldfd];
    if (!old_file) {
        lock_release(ftable->table_lock);
        return EBADF;
    }

    struct file *new_file = ftable->fd_array[newfd];
    if (new_file) {
        file_destroy(new_file);
    }
    
    refcount_inc(&old_file->refcount);
    ftable->fd_array[newfd] = old_file;
    
    lock_release(ftable->table_lock);

    return 0;
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

        ftable->fd_array[fd] = NULL;
        ftable->open_files -= 1;
        file_destroy(file);
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
        
        refcount_inc(&file->refcount);

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
    return retval;
}