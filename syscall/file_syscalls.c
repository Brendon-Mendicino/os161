#include <syscall.h>
#include <lib.h>
#include <vfs.h>
#include <file.h>
#include <current.h>
#include <proc.h>
#include <uio.h>
#include <limits.h>
#include <copyinout.h>
#include <kern/iovec.h>
#include <kern/errno.h>
#include <kern/seek.h>
#include <kern/unistd.h>
#include <kern/stat.h>

static inline bool check_whence(int whence)
{
    return (whence == SEEK_CUR) || (whence == SEEK_SET) || (whence == SEEK_END);
}

int sys_dup2(int oldfd, int newfd)
{
    struct proc *proc = curproc;
    KASSERT(proc != NULL);

    return file_table_dup2(proc->ftable, oldfd, newfd);
}

int sys_write(int fd, const_userptr_t buf, size_t nbyte, size_t *size_wrote)
{
#if OPT_SYSFS
    struct proc *curr;
    struct file *file;
    void *kbuf;
    int result;

    KASSERT(curproc != NULL);

    curr = curproc;

    file = proc_get_file(curr, fd);
    if (!file)
        return EBADF;

    kbuf = kmalloc(nbyte);
    if (!kbuf)
        return ENOMEM;

    /* copy from userspace to kernel buffer */
    result = copyin(buf, kbuf, nbyte);
    if (result)
        goto out;

    result = file_write(file, kbuf, nbyte, size_wrote);

out:
    kfree(kbuf);
    return result;
#else // OPT_SYSFS
    if (fd != STDOUT_FILENO && fd != STDERR_FILENO)
    {
        kprintf("Error: writing to a file!\n");
        return ENOSYS;
    }

    for (size_t i = 0; i < nbyte; i++)
        putch(((uint8_t *)buf)[i]);

    *size_wrote = nbyte;

    return 0;
#endif // OPT_SYSFS
}

int sys_read(int fd, userptr_t buf, size_t nbyte, size_t *size_read)
{
#if OPT_SYSFS
    struct proc *curr;
    struct file *file;
    void *kbuf;
    int result;

    KASSERT(curproc != NULL);

    curr = curproc;

    file = proc_get_file(curr, fd);
    if (!file)
        return EBADF;

    kbuf = kmalloc(nbyte);
    if (!kbuf)
        return ENOMEM;

    result = file_read(file, kbuf, nbyte, size_read);
    if (result)
        goto out;

    result = copyout(kbuf, buf, nbyte);

out:
    kfree(kbuf);
    return result;
#else // OPT_SYSFS
    if (fd != STDOUT_FILENO && fd != STDERR_FILENO)
    {
        kprintf("Error: reading from a file!\n");
        return ENOSYS;
    }

    for (size_t i = 0; i < nbyte; i++)
        ((uint8_t *)buf)[i] = (uint8_t)getch();

    *size_read = nbyte;

    return 0;
#endif // OPT_SYSFS
}

int sys_lseek(int fd, off_t offset, int whence, off_t *offset_location)
{
    struct file *file;
    int retval;

    KASSERT(curcpu != NULL);

    if (!check_whence(whence))
        return EINVAL;

    file = proc_get_file(curproc, fd);
    if (!file)
        return EBADF;

    retval = file_lseek(file, offset, whence, offset_location);
    if (retval)
        return retval;

    return 0;
}

#if OPT_SYSFS
int sys_open(const_userptr_t pathname, int flags, mode_t mode, int *fd)
{
    struct proc *curr;
    struct file *new_file;
    struct vnode *vnode;
    char kpathname[PATH_MAX];
    int retval;

    KASSERT(curproc != NULL);

    curr = curproc;

    new_file = file_create();
    if (!new_file)
        return ENOMEM;

    retval = copyinstr(pathname, kpathname, sizeof(kpathname), NULL);
    if (retval)
        return retval;

    retval = vfs_open(kpathname, flags, mode, &vnode);
    if (retval)
        goto bad_open_cleanup;

    /* add vnode to the new_file */
    new_file->vnode = vnode;

    *fd = proc_add_new_file(curr, new_file);
    
    return 0;

bad_open_cleanup:
    kfree(new_file);
    return retval;
}

int sys_close(int fd)
{
    KASSERT(curproc != NULL);

    return proc_removed_file(curproc, fd);
}

int sys_remove(const_userptr_t path)
{
    int retval;
    char kpath[PATH_MAX];

    retval = copyinstr(path, kpath, sizeof(kpath), NULL);
    if (retval)
        return retval;

    retval = vfs_remove(kpath);
    if (retval)
        return retval;

    return 0;
}

int sys_fstat(int fd, userptr_t statbuf)
{
    int retval;
    struct stat stat;

    struct proc *proc = curproc;
    KASSERT(proc != NULL);

    struct file *file = proc_get_file(proc, fd);
    if (!file) 
        return EBADF;

    retval = VOP_STAT(file->vnode, &stat);
    if (retval)
        return retval;

    retval = copyout(&stat, statbuf, sizeof(struct stat));
    if (retval)
        return retval;

    return 0;
}
#endif // OPT_SYSFS