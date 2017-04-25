
/* gcrLib.c -
 *
 *	Miscellaneous stuff for the greedy router.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/gcr/gcrLib.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "gcr/gcr.h"
#include "utils/malloc.h"

/* Forward declarations */
void gcrUnlinkTrack();


/*
 * ----------------------------------------------------------------------------
 *
 * gcrBlocked --
 *
 * See if a given location is blocked for a vertical run.
 *
 * Results:
 *	TRUE if the location is blocked.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
gcrBlocked(col, i, net, last)
    GCRColEl *col;	/* Current column information */
    int i;		/* Which element */
    GCRNet *net;	/* Net we're interested in processing: locations
			 * that already contain this net aren't considered
			 * to be blocked; all others are.
			 */
    int last;
{
    GCRColEl *colptr = &col[i];

    /* True if already wired vertically */
    if (colptr->gcr_v != net && colptr->gcr_v)
	return (TRUE);

    /* True if column ended */
    if ((colptr->gcr_flags & GCRCE) && i != last && colptr->gcr_h != net)
	return (TRUE);

    /* True if poly and metal blocked and not this net */
    if ((colptr->gcr_flags & (GCRBLKP|GCRBLKM|GCRCC))
	    && colptr->gcr_h && colptr->gcr_h != net)
	return (TRUE);

    /* Blocked if there's a contact there */
    if (colptr->gcr_flags & GCRX)
	return (TRUE);

    /* All clear */
    return (FALSE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * gcrMoveTrack --
 *
 * Move a net to a new track.  Add it to the occupied net list for the
 * given net.  If the destination track belongs to the same net, then
 * just remove the duplicate.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds vertical paint for the track.
 *
 * ----------------------------------------------------------------------------
 */

void
gcrMoveTrack(column, net, from, to)
    GCRColEl *	column;
    GCRNet   *  net;		/* Net to be assigned to a track	*/
    int		from;
    int		to;
{
    int		i, last;

/* Paint vertical segments.  Delete tracks already assigned to this net */

    if(from==to)
	return;

    if(net==(GCRNet *) NULL)
	if(column[from].gcr_wanted!=(GCRNet *) NULL)
	    net=column[from].gcr_wanted;

    last=from;
    if(from<to)	/* Paint an ascending jog into the column */
    {
	for(i=from+1; i<to; i++)
	{
	/* Paint a vertical segment.  Free intermediate tracks assigned to
	 * to the net, unless those tracks are needed for right edge pins.
	 */
	    column[i].gcr_v=net;
	    if(column[i].gcr_h==net)
	    {
		if(column[i].gcr_wanted!=net)
		    gcrUnlinkTrack(column, i);
		else
		{
		    column[   i].gcr_lOk=TRUE;
		    column[last].gcr_hOk=TRUE;
		    last=i;
		}
	    }
	    if(column[i].gcr_flags&GCRCC)
		column[i].gcr_flags|=(GCRX|GCRBLKM|GCRBLKP);
	}

    /* Added to fix the track extension bug */
	if((column[to].gcr_wanted==net) ||
	   (!column[to].gcr_hOk && !column[to].gcr_lOk))
	    column[to].gcr_h=net;
	else
	{
	    column[to].gcr_h=(GCRNet *) NULL;
	    column[to].gcr_hOk=FALSE;
	    column[to].gcr_lOk=FALSE;
	    column[to].gcr_hi=column[to].gcr_lo= EMPTY;
	}

	if(column[from].gcr_wanted!=net)
	{
	/* Replace the "from" track with the "to" track*/
	    column[to].gcr_lo=column[from].gcr_lo;
	    if(column[from].gcr_lo!= EMPTY)
		column[column[from].gcr_lo].gcr_hi=to;
	    if(to<column[from].gcr_hi)
	    {
		column[to].gcr_hi=column[from].gcr_hi;
		if(column[from].gcr_hi!= EMPTY)
		    column[column[from].gcr_hi].gcr_lo=to;
	    }
	    column[from].gcr_lo=column[from].gcr_hi= EMPTY;
	}
	else
	{
	    column[from].gcr_hi=to;
	    column[to  ].gcr_lo=from;
	    column[from].gcr_hOk=TRUE;
	    column[to  ].gcr_lOk=TRUE;
	}
    }
    else
    {
	for(i=from-1; i>to; i--)	/*Paint a descending vertica jog*/
	{
	    column[i].gcr_v=net;
	    if(column[i].gcr_h==net)
	    {
		if(column[i].gcr_wanted!=net)
		    gcrUnlinkTrack(column, i);
		else
		{
		    column[last].gcr_lOk=TRUE;
		    column[   i].gcr_hOk=TRUE;
		    last=i;
		}
	    }
	    if(column[i].gcr_flags&GCRCC)
		column[i].gcr_flags|=(GCRX|GCRBLKM|GCRBLKP);
	}

    /* Added to fix the track extension bug */
	if((column[to].gcr_wanted==net) ||
	   (!column[to].gcr_hOk && !column[to].gcr_lOk))
	    column[  to].gcr_h=net;
	else
	{
	    column[to].gcr_h=(GCRNet *) NULL;
	    column[to].gcr_hOk=FALSE;
	    column[to].gcr_lOk=FALSE;
	    column[to].gcr_hi=column[to].gcr_lo= EMPTY;
	}

	if(column[from].gcr_wanted!=net)
	{
	    column[to].gcr_hi=column[from].gcr_hi;
	    if(column[from].gcr_hi!= EMPTY)
		column[column[from].gcr_hi].gcr_lo=to;
	    if(column[from].gcr_lo<to) 
	    {
		column[to].gcr_lo=column[from].gcr_lo;
		if(column[from].gcr_lo!= EMPTY)
		    column[column[from].gcr_lo].gcr_hi=to;
	    }
	    column[from].gcr_lo=column[from].gcr_hi= EMPTY;
	}
	else
	{
	    column[from].gcr_lo=to;
	    column[to  ].gcr_hi=from;
	    column[from].gcr_lOk=TRUE;
	    column[to  ].gcr_hOk=TRUE;
	}
    }
    column[from].gcr_v=net;	/*Paint a vertical segment	*/
    column[  to].gcr_v=net;

/* Don't free a track that is needed for a right or left edge connection 
 */
    if(column[from].gcr_wanted!=net)
    {
	column[from].gcr_h=(GCRNet *) NULL;
	column[from].gcr_hOk=FALSE;
	column[from].gcr_lOk=FALSE;
	column[from].gcr_hi=column[from].gcr_lo= EMPTY;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * gcrUnlinkTrack --
 *
 * Unlink a track from the linked list for the net.  This involves
 * updating the two-way links for the column element and its next
 * and previous list element.
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
gcrUnlinkTrack(col, toUnlink)
    GCRColEl *col;
    int toUnlink;
{
    GCRColEl *colPtr = &col[toUnlink];

    if (colPtr->gcr_hi != EMPTY)
	col[colPtr->gcr_hi].gcr_lo = colPtr->gcr_lo;
    if (colPtr->gcr_lo != EMPTY)
	col[colPtr->gcr_lo].gcr_hi = colPtr->gcr_hi;
    colPtr->gcr_lo = colPtr->gcr_hi = EMPTY;
    colPtr->gcr_h = (GCRNet *) NULL;
}

/*
 * ----------------------------------------------------------------------------
 *
 * gcrShellSort --
 *
 * The shell sort from page 116 of Kernighan and Ritchie.  Sorts in
 * increasing or decreasing order, depending on whether the flag is set.
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
gcrShellSort(v, n, isUp)
    GCRNet **v;
    int n;
    bool isUp;
{
    int gap, i, j, a1, a2;
    GCRNet * net;

    for(gap=n/2; gap>0; gap/=2)
	for(i=gap; i<n; i++)
	    for(j=i-gap; j>=0; j-=gap)
	    {
		a1=v[    j]->gcr_sortKey;
		a2=v[j+gap]->gcr_sortKey;
		if(isUp)
		    if(a1>a2)
		    {
			net=v[j+gap];
			v[j+gap]=v[j];
			v[j]=net;
		    }
		    else ;
		else
		    if(a1<a2)
		    {
			net=v[j+gap];
			v[j+gap]=v[j];
			v[j]=net;
		    }
	    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * gcrVertClear --
 *
 * See if there is a clear run from "from" to "to" in the given column.
 *
 * Results:
 *	Return TRUE if clear, else FALSE.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
gcrVertClear(col, from, to)
    GCRColEl *	col;
    int		from;
    int		to;
{
    int		i, flags;
    GCRNet   *	net;

    net=col[from].gcr_h;
    if(from>to)
    {
	i=from;
	from=to;
	to=i;
    }

    for(i=from; i<=to; i++)
    {
	flags=col[i].gcr_flags;
	if((col[i].gcr_v!=net)&&(col[i].gcr_v!=(GCRNet *) NULL))
	    return(FALSE);		/* Already wired vertically	*/
	else
	if((flags & GCRCE) && (i!=to))
	    return(FALSE);		/* Column ended			*/
	else
	if( (flags & (GCRBLKP|GCRBLKM|GCRX|GCRCC)) && 
		(col[i].gcr_h!=net) && (col[i].gcr_h!=(GCRNet *) NULL) )
	    return(FALSE);		/* Poly and metal blocked	*/
    }

    return(TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * gcrCopyCol --
 *
 * Make a copy of the given column array.
 *
 * Results:
 *	A pointer to a newly malloc'ed column array of the given size.
 *
 * Side effects:
 *	Allocates memory.
 *
 * ----------------------------------------------------------------------------
 */

GCRColEl *
gcrCopyCol(col, size)
    GCRColEl *col;
    int size;
{
    GCRColEl *	result;
    int i, limit;

    result = (GCRColEl *) mallocMagic((unsigned) ((size+2) * sizeof (GCRColEl)));
    limit = size + 2;
    for (i = 0; i < limit; i++)
	result[i] = col[i];
    return (result);
}

/*
 * ----------------------------------------------------------------------------
 *
 * gcrLinkTrack --
 *
 * Establishes hi and lo links when a net gets assigned to a track
 * the hard way.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes links in the column.
 *
 * ----------------------------------------------------------------------------
 */

void
gcrLinkTrack(col, net, track, width)
    GCRColEl *col;
    GCRNet *net;
    int track, width;
{
    int i;

    ASSERT(col[track].gcr_hi == EMPTY, "LinkTrack");
    ASSERT(col[track].gcr_lo == EMPTY, "LinkTrack");

    col[track].gcr_h = net;
    col[track].gcr_hi = EMPTY;
    for (i = track + 1; i <= width; i++)
	if (col[i].gcr_h == net)
	{
	    col[track].gcr_hi = i;
	    col[i].gcr_lo = track;
	    break;
	}

    col[track].gcr_lo = EMPTY;
    for (i = track - 1; i > 0; i--)
	if (col[i].gcr_h == net)
	{
	    col[track].gcr_lo = i;
	    col[i].gcr_hi = track;
	    break;
	}
}
