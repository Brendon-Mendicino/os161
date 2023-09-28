#ifndef _FILE_H_
#define _FILE_H_


#include <types.h>
#include <limits.h>
#include <vnode.h>
#include <list.h>
#include <refcount.h>
#include <spinlock.h>
#include <vfs.h>
#include <synch.h>


struct file {
    int fd;
    refcount_t refcount;            /* how many own this struct */
    struct vnode *vnode;

    off_t offset;                   /* offset inside the file */

    struct lock *file_lock;      /* struct file lock */
};

struct file_table {
    size_t open_files;                  /* counts the number of open files */
    struct lock *table_lock;            /* locks the fd_array */
    struct file *fd_array[OPEN_MAX];    /* linear array of open files */
};


extern struct file *file_create(void);

extern void file_destroy(struct file *file);

extern int file_read(struct file *file, void *kbuf, size_t nbyte, size_t *byte_read);

extern int file_write(struct file *file, void *kbuf, size_t nbyte, size_t *byte_wrote);

extern int file_lseek(struct file *file, off_t offset, int whence, off_t *offset_location);

extern int file_copy(struct file *file, struct file **copy);

extern void file_add_offset(struct file *file, off_t offset);

extern off_t file_read_offset(struct file *file);

extern int file_next_fd(struct file_table *head);

extern struct file_table *file_table_create(void);

extern void file_table_destroy(struct file_table *ftable);

extern int file_table_add(struct file *file, struct file_table *head);

extern int file_table_remove(struct file_table *ftable, int fd);

extern int file_table_init(struct file_table *ftable);

extern struct file *file_table_get(struct file_table *head, int fd);

extern int file_table_dup2(struct file_table *ftable, int oldfd, int newfd);

extern void file_table_clear(struct file_table *ftable);

extern int file_table_copy(struct file_table *ftable, struct file_table *copy);

#endif // _FILE_H_