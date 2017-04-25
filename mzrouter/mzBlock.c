/*
 * mzBlock.c --
 *
 * Construction of blockage planes.
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
 * A blockage plane is used to determine the legal areas for routing.
 * Each point on the interior of a space tile in a blockage plane is
 * a legal position to place the lower-left corner of a piece of wiring.
 *
 * To build a blockage plane, each solid mask tile in the 
 * layout is bloated in
 * all four directions and painted into the blockage plane.  To the
 * top and right, it is only bloated by the minimum separation from
 * that tile to routing on that plane (s).  To the bottom and left,
 * though, it is bloated by this distance PLUS one less than the
 * minimum width of routing on that layer (w-1):
 *
 *
 *
 *		  s+w-1			 s
 *		<------->	        <-->
 *		+---------------------------+	^
 *		|			    |	| s
 *		|	+---------------+   |	v
 *		|	|. . . . . . . .|   |
 *		|	| . . tile. . .	|   |
 *		|	|. . . . . . . .|   |
 *		|	+---------------+   |	^
 *		|			    |	|
 *		|   blockage		    |	| s+w-1
 *		|			    |	|
 *		+---------------------------+	v
 *
 * Blockage planes are kept for each routeLayer and routeContact.  They
 * are part of ther RouteType datastructure.
 *
 * There are two blockage planes for each routeType.  One is merged into
 * maximal horizontal strips, and the other
 * into maximal vertical strips.  (To be done---there's no reason to
 * have separate routines for maximal vertical strips;  just use max
 * horizontal strips routines with x and y appropriately swapped).
 *
 * Start and destination nodes are treated differently, by considering
 * the areas in which a route's lower left corner can be placed that will
 * let the route terminate without DRC errors.  This is the following:
 *
 *		    w-1		        w-1	
 *		  <----->	      <---->
 *		        +-------------+-----+	^
 *		        |. . . . . . .|. . .|	| w-1
 *		        | . . . . . . | . . |	| 
 *		  +-----+. . . . . . .+-----+	v
 *		  |	| . . . . . . . . . |
 *		  |	|. . .tile . . . . .|
 *		  |	| . . . . . . . . . |
 *		  +-----+-------------+-----+	^
 *		        | 	      |	     	|
 *		        |	      |	     	| w-1
 *		        +-------------+      	v
 *
 * The upper-right corner of the tile is a keep-out area, where a route
 * would violate width rules.  The rest of the tile is valid for placement
 * of a route's lower left corner, as well as two extension areas to the
 * left and bottom.
 *
 * (to be done)---Both start and dest areas should not trim back the
 * upper corners of tiles that are connected to other start/dest tiles,
 * since that would leave unnecessary holes in the routable areas.
 * Not doing this tends to cause troubles when the start/dest nodes are
 * broken up into many tiles.
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/mzrouter/mzBlock.c,v 1.2 2010/10/22 15:02:15 tim Exp $";
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

/* Forward declarations */
extern void mzPaintBlockType();


/*
 * ----------------------------------------------------------------------------
 *
 * mzBuildMaskDataBlocks --
 *
 * Build blockage info from paint in buildArea.
 *
 * The design rules are used 
 * to build a map of blocked areas in blockage planes of RouteTypes.
 * This map will consist of TT_SPACE tiles, where a zero-width
 * path will yield a 
 * legal route when flushed out to wire width paint, and various
 * types of block tiles.   Multiple block types are needed to handle route 
 * termination at the destination node properly.
 *
 * TT_BLOCKED and TT_SAMENODE are generated from the mask data
 * (SAMENODE areas are only blocked by the destination node itself).
 *
 * In addition to blockage planes for route layers, there are
 * blockage planes for contacts, assumed to connect two planes.  
 * Both planes of mask information 
 * are effectively AND-ed together to form the blockage plane for
 * that type of contact.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Paints into Blockage planes in RouteTypes
 *
 * ----------------------------------------------------------------------------
 */

void
mzBuildMaskDataBlocks(buildArea)
    Rect *buildArea;	/* Area over which blockage planes will be built */
{
    Rect searchArea;
    int pNum;
    int mzPaintSameNodeFunc();	/* Fwd declaration */

    /* search area  = build area + context */
    searchArea.r_xbot = buildArea->r_xbot - mzContextRadius;
    searchArea.r_ybot = buildArea->r_ybot - mzContextRadius;
    searchArea.r_xtop = buildArea->r_xtop + mzContextRadius;
    searchArea.r_ytop = buildArea->r_ytop + mzContextRadius;

    /* Paint SAMENODE on all destination areas		 */
    /* (all areas that were painted into mzDestAreasUse->cu_def) */

    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
    {
	DBSrPaintArea((Tile *)NULL, mzDestAreasUse->cu_def->cd_planes[pNum],
		&searchArea, &DBAllButSpaceAndDRCBits,
		mzPaintSameNodeFunc, (ClientData)buildArea);
    }

    /* Build the blockage planes on all layers, ignore unexpanded subcells
     * (unexpanded in reference window)
     */
    {
	int mzBuildBlockFunc();
	SearchContext scx;

	scx.scx_area = searchArea;
	scx.scx_trans = GeoIdentityTransform;
	scx.scx_use = mzRouteUse;

	(void) DBTreeSrTiles(
			     &scx, 
			     &DBAllButSpaceAndDRCBits, 
			     mzCellExpansionMask,
			     mzBuildBlockFunc, 
			     (ClientData) buildArea);
    }

    /* Add blocks at unexpanded subcells on all blockage planes 
     *
     * NOTE: A 0 expansion mask is special cased since 
     *     the mzrotuer interpets a 0 mask to mean all subcells are 
     *     expanded,
     *     while DBTreeSrCells() takes a 0 mask to mean all subcells are
     *     unexpanded.
     */

    if(mzCellExpansionMask != 0)
    {
	int mzBlockSubcellsFunc();
	SearchContext scx;

	scx.scx_area = searchArea;
	scx.scx_trans = GeoIdentityTransform;
	scx.scx_use = mzRouteUse;

	DBTreeSrCells(
		      &scx,
		      mzCellExpansionMask,
		      mzBlockSubcellsFunc,
		      (ClientData) buildArea);
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzBuildBlockFunc --
 *
 * Filter function called via DBTreeSrTiles on behalf of mzBuildBlock()
 * above, for each solid tile in the area of interest.  Paints TT_BLOCKED
 * (TT_SAMENODE if the tile is marked) areas on each of the planes 
 * affected by this tile.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Paints into Blockage Planes in RouteType structures.
 *
 * ----------------------------------------------------------------------------
 */

int
mzBuildBlockFunc(tile, cxp)

    Tile *tile;
    TreeContext *cxp;
{
    SearchContext *scx = cxp->tc_scx;
    Rect *buildArea = (Rect *) (cxp->tc_filter->tf_arg);
    Rect r, rDest;

    /* Transform to result coordinates */
    TITORECT(tile, &r);
    GEOCLIP(&r, &scx->scx_area);
    GEOTRANSRECT(&scx->scx_trans, &r, &rDest);

    mzPaintBlockType(&rDest, TiGetType(tile), buildArea, TT_BLOCKED);
    return (0);
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzBlockSubcellsFunc --
 *
 * Filter function called via DBTreeSrTiles on behalf of mzBuildBlock()
 * above, for each unexpanded subcell in the area of interest, 
 * a "blocked" area (TT_BLOCKED) is painted on each blockage plane for
 * the bounding box of the subcell, bloated by the maximum design rule
 * spacing on that plane.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Paints into Blockage Planes in RouteType structures.
 *
 * ----------------------------------------------------------------------------
 */

int
mzBlockSubcellsFunc(scx, cdarg)
    SearchContext *scx;
    ClientData cdarg;
{
    Rect r, rDest;
    Rect *buildArea = (Rect *) cdarg;

    /* Transform bounding box to result coords */
    r = scx->scx_use->cu_def->cd_bbox;
    GEOTRANSRECT(&scx->scx_trans, &r, &rDest);

    if((int)(scx->scx_use->cu_client) != MZ_EXPAND_DEST)
    /* cell over part of dest node, paint normal blocks onto affected
     * planes.
     * (area is bloated by appropriate spacing on each affected plane)
     */
    {
	mzPaintBlockType(&rDest, TT_SUBCELL, buildArea, TT_BLOCKED);
    }
    else
    /* cell is over part of dest node, paint samenode blocks */
    {
	mzPaintBlockType(&rDest, TT_SUBCELL, buildArea, TT_SAMENODE);
    }

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * mzPaintSameNodeFunc --
 *
 * Add TT_SAMENODE paint into the route layer block plane for each
 * tile found in mzDestAreasUse->cu_def
 *
 * Results:
 *	Always return 0 to keep the search going.
 *
 * ----------------------------------------------------------------------------
 */

int
mzPaintSameNodeFunc(Tile *t, Rect *buildArea)
{
    Rect r;
    TileType ttype;

    ttype = TiGetType(t);
    TiToRect(t, &r);

    mzPaintBlockType(&r, ttype, buildArea, TT_SAMENODE);

    return 0;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzPaintBlockType --
 *
 * Add "blockType" paint appropriate to data tile of given location
 * and type to blockage planes of all relevant routeTypes.
 *
 * The blockage tiles painted are bloated versions of the data tiles.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies blockage planes in RouteType structures.
 *
 * ----------------------------------------------------------------------------
 */

void
mzPaintBlockType(r, type, buildArea, blockType)
    Rect *r;
    TileType type;
    Rect *buildArea;
    TileType blockType;
{
    RouteType *rT;
    TileType locBlockType;

    /* process routeTypes  */
    for (rT=mzActiveRTs; rT!=NULL; rT=rT->rt_nextActive)
    {
	Rect rblock;

	/* Added by Tim 7/28/06 --- Ignore those planes that don't interact! */

	if (!(DBTypePlaneMaskTbl[type] & DBTypePlaneMaskTbl[rT->rt_tileType]))
	    continue;

	/* if there is a constraint between RouteType and type, paint a block */

	locBlockType = blockType;

	if (rT->rt_bloatBot[type] >= 0)
	{
	    int bot, top;

	    /* If we're painting TT_BLOCKED, make sure we're not painting */
	    /* on top of a start or dest area (has TT_SAMENODE)	  */

	    if (blockType != TT_SAMENODE)
	    {
		RouteContact *rC;
		Tile *tp = rT->rt_hBlock->pl_hint;
		GOTOPOINT(tp, &r->r_ll);
		if (TiGetType(tp) == TT_SAMENODE)
		{
		    /* This can fail to paint a proper blockage on a	*/
		    /* contact plane when the terminal is in one plane	*/
		    /* and the blocking route is on the other.  So, if	*/
		    /* painting into a contact plane, check that the	*/
		    /* tile in the residue layer that is "type" is also	*/
		    /* covered in TT_SAMENODE, or this is a block.	*/

		    if (DBIsContact(rT->rt_tileType))
		    {
			for (rC = mzRouteContacts; rC != NULL; rC = rC->rc_next)
			{
			    if (rC->rc_routeType.rt_tileType == rT->rt_tileType)
			    {
				Tile *tp2;
				if (rC->rc_rLayer1->rl_routeType.rt_tileType == type)
				{
				    tp2 = rC->rc_rLayer1->
						rl_routeType.rt_hBlock->pl_hint;
				    GOTOPOINT(tp2, &r->r_ll);
				    if (TiGetType(tp) == TT_SAMENODE) break;
				}
				else if (rC->rc_rLayer2->rl_routeType.rt_tileType
						== type)
				{
				    tp2 = rC->rc_rLayer2->
						rl_routeType.rt_hBlock->pl_hint;
				    GOTOPOINT(tp2, &r->r_ll);
				    if (TiGetType(tp) == TT_SAMENODE) break;
				}
			    }
			}
			if (rC != NULL) continue;
		    }
		    else
			continue;
		}
	    }
	    else
	    {
		/* If the tile type is a contact of the same 
		 * type as the route type, draw a block layer, not SAMENODE.
		 * This prevents the maze router from attempting to place
		 * a contact too close to an existing one, causing a DRC
		 * error.
		 */
		if ((DBIsContact(type)) && (rT->rt_tileType == type))
		    locBlockType = TT_BLOCKED;
	    }

	    /* Compute blockage rectangle */
	    bot = rT->rt_bloatBot[type];
	    top = rT->rt_bloatTop[type];

	    if (locBlockType != TT_SAMENODE)
	    {
		rblock.r_xbot = r->r_xbot - bot;
		rblock.r_ybot = r->r_ybot - bot;

		rblock.r_xtop = r->r_xtop + top;
		rblock.r_ytop = r->r_ytop + top;
	    }
	    else
	    {
		int wless = bot - top + 1;

		/* SAMENODE:  valid route can start in one of	*/
		/* two overlapping rectangles as computed	*/
		/* below.  The left and bottom sides are	*/
		/* extended by the route width, but the		*/
		/* corners are prohibited.			*/

		rblock.r_xbot = r->r_xbot - wless;
		rblock.r_ybot = r->r_ybot;

		rblock.r_xtop = r->r_xtop;
		rblock.r_ytop = r->r_ytop - wless;

		GEOCLIP(&rblock, buildArea);
		if (!GEO_RECTNULL(&rblock))
		{
		    DBPaintPlane(rT->rt_hBlock, &rblock, 
				mzBlockPaintTbl[blockType],
				(PaintUndoInfo *) NULL);
		    DBPaintPlaneVert(rT->rt_vBlock, &rblock, 
		 		mzBlockPaintTbl[blockType],
		 		(PaintUndoInfo *) NULL);
		}

		rblock.r_xbot = r->r_xbot;
		rblock.r_ybot = r->r_ybot - wless;

		rblock.r_xtop = r->r_xtop - wless;
		rblock.r_ytop = r->r_ytop;
	    }

	    /* clip to build area */

	    GEOCLIP(&rblock, buildArea);
	    if (!GEO_RECTNULL(&rblock))
	    {
		/* and paint it */

		DBPaintPlane(rT->rt_hBlock, &rblock, 
				mzBlockPaintTbl[locBlockType],
				(PaintUndoInfo *) NULL);
		DBPaintPlaneVert(rT->rt_vBlock, &rblock, 
		 		mzBlockPaintTbl[locBlockType],
		 		(PaintUndoInfo *) NULL);
	    }
	}
    }

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzBuildFenceBlocks -- 
 *
 * Blocks regions of fence parity opposite of the destination-point.
 * (Fence boundaries can not be crossed).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Blockage Planes modified.
 *
 * ----------------------------------------------------------------------------
 */

void
mzBuildFenceBlocks(buildArea)
    Rect *buildArea;		/* Area over which planes modified */
{
    int mzBuildFenceBlocksFunc();
    Rect searchArea;


    /* search area  = build area + context */
    searchArea.r_xbot = buildArea->r_xbot - mzContextRadius;
    searchArea.r_ybot = buildArea->r_ybot - mzContextRadius;
    searchArea.r_xtop = buildArea->r_xtop + mzContextRadius;
    searchArea.r_ytop = buildArea->r_ytop + mzContextRadius;

    /* If route inside fence, gen blocks at space tiles.
     * if route outside fence, gen blocks at fence tiles.
     */
    if(mzInsideFence)
    {
	DBSrPaintArea(NULL,	/* no hint tile */
		      mzHFencePlane, 
		      &searchArea, 
		      &DBSpaceBits,
		      mzBuildFenceBlocksFunc, 
		      (ClientData) buildArea);
    }
    else
    {
	DBSrPaintArea(NULL,	/* no hint tile */
		      mzHFencePlane, 
		      &searchArea, 
		      &DBAllButSpaceBits,
		      mzBuildFenceBlocksFunc, 
		      (ClientData) buildArea);
    }

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzBuildFenceBlocksFunc --
 *
 * Called by DBSrPaintArea for tile in given area on fence plane where routing
 * is prohibited.  These areas are blocked,
 * adjusting for wire width on each blockage plane.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Paints into blockage planes.
 *
 * ----------------------------------------------------------------------------
 */

int
mzBuildFenceBlocksFunc(tile, buildArea)
    Tile *tile;
    Rect *buildArea; /* clip to this area before painting */
{
    RouteType *rT;
    int d;
    Rect r, rAdjusted; 
    TileType tt = TiGetType(tile);

    /* Get boundary of tile */
    TITORECT(tile, &r);

    for (rT=mzActiveRTs; rT!=NULL; rT=rT->rt_nextActive)
    {
	/* Added by Tim 7/28/06 --- Ignore those planes that don't interact! */
	/* Revised 8/22/06---Fence tiles are supposed to block all layers!   */

	// if (!(DBTypePlaneMaskTbl[tt] & DBTypePlaneMaskTbl[rT->rt_tileType]))
	//    continue;

	/* adjust to compensate for wire width */
	d = rT->rt_effWidth - 1;
	rAdjusted.r_xbot = r.r_xbot - d;
	rAdjusted.r_xtop = r.r_xtop;
	rAdjusted.r_ybot = r.r_ybot - d;
	rAdjusted.r_ytop = r.r_ytop;

	/* clip to area being generated. */
	GEOCLIP(&rAdjusted, buildArea);

	/* Paint into blockage planes */
	DBPaintPlane(rT->rt_hBlock, 
		     &rAdjusted, 
		     mzBlockPaintTbl[TT_BLOCKED], 
		     (PaintUndoInfo *) NULL);
	DBPaintPlaneVert(rT->rt_vBlock, 
	 		 &rAdjusted, 
	 		 mzBlockPaintTbl[TT_BLOCKED], 
	 		 (PaintUndoInfo *) NULL);
    }
   
    /* return 0 - to continue search */
    return(0);
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzExtendBlockBoundsR --
 *
 * Generate blockage information around given rect.
 * Info required for radius of twice the bounds-increment around the rect.
 * The gen area is broken down into areas which have not already been gened.
 * Blockage info is generated for these subregions.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Paints into Blockage planes in RouteTypes
 *	Updates bounds plane.
 *
 * ----------------------------------------------------------------------------
 */

void
mzExtendBlockBoundsR(rect)
    Rect *rect;
{
    Rect area;
    TileTypeBitMask genMask;
    /* Generate twice the required bounds increment, so we don't have
     * to regenerate as soon as we move from the center of the newly
     * generated region. 
     */
    int genBoundsIncrement = mzBoundsIncrement * 2;
    int mzExtendBlockFunc();

    /* keep stats */
    mzBlockGenCalls++;

    /* mark area about rect in which blockage plane required to be present */
    area.r_xbot = rect->r_xbot - genBoundsIncrement;
    area.r_ybot = rect->r_ybot - genBoundsIncrement;
    area.r_xtop = rect->r_xtop + genBoundsIncrement;
    area.r_ytop = rect->r_ytop + genBoundsIncrement;


    DBPaintPlane(mzHBoundsPlane, 
	    &area,
	    mzBoundsPaintTbl[TT_GENBLOCK],
	    (PaintUndoInfo *) NULL);

    /* Generate blockage planes under each GENBLOCK tile = regions in
     * new area where blockage planes not previously generated 
    */
    TTMaskZero(&genMask);
    TTMaskSetType(&genMask,TT_GENBLOCK);
    DBSrPaintArea(NULL,         /* no hint tile */ 
       mzHBoundsPlane,
       &area,
       &genMask,
       mzExtendBlockFunc,
       (ClientData) NULL);

    /* Paint area INBOUNDS in both bounds planes 
     *(blockage planes now generated here) 
     */
    DBPaintPlane(mzHBoundsPlane, 
	    &area,
	    mzBoundsPaintTbl[TT_INBOUNDS],
	    (PaintUndoInfo *) NULL);
    DBPaintPlaneVert(mzVBoundsPlane, 
 	    &area,
 	    mzBoundsPaintTbl[TT_INBOUNDS],
 	    (PaintUndoInfo *) NULL);

    return;
}

#define RECT_AREA(r) \
    ((double)(r.r_xtop - r.r_xbot)*(double)(r.r_ytop - r.r_ybot))


/*
 * ----------------------------------------------------------------------------
 *
 * mzExtendBlockFunc --
 *
 * Called by DBSrPaintArea for rectangles where blockage info must be 
 * generated.
 *
 * Results:
 *	Return 0 always (to continue the search)
 *
 * Side effects:
 *	Paints into Blockage planes in RouteTypes
 *
 * ----------------------------------------------------------------------------
 */

int
mzExtendBlockFunc(tile, cdarg)
    Tile *tile;
    ClientData cdarg;
{
    Rect area;

    /* Get location of tile */
    TITORECT(tile, &area);

    /* Don't actually generate blocks outside of user supplied
     * route area hint.
     */
    if(mzBoundsHint)
    {
	GEOCLIP(&area,mzBoundsHint);
	if((area.r_xbot > area.r_xtop) || (area.r_ybot > area.r_ytop))
	/*no overlap just return */
	{
	    return 0;
	}
    }

    /* Grow clip area by 2 units to take care of boundary conditions. */
    area.r_xbot -= 2;
    area.r_xtop += 2;
    area.r_ybot -= 2;
    area.r_ytop += 2;

    mzBuildMaskDataBlocks(&area);
    mzBuildFenceBlocks(&area);

    /* keep stats */
    mzBlockGenArea += RECT_AREA(area);

    /* return 0 to continue search */
    return 0;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzExtendBlockBounds --
 *
 * Generate blockage information around given point.
 * Info required for radius of twice the bounds-increment around the point.
 * The gen area is broken down into areas which have not already been gened.
 * Blockage info is generated for these subregions.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Paints into Blockage planes in RouteTypes
 *	Updates bounds plane.
 *
 * ----------------------------------------------------------------------------
 */

void
mzExtendBlockBounds(point)
    Point *point;
{
    Rect rect;
    
    rect.r_ll = *point;
    rect.r_ur = *point;

    mzExtendBlockBoundsR(&rect);

    return;
}

/* 
 * This struc is used to store generated walks, since it is necessary
 * to defer painting walks until all are generated.
 */
typedef struct
{
    RouteType * w_rT;
    Rect 	w_rect;
    TileType	w_type;
} Walk;

/* global used to store walks prior to painting them */
List *mzWalkList;


/*
 * ----------------------------------------------------------------------------
 *
 * mzBuildDestAreaBlocks --
 *
 * Generate blockage information around all dest areas, and do special dest
 * area processing (generation of dest area blocks and walks).  In addition
 * dest area alignment coords are added to the alignment structures.
 *
 * TT_DEST_AREA and TT_*_WALK regions are generated from the special dest area
 * cell.  Dest areas are regions to connect to and walks are regions blocked
 * only by dest nodes and directly adjacent to a dest area.  Walks are 
 * routed through by special termination code.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies blockage planes.
 *	Modifies alignment strucs.
 *	Updates bounds plane.
 *
 * ----------------------------------------------------------------------------
 */

void
mzBuildDestAreaBlocks()
{
    int mzDestAreaFunc();
    int mzDestWalksFunc();
    int mzLRCWalksFunc();
    int mzUDCWalksFunc();
    SearchContext scx;

    /* initialize global walk list */
    mzWalkList = NULL;

    /* Compute bounding box for dest areas cell */
    DBReComputeBbox(mzDestAreasUse->cu_def);

    /* Process dest areas in dest area cell one by one.  
     *     - generates normal blockage info.
     *     - paints dest area into appropriate blockage planes.
     *     - generates alignments.
     *     - generates list of walks.
     *     
     * Walks are not actually painted yet because existing 
     * walks can interfere with the generation of new ones.
     */

    scx.scx_area = mzBoundingRect;
    scx.scx_trans = GeoIdentityTransform;
    scx.scx_use = mzDestAreasUse;
	
    /* clip area to bounding box to avoid overflow during transforms */
    GEOCLIP(&(scx.scx_area), &(mzDestAreasUse->cu_def->cd_bbox));

    (void) DBTreeSrTiles( &scx, &DBAllButSpaceAndDRCBits, 
			     0, mzDestAreaFunc, (ClientData) NULL);

    (void) DBTreeSrTiles( &scx, &DBAllButSpaceAndDRCBits, 
			     0, mzDestWalksFunc, (ClientData) NULL);

    (void) DBTreeSrTiles(&scx, &DBAllButSpaceAndDRCBits, 
			     0, mzUDCWalksFunc, (ClientData) NULL);

    (void) DBTreeSrTiles(&scx, &DBAllButSpaceAndDRCBits, 
			     0, mzLRCWalksFunc, (ClientData) NULL);

    /* Paint walks into blockage planes (and dealloc walk list) */

    {
	List *l;

	for(l = mzWalkList; l!= NULL; l=LIST_TAIL(l))
	{
	    Walk *walk = (Walk *) LIST_FIRST(l);

	    /* Diagnostic---something is wrong here! */
	    if (walk->w_type > TT_MAXROUTETYPES)
	    {
		TxError("Fatal: Bad destination walk!\n");
	 	continue;	/* Try to go on anyway */
	    }
		
	    DBPaintPlane(walk->w_rT->rt_hBlock, 
			 &(walk->w_rect),
			 mzBlockPaintTbl[walk->w_type],
			 (PaintUndoInfo *) NULL);

	    DBPaintPlaneVert(walk->w_rT->rt_vBlock, 
			     &(walk->w_rect),
			     mzBlockPaintTbl[walk->w_type],
			     (PaintUndoInfo *) NULL);
	}

	ListDeallocC(mzWalkList);
    }
    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzDestAreaFunc --
 *
 * Pregenerate blockage info around the destination area
 *
 * Results:
 *	Returns 0 on success.  In case we are stymied by an incomplete
 *	techfile entry for the maze router, return with 1 to stop the
 *	whole process.
 *
 * Side effects:
 *	Generates blockage info.
 *
 * ----------------------------------------------------------------------------
 */

int
mzDestAreaFunc(tile, cxp)
    Tile *tile;
    TreeContext *cxp;
{
    SearchContext *scx = cxp->tc_scx;
    TileType type = TiGetType(tile);
    Rect r, rect;
    RouteType *rT;

    /* Transform to result coordinates */
    TITORECT(tile, &r);
    GEOTRANSRECT(&scx->scx_trans, &r, &rect);

    mzExtendBlockBoundsR(&rect);

    /* find route type for this dest area */

    rT = mzActiveRTs;
    while ((rT != NULL) && (rT->rt_tileType != type))
	rT = rT->rt_nextActive;

    /* This can happen if the techfile does not completely define */
    /* the gaRoute info. . .					  */
    if (rT == NULL)
	return 1;
	
    /* Draw the routable destination area (see comments at top of file). */

    r.r_xtop = rect.r_xtop - rT->rt_width;
    r.r_ytop = rect.r_ytop;
    r.r_xbot = rect.r_xbot;
    r.r_ybot = rect.r_ybot - rT->rt_width;

    /* paint dest area into blockage planes of appropriate route type */
    DBPaintPlane(rT->rt_hBlock, &r, mzBlockPaintTbl[TT_DEST_AREA],
			(PaintUndoInfo *) NULL);

    DBPaintPlaneVert(rT->rt_vBlock, &r, mzBlockPaintTbl[TT_DEST_AREA],
			(PaintUndoInfo *) NULL);

    r.r_xtop = rect.r_xtop;
    r.r_ytop = rect.r_ytop - rT->rt_width;
    r.r_xbot = rect.r_xbot - rT->rt_width;
    r.r_ybot = rect.r_ybot;

    /* paint dest area into blockage planes of appropriate route type */
    DBPaintPlane(rT->rt_hBlock, &r, mzBlockPaintTbl[TT_DEST_AREA],
			(PaintUndoInfo *) NULL);

    DBPaintPlaneVert(rT->rt_vBlock, &r, mzBlockPaintTbl[TT_DEST_AREA],
			(PaintUndoInfo *) NULL);

    /* continue with next dest area */
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * mzDestWalksFunc --
 *
 * Generate walks around each destination area tile, showing the router
 * the direct "walk" into the destination area from points around the
 * perimeter.  This prevents the routing algorithm from having to blindly
 * find the exact destination area;  it only has to get close enough to
 * be told how to paint a path directly to the destination node.
 *
 * Results:
 *	Returns 0 on success.  In case we are stymied by an incomplete
 *	techfile entry for the maze router, return with 1 to stop the
 *	whole process.
 *
 * Side effects:
 *	Generates walk tiles in the blockage planes
 *
 * ----------------------------------------------------------------------------
 */

int
mzDestWalksFunc(tile, cxp)
    Tile *tile;
    TreeContext *cxp;
{
    SearchContext *scx = cxp->tc_scx;
    TileType type = TiGetType(tile);
    Rect r, rect;
    RouteType *rT;
    int mzHWalksFunc();
    int mzVWalksFunc();
    int mzUDCWalksFunc();
    int mzLRCWalksFunc();
    TileTypeBitMask destAreaMask;

    /* Transform to result coordinates */
    TITORECT(tile, &r);
    GEOTRANSRECT(&scx->scx_trans, &r, &rect);

    /* find route type for this dest area */

    rT = mzActiveRTs;
    while ((rT != NULL) && (rT->rt_tileType != type))
	rT = rT->rt_nextActive;

    /* This can happen if the techfile does not completely define */
    /* the gaRoute info. . .					  */
    if (rT == NULL)
	return 1;
	
     /* Generate alignments and walks for dest areas.
      *
      * Since the above dest area may be partially blocked, the blockage
      * planes are searched for DEST_AREA tiles underneath
      * the dest area painted above to build lists of dest tiles.
      *
      * walks are accumulated into a list for painting after ALL walks
      * have been calculated.
      *
      */

    TTMaskSetOnlyType(&destAreaMask, TT_DEST_AREA);

    DBSrPaintArea(NULL,	/* no hint tile */
		      rT->rt_hBlock, &rect, 
		      &destAreaMask, mzHWalksFunc, 
		      (ClientData) rT);

    DBSrPaintArea(NULL,	/* no hint tile */
		      rT->rt_vBlock, &rect, 
		      &destAreaMask, mzVWalksFunc, 
		      (ClientData) rT);

    DBSrPaintArea(NULL,	/* no hint tile */
		      rT->rt_hBlock, &rect, 
		      &destAreaMask, mzLRCWalksFunc, 
		      (ClientData) rT);

    DBSrPaintArea(NULL,	/* no hint tile */
		      rT->rt_vBlock, &rect, 
		      &destAreaMask, mzUDCWalksFunc, 
		      (ClientData) rT);

    /* continue with next dest area */
    return 0;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzHWalksFunc --
 *
 * Generate walks to top and bottom, and alignment y-coords for TT_DESTAREA
 * (Walks are added to list for painting after search completes.)
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Adds to walks list and alignment structure.
 *
 * ----------------------------------------------------------------------------
 */

int
mzHWalksFunc(tile, cdarg)
    Tile *tile;
    ClientData cdarg;
{
    RouteType *rT = (RouteType *) cdarg;

    /* Add dest area coordinates to dest alignment structure */
    {
	mzNLInsert(&mzXAlignNL, LEFT(tile));
	mzNLInsert(&mzXAlignNL, RIGHT(tile));
    }

    /* compute LEFT_WALK(s) */
    {
	Walk *walk;
	Tile *tLeft = BL(tile);
	
	/* Build walks for blocks to left of tile */
	while(BOTTOM(tLeft)<TOP(tile))
	{
	    if(TiGetType(tLeft)==TT_SAMENODE)
	    {
		walk = (Walk *) mallocMagic((unsigned) (sizeof(Walk)));
		walk->w_rT = rT; 
		walk->w_type = TT_LEFT_WALK;
		walk->w_rect.r_ybot = MAX(BOTTOM(tile),BOTTOM(tLeft));
		walk->w_rect.r_ytop = MIN(TOP(tile),TOP(tLeft));
		walk->w_rect.r_xtop = RIGHT(tLeft);
		walk->w_rect.r_xbot = MAX(LEFT(tLeft),
					   RIGHT(tLeft)-mzMaxWalkLength);
		LIST_ADD(walk, mzWalkList);
	    }
	    
	    /* move to next tile up */
	    tLeft = RT(tLeft);
	}
    }

    /* compute RIGHT_WALK */
    {
	Walk *walk;
	Tile *tRight = TR(tile);

	/* Build walks for blocks to right of tile */
	while(TOP(tRight)>BOTTOM(tile))
	{
	   if(TiGetType(tRight)==TT_SAMENODE)
	   {
	       walk = (Walk *) mallocMagic((unsigned) (sizeof(Walk)));
	       walk->w_rT = rT; 
	       walk->w_type = TT_RIGHT_WALK;
	       walk->w_rect.r_ybot = MAX(BOTTOM(tile),BOTTOM(tRight));
	       walk->w_rect.r_ytop = MIN(TOP(tile),TOP(tRight));
	       walk->w_rect.r_xbot = LEFT(tRight);
	       walk->w_rect.r_xtop = MIN(RIGHT(tRight), 
					  LEFT(tRight)+mzMaxWalkLength);
	       LIST_ADD(walk, mzWalkList);
	   }

	   /* move to next tile down */
	   tRight = LB(tRight);
       }
    }
   
    /* return 0 - to continue search */
    return(0);
}



/*
 * ----------------------------------------------------------------------------
 *
 * mzVWalksFunc --
 *
 * Generate walks to top and bottom, and alignment y-coords for TT_DESTAREA
 * (Walks are added to list for painting after search completes.)
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Adds to walks list and alignment structure.
 *
 * ----------------------------------------------------------------------------
 */

int
mzVWalksFunc(tile, cdarg)
    Tile *tile;
    ClientData cdarg;
{
    RouteType  *rT = (RouteType *) cdarg;

    /* Add dest area coordinates to dest alignment structure */
    {
	mzNLInsert(&mzYAlignNL, BOTTOM(tile));
	mzNLInsert(&mzYAlignNL, TOP(tile));
    }

    /* compute BOTTOM_WALK */
    {
	Walk *walk;
	Tile *tBelow = LB(tile);
	
	/* Build walks for blocks to below tile */
	while(LEFT(tBelow)<RIGHT(tile))
	{
	    if(TiGetType(tBelow)==TT_SAMENODE)
	    {
		walk = (Walk *) mallocMagic((unsigned) (sizeof(Walk)));
		walk->w_rT = rT;
		walk->w_type = TT_BOTTOM_WALK;
		walk->w_rect.r_xbot = MAX(LEFT(tile),LEFT(tBelow));
		walk->w_rect.r_xtop = MIN(RIGHT(tile),RIGHT(tBelow));
		walk->w_rect.r_ytop = TOP(tBelow);
		walk->w_rect.r_ybot = MAX(BOTTOM(tBelow),
					   TOP(tBelow)-mzMaxWalkLength);
		LIST_ADD(walk, mzWalkList);
	    }

	    /* move to next tile to left */
	    tBelow = TR(tBelow);
	}
    }

    /* compute TOP_WALK */
    {
	Walk *walk;
	Tile *tAbove = RT(tile);
	
	
	/* Build walks for blocks above tile */
	while(RIGHT(tAbove)>LEFT(tile))
	{
	    if(TiGetType(tAbove)==TT_SAMENODE)
	    {
		walk = (Walk *) mallocMagic((unsigned) (sizeof(Walk)));
		walk->w_rT = rT;
		walk->w_type = TT_TOP_WALK;
		walk->w_rect.r_xbot = MAX(LEFT(tile),LEFT(tAbove));;
		walk->w_rect.r_xtop = MIN(RIGHT(tile),RIGHT(tAbove));
		walk->w_rect.r_ybot = BOTTOM(tAbove);
		walk->w_rect.r_ytop = MIN(TOP(tAbove), 
					   BOTTOM(tAbove)+mzMaxWalkLength);
		LIST_ADD(walk, mzWalkList);
	    }

	    /* move to next tile to right */
	    tAbove = BL(tAbove);
	}
    }
   
    /* return 0 - to continue search */
    return(0);
}

/* 
 *  Structure to pass data between mzLRCWalksFunc, mzUDCWalksFunc, and mzCWalksFunc2
 */
typedef struct walkContactFuncData
{
    Rect		*wd_bounds;	/* dest area bounds */
    RouteLayer 		*wd_rL;		/* Route layer of walk_contact */
    int			wd_walk;	/* TT_ABOVE_*_WALK or TT_BELOW_*_WALK */
} WalkContactFuncData;


/*
 * ----------------------------------------------------------------------------
 *
 * mzLRCWalksFunc --
 *
 * Search dest area for regions where contacts are ok, and paint
 * CONTACT_WALK(s) there in adjacent layers.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Paints into adjacent blocakge planes.
 *
 * ----------------------------------------------------------------------------
 */

int
mzLRCWalksFunc(tile, cdarg)
    Tile *tile;
    ClientData cdarg;
{
    RouteType *rT = (RouteType *) cdarg; /* RouteType of this dest area */
    RouteContact *rC;	
    Rect rect;
    int walkType;

    /* set rect to boundary of this dest area */
    TITORECT(tile, &rect);

    /* process contact types that can connect to this dest area */
    for (rC = mzRouteContacts; rC != NULL; rC = rC->rc_next)
    {
	RouteLayer *rLOther = NULL; 

	/* skip inactive contact types */
	if (!(rC->rc_routeType.rt_active)) continue;

	/* Find layer connecting to dest area FROM */
	if (&(rC->rc_rLayer1->rl_routeType) == rT)
	{
	    rLOther = rC->rc_rLayer2;
	    walkType = TT_ABOVE_LR_WALK;
	}
	else if (&(rC->rc_rLayer2->rl_routeType) == rT)
	{
	    rLOther = rC->rc_rLayer1;
	    walkType = TT_BELOW_LR_WALK;
	}

	/* If current contact type (RC) permits connection to dest area, 
	 * insert CONTACT_WALK(s) into blockage planes for other layer.
	 */
	if (rLOther)
	{
	    /* process contact-OK tiles in blockage plane for this contact */
	    int mzCWalksFunc2();
	    WalkContactFuncData wD;
	    TileTypeBitMask contactOKMask;

	    TTMaskSetOnlyType(&contactOKMask, TT_SPACE);
	    TTMaskSetType(&contactOKMask, TT_SAMENODE);

	    wD.wd_bounds = &rect; 
	    wD.wd_rL = rLOther;
	    wD.wd_walk = walkType;

	    /* search for OK places for contact in the horizontal plane	*/
	    /* and paint them.						*/

	    DBSrPaintArea(NULL,	/* no hint tile */
			      rC->rc_routeType.rt_hBlock,
			      &rect, 
			      &contactOKMask,
			      mzCWalksFunc2, 
			      (ClientData) &wD);
	}
    }
    
    /* return 0 - to continue search */
    return(0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * mzUDCWalksFunc --
 *
 * Search dest area for regions where contacts are ok, and paint
 * CONTACT_WALK(s) there in adjacent layers.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Paints into adjacent blocakge planes.
 *
 * ----------------------------------------------------------------------------
 */

int
mzUDCWalksFunc(tile, cdarg)
    Tile *tile;
    ClientData cdarg;
{
    RouteType *rT = (RouteType *) cdarg; /* RouteType of this dest area */
    RouteContact *rC;	
    Rect rect;
    int walkType;

    /* set rect to boundary of this dest area */
    TITORECT(tile, &rect);

    /* process contact types that can connect to this dest area */
    for (rC = mzRouteContacts; rC != NULL; rC = rC->rc_next)
    {
	RouteLayer *rLOther = NULL; 

	/* skip inactive contact types */
	if (!(rC->rc_routeType.rt_active)) continue;

	/* Find layer connecting to dest area FROM */
	if (&(rC->rc_rLayer1->rl_routeType) == rT)
	{
	    rLOther = rC->rc_rLayer2;
	    walkType = TT_ABOVE_UD_WALK;
	}
	else if (&(rC->rc_rLayer2->rl_routeType) == rT)
	{
	    rLOther = rC->rc_rLayer1;
	    walkType = TT_BELOW_UD_WALK;
	}

	/* If current contact type (RC) permits connection to dest area, 
	 * insert CONTACT_WALK(s) into blockage planes for other layer.
	 */
	if (rLOther)
	{
	    /* process contact-OK tiles in blockage plane for this contact */
	    int mzCWalksFunc2();
	    WalkContactFuncData wD;
	    TileTypeBitMask contactOKMask;

	    TTMaskSetOnlyType(&contactOKMask, TT_SPACE);
	    TTMaskSetType(&contactOKMask, TT_SAMENODE);

	    wD.wd_bounds = &rect; 
	    wD.wd_rL = rLOther;
	    wD.wd_walk = walkType;

	    /* search for OK places for contact in the vertical plane	*/
	    /* and paint them.						*/

	    DBSrPaintArea(NULL,	/* no hint tile */
			      rC->rc_routeType.rt_vBlock,
			      &rect, 
			      &contactOKMask,
			      mzCWalksFunc2, 
			      (ClientData) &wD);
	}
    }
    
    /* return 0 - to continue search */
    return(0);
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzCWalksFunc2 --
 *
 * Called for tiles on contact plane where contact is OK that overlap a
 * dest area.  We paint a CONTACT_WALK on the blockage planes for the
 * other routeLayer, the one the contact connects to the dest area.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Paints into blockage planes for "other" route layer.
 *
 * ----------------------------------------------------------------------------
 */

int
mzCWalksFunc2(tile, cdarg)
    Tile *tile;
    ClientData cdarg;
{
    WalkContactFuncData *wD = (WalkContactFuncData *) cdarg;
    Rect rect;
    Walk *walk;

    /* rect = tile clipped dest area bounds. */
    TITORECT(tile, &rect);
    GEOCLIP(&rect, wD->wd_bounds);

    /* To-do: check if non-square contacts fit? */

    /* Build walks for contacts above or below the tile */
    {
	walk = (Walk *) mallocMagic((unsigned) (sizeof(Walk)));
	walk->w_rT = &(wD->wd_rL->rl_routeType);
	walk->w_type = wD->wd_walk;
	walk->w_rect = rect;
	LIST_ADD(walk, mzWalkList);
    }

    /* return 0 - to continue search */
    return(0);
}

