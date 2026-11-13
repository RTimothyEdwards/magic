/* DBconnect.c -
 *
 *	This file contains routines that extract electrically connected
 *	regions of a layout for Magic.  There are two extractors, one
 *	that operates only within the paint of a single cell (DBSrConnect),
 *	and one	that operates hierarchically, across cell boundaries
 *	(DBTreeCopyConnect).
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/database/DBconnect.c,v 1.6 2010/09/15 18:15:40 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>		// for memcpy()
#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "utils/stack.h"
#include "database/database.h"
#include "database/databaseInt.h"
#include "select/select.h"
#include "utils/signals.h"
#include "utils/malloc.h"

/* C99 compat */
#include "textio/textio.h"

/* Global variable */
Stack *dbConnectStack = (Stack *)NULL;

/* General note for DBSrConnect:
 *
 * The connectivity extractor works in two passes, in order to avoid
 * circularities.  During the first pass, each connected tile gets
 * marked, using the ti_client field.  This marking is needed to
 * avoid infinite searches on circular structures.  The second pass
 * is used to clear the markings again.
 */

/*
 *-----------------------------------------------------------------
 * DBTransformDiagonal --
 *
 *	Resolve geometric transformations on diagonally-split tiles
 *	Assumes that we have already determined that this tile is
 *	split.
 *
 * Results:
 *	A tile type containing embedded diagonal and side information.
 *	Note that this tile type does NOT contain any actual type
 *	information.
 *
 * Side Effects:
 *	None.
 *-----------------------------------------------------------------
 */

TileType
DBTransformDiagonal(oldtype, trans)
    TileType oldtype;
    Transform *trans;
{
    TileType dinfo;
    int o1, o2, o3, dir, side;

    o1 = ((trans->t_e > 0) || (trans->t_d > 0)) ? 1 : 0;
    o2 = ((trans->t_a > 0) || (trans->t_b > 0)) ? 1 : 0;
    o3 = (trans->t_a != 0) ? 1 : 0;
    dir = (oldtype & TT_DIRECTION) ? 1 : 0;
    side = ((oldtype & TT_SIDE) ? 1 : 0) ^ o2 ^ (dir | o3);
    dir ^= o1 ^ o2;

    dinfo = TT_DIAGONAL;
    if (side) dinfo |= TT_SIDE;
    if (dir) dinfo |= TT_DIRECTION;

    return dinfo;
}

/*
 *-----------------------------------------------------------------
 * DBInvTransformDiagonal --
 *
 *	This is equivalent to the routine above, but inverted, so
 *	that the result is correct for the orientation of the
 *	triangle in the coordinate system of the child cell,
 *	rather than the parent cell (which comes down to merely
 *	swapping transform positions b and d, since translation
 *	isn't considered).
 *-----------------------------------------------------------------
 */

TileType
DBInvTransformDiagonal(oldtype, trans)
    TileType oldtype;
    Transform *trans;
{
    TileType dinfo;
    int o1, o2, o3;
    int dir, side;

    o1 = ((trans->t_e > 0) || (trans->t_b > 0)) ? 1 : 0;
    o2 = ((trans->t_a > 0) || (trans->t_d > 0)) ? 1 : 0;
    o3 = (trans->t_a != 0) ? 1 : 0;

    dir = (oldtype & TT_DIRECTION) ? 1 : 0;
    side = ((oldtype & TT_SIDE) ? 1 : 0) ^ o2 ^ (dir | o3);
    dir ^= o1 ^ o2;

    dinfo = TT_DIAGONAL;
    if (side) dinfo |= TT_SIDE;
    if (dir) dinfo |= TT_DIRECTION;

    return dinfo;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBSrConnect --
 *
 * 	Search through a cell to find all paint that is electrically
 *	connected to things in a given starting area.
 *
 * Results:
 *	0 is returned if the search finished normally.  1 is returned
 *	if the search was aborted.
 *
 * Side effects:
 *	The search starts from one (random) non-space tile in "startArea"
 *	that matches the types in the mask parameter.  For every paint
 *	tile that is electrically connected to the initial tile and that
 *	intersects the rectangle "bounds", func is called.  Func should
 *	have the following form:
 *
 *	    int
 *	    func(tile, clientData)
 *		Tile *tile;
 *		ClientData clientData;
 *    	    {
 *	    }
 *
 *	The clientData passed to func is the same one that was passed
 *	to us.  Func returns 0 under normal conditions;  if it returns
 *	1 then the search is aborted.
 *
 *				*** WARNING ***
 *
 *	Func should not modify any paint during the search, since this
 *	will mess up pointers kept by these procedures and likely cause
 *	a core-dump.
 *
 * ----------------------------------------------------------------------------
 */

int
DBSrConnect(def, startArea, mask, connect, bounds, func, clientData)
    CellDef *def;		/* Cell definition in which to carry out
				 * the connectivity search.  Only paint
				 * in this definition is considered.
				 */
    Rect *startArea;		/* Area to search for an initial tile.  Only
				 * tiles OVERLAPPING the area are considered.
				 * This area should have positive x and y
				 * dimensions.
				 */
    TileTypeBitMask *mask;	/* Only tiles of one of these types are used
				 * as initial tiles.
				 */
    TileTypeBitMask *connect;	/* Pointer to a table indicating what tile
				 * types connect to what other tile types.
				 * Each entry gives a mask of types that
				 * connect to tiles of a given type.
				 */
    Rect *bounds;		/* Area, in coords of scx->scx_use->cu_def,
				 * that limits the search:  only tiles
				 * overalapping this area will be returned.
				 * Use TiPlaneRect to search everywhere.
				 */
    int (*func)();		/* Function to apply at each connected tile. */
    ClientData clientData;	/* Client data for above function. */

{
    struct conSrArg csa;
    int startPlane, result;
    TileAndDinfo start_tad;	/* Starting tile and split information */

    result = 0;
    csa.csa_def = def;
    csa.csa_bounds = *bounds;

    /* Find a starting tile (if there are many tiles underneath the
     * starting area, pick any one).  The search function just saves
     * the tile address and returns.
     */

    start_tad.tad_tile = NULL;
    start_tad.tad_next = NULL;  /* unused */
    for (startPlane = PL_TECHDEPBASE; startPlane < DBNumPlanes; startPlane++)
    {
    	csa.csa_pNum = startPlane;
	if (DBSrPaintArea((Tile *) NULL,
	    def->cd_planes[startPlane], startArea, mask,
	    dbSrConnectStartFunc, PTR2CD(&start_tad)) != 0) break;
    }
    if (start_tad.tad_tile == NULL) return 0;
    /* The following lets us call DBSrConnect recursively */
    else if (start_tad.tad_tile->ti_client == (ClientData)1) return 0;

    /* Pass 1.  During this pass the client function gets called. */

    csa.csa_clientFunc = func;
    csa.csa_clientData = clientData;
    csa.csa_clientDefault = CLIENTDEFAULT;
    csa.csa_clear = FALSE;
    csa.csa_connect = connect;
    if (dbSrConnectFunc(start_tad.tad_tile, start_tad.tad_dinfo,
			PTR2CD(&csa)) != 0) result = 1;

    /* Pass 2.  Don't call any client function, just clear the marks.
     * Don't allow any interruptions.
     */

    SigDisableInterrupts();
    csa.csa_clientFunc = NULL;
    csa.csa_clear = TRUE;
    (void) dbSrConnectFunc(start_tad.tad_tile, start_tad.tad_dinfo, PTR2CD(&csa));
    SigEnableInterrupts();

    return result;
}

/** @typedef cb_database_srpaintarea_t */
int
dbSrConnectStartFunc(
    Tile *tile,		/* This will be the starting tile. */
    TileType dinfo,	/* (unused) */
    ClientData cdata)	/* We store tile and split info here. */
{
    TileAndDinfo *tad = (TileAndDinfo *)CD2PTR(cdata);
    tad->tad_tile = tile;
    tad->tad_dinfo = dinfo;
    return 1;
}

/* Function similar to DBSrConnect but which does the first pass only	*/
/* and leaves the marked tiles intact.  Tiles must be cleared by the	*/
/* caller.								*/

int
DBSrConnectOnePass(def, startArea, mask, connect, bounds, func, clientData)
    CellDef *def;		/* Cell definition in which to carry out
				 * the connectivity search.  Only paint
				 * in this definition is considered.
				 */
    Rect *startArea;		/* Area to search for an initial tile.  Only
				 * tiles OVERLAPPING the area are considered.
				 * This area should have positive x and y
				 * dimensions.
				 */
    TileTypeBitMask *mask;	/* Only tiles of one of these types are used
				 * as initial tiles.
				 */
    TileTypeBitMask *connect;	/* Pointer to a table indicating what tile
				 * types connect to what other tile types.
				 * Each entry gives a mask of types that
				 * connect to tiles of a given type.
				 */
    Rect *bounds;		/* Area, in coords of scx->scx_use->cu_def,
				 * that limits the search:  only tiles
				 * overalapping this area will be returned.
				 * Use TiPlaneRect to search everywhere.
				 */
    int (*func)();		/* Function to apply at each connected tile. */
    ClientData clientData;	/* Client data for above function. */

{
    struct conSrArg csa;
    int startPlane, result;
    TileAndDinfo tad;

    result = 0;
    csa.csa_def = def;
    csa.csa_bounds = *bounds;

    /* Find a starting tile (if there are many tiles underneath the
     * starting area, pick any one).  The search function just saves
     * the tile address and returns.
     */

    tad.tad_tile = NULL;
    tad.tad_next = NULL;   /* unused */
    for (startPlane = PL_TECHDEPBASE; startPlane < DBNumPlanes; startPlane++)
    {
    	csa.csa_pNum = startPlane;
	if (DBSrPaintArea((Tile *) NULL,
	    def->cd_planes[startPlane], startArea, mask,
	    dbSrConnectStartFunc, PTR2CD(&tad)) != 0) break;
    }
    if (tad.tad_tile == NULL) return 0;
    /* The following lets us call DBSrConnect recursively */
    else if (tad.tad_tile->ti_client == (ClientData)1) return 0;

    /* Pass 1.  During this pass the client function gets called. */

    csa.csa_clientFunc = func;
    csa.csa_clientData = clientData;
    csa.csa_clientDefault = CLIENTDEFAULT;
    csa.csa_clear = FALSE;
    csa.csa_connect = connect;
    if (dbSrConnectFunc(tad.tad_tile, tad.tad_dinfo, PTR2CD(&csa)) != 0) result = 1;

    return result;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbcFindTileFunc --
 *
 *	Simple callback used by dbSrConnectFunc to return any tile found
 *	matching the search type mask.
 *
 * Results:
 *	Always 1.
 *
 * Side effects:
 *	Fill client data with a pointer to the tile found.
 *
 * ----------------------------------------------------------------------------
 */

int
dbcFindTileFunc(tile, dinfo, arg)
    Tile *tile;
    TileType dinfo;
    ClientData arg;
{
    TileAndDinfo *tad = (TileAndDinfo *)arg;

    tad->tad_tile = tile;
    tad->tad_dinfo = dinfo;
    return 1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbSrConnectFunc --
 *
 * 	This search function gets called by DBSrPaintArea as part
 *	of DBSrConnect, and also recursively by itself.  Each invocation
 *	is made to process a single tile that is of interest.
 *
 * Results:
 *	0 is returned unless the client function returns a non-zero
 *	value, in which case 1 is returned.
 *
 * Side effects:
 *	If this tile has been seen before, then just return
 *	immediately. If this tile hasn't been seen before, it is
 *	marked and the client procedure is called.  A NULL client
 *	procedure is not called, of course.  In addition, we scan
 *	the tiles perimeter for any connected tiles, and call
 *	ourselves recursively on them.
 *
 * Design note:
 *	This one procedure is used during both the marking and clearing
 *	passes, so "seen before" is a function both of the ti_client
 *	field in the tile and the csa_clear value.
 *
 *	9/21/2022:  Changed from being a recursive routine to using a
 *	stack method, as large power/ground networks were causing stack
 *	smashing.
 *
 * ----------------------------------------------------------------------------
 */

int
dbSrConnectFunc(
    Tile *tile,		/* Tile that is connected. */
    TileType dinfo,	/* Split tile information */
    ClientData cdata)	/* Contains information about the search. */
			/* (struct conSrArg *csa) */
{
    struct conSrArg *csa = (struct conSrArg *)CD2PTR(cdata);
    Tile *t2;
    Rect tileArea;
    int i, pNum;
    int result = 0;
    bool callClient;
    const TileTypeBitMask *connectMask;
    TileType loctype, checktype;
    PlaneMask planes;

    if (dbConnectStack == (Stack *)NULL)
	dbConnectStack = StackNew(256);

    /* Drop the first entry on the stack */
    pNum = csa->csa_pNum;
    STACKPUSH(INT2CD(tile), dbConnectStack);
    STACKPUSH(INT2CD(dinfo), dbConnectStack);
    STACKPUSH(INT2CD(pNum), dbConnectStack);

    while (!StackEmpty(dbConnectStack))
    {
	pNum = (int)CD2INT(STACKPOP(dbConnectStack));
	dinfo = (int)CD2INT(STACKPOP(dbConnectStack));
	tile = (Tile *)CD2INT(STACKPOP(dbConnectStack));
	if (result == 1) continue;

	TiToRect(tile, &tileArea);

	/* Make sure this tile overlaps the area we're interested in. */

	if (!GEO_OVERLAP(&tileArea, &csa->csa_bounds)) continue;

	/* See if we've already been here before, and mark the tile as already
	 * visited.
	 */

	callClient = TRUE;
	if (csa->csa_clear)
	{
	    if (tile->ti_client == csa->csa_clientDefault) continue;
	    tile->ti_client = csa->csa_clientDefault;
	}
	else
	{
	    if (tile->ti_client == (ClientData) 1) continue;

	    /* Allow a process to mark tiles for skipping the client function */
	    if (tile->ti_client != csa->csa_clientDefault)
		callClient = FALSE;
	    tile->ti_client = (ClientData) 1;
	}

	/* Call the client function, if there is one. */

	if (callClient && (csa->csa_clientFunc != NULL))
	{
	    if ((*csa->csa_clientFunc)(tile, dinfo, pNum, csa->csa_clientData) != 0)
	    {
		result = 1;
		continue;
	    }
	}

	/* Now search around each of the four sides of this tile for
	 * connected tiles.  For each one found, call ourselves
	 * recursively.
	 */

	if (IsSplit(tile))
	{
	    if (dinfo & TT_SIDE)
		loctype = SplitRightType(tile);
	    else
		loctype = SplitLeftType(tile);
	}
	else
	    loctype = TiGetTypeExact(tile);
	connectMask = &csa->csa_connect[loctype];

	/* Left side: */

	if (IsSplit(tile) && (dinfo & TT_SIDE)) goto bottomside;

	for (t2 = BL(tile); BOTTOM(t2) < tileArea.r_ytop; t2 = RT(t2))
	{
	    if (IsSplit(t2))
	    {
		checktype = SplitRightType(t2);
	    }
	    else
		checktype = TiGetTypeExact(t2);
	    if (TTMaskHasType(connectMask, checktype))
	    {
		if (csa->csa_clear)
		{
		    if (t2->ti_client == csa->csa_clientDefault) continue;
		}
		else if (t2->ti_client == (ClientData) 1) continue;
		STACKPUSH(INT2CD(t2), dbConnectStack);
		if (IsSplit(t2))
		    STACKPUSH(INT2CD((TileType)TT_SIDE), dbConnectStack);
		else
		    STACKPUSH(INT2CD(0), dbConnectStack);
		STACKPUSH(INT2CD(pNum), dbConnectStack);
	    }
	}

	/* Bottom side: */

bottomside:
	if (IsSplit(tile) && ((!((dinfo & TT_SIDE) ? 1 : 0)) ^ SplitDirection(tile)))
	    goto rightside;

	for (t2 = LB(tile); LEFT(t2) < tileArea.r_xtop; t2 = TR(t2))
	{
	    if (IsSplit(t2))
	    {
		checktype = SplitTopType(t2);
	    }
	    else
		checktype = TiGetTypeExact(t2);
	    if (TTMaskHasType(connectMask, checktype))
	    {
		if (csa->csa_clear)
		{
		    if (t2->ti_client == csa->csa_clientDefault) continue;
		}
		else if (t2->ti_client == (ClientData) 1) continue;
		STACKPUSH(INT2CD(t2), dbConnectStack);
		if (IsSplit(t2))
		{
		    if (SplitDirection(t2))
			STACKPUSH(INT2CD((TileType)TT_SIDE), dbConnectStack);
		    else
			/* bit clear */
			STACKPUSH(INT2CD(0), dbConnectStack);
		}
		else
		    STACKPUSH(INT2CD(0), dbConnectStack);
		STACKPUSH(INT2CD(pNum), dbConnectStack);
	    }
	}

	/* Right side: */

rightside:
	if (IsSplit(tile) && !(dinfo & TT_SIDE)) goto topside;

	for (t2 = TR(tile); ; t2 = LB(t2))
	{
	    if (IsSplit(t2))
	    {
		checktype = SplitLeftType(t2);
	    }
	    else
		checktype = TiGetTypeExact(t2);
	    if (TTMaskHasType(connectMask, checktype))
	    {
		if (csa->csa_clear)
		{
		    if (t2->ti_client == csa->csa_clientDefault) goto nextRight;
		}
		else if (t2->ti_client == (ClientData) 1) goto nextRight;
		STACKPUSH(INT2CD(t2), dbConnectStack);
		STACKPUSH(INT2CD(0), dbConnectStack);
		STACKPUSH(INT2CD(pNum), dbConnectStack);
	    }
	    nextRight: if (BOTTOM(t2) <= tileArea.r_ybot) break;
	}

	/* Top side: */
topside:

	if (IsSplit(tile) && (((dinfo & TT_SIDE) ? 1 : 0) ^ SplitDirection(tile)))
	    goto donesides;

	for (t2 = RT(tile); ; t2 = BL(t2))
	{
	    if (IsSplit(t2))
	    {
		checktype = SplitBottomType(t2);
	    }
	    else
		checktype = TiGetTypeExact(t2);
	    if (TTMaskHasType(connectMask, checktype))
	    {
		if (csa->csa_clear)
		{
		    if (t2->ti_client == csa->csa_clientDefault) goto nextTop;
		}
		else if (t2->ti_client == (ClientData) 1) goto nextTop;
		STACKPUSH(INT2CD(t2), dbConnectStack);
		if (IsSplit(t2))
		{
		    if (SplitDirection(t2))
			/* bit clear */
			STACKPUSH(INT2CD(0), dbConnectStack);
		    else
			/* bit set */
			STACKPUSH(INT2CD((TileType)TT_SIDE), dbConnectStack);
		}
		else
		    STACKPUSH(INT2CD(0), dbConnectStack);
		STACKPUSH(INT2CD(pNum), dbConnectStack);
	    }
	    nextTop: if (LEFT(t2) <= tileArea.r_xbot) break;
	}

donesides:
	if (pNum < 0) continue;		/* Used for single-plane search */

	/* Lastly, check to see if this tile connects to anything on
	 * other planes.  If so, search those planes.
	 */

	planes = DBConnPlanes[loctype];
	planes &= ~(PlaneNumToMaskBit(pNum));
	if (planes != 0)
	{
	    Rect newArea;
	    GEO_EXPAND(&tileArea, 1, &newArea);
	    TileAndDinfo tad;

	    for (i = PL_TECHDEPBASE; i < DBNumPlanes; i++)
	    {
		if (!PlaneMaskHasPlane(planes, i)) continue;
		if (IsSplit(tile))
		{
		    if (DBSrPaintNMArea((Tile *) NULL, csa->csa_def->cd_planes[i],
				TiGetTypeExact(tile) | dinfo, &newArea, connectMask,
				dbcFindTileFunc, (ClientData)&tad) != 0)
		    {
			STACKPUSH(PTR2CD(tad.tad_tile), dbConnectStack);
			STACKPUSH(INT2CD(tad.tad_dinfo), dbConnectStack);
			STACKPUSH(INT2CD(i), dbConnectStack);
		    }
		}
		else if (DBSrPaintArea((Tile *) NULL, csa->csa_def->cd_planes[i],
			&newArea, connectMask, dbcFindTileFunc,
			(ClientData)&tad) != 0)
		{
		    STACKPUSH(PTR2CD(tad.tad_tile), dbConnectStack);
		    STACKPUSH(INT2CD(tad.tad_dinfo), dbConnectStack);
		    STACKPUSH(INT2CD(i), dbConnectStack);
		}
	    }
	}
    }
    return result;
}


/*
 * ----------------------------------------------------------------------------
 *
 * dbcUnconnectFunc --
 *
 * 	This search function is invoked by DBSrPaintArea from
 *	DBTreeCopyConnect, whenever a tile is found in the result
 *	plane that is NOT connected to the current area.  It
 *	returns 1 so that DBTreeCopyConnect will know it has
 *	to do a hierarchical check for the current area.
 *
 * Results:
 *	If the current tile OVERLAPS the search area, 1 is
 *	returned.  Otherwise 0 is returned.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

/** @typedef cb_database_srpaintnmarea_t */
/** @typedef cb_database_srpaintarea_t */
int
dbcUnconnectFunc(tile, dinfo, clientData)
    Tile *tile;				/* Current tile	*/
    TileType dinfo;			/* Split tile information, unused */
    ClientData clientData;		/* Unused. */

{
    return 1;
}


/* Key for the csa2_visited hash table of source tiles already	*/
/* processed by dbcConnectFunc().  This must be zeroed before	*/
/* filling, as any structure padding is part of the key.	*/

typedef struct
{
    Tile	*vk_tile;	/* Source tile */
    Rect	 vk_area;	/* Tile area in destination coordinates */
    int		 vk_side;	/* Side of a split tile, or zero */
} dbcVisitKey;

#define DBC_VISITKEY_WORDS (sizeof(dbcVisitKey) / sizeof(unsigned))

/*
 * ----------------------------------------------------------------------------
 *
 * DBConnectVisitInit --
 *
 *	Set up the table of source tiles already processed by a
 *	connectivity search (see DBConnectVisited()).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initializes csa2->csa2_visited, which must be freed with
 *	DBConnectVisitFree() when the search is done.
 *
 * ----------------------------------------------------------------------------
 */

void
DBConnectVisitInit(
    struct conSrArg2 *csa2)
{
    HashInit(&csa2->csa2_visited, 1024, DBC_VISITKEY_WORDS);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBConnectVisitFree --
 *
 *	Free the table set up by DBConnectVisitInit().
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees memory.
 *
 * ----------------------------------------------------------------------------
 */

void
DBConnectVisitFree(
    struct conSrArg2 *csa2)
{
    HashKill(&csa2->csa2_visited);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBConnectVisited --
 *
 *	Check whether a source tile has already been processed by a
 *	connectivity search, and mark it as processed if not.
 *
 *	In a connectivity search, each tile is found again from the search
 *	area of every tile that it touches.  The check of the destination
 *	cell for a tile covers the whole area of the tile, so repeating it
 *	for a large tile with many neighbors (e.g., a power rail with many
 *	contacts) makes the search time quadratic.  The transformed tile
 *	area distinguishes the different instances of the same tile in the
 *	hierarchy.
 *
 * Results:
 *	TRUE if the tile was already processed, FALSE if not.
 *
 * Side effects:
 *	Adds an entry for the tile to csa2->csa2_visited.
 *
 * ----------------------------------------------------------------------------
 */

bool
DBConnectVisited(
    struct conSrArg2 *csa2,
    Tile *tile,			/* Source tile */
    TileType dinfo,		/* Split tile information */
    const Rect *area)		/* Tile area, transformed to the coordinates
				 * of the destination, before clipping.
				 */
{
    dbcVisitKey vkey;
    HashEntry *he;

    memset(&vkey, 0, sizeof(vkey));
    vkey.vk_tile = tile;
    vkey.vk_area = *area;
    vkey.vk_side = (IsSplit(tile)) ? (dinfo & TT_SIDE) : 0;
    he = HashFind(&csa2->csa2_visited, (char *)&vkey);
    if (HashGetValue(he) != NULL) return TRUE;
    HashSetValue(he, (ClientData)1);
    return FALSE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbcLabelRecord --
 *
 *	Record a label of the given area, type, and text in the
 *	csa2_labels hash table.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds an entry to the hash table, or adds the type to the mask of
 *	an existing entry.
 *
 * ----------------------------------------------------------------------------
 */

void
dbcLabelRecord(csa2, r, type, text)
    struct conSrArg2 *csa2;
    const Rect *r;
    TileType type;
    const char *text;
{
    char buf[DB_LABKEY_SIZE], *key;
    HashEntry *he;
    TileTypeBitMask *tmask;

    key = DBLabelHashKey(buf, DB_LABKEY_SIZE, r, text);
    he = HashFind(&csa2->csa2_labels, key);
    tmask = (TileTypeBitMask *)HashGetValue(he);
    if (tmask == NULL)
    {
	tmask = (TileTypeBitMask *)mallocMagic(sizeof(TileTypeBitMask));
	TTMaskZero(tmask);
	HashSetValue(he, (ClientData)tmask);
    }
    TTMaskSetType(tmask, type);
    if (key != buf) freeMagic(key);
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbcLabelLookup --
 *
 *	Find the mask of types of labels in the destination which have
 *	the given area and text.
 *
 * Results:
 *	Pointer to the type mask, or NULL if there is no such label.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

TileTypeBitMask *
dbcLabelLookup(csa2, r, text)
    struct conSrArg2 *csa2;
    const Rect *r;
    const char *text;
{
    char buf[DB_LABKEY_SIZE], *key;
    HashEntry *he;

    key = DBLabelHashKey(buf, DB_LABKEY_SIZE, r, text);
    he = HashLookOnly(&csa2->csa2_labels, key);
    if (key != buf) freeMagic(key);
    if (he == NULL) return (TileTypeBitMask *)NULL;
    return (TileTypeBitMask *)HashGetValue(he);
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbcLabelInit --
 *
 *	Set up the csa2_labels hash table from the labels already in
 *	the destination cell.  DBTreeCopyConnect() does not clear the
 *	destination, so it may already have labels.  This is done on
 *	demand, when the first connected label is found, so that
 *	searches that find no labels do not pay for it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initializes and fills csa2->csa2_labels.
 *
 * ----------------------------------------------------------------------------
 */

void
dbcLabelInit(csa2)
    struct conSrArg2 *csa2;
{
    Label *lab;

    HashInit(&csa2->csa2_labels, 256, HT_STRINGKEYS);
    for (lab = csa2->csa2_use->cu_def->cd_labels; lab; lab = lab->lab_next)
	dbcLabelRecord(csa2, &lab->lab_rect, lab->lab_type, lab->lab_text);
    csa2->csa2_labinit = TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * Connections by name ("virtual" connections)
 *
 *	Besides drawn material, a net may be connected through labels of
 *	the same name in one cell.  Which labels count depends on the
 *	mode (DB_CONN_* in database.h):  none, ports in the top cell,
 *	ports in any cell, or all labels in any cell.  For "select net"
 *	and "getnode" the mode follows the "extract do unique" setting,
 *	so that these commands agree with the extracted netlist.
 *
 *	A connection by name is used only where the drawn connections do
 *	not already reach the other label.  To know that, it is not made
 *	when the label is found.  Instead, each (instance, cell, name)
 *	found is recorded, and when the search of drawn material has run
 *	out, DBConnectVirtualFlush() checks every label of that name in
 *	that instance.  Any whose material is not yet in the result is
 *	connected by name only:  its area is added to the search, and,
 *	with DB_CONN_REPORT, it is reported (once per cell and name, by
 *	DBConnectVirtualFree()).
 *
 * ----------------------------------------------------------------------------
 */

/* One (instance, cell, name) found by the search */
typedef struct
{
    CellDef	*dvr_def;	/* Cell containing the label */
    Transform	 dvr_trans;	/* Transform from the cell to the root */
    char	*dvr_name;	/* Label text (owned by the label) */
} dbcVirtualRec;

/* Labels of one name in one cell */
typedef struct
{
    int		 dvl_count;	/* Number of labels */
    Label	*dvl_labels[1];	/* Labels (allocated to size) */
} dbcVirtualList;

/* Per-cell information */
typedef struct
{
    bool	 dvd_abstract;	/* TRUE if any labels connect (abstract view) */
    HashTable	*dvd_names;	/* Name -> dbcVirtualList *, or NULL if
				 * not yet made
				 */
} dbcVirtualCell;

/* Labels connected by name only, for the report */
typedef struct
{
    CellDef	*dvc_def;	/* Cell containing the labels */
    char	*dvc_name;	/* Label text */
    int		 dvc_count;	/* Number of areas connected by name */
    Rect	 dvc_first;	/* Root area of the first one */
} dbcVirtualCount;

struct dbcVirtual
{
    int		 dv_mode;	/* DB_CONN_* mode and flags */
    HashTable	 dv_found;	/* (instance, cell, name) found, string keys */
    dbcVirtualRec **dv_recs;	/* Records in the order found */
    int		 dv_nrecs;	/* Number of records */
    int		 dv_size;	/* Allocated size of dv_recs */
    int		 dv_next;	/* First record not yet checked */
    HashTable	 dv_cells;	/* Cell -> dbcVirtualCell * */
    HashTable	 dv_report;	/* "cell name" -> dbcVirtualCount * */
};

/*
 * ----------------------------------------------------------------------------
 *
 * dbcVirtualLabel --
 *
 *	Decide whether a label can connect a net by name in the current
 *	mode, or in an abstract view with DB_CONN_ABSTRACT.
 *
 * Results:
 *	TRUE if the label counts, FALSE if not.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

static bool
dbcVirtualLabel(
    struct dbcVirtual *dv,
    Label *lab,
    bool isTop,		/* TRUE if the label is in the top cell */
    bool isAbstract)	/* TRUE if the label is in an abstract view */
{
    char *text = lab->lab_text;
    char last;
    int mode;

    /* Labels on space, and empty labels, are not connected to anything */
    if (lab->lab_type == TT_SPACE) return FALSE;
    if (*text == '\0') return FALSE;

    mode = dv->dv_mode & DB_CONN_MODEMASK;
    if (isAbstract && (dv->dv_mode & DB_CONN_ABSTRACT)) mode = DB_CONN_NAMES;

    switch (mode)
    {
	case DB_CONN_TOPPORTS:
	    if (!isTop) return FALSE;
	    /* Fall through */
	case DB_CONN_ALLPORTS:
	    return (lab->lab_flags & PORT_DIR_MASK) ? TRUE : FALSE;
	case DB_CONN_NAMES:
	    /* Attribute labels (see extLabType()) are not node names */
	    last = text[strlen(text) - 1];
	    return ((last == '@') || (last == '$') || (last == '^')) ?
			FALSE : TRUE;
	default:
	    return FALSE;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBConnectVirtualInit --
 *
 *	Set up connections by name for a connectivity search (see above).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets csa2->csa2_virtual, to NULL if the mode is DB_CONN_PHYSICAL
 *	without DB_CONN_ABSTRACT.  Otherwise the caller must call
 *	DBConnectVirtualFree() when done.
 *
 * ----------------------------------------------------------------------------
 */

void
DBConnectVirtualInit(
    struct conSrArg2 *csa2,
    int mode)			/* DB_CONN_* mode, optionally with
				 * DB_CONN_REPORT
				 */
{
    struct dbcVirtual *dv;

    if (((mode & DB_CONN_MODEMASK) == DB_CONN_PHYSICAL) &&
		!(mode & DB_CONN_ABSTRACT))
    {
	csa2->csa2_virtual = (struct dbcVirtual *)NULL;
	return;
    }
    dv = (struct dbcVirtual *)mallocMagic(sizeof(struct dbcVirtual));
    dv->dv_mode = mode;
    HashInit(&dv->dv_found, 64, HT_STRINGKEYS);
    dv->dv_size = 64;
    dv->dv_recs = (dbcVirtualRec **)mallocMagic(dv->dv_size *
		sizeof(dbcVirtualRec *));
    dv->dv_nrecs = 0;
    dv->dv_next = 0;
    HashInit(&dv->dv_cells, 16, HT_WORDKEYS);
    HashInit(&dv->dv_report, 16, HT_STRINGKEYS);
    csa2->csa2_virtual = dv;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBConnectVirtualFree --
 *
 *	Report the connections made by name (if DB_CONN_REPORT was given),
 *	and free everything allocated by DBConnectVirtualInit().
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May print warnings.  Sets csa2->csa2_virtual to NULL.
 *
 * ----------------------------------------------------------------------------
 */

void
DBConnectVirtualFree(
    struct conSrArg2 *csa2)
{
    struct dbcVirtual *dv = csa2->csa2_virtual;
    HashSearch hs;
    HashEntry *he;
    dbcVirtualCount *dvc;
    int i;

    if (dv == NULL) return;

    HashStartSearch(&hs);
    while ((he = HashNext(&dv->dv_report, &hs)) != NULL)
    {
	dvc = (dbcVirtualCount *)HashGetValue(he);
	TxError("Warning:  net is connected by the name \"%s\" only (to %d "
		"area%s in cell %s, first at (%d, %d))\n",
		dvc->dvc_name, dvc->dvc_count, (dvc->dvc_count == 1) ? "" : "s",
		dvc->dvc_def->cd_name, dvc->dvc_first.r_xbot,
		dvc->dvc_first.r_ybot);
    }
    HashFreeKill(&dv->dv_report);

    HashStartSearch(&hs);
    while ((he = HashNext(&dv->dv_cells, &hs)) != NULL)
    {
	dbcVirtualCell *dvd = (dbcVirtualCell *)HashGetValue(he);
	if (dvd->dvd_names != NULL)
	{
	    HashFreeKill(dvd->dvd_names);
	    freeMagic((char *)dvd->dvd_names);
	}
	freeMagic((char *)dvd);
    }
    HashKill(&dv->dv_cells);

    for (i = 0; i < dv->dv_nrecs; i++)
	freeMagic((char *)dv->dv_recs[i]);
    freeMagic((char *)dv->dv_recs);
    HashKill(&dv->dv_found);
    freeMagic((char *)dv);
    csa2->csa2_virtual = (struct dbcVirtual *)NULL;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbcVirtualCellInfo --
 *
 *	Find the information kept for one cell, making it if needed.
 *
 * Results:
 *	Pointer to the cell's information.
 *
 * Side effects:
 *	May add an entry to csa2->csa2_virtual.
 *
 * ----------------------------------------------------------------------------
 */

static dbcVirtualCell *
dbcVirtualCellInfo(
    struct dbcVirtual *dv,
    CellDef *def)
{
    HashEntry *he;
    dbcVirtualCell *dvd;
    bool found;

    he = HashFind(&dv->dv_cells, (char *)def);
    dvd = (dbcVirtualCell *)HashGetValue(he);
    if (dvd == NULL)
    {
	dvd = (dbcVirtualCell *)mallocMagic(sizeof(dbcVirtualCell));
	(void) DBPropGet(def, "LEFview", &found);
	dvd->dvd_abstract = found;
	dvd->dvd_names = (HashTable *)NULL;
	HashSetValue(he, (ClientData)dvd);
    }
    return dvd;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbcVirtualRecord --
 *
 *	Record a label found by the connectivity search, if it can connect
 *	the net by name, so that DBConnectVirtualFlush() will check the
 *	other labels of the same name in the same instance.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May add a record to csa2->csa2_virtual.
 *
 * ----------------------------------------------------------------------------
 */

static void
dbcVirtualRecord(
    SearchContext *scx,		/* Instance containing the label */
    Label *lab,			/* Label found */
    struct conSrArg2 *csa2)
{
    struct dbcVirtual *dv = csa2->csa2_virtual;
    dbcVirtualRec *dvr;
    dbcVirtualCell *dvd;
    HashEntry *he;
    const Transform *t = &scx->scx_trans;
    char key[64 + FLATTERMSIZE];
    bool isTop;

    if (dv == NULL) return;
    isTop = (scx->scx_use == csa2->csa2_topscx->scx_use) ? TRUE : FALSE;
    dvd = dbcVirtualCellInfo(dv, scx->scx_use->cu_def);
    if (!dbcVirtualLabel(dv, lab, isTop, dvd->dvd_abstract)) return;

    snprintf(key, sizeof(key), "%p %d %d %d %d %d %d %s",
		(void *)scx->scx_use->cu_def, t->t_a, t->t_b, t->t_c,
		t->t_d, t->t_e, t->t_f, lab->lab_text);
    he = HashFind(&dv->dv_found, key);
    if (HashGetValue(he) != NULL) return;
    HashSetValue(he, INT2CD(1));

    dvr = (dbcVirtualRec *)mallocMagic(sizeof(dbcVirtualRec));
    dvr->dvr_def = scx->scx_use->cu_def;
    dvr->dvr_trans = *t;
    dvr->dvr_name = lab->lab_text;
    if (dv->dv_nrecs == dv->dv_size)
    {
	dbcVirtualRec **newrecs;

	newrecs = (dbcVirtualRec **)mallocMagic(2 * dv->dv_size *
		sizeof(dbcVirtualRec *));
	memcpy(newrecs, dv->dv_recs, dv->dv_size * sizeof(dbcVirtualRec *));
	freeMagic((char *)dv->dv_recs);
	dv->dv_recs = newrecs;
	dv->dv_size *= 2;
    }
    dv->dv_recs[dv->dv_nrecs++] = dvr;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbcVirtualNames --
 *
 *	Find the labels of one name in one cell that can connect a net by
 *	name.  The labels of each cell are sorted by name the first time
 *	the cell is used, so that the label list is not searched for
 *	every name.
 *
 * Results:
 *	The list of labels, or NULL if there are none.
 *
 * Side effects:
 *	May add a table of the cell's labels to csa2->csa2_virtual.
 *
 * ----------------------------------------------------------------------------
 */

static dbcVirtualList *
dbcVirtualNames(
    struct dbcVirtual *dv,
    CellDef *def,
    char *name,
    bool isTop)			/* TRUE if def is the top cell */
{
    HashEntry *ne;
    HashTable *names;
    dbcVirtualCell *dvd;
    dbcVirtualList *dvl, *newdvl;
    Label *lab;

    dvd = dbcVirtualCellInfo(dv, def);
    names = dvd->dvd_names;
    if (names == NULL)
    {
	names = (HashTable *)mallocMagic(sizeof(HashTable));
	HashInit(names, 32, HT_STRINGKEYS);
	dvd->dvd_names = names;

	for (lab = def->cd_labels; lab != NULL; lab = lab->lab_next)
	{
	    if (!dbcVirtualLabel(dv, lab, isTop, dvd->dvd_abstract)) continue;
	    ne = HashFind(names, lab->lab_text);
	    dvl = (dbcVirtualList *)HashGetValue(ne);
	    if (dvl == NULL)
	    {
		dvl = (dbcVirtualList *)mallocMagic(sizeof(dbcVirtualList));
		dvl->dvl_count = 0;
	    }
	    else if ((dvl->dvl_count & (dvl->dvl_count - 1)) == 0)
	    {
		/* Count is a power of 2:  double the size */
		newdvl = (dbcVirtualList *)mallocMagic(sizeof(dbcVirtualList)
			+ (2 * dvl->dvl_count - 1) * sizeof(Label *));
		memcpy(newdvl, dvl, sizeof(dbcVirtualList)
			+ (dvl->dvl_count - 1) * sizeof(Label *));
		freeMagic((char *)dvl);
		dvl = newdvl;
	    }
	    dvl->dvl_labels[dvl->dvl_count++] = lab;
	    HashSetValue(ne, (ClientData)dvl);
	}
    }
    ne = HashLookOnly(names, name);
    return (ne == NULL) ? NULL : (dbcVirtualList *)HashGetValue(ne);
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbcPushArea --
 *
 *	Add an area to the list of areas still to be searched.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds an entry to csa2->csa2_list, starting a new list if it is full.
 *
 * ----------------------------------------------------------------------------
 */

static void
dbcPushArea(
    struct conSrArg2 *csa2,
    const Rect *area,
    const TileTypeBitMask *connectMask)
{
    if (++csa2->csa2_top == CSA2_LIST_SIZE)
    {
	conSrArea *newlist;

	newlist = (conSrArea *)mallocMagic(CSA2_LIST_SIZE * sizeof(conSrArea));
	StackPush((ClientData)csa2->csa2_list, csa2->csa2_stack);
	csa2->csa2_list = newlist;
	csa2->csa2_top = 0;
	csa2->csa2_lasttop = -1;
    }
    csa2->csa2_list[csa2->csa2_top].area = *area;
    csa2->csa2_list[csa2->csa2_top].connectMask = connectMask;
    csa2->csa2_list[csa2->csa2_top].dinfo = 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBConnectVirtualFlush --
 *
 *	Called when the search of drawn material has run out.  For each
 *	(instance, cell, name) recorded since the last call, check each
 *	label of that name in the cell.  If the material under the label
 *	is not in the result, then it is connected to the net by name
 *	only:  add its area to the search, and count it for the report.
 *
 * Results:
 *	TRUE if any area was added, so the search must continue;
 *	FALSE if the search is complete.
 *
 * Side effects:
 *	May add entries to csa2->csa2_list.
 *
 * ----------------------------------------------------------------------------
 */

bool
DBConnectVirtualFlush(
    struct conSrArg2 *csa2)
{
    struct dbcVirtual *dv = csa2->csa2_virtual;
    CellDef *destDef = csa2->csa2_use->cu_def;
    CellDef *topDef;
    dbcVirtualRec *dvr;
    dbcVirtualList *dvl;
    dbcVirtualCount *dvc;
    const TileTypeBitMask *connectMask;
    HashEntry *he;
    Label *lab;
    Rect area, check;
    char key[FLATTERMSIZE + 256];
    bool pushed = FALSE;
    int i;

    if (dv == NULL) return FALSE;
    topDef = csa2->csa2_topscx->scx_use->cu_def;

    while (dv->dv_next < dv->dv_nrecs)
    {
	dvr = dv->dv_recs[dv->dv_next++];
	dvl = dbcVirtualNames(dv, dvr->dvr_def, dvr->dvr_name,
		(dvr->dvr_def == topDef) ? TRUE : FALSE);
	if ((dvl == NULL) || (dvl->dvl_count < 2)) continue;

	for (i = 0; i < dvl->dvl_count; i++)
	{
	    lab = dvl->dvl_labels[i];
	    GeoTransRect(&dvr->dvr_trans, &lab->lab_rect, &area);
	    connectMask = &csa2->csa2_connect[lab->lab_type];

	    /* Is there connected material under the label in the result? */
	    check = area;
	    if ((check.r_xbot == check.r_xtop) || (check.r_ybot == check.r_ytop))
		GEO_EXPAND(&check, 1, &check);
	    if (DBSrPaintArea((Tile *)NULL,
			destDef->cd_planes[DBPlane(lab->lab_type)], &check,
			connectMask, dbcUnconnectFunc, (ClientData)NULL) == 1)
		continue;

	    /* No:  it is connected by name only */
	    if (dv->dv_mode & DB_CONN_REPORT)
	    {
		snprintf(key, sizeof(key), "%s %s", dvr->dvr_def->cd_name,
			lab->lab_text);
		he = HashFind(&dv->dv_report, key);
		dvc = (dbcVirtualCount *)HashGetValue(he);
		if (dvc == NULL)
		{
		    dvc = (dbcVirtualCount *)mallocMagic(sizeof(dbcVirtualCount));
		    dvc->dvc_def = dvr->dvr_def;
		    dvc->dvc_name = lab->lab_text;
		    dvc->dvc_count = 0;
		    dvc->dvc_first = area;
		    HashSetValue(he, (ClientData)dvc);
		}
		dvc->dvc_count++;
	    }
	    GEO_EXPAND(&area, 1, &area);
	    dbcPushArea(csa2, &area, connectMask);
	    pushed = TRUE;
	}
    }
    return pushed;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbcVirtualLabelFunc --
 *
 *	Called by DBTreeSrLabels() from DBConnectVirtualLabels() for each
 *	label attached to the material being searched.
 *
 * Results:
 *	Always 0, to keep the search going.
 *
 * Side effects:
 *	See dbcVirtualRecord().
 *
 * ----------------------------------------------------------------------------
 */

static int
dbcVirtualLabelFunc(
    SearchContext *scx,
    Label *lab,
    TerminalPath *tpath,	/* (unused) */
    struct conSrArg2 *csa2)
{
    dbcVirtualRecord(scx, lab, csa2);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbcLabelSearchType --
 *
 *	Label search flags for labels attached to material of a search
 *	area, which may be one side of a split tile.
 *
 * Results:
 *	Flags for DBTreeSrLabels().
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

static unsigned char
dbcLabelSearchType(
    TileType dinfo)		/* Split tile information of the area */
{
    unsigned char searchtype = TF_LABEL_ATTACH;

    /* If the tile is split, then labels attached to the opposite	*/
    /* point of the triangle are NOT connected.				*/

    if (dinfo & TT_DIAGONAL)
    {
	if (dinfo & TT_SIDE)
	{
	    if (dinfo & TT_DIRECTION)
		searchtype |= TF_LABEL_ATTACH_NOT_SW;
	    else
		searchtype |= TF_LABEL_ATTACH_NOT_NW;
	}
	else
	{
	    if (dinfo & TT_DIRECTION)
		searchtype |= TF_LABEL_ATTACH_NOT_NE;
	    else
		searchtype |= TF_LABEL_ATTACH_NOT_SE;
	}
    }
    return searchtype;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBConnectVirtualLabels --
 *
 *	Record the labels attached to the material in a search area that
 *	can connect the net by name, for a connectivity search that does
 *	not otherwise search for labels (see DBConnectVirtualFlush()).
 *
 * Results:
 *	The result of DBTreeSrLabels():  nonzero if interrupted.
 *
 * Side effects:
 *	May add records to csa2->csa2_virtual.
 *
 * ----------------------------------------------------------------------------
 */

int
DBConnectVirtualLabels(
    SearchContext *scx,		/* Root use and search area */
    const TileTypeBitMask *mask, /* Types of the material searched */
    TileType dinfo,		/* Split tile information of the area */
    int xMask,			/* Expansion mask */
    struct conSrArg2 *csa2)
{
    char pathstring[FLATTERMSIZE];
    TerminalPath tpath;

    if (csa2->csa2_virtual == NULL) return 0;
    tpath.tp_first = tpath.tp_next = pathstring;
    tpath.tp_last = pathstring + FLATTERMSIZE;
    pathstring[0] = '\0';
    return DBTreeSrLabels(scx, mask, xMask, &tpath, dbcLabelSearchType(dinfo),
		dbcVirtualLabelFunc, (ClientData)csa2);
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbcConnectLabelFunc --
 *
 * 	This function is invoked by DBTreeSrTiles from DBTreeCopyConnect;
 *	when a label is found which is connected to paint belonging to the
 *	network, it adds it to the destination definition.
 *
 *	The label is also recorded for connections by name to other
 *	labels of the same name (see DBConnectVirtualFlush()).
 *
 * Results:
 *	Always 0.
 *
 * Side effects:
 *	Adds a label to the destination definition "def".
 *
 * ----------------------------------------------------------------------------
 */

int
dbcConnectLabelFunc(scx, lab, tpath, csa2)
    SearchContext *scx;
    Label *lab;
    TerminalPath *tpath;
    struct conSrArg2 *csa2;
{
    CellDef *def = csa2->csa2_use->cu_def;
    Rect r;
    Point offset;
    int pos, rotate;
    char newlabtext[FLATTERMSIZE];
    char *newlabptr;
    TileTypeBitMask *tmask;
    int dbcConnectFunc();		/* Forward declaration */

    dbcVirtualRecord(scx, lab, csa2);

    GeoTransRect(&scx->scx_trans, &lab->lab_rect, &r);
    pos = GeoTransPos(&scx->scx_trans, lab->lab_just);
    GeoTransPointDelta(&scx->scx_trans, &lab->lab_offset, &offset);
    rotate = GeoTransAngle(&scx->scx_trans, lab->lab_rotate);

    /* Do not add any labels to the destination unless they are on  */
    /* the top level (Note:  Could alter label to be placed with    */
    /* tpath).							    */

    if (scx->scx_use != csa2->csa2_topscx->scx_use)
    {
	if (tpath)
	{
	    int newllen = tpath->tp_next - tpath->tp_first;
	    newlabtext[0] = '\0';
	    if (tpath->tp_first == NULL)
		newlabptr = lab->lab_text;
	    else
	    {
		if (newllen > 0)
		    strncpy(newlabtext, tpath->tp_first, newllen);
		sprintf(newlabtext + newllen, "%s", lab->lab_text);
		newlabptr = newlabtext;
	    }
	}
	else return 0;
    }
    else
	newlabptr = lab->lab_text;

    /* Do not repeat a label copy;  check that the label doesn't	*/
    /* already exist in the destination def first.  The labels in	*/
    /* the destination are tracked in a hash table, as a linear	*/
    /* search of the label list for every label found makes the	*/
    /* search time quadratic in the number of labels.			*/

    if (!csa2->csa2_labinit) dbcLabelInit(csa2);

    tmask = dbcLabelLookup(csa2, &r, lab->lab_text);
    if (tmask && TTMaskHasType(tmask, lab->lab_type))
	return 0;

    if (newlabptr != lab->lab_text)
    {
	TileTypeBitMask *nmask = dbcLabelLookup(csa2, &r, newlabptr);
	if (nmask && TTMaskHasType(nmask, lab->lab_type))
	    return 0;
    }

    /* Replace any label with the same text and area on any other type */
    if (tmask && !TTMaskIsZero(tmask))
    {
	DBEraseLabelsByContent(def, &r, -1, lab->lab_text);
	TTMaskZero(tmask);
    }
    DBPutFontLabel(def, &r, lab->lab_font, lab->lab_size, rotate, &offset,
		pos, newlabptr, lab->lab_type, lab->lab_flags, lab->lab_port);
    dbcLabelRecord(csa2, &r, lab->lab_type, newlabptr);

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbcConnectFunc --
 *
 * 	This procedure is invoked by DBTreeSrTiles from DBTreeCopyConnect,
 *	whenever a tile is found that is connected to the current area
 *	being processed.  If the tile overlaps the search area in a non-
 *	trivial way (i.e. more than a 1x1 square of overlap at a corner),
 *	then its area is checked against the equivalent destination area.
 *	If the destination area contains unconnected portions, then the
 *	area of the tile is painted into the destination, and an area 1
 *	unit larger than the tile is recursively checked for connecting
 *	tiles.  The "non-trivial" overlap check is needed to prevent
 *	catecorner tiles from being considered as connected.
 *
 * Results:
 *	Returns 0 normally to keep the search from aborting;  returns 1
 *	if allocation of list failed due to exceeding integer bounds.
 *
 * Side effects:
 *	Adds paint to the destination definition.
 *
 * ----------------------------------------------------------------------------
 */

int
dbcConnectFunc(tile, dinfo, cx)
    Tile *tile;			/* Tile found. */
    TileType dinfo;		/* Split tile information */
    TreeContext *cx;		/* Describes context of search.  The client
				 * data is a pointer to a conSrArg2 record
				 * containing various required information.
				 */
{
    struct conSrArg2 *csa2;
    Rect tileArea, newarea;
    const TileTypeBitMask *connectMask;
    TileTypeBitMask notConnectMask;
    Rect *srArea;
    SearchContext *scx = cx->tc_scx;
    SearchContext scx2;
    TileType loctype = TiGetTypeExact(tile) | dinfo;
    TileType newdinfo = 0;
    int retval, i, pNum = cx->tc_plane;
    CellDef *def;

    TiToRect(tile, &tileArea);
    srArea = &scx->scx_area;

    if (((tileArea.r_xbot >= srArea->r_xtop-1) ||
	(tileArea.r_xtop <= srArea->r_xbot+1)) &&
	((tileArea.r_ybot >= srArea->r_ytop-1) ||
	(tileArea.r_ytop <= srArea->r_ybot+1)))
    {
	/* If the search area is only one unit wide or tall, then it's
	 * OK to have only a small overlap.  This happens only when
	 * looking for an initial search tile.
	 */

	if (((srArea->r_xtop-1) != srArea->r_xbot)
	    && ((srArea->r_ytop-1) != srArea->r_ybot)) return 0;
    }
    GeoTransRect(&scx->scx_trans, &tileArea, &newarea);

    /* Process each tile (instance) only the first time it is found */

    csa2 = (struct conSrArg2 *)cx->tc_filter->tf_arg;
    if (DBConnectVisited(csa2, tile, dinfo, &newarea)) return 0;

    /* Clip the current area down to something that overlaps the
     * area of interest.
     */

    GeoClip(&newarea, csa2->csa2_bounds);
    if (GEO_RECTNULL(&newarea)) return 0;

    if (IsSplit(tile))
    {
	newdinfo = DBTransformDiagonal(loctype, &scx->scx_trans);
	loctype = ((dinfo & TT_SIDE)) ? SplitRightType(tile) : SplitLeftType(tile);
    }

    /* See if the destination cell contains stuff over the whole
     * current area (on its home plane) that is connected to it.
     * If so, then there's no need to process the current area,
     * since any processing that is needed was already done before.
     */

    connectMask = &csa2->csa2_connect[loctype];

    /* In the case of contact bits, the types underneath
     * must be constituents of the contact before we punt
     */

    if (DBIsContact(loctype))
    {
	/* Different contact types may share residues (6/18/04) */
	/* Use TTMaskIntersect(), not TTMaskEqual()---types	*/
	/* which otherwise stack may be in separate cells	*/
	/* (12/1/05)						*/

	/* The mask of contact types must include all stacked contacts */

	TTMaskZero(&notConnectMask);
	TTMaskSetMask(&notConnectMask, &DBNotConnectTbl[loctype]);
    }
    else
    {
	TTMaskCom2(&notConnectMask, connectMask);
    }

    /* Only check those tiles in the destination (select)	*/
    /* which have not already been painted.			*/

    def = csa2->csa2_use->cu_def;
    retval = 1;
    if (DBSrPaintNMArea((Tile *) NULL, def->cd_planes[pNum],
		newdinfo, &newarea, &notConnectMask, dbcUnconnectFunc,
		(ClientData) NULL) == 0)
	retval = 0;

    /* Paint this tile into the destination cell.  This
     * marks its area has having been processed.  Then recycle
     * the storage for the current list element.
     */

    DBNMPaintPlane(def->cd_planes[pNum], newdinfo,
		&newarea, DBStdPaintTbl(loctype, pNum),
		(PaintUndoInfo *) NULL);

    if (retval == 0) return 0;

    /* Since the whole area of this tile hasn't been recorded,
     * we must process its area to find any other tiles that
     * connect to it.  Add each of them to the list of things
     * to process.  We have to expand the search area by 1 unit
     * on all sides because DBTreeSrTiles only returns things
     * that overlap the search area, and we want things that
     * even just touch.
     */

    /* Only extend those sides bordering the diagonal tile */

    if (newdinfo & TT_DIAGONAL)
    {
	if (newdinfo & TT_SIDE)			/* right */
	    newarea.r_xtop += 1;
	else					/* left */
	    newarea.r_xbot -= 1;
	if (((newdinfo & TT_SIDE) >> 1)
		== (newdinfo & TT_DIRECTION))	/* top */
	    newarea.r_ytop += 1;
	else					/* bottom */
	    newarea.r_ybot -= 1;
    }
    else
    {
	newarea.r_ybot -= 1;
	newarea.r_ytop += 1;
	newarea.r_xbot -= 1;
	newarea.r_xtop += 1;
    }

    /* Check if any of the last 5 entries has the same type and	*/
    /* area.  If so, don't duplicate the existing entry.	*/
    /* (NOTE:  Connect masks are all from the same table, so	*/
    /* they can be compared by address, no need for TTMaskEqual)*/

    for (i = csa2->csa2_lasttop; (i >= 0) && (i > csa2->csa2_lasttop - 5); i--)
	if (connectMask == csa2->csa2_list[i].connectMask)
	    if (GEO_SURROUND(&csa2->csa2_list[i].area, &newarea))
		return 0;

    /* Register the area and connection mask as needing to be processed */

    if (++csa2->csa2_top == CSA2_LIST_SIZE)
    {
	/* Reached list size limit---need to push the list and	*/
	/* start a new one.  NOTE:  Setting lasttop to -1 means	*/
	/* that some entries may be duplicated between the	*/
	/* stacks, which is a small inefficiency.  In theory,	*/
	/* lasttop could be left as is, then if lasttop > top	*/
	/* when searching the last 5 entries, pop the stack, do	*/
	/* the search, and then push the stack again.  But it's	*/
	/* a lot easier just to be slightly inefficient.	*/

	conSrArea *newlist;

	newlist = (conSrArea *)mallocMagic(CSA2_LIST_SIZE * sizeof(conSrArea));
	StackPush((ClientData)csa2->csa2_list, csa2->csa2_stack);
	csa2->csa2_list = newlist;
	csa2->csa2_top = 0;
	csa2->csa2_lasttop = -1;
    }

    csa2->csa2_list[csa2->csa2_top].area = newarea;
    csa2->csa2_list[csa2->csa2_top].connectMask = connectMask;
    csa2->csa2_list[csa2->csa2_top].dinfo = newdinfo;

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBTreeCopyConnect --
 *
 * 	This procedure copies connected information from a given cell
 *	hierarchy to a given (flat) cell.  Starting from the tile underneath
 *	the given area, this procedure finds all paint in all cells
 *	that is connected to that information.  All such paint is
 *	copied into the result cell.  If there are several electrically
 *	distinct nets underneath the given area, one of them is picked
 *	at more-or-less random.
 *
 *	Modified so the result cell is NOT first cleared of all paint.  This
 *	allows multiple calls, to highlight incomplete routing nets.
 *
 *	Ports of the same name in a cell are connected (DB_CONN_ALLPORTS),
 *	unless doLabels is SEL_NO_LABELS.  See DBTreeCopyConnectMode().
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The contents of the result cell are modified.
 *
 * ----------------------------------------------------------------------------
 */

void
DBTreeCopyConnect(scx, mask, xMask, connect, area, doLabels, destUse)
    SearchContext *scx;
    TileTypeBitMask *mask;
    int xMask;
    TileTypeBitMask *connect;
    Rect *area;
    unsigned char doLabels;
    CellUse *destUse;
{
    DBTreeCopyConnectMode(scx, mask, xMask, connect, area, doLabels, destUse,
		(doLabels == SEL_NO_LABELS) ? DB_CONN_PHYSICAL : DB_CONN_ALLPORTS);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBTreeCopyConnectMode --
 *
 *	Same as DBTreeCopyConnect(), but connMode says which labels of the
 *	same name in a cell connect the net (DB_CONN_* in database.h, with
 *	DB_CONN_REPORT to report each name that connects material that is
 *	not otherwise connected).  Connections by name are followed whether
 *	or not labels are copied (doLabels).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The contents of the result cell are modified.  May print warnings.
 *
 * ----------------------------------------------------------------------------
 */

void
DBTreeCopyConnectMode(scx, mask, xMask, connect, area, doLabels, destUse,
		connMode)
    SearchContext *scx;			/* Describes starting area.  The
					 * scx_use field gives the root of
					 * the hierarchy to search, and the
					 * scx_area field gives the starting
					 * area.  An initial tile must overlap
					 * this area.  The transform is from
					 * coords of scx_use to destUse.
					 */
    TileTypeBitMask *mask;		/* Tile types to start from in area. */
    int xMask;				/* Information must be expanded in all
					 * of the windows indicated by this
					 * mask.  Use 0 to consider all info
					 * regardless of expansion.
					 */
    TileTypeBitMask *connect;		/* Points to table that defines what
					 * each tile type is considered to
					 * connect to.  Use DBConnectTbl as
					 * a default.
					 */
    Rect *area;				/* The resulting information is
					 * clipped to this area.  Pass
					 * TiPlaneRect to get everything.
					 */
    unsigned char doLabels;		/* If SEL_DO_LABELS, copy connected labels
					 * and paint.  If SEL_NO_LABELS, copy only
					 * connected paint.  If SEL_SIMPLE_LABELS,
					 * copy only root of labels in subcircuits.
					 */
    CellUse *destUse;			/* Result use in which to place
					 * anything connected to material of
					 * type mask in area of rootUse.
					 */
    int connMode;			/* Connections by name:  DB_CONN_* */
{
    struct conSrArg2 csa2;
    const TileTypeBitMask *newmask;
    TileType newtype;
    unsigned char searchtype;
    int result;
    bool interrupted = FALSE;

    csa2.csa2_use = destUse;
    csa2.csa2_bounds = area;
    csa2.csa2_connect = connect;
    csa2.csa2_topscx = scx;

    /* Instead of using a linked list, we keep down the number of	*/
    /* malloc calls by maintaining a small list and expanding it only	*/
    /* when necessary.							*/

    csa2.csa2_list = (conSrArea *)mallocMagic(CSA2_LIST_SIZE * sizeof(conSrArea));
    csa2.csa2_top = -1;
    csa2.csa2_lasttop = -1;

    csa2.csa2_stack = StackNew(100);

    DBConnectVisitInit(&csa2);
    csa2.csa2_labinit = FALSE;
    DBConnectVirtualInit(&csa2, connMode);

    DBTreeSrTiles(scx, mask, xMask, dbcConnectFunc, (ClientData) &csa2);

    /* Search drawn connections until they run out, then follow	*/
    /* any connections by name and continue from there.		*/

    while (TRUE)
    {
	while (csa2.csa2_top >= 0)
	{
	    char pathstring[FLATTERMSIZE];
	    TerminalPath tpath;

	    tpath.tp_first = tpath.tp_next = pathstring;
	    tpath.tp_last = pathstring + FLATTERMSIZE;
	    pathstring[0] = '\0';

	    newmask = csa2.csa2_list[csa2.csa2_top].connectMask;
	    scx->scx_area = csa2.csa2_list[csa2.csa2_top].area;
	    newtype = csa2.csa2_list[csa2.csa2_top].dinfo;
	    if (csa2.csa2_top == 0)
	    {
		if (StackLook(csa2.csa2_stack) != (ClientData)NULL)
		{
		    freeMagic(csa2.csa2_list);
		    csa2.csa2_list = (conSrArea *)StackPop(csa2.csa2_stack);
		    csa2.csa2_top = CSA2_LIST_SIZE - 1;
		}
		else
		    csa2.csa2_top--;
	    }
	    else
		csa2.csa2_top--;

	    csa2.csa2_lasttop = csa2.csa2_top;

	    if (newtype & TT_DIAGONAL)
		DBTreeSrNMTiles(scx, newtype, newmask, xMask, dbcConnectFunc,
			    (ClientData) &csa2);
	    else
		DBTreeSrTiles(scx, newmask, xMask, dbcConnectFunc,
			    (ClientData) &csa2);

	    /* Check the source def for any labels belonging to this        */
	    /* tile area and plane, and add them to the destination.        */

	    /* (This code previously in dbcConnectFunc, but moved to avoid	*/
	    /* running the cell search from the top within another cell	*/
	    /* search, which creates deep stacks and can trigger stack	*/
	    /* overflow.)							*/

	    /* Labels found are also recorded for connections by name.	*/

	    searchtype = dbcLabelSearchType(newtype);
	    if (doLabels == SEL_SIMPLE_LABELS) tpath.tp_first = NULL;
	    if (doLabels != SEL_NO_LABELS)
		result = DBTreeSrLabels(scx, newmask, xMask, &tpath, searchtype,
			    dbcConnectLabelFunc, (ClientData) &csa2);
	    else
		result = DBConnectVirtualLabels(scx, newmask, newtype, xMask,
			    &csa2);
	    if (result != 0)
	    {
		TxError("Connection search was interrupted or hit "
			"memory limit and stopped.\n");
		interrupted = TRUE;
		break;
	    }
	}
	if (interrupted || !DBConnectVirtualFlush(&csa2)) break;
    }

    DBConnectVirtualFree(&csa2);
    freeMagic((char *)csa2.csa2_list);
    StackFree(csa2.csa2_stack);
    DBConnectVisitFree(&csa2);
    if (csa2.csa2_labinit) HashFreeKill(&csa2.csa2_labels);

    /* Recompute the bounding box of the destination and record its area
     * for redisplay.
     */

    DBReComputeBbox(destUse->cu_def);
}
