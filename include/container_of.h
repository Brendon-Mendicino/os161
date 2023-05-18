/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_CONTAINER_OF_H
#define _LINUX_CONTAINER_OF_H

#define typeof_member(T, m)	typeof(((T*)0)->m)

/* Offset of member MEMBER in a struct of type TYPE. */
#define offsetof(TYPE, MEMBER) __builtin_offsetof (TYPE, MEMBER)

/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 *
 * WARNING: any const qualifier of @ptr is lost.
 */
#define container_of(ptr, type, member) ({				\
	void *__mptr = (void *)(ptr);					\
	((type *)(__mptr - offsetof(type, member))); })

#endif	/* _LINUX_CONTAINER_OF_H */
