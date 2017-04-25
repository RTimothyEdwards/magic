/*
 * ExtNeighbors.c --
 *
 * Circuit extraction.
 * This file contains the primitive function ExtFindNeighbors()
 * for visiting all neighbors of a tile that connect to it, and
 * applying a filter function at each tile.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/extract/ExtNghbors.c,v 1.3 2010/09/12 20:32:33 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/malloc.h"
#include "debug/debug.h"
#include "extract/extract.h"
#include "extract/extractInt.h"
#include "utils/signals.h"
#include "utils/stack.h"

/*
 * The algorithm used by ExtFindNeighbors is non-recursive.
 * It uses a stack (extNodeStack) to hold a list of tiles yet to
 * be processed.  To mark a tile as being on the stack, we store
 * the value VISITPENDING in its ti_client field.
 */

/* Used for communicating with extNbrPushFunc */
ClientData extNbrUn;

/*
 * ----------------------------------------------------------------------------
 *
 * ExtFindNeighbors --
 *
 * For each tile adjacent to 'tile' that connects to it (according to
 * arg->fra_connectsTo), and (if it is a contact) for tiles on other
 * planes that connect to it, we recursively visit the tile, call the
 * client's filter procedure (*arg->fra_each)(), if it is non-NULL.
 * The tile is marked as being visited by setting it's ti_client field
 * to arg->fra_region.
 *
 * Results:
 *	Returns the number of tiles that are found to be connected.
 *	This is used to find, for example, transistor gates made of
 *	only one tile, for which a simple calculation suffices for
 *	computing length and width.  If an error occurred, returns
 *	the value -1.
 *
 * Side effects:
 *	See comments above.
 *
 * ----------------------------------------------------------------------------
 */

int
ExtFindNeighbors(tile, tilePlaneNum, arg)
    Tile *tile;
    int tilePlaneNum;
    FindRegion *arg;
{
    TileTypeBitMask *connTo = arg->fra_connectsTo;
    Tile *tp;
    TileType type, t;
    TileTypeBitMask *mask;
    Rect biggerArea;
    int pNum, tilesfound;
    PlaneMask pMask;
    PlaneAndArea pla;

    tilesfound = 0;

    extNbrUn = arg->fra_uninit;
    if (extNodeStack == (Stack *) NULL)
	extNodeStack = StackNew(64);

    /* Mark this tile as pending and push it */
    PUSHTILE(tile, tilePlaneNum);

    while (!StackEmpty(extNodeStack))
    {
	POPTILE(tile, tilePlaneNum);

	if (IsSplit(tile))
	{
            type = (SplitSide(tile)) ? SplitRightType(tile):
			SplitLeftType(tile);
	}
        else
	    type = TiGetTypeExact(tile);

	mask = &connTo[type];

	/*
	 * Since tile was pushed on the stack, we know that it
	 * belongs to this region.  Check to see that it hasn't
	 * been visited in the meantime.  If it's still unvisited,
	 * visit it and process its neighbors.
	 */
	if (tile->ti_client == (ClientData) arg->fra_region)
	    continue;
	tile->ti_client = (ClientData) arg->fra_region;
	tilesfound++;
	if (DebugIsSet(extDebugID, extDebNeighbor))
	    extShowTile(tile, "neighbor", 1);

	/* Top */
topside:
        if (IsSplit(tile) && (SplitSide(tile) ^ SplitDirection(tile))) goto leftside;
	for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp = BL(tp))
	{
            if (IsSplit(tp))
	    {
                t = SplitBottomType(tp);
		// if (tp->ti_client == extNbrUn && TTMaskHasType(mask, t))
		if (tp->ti_client != (ClientData)arg->fra_region && TTMaskHasType(mask, t))
		{
		    PUSHTILEBOTTOM(tp, tilePlaneNum);
		}
	    }
            else
	    {
        	t = TiGetTypeExact(tp);
		if (tp->ti_client == extNbrUn && TTMaskHasType(mask, t))
		{
		    PUSHTILE(tp, tilePlaneNum);
		}
	    }
	}

	/* Left */
leftside:
        if (IsSplit(tile) && SplitSide(tile)) goto bottomside;
	for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
	{
            if (IsSplit(tp))
	    {
                t = SplitRightType(tp);
		// if (tp->ti_client == extNbrUn && TTMaskHasType(mask, t))
		if (tp->ti_client != (ClientData)arg->fra_region && TTMaskHasType(mask, t))
		{
		    PUSHTILERIGHT(tp, tilePlaneNum);
		}
	    }
            else
	    {
		t = TiGetTypeExact(tp);
		if (tp->ti_client == extNbrUn && TTMaskHasType(mask, t))
		{
		    PUSHTILE(tp, tilePlaneNum);
		}
	    }
	}

	/* Bottom */
bottomside:
        if (IsSplit(tile) && (!(SplitSide(tile) ^ SplitDirection(tile))))
	    goto rightside;
	for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp = TR(tp))
	{
            if (IsSplit(tp))
	    {
                t = SplitTopType(tp);
		// if (tp->ti_client == extNbrUn && TTMaskHasType(mask, t))
		if (tp->ti_client != (ClientData)arg->fra_region && TTMaskHasType(mask, t))
		{
		    PUSHTILETOP(tp, tilePlaneNum);
		}
	    }
            else
	    {
        	t = TiGetTypeExact(tp);
		if (tp->ti_client == extNbrUn && TTMaskHasType(mask, t))
		{
		    PUSHTILE(tp, tilePlaneNum);
		}
	    }
	}

	/* Right */
rightside:
        if (IsSplit(tile) && !SplitSide(tile)) goto donesides;
	for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp = LB(tp))
	{
            if (IsSplit(tp))
	    {
                t = SplitLeftType(tp);
		// if (tp->ti_client == extNbrUn && TTMaskHasType(mask, t))
		if (tp->ti_client != (ClientData)arg->fra_region && TTMaskHasType(mask, t))
		{
		    PUSHTILELEFT(tp, tilePlaneNum);
		}
	    }
            else
	    {
		t = TiGetTypeExact(tp);
		if (tp->ti_client == extNbrUn && TTMaskHasType(mask, t))
		{
		    PUSHTILE(tp, tilePlaneNum);
		}
	    }
	}

donesides:
	/* Apply the client's filter procedure if one exists */
	if (arg->fra_each)
	    if ((*arg->fra_each)(tile, tilePlaneNum, arg))
		goto fail;

	/* If this is a contact, visit all the other planes */
	if (DBIsContact(type))
	{
	    pMask = DBConnPlanes[type];
	    pMask &= ~(PlaneNumToMaskBit(tilePlaneNum));
	    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
		if (PlaneMaskHasPlane(pMask, pNum))
		{
		    Plane *plane = arg->fra_def->cd_planes[pNum];

		    tp = plane->pl_hint;
		    GOTOPOINT(tp, &tile->ti_ll);
		    plane->pl_hint = tp;
		    if (tp->ti_client != extNbrUn) continue;

                    /* tp and tile should have the same geometry for a contact */
                    if (IsSplit(tile) && IsSplit(tp))
                    {
			if (SplitSide(tile))
			{
			    t = SplitRightType(tp);
			    if (TTMaskHasType(mask, t))
			    {
				PUSHTILERIGHT(tp, pNum);
			    }
			}
			else
			{
			    t = SplitLeftType(tp);
			    if (TTMaskHasType(mask, t))
			    {
				PUSHTILELEFT(tp, pNum);
			    }
			}
                    }
                    else if (IsSplit(tp))
		    {
			t = SplitRightType(tp);
			if (TTMaskHasType(mask, t))
			{
			    PUSHTILERIGHT(tp, pNum);
			}
			t = SplitLeftType(tp);
			if (TTMaskHasType(mask, t))
			{
			    PUSHTILELEFT(tp, pNum);
			}
		    }
		    else
		    {
			t = TiGetTypeExact(tp);
			if (TTMaskHasType(mask, t))
			{
			    PUSHTILE(tp, pNum);
			}
		    }
		}
	}

	/*
	 * The hairiest case is when this type connects to stuff on
	 * other planes, but isn't itself connected as a contact.
	 * For example, a CMOS pwell connects to diffusion of the
	 * same doping (p substrate diff).  In a case like this,
	 * we need to search the entire AREA of the tile plus a
	 * 1-lambda halo to find everything it overlaps or touches
	 * on the other plane.
	 */
	if (pMask = DBAllConnPlanes[type])
	{
	    TITORECT(tile, &pla.area);
	    GEO_EXPAND(&pla.area, 1, &biggerArea);
	    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
		if ((pNum != tilePlaneNum) && PlaneMaskHasPlane(pMask, pNum))
		{
		    pla.plane = pNum;
		    (void) DBSrPaintArea((Tile *) NULL,
			    arg->fra_def->cd_planes[pNum], &biggerArea,
			    mask, extNbrPushFunc, (ClientData) &pla);
		}
	}
    }

    return tilesfound;

fail:
    /* Flush the stack */
    while (!StackEmpty(extNodeStack))
    {
	POPTILE(tile, tilePlaneNum);
	tile->ti_client = (ClientData) arg->fra_region;
    }
    return -1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extNbrPushFunc --
 *
 * Called for each tile overlapped by a 1-unit wide halo around the area
 * tileArea.  If the tile overlaps or shares a non-null segment of border
 * with tileArea, and it hasn't already been visited, push it on the stack
 * extNodeStack.
 *
 * Uses the global parameter extNbrUn to determine whether or not a tile
 * has been visited; if the tile's client field is equal to extNbrUn, then
 * this is the first time the tile has been seen.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
extNbrPushFunc(tile, pla)
    Tile *tile;
    PlaneAndArea *pla;
{
    Rect *tileArea;
    Rect r;

    tileArea = &pla->area;

    /* Ignore tile if it's already been visited */
    if (tile->ti_client != extNbrUn)
	return 0;

    /* Only consider tile if it overlaps tileArea or shares part of a side */
    TITORECT(tile, &r);
    if (!GEO_OVERLAP(&r, tileArea))
    {
	GEOCLIP(&r, tileArea);
	if (r.r_xbot >= r.r_xtop && r.r_ybot >= r.r_ytop)
	    return 0;
    }

    /* Push tile on the stack and mark as being visited */
    PUSHTILE(tile, pla->plane);

    return 0;
}
