/*
 * grouteNet.c -
 *
 * Global signal router.  Code to route one segment of a
 * net, from a set of possible starting points to a single
 * destination point.  Uses a Lee-like wavefront maze router
 * approach, with several performance heuristics to focus
 * the search strongly toward the destination rather than
 * propagating isotropically from the starting points.
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
static char sccsid[] = "@(#)grouteNet.c	4.3 MAGIC (Berkeley) 12/6/85";
#endif  /* not lint */

#include <stdio.h>
#include <sys/types.h>
#include <sys/times.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "utils/hash.h"
#include "utils/heap.h"
#include "utils/malloc.h"
#include "debug/debug.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "gcr/gcr.h"
#include "windows/windows.h"
#include "utils/main.h"
#include "dbwind/dbwind.h"
#include "utils/signals.h"
#include "router/router.h"
#include "grouter/grouter.h"
#include "utils/netlist.h"
#include "textio/textio.h"
#include "utils/styles.h"

Point glDestPoint;	/* Point we're routing to */

/* The following penalties get scaled by RtrGridSpacing */
bool glPenaltiesScaled = FALSE;
int glJogPenalty = 5;
int glObsPenalty1 = 5, glObsPenalty2 = 3;
int glNbrPenalty1 = 2, glNbrPenalty2 = 5;
int glOrphanPenalty = 3;
int glChanPenalty = 1;

/* Used in glNormalPropagate() */
typedef struct
{
    int		 pr_tmin, pr_tmax;	/* Range of top pin indices */
    int		 pr_bmin, pr_bmax;	/* Range of bottom pin indices */
    int		 pr_lmin, pr_lmax;	/* Range of left pin indices */
    int		 pr_rmin, pr_rmax;	/* Range of right pin indices */
} PinRanges;

PinRanges glInitRange =
{
	INFINITY, MINFINITY,
	INFINITY, MINFINITY,
	INFINITY, MINFINITY,
	INFINITY, MINFINITY
};

/*
 * Auxiliary information used by the new maze routing algorithm.
 *
 * Whenever the maze router is ready to select the next frontier
 * point to expand, it first moves some points to these two heaps:
 * glProximityHeap, which is sorted by distance to the destination
 * glDestPoint, and glBestHeap, which is sorted by the same cost
 * as glHeap, namely total estimated cost: gl_length plus distance
 * to glDestPoint.
 *
 * In addition, glBestCost is used to remember the total estimated
 * cost of the best total estimated cost point in any of the heaps.
 */
Heap glProximityHeap;
Heap glBestHeap;
int glBestCost;
GlPoint *glBestPt;

/*
 * Points are transferred from glHeap to the two heaps above
 * only when their cost is less than (fudgeNumer/fudgeDenom)
 * times the best total cost of any point still in any of the
 * heaps, i.e, (fudgeNumer * glBestCost) / fudgeDenom.
 */
int fudgeNumer = 13;
int fudgeDenom = 10;

/* Marker to indicate an already-processed GlPoint */
#define	PROCESSED_MARK	((GlPoint *) 1)

/*
 * ----------------------------------------------------------------------------
 *
 * glRouteToPoint --
 *
 * Perform the global routing for the given pin.  The heap starts off
 * with one or more starting points.  This routine finds the least
 * cost path from any starting point to the destination point,
 * where cost includes both distance and crossing penalties.
 *
 * Results:
 *	Pointer to the ending point in the global routing.  Chasing parent
 *	pointers to the root yields the global routing result.  Returns
 *	NULL if no path was found.
 *
 * Side effects:
 *	The search point heap (glHeap) changes.  If a successful global
 *	routing was found, all points along the path are removed from
 *	the heap, leaving other partial path points still on the heap.
 *	If no routing was found, the heap is empty.
 *
 * ----------------------------------------------------------------------------
 */

GlPoint *
glRouteToPoint(loc, bestCost)
    NLTermLoc *loc;	/* Route from points on heap to this point */
    int bestCost;		/* If we haven't found a path with less than
				 * this cost, return NULL.
				 */
{
    int heapPts, frontierPts, startPts, headFree;
    GCRChannel *inCh;
    GlPoint *inPt;
    bool newBest, newPaths;
    HeapEntry hEntry;
    GlPage *headPage;
    GlPoint *lastPt;

    /* Initialize auxiliary heaps */
    HeapInit(&glBestHeap, 64, FALSE, FALSE);
    HeapInit(&glProximityHeap, 64, FALSE, FALSE);

    glNumTries++;
    if (glLogFile)
	fprintf(glLogFile, "---\t%d\t0,0\t0\t0\n", glNumTries);

    /* Initialization */
    if (!glPenaltiesScaled)
	glScalePenalties();

    /* Remember for resetting GCRPins later */
    headPage = glCurPage;
    headFree = glCurPage->glp_free;

    /* Remember for debugging */
    heapPts = glCrossingsExpanded;
    frontierPts = glCrossingsConsidered;
    startPts = glHeap.he_used;

    /*
     * Passed to glPropagateFn() for use in estimating the remaining
     * distance to the destination point.  Invert sense of loc->nloc_dir
     * (the direction of loc->nloc_ch relative to the cell) to give the
     * side of the channel on which loc->nloc_stem lies.
     */
    glDestPoint = loc->nloc_stem;
    ASSERT(GEO_SAMEPOINT(loc->nloc_pin->gcr_point, loc->nloc_stem),
		"glRouteToPoint");

    /*
     * The modified shortest path algorithm extends the partial path for
     * which the sum of the current path cost plus the Manhattan distance
     * to the destination point is the smallest.
     */
    glBestCost = 0;
    glBestPt = (GlPoint *) NULL;
    newPaths = newBest = TRUE;
    lastPt = (GlPoint *) NULL;
    while (!SigInterruptPending && glPopFromHeap(&newBest, newPaths, &hEntry))
    {
	newPaths = FALSE;
	glCrossingsExpanded++;
	inPt = (GlPoint *) hEntry.he_id;

	/* Done if we reach the destination crossing point */
	if (GEO_SAMEPOINT(inPt->gl_point, glDestPoint))
	{
	    lastPt = inPt;
	    break;
	}

	/* Done if the best path is already more expensive than best cost */
	if (inPt->gl_length >= bestCost && !DebugIsSet(glDebugID,glDebNewHeaps))
	    break;

	/* Reject if its pin has already been visited more cheaply */
	if (inPt->gl_length > inPt->gl_pin->gcr_cost)
	{
	    glCrossingsObsolete++;
	    continue;
	}

	if (glLogFile)
	    glLogPath(inPt, hEntry.he_int);

	/*
	 * There are three possibilities:
	 *	- inPt is in the destination channel, or
	 *	- it is in a river-routing channel, or
	 *	- it is in a normal channel.
	 * In the latter case, we use a trick to avoid having to process
	 * all the crossings in the channel at once.
	 */
	inCh = inPt->gl_ch;
	if (inCh != loc->nloc_chan || !glFinalPropagate(inPt, loc))
	{
	    if (inCh->gcr_type != CHAN_NORMAL)
		(void) glRiverPropagate(inPt);
	    else
		glNormalPropagate(inPt, inCh, hEntry.he_int);
	}

	/* Remember that points may have been added to glHeap */
	newPaths = TRUE;
    }

    /* Reset the cost stored with each GCRPin */
    glResetCost(headPage, headFree);

    /* Record number of points processed if debugging */
    if (DebugIsSet(glDebugID, glDebHisto))
	glHistoAdd(heapPts, frontierPts, startPts);

    /* Free the auxiliary heaps */
    HeapKill(&glBestHeap, (void (*)()) NULL);
    HeapKill(&glProximityHeap, (void (*)()) NULL);

    return (lastPt);
}

/*
 * ----------------------------------------------------------------------------
 *
 * glScalePenalties --
 *
 * Scale the penalties used in the global router cost function so they
 * reflect the actual grid size used during routing.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

void
glScalePenalties()
{
    glJogPenalty *= RtrGridSpacing;
    glObsPenalty1 *= RtrGridSpacing;
    glObsPenalty2 *= RtrGridSpacing;
    glNbrPenalty1 *= RtrGridSpacing;
    glNbrPenalty2 *= RtrGridSpacing;
    glOrphanPenalty *= RtrGridSpacing;
    glChanPenalty *= RtrGridSpacing;
    glPenaltiesScaled = TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glPopFromHeap --
 *
 * Obtain the next frontier point for consideration from the
 * heap glHeap.  The variable newPaths should be TRUE if
 * points were added to glHeap since the last call to this
 * procedure.
 *
 * Algorithm:
 *	We maintain three heaps.
 *	The first, glHeap, is the one to which points are added,
 *	and is referred to as the Reserve heap.
 *
 *	The two remaining heaps are auxiliary: glProximityHeap is sorted
 *	in order of increasing distance to the goal, and glBestHeap is
 *	sorted using the same key as glHeap (cost so far plus an estimate
 *	of the cost remaining to the destination).
 *
 *	The idea is to remove all points from glHeap that are
 *	within a certain percentage (fudgeNumer/fudgeDenom) of
 *	the best estimated cost so far (glBestCost), and add these
 *	to glProximityHeap.  The actual points we return are then
 *	selected from glProximityHeap, which causes points closest
 *	to the destination to be preferred over points farther away.
 *
 * Results:
 *	Returns TRUE if a new entry was stored in hEntry, or
 *	FALSE if no more points were available.
 *
 * Side effects:
 *	May pop points from glHeap and store points in or pop
 *	points from glBestHeap or glProximityHeap.  May change
 *	*pNewBest (it should be initially TRUE on the very first
 *	call to glPopFromHeap).  Also, may modify glBestCost
 *	(which should be zero on the first call).  Stores the
 *	entry popped from the top of glProximityHeap in the
 *	HeapEntry pointed to by hEntry.
 *
 * ----------------------------------------------------------------------------
 */

bool
glPopFromHeap(pNewBest, newPaths, hEntry)
    bool *pNewBest;	/* Should be TRUE on initial call; afterwards,
			 * we set it to TRUE when glBestCost is updated.
			 */
    bool newPaths;	/* TRUE if points added to glHeap since the last
			 * call to glPopFromHeap().
			 */
    HeapEntry *hEntry;	/* See above */
{
    HeapEntry *bestCostTop, *glHeapTop;
    int minAcceptableCost, newBestCost;
    GlPoint *topPt;

    if (!DebugIsSet(glDebugID, glDebNewHeaps))
	return (HeapRemoveTop(&glHeap, hEntry) != NULL);

    /*
     * If the top element on glBestHeap changed (this occurs
     * when it is popped from glProximityHeap in the previous
     * call), we have to move more points from glHeap to
     * glProximityHeap.
     */
    if (*pNewBest)
    {
	/*
	 * Pop bestcost heap until path that hasn't already been 
	 * processed is reached.   (Already processed paths are marked
	 * by setting their gl_next field to PROCESSED_MARK).  This
	 * point will be the one with the best possible cost plus
	 * estimate to the destination, and will be used to compute
	 * the minimum acceptable cost for transfer to glProximityHeap.
	 */
	while ((bestCostTop = HeapLookAtTop(&glBestHeap))
	    && ((GlPoint *) bestCostTop->he_id)->gl_next == PROCESSED_MARK)
	{
	    if (DebugIsSet(glDebugID, glDebHeap))
	    {
		TxPrintf("Discarding point (cost=%d): ", bestCostTop->he_int);
		glPrintPoint((GlPoint *) bestCostTop->he_id);
		TxPrintf("\n");
	    }
	    HeapRemoveTop(&glBestHeap, hEntry);
	}

	/*
	 * The "new" best cost is min of cost of top of the best-cost
	 * heap and the reserve heap.  If both heaps are empty, we've
	 * failed and should return FALSE.
	 */
	if (glHeapTop = HeapLookAtTop(&glHeap))
	{
	    if (bestCostTop == NULL || glHeapTop->he_int < bestCostTop->he_int)
	    {
		if (DebugIsSet(glDebugID, glDebHeap))
		    TxPrintf("Best cost really comes from glHeap\n");
		bestCostTop = glHeapTop;
	    }
	}
	else if (bestCostTop == NULL)
	    return FALSE;

	newBestCost = bestCostTop->he_int;
	glBestPt = (GlPoint *) bestCostTop->he_id;
	if (newBestCost == glBestCost)
	    *pNewBest = FALSE;
	glBestCost = newBestCost;

	if (DebugIsSet(glDebugID, glDebHeap))
	{
	    TxPrintf("New best (cost=%d): ", newBestCost);
	    glPrintPoint(glBestPt);
	    TxPrintf("\nCost %s\n", *pNewBest ? "changed" : "didn't change");
	}
    }

    /*
     * Move acceptably cheap paths from reserve to best heaps.
     * This has to happen if either the cutoff point changed in
     * the code above (*pNewBest == TRUE), or if points had been
     * added to glHeap since the last time we were called
     * (newPaths == TRUE).
     */
    if (*pNewBest || newPaths)
    {
	/*
	 * The minimum acceptable cost for transfer from the
	 * Reserve heap (glHeap) to the proximity and best heaps
	 * will be glBestCost * (fudgeNumer / fudgeDenom).
	 */ 
	minAcceptableCost = (glBestCost * fudgeNumer) / fudgeDenom;
	if (DebugIsSet(glDebugID, glDebHeap))
	    TxPrintf("Min acceptable cost = %d\n", minAcceptableCost);

	while ((glHeapTop = HeapRemoveTop(&glHeap, hEntry))
		&& glHeapTop->he_int <= minAcceptableCost)
	{
	    Point *p = &(((GlPoint *)(glHeapTop->he_id))->gl_point);
	    int dist = ABSDIFF(p->p_x, glDestPoint.p_x)
		     + ABSDIFF(p->p_y, glDestPoint.p_y);

	    if (DebugIsSet(glDebugID, glDebHeap))
	    {
		TxPrintf("Move to prox (pcost=%d,cost=%d): ",
			dist, glHeapTop->he_int);
		glPrintPoint((GlPoint *) glHeapTop->he_id);
		TxPrintf("\n");
	    }
	    HeapAddInt(&glBestHeap, glHeapTop->he_int, glHeapTop->he_id);
	    HeapAddInt(&glProximityHeap, dist, glHeapTop->he_id);
	}

	if (glHeapTop)
	    HeapAddInt(&glHeap, glHeapTop->he_int, glHeapTop->he_id);
    }

    /*
     * The next point to be processed is the one at the top of
     * the proximity heap, i.e., the one closest to the destination.
     * If we popped the current best, set newBest flag, so we know
     * to compute a new "best cost" the next time we're called.
     * Mark the point as "processed", so it will be discarded if
     * it ever comes to the top of glBestHeap above.
     */
    if (HeapRemoveTop(&glProximityHeap, hEntry) == NULL)
	return FALSE;

    topPt = (GlPoint *) hEntry->he_id;
    *pNewBest = (topPt == glBestPt);
    topPt->gl_next = PROCESSED_MARK;

    /*
     * Fix up the cost field of hEntry (remember, it was popped
     * from the proximity heap, which is keyed only on the estimated
     * distance remaining, while we want the cost plus the estimate
     * to the destination).
     */
    hEntry->he_int = topPt->gl_length
	    + ABSDIFF(topPt->gl_point.p_x, glDestPoint.p_x)
	    + ABSDIFF(topPt->gl_point.p_y, glDestPoint.p_y);

    if (DebugIsSet(glDebugID, glDebHeap))
    {
	TxPrintf("Returning point (%s, cost=%d): ",
			*pNewBest ? "best" : "not best", hEntry->he_int);
	glPrintPoint(topPt);
	TxPrintf("\n");
    }
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glFinalPropagate --
 *
 * Process a point that lies in the destination channel.
 * These points are treated specially since we don't need
 * to find any more crossings to other channels.
 *
 * Note that we do perform a density computation here, so
 * we should avoid attempts to route through a portion of
 * a channel where its capacity has been exceeded.
 *
 * Results:
 *	FALSE if the path to the destination was blocked because
 *	it was unreachable; TRUE otherwise.
 *
 * Side effects:
 *	May add a point to the heap.
 *
 * ----------------------------------------------------------------------------
 */

bool
glFinalPropagate(inPt, loc)
    GlPoint *inPt;	/* Point being processed */
    NLTermLoc *loc;	/* Destination point */
{
    GCRChannel *destCh = loc->nloc_chan;
    GCRPin *destPin = loc->nloc_pin;
    Point *destPoint = &loc->nloc_stem;
    GlPoint *outPt;
    int cost;

    cost = inPt->gl_length;
    cost += ABSDIFF(inPt->gl_point.p_x, destPoint->p_x);
    cost += ABSDIFF(inPt->gl_point.p_y, destPoint->p_y);

    /* Disallow the path if it exceeds the channel density */
    if (destCh->gcr_dMaxByRow >= destCh->gcr_length
	|| destCh->gcr_dMaxByCol >= destCh->gcr_width)
    {
	if (glDensityExceeded(destCh, inPt->gl_pin, destPin))
	    return (FALSE);
    }

#ifdef	notdef	/* Don't make it too difficult in the final channel */
    if (DebugIsSet(glDebugID, glDebStraight))
    {
	/* If the net runs across the channel, it must not jog */
	if (!glJogsAcrossChannel(inPt->gl_pin, destPin))
	    return (FALSE);
    }
#endif	/* notdef */

    cost = glCrossPenalty(cost, destCh, (GCRChannel *) NULL,
		    inPt->gl_pin, destPin);
    if (cost >= destPin->gcr_cost)
	return (TRUE);

    glCrossingsComplete++;
    outPt = glNewPoint(destPoint, destCh, destPin, cost, inPt);
    HeapAddInt(&glHeap, cost, (char *) outPt);
    if (glLogFile)
    {
	fprintf(glLogFile, "FIN\t%d\t%d,%d\t%d\n",
		glNumTries,
		outPt->gl_point.p_x, outPt->gl_point.p_y,
		outPt->gl_length);
    }

    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * glRiverPropagate --
 *
 * Process a point belonging to a river-routing channel.  Since these
 * channels can only be used for routing straight across to their
 * other side, we only need to consider a single crossing point.
 *
 * Results:
 *	Returns the cost with which the point was added to the heap,
 *	or -1 if the point wasn't added.
 *
 * Side effects:
 *	May add a point to the heap.
 *
 * ----------------------------------------------------------------------------
 */

int
glRiverPropagate(inPt)
    GlPoint *inPt;
{
    GCRPin *inPin = inPt->gl_pin, *outPin, *linkedPin;
    GCRChannel *inCh = inPt->gl_ch;
    int cost;
    GlPoint *outPt;

    /* Find the opposing pin */
    switch (inPin->gcr_side)
    {
	case GEO_NORTH: outPin = &inCh->gcr_bPins[inPin->gcr_x]; break;
	case GEO_SOUTH: outPin = &inCh->gcr_tPins[inPin->gcr_x]; break;
	case GEO_EAST:  outPin = &inCh->gcr_lPins[inPin->gcr_y]; break;
	case GEO_WEST:  outPin = &inCh->gcr_rPins[inPin->gcr_y]; break;
    }

    /* Propagate to this pin if it is free */
    if ((linkedPin = outPin->gcr_linked)
	    && linkedPin->gcr_pId == (GCRNet *) NULL)
    {
	/* Only add to heap if this path is cheapest so far */
	cost = inPt->gl_length
	     + ABSDIFF(inPt->gl_point.p_x, linkedPin->gcr_point.p_x)
	     + ABSDIFF(inPt->gl_point.p_y, linkedPin->gcr_point.p_y);
	if (cost < linkedPin->gcr_cost)
	{
	    linkedPin->gcr_cost = outPin->gcr_cost = cost;
	    outPt = glNewPoint(&outPin->gcr_point, linkedPin->gcr_ch, linkedPin,
			cost, inPt);
	    cost += ABSDIFF(glDestPoint.p_x, linkedPin->gcr_point.p_x)
		  + ABSDIFF(glDestPoint.p_y, linkedPin->gcr_point.p_y);
	    HeapAddInt(&glHeap, cost, (char *) outPt);
	    if (glLogFile)
	    {
		fprintf(glLogFile, "RIV\t%d\t%d,%d\t%d\t%d\n",
			glNumTries,
			outPt->gl_point.p_x, outPt->gl_point.p_y,
			outPt->gl_length, cost);
	    }
	    return (cost);
	}
    }

    return (-1);
}

/*
 * ----------------------------------------------------------------------------
 *
 * glNormalPropagate --
 *
 * Process a point (inPt) that lies in a normal routing channel (inCh)
 * that is not the destination channel.  Use a trick to minimize the
 * number of crossing points in inCh that must be processed, speeding
 * up global routing by a factor of 3 - 5, particularly in the case
 * of large channels with lots of crossing points per channel.
 *
 * Motivation:
 *	Most channels contain lots of pins.  Usually, most of them
 *	aren't even close to being on the least-cost path, as in
 *	the case below:
 *
 *				    D
 *
 *	+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *	|							|
 *	+							+
 *	|			    C				|
 *	+							+
 *	|							|
 *	+---+---+---+---+---+---+---S---+---+---+---+---+---+---+
 *
 *	(Here, "S" is a point on the boundary of channel "C", and "D"
 *	is the destination point, which happens to lie in the adjacent
 *	channel.  "D" is a very short distance from "S" compared with
 *	the width of the channel).
 *
 *	It's usually a waste of time to process the points of "C" that
 *	aren't on the direct path to the destination.  However, we
 *	can't just throw them away, because it may turn out that the
 *	direct path is blocked, and we need to use one of the points
 *	to the side, e.g:
 *
 *				    D
 *	BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB
 *	+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *	|							|
 *	+							+
 *	|			    C				|
 *	+							+
 *	|							|
 *	+---+---+---+---+---+---+---S---+---+---+---+---+---+---+
 *
 *	(Here "B" is a blockage in the channel containing "D", forcing
 *	points to the side of the channel to be used.)
 *
 * Algorithm:
 *	The trick is to ensure that the points to the side are added
 *	to the heap before they are needed, but not necessarily all
 *	at once.
 *
 *	We compute a rectangle around the starting point "S" that
 *	will contain the crossing points most likely to be of interest.
 *	This rectangle "R" is a bloated version of the bounding rectangle
 *	containing "S" and the destination point, clipped against the
 *	reachable portion of the channel (reachable in the sense of
 *	passing through no regions of maximum density):
 *
 *				................D....
 *				:		    :
 *	+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *	|			:		    :		|
 *	+			:	  R	    :		+
 *	|			:		    :		|
 *	+			:		    :		+
 *	|			:		    :		|
 *	+---+---+---+---+---+---+---S---+---+---+---+---+---+---+
 *
 *	Only crossings inside of "R" are added to the heap.  However,
 *	we put the point "S" BACK on the heap, remembering the area
 *	"R" already processed.  The trick is the cost with which "S"
 *	is added to the heap: sufficiently high so that points on the
 *	direct path to the destination are processed before "S" is
 *	reprocessed, but sufficiently low so that "S" is processed
 *	BEFORE the points outside of "R" would have been removed
 *	from the heap, had they been added at the same time "S"
 *	was originally processed.
 *
 *	When a point is removed from the heap, then, it may either
 *	be "virgin", or it may have been partially expanded.  The
 *	state of its expansion is described in inPt->gl_visited,
 *	which gives grid coordinates of a rectangle (including its
 *	top and right coordinates) covering all pins visited so far.
 *
 *	We determine a new set of pins to visit (based on heapCost,
 *	as described in the comments for glSetPinClip()), visit them,
 *	and then if all feasible points haven't yet been reached, we
 *	add a new point to the heap with a new gl_visited showing the
 *	points we processed on this iteration.  The cost for this new
 *	heap point is chosen to be less than the cost of any of the
 *	remaining points, so we have a chance to process them and put
 *	them on the heap before they are needed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May add many points to the heap.
 *
 * ----------------------------------------------------------------------------
 */

void
glNormalPropagate(inPt, inCh, heapCost)
    GlPoint *inPt;	/* Point on the boundary of inCh */
    GCRChannel *inCh;	/* Channel through which we're passing */
    int heapCost;		/* Cost with which inPt was added to heap */
{
    PinRanges pinRange, prevRange;
    int x, y, baseCost, min, max;
    GCRPin *inPin = inPt->gl_pin;
    Rect pinRect, densRect;
    GCRPin *outPin;
    int i, cost;
    bool noCheckJogs;

    /*
     * Starting at inPt, figure out how high, low, left, and right
     * we can go based on density limits.  If we're completely
     * hemmed in and can't go anywhere, mark the pin as now being
     * blocked and return.
     */
    if (!glSetDensityClip(inPt, inCh, &densRect))
    {
#ifdef	notdef
	inPin->gcr_pId = GCR_BLOCKEDNETID;
	if (inPin->gcr_linked)
	    inPin->gcr_linked->gcr_pId = GCR_BLOCKEDNETID;
#endif	/* notdef */
	return;
    }

    /*
     * Next, figure out the range of pins we're interested in
     * visiting on this pass.  This range will generally cover
     * less than the entire channel, and will depend on the
     * relative positions of inPt and glDestPoint.  The range
     * is clipped against densRect.
     */
    glSetPinClip(inPt, inCh, heapCost, &densRect, &pinRect);
    glRectToRange(inCh, &pinRect, &pinRange);

    /*
     * We've already visited the pins identified in pinRect.
     * Visit all the pins that lie in pinClip but not in pinRect.
     * At the end, if pinRect is not equal to densRect, create
     * a new GlPoint like inPt but with gl_range equal to pinRect,
     * and add this point back to the heap with a carefully-chosen
     * new cost (see below).
     */
    glRectToRange(inCh, &inPt->gl_range, &prevRange);
    baseCost = inPt->gl_length + glChanPenalty;
    x = inPt->gl_point.p_x;
    y = inPt->gl_point.p_y;

#define	OKPIN(p)	\
    ((p)->gcr_pId == NULL \
	&& (p)->gcr_linked && (p)->gcr_linked->gcr_pId == NULL)
#define	XCOST(x, pin)	ABSDIFF((x), (pin)->gcr_point.p_x)
#define	YCOST(y, pin)	ABSDIFF((y), (pin)->gcr_point.p_y)

    /* Top */
    min = pinRange.pr_tmin, max = pinRange.pr_tmax;
    if (inCh->gcr_tPins->gcr_pNext && min <= max)
    {
	cost = baseCost + inCh->gcr_area.r_ytop - y;
	noCheckJogs = TRUE;
	if (inPin->gcr_side != GEO_SOUTH) cost += glJogPenalty;
	else if (DebugIsSet(glDebugID, glDebStraight)) noCheckJogs = FALSE;
	for (i = min, outPin = &inCh->gcr_tPins[i]; i <= max; i++, outPin++)
	{
	    if (i == prevRange.pr_tmin)
	    {
		i = prevRange.pr_tmax;
		outPin += prevRange.pr_tmax - prevRange.pr_tmin;
		continue;
	    }
	    if (OKPIN(outPin) && cost + XCOST(x, outPin) < outPin->gcr_cost)
		if (noCheckJogs || !glJogsAcrossChannel(inPin, outPin))
		    (void) glPropagateFn(outPin->gcr_linked->gcr_ch,
				outPin, inPt);
	}
    }

    /* Bottom */
    min = pinRange.pr_bmin, max = pinRange.pr_bmax;
    if (inCh->gcr_bPins->gcr_pNext && min <= max)
    {
	cost = baseCost + y - inCh->gcr_area.r_ybot;
	noCheckJogs = TRUE;
	if (inPin->gcr_side != GEO_NORTH) cost += glJogPenalty;
	else if (DebugIsSet(glDebugID, glDebStraight)) noCheckJogs = FALSE;
	for (i = min, outPin = &inCh->gcr_bPins[i]; i <= max; i++, outPin++)
	{
	    if (i == prevRange.pr_bmin)
	    {
		i = prevRange.pr_bmax;
		outPin += prevRange.pr_bmax - prevRange.pr_bmin;
		continue;
	    }
	    if (OKPIN(outPin) && cost + XCOST(x, outPin) < outPin->gcr_cost)
		if (noCheckJogs || !glJogsAcrossChannel(inPin, outPin))
		    (void) glPropagateFn(outPin->gcr_linked->gcr_ch,
				outPin, inPt);
	}
    }

    /* Left */
    min = pinRange.pr_lmin, max = pinRange.pr_lmax;
    if (inCh->gcr_lPins->gcr_pNext && min <= max)
    {
	cost = baseCost + x - inCh->gcr_area.r_xbot;
	noCheckJogs = TRUE;
	if (inPin->gcr_side != GEO_EAST) cost += glJogPenalty;
	else if (DebugIsSet(glDebugID, glDebStraight)) noCheckJogs = FALSE;
	for (i = min, outPin = &inCh->gcr_lPins[i]; i <= max; i++, outPin++)
	{
	    if (i == prevRange.pr_lmin)
	    {
		i = prevRange.pr_lmax;
		outPin += prevRange.pr_lmax - prevRange.pr_lmin;
		continue;
	    }
	    if (OKPIN(outPin) && cost + YCOST(y, outPin) < outPin->gcr_cost)
		if (noCheckJogs || !glJogsAcrossChannel(inPin, outPin))
		    (void) glPropagateFn(outPin->gcr_linked->gcr_ch,
				outPin, inPt);
	}
    }

    /* Right */
    min = pinRange.pr_rmin, max = pinRange.pr_rmax;
    if (inCh->gcr_rPins->gcr_pNext && min <= max)
    {
	cost = baseCost + inCh->gcr_area.r_xtop - x;
	noCheckJogs = TRUE;
	if (inPin->gcr_side != GEO_WEST) cost += glJogPenalty;
	else if (DebugIsSet(glDebugID, glDebStraight)) noCheckJogs = FALSE;
	for (i = min, outPin = &inCh->gcr_rPins[i]; i <= max; i++, outPin++)
	{
	    if (i == prevRange.pr_rmin)
	    {
		i = prevRange.pr_rmax;
		outPin += prevRange.pr_rmax - prevRange.pr_rmin;
		continue;
	    }
	    if (OKPIN(outPin) && cost + YCOST(y, outPin) < outPin->gcr_cost)
		if (noCheckJogs || !glJogsAcrossChannel(inPin, outPin))
		    (void) glPropagateFn(outPin->gcr_linked->gcr_ch,
				outPin, inPt);
	}
    }

    /*
     * If we haven't visited all the points in this channel that
     * are reachable because of density, create a copy of inPt and
     * add it to the heap, marking its gl_range as the Rect equivalent
     * of pinRange.  The cost we use in adding this copy to the heap
     * is chosen to ensure that we get a chance to add the least cost
     * of the remaining points to the heap before anything else of that
     * cost gets removed from the heap.
     */
    cost = glMinRemainingCost(inPt, inCh, &pinRect, &densRect);
    if (cost < INFINITY)
    {
	inPt = glNewPoint(&inPt->gl_point, inCh, inPin, inPt->gl_length,
			inPt->gl_parent);
	inPt->gl_range = pinRect;
	HeapAddInt(&glHeap, cost - 1, (char *) inPt);
	glCrossingsPartial++;
    }
    else glCrossingsComplete++;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glMinRemainingCost --
 *
 * Choose the cost with which glNormalPropagate will add a point to the
 * heap.  This cost is INFINITY if all the points inside of dRect have
 * been processed (*pRect == *dRect); otherwise, it is the cost of a
 * route from inPt to the closest pin in the set contained in *dRect
 * but not in *pRect, plus an estimate of the cost from this pin to
 * the destination point, plus the cost of inPt.
 *
 * Results:
 *	Returns the cost above.
 *
 * Side effects:
 *	May add many points to the heap.
 *
 * ----------------------------------------------------------------------------
 */


int
glMinRemainingCost(inPt, inCh, pRect, dRect)
    GlPoint *inPt;
    GCRChannel *inCh;
    Rect *pRect, *dRect;
{
    int cost, n;
    GCRPin *pins;

    /*
     * In the code below, 'cost' represents the sum of the cost
     * from inPt to a point in densRect but not in pinRect, plus
     * the estimated cost from that point to the destination.
     */
    cost = INFINITY;

    /*
     * Pins on top and bottom.
     * Figure out which x-coordinate on the top or bottom will
     * give minimum cost to the destination.
     */
    n = (glDestPoint.p_x - inCh->gcr_origin.p_x) / RtrGridSpacing;
    n = INRANGE(n, pRect->r_xbot - 1, pRect->r_xtop + 1);
    n = INRANGE(n, dRect->r_xbot, dRect->r_xtop);

	/* Top pins */
    if (dRect->r_ytop == inCh->gcr_width + 1)
    {
	pins = inCh->gcr_tPins;
	if (pRect->r_ytop < dRect->r_ytop)
	{
	    /* Haven't reached top yet */
	    cost = glPinCost(inPt, &pins[n], cost);
	}
	else
	{
	    /* Have processed part of the top already */
	    if (pRect->r_xbot > dRect->r_xbot)
		cost = glPinCost(inPt, &pins[pRect->r_xbot - 1], cost);
	    if (pRect->r_xtop < dRect->r_xtop)
		cost = glPinCost(inPt, &pins[pRect->r_xtop + 1], cost);
	}
    }

	/* Bottom pins */
    if (dRect->r_ybot == 0)
    {
	pins = inCh->gcr_bPins;
	if (pRect->r_ybot > dRect->r_ybot)
	{
	    /* Haven't reached bottom yet */
	    cost = glPinCost(inPt, &pins[n], cost);
	}
	else
	{
	    /* Have processed part of the bottom already */
	    if (pRect->r_xbot > dRect->r_xbot)
		cost = glPinCost(inPt, &pins[pRect->r_xbot - 1], cost);
	    if (pRect->r_xtop < dRect->r_xtop)
		cost = glPinCost(inPt, &pins[pRect->r_xtop + 1], cost);
	}
    }

    /* Pins on right and left */
    n = (glDestPoint.p_y - inCh->gcr_origin.p_y) / RtrGridSpacing;
    n = INRANGE(n, pRect->r_ybot - 1, pRect->r_ytop + 1);
    n = INRANGE(n, dRect->r_ybot, dRect->r_ytop);

	/* Right pins */
    if (dRect->r_xtop == inCh->gcr_length + 1)
    {
	pins = inCh->gcr_rPins;
	if (pRect->r_xtop < dRect->r_xtop)
	{
	    /* Haven't reached RHS yet */
	    cost = glPinCost(inPt, &pins[n], cost);
	}
	else
	{
	    /* Have processed part of the RHS already */
	    if (pRect->r_ybot > dRect->r_ybot)
		cost = glPinCost(inPt, &pins[pRect->r_ybot - 1], cost);
	    if (pRect->r_ytop < dRect->r_ytop)
		cost = glPinCost(inPt, &pins[pRect->r_ytop + 1], cost);
	}
    }

	/* Left pins */
    if (dRect->r_xbot == 0)
    {
	pins = inCh->gcr_lPins;
	if (pRect->r_xbot > dRect->r_xbot)
	{
	    /* Haven't reached LHS yet */
	    cost = glPinCost(inPt, &pins[n], cost);
	}
	else
	{
	    /* Have processed part of the LHS already */
	    if (pRect->r_ybot > dRect->r_ybot)
		cost = glPinCost(inPt, &pins[pRect->r_ybot - 1], cost);
	    if (pRect->r_ytop < dRect->r_ytop)
		cost = glPinCost(inPt, &pins[pRect->r_ytop + 1], cost);
	}
    }


    if (cost == INFINITY)
	return (cost);

    return (cost + inPt->gl_length);
}

/*
 * glPinCost --
 *
 * Used by above to give the distance from inPt->gl_point to pin->gcr_point,
 * plus the distance from pin->gcr_point to the destination, plus any
 * penalties that are guaranteed to apply (the channel penalty and
 * possibly a jog penalty are all we consider now).
 *
 * Results:
 *	Returns the minimum of the above cost and 'oldCost'.
 *
 * Side effects:
 *	None.
 */


int
glPinCost(inPt, pin, oldCost)
    GlPoint *inPt;
    GCRPin *pin;
    int oldCost;
{
    int cost;

    /* Length from inPt to pin */
    cost = ABSDIFF(inPt->gl_point.p_x, pin->gcr_point.p_x)
	 + ABSDIFF(inPt->gl_point.p_y, pin->gcr_point.p_y);

    /* Estimate of length from pin to destination */
    cost += ABSDIFF(glDestPoint.p_x, pin->gcr_point.p_x)
	 +  ABSDIFF(glDestPoint.p_y, pin->gcr_point.p_y);

    /* Penalties */
    cost += glChanPenalty;
    if (inPt->gl_point.p_x != pin->gcr_point.p_x
	    && inPt->gl_point.p_y != pin->gcr_point.p_y)
	cost += glJogPenalty;

    return (MIN(cost, oldCost));
}

/*
 * ----------------------------------------------------------------------------
 *
 * glSetDensityClip --
 *
 * Determine which pins in inCh are reachable from inPt->gl_pin,
 * given density restrictions.  Leaves *dRect set to the Rect
 * (in grid coordinates for 'ch') describing visitable pins
 * on each side.  If a whole side of pins is not reachable
 * (e.g, the top), then that coordinate of *dRect (e.g, r_ytop)
 * won't reach all the way to the corresponding extreme value
 * for the pin indices for 'ch' (e.g, ch->gcr_width+1).
 *
 * Results:
 *	Returns the cost above.
 *
 * Side effects:
 *	May add many points to the heap.
 *
 * ----------------------------------------------------------------------------
 */

bool
glSetDensityClip(inPt, ch, dRect)
    GlPoint *inPt;
    GCRChannel *ch;
    Rect *dRect;
{
    GCRPin *inPin = inPt->gl_pin;
    short *den, maxdensity;
    int n;

    /*
     * Default: in the absence of density violations, we can
     * visit all the pins on each side of the channel.
     */
    dRect->r_xbot = dRect->r_ybot = 0;
    dRect->r_xtop = ch->gcr_length + 1;
    dRect->r_ytop = ch->gcr_width + 1;

    if (ch->gcr_dMaxByRow >= ch->gcr_length)
    {
	den = ch->gcr_dColsByRow;
	maxdensity = ch->gcr_length;
	glVDensityChecks++;

	/* Walk up */
	for (n = MAX(inPin->gcr_y, 1); n <= ch->gcr_width; n++)
	{
	    if (den[n] >= maxdensity)
	    {
		/* Can't reach top */
		glVDensityFailures++;
		dRect->r_ytop = n-1;
		break;
	    }
	}

	/* Walk down */
	for (n = MIN(inPin->gcr_y, ch->gcr_width); n >= 1; n--)
	    if (den[n] >= maxdensity)
	    {
		/* Can't reach bottom */
		glVDensityFailures++;
		dRect->r_ybot = n+1;
		break;
	    }
    }

    if (ch->gcr_dMaxByCol >= ch->gcr_width)
    {
	den = ch->gcr_dRowsByCol;
	maxdensity = ch->gcr_width;
	glHDensityChecks++;

	/* Walk right */
	for (n = MAX(inPin->gcr_x, 1); n <= ch->gcr_length; n++)
	    if (den[n] >= maxdensity)
	    {
		/* Can't reach right hand side */
		glHDensityFailures++;
		dRect->r_xtop = n-1;
		break;
	    }

	/* Walk left */
	for (n = MIN(inPin->gcr_x, ch->gcr_length); n >= 1; n--)
	    if (den[n] >= maxdensity)
	    {
		/* Can't reach left hand side */
		glHDensityFailures++;
		dRect->r_xbot = n+1;
		break;
	    }
    }

    if (dRect->r_xtop < dRect->r_xbot) dRect->r_ytop = dRect->r_ybot - 1;
    else if (dRect->r_ytop < dRect->r_ybot) dRect->r_xtop = dRect->r_xbot - 1;

    return (dRect->r_xtop >= dRect->r_xbot && dRect->r_ytop >= dRect->r_ybot);
}

/*
 * ----------------------------------------------------------------------------
 *
 * glSetPinClip --
 *
 * Figure out the range of pins we're interested in visiting on this pass.
 * The Rect for these pins will generally cover less than the entire channel,
 * and will depend on the relative positions of inPt and glDestPoint.  The
 * Rect is clipped against dRect.
 *
 * Algorithm:
 *	Construct an imaginary rectangle ('R') with inPt at one corner and
 *	glDestPoint at the other.  Bloat R on all sides by the amount
 *	described below and then determine all pins that lie both within
 *	this rectangle and dRange.
 *
 *	The amount that R is bloated depends on heapCost, which is the
 *	value currently at the top of the heap.  The simple case is when
 *	heapCost is just inPt->gl_length plus the Manhattan distance from
 *	inPt to glDestPoint (which we'll call estCost).  This case should
 *	correspond to the first time inPt is processed.  If we weren't to
 *	bloat R at all, all pins lying inside it could end up with the same
 *	total cost (gl_length plus estimated cost to glDestPoint) as inPt,
 *	so we at least have to add them.
 *
 *	Things are more complex if heapCost is greater than inPt->gl_length
 *	plus estCost.  Let diff be the difference.  We will bloat the rect
 *	by diff/2.  The reason is that points in the bloated rectangle could
 *	end up with a total cost equal to heapCost, since they involve a
 *	detour of diff/2 units out and then diff/2 units back to align
 *	with the destination point.
 *
 *	The actual amount of the bloat is diff/2 + RtrGridSpacing*4 to
 *	give a little extra slop.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Fills in *pRect.
 *
 * ----------------------------------------------------------------------------
 */

void
glSetPinClip(inPt, inCh, heapCost, dRect, pRect)
    GlPoint *inPt;
    GCRChannel *inCh;
    int heapCost;
    Rect *dRect, *pRect;
{
    int bloat, estCost, t;
    Rect r;

    if (DebugIsSet(glDebugID, glDebAllPoints))
    {
	*pRect = *dRect;
	return;
    }

    /*
     * First construct the rectangle in lambda coordinates.
     * Make it canonical: ll <= ur.
     */
    r.r_ll = inPt->gl_point;
    r.r_ur = glDestPoint;
    if (r.r_xbot > r.r_xtop) t = r.r_xbot, r.r_xbot = r.r_xtop, r.r_xtop = t;
    if (r.r_ybot > r.r_ytop) t = r.r_ybot, r.r_ybot = r.r_ytop, r.r_ytop = t;

    /* Bloat it */
    estCost = ABSDIFF(inPt->gl_point.p_x, glDestPoint.p_x)
	    + ABSDIFF(inPt->gl_point.p_y, glDestPoint.p_y);
    bloat = (heapCost - (estCost + inPt->gl_length)) / 2;
    bloat += RtrGridSpacing * 4;
    GEO_EXPAND(&r, bloat, &r);

    /* Convert to grid coordinates */
    pRect->r_xbot = (r.r_xbot - inCh->gcr_origin.p_x) / RtrGridSpacing;
    pRect->r_ybot = (r.r_ybot - inCh->gcr_origin.p_y) / RtrGridSpacing;
    pRect->r_xtop = (r.r_xtop - inCh->gcr_origin.p_x) / RtrGridSpacing;
    pRect->r_ytop = (r.r_ytop - inCh->gcr_origin.p_y) / RtrGridSpacing;

    /* Clip against dRect */
    GEOCLIP(pRect, dRect);
}

/*
 * ----------------------------------------------------------------------------
 *
 * glResetCost --
 *
 * Reset the costs stored with each GlPin after we've completed
 * the global routing for a single net.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets each pin's cost to INFINITY.
 *
 * ----------------------------------------------------------------------------
 */

void
glResetCost(headPage, headFree)
    GlPage *headPage;
    int headFree;
{
    GlPage *gpage;
    GCRPin *pin;
    int n;

    for (gpage = headPage; gpage; gpage = gpage->glp_next)
    {
	for (n = headFree; n < gpage->glp_free; n++)
	    if (pin = gpage->glp_array[n].gl_pin)
	    {
		pin->gcr_cost = INFINITY;
		if (pin->gcr_linked)
		    pin->gcr_linked->gcr_cost = INFINITY;
	    }
	if (gpage == glCurPage)
	    break;
	headFree = 0;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * glPropagateFn --
 *
 * Search function called by glRouteToPoint for each unused crossing
 * point exiting the channel whose entry point is being processed.
 *
 * Results:
 *	Returns the cost with which the point was added to the heap,
 *	if it was added, or -1 if not.
 *
 * Side effects:
 *	If the crossing point is usable and the path through inPin
 *	is the cheapest, add the new crossing point to the heap.
 *
 * ----------------------------------------------------------------------------
 */

int
glPropagateFn(outCh, outPin, inPt)
    GCRChannel *outCh;		/* Channel entered from outPin */
    GCRPin *outPin;	/* Exit pin, also in inCh */
    GlPoint *inPt;	/* Point being considered */
{
    int cost, n;
    int finalCost;
    GlPoint *outPt;

    glCrossingsConsidered++;

    /* Add the distance to the exit point to the cost */
    cost = inPt->gl_length;
    n = inPt->gl_point.p_x - outPin->gcr_point.p_x;
    if (n < 0) cost -= n; else cost += n;
    n = inPt->gl_point.p_y - outPin->gcr_point.p_y;
    if (n < 0) cost -= n; else cost += n;

    /* Adjust by penalty and see if we can throw out this path */
    cost = glCrossPenalty(cost, inPt->gl_ch, outCh, inPt->gl_pin, outPin);
    if (cost >= outPin->gcr_cost)
	return (-1);

    if (DebugIsSet(glDebugID, glDebMaze))
	glPropagateDebug(inPt, inPt->gl_pin, outCh, outPin,
			outPin->gcr_cost, cost);

    /* Remember the new cheapest cost path and add point to the heap */
#ifdef	notdef
    if (outPin->gcr_point.p_x == iPt.p_x && outPin->gcr_point.p_y == iPt.p_y)
	TxPrintf("Bingo! cost=%d\n", cost);
#endif	/* notdef */
    outPin->gcr_cost = cost;
    if (outPin->gcr_linked)
	outPin->gcr_linked->gcr_cost = cost;
    outPt = glNewPoint(&outPin->gcr_point, outCh,
		outPin->gcr_linked, cost, inPt);

    /*
     * Special handling if outPt is in a river-routing channel.
     * There's no point in adding it to the heap, since we know
     * the only point that is reachable from it.  Just propagate
     * depth-first from here.
     */
    if (outCh->gcr_type != CHAN_NORMAL)
	return (glRiverPropagate(outPt));

    /*
     * Estimate the least possible cost to reach the destination
     * using this path.
     */
    finalCost = cost + ABSDIFF(glDestPoint.p_x, outPin->gcr_point.p_x)
		     + ABSDIFF(glDestPoint.p_y, outPin->gcr_point.p_y);
    HeapAddInt(&glHeap, finalCost, (char *) outPt);
    if (glLogFile)
    {
	fprintf(glLogFile, "ADD\t%d\t%d,%d\t%d\t%d\n",
		glNumTries,
		outPt->gl_point.p_x, outPt->gl_point.p_y,
		outPt->gl_length, finalCost);
    }
    return (finalCost);
}

/*
 * ----------------------------------------------------------------------------
 *
 * glCrossPenalty --
 *
 * Evaluate a set of crossing points through a channel to determine the
 * total cost of bad crossing penalties.  These penalties are added to
 * 'cost' to give the total cost.  (The argument 'cost' should be the
 * sum of the cost to get to 'inPin' plus the Manhattan distance from
 * 'inPin' to 'outPin').
 *
 * Density considerations are ignored here; they are the responsibilty
 * of the caller.
 *
 * Results:
 *	The total cost plus penalty, in lambda units.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
glCrossPenalty(cost, inCh, outCh, inPin, outPin)
    int cost;			/* Distance cost */
    GCRChannel *inCh;		/* Both inPin and outPin are in this channel */
    GCRChannel *outCh;		/* Channel to which outPin exits, or NULL
				 * if outPin is the destination.
				 */
    GCRPin *inPin;	/* Pin used to enter inCh */
    GCRPin *outPin;	/* Pin used to exit inCh into outCh */
{
    GCRPin *otherPin;
    int count;

    /* Penalty for using lots of channels */
    cost += glChanPenalty;

    /* Penalize if the net doesn't run straight across the channel */
    if (inPin->gcr_x != outPin->gcr_x && inPin->gcr_y != outPin->gcr_y)
	cost += glJogPenalty;

    /*
     * If there is an obstacle or hazard over a crossing, or an
     * obstacle somewhere along the track or column of the pin,
     * then assess a penalty.  Look on both sides of the crossing
     * to get this penalty.
     */
#define	BADCROSSFLAGS	(GCROBST|GCRHAZRD|GCRTCC)
    otherPin = outPin->gcr_linked;
    if (outCh && outCh->gcr_type == CHAN_NORMAL)
    {
	if ((otherPin->gcr_pFlags & BADCROSSFLAGS)
		|| otherPin->gcr_pSize != 0)
	{
	    ASSERT(otherPin->gcr_pSize >= 0, "glCrossPenalty");
	    cost += glObsPenalty1;
	    if (otherPin->gcr_pFlags & GCROBST)
		cost += glObsPenalty2 * otherPin->gcr_pSize;
	    else if (otherPin->gcr_pFlags & GCRHAZRD)
		cost += MAX(glObsPenalty2*otherPin->gcr_pSize
				- otherPin->gcr_pDist, 0);
	}
    }

    /*
     * Done if this is not a cheaper way of reaching outPin,
     * or if this channel is used for river-routing (in which
     * case the subsequent penalty computation is not needed).
     */
    if (cost >= outPin->gcr_cost || inCh->gcr_type != CHAN_NORMAL)
	return (cost);

    if ((outPin->gcr_pFlags & BADCROSSFLAGS)
	|| outPin->gcr_pSize != 0)
    {
	ASSERT(outPin->gcr_pSize >= 0, "glCrossPenalty");
	cost += glObsPenalty1;
	if (outPin->gcr_pFlags & GCROBST)
	    cost += glObsPenalty2 * outPin->gcr_pSize;
	else if (outPin->gcr_pFlags & GCRHAZRD)
	    cost += MAX(glObsPenalty2 * outPin->gcr_pSize
				- outPin->gcr_pDist, 0);
    }

    /* Done if this is not a cheaper way of reaching outPin */
    if (cost >= outPin->gcr_cost)
	return (cost);

    /*
     * If both neighboring pins are used, the penalty is 5.
     * If only one of the neighboring pins is used, the penalty is only 2.
     */
    count = 0;
    if ((outPin + 1)->gcr_pId) count++;
    if ((outPin - 1)->gcr_pId) count++;
    if (count == 2) cost += glNbrPenalty2;
    else if (count == 1) cost += glNbrPenalty1;

    /*
     * If the path turns in the channel and the exit crossing
     * isn't a paired orphan, the penalty is 3.
     */
    if (outPin->gcr_side != GeoOppositePos[inPin->gcr_side])
    {
	switch (outPin->gcr_side)
	{
	    case GEO_NORTH: otherPin = &inCh->gcr_bPins[outPin->gcr_x]; break;
	    case GEO_SOUTH: otherPin = &inCh->gcr_tPins[outPin->gcr_x]; break;
	    case GEO_EAST:  otherPin = &inCh->gcr_lPins[outPin->gcr_y]; break;
	    case GEO_WEST:  otherPin = &inCh->gcr_rPins[outPin->gcr_y]; break;
	}
	if (otherPin->gcr_pId == (GCRNet *) NULL)
	    cost += glOrphanPenalty;
    }

    return (cost);
}

/*
 * ----------------------------------------------------------------------------
 *
 * glDensityExceeded --
 *
 * Determine if the path from inPin to outPin through inCh passes
 * through any region of maximum density.
 *
 * Results:
 *	Returns TRUE if the path passes through a maximum-density
 *	region, FALSE if not.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
glDensityExceeded(inCh, inPin, outPin)
    GCRChannel *inCh;
    GCRPin *inPin, *outPin;
{
    int min, max, maxdensity;
    short *den;
    short *dlast;

    if (inCh->gcr_dMaxByRow >= inCh->gcr_length)
    {
	glVDensityChecks++;
	maxdensity = inCh->gcr_length;
	min = MIN(inPin->gcr_y, outPin->gcr_y);
	min = INRANGE(min, 1, inCh->gcr_width);
	max = MAX(inPin->gcr_y, outPin->gcr_y);
	max = INRANGE(max, 1, inCh->gcr_width);
	den = &inCh->gcr_dColsByRow[min];
	dlast = &inCh->gcr_dColsByRow[max];
	while (den <= dlast)
	    if (*den++ >= maxdensity)
	    {
		glVDensityFailures++;
		return (TRUE);
	    }
    }
    if (inCh->gcr_dMaxByCol >= inCh->gcr_width)
    {
	glHDensityChecks++;
	maxdensity = inCh->gcr_width;
	min = MIN(inPin->gcr_x, outPin->gcr_x);
	min = INRANGE(min, 1, inCh->gcr_length);
	max = MAX(inPin->gcr_x, outPin->gcr_x);
	max = INRANGE(max, 1, inCh->gcr_length);
	den = &inCh->gcr_dRowsByCol[min];
	dlast = &inCh->gcr_dRowsByCol[max];
	while (den <= dlast)
	    if (*den++ >= maxdensity)
	    {
		glHDensityFailures++;
		return (TRUE);
	    }
    }

    return (FALSE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * glRectToRange --
 *
 * Convert from a Rect to a PinRanges representation of a range of pin
 * values.  Both representations contain the same information, but the
 * latter is more convenient to work with in glNormalPropagate(), while
 * the former requires half the storage.
 *
 * The dimensions of 'ch' are used to determine whether the pins on a
 * given side of a channel are to be excluded or not: if the Rect 'r'
 * doesn't extend all the way to 0 on the left or bottom, or to
 * ch->gcr_width + 1 on the top or ch->gcr_length + 1 on the right,
 * then the pins on that side are excluded.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets *pr to the PinRanges structure implied by 'r'.
 *
 * ----------------------------------------------------------------------------
 */

void
glRectToRange(ch, r, pr)
    GCRChannel *ch;
    Rect *r;
    PinRanges *pr;
{
    Rect clipR;

    /* Initialize to empty */
    *pr = glInitRange;

    /* Actual ranges will be from 1 to height or width of channel */
    clipR.r_xbot = MAX(r->r_xbot, 1);
    clipR.r_ybot = MAX(r->r_ybot, 1);
    clipR.r_xtop = MIN(r->r_xtop, ch->gcr_length);
    clipR.r_ytop = MIN(r->r_ytop, ch->gcr_width);

    /* Top */
    if (r->r_ytop == ch->gcr_width + 1)
	pr->pr_tmin = clipR.r_xbot, pr->pr_tmax = clipR.r_xtop;

    /* Bottom */
    if (r->r_ybot == 0)
	pr->pr_bmin = clipR.r_xbot, pr->pr_bmax = clipR.r_xtop;

    /* Left */
    if (r->r_xbot == 0)
	pr->pr_lmin = clipR.r_ybot, pr->pr_lmax = clipR.r_ytop;

    /* Right */
    if (r->r_xtop == ch->gcr_length + 1)
	pr->pr_rmin = clipR.r_ybot, pr->pr_rmax = clipR.r_ytop;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glJogsAcrossChannel --
 *
 * Determine whether a signal crosses from one side of a channel
 * to the other with a jog.
 *
 * Results:
 *	TRUE if inPin and outPin are on opposite sides of the channel
 *	but are not directly opposite each other.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
glJogsAcrossChannel(inPin, outPin)
    GCRPin *inPin, *outPin;
{
    switch (inPin->gcr_side)
    {
	case GEO_NORTH:
	    if (outPin->gcr_side == GEO_SOUTH
		    && outPin->gcr_point.p_x != inPin->gcr_point.p_x)
		return (TRUE);
	    break;
	case GEO_SOUTH:
	    if (outPin->gcr_side == GEO_NORTH
		    && outPin->gcr_point.p_x != inPin->gcr_point.p_x)
		return (TRUE);
	    break;
	case GEO_EAST:
	    if (outPin->gcr_side == GEO_WEST
		    && outPin->gcr_point.p_y != inPin->gcr_point.p_y)
		return (TRUE);
	    break;
	case GEO_WEST:
	    if (outPin->gcr_side == GEO_EAST
		    && outPin->gcr_point.p_y != inPin->gcr_point.p_y)
		return (TRUE);
	    break;
    }

    return (FALSE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * glPropagateDebug --
 *
 * Used for debugging; print lots of information about the pair of
 * crossing points inPin and outPin (propagating through inPt->gl_ch).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Prints stuff on the terminal; also displays the areas of
 *	the two crossing points on the screen.
 *
 * ----------------------------------------------------------------------------
 */

void
glPropagateDebug(inPt, inPin, outCh, outPin, prevCost, distCost)
    GlPoint *inPt;
    GCRPin *inPin, *outPin;
    GCRChannel *outCh;
    int prevCost, distCost;
{
    char mesg[256];
    Point linkedPt;
    Rect r;

    r.r_ll = r.r_ur = inPt->gl_point;
    r.r_xtop++, r.r_ytop++;
    nrShowRect(EditCellUse->cu_def, &r, STYLE_SOLIDHIGHLIGHTS);
    if (inPin->gcr_linked) linkedPt = inPin->gcr_linked->gcr_point;
    else linkedPt = TiPlaneRect.r_ll;
    (void) sprintf(mesg,
	"ENTRY ch=%x (%d,%d) (%d,%d) pin=%x (%d,%d) linked=%x (%d,%d)",
		    outCh,
		    outCh->gcr_area.r_xbot, outCh->gcr_area.r_ybot,
		    outCh->gcr_area.r_xtop, outCh->gcr_area.r_ytop,
		    inPin, inPin->gcr_point.p_x, inPin->gcr_point.p_y,
		    inPin->gcr_linked, linkedPt.p_x, linkedPt.p_y);
    nrMore(mesg);
    nrShowRect(EditCellUse->cu_def, &r, STYLE_ERASEHIGHLIGHTS);

    if (outPin->gcr_linked) linkedPt = outPin->gcr_linked->gcr_point;
    else linkedPt = TiPlaneRect.r_ll;
    (void) sprintf(mesg,
	"EXIT  ch=%x (%d,%d) (%d,%d) %d/%d pin=%x (%d,%d) linked=%x (%d,%d)",
		    outCh,
		    outCh->gcr_area.r_xbot, outCh->gcr_area.r_ybot,
		    outCh->gcr_area.r_xtop, outCh->gcr_area.r_ytop,
		    prevCost, distCost,
		    outPin, outPin->gcr_point.p_x, outPin->gcr_point.p_y,
		    outPin->gcr_linked, linkedPt.p_x, linkedPt.p_y);
    r.r_ll = r.r_ur = outPin->gcr_point;
    r.r_xtop++, r.r_ytop++;
    nrShowRect(EditCellUse->cu_def, &r, STYLE_SOLIDHIGHLIGHTS);
    nrMore(mesg);
    nrShowRect(EditCellUse->cu_def, &r, STYLE_ERASEHIGHLIGHTS);
}

/*
 * ----------------------------------------------------------------------------
 *
 * glLogPath --
 *
 * Print information about the point 'inPt' to the file glLogFile,
 * but only if this is the first time the point was removed from
 * the heap (indicated by inPt->gl_range being the same as glInitRect.)
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May print to glLogFile.
 *
 * ----------------------------------------------------------------------------
 */

void
glLogPath(inPt, cost)
    GlPoint *inPt;
    int cost;
{
    extern Rect glInitRect;

    if (bcmp((char *)&inPt->gl_range, (char *)&glInitRect, sizeof (Rect)) == 0)
    {
	fprintf(glLogFile, "TOP\t%d\t%d,%d\t%d\t%d\n",
		glNumTries,
		inPt->gl_point.p_x, inPt->gl_point.p_y,
		inPt->gl_length, cost);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * glPrintPoint --
 *
 * Print information about the GlPoint 'inPt'.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Prints to the terminal.
 *
 * ----------------------------------------------------------------------------
 */

void
glPrintPoint(inPt)
    GlPoint *inPt;
{
    TxPrintf("(%d,%d) l=%d p=0x%x c=0x%x",
	inPt->gl_point.p_x, inPt->gl_point.p_y,
	inPt->gl_length, inPt->gl_pin, inPt->gl_ch);
}

