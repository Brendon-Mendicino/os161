#ifndef _REFCOUNT_H_
#define _REFCOUNT_H_

#include <types.h>
#include <machine/atomic.h>
#include <lib.h>


typedef struct refcount {
    atomic_t count;
} refcount_t;


#define REFCOUNT_INIT(initial_counter)    ((refcount_t){ .count = ATOMIC_INIT(initial_counter) })


static inline unsigned int refcount_read(refcount_t *r)
{
    return atomic_read(&r->count);
}

static inline unsigned int refcount_inc(refcount_t *r)
{
    int initial_count = atomic_fetch_add(&r->count, 1);
    if (initial_count == 0)
        panic("Tried to increase a refcount of 0!\n");

    return initial_count + 1;
}

static inline bool refcount_inc_not_zero(refcount_t *r, unsigned int *final_count)
{
    int initial_count = atomic_fetch_add(&r->count, 1);
    if (initial_count == 0) {
        atomic_add(&r->count, -1);
        *final_count = 0;
        return false;
    }

    *final_count = initial_count + 1;
    return true;
}

static inline unsigned int refcount_dec(refcount_t *r)
{
    int initial_count = atomic_fetch_add(&r->count, -1);
    if (initial_count == 0)
        panic("Tried to decreace a refcount that was 0!\n");

    return initial_count - 1;
}

static inline bool refcount_dec_not_zero(refcount_t *r, unsigned int *final_count)
{
    int initial_count = atomic_fetch_add(&r->count, -1);
    if (initial_count == 0) {
        atomic_add(&r->count, 1);
        *final_count = 0;
        return false;
    }

    *final_count = initial_count - 1;
    return true;
}

#endif // _REFCOUNT_H_