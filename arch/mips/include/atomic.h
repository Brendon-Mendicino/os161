#ifndef _ATOMIC_H_
#define _ATOMIC_H_

#include <cdefs.h>
#include <types.h>
#include <rwonce.h>
#include <machine/synch.h>

typedef volatile struct atomic_t {
    int counter;
} atomic_t;

#define ATOMIC_INIT(initial_counter) {.counter = (initial_counter)}

static inline void INIT_ATOMIC(atomic_t *atomic, int value)
{
    atomic->counter = value;
}

static inline int atomic_read(const atomic_t *atomic)
{
    return READ_ONCE(atomic->counter);
}

static inline void atomic_set(atomic_t *atomic, int i)
{
    WRITE_ONCE(atomic->counter, i);
}

/**
 * @brief atomically add the value, the code does not return
 * until the operation is completed.
 * 
 * @param atomic atomic varaible
 * @param val value to add
 */
static inline void
atomic_add(atomic_t *atomic, int val)
{
    int temp;
    int result;

    __asm volatile(
        "    .set push;"       /* save assembler mode */
        "    .set mips32;"     /* allow MIPS32 instructions */
        "    sync;"            /* memory barrier for previous read/write */
        "    .set volatile;"   /* avoid unwanted optimization */
        "1:  ll   %1, 0(%2);"  /*   temp = atomic->val */
        "    add  %0, %1, %3;" /*   result = temp + val */
        "    sc   %0, 0(%2);"  /*   *sd = result; result = success? */
        "    beqz %0, 1b;"     /* try again if there is a failure */
        "    .set pop;"        /* restore assembler mode */
        : "=&r"(result), "=&r"(temp)
        : "r"(&atomic->counter), "Ir"(val)
        : "memory"); /* memory barrier for the current assembly block */
}

/**
 * @brief atomically add the value, the code does not return
 * until the operation is completed and the return value will
 * be the previous value of the atomic.
 * 
 * @param atomic atomic varaible
 * @param val value to add
 * @return retruns the previous value of the atomc
 */
static inline int
atomic_fetch_add(atomic_t *atomic, int val)
{
    int temp;
    int result;

    __asm volatile(
        "    .set push;"       /* save assembler mode */
        "    .set mips32;"     /* allow MIPS32 instructions */
        "    sync;"            /* memory barrier for previous read/write */
        "    .set volatile;"   /* avoid unwanted optimization */
        "1:  ll   %1, 0(%2);"  /*   temp = atomic->val */
        "    add  %0, %1, %3;" /*   result = temp + val */
        "    sc   %0, 0(%2);"  /*   *sd = result; result = success? */
        "    beqz %0, 1b;"     /* try again if there is a failure */
        "    .set pop;"        /* restore assembler mode */
        "    move %0, %1;"     /*   result = temp */
        : "=&r"(result), "=&r"(temp)
        : "r"(&atomic->counter), "Ir"(val)
        : "memory"); /* memory barrier for the current assembly block */

    return result;
}

#endif // _ATOMIC_H_