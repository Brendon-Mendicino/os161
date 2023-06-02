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
#include <kern/unistd.h>

int sys_write(int fd, const_userptr_t buf, size_t nbyte, size_t *size_wrote)
{
#if OPT_SYSFS
    struct proc *curr;
    struct file *file;
    struct iovec iovec;
    struct uio uio;
    void *kbuf;
    int result;

    KASSERT(curproc != NULL);

    curr = curproc;

    kbuf = kmalloc(nbyte);
    if (!kbuf)
        return ENOMEM;

    file = proc_get_file(curr, fd);
    if (!file)
        return ENOENT;

    /* copy from userspace to kernel buffer */
    result = copyin(buf, kbuf, nbyte);
    if (result)
        return result;

    uio_kinit(&iovec, &uio, kbuf, nbyte, file_read_offset(file), UIO_WRITE);

    result = VOP_WRITE(file->vnode, &uio);
    if (result)
        return result;

    /* check if all the bytes were written */
    *size_wrote = nbyte - uio.uio_resid;
    file_add_offset(file, (off_t)*size_wrote);

    kfree(kbuf);

    return 0;
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
    struct iovec iovec;
    struct uio uio;
    void *kbuf;
    int result;

    KASSERT(curproc != NULL);

    curr = curproc;

    kbuf = kmalloc(nbyte);
    if (!kbuf)
        return ENOMEM;

    file = proc_get_file(curr, fd);
    if (!file)
        return ENOENT;

    uio_kinit(&iovec, &uio, kbuf, nbyte, file_read_offset(file), UIO_READ);

    result = VOP_READ(file->vnode, &uio);
    if (result)
        return result;

    result = copyout(kbuf, buf, nbyte);
    if (result)
        return result;

    /* check if all the bytes were read */
    *size_read = nbyte - uio.uio_resid;
    file_add_offset(file, (off_t)*size_read);

    return 0;

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
    (void)fd;
    return 0;
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
#endif // OPT_SYSFS