#ifndef _EXEC_H_
#define _EXEC_H_

#include <types.h>

struct exec_params {
    vaddr_t entrypoint;
    vaddr_t stackprt;
    userptr_t uargv;
};


#endif // _EXEC_H_