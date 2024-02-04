#include <proc.h>
#include <exec.h>
#include <addrspace.h>
#include <copyinout.h>
#include <lib.h>
#include <syscall.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>

static int exec_new_as(char *pathname, int argc, char **argv, struct exec_params *params) {
	struct addrspace *as;
	struct vnode *vnode;
	int retval;

	/* Open the file. */
	retval = vfs_open(pathname, O_RDONLY, 0, &vnode);
	if (retval)
		return retval;

	/* Create a new address space. */
	as = as_create();
	if (as == NULL) {
        retval = ENOMEM;
        goto vnode_cleanup;
	}

    /* file needs to be attached before the as can be deleted */
	/* attach the file to address space */
	as->source_file = vnode;

	/* Load the executable. */
	retval = load_elf(as, vnode, &params->entrypoint);
	if (retval)
        goto as_cleanup;

// #if OPT_PAGING
// 	/* attach the file to address space */
// 	as->source_file = vnode;
// #else // OPT_PAGING
// 	/* Done with the file now. */
// 	vfs_close(vnode);
// #endif // OPT_PAGING

    /* Define the user args in the address space. */
	retval = as_define_args(as, argc, argv, &params->uargv);
	if (retval)
		goto as_cleanup;

	/* Define the user stack in the address space. */
	retval = as_define_stack(as, &params->stackprt);
	if (retval)
        goto as_cleanup;

	/* Switch to it and activate it. */
	proc_setas(as);
	as_activate();

    return 0;

as_cleanup:
    as_destroy(as);

vnode_cleanup:
	/* p_addrspace will go away when curproc is destroyed */
    vfs_close(vnode);

    return retval;
}

int sys_execv(const_userptr_t pathname, userptr_t argv) {
    int retval = 0;
    size_t argc = 0;
    size_t argv_len = 0;

    char *kern_pahtname = kmalloc(__PATH_MAX);
    if (kern_pahtname == NULL)
        return ENOMEM;

    retval = copyinstr(pathname, kern_pahtname, __PATH_MAX, NULL);
    if (retval)
        goto pathname_cleanup;

    /*
     * It's important to remember that argv pointer and the 
     * actual argumets will share the memory space in the address
     * space, in this section of code the two lists are overallocated
     * then the copy will be handeled later.
     */
    
    char **kern_argv = kmalloc(__ARG_MAX / sizeof(char *));
    if (kern_argv == NULL) {
        retval = ENOMEM;
        goto pathname_cleanup;
    }

    char *argv_space = kmalloc(__ARG_MAX);
    if (argv_space == NULL) {
        retval = ENOMEM;
        goto kern_argv_cleanup;
    }

    for (;;) {
        char **argv_cast = (char **)argv;
        const_userptr_t single_arg;
        size_t got;

        retval = copyin((const_userptr_t)&argv_cast[argc], &single_arg, sizeof(single_arg));
        if (retval)
            goto argv_space_cleanup;

        if (single_arg == NULL)
            break;

        if ((__ARG_MAX - (ssize_t)(argv_len + (argc + 1) * sizeof(char *))) < 0) {
            retval = E2BIG;
            goto argv_space_cleanup;
        }

        char *curr_argv_space = argv_space + argv_len;
        retval = copyinstr(single_arg, curr_argv_space, __ARG_MAX - (ssize_t)(argv_len + (argc + 1) * sizeof(char *)), &got);
        if (retval) {
            if (retval == ENAMETOOLONG)
                retval = E2BIG;
            goto argv_space_cleanup;
        }

        kern_argv[argc] = curr_argv_space;

        argv_len += got;
        argc += 1; 
    }

    /* Last argumt must be NULL. */
    kern_argv[argc] = NULL;

	/* We should be a process. */
	KASSERT(proc_getas() != NULL);

    struct exec_params params;
    retval = exec_new_as(kern_pahtname, argc, kern_argv, &params);


argv_space_cleanup:
    kfree(argv_space);
    
kern_argv_cleanup:
    kfree(kern_argv);

pathname_cleanup:
    kfree(kern_pahtname);

    if (retval)
        return retval;

    enter_new_process(
        (int)argc,
        params.uargv,
        NULL,
        params.stackprt,
        params.entrypoint);

    panic("Process returned from `enter_new_process()`!");
}

