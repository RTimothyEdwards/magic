/* list.c -
 *
 *	Routines to create and manipulate lisp style lists.
 *	A separate structure is used to link list elements together.
 *	Thus list elements do not require pointers and can be anything.
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

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/list.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/utils.h"
#include "utils/malloc.h"
#include "utils/list.h"


/* ----------------------------------------------------------------------------
 *	ListPop --
 *	Returns top data item on list, deallocates top list element, and
 *	points list pointer to tail of list.
 *
 * Results:
 * 	First data item on list.
 *
 * Side effects:
 *	Deallocates head of list, and changes pointer to tail of list.
 * ----------------------------------------------------------------------------
 */

ClientData
ListPop(listPP)
    List ** listPP;		/* Pointer to pointer to list to pop */
{
    List *head = *listPP;
    ClientData result = LIST_FIRST(head);

    (*listPP) = LIST_TAIL(head);
    freeMagic((char *) head);

    return result;
}


/* ----------------------------------------------------------------------------
 *	ListContainsP --
 * 	Check if list contains given element.
 *
 * Results:
 * 	TRUE if element in list.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

bool
ListContainsP(element,list)
    ClientData element;	/* element to check for */
    List *list;		/* List to search */
{
    for(;list!=NULL && LIST_FIRST(list)!=element; list=LIST_TAIL(list))
	;	/* Null body */

    return list!=NULL;
}

/* ----------------------------------------------------------------------------
 *	ListDealloc --
 * 	Deallocate list strucs in list.
 *
 * Results:
 * 	None.
 *
 * Side effects:
 *	Storage reclaimed.
 *
 * NOTE:  Remember to set pointers to lists to null after deallocating 
 *        the list (if pointers are to be used again)!
 * ----------------------------------------------------------------------------
 */

void
ListDealloc(list)
    List *list;		/* List to free */
{
    for(;list!=NULL; list=LIST_TAIL(list))
	freeMagic((char *) list);

    return;
}


/* ----------------------------------------------------------------------------
 *	ListDeallocC --
 * 	Deallocate list strucs in list AND CONTENTS of list.
 *
 * Results:
 * 	None.
 *
 * Side effects:
 *	Storage reclaimed.
 *
 * NOTE:  Remember to set pointers to lists to null after deallocating 
 *        the list (if pointers are to be used again)!
 * ----------------------------------------------------------------------------
 */

void
ListDeallocC(list)
    List *list;		/* List to free */
{
    for(;list!=NULL; list=LIST_TAIL(list))
    {
        freeMagic((char *) LIST_FIRST(list));
	freeMagic((char *) list);
    }

    return;
}

/* ----------------------------------------------------------------------------
 *	ListLength --
 * 	Count number of elements in list.
 *
 * Results:
 * 	Returns number of elements in list
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

int
ListLength(list)
    List *list;		/* List to search */
{
    int count = 0;

    for(;list!=NULL; list=LIST_TAIL(list))
	count++;

    return count;
}

/* ----------------------------------------------------------------------------
 *	ListReverse --
 * 	Make reversed copy of list.
 *
 * Results:
 * 	Pointer to reverse copy of list.
 *
 * Side effects:
 *	Allocates and builds new list.
 * ----------------------------------------------------------------------------
 */

List *
ListReverse(list)
    List *list;		/* List to search */
{
    List *revList = NULL;

    for(; list!=NULL; list=LIST_TAIL(list))
	LIST_ADD(LIST_FIRST(list),revList);

    return revList;
}
