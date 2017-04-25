/*
 * grouteMaze.c -
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/grouter/grouteMaze.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
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

/* Information about the target point */
Point glMazeDestPoint;	/* Point we're routing to */
Tile *glMazeDestTile;	/* Tile in glChanPlane containing destination */

/*
 * TRUE if we're only finding the shortest path,
 * FALSE if we consider all paths.
 */
bool glMazeShortest;

/* Forward declarations */
GlPoint *glMazeFindPath();
int glMazeTileFunc();
void glMazePropFinal();
void glMazePropRiver();
void glMazePropNormal();
void glMazeTile();
bool glMazeCheckLoop();


/*
 * ----------------------------------------------------------------------------
 *
 * glMazeFindPath --
 *
 * This is the inner loop of the global router.  It assumes that a collection
 * of starting points has already been added to the heap, and returns the
 * best cost path to the destination from those paths on the heap.
 *
 * Results:
 *	Pointer to the crossing point for the destination.  Follow
 *	the gl_path pointers to get the remaining points in the route
 *	all the way back to one of the starting points that were
 *	initially added to the heap.  Returns NULL if no path was
 * 	found with less cost than bestCost.
 *
 * Side effects:
 *	The search point heap (glMazeHeap) changes.  If a successful global
 *	routing was found, all points along the path are removed from
 *	the heap, leaving other partial path points still on the heap.
 *
 * ----------------------------------------------------------------------------
 */

GlPoint *
glMazeFindPath(loc, bestCost)
    NLTermLoc *loc;	/* Destination point */
    int bestCost;	/* Beat this cost or give up */
{
    int heapPts, startPts, frontierPts;
    GlPoint *inPt;
    GCRPin *inPin;
    HeapEntry hEntry;
    GlPoint *lastPt;

    /* Remember for debugging */
    heapPts = glCrossingsExpanded;
    frontierPts = glCrossingsAdded;
    startPts = glMazeHeap.he_used;

    /*
     * The modified shortest path algorithm extends the partial path for
     * which the sum of the current path cost plus the Manhattan distance
     * to the destination point is the smallest.
     */
    lastPt = (GlPoint *) NULL;
    while (!SigInterruptPending && HeapRemoveTop(&glMazeHeap, &hEntry))
    {
	glCrossingsExpanded++;
	inPt = (GlPoint *) hEntry.he_id;
	inPin = inPt->gl_pin;

	/* Done if we reach the destination point */
	if (GEO_SAMEPOINT(inPin->gcr_point, glMazeDestPoint))
	{
	    lastPt = inPt;
	    break;
	}

	/*
	 * Give up if the best candidate for expansion is already
	 * more expensive than the previous best-cost path.
	 */
	if (inPt->gl_cost >= bestCost)
	    break;

	/*
	 * Reject if this pin already has another path to it
	 * that's cheaper (only reject if we're looking for
	 * the shortest path).
	 */
	if (glMazeShortest && inPt->gl_cost > inPt->gl_pin->gcr_cost)
	    continue;

	/*
	 * Expand this point.
	 * Use the type of tile to determine whether we process this
	 * point as being in a river-routing channel, instead of the
	 * type of channel overlapping the tile, since it's possible
	 * for a normal channel to become covered with CHAN_HRIVER or
	 * CHAN_VRIVER tiles if it contains a point of maximum density.
	 */
	if (inPt->gl_tile == glMazeDestTile)
	    glMazePropFinal(inPt, loc);
	else if (TiGetType(inPt->gl_tile) == CHAN_NORMAL)
	    glMazePropNormal(inPt);
	else
	    glMazePropRiver(inPt);
    }

    /* Record number of points processed if debugging */
    if (DebugIsSet(glDebugID, glDebHisto))
	glHistoAdd(heapPts, frontierPts, startPts);

    return lastPt;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glMazePropFinal --
 *
 * Process a point that lies in the destination channel.
 * These points are treated specially since we don't need
 * to find any more crossings to other channels.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds a point to the heap.
 *
 * ----------------------------------------------------------------------------
 */

void
glMazePropFinal(inPt, loc)
    GlPoint *inPt;	/* Point being processed */
    NLTermLoc *loc;	/* Destination point */
{
    GCRPin *destPin = loc->nloc_pin;
    Point *destPoint = &loc->nloc_stem;
    GlPoint *outPt;
    int cost;

    cost = inPt->gl_cost;
    cost += ABSDIFF(inPt->gl_pin->gcr_point.p_x, destPoint->p_x);
    cost += ABSDIFF(inPt->gl_pin->gcr_point.p_y, destPoint->p_y);
    cost += glChanPenalty;

    if (glMazeShortest)
    {
	if (cost >= destPin->gcr_cost)
	    return;
	destPin->gcr_cost = cost;
    }

    outPt = glPathNew(destPin, cost, inPt);
    outPt->gl_tile = glMazeDestTile;
    HeapAddInt(&glMazeHeap, cost, (char *) outPt);
    glCrossingsAdded++;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glMazePropRiver --
 *
 * Process a point belonging to a river-routing channel.  Since these
 * channels can only be used for routing straight across to their
 * other side, we only need to consider a single crossing point.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May add a point to the heap.
 *
 * ----------------------------------------------------------------------------
 */

void
glMazePropRiver(inPt)
    GlPoint *inPt;
{
    GCRPin *inPin = inPt->gl_pin, *outPin, *linkedPin;
    GCRChannel *inCh = inPin->gcr_ch;
    int cost;
    Tile *outTile;
    GlPoint *outPt;

    /* Find the opposing pin */
    switch (inPin->gcr_side)
    {
	case GEO_NORTH: outPin = &inCh->gcr_bPins[inPin->gcr_x]; break;
	case GEO_SOUTH: outPin = &inCh->gcr_tPins[inPin->gcr_x]; break;
	case GEO_EAST:  outPin = &inCh->gcr_lPins[inPin->gcr_y]; break;
	case GEO_WEST:  outPin = &inCh->gcr_rPins[inPin->gcr_y]; break;
    }

    /* Ignore if opposing pin is occupied */
    if (!PINOK(outPin) || !PINOK(outPin->gcr_linked))
	return;
    linkedPin = outPin->gcr_linked;
    outTile = glChanPinToTile(inPt->gl_tile, linkedPin);
    ASSERT(outTile != (Tile *) NULL, "glMazePropRiver");

    /* Cost to cross the channel */
    cost = inPt->gl_cost
	 + ABSDIFF(inPin->gcr_point.p_x, linkedPin->gcr_point.p_x)
	 + ABSDIFF(inPin->gcr_point.p_y, linkedPin->gcr_point.p_y)
	 + glChanPenalty;

    /* Avoid looping or revisiting points unnecessarily */
    if (glMazeShortest)
    {
	if (cost >= linkedPin->gcr_cost)
	    return;
	linkedPin->gcr_cost = outPin->gcr_cost = cost;
    }
    else if (glMazeCheckLoop(inPt, outTile))
	return;

    outPt = glPathNew(linkedPin, cost, inPt);
    outPt->gl_tile = outTile;

    /* Add in estimate of distance remaining to goal */
    cost += ABSDIFF(glMazeDestPoint.p_x, linkedPin->gcr_point.p_x)
	  + ABSDIFF(glMazeDestPoint.p_y, linkedPin->gcr_point.p_y);
    {
	HeapAddInt(&glMazeHeap, cost, (char *) outPt);
    }
    glCrossingsAdded++;

}

/*
 * ----------------------------------------------------------------------------
 *
 * glMazePropNormal --
 *
 * Process a normal top-of-heap point contained in a normal routing channel
 * segment that is NOT the same one as the destination.
 *
 * Algorithm:
 *	Visit all the channel tiles around inPt->gl_tile.
 *	Each tile corresponds to a segment of a channel in which
 *	there are regions of maximum density.  For each tile, pick
 *	the closest crossing point to inPt and add this point to the
 *	heap.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May add points to the heap.
 *
 * ----------------------------------------------------------------------------
 */

#define	NOTBLOCKEDH(tp)	(NOTBLOCKED(tp) && TiGetType(tp) != CHAN_VRIVER)
#define	NOTBLOCKEDV(tp)	(NOTBLOCKED(tp) && TiGetType(tp) != CHAN_HRIVER)

void
glMazePropNormal(inPt)
    GlPoint *inPt;
{
    Tile *tile = inPt->gl_tile, *tp;

    /* TOP */
    for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp = BL(tp))
	if (NOTBLOCKEDV(tp))
	    glMazeTile(inPt, tp, GEO_NORTH);

    /* LEFT */
    for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
	if (NOTBLOCKEDH(tp))
	    glMazeTile(inPt, tp, GEO_WEST);

    /* BOTTOM */
    for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp = TR(tp))
	if (NOTBLOCKEDV(tp))
	    glMazeTile(inPt, tp, GEO_SOUTH);

    /* RIGHT */
    for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp = LB(tp))
	if (NOTBLOCKEDH(tp))
	    glMazeTile(inPt, tp, GEO_EAST);
}

/*
 * ----------------------------------------------------------------------------
 *
 * glMazeTile --
 *
 * Propagate from inPt to some tile 'tile' that borders it.  The
 * caller should make sure it's possible to propagate in that direction
 * (it may not be if the channel covered by 'tile' is a river-routing
 * channel and we're entering it from the wrong side).
 *
 * Special handling: it's possible that tile covers the same channel
 * as inPt->gl_tile, in which case we just want to skip to the
 * opposite side of tile and consider all the tiles that abut it,
 * calling glMazeTile on each.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May add points to the heap.
 *
 * ----------------------------------------------------------------------------
 */

void
glMazeTile(inPt, tile, dir)
    GlPoint *inPt;		/* Top of heap point being expanded */
    Tile *tile;	/* Tile adjacent to inPt->gl_tile */
    int dir;			/* Direction from inPt->gl_tile to tile */
{
    GCRChannel *ch = (GCRChannel *) tile->ti_client;
    TileType type = TiGetType(tile);
    Tile *tp;

    ASSERT((int) ch != MINFINITY, "glMazeTile");

    /*
     * If this is a "real" channel boundary, pick a crossing point,
     * add it to the heap, and return.
     */
    if (inPt->gl_pin->gcr_ch != ch)
    {
	(void) glCrossEnum(inPt, tile, glMazeTileFunc, (ClientData) NULL);
	return;
    }

    /*
     * This isn't a real channel boundary, but only the border between
     * two tiles overlapping the same channel.  Visit the neighbors of
     * tile on the side opposite GeoOppositePos[dir] if tile is a river
     * routing tile, or the neighbors of tile on all sides except for
     * GeoOppositePos[dir] if tile is a normal tile.
     */
    switch (type)
    {
	case CHAN_HRIVER:
	    if (dir == GEO_EAST)
	    {
		/* RIGHT */
		for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp = LB(tp))
		    if (NOTBLOCKEDH(tp))
			glMazeTile(inPt, tp, GEO_EAST);
	    }
	    else
	    {
		/* LEFT */
		for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
		    if (NOTBLOCKEDH(tp))
			glMazeTile(inPt, tp, GEO_WEST);
	    }
	    break;
	case CHAN_VRIVER:
	    if (dir == GEO_NORTH)
	    {
		/* TOP */
		for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp = BL(tp))
		    if (NOTBLOCKEDV(tp))
			glMazeTile(inPt, tp, GEO_NORTH);
	    }
	    else
	    {
		/* BOTTOM */
		for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp = TR(tp))
		    if (NOTBLOCKEDV(tp))
			glMazeTile(inPt, tp, GEO_SOUTH);
	    }
	    break;
	case CHAN_NORMAL:
	    if (dir != GEO_SOUTH)
		for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp = BL(tp))
		    if (NOTBLOCKEDV(tp))
			glMazeTile(inPt, tp, GEO_NORTH);

	    if (dir != GEO_EAST)
		for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
		    if (NOTBLOCKEDH(tp))
			glMazeTile(inPt, tp, GEO_WEST);

	    if (dir != GEO_NORTH)
		for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp = TR(tp))
		    if (NOTBLOCKEDV(tp))
			glMazeTile(inPt, tp, GEO_SOUTH);

	    if (dir != GEO_WEST)
		for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp = LB(tp))
		    if (NOTBLOCKEDH(tp))
			glMazeTile(inPt, tp, GEO_EAST);
	    break;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * glMazeTileFunc --
 *
 * Called by glCrossEnum() on behalf of glMazeTile() above.
 * Add a new point to the heap for the path from 'inPt' to 'pin'.
 * The cost of the new point is inPt->gl_cost plus the distance
 * from inPt to pin.
 *
 * The caller should ensure on the call to glCrossEnum() that
 * tp overlaps a different channel than inPt.
 *
 * Results:
 *	Always returns 1.
 *
 * Side effects:
 *	Adds a new point to the heap.
 *
 * ----------------------------------------------------------------------------
 */

int
glMazeTileFunc(inPt, tp, pin)
    GlPoint *inPt;	/* Top of heap point being expanded */
    Tile *tp;		/* Tile adjacent to inPt->gl_tile */
    GCRPin *pin;	/* Available pin on boundary of tp */
{
    GlPoint *outPt;
    int cost;

    /* Sanity check */
    ASSERT(pin->gcr_ch != inPt->gl_pin->gcr_ch, "glMazeTileFunc");

    cost = inPt->gl_cost;
    cost += ABSDIFF(inPt->gl_pin->gcr_point.p_x, pin->gcr_point.p_x);
    cost += ABSDIFF(inPt->gl_pin->gcr_point.p_y, pin->gcr_point.p_y);
    cost += glChanPenalty;

    /* Avoid looping or revisiting points unnecessarily */
    if (glMazeShortest)
    {
	if (cost >= pin->gcr_cost)
	    return 1;
	pin->gcr_cost = cost;
	if (pin->gcr_linked)
	    pin->gcr_linked->gcr_cost = cost;
    }
    else if (glMazeCheckLoop(inPt, tp))
	return 1;

    outPt = glPathNew(pin, cost, inPt);
    outPt->gl_tile = tp;

    /* Add in estimate of distance to destination */
    cost += ABSDIFF(glMazeDestPoint.p_x, pin->gcr_point.p_x);
    cost += ABSDIFF(glMazeDestPoint.p_y, pin->gcr_point.p_y);

    /* Put this point on the heap */
    HeapAddInt(&glMazeHeap, cost, (char *) outPt);
    glCrossingsAdded++;

    return 1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glMazeCheckLoop --
 *
 * Determine if a path loops, i.e., if it already contains the the tile 'tp'.
 *
 * Results:
 *	TRUE if the path loops, FALSE if not.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
glMazeCheckLoop(path, tp)
    GlPoint *path;
    Tile *tp;
{
    for ( ; path; path = path->gl_path)
	if (path->gl_tile == tp)
	    return TRUE;

    return FALSE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glMazeResetCost --
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
glMazeResetCost(headPage, headFree)
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
	if (gpage == glPathCurPage)
	    break;
	headFree = 0;
    }
}

void
glPathPrint(path)
    GlPoint *path;
{
    GlPoint *rp;
    GCRPin *pin;
    GCRChannel *ch;
    Tile *tp;

    for (rp = path; rp; rp = rp->gl_path)
    {
	pin = rp->gl_pin;
	ch = pin->gcr_ch;
	tp = rp->gl_tile;
	TxPrintf("(%d,%d) cost=%d pcost=%d pId=%d/%d\n",
		pin->gcr_point.p_x, pin->gcr_point.p_y,
		rp->gl_cost, pin->gcr_cost,
		pin->gcr_pId, pin->gcr_pSeg);
	TxPrintf("\tchan=(%d,%d,%d,%d)/%d\n",
		ch->gcr_area.r_xbot, ch->gcr_area.r_ybot,
		ch->gcr_area.r_xtop, ch->gcr_area.r_ytop,
		ch->gcr_type);
	TxPrintf("\ttile=(%d,%d,%d,%d)/%d\n",
		LEFT(tp), BOTTOM(tp), RIGHT(tp), TOP(tp), TiGetType(tp));
    }
}
