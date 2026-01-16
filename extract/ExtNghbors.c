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
ExtFindNeighbors(tile, dinfo, tilePlaneNum, arg)
    Tile *tile;
    TileType dinfo;
    int tilePlaneNum;
    FindRegion *arg;
{
    TileTypeBitMask *connTo = arg->fra_connectsTo;
    Tile *tp;
    TileType type, t, tpdinfo;
    TileTypeBitMask *mask;
    Rect biggerArea;
    int pNum, tilesfound;
    PlaneMask pMask;
    PlaneAndArea pla;
    ClientData extNbrUn = arg->fra_uninit;

    tilesfound = 0;

    if (extNodeStack == (Stack *) NULL)
	extNodeStack = StackNew(64);

    /* Mark this tile as pending and push it */
    PUSHTILE(tile, dinfo, tilePlaneNum);

    while (!StackEmpty(extNodeStack))
    {
	POPTILE(tile, dinfo, tilePlaneNum);

	if (IsSplit(tile))
	{
            type = (dinfo & TT_SIDE) ? SplitRightType(tile):
			SplitLeftType(tile);
	}
        else
	    type = TiGetTypeExact(tile);

	ASSERT(type != TT_SPACE, "ExtFindNeighbors");

	mask = &connTo[type];

	/*
	 * Since tile was pushed on the stack, we know that it
	 * belongs to this region.  Check to see that it hasn't
	 * been visited in the meantime.  If it's still unvisited,
	 * visit it and process its neighbors.
	 */

	if (ExtGetRegion(tile, dinfo) == arg->fra_region)
	    continue;

	ExtSetRegion(tile, dinfo, arg->fra_region);

	tilesfound++;
	if (DebugIsSet(extDebugID, extDebNeighbor))
	    extShowTile(tile, "neighbor", 1);

	/* Top */
topside:
        if (IsSplit(tile) && ((dinfo & TT_SIDE) ? 1 : 0) ^ SplitDirection(tile))
	    goto leftside;
	for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp = BL(tp))
	{
            if (IsSplit(tp))
	    {
                t = SplitBottomType(tp);
		tpdinfo = SplitDirection(tp) ? (TileType)0 : (TileType)TT_SIDE;
		if (ExtGetRegion(tp, tpdinfo) == CD2PTR(extNbrUn) && TTMaskHasType(mask, t))
		{
		    PUSHTILEBOTTOM(tp, tilePlaneNum);
		}
	    }
            else
	    {
        	t = TiGetTypeExact(tp);
		if (TiGetClient(tp) == extNbrUn && TTMaskHasType(mask, t))
		{
		    PUSHTILELEFT(tp, tilePlaneNum);
		}
	    }
	}

	/* Left */
leftside:
        if (IsSplit(tile) && (dinfo & TT_SIDE)) goto bottomside;
	for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
	{
            if (IsSplit(tp))
	    {
                t = SplitRightType(tp);
		if (ExtGetRegion(tp, (TileType)TT_SIDE) == CD2PTR(extNbrUn)
				&& TTMaskHasType(mask, t))
		{
		    PUSHTILERIGHT(tp, tilePlaneNum);
		}
	    }
            else
	    {
		t = TiGetTypeExact(tp);
		if (TiGetClient(tp) == extNbrUn && TTMaskHasType(mask, t))
		{
		    PUSHTILELEFT(tp, tilePlaneNum);
		}
	    }
	}

	/* Bottom */
bottomside:
        if (IsSplit(tile) && (!(((dinfo & TT_SIDE) ? 1 : 0) ^ SplitDirection(tile))))
	    goto rightside;
	for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp = TR(tp))
	{
            if (IsSplit(tp))
	    {
                t = SplitTopType(tp);
		tpdinfo = SplitDirection(tp) ? (TileType)TT_SIDE : (TileType)0;
		if (ExtGetRegion(tp, tpdinfo) == CD2PTR(extNbrUn) && TTMaskHasType(mask, t))
		{
		    PUSHTILETOP(tp, tilePlaneNum);
		}
	    }
            else
	    {
        	t = TiGetTypeExact(tp);
		if (TiGetClient(tp) == extNbrUn && TTMaskHasType(mask, t))
		{
		    PUSHTILELEFT(tp, tilePlaneNum);
		}
	    }
	}

	/* Right */
rightside:
        if (IsSplit(tile) && !(dinfo & TT_SIDE)) goto donesides;
	for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp = LB(tp))
	{
            if (IsSplit(tp))
	    {
                t = SplitLeftType(tp);
		if (ExtGetRegion(tp, (TileType)0) == CD2PTR(extNbrUn)
				&& TTMaskHasType(mask, t))
		{
		    PUSHTILELEFT(tp, tilePlaneNum);
		}
	    }
            else
	    {
		t = TiGetTypeExact(tp);
		if (TiGetClient(tp) == extNbrUn && TTMaskHasType(mask, t))
		{
		    PUSHTILELEFT(tp, tilePlaneNum);
		}
	    }
	}

donesides:
	/* Apply the client's filter procedure if one exists */
	if (arg->fra_each)
	    if ((*arg->fra_each)(tile, dinfo, tilePlaneNum, arg))
		goto fail;

	/* Use tilePlaneNum value -1 to force ExtFindNeighbors to stay
	 * on a single plane.
	 */
	if (tilePlaneNum < 0) continue;

	/* If this is a contact, visit all the other planes */
	if (DBIsContact(type))
	{
	    pMask = DBConnPlanes[type];
	    pMask &= ~(PlaneNumToMaskBit(tilePlaneNum));
	    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
		if (PlaneMaskHasPlane(pMask, pNum))
		{
		    ExtRegion *tpreg;
		    Plane *plane = arg->fra_def->cd_planes[pNum];

		    /* Find the point on the new plane */
		    tp = PlaneGetHint(plane);
		    GOTOPOINT(tp, &tile->ti_ll);
		    PlaneSetHint(plane, tp);

                    /* tp and tile should have the same geometry for a contact */
                    if (IsSplit(tile) && IsSplit(tp))
                    {
			if (dinfo & TT_SIDE)
			{
			    /* Only process tp if not yet visited */
			    tpreg = ExtGetRegion(tp, (TileType)TT_SIDE);
			    if (tpreg != CD2PTR(extNbrUn)) continue;
			    t = SplitRightType(tp);
			    if (TTMaskHasType(mask, t))
			    {
				PUSHTILERIGHT(tp, pNum);
			    }
			}
			else
			{
			    /* Only process tp if not yet visited */
			    tpreg = ExtGetRegion(tp, (TileType)0);
			    if (tpreg != CD2PTR(extNbrUn)) continue;
			    t = SplitLeftType(tp);
			    if (TTMaskHasType(mask, t))
			    {
				PUSHTILELEFT(tp, pNum);
			    }
			}
                    }
                    else if (IsSplit(tp))
		    {
			/* Only process tp if not yet visited */
			tpreg = ExtGetRegion(tp, (TileType)TT_SIDE);
			if (tpreg == CD2PTR(extNbrUn))
			{
			    t = SplitRightType(tp);
			    if (TTMaskHasType(mask, t))
			    {
				PUSHTILERIGHT(tp, pNum);
			    }
			}
			/* Try both sides */
			tpreg = ExtGetRegion(tp, (TileType)0);
			if (tpreg == CD2PTR(extNbrUn))
			{
			    t = SplitLeftType(tp);
			    if (TTMaskHasType(mask, t))
			    {
				PUSHTILELEFT(tp, pNum);
			    }
			}
		    }
		    else
		    {
			/* Only process tp if not yet visited */
			tpreg = ExtGetRegion(tp, (TileType)0);
			if (tpreg != CD2PTR(extNbrUn)) continue;
			t = TiGetTypeExact(tp);
			if (TTMaskHasType(mask, t))
			{
			    PUSHTILELEFT(tp, pNum);
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
	if ((pMask = DBAllConnPlanes[type]))
	{
	    pla.uninit = extNbrUn;
	    TITORECT(tile, &pla.area);
	    GEO_EXPAND(&pla.area, 1, &biggerArea);
	    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
		if ((pNum != tilePlaneNum) && PlaneMaskHasPlane(pMask, pNum))
		{
		    pla.plane = pNum;
		    (void) DBSrPaintNMArea((Tile *) NULL,
			    arg->fra_def->cd_planes[pNum], dinfo, &biggerArea,
			    mask, extNbrPushFunc, (ClientData) &pla);
		}
	}
    }
    return tilesfound;

fail:
    /* Flush the stack */
    while (!StackEmpty(extNodeStack))
    {
	POPTILE(tile, dinfo, tilePlaneNum);
	ExtSetRegion(tile, dinfo, arg->fra_region);
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
 * Uses the value pla->uninit to determine whether or not a tile has been
 * visited; if the tile's client field is equal to pla->uninit, then this
 * is the first time the tile has been seen.
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
extNbrPushFunc(tile, dinfo, pla)
    Tile *tile;
    TileType dinfo;
    PlaneAndArea *pla;
{
    Rect *tileArea;
    Rect r;

    tileArea = &pla->area;

    /* Ignore tile if it's already been visited */
    if (ExtGetRegion(tile, dinfo) != CD2PTR(pla->uninit))
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
    PUSHTILE(tile, dinfo, pla->plane);

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extEnumTerminal ---
 *
 *	Search out an area belonging to a device terminal starting with a given
 *	tile, and running a callback function for each tile found.  Note that
 *	this routine is called from inside extEnumTilePerim and so is already
 *	inside an extFindNeighbors() search function.  The function must be
 *	careful not to modify regions, as the outer search function depends on
 *	them.  Because a device terminal should be a compact area, it is okay
 *	to create a linked list of tiles and use the linked list to reset the
 *	regions at the end, rather than depending on the state of any tile's
 *	ClientData record.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Whatever the callback function does.  Specifically, changing tile
 *	ClientData records is *not* supposed to be a side effect of this
 *	function, and all ClientData modifications must be put back exactly
 *	as they were found.
 *
 * NOTE: This routine should be called only once for each device terminal.
 * Once the terminal area and perimeter have been measured, it will not be
 * called again.
 *
 * ----------------------------------------------------------------------------
 */

void
extEnumTerminal(Tile *tile,		/* Starting tile for search */
	TileType dinfo,			/* Split tile information */
	TileTypeBitMask *connect,	/* Pointer to connection table */
	void (*func)(),			/* Callback function */
	ClientData clientData)		/* Client data for callback function */
{
    ExtRegion *termreg;
    TileAndDinfo *pendlist = NULL, *resetlist = NULL;
    TileAndDinfo *curtad;
    const TileTypeBitMask *connectMask;
    Tile *tp, *t2;
    TileType tpdi, t2di;
    TileType loctype, checktype;
    Rect tileArea;

    /* The region attached to the first terminal tile will be the
     * "uninitialized" value to check.
     */

    termreg = ExtGetRegion(tile, dinfo);
    /* Set the ClientData to VISITPENDING */
    ExtSetRegion(tile, dinfo, (ExtRegion *)VISITPENDING);

    /* Start the linked list with this file */
    curtad = (TileAndDinfo *)mallocMagic(sizeof(TileAndDinfo));
    curtad->tad_tile = tile;
    curtad->tad_dinfo = dinfo;
    curtad->tad_next = NULL;
    pendlist = curtad;

    /* Yet another boundary search routine.  Just done with a linked list
     * and not a stack because it's expected to search only a handful of
     * tiles.
     */

    while (pendlist != NULL)
    {
	tp = pendlist->tad_tile;
	tpdi = pendlist->tad_dinfo;

	TiToRect(tp, &tileArea);

	/* Call the client function.  The function has no return value. */
	(*func)(tp, tpdi, clientData);

	/* Move this tile entry to reset list */
	curtad = pendlist;
	pendlist = pendlist->tad_next;
	curtad->tad_next = resetlist;
	resetlist = curtad;

	/* Search all sides of the tile for other tiles having the same
	 * terminal node (same region in the ClientData record),
	 * and add them to the linked list.  This code is largely copied
	 * from dbSrConnectFunc().  Note that the connect table is used
	 * because the device's gate node may have the same region but
	 * is not part of the terminal.
	 */

	if (IsSplit(tp))
	{
	    if (tpdi & TT_SIDE)
		loctype = SplitRightType(tp);
	    else
		loctype = SplitLeftType(tp);
	}
	else
	    loctype = TiGetTypeExact(tp);
	connectMask = &connect[loctype];

	/* Left side */
	if (IsSplit(tp) && (tpdi & TT_SIDE)) goto termbottom;

	for (t2 = BL(tp); BOTTOM(t2) < tileArea.r_ytop; t2 = RT(t2))
	{
	    if (IsSplit(t2))
	 	checktype = SplitRightType(t2);
	    else
		checktype = TiGetTypeExact(t2);
	    if (TTMaskHasType(connectMask, checktype))
	    {
		t2di = (TileType)TT_SIDE;
		/* Tile must belong to the terminal node and not been visited */
		if (ExtGetRegion(t2, t2di) != termreg) continue;
		/* Add t2 to the linked list */
		curtad = (TileAndDinfo *)mallocMagic(sizeof(TileAndDinfo));
		curtad->tad_tile = t2;
		curtad->tad_dinfo = t2di;
		curtad->tad_next = pendlist;
		pendlist = curtad;
		/* Set the ClientData to VISITPENDING */
		ExtSetRegion(t2, t2di, (ExtRegion *)VISITPENDING);
	    }
	}

	/* Bottom side */
termbottom:
	if (IsSplit(tp) && ((!((tpdi & TT_SIDE) ? 1 : 0)) ^ SplitDirection(tp)))
	    goto termright;

	for (t2 = LB(tp); LEFT(t2) < tileArea.r_xtop; t2 = TR(t2))
	{
	    if (IsSplit(t2))
		checktype = SplitTopType(t2);
	    else
		checktype = TiGetTypeExact(t2);
	    if (TTMaskHasType(connectMask, checktype))
	    {
		t2di = SplitDirection(t2) ? (TileType)TT_SIDE : (TileType)0;
		/* Tile must belong to the terminal node and not been visited */
		if (ExtGetRegion(t2, t2di) != termreg) continue;
		/* Add t2 to the linked list */
		curtad = (TileAndDinfo *)mallocMagic(sizeof(TileAndDinfo));
		curtad->tad_tile = t2;
		curtad->tad_dinfo = t2di;
		curtad->tad_next = pendlist;
		pendlist = curtad;
		/* Set the ClientData to VISITPENDING */
		ExtSetRegion(t2, t2di, (ExtRegion *)VISITPENDING);
	    }
	}

	/* Right side: */
termright:
	if (IsSplit(tp) && !(tpdi & TT_SIDE)) goto termtop;

	for (t2 = TR(tp); BOTTOM(t2) > tileArea.r_ybot; t2 = LB(t2))
	{
	    if (IsSplit(t2))
		checktype = SplitLeftType(t2);
	    else
		checktype = TiGetTypeExact(t2);
	    if (TTMaskHasType(connectMask, checktype))
	    {
		t2di = (TileType)0;
		/* Tile must belong to the terminal node and not been visited */
		if (ExtGetRegion(t2, t2di) != termreg) continue;
		/* Add t2 to the linked list */
		curtad = (TileAndDinfo *)mallocMagic(sizeof(TileAndDinfo));
		curtad->tad_tile = t2;
		curtad->tad_dinfo = t2di;
		curtad->tad_next = pendlist;
		pendlist = curtad;
		/* Set the ClientData to VISITPENDING */
		ExtSetRegion(t2, t2di, (ExtRegion *)VISITPENDING);
	    }
	}

	/* Top side */
termtop:
	if (IsSplit(tp) && (((tpdi & TT_SIDE) ? 1 : 0) ^ SplitDirection(tp)))
	    goto termdone;

	for (t2 = RT(tp); LEFT(t2) > tileArea.r_xbot; t2 = BL(t2))
	{
	    if (IsSplit(t2))
		checktype = SplitBottomType(t2);
	    else
		checktype = TiGetTypeExact(t2);
	    if (TTMaskHasType(connectMask, checktype))
	    {
		t2di = SplitDirection(t2) ? (TileType)0 : (TileType)TT_SIDE;
		/* Tile must belong to the terminal node and not been visited */
		if (ExtGetRegion(t2, t2di) != termreg) continue;
		/* Add t2 to the linked list */
		curtad = (TileAndDinfo *)mallocMagic(sizeof(TileAndDinfo));
		curtad->tad_tile = t2;
		curtad->tad_dinfo = t2di;
		curtad->tad_next = pendlist;
		pendlist = curtad;
		/* Set the ClientData to VISITPENDING */
		ExtSetRegion(t2, t2di, (ExtRegion *)VISITPENDING);
	    }
	}

termdone:
	/* (continue) */
    }
    
    /* Clean up---Put the ClientData entries in the tiles back to
     * term reg and free up the linked list memory.
     */

    while (resetlist != NULL)
    {
	curtad = resetlist->tad_next;
	ExtSetRegion(resetlist->tad_tile, resetlist->tad_dinfo, termreg);
	freeMagic(resetlist);
	resetlist = curtad;
    }
}

