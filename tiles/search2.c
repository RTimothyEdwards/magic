/*
 * search2.c --
 *
 * Area searching.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/tiles/search2.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif	/* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/signals.h"

/* -------------------- Local function headers ------------------------ */

int tiSrAreaEnum();

/*
 * --------------------------------------------------------------------
 *
 * TiSrArea --
 *
 * Find all tiles contained in or incident upon a given area.
 * Applies the given procedure to all tiles found.  The procedure
 * should be of the following form:
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
 * THIS PROCEDURE IS OBSOLETE EXCEPT FOR THE SUBCELL PLANE.  USE
 * DBSrPaintArea() IF YOU WANT TO SEARCH FOR PAINT TILES.
 *
 * Results:
 *	0 is returned if the search completed normally.  1 is returned
 *	if it aborted.
 *
 * Side effects:
 *	Whatever side effects result from application of the
 *	supplied procedure.
 *
 * NOTE:
 *	The procedure called is free to do anything it wishes to tiles
 *	which have already been visited in the area search, but it must
 *	not affect anything about tiles not visited other than possibly
 *	corner stitches to tiles already visited.
 *
 * *************************************************************************
 * *************************************************************************
 * ****									****
 * ****				  WARNING				****
 * ****									****
 * ****		This code is INCREDIBLY sensitive to modification!	****
 * ****		Change it only with the utmost caution, or you'll	****
 * ****		be verrry sorry!					****
 * ****									****
 * *************************************************************************
 * *************************************************************************
 *
 * --------------------------------------------------------------------
 */

int
TiSrArea(hintTile, plane, rect, func, arg)
    Tile *hintTile;	/* Tile at which to begin search, if not NULL.
			 * If this is NULL, use the hint tile supplied
			 * with plane.
			 */
    Plane *plane;	/* Plane in which tiles lie.  This is used to
			 * provide a hint tile in case hintTile == NULL.
			 * The hint tile in the plane is updated to be
			 * the last tile visited in the area enumeration.
			 */
    Rect *rect;/* Area to search */
    int (*func)();	/* Function to apply at each tile */
    ClientData arg;	/* Additional argument to pass to (*func)() */
{
    Point here;
    Tile *tp, *enumTR, *enumTile;
    int enumRight, enumBottom;

    /*
     * We will scan from top to bottom along the left hand edge
     * of the search area, searching for tiles.  Each tile we
     * find in this search will be enumerated.
     */

    here.p_x = rect->r_xbot;
    here.p_y = rect->r_ytop - 1;
    enumTile = hintTile ? hintTile : plane->pl_hint;
    GOTOPOINT(enumTile, &here);
    plane->pl_hint = enumTile;

    while (here.p_y >= rect->r_ybot)
    {
	if (SigInterruptPending) return 1;
	/*
	 * Find the tile (tp) immediately below the one to be
	 * enumerated (enumTile).  This must be done before we enumerate
	 * the tile, as the filter function applied to enumerate
	 * it can result in its deallocation or modification in
	 * some other way.
	 */
	here.p_y = BOTTOM(enumTile) - 1;
	tp = enumTile;
	GOTOPOINT(tp, &here);
	plane->pl_hint = tp;

	enumRight = RIGHT(enumTile);
	enumBottom = BOTTOM(enumTile);
	enumTR = TR(enumTile);
	if ((*func)(enumTile, arg)) return 1;

	/*
	 * If the right boundary of the tile being enumerated is
	 * inside of the search area, recursively enumerate
	 * tiles to its right.
	 */

	if (enumRight < rect->r_xtop)
	    if (tiSrAreaEnum(enumTR, enumBottom, rect, func, arg))
		return 1;
	enumTile = tp;
    }
    return 0;
}

/*
 * --------------------------------------------------------------------
 *
 * tiSrAreaEnum --
 *
 * Perform the recursive edge search of the tile which has just been
 * enumerated in an area search.  The arguments passed are the RT
 * corner stitch and bottom coordinate of the tile just enumerated.
 *
 * Results:
 *	0 is returned if the search completed normally, 1 if
 *	it was aborted.
 *
 * Side effects:
 *	Attempts to enumerate recursively each tile found in walking
 *	along the right edge of the tile just enumerated.  Whatever
 *	side effects occur result from the application of the client's
 *	filter function.
 *
 * --------------------------------------------------------------------
 */

int
tiSrAreaEnum(enumRT, enumBottom, rect, func, arg)
    Tile *enumRT;	/* TR corner stitch of tile just enumerated */
    int enumBottom;		/* Bottom coordinate of tile just enumerated */
    Rect *rect;	/* Area to search */
    int (*func)();		/* Function to apply at each tile */
    ClientData arg;		/* Additional argument to pass to (*func)() */
{
    Tile *tp, *tpLB, *tpTR;
    int tpRight, tpNextTop, tpBottom, srchBottom;
    int atBottom = (enumBottom <= rect->r_ybot);


    /*
     * Begin examination of tiles along right edge.
     * A tile to the right of the one being enumerated is enumerable if:
     *	- its bottom lies at or above that of the tile being enumerated, or,
     *	- the bottom of the tile being enumerated lies at or below the
     *	  bottom of the search rectangle.
     */

    if ((srchBottom = enumBottom) < rect->r_ybot)
	srchBottom = rect->r_ybot;

    for (tp = enumRT, tpNextTop = TOP(tp); tpNextTop > srchBottom; tp = tpLB)
    {
	if (SigInterruptPending) return 1;

	/*
	 * Since the client's filter function may result in this tile
	 * being deallocated or otherwise modified, we must extract
	 * all the information we will need from the tile before we
	 * apply the filter function.
	 */

	tpLB = LB(tp);
	tpNextTop = TOP(tpLB);	/* Since TOP(tpLB) comes from tp */

	if (BOTTOM(tp) < rect->r_ytop && (atBottom || BOTTOM(tp) >= enumBottom))
	{
	    /*
	     * We extract more information from the tile, which we will use
	     * after applying the filter function.
	     */

	    tpRight = RIGHT(tp);
	    tpBottom = BOTTOM(tp);
	    tpTR = TR(tp);
	    if ((*func)(tp, arg)) return 1;

	    /*
	     * If the right boundary of the tile being enumerated is
	     * inside of the search area, recursively enumerate
	     * tiles to its right.
	     */

	    if (tpRight < rect->r_xtop)
		if (tiSrAreaEnum(tpTR, tpBottom, rect, func, arg))
		    return 1;
	}
    }
    return 0;
}
