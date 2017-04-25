/*
 * DBtiles.c --
 *
 * Low-level tile primitives for the database.
 * This includes area searching and all other primitives that
 * need to know what lives in a tile body.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/database/DBtiles.c,v 1.2 2010/06/24 12:37:15 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/signals.h"
#include "utils/hash.h"
#include "database/database.h"
#include "database/databaseInt.h"
#include "utils/malloc.h"

/* Used by DBCheckMaxHStrips() and DBCheckMaxVStrips() */
struct dbCheck
{
    int		(*dbc_proc)();
    Rect	  dbc_area;
    ClientData    dbc_cdata;
};

int dbCheckMaxHFunc(), dbCheckMaxVFunc();

/*
 * --------------------------------------------------------------------
 *
 * DBSrPaintNMArea --
 *
 * Find all tiles overlapping a given triangular area whose types are
 * contained in the mask supplied.  Apply the given procedure to each
 * such tile.  The procedure should be of the following form:
 *
 *	int
 *	func(tile, cdata)
 *	    Tile *tile;
 *	    ClientData cdata;
 *	{
 *	}
 *
 * This is equivalent to DBSrPaintArea, but is used when only one
 * diagonal half (nonmanhattan extension) of the area should be searched.
 * The bitmask for the diagonal are passed in "ttype" (diagonal, side and
 * direction only are used, as in DBNMPaintPlane).
 *
 * Results:
 *	0 is returned if the search completed normally.  1 is returned
 *	if it aborted.
 *
 * Side effects:
 *	Whatever side effects result from application of the
 *	supplied procedure.
 *
 * Notes:
 *	Do not call this routine on an infinite search area (e.g., area
 *	== &TiPlaneRect), even if "ttype" is set appropriately.
 * --------------------------------------------------------------------
 */

#define IGNORE_LEFT 1
#define IGNORE_RIGHT 2

int
DBSrPaintNMArea(hintTile, plane, ttype, rect, mask, func, arg)
    Tile *hintTile;		/* Tile at which to begin search, if not NULL.
				 * If this is NULL, use the hint tile supplied
				 * with plane.
				 */
    Plane *plane;		/* Plane in which tiles lie.  This is used to
				 * provide a hint tile in case hintTile == NULL.
				 * The hint tile in the plane is updated to be
				 * the last tile visited in the area
				 * enumeration.
				 */
    TileType ttype;		/* Information about the non-manhattan area to
			 	 * search; zero if area is manhattan.
			 	 */
    Rect *rect;			/* Area to search.  This area should not be
				 * degenerate.  Tiles must OVERLAP the area.
				 */
    TileTypeBitMask *mask;	/* Mask of those paint tiles to be passed to
				 * func.
				 */
    int (*func)();		/* Function to apply at each tile */
    ClientData arg;		/* Additional argument to pass to (*func)() */
{
    Point start;
    Tile *tp, *tpnew;
    TileType tpt;
    int rheight, rwidth, rmax;
    dlong f1, f2, f3, f4;
    bool ignore_sides;

    /* If the search area is not diagonal, return the result of the	*/
    /* standard (manhattan) search function.				*/

    if (!(ttype & TT_DIAGONAL))
	return DBSrPaintArea(hintTile, plane, rect, mask, func, arg);

    start.p_x = rect->r_xbot;
    start.p_y = rect->r_ytop - 1;
    tp = hintTile ? hintTile : plane->pl_hint;
    GOTOPOINT(tp, &start);

    /* Each iteration visits another tile on the LHS of the search area */
    while (TOP(tp) > rect->r_ybot)
    {
	/* Each iteration enumerates another tile */
nm_enum:
	plane->pl_hint = tp;
	if (SigInterruptPending)
	    return (1);

	/* Check if the tile is in the (nonmanhattan) area, and continue */ 
	/* the tile enumeration if it is not.				 */
	/* Watch for calculations involving (M)INFINITY in tile (tp)!	 */

	rheight = rect->r_ytop - rect->r_ybot;
	rwidth = rect->r_xtop - rect->r_xbot;
	rmax = MAX(rheight, rwidth);
	f1 = (BOTTOM(tp) > MINFINITY + 2) ?
		((dlong)(rect->r_ytop - BOTTOM(tp)) * rwidth) : DLONG_MAX;
	f2 = (TOP(tp) < INFINITY - 2) ?
		((dlong)(TOP(tp) - rect->r_ybot) * rwidth) : DLONG_MAX;

	if (ttype & TT_SIDE)
	{
	    /* Outside-of-triangle check---ignore sub-integer slivers */
	    if (RIGHT(tp) < INFINITY - 2)
	    {
		f3 = (dlong)(rect->r_xtop - RIGHT(tp)) * rheight;
		f3 += rmax;
	    }
	    else
		f3 = DLONG_MIN;
	    if ((ttype & TT_DIRECTION) ? (f2 < f3) : (f1 < f3))
		goto enum_next;
	}
	else
	{
	    /* Outside-of-triangle check---ignore sub-integer slivers */
	    if (LEFT(tp) > MINFINITY + 2)
	    {
		f4 = (dlong)(LEFT(tp) - rect->r_xbot) * rheight;
		f4 += rmax;
	    }
	    else
		f4 = DLONG_MIN;
	    if ((ttype & TT_DIRECTION) ? (f1 < f4) : (f2 < f4))
		goto enum_next;
	}

	/* Secondary checks---if tile is also non-Manhattan, is		*/
	/* either side of it outside the area?  If so, restrict it.	*/
	/* This check is only necessary if the split directions are	*/
	/* the same, so we have to see if either of the neighboring	*/
	/* points is also inside the search triangle.			*/

	ignore_sides = 0;

	if (IsSplit(tp))
	{
	    if (!TTMaskHasType(mask, SplitLeftType(tp)))
		ignore_sides |= IGNORE_LEFT;
	    if (!TTMaskHasType(mask, SplitRightType(tp)))
		ignore_sides |= IGNORE_RIGHT;

	    tpt = TiGetTypeExact(tp);
	    if ((tpt & TT_DIRECTION) == (ttype & TT_DIRECTION))
	    {
		f3 = (LEFT(tp) > MINFINITY + 2) ?
			((dlong)(rect->r_xtop - LEFT(tp)) * rheight) : DLONG_MAX;
		f4 = (RIGHT(tp) < INFINITY - 2) ?
			((dlong)(RIGHT(tp) - rect->r_xbot) * rheight) : DLONG_MAX;

		if (ttype & TT_SIDE)
		{
		    /* Ignore sub-integer slivers */
		    if (f4 != DLONG_MAX) f4 -= rmax;
		    if (f3 != DLONG_MAX) f3 += rmax;
		    
		    if (ttype & TT_DIRECTION)
		    {
			if ((f2 < f3) && (f1 > f4))
			    /* Tile bottom left is outside search area */
			    ignore_sides |= IGNORE_LEFT;
		    }
		    else
		    {
			if ((f1 < f3) && (f2 > f4))
			    /* Tile top left is outside search area */
			    ignore_sides |= IGNORE_LEFT;
		    }
		}
		else
		{
		    /* Ignore sub-integer slivers */
		    if (f4 != DLONG_MAX) f4 += rmax;
		    if (f3 != DLONG_MAX) f3 -= rmax;
		    
		    if (ttype & TT_DIRECTION)
		    {
			if ((f2 > f3) && (f1 < f4))
			    /* Tile top right is outside search area */
			    ignore_sides |= IGNORE_RIGHT;
		    }
		    else
		    {
			if ((f1 > f3) && (f2 < f4))
			    /* Tile bottom right is outside search area */
			    ignore_sides |= IGNORE_RIGHT;
		    }
		}
	    }

	    /* If the tile is larger than the search area or overlaps	*/
	    /* the search area, we need to check if one of the sides	*/
	    /* of the tile is disjoint from the search area.		*/

	    rheight = TOP(tp) - BOTTOM(tp);
	    rwidth = RIGHT(tp) - LEFT(tp);
	    rmax = MAX(rheight, rwidth);
	    f1 = (TOP(tp) < INFINITY - 2) ?
			((dlong)(TOP(tp) - rect->r_ybot) * rwidth) : DLONG_MAX;
	    f2 = (BOTTOM(tp) > MINFINITY + 2) ?
			((dlong)(rect->r_ytop - BOTTOM(tp)) * rwidth) : DLONG_MAX;
	    f3 = (RIGHT(tp) < INFINITY - 2) ?
			((dlong)(RIGHT(tp) - rect->r_xtop) * rheight) : DLONG_MAX;
	    f4 = (LEFT(tp) > MINFINITY + 2) ?
			((dlong)(rect->r_xbot - LEFT(tp)) * rheight) : DLONG_MAX;

	    /* ignore sub-integer slivers */
	    if (f4 < DLONG_MAX) f4 += rmax;
	    if (f3 < DLONG_MAX) f3 += rmax;

	    if (SplitDirection(tp) ? (f1 < f4) : (f2 < f4))
		ignore_sides |= IGNORE_LEFT;

	    if (SplitDirection(tp) ? (f2 < f3) : (f1 < f3))
		ignore_sides |= IGNORE_RIGHT;

	    /* May call function twice to paint both sides of   */
	    /* the split tile, if necessary.                    */

	    if (!(ignore_sides & IGNORE_LEFT))
	    {
		TiSetBody(tp, (ClientData)(tpt & ~TT_SIDE)); /* bit clear */
		if ((*func)(tp, arg)) return (1);
	    }
	    if (!(ignore_sides & IGNORE_RIGHT))
	    {
		TiSetBody(tp, (ClientData)(tpt | TT_SIDE)); /* bit set */
		if ((*func)(tp, arg)) return (1);
	    }
        }
	else
	    if (TTMaskHasType(mask, TiGetType(tp)) && (*func)(tp, arg))
		return (1);

enum_next:
	tpnew = TR(tp);
	if (LEFT(tpnew) < rect->r_xtop)
	{
	    while (BOTTOM(tpnew) >= rect->r_ytop) tpnew = LB(tpnew);
	    if (BOTTOM(tpnew) >= BOTTOM(tp) || BOTTOM(tp) <= rect->r_ybot)
	    {
		tp = tpnew;
		goto nm_enum;
	    }
	} 

	/* Each iteration returns one tile further to the left */
	while (LEFT(tp) > rect->r_xbot)
	{
	    if (BOTTOM(tp) <= rect->r_ybot) 
		return (0);
	    tpnew = LB(tp);
	    tp = BL(tp);
	    if (BOTTOM(tpnew) >= BOTTOM(tp) || BOTTOM(tp) <= rect->r_ybot)
	    {
		tp = tpnew;
		goto nm_enum;
	    }
	}

	/* At left edge -- walk down to next tile along the left edge */
	for (tp = LB(tp); RIGHT(tp) <= rect->r_xbot; tp = TR(tp))
	    /* Nothing */;
    }
    return (0);
}


/*
 * --------------------------------------------------------------------
 *
 * DBSrPaintArea --
 *
 * Find all tiles overlapping a given area whose types are contained
 * in the mask supplied.  Apply the given procedure to each such tile.
 * The procedure should be of the following form:
 *
 *	int
 *	func(tile, cdata)
 *	    Tile *tile;
 *	    ClientData cdata;
 *	{
 *	}
 *
 * Func normally should return 0.  If it returns 1 then the search
 * will be aborted.  WARNING: THE CALLED PROCEDURE MAY NOT MODIFY
 * THE PLANE BEING SEARCHED!!!
 *
 *			NOTE:
 *
 * THIS IS THE PREFERRED WAY TO FIND ALL TILES IN A GIVEN AREA;
 * TiSrArea IS OBSOLETE FOR ALL BUT THE SUBCELL PLANE.
 *
 * Results:
 *	0 is returned if the search completed normally.  1 is returned
 *	if it aborted.
 *
 * Side effects:
 *	Whatever side effects result from application of the
 *	supplied procedure.
 *
 * --------------------------------------------------------------------
 */

int
DBSrPaintArea(hintTile, plane, rect, mask, func, arg)
    Tile *hintTile;		/* Tile at which to begin search, if not NULL.
				 * If this is NULL, use the hint tile supplied
				 * with plane.
				 */
    Plane *plane;	/* Plane in which tiles lie.  This is used to
				 * provide a hint tile in case hintTile == NULL.
				 * The hint tile in the plane is updated to be
				 * the last tile visited in the area
				 * enumeration.
				 */
    Rect *rect;	/* Area to search.  This area should not be
				 * degenerate.  Tiles must OVERLAP the area.
				 */
    TileTypeBitMask *mask;	/* Mask of those paint tiles to be passed to
				 * func.
				 */
    int (*func)();		/* Function to apply at each tile */
    ClientData arg;		/* Additional argument to pass to (*func)() */
{
    Point start;
    Tile *tp, *tpnew;

    start.p_x = rect->r_xbot;
    start.p_y = rect->r_ytop - 1;
    tp = hintTile ? hintTile : plane->pl_hint;
    GOTOPOINT(tp, &start);

    /* Each iteration visits another tile on the LHS of the search area */
    while (TOP(tp) > rect->r_ybot)
    {
	/* Each iteration enumerates another tile */
enumerate:
	plane->pl_hint = tp;
	if (SigInterruptPending)
	    return (1);

	/* Only perform func() on diagonal tiles if the mask includes the  */
	/* tile type for either the left or right sides of the tile (could */
	/* also use top and bottom)---by definition, opposite sides must   */
	/* be the two tile types defined by the diagonal split.            */
	if (IsSplit(tp))
	{
	    /* May call function twice to paint both sides of   */
	    /* the split tile, if necessary.                    */

	    /* f1 to f4 are used to find if the search box rect */
	    /* is completely outside the triangle.  If so, do   */
	    /* not call the function func().                    */

	    /* Watch for calculations involving (M)INFINITY     */

	    int theight, twidth;
	    dlong f1, f2, f3, f4;

	    theight = TOP(tp) - BOTTOM(tp);
	    twidth = RIGHT(tp) - LEFT(tp);
	    f1 = (rect->r_ybot > MINFINITY + 2) ?
		(dlong)(TOP(tp) - rect->r_ybot) * twidth : DLONG_MAX;
	    f2 = (rect->r_ytop < INFINITY - 2) ?
		(dlong)(rect->r_ytop - BOTTOM(tp)) * twidth : DLONG_MAX;

	    if (TTMaskHasType(mask, SplitLeftType(tp)))
	    {
		/* !Outside-of-triangle check */
		f4 = (rect->r_xbot > MINFINITY + 2) ?
			(dlong)(rect->r_xbot - LEFT(tp)) * theight : DLONG_MIN;
		if (SplitDirection(tp) ? (f1 > f4) : (f2 > f4))
		{
		    TiSetBody(tp, (ClientData)((TileType)TiGetBody(tp)
				& ~TT_SIDE));  /* bit clear */
		    if ((*func)(tp, arg)) return (1);
		}
	    }

	    if (TTMaskHasType(mask, SplitRightType(tp)))
	    {
		/* !Outside-of-triangle check */
		f3 = (rect->r_xtop < INFINITY - 2) ?
			(dlong)(RIGHT(tp) - rect->r_xtop) * theight : DLONG_MIN;
		if (SplitDirection(tp) ? (f2 > f3) : (f1 > f3))
		{
		    TiSetBody(tp, (ClientData)((TileType)TiGetBody(tp)
				| TT_SIDE));      /* bit set */
		    if ((*func)(tp, arg)) return (1);
		}
	    }
	}
	else
	    if (TTMaskHasType(mask, TiGetType(tp)) && (*func)(tp, arg))
		return (1);

	tpnew = TR(tp);
	if (LEFT(tpnew) < rect->r_xtop)
	{
	    while (BOTTOM(tpnew) >= rect->r_ytop) tpnew = LB(tpnew);
	    if (BOTTOM(tpnew) >= BOTTOM(tp) || BOTTOM(tp) <= rect->r_ybot)
	    {
		tp = tpnew;
		goto enumerate;
	    }
	} 

	/* Each iteration returns one tile further to the left */
	while (LEFT(tp) > rect->r_xbot)
	{
	    if (BOTTOM(tp) <= rect->r_ybot) 
		return (0);
	    tpnew = LB(tp);
	    tp = BL(tp);
	    if (BOTTOM(tpnew) >= BOTTOM(tp) || BOTTOM(tp) <= rect->r_ybot)
	    {
		tp = tpnew;
		goto enumerate;
	    }
	}

	/* At left edge -- walk down to next tile along the left edge */
	for (tp = LB(tp); RIGHT(tp) <= rect->r_xbot; tp = TR(tp))
	    /* Nothing */;
    }
    return (0);
}


/*
 * --------------------------------------------------------------------
 *
 * DBSrPaintClient --
 *
 * Find all tiles overlapping a given area whose types are contained
 * in the mask supplied, and whose ti_client field matches 'client'.
 * Apply the given procedure to each such tile.  The procedure should
 * be of the following form:
 *
 *	int
 *	func(tile, cdata)
 *	    Tile *tile;
 *	    ClientData cdata;
 *	{
 *	}
 *
 * Func normally should return 0.  If it returns 1 then the search
 * will be aborted.
 *
 * Results:
 *	0 is returned if the search completed normally.  1 is returned
 *	if it aborted.
 *
 * Side effects:
 *	Whatever side effects result from application of the
 *	supplied procedure.
 *
 * --------------------------------------------------------------------
 */

int
DBSrPaintClient(hintTile, plane, rect, mask, client, func, arg)
    Tile *hintTile;		/* Tile at which to begin search, if not NULL.
				 * If this is NULL, use the hint tile supplied
				 * with plane.
				 */
    Plane *plane;	/* Plane in which tiles lie.  This is used to
				 * provide a hint tile in case hintTile == NULL.
				 * The hint tile in the plane is updated to be
				 * the last tile visited in the area
				 * enumeration.
				 */
    Rect *rect;	/* Area to search.  This area should not be
				 * degenerate.  Tiles must OVERLAP the area.
				 */
    TileTypeBitMask *mask;	/* Mask of those paint tiles to be passed to
				 * func.
				 */
    ClientData client;		/* The ti_client field of each tile must
				 * match this.
				 */
    int (*func)();		/* Function to apply at each tile */
    ClientData arg;		/* Additional argument to pass to (*func)() */
{
    Point start;
    Tile *tp, *tpnew;

    start.p_x = rect->r_xbot;
    start.p_y = rect->r_ytop - 1;
    tp = hintTile ? hintTile : plane->pl_hint;
    GOTOPOINT(tp, &start);

    /* Each iteration visits another tile on the LHS of the search area */
    while (TOP(tp) > rect->r_ybot)
    {
	/* Each iteration enumerates another tile */
enumerate:
	plane->pl_hint = tp;
	if (SigInterruptPending)
	    return (1);

	/* Only perform func() on diagonal tiles if the mask includes the  */
	/* tile type for either the left or right sides of the tile (could */
	/* also use top and bottom)---by definition, opposite sides must   */
	/* be the two tile types defined by the diagonal split.            */
	if (IsSplit(tp))
	{
	    /* May call function twice to paint both sides of   */
	    /* the split tile, if necessary.                    */

	    /* f1 to f4 are used to find if the search box rect */
	    /* is completely outside the triangle.  If so, do   */
	    /* not call the function func().                    */

	    /* Watch for calculations involving (M)INFINITY     */

	    int theight, twidth;
	    dlong f1, f2, f3, f4;

	    theight = TOP(tp) - BOTTOM(tp);
	    twidth = RIGHT(tp) - LEFT(tp);
	    f1 = (rect->r_ybot > MINFINITY + 2) ?
		(TOP(tp) - rect->r_ybot) * twidth : DLONG_MAX;
	    f2 = (rect->r_ytop < INFINITY - 2) ?
		(rect->r_ytop - BOTTOM(tp)) * twidth : DLONG_MAX;

	    if (TTMaskHasType(mask, SplitLeftType(tp)))
	    {
		/* !Outside-of-triangle check */
		f4 = (rect->r_xbot > MINFINITY + 2) ?
			(rect->r_xbot - LEFT(tp)) * theight : DLONG_MIN;
		if (SplitDirection(tp) ? (f1 > f4) : (f2 > f4))
		{
		    TiSetBody(tp, (ClientData)((TileType)TiGetBody(tp)
				& ~TT_SIDE));  /* bit clear */
		    if ((tp->ti_client == client) && (*func)(tp, arg))
			return (1);
		}
	    }

	    if (TTMaskHasType(mask, SplitRightType(tp)))
	    {
		/* !Outside-of-triangle check */
		f3 = (rect->r_xtop < INFINITY - 2) ?
			(RIGHT(tp) - rect->r_xtop) * theight : DLONG_MIN;
		if (SplitDirection(tp) ? (f2 > f3) : (f1 > f3))
		{
		    TiSetBody(tp, (ClientData)((TileType)TiGetBody(tp)
				| TT_SIDE));      /* bit set */
		    if ((tp->ti_client == client) && (*func)(tp, arg))
			return (1);
		}
	    }
	}
	else
	    if (TTMaskHasType(mask, TiGetType(tp)) && tp->ti_client == client
				&& (*func)(tp, arg))
		return (1);

	tpnew = TR(tp);
	if (LEFT(tpnew) < rect->r_xtop)
	{
	    while (BOTTOM(tpnew) >= rect->r_ytop) tpnew = LB(tpnew);
	    if (BOTTOM(tpnew) >= BOTTOM(tp) || BOTTOM(tp) <= rect->r_ybot)
	    {
		tp = tpnew;
		goto enumerate;
	    }
	}

	/* Each iteration returns one tile further to the left */
	while (LEFT(tp) > rect->r_xbot)
	{
	    if (BOTTOM(tp) <= rect->r_ybot)
		return (0);
	    tpnew = LB(tp);
	    tp = BL(tp);
	    if (BOTTOM(tpnew) >= BOTTOM(tp) || BOTTOM(tp) <= rect->r_ybot)
	    {
		tp = tpnew;
		goto enumerate;
	    }
	}

	/* At left edge -- walk down to next tile along the left edge */
	for (tp = LB(tp); RIGHT(tp) <= rect->r_xbot; tp = TR(tp))
	    /* Nothing */;
    }
    return (0);
}

/*
 * --------------------------------------------------------------------
 *
 * DBResetTilePlane --
 *
 * Reset the ti_client fields of all tiles in a paint tile plane to
 * the value 'cdata'.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Resets the ti_client fields of all tiles.
 *
 * --------------------------------------------------------------------
 */

void
DBResetTilePlane(plane, cdata)
    Plane *plane;	/* Plane whose tiles are to be reset */
    ClientData cdata;
{
    Tile *tp, *tpnew;
    Rect *rect = &TiPlaneRect;

    /* Start with the leftmost non-infinity tile in the plane */
    tp = TR(plane->pl_left);

    /* Each iteration visits another tile on the LHS of the search area */
    while (TOP(tp) > rect->r_ybot)
    {
	/* Each iteration frees another tile */
enumerate:
	tp->ti_client = cdata;

	/* Move along to the next tile */
	tpnew = TR(tp);
	if (LEFT(tpnew) < rect->r_xtop)
	{
	    while (BOTTOM(tpnew) >= rect->r_ytop) tpnew = LB(tpnew);
	    if (BOTTOM(tpnew) >= BOTTOM(tp) || BOTTOM(tp) <= rect->r_ybot)
	    {
		tp = tpnew;
		goto enumerate;
	    }
	}

	/* Each iteration returns one tile further to the left */
	while (LEFT(tp) > rect->r_xbot)
	{
	    if (BOTTOM(tp) <= rect->r_ybot)
		return;
	    tpnew = LB(tp);
	    tp = BL(tp);
	    if (BOTTOM(tpnew) >= BOTTOM(tp) || BOTTOM(tp) <= rect->r_ybot)
	    {
		tp = tpnew;
		goto enumerate;
	    }
	}

	/* At left edge -- walk down to next tile along the left edge */
	for (tp = LB(tp); RIGHT(tp) <= rect->r_xbot; tp = TR(tp))
	    /* Nothing */;
    }
}

/*
 * --------------------------------------------------------------------
 *
 * DBFreePaintPlane --
 *
 * Deallocate all tiles in a paint tile plane of a given CellDef.
 * Don't deallocate the four boundary tiles, or the plane itself.
 *
 * This is a procedure internal to the database.  The only reason
 * it lives in DBtiles.c rather than DBcellsubr.c is that it requires
 * intimate knowledge of the contents of paint tiles and tile planes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Deallocates a lot of memory.  
 *
 *			*** WARNING ***
 *
 * This procedure uses a carfully constructed non-recursive area 
 * enumeration algorithm.  Care is taken to not access a tile that has
 * been deallocated.  The only exception is for a tile that has just been
 * passed to free(), but no more calls to free() or malloc() have been made.  
 * Magic's malloc allows this.
 *
 * --------------------------------------------------------------------
 */

void
DBFreePaintPlane(plane)
    Plane *plane;	/* Plane whose storage is to be freed */
{
    Tile *tp, *tpnew;
    Rect *rect = &TiPlaneRect;

    /* Start with the bottom-right non-infinity tile in the plane */
    tp = BL(plane->pl_right);

    /* Each iteration visits another tile on the RHS of the search area */
    while (BOTTOM(tp) < rect->r_ytop)
    {
enumerate:

#define	CLIP_TOP(t)	(MIN(TOP(t),rect->r_ytop))

	/* Move along to the next tile to the left */
	if (LEFT(tp) > rect->r_xbot)
	{
	    tpnew = BL(tp);
	    while (TOP(tpnew) <= rect->r_ybot) tpnew = RT(tpnew);
	    if (CLIP_TOP(tpnew) <= CLIP_TOP(tp))
	    {
		tp = tpnew;
		goto enumerate;
	    }
	}

	/* Each iteration returns one tile further to the right */
	while (RIGHT(tp) < rect->r_xtop)
	{
	    TiFree(tp); 
	    tpnew = RT(tp);
	    tp = TR(tp);
	    if (CLIP_TOP(tpnew) <= CLIP_TOP(tp) && BOTTOM(tpnew) < rect->r_ytop)
	    {
		tp = tpnew;
		goto enumerate;
	    }
	}

	TiFree(tp); 
	/* At right edge -- walk up to next tile along the right edge */
	tp = RT(tp);
	if (BOTTOM(tp) < rect->r_ytop) {
	    while(LEFT(tp) >= rect->r_xtop) tp = BL(tp);
	}
    }
}

/*
 * --------------------------------------------------------------------
 *
 * DBFreeCellPlane --
 *
 * Deallocate all tiles in the cell tile plane of a given CellDef.
 * Also deallocates the lists of CellTileBodies and their associated
 * CellUses, but not their associated CellDefs.
 * Don't free the cell tile plane itself or the four boundary tiles.
 *
 * Since cell tile planes contain less stuff than paint tile planes
 * usually, we don't have to be as performance-conscious here.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Deallocates a lot of memory.  
 *
 * --------------------------------------------------------------------
 */

void
DBFreeCellPlane(plane)
    Plane *plane;	/* Plane whose storage is to be freed */
{
    int dbFreeCellFunc();

    /* Don't let this search be interrupted. */

    SigDisableInterrupts();
    (void) TiSrArea((Tile *) NULL, plane, &TiPlaneRect,
	    dbFreeCellFunc, (ClientData) NULL);
    SigEnableInterrupts();
}

/*
 * Filter function called via TiSrArea on behalf of DBFreeCellPlane()
 * above.  Deallocates each tile it is passed.  If the tile has a vanilla
 * body, only the tile is deallocated; otherwise, the tile body and its
 * label list are both deallocated along with the tile itself.
 */

int
dbFreeCellFunc(tile)
    Tile *tile;
{
    CellTileBody *body;
    CellUse *use;
    Rect *bbox;

    for (body = (CellTileBody *) TiGetBody(tile);
	    body != NULL;
	    body = body->ctb_next)
    {
	use = body->ctb_use;
	ASSERT(use != (CellUse *) NULL, "dbCellSrFunc");

	bbox = &use->cu_bbox;
	if ((BOTTOM(tile) <= bbox->r_ybot) && (RIGHT(tile) >= bbox->r_xtop))
	{
	    /* The parent must be null before DBCellDeleteUse will work */
	    use->cu_parent = (CellDef *) NULL;
	    DBCellDeleteUse(use);
	}
	freeMagic((char *)body);
    }

    TiFree(tile);
    return 0;
}

/*
 * --------------------------------------------------------------------
 *
 * DBCheckMaxHStrips --
 *
 * Check the maximal horizontal strip property for the
 * tile plane 'plane' over the area 'area'.
 *
 * Results:
 *	Normally returns 0; returns 1 if the procedure
 *	(*proc)() returned 1 or if the search were
 *	aborted with an interrupt.
 *
 * Side effects:
 *	Calls the procedure (*proc)() for each offending tile.
 *	This procedure should have the following form:
 *
 *	int
 *	proc(tile, side, cdata)
 *	    Tile *tile;
 *	    int side;
 *	    ClientData cdata;
 *	{
 *	}
 *
 *	The client data is the argument 'cdata' passed to us.
 *	The argument 'side' is one of GEO_NORTH, GEO_SOUTH,
 *	GEO_EAST, or GEO_WEST, and indicates which side of
 *	the tile the strip property was violated on.
 *	If (*proc)() returns 1, we abort and return 1
 *	to our caller.
 *
 * --------------------------------------------------------------------
 */

int
DBCheckMaxHStrips(plane, area, proc, cdata)
    Plane *plane;	/* Search this plane */
    Rect *area;		/* Process all tiles in this area */
    int (*proc)();	/* Filter procedure: see above */
    ClientData cdata;	/* Passed to (*proc)() */
{
    struct dbCheck dbc;

    dbc.dbc_proc = proc;
    dbc.dbc_area = *area;
    dbc.dbc_cdata = cdata;
    return (DBSrPaintArea((Tile *) NULL, plane, area,
		&DBAllTypeBits, dbCheckMaxHFunc, (ClientData) &dbc));
}

/*
 * dbCheckMaxHFunc --
 *
 * Filter function for above.
 * See the description at the top.
 */

int
dbCheckMaxHFunc(tile, dbc)
    Tile *tile;
    struct dbCheck *dbc;
{
    Tile *tp;

    /*
     * Property 1:
     * No tile to the left or to the right should have the same
     * type as 'tile'.
     */
    if (RIGHT(tile) < dbc->dbc_area.r_xtop)
	for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp = LB(tp))
	    if (TiGetType(tp) == TiGetType(tile))
		if ((*dbc->dbc_proc)(tile, GEO_EAST, dbc->dbc_cdata))
		    return (1);
    if (LEFT(tile) > dbc->dbc_area.r_xbot)
	for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
	    if (TiGetType(tp) == TiGetType(tile))
		if ((*dbc->dbc_proc)(tile, GEO_WEST, dbc->dbc_cdata))
		    return (1);

    /*
     * Property 2:
     * No tile to the top or bottom should be of the same type and
     * have the same width.
     */
    if (TOP(tile) < dbc->dbc_area.r_ytop)
    {
	tp = RT(tile);
	if (TiGetType(tp) == TiGetType(tile)
		&& LEFT(tp) == LEFT(tile)
		&& RIGHT(tp) == RIGHT(tile))
	    if ((*dbc->dbc_proc)(tile, GEO_NORTH, dbc->dbc_cdata))
		return (1);
    }
    if (BOTTOM(tile) > dbc->dbc_area.r_ybot)
    {
	tp = LB(tile);
	if (TiGetType(tp) == TiGetType(tile)
		&& LEFT(tp) == LEFT(tile)
		&& RIGHT(tp) == RIGHT(tile))
	    if ((*dbc->dbc_proc)(tile, GEO_SOUTH, dbc->dbc_cdata))
		return (1);
    }

    return (0);
}

/*
 * --------------------------------------------------------------------
 *
 * DBCheckMaxVStrips --
 *
 * Check the maximal vertical strip property for the
 * tile plane 'plane' over the area 'area'.
 *
 * Results:
 *	Normally returns 0; returns 1 if the procedure
 *	(*proc)() returned 1 or if the search were
 *	aborted with an interrupt.
 *
 * Side effects:
 *	See DBCheckMaxHStrips() above.
 *
 * --------------------------------------------------------------------
 */

int
DBCheckMaxVStrips(plane, area, proc, cdata)
    Plane *plane;	/* Search this plane */
    Rect *area;		/* Process all tiles in this area */
    int (*proc)();	/* Filter procedure: see above */
    ClientData cdata;	/* Passed to (*proc)() */
{
    struct dbCheck dbc;

    dbc.dbc_proc = proc;
    dbc.dbc_area = *area;
    dbc.dbc_cdata = cdata;
    return (DBSrPaintArea((Tile *) NULL, plane, area,
		&DBAllTypeBits, dbCheckMaxVFunc, (ClientData) &dbc));
}

/*
 * dbCheckMaxVFunc --
 *
 * Filter function for above.
 * See the description at the top.
 */

int
dbCheckMaxVFunc(tile, dbc)
    Tile *tile;
    struct dbCheck *dbc;
{
    Tile *tp;

    /*
     * Property 1:
     * No tile to the top or to the bottom should have the same
     * type as 'tile'.
     */
    if (TOP(tile) < dbc->dbc_area.r_ytop)
	for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp = BL(tp))
	    if (TiGetType(tp) == TiGetType(tile))
		if ((*dbc->dbc_proc)(tile, GEO_NORTH, dbc->dbc_cdata))
		    return (1);
    if (BOTTOM(tile) > dbc->dbc_area.r_ybot)
	for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp = TR(tp))
	    if (TiGetType(tp) == TiGetType(tile))
		if ((*dbc->dbc_proc)(tile, GEO_SOUTH, dbc->dbc_cdata))
		    return (1);

    /*
     * Property 2:
     * No tile to the left or right should be of the same type and
     * have the same height.
     */
    if (RIGHT(tile) < dbc->dbc_area.r_xtop)
    {
	tp = TR(tile);
	if (TiGetType(tp) == TiGetType(tile)
		&& BOTTOM(tp) == BOTTOM(tile)
		&& TOP(tp) == TOP(tile))
	    if ((*dbc->dbc_proc)(tile, GEO_EAST, dbc->dbc_cdata))
		return (1);
    }
    if (LEFT(tile) > dbc->dbc_area.r_xbot)
    {
	tp = BL(tile);
	if (TiGetType(tp) == TiGetType(tile)
		&& BOTTOM(tp) == BOTTOM(tile)
		&& TOP(tp) == TOP(tile))
	    if ((*dbc->dbc_proc)(tile, GEO_WEST, dbc->dbc_cdata))
		return (1);
    }

    return (0);
}

