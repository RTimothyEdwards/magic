/*
 * mzSearch.c --
 *
 * Search strategy for maze router.
 * Low level of Maze router (finding next interesting point) is done in 
 * mzSearchRight.c etc. 
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
 *
 * SEARCH STRATEGY:
 *
 * Partial paths are expanded one interesting point at a time - that is a
 * path is expanded to the next interesting point left, right, up and down
 * and vias to neighboring layers are considered, then a new (though possibly
 * one of the extensions just created) path is selected for expansion.  
 *
 * The "search strategy" employed determines which partial-path is
 * chosen for exansion at each stage.
 *
 * A windowed search strategy is used.  At any given time the window
 * bounds define a minimum and maximum distance to the goal.  Partial paths
 * are divided into three groups: those farther from the goal than the
 * window, those within the window, and those nearer to the goal than the
 * window.  The window begins at the start point and is slowly shifted
 * toward the goal over time. Normally the partial-path of least 
 * estimated-total-cost WITHIN
 * the window is chosen for expansion, although a further path (i.e. one
 * beyond the window) that has exceptionally low estimated-total-cost is
 * sometimes chosen instead.  When the trailing edge of the window reaches
 * the goal, the search is deemed complete and the lowest cost complete
 * path found is returned.  However if no complete-path has been found
 * searching will continue until the first complete path is found.
 * The idea behind the windowed search is to
 * expand the paths that promise to give the best (lowest cost) solution, but
 * to slowly shift attention toward the goal to avoid the VERY long time
 * it would take to examine all possible paths.
 *
 * In the case of many partial paths within the window, the above search may
 * be too scattered; "blooming"  is used to give a local-focus to the search
 * by examining a number of expansions
 * from a given partial path before going on to consider other paths.  Blooming
 * is equivalent to searching the move tree in chess before applyng static
 * evaluations of "final" positions.  In practice it makes the router behave
 * much better.
 *
 * The search strategy is implemented with three heaps and four stacks:
 * 
 * mzMaxToGoHeap - keeps partial paths NEARER the goal than the window.
 *                 these paths are not yet eligible for expansion.
 *                 Whenever the window is moved, the top elements of
 *                 this heap (i.e. those furthest from the goal) are
 *                 moved onto the mzMinCostHeap (i.e. into the window).
 *
 * mzMinCostHeap - keeps partial paths inside the window.  the top of this 
 *                 heap, i.e. the least cost path inside
 *                 the window, is compared with the least (penalized) cost of
 *                 the paths beyond the window, and the lesser of these
 *	           two is chosen for expansion.
 *
 * mzMinAdjCostHeap - keeps partial paths that are further from the goal 
 *                    than the 
 *                    window.  These paths are sorted by adjusted cost.
 *                    Adjusted cost is estimated-total-cost plus a penalty
 *                    proportial to the distance of the path from the window.
 *                    The ordering of the paths is independent of the current
 *                    window position - so a "reference position" is used to
 *                    avoid recomputing adjusted costs for these paths 
 *                    everytime the window moves.  The adjusted cost of the
 *                    top (least cost) element is normalized to the current
 *                    window position before comparison with the least cost
 *                    path on the mzMinCostHeap.  The idea behind the
 *                    mzMinAdjCostHeap is
 *                    to discourage searching of paths beyond the window, but
 *                    to do so in a gentle and flexible way, so that 
 *                    behaviour will degrade gracefully under unusual
 *                    circumstances rather than fail catastrophically.
 *                    For example, if the window moves too fast and all the
 *                    reasonable paths are left behind, the search will
 *                    degenerate to a simple cost-based search biased towards 
 *                    paths nearer to completion - a startegy that is
 *                    adequate in many situations.
 *
 * mzBloomStack - paths in current local focus = all expansions of a given
 *                partial-path that do not exceed a given estimated-cost
 *                (also see stacks below)
 *
 * mzStraightStack - focus paths being extended in straight line.
 *
 * mzDownHillStack - focus paths followed only until cost increases.
 *
 * mzWalkStack - paths inside walk (i.e. inside blocked area adjacent to
 *               destination.
 *
 * If the walk stack is not empty the next path is taken from there (thus
 * paths that have reached the threshold of a dest area are given priority.)
 *
 * Next priority is given to the down hill stack, thus a focus path is always
 * expanded until the estimated cost increases.
 *
 * Next the straight stack is processed.  The purpose of this stack is to
 * consider straight extensions in all directions to some given minimum 
 * distance.
 *
 * Next the bloom stack itself is processed.  All extensions derived from
 * the bloom stack are placed on one of the stacks until a fixed cost increment
 * is exceeded (in practice a small increment is used, thus the local focus
 * is only extended in straight lines, and then downhill).
 *
 * If all the stacks are empty, a new focus is pickd from the heaps and 
 * the bloom stack is initialized with it.
 *
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/mzrouter/mzSearch.c,v 1.2 2010/06/24 12:37:19 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "utils/hash.h"
#include "utils/heap.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "utils/signals.h"
#include "textio/textio.h"
#include "utils/malloc.h"
#include "utils/list.h"
#include "debug/debug.h"
#include "utils/styles.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "mzrouter/mzrouter.h"
#include "mzrouter/mzInternal.h"

/* Forward declarations */

extern void mzExtendViaLRContacts();
extern void mzExtendViaUDContacts();
extern void mzBloomInit();
extern void mzMakeStatReport();
extern void mzExtendPath(RoutePath *);


/*
 * ----------------------------------------------------------------------------
 *
 * mzSearch --
 *
 * Find a path between one of the starting terminals and the
 * destination.  The path must lie within the
 * area mzBoundingRect.
 *
 * Results:
 *	Returns a pointer to best complete RoutePath found (NULL
 *	if none found).  This RoutePath may be passed to
 *	MZPaintPath() to be converted into paint.
 *
 * Side effects:
 *	Allocates RoutePaths from our own private storage area.
 *	The caller must copy these to permanent storage with
 *	mzCopyPath() prior to calling MZClean; otherwise,
 *	the returned RoutePath will be lost.
 *
 * ----------------------------------------------------------------------------
 */
RoutePath *
mzSearch(mzResult)
    int *mzResult;
{
    RoutePath *path;		/* solution */
    bool morePartialPaths = TRUE;
    bool bloomLimitHit = FALSE;
    bool windowSweepDoneAndCompletePathFound = FALSE;

    /* Report intial cost estimate */
    if(mzVerbosity>=VERB_STATS)
    {
	TxPrintf("Initial Cost Estimate:   %.0f\n",
		(double)(mzInitialEstimate));
    }

    if (DebugIsSet(mzDebugID, mzDebMaze))
    {
	TxPrintf("\nBEGINNING SEARCH.\n");
	TxPrintf("\tmzWRate = %.0f\n", (double)(mzWRate));
	TxPrintf("\tmzWInitialMinToGo = %.0f\n", (double)(mzWInitialMinToGo));
	TxPrintf("\tmzWInitialMaxToGo = %.0f\n", (double)(mzWInitialMaxToGo));
	TxPrintf("\tmzBloomDeltaCost = %.0f\n", (double)(mzBloomDeltaCost));
    }

    while (morePartialPaths && 
	   !windowSweepDoneAndCompletePathFound &&
	   !bloomLimitHit &&
	   !SigInterruptPending)
    /* Find next path to extend from */
    {
	if (DebugIsSet(mzDebugID, mzDebMaze))
	{
	    TxPrintf("\nABOUT TO SELECT NEXT PATH TO EXTEND.\n");
	    TxMore("");
	}

	/* pop a stack */
	path = NULL;
	if(mzWalkStack != NULL)
	{
	    mzPathSource = SOURCE_WALK;
	    path = (RoutePath *) ListPop(&mzWalkStack);
	 	
	    if (DebugIsSet(mzDebugID, mzDebMaze))
	    {
		TxPrintf("POPPING TOP OF WALK STACK for extension.\n");
		mzPrintPathHead(path);
	    }
	}   
	else if(mzDownHillStack != NULL)
	{
	    mzPathSource = SOURCE_DOWNHILL;
	    path = (RoutePath *) ListPop(&mzDownHillStack);
	
	    if (DebugIsSet(mzDebugID, mzDebMaze))
	    {
		TxPrintf("POPPING TOP OF DOWNHILL STACK for extension.\n");
		mzPrintPathHead(path);
	    }
	}
	else if(mzStraightStack != NULL)
	{
	    mzPathSource = SOURCE_STRAIGHT;
	    path = (RoutePath *) ListPop(&mzStraightStack);
	
	    if (DebugIsSet(mzDebugID, mzDebMaze))
	    {
		TxPrintf("POPPING TOP OF STRAIGHT STACK for extension.\n");
		mzPrintPathHead(path);
	    }
	}
	else if(mzBloomStack != NULL)
	{
	    mzPathSource = SOURCE_BLOOM;
	    path = (RoutePath *) ListPop(&mzBloomStack);

	    if (DebugIsSet(mzDebugID, mzDebMaze))
	    {
		TxPrintf("POPPING TOP OF BLOOM STACK for extension.\n");
		mzPrintPathHead(path);
	    }
	}

	/* Stacks contained a path, go about the buisness of extending it */
	if(path)
	{
	    /* Check hashtable to see if path already obsolete,
	     * (i.e. cheaper path to its enpt found.) 
	     */
	    {
		HashEntry *he;
		PointKey pk;
		RoutePath *hashedPath;

		pk.pk_point = path->rp_entry;
		pk.pk_rLayer = path->rp_rLayer;
		pk.pk_orient = path->rp_orient;
#if SIZEOF_VOID_P == 8
		pk.pk_buffer = 0;	/* Otherwise the hash function screws up */
#endif

		he = HashFind(&mzPointHash, (char *) &pk);
		hashedPath = (RoutePath *) HashGetValue(he);
		ASSERT(hashedPath!=NULL,"mzFindPath");

		if (path!=hashedPath)
		{
		    /* DEBUG - report path rejected due to hash value. */
		    if (DebugIsSet(mzDebugID, mzDebMaze))
		    {
			TxPrintf("HASH LOOKUP reveals better path, REJECT path.\n");
		    }
		    
		    /* better path to this pt already exists, 
		     * skip to next path
		     */
		    continue;
		}
	    }

	    /* Make sure blockage planes have been generated around the path
	     * end.
	     */
	    {
		Point *point = &(path->rp_entry);
		Tile *tp = TiSrPointNoHint(mzHBoundsPlane, point);

		if (TiGetType(tp)==TT_SPACE ||
		    point->p_x == LEFT(tp) || point->p_x == RIGHT(tp))
		{
		    if (DebugIsSet(mzDebugID, mzDebMaze))
		    {
			TxPrintf("Path ends on vertical boundary of blockage");
			TxPrintf(" planes, BLOCKAGE PLANES BEING EXTENDED.\n");
		    }

		    mzExtendBlockBounds(point);
		    if(SigInterruptPending) continue;
		}

		else
		{

		    tp = TiSrPointNoHint(mzVBoundsPlane, point);
		    if (point->p_y == BOTTOM(tp) || point->p_y == TOP(tp))
		    {
			if (DebugIsSet(mzDebugID, mzDebMaze))
			{
			    TxPrintf("Path ends on horizontal boundary");
			    TxPrintf("of blockage planes,  BLOCKAGE PLANES");
			    TxPrintf("BEING EXTENDED.\n");
			}

			mzExtendBlockBounds(point);
			if(SigInterruptPending) continue;
		    }
		}
	    }
	
	    /* DEBUG - if single-stepping, print data, show path end visually,
	     *	and pause.
	     */
	    if (DebugIsSet(mzDebugID, mzDebStep))
	    {
		Rect box;
		CellDef *boxDef;

		/* print stats on this path */
		{
		    int i;
		    RoutePath *p;

		    /* path # */
		    TxPrintf("READY TO EXTEND PATH ");
		    TxPrintf("(blooms: %d, points-processed: %d):\n", 
			     mzNumBlooms,
			     mzNumPaths);
		    mzPrintPathHead(path);

		    /* # of segments in path segments */
		    i=0;
		    for(p=path; p->rp_back!=0; p=p->rp_back)
		    i++;
		    TxPrintf("  (%d segments in path)\n", i);
		}
	    
		/* move box to path end-point */
		if(ToolGetBox(&boxDef,&box))
		{
		    int deltaX = box.r_xtop - box.r_xbot;
		    int deltaY = box.r_ytop - box.r_ybot;
		    box.r_ll = path->rp_entry;
		    box.r_ur.p_x = path->rp_entry.p_x + deltaX;
		    box.r_ur.p_y = path->rp_entry.p_y + deltaY;
		    DBWSetBox(mzRouteUse->cu_def, &box);
		    WindUpdate();
		}

		/* wait for user to continue */
		TxMore("");
	    }

	    /* Extend Path */
	    mzExtendPath(path);

	    /* update statistics */
	    mzNumPaths++;	/* increment number of paths processed */
	    if(--mzPathsTilReport == 0)
	    {
		mzPathsTilReport = mzReportInterval;
		mzMakeStatReport();
	    }

	}
	else
	/* stacks are empty, choose path from heaps to initial them with */
	{
	    HeapEntry maxToGoTopBuf, minCostTopBuf, buf;
	    HeapEntry *maxToGoTop, *minCostTop, *minAdjCostTop;
	    
	    if (DebugIsSet(mzDebugID, mzDebMaze))
	    {
		TxPrintf("BLOOM STACK EMPTY.  ");
                TxPrintf("Choosing path from heaps to initial it with.\n");
	    }

	    /* If the bloom limit has been exceeded, stop searching */
	    if(mzBloomLimit >0 && 
	       mzNumBlooms > mzBloomLimit)
	    {
		if(mzVerbosity>=VERB_BRIEF)
		{
		    TxPrintf("Bloom limit (%d) hit.\n", mzBloomLimit);
		}

		bloomLimitHit = TRUE;
		continue;
	    }

	    /* set window thresholds */
	    {
		dlong offset;

		offset = (dlong) (mzWRate * mzNumBlooms);

		if(offset <= mzWInitialMinToGo)
		{
		    mzWindowMinToGo =  mzWInitialMinToGo - offset;
		}
		else
		{
		    mzWindowMinToGo = 0;
		}

		if(offset <= mzWInitialMaxToGo)
		{
		    mzWindowMaxToGo = mzWInitialMaxToGo - offset;
		}
		else
		{
		    mzWindowMaxToGo = 0;
		}

	        if (DebugIsSet(mzDebugID, mzDebMaze))
	        {
		    TxPrintf("New window thresholds:  ");
                    TxPrintf("windowMinToGo = %.0f, ",
													     (double)mzWindowMinToGo);
		    TxPrintf("windowMaxToGo = %.0f\n ",
													     (double)mzWindowMaxToGo);
  	        }
	    }

	    /* If window sweep is complete, and a complete path
	     * has been found, stop searching.
	     */
	    if((mzWindowMaxToGo == 0) &&
	       HeapLookAtTop(&mzMinCostCompleteHeap))
	    {
	        if (DebugIsSet(mzDebugID, mzDebMaze))
	        {
		    TxPrintf("WINDOW SWEEP DONE AND COMPLETE PATH EXISTS.");
		    TxPrintf("  Stop searching.\n");
		}

		windowSweepDoneAndCompletePathFound = TRUE;
		continue;
	    }

	    /* Move points that meet the minTogo threshold to window 
	    * (Points are moved from the MaxToGo heap to the minCost heap)
	    */
	    {
		if (DebugIsSet(mzDebugID, mzDebMaze))
		{
		    TxPrintf("Moving paths into window "); 
		    TxPrintf("(maxTogoHeap -> minCostHeap):  \n");
		}

		while((maxToGoTop=HeapRemoveTop(&mzMaxToGoHeap,
			  &maxToGoTopBuf)) != NULL && 
		      (maxToGoTop->he_union.hu_dlong >= mzWindowMinToGo))
		{
		    if (DebugIsSet(mzDebugID, mzDebMaze))
		    {
			mzPrintPathHead((RoutePath*)(maxToGoTop->he_id));
		    }

		    HeapAddDLong(&mzMinCostHeap, 
			      ((RoutePath *)(maxToGoTop->he_id))->rp_cost, 
			      (char *) (maxToGoTop->he_id));
		}
		if(maxToGoTop!=NULL)
		{
		    HeapAddDLong(&mzMaxToGoHeap,
			      maxToGoTop->he_union.hu_dlong, 
			      (char *) (maxToGoTop->he_id));
		}
	    }

	    /* Clear top of minCostHeap of points that no longer meet the
	     * mzWindowMaxToGo threshold.
	     * (minCostHeap -> minAdjCostHeap)
	     */
	    {
		if (DebugIsSet(mzDebugID, mzDebMaze))
		{
		    TxPrintf("Moving paths out of window ");
		    TxPrintf("(minCostHeap -> minAdjCostHeap):  \n");
		}

		while((minCostTop=HeapRemoveTop(&mzMinCostHeap,
			&minCostTopBuf))!=NULL &&
			(((RoutePath *)(minCostTop->he_id))->rp_togo >
			mzWindowMaxToGo))
		{
		    dlong adjCost;

		    /* compute adjusted cost */
		    adjCost = (dlong)((RoutePath *)(minCostTop->he_id))->rp_togo;
		    adjCost = (dlong)(adjCost * mzPenalty.rf_mantissa);
		    adjCost = adjCost >> mzPenalty.rf_nExponent;
		    adjCost += minCostTop->he_union.hu_dlong;

		    if (DebugIsSet(mzDebugID, mzDebMaze))
		    {
			mzPrintPathHead((RoutePath*)(minCostTop->he_id));
			TxPrintf("  Heap-key adjCost = %.0f\n", (double)adjCost);
		    }

		    /* add to adjusted cost heap */
		    HeapAddDLong(&mzMinAdjCostHeap, adjCost,
			      (char *) (minCostTop->he_id));
		}

		if(minCostTop!=NULL)
		{
		    HeapAddDLong(&mzMinCostHeap,
			      minCostTop->he_union.hu_dlong, 
			      (char *) (minCostTop->he_id));
		}
	    }

	    /* Peek at tops of heaps 
	     * (equal cost elements might have got shuffled above when
	     * we placed the last poped element back on the heap.)
	     */
	    minAdjCostTop = HeapLookAtTop(&mzMinAdjCostHeap);
	    maxToGoTop = HeapLookAtTop(&mzMaxToGoHeap);
	    minCostTop = HeapLookAtTop(&mzMinCostHeap);
	   
	    /* Print tops of maxToGo, minCost and minAdjCost heaps */
	    if (DebugIsSet(mzDebugID, mzDebMaze))
	    {
		TxPrintf("Max togo top:\n");
		if(maxToGoTop)
		{
		    mzPrintPathHead((RoutePath*)(maxToGoTop->he_id));
		}
		else
		{
		    TxPrintf("  nil\n");
		}
		TxPrintf("Min cost top:\n");
		if(minCostTop)
		{
		    mzPrintPathHead((RoutePath*)(minCostTop->he_id));
		}
		else
		{
		    TxPrintf("  nil\n");
		}
		TxPrintf("Min adjcost top:\n");
		if(minAdjCostTop)
		{
		    TxPrintf("  Heap-key adjCost:  %.0f\n", 
			     (double)(minAdjCostTop->he_union.hu_dlong));
		}
		else
		{
		    TxPrintf("  nil\n");
		}
	    }

	    if(minCostTop && minAdjCostTop)
	    /* need to compare minCostTop and minAdjCostTop */
	    {
		/* compute adjusted cost corresponding to current window
		 * position (we only penalize for amount toGo exceeding
		 * mzWindowMaxToGo)
		 */
		dlong cost, adjCost;

		cost =(dlong)((RoutePath *)(minAdjCostTop->he_id))->rp_cost;
		adjCost = (dlong)((RoutePath *)(minAdjCostTop->he_id))->rp_togo;
		ASSERT(adjCost >= mzWindowMaxToGo,"mzSearch");
		adjCost -= mzWindowMaxToGo;
		adjCost *= mzPenalty.rf_mantissa;
		adjCost = adjCost >> mzPenalty.rf_nExponent;
		adjCost += cost;
		if (DebugIsSet(mzDebugID, mzDebMaze))
		{		
		    TxPrintf("WINDOW-CORRECTED ADJCOST:  %.0f\n",
			 	(double)(adjCost));
		}
		if(minCostTop->he_union.hu_dlong <= adjCost)
		{
		    if (DebugIsSet(mzDebugID, mzDebMaze))
		    {
			TxPrintf("INITIALIZING BLOOM STACK ");
			TxPrintf("WITH TOP OF MIN COST HEAP.\n");
		    }
		    minCostTop = HeapRemoveTop(&mzMinCostHeap, &buf);
		    mzBloomInit((RoutePath *) (minCostTop->he_id));
		}
		else
		{
		    if (DebugIsSet(mzDebugID, mzDebMaze))
		    {
			TxPrintf("INITIALIZING BLOOM STACK ");
			TxPrintf("WITH TOP OF MIN ADJCOST HEAP.\n");
		    }
		    minAdjCostTop = HeapRemoveTop(&mzMinAdjCostHeap, &buf);
		    mzBloomInit((RoutePath *) (minAdjCostTop->he_id));
		    mzNumOutsideBlooms++;
		}
	    }
	    else if(minCostTop)
	    /* minAdjCostHeap empty, so bloom from minCostTop */
	    {
		if (DebugIsSet(mzDebugID, mzDebMaze))
		{
		    TxPrintf("INITIALIZING BLOOM STACK ");
		    TxPrintf("WITH TOP OF MIN COST HEAP.\n");
		}
		minCostTop = HeapRemoveTop(&mzMinCostHeap, &buf);
		mzBloomInit((RoutePath *) (minCostTop->he_id));
   	    }
	    else if(minAdjCostTop)
	    /* minCost Heap empty, so bloom from minAdjCostTop */
	    {
		if (DebugIsSet(mzDebugID, mzDebMaze))
		{
		    TxPrintf("INITIALIZING BLOOM STACK ");
		    TxPrintf("WITH TOP OF MIN ADJCOST HEAP.\n");
		}
		minAdjCostTop = HeapRemoveTop(&mzMinAdjCostHeap, &buf);
		mzBloomInit((RoutePath *) (minAdjCostTop->he_id));
		mzNumOutsideBlooms++;
	    }
	    else
	    /* minCost and minAdjCost heaps empty, 
	     * bloom from top of TOGO heap */
	    {
		if(maxToGoTop)
		{
		    if (DebugIsSet(mzDebugID, mzDebMaze))
		    {
			TxPrintf("INITIALIZING BLOOM STACK ");
			TxPrintf("WITH TOP OF MAX TOGO HEAP.\n");
		    }
		    maxToGoTop = HeapRemoveTop(&mzMaxToGoHeap, &buf);
		    mzBloomInit((RoutePath *) (maxToGoTop->he_id));
		    mzNumOutsideBlooms++;
		}
		else
		/* No paths left to extend from */
		{
		    if (DebugIsSet(mzDebugID, mzDebMaze))
		    {
			TxPrintf("NO PATHS LEFT TO EXTEND FROM.\n");
		    }
		    morePartialPaths = FALSE;
		}
	    }
	}
    }

    /* Give final stat report. */
    mzMakeStatReport();

    /* Return best complete path */
    {
	HeapEntry heEntry;

	if(HeapRemoveTop(&mzMinCostCompleteHeap,&heEntry))
	{
	    if (mzResult)
	    {
		if (SigInterruptPending)
		    *mzResult = MZ_CURRENT_BEST;
		else
		    *mzResult = MZ_SUCCESS;
	    }
	    return (RoutePath *)(heEntry.he_id);
	}
	else
	{
	    if (mzResult)
	    {
		if (SigInterruptPending)
		    *mzResult = MZ_INTERRUPTED;
		else
		    *mzResult = MZ_FAILURE;
	    }
	    return NULL;
	}
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzExtendPath --
 *
 * Spread out to next interesting point in four directions, and to adjacent
 * layers through contacts.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds new routepaths to the queues (ie. heaps and bloomstack).
 *
 * ----------------------------------------------------------------------------
 */

void
mzExtendPath(path)
    RoutePath *path;
{
    int extendCode = path->rp_extendCode;

    if (extendCode & EC_RIGHT)
    {
	mzExtendRight(path);
    }

    if (extendCode & EC_LEFT)
    {
	mzExtendLeft(path);
    }

    if (extendCode & EC_UP)
    {
	mzExtendUp(path);
    }

    if (extendCode & EC_DOWN)
    {
	mzExtendDown(path);
    }

    if (extendCode & EC_UDCONTACTS)
    {
	mzExtendViaUDContacts(path);
    }
    if (extendCode & EC_LRCONTACTS)
    {
	mzExtendViaLRContacts(path);
    }

    if (extendCode >= EC_WALKRIGHT)
    {
	if (extendCode & EC_WALKRIGHT)
	{
	    mzWalkRight(path);
	}
	else if (extendCode & EC_WALKLEFT)
	{
	    mzWalkLeft(path);
	}

	else if (extendCode & EC_WALKUP)
	{
	    mzWalkUp(path);
	}

	else if (extendCode & EC_WALKDOWN)
	{
	    mzWalkDown(path);
	}
	else if (extendCode & EC_WALKUDCONTACT)
	{
	    mzWalkUDContact(path);
	}
	else if (extendCode & EC_WALKLRCONTACT)
	{
	    mzWalkLRContact(path);
	}
    }

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzExtendViaLRContacts --
 *
 * Spread from a point to other planes reachable from it,
 * by using contacts.  Stacked contacts are allowed.  Search the horizontal
 * block plane.  Limit to areas that fit the contact minimum length.
 *
 * Results	None.
 *
 * Side effects:
 *	Adds RoutePaths to the heap (mzReservePathCostHeap).
 *
 * ----------------------------------------------------------------------------
 */
void
mzExtendViaLRContacts(path)
    RoutePath *path;
{
    Point p = path->rp_entry, *lastCpos = NULL;
    RouteLayer *rLayer = path->rp_rLayer; 
    RouteContact *rC;
    List *cL;
    RouteLayer *newRLayer;
    Tile *tp;
    TileType type, lastCtype = TT_SPACE;
    RoutePath *spath;
    int bendDist = 0;

    /* DEBUG - trace calls to this routine */
    if (DebugIsSet(mzDebugID, mzDebMaze))
	TxPrintf("EXTENDING WITH CONTACTS (HORIZONTAL)\n");

    /* Check what the last contact was and remember its	*/
    /* position.  Do not allow two contacts of the same	*/
    /* type within a DRC error distance of each other.	*/

    for (spath = path; spath && spath->rp_back && (spath->rp_orient != 'O');
		spath = spath->rp_back);
    if (spath->rp_back)
    {
	lastCpos = &spath->rp_entry;
	rC = MZGetContact(spath, spath->rp_back);
	lastCtype = rC->rc_routeType.rt_tileType;
    }

    /* Check where the last route bend is.  Don't allow a	*/
    /* contact within a DRC error distance of a	bend.		*/

    if (path)
    {
	if (path->rp_orient == 'V')
	{
	    for (spath = path->rp_back; spath && spath->rp_orient == 'V';
			spath = spath->rp_back);
	    if (spath && spath->rp_orient == 'H')
		bendDist = spath->rp_entry.p_y - p.p_y;
	    if (bendDist < 0)
		bendDist += rLayer->rl_routeType.rt_width;
	}
	else if (path->rp_orient == 'H')
	{
	    for (spath = path->rp_back; spath && spath->rp_orient == 'H';
			spath = spath->rp_back);
	    if (spath && spath->rp_orient == 'V')
		bendDist = spath->rp_entry.p_x - p.p_x;
	    if (bendDist < 0)
		bendDist += rLayer->rl_routeType.rt_width;
	}
    }

    /* Loop through contacts that connect to current rLayer */
    for (cL=rLayer->rl_contactL; cL!=NULL; cL=LIST_TAIL(cL))
    {
	rC = (RouteContact *) LIST_FIRST(cL);

	/* Don't use inactive contacts */
	if (!(rC->rc_routeType.rt_active))
	    continue;

	/* Get "other" route Layer contact connects to */
	if (rC->rc_rLayer1 == rLayer)
	{
	    newRLayer = rC->rc_rLayer2;
	}
	else
	{
	    ASSERT(rC->rc_rLayer2 == rLayer,
	    "mzExtendViaLRContacts");
	    newRLayer = rC->rc_rLayer1;
	}

	/* Don't spread to inactive layers */
	if (!(newRLayer->rl_routeType.rt_active)) continue;

	/* Find tile on contact plane that contains point. */

	tp = TiSrPointNoHint(rC->rc_routeType.rt_hBlock, &p);
	type = TiGetType(tp);

	/* 1. If blocked, don't place a contact */
	if ((type != TT_SPACE) && (type != TT_SAMENODE))
	    continue;

	/* 2. If tile space is not long enough for the contact, don't place */
	if (RIGHT(tp) - p.p_x <= rC->rc_routeType.rt_length
			- rC->rc_routeType.rt_width)
	    continue;

	/* 3. Check distance from last contact, if the same type */
	if (rC->rc_routeType.rt_tileType == lastCtype)
	{
	    int cdist = rC->rc_routeType.rt_spacing[lastCtype] +
			rC->rc_routeType.rt_width;
	    if ((abs(p.p_x - lastCpos->p_x) < cdist) &&
			(abs(p.p_y - lastCpos->p_y) < cdist))
		continue;
	}

	/* 4. Check distance to last route bend */
	if (bendDist != 0)
	{
	    int cwidth = rC->rc_routeType.rt_width;
	    int spacing = rC->rc_routeType.rt_spacing[rLayer->rl_routeType.rt_tileType];
	    if (bendDist > cwidth && bendDist < cwidth + spacing)
		continue;
	    if (bendDist < 0 && bendDist > -spacing)
		continue;
	}

	/* OK to drop a contact here */
	{
	    dlong conCost;
	    int extendCode;

	    /* set contact cost */
	    conCost = (dlong) rC->rc_cost;

	    /* determine appropriate extendcode */
	    tp = TiSrPointNoHint(newRLayer->rl_routeType.rt_hBlock, &p);
	    type = TiGetType(tp);

	    switch (type)
	    {
		case TT_SPACE:
		case TT_SAMENODE:
		   extendCode = EC_RIGHT | EC_LEFT | EC_UP | EC_DOWN;
		   break;

		case TT_LEFT_WALK:
		    extendCode = EC_WALKRIGHT;
		    break;

		case TT_RIGHT_WALK:
		    extendCode = EC_WALKLEFT;
		    break;

		case TT_TOP_WALK:
		    extendCode = EC_WALKDOWN;
		    break;

		case TT_BOTTOM_WALK:
		    extendCode = EC_WALKUP;
		    break;

		case TT_ABOVE_LR_WALK:
		case TT_BELOW_LR_WALK:
		    /* TO DO:  Check if stacked contacts are allowed! */
		    extendCode = EC_WALKLRCONTACT;
		    break;

		case TT_ABOVE_UD_WALK:
		case TT_BELOW_UD_WALK:
		    /* TO DO:  Check if stacked contacts are allowed! */
		    extendCode = EC_WALKUDCONTACT;
		    break;

		case TT_DEST_AREA:

		    extendCode = EC_COMPLETE;
		    break;

		default:
		    /* this shouldn't happen */
		    ASSERT(FALSE,"mzExtendViaLRContacts");
		    continue;
	    }

	    /* Finally add point after contact */
	    mzAddPoint(path, &p, newRLayer, 'O', extendCode, &conCost);
	}
    }

    return;
}

/*
 * ----------------------------------------------------------------------------
 *
 * mzExtendViaUDContacts --
 *
 * Spread from a point to other planes reachable from it,
 * by using contacts.  Stacked contacts are allowed.  Search the vertical
 * block plane.  Limit to areas that fit the contact minimum length.
 *
 * Results	None.
 *
 * Side effects:
 *	Adds RoutePaths to the heap (mzReservePathCostHeap).
 *
 * ----------------------------------------------------------------------------
 */
void
mzExtendViaUDContacts(path)
    RoutePath *path;
{
    Point p = path->rp_entry, *lastCpos = NULL;
    RouteLayer *rLayer = path->rp_rLayer; 
    RouteContact *rC;
    List *cL;
    RouteLayer *newRLayer;
    Tile *tp;
    TileType type, lastCtype = TT_SPACE;
    RoutePath *spath;
    int bendDist = 0;

    /* DEBUG - trace calls to this routine */
    if (DebugIsSet(mzDebugID, mzDebMaze))
	TxPrintf("EXTENDING WITH CONTACTS (VERTICAL)\n");

    /* Check what the last contact was and remember its	*/
    /* position.  Do not allow two contacts of the same	*/
    /* type within a DRC error distance of each other.	*/

    for (spath = path; spath && spath->rp_back && (spath->rp_orient != 'X');
		spath = spath->rp_back);
    if (spath->rp_back)
    {
	lastCpos = &spath->rp_entry;
	rC = MZGetContact(spath, spath->rp_back);
	lastCtype = rC->rc_routeType.rt_tileType;
    }

    /* Check where the last route bend is.  Don't allow a	*/
    /* contact within a DRC error distance of a	bend.		*/

    if (path)
    {
	if (path->rp_orient == 'V')
	{
	    for (spath = path->rp_back; spath && spath->rp_orient == 'V';
			spath = spath->rp_back);
	    if (spath && spath->rp_orient == 'H')
		bendDist = spath->rp_entry.p_y - p.p_y;
	    if (bendDist < 0)
		bendDist += rLayer->rl_routeType.rt_width;
	}
	else if (path->rp_orient == 'H')
	{
	    for (spath = path->rp_back; spath && spath->rp_orient == 'H';
			spath = spath->rp_back);
	    if (spath && spath->rp_orient == 'V')
		bendDist = spath->rp_entry.p_x - p.p_x;
	    if (bendDist < 0)
		bendDist += rLayer->rl_routeType.rt_width;
	}
    }

    /* Loop through contacts that connect to current rLayer */
    for (cL=rLayer->rl_contactL; cL!=NULL; cL=LIST_TAIL(cL))
    {
	rC = (RouteContact *) LIST_FIRST(cL);

	/* Don't use inactive contacts */
	if (!(rC->rc_routeType.rt_active))
	    continue;

	/* Get "other" route Layer contact connects to */
	if (rC->rc_rLayer1 == rLayer)
	{
	    newRLayer = rC->rc_rLayer2;
	}
	else
	{
	    ASSERT(rC->rc_rLayer2 == rLayer,
	    "mzExtendViaUDContacts");
	    newRLayer = rC->rc_rLayer1;
	}

	/* Don't spread to inactive layers */
	if (!(newRLayer->rl_routeType.rt_active)) continue;

	/* Find tile on contact plane that contains point. */

	tp = TiSrPointNoHint(rC->rc_routeType.rt_vBlock, &p);
	type = TiGetType(tp);

	/* 1. If blocked, don't place a contact */
	if ((type != TT_SPACE) && (type != TT_SAMENODE))
	    continue;

	/* 2. If tile space is not long enough for the contact, don't place */
	if (TOP(tp) - p.p_y <= rC->rc_routeType.rt_length
			- rC->rc_routeType.rt_width)
	    continue;

	/* 3. Check distance from last contact, if the same type */
	if (rC->rc_routeType.rt_tileType == lastCtype)
	{
	    int cdist = rC->rc_routeType.rt_spacing[lastCtype] +
			rC->rc_routeType.rt_width;
	    if ((abs(p.p_x - lastCpos->p_x) < cdist) &&
			(abs(p.p_y - lastCpos->p_y) < cdist))
		continue;
	}

	/* 4. Check distance to last route bend */
	if (bendDist != 0)
	{
	    int cwidth = rC->rc_routeType.rt_width;
	    int spacing = rC->rc_routeType.rt_spacing[rLayer->rl_routeType.rt_tileType];
	    if (bendDist > cwidth && bendDist < cwidth + spacing)
		continue;
	    if (bendDist < 0 && bendDist > -spacing)
		continue;
	}

	/* OK to drop a contact here */
	{
	    dlong conCost;
	    int extendCode;

	    /* set contact cost */
	    conCost = (dlong) rC->rc_cost;

	    /* determine appropriate extendcode */
	    tp = TiSrPointNoHint(newRLayer->rl_routeType.rt_vBlock, &p);
	    type = TiGetType(tp);

	    switch (type)
	    {
		case TT_SPACE:
		case TT_SAMENODE:
		   extendCode = EC_RIGHT | EC_LEFT | EC_UP | EC_DOWN;
		   break;

		case TT_LEFT_WALK:
		    extendCode = EC_WALKRIGHT;
		    break;

		case TT_RIGHT_WALK:
		    extendCode = EC_WALKLEFT;
		    break;

		case TT_TOP_WALK:
		    extendCode = EC_WALKDOWN;
		    break;

		case TT_BOTTOM_WALK:
		    extendCode = EC_WALKUP;
		    break;

		case TT_ABOVE_LR_WALK:
		case TT_BELOW_LR_WALK:
		    /* TO DO:  Check if stacked contacts are allowed! */
		    extendCode = EC_WALKLRCONTACT;
		    break;

		case TT_ABOVE_UD_WALK:
		case TT_BELOW_UD_WALK:
		    /* TO DO:  Check if stacked contacts are allowed! */
		    extendCode = EC_WALKUDCONTACT;
		    break;

		case TT_DEST_AREA:

		    extendCode = EC_COMPLETE;
		    break;

		default:
		    /* this shouldn't happen */
		    ASSERT(FALSE,"mzExtendViaUDContacts");
		    continue;
	    }

	    /* Finally add point after contact */
	    mzAddPoint(path, &p, newRLayer, 'X', extendCode, &conCost);
	}
    }

    return;
}

/*
 * ----------------------------------------------------------------------------
 *
 * mzAddPoint --
 *
 * Process interesting point.  If point within bounds and not redundant, 
 * link to previous path, update cost, and add to heap.
 *
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds a RoutePath to the heap.
 *
 * ----------------------------------------------------------------------------
 */
void
mzAddPoint(path, p, rLayer, orient, extendCode, costptr)
    RoutePath *path;	/* path that new point extends */
    Point *p;		/* new point */
    RouteLayer *rLayer;	/* Route Layer of new point */
    int orient;		/* 'H' = endpt of hor seg, 'V' = endpt of vert seg, 
			 * 'O' = LR contact, 'X' = UD contact,
			 * 'B' = first point in path and blocked
			 */
    int extendCode;	/* interesting directions to extend in */
    dlong *costptr;	/* Incremental cost of new path segment */

{
    RoutePath *newPath;
    RoutePath *hashedPath;
    HashEntry *he;
    PointKey pk;
    dlong togo, cost;

    /* DEBUG - print candidate frontier point */
    if (DebugIsSet(mzDebugID, mzDebMaze))
    {
	TxPrintf("mzAddPoint called:  point=(%d,%d), layer=%s, orient='%c'\n",
	    	p->p_x, p->p_y,
	    	DBTypeLongNameTbl[rLayer->rl_routeType.rt_tileType],
	    	orient);
    }

    cost = *costptr;
    ASSERT(cost >= 0,"mzAddPoint");

    /* Ignore if outside of bounding box */
    if (!GEO_ENCLOSE(p, &mzBoundingRect))
	return;

    /* get estimated distance togo */
    if(extendCode == EC_COMPLETE)
	togo = 0;
    else
	togo = mzEstimatedCost(p);

    /* compute (total) cost for new path */
    {
	/* initially cost = cost of new leg in path */

	/* Add jogcost if appropriate */

	if (path != NULL && 
	   path->rp_rLayer == rLayer &&
	   path->rp_orient != 'O' && path->rp_orient != 'X' &&
	   path->rp_orient != orient)
	{
	    cost += rLayer->rl_jogCost;
	}

	/* Add estimated total cost prior to new point, 
	 * (or cost so far in the case of initial paths).
	 */
	if (path != NULL)
	{
	    cost += path->rp_cost;
	}
	/* If not initial path, subtract out old estimated cost togo */
	if (mzPathSource != SOURCE_INIT)
	{
	    cost -= path->rp_togo;
	}

	/* Add new estimated cost to completion */
	cost += togo;
    }

    /* Check hash table to see if cheaper path to this point already
     * found - if so don't add this point.
     */
    pk.pk_point = *p;
    pk.pk_rLayer = rLayer;
    pk.pk_orient = orient;
#if SIZEOF_VOID_P == 8
    pk.pk_buffer = 0;       /* Otherwise the hash function screws up */
#endif          

    he = HashFind(&mzPointHash, (char *) &pk);
    hashedPath = (RoutePath *) HashGetValue(he);

    if (hashedPath != NULL && (hashedPath->rp_cost <= cost))
    {
	if (DebugIsSet(mzDebugID, mzDebMaze))
	{
	    TxPrintf("new point NOT added, at least as good "
			"path to pt already exists:  ");
	    TxPrintf("new cost = %.0f, ",
			(double)(cost));
	    TxPrintf("cheapest cost = %.0f\n",
			(double)(hashedPath->rp_cost));
	}
	return;
    }

    /* If initial path, update min initial cost */
    if(mzPathSource==SOURCE_INIT)
    {
	if(cost < mzMinInitialCost)
	{
	    mzMinInitialCost = cost;
	}
    }

    /* Create new path element */
    newPath = NEWPATH();
    newPath->rp_rLayer = rLayer;
    newPath->rp_entry = *p;
    newPath->rp_orient = orient;
    newPath->rp_cost = cost;
    newPath->rp_extendCode = extendCode;
    newPath->rp_togo = togo;
    newPath->rp_back = path;

    /* keep statistics */
    mzNumPathsGened++;

    /* Enter in hash table */
    HashSetValue(he, (ClientData) newPath);

    /* Add to appropriate queue or stack */
    if(extendCode == EC_COMPLETE)
    {
	if (DebugIsSet(mzDebugID, mzDebMaze))
	{
	    TxPrintf("PATH COMPLETE (WALKED IN).  Add to complete heap.\n");
	}

	HeapAddDLong(&mzMinCostCompleteHeap, newPath->rp_cost, 
		  (char *) newPath);

	/* compute stats and make completed path report */
	{		    
	    mzNumComplete++;
	    
	    if(mzVerbosity>=VERB_STATS)
	    {
		dlong cost, excessCost;
		double excessPercent;

		mzMakeStatReport();

		TxPrintf("PATH #%d  ", mzNumComplete);
		

		cost = newPath->rp_cost;
		TxPrintf("cst:%.0f, ", (double)(newPath->rp_cost));
		if(cost < mzInitialEstimate)
		{
		    TxPrintf("(<est)");
		}
		else
		{
		    excessCost = cost - mzInitialEstimate;
		    excessPercent = 100.0 * (double)(excessCost)/
														    (double)(mzInitialEstimate);
		    
		    TxPrintf("overrun: %.0f%%", excessPercent);
		}

		TxPrintf("\n");
	    }
	}
    }
    else if(extendCode >= EC_WALKRIGHT)
    {
	LIST_ADD(newPath, mzWalkStack);
    }
    else
    {
	switch(mzPathSource)
	{
	    case SOURCE_BLOOM:
	    if(orient=='O')
	    {
		/* just changing layers, add back to bloom */
		LIST_ADD(newPath, mzBloomStack);
	    }
	    else if((orient=='H' && rLayer->rl_hCost<=rLayer->rl_vCost) ||
		    (orient=='V' && rLayer->rl_vCost<=rLayer->rl_hCost))
	    {
		/* going in preferred direction */
		LIST_ADD(newPath, mzStraightStack);
	    }
	    else
	    {
		/* non preferred, add to heaps */
		HeapAddDLong(&mzMaxToGoHeap, togo, (char *) newPath);
	    }
	    break;

	    case SOURCE_STRAIGHT:
	    if(path->rp_orient==orient && (cost < mzBloomMaxCost))
	    {
		/* straight and within bounds, keep going*/
		LIST_ADD(newPath, mzStraightStack);
	    }
	    else
	    {
		/* from here on, follow downhill only */
		LIST_ADD(newPath, mzDownHillStack);
	    }
	    break;

	    case SOURCE_DOWNHILL:
	    {
		dlong oldCostPlusOne;

		oldCostPlusOne = path->rp_cost + 1;
		if(cost <oldCostPlusOne)
		{
		    /* cost within a unit, keep following down hill */
	  	    LIST_ADD(newPath, mzDownHillStack);
		}
		else
		{
		    /* increased cost, add to heaps */
	  	    HeapAddDLong(&mzMaxToGoHeap, togo, (char *) newPath);
		}
	    }
	    break;

	    case SOURCE_INIT:
	    /* Initial points go on heap */
	    HeapAddDLong(&mzMaxToGoHeap, togo, (char *) newPath);
	    break;
	}
    }
    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzBloomInit --
 *
 * Initialize bloom stack with given path.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds a routepath to bloom stack, and sets mzBloomMaxCost.
 *
 * ----------------------------------------------------------------------------
 */
void
mzBloomInit(path)
    RoutePath *path;	/* path that new point extends */
{
    ASSERT(mzBloomStack==NULL,"mzBloomInit");

    LIST_ADD(path, mzBloomStack);
    mzBloomMaxCost = path->rp_cost + mzBloomDeltaCost;

    mzNumBlooms++;

    return;
}

/*
 * ----------------------------------------------------------------------------
 *
 * mzMakeStatReport --
 *
 * Print out route statistics
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Output via TxPrintf()
 *
 * ----------------------------------------------------------------------------
 */

void
mzMakeStatReport()
{
    
    /* if we aren't being verbose, skip this */
    if(mzVerbosity<VERB_STATS) 	return;

    TxPrintf("  Blms:%d(%d)", mzNumBlooms - mzNumOutsideBlooms,
	     mzNumBlooms);

    TxPrintf("  Wndw:%.0f(%.0f%%)", (double)(mzWindowMaxToGo),
	     	(100.0 * (1- (double)(mzWindowMaxToGo)/
		((double)(mzInitialEstimate) +(double)(mzWWidth)))));

    TxPrintf("  Pts:%d(%d)",
	     mzNumPaths,
	     mzNumPathsGened);

    TxPrintf("  Blkgen: %dx%.0f",
	     mzBlockGenCalls,
	     mzBlockGenArea/mzBlockGenCalls);

    TxPrintf("(%.0f/icst)",
	     (double)mzBlockGenArea/(double)(mzInitialEstimate));
    
    TxPrintf("\n");
    return;
}

