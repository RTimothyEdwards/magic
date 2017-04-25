/*
 * mzEstimate.c --
 *
 * Management of tile plane for estimation of remaining cost to completion.
 *
 * Contains code for building estimation plane (just prior to route), and
 * routine for computing estimates (using the estimation plane) during routing.
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
 * EXPLANATION OF ESTIMATION CODE:
 * The purpose of the estimation code is to estimate cost to completion 
 * from any point to the destintation taking into account the need to 
 * detour around major obstactles such as subcells and fences.
 *
 * The estimation plane permits multiple destination area (routes are
 * made to what ever area is easiest to reach).  The mzrouter also permits
 * multiple start points of course.
 * 
 * To achieve this purpose, prior to beginning the search for a route an
 * estimation plane is generated with info that allows quick estimation
 * of cost to completion during routing.  
 *
 * The estimation plane contains
 * ``solid'' tiles for major obstacles (subcells and fences) and space
 * tiles.  The space tiles are split on the extensions of solid tile edges
 * so that one can always get from a solid tile corner straight out to
 * the next blocking solid tile along tile edges.  Space tiles are also split
 * outward in each direction from the destination point.
 *
 * Estimators are generated for each tile in the estimation plane that allow
 * estimates to be made for points in that tile.  An estimator consists
 * of five numbers: 
 *     (x0,y0,hcost,vcost,cost0) 
 * that are used in the following formula:
 *     EstCost = (x - x0)*hcost + (y -y0)*vcost + cost0.
 * An estimator represents the approximate cost of a path beginning at any
 * point (x,y) in the current tile and proceedings horizontally and then 
 * vertically
 * (or vice versa) to the tile corner (x0,y0) and then following the cheapest
 * path along tile-edges to a destination area.  The cheapest path from 
 * (x0,y0) is precomputed and has cost cost0.  Currently only
 * tile corners (x0,y0) of the tile the estimator is attached to are used
 * for estimators.  Estimators are also generated for paths from edges of
 * a tile straight across to the nearest dest area (with out jogs).  These
 * "straight shot" estimators have zero hcost or vcost.
 * Several estimators are attached to each
 * tile, and the least cost one is used for any given (x,y). 
 *
 * The estimation plane is generated in the following steps:
 *
 *     1. Generate solid tiles for unexpanded subcells (if subcells
          can not be routed across on any active layer)  and fences.
 *
 *     2. Split space tiles along extended edges of solid tiles and 
 *        destination areas.
 *
 *     3. Assign a horizontal and vertical cost for routing in each tile.
 *        Cost for space tiles is cost of min active route-layer, cost for
 *        solid tiles is INFINITY.  (could be more sophisticated - e.g.
 *        if routing is allowed over subcells on an expensive layer.)
 *
 *     4. Apply djikstra's shortest path algorithm to graph whose vertices
 *        are tile-corners and whose edges are tile edges.  Weight on
 *        hor edges is min hor cost of the two adjacent tiles, weight on
 *        vertical edges is min of vert costs of adjacent tiles.  Djikstra's
 *        algor. computes least cost path along tile-edges to
 *        destination for all tile corners 
 *        (e.g. it generates all the cost0's for the estimators).
 *
 *     5. Build estimators for each tile - currently one estimator is built
 *        for each corner of the tile.
 *
 *     6. Trim away redundant estimators - currently if an estimator e0 is
 *        always cheaper than a second e1, e1 is thrown away.
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/mzrouter/mzEstimate.c,v 1.2 2008/06/01 18:37:44 tim Exp $";
#endif  /* not lint */

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

/* largest finite tile plane coordinates, need to reduce INFINITY and MINFINITY
 * defined in tile.h because of some funny buisness off at infinity?
 */

/* Special case mips machines because of a compiler bug, as of 8/9/89 */
#ifdef mips
static int MAX_FINITE_COORDINATE = (INFINITY-10);
static int MIN_FINITE_COORDINATE = (MINFINITY+10);
#else
#define MAX_FINITE_COORDINATE	(INFINITY-10)
#define MIN_FINITE_COORDINATE	(MINFINITY+10)
#endif

/*----------- Vertex structure for shortest path algorithm ---------------*/
typedef struct vertex
{
    int vx_status;	/* which corner in tile, IN set when min distance
			 * to vertex determined (shortest path algor) */
    Tile *vx_tile;	/* tile vertex is attached to */
    dlong vx_cost;	/* Min cost from here to destination point */
} Vertex;

#define VX_CORNER	7
#define VX_NONE		0
#define VX_L_LEFT	1
#define VX_U_LEFT	2
#define VX_L_RIGHT	4
#define VX_IN		8

/* Estimators for estimating cost from point within tile, (x,y), along a 
 * certain path to destination.
 *
 * EstCost = (x - x0) * hCost + (y - y0) * vCost + cost0
 */
typedef struct estimate
{
    int e_x0;
    int e_y0;
    dlong e_cost0;
    int e_hCost;
    int e_vCost;
    struct estimate *e_next;
} Estimate;

/* --------------- tileCosts structure --------------------------------------*/
/* One of these is associated with each tile in the mzEstimate plane.  They
 * are pointed to by the client fields of the tiles.
 */
typedef struct tileCosts
{
    int	tc_hCost;	/* horizontal cost / unit distance  */
    int tc_vCost;	/* vertical cost / unit distance */
    Vertex tc_vxLLeft; /* Vertices corresponding to 3 corners of this tile */
    Vertex tc_vxULeft;
    Vertex tc_vxLRight;
    Estimate *tc_estimates;	/* path estimates for point in this tile */
} TileCosts;

/*---------------- static data, local to this file --------------------------*/
bool mzEstimateExists = FALSE;	/* Set on first call to mzBuildEstimate */

/* Forward declarations */
extern void mzCleanEstimate();
extern void mzBuildCornerEstimators();
extern void mzBuildStraightShotEstimators();
extern void mzSplitTiles();
extern void mzAssignVertexCosts();
extern void mzAddVertex();


/*
 * ----------------------------------------------------------------------------
 *
 * mzBuildEstimate --
 * 	Setup contents of Estimation plane.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	Add tiles to mzEstimate plane, create a tileCost struc for each tile
 *	complete with estimates.
 *
 * ----------------------------------------------------------------------------
 */

void
mzBuildEstimate()
{
    /* Clear estimation plane, reclaiming storage */
    if(mzEstimateExists)
    {
	mzCleanEstimate();
    }
    mzEstimateExists = TRUE;	/* Set flag, so we know to clean next time */

    /* Now build the estimate plane from scratch */
    if(mzEstimate)
    {
	bool subcellsOpaque;

	/* determine whether subcells can be crossed on any active layer */
	{
	    RouteLayer *rL;

	    subcellsOpaque = TRUE;
	    for(rL = mzActiveRLs; 
		rL!=NULL && subcellsOpaque; 
		rL=rL->rl_nextActive)
	    {
		if(rL->rl_routeType.rt_spacing[TT_SUBCELL] < 0)
		{
		    subcellsOpaque = FALSE;
		}
	    }
	}

	/* If over-the-cell routing is not possible,
	 * add a tile to the estimation plane for each unexpanded subcell.
	 *
	 * NOTE: A 0 expansion mask is special cased since 
	 *     the mzrouter interpets a 0 mask to mean all subcells are 
	 *     expanded,
	 *     while DBTreeSrCells() takes a 0 mask to mean all subcells are
	 *     unexpanded.
	 */
	if(mzCellExpansionMask != 0 && subcellsOpaque)
	{
	    int mzAddSubcellEstFunc();
	    SearchContext scx;

	    scx.scx_area = mzBoundingRect;
	    scx.scx_trans = GeoIdentityTransform;
	    scx.scx_use = mzRouteUse;

	    /* clip area to bounding box to avoid overflow during transfroms */
	    GEOCLIP(&(scx.scx_area),&(mzRouteUse->cu_def->cd_bbox));

	    DBTreeSrCells(
			  &scx,
			  mzCellExpansionMask,
			  mzAddSubcellEstFunc,
			  (ClientData) &mzBoundingRect);
	}

	/* If route is OUTSIDE fence, add ``fence'' tiles to estimation plane
	 * for each FENCE tile on fence plane.
	 * If route is INSIDE fence, add ``fence'' tiles for each SPACE tile on
	 * the fence plane.
	 */
	{
	    int mzAddFenceEstFunc();

	    if(mzInsideFence)
	    {

		DBSrPaintArea(NULL, /* no hint tile */
			      mzHFencePlane,
			      &mzBoundingRect,
			      &DBSpaceBits,
			      mzAddFenceEstFunc,
			      (ClientData) &mzBoundingRect);
	    }
	    else
	    {

		DBSrPaintArea(NULL, /* no hint tile */
			      mzHFencePlane,
			      &mzBoundingRect,
			      &DBAllButSpaceBits,
			      mzAddFenceEstFunc,
			      (ClientData) &mzBoundingRect);
	    }
	}
    }

    /* Add a tile to the estimation plane for each dest area, and cut
     * holes at the walks leading to the dest areas
     */
    {
	int mzProcessDestEstFunc();
	SearchContext scx;

	scx.scx_area = mzBoundingRect;
	scx.scx_trans = GeoIdentityTransform;
	scx.scx_use = mzDestAreasUse;

	/* clip area to bounding box to avoid overflow during transforms */
	GEOCLIP(&(scx.scx_area),&(mzDestAreasUse->cu_def->cd_bbox));
	
	(void) DBTreeSrTiles(
			     &scx, 
			     &DBAllButSpaceAndDRCBits, 
			     0, 
			     mzProcessDestEstFunc, 
			     (ClientData) NULL);
	}

    /*--- Split space tiles at edges of solid tiles. ---*/
    {
	int mzBuildSolidsListFunc();
	List *solidsList = NULL;
	List *l;

	/* Build list of all solid, 
	 * i.e. nonspace, tiles on estimation plane. */
        DBSrPaintArea(NULL,         /* no hint tile */
	   mzEstimatePlane,
	   &TiPlaneRect,	    /* max paintable rect. */
	   &DBAllButSpaceBits,
	   mzBuildSolidsListFunc,
	   (ClientData) &solidsList);

	/* Split tiles along perpendiculars of solid tile corners. */
	for(l=solidsList; l!=NULL; l = LIST_TAIL(l))
	{
	    Tile *solid = (Tile *) LIST_FIRST(l);
	    Point p; 

	    /* lower left corner */
	    mzSplitTiles(mzEstimatePlane,&(solid->ti_ll));

	    /* top left corner */
	    p.p_x = LEFT(solid);
	    p.p_y = TOP(solid);
	    mzSplitTiles(mzEstimatePlane,&p);

	    /* top right corner */
	    p.p_x = RIGHT(solid);
	    mzSplitTiles(mzEstimatePlane,&p);

	    /* bottom right corner */
	    p.p_y = BOTTOM(solid);
	    mzSplitTiles(mzEstimatePlane,&p);
	}

	/* Free up solids list */
	ListDealloc(solidsList);
    }

    /* Assign costs to tiles in estimation plane:
     *     Dest tiles - 0 cost.
     *     Space tiles - min active costs.
     *     Fence tiles - infinite cost.
     *     Subcell tiles - infinite cost.
     */
    {
	int mzAssignCostsFunc();
	TileCosts spaceCosts;
	RouteLayer *rL;

	/* set space costs to min costs of active layers */
	spaceCosts.tc_hCost = INT_MAX;
	spaceCosts.tc_vCost = INT_MAX;
	for(rL = mzRouteLayers; rL!=NULL; rL=rL->rl_next)
	{
	    if(rL->rl_routeType.rt_active)
	    {
		if(rL->rl_hCost < spaceCosts.tc_hCost)
		    spaceCosts.tc_hCost = rL->rl_hCost;
		if(rL->rl_vCost < spaceCosts.tc_vCost)
		    spaceCosts.tc_vCost = rL->rl_vCost;
	    }
        }

	/* visit all tiles in estimate plane attaching cost structures 
	 * (including vertices) to the client fields.  Horizontal and
	 * vertical costs are assigned.
	 */
        DBSrPaintArea(NULL,         /* no hint tile */
	   mzEstimatePlane,
	   &TiPlaneRect,
	   &DBAllTypeBits,
	   mzAssignCostsFunc,
	   (ClientData) &spaceCosts);
    }

    /* Apply djikstra shortest path algorithm to determine minimum costs
     * to vertices in tile edge graph.
     */
    mzAssignVertexCosts();

    /* Build cost estimates for each tile in estimate plane */
    {
	int mzBuildEstimatesFunc();

	DBSrPaintArea(NULL,         /* no hint tile */
		      mzEstimatePlane,
		      &TiPlaneRect,
		      &DBAllTypeBits,
		      mzBuildEstimatesFunc,
		      (ClientData) NULL);
    }

    /* Trim away redundant cost estimates on tiles */
    {
	int mzTrimEstimatesFunc();

	DBSrPaintArea(NULL,         /* no hint tile */
		      mzEstimatePlane,
		      &TiPlaneRect,
		      &DBAllTypeBits,
		      mzTrimEstimatesFunc,
		      (ClientData) NULL);
    }

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzCleanEstimate --
 *
 * Clear estimate plane and reclaim storage.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	See Above.
 *
 * ----------------------------------------------------------------------------
 */

void
mzCleanEstimate()
{
    if (mzEstimateExists)
    {
	int mzReclaimTCFunc();

	SigDisableInterrupts();	/* Make atomic so we don't forget to reclaim
				 * anything nor reclaim it twice.
				 */

	/* visit all tiles in estimate plane reclaiming attached
         * cost structures.
	 */
        DBSrPaintArea(NULL,         /* no hint tile */
	   mzEstimatePlane,
	   &TiPlaneRect,	    /* max paintable rect */
	   &DBAllTypeBits,
	   mzReclaimTCFunc,
	   (ClientData) NULL);	

        DBClearPaintPlane(mzEstimatePlane);

	mzEstimateExists = FALSE;

	SigEnableInterrupts();
    }

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzReclaimTCFunc --
 *
 * Free TileCost struc (prior to erasing old estimate plane.)
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Frees TileCost pointed to by client field in tile struc.  Clears
 *      client field pointer - just a precaution.
 *
 * ----------------------------------------------------------------------------
 */

int
mzReclaimTCFunc(tile, notUsed)
    Tile *tile;
    ClientData notUsed;
{
    if (tile->ti_client != (ClientData)CLIENTDEFAULT)
    {
	TileCosts *tc = ((TileCosts *) (tile->ti_client));
	Estimate *e;
	
	/* free estimates attached to tilecosts struc */
	for(e=tc->tc_estimates; e!=NULL; e=e->e_next)
	{
	    freeMagic((char *) e);
	}
	
	/* free tilecosts struc */
	freeMagic((char *) (tile->ti_client));

	/* reset client field in tile */
	tile->ti_client = ((ClientData) CLIENTDEFAULT);
    }

    /* return 0 - to continue traversal of old estimate plane */
    return 0;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzProcessDestEstFunc --
 *
 * Filter function called via DBTreeSrTiles on behalf of mzBuildEstimate()
 * above, for each dest area in the area of interest.   Searches blockage
 * planes for dest tiles and walks under dest area and paints corresponding
 * tiles in estimation plane.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Paints into mzEstimatePlane
 *
 * ----------------------------------------------------------------------------
 */

int
mzProcessDestEstFunc(tile, cxp)
    Tile *tile;
    TreeContext *cxp;
{
    SearchContext *scx = cxp->tc_scx;
    TileType type = TiGetType(tile);
    RouteType *rT;
    Rect r, rect;

    /* Transform to result coordinates */
    TITORECT(tile, &r);
    GEOTRANSRECT(&scx->scx_trans, &r, &rect);

    /* Grow rect by max walk size in all directions so we find walks as
     * well as the dest area.
     */
    rect.r_xbot -= mzMaxWalkLength;
    rect.r_ybot -= mzMaxWalkLength;
    rect.r_xtop += mzMaxWalkLength;
    rect.r_ytop += mzMaxWalkLength;

    /* find route type for this dest area */
    {
	rT = mzActiveRTs;
	while ((rT->rt_tileType != type) && (rT!=NULL))
	{
	    rT = rT->rt_nextActive;
	}
	ASSERT(rT!=NULL,"mzAddDestTileEstFunc");
    }

    /* process dest and walk tiles below dest area */
    {
	int mzDestTileEstFunc();
	TileTypeBitMask destMask;
	
	TTMaskSetOnlyType(&destMask, TT_DEST_AREA);
	TTMaskSetType(&destMask, TT_LEFT_WALK);
	TTMaskSetType(&destMask, TT_RIGHT_WALK);
	TTMaskSetType(&destMask, TT_TOP_WALK);
	TTMaskSetType(&destMask, TT_BOTTOM_WALK);

	DBSrPaintArea(NULL,	/* no hint tile */
		      rT->rt_hBlock,
		      &rect, 
		      &destMask,
		      mzDestTileEstFunc, 
		      (ClientData) NULL);
    }

    return 0;
}



/*
 * ----------------------------------------------------------------------------
 *
 * mzDestTileEstFunc --
 *
 * Paint dest area into estimate plane, or cut whole over walks.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Modifies estimate plane.
 *
 * ----------------------------------------------------------------------------
 */

int
mzDestTileEstFunc(tile, cdarg)
    Tile *tile;
    ClientData cdarg;
{
    Rect rect;

    /* set rect to bounding box of tile */
    TITORECT(tile, &rect);

    if(TiGetType(tile)==TT_DEST_AREA)
    /* paint dest area into estimate plane */
    {
    	DBPaintPlane(mzEstimatePlane, 
		     &rect,
		     mzEstimatePaintTbl[TT_EST_DEST],
		     (PaintUndoInfo *) NULL);
    }
    else
    /* cut hole for walk in estimate plane */
    {
	DBPaintPlane(mzEstimatePlane, 
		     &rect,
		     mzEstimatePaintTbl[TT_SPACE],
		     (PaintUndoInfo *) NULL);
    }

    return 0;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzAddSubcellEstFunc --
 *
 * Filter function called via DBTreeSrTiles on behalf of mzBuildEstimate()
 * above, for each unexpanded subcell in the area of interest, 
 * a TT_EST_SUBCELL tile is painted on each estimate plane for
 * the bounding box of the subcell.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Paints into mzEstimatePlane
 *
 * ----------------------------------------------------------------------------
 */

int
mzAddSubcellEstFunc(scx, cdarg)
    SearchContext *scx;
    ClientData cdarg;
{
    Rect r, rDest;

    /* Transform bounding box to result coords */
    r = scx->scx_use->cu_def->cd_bbox;
    GEOTRANSRECT(&scx->scx_trans, &r, &rDest);

    /* paint subcell block onto estimate plane */
    DBPaintPlane(mzEstimatePlane, 
	&rDest, 
	mzEstimatePaintTbl[TT_EST_SUBCELL],
	(PaintUndoInfo *) NULL);

    /* continue search */
    return (0);
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzAddFenceEstFunc --
 *
 * Filter function called via DBSrPaintArea on behalf of mzBuildEstimate()
 * above, for each fence tile in the area of interest, 
 * a TT_EST_FENCE tile is painted on the estimate plane for
 * each nonspace tile on the fence plane.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Paints into mzEstimatePlane
 *
 * ----------------------------------------------------------------------------
 */

int
mzAddFenceEstFunc(tile, buildArea)
    Tile *tile;
    Rect *buildArea; /* currently ignored */
{
    Rect r;
 
    /* Get boundary of tile */
    TITORECT(tile, &r);

    /* paint fence into estimate plane */
    DBPaintPlane(mzEstimatePlane, 
	&r, 
	mzEstimatePaintTbl[TT_EST_FENCE],
	(PaintUndoInfo *) NULL);

    /* continue search */
    return (0);
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzBuildSolidsList --
 *
 * Called by DBSrPaintArea for each solid tile in estimation plane
 * Creates list of these tiles.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Adds to list passed as arg.
 *
 * ----------------------------------------------------------------------------
 */

int
mzBuildSolidsListFunc(tile, listPtr)
    Tile *tile;
    List **listPtr; /* pointer to list to add tile to */
{
    LIST_ADD(tile,*listPtr);

    /* return 0 - to continue search */
    return(0);
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzAssignCostsFunc --
 *
 * Assigns horizontal and vertical costs to tiles in estimate plane.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Adds to list passed as arg.
 *
 * ----------------------------------------------------------------------------
 */
int
mzAssignCostsFunc(tile, spaceCosts)
    Tile *tile;
    TileCosts *spaceCosts; /* costs to assign to space tiles */
{
    Tile *tRight, *tUp;
    TileCosts *newCosts;
    Vertex *v;

    /* Alloc TileCosts struc for this tile, and attach it */
    newCosts = (TileCosts *) mallocMagic((unsigned) (sizeof(TileCosts)));
    tile->ti_client = (ClientData) newCosts;

    /* Assign hor and vert costs for tile */
    switch(TiGetType(tile))
    {
	case TT_EST_DEST:
	newCosts->tc_hCost = 0;
	newCosts->tc_vCost = 0;
	break;

	case TT_SPACE:
        *newCosts = *spaceCosts;
	break;

	case TT_EST_FENCE:
	case TT_EST_SUBCELL:
	newCosts->tc_hCost = INT_MAX;
	newCosts->tc_vCost = INT_MAX;
	break;
	
	default:
	/* unrecognized tile type */
	ASSERT(FALSE,"mzAssignCostsFunc");
    }

    /* add lower-left vertex */
    v = &(newCosts->tc_vxLLeft);
    v->vx_status = VX_L_LEFT;
    v->vx_tile = tile;
    v->vx_cost = COST_MAX;

    /* add lower-right vertex, if at 'T' */
    NEXT_TILE_RIGHT(tRight, tile, BOTTOM(tile));
    if (BOTTOM(tRight)!=BOTTOM(tile))
    {
	/* lower-right vertex at 'T', so add it */
	v = &(newCosts->tc_vxLRight);
	v->vx_status = VX_L_RIGHT;
	v->vx_tile = tile;
	v->vx_cost = COST_MAX;
    }
    else
    {	
	/* no 'T' */
	newCosts->tc_vxLRight.vx_status = VX_NONE;
    }

    /* add upper-left vertex, if at 'T' */
    NEXT_TILE_UP(tUp, tile, LEFT(tile));
    if (LEFT(tUp)!=LEFT(tile))
    {
	/* upper-left vertex at 'T', so add it */
	v = &(newCosts->tc_vxULeft);
	v->vx_status = VX_U_LEFT;
	v->vx_tile = tile;
	v->vx_cost = COST_MAX;
    }
    else
    {	
	/* no 'T' */
	newCosts->tc_vxULeft.vx_status = VX_NONE;
    }

    /* initial estimates to NULL list */
    newCosts->tc_estimates = NULL;

    /* return 0 - to continue traversal of estimate plane */
    return(0);
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzDestInitialAssignFunc
 *
 * Add one vertex of dest tile to adjacency heap to initialize graph for
 * Djikstra's shortest path computation.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Initials cost to zero and adds a vertex to adjacency heap.
 *
 * ----------------------------------------------------------------------------
 */
int
mzDestInitialAssignFunc(tile, cdarg)
    Tile *tile;
    ClientData cdarg;
{
    Heap *adjHeap = (Heap *) cdarg;
    Vertex *v;

    /* get lower left vertex */
    v = &(((TileCosts *)(tile->ti_client))->tc_vxLLeft);

    /* cost from dest is zero */
    v->vx_cost = 0;

    /* add vertex to adjHeap */
    HeapAddDLong(adjHeap, 0, (char *) v);

    /* return 0 - to continue search */
    return(0);
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzBuildEstimatesFunc --
 *
 * Build path estimates for this tile.
 * (For now builds 
 *    + one estimate for each corner of the tile.
 *    + one estimate for no jog path to destination in each direction where
 *      this is possible.)
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Adds estimates to TileCost struc attached to tile.
 *
 * ----------------------------------------------------------------------------
 */

int
mzBuildEstimatesFunc(tile, notUsed)
    Tile *tile;
    ClientData notUsed;
{

    mzBuildCornerEstimators(tile);
    mzBuildStraightShotEstimators(tile);

    /* return 0 - to continue traversal of estimate plane */
    return(0);
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzBuildCornerEstimators --
 *
 * Build path estimates for paths via corners of this tile.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds estimates to TileCost struc attached to tile.
 *
 * ----------------------------------------------------------------------------
 */

void
mzBuildCornerEstimators(tile)
    Tile *tile;
{
    TileCosts *tc = (TileCosts *) (tile->ti_client);
    Vertex *vLLeft = NULL; 
    Vertex *vULeft = NULL; 
    Vertex *vLRight = NULL;
    Vertex *vURight = NULL;

    /* find vertex strucs corresponding to four corners. 
     *  (NULL, for corners at infinity)
     */
    {
	Tile *tUp, *tRight, *tDiag;
	Tile *tRT, *tTR;

	if(LEFT(tile)>=MIN_FINITE_COORDINATE)
	{
	    if(BOTTOM(tile)>=MIN_FINITE_COORDINATE)
	    {
		/* Lower Left */
		vLLeft = &(tc->tc_vxLLeft);
	    }

	    if(TOP(tile)<=MAX_FINITE_COORDINATE)
	    {
		/* Upper Left */
		NEXT_TILE_UP(tUp, tile, LEFT(tile));
		if (LEFT(tUp)<LEFT(tile))
		{
		    /* upper-left vertex at 'T', so stored with tile */
		    vULeft = &(tc->tc_vxULeft);
		}
		else
		{
		    /* no 'T', stored with tUp  */
		    vULeft = &(((TileCosts *)(tUp->ti_client))->tc_vxLLeft);
		}
	    }
	}

	if(RIGHT(tile)<=MAX_FINITE_COORDINATE)
	{
	    if(BOTTOM(tile)>=MIN_FINITE_COORDINATE)
	    {
		/* Lower Right */
		NEXT_TILE_RIGHT(tRight, tile, BOTTOM(tile));
		if (BOTTOM(tRight)<BOTTOM(tile))
		{
		    /* lower-right vertex at 'T', so stored with tile */
		    vLRight = &(tc->tc_vxLRight);
		}
		else
		{   
		    /* no 'T', stored with tRight */
		    vLRight = &(((TileCosts *)(tRight->ti_client))->tc_vxLLeft);
		}
	    }

	    if(TOP(tile)<=MAX_FINITE_COORDINATE)
	    {
		/* Upper Right */
		tRT = RT(tile);		/* right top corner stitch (up) */
		tTR = TR(tile);		/* top right corner stitch (to right) */
		if(RIGHT(tRT)>RIGHT(tile))
		{
		    /* upper right at 'T'  stored with tTR */
		    vURight = &(((TileCosts *)(tTR->ti_client))->tc_vxULeft);
		}
		else if (TOP(tTR)>TOP(tile))
		{
		    /* upper right at 'T'  stored with tRT */
		    vURight = &(((TileCosts *)(tRT->ti_client))->tc_vxLRight);
		}
		else 
		{
		    /* no 'T', stored in own tile */
		    NEXT_TILE_UP(tDiag, tTR, RIGHT(tile));
		    vURight = &(((TileCosts *)(tDiag->ti_client))->tc_vxLLeft);
		}
	    }
	}
    }

    /* Build estimates */
    {
        Estimate *e;

	/* Estimate for lower left corner */ 
	if (vLLeft)
	{
	    e = (Estimate *) mallocMagic((unsigned) (sizeof(Estimate)));
	    e->e_x0 = LEFT(tile);
	    e->e_y0 = BOTTOM(tile);
	    e->e_cost0 = vLLeft->vx_cost;
	    e->e_hCost = tc->tc_hCost;
	    e->e_vCost = tc->tc_vCost;
	    e->e_next = tc->tc_estimates;
	    tc->tc_estimates = e;
	}
	    
	/* Estimate for lower right corner */
	if (vLRight)
	{
	    e = (Estimate *) mallocMagic((unsigned) (sizeof(Estimate)));
	    e->e_x0 = RIGHT(tile);
	    e->e_y0 = BOTTOM(tile);
	    e->e_cost0 = vLRight->vx_cost;
	    e->e_hCost = tc->tc_hCost;
	    e->e_vCost = tc->tc_vCost;
	    e->e_next = tc->tc_estimates;
	    tc->tc_estimates = e;
	}

	/* Estimate for upper right corner */
	if (vURight)
	{
	    e = (Estimate *) mallocMagic((unsigned) (sizeof(Estimate)));
	    e->e_x0 = RIGHT(tile);
	    e->e_y0 = TOP(tile);
	    e->e_cost0 = vURight->vx_cost;
	    e->e_hCost = tc->tc_hCost;
	    e->e_vCost = tc->tc_vCost;
	    e->e_next = tc->tc_estimates;
	    tc->tc_estimates = e;
	}

	/* Estimate for upper left corner */
	if (vULeft)
	{
	    e = (Estimate *) mallocMagic((unsigned)(sizeof(Estimate)));
	    e->e_x0 = LEFT(tile);
	    e->e_y0 = TOP(tile);
	    e->e_cost0 = vULeft->vx_cost;
	    e->e_hCost = tc->tc_hCost;
	    e->e_vCost = tc->tc_vCost;
	    e->e_next = tc->tc_estimates;
	    tc->tc_estimates = e;
	}
    }

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzBuildStraightShotEstimators --
 *
 * Build path estimates for paths straight to dest area (no jogs)
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds estimates to TileCost struc attached to tile.
 *
 * ----------------------------------------------------------------------------
 */
void
mzBuildStraightShotEstimators(tile)
    Tile *tile;
{
    TileCosts *tc = (TileCosts *) (tile->ti_client);

    /* straight right */
    {
	Tile *tSolid = tile;
	
	/* get first solid tile to right */
	while(TiGetType(tSolid)==TT_SPACE && 
	      tSolid!=mzEstimatePlane->pl_right)
	{
	    tSolid = TR(tSolid);
	}

	/* if dest tile, build estimator */
	if(TiGetType(tSolid) == TT_EST_DEST)
	{
	    Estimate *e;

	    e = (Estimate *) mallocMagic((unsigned)(sizeof(Estimate)));
	    e->e_x0 = RIGHT(tile);
	    e->e_y0 = 0;
	    if (tc->tc_hCost == INT_MAX)
		e->e_cost0 = COST_MAX;
	    else
	        e->e_cost0 = (dlong) (LEFT(tSolid) - RIGHT(tile)) * tc->tc_hCost;
	    e->e_hCost = tc->tc_hCost;
	    e->e_vCost = 0;
	    e->e_next = tc->tc_estimates;
	    tc->tc_estimates = e;
	}
    }

    /* straight left */
    {
	Tile *tSolid = tile;
	
	/* get first solid tile to left */
	while(TiGetType(tSolid)==TT_SPACE && 
	      tSolid!=mzEstimatePlane->pl_left)
	{
	    tSolid = BL(tSolid);
	}

	/* if dest tile, build estimator */
	if(TiGetType(tSolid) == TT_EST_DEST)
	{
	    Estimate *e;

	    e = (Estimate *) mallocMagic((unsigned)(sizeof(Estimate)));
	    e->e_x0 = LEFT(tile);
	    e->e_y0 = 0;
	    if (tc->tc_hCost == INT_MAX)
		e->e_cost0 = COST_MAX;
	    else
		e->e_cost0 = (dlong) (RIGHT(tSolid) - LEFT(tile)) * tc->tc_hCost;
	    e->e_hCost = tc->tc_hCost;
	    e->e_vCost = 0;
	    e->e_next = tc->tc_estimates;
	    tc->tc_estimates = e;
	}
    }

    /* straight up */
    {
	Tile *tSolid = tile;
	
	/* get first solid tile above */
	while(TiGetType(tSolid)==TT_SPACE && 
	      tSolid!=mzEstimatePlane->pl_top)
	{
	    tSolid = RT(tSolid);
	}

	/* if dest tile, build estimator */
	if(TiGetType(tSolid) == TT_EST_DEST)
	{
	    Estimate *e;

	    e = (Estimate *) mallocMagic((unsigned)(sizeof(Estimate)));
	    e->e_x0 = 0;
	    e->e_y0 = TOP(tile);
	    if (tc->tc_vCost == INT_MAX)
		e->e_cost0 = COST_MAX;
	    else
		e->e_cost0 = (dlong) (BOTTOM(tSolid) - TOP(tile)) * tc->tc_vCost;
	    e->e_hCost = 0;
	    e->e_vCost = tc->tc_vCost;
	    e->e_next = tc->tc_estimates;
	    tc->tc_estimates = e;
	}
    }

    /* straight down */
    {
	Tile *tSolid = tile;
	
	/* get first solid tile below */
	while(TiGetType(tSolid)==TT_SPACE && 
	      tSolid!=mzEstimatePlane->pl_bottom)
	{
	    tSolid = LB(tSolid);
	}

	/* if dest tile, build estimator */
	if(TiGetType(tSolid) == TT_EST_DEST)
	{
	    Estimate *e;

	    e = (Estimate *) mallocMagic((unsigned)(sizeof(Estimate)));
	    e->e_x0 = 0;
	    e->e_y0 = BOTTOM(tile);
	    if (tc->tc_vCost == INT_MAX)
		e->e_cost0 = COST_MAX;
	    else
		e->e_cost0 = (dlong)(TOP(tSolid) - BOTTOM(tile)) * tc->tc_vCost;
	    e->e_hCost = 0;
	    e->e_vCost = tc->tc_vCost;
	    e->e_next = tc->tc_estimates;
	    tc->tc_estimates = e;
	}
    }

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * AlwaysAsGood --
 *
 * Compares two estimators.
 *
 * Results:
 *	Returns TRUE iff est1 is always less than or equal to est2.
 *
 * Side effects:
 *	modifies estimates list in TileCost struc attached to tile,
 *      specifically sets floating origin coords.  (Floating coords
 *      are those with corresponding cost field of 0, hence
 *      there value does not matter when computing estimates.  They
 *      are set here as a convience, to permit uniform treatment of 
 *      all estimators within this function.)
 *      
 *      
 *
 * ----------------------------------------------------------------------------
 */

bool
AlwaysAsGood(est1, est2, tile) 
    Estimate *est1;
    Estimate *est2;
    Tile *tile;
{
    if(est1->e_cost0 > est2->e_cost0)
    {
	return FALSE;
    }
    else
    /* check if using est1 even from est2 origin 
     * is cheaper than using est2 */
    {
	/* If est2 x origin is floating, set to worst case */
	if(est2->e_hCost == 0)
	{
	    est2->e_x0 = (ABS(LEFT(tile) - est1->e_x0) > 
			  ABS(RIGHT(tile) - est1->e_x0)) ?
			  LEFT(tile) : RIGHT(tile);
	}

	/* If est2 y origin is floating, set to worst case */
	if(est2->e_vCost == 0)
	{
	    est2->e_y0 = (ABS(BOTTOM(tile) - est1->e_y0) > 
			  ABS(TOP(tile) - est1->e_y0)) ?
			  BOTTOM(tile) : TOP(tile);
	}

	/* now compute the cost from est2 origin using est1 */

	{
 	    dlong hCost, vCost, cost;

	    if ((est1->e_hCost == INT_MAX) || (est1->e_vCost == INT_MAX))
		return FALSE;

	    hCost = (dlong) (est1->e_hCost *
				 ABS(est2->e_x0 - est1->e_x0));
	    vCost = (dlong) (est1->e_vCost *
				 ABS(est2->e_y0 - est1->e_y0));	
	    
	    cost = hCost + vCost;
	    cost += est1->e_cost0;

	    return (cost <= est2->e_cost0);
	}
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzTrimEstimatesFunc --
 *
 * Throw away redundant cost estimates.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	modifies estimates list in TileCost struc attached to tile.
 *
 * ----------------------------------------------------------------------------
 */

int
mzTrimEstimatesFunc(tile, notUsed)
    Tile *tile;
    ClientData notUsed;
{
    TileCosts *tc = (TileCosts *) (tile->ti_client);
    Estimate *e;
    Estimate *reqEstimates = NULL;

    e = tc->tc_estimates;
    while(e)
    {
	Estimate *e2;
	bool found = FALSE;

	/* Check if a required estimate is always as good as e */
	for(e2 = reqEstimates; e2!= NULL && !found; e2=e2->e_next)
	{
	    if(AlwaysAsGood(e2,e,tile))
	    {
		found = TRUE;
	    }
	}

	/* Check if a not-yet-processed estimate is always as good as e */
	for(e2 = e->e_next; e2!= NULL && !found; e2=e2->e_next)
	{
	    if(AlwaysAsGood(e2,e,tile))
	    {
		found = TRUE;
	    }
	}

	/* Throw away e if redundant, else save on reqEstimates list, and
	 * continue with next unprocessed estimate.
	 */
	{
	    Estimate *eNext = e->e_next;
	    if(found)
	    /* Throw away */
	    {
		freeMagic((char *) e);
	    }
	    else
	    /* Add to required list */
	    {
		e->e_next = reqEstimates;
		reqEstimates = e;
	    }
	    e = eNext;
	}
    }

    /* save required estimate list */
    tc->tc_estimates = reqEstimates;

    /* return 0 - to continue traversal of estimate plane */
    return(0);
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzSplitTiles --
 *
 * Split space tiles in four directions from point - stopping at solid tiles.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	Modifies tile structure of plane.
 *
 * ----------------------------------------------------------------------------
 */

void
mzSplitTiles(plane, point)
    Plane * plane;
    Point * point;     /* origin from which tiles split */
{
    Tile *pointTile = TiSrPointNoHint(plane, point);
    Tile *t;
    int x = point->p_x;
    int y = point->p_y;

    /* Don't split from infinite points */
    if(x<MIN_FINITE_COORDINATE || x>MAX_FINITE_COORDINATE ||
       y<MIN_FINITE_COORDINATE || y>MAX_FINITE_COORDINATE)
    {
        return;
    }

    /* split tiles to right of point */
    {
	/* init t to tile to right of pointTile */
	NEXT_TILE_RIGHT(t,pointTile,y);
	
	while (TiGetType(t)==TT_SPACE && BOTTOM(t)!=y && t!=plane->pl_right)
	{
	    /* split t */
	    t=TiSplitY(t, y);

	    /* move one tile to right */
	    NEXT_TILE_RIGHT(t,t,y);
	}
    }

    /* split tiles up from point */
    {
	/* init t to tile above pointTile */
	NEXT_TILE_UP(t,pointTile,x)
	
	while (TiGetType(t)==TT_SPACE && LEFT(t)!=x && t!=plane->pl_top)
	{
	    /* split t */
	    t=TiSplitX(t, x);

	    /* move one tile up */
	    NEXT_TILE_UP(t,t,x);
	}
    }

    /* split tiles to left of point */
    {
	/* init t to tile to left of pointTile */
	NEXT_TILE_LEFT(t,pointTile,y);
	
	while (TiGetType(t)==TT_SPACE && BOTTOM(t)!=y && t!=plane->pl_left)
	{
	    /* split t */
	    t = TiSplitY(t, y);

	    /* move one tile to left */
	    NEXT_TILE_LEFT(t,t,y);
	}
    }

    /* split tiles down from point */
    {
	/* init t to tile below pointTile */
	NEXT_TILE_DOWN(t,pointTile,x);
	
	while (TiGetType(t)==TT_SPACE && LEFT(t)!=x && t!=plane->pl_bottom)
	{
	    /* split t */
	    t=TiSplitX(t, x);

	    /* move one tile down */
	    NEXT_TILE_DOWN(t,t,x);
	}
    }

    /* finally, if point is in a SPACE tile, split it in four */
    if(TiGetType(pointTile)==TT_SPACE)
    {
	t = pointTile;
	if(x != LEFT(t))
	{
	    Tile *tOther = TiSplitX(t, x);
	    if(y != BOTTOM(tOther))
		TiSplitY(tOther, y);
	}
	if(y != BOTTOM(t))
	    TiSplitY(t, y);
    }

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzAssignVertexCosts --
 *
 * Applies Djikstra's shortest path algorithm to compute minimum cost to
 * each tile corner, assuming cost along edge is minimum of cost associated
 * with adjacent tiles times length of the edge.  (Hor costs for hor. edges,
 * vertical costs for vertical edges.)
 *
 * Treats estimate plane as a graph, with tile corners as vertices and
 * tile edges as nodes.  Weights
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Fills in vertex costs in strucs hanging of clientData fields of
 *	tiles in estimation plane.
 *
 * ----------------------------------------------------------------------------
 */

void
mzAssignVertexCosts()
{
    Heap adjHeap;	/* vertices adjacent to the IN set are put here */
    HeapEntry buf, *he;
    Tile *t;

    /* Initialize Heap */
    HeapInitType(&adjHeap, 1024, FALSE, FALSE, HE_DLONG);

    /* Initial at least one vertex of each dest term to zero cost and add
     * to adjHeap.  Zero cost will be propagated to other vertices of dest
     * terms since hcost and vcost are 0 for dest tiles.
     */
    {
	int mzDestInitialAssignFunc();
	TileTypeBitMask destOnly;

	TTMaskSetOnlyType(&destOnly, TT_EST_DEST);
	
        DBSrPaintArea(NULL,         /* no hint tile */
	   mzEstimatePlane,
	   &mzBoundingRect,
	   &destOnly,
	   mzDestInitialAssignFunc,
	   (ClientData) &adjHeap);
    }

    /* keep adding least cost ADJACENT vertex to IN until no ADJACENT vertices
     * left.  (Vertices adjacent to the addvertex are added to adjHeap.)
     */
    while((he = HeapRemoveTop(&adjHeap,&buf))!=NULL)
    {
	Vertex *v = (Vertex *)(he->he_id);
	if (!(v->vx_status & VX_IN))
	{
	    /* vertex not already IN, so process it */
	    mzAddVertex(v,&adjHeap);
	}
    }

    /* Free heap */
    HeapKill(&adjHeap, (void (*)()) NULL);

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzAddVertex --
 *
 * Subroutine of mzAssignVertexCosts.
 * Adds least cost vertex on adjHeap to IN set.  Adds vertices adjacent to
 * new IN vertex to adjHeap, or adjusts there cost if they are already there.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies adjHeap and vertex strucs attached to tiles
 *	on estimation plane.
 *
 * ----------------------------------------------------------------------------
 */

void
mzAddVertex(vxThis, adjHeap)
    Vertex *vxThis;
    Heap *adjHeap;
{
    Tile *tThis;	 /* Tile vxThis is attached to */
    Point loc; 		/* location of vxThis */
    Tile *tLoc;	 	/* Tile containing location of vxThis */
    Tile *tLeft, *tRight, *tAbove, *tBelow;	/* Neighbors of tLoc */

    /* Mark this vertex IN */
    vxThis->vx_status |= VX_IN;

    /* Ignore if we're already at maximum cost */
    if (vxThis->vx_cost == COST_MAX) return;

    /* compute location of this vertex, and tile containing that loc */
    tThis = vxThis->vx_tile;
    switch (vxThis->vx_status & VX_CORNER)
    {
	case VX_L_LEFT:
	    loc.p_x = LEFT(tThis);
	    loc.p_y = BOTTOM(tThis);
	    tLoc = tThis;
	break;

	case VX_L_RIGHT:
	    loc.p_x = RIGHT(tThis);
	    loc.p_y = BOTTOM(tThis);
	    NEXT_TILE_RIGHT(tLoc, tThis, BOTTOM(tThis));
	break;

	case VX_U_LEFT:
	    loc.p_x = LEFT(tThis);
	    loc.p_y = TOP(tThis);
	    NEXT_TILE_UP(tLoc, tThis, LEFT(tThis));
	break;
    }

    /* find tiles neighboring loc */
    NEXT_TILE_LEFT(tLeft, tLoc, loc.p_y);
    NEXT_TILE_RIGHT(tRight, tLoc, loc.p_y);
    NEXT_TILE_UP(tAbove, tLoc, loc.p_x);
    NEXT_TILE_DOWN(tBelow, tLoc, loc.p_x);

    /* process adjacent vertex ABOVE */
    {
	Vertex *vxAbove;
	int yAbove; 

	/* Check for no edge above */
	if(LEFT(tLoc)!=loc.p_x)
	    goto noAbove;

	if(TOP(tLeft) < TOP(tLoc))
	{
	    /* T from left */
	    vxAbove = &(((TileCosts *)(RT(tLeft)->ti_client))->tc_vxLRight);
	    yAbove = TOP(tLeft);
	}
	else
	{
	    if(LEFT(tAbove)==LEFT(tLoc))
	    {
		/* no T */
		vxAbove = &(((TileCosts *)(tAbove->ti_client))->tc_vxLLeft);
		yAbove = BOTTOM(tAbove);
	    }
	    else
	    { 
		/* T from bottom */
		vxAbove = &(((TileCosts *)(tLoc->ti_client))->tc_vxULeft);
		yAbove = BOTTOM(tAbove);
	    }
	}

	/* adjust cost */
	{
	    int rate, distance;
	    dlong newCost;

	    if(yAbove > MAX_FINITE_COORDINATE) goto noAbove;

	    rate =  MIN(((TileCosts *)(tLoc->ti_client))->tc_vCost,
		    ((TileCosts *)(tLeft->ti_client))->tc_vCost);

	    if(rate == INT_MAX) goto noAbove;

	    distance = yAbove - loc.p_y;
	    newCost = (dlong) (rate * distance);
	    newCost += vxThis->vx_cost;

	    if(newCost < vxAbove->vx_cost)
	    {
		vxAbove->vx_cost = newCost;
		HeapAddDLong(adjHeap, newCost, (char *) vxAbove);
	    }
	}
	noAbove:;
    }


    /* process adjacent vertex to RIGHT */
    {
	Vertex *vxRight;
	int xRight;

	/* Check for no edge to RIGHT */
	if(BOTTOM(tLoc)!=loc.p_y)
	    goto noRight;

	if(RIGHT(tBelow) < RIGHT(tLoc))
	{
	    /* T from below */
	    vxRight = &(((TileCosts *)(TR(tBelow)->ti_client))->tc_vxULeft);
	    xRight = RIGHT(tBelow);
	}
	else
	{
	    if(BOTTOM(tRight)==BOTTOM(tLoc))
	    {
		/* no T */
		vxRight = &(((TileCosts *)(tRight->ti_client))->tc_vxLLeft);
		xRight = LEFT(tRight);
	    }
	    else
	    { 
		/* T from left */
		vxRight = &(((TileCosts *)(tLoc->ti_client))->tc_vxLRight);
		xRight = LEFT(tRight);
	    }
	}

	/* adjust cost */
	{
	    int rate, distance;
	    dlong newCost;

	    if(xRight > MAX_FINITE_COORDINATE) goto noRight;

	    rate =  MIN(
		    ((TileCosts *)(tLoc->ti_client))->tc_hCost,
		    ((TileCosts *)(tBelow->ti_client))->tc_hCost);

	    if(rate == INT_MAX) goto noRight;

	    distance = xRight - loc.p_x;
	    newCost = (dlong) (rate * distance);
	    newCost += vxThis->vx_cost;

	    if (newCost < vxRight->vx_cost)
	    {
		vxRight->vx_cost = newCost;
		HeapAddDLong(adjHeap, newCost, (char *) vxRight);
	    }
	}
	noRight:;
    }

    /* For going down and to the left, we want tiles to contain their
     * right and upper edges.  Adjust tLoc and neighbors accordingly.
     * The trick is to center tLoc and neighbors around loc - (1,1).
     */
    {
	Point locMinus;

	/* locMinus used to get tiles for loc, that contain top and right
	 * edges.
	 */

        locMinus = loc;
	--(locMinus.p_x);
	--(locMinus.p_y);

	if(BOTTOM(tLoc)>locMinus.p_y)
	    NEXT_TILE_DOWN(tLoc, tLoc, loc.p_x);
	if(LEFT(tLoc)>locMinus.p_x)
	    NEXT_TILE_LEFT(tLoc, tLoc, locMinus.p_y);

	/* find tiles neighboring loc */
	NEXT_TILE_LEFT(tLeft, tLoc, locMinus.p_y);
	NEXT_TILE_RIGHT(tRight, tLoc, locMinus.p_y);
	NEXT_TILE_UP(tAbove, tLoc, locMinus.p_x);
	NEXT_TILE_DOWN(tBelow, tLoc, locMinus.p_x);
    }

    /* process adjacent vertex BELOW */
    {
	Vertex *vxBelow;
	int yBelow;

	/* Check for no edge below */
	if(RIGHT(tLoc)!=loc.p_x)
	    goto noBelow;

	if(BOTTOM(tRight) >= BOTTOM(tLoc))
	{
	    /* LowerLeft of tRight */
	    vxBelow = &(((TileCosts *)(tRight->ti_client))->tc_vxLLeft);
	    yBelow = BOTTOM(tRight);
	}
	else
	{
	    /* T from Left */
	    vxBelow = &(((TileCosts *)(tLoc->ti_client))->tc_vxLRight);
	    yBelow = BOTTOM(tLoc);
	}

	/* adjust cost */
	{
	    int rate, distance;
	    dlong newCost;

	    if(yBelow < MIN_FINITE_COORDINATE) goto noBelow;

	    rate =  MIN(
		    ((TileCosts *)(tLoc->ti_client))->tc_vCost,
		    ((TileCosts *)(tRight->ti_client))->tc_vCost);

	    if(rate == INT_MAX) goto noBelow;

	    distance = loc.p_y - yBelow;
	    newCost = (dlong) (rate * distance);
	    newCost += vxThis->vx_cost;

	    if(newCost < vxBelow->vx_cost)
	    {
		vxBelow->vx_cost = newCost;
		HeapAddDLong(adjHeap, newCost, (char *) vxBelow);
	    }
	}
	noBelow:;
    }

    /* process adjacent vertex to LEFT */
    {
	Vertex *vxLeft;
	int xLeft;

	/* Check for no edge to Left */
	if(TOP(tLoc)!=loc.p_y)
	    goto noLeft;

	if(LEFT(tAbove) >= LEFT(tLoc))
	{
	    /* LowerLeft of tAbove */
	    vxLeft = &(((TileCosts *)(tAbove->ti_client))->tc_vxLLeft);
	    xLeft = LEFT(tAbove);
	}
	else
	{
	    /* T from Bottom */
	    vxLeft = &(((TileCosts *)(tLoc->ti_client))->tc_vxULeft);
	    xLeft = LEFT(tLoc);
	}

	/* adjust cost */
	{
	    int rate, distance;
	    dlong newCost;

	    if(xLeft < MIN_FINITE_COORDINATE) goto noLeft;

	    rate =  MIN(
		    ((TileCosts *)(tLoc->ti_client))->tc_hCost,
		    ((TileCosts *)(tAbove->ti_client))->tc_hCost);

	    if(rate == INT_MAX) goto noLeft;

	    distance = loc.p_x - xLeft;
	    newCost = (dlong) (rate * distance);
	    newCost += vxThis->vx_cost;

	    if(newCost < vxLeft->vx_cost)
	    {
		vxLeft->vx_cost = newCost;
		HeapAddDLong(adjHeap, newCost, (char *) vxLeft);
	    }
	}
noLeft:;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * mzEstimatedCost --
 *
 * Results:  
 *	Estimated cost of route from point to destination.
 *
 * Side effects:  
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

// changed from DoubleInt to dlong
dlong
mzEstimatedCost(point)
    Point *point;
{
    Tile *t = TiSrPointNoHint(mzEstimatePlane, point);
    TileCosts *tc = ((TileCosts *) t->ti_client);
    Estimate *e;
    dlong bestCost;

    bestCost = COST_MAX;
    for (e=tc->tc_estimates; e!=NULL; e=e->e_next)
    {
	dlong hCost, vCost, cost;

	if (e->e_hCost == INT_MAX || e->e_vCost == INT_MAX) continue;

	hCost = (dlong)e->e_hCost * (dlong)ABS(point->p_x - e->e_x0);
	vCost = (dlong)e->e_vCost * (dlong)ABS(point->p_y - e->e_y0);

	cost = hCost + vCost;
	cost += e->e_cost0;

	if(cost < bestCost)
	    bestCost = cost;
    }

    return bestCost;
}

/*
 * ----------------------------------------------------------------------------
 *
 * mzDumpEstimates -- 
 *
 * Dump info in estimate plane (for debugging).
 *
 * Results:  
 *	None.
 *
 * Side effects:  
 *	info written to file or via TxPrintf()
 *
 * ----------------------------------------------------------------------------
 */

void
mzDumpEstimates(area,fd)
    Rect *area;
    FILE *fd;
{
    int mzDumpEstFunc();

    if(mzEstimateExists)
    {
	/* Visit each tile in the Estimate plane - dumping associated info */
	DBSrPaintArea(NULL,    /* no hint tile */
		      mzEstimatePlane,
		      area,
		      &DBAllTypeBits,
		      mzDumpEstFunc,
		      (ClientData) fd);
    }
    else
    {
	TxPrintf("No estimate plane!\n");
	TxPrintf("(Must ``:*ir deb noclean true'' and do a route first.)\n");
    }

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzDumpEstFunc --
 *
 * Filter function called via DBSrPaintArea on behalf of mzDumpEstimates()
 * above, for each estimate tile in the area of interest, 
 * the info associated with each tile is dumped.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Dumps info associated with tile.
 *
 * ----------------------------------------------------------------------------
 */

int
mzDumpEstFunc(tile, fd)
    Tile *tile;
    FILE *fd;
{
    Rect r;
    TileCosts *tilec = (TileCosts *) tile->ti_client;
 
    /* Get boundary of tile */
    TITORECT(tile, &r);

    /* dump info, to file if provided, else to screen */
    if(fd)
    {
	fprintf(fd,"\ntile %p\t\t  (x: %d to %d, y: %d to %d)\n",
		tile, r.r_xbot, r.r_xtop, r.r_ybot, r.r_ytop);
	fprintf(fd,"\thcost = %d ", 
		tilec->tc_hCost); 
	fprintf(fd,"vcost = %d \n", 
		tilec->tc_vCost);
	{
	    char str[100];
	    Estimate *e;

	    fprintf(fd,"\tEstimates:\n");	    

	    for(e=tilec->tc_estimates; e!=NULL; e=e->e_next)
	    {
		fprintf(fd,"\t\t%"DLONG_PREFIX"d + ABS(x - %d)*%d + ABS(y - %d)*%d\n",
			e->e_cost0,e->e_x0,e->e_hCost,
			e->e_y0,e->e_vCost);
	    }
	}
    }
    else 
    {
	TxPrintf("\ntile %x\t\t  (x: %d to %d, y: %d to %d)\n",
		(pointertype) tile, r.r_xbot, r.r_xtop, r.r_ybot, r.r_ytop);
	TxPrintf("\thcost = %d, ", 
		tilec->tc_hCost);
	TxPrintf("vcost = %d \n", 
		tilec->tc_vCost);
	{
	    char str[100];
	    Estimate *e;

	    TxPrintf("\tEstimates:\n");	    

	    for(e=tilec->tc_estimates; e!=NULL; e=e->e_next)
	    {
		TxPrintf("\t\t%lld + ABS(x - %d)*%d + ABS(y - %d)*%d\n",
			 e->e_cost0,e->e_x0,e->e_hCost,
			 e->e_y0,e->e_vCost);
	    }
	}
    }
		
    /* continue search */
    return (0);
}
