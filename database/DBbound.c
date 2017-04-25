/*
 * DBbound.c --
 *
 * Computation of boundaries of a database tile plane.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/database/DBbound.c,v 1.2 2008/12/11 04:20:04 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include "utils/magic.h"
#include "utils/geometry.h"
#include "database/database.h"
#include "tiles/tile.h"


typedef struct dbcellboundstruct
{
    Rect *area;
    bool extended;
    bool found;
} DBCellBoundStruct;

/*
 * --------------------------------------------------------------------
 * --------------------------------------------------------------------
 */

int
DBBoundCellPlane(def, extended, rect)
    CellDef *def;
    bool extended;
    Rect *rect;
{
    TreeFilter filter;
    DBCellBoundStruct cbs;
    int dbCellBoundFunc();
    Plane *plane;
    
    filter.tf_func = NULL;
    filter.tf_arg = (ClientData)&cbs;

    cbs.area = rect;
    cbs.extended = extended;
    cbs.found = FALSE;

    *rect = GeoNullRect;
    if (TiSrArea((Tile *)NULL, def->cd_planes[PL_CELL],
		&TiPlaneRect, dbCellBoundFunc, (ClientData) &filter) == 0)
	return cbs.found;
    else
	return -1;
}

int
dbCellBoundFunc(tile, fp)
    Tile *tile;
    TreeFilter *fp;
{
    CellUse *use;
    CellTileBody *body;
    Rect *bbox;
    DBCellBoundStruct *cbs;

    cbs = (DBCellBoundStruct *)fp->tf_arg;

    for (body = (CellTileBody *) TiGetBody(tile); body != NULL;
		body = body->ctb_next)
    {
	use = body->ctb_use;
	bbox = &use->cu_bbox;
	if ((BOTTOM(tile) <= bbox->r_ybot) && (RIGHT(tile) >= bbox->r_xtop))
 	{
	    if (cbs->found)
	    {
		if (cbs->extended)
		    GeoInclude(&use->cu_extended, cbs->area);
		else
		    GeoInclude(&use->cu_bbox, cbs->area);
	    }
	    else
	    {
		if (cbs->extended)
		    *cbs->area = use->cu_extended;
		else
		    *cbs->area = use->cu_bbox;
		cbs->found = TRUE;
	    }
	}
    }
    return 0;
}

/*
 * --------------------------------------------------------------------
 *
 * DBBoundPlane --
 *
 * Determine the bounding rectangle for the supplied tile plane.
 * The bounding rectangle is the smallest rectangle that completely
 * encloses all non-space tiles.
 *
 * If the tile plane is completely empty, we return a 0x0 bounding
 * box at the origin.
 *
 * Results:
 *	TRUE if the tile plane contains any geometry, FALSE
 *	if it is completely empty.
 *
 * Side effects:
 *	Sets *rect to the bounding rectangle.
 *
 * --------------------------------------------------------------------
 */

bool
DBBoundPlane(plane, rect)
    Plane *plane;
    Rect *rect;
{
    Tile *left, *right, *top, *bottom, *tp;

    left = plane->pl_left;
    right = plane->pl_right;
    top = plane->pl_top;
    bottom = plane->pl_bottom;

    rect->r_ur = TiPlaneRect.r_ll;
    rect->r_ll = TiPlaneRect.r_ur;

    /*
     * To find the rightmost and leftmost solid edges, we
     * scan along the respective edges.  Our assumption is
     * that the only tiles along the edges are space tiles,
     * which, by the maximum horizontal strip property, must
     * have either solid tiles or the edge of the plane on
     * their other sides.
     */

    for (tp = TR(left); tp != bottom; tp = LB(tp))
	if (RIGHT(tp) < rect->r_xbot)
	    rect->r_xbot = RIGHT(tp);

    for (tp = BL(right); tp != top; tp = RT(tp))
	if (LEFT(tp) > rect->r_xtop)
	    rect->r_xtop = LEFT(tp);

    /*
     * We assume that only space tiles extend all the way
     * from the left edge of the plane to the right.  We
     * also assume that the topmost and bottommost tiles
     * are space tiles.
     */

    rect->r_ytop = BOTTOM(LB(top));
    rect->r_ybot = TOP(RT(bottom));

    /*
     * If the bounding rectangle is degenerate (indicating no solid
     * tiles in the plane), we make it the 1x1 rectangle: (0,0)::(1,1).
     */
    if (rect->r_xtop < rect->r_xbot || rect->r_ytop < rect->r_ybot)
    {
	rect->r_xbot = rect->r_xtop = 0;
	rect->r_ybot = rect->r_ytop = 0;
	return (FALSE);
    }

    return (TRUE);
}

/*
 * --------------------------------------------------------------------
 *
 * DBBoundPlaneVert --
 *
 * Determine the bounding rectangle for the supplied tile plane,
 * which is organized into maximal vertical strips instead of
 * maximal horizontal ones.
 *
 * The bounding rectangle is the smallest rectangle that completely
 * encloses all non-space tiles.
 *
 * If the tile plane is completely empty, we return a 0x0 bounding
 * box at the origin.
 *
 * Results:
 *	TRUE if the tile plane contains any geometry, FALSE
 *	if it is completely empty.
 *
 * Side effects:
 *	Sets *rect to the bounding rectangle.
 *
 * --------------------------------------------------------------------
 */

bool
DBBoundPlaneVert(plane, rect)
    Plane *plane;
    Rect *rect;
{
    Tile *left, *right, *top, *bottom, *tp;

    left = plane->pl_left;
    right = plane->pl_right;
    top = plane->pl_top;
    bottom = plane->pl_bottom;

    rect->r_ur = TiPlaneRect.r_ll;
    rect->r_ll = TiPlaneRect.r_ur;

    /*
     * To find the topmost and bottommost solid edges, we
     * scan along the respective edges.  Our assumption is
     * that the only tiles along the edges are space tiles,
     * which, by the maximum vertical strip property, must
     * have either solid tiles or the edge of the plane on
     * their other sides.
     */
    for (tp = RT(bottom); tp != left; tp = BL(tp))
	if (TOP(tp) < rect->r_ybot)
	    rect->r_ybot = TOP(tp);
    for (tp = LB(top); tp != right; tp = TR(tp))
	if (BOTTOM(tp) > rect->r_ytop)
	    rect->r_ytop = BOTTOM(tp);

    /*
     * We assume that only space tiles extend all the way
     * from the top edge of the plane to the bottom.  We
     * also assume that the leftmost and rightmost tiles
     * are space tiles.
     */
    rect->r_xtop = LEFT(BL(right));
    rect->r_xbot = RIGHT(TR(left));

    /*
     * If the bounding rectangle is degenerate (indicating no solid
     * tiles in the plane), we make it the 1x1 rectangle: (0,0)::(1,1).
     */
    if (rect->r_xtop < rect->r_xbot || rect->r_ytop < rect->r_ybot)
    {
	rect->r_xbot = rect->r_xtop = 0;
	rect->r_ybot = rect->r_ytop = 0;
	return (FALSE);
    }

    return (TRUE);
}
