/*
 * grouteCross.c -
 *
 * Global signal router.  Code to do crossing placement.
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
 *		      Lawrence Livermore National Laboratory
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/grouter/grouteCrss.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

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

/* Passed to glCrossChoose() */
GlPoint *glCrossLookAhead;

/* The following penalties get scaled by RtrGridSpacing */
int glJogPenalty = 5;
int glObsPenalty1 = 5, glObsPenalty2 = 3;
int glNbrPenalty1 = 2, glNbrPenalty2 = 5;
int glOrphanPenalty = 3;
int glChanPenalty = 1;
bool glPenaltiesScaled = FALSE;

/* Forward declarations */
GlPoint *glCrossAdjust();
int glCrossChoose();
void glCrossTakePin();


/*
 * ----------------------------------------------------------------------------
 *
 * glCrossMark --
 *
 * Mark all the pins crossed by the linked list of GlPoints pointed to
 * by 'path' as taken.  This list is linked along the gl_path pointers.
 * The starting entry 'path' indicates a pin reserved for a NLTermLoc;
 * the last entry is one of the pins on the previous starting-point list.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Marks the pins along the path as taken by 'net'.
 *	Assigns a segment identifier to each pin that is different
 *		for each pair of GlPoints in the path.
 *	Increments pNetId->netid_seg once for each new segment identifier
 *		created.
 *	Updates the density information in each channel as the pins are
 *		marked as taken.
 *
 * ----------------------------------------------------------------------------
 */

void
glCrossMark(rootUse, path, pNetId)
    CellUse *rootUse;	/* For error feedback if non-NULL */
    GlPoint *path;	/* Path linked via gl_path pointers */
    NetId *pNetId;	/* Net and segment identifier; netid_seg is updated */
{
    GCRPin *srcPin, *dstPin;
    GlPoint *rp;
    bool srcTaken;
    NetId markNetId;

    /*
     * Walk from path back along gl_path pointers down the list.
     * At each step, process the segment between srcPin and
     * dstPin in the channel srcPin->gcr_ch.
     */
    for (rp = path; rp->gl_path; rp = rp->gl_path)
    {
	/*
	 * Increment the segment id once for each channel the net passes
	 * through.  The intent is to make this net appear different to
	 * the channel router for each global segment processed.
	 */
	pNetId->netid_seg++;
	glCrossingsUsed++;

	/*
	 * Figure out which segment number to use:
	 * If srcPin has already been assigned a net, use its segment id
	 * to mark dstPin and don't do anything to srcPin; otherwise,
	 * use pNetId->netid_seg as the segment identifier and mark
	 * both pins as taken.
	 */
	markNetId = *pNetId;
	srcPin = rp->gl_path->gl_pin;
	srcTaken = srcPin->gcr_pId && srcPin->gcr_pSeg != GCR_STEMSEGID;
	if (srcTaken)
	    markNetId.netid_seg = srcPin->gcr_pSeg;

	/* Pick the dest pin in the same channel as srcPin */
	dstPin = rp->gl_pin;
	if (dstPin->gcr_ch != srcPin->gcr_ch) dstPin = dstPin->gcr_linked;
	ASSERT(dstPin&&dstPin->gcr_ch == srcPin->gcr_ch, "glCrossMark");

	/* Adjust the density -- must happen before pins are marked! */
	if (glDensAdjust(((GlobChan *) srcPin->gcr_ch->gcr_client)->gc_postDens,
			    srcPin, dstPin, markNetId))
	    glChanBlockDens(srcPin->gcr_ch);

	/* Mark pins as taken */
	if (!srcTaken)
	    glCrossTakePin(rootUse, srcPin, markNetId);
	glCrossTakePin(rootUse, dstPin, markNetId);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * glCrossTakePin --
 *
 * Reserve a channel's pin for the given net.
 * Make a number of sanity checks.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Marks the pin as being taken by the net, by setting
 *	gcr_pId and gcr_pSeg.
 *
 * ----------------------------------------------------------------------------
 */

void
glCrossTakePin(rootUse, pin, netid)
    CellUse *rootUse;	/* For error feedback if non-NULL */
    GCRPin *pin;	/* Pin to take */
    NetId netid;	/* Identifier to assign */
{
    char c[256], name1[1024], name2[1024];
    Rect r;

    if (DebugIsSet(glDebugID, glDebGreedy))
	return;

    if (DebugIsSet(glDebugID, glDebCross))
    {
	glShowCross(pin, netid, CROSS_PERM);
	TxMore("-- crossing --");
    }

    r.r_ll = r.r_ur = pin->gcr_point; r.r_xtop++; r.r_ytop++;
    ASSERT(netid.netid_net != (NLNet *) NULL, "glCrossTakePin");

    if (pin->gcr_pId == (GCRNet *) NULL
	|| (pin->gcr_pId == (GCRNet *) netid.netid_net
			&& pin->gcr_pSeg == GCR_STEMSEGID))
    {
	pin->gcr_pId = (GCRNet *) netid.netid_net;
	pin->gcr_pSeg = netid.netid_seg;

	/* Unlink from list along channel side */
	if (pin->gcr_pPrev && (pin->gcr_pPrev->gcr_pNext = pin->gcr_pNext))
	    pin->gcr_pNext->gcr_pPrev = pin->gcr_pPrev;
    }
    else if (pin->gcr_pId == (GCRNet *) netid.netid_net
	    && pin->gcr_pSeg == netid.netid_seg)
    {
	(void) sprintf(c, "Warning: crossing reassigned to same net/seg");
	if (rootUse)
	    DBWFeedbackAdd(&r, c, rootUse->cu_def, 1, STYLE_PALEHIGHLIGHTS);
	else TxError("%s\n", c);
    }
    else
    {
	/* Shouldn't happen: indicates a bug elsewhere */
	(void) strcpy(name1, NLNetName(pin->gcr_pId));
	(void) strcpy(name2, NLNetName(netid.netid_net));
	(void) sprintf(c, "Crossing multiply used, nets %s/%d, %s/%d",
	    name1, pin->gcr_pSeg, name2, netid.netid_seg);
	if (rootUse)
	    DBWFeedbackAdd(&r, c, rootUse->cu_def, 1, STYLE_PALEHIGHLIGHTS);
	else TxError("%s\n", c);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * glCrossUnreserve --
 *
 * Visit all of the pins used by the stems of the NLNet 'net' and reset
 * the gcr_pId and gcr_pSeg fields to show a NULL net and no segment id.
 * This procedure is called only just prior to the global routing for each
 * net; it keeps the crossings that might be used by this net reserved up
 * until the last possible minute, but makes sure that they don't stay
 * reserved once we actually know the routing for this net (since otherwise
 * the channel router would try to connect them up!).
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
glCrossUnreserve(net)
    NLNet *net;
{
    NLTermLoc *loc;
    NLTerm *term;
    GCRPin *pin;
 
    /* De-reserve the pins taken by each terminal location in this net */
    for (term = net->nnet_terms; term; term = term->nterm_next)
        for (loc = term->nterm_locs; loc; loc = loc->nloc_next)
        {
            pin = loc->nloc_pin;
	    ASSERT(pin->gcr_pSeg == GCR_STEMSEGID, "glCrossUnreserve");
	    pin->gcr_pId = (GCRNet *) NULL;
	    pin->gcr_pSeg = 0;
        }
}

/*
 * ----------------------------------------------------------------------------
 *
 * glCrossEnum --
 *
 * Enumerate the crossing points between the tile inPt->gl_tile
 * and 'tp'.  The two tiles should overlap different channels.
 *
 * For each available crossing (i.e., a pin that is yet unassigned
 * and has a linked pin), call the procedure (*func)(), which should
 * be of the following form:
 *
 *	int
 *	(*func)(inPt, tp, pin, cdata)
 *	    GlPoint *inPt;	/# Same as inPt passed to glCrossEnum #/
 *	    Tile *tp;		/# Same as tp passed to glCrossEnum #/
 *	    GCRPin *pin;	/# A free pin in 'tp' #/
 *	    ClientData cdata;	/# Same as cdata passed to glCrossEnum #/
 *	{
 *	}
 *
 * If (*func)() returns 1, glCrossEnum() immediately stops visiting
 * further crossing points and returns 1 itself; otherwise, we keep
 * going until we run out of crossing points to visit.
 *
 * Results:
 *	Returns 1 if (*func)() returned 1; otherwise, returns 0.
 *
 * Side effects:
 *	Whatever (*func)() does.
 *
 * ----------------------------------------------------------------------------
 */

int
glCrossEnum(inPt, tp, func, cdata)
    GlPoint *inPt;	/* Top of heap point being expanded */
    Tile *tp;			/* Tile adjacent to inPt->gl_tile */
    int (*func)();		/* Called for each crossing */
    ClientData cdata;		/* Passed to (*func)() */
{
    int outSide, origin, lo, hi, max, start, n, nhi;
    GCRChannel *ch = inPt->gl_pin->gcr_ch;
    Tile *inTile = inPt->gl_tile;
    GCRPin *pins, *pin;

    /* Sanity checks: callers should ensure that these are true */
    ASSERT(tp->ti_client != (ClientData) CLIENTDEFAULT, "glCrossEnum");
    ASSERT((GCRChannel *) tp->ti_client != ch, "glCrossEnum");

    /*
     * Find out the direction from inTile to tp.
     * We assume that the two tiles share a side, so this
     * is pretty straightforward.
     */
    if (LEFT(inTile) == RIGHT(tp)) outSide = GEO_WEST;
    else if (RIGHT(inTile) == LEFT(tp)) outSide = GEO_EAST;
    else if (TOP(inTile) == BOTTOM(tp)) outSide = GEO_NORTH;
    else if (BOTTOM(inTile) == TOP(tp)) outSide = GEO_SOUTH;

    /*
     * Find the range of points shared between the two tiles.
     * Note that we look at the TILES, not the CHANNELS, since
     * there can be many tiles for a single channel.
     */
    if (outSide == GEO_NORTH || outSide == GEO_SOUTH)
    {
	lo = MAX(LEFT(inTile), LEFT(tp));
	hi = MIN(RIGHT(inTile), RIGHT(tp));
	origin = ch->gcr_origin.p_x;
	max = ch->gcr_length;
    }
    else
    {
	lo = MAX(BOTTOM(inTile), BOTTOM(tp));
	hi = MIN(TOP(inTile), TOP(tp));
	origin = ch->gcr_origin.p_y;
	max = ch->gcr_width;
    }

    /*
     * Now find the range of pins shared between the two tiles.
     * We're careful about which pins are actually shared: we
     * round the lower coordinate up unless it's grid-aligned,
     * but round the higher coordinate down even if it is
     * grid-aligned.  I.e.,
     *
     *	lo' = pin with least coordinate >= lo
     *	hi' = pin with greatest coordinate < hi
     * 
     * The range of pins shared should be non-NULL; if it's not,
     * something is wrong (most likely the channel is too small).
     * The pins range from lo to hi, inclusive, and should not
     * include pin[0] or pin[length+1] or pin[width+1] (the latter
     * respectively for vertical or horizontal edges between tiles).
     */
    lo = (lo + RtrGridSpacing - 1 - origin) / RtrGridSpacing;
    hi = (hi - origin - 1) / RtrGridSpacing;
    ASSERT(lo >= 1 && lo <= max, "glCrossEnum");
    if (lo > hi)
	return 0;

    switch (outSide)
    {
	case GEO_NORTH:	pins = ch->gcr_tPins; break;
	case GEO_SOUTH:	pins = ch->gcr_bPins; break;
	case GEO_EAST:	pins = ch->gcr_rPins; break;
	case GEO_WEST:	pins = ch->gcr_lPins; break;
    }

    /*
     * Now comes the fun part: enumerating pins in the range
     * from lo .. hi inclusive in such a way that we visit
     * the CLOSEST pin to inPt FIRST.  Choose start so that
     * it's the index of the closest pin on the exit side to
     * inPt, and then worry about whether or not it's in the
     * range of pins shared with the next tile.
     */
    start = (outSide == GEO_NORTH || outSide == GEO_SOUTH)
		? inPt->gl_pin->gcr_x
		: inPt->gl_pin->gcr_y;

    /* CASE 1: visit from bottom to top (or left to right) */
    if (start <= lo)
    {
	for (n = lo; n <= hi; n++, glCrossingsSeen++)
	{
	    pin = &pins[n];
	    if (PINOK(pin) && PINOK(pin->gcr_linked)
		    && (*func)(inPt, tp, pin->gcr_linked, cdata))
		return 1;
	}
	return 0;
    }

    /* CASE 1: visit from top to bottom (or right to left) */
    if (start >= hi)
    {
	for (n = hi; n >= lo; n--, glCrossingsSeen++)
	{
	    pin = &pins[n];
	    if (PINOK(pin) && PINOK(pin->gcr_linked)
		    && (*func)(inPt, tp, pin->gcr_linked, cdata))
		return 1;
	}
	return 0;
    }

    /*
     * CASE 3:
     * Have to consider candidates alternating from
     * left to right (or top to bottom) to find the
     * closest available pin.  Start at the center.
     */
    for (n = start, nhi = start+1; n >= lo || nhi <= hi; n--, nhi++)
    {
	if (n >= lo)
	{
	    glCrossingsSeen++;
	    pin = &pins[n];
	    if (PINOK(pin) && PINOK(pin->gcr_linked)
		    && (*func)(inPt, tp, pin->gcr_linked, cdata))
		return 1;
	}
	if (nhi <= hi)
	{
	    glCrossingsSeen++;
	    pin = &pins[nhi];
	    if (PINOK(pin) && PINOK(pin->gcr_linked)
		    && (*func)(inPt, tp, pin->gcr_linked, cdata))
		return 1;
	}
    }

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glCrossAdjust --
 *
 * Given a path of crossing points, wiggle them around in each channel
 * to try to reduce the total penalty on the path.  Construct a new
 * path of crossing points that uses the points we select in this way,
 * and whose cost fields (gl_cost) include both distance and the
 * penalties.  Be careful when wiggling crossings through a river
 * routing channel: they all have to wiggle at the same time.
 *
 * Algorithm:
 *	Two crossing points in the input path are already fixed:
 *	path itself, and the crossing point at the very end (the
 *	one for which gl_path is NULL).  Starting from the crossing
 *	point at the end and working back toward 'path', assign
 *	crossings to all points in the middle.  The procedure to
 *	do this is recursive, considering at each stage three
 *	crossing points: one that has already been assigned
 *	(which is either the last point in the path, or the result
 *	of the previous recursive call), one that is to be assigned
 *	(the current point along the path), and a "lookahead" point
 *	(the one that will be next assigned, so we can consider
 *	it as well when positining the current point.
 *
 * Results:
 *	Returns a pointer to the final point in a new path of crossings,
 *	linked via their gl_path pointers.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

GlPoint *
glCrossAdjust(lookAhead, path)
    GlPoint *lookAhead;	/* Normally, lookAhead->gl_path == path, except on the
			 * initial call, in which case lookAhead == NULL.
			 */
    GlPoint *path;	/* Adjust crossings along this path */
{
    GlPoint *newPath, *newRest;
    GCRPin *linkedPin, *pin;
    GCRChannel *ch;

    /*
     * The location of the last crossing along the path is fixed,
     * since it's the "starting point".  Simply return when we
     * reach this point, since there's no penalty to be computed
     * yet.
     */
    ASSERT(path != (GlPoint *) NULL, "glCrossAdjust");
    if (path->gl_path == (GlPoint *) NULL)
	return path;

    /*
     * Basic recursive step: assign crossings to everything
     * in the remainder of the path, then cons a new GlPoint
     * to the front of the path and adjust its position
     * below.  The initial cost of newPath will be that
     * to the pin it initially occupies, before being
     * wiggled by glCrossChoose() later.  This gives
     * us an upper bound on an acceptable cost.
     */
    newRest = glCrossAdjust(path, path->gl_path);
    newPath = glPathNew(path->gl_pin, 0, newRest);
    newPath->gl_cost = newRest->gl_cost + glCrossCost(lookAhead, path, newRest);
    newPath->gl_tile = path->gl_tile;

    /*
     * If this was the first crossing in the path, it's also
     * immutable and so we just cons it to the path and return.
     * We still use a new GlPoint, though, since the cost of
     * this point has to include the penalties along the path.
     */
    if (lookAhead == (GlPoint *) NULL)
	return newPath;

    if (TiGetType(newRest->gl_tile) != CHAN_NORMAL)
    {
	/*
	 * If newRest was through a river-routing channel, it fixes
	 * the pin on the exit side.
	 */
	pin = newRest->gl_pin;
	ch = pin->gcr_ch;
	switch (pin->gcr_side)
	{
	    case GEO_NORTH:	linkedPin = &ch->gcr_bPins[pin->gcr_x]; break;
	    case GEO_EAST:	linkedPin = &ch->gcr_lPins[pin->gcr_y]; break;
	    case GEO_SOUTH:	linkedPin = &ch->gcr_tPins[pin->gcr_x]; break;
	    case GEO_WEST:	linkedPin = &ch->gcr_rPins[pin->gcr_y]; break;
	}
	ASSERT(PINOK(linkedPin), "glCrossAdjust");
	newPath->gl_pin = linkedPin->gcr_linked;
	newPath->gl_cost = newRest->gl_cost;
	newPath->gl_cost += glCrossCost(lookAhead, newPath, newRest);
    }
    else
    {
	/*
	 * Time to choose a crossing for 'path'.
	 * It has to lie somewhere along the boundary between path->gl_tile
	 * and newRest->gl_tile.  (These two tiles will be different unless
	 * path is the destination point, but we checked for that above when
	 * we looked for lookAhead being NULL).  Enumerate the crossings
	 * along this boundary looking for the best one.
	 */
	glCrossLookAhead = lookAhead;
	(void) glCrossEnum(newRest, path->gl_tile, glCrossChoose,
		    (ClientData) newPath);
    }

    return newPath;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glCrossChoose --
 *
 * Called by glCrossEnum() on behalf of glCrossAdjust() above.
 * Evaluate 'pin' as a possible crossing by computing the penalty of
 * a segment from newRest through newPath (setting newPath->gl_pin to
 * pin), and on through to glCrossLookAhead (if it's non-NULL).
 *
 * Results:
 *	Returns 0 normally, or 1 to tell glCrossEnum() that it
 *	can stop looking at further crossings.
 *
 * Side effects:
 *	May modify newPath->gl_cost and newPath->gl_pin.
 *
 * ----------------------------------------------------------------------------
 */

    /*ARGSUSED*/
int
glCrossChoose(newRest, tp, pin, newPath)
    GlPoint *newRest;	/* Portion of path already assigned */
    Tile *tp;		/* UNUSED */
    GCRPin *pin;	/* Pin on boundary of tp being considered */
    GlPoint *newPath;		/* Update newPath->gl_pin, newPath->gl_cost */
{
    GCRPin *savePin;
    int cost;

    /*
     * We've been visiting crossings in order of increasing distance
     * from newRest.  If the distance to 'pin' plus newRest->gl_cost
     * already exceeds the best cost we've been able to find for
     * newPath, then we can't gain any more improvement by considering
     * any more crossing point, so we stop.
     */
    cost = ABSDIFF(pin->gcr_point.p_x, newRest->gl_pin->gcr_point.p_x)
	 + ABSDIFF(pin->gcr_point.p_y, newRest->gl_pin->gcr_point.p_y)
	 + newRest->gl_cost;
    if (cost >= newPath->gl_cost)
	return 1;

    /*
     * Evaluate the cost of using 'pin' as the crossing point.
     * If it's a better choice than what's currently stored in newPath,
     * then use it and update the cost in newPath.
     */
    savePin = newPath->gl_pin;
    newPath->gl_pin = pin;
    cost = newRest->gl_cost + glCrossCost(glCrossLookAhead, newPath, newRest);
    if (cost < newPath->gl_cost)
	newPath->gl_cost = cost;
    else
	newPath->gl_pin = savePin;

    /* Keep looking at more crossing points */
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glCrossCost --
 *
 * Evaluate the cost of the segment from 'entryPt' to 'exitPt' (and on through
 * 'lookAhead' if it is non-NULL).
 *
 * Results:
 *	Returns the sum of the distance from rest to path and the
 *	penalties associated with this segment.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
glCrossCost(lookAhead, exitPt, entryPt)
    GlPoint *lookAhead;
    GlPoint *exitPt;
    GlPoint *entryPt;
{
    GCRPin *entryPin, *exitPin, *otherPin;
    GCRPin *opposite;
    int count, cost;

    /*
     * Make sure that both entryPin and exitPin are in the same
     * channel, and otherPin is in the next channel.
     */
    entryPin = entryPt->gl_pin;
    exitPin = exitPt->gl_pin;
    if (exitPin->gcr_ch != entryPin->gcr_ch)
	exitPin = exitPin->gcr_linked;
    otherPin = exitPin->gcr_linked;
    ASSERT(exitPin != NULL, "glCrossCost");

    /* Distance cost */
    cost = ABSDIFF(entryPin->gcr_point.p_x, exitPin->gcr_point.p_x)
	 + ABSDIFF(entryPin->gcr_point.p_y, exitPin->gcr_point.p_y);

    /*
     * If exitPt is in a river-routing tile, and we have to continue
     * across the channel (lookAhead is non-NULL), then make sure the pin
     * on the opposite side is free; otherwise, exitPt has "infinite"
     * penalty since it's unusable.
     */
    if (lookAhead && TiGetType(exitPt->gl_tile) != CHAN_NORMAL)
    {
	switch (otherPin->gcr_side)
	{
	    case GEO_NORTH:
		opposite = &otherPin->gcr_ch->gcr_bPins[otherPin->gcr_x];
		break;
	    case GEO_SOUTH:
		opposite = &otherPin->gcr_ch->gcr_tPins[otherPin->gcr_x];
		break;
	    case GEO_EAST:
		opposite = &otherPin->gcr_ch->gcr_lPins[otherPin->gcr_y];
		break;
	    case GEO_WEST:
		opposite = &otherPin->gcr_ch->gcr_rPins[otherPin->gcr_y];
		break;
	}
	if (!PINOK(opposite))
	    return INFINITY;
    }

    /* Penalty for using lots of channels */
    cost += glChanPenalty;

    /* Penalize if the net doesn't run straight across the channel */
    if (entryPin->gcr_x != exitPin->gcr_x && entryPin->gcr_y != exitPin->gcr_y)
	cost += glJogPenalty;

    /*
     * If there is an obstacle or hazard over a crossing, or an
     * obstacle somewhere along the track or column of the pin,
     * then assess a penalty.  Look on both sides of the crossing
     * to get this penalty.
     */
#define	BADCROSSFLAGS	(GCROBST|GCRHAZRD|GCRTCC)
    if (otherPin && otherPin->gcr_ch->gcr_type == CHAN_NORMAL)
    {
	if ((otherPin->gcr_pFlags & BADCROSSFLAGS) || otherPin->gcr_pSize != 0)
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
     * Done if this channel is used for river-routing (in which
     * case the subsequent penalty computation is not needed).
     */
    if (entryPin->gcr_ch->gcr_type != CHAN_NORMAL)
	return cost;

    if ((exitPin->gcr_pFlags & BADCROSSFLAGS) || exitPin->gcr_pSize != 0)
    {
	ASSERT(exitPin->gcr_pSize >= 0, "glCrossPenalty");
	cost += glObsPenalty1;
	if (exitPin->gcr_pFlags & GCROBST)
	    cost += glObsPenalty2 * exitPin->gcr_pSize;
	else if (exitPin->gcr_pFlags & GCRHAZRD)
	    cost += MAX(glObsPenalty2 * exitPin->gcr_pSize
				- exitPin->gcr_pDist, 0);
    }

    /* Penalty for "congestion" (neighboring pins in use) */
    count = 0;
    if ((exitPin + 1)->gcr_pId) count++;
    if ((exitPin - 1)->gcr_pId) count++;
    if (count == 2) cost += glNbrPenalty2;
    else if (count == 1) cost += glNbrPenalty1;

    /*
     * If the path turns in the channel and the exit crossing
     * isn't a paired orphan, assess a penalty.
     */
    if (exitPin->gcr_side != GeoOppositePos[entryPin->gcr_side])
    {
	switch (exitPin->gcr_side)
	{
	    case GEO_NORTH:
		otherPin = &entryPin->gcr_ch->gcr_bPins[exitPin->gcr_x];
		break;
	    case GEO_SOUTH:
		otherPin = &entryPin->gcr_ch->gcr_tPins[exitPin->gcr_x];
		break;
	    case GEO_EAST:
		otherPin = &entryPin->gcr_ch->gcr_lPins[exitPin->gcr_y];
		break;
	    case GEO_WEST:
		otherPin = &entryPin->gcr_ch->gcr_rPins[exitPin->gcr_y];
		break;
	}
	if (otherPin->gcr_pId == (GCRNet *) NULL)
	    cost += glOrphanPenalty;
    }

    return cost;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glCrossScalePenalties --
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
glCrossScalePenalties()
{
    if (glPenaltiesScaled)
	return;

    glPenaltiesScaled = TRUE;
    glJogPenalty *= RtrGridSpacing;
    glObsPenalty1 *= RtrGridSpacing;
    glObsPenalty2 *= RtrGridSpacing;
    glNbrPenalty1 *= RtrGridSpacing;
    glNbrPenalty2 *= RtrGridSpacing;
    glOrphanPenalty *= RtrGridSpacing;
    glChanPenalty *= RtrGridSpacing;
}
