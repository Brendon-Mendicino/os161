#ifndef _ATABLE_H_
#define _ATABLE_H_

/**
 * Allocation Page Table, this is the first data structure created in memory
 * during vm_bootstrap() that allows allocation and deallocation of page frames
 * in the ram.
 */

#include <types.h>

struct atable; /* Opaque */

struct atable *atable_create(void);

paddr_t atable_getfreeppages(struct atable *t, size_t npages);

void atable_freeppages(struct atable *t, paddr_t addr);

size_t atable_get_size(struct atable *t);

#endif // _ATABLE_H_
