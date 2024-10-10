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
 * DBSrConnectOnePlane --
 *
 * 	Search from a starting tile to find all paint that is electrically
 *	connected to that tile in the same plane.
 *
 * Results:
 *	0 is returned if the search finished normally.  1 is returned
 *	if the search was aborted.
 *
 * Side effects:
 *	For every paint tile that is electrically connected to the initial
 *	tile, func is called.  Func should have the following form:
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
DBSrConnectOnePlane(startTile, connect, func, clientData)
    Tile *startTile;		/* Starting tile for search */
    TileTypeBitMask *connect;	/* Pointer to a table indicating what tile
				 * types connect to what other tile types.
				 * Each entry gives a mask of types that
				 * connect to tiles of a given type.
				 */
    int (*func)();		/* Function to apply at each connected tile. */
    ClientData clientData;	/* Client data for above function. */

{
    struct conSrArg csa;
    int result;
    extern int dbSrConnectFunc();	/* Forward declaration. */

    result = 0;
    csa.csa_def = (CellDef *)NULL;
    csa.csa_bounds = TiPlaneRect;

    /* Pass 1.  During this pass the client function gets called. */

    csa.csa_clientFunc = func;
    csa.csa_clientData = clientData;
    csa.csa_clientDefault = startTile->ti_client;
    csa.csa_clear = FALSE;
    csa.csa_connect = connect;
    csa.csa_pNum = -1;
    if (dbSrConnectFunc(startTile, &csa) != 0) result = 1;

    /* Pass 2.  Don't call any client function, just clear the marks.
     * Don't allow any interruptions.
     */

    SigDisableInterrupts();
    csa.csa_clientFunc = NULL;
    csa.csa_clear = TRUE;
    (void) dbSrConnectFunc(startTile, &csa);
    SigEnableInterrupts();

    return result;
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
    Tile *startTile;			/* Starting tile for search. */
    extern int dbSrConnectFunc();	/* Forward declaration. */
    extern int dbSrConnectStartFunc();

    result = 0;
    csa.csa_def = def;
    csa.csa_bounds = *bounds;

    /* Find a starting tile (if there are many tiles underneath the
     * starting area, pick any one).  The search function just saves
     * the tile address and returns.
     */

    startTile = NULL;
    for (startPlane = PL_TECHDEPBASE; startPlane < DBNumPlanes; startPlane++)
    {
    	csa.csa_pNum = startPlane;
	if (DBSrPaintArea((Tile *) NULL,
	    def->cd_planes[startPlane], startArea, mask,
	    dbSrConnectStartFunc, (ClientData) &startTile) != 0) break;
    }
    if (startTile == NULL) return 0;
    /* The following lets us call DBSrConnect recursively */
    else if (startTile->ti_client == (ClientData)1) return 0;

    /* Pass 1.  During this pass the client function gets called. */

    csa.csa_clientFunc = func;
    csa.csa_clientData = clientData;
    csa.csa_clientDefault = CLIENTDEFAULT;
    csa.csa_clear = FALSE;
    csa.csa_connect = connect;
    if (dbSrConnectFunc(startTile, &csa) != 0) result = 1;

    /* Pass 2.  Don't call any client function, just clear the marks.
     * Don't allow any interruptions.
     */

    SigDisableInterrupts();
    csa.csa_clientFunc = NULL;
    csa.csa_clear = TRUE;
    (void) dbSrConnectFunc(startTile, &csa);
    SigEnableInterrupts();

    return result;
}

int
dbSrConnectStartFunc(tile, pTile)
    Tile *tile;			/* This will be the starting tile. */
    Tile **pTile;		/* We store tile's address here. */
{
    *pTile = tile;
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
    Tile *startTile;			/* Starting tile for search. */
    extern int dbSrConnectFunc();	/* Forward declaration. */
    extern int dbSrConnectStartFunc();

    result = 0;
    csa.csa_def = def;
    csa.csa_bounds = *bounds;

    /* Find a starting tile (if there are many tiles underneath the
     * starting area, pick any one).  The search function just saves
     * the tile address and returns.
     */

    startTile = NULL;
    for (startPlane = PL_TECHDEPBASE; startPlane < DBNumPlanes; startPlane++)
    {
    	csa.csa_pNum = startPlane;
	if (DBSrPaintArea((Tile *) NULL,
	    def->cd_planes[startPlane], startArea, mask,
	    dbSrConnectStartFunc, (ClientData) &startTile) != 0) break;
    }
    if (startTile == NULL) return 0;
    /* The following lets us call DBSrConnect recursively */
    else if (startTile->ti_client == (ClientData)1) return 0;

    /* Pass 1.  During this pass the client function gets called. */

    csa.csa_clientFunc = func;
    csa.csa_clientData = clientData;
    csa.csa_clientDefault = CLIENTDEFAULT;
    csa.csa_clear = FALSE;
    csa.csa_connect = connect;
    if (dbSrConnectFunc(startTile, &csa) != 0) result = 1;

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
dbcFindTileFunc(tile, arg)
    Tile *tile;
    ClientData arg;
{
    Tile **tptr = (Tile **)arg;
    *tptr = tile;
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
dbSrConnectFunc(tile, csa)
    Tile *tile;			/* Tile that is connected. */
    struct conSrArg *csa;	/* Contains information about the search. */
{
    Tile *t2;
    Rect tileArea;
    int i, pNum;
    int result = 0;
    bool callClient;
    TileTypeBitMask *connectMask;
    TileType loctype, checktype;
    PlaneMask planes;

    if (dbConnectStack == (Stack *)NULL)
	dbConnectStack = StackNew(256);

    /* Drop the first entry on the stack */
    pNum = csa->csa_pNum;
    STACKPUSH((ClientData)tile, dbConnectStack);
    STACKPUSH((ClientData)pNum, dbConnectStack);

    while (!StackEmpty(dbConnectStack))
    {
	pNum = (int)STACKPOP(dbConnectStack);
	tile = (Tile *)STACKPOP(dbConnectStack);
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
	    if ((*csa->csa_clientFunc)(tile, pNum, csa->csa_clientData) != 0)
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
	    if (SplitSide(tile))
		loctype = SplitRightType(tile);
	    else
		loctype = SplitLeftType(tile);
	}
	else
	    loctype = TiGetTypeExact(tile);
	connectMask = &csa->csa_connect[loctype];

	/* Left side: */

	if (IsSplit(tile) && SplitSide(tile)) goto bottomside;

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
		if (IsSplit(t2))
		    TiSetBody(t2, (ClientData)(t2->ti_body | TT_SIDE)); /* bit set */
		STACKPUSH((ClientData)t2, dbConnectStack);
    		STACKPUSH((ClientData)pNum, dbConnectStack);
	    }
	}

	/* Bottom side: */

bottomside:
	if (IsSplit(tile) && (!(SplitSide(tile) ^ SplitDirection(tile))))
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
		if (IsSplit(t2))
		{
		    if (SplitDirection(t2))
			/* bit set */
			TiSetBody(t2, (ClientData)(t2->ti_body | TT_SIDE));
		    else
			/* bit clear */
			TiSetBody(t2, (ClientData)(t2->ti_body & ~TT_SIDE));
		}
		STACKPUSH((ClientData)t2, dbConnectStack);
    		STACKPUSH((ClientData)pNum, dbConnectStack);
	    }
	}

	/* Right side: */

rightside:
	if (IsSplit(tile) && !SplitSide(tile)) goto topside;

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
		if (IsSplit(t2))
		    TiSetBody(t2, (ClientData)(t2->ti_body & ~TT_SIDE)); /* bit clear */
		STACKPUSH((ClientData)t2, dbConnectStack);
    		STACKPUSH((ClientData)pNum, dbConnectStack);
	    }
	    nextRight: if (BOTTOM(t2) <= tileArea.r_ybot) break;
	}

	/* Top side: */
topside:

	if (IsSplit(tile) && (SplitSide(tile) ^ SplitDirection(tile))) goto donesides;

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
		if (IsSplit(t2))
		{
		    if (SplitDirection(t2))
			/* bit clear */
			TiSetBody(t2, (ClientData)(t2->ti_body & ~TT_SIDE));
		    else
			/* bit set */
			TiSetBody(t2, (ClientData)(t2->ti_body | TT_SIDE));
		}
		STACKPUSH((ClientData)t2, dbConnectStack);
    		STACKPUSH((ClientData)pNum, dbConnectStack);
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

	    for (i = PL_TECHDEPBASE; i < DBNumPlanes; i++)
	    {
		if (!PlaneMaskHasPlane(planes, i)) continue;
		if (IsSplit(tile))
		{
		    if (DBSrPaintNMArea((Tile *) NULL, csa->csa_def->cd_planes[i],
				TiGetTypeExact(tile), &newArea, connectMask,
				dbcFindTileFunc, (ClientData)&t2) != 0)
		    {
			STACKPUSH((ClientData)t2, dbConnectStack);
    			STACKPUSH((ClientData)i, dbConnectStack);
		    }
		}
		else if (DBSrPaintArea((Tile *) NULL, csa->csa_def->cd_planes[i],
			&newArea, connectMask, dbcFindTileFunc,
			(ClientData)&t2) != 0)
		{
    		    STACKPUSH((ClientData)t2, dbConnectStack);
    		    STACKPUSH((ClientData)i, dbConnectStack);
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

int
dbcUnconnectFunc(tile, clientData)
    Tile *tile;				/* Current tile	*/
    ClientData clientData;		/* Unused.	*/

{
    return 1;
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
 *	In addition to simply adding a label to the destination, this
 *	routine also checks port connections on abstract cells.  Abstract
 *      cells can have multiple ports of the same name with implied
 *	connectivity between them.
 *
 * Results:
 *	Return 0 normally, 1 if list size exceeds integer bounds.
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
    int dbcConnectFunc();		/* Forward declaration */

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
    /* already exist in the destination def first.			*/
    if (DBCheckLabelsByContent(def, &r, lab->lab_type, lab->lab_text))
	return 0;

    if (DBCheckLabelsByContent(def, &r, lab->lab_type, newlabptr))
	return 0;

    DBEraseLabelsByContent(def, &r, -1, lab->lab_text);
    DBPutFontLabel(def, &r, lab->lab_font, lab->lab_size, rotate, &offset,
		pos, newlabptr, lab->lab_type, lab->lab_flags, lab->lab_port);

    if (lab->lab_flags & PORT_DIR_MASK)
    {
	CellDef *orig_def = scx->scx_use->cu_def;
	Label *slab;
	int lidx = lab->lab_port;
	TileTypeBitMask *connectMask;

	/* Check for equivalent ports. For any found, call	*/
	/* DBTreeSrTiles recursively on the type and area	*/
	/* of the label.					*/

	/* Don't recurse, just add area to the csa2_list.       */
	/* Only add the next label found to the list.  If there	*/
	/* are more equivalent ports, they will be found when	*/
	/* processing this label's area.			*/

	for (slab = orig_def->cd_labels; slab != NULL; slab = slab->lab_next)
	    if ((slab->lab_flags & PORT_DIR_MASK) && (slab != lab))
		if (slab->lab_port == lidx)
		{
		    Rect newarea;
		    int i, pNum;

		    // Do NOT go searching on labels connected to space!
		    if (slab->lab_type == TT_SPACE) continue;

		    GeoTransRect(&scx->scx_trans, &slab->lab_rect, &newarea);

		    // Avoid infinite looping.  If material under the label
		    // has already been added to the destination, then ignore.

		    connectMask = &csa2->csa2_connect[slab->lab_type];

		    pNum = DBPlane(slab->lab_type);

		    // Do *not* run this check on zero area labels
		    if (!GEO_RECTNULL(&newarea))
		    	if (DBSrPaintArea((Tile *) NULL, def->cd_planes[pNum],
				&newarea, connectMask, dbcUnconnectFunc,
				(ClientData) NULL) == 1)
			    continue;

		    newarea.r_xbot--;
		    newarea.r_xtop++;
		    newarea.r_ybot--;
		    newarea.r_ytop++;

    		    /* Check if any of the last 5 entries has the same type and	*/
    		    /* area.  If so, don't duplicate the existing entry.	*/

		    for (i = csa2->csa2_lasttop; (i >= 0) &&
					(i > csa2->csa2_lasttop - 5); i--)
			if (connectMask == csa2->csa2_list[i].connectMask)
			    if (GEO_SURROUND(&csa2->csa2_list[i].area, &newarea))
				return 0;

		    /* Register the area and connection mask as needing to be processed */

		    if (++csa2->csa2_top == CSA2_LIST_SIZE)
		    {
			/* Reached list size limit---need to push the list and	*/
			/* start a new one.					*/

			conSrArea *newlist;

			newlist = (conSrArea *)mallocMagic(CSA2_LIST_SIZE *
				sizeof(conSrArea));
			StackPush((ClientData)csa2->csa2_list, csa2->csa2_stack);
			csa2->csa2_list = newlist;
			csa2->csa2_top = 0;
		    }

		    csa2->csa2_list[csa2->csa2_top].area = newarea;
		    csa2->csa2_list[csa2->csa2_top].connectMask = connectMask;
		    csa2->csa2_list[csa2->csa2_top].dinfo = 0;

		    /* See above:  Process only one equivalent port at a time */
		    break;
		}
    }
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
dbcConnectFunc(tile, cx)
    Tile *tile;			/* Tile found. */
    TreeContext *cx;		/* Describes context of search.  The client
				 * data is a pointer to a conSrArg2 record
				 * containing various required information.
				 */
{
    struct conSrArg2 *csa2;
    Rect tileArea, newarea;
    TileTypeBitMask *connectMask, notConnectMask;
    Rect *srArea;
    SearchContext *scx = cx->tc_scx;
    SearchContext scx2;
    TileType loctype = TiGetTypeExact(tile);
    TileType dinfo = 0;
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

    /* Clip the current area down to something that overlaps the
     * area of interest.
     */

    csa2 = (struct conSrArg2 *)cx->tc_filter->tf_arg;
    GeoClip(&newarea, csa2->csa2_bounds);
    if (GEO_RECTNULL(&newarea)) return 0;

    if (IsSplit(tile))
    {
	dinfo = DBTransformDiagonal(loctype, &scx->scx_trans);
	loctype = (SplitSide(tile)) ? SplitRightType(tile) : SplitLeftType(tile);
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
		dinfo, &newarea, &notConnectMask, dbcUnconnectFunc,
		(ClientData) NULL) == 0)
	retval = 0;

    /* Paint this tile into the destination cell.  This
     * marks its area has having been processed.  Then recycle
     * the storage for the current list element.
     */

    DBNMPaintPlane(def->cd_planes[pNum], dinfo,
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

    if (dinfo & TT_DIAGONAL)
    {
	if (dinfo & TT_SIDE)			/* right */
	    newarea.r_xtop += 1;
	else					/* left */
	    newarea.r_xbot -= 1;
	if (((dinfo & TT_SIDE) >> 1)
		== (dinfo & TT_DIRECTION))	/* top */
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
	/* start a new one.					*/

	conSrArea *newlist;

	newlist = (conSrArea *)mallocMagic(CSA2_LIST_SIZE * sizeof(conSrArea));
	StackPush((ClientData)csa2->csa2_list, csa2->csa2_stack);
	csa2->csa2_list = newlist;
	csa2->csa2_top = 0;
    }

    csa2->csa2_list[csa2->csa2_top].area = newarea;
    csa2->csa2_list[csa2->csa2_top].connectMask = connectMask;
    csa2->csa2_list[csa2->csa2_top].dinfo = dinfo;

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
{
    struct conSrArg2 csa2;
    TileTypeBitMask *newmask;
    TileType newtype;
    unsigned char searchtype;

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

    DBTreeSrTiles(scx, mask, xMask, dbcConnectFunc, (ClientData) &csa2);
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

	searchtype = TF_LABEL_ATTACH;
	if (newtype & TT_DIAGONAL)
	{
	    /* If the tile is split, then labels attached to the        */
	    /* opposite point of the triangle are NOT connected.        */

        if (newtype & TT_SIDE)
        {
	    if (newtype & TT_DIRECTION)
	        searchtype |= TF_LABEL_ATTACH_NOT_SW;
	    else
	        searchtype |= TF_LABEL_ATTACH_NOT_NW;
	    }
	    else
	    {
	    if (newtype & TT_DIRECTION)
	        searchtype |= TF_LABEL_ATTACH_NOT_NE;
	    else
	        searchtype |= TF_LABEL_ATTACH_NOT_SE;
	    }
	}
	if (doLabels == SEL_SIMPLE_LABELS) tpath.tp_first = NULL;
	if (doLabels != SEL_NO_LABELS)
	    if (DBTreeSrLabels(scx, newmask, xMask, &tpath, searchtype,
			dbcConnectLabelFunc, (ClientData) &csa2) != 0)
	    {
		TxError("Connection search hit memory limit and stopped.\n");
		break;
	    }
    }
    freeMagic((char *)csa2.csa2_list);
    StackFree(csa2.csa2_stack);

    /* Recompute the bounding box of the destination and record its area
     * for redisplay.
     */

    DBReComputeBbox(destUse->cu_def);
}
