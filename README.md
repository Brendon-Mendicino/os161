- [Paging](#paging)
  - [Introduction](#introduction)
  - [TLB Support](#tlb-support)
    - [Empty TLB Entry](#empty-tlb-entry)
    - [Entry Present](#entry-present)
  - [2-Level Page Table](#2-level-page-table)
  - [On-Demand Page Loading](#on-demand-page-loading)
  - [Page Replacement](#page-replacement)
  - [Swap Memory](#swap-memory)
  - [Buddy System](#buddy-system)
  - [Statistics](#statistics)
  - [List e HashTable](#list-e-hashtable)
  - [Atomic](#atomic)
  - [Conclusion](#conclusion)

# Paging

## Introduction

The aim of the this project is to create a support for using the virtual memory
subsystem on OS161, that in its initial form the kernl has a rudimentary system to
support user processes isolation, in this current version that is replaced
entirely with a two level _Hierarchical Page Table_ for single process. The
_Demand Paging_ technique is used in order to fill the pages in the _Page Table_,
where each time a **Page Fault** is triggered, if the page is not present in memory,
that will be created from source file of the program (file ELF). When a **Page Fualt**
is triggered, there are two scenarios on how the page can be inserted:

- from the source file (ELF)
- from the Swap memory

In general, when a **TLB Fault** is triggered a **Page Fault** happens only if that
page is not present in the _Page Talbe_, if on the other hand the page is present
in the _Page Table_ and the fault kind is **ReadOnly**, then it means that the page
is shared between more than one process, previously shared with `fork()`, this will
susequently be copied and added to the actual _Page Table_.

In OS161 a mimal support for frame (physical pages) management is present as well,
in the current version a **Buddy System** algorithm has been implemented for the
relative page management. An array with all the pages is created from the physical
memory, those will e managed during the call of `alloc()` and `free()`. When the
system is under pressere the available pages are getting low, some of them will be
move to the **Swap Memory**, those will be retrieved back in the memory after a
**Page Fault**.

## TLB Support

Each time a **TLB Fault** occurs, the `vm_fault()` function is called
to handle the exception. This function manages either the absence of an entry
inside the TLB or a write attempt to an address where only read access
is allowed. Let us analyze the two cases separately as shown in the following
code section.

```c
static int vm_handle_fault(struct addrspace *as, vaddr_t fault_address, int fault_type)
{
  // ...

  pte = pt_get_or_alloc_pte(&as->pt, fault_address);
  if (!pte)
    return ENOMEM;

  pte_entry = *pte;

  /* The page is not present in memory */
  if (!pte_present(pte_entry)) {
    return page_not_present_fault(as, area, pte, fault_address, fault_type);
  }

  if (fault_type & VM_FAULT_READONLY) {
    return readonly_fault(as, area, pte, fault_address, fault_type);
  }

  vm_tlb_set_page(fault_address, pte_paddr(pte_entry), pte_write(pte_entry));

  // ...
}
```

### Empty TLB Entry

If the entry does not exist in the TLB, the physical address is looked up in the
process's _Page Table_. When reaching the PTE (_Page Table Entry_),
i.e., the second-level entry of the _Page Table_, it is analyzed to determine
its state. It can be in one of the following three states:

- `none`: the entry has a `NULL` value, which means the page has not yet been mapped.
  In this case, the page is loaded from the program's source ELF and brought into memory.
- `swap`: the entry has a value that points to the swap memory. In this case, the page is
  copied into memory from the swap and the refcount within the swap memory is decremented.

```c
static int page_not_present_fault()
{
  struct page *page = alloc_user_page();
  if (!page)
    return ENOMEM;

  /* load page from swap memory */
  if (pte_swap_mapped(*pte)) {
    retval = swap_get_page(page, pte_swap_entry(*pte));
    // ...
  }
  /* load page from memory if file mapped */
  else if (pte_none(*pte) && asa_file_mapped(area)) {
    clear_page(page);
    retval = load_demand_page(as, area, fault_address, page_to_paddr(page));
    // ...
  }
  // ...

  pte_set_page(pte, page_to_kvaddr(page), flags);

  // ...
}
```

- `present`: the page is already inside the _Page Table_. Only the address
  value of the physical page will be added to the a TLB entry.

### Entry Present

When the entry is present, it can only be a **Read Only Fault**.
There are only 2 possible causes:

- the faulting address does not correspond to any `addrspace_area` (a mapped memory
  area of the process), or the corresponding area is not writable, which leads to a segmentation fault
- the address corresponds to a **shared** page among multiple processes.
  The page is defined as **COW** (_Copy On Write_). In this case, the page is copied
  and assigned to the process's _Page Table_ that caused the fault, while the refcount
  of the previous page is decremented.

```c
static int readonly_fault()
{
  struct page *page = pte_page(*pte);

  // ...

  if (asa_readonly(area))
    return EFAULT;

  if (is_cow_mapping(area->area_flags)) {
    page = user_page_copy(page);
  }

  // ...

  pte_set_page(pte, page_to_kvaddr(page), PAGE_PRESENT | PAGE_RW | PAGE_ACCESSED | PAGE_DIRTY);
  vm_tlb_set_page(fault_address, page_to_paddr(page), true);

  // ...
}
```

## 2-Level Page Table

The page table is placed inside the `struct proc` and has a two-level structure,
dividing the virtual address into three parts, respectively in

```
| [31 ------------ 22 ] | [ 21 ------ 12 ] | [11 ---- 0] |
| Page Middle Directory | Paga Table Entry | Page Offset |
```

le parti di indirizzo forniranno l'offset all'interno delle tabelle di livello
che dovranno essere contigue

```
| [31 ------------ 22 ] | [ 21 ------ 12 ] | [11 ---- 0] |
| Page Middle Directory | Paga Table Entry | Page Offset |
  |                       |                            |
  |                       |                            |
  pmd_offset              pte_offset                   |
  |   pmd                 |   pte                      |
  |   +-------+           |   +-------+     page       |
  |   |       |           |   |       |     +-------+  |
  |   |-------|           |   |-------|     |       |  |
  |   |       |           |   |       |     |       |<-/
  |   |-------|           |   |-------|     |       |
  |   |       |           |   |       |---->+-------+
  |   |-------|           \-->|-------|
  |   |       |-----\         |       |
  \-->|-------|     |         |-------|
      |       |     |         |       |
  /-->+-------+     \-------->+-------+
  |
  |
pmd_t *
```

The `struct proc` has a field `pmd_t *pmt` that points to the first
level of the table, which is always allocated,
while the `pte`s are only allocated on demand.
This saves a lot of memory that would otherwise be wasted
due to limited usage.
Having a _Page Table_ per process also has the advantage
that waiting times are not long, unlike what would happen with a
global _Page Table_ shared by all processes, which would require
a heavy locking mechanism.

## On-Demand Page Loading

The pages of a process are not initially loaded into memory during `load_elf()`.
Instead, the ELF headers are loaded, which contain within them
the description of the memory area. Their structure is as follows:

```c
struct addrspace_area {
  area_flags_t area_flags;        /* flags of the area */
  area_type_t  area_type;         /* type of the area */
  struct list_head next_area;
  /*
   * Borders of the area, the end is not included
   * in the interval [area_start, area_end)
   */
  vaddr_t area_start, area_end;

  size_t seg_size;                /* Size of the segment within the source file */
  off_t seg_offset;               /* Offset of the segment within the source file */
};
```

Only when a **Page Fualt** is triggered the page is loaded from ELF,
using the `load_elf_page()` the page will be loaded into the memory.

```c
int load_demand_page(struct addrspace *as, struct addrspace_area *area, vaddr_t fault_address, paddr_t paddr)
{
  int retval;
  off_t page_offset, file_offset;
  size_t memsize, filesz;
  vaddr_t vaddr;

  /*
   * Calculate the offset of the page to be
   * loaded inside the segment
   */
  KASSERT(fault_address >= area->area_start);
  KASSERT(fault_address <  area->area_end);

  /* align the offset with the begenning of a page */
  if ((fault_address & PAGE_FRAME) > area->area_start) {
    page_offset = (fault_address & PAGE_FRAME) - area->area_start;
  } else {
    page_offset = 0;
  }

  file_offset = area->seg_offset + page_offset;
  KASSERT((page_offset == 0) || PAGE_ALIGNED(area->area_start + page_offset));
  vaddr = PADDR_TO_KVADDR(paddr) + ((area->area_start + page_offset) % PAGE_SIZE);
  memsize = PAGE_SIZE - ((area->area_start + page_offset) % PAGE_SIZE);
  filesz = (page_offset < area->seg_size) ? area->seg_size - page_offset : 0;

  /*
   * only load the demanded page inside memory,
   * calculate the size of the page to load inside
   */
  retval = load_ksegment(as->source_file,
      file_offset,
      vaddr,
      memsize,
      MIN(filesz, memsize));
  if (retval)
    return retval;

  return 0;
}
```

## Page Replacement

When the system starts to become overloaded, the _Page Replacement_ algorithm
comes into play, ensuring that memory is always available when needed.
It starts operating once a certain _threshold_ is exceeded,
set at 80% of occupied memory. To replace a page, the process's
_Page Table_ that requested memory is scanned, and if a page is not marked as
`PTE_ACCESSED` (which is set when the page is accessed during a _Page Fault_),
it is unmarked, and the scan continues until the first available page is found.
Additionally, pages shared among multiple processes are skipped.
This process may also result in no pages being moved to swap memory.
The functions used are `choose_victim_page()` and `pt_walk_page_table()`.

## Swap Memory

The **Swap Memory** is represented through `struct swap_memory`.
This structure contains the number of pages allocated in swap memory
and an array with the current `refcount` values of each page in swap.
If the `refcount` is zero, it means that position is free.

```c
struct swap_entry {
    unsigned refcount;
};

struct swap_memory {
    size_t swap_pages;
    size_t swap_size;

    struct spinlock swap_lock;
    struct lock *swap_file_lock;

    struct vnode *swap_file;

    struct swap_entry swap_page_list[SWAP_ENTRIES];
};
```

The total number of entries is defined through a macro at _compile-time_,
which means that dynamic resizing is not possible. To use
this memory, a `/swap` file is created to allow the temporary
storage of pages. For performing a `swap-in`, `swap_mem.swap_page_list`
is used to determine the first available free space.
The algorithm is a simple linear scan that looks for the first entry
with a `refcount` equal to 0, after which the page is copied
into swap.

```c
static int handle_swap_add_page(struct swap_memory *swap, struct page *page, swap_entry_t *entry)
{
  // ...

  /*
   * Lock for the file access goes this early
   * beacause there is a race condition after the
   * spinlock ends, another thread might come
   * before this one taking the lock, thus
   * reading garbage from the memory.
   */
  lock_acquire(swap->swap_file_lock);
  spinlock_acquire(&swap->swap_lock);

  first_free = swap_get_first_free(swap);

  swap->swap_page_list[first_free].refcount += 1;
  swap->swap_pages += 1;

  KASSERT(swap->swap_page_list[first_free].refcount == 1);
  spinlock_release(&swap->swap_lock);

  file_offset = first_free * PAGE_SIZE;
  uio_kinit(&iovec, &uio, (void *)page_to_kvaddr(page), PAGE_SIZE, file_offset, UIO_WRITE);

  retval = VOP_WRITE(swap->swap_file, &uio);
  lock_release(swap->swap_file_lock);
  if (retval)
      goto bad_write_cleanup;

  // ...
}
```

The function used to look for the first available is quite simple

```c
static size_t swap_get_first_free(struct swap_memory *swap)
{
    for (size_t i = 0; i < SWAP_ENTRIES; i += 1) {
        if (swap->swap_page_list[i].refcount != 0)
            continue;

        return i;
    }

    panic("Out of swap space!\n");
}
```

## Buddy System

To allocate and free system pages, a **Buddy System** strategy is used.
The buddy system has a maximum order of 6, which means
the maximum number of contiguous pages that can be allocated is `2^6 = 64`.
During the bootstrap phase, all remaining memory (memory not used by the kernel code
at bootstrap) is divided into the maximum number of `order-6` pages. Each
page contains a list that allows it to be part of various levels
and to be searched in `O(1)`. In fact, checking whether an order contains pages
is limited to taking an element from the list.

```c
static struct page *get_page_from_free_area(struct free_area *area)
{
  return list_first_entry_or_null(&area->free_list, struct page, buddy_list);
}
```

## Statistics

Statistics are also available, showing
details about the system's state and are accessible through
commands in the menu:

- `mem`: info on the state of the buddy system and system pages
- `fault`: info on **TLB Faults**, containing statistics about the TLB
  and page movements in memory
- `swap`: statistics on swap memory
- `swapdump [start end]`: dumps every entry in the swap memory
  within the specified range

## List e HashTable

The lists and hash tables are taken from the Linux libraries by including
the respective files `list.h` and `hash.h` (with slight modifications
to adapt them to OS161). The use of lists is very extensive
in this version thanks to the generic nature of Linux lists.
In fact, if we look at the definition of the linked list, we notice it has
only two fields.

```c
struct list_head {
  struct list_head *next, *prev;
}
```

At first glance, this might seem confusing, as the list only contains pointers
and no field for the data structure. In reality, it is very
powerful. Thanks to a GCC buitlin function `offsetof()`, which takes as input the
name of the structure and the name of one of its members and it returns
the offset of the member from the start of the structure, allowing
to derive a pointer to the structure in which the `struct list_head` is located.

## Atomic

Support for atomics is implemented in assembly. The code is very similar
to that of `testandset()` in the `spinlock`. In `atomic_fetch_add()`, the following precautions are taken:

- The assembly code is preceded by a `membar` (the `sync` instruction)
  and is also followed by a memory barrier (the _clobber_ `"memory"` as part of GCC syntax).
- The `llsc` can always fail; if this happens, the code section is repeated
  using a jump instruction.
- The increment of the counter is visible to all CPUs in a non-ordered manner
  relative to the CPU that executes the first `ll` instruction, because if
  another CPU tries to write to that memory area, the first attempt will fail.

```c
static inline int
atomic_fetch_add(atomic_t *atomic, int val)
{
    int temp;
    int result;

    asm volatile(
    "    .set push;"          /* save assembler mode */
    "    .set mips32;"        /* allow MIPS32 instructions */
    "    sync;"               /* memory barrier for previous read/write */
    "    .set volatile;"      /* avoid unwanted optimization */
    "1:  ll   %1, 0(%2);"     /*   temp = atomic->val */
    "    add  %0, %1, %3;"    /*   result = temp + val */
    "    sc   %0, 0(%2);"     /*   *sd = result; result = success? */
    "    beqz %0, 1b;"
    "    .set pop;"           /* restore assembler mode */
    "    move %0, %1;"        /*   result = temp */
    : "=&r" (result), "=&r" (temp)
    : "r" (&atomic->counter), "Ir" (val)
    : "memory");              /* memory barrier for the current assembly block */

    return result;
}
```

To test this function, the file `test/atomic_unit.c` was added,
which spawns `n` threads that simultaneously increment the same atomic variable.
This test can be executed using the command `atmu1 <n-thread>`.

## Conclusion

In conclusion, this project aims to implement virtual memory management
in a system where processes have only a single thread. This is very limiting
for an OS, which is why many data structures are simplified and there is little
use of synchronization primitives. For example, in Linux, to access a
process's _Page Table_, the `mmap_lock` semaphore must be held, which blocks
the entire address space of a process (`mm_struct` = MemoryMap in Linux).
In OS161, it is possible to access a process's address space without needing a lock.

The implementation of `fork()` with COW improves performance on one hand,
but presents unresolved limitations on the other. For example, when trying
to move a page to swap memory, if a process has only shared pages,
this is not possible. The reason is that moving a page without notifying
other owners can lead to _race conditions_ when accessing that memory area.
In OS161, there is support for notifying other CPUs to flush
their TLBs via the `struct tlbshootdown` and the function `ipi_tlbshootdown()`.
Over time, this lack of page movement can lead to memory saturation.
A possible improvement would be to create a `page cache` that
manages exchanges with swap memory. In Linux, the `page cache` also
manages exchanges with other memory zones, such as NUMA, etc.

In general, execution performance is slightly reduced due to
the overhead caused by the double indirection of the _Page Table_,
the larger size of `vm_fault()`, the introduction of _Page Replacement_,
and the search in _Swap Memory_. On the positive side, the system
can allocate and deallocate memory up to the filling of swap memory,
and if managed properly, it can avoid **Out Of Memory** situations.
Additionally, when a process performs illegal operations, it is terminated
instead of crashing the entire system.
