#ifndef _FILE_H_
#define _FILE_H_


#include <vnode.h>
#include <list.h>
#include <refcount.h>
#include <spinlock.h>
#include <vfs.h>


struct file {
    int fd;

    refcount_t refcount;
    struct vnode *vnode;

    struct spinlock file_lock;

    struct list_head file_head;
};

struct file_table {
    struct list_head file_head;
};


extern struct file *file_create(void);

extern void file_destroy(struct file *file);

extern int file_next_fd(struct file_table *head);

extern void file_table_add(struct file *file, struct file_table *head);

#endif // _FILE_H_