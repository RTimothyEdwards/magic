/*
 * DBpaint.c --
 *
 * Fast paint primitive.
 * This uses a very fast, heavily tuned algorithm for painting.
 * The basic outer loop is a non-recursive area enumeration, and
 * the inner loop attempts to avoid merging as much as possible.
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

/* #define	PAINTDEBUG /* For debugging */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/database/DBpaint.c,v 1.15 2010/09/24 19:53:19 tim Exp $";
#endif  /* not lint */

#include <sys/types.h>
#include <stdio.h>

#include "utils/magic.h"
#include "utils/malloc.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "database/databaseInt.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "dbwind/dbwind.h"
#include "utils/signals.h"
#include "textio/textio.h"
#include "utils/undo.h"

/* ---------------------- Imports from DBundo.c ----------------------- */

extern CellDef *dbUndoLastCell;
extern UndoType dbUndoIDPaint, dbUndoIDSplit, dbUndoIDJoin;

/* ----------------------- Forward declarations ----------------------- */

Tile *dbPaintMerge();
Tile *dbMergeType();
Tile *dbPaintMergeVert();

bool TiNMSplitX();
bool TiNMSplitY();
Tile *TiNMMergeRight();
Tile *TiNMMergeLeft();

#ifdef	PAINTDEBUG
int dbPaintDebug = 0;
#endif	/* PAINTDEBUG */

/* ----------------------- Flags to dbPaintMerge ---------------------- */

#define MRG_TOP		0x01
#define	MRG_LEFT	0x02
#define	MRG_RIGHT	0x04
#define	MRG_BOTTOM	0x08

/* -------------- Macros to see if merging is possible ---------------- */

#define	CANMERGE_Y(t1, t2)	(	LEFT(t1) == LEFT(t2) \
				    &&  TiGetTypeExact(t1) == TiGetTypeExact(t2) \
				    &&  !IsSplit(t1) \
				    &&	RIGHT(t1) == RIGHT(t2) )

#define	CANMERGE_X(t1, t2)	(	BOTTOM(t1) == BOTTOM(t2) \
				    &&  TiGetTypeExact(t1) == TiGetTypeExact(t2) \
				    &&  !IsSplit(t1) \
				    &&	TOP(t1) == TOP(t2) )

#define	SELMERGE_Y(t1, t2, msk)	(       LEFT(t1) == LEFT(t2) \
				    &&  TiGetTypeExact(t1) == TiGetTypeExact(t2) \
				    &&  !IsSplit(t1) \
				    &&	RIGHT(t1) == RIGHT(t2) \
				    &&  ! TTMaskHasType(msk, TiGetType(t1)) )

#define	SELMERGE_X(t1, t2, msk)	(	BOTTOM(t1) == BOTTOM(t2) \
				    &&  TiGetTypeExact(t1) == TiGetTypeExact(t2) \
				    &&  !IsSplit(t1) \
				    &&	TOP(t1) == TOP(t2) \
				    &&  ! TTMaskHasType(msk, TiGetType(t1)) )

/* This macro seems to buy us about 15% in speed */
#define	TISPLITX(res, otile, xcoord) \
    { \
	Tile *xtile = otile, *xxnew, *xp; \
	int x = xcoord; \
 \
	xxnew = (Tile *) TiAlloc(); \
	xxnew->ti_client = (ClientData) CLIENTDEFAULT; \
 \
	LEFT(xxnew) = x, BOTTOM(xxnew) = BOTTOM(xtile); \
	BL(xxnew) = xtile, TR(xxnew) = TR(xtile), RT(xxnew) = RT(xtile); \
 \
	/* Left edge */ \
	for (xp = TR(xtile); BL(xp) == xtile; xp = LB(xp)) BL(xp) = xxnew; \
	TR(xtile) = xxnew; \
 \
	/* Top edge */ \
	for (xp = RT(xtile); LEFT(xp) >= x; xp = BL(xp)) LB(xp) = xxnew; \
	RT(xtile) = xp; \
 \
	/* Bottom edge */ \
	for (xp = LB(xtile); RIGHT(xp) <= x; xp = TR(xp)) /* nothing */; \
	for (LB(xxnew) = xp; RT(xp) == xtile; RT(xp) = xxnew, xp = TR(xp)); \
	res = xxnew; \
    }

/* Use this for debugging purposes when necessary */
// #undef TISPLITX
// #define TISPLITX(a, b, c) a = TiSplitX(b, c)

/* Record undo information */

#define	DBPAINTUNDO(tile, newType, undo) \
    { \
	paintUE *xxpup; \
\
	if (undo->pu_def != dbUndoLastCell) dbUndoEdit(undo->pu_def); \
\
	xxpup = (paintUE *) UndoNewEvent(dbUndoIDPaint, sizeof(paintUE)); \
	if (xxpup) \
	{ \
	    xxpup->pue_rect.r_xbot = LEFT(tile); \
	    xxpup->pue_rect.r_xtop = RIGHT(tile); \
	    xxpup->pue_rect.r_ybot = BOTTOM(tile); \
	    xxpup->pue_rect.r_ytop = TOP(tile); \
	    xxpup->pue_oldtype = TiGetTypeExact(tile); \
	    xxpup->pue_newtype = newType; \
	    xxpup->pue_plane = undo->pu_pNum; \
	} \
    }



/*
 * ----------------------------------------------------------------------------
 *
 * dbSplitUndo(tile, splitx, undo)
 * dbJoinUndo(tile, splitx, undo)
 *
 * Record information about a non-Manhattan tile split, where a triangle
 * tile is subdivided in x and y.  dbSplitUndo and dbJoinUndo are
 * opposites, but use the same structures.
 *
 * ----------------------------------------------------------------------------
 */

void
dbSplitUndo(tile, splitx, undo)
    Tile *tile;
    int   splitx;
    PaintUndoInfo *undo;
{
    splitUE *xxsup;

    if (undo->pu_def != dbUndoLastCell) dbUndoEdit(undo->pu_def);
    xxsup = (splitUE *)UndoNewEvent(dbUndoIDSplit, sizeof(splitUE));
    if (xxsup)
    {
	xxsup->sue_point.p_x = LEFT(tile);
	xxsup->sue_point.p_y = BOTTOM(tile);
	xxsup->sue_splitx = splitx;
	xxsup->sue_plane = undo->pu_pNum;
    }
}

void
dbJoinUndo(tile, splitx, undo)
    Tile *tile;
    int   splitx;
    PaintUndoInfo *undo;
{
    splitUE *xxsup;

    if (undo->pu_def != dbUndoLastCell) dbUndoEdit(undo->pu_def);
    xxsup = (splitUE *)UndoNewEvent(dbUndoIDJoin, sizeof(splitUE));
    if (xxsup)
    {
	xxsup->sue_point.p_x = LEFT(tile);
	xxsup->sue_point.p_y = BOTTOM(tile);
	xxsup->sue_splitx = splitx;
	xxsup->sue_plane = undo->pu_pNum;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBPaintPlane0 --
 *
 * Paint a rectangular area ('area') on a single tile plane ('plane').
 *
 * The argument 'resultTbl' is a table, indexed by the type of each tile
 * found while enumerating 'area', that gives the result type for this
 * operation.  The semantics of painting, erasing, and "writing" (storing
 * a new type in the area without regard to the previous contents) are
 * all encapsulated in this table.
 *
 * If undo is desired, 'undo' should point to a PaintUndoInfo struct
 * that contains everything needed to build an undo record.  Otherwise,
 * 'undo' can be NULL.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the database plane that contains the given tile.
 *
 * REMINDER:
 *	Callers should remember to set the CDMODIFIED and CDGETNEWSTAMP
 *	bits in the cell definition containing the plane being painted.
 *
 * NOTE:
 *	This routine was modified from DBPaintPlane().  To prevent making
 *	a nested call (since this routine is very high frequency), DBPaintPlane()
 *	is defined in database.h as DBPaintPlane0(..., FALSE).  The use of
 *	DBPaintPlane(..., TRUE) is a replacement for the original
 *	DBPaintPlaneMergeOnce(), the purpose of which is to avoid painting
 *	any tile twice in the same pass, since the DRC overlap rule depends
 *	on it.
 *
 * ----------------------------------------------------------------------------
 */

void
DBPaintPlane0(plane, area, resultTbl, undo, method)
    Plane *plane;		/* Plane whose paint is to be modified */
    Rect *area;			/* Area to be changed */
    PaintResultType *resultTbl;	/* Table, indexed by the type of tile already
				 * present in the plane, giving the type to
				 * which the existing tile must change as a
				 * result of this paint operation.
				 */
    PaintUndoInfo *undo;	/* Record containing everything needed to
				 * save undo entries for this operation.
				 * If NULL, the undo package is not used.
				 */
    unsigned char method;	/* If PAINT_MARK, the routine marks tiles as it
				 * goes to avoid processing tiles twice.
				 */
{
    Point start;
    int clipTop, mergeFlags;
    TileType oldType, newType;
    Tile *tile, *newtile;
    Tile *tpnew;	/* Used for area search */
    Tile *tp;		/* Used for paint */
    bool haschanged;

    if (area->r_xtop <= area->r_xbot || area->r_ytop <= area->r_ybot)
	return;

    /*
     * The following is a modified version of the area enumeration
     * algorithm.  It expects the in-line paint code below to leave
     * 'tile' pointing to the tile from which we should continue the
     * search.
     */

    start.p_x = area->r_xbot;
    start.p_y = area->r_ytop - 1;
    tile = plane->pl_hint;
    GOTOPOINT(tile, &start);

    /* Each iteration visits another tile on the LHS of the search area */
    while (TOP(tile) > area->r_ybot)
    {
	/***
	 *** AREA SEARCH.
	 *** Each iteration enumerates another tile.
	 ***/
enumerate:
	if (SigInterruptPending)
	    break;

	clipTop = TOP(tile);
	if (clipTop > area->r_ytop) clipTop = area->r_ytop;

	/* Skip processed tiles, if the "method" option was PAINT_MARK */
	if (method == (unsigned char)PAINT_MARK)
	    if (tile->ti_client != (ClientData) CLIENTDEFAULT)
		goto paintdone;

	oldType = TiGetTypeExact(tile);

#ifdef	PAINTDEBUG
	if (dbPaintDebug)
	    dbPaintShowTile(tile, undo, "area enum");
#endif	/* PAINTDEBUG */

	/***
	 *** ---------- THE FOLLOWING IS IN-LINE PAINT CODE ----------
	 ***/

	/*
	 * Set up the directions in which we will have to
	 * merge initially.  Clipping can cause some of these
	 * to be turned off.
	 */
	mergeFlags = MRG_TOP | MRG_LEFT;
	if (RIGHT(tile) >= area->r_xtop) mergeFlags |= MRG_RIGHT;
	if (BOTTOM(tile) <= area->r_ybot) mergeFlags |= MRG_BOTTOM;

	/*
	 * Determine new type of this tile.
	 * Change the type if necessary.
	 */
	haschanged = FALSE;

	/* If the source tile is split, apply table to each side */

	if (method == (unsigned char)PAINT_XOR)
	    newType = *resultTbl;
	else if (!IsSplit(tile))
	    newType = resultTbl[oldType];
	else
	    newType = resultTbl[SplitLeftType(tile)]
		| (resultTbl[SplitRightType(tile)] << 14)
		| (oldType & (TT_DIAGONAL | TT_DIRECTION | TT_SIDE));

	if (oldType != newType)
	{
	    /*
	     * Clip the tile against the clipping rectangle.
	     * Merging is only necessary if we clip to the left or to
	     * the right, and then only to the top or the bottom.
	     * We do the merge in-line for efficiency.
	     */

	    /* Clip up */
	    if (TOP(tile) > area->r_ytop)
	    {
		if (IsSplit(tile))
		{
		    haschanged |= TiNMSplitY(&tile, &newtile, area->r_ytop, 1, undo);
		    if (!IsSplit(tile))
		    {
			oldType = TiGetTypeExact(tile);
			newType = (method == (unsigned char)PAINT_XOR) ?
				 *resultTbl : resultTbl[oldType];

			tile = TiNMMergeLeft(tile, plane);
			TiNMMergeRight(TR(newtile), plane);
		    }
		    else
		    {
			TiNMMergeLeft(newtile, plane);
			TiNMMergeRight(TR(tile), plane);
		    }
		}
		else
		{
		    newtile = TiSplitY(tile, area->r_ytop);
    		    TiSetBody(newtile, TiGetBody(tile));
		}
		mergeFlags &= ~MRG_TOP;
	    }

	    /* Clipping diagonals can cause the new tile to no longer be */
	    /* in the search path!					 */
	    if (RIGHT(tile) <= area->r_xbot)
		goto paintdone;
	    if (oldType == newType) goto clipdone;

	    /* Clip down */
	    if (BOTTOM(tile) < area->r_ybot)
	    {
		if (IsSplit(tile))
		{
		    haschanged |= TiNMSplitY(&tile, &newtile, area->r_ybot, 0, undo);
		    if (!IsSplit(tile))
		    {
			oldType = TiGetTypeExact(tile);
			newType = (method == (unsigned char)PAINT_XOR) ?
				 *resultTbl : resultTbl[oldType];

			tile = TiNMMergeLeft(tile, plane);
			TiNMMergeRight(TR(newtile), plane);
		    }
		    else
		    {
			TiNMMergeLeft(newtile, plane);
			TiNMMergeRight(TR(tile), plane);
		    }
		}
		else
		{
		    newtile = tile, tile = TiSplitY(tile, area->r_ybot);
		    TiSetBody(tile, TiGetBody(newtile));
		}
		mergeFlags &= ~MRG_BOTTOM;
	    }

	    /* Clipping diagonals can cause the new tile to no longer be */
	    /* in the search path!					 */
	    if (RIGHT(tile) <= area->r_xbot)
		goto paintdone;
	    if (oldType == newType) goto clipdone;

	    /* Clip right */
	    if (RIGHT(tile) > area->r_xtop)
	    {
		if (IsSplit(tile))
		{
		    haschanged |= TiNMSplitX(&tile, &newtile, area->r_xtop, 1, undo);
		    if (!IsSplit(tile))
		    {
			oldType = TiGetTypeExact(tile);
			newType = (method == (unsigned char)PAINT_XOR) ?
				 *resultTbl : resultTbl[oldType];

			tile = TiNMMergeLeft(tile, plane);
			TiNMMergeRight(LB(newtile), plane);
		    }
		    else
		    {
			TiNMMergeRight(newtile, plane);
			TiNMMergeLeft(LB(tile), plane);
		    }
		}
		else
		{
		    TISPLITX(newtile, tile, area->r_xtop);
		    TiSetBody(newtile, TiGetBody(tile));

		    /* Merge the outside tile to its top */
		    tp = RT(newtile);
		    if (CANMERGE_Y(newtile, tp)) TiJoinY(newtile, tp, plane);

		    /* Merge the outside tile to its bottom */
		    tp = LB(newtile);
		    if (CANMERGE_Y(newtile, tp)) TiJoinY(newtile, tp, plane);
		}
		mergeFlags &= ~MRG_RIGHT;
	    }

	    /* Clipping diagonals can cause the new tile	*/
	    /* to no longer be in the search path!		*/
	    if (BOTTOM(tile) >= area->r_ytop || RIGHT(tile) <= area->r_xbot)
		goto paintdone;
	    if (oldType == newType) goto clipdone;

	    /* Clip left */
	    if (LEFT(tile) < area->r_xbot)
	    {
		if (IsSplit(tile))
		{
		    haschanged |= TiNMSplitX(&tile, &newtile, area->r_xbot, 0, undo);
		    if (!IsSplit(tile))
		    {
			oldType = TiGetTypeExact(tile);
			newType = (method == (unsigned char)PAINT_XOR) ?
				 *resultTbl : resultTbl[oldType];

			// tile = TiNMMergeRight(tile, plane);
			TiNMMergeLeft(LB(newtile), plane);
		    }
		    else
		    {
			TiNMMergeLeft(newtile, plane);
			// TiNMMergeRight(LB(tile), plane);
		    }
		}
		else
		{
		    newtile = tile;
		    TISPLITX(tile, tile, area->r_xbot);
		    TiSetBody(tile, TiGetBody(newtile));

		    /* Merge the outside tile to its top */
		    tp = RT(newtile);
		    if (CANMERGE_Y(newtile, tp)) TiJoinY(newtile, tp, plane);

		    /* Merge the outside tile to its bottom */
		    tp = LB(newtile);
		    if (CANMERGE_Y(newtile, tp)) TiJoinY(newtile, tp, plane);
		}
		mergeFlags &= ~MRG_LEFT;
	    }

	    /* Clipping diagonals can cause the new tile	*/
	    /* to no longer be in the search path!		*/
	    if (BOTTOM(tile) >= area->r_ytop)
		goto paintdone;

#ifdef	PAINTDEBUG
	    if (dbPaintDebug)
		dbPaintShowTile(tile, undo, "after clip");
#endif	/* PAINTDEBUG */
	}

clipdone:

	if (newType & TT_DIAGONAL)
	{
	    /* If left and right types of a diagonal tile are   */
	    /* the same, revert back to a rectangular tile.	*/

	    if ((newType & TT_LEFTMASK) == ((newType & TT_RIGHTMASK) >> 14))
	    {
	        newType &= TT_LEFTMASK;
		if (undo && UndoIsEnabled())
		    DBPAINTUNDO(tile, newType, undo);
		TiSetBody(tile, newType);
	 	// if (method == PAINT_MARK) tile->ti_client = (ClientData)1;
		/* Reinstate the left and right merge requirements */
		mergeFlags |= MRG_LEFT;
		if (RIGHT(tile) >= area->r_xtop) mergeFlags |= MRG_RIGHT;
	    }
	    else
		mergeFlags = 0;
	}

	/*
	 * Merge the tile back into the parts of the plane that have
	 * already been visited.  Note that if we clipped in a particular
	 * direction we avoid merging in that direction.
	 *
	 * We avoid calling dbPaintMerge if at all possible.
	 */
	if (mergeFlags & MRG_LEFT)
	{
	    for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
		if (TiGetTypeExact(tp) == newType)
		{
		    tile = dbPaintMerge(tile, newType, area, plane, mergeFlags,
					undo, (method == (unsigned char)PAINT_MARK)
					? TRUE : FALSE);
		    goto paintdone;
		}
	    mergeFlags &= ~MRG_LEFT;
	}
	if (mergeFlags & MRG_RIGHT)
	{
	    for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp = LB(tp))
		if (TiGetTypeExact(tp) == newType)
		{
		    tile = dbPaintMerge(tile, newType, area, plane, mergeFlags,
					undo, (method == (unsigned char)PAINT_MARK)
					? TRUE : FALSE);
		    goto paintdone;
		}
	    mergeFlags &= ~MRG_RIGHT;
	}

	/*
	 * Cheap and dirty merge -- we don't have to merge to the
	 * left or right, so the top/bottom merge is very fast.
	 *
	 * Now it's safe to change the type of this tile, and
	 * record the event on the undo list.
	 */
	if (undo && UndoIsEnabled())
	    if (haschanged || (oldType != newType))
		DBPAINTUNDO(tile, newType, undo);

	TiSetBody(tile, newType);
	if (method == (unsigned char)PAINT_MARK) tile->ti_client = (ClientData)1;

#ifdef	PAINTDEBUG
	if (dbPaintDebug)
	    dbPaintShowTile(tile, undo, "changed type");
#endif	/* PAINTDEBUG */

	if (mergeFlags & MRG_TOP)
	{
	    tp = RT(tile);
	    if (CANMERGE_Y(tile, tp)) TiJoinY(tile, tp, plane);
#ifdef	PAINTDEBUG
	    if (dbPaintDebug)
		dbPaintShowTile(tile, undo, "merged up (CHEAP)");
#endif	/* PAINTDEBUG */
	}
	if (mergeFlags & MRG_BOTTOM)
	{
	    tp = LB(tile);
	    if (CANMERGE_Y(tile, tp)) TiJoinY(tile, tp, plane);
#ifdef	PAINTDEBUG
	    if (dbPaintDebug)
		dbPaintShowTile(tile, undo, "merged down (CHEAP)");
#endif	/* PAINTDEBUG */
	}

paintdone:
	/***
	 ***		END OF PAINT CODE
	 *** ---------- BACK TO AREA SEARCH ----------
	 ***/
	/* Move right if possible */
	tpnew = TR(tile);
	if (LEFT(tpnew) < area->r_xtop)
	{
	    /* Move back down into clipping area if necessary */
	    while (BOTTOM(tpnew) >= clipTop) tpnew = LB(tpnew);
	    if (BOTTOM(tpnew) >= BOTTOM(tile) || BOTTOM(tile) <= area->r_ybot)
	    {
		tile = tpnew;
		goto enumerate;
	    }
	}

	/* Each iteration returns one tile further to the left */
	while (LEFT(tile) > area->r_xbot)
	{
	    /* Move left if necessary */
	    if (BOTTOM(tile) <= area->r_ybot)
		goto done;

	    /* Move down if possible; left otherwise */
	    tpnew = LB(tile); tile = BL(tile);
	    if (BOTTOM(tpnew) >= BOTTOM(tile) || BOTTOM(tile) <= area->r_ybot)
	    {
		tile = tpnew;
		goto enumerate;
	    }
	}
	/* At left edge -- walk down to next tile along the left edge */
	for (tile = LB(tile); RIGHT(tile) <= area->r_xbot; tile = TR(tile))
	    /* Nothing */;
    }

done:

    if (method == (unsigned char)PAINT_MARK)
    {
	/* Now unmark the processed tiles with the same search algorithm */
	/* Expand the area by one to catch tiles that were clipped at	 */
	/* the area boundary.						 */
	
	area->r_xbot -= 1;
	area->r_ybot -= 1;
	area->r_xtop += 1;
	area->r_ytop += 1;
	start.p_x = area->r_xbot;
	start.p_y = area->r_ytop - 1;

	tile = plane->pl_hint;
	GOTOPOINT(tile, &start);

	while (TOP(tile) > area->r_ybot)
	{
enum2:
	    clipTop = TOP(tile);
	    if (clipTop > area->r_ytop) clipTop = area->r_ytop;

	    tile->ti_client = (ClientData)CLIENTDEFAULT;

	    /* Move right if possible */
	    tpnew = TR(tile);
	    if (LEFT(tpnew) < area->r_xtop)
	    {
		/* Move back down into clipping area if necessary */
		while (BOTTOM(tpnew) >= clipTop) tpnew = LB(tpnew);
		if (BOTTOM(tpnew) >= BOTTOM(tile) || BOTTOM(tile) <= area->r_ybot)
		{
		    tile = tpnew;
		    goto enum2;
		}
	    }

	    /* Each iteration returns one tile further to the left */
	    while (LEFT(tile) > area->r_xbot)
	    {
		/* Move left if necessary */
		if (BOTTOM(tile) <= area->r_ybot)
		    goto done2;

		/* Move down if possible; left otherwise */
		tpnew = LB(tile); tile = BL(tile);
		if (BOTTOM(tpnew) >= BOTTOM(tile) || BOTTOM(tile) <= area->r_ybot)
		{
		    tile = tpnew;
		    goto enum2;
		}
	        tile->ti_client = (ClientData)CLIENTDEFAULT;
	    }
	    /* At left edge -- walk down to next tile along the left edge */
	    for (tile = LB(tile); RIGHT(tile) <= area->r_xbot; tile = TR(tile))
	        tile->ti_client = (ClientData)CLIENTDEFAULT;
	}
	tile->ti_client = (ClientData)CLIENTDEFAULT;
    }

done2:
    plane->pl_hint = tile;
}

/*
 * ----------------------------------------------------------------------------
 * DBSplitTile --
 *
 *	This routine fractures a non-Manhattan tile into four parts, by
 *	splitting along the X and Y axes (X is provided as an argument;
 *	Y is determined from the intercept of the X split position and
 *	the tile diagonal).  This routine is used only by the "undo"
 *	code.
 * ----------------------------------------------------------------------------
 */

void
DBSplitTile(plane, point, splitx)
    Plane *plane;
    Point *point;
    int splitx;
{
    Tile *tile, *newtile, *tp;
    tile = plane->pl_hint;
    GOTOPOINT(tile, point);

    if (IsSplit(tile))		/* This should always be true */
    {
	TiNMSplitX(&tile, &newtile, splitx, 1, (PaintUndoInfo *)NULL);
	if (!IsSplit(tile))
	{
	    TiNMMergeLeft(tile, plane);
	    TiNMMergeRight(LB(newtile), plane);
	}
	else
	{
	    TiNMMergeRight(newtile, plane);
	    TiNMMergeLeft(LB(tile), plane);
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBFracturePlane --
 *
 * This routine fractures the plane around the given area, splitting
 * existing non-Manhattan tiles which cross the area boundary.  After
 * fracturing, it merges tiles where possible inside and outside the
 * area boundary to maintain the maximum horizontal stripes rule.
 * This routine is used by DBNMPaintPlane to prepare an area for
 * painting with a non-manhattan diagonally split tile.
 *
 * Use the resultTbl such that we ONLY fracture tiles that interact
 * with the type to be painted.  This prevents "frivolous fracturing".
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the database plane that contains the given tile.
 *
 * ----------------------------------------------------------------------------
 */

void
DBFracturePlane(plane, area, resultTbl, undo)
    Plane *plane;		/* Plane whose paint is to be modified */
    Rect *area;	/* Area to be changed */
    PaintResultType *resultTbl;	/* Paint table, to pinpoint those tiles
				 * that interact with the paint type.
				 */
    PaintUndoInfo *undo;	/* Record containing everything needed to
				 * save undo entries for this operation.
				 * If NULL, the undo package is not used.
				 */
{
    Point start;
    int clipTop;
    TileType oldType;
    Tile *tile, *newtile;
    Tile *tpnew;	/* Used for area search */
    Tile *tp;		/* Used for paint */

    if (area->r_xtop <= area->r_xbot || area->r_ytop <= area->r_ybot)
	return;

    /*
     * The following is a modified version of the area enumeration
     * algorithm.  It expects the in-line paint code below to leave
     * 'tile' pointing to the tile from which we should continue the
     * search.
     */

    start.p_x = area->r_xbot;
    start.p_y = area->r_ytop - 1;
    tile = plane->pl_hint;
    GOTOPOINT(tile, &start);

    /* Each iteration visits another tile on the LHS of the search area */
    while (TOP(tile) > area->r_ybot)
    {
	/***
	 *** AREA SEARCH.
	 *** Each iteration enumerates another tile.
	 ***/
enumerate:
	if (SigInterruptPending)
	    break;

	clipTop = TOP(tile);
	if (clipTop > area->r_ytop) clipTop = area->r_ytop;

	/* Ignore all tiles that are not non-Manhattan */
	if (!IsSplit(tile)) goto paintdone;

	/* Important!  Determine if the tile actually interacts	*/
	/* with the type to be painted.  If not, then ignore it	*/

	oldType = TiGetLeftType(tile);
	if (resultTbl[oldType] == oldType)
	{
	    oldType = TiGetRightType(tile);
	    if (resultTbl[oldType] == oldType)
		goto paintdone;
	}
	oldType = TiGetTypeExact(tile);

#ifdef	PAINTDEBUG
	if (dbPaintDebug)
	    dbPaintShowTile(tile, undo, "area enum");
#endif	/* PAINTDEBUG */

	/***
	 *** ---------- THE FOLLOWING IS IN-LINE PAINT CODE ----------
	 ***/

	/*
	 * Clip the tile against the clipping rectangle.
	 * Merging is only necessary if we clip to the left or to
	 * the right, and then only to the top or the bottom.
	 * We do the merge in-line for efficiency.
	 */

	/* Clip up */
	if (TOP(tile) > area->r_ytop)
	{
	    if (IsSplit(tile))
	    {
		TiNMSplitY(&tile, &newtile, area->r_ytop, 1, undo);
		if (!IsSplit(tile))
		{
		    oldType = TiGetTypeExact(tile);
		    tile = TiNMMergeLeft(tile, plane);
		    TiNMMergeRight(TR(newtile), plane);
		}
		else
		{
		    TiNMMergeLeft(newtile, plane);
		    TiNMMergeRight(TR(tile), plane);
		}
	    }
	}

	/* Clipping diagonals can cause the new tile to no	*/
	/* longer be in the search path!			*/
	if (RIGHT(tile) <= area->r_xbot)
	    goto paintdone;

	/* Clip down */
	if (BOTTOM(tile) < area->r_ybot)
	{
	    if (IsSplit(tile))
	    {
		TiNMSplitY(&tile, &newtile, area->r_ybot, 0, undo);
		if (!IsSplit(tile))
		{
		    oldType = TiGetTypeExact(tile);

		    tile = TiNMMergeLeft(tile, plane);
		    TiNMMergeRight(TR(newtile), plane);
		}
		else
		{
		    TiNMMergeLeft(newtile, plane);
		    TiNMMergeRight(TR(tile), plane);
		}
	    }
	    else
		newtile = tile;
	}

	/* Clipping diagonals can cause the new tile to no longer be	*/
	/* in the search path!						*/
	if (RIGHT(tile) <= area->r_xbot)
	    goto paintdone;

	/* Clip right */
	if (RIGHT(tile) > area->r_xtop)
	{
	    if (IsSplit(tile))
	    {
		TiNMSplitX(&tile, &newtile, area->r_xtop, 1, undo);
		if (!IsSplit(tile))
		{
		    oldType = TiGetTypeExact(tile);
		    tile = TiNMMergeLeft(tile, plane);
		    TiNMMergeRight(LB(newtile), plane);
		}
		else
		{
		    TiNMMergeRight(newtile, plane);
		    TiNMMergeLeft(LB(tile), plane);
		}
	    }
	}

	/* Clipping diagonals can cause the new tile	*/
	/* to no longer be in the search path!		*/
	if (BOTTOM(tile) >= area->r_ytop)
	    goto paintdone;

	/* Clip left */
	if (LEFT(tile) < area->r_xbot)
	{
	    if (IsSplit(tile))
	    {
		TiNMSplitX(&tile, &newtile, area->r_xbot, 0, undo);
		if (!IsSplit(tile))
		{
		    oldType = TiGetTypeExact(tile);
		    tile = TiNMMergeRight(tile, plane);
		    TiNMMergeLeft(LB(newtile), plane);
		}
		else
		{
		    TiNMMergeLeft(newtile, plane);
		    TiNMMergeRight(LB(tile), plane);
		}
	    }
	    else
		newtile = tile;
	}

	/* Clipping diagonals can cause the new tile	*/
	/* to no longer be in the search path!		*/
	if (BOTTOM(tile) >= area->r_ytop)
	    goto paintdone;

#ifdef	PAINTDEBUG
	if (dbPaintDebug)
	    dbPaintShowTile(tile, undo, "after clip");
#endif	/* PAINTDEBUG */

	/***
	 ***		END OF PAINT CODE
	 *** ---------- BACK TO AREA SEARCH ----------
	 ***/
paintdone:
	/* Move right if possible */
	tpnew = TR(tile);
	if (LEFT(tpnew) < area->r_xtop)
	{
	    /* Move back down into clipping area if necessary */
	    while (BOTTOM(tpnew) >= clipTop) tpnew = LB(tpnew);
	    if (BOTTOM(tpnew) >= BOTTOM(tile) || BOTTOM(tile) <= area->r_ybot)
	    {
		tile = tpnew;
		goto enumerate;
	    }
	}

	/* Each iteration returns one tile further to the left */
	while (LEFT(tile) > area->r_xbot)
	{
	    /* Move left if necessary */
	    if (BOTTOM(tile) <= area->r_ybot)
		goto done;

	    /* Move down if possible; left otherwise */
	    tpnew = LB(tile); tile = BL(tile);
	    if (BOTTOM(tpnew) >= BOTTOM(tile) || BOTTOM(tile) <= area->r_ybot)
	    {
		tile = tpnew;
		goto enumerate;
	    }
	}
	/* At left edge -- walk down to next tile along the left edge */
	for (tile = LB(tile); RIGHT(tile) <= area->r_xbot; tile = TR(tile))
	    /* Nothing */;
    }

done:
    plane->pl_hint = tile;
}

/*
 * ----------------------------------------------------------------------------
 * DBMergeNMTiles0 --
 *
 *	This procedure works through the plane like DBPaintPlane.  As it
 *	goes, it checks for possible non-Manhattan tile merges, and
 *	merges those tiles that meet the criteria.  This routine should
 *	be run before all writes to external databases to avoid problems
 *	caused by severe plane fracturing, but it may be run after any
 *	paint/erase routine to clean up fracturing.
 *
 *	Because the resulting database is identical to the original for
 *	all practical purposes, there is no "undo" record for this
 *	operation.
 *
 * Results:
 *	Return 0
 *
 * Side Effects:
 *	Messes with the corner-stitched database.
 *
 * Notes:
 *	DBMergeNMTiles is a macro definition of a wrapper for calling
 *	DBMergeNMTiles0 with mergeOnce = FALSE.
 *
 * ----------------------------------------------------------------------------
 */

int
DBMergeNMTiles0(plane, area, undo, mergeOnce)
    Plane *plane;
    Rect *area;
    PaintUndoInfo *undo;
    bool mergeOnce;
{
    Point start;
    int clipTop;
    Tile *tile, *tp, *tp2, *newtile, *tpnew;
    int aspecta, aspectb;
    TileType ttype, ltype, rtype;

    start.p_x = area->r_xbot;
    start.p_y = area->r_ytop - 1;
    tile = plane->pl_hint;
    GOTOPOINT(tile, &start);

    /* Each iteration visits another tile on the LHS of the search area */
    while (TOP(tile) > area->r_ybot)
    {

nmenum:
	if (SigInterruptPending)
	    break;

	clipTop = TOP(tile);
	if (clipTop > area->r_ytop) clipTop = area->r_ytop;

	while (IsSplit(tile))
	{
	    ttype = (TiGetTypeExact(tile) & ~TT_SIDE);

	    /* Two main cases:  direction 0 (/) and direction 1 (\) */

	    if (SplitDirection(tile) == 0)
	    {
		/* find tile at SW corner */
		tp = LB(tile);
		tp = BL(tp);
		while (BOTTOM(tile) > TOP(tp))
		    tp = RT(tp);
		if ((RIGHT(tp) != LEFT(tile)) || (BOTTOM(tile) != TOP(tp)))
		    break;	
	    }
	    else
	    {
		/* find tile at SE corner */
		tp = LB(tile);
		while (LEFT(tp) < RIGHT(tile))
		    tp = TR(tp);
		if ((RIGHT(tile) != LEFT(tp)) || (BOTTOM(tile) != TOP(tp)))
		    break;
	    }
	    if ((TiGetTypeExact(tp) & ~TT_SIDE) != ttype)
		break;

	    aspecta = (RIGHT(tp) - LEFT(tp)) * (TOP(tile) - BOTTOM(tile));
	    aspectb = (RIGHT(tile) - LEFT(tile)) * (TOP(tp) - BOTTOM(tp));
	    if (aspecta != aspectb)
	 	break;

	    ltype = TiGetLeftType(tile);
	    rtype = TiGetRightType(tile);

	    if (SplitDirection(tile) == 0)
	    {
		/* Walk up from tp */
		tp2 = RT(tp);
		while (BOTTOM(tp2) < TOP(tile))
		{
		    if (LEFT(tp2) > LEFT(tp)) break;
		    if (TiGetTypeExact(tp2) != ltype) break;
		    tp2 = RT(tp2);
		}
		if (BOTTOM(tp2) < TOP(tile)) break;

		/* Walk down from tile */
		tp2 = LB(tile);
		while (TOP(tp2) > BOTTOM(tp))
		{
		    if (RIGHT(tp2) < RIGHT(tile)) break;
		    if (TiGetTypeExact(tp2) != rtype) break;
		    tp2 = LB(tp2);
		}
		if (TOP(tp2) > BOTTOM(tp)) break;

		/* Announce merge to undo system */
		if (undo && UndoIsEnabled())
		    dbJoinUndo(tile, LEFT(tile), undo);

		/* All's clear to merge.  Again, walk up from tp */
		tp2 = RT(tp);
		while (BOTTOM(tp2) < TOP(tile))
		{
		    if (TOP(tp2) > TOP(tile))
		    {
			newtile = TiSplitY(tp2, TOP(tile));
			TiSetBody(newtile, ltype);
			if (CANMERGE_X(newtile, BL(newtile)))
			    TiJoinX(newtile, BL(newtile), plane);
			if (CANMERGE_X(newtile, TR(newtile)))
			    TiJoinX(newtile, TR(newtile), plane);
			if (CANMERGE_Y(newtile, RT(newtile)))
			    TiJoinY(newtile, RT(newtile), plane);
		    }
		    if (LEFT(tp2) < LEFT(tp))
		    {
			newtile = TiSplitX(tp2, LEFT(tp));
			TiSetBody(newtile, ltype);
			if (CANMERGE_Y(tp2, LB(tp2)))
			    TiJoinY(tp2, LB(tp2), plane);
			if (CANMERGE_Y(tp2, RT(tp2)))
			    TiJoinY(tp2, RT(tp2), plane);
			tp2 = newtile;
		    }
		    TiJoinY(tp2, tp, plane);
		    tp = tp2;
		    tp2 = RT(tp2);
		}

		/* Walk down from tile */
		tp2 = LB(tile);
		while (TOP(tp2) > BOTTOM(tp))
		{
		    if (BOTTOM(tp2) < BOTTOM(tp))
		    {
			newtile = TiSplitY(tp2, BOTTOM(tp));
			TiSetBody(newtile, rtype);
			if (CANMERGE_X(tp2, BL(tp2)))
			    TiJoinX(tp2, BL(tp2), plane);
			if (CANMERGE_X(tp2, TR(tp2)))
			    TiJoinX(tp2, TR(tp2), plane);
			if (CANMERGE_Y(tp2, LB(tp2)))
			    TiJoinY(tp2, LB(tp2), plane);
			tp2 = newtile;
		    }
		    if (RIGHT(tp2) > RIGHT(tile))
		    {
			newtile = TiSplitX(tp2, RIGHT(tile));
			TiSetBody(newtile, rtype);
			if (CANMERGE_Y(newtile, LB(newtile)))
			    TiJoinY(newtile, LB(newtile), plane);
			if (CANMERGE_Y(newtile, RT(newtile)))
			    TiJoinY(newtile, RT(newtile), plane);
		    }
		    TiJoinY(tp2, tile, plane);
		    tile = tp2;
		    tp2 = LB(tp2);
		}
		/* Merge tp and tile */
		TiJoinX(tile, tp, plane);
		TiSetBody(tile, ttype);
	    }
	    else	/* split direction 1 */
	    {
		/* Walk down from tile */
		tp2 = LB(tile);
		while (TOP(tp2) > BOTTOM(tp))
		{
		    while (RIGHT(tp2) < LEFT(tp)) tp2 = TR(tp2);
		    if (LEFT(tp2) > LEFT(tile)) break;
		    if (TiGetTypeExact(tp2) != ltype) break;
		    tp2 = LB(tp2);
		}
		if (TOP(tp2) > BOTTOM(tp)) break;

		/* Walk up from tp */
		tp2 = RT(tp);
		while (BOTTOM(tp2) < TOP(tile))
		{
		    while (LEFT(tp2) > RIGHT(tile)) tp2 = BL(tp2);
		    if (RIGHT(tp2) < RIGHT(tp)) break;
		    if (TiGetTypeExact(tp2) != rtype) break;
		    tp2 = RT(tp2);
		}
		if (BOTTOM(tp2) < TOP(tile)) break;

		/* Announce merge to undo system */
		if (undo && UndoIsEnabled())
		    dbJoinUndo(tile, RIGHT(tile), undo); 

		/* All's clear to merge.  Again, walk down from tile */
		tp2 = LB(tile);
		while (TOP(tp2) > BOTTOM(tp))
		{
		    while (RIGHT(tp2) < LEFT(tp)) tp2 = TR(tp2);
		    if (BOTTOM(tp2) < BOTTOM(tp))
		    {
			newtile = TiSplitY(tp2, BOTTOM(tp));
			TiSetBody(newtile, ltype);
			if (CANMERGE_X(tp2, BL(tp2)))
			    TiJoinX(tp2, BL(tp2), plane);
			if (CANMERGE_X(tp2, TR(tp2)))
			    TiJoinX(tp2, TR(tp2), plane);
			if (CANMERGE_Y(tp2, LB(tp2)))
			    TiJoinY(tp2, LB(tp2), plane);
			tp2 = newtile;
		    }
		    if (LEFT(tp2) < LEFT(tile))
		    {
			newtile = TiSplitX(tp2, LEFT(tile));
			TiSetBody(newtile, ltype);
			if (CANMERGE_Y(tp2, LB(tp2)))
			    TiJoinY(tp2, LB(tp2), plane);
			if (CANMERGE_Y(tp2, RT(tp2)))
			    TiJoinY(tp2, RT(tp2), plane);
			tp2 = newtile;
		    }
		    TiJoinY(tp2, tile, plane);
		    tile = tp2;
		    tp2 = LB(tp2);
		}

		/* Walk up from tp */
		tp2 = RT(tp);
		while (BOTTOM(tp2) < TOP(tile))
		{
		    while (LEFT(tp2) > RIGHT(tile)) tp2 = BL(tp2);
		    if (TOP(tp2) > TOP(tile))
		    {
			newtile = TiSplitY(tp2, TOP(tile));
			TiSetBody(newtile, rtype);
			if (CANMERGE_X(newtile, BL(newtile)))
			    TiJoinX(newtile, BL(newtile), plane);
			if (CANMERGE_X(newtile, TR(newtile)))
			    TiJoinX(newtile, TR(newtile), plane);
			if (CANMERGE_Y(newtile, RT(newtile)))
			    TiJoinY(newtile, RT(newtile), plane);
		    }
		    if (RIGHT(tp2) > RIGHT(tp))
		    {
			newtile = TiSplitX(tp2, RIGHT(tp));
			TiSetBody(newtile, rtype);
			if (CANMERGE_Y(newtile, LB(newtile)))
			    TiJoinY(newtile, LB(newtile), plane);
			if (CANMERGE_Y(newtile, RT(newtile)))
			    TiJoinY(newtile, RT(newtile), plane);
		    }
		    TiJoinY(tp2, tp, plane);
		    tp = tp2;
		    tp2 = RT(tp2);
		}
	 	/* Merge tp and tile */	
		TiJoinX(tile, tp, plane);
		TiSetBody(tile, ttype);
	    }
	    /* Now repeat until no more merging is possible */

	    if (mergeOnce) break;
	}

	/* Move right if possible */
	tpnew = TR(tile);
	if (LEFT(tpnew) < area->r_xtop)
	{
	    /* Move back down into clipping area if necessary */
	    while (BOTTOM(tpnew) >= clipTop) tpnew = LB(tpnew);
	    if (BOTTOM(tpnew) >= BOTTOM(tile) || BOTTOM(tile) <= area->r_ybot)
	    {
		tile = tpnew;
		goto nmenum;
	    }
	}

	/* Each iteration returns one tile further to the left */
	while (LEFT(tile) > area->r_xbot)
	{
	    /* Move left if necessary */
	    if (BOTTOM(tile) <= area->r_ybot)
		goto nmdone;

	    /* Move down if possible; left otherwise */
	    tpnew = LB(tile); tile = BL(tile);
	    if (BOTTOM(tpnew) >= BOTTOM(tile) || BOTTOM(tile) <= area->r_ybot)
	    {
		tile = tpnew;
		goto nmenum;
	    }
	}
	/* At left edge -- walk down to next tile along the left edge */
	for (tile = LB(tile); RIGHT(tile) <= area->r_xbot; tile = TR(tile));
    }

nmdone:
    plane->pl_hint = tile;
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 * DBDiagonalProc(type) --
 *
 *	Return the result type of a diagonal tile painted on oldtype;
 *	Argument "cdata", gives direction and side of diagonal, and the
 *	paint result table for the given diagonal side.
 *
 *	If the result cannot be described with a single tile, then
 *	return -1.
 *
 *	This routine is called only from DBNMPaintPlane().
 * 
 * Results:
 *	Returns the new type to be painted, or -1 if the result can
 *	only be generated by quartering the original tile.
 *
 * Side Effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

int
DBDiagonalProc(oldtype, cdata)
    TileType oldtype;
    ClientData cdata;
{
    TileType old_n, old_s, old_e, old_w;
    TileType new_n, new_s, new_e, new_w;
    TileType newtype;
    DiagInfo *dinfo = (DiagInfo *)cdata;
    PaintResultType *resultTbl = dinfo->resultTbl;

    /* Disassemble old and new types into four quadrants, find the	*/
    /* paint result for each quadrant, then reassemble the result.	*/

    if (oldtype & TT_DIAGONAL)
    {
	old_e = (oldtype & TT_RIGHTMASK) >> 14;
	old_w = oldtype & TT_LEFTMASK;

	if (oldtype & TT_DIRECTION)
	{
	    old_n = old_e;
	    old_s = old_w;
	}
	else
	{
	    old_n = old_w;
	    old_s = old_e;
	}
    }
    else
	old_n = old_s = old_e = old_w = oldtype;

    /* Determine result for each quadrant */

    if (dinfo->side == 0)
    {
	new_e = old_e;
	new_w = resultTbl[old_w];
    }
    else
    {
	new_e = resultTbl[old_e];
	new_w = old_w;
    }

    if (dinfo->dir == dinfo->side)
    {
	new_n = resultTbl[old_n];
	new_s = old_s;
    }
    else
    {
	new_n = old_n;
	new_s = resultTbl[old_s];
    }

    /* Now reassemble */

    if ((new_n == new_e) && (new_s == new_w))
    {
	if (new_n == new_w)
	{
	    newtype = new_n;	/* Turned back into a rectangle */
	    return newtype;
	}
	else 
	{
	    newtype = new_w | (new_e << 14);
	    newtype |= (TT_DIAGONAL | TT_DIRECTION);
	}
    }
    else if ((new_n == new_w) && (new_s == new_e))
    {
	newtype = new_w | (new_e << 14);
	newtype |= TT_DIAGONAL;
    }
    else
	return -1;

    /* For purposes of "undo" recording, record which side we just painted */
    if (dinfo->side)
	newtype |= TT_SIDE;

    return newtype;
}	
    
typedef struct
{
    Rect	*area;		/* An area to be painted with a triangle */
    int		width;		/* Dimensions of area, preprocessed for speed */
    int		height;
    TileType	dinfo;		/* Information about the triangle to paint */
    Plane	*plane;
    PaintUndoInfo *undo;
} TileRect;

/*
 * ----------------------------------------------------------------------------
 *
 * DBNMPaintPlane --
 *
 * Non-Manhattan PaintPlane function (wrapper for DBPaintPlane)
 *
 * Due to the intricacies of painting diagonals, it is necessary to
 * perform a search on the area to paint and return a list of sub-areas
 * matching the tiles underneath.  The sub-areas are checked against the
 * diagonal and further subdivided as necessary to prevent attempts to
 * paint quadrangular (clipped triangle) areas.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Plane is painted with a diagonal.  The plane may be hacked up
 *	to deal with the numerous situations involved in painting
 *	diagonal tiles and/or painting on diagonal tiles.
 *
 * ----------------------------------------------------------------------------
 */

void
DBNMPaintPlane0(plane, exacttype, area, resultTbl, undo, method)
    Plane *plane;		/* Plane whose paint is to be modified */
    TileType exacttype;		/* diagonal info for tile to be changed */
    Rect *area;	/* Area to be changed */
    PaintResultType *resultTbl;	/* Table, indexed by the type of tile already
				 * present in the plane, giving the type to
				 * which the existing tile must change as a
				 * result of this paint operation.
				 */
    PaintUndoInfo *undo;	/* Record containing everything needed to
				 * save undo entries for this operation.
				 * If NULL, the undo package is not used.
				 */
    unsigned char method;	/* If true, track tiles as they are processed */

#define RES_LEFT	0	/* Result is rect to left of diagonal */
#define RES_DIAG	1	/* Resulting rectangle is on diagonal */
#define RES_RIGHT	2	/* Result is rect to right of diagonal */

{
    DiagInfo dinfo;
    LinkedRect *lhead, *lr, *newlr;
    int xc, yc, width, height;
    dlong xref, yref;		/* xref, yref can easily exceed 32 bits */
    int resstate;

    if (exacttype & TT_DIAGONAL)
    {
	int dbNMEnumFunc();	/* Forward reference */
	TileRect arg;

	dinfo.resultTbl = resultTbl;
	dinfo.dir = (exacttype & TT_DIRECTION) ? 1 : 0;
	dinfo.side = (exacttype & TT_SIDE) ? 1 : 0;

	height = area->r_ytop - area->r_ybot;
	width = area->r_xtop - area->r_xbot;

	/* Break non-Manhattan tiles around the specified boundary */
	DBFracturePlane(plane, area, resultTbl, undo);

	/* Find all tiles under the area to paint, and make a	*/
	/* linked list out of them.				*/

	lhead = NULL;
	DBSrPaintArea(plane->pl_hint, plane, area, &DBAllTypeBits,
			dbNMEnumFunc, (ClientData) &lhead);

	/*--------------------------------------------------------------*/
	/* Each rectangular area, depending on how it intersects the	*/
	/* triangular area to paint, is subdivided into rectangles and	*/
	/* (non-clipped) triangles, and each painted in turn.		*/
	/*--------------------------------------------------------------*/

	lr = lhead;
	if (lr != NULL)
	    if (lhead->r_next == NULL)
	    {
		Point start;
		Tile *tile;
		TileType oldType, newType;

	        GeoClip(&lhead->r_r, area);
		start.p_x = area->r_xbot;
		start.p_y = area->r_ytop - 1;
		tile = plane->pl_hint;
		GOTOPOINT(tile, &start);

		/* Ignore tiles that don't interact.  This has	*/
		/* to match the same check done in		*/
		/* DBFracturePlane				*/

		oldType = TiGetLeftType(tile);
		if (resultTbl[oldType] == oldType)
		{
		    oldType = TiGetRightType(tile);
		    if (resultTbl[oldType] == oldType)
		    {
			freeMagic((char *) lr);
			return;
		    }
		}

		oldType = TiGetTypeExact(tile);
		newType = DBDiagonalProc(oldType, &dinfo);

		/* Ignore tiles that don't change type.	*/

		if (newType == oldType)
		{
		    freeMagic((char *) lr);
	            return;
		}

		/* Watch for the worst-case scenario of attempting to	*/
		/* draw a triangle on top of another triangle with the	*/
		/* diagonal in the opposite direction.  This forces	*/
		/* subsplitting into quarters.  But if we're down to	*/
		/* width=1 or height=1, we cannot subdivide any further	*/
		/* so we stop the madness.				*/
		/* 6/8/10: On reflection, it makes more sense to fill	*/
		/* in the 1x1 area instead of leaving it as-is.		*/

		else if (newType == -1)
		{
		    if ((width == 1) || (height == 1))
		    {
			DBPaintPlane(plane, &(lr->r_r), resultTbl, undo);
			freeMagic((char *) lr);
			return;
		    }

		    /* lr->r_r is drawn & quartered */

		    lr->r_r.r_xtop -= (width >> 1);
		    lr->r_r.r_ytop -= (height >> 1);

		    newlr = (LinkedRect *) mallocMagic(sizeof(LinkedRect));
		    newlr->r_r.r_xbot = area->r_xbot;
		    newlr->r_r.r_xtop = lr->r_r.r_xtop;
		    newlr->r_r.r_ybot = lr->r_r.r_ytop;
		    newlr->r_r.r_ytop = area->r_ytop;
		    newlr->r_next = (LinkedRect *)NULL;
		    lr->r_next = newlr;

		    newlr = (LinkedRect *) mallocMagic(sizeof(LinkedRect));
		    newlr->r_r.r_xbot = lr->r_r.r_xtop;
		    newlr->r_r.r_xtop = area->r_xtop;
		    newlr->r_r.r_ybot = area->r_ybot;
		    newlr->r_r.r_ytop = lr->r_r.r_ytop;
		    newlr->r_next = lr->r_next;
		    lr->r_next = newlr;

		    newlr = (LinkedRect *) mallocMagic(sizeof(LinkedRect));
		    newlr->r_r.r_xbot = lr->r_r.r_xtop;
		    newlr->r_r.r_xtop = area->r_xtop;
		    newlr->r_r.r_ybot = lr->r_r.r_ytop;
		    newlr->r_r.r_ytop = area->r_ytop;
		    newlr->r_next = lr->r_next;
		    lr->r_next = newlr;
		}
		else
		{
		    /* Easier case:  Triangles have the diagonal in the	*/
		    /* same direction, so we just find the new paint	*/
		    /* types.  Tile may become a non-split tile, in	*/
		    /* which case we call DBPaintPlane() to make sure	*/
		    /* that the tile is properly merged back into the	*/
		    /* surrounding area, if applicable.			*/

		    if (newType & TT_DIAGONAL)
		    {
			DBPaintPlane(plane, &(lr->r_r), DBSpecialPaintTbl,
				(PaintUndoInfo *)NULL);
			tile = plane->pl_hint;
			GOTOPOINT(tile, &(lr->r_r.r_ll));
			if (undo && UndoIsEnabled())
			{
			    TiSetBody(tile, oldType);
			    DBPAINTUNDO(tile, newType, undo);
			}
			TiSetBody(tile, newType);
		    }
		    else if (method == (unsigned char)PAINT_XOR)
		    {
			PaintResultType tempTbl;
			tempTbl = newType;
			DBPaintPlane0(plane, &(lr->r_r), &tempTbl, undo, method);
		    }
		    else
			DBPaintPlane(plane, &(lr->r_r), resultTbl, undo);

		    freeMagic((char *) lr);
		    /* goto nmmerge; */
	            return;
		}
	    }

	/*------------------------------------------------------*/
	/* Further subdivide the rects so that every rect is	*/
	/* either a (non-clipped) triangle or a rectangle.	*/
	/* Recursively call this function on triangular areas,	*/
	/* and DBPaintPlane() on rectangular areas.  Reject	*/
	/* areas which contain no paint.			*/
	/*------------------------------------------------------*/

	while (lr != NULL)
	{
	    resstate = RES_DIAG;

	    /* Clip to area */
	    GeoClip(&lr->r_r, area);

	    /* Split off left */
	    yref = (dlong)width * (dlong)((dinfo.dir) ?
			lr->r_r.r_ytop - area->r_ytop :
			lr->r_r.r_ybot - area->r_ybot);
	    xc = (((yref % height) << 1) >= height) ? 1 : 0;	/* round */
	    if (dinfo.dir) yref = -yref;
	    xc += area->r_xbot + (int)(yref / (dlong)height);
	    if (xc > lr->r_r.r_xbot && xc < lr->r_r.r_xtop)
	    {
		newlr = (LinkedRect *) mallocMagic(sizeof(LinkedRect));
		newlr->r_r.r_xtop = lr->r_r.r_xtop;
		newlr->r_r.r_xbot = xc;
		newlr->r_r.r_ybot = lr->r_r.r_ybot;
		newlr->r_r.r_ytop = lr->r_r.r_ytop;
		newlr->r_next = lr->r_next;
		lr->r_r.r_xtop = xc;
		lr->r_next = newlr;
		resstate = RES_LEFT;
	    }
	    else if (xc >= lr->r_r.r_xtop)
	    {
		if (dinfo.side == 0) resstate = RES_LEFT;
		else goto nextrect;
	    }
	    if (resstate != RES_DIAG) goto paintrect;

	    /* Split off right */
	    yref = (dlong)width * (dlong)((dinfo.dir) ?
			lr->r_r.r_ybot - area->r_ytop :
			lr->r_r.r_ytop - area->r_ybot);
	    xc = (((yref % height) << 1) >= height) ? 1 : 0;	/* round */
	    if (dinfo.dir) yref = -yref;
	    xc += area->r_xbot + yref / height;
	    if (xc > lr->r_r.r_xbot && xc < lr->r_r.r_xtop)
	    {
		newlr = (LinkedRect *) mallocMagic(sizeof(LinkedRect));
		newlr->r_r.r_xtop = xc;
		newlr->r_r.r_xbot = lr->r_r.r_xbot;
		newlr->r_r.r_ybot = lr->r_r.r_ybot;
		newlr->r_r.r_ytop = lr->r_r.r_ytop;
		newlr->r_next = lr->r_next;
		lr->r_r.r_xbot = xc;
		lr->r_next = newlr;
		resstate = RES_RIGHT;
	    }
	    else if (xc <= lr->r_r.r_xbot)
	    {
		if (dinfo.side == 1) resstate = RES_RIGHT;
		else goto nextrect;
	    }
	    if (resstate != RES_DIAG) goto paintrect;

	    /* Split off top */
	    xref = (dlong)height * (dlong)((dinfo.dir) ?
			lr->r_r.r_xbot - area->r_xtop :
			lr->r_r.r_xtop - area->r_xbot);
	    yc = (((xref % width) << 1) >= width) ? 1 : 0;	/* round */
	    if (dinfo.dir) xref = -xref;
	    yc += area->r_ybot + (int)(xref / (dlong)width);
	    if (yc > lr->r_r.r_ybot && yc < lr->r_r.r_ytop)
	    {
		newlr = (LinkedRect *) mallocMagic(sizeof(LinkedRect));
		newlr->r_r.r_xbot = lr->r_r.r_xbot;
		newlr->r_r.r_xtop = lr->r_r.r_xtop;
		newlr->r_r.r_ytop = yc;
		newlr->r_r.r_ybot = lr->r_r.r_ybot;
		newlr->r_next = lr->r_next;
		lr->r_r.r_ybot = yc;
		lr->r_next = newlr;
		resstate = (dinfo.dir) ? RES_RIGHT : RES_LEFT;
	    }
	    else if (yc <= lr->r_r.r_ybot)
	    {
		if (dinfo.side == dinfo.dir)
		    resstate = (dinfo.side) ? RES_RIGHT : RES_LEFT;
		else goto nextrect;
	    }
	    if (resstate != RES_DIAG) goto paintrect;

	    /* Split off bottom */
	    xref = (dlong)height * (dlong)((dinfo.dir) ?
			lr->r_r.r_xtop - area->r_xtop :
			lr->r_r.r_xbot - area->r_xbot);
	    yc = (((xref % width) << 1) >= width) ? 1 : 0;
	    if (dinfo.dir) xref = -xref;
	    yc += area->r_ybot + xref / width;
	    if (yc > lr->r_r.r_ybot && yc < lr->r_r.r_ytop)
	    {
		newlr = (LinkedRect *) mallocMagic(sizeof(LinkedRect));
		newlr->r_r.r_xbot = lr->r_r.r_xbot;
		newlr->r_r.r_xtop = lr->r_r.r_xtop;
		newlr->r_r.r_ytop = lr->r_r.r_ytop;
		newlr->r_r.r_ybot = yc;
		newlr->r_next = lr->r_next;
		lr->r_r.r_ytop = yc;
		lr->r_next = newlr;
		resstate = (dinfo.dir) ? RES_LEFT : RES_RIGHT;
	    }
	    else if (yc >= lr->r_r.r_ytop)
	    {
		if (dinfo.side != dinfo.dir)
		    resstate = (dinfo.side) ? RES_RIGHT : RES_LEFT;
		else goto nextrect;
	    }

paintrect:
	    if (resstate == RES_DIAG)
	    {
		/* Recursive call to self on sub-area */
		DBNMPaintPlane0(plane, exacttype, &(lr->r_r), resultTbl, undo, method);
	    }
	    else if ((resstate == RES_LEFT && !dinfo.side) ||
		     (resstate == RES_RIGHT && dinfo.side)) {
		DBPaintPlane(plane, &(lr->r_r), resultTbl, undo);
	    }
	    /* else: Rectangle does not contain type and should be ignored. */
nextrect:
	    lr = lr->r_next;
	}

	lr = lhead;
	while (lr != NULL)
	{
	    freeMagic((char *) lr);
	    lr = lr->r_next;
	}
    }
    else
	DBPaintPlane0(plane, area, resultTbl, undo, (method == PAINT_MARK) ?
		method : PAINT_NORMAL);
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbNMEnumFunc --
 *
 *   Routine which makes a linked list of all the tiles found in the search
 *   area.
 *
 * ----------------------------------------------------------------------------
 */

int
dbNMEnumFunc(tile, arg)
    Tile *tile;
    LinkedRect **arg;
{
    LinkedRect *lr;

    /* Ignore the second call to any diagonal---only count once! */
    if (IsSplit(tile) && SplitSide(tile)) return 0;

    lr = (LinkedRect *) mallocMagic(sizeof(LinkedRect));
    TiToRect(tile, &lr->r_r);

    lr->r_next = (*arg);
    (*arg) = lr;

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbMarkClient --
 *
 *	Mark a tile's client record for use in tracking if this tile has
 *	been handled in this pass of dbPaintPlane.  If the tile's area
 *	is outside of "clip", then we don't mark it, or else it will be
 *	missed during cleanup.
 *
 * ----------------------------------------------------------------------------
 */

void
dbMarkClient(tile, clip)
    Tile *tile;
    Rect *clip;
{
    if (LEFT(tile) < clip->r_xtop &&
		RIGHT(tile) > clip->r_xbot &&
		BOTTOM(tile) < clip->r_ytop &&
		TOP(tile) > clip->r_ybot)
	tile->ti_client = (ClientData)1;
    else
	tile->ti_client = (ClientData)CLIENTDEFAULT;
}


/*
 * ----------------------------------------------------------------------------
 *
 * dbPaintMerge -- 
 *
 * The tile 'tp' is to be changed to type 'newtype'.  To maintain
 * maximal horizontal strips, it may be necessary to merge the new
 * 'tp' with its neighbors.
 *
 * This procedure splits off the biggest segment along the top of the
 * tile 'tp' that can be merged with its neighbors to the left and right
 * (depending on which of MRG_LEFT and MRG_RIGHT are set in the merge flags),
 * then changes the type of 'tp' to 'newtype' and merges to the left, right,
 * top, and bottom (in that order).
 *
 * Results:
 *	Returns a pointer to the topmost tile resulting from any splits
 *	and merges of the original tile 'tp'.  By the maximal horizontal
 *	strip property and the fact that the original tile 'tp' gets
 *	painted a single color, we know that this topmost resulting tile
 *	extends across the entire top of the area occupied by 'tp'.
 *
 *	NOTE: the only tile whose type is changed is 'tp'.  Any tiles
 *	resulting from splits below this tile will not have had their
 *	types changed.
 *
 * Side effects:
 *	Modifies the database plane that contains the given tile.
 *
 * THIS IS SLOW, SO SHOULD BE AVOIDED IF AT ALL POSSIBLE.
 * THE CODE ABOVE GOES TO GREAT LENGTHS TO DO SO.
 *
 * ----------------------------------------------------------------------------
 */

Tile *
dbPaintMerge(tile, newType, area, plane, mergeFlags, undo, mark)
    Tile *tile;	/* Tile to be merged with its neighbors */
    TileType newType;	/* Type to which we will change 'tile' */
    Rect *area;			/* Original area painted, needed for marking */
    Plane *plane;		/* Plane on which this resides */
    int mergeFlags;		/* Specify which directions to merge */
    PaintUndoInfo *undo;	/* See DBPaintPlane() above */
    bool mark;			/* Mark tiles that were processed */
{
    Tile *tp, *tpLast;
    int ysplit;

    ysplit = BOTTOM(tile);
    if (mergeFlags & MRG_LEFT)
    {
	/*
	 * Find the split point along the LHS of tile.
	 * If the topmost tile 'tp' along the LHS is of type 'newType'
	 * the split point will be no lower than the bottom of 'tp'.
	 * If the topmost tile is NOT of type 'newType', then the split
	 * point will be no lower than the top of the first tile along
	 * the LHS that is of type 'newType'.
	 *
	 * NOTE:  This code depends on the maximum horizontal stripes rule!
	 */
	for (tpLast = NULL, tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
	    if (TiGetTypeExact(tp) == newType)
		tpLast = tp;

	/* If the topmost LHS tile is not of type 'newType', we don't merge */
	if (tpLast == NULL || TOP(tpLast) < TOP(tile))
	{
	    mergeFlags &= ~MRG_LEFT;
	    if (tpLast && TOP(tpLast) > ysplit) ysplit = TOP(tpLast);
	}
	else if (BOTTOM(tpLast) > ysplit) ysplit = BOTTOM(tpLast);
    }

    if (mergeFlags & MRG_RIGHT)
    {
	/*
	 * Find the split point along the RHS of 'tile'.
	 * If the topmost tile 'tp' along the RHS is of type 'newType'
	 * the split point will be no lower than the bottom of 'tp'.
	 * If the topmost tile is NOT of type 'newType', then the split
	 * point will be no lower than the top of the first tile along
	 * the RHS that is of type 'newType'.
	 *
	 * NOTE:  This code depends on the maximum horizontal stripes rule!
	 */
	tp = TR(tile);
	if (TiGetTypeExact(tp) == newType)
	{
	    if (BOTTOM(tp) > ysplit) ysplit = BOTTOM(tp);
	}
	else
	{
	    /* Topmost RHS tile is not of type 'newType', so don't merge */
	    do
		tp = LB(tp);
	    while (TiGetTypeExact(tp) != newType && TOP(tp) > ysplit);
	    if (TOP(tp) > ysplit) ysplit = TOP(tp);
	    mergeFlags &= ~MRG_RIGHT;
	}
    }

    /*
     * If 'tile' must be split horizontally, do so.
     * Any merging to the bottom will be delayed until the split-off
     * bottom tile is processed on a subsequent iteration of the area
     * enumeration loop in DBPaintPlane().
     */
    if (ysplit > BOTTOM(tile))
    {
	mergeFlags &= ~MRG_BOTTOM;
	tp = TiSplitY(tile, ysplit);
	TiSetBody(tp, TiGetTypeExact(tile));
	tile = tp;
#ifdef	PAINTDEBUG
	if (dbPaintDebug)
	    dbPaintShowTile(tile, undo, "(DBMERGE) after split");
#endif	/* PAINTDEBUG */
    }

    /*
     * Set the type of the new tile.
     * Record any undo information.
     */
    if (undo && TiGetTypeExact(tile) != newType && UndoIsEnabled())
	DBPAINTUNDO(tile, newType, undo);

    TiSetBody(tile, newType);
    if (mark) dbMarkClient(tile, area);

#ifdef	PAINTDEBUG
    if (dbPaintDebug)
	dbPaintShowTile(tile, undo, "(DBMERGE) changed type");
#endif	/* PAINTDEBUG */

    /*
     * Do the merging.
     * We are guaranteed that at most one tile abuts 'tile' on
     * any side that we will merge to, and that this tile is
     * of type 'newType'.
     */
    if (mergeFlags & MRG_LEFT)
    {
	tp = BL(tile);
	if (TOP(tp) > TOP(tile))
	{
	    tpLast = TiSplitY(tp, TOP(tile));
	    TiSetBody(tpLast, newType);
	    if (mark) dbMarkClient(tile, area);
	}
	if (BOTTOM(tp) < BOTTOM(tile)) tp = TiSplitY(tp, BOTTOM(tile));
	TiJoinX(tile, tp, plane);
#ifdef	PAINTDEBUG
	if (dbPaintDebug)
	    dbPaintShowTile(tile, undo, "(DBMERGE) merged left");
#endif	/* PAINTDEBUG */
    }
    if (mergeFlags & MRG_RIGHT)
    {
	tp = TR(tile);
	if (TOP(tp) > TOP(tile))
	{
	    tpLast = TiSplitY(tp, TOP(tile));
	    TiSetBody(tpLast, newType);
	    if (mark) dbMarkClient(tile, area);
	}
	if (BOTTOM(tp) < BOTTOM(tile)) tp = TiSplitY(tp, BOTTOM(tile));
	TiJoinX(tile, tp, plane);
#ifdef	PAINTDEBUG
	if (dbPaintDebug)
	    dbPaintShowTile(tile, undo, "(DBMERGE) merged right");
#endif	/* PAINTDEBUG */
    }
    if (mergeFlags&MRG_TOP)
    {
	tp = RT(tile);
	if (CANMERGE_Y(tp, tile)) TiJoinY(tile, tp, plane);
#ifdef	PAINTDEBUG
	if (dbPaintDebug)
	    dbPaintShowTile(tile, undo, "(DBMERGE) merged up");
#endif	/* PAINTDEBUG */
    }
    if (mergeFlags&MRG_BOTTOM)
    {
	tp = LB(tile);
	if (CANMERGE_Y(tp, tile)) TiJoinY(tile, tp, plane);
#ifdef	PAINTDEBUG
	if (dbPaintDebug)
	    dbPaintShowTile(tile, undo, "(DBMERGE) merged down");
#endif	/* PAINTDEBUG */
    }

    return (tile);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBPaintType --
 *
 * Paint a rectangular area ('area') of type ('newType') on plane ('plane').
 * Merge only with neighbors of the same type and client data.
 *
 * If undo is desired, 'undo' should point to a PaintUndoInfo struct
 * that contains everything needed to build an undo record.  Otherwise,
 * 'undo' can be NULL.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the database plane that contains the given tile.
 *
 * REMINDER:
 *	Callers should remember to set the CDMODIFIED and CDGETNEWSTAMP
 *	bits in the cell definition containing the plane being painted.
 *
 * ----------------------------------------------------------------------------
 */

void
DBPaintType(plane, area, resultTbl, client, undo, tileMask)
    Plane *plane;		/* Plane whose paint is to be modified */
    Rect *area;	/* Area to be changed */
    PaintResultType *resultTbl;	/* Table, indexed by the type of tile already
				 * present in the plane, giving the type to
				 * which the existing tile must change as a
				 * result of this paint operation.
				 */
    ClientData client;		/* ClientData for tile	*/
    PaintUndoInfo *undo;	/* Record containing everything needed to
				 * save undo entries for this operation.
				 * If NULL, the undo package is not used.
				 */
    TileTypeBitMask *tileMask;	/* Mask of un-mergable tile types */
{
    Point start;
    int clipTop, mergeFlags;
    TileType oldType;
    Tile *tile, *tpnew;	/* Used for area search */
    Tile *newtile, *tp;	/* Used for paint */
    TileType newType;			/* Type of new tile to be painted */


    if (area->r_xtop <= area->r_xbot || area->r_ytop <= area->r_ybot)
	return;

    /*
     * The following is a modified version of the area enumeration
     * algorithm.  It expects the in-line paint code below to leave
     * 'tile' pointing to the tile from which we should continue the
     * search.
     */

    start.p_x = area->r_xbot;
    start.p_y = area->r_ytop - 1;
    tile = plane->pl_hint;
    GOTOPOINT(tile, &start);

    /* Each iteration visits another tile on the LHS of the search area */
    while (TOP(tile) > area->r_ybot)
    {
	/***
	 *** AREA SEARCH.
	 *** Each iteration enumerates another tile.
	 ***/
enumerate:

	clipTop = TOP(tile);
	if (clipTop > area->r_ytop) clipTop = area->r_ytop;

#ifdef	PAINTDEBUG
	if (dbPaintDebug)
	    dbPaintShowTile(tile, undo, "area enum");
#endif	/* PAINTDEBUG */

	/***
	 *** ---------- THE FOLLOWING IS IN-LINE PAINT CODE ----------
	 ***/

	/*
	 * Set up the directions in which we will have to
	 * merge initially.  Clipping can cause some of these
	 * to be turned off.
	 */
	mergeFlags = MRG_TOP | MRG_LEFT;
	if (RIGHT(tile) >= area->r_xtop) mergeFlags |= MRG_RIGHT;
	if (BOTTOM(tile) <= area->r_ybot) mergeFlags |= MRG_BOTTOM;

	/*
	 * Map tile types using the *resultTbl* table.
	 * If the client field of the existing tile differs
	 * from the given client, ignore the type of the existing
	 * tile and treat as painting over space.
	 */

	oldType = TiGetTypeExact(tile);
	if ( TiGetClient(tile) == client )
	    newType = resultTbl[oldType];
	else
	{
	    if ( oldType != TT_SPACE )
		/*DEBUG*/ TxPrintf("Overwrite tile type %d\n",oldType);
	    newType = resultTbl[TT_SPACE];
	}

	if (oldType != newType)
	{
	    /*
	     * Clip the tile against the clipping rectangle.
	     * Merging is only necessary if we clip to the left or to
	     * the right, and then only to the top or the bottom.
	     * We do the merge in-line for efficiency.
	     */

	    /* Clip up */
	    if (TOP(tile) > area->r_ytop)
	    {
		newtile = TiSplitY(tile, area->r_ytop);
		TiSetBody(newtile, TiGetBody(tile));
		TiSetClient(newtile, TiGetClient(tile));
		mergeFlags &= ~MRG_TOP;
	    }

	    /* Clip down */
	    if (BOTTOM(tile) < area->r_ybot)
	    {
		newtile = tile, tile = TiSplitY(tile, area->r_ybot);
		TiSetBody(tile, TiGetBody(newtile));
		TiSetClient(tile, TiGetClient(newtile));
		mergeFlags &= ~MRG_BOTTOM;
	    }

	    /* Clip right */
	    if (RIGHT(tile) > area->r_xtop)
	    {
		TISPLITX(newtile, tile, area->r_xtop);
		TiSetBody(newtile, TiGetBody(tile));
		TiSetClient(newtile, TiGetClient(tile));
		mergeFlags &= ~MRG_RIGHT;

		/* Merge the outside tile to its top */
		tp = RT(newtile);
		if (CANMERGE_Y(newtile, tp) &&
			( (TiGetClient(tp) == TiGetClient(newtile)) ||
			( ! TTMaskHasType(tileMask, TiGetTypeExact(tp)) ) ) )
			    TiJoinY(newtile, tp, plane);

		/* Merge the outside tile to its bottom */
		tp = LB(newtile);
		if (CANMERGE_Y(newtile, tp) &&
			( (TiGetClient(tp) == TiGetClient(newtile)) ||
			( ! TTMaskHasType(tileMask, TiGetTypeExact(tp)) ) ) )
			    TiJoinY(newtile, tp, plane);
	    }

	    /* Clip left */
	    if (LEFT(tile) < area->r_xbot)
	    {
		newtile = tile;
		TISPLITX(tile, tile, area->r_xbot);
		TiSetBody(tile, TiGetBody(newtile));
		TiSetClient(tile, TiGetClient(newtile));
		mergeFlags &= ~MRG_LEFT;

		/* Merge the outside tile to its top */
		tp = RT(newtile);
		if (CANMERGE_Y(newtile, tp) &&
			( (TiGetClient(tp) == TiGetClient(newtile)) ||
			( ! TTMaskHasType(tileMask, TiGetTypeExact(tp)) ) ) )
			TiJoinY(newtile, tp, plane);

		/* Merge the outside tile to its bottom */
		tp = LB(newtile);
		if (CANMERGE_Y(newtile, tp) &&
			( (TiGetClient(tp) == TiGetClient(newtile)) ||
			( ! TTMaskHasType(tileMask, TiGetTypeExact(tp)) ) ) )
			TiJoinY(newtile, tp, plane);
	    }

#ifdef	PAINTDEBUG
	    if (dbPaintDebug)
		dbPaintShowTile(tile, undo, "after clip");
#endif	/* PAINTDEBUG */
	}

	/*
	 * Merge the tile back into the parts of the plane that have
	 * already been visited.  Note that if we clipped in a particular
	 * direction we avoid merging in that direction.
	 *
	 * We avoid calling dbPaintMerge if at all possible.
	 */
	if (mergeFlags & MRG_LEFT)
	{
	    for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
		if ( (TiGetTypeExact(tp) == newType) && (tp->ti_client == client) )
		{
		    tile = dbMergeType(tile, newType, plane, mergeFlags, undo, client);
		    goto paintdone;
		}
	    mergeFlags &= ~MRG_LEFT;
	}
	if (mergeFlags & MRG_RIGHT)
	{
	    for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp = LB(tp))
		if ( (TiGetTypeExact(tp) == newType) && (tp->ti_client == client) )
		{
		    tile = dbMergeType(tile, newType, plane, mergeFlags, undo, client);
		    goto paintdone;
		}
	    mergeFlags &= ~MRG_RIGHT;
	}

	/*
	 * Cheap and dirty merge -- we don't have to merge to the
	 * left or right, so the top/bottom merge is very fast.
	 *
	 * Now it's safe to change the type of this tile, and
	 * record the event on the undo list.
	 */
	if (undo && oldType != newType && UndoIsEnabled())
	    DBPAINTUNDO(tile, newType, undo);

	TiSetBody(tile, newType);
	TiSetClient(tile, client);

#ifdef	PAINTDEBUG
	if (dbPaintDebug)
	    dbPaintShowTile(tile, undo, "changed type");
#endif	/* PAINTDEBUG */

	if (mergeFlags & MRG_TOP)
	{
	    tp = RT(tile);
	    if (CANMERGE_Y(tile, tp) && (tp->ti_client == client))
		TiJoinY(tile, tp, plane);
#ifdef	PAINTDEBUG
	    if (dbPaintDebug)
		dbPaintShowTile(tile, undo, "merged up (CHEAP)");
#endif	/* PAINTDEBUG */
	}
	if (mergeFlags & MRG_BOTTOM)
	{
	    tp = LB(tile);
	    if (CANMERGE_Y(tile, tp) && (tp->ti_client == client))
		TiJoinY(tile, tp, plane);
#ifdef	PAINTDEBUG
	    if (dbPaintDebug)
		dbPaintShowTile(tile, undo, "merged down (CHEAP)");
#endif	/* PAINTDEBUG */
	}


	/***
	 ***		END OF PAINT CODE
	 *** ---------- BACK TO AREA SEARCH ----------
	 ***/
paintdone:
	/* Move right if possible */
	tpnew = TR(tile);
	if (LEFT(tpnew) < area->r_xtop)
	{
	    /* Move back down into clipping area if necessary */
	    while (BOTTOM(tpnew) >= clipTop) tpnew = LB(tpnew);
	    if (BOTTOM(tpnew) >= BOTTOM(tile) || BOTTOM(tile) <= area->r_ybot)
	    {
		tile = tpnew;
		goto enumerate;
	    }
	}

	/* Each iteration returns one tile further to the left */
	while (LEFT(tile) > area->r_xbot)
	{
	    /* Move left if necessary */
	    if (BOTTOM(tile) <= area->r_ybot)
		goto done;

	    /* Move down if possible; left otherwise */
	    tpnew = LB(tile); tile = BL(tile);
	    if (BOTTOM(tpnew) >= BOTTOM(tile) || BOTTOM(tile) <= area->r_ybot)
	    {
		tile = tpnew;
		goto enumerate;
	    }
	}
	/* At left edge -- walk down to next tile along the left edge */
	for (tile = LB(tile); RIGHT(tile) <= area->r_xbot; tile = TR(tile))
	    /* Nothing */;
    }

done:
    plane->pl_hint = tile;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbMergeType -- 
 *
 * The tile 'tp' is to be changed to type 'newtype'.  To maintain
 * maximal horizontal strips, it may be necessary to merge the new
 * 'tp' with its neighbors.
 *
 * This procedure splits off the biggest segment along the top of the
 * tile 'tp' that can be merged with its neighbors to the left and right
 * (depending on which of MRG_LEFT and MRG_RIGHT are set in the merge flags),
 * then changes the type of 'tp' to 'newtype' and merges to the left, right,
 * top, and bottom (in that order).
 *
 * Results:
 *	Returns a pointer to the topmost tile resulting from any splits
 *	and merges of the original tile 'tp'.  By the maximal horizontal
 *	strip property and the fact that the original tile 'tp' gets
 *	painted a single color, we know that this topmost resulting tile
 *	extends across the entire top of the area occupied by 'tp'.
 *
 *	NOTE: the only tile whose type is changed is 'tp'.  Any tiles
 *	resulting from splits below this tile will not have had their
 *	types changed.
 *
 * Side effects:
 *	Modifies the database plane that contains the given tile.
 *
 * THIS IS SLOW, SO SHOULD BE AVOIDED IF AT ALL POSSIBLE.
 * THE CODE ABOVE GOES TO GREAT LENGTHS TO DO SO.
 *
 * ----------------------------------------------------------------------------
 */

Tile *
dbMergeType(tile, newType, plane, mergeFlags, undo, client)
    Tile *tile;	/* Tile to be merged with its neighbors */
    TileType newType;	/* Type to which we will change 'tile' */
    Plane *plane;		/* Plane on which this resides */
    int mergeFlags;		/* Specify which directions to merge */
    PaintUndoInfo *undo;	/* See DBPaintPlane() above */
    ClientData client;
{
    Tile *tp, *tpLast;
    int ysplit;

    ysplit = BOTTOM(tile);
    if (mergeFlags & MRG_LEFT)
    {
	/*
	 * Find the split point along the LHS of tile.
	 * If the topmost tile 'tp' along the LHS is of type 'newType'
	 * the split point will be no lower than the bottom of 'tp'.
	 * If the topmost tile is NOT of type 'newType', then the split
	 * point will be no lower than the top of the first tile along
	 * the LHS that is of type 'newType'.
	 */
	for (tpLast = NULL, tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
	    if ((TiGetTypeExact(tp) == newType) && (tp->ti_client == client) )
		tpLast = tp;

	/* If the topmost LHS tile is not of type 'newType', we don't merge */
	if (tpLast == NULL || TOP(tpLast) < TOP(tile))
	{
	    mergeFlags &= ~MRG_LEFT;
	    if (tpLast && TOP(tpLast) > ysplit) ysplit = TOP(tpLast);
	}
	else if (BOTTOM(tpLast) > ysplit) ysplit = BOTTOM(tpLast);
    }

    if (mergeFlags & MRG_RIGHT)
    {
	/*
	 * Find the split point along the RHS of 'tile'.
	 * If the topmost tile 'tp' along the RHS is of type 'newType'
	 * the split point will be no lower than the bottom of 'tp'.
	 * If the topmost tile is NOT of type 'newType', then the split
	 * point will be no lower than the top of the first tile along
	 * the RHS that is of type 'newType'.
	 */
	tp = TR(tile);
	if ((TiGetTypeExact(tp) == newType) && (tp->ti_client == client))
	{
	    if (BOTTOM(tp) > ysplit) ysplit = BOTTOM(tp);
	}
	else
	{
	    /* Topmost RHS tile is not of type 'newType', so don't merge */
	    do
		tp = LB(tp);
	    while (TiGetTypeExact(tp) != newType && TOP(tp) > ysplit);
	    if (TOP(tp) > ysplit) ysplit = TOP(tp);
	    mergeFlags &= ~MRG_RIGHT;
	}
    }

    /*
     * If 'tile' must be split horizontally, do so.
     * Any merging to the bottom will be delayed until the split-off
     * bottom tile is processed on a subsequent iteration of the area
     * enumeration loop in DBPaintPlane().
     */
    if (ysplit > BOTTOM(tile))
    {
	mergeFlags &= ~MRG_BOTTOM;
	tp = TiSplitY(tile, ysplit);
	TiSetBody(tp, TiGetTypeExact(tile));
	TiSetClient(tp, TiGetClient(tile));
	tile = tp;
#ifdef	PAINTDEBUG
	if (dbPaintDebug)
	    dbPaintShowTile(tile, undo, "(DBMERGE) after split");
#endif	/* PAINTDEBUG */
    }

    /*
     * Set the type of the new tile.
     * Record any undo information.
     */
    if (undo && TiGetTypeExact(tile) != newType && UndoIsEnabled())
	DBPAINTUNDO(tile, newType, undo);
    TiSetBody(tile, newType);
    TiSetClient(tile, client);
#ifdef	PAINTDEBUG
    if (dbPaintDebug)
	dbPaintShowTile(tile, undo, "(DBMERGE) changed type");
#endif	/* PAINTDEBUG */

    /*
     * Do the merging.
     * We are guaranteed that at most one tile abuts 'tile' on
     * any side that we will merge to, and that this tile is
     * of type 'newType'.
     */
    if (mergeFlags & MRG_LEFT)
    {
	tp = BL(tile);
	if (TOP(tp) > TOP(tile))
	{
	    tpLast = TiSplitY(tp, TOP(tile));
	    TiSetBody(tpLast, newType);
	    TiSetClient(tpLast, client);
	}
	if (BOTTOM(tp) < BOTTOM(tile)) tp = TiSplitY(tp, BOTTOM(tile));
	TiJoinX(tile, tp, plane);
#ifdef	PAINTDEBUG
	if (dbPaintDebug)
	    dbPaintShowTile(tile, undo, "(DBMERGE) merged left");
#endif	/* PAINTDEBUG */
    }
    if (mergeFlags & MRG_RIGHT)
    {
	tp = TR(tile);
	if (TOP(tp) > TOP(tile))
	{
	    tpLast = TiSplitY(tp, TOP(tile));
	    TiSetBody(tpLast, newType);
	    TiSetClient(tpLast, client);
	}
	if (BOTTOM(tp) < BOTTOM(tile)) tp = TiSplitY(tp, BOTTOM(tile));
	TiJoinX(tile, tp, plane);
#ifdef	PAINTDEBUG
	if (dbPaintDebug)
	    dbPaintShowTile(tile, undo, "(DBMERGE) merged right");
#endif	/* PAINTDEBUG */
    }
    if (mergeFlags&MRG_TOP)
    {
	tp = RT(tile);
	if (CANMERGE_Y(tp, tile) && (tp->ti_client == client)) TiJoinY(tile, tp, plane);
#ifdef	PAINTDEBUG
	if (dbPaintDebug)
	    dbPaintShowTile(tile, undo, "(DBMERGE) merged up");
#endif	/* PAINTDEBUG */
    }
    if (mergeFlags&MRG_BOTTOM)
    {
	tp = LB(tile);
	if (CANMERGE_Y(tp, tile) && (tp->ti_client == client)) TiJoinY(tile, tp, plane);
#ifdef	PAINTDEBUG
	if (dbPaintDebug)
	    dbPaintShowTile(tile, undo, "(DBMERGE) merged down");
#endif	/* PAINTDEBUG */
    }

    return (tile);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBPaintPlaneVert --
 *
 * Paint a rectangular area ('area') on a single tile plane ('plane').
 *
 * --------------------------------------------------------------------
 * This is identical to DBPaintPlane above, except we merge in maximal
 * VERTICAL strips instead of maximal HORIZONTAL.  See the comments for
 * DBPaintPlane for details.
 * --------------------------------------------------------------------
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the database plane that contains the given tile.
 *
 * REMINDER:
 *	Callers should remember to set the CDMODIFIED and CDGETNEWSTAMP
 *	bits in the cell definition containing the plane being painted.
 *
 * ----------------------------------------------------------------------------
 */

void
DBPaintPlaneVert(plane, area, resultTbl, undo)
    Plane *plane;		/* Plane whose paint is to be modified */
    Rect *area;	/* Area to be changed */
    PaintResultType *resultTbl;	/* Table, indexed by the type of tile already
				 * present in the plane, giving the type to
				 * which the existing tile must change as a
				 * result of this paint operation.
				 */
    PaintUndoInfo *undo;	/* Record containing everything needed to
				 * save undo entries for this operation.
				 * If NULL, the undo package is not used.
				 */
{
    Point start;
    int clipTop, mergeFlags;
    TileType oldType, newType;
    Tile *tile, *tpnew;	/* Used for area search */
    Tile *newtile, *tp;	/* Used for paint */

    if (area->r_xtop <= area->r_xbot || area->r_ytop <= area->r_ybot)
	return;

    /*
     * The following is a modified version of the area enumeration
     * algorithm.  It expects the in-line paint code below to leave
     * 'tile' pointing to the tile from which we should continue the
     * search.
     */

    start.p_x = area->r_xbot;
    start.p_y = area->r_ytop - 1;
    tile = plane->pl_hint;
    GOTOPOINT(tile, &start);

    /* Each iteration visits another tile on the LHS of the search area */
    while (TOP(tile) > area->r_ybot)
    {
	/***
	 *** AREA SEARCH.
	 *** Each iteration enumerates another tile.
	 ***/
enumerate:
	if (SigInterruptPending)
	    break;

	clipTop = TOP(tile);
	if (clipTop > area->r_ytop) clipTop = area->r_ytop;
	oldType = TiGetTypeExact(tile);

#ifdef	PAINTDEBUG
	if (dbPaintDebug)
	    dbPaintShowTile(tile, undo, "area enum");
#endif	/* PAINTDEBUG */

	/***
	 *** ---------- THE FOLLOWING IS IN-LINE PAINT CODE ----------
	 ***/

	/*
	 * Set up the directions in which we will have to
	 * merge initially.  Clipping can cause some of these
	 * to be turned off.
	 */
	mergeFlags = MRG_TOP | MRG_LEFT;
	if (RIGHT(tile) >= area->r_xtop) mergeFlags |= MRG_RIGHT;
	if (BOTTOM(tile) <= area->r_ybot) mergeFlags |= MRG_BOTTOM;

	/*
	 * Determine new type of this tile.
	 * Change the type if necessary.
	 */
	newType = resultTbl[oldType];
	if (oldType != newType)
	{
	    /*
	     * Clip the tile against the clipping rectangle.
	     * Merging is only necessary if we clip to the top or to
	     * the bottom, and then only to the left or the right.
	     *
	     * *** REMEMBER, THESE ARE MAXIMAL VERTICAL STRIPS HERE ***
	     *
	     * We do the merge in-line for efficiency.
	     */

	    /* Clip right */
	    if (RIGHT(tile) > area->r_xtop)
	    {
		TISPLITX(newtile, tile, area->r_xtop);
		TiSetBody(newtile, TiGetBody(tile));
		mergeFlags &= ~MRG_RIGHT;
	    }

	    /* Clip left */
	    if (LEFT(tile) < area->r_xbot)
	    {
		newtile = tile;
		TISPLITX(tile, tile, area->r_xbot);
		TiSetBody(tile, TiGetBody(newtile));
		mergeFlags &= ~MRG_LEFT;
	    }

	    /* Clip up */
	    if (TOP(tile) > area->r_ytop)
	    {
		newtile = TiSplitY(tile, area->r_ytop);
		TiSetBody(newtile, TiGetBody(tile));
		mergeFlags &= ~MRG_TOP;

		/* Merge the outside tile to its left */
		tp = BL(newtile);
		if (CANMERGE_X(newtile, tp)) TiJoinX(newtile, tp, plane);

		/* Merge the outside tile to its right */
		tp = TR(newtile);
		if (CANMERGE_X(newtile, tp)) TiJoinX(newtile, tp, plane);
	    }

	    /* Clip down */
	    if (BOTTOM(tile) < area->r_ybot)
	    {
		newtile = tile, tile = TiSplitY(tile, area->r_ybot);
		TiSetBody(tile, TiGetBody(newtile));
		mergeFlags &= ~MRG_BOTTOM;

		/* Merge the outside tile to its left */
		tp = BL(newtile);
		if (CANMERGE_X(newtile, tp)) TiJoinX(newtile, tp, plane);

		/* Merge the outside tile to its right */
		tp = TR(newtile);
		if (CANMERGE_X(newtile, tp)) TiJoinX(newtile, tp, plane);
	    }

#ifdef	PAINTDEBUG
	    if (dbPaintDebug)
		dbPaintShowTile(tile, undo, "after clip");
#endif	/* PAINTDEBUG */
	}

	/*
	 * Merge the tile back into the parts of the plane that have
	 * already been visited.  Note that if we clipped in a particular
	 * direction we avoid merging in that direction.
	 *
	 * We avoid calling dbPaintMerge if at all possible.
	 */
	if (mergeFlags & MRG_BOTTOM)
	{
	    for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp = TR(tp))
		if (TiGetTypeExact(tp) == newType)
		{
		    tile = dbPaintMergeVert(tile, newType, plane, mergeFlags,
						undo);
		    goto paintdone;
		}
	    mergeFlags &= ~MRG_BOTTOM;
	}
	if (mergeFlags & MRG_TOP)
	{
	    for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp = BL(tp))
		if (TiGetTypeExact(tp) == newType)
		{
		    tile = dbPaintMergeVert(tile, newType, plane, mergeFlags,
						undo);
		    goto paintdone;
		}
	    mergeFlags &= ~MRG_TOP;
	}

	/*
	 * Cheap and dirty merge -- we don't have to merge to the
	 * top or bottom, so the left/right merge is very fast.
	 *
	 * Now it's safe to change the type of this tile, and
	 * record the event on the undo list.
	 */
	if (undo && oldType != newType && UndoIsEnabled())
	    DBPAINTUNDO(tile, newType, undo);
	TiSetBody(tile, newType);

#ifdef	PAINTDEBUG
	if (dbPaintDebug)
	    dbPaintShowTile(tile, undo, "changed type");
#endif	/* PAINTDEBUG */

	if (mergeFlags & MRG_LEFT)
	{
	    tp = BL(tile);
	    if (CANMERGE_X(tile, tp)) TiJoinX(tile, tp, plane);
#ifdef	PAINTDEBUG
	    if (dbPaintDebug)
		dbPaintShowTile(tile, undo, "merged left (CHEAP)");
#endif	/* PAINTDEBUG */
	}
	if (mergeFlags & MRG_RIGHT)
	{
	    tp = TR(tile);
	    if (CANMERGE_X(tile, tp)) TiJoinX(tile, tp, plane);
#ifdef	PAINTDEBUG
	    if (dbPaintDebug)
		dbPaintShowTile(tile, undo, "merged right (CHEAP)");
#endif	/* PAINTDEBUG */
	}


	/***
	 ***		END OF PAINT CODE
	 *** ---------- BACK TO AREA SEARCH ----------
	 ***/
paintdone:
	/* Move right if possible */
	tpnew = TR(tile);
	if (LEFT(tpnew) < area->r_xtop)
	{
	    /* Move back down into clipping area if necessary */
	    while (BOTTOM(tpnew) >= clipTop) tpnew = LB(tpnew);
	    if (BOTTOM(tpnew) >= BOTTOM(tile) || BOTTOM(tile) <= area->r_ybot)
	    {
		tile = tpnew;
		goto enumerate;
	    }
	}

	/* Each iteration returns one tile further to the left */
	while (LEFT(tile) > area->r_xbot)
	{
	    /* Move left if necessary */
	    if (BOTTOM(tile) <= area->r_ybot)
		goto done;

	    /* Move down if possible; left otherwise */
	    tpnew = LB(tile); tile = BL(tile);
	    if (BOTTOM(tpnew) >= BOTTOM(tile) || BOTTOM(tile) <= area->r_ybot)
	    {
		tile = tpnew;
		goto enumerate;
	    }
	}
	/* At left edge -- walk down to next tile along the left edge */
	for (tile = LB(tile); RIGHT(tile) <= area->r_xbot; tile = TR(tile))
	    /* Nothing */;
    }

done:
    plane->pl_hint = tile;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbPaintMergeVert -- 
 *
 * The tile 'tp' is to be changed to type 'newtype'.  To maintain
 * maximal vertical strips, it may be necessary to merge the new
 * 'tp' with its neighbors.
 *
 * --------------------------------------------------------------------
 * This is identical to dbPaintMerge above, except we merge in maximal
 * VERTICAL strips instead of maximal HORIZONTAL.  See the comments for
 * dbPaintMerge for details.
 * --------------------------------------------------------------------
 *
 * This procedure splits off the biggest segment along the left of the
 * tile 'tp' that can be merged with its neighbors to the top and bottom
 * (depending on which of MRG_TOP and MRG_BOTTOM are set in the merge flags),
 * then changes the type of 'tp' to 'newtype' and merges to the top, bottom,
 * left, and right (in that order).
 *
 * Results:
 *	Returns a pointer to the leftmost tile resulting from any splits
 *	and merges of the original tile 'tp'.  By the maximal vertical
 *	strip property and the fact that the original tile 'tp' gets
 *	painted a single color, we know that this leftmost resulting tile
 *	extends across the entire LHS of the area occupied by 'tp'.
 *
 *	NOTE: the only tile whose type is changed is 'tp'.  Any tiles
 *	resulting from splits to the right of this tile will not have
 *	had their types changed.
 *
 * Side effects:
 *	Modifies the database plane that contains the given tile.
 *
 * THIS IS SLOW, SO SHOULD BE AVOIDED IF AT ALL POSSIBLE.
 * THE CODE ABOVE GOES TO GREAT LENGTHS TO DO SO.
 *
 * ----------------------------------------------------------------------------
 */

Tile *
dbPaintMergeVert(tile, newType, plane, mergeFlags, undo)
    Tile *tile;	/* Tile to be merged with its neighbors */
    TileType newType;	/* Type to which we will change 'tile' */
    Plane *plane;		/* Plane on which this resides */
    int mergeFlags;		/* Specify which directions to merge */
    PaintUndoInfo *undo;	/* See DBPaintPlane() above */
{
    Tile *tp, *tpLast;
    int xsplit;

    xsplit = RIGHT(tile);
    if (mergeFlags & MRG_TOP)
    {
	/*
	 * Find the split point along the top of tile.
	 * If the leftmost tile 'tp' along the top is of type 'newType'
	 * the split point will be no further right than the RHS of 'tp'.
	 * If the leftmost tile is NOT of type 'newType', then the split
	 * point will be no further right than the LHS of the first tile
	 * along the top that is of type 'newType'.
	 */
	for (tpLast = NULL, tp = RT(tile); RIGHT(tp) > LEFT(tile); tp = BL(tp))
	    if (TiGetTypeExact(tp) == newType)
		tpLast = tp;

	/* If the leftmost top tile is not of type 'newType', don't merge */
	if (tpLast == NULL || LEFT(tpLast) > LEFT(tile))
	{
	    mergeFlags &= ~MRG_TOP;
	    if (tpLast && LEFT(tpLast) < xsplit) xsplit = LEFT(tpLast);
	}
	else if (RIGHT(tpLast) < xsplit) xsplit = RIGHT(tpLast);
    }

    if (mergeFlags & MRG_BOTTOM)
    {
	/*
	 * Find the split point along the bottom of 'tile'.
	 * If the leftmost tile 'tp' along the bottom is of type 'newType'
	 * the split point will be no further right than the LHS of 'tp'.
	 * If the leftmost tile is NOT of type 'newType', then the split
	 * point will be no further right than the LHS of the first tile
	 * along the bottom that is of type 'newType'.
	 */
	tp = LB(tile);
	if (TiGetTypeExact(tp) == newType)
	{
	    if (RIGHT(tp) < xsplit) xsplit = RIGHT(tp);
	}
	else
	{
	    /* Leftmost bottom tile is not of type 'newType', so don't merge */
	    do
		tp = TR(tp);
	    while (TiGetTypeExact(tp) != newType && LEFT(tp) < xsplit);
	    if (LEFT(tp) < xsplit) xsplit = LEFT(tp);
	    mergeFlags &= ~MRG_BOTTOM;
	}
    }

    /*
     * If 'tile' must be split vertically, do so.
     * Any merging to the right will be delayed until the split-off
     * right tile is processed on a subsequent iteration of the area
     * enumeration loop in DBPaintPlaneVert().
     */
    if (xsplit < RIGHT(tile))
    {
	mergeFlags &= ~MRG_RIGHT;
	tp = TiSplitX(tile, xsplit);
	TiSetBody(tp, TiGetTypeExact(tile));
#ifdef	PAINTDEBUG
	if (dbPaintDebug)
	    dbPaintShowTile(tile, undo, "(DBMERGE) after split");
#endif	/* PAINTDEBUG */
    }

    /*
     * Set the type of the new tile.
     * Record any undo information.
     */
    if (undo && TiGetTypeExact(tile) != newType && UndoIsEnabled())
	DBPAINTUNDO(tile, newType, undo);
    TiSetBody(tile, newType);
#ifdef	PAINTDEBUG
    if (dbPaintDebug)
	dbPaintShowTile(tile, undo, "(DBMERGE) changed type");
#endif	/* PAINTDEBUG */

    /*
     * Do the merging.
     * We are guaranteed that at most one tile abuts 'tile' on
     * any side that we will merge to, and that this tile is
     * of type 'newType'.
     */
    if (mergeFlags & MRG_TOP)
    {
	tp = RT(tile);
	if (LEFT(tp) < LEFT(tile)) tp = TiSplitX(tp, LEFT(tile));
	if (RIGHT(tp) > RIGHT(tile))
	    tpLast = TiSplitX(tp, RIGHT(tile)), TiSetBody(tpLast, newType);
	TiJoinY(tile, tp, plane);
#ifdef	PAINTDEBUG
	if (dbPaintDebug)
	    dbPaintShowTile(tile, undo, "(DBMERGE) merged up");
#endif	/* PAINTDEBUG */
    }

    if (mergeFlags & MRG_BOTTOM)
    {
	tp = LB(tile);
	if (LEFT(tp) < LEFT(tile)) tp = TiSplitX(tp, LEFT(tile));
	if (RIGHT(tp) > RIGHT(tile))
	    tpLast = TiSplitX(tp, RIGHT(tile)), TiSetBody(tpLast, newType);
	TiJoinY(tile, tp, plane);
#ifdef	PAINTDEBUG
	if (dbPaintDebug)
	    dbPaintShowTile(tile, undo, "(DBMERGE) merged down");
#endif	/* PAINTDEBUG */
    }

    if (mergeFlags&MRG_LEFT)
    {
	tp = BL(tile);
	if (CANMERGE_X(tp, tile)) TiJoinX(tile, tp, plane);
#ifdef	PAINTDEBUG
	if (dbPaintDebug)
	    dbPaintShowTile(tile, undo, "(DBMERGE) merged left");
#endif	/* PAINTDEBUG */
    }
    if (mergeFlags&MRG_RIGHT)
    {
	tp = TR(tile);
	if (CANMERGE_X(tp, tile)) TiJoinX(tile, tp, plane);
#ifdef	PAINTDEBUG
	if (dbPaintDebug)
	    dbPaintShowTile(tile, undo, "(DBMERGE) merged right");
#endif	/* PAINTDEBUG */
    }

    return (tile);
}

#ifdef	PAINTDEBUG
/*
 * ----------------------------------------------------------------------------
 *
 * dbPaintShowTile -- 
 *
 * Show the tile 'tp' in the cell undo->pu_def in a highlighted style,
 * then print a message, wait for more, and erase the highlights.
 * This procedure is for debugging the new paint code only.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Redisplays.
 *
 * ----------------------------------------------------------------------------
 */

#include "styles.h"

void
dbPaintShowTile(tile, undo, str)
    Tile *tile;			/* Tile to be highlighted */
    PaintUndoInfo *undo;	/* Cell to which tile belongs is undo->pu_def */
    char *str;			/* Message to be displayed */
{
    char answer[100];
    Rect r;

    if (undo == NULL)
	return;

    TiToRect(tile, &r);
    DBWAreaChanged(undo->pu_def, &r, DBW_ALLWINDOWS, &DBAllButSpaceBits);
    DBWFeedbackAdd(&r, str, undo->pu_def, 1, STYLE_MEDIUMHIGHLIGHTS);
    DBWFeedbackShow();
    WindUpdate();

    TxPrintf("%s --more--", str); fflush(stdout);
    (void) TxGetLine(answer, sizeof answer);
    DBWFeedbackClear(NULL);
}
#endif	/* PAINTDEBUG */

/*
 * --------------------------------------------------------------------
 *
 * TiNMSplitY --
 *
 * Extension of TiSplitY to the non-manhattan case.
 * Assumes that the tile has alredy been checked for type diagonal.
 *
 * When splitting diagonal tiles, a Y split must be accompanied by
 * an X split, cutting the tile vertically at the intersection of
 * the tile's diagonal and the horizontal Y cut.  If it is not
 * an integer number, it will be rounded down.
 *
 * Results:
 *	Returns TRUE if rounding fractional values has caused a
 *	change in the tile geometry.  May be used by the calling
 *	function for DBWAreaChanged().
 *
 * Side Effects:
 *	May change the geometry of the split tile by a fraction of
 *	an internal unit.
 *
 * --------------------------------------------------------------------
 */

bool
TiNMSplitY(oldtile, newtile, y, dir, undo)
    Tile **oldtile;	/* Tile to be split */
    Tile **newtile;	/* Tile to be generated */
    int y;		/* Y coordinate of split */
    int dir;		/* 1: new tile on top, 0: new tile on bottom */
    PaintUndoInfo *undo;	/* Undo record */
{
    long tmpdx;
    int x, delx, height;
    bool haschanged;	/* If split changes the geometry */
    Tile *newxtop, *newxbot;
    Rect r;

    height = TOP(*oldtile) - BOTTOM(*oldtile); 
    tmpdx = (long)(y - BOTTOM(*oldtile)) * (long)(RIGHT(*oldtile) - LEFT(*oldtile));
    haschanged = (x = (tmpdx % (long)height) << 1) ? ((undo) ? TRUE : FALSE) : FALSE;
    x = (x >= height) ? 1 : 0;
    tmpdx /= (long)height;
    delx = tmpdx + x;

    /* fprintf(stderr, "delx = %d\n", delx);	*/

    if (SplitDirection(*oldtile))
	x = RIGHT(*oldtile) - delx;
    else
	x = LEFT(*oldtile) + delx;

    /* fprintf(stderr, "x = %d, left = %d, right = %d\n",	*/
    /*		x, LEFT(*oldtile), RIGHT(*oldtile));		*/

    if (haschanged) TiToRect(*oldtile, &r);

    *newtile = TiSplitY(*oldtile, y);
    /* Only split if there is enough space to split */

    if (x > LEFT(*oldtile) && x < RIGHT(*oldtile))
    {
	newxbot = TiSplitX(*oldtile, x);
	newxtop = TiSplitX(*newtile, x);

    	/* fprintf(stderr, "Double split x = %d, y = %d\n", x, y); */

	if (SplitDirection(*oldtile))
	{
	    if (undo) dbSplitUndo(*newtile, x, undo);
	    TiSetBody(newxbot, TiGetBody(*oldtile));
	    TiSetBody(*newtile, TiGetBody(*oldtile));
	    TiSetBody(newxtop, SplitRightType(*oldtile));
	    TiSetBody(*oldtile, SplitLeftType(*oldtile));
	}
	else
	{
	    if (undo) dbSplitUndo(newxtop, x, undo);
	    TiSetBody(newxtop, TiGetBody(*oldtile));
	    TiSetBody(newxbot, SplitRightType(*oldtile));
	    TiSetBody(*newtile, SplitLeftType(*oldtile));
	}
    }
    else
    {
	TiSetBody(*newtile, TiGetBody(*oldtile));
	if (x == LEFT(*oldtile))
	{
	    if (SplitDirection(*newtile))
	    {
		if (undo) DBPAINTUNDO(*newtile, SplitRightType(*oldtile), undo);
	        TiSetBody(*newtile, SplitRightType(*oldtile));
	    }
	    else
	    {
		if (undo) DBPAINTUNDO(*oldtile, SplitRightType(*oldtile), undo);
	        TiSetBody(*oldtile, SplitRightType(*oldtile));
	    }
	}
	else
	{
	    if (SplitDirection(*newtile))
	    {
		if (undo) DBPAINTUNDO(*oldtile, SplitLeftType(*oldtile), undo);
	        TiSetBody(*oldtile, SplitLeftType(*oldtile));
	    }
	    else
	    {
		if (undo) DBPAINTUNDO(*newtile, SplitLeftType(*oldtile), undo);
	        TiSetBody(*newtile, SplitLeftType(*oldtile));
	    }
	}
    }
    if (!dir)
    {
	newxtop = *oldtile;
	*oldtile = *newtile;
	*newtile = newxtop;
    }

    /* Requires repaint if tile geometry was altered by integer round-off */
    if (haschanged)
	DBWAreaChanged(undo->pu_def, &r, DBW_ALLWINDOWS,
		&DBAllButSpaceBits);

    return haschanged;
}

/*
 * --------------------------------------------------------------------
 *
 * TiNMSplitX --
 *
 * Extension of TiSplitX to the non-manhattan case.
 * Assumes that the tile has alredy been checked for type diagonal.
 *
 * When splitting diagonal tiles, an X split must be accompanied by
 * a Y split, cutting the tile horizontally at the intersection of
 * the tile's diagonal and the vertical X cut.  If it is not
 * an integer number, it will be rounded to the nearest integer.
 *
 * Results:
 *	Returns TRUE if rounding fractional values has caused a
 *	change in the tile geometry.  May be used by the calling
 *	function for DBWAreaChanged().
 *
 * Side Effects:
 *	May change the geometry of the split tile by a fraction of
 *	an internal unit.
 *
 * --------------------------------------------------------------------
 */

bool
TiNMSplitX(oldtile, newtile, x, dir, undo)
    Tile **oldtile;	/* Tile to be split */
    Tile **newtile;	/* Tile to be generated */
    int x;		/* X coordinate of split */
    int dir;		/* 1: new tile on right, 0: new tile on left */
    PaintUndoInfo *undo;	/* Undo record */
{
    long tmpdy;
    int y, dely, width;
    bool haschanged;	/* If split changes the geometry */
    Tile *newyright, *newyleft;
    Rect r;

    width = RIGHT(*oldtile) - LEFT(*oldtile);
    tmpdy = (long)(x - LEFT(*oldtile)) * (long)(TOP(*oldtile) - BOTTOM(*oldtile));
    haschanged = (y = (tmpdy % (long)width) << 1) ? ((undo) ? TRUE : FALSE) : FALSE;
    y = (y >= width) ? 1 : 0;
    tmpdy /= (long)width;
    dely = tmpdy + y;

    /* fprintf(stderr, "dely = %d\n", dely); */

    if (SplitDirection(*oldtile))
	y = TOP(*oldtile) - dely;
    else
	y = BOTTOM(*oldtile) + dely;

    if (haschanged)
	TiToRect(*oldtile, &r);

    *newtile = TiSplitX(*oldtile, x);

    /* fprintf(stderr, "y = %d, bottom = %d, top = %d\n",	*/
    /*	  	y, BOTTOM(*oldtile), TOP(*oldtile));		*/

    /* Only split if there is enough space to split */

    if (y > BOTTOM(*oldtile) && y < TOP(*oldtile))
    {
	newyleft = *oldtile;
	*oldtile = TiSplitY(newyleft, y);
        newyright = *newtile;
	*newtile = TiSplitY(newyright, y);

    	/* fprintf(stderr, "Double split x = %d, y = %d\n", x, y); */

	if (SplitDirection(newyleft))
	{
	    if (undo) dbSplitUndo(*oldtile, x, undo);
	    TiSetBody(*oldtile, TiGetBody(newyleft));
	    TiSetBody(newyright, TiGetBody(newyleft));
	    TiSetBody(*newtile, SplitRightType(newyleft));
	    TiSetBody(newyleft, SplitLeftType(newyleft));
	}
	else
	{
	    if (undo) dbSplitUndo(*newtile, x, undo);
	    TiSetBody(*newtile, TiGetBody(newyleft));
	    TiSetBody(newyright, SplitRightType(newyleft));
	    TiSetBody(*oldtile, SplitLeftType(newyleft));
	}
    }
    else
    {
	TiSetBody(*newtile, TiGetBody(*oldtile));
	if (y == BOTTOM(*oldtile))
	{
	    if (SplitDirection(*newtile))
	    {
	 	if (undo) DBPAINTUNDO(*newtile, SplitRightType(*oldtile), undo);
	        TiSetBody(*newtile, SplitRightType(*oldtile));
	    }
	    else
	    {
	 	if (undo) DBPAINTUNDO(*oldtile, SplitRightType(*oldtile), undo);
	        TiSetBody(*oldtile, SplitLeftType(*oldtile));
	    }
	}
	else
	{
	    if (SplitDirection(*newtile))
	    {
	 	if (undo) DBPAINTUNDO(*oldtile, SplitLeftType(*oldtile), undo);
	        TiSetBody(*oldtile, SplitLeftType(*oldtile));
	    }
	    else
	    {
	 	if (undo) DBPAINTUNDO(*newtile, SplitLeftType(*oldtile), undo);
	        TiSetBody(*newtile, SplitRightType(*oldtile));
	    }
	}
    }
    if (!dir)
    {
	newyright = *oldtile;
	*oldtile = *newtile;
	*newtile = newyright;
    }

    /* Requires repaint if tile geometry was altered by integer round-off */
    if (haschanged)
	DBWAreaChanged(undo->pu_def, &r, DBW_ALLWINDOWS,
		&DBAllButSpaceBits);

    return haschanged;
}

/*
 * --------------------------------------------------------------------
 *
 * TiNMMergeRight --
 *
 *	After splitting a non-Manhattan tile into quarters, the two
 *	Manhattan quarters need to be merged back into the plane to
 *	preserve the maximum horizontal stripes rule.  This routine
 *	does the merge to the right.
 *
 * Results:
 *	Returns the topmost tile in the area formerly covered by "tile".
 *
 * Side effects:
 *	Tile splitting and merging.
 *
 * --------------------------------------------------------------------
 */

Tile *
TiNMMergeRight(tile, plane)
    Tile *tile;
    Plane *plane;
{
    TileType ttype = TiGetTypeExact(tile);
    Tile *tp, *tp2, *newtile;

    tp = TR(tile);
    if (TOP(tp) > TOP(tile))
    {
	if (TiGetTypeExact(tp) == ttype)
	{
	    newtile = TiSplitY(tp, TOP(tile));
	    TiSetBody(newtile, ttype);
	}
    }

    while (BOTTOM(tp) >= BOTTOM(tile))
    {
	tp2 = LB(tp);
	if (TiGetTypeExact(tp) == ttype)
	{
	    if (TOP(tp) < TOP(tile))
	    {
		newtile = TiSplitY(tile, TOP(tp));
		TiSetBody(newtile, ttype);
	    }
	    if (BOTTOM(tp) > BOTTOM(tile))
	    {
		newtile = TiSplitY(tile, BOTTOM(tp));
		TiSetBody(newtile, ttype);
	    }
	    else
		newtile = tile;
	    // Join tp to newtile
	    TiJoinX(newtile, tp, plane);
	}
	tp = tp2;
    }

    if (TOP(tp) > BOTTOM(tile))
    {
	if (TiGetTypeExact(tp) == ttype)
	{
	    if (TOP(tp) < TOP(tile))
	    {
		newtile = TiSplitY(tile, TOP(tp));
		TiSetBody(newtile, ttype);
	    }
	    newtile = TiSplitY(tp, BOTTOM(tile));
	    TiSetBody(newtile, ttype);
	    // join newtile to tile
	    TiJoinX(tile, newtile, plane);
	    // merge up if possible
	    if (CANMERGE_Y(tile, RT(tile))) TiJoinY(tile, RT(tile), plane);
	}
    }
    return tile;
}

/*
 * --------------------------------------------------------------------
 *
 * TiNMMergeLeft --
 *
 *	After splitting a non-Manhattan tile into quarters, the two
 *	Manhattan quarters need to be merged back into the plane to
 *	preserve the maximum horizontal stripes rule.  This routine
 *	does the merge to the left.
 *
 * Results:
 *	Returns the topmost tile in the area formerly covered by "tile".
 *
 * Side effects:
 *	Tile splitting and merging.
 *
 * --------------------------------------------------------------------
 */

Tile *
TiNMMergeLeft(tile, plane)
    Tile *tile;
    Plane *plane;
{
    TileType ttype = TiGetTypeExact(tile);
    Tile *tp, *tp2, *newtile;

    tp = BL(tile);
    if (BOTTOM(tp) < BOTTOM(tile))
    {
	if (TiGetTypeExact(tp) == ttype)
	{
	    newtile = TiSplitY(tp, BOTTOM(tile));
	    TiSetBody(newtile, ttype);
	    tp = newtile;
	}
    }

    while (TOP(tp) <= TOP(tile))
    {
	tp2 = RT(tp);
	if (TiGetTypeExact(tp) == ttype)
	{
	    if (BOTTOM(tile) < BOTTOM(tp))
	    {
		tile = TiSplitY(tile, BOTTOM(tp));
		TiSetBody(tile, ttype);
	    }
	    if (TOP(tp) < TOP(tile))
	    {
		newtile = TiSplitY(tile, TOP(tp));
		TiSetBody(newtile, ttype);
	    }
	    else
		newtile = tile;
	    // Join tp to tile
	    TiJoinX(tile, tp, plane);
	    tile = newtile;
	}
	tp = tp2;
    }

    if (BOTTOM(tp) < TOP(tile))
    {
	if (TiGetTypeExact(tp) == ttype)
	{
	    if (BOTTOM(tile) < BOTTOM(tp))
	    {
		tile = TiSplitY(tile, BOTTOM(tp));
		TiSetBody(tile, ttype);
	    }
	    newtile = TiSplitY(tp, TOP(tile));
	    TiSetBody(newtile, ttype);
	    // join tp to tile
	    TiJoinX(tile, tp, plane);
	}
    }
    else
    {
	// Merge up if possible
	if (CANMERGE_Y(tile, tp)) TiJoinY(tile, tp, plane);
    }
    return tile;
}

