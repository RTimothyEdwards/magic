/*
 * list.h --
 * Structure and macros for mantaining and manipulating  linked lists 
 * of arbitrary things.
 * These lists are lisp like, i.e. list pointers are in separate strucutures
 * rather than in the strucs being linked.
 * (Equivalent to lisp lists.)
 * 
 * Global definitions for all MAGIC modules
 *
 *     ********************************************************************* 
 *     * Copyright (C) 1985, 1990 Regents of the University of California. * 
 *     * Permission to use, copy, modify, and distribute this              * 
 *     * software and its documentation for any purpose and without        * 
 *     * fee is hereby granted, provided that the above copyright          * 
 *     * notice appear in all copies.  The University of California        * 
 *     * makes no representations about the suitability of this            * 
 *     * software for any purpose.  It is provided "as is" without         * 
 *     * express or implied warranty.  Export of this software outside     * 
 *     * of the United States of America may require an export license.    * 
 *     *********************************************************************
 *
 * Needs to include <stdio.h> and <sys/types.h>
 *
 * rcsid $Header: /usr/cvsroot/magic-8.0/utils/list.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 */

#ifndef	_LIST_H
#define	_LIST_H

#include "utils/magic.h"

/* --------------------- Lists (Lisp like) ---------------------------- */

/* structure to make lists of anything - used to link together other strucs
 * "externally", i.e. with out useing pointers inside the strucs.
 */
typedef struct
list
{
    ClientData list_first;
    struct list *list_tail;
} List;

/* -------------------- Functions (exported by list.c) ---------------- */
extern ClientData ListPop();	/* usage   ListPop(&&List) */
extern bool ListContainsP();	/* usage:  ListContainsP(ClientData,&List) */
extern int ListLength();	/* usage:  ListLength(&List) */
extern void ListDealloc();	/* usage:  ListDealloc(&List) */
extern void ListDeallocC();	/* usage:  ListDeallocC(&List) */
extern List *ListReverse();	/* usage:  ListReverse(&List) */

/* --------------------- Macros for Lists  ----------------------------- */
#define LIST_FIRST(l)	((l)->list_first)
#define LIST_TAIL(l)	((l)->list_tail)
#define LIST_ADD(i,l) \
    if(TRUE) \
    { \
	List * LIST_l; \
        LIST_l = (List *) mallocMagic((unsigned) (sizeof(List))); \
	LIST_l->list_first = (ClientData) (i); \
	LIST_l->list_tail = (l); \
	(l) = LIST_l; \
    } else
#define LIST_COPY(l,lnew) \
    if(TRUE) \
    { \
	List * LIST_intermediate; \
	LIST_intermediate = ListReverse(l); \
	(lnew) = ListReverse(LIST_intermediate); \
	ListDealloc(LIST_intermediate); \
    } else

#endif	/* _LIST_H */
