/*
 * mzSubrs.c --
 *
 * Misc. surport routines for the Maze router.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/mzrouter/mzSubrs.c,v 1.2 2008/06/01 18:37:44 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "drc/drc.h"
#include "select/select.h"
#include "utils/signals.h"
#include "textio/textio.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/styles.h"
#include "debug/debug.h"
#include "utils/undo.h"
#include "textio/txcommands.h"
#include "utils/malloc.h"
#include "utils/main.h"
#include "utils/geofast.h"
#include "utils/list.h"
#include "utils/touchingtypes.h"
#include "utils/heap.h"
#include "mzrouter/mzrouter.h"
#include "mzrouter/mzInternal.h"



/*
 * ----------------------------------------------------------------------------
 *
 * mzComputeDerivedParms --
 *
 * Processes current parms deriving some parameters from others.
 *
 * Results:
 *	None
 *
 * Side effects:
 *      Modifies current parms.
 *
 * ----------------------------------------------------------------------------
 */

void
mzComputeDerivedParms()
{
    RouteContact *rC;
    RouteLayer *rL;
    RouteType *rT;

    /* Compute active routelayer list */
    mzActiveRLs = NULL;
    for(rL=mzRouteLayers; rL!=NULL; rL=rL->rl_next)
    {
	if(rL->rl_routeType.rt_active)
	{
	    rL->rl_nextActive = mzActiveRLs;
	    mzActiveRLs = rL;
	}
    }

    /* Compute active routetype list */
    mzActiveRTs = NULL;
    for(rT=mzRouteTypes; rT!=NULL; rT=rT->rt_next)
    {
	if(rT->rt_active)
	{
	    rT->rt_nextActive = mzActiveRTs;
	    mzActiveRTs = rT;
	}
    }

    /* Compute bloats and effWidth for route layers.
     * Since we are routing bottom left edge of wires:
     *     bloat to bottom = spacing + width - 1;
     *     bloat to top = spacing
     */
    for(rL=mzRouteLayers; rL!=NULL; rL=rL->rl_next)
    {
	RouteType *rT = &(rL->rl_routeType);
	int i;

	rT->rt_effWidth = rT->rt_width;

        for(i=0;i<=TT_MAXTYPES;i++)
	{
	    if(rT->rt_spacing[i]>=0)
	    {
		rT->rt_bloatBot[i] = rT->rt_width + rT->rt_spacing[i] - 1;
		rT->rt_bloatTop[i] = rT->rt_spacing[i];
	    }
	    else
	    {
		rT->rt_bloatBot[i] = -1;
		rT->rt_bloatTop[i] = -1;
	    }
	}
    }

    /* contact widths and bloats are max of components */
    for(rC=mzRouteContacts; rC!=NULL; rC=rC->rc_next)
    {
	RouteType *rT = &(rC->rc_routeType);
	RouteType *rT1 = &(rC->rc_rLayer1->rl_routeType);
	RouteType *rT2 = &(rC->rc_rLayer2->rl_routeType);
	int i;

	rT->rt_effWidth = MAX(MAX(rT1->rt_width,rT2->rt_width),rT->rt_width);
    
        for(i=0;i<=TT_MAXTYPES;i++)
	{
	    int bot, bot1, bot2;
	    int top, top1, top2;

	    if(rT->rt_spacing[i]>=0)
	    {
		bot = rT->rt_width + rT->rt_spacing[i] - 1;
		top = rT->rt_spacing[i];
	    }
	    else
	    {
		bot = -1;
		top = -1;
	    }

	    if(rT1->rt_spacing[i]>=0)
	    {
		bot1 = rT1->rt_width + rT1->rt_spacing[i] - 1;
		top1 = rT1->rt_spacing[i];
	    }
	    else
	    {
		bot1 = -1;
		top1 = -1;
	    }

	    if(rT2->rt_spacing[i]>=0)
	    {
		bot2 = rT2->rt_width + rT2->rt_spacing[i] - 1;
		top2 = rT2->rt_spacing[i];
	    }
	    else
	    {
		bot2 = -1;
		top2 = -1;
	    }

	    rT->rt_bloatBot[i] = MAX(MAX(bot1,bot2),bot);
	    rT->rt_bloatTop[i] = MAX(MAX(top1,top2),top);
	}
    }

    /*
     * compute context radius = how much to bloat blockage area
     * to be built to obtain search area for mask data.
     */
    {
        int i;

	mzContextRadius = 0;
	for(rT=mzActiveRTs; rT!=NULL; rT=rT->rt_nextActive)
        for(i=0;i<=TT_MAXTYPES;i++)
	mzContextRadius = MAX(mzContextRadius,rT->rt_bloatBot[i]);
    }

    /* If max walk length is -1, set to twice the context radius */
    if(mzMaxWalkLength==-1)
    {
	mzMaxWalkLength = mzContextRadius * 2;
    }

    /* If bounds increment is -1, set to 30 times min active pitch */
    if(mzBoundsIncrement==-1)
    {
	int minPitch = INFINITY;

	for(rL=mzActiveRLs; rL!=NULL; rL=rL->rl_nextActive)
	{
	    RouteType *rT = &(rL->rl_routeType);
	    int pitch = rT->rt_width + rT->rt_spacing[rT->rt_tileType];

	    minPitch = MIN(minPitch, pitch);
	}

	if(minPitch==INFINITY)
	/* Don't coredump just because no active rL's */
	{
	    mzBoundsIncrement = 100;
	}
	else
	{
	    mzBoundsIncrement = 30*minPitch;
	}
    }
	
    /* Set up (global) bounding rect for route */
    if(mzBoundsHint)
    /* Blockage gen will be confined to user supplied hint (+ 2 units)
     * generate slightly larger bounds for other purposes (estimate generation
     * and marking of tiles connected to dest nodes, for example) to avoid
     * edge effects.
     */
    {
	mzBoundingRect = *mzBoundsHint;

	mzBoundingRect.r_xbot -= 2*mzContextRadius;
	mzBoundingRect.r_ybot -= 2*mzContextRadius;
	mzBoundingRect.r_xtop += 2*mzContextRadius;
	mzBoundingRect.r_ytop += 2*mzContextRadius;
    }
    else
    /* No user supplied bounds, shrink maximum paintable rect by a
     * conservative amount (to avoid overflow during blockage
     * plane generation etc.)
     */
    {
	int maxWidth, maxSpacing, safeHalo;
	RouteType *rT;

	mzBoundingRect = TiPlaneRect;

	maxWidth = 0;
	maxSpacing = 0;
	for (rT = mzRouteTypes; rT!=NULL; rT=rT->rt_next)
	{   
	    int i;

	    maxWidth = MAX(maxWidth,rT->rt_width);
	    for(i=0;i<TT_MAXTYPES+1;i++)
		maxSpacing = MAX(maxSpacing,rT->rt_spacing[i]);
	}
	safeHalo = 3 * (maxSpacing + maxWidth + 2);

	mzBoundingRect.r_xbot += safeHalo;
	mzBoundingRect.r_xtop -= safeHalo;
	mzBoundingRect.r_ybot += safeHalo;
	mzBoundingRect.r_ytop -= safeHalo;

	ASSERT(mzBoundingRect.r_xbot < mzBoundingRect.r_xtop, 
	       "mzComputeDerivedParms");
	ASSERT(mzBoundingRect.r_ybot < mzBoundingRect.r_ytop, 
	       "mzComputeDerivedParms");
    }
}

int mzMakeEndpoints;  /* Set to MZ_EXPAND_START, MZ_EXPAND_DEST, or
		       * MZ_EXPAND_NONE.
		       */
/*
 * ----------------------------------------------------------------------------
 *
 * mzMarkConnectedTiles --
 *
 * Marks tiles connected to given area on given layer in mask data.
 * Used to mark tiles that are part of start or dest nodes.
 *
 * If mzExpandEndpoints is set, also adds dest areas for connected tiles.
 * 
 * Results:
 *	none.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

void
mzMarkConnectedTiles(rect, type, expandType)
    Rect *rect;
    TileType type;
    int expandType;
{
    List *expandList = NULL;	/* areas remaining to be expanded from */
   
    /* set global controlling the creation of dest areas for each connected
     * tile found.
     */
    mzMakeEndpoints = expandType;

    /* Create colored rect corresponding to passed args and initial
     *  expandList with it.
     */
    {
	ColoredRect *e;
	e = (ColoredRect *) mallocMagic((unsigned)(sizeof(ColoredRect)));
	e->cr_type = type;
	e->cr_rect = *rect;
	LIST_ADD(e, expandList);
    }
    
    /* repeatedly expand from top area on expandList */
    while(expandList)
    {
	ColoredRect *e = (ColoredRect *) LIST_FIRST(expandList);
	SearchContext scx;
	TileTypeBitMask typeMask;
	int mzConnectedTileFunc();
	
	/* Restrict marking to route bounds */
	if(GEO_OVERLAP(&mzBoundingRect, &(e->cr_rect)))
	{
	    /* Set search area to colored rect */
	    scx.scx_trans = GeoIdentityTransform;
	    scx.scx_use = mzRouteUse;
	    scx.scx_area = e->cr_rect;

	    /* Grow search area by one unit in each direction so that in
	     * addition to overlapping 
	     * tiles we also get those that just touch it.
	     */	    
	    scx.scx_area.r_xbot -= 1;
	    scx.scx_area.r_ybot -= 1;
	    scx.scx_area.r_xtop += 1;
	    scx.scx_area.r_ytop += 1;

	    /* Build type mask with just the type of e set */
	    TTMaskSetOnlyType(&typeMask, e->cr_type);

	    /* search for connecting tiles, mark them, and add them to
	     * expandList (They are inserted 
	     * AFTER the first (= current) element.)
	     */
	    (void) 
	    DBTreeSrTiles(
			  &scx, 
			  &(DBConnectTbl[e->cr_type]), /* enumerate all 
							* tiles of connecting 
							* types */ 
			  mzCellExpansionMask,
			  mzConnectedTileFunc, 
			  (ClientData) expandList);
	}

	/* Done processing top element of expandList, toss it */ 
	e = (ColoredRect *) ListPop(&expandList);
	freeMagic((char *) e);
    }


    /* mark unexpanded subcells intersecting dest area */
    {
	if(mzCellExpansionMask != 0)
	{
	    int mzConnectedSubcellFunc();
	    SearchContext scx;

	    scx.scx_trans = GeoIdentityTransform;
	    scx.scx_use = mzRouteUse;
	    scx.scx_area = *rect;

	    /* clip area to bounding box to avoid overflow during transforms */
	    GEOCLIP(&(scx.scx_area),&(mzRouteUse->cu_def->cd_bbox));

	    /* clip to route bounds for performance */
	    GEOCLIP(&(scx.scx_area),&mzBoundingRect);

	    DBTreeSrCells(
			  &scx,
			  mzCellExpansionMask,
			  mzConnectedSubcellFunc,
			  (ClientData) NULL);
	}
    }

    return;
}


/*
 * ---------------------------------------------------------------------
 *
 * mzConnectedTileFunc --
 *
 * Called by MZAddDest to mark mask data tile connected to a dest terminal.

 *
 * Results:
 *	Always returns 0 to continue search.
 *
 * Side effects:
 *	Mark clientdata fileds of connected tiles.
 *	Add freshly marked tiles to expandList (for recursive expansion).
 *      If mzExpandEndpoints is set, also adds dest areas for connected
 *	tiles.
 *
 * ---------------------------------------------------------------------
 */

int
mzConnectedTileFunc(tile, cxp)
    Tile *tile;
    TreeContext *cxp;
{
    /* If tile not marked, mark it, add it to marked list, and add
     * corresponding area to expand list.  Mark start tiles MZ_EXPAND_START
     * and destination tiles MZ_EXPAND_DEST so that we don't have to run
     * the tile cleanup routing (in MZClean()) unnecessarily between
     * MZAddStart() and MZAddDest().
     */

    if ((int)tile->ti_client != mzMakeEndpoints)
    {
	SearchContext *scx = cxp->tc_scx;
	List *expandList = (List *) (cxp->tc_filter->tf_arg);
	Rect rRaw, r;
    
	/* Get bounding box of tile */
	TITORECT(tile, &rRaw);
	GEOTRANSRECT(&scx->scx_trans, &rRaw, &r);

	/* mark tile with destination type */
	tile->ti_client = (ClientData) mzMakeEndpoints;

	/* Add tiles connected to Start to mzStartTerms */
	/* (Added by Tim, August 2006)			*/

	if (mzMakeEndpoints == MZ_EXPAND_START)
	{
	    ColoredRect *newTerm;
	    extern List *mzStartTerms;

            newTerm = (ColoredRect *) mallocMagic((unsigned)(sizeof(ColoredRect)));
            newTerm->cr_rect = r;
            newTerm->cr_type = TiGetType(tile);
            LIST_ADD(newTerm, mzStartTerms);
	}

	/* Add dest area (if appropriate).  Don't paint contact types,	*/
	/* or the planes will get fractured up, possibly into areas too	*/
	/* small to place a valid route.				*/

	else if (mzMakeEndpoints == MZ_EXPAND_DEST)
	{
	    RouteLayer *rL;
	    TileType ttype = TiGetType(tile);
		
	    for(rL=mzRouteLayers; rL!=NULL; rL=rL->rl_next)
	    {
		if (rL->rl_routeType.rt_active && 
		   TTMaskHasType(&(DBConnectTbl[ttype]),
				   rL->rl_routeType.rt_tileType))
		{
		    DBPaint(mzDestAreasUse->cu_def,
			    &r, 
			    rL->rl_routeType.rt_tileType);
		}
	    }
	}

	/* add entry to expandList */
	{
	    ColoredRect *e;

	    /* build colored rect corresponding to tile */
	    e = (ColoredRect *) mallocMagic((unsigned)(sizeof(ColoredRect)));
	    e->cr_type = TiGetType(tile);
	    e->cr_rect = r;

	    /* add new entry to second position of expandList (just
	     * past current entry)
	     */
	    LIST_ADD(e, LIST_TAIL(expandList));
	}
    }

    /* return 0 to continue search */
    return 0;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzConnectedSubcellFunc --
 *
 * Called by MZAddDest to mark subcells overlapping a dest terminal.
 * (The blockage generation code treats marked subcells as same-node geometry,
 * allowing walks and dest areas near them.)
 *
 * Results:
 *	Always returns 0 (to continue search)
 *
 * Side effects:
 *	Mark client field in subcell use, and add use to list of marked 
 *      subcells.
 *
 * ----------------------------------------------------------------------------
 */

int
mzConnectedSubcellFunc(scx, cdarg)
    SearchContext *scx;
    ClientData cdarg;
{
    CellUse *cu = scx->scx_use;

    /* If not already marked, mark celluse and add to marked list */
    if (cu->cu_client == (ClientData)MZ_EXPAND_NONE)
    {
	cu->cu_client = (ClientData)MZ_EXPAND_DEST;
	LIST_ADD(cu, mzMarkedCellsList);
    }

    /* continue search */
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * mzGetContact --
 *
 * Get the RouteContact record that matches the contact type between
 * planes path->rp_rLayer and prev->rp_rLayer,
 *
 * Results:
 *	A pointer to the appropriate RouteContact structure, or NULL if
 *	none exists (which shouldn't happen).
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

RouteContact *
MZGetContact(path, prev)
    RoutePath *path, *prev;
{
    RouteContact *rC;
    List *cL;

    /* Find RouteContact connecting the route layers of path and prev.  */

    for (cL = path->rp_rLayer->rl_contactL;  cL != NULL && 
	    ((RouteContact*)LIST_FIRST(cL))->rc_rLayer1 != prev->rp_rLayer &&
	    ((RouteContact*)LIST_FIRST(cL))->rc_rLayer2 != prev->rp_rLayer;
	    cL = LIST_TAIL(cL));

    ASSERT(cL != NULL, "mzPaintContact");
    rC = ((RouteContact*)LIST_FIRST(cL));

    return rC;
}

/*
 * ----------------------------------------------------------------------------
 *
 * mzPaintContact --
 *
 * Paint a single contact between planes path->rp_rLayer and prev->rp_rLayer,
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates both mask and blockage planes.
 *
 * ----------------------------------------------------------------------------
 */

int
mzPaintContact(path, prev)
    RoutePath *path, *prev;
{
    RouteContact *rC;
    int pNum, pNumC, cWidth;
    Rect r;
    TileType cType;

    rC = MZGetContact(path, prev);

    /* compute the contact tileType */
    cType = rC->rc_routeType.rt_tileType;
    cWidth = rC->rc_routeType.rt_width;

    /* compute bounds of contact tile */
    r.r_ll = path->rp_entry;

    if (path->rp_orient == 'X')
    {
	r.r_xtop = r.r_xbot + cWidth;
	r.r_ytop = r.r_ybot + rC->rc_routeType.rt_length;
    }
    else if (path->rp_orient == 'O')
    {
	r.r_xtop = r.r_xbot + rC->rc_routeType.rt_length;
	r.r_ytop = r.r_ybot + cWidth;
    }
    else	/* Type "C", residues only */
    {
	r.r_xtop = r.r_xbot + cWidth;
	r.r_ytop = r.r_ybot + cWidth;
    }

    /* Paint the contact on all connected mask planes */
    /* (should just let DBPaint() do this. . . ) */

    if (DBIsContact(cType))
    {
	if (path->rp_orient != 'C')
	{
	    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
		if (PlaneMaskHasPlane(DBConnPlanes[cType], pNum))
		    DBPaintPlane(mzResultDef->cd_planes[pNum],
				&r, DBStdPaintTbl(cType, pNum),
				(PaintUndoInfo *) NULL);
	}
	else
	{
	    /* Paint residues, not the contact type, to avoid DRC errors */
	    RouteLayer *rL;

	    rL = rC->rc_rLayer1;
	    DBPaintPlane(mzResultDef->cd_planes[rL->rl_planeNum], &r,
			DBStdPaintTbl(rL->rl_routeType.rt_tileType,
			rL->rl_planeNum), (PaintUndoInfo *)NULL);
	    rL = rC->rc_rLayer2;
	    DBPaintPlane(mzResultDef->cd_planes[rL->rl_planeNum], &r,
			DBStdPaintTbl(rL->rl_routeType.rt_tileType,
			rL->rl_planeNum), (PaintUndoInfo *)NULL);
	}
    }
    return cWidth;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzCopyPath --
 *
 * Copy a RoutePath from the temporary arena into permanent storage
 * allocated via mallocMagic.
 *
 * Results:
 *	Returns a pointer to a copy of the RoutePath passed to us
 *	as an argument.
 *
 * Side effects:
 *	Allocates memory.
 *
 * ----------------------------------------------------------------------------
 */

RoutePath *
mzCopyPath(path)
    RoutePath *path;
{
    RoutePath *newHead, *newPrev, *new;

    newPrev = newHead = (RoutePath *) NULL;
    for (newPrev = NULL; path; newPrev = new, path = path->rp_back)
    {
	new = (RoutePath *) mallocMagic((unsigned)(sizeof (RoutePath)));
	*new = *path;
	if (newPrev) newPrev->rp_back = new;
	if (newHead == NULL) newHead = new;
    }

    return (newHead);
}

/*--------------- Static variables only referenced by RPath allocation
                  and deallocation routines below ----------------------- */

/* First, last, and current RoutePages on list for allocating RoutePaths */
RoutePage *mzFirstPage = NULL;
RoutePage *mzLastPage = NULL;
RoutePage *mzCurPage = NULL;


/*
 * ----------------------------------------------------------------------------
 *
 * mzAllocRPath --
 *
 * Allocate a new RoutePath from our temporary RoutePath arena.
 *
 * Results:
 *	Returns a pointer to a newly allocated RoutePath.
 *	This RoutePath is NOT allocated directly via
 *	mallocMagic/freeMagic,
 *	but rather via our own temporary mechanism.  It goes away
 *	once mzFreeAllTemp() gets called, so callers that wish to
 *	retain the "best" RoutePath must call mzCopyPath() to
 *	preserve it.
 *
 * Side effects:
 *	May allocate memory.
 *
 * ----------------------------------------------------------------------------
 */

RoutePath *
mzAllocRPath()
{
    /* Skip to next page if this one is full */
    if (mzCurPage && mzCurPage->rpp_free >= PATHSPERSEG)
	mzCurPage = mzCurPage->rpp_next;

    /* If out of pages, allocate a new one */
    if (mzCurPage == NULL)
    {
	mzCurPage = (RoutePage *) mallocMagic((unsigned)(sizeof (RoutePage)));
	mzCurPage->rpp_next = (RoutePage *) NULL;
	mzCurPage->rpp_free = 0;
	if (mzLastPage == NULL)
	    mzFirstPage = mzLastPage = mzCurPage;
	else
	    mzLastPage->rpp_next = mzCurPage, mzLastPage = mzCurPage;
    }

    return (&mzCurPage->rpp_array[mzCurPage->rpp_free++]);
}

/*
 * ----------------------------------------------------------------------------
 *
 * mzFreeAllRPaths --
 *
 * Reset the temporary arena used for allocating RoutePaths.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Any RoutePaths in the arena become available again for
 *	allocation.  Callers wishing to preserve paths must
 *	do so by calling mzCopyPath().
 *
 * ----------------------------------------------------------------------------
 */

void
mzFreeAllRPaths()
{
    RoutePage *rpage;

    for (rpage = mzFirstPage; rpage; rpage = rpage->rpp_next)
    {
	/* Mark page of RoutePaths as being free */
	rpage->rpp_free = 0;

	/* Can stop after processing the last page used in this cycle */
	if (rpage == mzCurPage)
	    break;
    }

    /* Start allocating again from the first page on the list */
    mzCurPage = mzFirstPage;
}

/*
 * ----------------------------------------------------------------------------
 *
 * mzPresent --
 *
 * Predicate, checkes if type corresponding to rL is set in touchingTypes
 * mask.
 *
 * Results:
 *	TRUE if RL in touchingTypes, else FALSE
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool 
mzPresent(rL,touchingTypes)
    RouteLayer *rL;
    TileTypeBitMask *touchingTypes;
{
    List *l;

    /* return true if rL present */
    if(TTMaskHasType(touchingTypes, rL->rl_routeType.rt_tileType))
	return TRUE;

    /* return true if rL present as part of contact */
    for(l=rL->rl_contactL; l!=NULL; l=LIST_TAIL(l))
    {
	RouteContact *rC = (RouteContact *)LIST_FIRST(l);
	if(TTMaskHasType(touchingTypes, rC->rc_routeType.rt_tileType) &&
		(rC->rc_rLayer1==rL || rC->rc_rLayer2==rL))
	    return TRUE;
    }
    
    return FALSE;
}
