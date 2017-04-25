/* rtrTraverse.c -
 *
 *	This file contains routines for traversing electrically
 *	connected regions of a layout.
 *	This code was copied from DBconnect.c and
 *	modified to maintain a traversal path using
 *	the C runtime stack.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/router/rtrTravers.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */


#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/signals.h"
#include "utils/malloc.h"
#include "router/router.h"
#include "windows/windows.h"
#include "utils/main.h"
#include "gcr/gcr.h"
#include "router/routerInt.h"

int	rtrTarget;			/* Via minimization, target type	*/
int	rtrReplace;			/* Via minimization, replacement type	*/
int	rtrDelta;			/* Change in layer width		*/

/* General note for rtrSrTraverse:
 *
 * The connectivity extractor works in two passes, in order to avoid
 * circularities.  During the first pass, each connected tile gets
 * marked, using the ti_client field.  This marking is needed to
 * avoid infinite searches on circular structures.  The second pass
 * is used to clear the markings again.
 */

/* The following structure is used to hold several pieces
 * of information that must be passed through multiple
 * levels of search function.
 */
	
struct conSrArg
{
    CellDef *csa_def;			/* Definition being searched. */
    int csa_pNum;			/* Index of plane being searched. */
    TileTypeBitMask *csa_connect;	/* Table indicating what connects
					 * to what.
					 */
    int (*csa_clientFunc)();		/* Client function to call. */
    ClientData csa_clientData;		/* Argument for clientFunc. */
    bool csa_clear;			/* FALSE means pass 1, TRUE
					 * means pass 2.
					 */
    Rect csa_bounds;			/* Area that limits search. */
};

/*
 * The search path is maintained on the C runtime stack
 * with rtrTileStack sructures.  Each entry on the stack
 * points back to the previous connected tile.
 */

struct	rtrTileStack
{
    Tile *ts_tile;			/* Tile at this level in the stack */
    struct rtrTileStack *ts_link;	/* Pointer to previous stack entry */
    struct conSrArg *ts_csa;		/* Pointer to search arguments */
};

/*
 * ----------------------------------------------------------------------------
 *
 * rtrSrTraverse --
 *
 *	This function is almost identical to DBSrConnect
 *	in that it searches through a cell to find all
 *	paint that is electrically connected to things
 *	in a given starting area.
 *	It differs in that it maintains a stack
 *	(on the C runtime stack) of the search path.
 *	This enables the client routine to examine
 *	the stack and recognize patterns of material
 *	in the connection path.  Since the connection path
 *	is a tree, a stack is convenient data structure for
 *	recording a particular path originating from a single node. 
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
rtrSrTraverse(def, startArea, mask, connect, bounds, func, clientData)
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
    struct rtrTileStack ts;
    int startPlane, result;
    Tile *startTile;			/* Starting tile for search. */
    extern int rtrSrTraverseFunc();	/* Forward declaration. */
    extern int rtrSrTraverseStartFunc();

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
	if (DBSrPaintArea((Tile *) NULL,
	    def->cd_planes[startPlane], startArea, mask,
	    rtrSrTraverseStartFunc, (ClientData) &startTile) != 0) break;
    }
    if (startTile == NULL)
	return 0;

    /* Pass 1.  During this pass the client function gets called. */

    csa.csa_clientFunc = func;
    csa.csa_clientData = clientData;
    csa.csa_clear = FALSE;
    csa.csa_connect = connect;
    csa.csa_pNum = startPlane;

    ts.ts_tile = (Tile *) NULL;
    ts.ts_link = (struct rtrTileStack *) NULL;
    ts.ts_csa  = &csa;

    if (rtrSrTraverseFunc(startTile, &ts) != 0)
	result = 1;

    /* Pass 2.  Don't call any client function, just clear the marks.
     * Don't allow any interruptions.
     */

    SigDisableInterrupts();
    csa.csa_clientFunc = NULL;
    csa.csa_clear = TRUE;
    csa.csa_pNum = startPlane;
    (void) rtrSrTraverseFunc(startTile, &ts);
    SigEnableInterrupts();

    return result;
}

int
rtrSrTraverseStartFunc(tile, pTile)
    Tile *tile;			/* This will be the starting tile. */
    Tile **pTile;		/* We store tile's address here. */
{
    *pTile = tile;
    return 1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrSrTraverseFunc --
 *
 * 	This search function gets called by DBSrPaintArea as part
 *	of rtrSrTraverse, and also recursively by itself.  Each invocation
 *	is made to process a single tile that is of interest.
 *	This function is copied from dbSrConnectFunc and differs by 
 *	maintaining a stack (on the C-run time stack) of the search
 *	path.
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
 * ----------------------------------------------------------------------------
 */

int
rtrSrTraverseFunc(tile, ts)
    Tile *tile;			/* Tile that is connected. */
    struct rtrTileStack *ts;	/* Contains information about the search. */
{
    Tile *t2;
    Rect tileArea;
    int i;
    TileTypeBitMask *connectMask;
    TileType ttype;
    unsigned int planes;
    struct conSrArg *csa = ts->ts_csa;
    struct rtrTileStack nts;

    nts.ts_csa = csa;
    nts.ts_tile = tile;
    nts.ts_link = ts;

    TiToRect(tile, &tileArea);
    ttype = TiGetType(tile);

    /* Make sure this tile overlaps the area we're interested in. */

    if (!GEO_OVERLAP(&tileArea, &csa->csa_bounds)) return 0;

    /* See if we've already been here before, and mark the tile as already
     * visited.
     */

    if (csa->csa_clear)
    {
	if (tile->ti_client == (ClientData) CLIENTDEFAULT) return 0;
	tile->ti_client = (ClientData) CLIENTDEFAULT;
    }
    else
    {
	if (tile->ti_client != (ClientData) CLIENTDEFAULT) return 0;
	tile->ti_client = (ClientData) 1;
    }

    /* Call the client function, if there is one. */

    if (csa->csa_clientFunc != NULL)
    {
	if ((*csa->csa_clientFunc)(tile, &nts) != 0)
	    return 1;
    }

    /* Now search around each of the four sides of this tile for
     * connected tiles.  For each one found, call ourselves
     * recursively.
     */
    
    connectMask = &csa->csa_connect[ttype];

    /* Left side: */

    for (t2 = BL(tile); BOTTOM(t2) < tileArea.r_ytop; t2 = RT(t2))
    {
	if (TTMaskHasType(connectMask, TiGetType(t2)))
	{
	    if (csa->csa_clear)
	    {
		if (t2->ti_client == (ClientData) CLIENTDEFAULT) continue;
	    }
	    else if (t2->ti_client != (ClientData) CLIENTDEFAULT) continue;
	    if (rtrSrTraverseFunc(t2, &nts) != 0) return 1;
	}
    }

    /* Bottom side: */

    for (t2 = LB(tile); LEFT(t2) < tileArea.r_xtop; t2 = TR(t2))
    {
	if (TTMaskHasType(connectMask, TiGetType(t2)))
	{
	    if (csa->csa_clear)
	    {
		if (t2->ti_client == (ClientData) CLIENTDEFAULT) continue;
	    }
	    else if (t2->ti_client != (ClientData) CLIENTDEFAULT) continue;
	    if (rtrSrTraverseFunc(t2, &nts) != 0) return 1;
	}
    }

    /* Right side: */

    for (t2 = TR(tile); ; t2 = LB(t2))
    {
	if (TTMaskHasType(connectMask, TiGetType(t2)))
	{
	    if (csa->csa_clear)
	    {
		if (t2->ti_client == (ClientData) CLIENTDEFAULT) goto nextRight;
	    }
	    else if (t2->ti_client != (ClientData) CLIENTDEFAULT) goto nextRight;
	    if (rtrSrTraverseFunc(t2, &nts) != 0) return 1;
	}
	nextRight: if (BOTTOM(t2) <= tileArea.r_ybot) break;
    }

    /* Top side: */

    for (t2 = RT(tile); ; t2 = BL(t2))
    {
	if (TTMaskHasType(connectMask, TiGetType(t2)))
	{
	    if (csa->csa_clear)
	    {
		if (t2->ti_client == (ClientData) CLIENTDEFAULT) goto nextTop;
	    }
	    else if (t2->ti_client != (ClientData) CLIENTDEFAULT) goto nextTop;
	    if (rtrSrTraverseFunc(t2, &nts) != 0) return 1;
	}
	nextTop: if (LEFT(t2) <= tileArea.r_xbot) break;
    }

    /* Lastly, check to see if this tile connects to anything on
     * other planes.  If so, search those planes.
     */
    
    planes = DBConnPlanes[ttype];
    planes &= ~(csa->csa_pNum);
    if (planes != 0)
    {
        struct conSrArg newcsa;

	newcsa = *csa;
	nts.ts_csa = &newcsa;
	for (i = PL_TECHDEPBASE; i < DBNumPlanes; i++)
	{
	    newcsa.csa_pNum = i;
	    if (DBSrPaintArea((Tile *) NULL,
			newcsa.csa_def->cd_planes[i],
			&tileArea, connectMask, rtrSrTraverseFunc,
			(ClientData) &nts) != 0)
		return 1;
	}
    }

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrExamineTile --
 *
 *	Examine a tile to see if it overlaps or connects
 *	the target tile.
 *
 * Results:
 *	Returns 1 if tile overlaps or connects
 *	the target tile.  This means the target tile
 *	cannot be moved to another routing layer.
 *	Returns 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
rtrExamineTile(tile, cdata)
    Tile *tile;
    ClientData cdata;
{
    if ( TiGetType(tile) == rtrTarget )
	return 1;
    
    if ( (tile != (Tile *) cdata) && 
	 (TiGetType(tile) == rtrReplace) )
	    return 1;

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrExamineStack --
 *
 *	Examine a segment of the traversal path 
 *	and identify segments of replacement material
 *	connected by vias at both ends and not overlapped
 *	or electrically connected to other routing material.
 *
 * Results:
 *	Always returns 0 to continue search.
 *
 * Side effects:
 *	Segments of replacement material are added to a list
 *	for later conversion to the target material.
 *	The vias are added to a list for later removal.
 *	They can't be processed now as pointers would get
 *	fouled up for the area search.
 *
 * ----------------------------------------------------------------------------
 */

int
rtrExamineStack(tile, ts)
    Tile *tile;
    struct rtrTileStack *ts;
{
    int i;
    Tile *tp[3];
    struct conSrArg *csa = ts->ts_csa;
    CellDef *def = csa->csa_def;

    /*
     * Collect topmost three elements of the stack.
     */

    i = 0;
    while ( i < 3 && ts && ts->ts_tile )
    {
	tp[i++] = ts->ts_tile;
	ts = ts->ts_link;
    }

    if ( i == 3 )
    {
	/*
	 * Identify pattern  --  *via*   *replacement_material*   *via*
	 */

	if ( DBIsContact(TiGetType(tp[0])) &&
	     (TiGetType(tp[1]) == rtrReplace) &&
	     DBIsContact(TiGetType(tp[2])))
	{
	    int plane;
	    Rect area;
	    TileTypeBitMask mask;
	    int deltax = rtrDelta, deltay = rtrDelta;

	    /*
	     * Search for overlapping or
	     * electrically connected routing material.
	     */

	    TTMaskZero(&mask);
	    TTMaskSetType(&mask, RtrPolyType);
	    TTMaskSetType(&mask, RtrMetalType);
	    TITORECT(tp[1], &area);
	    area.r_xbot--;
	    area.r_xtop++;
	    for ( plane = PL_PAINTBASE; plane < DBNumPlanes; plane++ )
		if ( DBPaintOnPlane(RtrPolyType, plane) ||
		     DBPaintOnPlane(RtrMetalType, plane) )
		    if ( DBSrPaintArea((Tile *)NULL, def->cd_planes[plane],
			    &area, &mask, rtrExamineTile, (ClientData) tp[1]) )
				return 0;

	    /*
	     * Mark areas for later processing.
	     */

	    if ( rtrDelta < 0 )
	    {
		if ( (TOP(tp[1]) == BOTTOM(tp[0])) || (TOP(tp[1]) == BOTTOM(tp[2])))
		    deltay = 0;
		if ( (RIGHT(tp[1]) == LEFT(tp[0])) || (RIGHT(tp[1]) == LEFT(tp[2])))
		    deltax = 0;
	    }

	    rtrListVia (tp[0]);
	    rtrListArea(tp[1], rtrReplace, rtrTarget, deltax, deltay);
	    rtrListVia (tp[2]);
	}
    }
    return 0;
}
