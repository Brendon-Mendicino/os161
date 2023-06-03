#ifndef _FILE_H_
#define _FILE_H_


#include <vnode.h>
#include <list.h>
#include <refcount.h>
#include <spinlock.h>
#include <vfs.h>


struct file {
    int fd;
    refcount_t refcount;            /* how many own this struct */
    struct vnode *vnode;

    off_t offset;                   /* offset inside the file */

    struct spinlock file_lock;      /* struct file lock */
};

struct file_table_entry {
    struct list_head file_head;     /* list of files per process */
    struct file *file;
};

struct file_table {
    struct list_head file_head;
    size_t open_files;
};


extern struct file *file_create(void);

extern void file_destroy(struct file *file);

extern int file_copy(struct file *file, struct file **copy);

extern void file_add_offset(struct file *file, off_t offset);

extern off_t file_read_offset(struct file *file);

extern int file_next_fd(struct file_table *head);

extern int file_table_add(struct file *file, struct file_table *head);

extern int file_table_init(struct file_table *ftable);

extern struct file *file_table_get(struct file_table *head, int fd);

extern void file_table_clear(struct file_table *ftable);

extern int file_table_copy(struct file_table *ftable, struct file_table *copy);

#endif // _FILE_H_