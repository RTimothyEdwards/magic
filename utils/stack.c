/*
 * stack.c --
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
 */

#ifndef	lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/stack.c,v 1.2 2010/06/24 12:37:58 tim Exp $";
#endif	/* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/utils.h"
#include "utils/stack.h"
#include "utils/malloc.h"

bool stackCopyStr;

/*
 * ----------------------------------------------------------------------------
 * StackNew --
 *
 * Allocate and initialize a new Stack.
 *
 * Results:
 *	Returns a pointer to a newly heap-allocated and initialized
 *	stack, with its growth increment set to the specified
 *	size (in entries).
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

Stack *
StackNew(sincr)
    int sincr;		/* Number of entries by which to grow storage area */
{
    Stack *stack;

    stack = (Stack *) mallocMagic((unsigned) (sizeof (Stack)));
    stack->st_incr = sincr;
    stack->st_body = (struct stackBody *) mallocMagic((unsigned) (stackSize(sincr)));
    stack->st_ptr = stack->st_body->sb_data;
    stack->st_body->sb_next = NULL;

    return (stack);
}

/*
 * ----------------------------------------------------------------------------
 * StackFree --
 *
 * Deallocate a Stack.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Deallocates all memory currently assigned to the Stack.
 *
 * ----------------------------------------------------------------------------
 */

void
StackFree(stack)
    Stack *stack;
{
    struct stackBody *stackp, *stacknext;

    for (stackp = stack->st_body; stackp != NULL; stackp = stacknext)
    {
	stacknext = stackp->sb_next;
	freeMagic((char *) stackp);
    }

    freeMagic((char *) stack);
}

/*
 * ----------------------------------------------------------------------------
 * StackPush --
 *
 * Push a new element on to a stack.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The argument stack is updated to reflect the new
 *	item placed upon it.
 *
 * ----------------------------------------------------------------------------
 */
void
StackPush(arg, stack)
    ClientData arg;
    Stack *stack;
{
    struct stackBody *bodyNew;

    if (stack->st_ptr >= &stack->st_body->sb_data[stack->st_incr])
    {
	bodyNew = (struct stackBody *) mallocMagic((unsigned) (stackSize(stack->st_incr)));
	bodyNew->sb_next = stack->st_body;
	stack->st_ptr = bodyNew->sb_data;
	stack->st_body = bodyNew;
    }

    *(stack->st_ptr++) = arg;
}

/*
 * ----------------------------------------------------------------------------
 * StackPop --
 *
 * Pop the top element from a Stack and return it.
 *
 * Results:
 *	Top element from stack.
 *	If the stack is already empty, returns NULL.
 *	Callers should probably avoid popping from an empty
 *	stack.
 *
 * Side effects:
 *	Updates the stack to reflect the result of popping its top.
 *
 * ----------------------------------------------------------------------------
 */

ClientData
StackPop(stack)
    Stack *stack;
{
    struct stackBody *bodyOld;

    if (stackBodyEmpty(stack))
    {
	bodyOld = stack->st_body;
	if (bodyOld->sb_next == NULL)
	    return ((ClientData) NULL);
	stack->st_body = bodyOld->sb_next;
	stack->st_ptr = &stack->st_body->sb_data[stack->st_incr];
	freeMagic((char *) bodyOld);
    }

    return (*--(stack->st_ptr));
}

/*
 * ----------------------------------------------------------------------------
 * StackLook --
 *
 * Return the top element from a Stack, but don't pop it off.
 *
 * Results:
 *	Top element from stack.
 *	If the stack is already empty, returns NULL.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

ClientData
StackLook(stack)
    Stack *stack;
{
    struct stackBody *bodyNext;

    if (stackBodyEmpty(stack))
    {
	bodyNext = stack->st_body->sb_next;
	if (bodyNext == NULL)
	    return ((ClientData) NULL);
	return (bodyNext->sb_data[stack->st_incr - 1]);
    }

    return ( *(stack->st_ptr - 1) );
}

/*
 * ----------------------------------------------------------------------------
 *
 * StackEnum --
 *
 * 	Enumerate all elements on the stack.  Call the supplied function
 *	for each occurrence.
 *
 *	The supplied function is of the form:
 *		int func(stackItem, i, clientData)
 *		    ClientData stackItem;    Item put on the stack
 *		    int i;		     Index of the item on stack
 *		    ClientData cd;	     Points to whatever you want
 *	The function normally returns 0.  The enumeration terminates if it
 *	returns anything else.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */
void
StackEnum(stack, func, cd)
    Stack    * stack;
    int     (* func)();
    ClientData cd;
{
    int i, j;
    struct stackBody * sb;

    i=1;
    for(sb=stack->st_body; sb!=(struct stackBody *) NULL; sb=sb->sb_next)
    {
	for(j=0; j<=stack->st_incr; j++)
	{
	    if( &(sb->sb_data[j]) == stack->st_ptr ) return;
	    if((*func)(sb->sb_data[j], i, cd)) return;
	    i++;
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * StackCopy --
 *
 * 	Make a copy of a stack.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory may get allocated if the copystr parameter says to copy
 *	strings rather than pointers to them.
 *	dest gets a copy of src.  If dest is non-null, it gets freed.
 *
 * ----------------------------------------------------------------------------
 */
void
StackCopy(src, dest, copystr)
    Stack * src, ** dest;
    bool copystr;
{
    int stackCopyFn();

    stackCopyStr=copystr;
    if(*dest!=(Stack *) NULL)
	StackFree(*dest);
    if(src==(Stack *) NULL)
	*dest = src;
    else
    {
	*dest = StackNew(src->st_incr);
	StackEnum(src, stackCopyFn, (ClientData) * dest);
    }
}

/*ARGSUSED*/
int
stackCopyFn(stackItem, i, cd)
    ClientData stackItem;
    int i;
    ClientData cd;
{
    if(stackCopyStr)
	StackPush((ClientData) StrDup((char **) NULL, (char *)stackItem), (Stack *) cd);
    else
	StackPush(stackItem, (Stack *) cd);
    return(0);
}
