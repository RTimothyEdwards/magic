/*
 * mzNumberLine.c --
 *
 * Implements datastructure that permits division of line into intervals
 * (end points) are integers, and given any interger, allows (quick) access to
 * the interval containing that integer.
 * 
 *     ********************************************************************* 
 *     * Copyright (C) 1988, 1990 Michael H. Arnold and the Regents of the *
 *     * University of California.                                         *
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/mzrouter/mzNumLine.c,v 1.2 2010/10/22 15:02:15 tim Exp $";
#endif  /* not lint */

/* -- includes -- */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/signals.h"
#include "textio/textio.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/malloc.h"
#include "utils/list.h"
#include "debug/debug.h"
#include "textio/textio.h"
#include "utils/heap.h"
#include "mzrouter/mzrouter.h"
#include "mzrouter/mzInternal.h"


/*
 * ----------------------------------------------------------------------------
 *
 * mzNLInit -
 *
 * Initial a number line.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *      Allocs entires array and sets number line to the single interval 
 *      from MINFINITY to INFINITY. 
 *
 * ----------------------------------------------------------------------------
 */

void
mzNLInit(nL, size)
    NumberLine *nL;
    int size;	/*initial size of number line */
{
    int *entries;
    size = MAX(size, 2);

    nL->nl_sizeAlloced = size;
    nL->nl_sizeUsed = 2;

    entries = (int *) mallocMagic((unsigned)(sizeof(int)*size));
    entries[0] = MINFINITY;
    entries[1] = INFINITY;

    nL->nl_entries = entries;

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzNLInsert -
 *
 * Add new division point to numberline.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *      Modifiy number line, allocate larger entry array if necessary.
 *
 * ----------------------------------------------------------------------------
 */

void
mzNLInsert(nL,x)
    NumberLine *nL;
    int x;		/* new point */
{

    int lowI, highI;

    /* find entries bounding x */
    {
	lowI = 0;
	highI = nL->nl_sizeUsed - 1;


	while(highI-lowI > 1)
	{
	    int newI = lowI + (highI - lowI)/2;
	    int newV = nL->nl_entries[newI];
	    
	    if(newV <= x)
	    {
		lowI = newI;
	    }
	    if(newV >= x)
	    {
		highI = newI;
	    }
	}
    }
	
    /* if x is  already an entry, just return */
    if(lowI == highI)
    {
        return;
    }

    /* if number line is full, allocate twice as big an entry array */
    if(nL->nl_sizeUsed == nL->nl_sizeAlloced)
    {
	int *newEntries;
	int newSize;

	/* allocate new entry array */
	newSize = nL->nl_sizeUsed*2;
	newEntries = (int *) mallocMagic(sizeof(int) * (unsigned)(newSize));

	/* copy old entries to new */
	{
	    int *sentinel = &(nL->nl_entries[nL->nl_sizeAlloced]);
	    int *source = nL->nl_entries;
	    int *target = newEntries;
	    while(source != sentinel)
	    {
		*(target++) = *(source++);
	    }
	}

	/* free up old array */
	freeMagic(nL->nl_entries);

	/* update numberline */
	nL->nl_sizeAlloced = newSize;
	nL->nl_entries = newEntries;
    }

    /* move larger entries down one to make room */
    {
	int * sentinel = &((nL->nl_entries)[lowI]);
	int * target = &((nL->nl_entries)[nL->nl_sizeUsed]);
	int * source = target-1;
	
	while(source!=sentinel)
	{
	    *(target--) = *(source--);
	}
    }

    /* insert new entry */
    (nL->nl_entries)[highI] = x;

    /* update entry count */
    (nL->nl_sizeUsed)++;

    /* and return */
    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzNLGetContainingInterval -
 *
 * Find interval containing x (by binary search)
 *
 * Results:
 *	Pointer to array of two ints bounding x.
 *
 * Side effects:
 *      None.
 *
 * ----------------------------------------------------------------------------
 */

int *
mzNLGetContainingInterval(nL,x)
    NumberLine *nL;
    int x;		/* new point */
{

    int lowI, highI;

    /* find entries bounding x */
    {
	lowI = 0;
	highI = nL->nl_sizeUsed - 1;

	while(highI-lowI > 1)
	{
	    int newI = lowI + (highI - lowI)/2;
	    int newV = nL->nl_entries[newI];
	    
	    if(newV <= x)
	    {
		lowI = newI;
	    }
	    if(newV >= x)
	    {
		highI = newI;
	    }
	}
    }

    /* return pointer to bounding entries */
    return &((nL->nl_entries)[lowI]);
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzNLClear -
 *
 * Clears numberline to single interval from MINFINITY to INFINITY.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *      See above.  CURRENTLY WE LEAVE THE ALLOCATED SIZE OF THE NUMBERLINE
 *      ALONE.
 *
 * ----------------------------------------------------------------------------
 */

void
mzNLClear(nL)
    NumberLine *nL;
{

    nL->nl_entries[0] = MINFINITY;
    nL->nl_entries[1] = INFINITY;
    nL->nl_sizeUsed = 2;

    return;
}
