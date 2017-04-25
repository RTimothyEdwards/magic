/*
 * grouteMain.c --
 *
 * Top level code for the global signal router.
 *
 * The global router's job is to find the sequence of channel pins
 * through which each signal must pass, and mark these pins so the
 * channel router can connect them within each channel.
 *
 * Our overall approach is greedy: we compute the area of the bounding
 * box of all terminals in a net for each net, and perform global routing
 * of each net in order of increasing area of this bounding box.  This
 * has the effect of routing more constrained nets first, and less
 * constrained ones later.
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

#include <stdio.h>
#include <sys/types.h>
#include <sys/times.h>
#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/styles.h"
#include "utils/hash.h"
#include "utils/heap.h"
#include "utils/utils.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "debug/debug.h"
#include "gcr/gcr.h"
#include "windows/windows.h"
#include "utils/main.h"
#include "dbwind/dbwind.h"
#include "utils/signals.h"
#include "router/router.h"
#include "grouter/grouter.h"
#include "utils/netlist.h"
#include "utils/styles.h"
#include "textio/textio.h"
#include "utils/malloc.h"

/* Global data */
Heap glMazeHeap;	/* Heap of search points for global routing */
FILE *glLogFile;	/* Used for debugging to remember crossings processed */
int glNumTries;		/* Debugging too -- # calls to glProcessLoc() */

/* Forward declarations */
void glClientInit();
void glClientFree();


/*
 * ----------------------------------------------------------------------------
 *
 * GlGlobalRoute --
 *
 * Build a heap of nets, ordered with smallest area first.
 * Globally route nets on the heap, decomposing multi-pin nets using
 * Steiner-like trees.  Leave routing problems in channel structures.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	On completion crossing points for nets have been set.  Channel
 *	structures are ready to be routed by the channel structure.
 *
 * ----------------------------------------------------------------------------
 */

void
GlGlobalRoute(chanList, netList)
    GCRChannel *chanList;	/* List of all channels in routing problem */
    NLNetList *netList;		/* Netlist built by caller */
{
    HeapEntry entry;
    Heap netHeap;
    bool doFast;
    int numTerms;
    NLNet *net;

    GlInit();
    glStatsInit();
    doFast = DebugIsSet(glDebugID, glDebFast);

    /*
     * Initialize the client-specific portion of each channel and
     * of each net.  These fields point to structures holding
     * (respectively) density information and blocked regions
     * during global routing.
     */
    glClientInit(chanList, netList);

    /*
     * Build a tile plane that represents all the channels in the
     * routing problem.  This tile plane will be used to search
     * for nearby channels during global routing.
     */
    glChanBuildMap(chanList);
    if (DebugIsSet(glDebugID, glDebChan))
    {
	SigInterruptPending = TRUE;
	return;
    }

    /* Compute penalties for passing through congested zones */
    if (DebugIsSet(glDebugID, glDebPen))
	glPenCompute(chanList, netList);

    /*
     * Build a heap of nets sorted in order of increasing size, then
     * successively remove the topmost entry of the heap and route its
     * net.  Make almost-Steiner tree global routes for multi-pin nets.
     */
    numTerms = 0;
    NLSort(netList, &netHeap);
    while (HeapRemoveTop(&netHeap, &entry) && !SigInterruptPending)
    {
	net = (NLNet *) entry.he_id;
	if (DebugIsSet(glDebugID, glDebPen))
	{
	    glCrossUnreserve(net);
	    glPenSetPerChan(net);
	}
	numTerms += glMultiSteiner(EditCellUse, net, glProcessLoc,
			glCrossMark, (ClientData) doFast, (ClientData) 0);
	if (DebugIsSet(glDebugID, glDebPen))
	    glPenClearPerChan(net);
	RtrMilestonePrint();
    }
    HeapKill(&netHeap, (void (*)()) NULL);

    glClientFree(chanList, netList);
    glChanFreeMap();
    glStatsDone(netList->nnl_numNets, numTerms);
}

/*
 * ----------------------------------------------------------------------------
 *
 * glClientInit --
 *
 * Allocate and initialize the structures that go in the gcr_client and
 * nnet_cdata fields of all the channels and nets respectively in chanList
 * and netList, and leave these fields pointing to the newly allocated and
 * initialized structures.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates memory; see above.
 *
 * ----------------------------------------------------------------------------
 */

void
glClientInit(chanList, netList)
    GCRChannel *chanList;
    NLNetList *netList;
{
    GCRChannel *ch;
    GlobChan *gc;
    NLNet *net;
    int nrow, ncol;

    for (ch = chanList; ch; ch = ch->gcr_next)
    {
	gc = (GlobChan *) mallocMagic((unsigned) (sizeof (GlobChan)));
	gc->gc_penList = (CZone *) NULL;
	nrow = ch->gcr_width;
	ncol = ch->gcr_length;
	glDMAlloc(&gc->gc_prevDens[CZ_COL], ncol, nrow);
	glDMAlloc(&gc->gc_prevDens[CZ_ROW], nrow, ncol);
	glDMAlloc(&gc->gc_postDens[CZ_COL], ncol, nrow);
	glDMAlloc(&gc->gc_postDens[CZ_ROW], nrow, ncol);
	glDensInit(gc->gc_prevDens, ch);
	glDMCopy(&gc->gc_prevDens[CZ_COL], &gc->gc_postDens[CZ_COL]);
	glDMCopy(&gc->gc_prevDens[CZ_ROW], &gc->gc_postDens[CZ_ROW]);
	ch->gcr_client = (ClientData) gc;
    }

    for (net = netList->nnl_nets; net; net = net->nnet_next)
	net->nnet_cdata = (ClientData) callocMagic((unsigned) (sizeof (NetClient)));
}

/*
 * ----------------------------------------------------------------------------
 *
 * glClientFree --
 *
 * Free the memory allocated by glClientInit() above (as well as any
 * memory allocated to CZone lists pointed to by the NetClient structs
 * in the NLNetList netList).
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
glClientFree(chanList, netList)
    GCRChannel *chanList;
    NLNetList *netList;
{
    GlobChan *gc;
    CZone *cz;
    NetClient *nclient;
    GCRChannel *ch;
    NLNet *net;

    for (ch = chanList; ch; ch = ch->gcr_next)
    {
	gc = (GlobChan *) ch->gcr_client;
	glDMFree(&gc->gc_prevDens[CZ_COL]);
	glDMFree(&gc->gc_prevDens[CZ_ROW]);
	glDMFree(&gc->gc_postDens[CZ_COL]);
	glDMFree(&gc->gc_postDens[CZ_ROW]);
	freeMagic((char *) gc);
	ch->gcr_client = (ClientData) NULL;
    }

    for (net = netList->nnl_nets; net; net = net->nnet_next)
    {
	nclient = (NetClient *) net->nnet_cdata;
	for (cz = nclient->nc_pens; cz; cz = cz->cz_next)
	    freeMagic((char *) cz);
	net->nnet_cdata = (ClientData) NULL;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * glProcessLoc --
 *
 * Function called for all but the first NLTermLoc in a net: finds and
 * returns the best-cost path from any of the points in the starting
 * point list to loc->nloc_stem.
 *
 * Algorithm:
 *	We use two passes.  The first attempts to find the shortest
 *	distance path between the source and the destination, if
 *	such a path exists.  It uses a mechanism for storing the
 *	best cost so far to each crossing point considered, so
 *	we avoid looping.  If no path can be found, we simply
 *	give up; otherwise, we perform a second pass where we
 *	try to generate not only the shortest path, but the
 *	next shortest, etc, until we find one whose distance
 *	plus crossing penalty is minimized.
 *
 * Results:
 *	Returns the best path.  The first element on the list
 *	(linked by gl_path pointers) is the destination point.
 *	If no path could be found with cost less than bestCost,
 *	returns NULL.
 *
 * Side effects:
 *	Allocates memory from the temporary GlPoint arena.
 *
 * ----------------------------------------------------------------------------
 */

GlPoint *
glProcessLoc(startList, loc, bestCost, doFast)
    GlPoint *startList;	/* List of starting points */
    NLTermLoc *loc;	/* Location of terminal being routed to */
    int bestCost;	/* Best cost so far; if we can't find a path in
			 * less than this cost, give up.
			 */
    bool doFast;	/* If TRUE, only wiggle crossings around within the
			 * channels on the shortest path; don't bother
			 * considering other sequences of channels.  If FALSE,
			 * we keep generating longer and longer paths until
			 * the sheer length of a path exceeds our best
			 * adjusted cost to date.
			 */
{
    extern bool glMazeShortest;
    extern Tile *glMazeDestTile;
    extern Point glMazeDestPoint;
    extern GlPoint *glMazeFindPath();
    extern GlPoint *glCrossAdjust();
    int headFree, shortLength, bestLength;
    GlPoint *lastPt, *bestPt, *adjustedPt;
    GlPage *headPage;

    glNumTries++;
    glCrossScalePenalties();

    /* Sanity checks */
    ASSERT(GEO_SAMEPOINT(loc->nloc_pin->gcr_point, loc->nloc_stem),
		"glProcessLoc");

    /*
     * Passed to glMazeFindPath() for use in estimating the remaining
     * distance to the destination point.  Also figure out which
     * channel tile contains the destination, so we can handle
     * points in it specially.  If the destination point is inside
     * a blocked area, give up immediately.
     */
    glMazeDestPoint = loc->nloc_stem;
    glMazeDestTile = glChanPinToTile((Tile *) NULL, loc->nloc_pin);

    /* Abort immediately if destination is obviously unreachable */
    if (glMazeDestTile == NULL)
	return (GlPoint *) NULL;

    /*
     * First try finding the shortest path.
     * This goes very quickly, because we are able to chop
     * off unpromising paths at an early stage.
     */
    glMazeShortest = TRUE;
    HeapInit(&glMazeHeap, 128, FALSE, FALSE);
    glListToHeap(startList, &loc->nloc_stem);
    headPage = glPathCurPage;
    headFree = glPathCurPage->glp_free;
    bestPt = glMazeFindPath(loc, bestCost);
    glMazeResetCost(headPage, headFree);
    HeapKill(&glMazeHeap, (void (*)()) NULL);
    if (bestPt == (GlPoint *) NULL)
    {
	glBadRoutes++;
	return bestPt;
    }
    shortLength = bestPt->gl_cost;

    /*
     * Now try finding a path that minimizes the crossing penalty.
     * We do this by continuing to generate paths in order of
     * increasing length, then adjusting the crossing points
     * along the path to minimize (locally) the penalty for
     * the path, until we find a path whose unadjusted length
     * exceeds the best cost we've been able to achieve so far.
     * The gl_cost fields in the paths generated by glCrossAdjust
     * incorporate the penalties as well as distance.
     */
    HeapInit(&glMazeHeap, 128, FALSE, FALSE);
    glListToHeap(startList, &loc->nloc_stem);
    if (doFast)
    {
	headPage = glPathCurPage;
	headFree = glPathCurPage->glp_free;
    }
    else glMazeShortest = FALSE;
    bestPt = (GlPoint *) NULL;
    while (lastPt = glMazeFindPath(loc, bestCost))
    {
	adjustedPt = glCrossAdjust((GlPoint *) NULL, lastPt);
	if (adjustedPt->gl_cost < bestCost)
	{
	    bestCost = adjustedPt->gl_cost;
	    bestLength = lastPt->gl_cost;
	    bestPt = adjustedPt;
	}
    }
    if (doFast)
	glMazeResetCost(headPage, headFree);
    HeapKill(&glMazeHeap, (void (*)()) NULL);
    if (bestPt)
    {
	if (glLogFile)
	{
	    fprintf(glLogFile, "%d\t%d (%.2f)\t%d (%.2f)\n",
		shortLength, bestLength,
		(float) bestLength / (float) shortLength * 100.0,
		bestPt->gl_cost,
		(float) bestPt->gl_cost / (float) shortLength * 100.0);
	}
	glGoodRoutes++;
	return bestPt;
    }

    glBadRoutes++;
    glNoRoutes++;
    return (GlPoint *) NULL;
}
