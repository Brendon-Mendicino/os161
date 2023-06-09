#ifndef _REFCOUNT_H_
#define _REFCOUNT_H_

#include <types.h>
#include <spinlock.h>

#define WITH_SPINLOCK 0
#define WITH_ATOMIC 0


#if WITH_SPINLOCK
typedef struct refcount {
    struct spinlock lock;
    unsigned int count;
} refcount_t;


#define REFCOUNT_INIT(n)     { .lock = SPINLOCK_INITIALIZER , .count = (n) }

static inline void INIT_REFCOUNT(refcount_t *refcount, unsigned int count)
{
    refcount->count = count;
    spinlock_init(&refcount->lock);
}

static inline unsigned int refcount_read(refcount_t *r)
{
    unsigned int count;
    spinlock_acquire(&r->lock);
    count = r->count;
    spinlock_release(&r->lock);
    return count;
}

static inline void refcount_inc(refcount_t *r)
{
    spinlock_acquire(&r->lock);
    r->count += 1;
    spinlock_release(&r->lock);
}

static inline bool refcount_inc_not_zero(refcount_t *r)
{
    bool success = false;
    spinlock_acquire(&r->lock);
    if (r->count != 0) {
        success = true;
        r->count += 1;
    }
    spinlock_release(&r->lock);

    return success;
}

static inline void refcount_dec(refcount_t *r)
{
    spinlock_acquire(&r->lock);
    r->count -= 1;
    spinlock_release(&r->lock);
}

static inline bool refcount_dec_not_zero(refcount_t *r)
{
    bool success = false;
    spinlock_acquire(&r->lock);
    if (r->count > 0) {
        r->count -= 1;
        success = true;
    }
    spinlock_release(&r->lock);

    return success;
}

static inline void refcount_add(unsigned int i, refcount_t *r)
{
    spinlock_acquire(&r->lock);
    r->count += i;
    spinlock_release(&r->lock);
}
#else

/*
 * This structure is not atomic
 */
typedef struct refcount {
    unsigned int count;
} refcount_t;


#define REFCOUNT_INIT(n)     { .count = (n) }


static inline void INIT_REFCOUNT(refcount_t *refcount, unsigned int count)
{
    refcount->count = count;
}

static inline unsigned int refcount_read(refcount_t *r)
{
    unsigned int count;

    count = r->count;

    return count;
}

static inline void refcount_inc(refcount_t *r)
{
    r->count += 1;
}

static inline bool refcount_inc_not_zero(refcount_t *r)
{
    bool success = false;

    if (r->count != 0) {
        success = true;
        r->count += 1;
    }

    return success;
}

static inline void refcount_dec(refcount_t *r)
{
    r->count -= 1;
}

static inline bool refcount_dec_not_zero(refcount_t *r)
{
    bool success = false;

    if (r->count > 0) {
        r->count -= 1;
        success = true;
    }

    return success;
}

static inline void refcount_add(unsigned int i, refcount_t *r)
{
    r->count += i;
}
#endif

#endif // _REFCOUNT_H_