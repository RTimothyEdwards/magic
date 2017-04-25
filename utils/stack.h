/*
 * stack.h --
 *
 * General purpose stack manipulation routines.
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
 * Needs to include magic.h
 *
 * sccsid @(#)stack.h	4.1 MAGIC (Berkeley) 7/4/85
 */

#ifndef	_STACK_H
#define	_STACK_H

#include "utils/magic.h"

struct stackBody
{
    struct stackBody	*sb_next;	/* Next block on stack chain */
    ClientData		 sb_data[1];	/* Size determined when malloc'd */
};

/*
 * The following macro determines the size of the region to malloc
 * for a stack body able to hold sincr elements.
 */

#define sHDRSIZE		(sizeof (struct stackBody *))
#define stackSize(sincr)	(sHDRSIZE + (sizeof (ClientData)) * (sincr))

typedef struct stack
{
    int			 st_incr;	/* Amount by which to grow stack */
    ClientData		*st_ptr;	/* Stack pointer */
    struct stackBody	*st_body;	/* First stack block on chain */
} Stack;

/* --------------------- Procedure headers ---------------------------- */

Stack *StackNew();
ClientData StackPop();
ClientData StackLook();
void StackPush();
void StackFree();
void StackEnum();
void StackCopy();

#define	stackBodyEmpty(st)	((st)->st_ptr <= (st)->st_body->sb_data)

/*
 * bool StackEmpty(st) Stack *st;	returns TRUE if stack is empty
 */
#define	StackEmpty(st)	(stackBodyEmpty(st) && (st)->st_body->sb_next == NULL)

/*
 * Macro interfaces to StackLook(), StackPop(), and StackPush().
 */
#define	STACKLOOK(st) \
	(stackBodyEmpty(st) ? StackLook(st) : *((st)->st_ptr - 1))

#define	STACKPOP(st) \
	(stackBodyEmpty(st) ? StackPop(st)  : *--((st)->st_ptr))

#define	STACKPUSH(a, st) \
	if (1) { \
	    if ((st)->st_ptr >= &(st)->st_body->sb_data[(st)->st_incr]) \
		StackPush(a, st); \
	    else \
		*((st)->st_ptr++) = (ClientData)(pointertype) (a); \
	} else

#endif	/* _STACK_H */
