#include <test.h>
#include <lib.h>
#include <thread.h>
#include <current.h>
#include <synch.h>
#include <proc.h>
#include <kern/errno.h>
#include <machine/atomic.h>

#define UPPER_LIMIT 100000

static atomic_t counter;
static int n_thread;

static void thread_worker_test(struct semaphore *sem, unsigned long none)
{
    int i;
    int val, prev = 0;
    (void)none;

    for (i = 0; i < UPPER_LIMIT / n_thread; i++) {
        val = atomic_fetch_add(&counter, 1);
        KASSERT(val + 1 > prev);
    }

    V(sem);
}

int atmu1(int argc, char **argv)
{
    struct proc *curr;
    struct semaphore *sem;

    if (argc != 2) {
        kprintf("\nWrong usage: atmu1 nthread\n\n");
        return EINVAL;
    }

    counter = ATOMIC_INIT(0);
    n_thread = atoi(argv[1]);
    curr = curproc;

    sem = sem_create("atmu1", 0);
    if (!sem)
        return ENOMEM;
    
    kprintf("Spawning %d threads...\n\n", n_thread);
    for (int i = 0; i < n_thread; i++) 
        thread_fork("atmu1", curr, (void (*)(void *, unsigned long))thread_worker_test, sem,0);

    kprintf("Wating for threads...\n\n");
    /* wait for all the tread to finish */
    for (int i = 0; i < n_thread; i++)
        P(sem);

    /* check if the counter is the sum of all the threads */
    KASSERT(atomic_read(&counter) == (UPPER_LIMIT / n_thread) * n_thread);

    kprintf("Total sum was: %d, must be: %d\n", atomic_read(&counter), UPPER_LIMIT);

    return 0;
}
