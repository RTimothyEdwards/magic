/*
 * rtrSide.c -
 *
 * Contains procedures for enumerating the sides of groups of
 * collinear cells.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/router/rtrSide.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */


#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/hash.h"
#include "utils/heap.h"
#include "utils/malloc.h"
#include "debug/debug.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "router/router.h"
#include "gcr/gcr.h"
#include "grouter/grouter.h"
#include "textio/textio.h"
#include "windows/windows.h"
#include "utils/main.h"
#include "utils/signals.h"

/*
 * Local data used for communicating with filter functions.
 * This procedure is non-reentrant because it needs to use a temporary
 * plane for holding the transformed cell plane it uses for searching.
 */

    /* Transform information */
int rtrSideSide;		/* Which sides of cells are being visited */
Transform rtrSideTrans;		/* Transform from rtrSideTransDef back to
				 * the original cell plane.
				 */

    /* Transformed coords */
Rect rtrSideArea;		/* Area being searched */
CellDef *rtrSideTransDef = NULL;/* Holds a copy of original cell plane, but
				 * transformed so the direction of Side being
				 * processed is always GEO_EAST (the rhs).
				 */
CellUse *rtrSideTransUse = NULL;/* Cell use for above */
Plane *rtrSideTransPlane;	/* Points to the cell plane of either the
				 * original def (if processing GEO_EAST
				 * sides), or of rtrSideTransDef if processing
				 * other sides.
				 */

    /* Filter function */
int (*rtrSideFunc)();		/* Function called for each Side found */
ClientData rtrSideCdata;	/* Passed to (*rtrSideFunc)() */
int rtrSideMinChanWidth;	/* See comments in rtrEnumSides() */

/* Forward declarations */
int rtrEnumSidesFunc();
int rtrSideInitClient();
int rtrSideLookCellsFunc();

/*
 * ----------------------------------------------------------------------------
 *
 * rtrEnumSides --
 *
 * Enumerate each colinear boundary between a cell and empty space
 * in the cell 'def' that lies entirely within the area 'area'.
 * Colinear boundaries occur when, for example, two vertically
 * abutting cells share the same right-hand coordinate:
 *
 *		+-------+  ^
 *		|	|  |
 *		|   A	|  |
 *		|	|  |
 *	   +----+-------+  | colinear boundary
 *	   |		|  |
 *	   |	  B	|  |
 *	   |		|  |
 *	   +------------+  v
 *
 * The parameter minChannelWidth is a threshold on how much space
 * must exist between a cell and its neighbors in order for us
 * to enumerate a Side facing in a particular direction.  It is
 * interpreted as follows:
 *
 *	--------+	|			       |	+-------
 *		|	|			       |	|
 *	  cell1	|	A <----- minChannelWidth ----> B	| cell2
 *		|	|			       |	|
 *	--------+	|			       |	+-------
 *
 * The line 'A' is the closest legal grid line to cell1, as is 'B'
 * the closest legal grid line to cell2 (the nearest neighbor to cell1
 * along its RHS).  If these are not at least minChannelWidth apart,
 * don't enumerate the RHS of cell1.
 *
 * Applies the supplied function to each boundary found.  This
 * function should be of the following form:
 *
 *	int
 *	(*func)(side, cdata)
 *	    Side *side;
 *	    ClientData cdata;
 *	{
 *	}
 *
 * The Side it is passed has side_trans set to the transform from the
 * canonical orientation (where side appears to be on the RHS of a group
 * of cells) to the original orientation.  Side_line is set to a zero-
 * width rectangle along the boundary being processed, in transformed
 * coordinates.  Also in transformed coordinates are side_search, set
 * to a 1-unit wide rectangle along the boundary but extending in the
 * direction of the cells' interiors, and side_used extending in the
 * opposite direction from side_search to the nearest usable grid
 * point.  Side_next is set to NULL.
 *
 * The function should return 0 if we should continue enumerating
 * Sides, or 1 if we should abort.
 *
 * Results:
 *	Returns 0 if we completed successfully, or 1 if (*func)()
 *	returned 1 or we were interrupted (SigInterruptPending != 0).
 *
 * Side effects:
 *	Whatever is done by the client procedure.
 *	We use the ti_client fields of the tiles in the cell tile
 *	plane for markings, and reset them to their uninitialized
 *	value when done (CLIENTDEFAULT).
 *
 * ----------------------------------------------------------------------------
 */

int
rtrEnumSides(use, area, minChannelWidth, func, cdata)
    CellUse *use;		/* Enumerate sides of use->cu_def */
    Rect *area;			/* Only consider sides inside this area;
				 * this does not include sides along the
				 * border.
				 */
    int minChannelWidth;	/* See above */
    int (*func)();		/* Applied to each Side found */
    ClientData cdata;		/* Passed to (*func)() */
{
    /* Create the yank buffer if it doesn't exist */
    if (rtrSideTransUse == NULL)
	DBNewYank("__side_def__", &rtrSideTransUse, &rtrSideTransDef);

    /* Initialize stuff to pass to our clients */
    rtrSideMinChanWidth = minChannelWidth;
    rtrSideFunc = func;
    rtrSideCdata = cdata;

    /*
     * Search each of four different directions for Sides.
     * To make matters easy, we transform the original cell tile
     * plane to make it always look as though we're searching for
     * Sides on the RHS of cells.
     */
    if (rtrSideProcess(use, GEO_EAST, area, &GeoIdentityTransform)) return 1;
    if (rtrSideProcess(use, GEO_WEST, area, &GeoSidewaysTransform)) return 1;
    if (rtrSideProcess(use, GEO_NORTH, area, &Geo270Transform)) return 1;
    if (rtrSideProcess(use, GEO_SOUTH, area, &Geo90Transform)) return 1;

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrSideProcess --
 *
 * Process all Sides that lie on 'side' of a cell (GEO_NORTH,
 * GEO_SOUTH, etc).  Does the real work of rtrEnumSides()
 * above.
 *
 * The caller must initialize (*rtrSideFunc)(), rtrSideCdata,
 * and rtrSideMinChanWidth before calling this procedure.
 *
 * Results:
 *	Returns 0 if we completed successfully, or 1 if (*func)()
 *	returned 1 or we were interrupted (SigInterruptPending != 0).
 *
 * Side effects:
 *	Whatever is done by the client procedure (*rtrSideFunc)()
 *	that was initialized by rtrEnumSides() above.  We use the
 *	ti_client fields of the tiles in the cell tile plane for
 *	markings, and reset them to their uninitialized value when
 *	done (CLIENTDEFAULT).  However, the cell plane of use->cu_def
 *	is only used when side == GEO_EAST; for other directions,
 *	the cell plane modified is that of rtrSideTransDef.
 *
 * ----------------------------------------------------------------------------
 */

int
rtrSideProcess(use, side, area, trans)
    CellUse *use;	/* Enumerating Sides of use->cu_def */
    int side;		/* Which sides (GEO_NORTH, etc) of cells to process */
    Rect *area;		/* Find sides in this area (in use->cu_def coords) */
    Transform *trans;	/* Transform from use->cu_def coords to those of the
			 * cell tile plane where we actually try to find Sides.
			 */
{
    SearchContext scx;
    int retval;

    rtrSideSide = side;
    GeoInvertTrans(trans, &rtrSideTrans);
    GeoTransRect(trans, area, &rtrSideArea);
    switch (side)
    {
	/* EAST (easy case since we don't have to transform) */
	case GEO_EAST:
	    rtrSideTransPlane = use->cu_def->cd_planes[PL_CELL];
	    break;

	/* Other cases use the transformed plane */
	case GEO_SOUTH:
	case GEO_NORTH:
	case GEO_WEST:
	    rtrSideTransPlane = rtrSideTransDef->cd_planes[PL_CELL];
	    scx.scx_area = *area;
	    scx.scx_use = use;
	    scx.scx_trans = *trans;
	    DBCellClearDef(rtrSideTransDef);
	    DBCellCopyCells(&scx, rtrSideTransUse, (Rect *) NULL);
	    break;
    }

    /* Initialize all client fields to NULL in the cell tile plane */
    (void) TiSrArea((Tile *) NULL, rtrSideTransPlane, &rtrSideArea,
	rtrSideInitClient, (ClientData) INFINITY);

    /* Process all Sides for this direction */
    retval = TiSrArea((Tile *) NULL, rtrSideTransPlane, &rtrSideArea,
		    rtrEnumSidesFunc, (ClientData) NULL);

    /* Clean up; be absolutely sure to reset client info */
    if (side == GEO_EAST)
    {
	SigDisableInterrupts();
	(void) TiSrArea((Tile *) NULL, rtrSideTransPlane, area,
	    rtrSideInitClient, (ClientData) CLIENTDEFAULT);
	SigEnableInterrupts();
    }

    return (retval);
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrSideInitClient --
 *
 * Called for each tile in the area over which Sides are being enumerated.
 * Initializes ti_client for each tile to the value cdata.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	Sets tile->ti_client.
 *
 * ----------------------------------------------------------------------------
 */

int
rtrSideInitClient(tile, client)
    Tile *tile;
    ClientData client;
{
    tile->ti_client = client;
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrEnumSidesFunc --
 *
 * Called for each tile during an area enumeration of the cell
 * tile plane rtrSideTransPlane.
 *
 * Results:
 *	Returns 0 if we completed successfully, or 1 if (*func)()
 *	returned 1 or we were interrupted (SigInterruptPending != 0).
 *
 * Side effects:
 *	Whatever is done by the client procedure.
 *	We use the ti_client fields of the tiles in the cell tile
 *	plane for markings, and reset them to their uninitialized
 *	value when done (CLIENTDEFAULT).
 *
 * ----------------------------------------------------------------------------
 */

int
rtrEnumSidesFunc(tile)
    Tile *tile;
{
    int ybot, ytop, yprev, sep, x, origin;
    Tile *tp, *tpB;
    Side side;

    /* Skip if already processed, out of the area, or not a cell tile */
    yprev = (int) tile->ti_client;
    ybot = MAX(BOTTOM(tile), rtrSideArea.r_ybot);
    if (yprev <= ybot || tile->ti_body == (ClientData) NULL
	    || RIGHT(tile) >= rtrSideArea.r_xtop)
	return (0);

    switch (rtrSideSide)
    {
	case GEO_NORTH:
	case GEO_SOUTH:
	    origin = RtrOrigin.p_y;
	    break;
	case GEO_EAST:
	case GEO_WEST:
	    origin = RtrOrigin.p_x;
	    break;
    }

    /*
     * Figure out the minimum width of space tiles we are willing to consider.
     * This is to ensure that there's at least rtrSideMinChanWidth worth of
     * usable channel space along this Side; if there isn't, we will terminate
     * each Side as soon as we hit a space tile that's too narrow.
     */
    x = RIGHT(tile);
    if (rtrSideMinChanWidth >= 0)
    {
	switch (rtrSideSide)
	{
	    case GEO_NORTH:
	    case GEO_EAST:
		x = RTR_GRIDUP(x + RtrSubcellSepUp, origin);
		x = RTR_GRIDUP(x + rtrSideMinChanWidth, origin)
			+ RtrSubcellSepDown;
		break;
	    case GEO_SOUTH:
	    case GEO_WEST:
		x = RTR_GRIDUP(x + RtrSubcellSepDown, origin);
		x = RTR_GRIDUP(x + rtrSideMinChanWidth, origin)
			+ RtrSubcellSepUp;
		break;
	}
    }

    /*
     * Walk down the outside of the RHS as far as possible,
     * first skipping all non-space tiles (or space tiles that
     * are too narrow) and then stopping when we come to usable
     * space or if the LHS of the outside tile changes from the
     * previous one.
     *
     * Once we've found the biggest stretch formed by the
     * space tiles on the RHS, we turn them into one or more
     * Sides by looking at the cell tiles on the LHS.
     */

    /*
     * Skip non-space tiles; give up if we leave the side
     * of the original cell tile since we'll be certain to
     * process the rest of the side when we see later tiles.
     */
    yprev = MIN(yprev, rtrSideArea.r_ytop);
    for (tp = TR(tile);
	    BOTTOM(tp) >= yprev || tp->ti_body || RIGHT(tp) < x;
	    tp = LB(tp))
    {
	if (LEFT(tp) != RIGHT(tile) || TOP(tp) <= ybot)
	{
	    /* Processed this tile completely */
	    tile->ti_client = (ClientData) ybot;
	    return (0);
	}
    }

    /*
     * Now tp is a usable space tile to the right of tile.
     * Continue walking down to find the longest stretch
     * of collinear space.
     */
    ytop = MIN(TOP(tile), TOP(tp));
    ytop = MIN(ytop, yprev);
    while (tp->ti_body == (ClientData) NULL && TOP(tp) > rtrSideArea.r_ybot
			       && LEFT(tp) == RIGHT(tile)
			       && RIGHT(tp) >= x)
    {
	tpB = tp;
	tp = LB(tp);
    }
    ybot = MAX(BOTTOM(tpB), rtrSideArea.r_ybot);

    side.side_trans = rtrSideTrans;
    side.side_side = rtrSideSide;
    side.side_next = (Side *) NULL;
    side.side_line.r_xbot = side.side_line.r_xtop = RIGHT(tile);
    side.side_line.r_ybot = side.side_line.r_ytop = ybot;
    side.side_search.r_xtop = RIGHT(tile);
    side.side_search.r_xbot = RIGHT(tile) - 1;
    side.side_used.r_xbot = RIGHT(tile);
    sep = (rtrSideSide == GEO_NORTH || rtrSideSide == GEO_EAST)
		? RtrSubcellSepUp
		: RtrSubcellSepDown;
    side.side_used.r_xtop = RTR_GRIDUP(RIGHT(tile) + sep, origin);

    /* Walk back up the inside (LHS) of the edge just found */
    for (tp = BL(tpB); BOTTOM(tp) < ytop; tp = RT(tp))
    {
	if (TOP(tp) > ybot)
	{
	    if (tp->ti_body == (ClientData) NULL)
	    {
		if (side.side_line.r_ytop > side.side_line.r_ybot)
		    if (rtrSidePassToClient(&side))
			return (1);
		side.side_line.r_ybot = TOP(tp);
	    }
	    else
	    {
		side.side_line.r_ytop = MIN(TOP(tp), ytop);
		tp->ti_client = (ClientData) ybot;
	    }
	}
    }

    /* If the Side extended all the way to the top */
    if (side.side_line.r_ytop > side.side_line.r_ybot)
	if (rtrSidePassToClient(&side))
	    return (1);

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrSidePassToClient --
 *
 * Make side->side_search and side->side_used cover the same
 * vertical span as side->side_line, and then call the client
 * procedure (*rtrSideFunc)().
 *
 * Results:
 *	Returns what the client procedure returned.
 *
 * Side effects:
 *	Whatever is done by the client procedure.
 *	See above for other effects.
 *
 * ----------------------------------------------------------------------------
 */

int
rtrSidePassToClient(side)
    Side *side;
{
    side->side_search.r_ybot = side->side_line.r_ybot;
    side->side_search.r_ytop = side->side_line.r_ytop;
    side->side_used.r_ybot = side->side_line.r_ybot;
    side->side_used.r_ytop = side->side_line.r_ytop;
    return ((*rtrSideFunc)(side, rtrSideCdata));
}

