

#include <machine/atable.h>
#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <vm.h>

/*
 * It would be a lot more efficient on most platforms to use uint32_t
 * or unsigned long as the base type for holding bits. But we don't,
 * because if one uses any data type more than a single byte wide,
 * bitmap data saved on disk becomes endian-dependent, which is a
 * severe nuisance.
 */
#define BITS_PER_WORD (CHAR_BIT)
#define WORD_TYPE uint8_t
#define WORD_ALLBITS (0xff)

/*
 * The maximum number of allocable consecutive pages
 * is as big as the addressable memory, this is
 * size_t is used instead of uint32_t
 */
#define ALLOC_TYPE size_t

#define ALIGN_BYTE (sizeof(size_t))
#define ALIGN_4BYTE(addr) ((((addr) % ALIGN_BYTE) == 0) ? (addr) : ((addr) - ((addr) % ALIGN_BYTE) + ALIGN_BYTE))

struct atable
{
    size_t nbits;
    size_t ntaken;
    paddr_t firstpaddr;
    WORD_TYPE *taken_pages;
    /// @brief size of allocated contiguos pages associated to paddr
    ALLOC_TYPE *alloc_space;
};

struct atable *
atable_create(void)
{
    struct atable *table;
    size_t nbits = 0;
    size_t words;
    size_t npages;
    size_t tsize;
    paddr_t ram_size;
    paddr_t first_available;
    size_t ram_free_space;

    first_available = ram_stealmem(1);
    KASSERT(first_available != 0);

    ram_size = ram_getsize() - first_available;

    tsize = sizeof(struct atable);
    // Calculate the space that the bitmap and the alloc_space take
    // ram_size = x + a + x/(p * CHAR_BIT) + x*sizeof(ALLOC_TYPE)/p
    // x = ram_free_space
    // p = PAGE_SIZE
    // a = sizeof(struct atable)
    ram_free_space = (size_t)(ram_size - tsize)/(1 + CHAR_BIT * PAGE_SIZE + CHAR_BIT * sizeof(ALLOC_TYPE));
    ram_free_space *= PAGE_SIZE * CHAR_BIT;

    // checkt he size of "atable + bitmap + alloc_space + alignment padding" in pages
    npages = DIVROUNDUP(tsize + ram_free_space / (PAGE_SIZE * BITS_PER_WORD) * (1 + sizeof(ALLOC_TYPE) * BITS_PER_WORD) + 3 * ALIGN_BYTE, PAGE_SIZE);
    ram_free_space -= npages * PAGE_SIZE;

    // check if the data fits in the ram
    nbits = ram_free_space * BITS_PER_WORD / (PAGE_SIZE);
    words = DIVROUNDUP(nbits, BITS_PER_WORD);
    KASSERT(sizeof(struct atable) + words + words * sizeof(ALLOC_TYPE) + 3 * ALIGN_BYTE + ram_free_space < ram_getsize());

    table = (struct atable *)PADDR_TO_KVADDR(first_available);

    table->nbits = nbits;
    table->ntaken = 0;
    table->firstpaddr = first_available + npages * PAGE_SIZE;
    /* align on 4 bytes */
    table->taken_pages = (WORD_TYPE *)ALIGN_4BYTE((size_t)table + tsize);
    /* align on 4 bytes */
    table->alloc_space = (ALLOC_TYPE *)ALIGN_4BYTE((size_t)table->taken_pages + words);

    /* Initialize the tables */
    memset(table->taken_pages, 0, words * sizeof(WORD_TYPE));
    memset(table->alloc_space, 0, words * sizeof(ALLOC_TYPE));

    /* Mark any leftover bits at the end in use */
    if (words > nbits / BITS_PER_WORD)
    {
        unsigned j, ix = words - 1;
        unsigned overbits = nbits - ix * BITS_PER_WORD;

        KASSERT(nbits / BITS_PER_WORD == words - 1);
        KASSERT(overbits > 0 && overbits < BITS_PER_WORD);

        for (j = overbits; j < BITS_PER_WORD; j++)
        {
            table->taken_pages[ix] |= ((WORD_TYPE)1 << j);
        }
    }

    return table;
}

static inline void
atable_translate(size_t bitno, size_t *ix, WORD_TYPE *mask)
{
    unsigned offset;
    *ix = bitno / BITS_PER_WORD;
    offset = bitno % BITS_PER_WORD;
    *mask = ((WORD_TYPE)1) << offset;
}

static inline bool
atable_at(WORD_TYPE *b, size_t index)
{
    size_t ix;
    WORD_TYPE mask;
    atable_translate(index, &ix, &mask);
    return (b[ix] & mask) != 0;
}

paddr_t
atable_getfreeppages(struct atable *t, size_t npages)
{
    size_t first_free;
    bool found;
    size_t ix;
    WORD_TYPE mask;

    KASSERT(npages < t->nbits);

    found = false;
    for (size_t i = 0; i < t->nbits; i++)
    {
        if (atable_at(t->taken_pages, i))
        {
            i += t->alloc_space[i] - 1;
            continue;
        }

        if (i == 0 || atable_at(t->taken_pages, i - 1))
            first_free = i;

        if (i - first_free + 1 == npages)
        {
            found = true;
            break;
        }
    }

    if (!found)
        return 0;

    // update the allocation size
    KASSERT(t->alloc_space[first_free] == 0);
    t->alloc_space[first_free] = npages;

    for (size_t i = first_free; i < npages + first_free; i++)
    {
        atable_translate(i, &ix, &mask);
        KASSERT((t->taken_pages[ix] & mask) == 0);
        t->taken_pages[ix] |= mask;
    }

    t->ntaken += npages;

    return t->firstpaddr + (paddr_t)first_free * PAGE_SIZE;
}

void atable_freeppages(struct atable *t, paddr_t addr)
{
    size_t ix;
    WORD_TYPE mask;
    size_t index;
    size_t npages;

    index = (addr - t->firstpaddr) / PAGE_SIZE;
    KASSERT(index < t->nbits);

    KASSERT(t->alloc_space[index] != 0);
    npages = t->alloc_space[index];
    t->alloc_space[index] = 0;

    for (size_t i = index; i < index + npages; i++)
    {
        atable_translate(i, &ix, &mask);
        KASSERT((t->taken_pages[ix] & mask) != 0);
        t->taken_pages[ix] &= ~mask;
    }

    t->ntaken -= npages;
}

size_t
atable_size(struct atable *t)
{
    KASSERT(t != NULL);
    return t->ntaken;
}

size_t
atable_capacity(struct atable *t)
{
    KASSERT(t != NULL);
    return t->nbits;
}
