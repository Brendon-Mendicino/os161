#include <syscall.h>
#include <lib.h>
#include <vfs.h>
#include <file.h>
#include <current.h>
#include <proc.h>
#include <kern/errno.h>
#include <kern/unistd.h>

int sys_write(int fd, const_userptr_t buf, size_t nbyte, size_t *size_wrote)
{
    ssize_t wrote_bytes = 0;

    // to be implemented
    if (fd != STDOUT_FILENO && fd != STDERR_FILENO)
    {
        kprintf("Error: writing to a file!\n");
        return -1;
    }

    for (size_t i = 0; i < nbyte; i++)
    {
        putch(((uint8_t *)buf)[i]);
        wrote_bytes++;
    }

    return wrote_bytes;
}

int sys_read(int fd, const_userptr_t buf, size_t nbyte, size_t *size_read)
{
    ssize_t read_bytes = 0;

    if (fd != STDOUT_FILENO && fd != STDERR_FILENO)
    {
        kprintf("Error: reading from a file!\n");
        return -1;
    }

    for (size_t i = 0; i < nbyte; i++)
    {
        ((uint8_t *)buf)[i] = (uint8_t)getch();
        read_bytes++;
    }

    return read_bytes;
}


int sys_open(const_userptr_t pathname, int flags, mode_t mode, int *fd)
{
    struct proc *curr;
    struct file *new_file;
    struct vnode *vnode;
    int retval;

    KASSERT(curproc != NULL);

    curr = curproc;

    new_file = file_create();
    if (!new_file)
        return ENOMEM;

    retval = vfs_open(pathname, flags, mode, &vnode);
    if (retval)
        goto bad_open_cleanup;

    /* add vnode to the new_file */
    new_file->vnode = vnode;

    *fd = proc_add_new_file(curr, new_file);
    
    return 0;

bad_open_cleanup:
    file_destroy(new_file);
    return retval;
}

int sys_close(int fd)
{

}