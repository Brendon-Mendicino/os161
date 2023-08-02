#ifndef _PAGE_H_
#define _PAGE_H_

#include <addrspace_types.h>
#include <addrspace.h>
#include <refcount.h>


static inline struct page *pte_page(pte_t pte)
{
    return kvaddr_to_page(pte_value(pte));
}

static inline void user_page_get(struct page *page)
{
    refcount_inc(&page->_mapcount);
}

static inline void user_page_put(struct page *page)
{
    bool destroy;

    destroy = refcount_dec(&page->_mapcount) == 0;

    if (destroy)
        free_pages(page);
}

static inline struct page *user_page_copy(struct page *page)
{
    struct page *new_page;
    unsigned refcount;

    refcount = refcount_dec(&page->_mapcount);

    /* there is only one page left from the cow */
    if (refcount == 0) {
        refcount_set(&page->_mapcount, 1);
        return page;
    }

    new_page = alloc_user_zeroed_page();
    if (!new_page)
        return NULL;

    memcpy((void *)page_to_kvaddr(new_page), (void *)page_to_kvaddr(page), PAGE_SIZE);

    return new_page;
}


#endif // _PAGE_H_