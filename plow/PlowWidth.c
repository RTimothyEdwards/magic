/*
 * PlowWidth.c --
 *
 * Plowing.
 * Determine the true minimum width of a piece of geometry.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/plow/PlowWidth.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "plow/plowInt.h"

struct wclip
{
    Edge	*wc_edge;	/* Initial edge */
    Rect	 wc_area;	/* Area being clipped repeatedly */
};

int plowInitWidthFunc(), plowWidthFunc();
int plowInitWidthBackFunc(), plowWidthBackFunc();

#ifdef	COUNTWIDTHCALLS
int plowWidthNumCalls = 0;
int plowWidthNumChoices = 0;
#endif	/* COUNTWIDTHCALLS */

/*
 * ----------------------------------------------------------------------------
 *
 * plowFindWidth --
 *
 * Find the minimum width of material consisting of those types in the
 * TileTypeBitMask 'types', starting the search from the Edge 'edge'.
 *
 * Results:
 *	Returns the minimum width.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
plowFindWidth(edge, types, bbox, prect)
    Edge *edge;			/* Edge along the LHS of this material */
    TileTypeBitMask types;	/* Types whose width is being computed.  Note
				 * that this set is passed by value.
				 */
    Rect *bbox;			/* Bounding box of the cell */
    Rect *prect;		/* (Debugging only): if this is non-NULL,
				 * the rectangle it points to is filled
				 * in with the rectangle found by this
				 * algorithm to be the largest containing
				 * the edge 'edge' with only types in the
				 * set 'types'.
				 */
{
    Plane *plane = plowYankDef->cd_planes[edge->e_pNum];
    TileTypeBitMask ctypes;
    struct wclip wc;
    int x, y;

    TTMaskCom2(&ctypes, &types);
#ifdef	COUNTWIDTHCALLS
    plowWidthNumCalls++;
#endif	/* COUNTWIDTHCALLS */

    /*
     * Start with wc.wc_area extending from the edge rightward to the
     * cell boundary.  Search right to find some tile (not necessarily
     * the leftmost) that is not one of 'types'.  This provides an
     * upper bound on the minimum width, which we use to set wa.wa_area
     * for the next loop.
     */
    wc.wc_edge = edge;
    wc.wc_area.r_xtop = bbox->r_xtop + 1;
    wc.wc_area.r_xbot = edge->e_x;
    wc.wc_area.r_ybot = edge->e_ybot;
    wc.wc_area.r_ytop = edge->e_ytop;
#ifdef	DEBUGWIDTH
    TxPrintf("Initial area: X: %d .. %d  Y: %d .. %d\n",
	wc.wc_area.r_xbot, wc.wc_area.r_xtop,
	wc.wc_area.r_ybot, wc.wc_area.r_ytop);
#endif	/* DEBUGWIDTH */
    (void) DBSrPaintArea((Tile *) NULL,
		plane, &wc.wc_area, &ctypes,
		plowInitWidthFunc, (ClientData) &wc);
#ifdef	DEBUGWIDTH
    TxPrintf("After first search, area: X: %d .. %d  Y: %d .. %d\n",
	wc.wc_area.r_xbot, wc.wc_area.r_xtop,
	wc.wc_area.r_ybot, wc.wc_area.r_ytop);
#endif	/* DEBUGWIDTH */

    /*
     * Repeatedly search wc.wc_area for tiles whose
     * types are not in the set 'types'.  Each time we find such a
     * tile, we clip wc.wc_area so it still contains the original
     * edge in its entirety, but excludes the offending tile.
     * Continue until either the edge is degenerate (shouldn't
     * happen!) or everything in wc_area is of types in 'types'.
     */
    while (DBSrPaintArea((Tile *) NULL, plane, &wc.wc_area, &ctypes,
		plowWidthFunc, (ClientData) &wc))
    {
#ifdef	DEBUGWIDTH
	TxPrintf("Next area: X: %d .. %d  Y: %d .. %d\n",
	    wc.wc_area.r_xbot, wc.wc_area.r_xtop,
	    wc.wc_area.r_ybot, wc.wc_area.r_ytop);
#endif	/* DEBUGWIDTH */
	if (wc.wc_area.r_xbot == wc.wc_area.r_xtop)
	    break;
    }

    if (prect) *prect = wc.wc_area;
    x = wc.wc_area.r_xtop - wc.wc_area.r_xbot;
    y = wc.wc_area.r_ytop - wc.wc_area.r_ybot;
#ifdef	DEBUGWIDTH
    TxPrintf("Width = %d\n", MIN(x, y));
#endif	/* DEBUGWIDTH */
    return (MIN(x, y));
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowInitWidthFunc --
 *
 * Called by area search to obtain an initial estimate of the minimum
 * width of a wire.  The argument 'tile' will be of a type not included
 * in the set 'types' passed to plowFindWidth() above.
 *
 * We set our upper bound on minimum width to be the distance between
 * wc->wc_edge and the LHS of tile.  This means that the search rectangle
 * wc->wc_area has to be modified as follows:
 *
 *	wc_area.r_xtop will be (upper bound) to the right of wc_edge
 *	wc_area.r_ybot will be (upper bound) below the top of wc_edge
 *	wc_area.r_ytop will be (upper bound) above the bottom of wc_edge
 *
 * Results:
 *	Returns 1 always.
 *
 * Side effects:
 *	Modifies wc->wc_area as described above.
 *
 * ----------------------------------------------------------------------------
 */

int
plowInitWidthFunc(tile, wc)
    Tile *tile;	/* Tile whose type is not among the types
				 * passed to plowFindWidth(), whose LHS will
				 * provide the right-hand boundary.
				 */
    struct wclip *wc;	/* Contains original edge and area to clip */
{
    Edge *edge = wc->wc_edge;
    int upperBound = LEFT(tile) - edge->e_x;

#ifdef	DEBUGWIDTH
    TxPrintf("Tile type = %s, edgeHeight = %d, upperBound = %d\n",
	DBTypeLongName(TiGetTypeExact(tile)), edge->e_ytop-edge->e_ybot, upperBound);
#endif	/* DEBUGWIDTH */
    wc->wc_area.r_ytop = MAX(edge->e_ybot + upperBound, edge->e_ytop);
    wc->wc_area.r_ybot = MIN(edge->e_ytop - upperBound, edge->e_ybot);
    wc->wc_area.r_xtop = LEFT(tile);

    return (1);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowWidthFunc --
 *
 * Called by area search to update an estimate of the minimum width of
 * a wire.  The argument 'tile' will be of a type not included in the
 * set 'types' passed to plowFindWidth() above.
 *
 * The basic idea is to modify wc->wc_area so it does not contain the
 * area of 'tile', but still contains the entire edge wc->wc_edge.
 * When 'tile' overlaps wc->wc_edge in Y, this can only be achieved
 * by throwing away everything to the right of LEFT(tile).  When 'tile'
 * doesn't overlap wc->wc_edge in Y, there are generally two choices:
 * clip away the right-hand side of wc_area, or clip away the top (or
 * bottom).  To avoid a combinatoric explosion, we act greedily and
 * clip in such a way as to keep the largest dimension largest.
 *
 * Results:
 *	Returns 1 always.
 *
 * Side effects:
 *	Modifies wc->wc_area as described above.
 *
 * ----------------------------------------------------------------------------
 */

int
plowWidthFunc(tile, wc)
    Tile *tile;	/* Tile whose type is not among the types
				 * passed to plowFindWidth(), which will be
				 * clipped out of the area wc->wc_area in
				 * such a way that the original edge will
				 * remain a part of wc->wc_area.
				 */
    struct wclip *wc;	/* Contains original edge and area to clip */
{
    Edge *edge = wc->wc_edge;
    int xw, yw;
    int yt, yb;

    /*
     * If the tile overlaps the edge in Y, we have to clip the
     * RHS of the area.  If clipping the RHS makes X the new
     * smallest dimension, clip Y appropriately.
     */
    xw = LEFT(tile) - wc->wc_area.r_xbot;
    if (BOTTOM(tile) < edge->e_ytop && TOP(tile) > edge->e_ybot)
    {
	wc->wc_area.r_xtop = LEFT(tile);
	goto clipvert;
    }

    /*
     * If the tile is above the edge in Y, consider both
     * alternatives (clipping the top, and clipping the
     * right), choosing whichever one leaves us with the
     * larger minimum width.  This may not always work,
     * but is simple.
     */
#ifdef	COUNTWIDTHCALLS
    plowWidthNumChoices++;
#endif	/* COUNTWIDTHCALLS */
    if (BOTTOM(tile) >= edge->e_ytop)
    {
	yw = BOTTOM(tile) - wc->wc_area.r_ybot;
	if (xw >= yw)
	{
	    wc->wc_area.r_xtop = LEFT(tile);
	    goto clipvert;
	}
	else
	{
	    wc->wc_area.r_ytop = BOTTOM(tile);
	    goto clipright;
	}
    }

    /*
     * The tile is below the edge in Y, so consider both
     * alternatives (clipping the bottom, and clipping the
     * right), choosing whichever one leaves us with the
     * larger minimum width.  This may not always work,
     * but is simple.
     */
    yw = wc->wc_area.r_ytop - TOP(tile);
    if (xw >= yw)
    {
	wc->wc_area.r_xtop = LEFT(tile);
	goto clipvert;
    }
    else
    {
	wc->wc_area.r_ybot = TOP(tile);
	goto clipright;
    }

clipvert:
    yt = MIN(edge->e_ybot + xw, wc->wc_area.r_ytop);
    yb = MAX(edge->e_ytop - xw, wc->wc_area.r_ybot);
    if (yt > wc->wc_edge->e_ytop) wc->wc_area.r_ytop = yt;
    if (yb < wc->wc_edge->e_ybot) wc->wc_area.r_ybot = yb;
    return (1);

clipright:
    yw = wc->wc_area.r_ytop - wc->wc_area.r_ybot;
    xw = wc->wc_area.r_xtop - wc->wc_area.r_xbot;
    if (yw < xw)
	wc->wc_area.r_xtop = wc->wc_area.r_xbot + yw;
    return (1);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowFindWidthBack --
 *
 * Find the minimum width of material consisting of those types in the
 * TileTypeBitMask 'types', starting the search from the Edge 'edge',
 * but searching to the left instead of to the right.
 *
 * Results:
 *	Returns the minimum width.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
plowFindWidthBack(edge, types, bbox, prect)
    Edge *edge;			/* Edge along the RHS of this material */
    TileTypeBitMask types;	/* Types whose width is being computed.  Note
				 * that this set is passed by value.
				 */
    Rect *bbox;			/* Bounding box of the cell */
    Rect *prect;		/* (Debugging only): if this is non-NULL,
				 * the rectangle it points to is filled
				 * in with the rectangle found by this
				 * algorithm to be the largest containing
				 * the edge 'edge' with only types in the
				 * set 'types'.
				 */
{
    Plane *plane = plowYankDef->cd_planes[edge->e_pNum];
    TileTypeBitMask ctypes;
    struct wclip wc;
    int x, y;

    TTMaskCom2(&ctypes, &types);

    /*
     * Start with wc.wc_area extending from the edge rightward to the
     * cell boundary.  Search left to find some tile (not necessarily
     * the rightmost) that is not one of 'types'.  This provides an
     * upper bound on the minimum width, which we use to set wa.wa_area
     * for the next loop.
     */
    wc.wc_edge = edge;
    wc.wc_area.r_xtop = edge->e_x;
    wc.wc_area.r_xbot = bbox->r_xbot - 1;
    wc.wc_area.r_ybot = edge->e_ybot;
    wc.wc_area.r_ytop = edge->e_ytop;
#ifdef	DEBUGWIDTH
    TxPrintf("Initial area: X: %d .. %d  Y: %d .. %d\n",
	wc.wc_area.r_xbot, wc.wc_area.r_xtop,
	wc.wc_area.r_ybot, wc.wc_area.r_ytop);
#endif	/* DEBUGWIDTH */
    (void) DBSrPaintArea((Tile *) NULL,
		plane, &wc.wc_area, &ctypes,
		plowInitWidthBackFunc, (ClientData) &wc);
#ifdef	DEBUGWIDTH
    TxPrintf("After first search, area: X: %d .. %d  Y: %d .. %d\n",
	wc.wc_area.r_xbot, wc.wc_area.r_xtop,
	wc.wc_area.r_ybot, wc.wc_area.r_ytop);
#endif	/* DEBUGWIDTH */

    /*
     * Repeatedly search wc.wc_area for tiles whose
     * types are not in the set 'types'.  Each time we find such a
     * tile, we clip wc.wc_area so it still contains the original
     * edge in its entirety, but excludes the offending tile.
     * Continue until either the edge is degenerate (shouldn't
     * happen!) or everything in wc_area is of types in 'types'.
     */
    while (DBSrPaintArea((Tile *) NULL, plane, &wc.wc_area, &ctypes,
		plowWidthBackFunc, (ClientData) &wc))
    {
#ifdef	DEBUGWIDTH
	TxPrintf("Next area: X: %d .. %d  Y: %d .. %d\n",
	    wc.wc_area.r_xbot, wc.wc_area.r_xtop,
	    wc.wc_area.r_ybot, wc.wc_area.r_ytop);
#endif	/* DEBUGWIDTH */
	if (wc.wc_area.r_xbot == wc.wc_area.r_xtop)
	    break;
    }

    if (prect) *prect = wc.wc_area;
    x = wc.wc_area.r_xtop - wc.wc_area.r_xbot;
    y = wc.wc_area.r_ytop - wc.wc_area.r_ybot;
#ifdef	DEBUGWIDTH
    TxPrintf("Width = %d\n", MIN(x, y));
#endif	/* DEBUGWIDTH */
    return (MIN(x, y));
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowInitWidthBackFunc --
 *
 * Called by area search to obtain an initial estimate of the minimum
 * width of a wire.  The argument 'tile' will be of a type not included
 * in the set 'types' passed to plowFindWidth() above.
 *
 * We set our upper bound on minimum width to be the distance between
 * wc->wc_edge and the RHS of tile.  This means that the search rectangle
 * wc->wc_area has to be modified as follows:
 *
 *	wc_area.r_xbot will be (upper bound) to the left of wc_edge
 *	wc_area.r_ybot will be (upper bound) below the top of wc_edge
 *	wc_area.r_ytop will be (upper bound) above the bottom of wc_edge
 *
 * Results:
 *	Returns 1 always.
 *
 * Side effects:
 *	Modifies wc->wc_area as described above.
 *
 * ----------------------------------------------------------------------------
 */

int
plowInitWidthBackFunc(tile, wc)
    Tile *tile;	/* Tile whose type is not among the types
				 * passed to plowFindWidthBack(), whose RHS will
				 * provide the left-hand boundary.
				 */
    struct wclip *wc;	/* Contains original edge and area to clip */
{
    Edge *edge = wc->wc_edge;
    int upperBound = edge->e_x - RIGHT(tile);

#ifdef	DEBUGWIDTH
    TxPrintf("Tile type = %s, edgeHeight = %d, upperBound = %d\n",
	DBTypeLongName(TiGetTypeExact(tile)), edge->e_ytop-edge->e_ybot, upperBound);
#endif	/* DEBUGWIDTH */
    wc->wc_area.r_ytop = MAX(edge->e_ybot + upperBound, edge->e_ytop);
    wc->wc_area.r_ybot = MIN(edge->e_ytop - upperBound, edge->e_ybot);
    wc->wc_area.r_xbot = RIGHT(tile);

    return (1);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowWidthBackFunc --
 *
 * Called by area search to update an estimate of the minimum width of
 * a wire.  The argument 'tile' will be of a type not included in the
 * set 'types' passed to plowFindWidth() above.
 *
 * The basic idea is to modify wc->wc_area so it does not contain the
 * area of 'tile', but still contains the entire edge wc->wc_edge.
 * When 'tile' overlaps wc->wc_edge in Y, this can only be achieved
 * by throwing away everything to the left of RIGHT(tile).  When 'tile'
 * doesn't overlap wc->wc_edge in Y, there are generally two choices:
 * clip away the left-hand side of wc_area, or clip away the top (or
 * bottom).  To avoid a combinatoric explosion, we act greedily and
 * clip in such a way as to keep the largest dimension largest.
 *
 * Results:
 *	Returns 1 always.
 *
 * Side effects:
 *	Modifies wc->wc_area as described above.
 *
 * ----------------------------------------------------------------------------
 */

int
plowWidthBackFunc(tile, wc)
    Tile *tile;	/* Tile whose type is not among the types
				 * passed to plowFindWidth(), which will be
				 * clipped out of the area wc->wc_area in
				 * such a way that the original edge will
				 * remain a part of wc->wc_area.
				 */
    struct wclip *wc;	/* Contains original edge and area to clip */
{
    Edge *edge = wc->wc_edge;
    int xw, yw;
    int yt, yb;

    /*
     * If the tile overlaps the edge in Y, we have to clip the
     * LHS of the area.  If clipping the LHS makes X the new
     * smallest dimension, clip Y appropriately.
     */
    xw = wc->wc_area.r_xtop - RIGHT(tile);
    if (BOTTOM(tile) < edge->e_ytop && TOP(tile) > edge->e_ybot)
    {
	wc->wc_area.r_xbot = RIGHT(tile);
	goto clipvert;
    }

    /*
     * If the tile is above the edge in Y, consider both
     * alternatives (clipping the top, and clipping the
     * right), choosing whichever one leaves us with the
     * larger minimum width.  This may not always work,
     * but is simple.
     */
    if (BOTTOM(tile) >= edge->e_ytop)
    {
	yw = BOTTOM(tile) - wc->wc_area.r_ybot;
	if (xw >= yw)
	{
	    wc->wc_area.r_xbot = RIGHT(tile);
	    goto clipvert;
	}
	else
	{
	    wc->wc_area.r_ytop = BOTTOM(tile);
	    goto clipright;
	}
    }

    /*
     * The tile is below the edge in Y, so consider both
     * alternatives (clipping the bottom, and clipping the
     * right), choosing whichever one leaves us with the
     * larger minimum width.  This may not always work,
     * but is simple.
     */
    yw = wc->wc_area.r_ytop - TOP(tile);
    if (xw >= yw)
    {
	wc->wc_area.r_xbot = RIGHT(tile);
	goto clipvert;
    }
    else
    {
	wc->wc_area.r_ybot = TOP(tile);
	goto clipright;
    }

clipvert:
    yt = MIN(edge->e_ybot + xw, wc->wc_area.r_ytop);
    yb = MAX(edge->e_ytop - xw, wc->wc_area.r_ybot);
    if (yt > wc->wc_edge->e_ytop) wc->wc_area.r_ytop = yt;
    if (yb < wc->wc_edge->e_ybot) wc->wc_area.r_ybot = yb;
    return (1);

clipright:
    yw = wc->wc_area.r_ytop - wc->wc_area.r_ybot;
    xw = wc->wc_area.r_xtop - wc->wc_area.r_xbot;
    if (yw < xw)
	wc->wc_area.r_xbot = wc->wc_area.r_xtop - yw;
    return (1);
}
