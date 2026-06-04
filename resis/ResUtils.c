
#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/resis/ResUtils.c,v 1.3 2010/06/24 12:37:56 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/malloc.h"
#include "textio/textio.h"
#include "extract/extract.h"
#include "extract/extractInt.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/stack.h"
#include "utils/tech.h"
#include "textio/txcommands.h"
#include "resis/resis.h"


/*
 * ---------------------------------------------------------------------------
 *
 * ResFirst -- Checks to see if tile is a contact. If it is, allocate a
 *	contact structure.
 *
 *
 * Results: Always returns NULL (in the form of a Region pointer)
 *
 * Side effects:
 *	Memory is allocated by ResFirst.
 *	We cons the newly allocated region onto the front of the existing
 *	region list.
 *
 *
 * -------------------------------------------------------------------------
 */

ExtRegion *
ResFirst(tile, dinfo, arg)
    Tile *tile;
    TileType dinfo;
    FindRegion *arg;
{
    ResContactPoint *reg;
    TileType t;
    int i;

    if (IsSplit(tile))
    {
	t = (dinfo & TT_SIDE) ? SplitRightType(tile) : SplitLeftType(tile);
    }
    else
	t = TiGetType(tile);

    if (DBIsContact(t))
    {
	reg = (ResContactPoint *) mallocMagic((unsigned) (sizeof(ResContactPoint)));
	reg->cp_center.p_x = (LEFT(tile) + RIGHT(tile)) >> 1;
	reg->cp_center.p_y = (TOP(tile) + BOTTOM(tile)) >> 1;
	reg->cp_status = FALSE;
	reg->cp_type = t;
	reg->cp_width = RIGHT(tile) - LEFT(tile);
	reg->cp_height = TOP(tile) - BOTTOM(tile);
	for (i = 0; i < LAYERS_PER_CONTACT; i++)
	{
	    reg->cp_tile[i] = (Tile *) NULL;
	    reg->cp_cnode[i] = (resNode *) NULL;
	}
	reg->cp_currentcontact = 0;
	reg->cp_rect.r_ll.p_x = tile->ti_ll.p_x;
	reg->cp_rect.r_ll.p_y = tile->ti_ll.p_y;
	reg->cp_rect.r_ur.p_x = RIGHT(tile);
	reg->cp_rect.r_ur.p_y = TOP(tile);
	reg->cp_contactTile = tile;
	/* Prepend it to the region list */
	reg->cp_nextcontact = (ResContactPoint *) arg->fra_region;
	arg->fra_region = (ExtRegion *) reg;
    }
    return((ExtRegion *) NULL);
}

/*
 *--------------------------------------------------------------------------
 *
 * resMultiPlaneTerm --
 *
 * Callback function to set a resInfo field
 *
 *--------------------------------------------------------------------------
 */

int
resMultiPlaneTerm(
    Tile *tile,
    TileType dinfo,	// Unused (but should be handled)
    resInfo *rinfo2)
{
    resInfo *Info;
    
    Info = resAddField(tile);
    Info->ri_status |= RES_TILE_SD;
    rinfo2->sourceEdge |= OTHERPLANE;
    return 0;
}

/*
 *--------------------------------------------------------------------------
 *
 * resSubstrateTerm --
 *
 * Callback function to set a resInfo field
 *
 *--------------------------------------------------------------------------
 */

int
resSubstrateTerm(
    Tile *tile,
    TileType dinfo,
    ClientData clientdata)	/* (unused) */
{
    resInfo *Info;
    
    Info = resAddField(tile);
    Info->ri_status |= RES_TILE_SUBS;
    return 0;
}

/*
 *--------------------------------------------------------------------------
 *
 * ResEach--
 *
 * ResEach calls ResFirst unless this is the first contact, in which case it
 * has alreay been processed
 *
 * results: returns 0
 *
 * Side Effects: see ResFirst
 *
 * -------------------------------------------------------------------------
 */

int
ResEach(tile, dinfo, pNum, arg)
    Tile	*tile;
    TileType	dinfo;
    int		pNum;
    FindRegion	*arg;
{

    if (((ResContactPoint *)(arg->fra_region))->cp_contactTile != tile)
    {
	ResFirst(tile, dinfo, arg);
    }
    return(0);
}

/*
 *-------------------------------------------------------------------------
 *
 * ResAddTerminalPlumbing --
 *
 * Called from ResAddDevPlumbing().  When a tile has been found adjacent to
 * a device tile that is a terminal type, walk through all of the tiles
 * belonging to the terminal and mark them all with resInfo structures
 * and set a status of RES_TILE_SD.
 *
 * Results: None.
 *
 * Side effects:  See above.
 *
 *-------------------------------------------------------------------------
 */

void
ResAddTerminalPlumbing(
    Tile 	*tile,
    ExtDevice	*devptr,
    int 	sourceTerm)
{
    static Stack	*resSDStack = NULL;
    Tile *tp1, *tp2;
    TileType t1;

    if (resSDStack == NULL)
     	resSDStack = StackNew(64);

    STACKPUSH(PTR2CD(tile), resSDStack);

    while (!StackEmpty(resSDStack))
    {
	/* Find and mark all tiles belonging to the same source */

       	tp1 = (Tile *) STACKPOP(resSDStack);
	if (IsSplit(tp1))
	{
	    if (TTMaskHasType(&(devptr->exts_deviceSDTypes[sourceTerm]),
				TiGetRightType(tp1)))
		t1 = SplitRightType(tp1);
	    else
		t1 = SplitLeftType(tp1);
	}
	else
	    t1 = TiGetTypeExact(tp1);

	/* Top */
	for (tp2 = RT(tp1); RIGHT(tp2) > LEFT(tp1); tp2 = BL(tp2))
	{
	    if (TTMaskHasType(&(devptr->exts_deviceSDTypes[sourceTerm]),
				TiGetBottomType(tp2)))
	    {
	        resInfo *re = resAddField(tp2);
		if ((re->ri_status & RES_TILE_SD) == 0)
		{
		    re->ri_status |= RES_TILE_SD;
            	    STACKPUSH(PTR2CD(tp2), resSDStack);
		}
	    }
	}

	/* Bottom */
	for (tp2 = LB(tp1); LEFT(tp2) < RIGHT(tp1); tp2 = TR(tp2))
	{
	    if (TTMaskHasType(&(devptr->exts_deviceSDTypes[sourceTerm]),
				TiGetTopType(tp2)))
	    {
	        resInfo *re = resAddField(tp2);
		if ((re->ri_status & RES_TILE_SD) == 0)
		{
		    re->ri_status |= RES_TILE_SD;
            	    STACKPUSH(PTR2CD(tp2), resSDStack);
		}
	    }
	}

	/* Right */
	for (tp2 = TR(tp1); TOP(tp2) > BOTTOM(tp1); tp2 = LB(tp2))
	{
	    if (TTMaskHasType(&(devptr->exts_deviceSDTypes[sourceTerm]),
				TiGetLeftType(tp2)))
	    {
	        resInfo *re = resAddField(tp2);
		if ((re->ri_status & RES_TILE_SD) == 0)
		{
		    re->ri_status |= RES_TILE_SD;
            	    STACKPUSH(PTR2CD(tp2), resSDStack);
		}
	    }
	}

	/* Left */
	for (tp2 = BL(tp1); BOTTOM(tp2) < TOP(tp1); tp2 = RT(tp2))
	{
	    if (TTMaskHasType(&(devptr->exts_deviceSDTypes[sourceTerm]),
				TiGetRightType(tp2)))
	    {
	        resInfo *re = resAddField(tp2);
		if ((re->ri_status & RES_TILE_SD) == 0)
		{
		    re->ri_status |= RES_TILE_SD;
            	    STACKPUSH(PTR2CD(tp2), resSDStack);
		}
	    }
	}
    }
}

/*
 *-------------------------------------------------------------------------
 *
 * ResUnmarkTerminal --
 *
 * After adding structures to all tiles of a device, remove the RES_TILE_SD
 * status from all tiles.
 *
 * Results: None
 *
 * Side effects: Tile status changes
 *
 *-------------------------------------------------------------------------
 */

void
ResUnmarkTerminal(
    Tile 	*tile,
    ExtDevice	*devptr,
    int		sourceTerm)
{
    static Stack	*resSDStack = NULL;
    resInfo *re, *re2;
    Tile *tp1, *tp2;
    TileType t1;

    if (resSDStack == NULL)
     	resSDStack = StackNew(64);

    re = (resInfo *)TiGetClientPTR(tile);

    STACKPUSH(PTR2CD(tile), resSDStack);
    re->ri_status &= ~RES_TILE_SD;

    while (!StackEmpty(resSDStack))
    {
        tp1 = (Tile *) STACKPOP(resSDStack);
	if (IsSplit(tp1))
	{
	    if (TTMaskHasType(&(devptr->exts_deviceSDTypes[sourceTerm]),
				TiGetRightType(tp1)))
		t1 = SplitRightType(tp1);
	    else
		t1 = SplitLeftType(tp1);
	}
	else
	    t1 = TiGetTypeExact(tp1);

	/* Top */
	for (tp2 = RT(tp1); RIGHT(tp2) > LEFT(tp1); tp2 = BL(tp2))
	{
	    re2 = (resInfo *) TiGetClientPTR(tp2);
	    if ((re2 != (resInfo *)CLIENTDEFAULT) && (re2->ri_status & RES_TILE_SD))
	    {
	        if (TTMaskHasType(&(devptr->exts_deviceSDTypes[sourceTerm]),
				TiGetBottomType(tp2)))
		{
		    re2->ri_status &= ~RES_TILE_SD;
            	    STACKPUSH(PTR2CD(tp2), resSDStack);
		}
	    }
	}

	/* Bottom */
	for (tp2 = LB(tp1); LEFT(tp2) < RIGHT(tp1); tp2 = TR(tp2))
	{
	    re2 = (resInfo *) TiGetClientPTR(tp2);
	    if ((re2 != (resInfo *)CLIENTDEFAULT) && (re2->ri_status & RES_TILE_SD))
	    {
	        if (TTMaskHasType(&(devptr->exts_deviceSDTypes[sourceTerm]),
				TiGetTopType(tp2)))
		{
		    re2->ri_status &= ~RES_TILE_SD;
            	    STACKPUSH(PTR2CD(tp2), resSDStack);
		}
	    }
	}

	/* Right */
	for (tp2 = TR(tp1); TOP(tp2) > BOTTOM(tp1); tp2 = LB(tp2))
	{
	    re2 = (resInfo *) TiGetClientPTR(tp2);
	    if ((re2 != (resInfo *)CLIENTDEFAULT) && (re2->ri_status & RES_TILE_SD))
	    {
	        if (TTMaskHasType(&(devptr->exts_deviceSDTypes[sourceTerm]),
				TiGetLeftType(tp2)))
		{
		    re2->ri_status &= ~RES_TILE_SD;
            	    STACKPUSH(PTR2CD(tp2), resSDStack);
		}
	    }
	}

	/* Left */
	for (tp2 = BL(tp1); BOTTOM(tp2) < TOP(tp1); tp2 = RT(tp2))
	{
	    re2 = (resInfo *) TiGetClientPTR(tp2);
	    if ((re2 != (resInfo *)CLIENTDEFAULT) && (re2->ri_status & RES_TILE_SD))
	    {
	        if (TTMaskHasType(&(devptr->exts_deviceSDTypes[sourceTerm]),
				TiGetRightType(tp2)))
		{
		    re2->ri_status &= ~RES_TILE_SD;
            	    STACKPUSH(PTR2CD(tp2), resSDStack);
		}
	    }
	}
    }
}

/*
 *-------------------------------------------------------------------------
 *
 * ResAddlumbing --
 *
 * Each tile has a resInfo structure associated with it to keep track of
 * various things used by the extractor. ResAddDevPlumbing adds this structure
 * and sets the tile's ClientData field to point to it.
 *
 * Results: Always return 0 to keep the search going.
 *
 * Side Effects: See above
 *
 *-------------------------------------------------------------------------
 */

int
ResAddPlumbing(
    Tile *tile,
    TileType dinfo,		/* (unused) */
    ClientData clientdata)	/* (unused) */
{
    if (TiGetClient(tile) == CLIENTDEFAULT)
	resAddField(tile);
    return 0;
}

/*
 *-------------------------------------------------------------------------
 *
 * ResAddDevPlumbing --
 *
 * Each tile has a resInfo structure associated with it to keep track of
 * various things used by the extractor. ResAddDevPlumbing adds this structure
 * to tiles associated with devices and sets the tile's ClientData field to
 * point to it.  A device structure is also added; all connected device
 * tiles are enumerated and their deviceList fields set to the new structure.
 *
 * Results: None.
 *
 * Side Effects: See above
 *
 *-------------------------------------------------------------------------
 */

void
ResAddDevPlumbing(
    ResDevTile *thisDev,		/* Pointer to device from .ext file */
    resDevice  **resDevListPtr)		/* Add to this list */
{
    resInfo		*Info, *rinfo2;
    static Stack	*resDevStack = NULL;
    Tile 		*tile, *tp1, *tp2, *sourceTile = NULL;
    TileType		ttype, loctype, t1;
    TileTypeBitMask	locDevSubsMask;
    int			i, pNum, nterms;
    Plane		*plane;
    Rect		r;
    resDevice		*resDev;
    ExtDevice		*devptr;
    int			srcidx, sourceTerm = -1;

    if (resDevStack == NULL)
     	resDevStack = StackNew(64);

    ttype =  thisDev->type;
    pNum =   DBPlane(ttype);
    plane =  ResDef->cd_planes[pNum];
    devptr = thisDev->devptr;
    /* Add 2 to # terminals to include the device node and the substrate */
    nterms = devptr->exts_deviceSDCount + 2;
    
    /* Find the tile in the location of the device on the device's plane.
     * Note that GOTOPOINT() is used because the actual tile will have
     * changed after dissolving contacts and re-forming the entire database
     * with ResFract().
     */

    tile = PlaneGetHint(plane);
    GOTOPOINT(tile, &(thisDev->area.r_ll));
    PlaneSetHint(plane, tile);

    if (IsSplit(tile))
    {
	loctype = SplitRightType(tile);
	if (!TTMaskHasType(&ExtCurStyle->exts_deviceConn[ttype], loctype))
	    loctype = SplitLeftType(tile);
    }
    else
	loctype = TiGetTypeExact(tile);

    /* Create a record for the device type and add it to tile */

    resDev = (resDevice *)mallocMagic((unsigned)(sizeof(resDevice)));
    resDev->rd_nterms = nterms;

    resDev->rd_terminals = (resNode **)mallocMagic(nterms * sizeof(resNode *));
    for (i = 0; i != nterms; i++) resDev->rd_terminals[i] = (resNode *) NULL;

    resDev->rd_tile = tile;
    resDev->rd_inside.r_ll.p_x = LEFT(tile);
    resDev->rd_inside.r_ll.p_y = BOTTOM(tile);
    resDev->rd_inside.r_ur.p_x = RIGHT(tile);
    resDev->rd_inside.r_ur.p_y = TOP(tile);
    resDev->rd_devtype = loctype;
    resDev->rd_tiles = 0;
    resDev->rd_length = 0;
    resDev->rd_width = 0;
    resDev->rd_perim = 0;
    resDev->rd_area = 0;
    resDev->rd_status = 0;
    resDev->rd_nextDev = (resDevice *)*resDevListPtr;
    *resDevListPtr = (ClientData)resDev;

    /* Add a record to the initial tile */
    rinfo2 = resAddField(tile);
    rinfo2->deviceList = resDev;
    rinfo2->ri_status |= RES_TILE_DEV;

    /* Walk the area of the device, adding records to the tiles and
     * looking for terminals.  When a terminal is found, walk the
     * area of the terminal, adding records to the tiles.  Mark
     * terminal tiles adjacent to the device with the relative
     * position.
     */

    STACKPUSH(PTR2CD(tile), resDevStack);
    while (!StackEmpty(resDevStack))
    {
       	resInfo   *re0;

	tp1 = (Tile *)STACKPOP(resDevStack);
	if (IsSplit(tp1))
	{
	    t1 = SplitRightType(tp1);
	    if (!TTMaskHasType(&ExtCurStyle->exts_deviceConn[ttype], t1))
		t1 = SplitLeftType(tp1);
	}
	else
	    t1 = TiGetTypeExact(tp1);

	re0 = (resInfo *) TiGetClientPTR(tp1);

	/* Top */
	for (tp2 = RT(tp1); RIGHT(tp2) > LEFT(tp1); tp2 = BL(tp2))
	{
	    if (TTMaskHasType(&ExtCurStyle->exts_deviceConn[t1], TiGetBottomType(tp2))
			&& (TiGetClient(tp2) == CLIENTDEFAULT))
	    {
		STACKPUSH(PTR2CD(tp2), resDevStack);
       		Info = resAddField(tp2);
       		Info->deviceList = resDev;
       		Info->ri_status |= RES_TILE_DEV;

		/* Update device position to point to the lower-leftmost tile */
		if ((tp2->ti_ll.p_x < resDev->rd_inside.r_ll.p_x) ||
			((tp2->ti_ll.p_x == resDev->rd_inside.r_ll.p_x) &&
			(tp2->ti_ll.p_y < resDev->rd_inside.r_ll.p_y)))
		{
		    resDev->rd_inside.r_ll.p_x = LEFT(tp2);
		    resDev->rd_inside.r_ll.p_y = BOTTOM(tp2);
		    resDev->rd_inside.r_ur.p_x = RIGHT(tp2);
		    resDev->rd_inside.r_ur.p_y = TOP(tp2);
		}
	    }
	    else
	    {
		if (sourceTerm < 0)
		{
		    for (srcidx = 0; srcidx < (nterms - 2); srcidx++)
		    {
			if (TTMaskHasType(&(devptr->exts_deviceSDTypes[srcidx]),
					TiGetBottomType(tp2)))
			{
			    sourceTile = tp2;
			    sourceTerm = srcidx;
			    ResAddTerminalPlumbing(tp2, devptr, srcidx);
			    break;
			}
		    }
		}
		if (sourceTerm >= 0)
		{
		    if (TTMaskHasType(&(devptr->exts_deviceSDTypes[sourceTerm]),
				TiGetBottomType(tp2)))
		    {
			Info = resAddField(tp2);
			if (Info->ri_status & RES_TILE_SD)
			    re0->sourceEdge |= TOPEDGE;
		    }
		}
	    }
	}

	/* Bottom */
	for (tp2 = LB(tp1); LEFT(tp2) < RIGHT(tp1); tp2 = TR(tp2))
	{
	    if (TTMaskHasType(&ExtCurStyle->exts_deviceConn[t1], TiGetTopType(tp2))
		      && (TiGetClient(tp2) == CLIENTDEFAULT))
	    {
            	STACKPUSH(PTR2CD(tp2), resDevStack);
       		Info = resAddField(tp2);
       		Info->deviceList =  resDev;
       		Info->ri_status |= RES_TILE_DEV;

		/* Update device position to point to the lower-leftmost tile */
		if ((tp2->ti_ll.p_x < resDev->rd_inside.r_ll.p_x) ||
			((tp2->ti_ll.p_x == resDev->rd_inside.r_ll.p_x) &&
			(tp2->ti_ll.p_y < resDev->rd_inside.r_ll.p_y)))
		{
		    resDev->rd_inside.r_ll.p_x = LEFT(tp2);
		    resDev->rd_inside.r_ll.p_y = BOTTOM(tp2);
		    resDev->rd_inside.r_ur.p_x = RIGHT(tp2);
		    resDev->rd_inside.r_ur.p_y = TOP(tp2);
		}
	    }
	    else
	    {
		if (sourceTerm < 0)
		{
		    for (srcidx = 0; srcidx < (nterms - 2); srcidx++)
		    {
			if (TTMaskHasType(&(devptr->exts_deviceSDTypes[srcidx]),
					TiGetBottomType(tp2)))
			{
			    sourceTile = tp2;
			    sourceTerm = srcidx;
			    ResAddTerminalPlumbing(tp2, devptr, srcidx);
			    break;
			}
		    }
		}
		if (sourceTerm >= 0)
		{
		    if (TTMaskHasType(&(devptr->exts_deviceSDTypes[sourceTerm]),
				TiGetBottomType(tp2)))
		    {
			Info = resAddField(tp2);
			if (Info->ri_status & RES_TILE_SD)
			    re0->sourceEdge |= BOTTOMEDGE;
		    }
		}
	    }
	}

	/* Right */
	for (tp2 = TR(tp1); TOP(tp2) > BOTTOM(tp1); tp2 = LB(tp2))
	{
	    if (TTMaskHasType(&ExtCurStyle->exts_deviceConn[t1], TiGetLeftType(tp2))
		      && (TiGetClient(tp2) == CLIENTDEFAULT))
	    {
            	STACKPUSH(PTR2CD(tp2), resDevStack);
		Info = resAddField(tp2);
       		Info->deviceList =  resDev;
       		Info->ri_status |= RES_TILE_DEV;

		/* Update device position to point to the lower-leftmost tile */
		if ((tp2->ti_ll.p_x < resDev->rd_inside.r_ll.p_x) ||
			((tp2->ti_ll.p_x == resDev->rd_inside.r_ll.p_x) &&
			(tp2->ti_ll.p_y < resDev->rd_inside.r_ll.p_y)))
		{
		    resDev->rd_inside.r_ll.p_x = LEFT(tp2);
		    resDev->rd_inside.r_ll.p_y = BOTTOM(tp2);
		    resDev->rd_inside.r_ur.p_x = RIGHT(tp2);
		    resDev->rd_inside.r_ur.p_y = TOP(tp2);
		}
	    }
	    else
	    {
		if (sourceTerm < 0)
		{
		    for (srcidx = 0; srcidx < (nterms - 2); srcidx++)
		    {
			if (TTMaskHasType(&(devptr->exts_deviceSDTypes[srcidx]),
					TiGetBottomType(tp2)))
			{
			    sourceTile = tp2;
			    sourceTerm = srcidx;
			    ResAddTerminalPlumbing(tp2, devptr, srcidx);
			    break;
			}
		    }
		}
		if (sourceTerm >= 0)
		{
		    if (TTMaskHasType(&(devptr->exts_deviceSDTypes[sourceTerm]),
				TiGetBottomType(tp2)))
		    {
			Info = resAddField(tp2);
			if (Info->ri_status & RES_TILE_SD)
			    re0->sourceEdge |= RIGHTEDGE;
		    }
		}
	    }
	}

	/* Left */
	for (tp2 = BL(tp1); BOTTOM(tp2) < TOP(tp1); tp2 = RT(tp2))
	{
	    if (TTMaskHasType(&ExtCurStyle->exts_deviceConn[t1], TiGetRightType(tp2))
		      && (TiGetClient(tp2) == CLIENTDEFAULT))
	    {
            	STACKPUSH(PTR2CD(tp2), resDevStack);
       		Info = resAddField(tp2);
       		Info->deviceList =  resDev;
       		Info->ri_status |= RES_TILE_DEV;

		/* Update device position to point to the lower-leftmost tile */
		if ((tp2->ti_ll.p_x < resDev->rd_inside.r_ll.p_x) ||
			((tp2->ti_ll.p_x == resDev->rd_inside.r_ll.p_x) &&
			(tp2->ti_ll.p_y < resDev->rd_inside.r_ll.p_y)))
		{
		    resDev->rd_inside.r_ll.p_x = LEFT(tp2);
		    resDev->rd_inside.r_ll.p_y = BOTTOM(tp2);
		    resDev->rd_inside.r_ur.p_x = RIGHT(tp2);
		    resDev->rd_inside.r_ur.p_y = TOP(tp2);
		}
	    }
	    else
	    {
		if (sourceTerm < 0)
		{
		    for (srcidx = 0; srcidx < (nterms - 2); srcidx++)
		    {
			if (TTMaskHasType(&(devptr->exts_deviceSDTypes[srcidx]),
					TiGetBottomType(tp2)))
			{
			    sourceTile = tp2;
			    sourceTerm = srcidx;
			    ResAddTerminalPlumbing(tp2, devptr, srcidx);
			    break;
			}
		    }
		}
		if (sourceTerm >= 0)
		{
		    if (TTMaskHasType(&(devptr->exts_deviceSDTypes[sourceTerm]),
				TiGetBottomType(tp2)))
		    {
			Info = resAddField(tp2);
			if (Info->ri_status & RES_TILE_SD)
			    re0->sourceEdge |= LEFTEDGE;
		    }
		}
	    }
	}
    }

    TiToRect(tile, &r);

    /* Check other planes for terminals */
    if (sourceTile == NULL)
    {
	for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	{
	    for (srcidx = 0; srcidx < (nterms - 2); srcidx++)
	    {
		if (TTMaskIntersect(&DBPlaneTypes[pNum],
			&(devptr->exts_deviceSDTypes[srcidx])))
		    DBSrPaintArea((Tile *)NULL,
				ResUse->cu_def->cd_planes[pNum],
				&r, &(devptr->exts_deviceSDTypes[srcidx]),
				resMultiPlaneTerm, (ClientData)rinfo2);
	    }
	}
    }

    /* Find device substrate */

    TTMaskZero(&locDevSubsMask);
    TTMaskSetMask(&locDevSubsMask, &(devptr->exts_deviceSubstrateTypes));
    TTMaskClearType(&locDevSubsMask, TT_SPACE);

    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
    {
	if (TTMaskIntersect(&DBPlaneTypes[pNum], &locDevSubsMask))
	    DBSrPaintArea((Tile *)NULL, 
			    ResUse->cu_def->cd_planes[pNum],
			    &r, &locDevSubsMask,
			    resSubstrateTerm, (ClientData)NULL);
    }

    if (sourceTile)
	ResUnmarkTerminal(sourceTile, devptr, srcidx);
}

/*
 *-------------------------------------------------------------------------
 *
 * ResRemovePlumbing-- Removes and deallocates all the resInfo fields.
 *
 * Results: returns 0
 *
 * Side Effects: frees up memory; resets tile->ti_client fields to CLIENTDEFAULT
 *
 *-------------------------------------------------------------------------
 */

int
ResRemovePlumbing(tile, dinfo, arg)
    Tile	*tile;
    TileType	dinfo;		// Unused, but should be handled.
    ClientData	*arg;

{
    ClientData ticlient = TiGetClient(tile);
    if (ticlient != CLIENTDEFAULT)
    {
	freeMagic((char *)CD2PTR(ticlient));
	TiSetClient(tile, CLIENTDEFAULT);
    }
    return(0);
}


/*
 *-------------------------------------------------------------------------
 *
 * ResPreProcessDevices-- Given a list of all the device tiles and
 * a list of all the devices, this procedure calculates the width and
 * length.  The width is set equal to the sum of all edges that touch
 * diffusion divided by 2. The length is the remaining perimeter divided by
 * 2*tiles.  The perimeter and area fields of device structures are also
 * fixed.
 *
 * Results: none
 *
 * Side Effects: sets length and width of devices. "ResDevTile"
 * structures are freed.
 *
 *-------------------------------------------------------------------------
 */

void
ResPreProcessDevices(TileList, DeviceList, Def)
    ResDevTile		*TileList;
    resDevice		*DeviceList;
    CellDef		*Def;
{
    Tile	*tile;
    ResDevTile	*oldTile;
    resInfo	*tstruct;
    TileType	tt, residue;
    int		pNum;

    while (TileList != (ResDevTile *) NULL)
    {
	tt = TileList->type;
	if (DBIsContact(tt))
	{
	    /* Find which residue of the contact is a device. */
	    TileTypeBitMask ttresidues;

	    DBFullResidueMask(tt, &ttresidues);

	    for (residue = TT_TECHDEPBASE; residue < DBNumUserLayers; residue++)
	    {
		if (TTMaskHasType(&ttresidues, residue))
		{
		    if (TTMaskHasType(&ExtCurStyle->exts_deviceMask, residue))
		    {
			pNum = DBPlane(residue);
			break;
		    }
		}
	    }
	}
	else
	    pNum = DBPlane(tt);		/* always correct for non-contact types */

	tile = PlaneGetHint(Def->cd_planes[pNum]);
	GOTOPOINT(tile, &(TileList->area.r_ll));
	PlaneSetHint(Def->cd_planes[pNum], tile);

	tt = TiGetType(tile);
	tstruct = (resInfo *) TiGetClientPTR(tile);

	if ((tstruct == (resInfo *)CLIENTDEFAULT) ||
		    (tstruct->deviceList == NULL) ||
		    !TTMaskHasType(&ExtCurStyle->exts_deviceMask, tt))
	{
	    TxError("Bad Device Location at %d,%d\n",
			TileList->area.r_ll.p_x,
			TileList->area.r_ll.p_y);
	}
	else if ((tstruct->ri_status & RES_TILE_MARK) == 0)
	{
	    resDevice	*rd = tstruct->deviceList;

	    tstruct->ri_status |= RES_TILE_MARK;
	    rd->rd_perim += TileList->perim;
	    rd->rd_length += TileList->overlap;
	    rd->rd_area += (TileList->area.r_xtop - TileList->area.r_xbot)
			* (TileList->area.r_ytop - TileList->area.r_ybot);
	    rd->rd_tiles++;
	}
	oldTile = TileList;
	TileList = TileList->nextDev;
	freeMagic((char *)oldTile);
    }

    for (; DeviceList != NULL; DeviceList = DeviceList->rd_nextDev)
    {
     	int width  = DeviceList->rd_perim;
	int length = DeviceList->rd_length;
	if (DeviceList->rd_tiles != 0)
	{
	    if (length)
	    {
	        DeviceList->rd_length = (float) length /
			((float)((DeviceList->rd_tiles) << 1));
	        DeviceList->rd_width = (width-length) >> 1;
	    }
	    else
	    {
	       	double perimeter = DeviceList->rd_perim;
		double area = DeviceList->rd_area;

		perimeter /= 4.0;

		DeviceList->rd_width = perimeter +
			sqrt(perimeter * perimeter-area);
		DeviceList->rd_length = (DeviceList->rd_perim
			- 2 * DeviceList->rd_width) >> 1;
	    }
	}
    }
}


/*
 *-------------------------------------------------------------------------
 *
 * ResAddToQueue-- adds new nodes to list of nodes requiring processing.
 *
 * Side Effects: nodes are added to list (i.e they have their linked list
 *	pointers modified.)
 *
 *-------------------------------------------------------------------------
 */

void
ResAddToQueue(node, list)
   resNode  *node, **list;
{
   node->rn_more = *list;
   node->rn_less = NULL;
   if (*list) (*list)->rn_less = node;
   *list = node;
}

/*
 *-------------------------------------------------------------------------
 *
 * ResRemoveFromQueue-- removes node from queue. Complains if it notices
 *	that the node isn't in the supplied list.
 *
 * Results: none
 *
 * Side Effects: modifies nodelist
 *
 *-------------------------------------------------------------------------
 */

void
ResRemoveFromQueue(node, list)
    resNode	*node, **list;
{
    if (node->rn_less != NULL)
    {
     	node->rn_less->rn_more = node->rn_more;
    }
    else
    {
     	if (node != (*list))
	{
	    TxError("Error: Attempt to remove node from wrong list\n");
	}
	else
	{
	    *list = node->rn_more;
	}
    }
    if (node->rn_more != NULL)
    {
     	node->rn_more->rn_less = node->rn_less;
    }
    node->rn_more = NULL;
    node->rn_less = NULL;
}

resInfo *
resAddField(tile)
    Tile    *tile;
{
    ClientData ticlient = TiGetClient(tile);
    resInfo *Info = (resInfo *)CD2PTR(ticlient);
    if (ticlient == CLIENTDEFAULT)
    {
     	Info = (resInfo *) mallocMagic((unsigned) (sizeof(resInfo)));
	ResInfoInit(Info);
	TiSetClientPTR(tile, Info);
    }
    return Info;
}
