#ifndef _ATOMIC_H_
#define _ATOMIC_H_

#include <cdefs.h>
#include <types.h>
#include <rwonce.h>
#include <machine/synch.h>


typedef volatile struct atomic_t {
    int counter;
} atomic_t;

#define ATOMIC_INIT(initial_counter)  ((atomic_t){ .counter = (initial_counter) })

static inline int atomic_read(const atomic_t *atomic)
{
    return READ_ONCE(atomic->counter);
}

static inline void atomic_set(atomic_t *atomic, int i)
{
    WRITE_ONCE(atomic->counter, i);
}

/*
 * Test-and-set an atomic_t. Use the LL/SC instructions to
 * make it atomic.
 *
 * LL (load linked) loads a machine word from memory, and marks the
 * address. SC (store conditional) stores a machine word to memory,
 * but succeeds only if the address is marked from a previous LL on
 * the same processor. Stores from other processors clear that mark,
 * as do traps on the current processor. Note that there may be no
 * other memory accesses (besides instruction fetches) between the LL
 * and the SC or the behavior is *undefined*. You can only use LL/SC
 * to atomically update one machine word.
 */
static inline bool
atomic_testandset(atomic_t *atomic)
{
	bool x;
	bool y;

	/*
	 * Test-and-set using LL/SC.
	 *
	 * Load the existing value into X, and use Y to store 1.
	 * After the SC, Y contains 1 if the store succeeded,
	 * 0 if it failed.
	 *
	 * On failure, return 1 to pretend that the spinlock
	 * was already held.
	 */

	y = true;
	__asm volatile(
		".set push;"		/* save assembler mode */
		".set mips32;"		/* allow MIPS32 instructions */
		".set volatile;"	/* avoid unwanted optimization */
		"ll %0, 0(%2);"		/*   x = *sd */
		"sc %1, 0(%2);"		/*   *sd = y; y = success? */
		".set pop"		/* restore assembler mode */
		: "=&r" (x), "+r" (y) : "r" (&atomic->counter));

	if (y == false) {
		return true;
	}
	return x;
}

static inline int
atomic_fetch_add(atomic_t *atomic, int val)
{
    int temp;
    int result;

	/*
	 * Test-and-set using LL/SC.
	 *
	 * Load the existing value into X, and use Y to store 1.
	 * After the SC, Y contains 1 if the store succeeded,
	 * 0 if it failed.
	 *
	 * On failure, return 1 to pretend that the spinlock
	 * was already held.
	 */

    __asm volatile(
		".set push;"		/* save assembler mode */
		".set mips32;"		/* allow MIPS32 instructions */
        "sync;"             /* memory barrier for previous read/write */
		".set volatile;"	/* avoid unwanted optimization */
		"1: ll %1, 0(%2);"		/*   temp = atomic->val */
        "add %0, %1, %3;"    /*   result = temp + val */
		"sc %0, 0(%2);"		/*   *sd = result; result = success? */
        "beqz %0, 1b;"
		".set pop;"		/* restore assembler mode */
        "move %0, %1;"       /*   result = temp */
		: "=&r" (result), "=&r" (temp)  //, "+ZC" (atomic->counter)  // ZC: A memory operand whose address is formed by a base register and offset that is suitable for use in instructions with the same addressing mode as ll and sc.
        : "r" (&atomic->counter), "Ir" (val)
        : "memory");        /* memory barrier for the current assembly block */

    return result;
}



#endif // _ATOMIC_H_