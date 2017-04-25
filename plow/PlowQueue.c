/*
 * PlowQueue.c --
 *
 * Plowing.
 * Queue of edges to move.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/plow/PlowQueue.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "debug/debug.h"
#include "plow/plowInt.h"
#include "utils/malloc.h"

/*
 * The following describe the queue of edges to be moved.
 * The array plowBinArray is indexed by the initial X coordinate
 * of an edge (relative to plowBinXBase).  Each element plowBinArray[n]
 * is a pointer to a list of Edges whose initial X coordinates are
 * plowBinXBase + n.
 *
 * There is an array for each plane in a CellDef.
 */

int plowNumBins;		/* Number of horizontal bins */
int plowDistance;		/* Distance the plow is moving */
int plowBinXBase;		/* Left-hand coordinate of cell's bbox */
int plowNumEdges;		/* Number of edges currently in queue */
Edge **plowBinArray[NP];	/* Array of bins for each X coordinate */
Edge **plowFirstBin[NP];	/* First bin that has any edges in it */
Edge **plowLastBin[NP];		/* Last bin that has any edges in it */

/* Debugging */
int plowTooFar;			/* # times we reduced plow size */

/*
 * ----------------------------------------------------------------------------
 *
 * plowQueueInit --
 *
 * Initialize the queue of edges to be moved by plowing.
 * The argument 'bbox' is the bounding box of the cell
 * being plowed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Obvious.
 *
 * ----------------------------------------------------------------------------
 */

void
plowQueueInit(bbox, dist)
    Rect *bbox;	/* Bounding box for the cell being plowed */
    int dist;	/* Distance the plow moves */
{
    Edge **pptr, **pend;
    int pNum;
    unsigned binArraySize;

    plowNumBins = bbox->r_xtop - bbox->r_xbot + 1;
    plowDistance = dist;
    plowBinXBase = bbox->r_xbot;
    plowNumEdges = 0;
    plowTooFar = 0;

    binArraySize = plowNumBins * sizeof (Edge *);
    for (pNum = 0; pNum < DBNumPlanes; pNum++)
    {
	/* Don't need planes for DRC, etc. */
	if (pNum > 0 && pNum < PL_TECHDEPBASE)
	    continue;

	plowBinArray[pNum] = (Edge **) mallocMagic(binArraySize);
	plowFirstBin[pNum] = (Edge **) NULL;
	plowLastBin[pNum] = (Edge **) NULL;
	pptr = plowBinArray[pNum];
	pend = &pptr[plowNumBins];
	while (pptr < pend)
	    *pptr++ = (Edge *) NULL;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowQueueDone --
 *
 * Free the memory allocated by plowQueueInit above.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees memory.
 *
 * ----------------------------------------------------------------------------
 */

void
plowQueueDone()
{
    int pNum;

    for (pNum = 0; pNum < DBNumPlanes; pNum++)
    {
	if (pNum == 0 || pNum >= PL_TECHDEPBASE)
	    freeMagic((char *) plowBinArray[pNum]);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowQueueAdd --
 *
 * Add an edge to the queue of those to be moved.
 *
 * Results:
 *	Returns 0 always (so it can be used as a filter function).
 *
 * Side effects:
 *	Adds edges to the queue.
 *
 * ----------------------------------------------------------------------------
 */

#define	SAMETYPE(e1, e2)	((e1)->e_ltype == (e2)->e_ltype \
			      && (e1)->e_rtype == (e2)->e_rtype)

int
plowQueueAdd(eadd)
    Edge *eadd;	/* Edge added to queue.  We assume that
				 * e_ltype and e_rtype have been set to
				 * the types on the LHS and RHS of this
				 * edge, respectively.
				 */
{
    extern CellDef *plowYankDef;
    extern int plowQueuedEdges;
    int xbin = eadd->e_x - plowBinXBase;
    Edge *enew, *eprev, *ep;
    Edge **pbin;
    int pNum;
    Rect addRect;

    ASSERT(eadd->e_ybot < eadd->e_ytop, "plowQueueAdd");
    ASSERT(xbin < plowNumBins, "plowQueueAdd");

    /*
     * Make sure we're not moving the edge too far.
     * This should only happen in the event of design
     * rule violations in the initial circuit, but we
     * keep track of the number of times it happened
     * for the sake of debugging.
     */
    if (eadd->e_newx > eadd->e_x + plowDistance)
    {
	eadd->e_newx = eadd->e_x + plowDistance;
	plowTooFar++;
    }

    /* Display this edge if we're debugging */
    plowQueuedEdges++;
    if (DebugIsSet(plowDebugID, plowDebAdd))
	plowDebugEdge(eadd, plowCurrentRule, "add");

    /* Which plane is this on? */
    pNum = eadd->e_pNum;

    /*
     * If this is the first edge added to this bin, we may have to update
     * the first or last pointers.
     */
    pbin = &plowBinArray[pNum][xbin];
    if (*pbin == (Edge *) NULL)
    {
	if (plowFirstBin[pNum] == (Edge **) NULL)
	    plowFirstBin[pNum] = plowLastBin[pNum] = pbin;
	else if (pbin < plowFirstBin[pNum])
	    plowFirstBin[pNum] = pbin;
	else if (pbin > plowLastBin[pNum])
	    plowLastBin[pNum] = pbin;
	goto prepend;
    }

    /*
     * If a cell, see if there is already an edge ep in the queue
     * for this cell.  If so, update ep->e_newx to the larger of
     * eadd->e_newx and ep->e_newx and we're done.  Otherwise,
     * just prepend this edge to the queue.
     */
    if (pNum == 0)
    {
	ASSERT(eadd->e_use != (CellUse *) NULL, "plowQueueAdd");
	for (ep = *pbin; ep; ep = ep->e_next)
	{
	    if (ep->e_use == eadd->e_use)
	    {
		if (eadd->e_newx > ep->e_newx)
		    ep->e_newx = eadd->e_newx;
		goto done;
	    }
	}
	goto prepend;
    }

    /*
     * There was one or more edge in this bin.
     * Edges are sorted on increasing e_ybot coordinate, so we can
     * stop when ep->e_ybot > eadd->e_ytop.
     * The edges are disjoint in Y.
     */

    /* Skip all edges strictly below eadd */
    for (ep = *pbin, eprev = (Edge *) NULL;
	    ep && ep->e_ytop < eadd->e_ybot;
	    eprev = ep, ep = ep->e_next)
    {
	/* Nothing */;
    }

    /*
     * Keep going until we find an edge above or touching eadd's top,
     * or we run out of edges in this bin.  We keep addRect updated
     * to the unprocessed portion of eadd->e_rect.  Initially, this
     * rectangle is non-degenerate since eadd->e_ybot < eadd->e_ytop.
     * THE FOLLOWING LOOP IS HAIRY.
     */
    addRect = eadd->e_rect;
    for ( ; ep && ep->e_ybot < addRect.r_ytop; eprev = ep, ep = ep->e_next)
    {
	/*
	 * Done when we've processed all of addRect.
	 * Since we may have changed eprev->e_newx, we
	 * want to check to see if it can merge with
	 * the new ep.
	 */
	if (addRect.r_ybot >= addRect.r_ytop)
	    goto mergeFinal;

	/*
	 *	ep->e_ybot     <  addRect.r_ytop
	 *	addRect.r_ybot <= ep->e_ytop
	 *	addRect.r_ybot <  addRect.r_ytop
	 *
	 * We know that addRect is non-degenerate and either overlaps
	 * ep or touches its top.  Whatever portion of addRect that lies
	 * below ep we KNOW does not overlap any other edges.
	 */
	if (!SAMETYPE(ep, eadd))
	{
	    /*
	     * If not the same type of edge, then they can't overlap.
	     * Handle creating the new edge for addRect on the next iteration,
	     * since we never merge up.
	     */
	    ASSERT(addRect.r_ybot == ep->e_ytop, "plowQueueAdd");
	    continue;
	}

	/* If moving by the same amount as ep, absorb eadd into ep */
	if (ep->e_newx == eadd->e_newx)
	{
	    if (addRect.r_ybot < ep->e_ybot) ep->e_ybot = addRect.r_ybot;
	    goto mergeDown;
	}

	/* Remember, ep->e_ybot < addRect.r_ytop */
	if (addRect.r_ybot < ep->e_ybot)
	{
	    /*
	     * Clip the portion of addRect below ep (we know
	     * that there is a non-degenerate part of addRect
	     * above ep by the comment above).
	     * Try to merge the lower part of addRect with eprev;
	     * otherwise, create a new edge for this portion.
	     */
	    if (eprev && SAMETYPE(eadd, eprev)
		      && eprev->e_newx == eadd->e_newx
		      && eprev->e_ytop == addRect.r_ybot)
	    {
		/* Merge with eprev */
		eprev->e_ytop = ep->e_ybot;
	    }
	    else
	    {
		/* Create a new edge, replacing eprev */
		enew = (Edge *) mallocMagic((unsigned) (sizeof (Edge)));
		*enew = *ep;
		enew->e_ybot = addRect.r_ybot;
		enew->e_ytop = ep->e_ybot;
		enew->e_newx = eadd->e_newx;
		if (eprev) eprev->e_next = enew; else *pbin = enew;
		enew->e_next = ep;
		eprev = enew;
		plowNumEdges++;
	    }
	    addRect.r_ybot = ep->e_ybot;
	}
	else if (ep->e_ybot < addRect.r_ybot)
	{
	    /* Done except for merging if ep and addRect only touch */
	    if (ep->e_ytop == addRect.r_ybot)
		goto mergeDown;

	    /*
	     * Clip the portion of ep below addRect.
	     * The new edge becomes eprev.
	     */
	    enew = (Edge *) mallocMagic((unsigned) (sizeof (Edge)));
	    *enew = *ep;
	    enew->e_ytop = ep->e_ybot = addRect.r_ybot;
	    enew->e_next = ep;
	    if (eprev) eprev->e_next = enew; else *pbin = enew;
	    eprev = enew;
	    plowNumEdges++;
	}

	/*
	 * At this point, addRect.r_ybot == ep->e_ybot, and
	 * addRect.r_ytop > ep->e_ybot.  Clip the portion of
	 * ep above addRect.
	 */
	if (ep->e_ytop > addRect.r_ytop)
	{
	    enew = (Edge *) mallocMagic((unsigned) (sizeof (Edge)));
	    *enew = *ep;
	    enew->e_ybot = ep->e_ytop = addRect.r_ytop;
	    enew->e_next = ep->e_next;
	    ep->e_next = enew;
	    plowNumEdges++;
	}

	/*
	 * Update the portion of the original ep that
	 * was overlapped by addRect.
	 */
	ep->e_newx = MAX(ep->e_newx, eadd->e_newx);

	/*
	 * Try to merge ep with eprev.
	 * If successful, eprev->e_ytop gets updated, ep gets
	 * freed, and we leave ep pointing to eprev.
	 */
mergeDown:
	addRect.r_ybot = ep->e_ytop;
	if (eprev && SAMETYPE(ep, eprev)
		  && eprev->e_newx == ep->e_newx
		  && eprev->e_ytop == ep->e_ybot)
	{
	    eprev->e_ytop = ep->e_ytop;
	    eprev->e_next = ep->e_next;
	    freeMagic((char *) ep);
	    ep = eprev;
	    plowNumEdges--;
	}

	/* NOTE: we switched ep above if we merged with eprev */
    }

    /*
     * Something may be left of addRect.
     * Try to merge with eprev if possible; otherwise, allocate
     * a new edge and make this the new eprev.
     */
    if (addRect.r_ybot < addRect.r_ytop)
    {
	if (eprev && SAMETYPE(eprev, eadd)
		  && eprev->e_newx == eadd->e_newx
		  && eprev->e_ytop == addRect.r_ybot)
	{
	    eprev->e_ytop = addRect.r_ytop;
	}
	else
	{
	    enew = (Edge *) mallocMagic((unsigned) (sizeof (Edge)));
	    *enew = *eadd;
	    enew->e_ybot = addRect.r_ybot;
	    if (eprev) eprev->e_next = enew; else *pbin = enew;
	    enew->e_next = ep;
	    eprev = enew;
	    plowNumEdges++;
	}
    }

mergeFinal:
    /*
     * If ep is non-NULL, it is above or touching the top of
     * eprev (either as extended in the first branch above, or
     * as newly allocated in the second).
     */
    if (ep && SAMETYPE(ep, eprev)
	   && ep->e_newx == eprev->e_newx
	   && ep->e_ybot == eprev->e_ytop)
    {
	eprev->e_ytop = ep->e_ytop;
	eprev->e_next = ep->e_next;
	freeMagic((char *) ep);
	plowNumEdges--;
    }
    goto done;

    /*
     * Prepend eadd to the current bin.
     */
prepend:
    enew = (Edge *) mallocMagic((unsigned) (sizeof (Edge)));
    *enew = *eadd;
    enew->e_next = *pbin;
    *pbin = enew;
    plowNumEdges++;

done:
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowQueueLeftmost --
 *
 * Returns the leftmost edge from the queue.
 *
 * Results:
 *	TRUE if an edge was filled in, FALSE otherwise.
 *
 * Side effects:
 *	Removes an edge from the queue and deallocates it
 *	after filling in the caller's Edge.
 *
 * ----------------------------------------------------------------------------
 */

bool
plowQueueLeftmost(edge)
    Edge *edge;
{
    Edge *enew, **pp, **plast;
    int pNum;
    int pMin, xMin;

    if (plowNumEdges <= 0)
	return (FALSE);

    /* Find the leftmost edge in any of the planes */
    xMin = INFINITY, pMin = -1;
    for (pNum = 0; pNum < DBNumPlanes; pNum++)
	if (pNum == 0 || pNum >= PL_TECHDEPBASE)
	    if ((pp = plowFirstBin[pNum]) && (pp - plowBinArray[pNum]) < xMin)
		xMin = pp - plowBinArray[pMin = pNum];

    pNum = pMin;
    ASSERT(pNum >= 0, "plowQueueLeftmost");
    ASSERT(*plowFirstBin[pNum] != (Edge *) NULL, "plowQueueLeftmost");

    plowNumEdges--;
    enew = *plowFirstBin[pNum];
    *plowFirstBin[pNum] = enew->e_next;
    if (*plowFirstBin[pNum] == (Edge *) NULL)
    {
	/*
	 * No more edges left in this bin, so we have to advance the bounds
	 * pointer plowFirstBin[pNum].
	 */
	pp = plowFirstBin[pNum];
	plast = plowLastBin[pNum];
	while (pp < plast && *pp == (Edge *) NULL)
	    pp++;

	/*
	 * If there are no more edges on this plane, set both
	 * plowFirstBin[pNum] and plowLastBin[pNum] to NULL.
	 */
	if (*pp) plowFirstBin[pNum] = pp;
	else plowFirstBin[pNum] = plowLastBin[pNum] = (Edge **) NULL;
    }

    /* Display this edge if we're debugging */
    if (DebugIsSet(plowDebugID, plowDebNext))
	plowDebugEdge(enew, (RuleTableEntry *) NULL, "next");

    /* Debugging */
    ASSERT(enew->e_ytop > enew->e_ybot, "plowQueueLeftmost");
    ASSERT(enew->e_newx >= enew->e_x, "plowQueueLeftmost");

    /* Fill in the caller's edge */
    *edge = *enew;
    freeMagic((char *) enew);
    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowQueueRightmost --
 *
 * Returns the rightmost edge from the queue.
 *
 * Results:
 *	TRUE if an edge was filled in, FALSE otherwise.
 *
 * Side effects:
 *	Removes an edge from the queue and deallocates it
 *	after filling in the caller's Edge.
 *
 * ----------------------------------------------------------------------------
 */

bool
plowQueueRightmost(edge)
    Edge *edge;
{
    Edge *enew, **pp, **plast;
    int pNum;
    int pMax, xMax;

    if (plowNumEdges <= 0)
	return (FALSE);

    /* Find the rightmost edge in any of the planes */
    xMax = MINFINITY, pMax = -1;
    for (pNum = 0; pNum < DBNumPlanes; pNum++)
	if (pNum == 0 || pNum >= PL_TECHDEPBASE)
	    if ((pp = plowLastBin[pNum]) && (pp - plowBinArray[pNum]) > xMax)
		xMax = pp - plowBinArray[pMax = pNum];

    pNum = pMax;
    ASSERT(pNum >= 0, "plowQueueRightmost");
    ASSERT(*plowLastBin[pNum] != (Edge *) NULL, "plowQueueRightmost");

    plowNumEdges--;
    enew = *plowLastBin[pNum];
    *plowLastBin[pNum] = enew->e_next;
    if (*plowLastBin[pNum] == (Edge *) NULL)
    {
	/*
	 * No more edges left in this bin, so we have to advance the bounds
	 * pointer plowLastBin[pNum].
	 */
	pp = plowLastBin[pNum];
	plast = plowFirstBin[pNum];
	while (pp > plast && *pp == (Edge *) NULL)
	    pp--;

	/*
	 * If there are no more edges on this plane, set both
	 * plowFirstBin[pNum] and plowLastBin[pNum] to NULL.
	 */
	if (*pp) plowLastBin[pNum] = pp;
	else plowFirstBin[pNum] = plowLastBin[pNum] = (Edge **) NULL;
    }

    /* Display this edge if we're debugging */
    if (DebugIsSet(plowDebugID, plowDebNext))
	plowDebugEdge(enew, (RuleTableEntry *) NULL, "next");

    /* Debugging */
    ASSERT(enew->e_ytop > enew->e_ybot, "plowQueueRightmost");
    ASSERT(enew->e_newx >= enew->e_x, "plowQueueRightmost");

    /* Fill in the caller's edge */
    *edge = *enew;
    freeMagic((char *) enew);
    return (TRUE);
}
