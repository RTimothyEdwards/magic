/*
 * grouteMulti.c
 *
 * Route a multi-terminal net.
 * Currently includes just a single algorithm, for routing a net
 * where arbitrary branching is allowed.  This algorithm produces
 * something like a steiner-tree, with intermediate points introduced
 * at channel boundaries.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/grouter/grouteMult.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif	/* lint */

#include <stdio.h>
#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/hash.h"
#include "utils/heap.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "router/router.h"
#include "gcr/gcr.h"
#include "grouter/grouter.h"
#include "utils/netlist.h"
#include "utils/signals.h"
#include "textio/textio.h"
#include "utils/malloc.h"
#include "utils/styles.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"

/* Forward declarations */

void glMultiAddStart();


/*
 * ----------------------------------------------------------------------------
 *
 * glMultiSteiner --
 *
 * Perform global routing for all segments of a net.
 * The caller supplies two procedures: one to produce a list of
 * GlPoints from any of a set of possible starting points to the
 * single destination, and the other to accept a list of GlPoints
 * and remember it permanently, e.g, in the form of crossing assignments.
 *
 * The two procedures are of the following form:
 *
 *	GlPoint *
 *	(*routeProc)(startList, loc, bestCost, cdRoute)
 *	    GlPoint *startList;	--- list of GlPoints that are possible
 *				--- starting points for the route; the
 *				--- points in the list are linked via gl_path
 *				--- fields as though they were a path.
 *	    NLTermLoc *loc;	--- loc->nloc_pin is the destination
 *	    int bestCost;	--- return NULL if we can't beat this cost
 *	    ClientData cdRoute;	--- same as cdRoute passed to us
 *	{
 *	}
 *
 *	int
 *	(*markProc)(rootUse, path, pNetId, cdMark)
 *	    CellUse *rootUse;	--- leave feedback here if necessary and if
 *				--- rootUse is non-NULL
 *	    GlPoint *path;	--- path to be marked
 *	    NetId *pNetId;	--- netid_net is the argument 'net' to
 *				--- glMultiSteiner; netid_seg will be
 *				--- incremented for each new segment id
 *				--- assigned.
 *	    ClientData cdMark;	--- same as 'cdMark' passed to us
 *	{
 *	}
 *
 *
 * Assumptions:
 *	The net has at least two terminals, each of which has at least
 *	one valid location.
 *
 * Algorithm:
 *	Multiterminal nets are routed using an algorithm that finds
 *	pseudo-Steiner tree routes.  The idea is to process the
 *	terminals of the net in the order they appear in the netlist.
 *	Processing consists of finding the shortest path from the
 *	terminal being considered to all of the terminals processed
 *	before it, or to any of the crossing points used by the routes
 *	used to connect these previously-processed terminals.
 *
 * Results:
 *	Returns the number of terminals processed.
 *
 * Side effects:
 *	Whatever (*routeProc)() and (*markProc)() do.
 *
 * ----------------------------------------------------------------------------
 */

int
glMultiSteiner(rootUse, net, routeProc, markProc, cdRoute, cdMark)
    CellUse *rootUse;		/* If non-NULL, feedback for errors left here */
    NLNet *net;		/* Net to process */
    GlPoint *(*routeProc)();	/* Procedure to route a segment */
    int (*markProc)();		/* Procedure to remember the route */
    ClientData cdRoute;		/* Passed to (*routeProc)() */
    ClientData cdMark;		/* Passed to (*markProc)() */
{
    GlPoint *startList, *bestDest, *dest;
    char mesg[128], *lastTermName;
    int bestCost, nterms;
    NLTermLoc *loc;
    NLTerm *term;
    Rect errorArea;
    NetId netid;

    /* Skip to the first term that has a location */
    ASSERT(net != (NLNet *) NULL, "glMultiSteiner");
    for (term = net->nnet_terms; term; term = term->nterm_next)
	if (term->nterm_locs)
	    break;
    ASSERT(term != (NLTerm *) NULL, "glMultiSteiner");

    /*
     * For the first terminal in the net, mark the point where the terminal
     * enters its adjacent channel.  If there are several electrically
     * equivalent terminals, then mark them all.
     */
    nterms = 0;
    startList = (GlPoint *) NULL;
    lastTermName = term->nterm_name;
    for (loc = term->nterm_locs; loc; loc = loc->nloc_next)
	glListAdd(&startList, loc->nloc_pin, glMultiStemCost(loc));

    /* Process all other terminals in net */
    netid.netid_net = net;
    netid.netid_seg = 1;
    for (term = term->nterm_next; term; term = term->nterm_next)
    {
	/*
	 * Skip if no valid locations exist for this terminal;
	 * the error has already been reported (either in the
	 * stem generator or the netlist reader).
	 */
	if (term->nterm_locs == (NLTermLoc *) NULL)
	    continue;

	/*
	 * Consider routing to each of the possible locations for
	 * this terminal, and use the best path.  (The comparison of
	 * route cost includes the final channel in each path).
	 * After each call to rgRoutePath, 'dest' will be a GlPoint
	 * for one of the zero-cost points to 'loc' (or NULL if no
	 * path could be found).
	 */ 
	bestCost = INFINITY;
	bestDest = (GlPoint *) NULL;
	for (loc = term->nterm_locs; loc; loc = loc->nloc_next)
	{
	    nterms++;

	    /* Try to find a path from a zero-cost point to loc */
	    dest = (*routeProc)(startList, loc, bestCost, cdRoute);

	    /* Remember it if it was better than the previous best */
	    if (dest && dest->gl_cost < bestCost)
	    {
		if (bestDest) glPathFreePerm(bestDest);
		bestDest = glPathCopyPerm(dest);
		bestCost = dest->gl_cost;
	    }

	    /* Free all temporary storage used for GlPoints */
	    glPathFreeTemp();
	}

	/*
	 * If we were successful in finding a path, add the crossing points
	 * to the zero-cost list, mark all the crossings it used as allocated,
	 * and update the segment-id.
	 */
	if (bestDest)
	{
	    glMultiAddStart(bestDest, &startList);
	    (*markProc)(rootUse, bestDest, &netid, cdMark);
	    glPathFreePerm(bestDest);

	    /*
	     * Finally, move all of the locations for the terminal just
	     * processed to the zero-cost list, since any of them can
	     * be used as a new starting point.
	     */
	    for (loc = term->nterm_locs; loc; loc = loc->nloc_next)
		glListAdd(&startList, loc->nloc_pin, glMultiStemCost(loc));
	    lastTermName = term->nterm_name;
	}
	else
	{
	    GEO_EXPAND(&term->nterm_locs->nloc_rect, 1, &errorArea);
	    sprintf(mesg, "Can't find a path from \"%s\" to \"%s\"",
		term->nterm_name, lastTermName);
	    if (rootUse)
		DBWFeedbackAdd(&errorArea, mesg, rootUse->cu_def,
		    1, STYLE_PALEHIGHLIGHTS);
	    else TxError("%s\n", mesg);
	}
    }

    /* Free the list of starting points */
    glPathFreePerm(startList);
    return nterms;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glMultiStemCost --
 *
 * Compute the initial cost of a terminal.  This is the cost from the
 * terminal loc->nloc_rect to its initial crossing point loc->nloc_stem.
 *
 * Results:
 *	Returns the Manhattan distance just described.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
glMultiStemCost(loc)
    NLTermLoc *loc;
{
    int n1, n2, cost;

    n1 = ABSDIFF(loc->nloc_stem.p_x, loc->nloc_rect.r_xbot);
    n2 = ABSDIFF(loc->nloc_stem.p_x, loc->nloc_rect.r_xtop);
    cost = MIN(n1, n2);
    n1 = ABSDIFF(loc->nloc_stem.p_y, loc->nloc_rect.r_ybot);
    n2 = ABSDIFF(loc->nloc_stem.p_y, loc->nloc_rect.r_ytop);
    cost += MIN(n1, n2);

    return cost;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glMultiAddStart --
 *
 * Add all the pins along the GlPoint 'path' to the list of
 * starting points '*pStartList'.  For each crossing we add
 * up to two pins: one on each side of the crossing.
 * If a pin has already been marked as belonging to
 * a net, we don't add it, since it was already added
 * in an earlier iteration.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Prepends the GlPoints on the list 'path' to the list of
 *	starting points *pStart.
 *
 * ----------------------------------------------------------------------------
 */

void
glMultiAddStart(path, pStartList)
    GlPoint *path;		/* Path linked via gl_path pointers */
    GlPoint **pStartList;	/* List of starting points */
{
    GlPoint *srcEntry, *dstEntry;
    GCRPin *srcPin, *dstPin;

    /*
     * Walk from path back along gl_path pointers down the list.
     * At each step, process the segment between srcEntry and
     * dstEntry in the channel srcEntry->gl_pin->gcr_ch.
     */
    for (srcEntry = path->gl_path, dstEntry = path;
	    srcEntry;
	    dstEntry = srcEntry, srcEntry = srcEntry->gl_path)
    {
	/* Use srcPin's channel for both srcPin and dstPin */
	srcPin = srcEntry->gl_pin;
	dstPin = dstEntry->gl_pin;
	if (dstPin->gcr_ch != srcPin->gcr_ch) dstPin = dstPin->gcr_linked;
	ASSERT(dstPin && dstPin->gcr_ch == srcPin->gcr_ch, "glMultiAddStart");

	/* Add to list of starting points */
	if (srcPin->gcr_pId == NULL || srcPin->gcr_pSeg == GCR_STEMSEGID)
	    glListAdd(pStartList, srcPin, 0);
	glListAdd(pStartList, dstPin, 0);
    }
}
