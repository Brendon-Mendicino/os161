#include <proc.h>
#include <thread.h>
#include <kern/wait.h>

/**
 * Kernel process that will have all the orphaned
 * children of the system.
 * 
 */
struct proc orphanage = {
	.p_name           = (char *)"[orphanage]",
	.p_lock           = SPINLOCK_INITIALIZER,
	.p_numthreads     = 0,
	.p_addrspace      = NULL,
	.p_cwd            = NULL,
	.wait_cv          = NULL,
	.wait_lock        = NULL,
	.wait_sem         = NULL,
    .state            = PROC_RUNNING,
    .exit_state       = PROC_RUNNING,
    .children         = LIST_HEAD_INIT(orphanage.children),
	.siblings         = LIST_HEAD_INIT(orphanage.siblings),
	.parent           = NULL,
    .pid              = 0,
	.pid_link         = { .next = NULL, .pprev = NULL },
};

static void free_orphaned_children(void *ign, unsigned long ign2) {
    (void)ign;
    (void)ign2;

    struct proc *child;
    struct proc *temp;

    // TODO: insert a termination condition on shutdown
    for (;;) {

        spinlock_acquire(&orphanage.p_lock);
        proc_for_each_child(child, temp, (&orphanage)) {
            int wstatus;
            spinlock_release(&orphanage.p_lock);

            proc_check_zombie(child, &wstatus, WNOHANG, &orphanage);
            thread_yield();

            spinlock_acquire(&orphanage.p_lock);
        }
        spinlock_release(&orphanage.p_lock);

        thread_yield();
    }
}

void kproc_bootstrap(void) {
    /* Add `orphanage` to `kproc` children. */
    list_add_tail(&orphanage.siblings, &kproc.children);
    thread_fork("orphanage", &orphanage, free_orphaned_children, NULL, 0);
}