/*
 * selOps.c --
 *
 * This file contains top-level procedures to manipulate the selection,
 * e.g. to delete it, move it, etc.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/select/selOps.c,v 1.11 2010/08/22 21:58:26 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/main.h"
#include "select/select.h"
#include "select/selInt.h"
#include "textio/textio.h"
#include "utils/undo.h"
#include "plow/plow.h"
#include "utils/malloc.h"
#include "drc/drc.h"

/* The following variables are shared between SelectStretch and the
 * search functions that it causes to be invoked.
 */

static int selStretchX, selStretchY;	/* Stretch distances.  Only one should
					 * ever be non-zero.
					 */
static TileType selStretchType;		/* Type of material being stretched. */

typedef struct planeAndArea
{
    int pa_plane;			/* Plane of interest */
    Rect *pa_area;			/* Area affected */
    TileTypeBitMask *pa_mask;		/* Mask used in plane search */
} planeAndArea;

/* The following structure type is used to build up a list of areas
 * to be painted.  It's used to save information while a search of
 * the edit cell is in progress:  can't do the paints until the
 * search has finished.
 */

typedef struct stretchArea
{
    Rect sa_area;			/* Area to be painted. */
    TileType sa_type;			/* Type of material to paint. */
    struct stretchArea *sa_next;	/* Next element in list. */
} StretchArea;

static StretchArea *selStretchList;	/* List of areas to paint. */

/*
 * ----------------------------------------------------------------------------
 *
 * SelectDelete --
 *
 * 	Delete everything in the edit cell that's selected.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stuff is removed from the edit cell.  If there's selected
 *	stuff that isn't in the edit cell, the user is warned.
 *
 * ----------------------------------------------------------------------------
 */

void
SelectDelete(msg, do_clear)
    char *msg;		/* Some information to print in error messages.
			 * For example, if called as part of a move procedure,
			 * supply "moved".  This will appear in messages of
			 * the form "only edit cell information was moved".
			 */
    bool do_clear;	/* If TRUE, clear the select def before returning. */
{
    bool nonEdit;
    Rect editArea;

    extern int selDelPaintFunc(), selDelCellFunc(), selDelLabelFunc();

    (void) SelEnumPaint(&DBAllButSpaceAndDRCBits, TRUE, &nonEdit,
	    selDelPaintFunc, (ClientData) NULL);
    if (nonEdit)
    {
	TxError("You selected paint outside the edit cell.  Only\n");
	TxError("    the paint in the edit cell was %s.\n", msg);
    }
    (void) SelEnumCells(TRUE, &nonEdit, (SearchContext *) NULL,
	    selDelCellFunc, (ClientData) NULL);
    if (nonEdit)
    {
	TxError("You selected one or more subcells that aren't children\n");
	TxError("    of the edit cell.  Only those in the edit cell were\n");
	TxError("    %s.\n", msg);
    }
    (void) SelEnumLabels(&DBAllTypeBits, TRUE, &nonEdit,
	    selDelLabelFunc, (ClientData) NULL);
    if (nonEdit)
    {
	TxError("You selected one or more labels that aren't in the\n");
	TxError("    edit cell.  Only the label(s) in the edit cell\n");
	TxError("    were %s.\n", msg);
    }

    DBReComputeBbox(EditCellUse->cu_def);
    GeoTransRect(&RootToEditTransform, &SelectDef->cd_extended, &editArea);
    DBWAreaChanged(EditCellUse->cu_def, &editArea, DBW_ALLWINDOWS,
	(TileTypeBitMask *) NULL);
    DRCCheckThis(EditCellUse->cu_def, TT_CHECKPAINT, &editArea);
    if (do_clear) SelectClear();
}

/* Search function to delete paint. */

int
selDelPaintFunc(rect, type)
    Rect *rect;			/* Area of paint, in root coords. */
    TileType type;		/* Type of paint to delete. */
{
    Rect editRect;
    TileTypeBitMask tmask;
    TileType dinfo;

    /* Change diagonal side & direction according to the transform */

    if (type & TT_DIAGONAL)
    {
	dinfo = DBTransformDiagonal(type, &RootToEditTransform);
	TTMaskSetOnlyType(&tmask, type & TT_LEFTMASK);   
    }
    else
    {
	dinfo = 0;
	TTMaskSetOnlyType(&tmask, type);   
    }

    GeoTransRect(&RootToEditTransform, rect, &editRect);

    DBEraseValid(EditCellUse->cu_def, &editRect, &tmask, dinfo);
    return 0;
}

/* Search function to delete subcell uses. */

    /* ARGSUSED */
int
selDelCellFunc(selUse, use)
    CellUse *selUse;		/* Not used. */
    CellUse *use;		/* What to delete. */
{
    if (use->cu_flags & CU_LOCKED) return 0;

    DBUnLinkCell(use, use->cu_parent);
    DBDeleteCell(use);
    (void) DBCellDeleteUse(use);
    return 0;
}


/* Search function to delete labels.  Delete any label at the right
 * place with the right name, regardless of layer attachment, because
 * the selection can differ from the edit cell in this regard. */

int
selDelLabelFunc(label)
    Label *label;		/* Label to delete. */
{
    DBEraseLabelsByContent(EditCellUse->cu_def, &label->lab_rect, -1,
		label->lab_text);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * SelectCopy --
 *
 * 	This procedure makes a copy of the selection.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The selection is copied, with the copy being transformed by
 *	"transform" relative to the current selection.  The copy is
 *	made the new selection.
 *
 * ----------------------------------------------------------------------------
 */

void
SelectCopy(transform)
    Transform *transform;	/* How to displace the copy relative
				 * to the original.  This displacement
				 * is given in root coordinates.
				 */
{
    SearchContext scx;

    /* Copy from SelectDef to Select2Def while transforming, then
     * let SelectAndCopy2 do the rest of the work.  Don't record
     * anything involving Select2Def for undo-ing.
     */

    UndoDisable();
    DBCellClearDef(Select2Def);
    scx.scx_use = SelectUse;
    scx.scx_area = SelectUse->cu_bbox;
    GeoTransTrans(transform, &SelectUse->cu_transform, &scx.scx_trans);
    (void) DBCellCopyAllPaint(&scx, &DBAllButSpaceAndDRCBits, CU_DESCEND_NO_LOCK,
		Select2Use);
    (void) DBCellCopyAllLabels(&scx, &DBAllTypeBits, CU_DESCEND_NO_LOCK, Select2Use,
		(Rect *) NULL);
    (void) DBCellCopyAllCells(&scx, CU_DESCEND_NO_LOCK, Select2Use, (Rect *) NULL);
    DBReComputeBbox(Select2Def);
    UndoEnable();

    SelectClear();
    SelectAndCopy2(EditRootDef);
}

/*
 * ----------------------------------------------------------------------------
 *
 * SelectFlat --
 *
 * 	This procedure copies the selection into Select2Def, flattening
 *	it as it copies, then copies the result back into the selection
 *	cell.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The selection is flattened.  No changes are made in the edit cell.
 *
 * ----------------------------------------------------------------------------
 */

void
SelectFlat()
{
    SearchContext scx;

    /* Copy from SelectDef to Select2Def while transforming, then
     * let SelectAndCopy2 do the rest of the work.  Don't record
     * anything involving Select2Def for undo-ing.
     */

    UndoDisable();
    DBCellClearDef(Select2Def);
    scx.scx_use = SelectUse;
    scx.scx_area = SelectUse->cu_bbox;
    GeoTransTrans(&GeoIdentityTransform, &SelectUse->cu_transform, &scx.scx_trans);
    DBCellCopyAllPaint(&scx, &DBAllButSpaceAndDRCBits, CU_DESCEND_ALL, Select2Use);
    FlatCopyAllLabels(&scx, &DBAllTypeBits, CU_DESCEND_ALL, Select2Use);
    DBReComputeBbox(Select2Def);
    UndoEnable();

    /* Move everything from Select2 to Select */

    SelectClear();
    SelRememberForUndo(TRUE, (CellDef *) NULL, (Rect *) NULL);

    scx.scx_use = Select2Use;
    scx.scx_area = Select2Use->cu_bbox;
    GeoTransTrans(&GeoIdentityTransform, &Select2Use->cu_transform, &scx.scx_trans);

    DBCellCopyAllPaint(&scx, &DBAllButSpaceAndDRCBits, CU_DESCEND_SPECIAL,
		SelectUse);
    DBCellCopyAllLabels(&scx, &DBAllTypeBits, CU_DESCEND_SPECIAL, SelectUse,
		(Rect *)NULL);

    SelRememberForUndo(FALSE, SelectRootDef, &SelectUse->cu_bbox);

    /* Redisplay the select cell */
    DBWHLRedraw(SelectRootDef, &SelectDef->cd_extended, TRUE);
    DBWAreaChanged(SelectDef, &SelectDef->cd_extended, DBW_ALLWINDOWS,
	&DBAllButSpaceBits);

}

/*
 * ----------------------------------------------------------------------------
 *
 *  selShortFindPath --
 *
 *	Trace back through a path found by selShortFindNext from destination
 *	to source, picking the lowest cost return path, and adding each tile
 *	found to the linked list.
 *
 *	Algorithm notes (for this and selShortFindNext):  Note that by not
 *	using one of the standard database search routines, TT_SIDE is NOT
 *	set on any tile.  To find out what side we're looking at, we keep
 *	a record of what direction we were traveling from the previous
 *	tile.  Given this and the diagonal direction, we know the two
 *	valid sides to search.
 *
 * ----------------------------------------------------------------------------
 */

int
selShortFindPath(tile, pnum, rlist, fdir)
   Tile *tile;
   int pnum;
   ExtRectList **rlist;
   int fdir;
{
    Tile *tp, *mintp;
    // int mincost = (int)tile->ti_client;
    int mincost = INT_MAX;
    ExtRectList *newrrec;
    int minp, p, mindir;
    TileType ttype;

    newrrec = mallocMagic(sizeof(ExtRectList));

    if (IsSplit(tile))
    {
	newrrec->r_type = TiGetTypeExact(tile) & ~TT_SIDE;
	switch(fdir)
	{
	    case GEO_NORTH:
		ttype = SplitBottomType(tile);
		if (!SplitDirection(tile)) newrrec->r_type |= TT_SIDE;
		break;
	    case GEO_SOUTH:
		ttype = SplitTopType(tile);
		if (SplitDirection(tile)) newrrec->r_type |= TT_SIDE;
		break;
	    case GEO_EAST:
		ttype = SplitLeftType(tile);
		break;
	    case GEO_WEST:
		ttype = SplitRightType(tile);
		newrrec->r_type |= TT_SIDE;
		break;
	    default:
		ttype = SplitRightType(tile);
		if (ttype == TT_SPACE)
		    ttype = SplitLeftType(tile);
		else
		    newrrec->r_type |= TT_SIDE;
		break;
	}
    }
    else
    {
	ttype = TiGetTypeExact(tile);
	newrrec->r_type = ttype;
    }

    /* Add this tile (area and type) to the linked list */

    TiToRect(tile, &newrrec->r_r);
    newrrec->r_next = *rlist;
    *rlist = newrrec;

    if ((int)tile->ti_client == 0) return 0;	/* We're done */
    // if (mincost == 0) return 0;		/* We're done */
    minp = pnum;

    /* Search top */
    if (IsSplit(tile))
    {
	if (fdir == GEO_NORTH) goto leftside;
	else if (SplitDirection(tile) && fdir == GEO_EAST) goto leftside;
	else if (!SplitDirection(tile) && fdir == GEO_WEST) goto leftside;
    }

    for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp = BL(tp))
    {
	if (tp->ti_client == (ClientData)CLIENTDEFAULT) continue;
	if ((int)tp->ti_client < mincost)
	{
	    mincost = (int)tp->ti_client;
	    mintp = tp;
	    mindir = GEO_NORTH;
	}
    }

    /* Search left */
leftside:
    if (IsSplit(tile))
    {
	if (fdir == GEO_WEST) goto bottomside;
	else if (SplitDirection(tile) && fdir == GEO_SOUTH) goto bottomside;
	else if (!SplitDirection(tile) && fdir == GEO_NORTH) goto bottomside;
    }

    for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
    {
	if (tp->ti_client == (ClientData)CLIENTDEFAULT) continue;
	if ((int)tp->ti_client < mincost)
	{
	    mincost = (int)tp->ti_client;
	    mintp = tp;
	    mindir = GEO_WEST;
	}
    }

    /* Search bottom */
bottomside:
    if (IsSplit(tile))
    {
	if (fdir == GEO_SOUTH) goto rightside;
	else if (SplitDirection(tile) && fdir == GEO_WEST) goto rightside;
	else if (!SplitDirection(tile) && fdir == GEO_EAST) goto rightside;
    }

    for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp = TR(tp))
    {
	if (tp->ti_client == (ClientData)CLIENTDEFAULT) continue;
	if ((int)tp->ti_client < mincost)
	{
	    mincost = (int)tp->ti_client;
	    mintp = tp;
	    mindir = GEO_SOUTH;
	}
    }

    /* Search right */
rightside:
    if (IsSplit(tile))
    {
	if (fdir == GEO_EAST) goto donesides;
	else if (SplitDirection(tile) && fdir == GEO_NORTH) goto donesides;
	else if (!SplitDirection(tile) && fdir == GEO_SOUTH) goto donesides;
    }

    for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp = LB(tp))
    {
	if (tp->ti_client == (ClientData)CLIENTDEFAULT) continue;
	if ((int)tp->ti_client < mincost)
	{
	    mincost = (int)tp->ti_client;
	    mintp = tp;
	    mindir = GEO_EAST;
	}
    }

    /* Search other connecting planes */
donesides:
    if (DBIsContact(ttype))
    {
	PlaneMask pmask;

	pmask = DBConnPlanes[ttype];
	for (p = PL_TECHDEPBASE; p < DBNumPlanes; p++)
	{
	    if (PlaneMaskHasPlane(pmask, p) && (p != pnum))
	    {
		tp = SelectDef->cd_planes[p]->pl_hint;
		GOTOPOINT(tp, &tile->ti_ll);
		if (tp->ti_client == (ClientData)CLIENTDEFAULT) continue;
		if ((int)tp->ti_client < mincost)
		{
		    mincost = (int)tp->ti_client;
		    mintp = tp;
		    minp = p;
		    mindir = GEO_CENTER;
		}
	    }
	}
    }

    /* If mincost is still set to INT_MAX we have a real serious problem! */
    if (mincost == INT_MAX) return 1;

    /* If no tile had lower cost than this one, then we have an error */
    // if (mincost == (int)tile->ti_client) return 1;

    /* Stopgap measure:  Error should not happen, but it does!	*/
    /* Remove client data of current tile and take minimum.	*/
    if (mincost == (int)tile->ti_client) TiSetClient(tile, CLIENTDEFAULT);

    /* Now we have the minimum cost neighboring tile;  recursively search it */

    return selShortFindPath(mintp, minp, rlist, mindir);
}

/*
 * ----------------------------------------------------------------------------
 *
 * selShortFindNext --
 *
 *	Recursive function for finding shorts.  This routine makes strong
 *	assumptions;  namely, that all non-space material in the cell being
 *	searched belongs to the same net.  The cell searched is always
 *	SelectDef.
 *
 * Results:
 *	Return 0 to keep going;  return 1 to stop when the tile contains
 *	the destination point.
 *
 * Side effects:
 *	Each tile visited has its ClientData record set to the current
 *	cost, in units equal to the steps from the source.
 *
 * ----------------------------------------------------------------------------
 */

int
selShortFindNext(tile, pnum, ldest, cost, best, fdir, mask)
    Tile *tile;
    int pnum;
    Label *ldest;
    int cost, *best, fdir;
    TileTypeBitMask *mask;
{
    TileType ttype;
    TileTypeBitMask *lmask;
    Tile *tp;

    if (IsSplit(tile))
    {
	switch(fdir)
	{
	    case GEO_NORTH:
		ttype = SplitBottomType(tile);
		break;
	    case GEO_SOUTH:
		ttype = SplitTopType(tile);
		break;
	    case GEO_EAST:
		ttype = SplitLeftType(tile);
		break;
	    case GEO_WEST:
		ttype = SplitRightType(tile);
		break;
	    default:
		ttype = SplitLeftType(tile);
		if (ttype == TT_SPACE) ttype = SplitRightType(tile);
		break;
	}
    }
    else
	ttype = TiGetTypeExact(tile);

    /* Ignore space tiles */
    if (ttype == TT_SPACE) return 0;

    /* Ignore non-connecting tiles */
    if (!TTMaskHasType(mask, ttype)) return 0;

    /* If this tile is unvisited, or has a lower cost, then return and	*/
    /* keep going.  Otherwise, return 1 to stop the search this direction */

    if (tile->ti_client == (ClientData)CLIENTDEFAULT)
	TiSetClient(tile, cost);
    else if ((int)tile->ti_client > cost)
	TiSetClient(tile, cost);
    else
	return 0;

    /* If this tile contains the destination point, do not search further */

    if ((ttype == ldest->lab_type) && EnclosePoint(tile, &ldest->lab_rect.r_ll))
    {
	if (*best >= cost) *best = (cost - 1);
	return 0;
    }

    /* If we're more costly than the best known path to destination, do	*/
    /* not search further.						*/

    if (cost >= *best) return 0;
    lmask = &DBConnectTbl[ttype];

    /* Search top */
    if (IsSplit(tile))
    {
	if (fdir == GEO_NORTH) goto srchleft;
	else if (SplitDirection(tile) && fdir == GEO_EAST) goto srchleft;
	else if (!SplitDirection(tile) && fdir == GEO_WEST) goto srchleft;
    }

    for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp = BL(tp))
    {
	selShortFindNext(tp, pnum, ldest, cost + 1, best, GEO_NORTH, lmask);
    }

    /* Search left */
srchleft:
    if (IsSplit(tile))
    {
	if (fdir == GEO_WEST) goto srchbot;
	else if (SplitDirection(tile) && fdir == GEO_SOUTH) goto srchbot;
	else if (!SplitDirection(tile) && fdir == GEO_NORTH) goto srchbot;
    }

    for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
    {
	selShortFindNext(tp, pnum, ldest, cost + 1, best, GEO_WEST, lmask);
    }

    /* Search bottom */
srchbot:
    if (IsSplit(tile))
    {
	if (fdir == GEO_SOUTH) goto srchright;
	else if (SplitDirection(tile) && fdir == GEO_WEST) goto srchright;
	else if (!SplitDirection(tile) && fdir == GEO_EAST) goto srchright;
    }

    for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp = TR(tp))
    {
	selShortFindNext(tp, pnum, ldest, cost + 1, best, GEO_SOUTH, lmask);
    }

    /* Search right */
srchright:
    if (IsSplit(tile))
    {
	if (fdir == GEO_EAST) goto donesrch;
	else if (SplitDirection(tile) && fdir == GEO_NORTH) goto donesrch;
	else if (!SplitDirection(tile) && fdir == GEO_SOUTH) goto donesrch;
    }

    for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp = LB(tp))
    {
	selShortFindNext(tp, pnum, ldest, cost + 1, best, GEO_EAST, lmask);
    }

    /* Search other connecting planes */
donesrch:
    if (DBIsContact(ttype))
    {
	PlaneMask pmask;
	int p;

	pmask = DBConnPlanes[ttype];
	for (p = PL_TECHDEPBASE; p < DBNumPlanes; p++)
	{
	    if (PlaneMaskHasPlane(pmask, p) && (p != pnum))
	    {
		tp = SelectDef->cd_planes[p]->pl_hint;
		GOTOPOINT(tp, &tile->ti_ll);
		selShortFindNext(tp, p, ldest, cost + 1, best, GEO_CENTER, lmask);
	    }
	}
    }
    return 0;
}
    

/*
 * ----------------------------------------------------------------------------
 *
 * SelectShort --
 *
 *	This procedure, given two labels, finds the location of those
 *	labels in SelectDef.  One is marked as source, the other as
 *	destination.  Then the SelectDef paint (which is assumed to be
 *	a net selection that is all local to SelectDef) is recursively
 *	searched for connecting material.  Each tile ClientData is
 *	given a cost which is the number of steps from the source.
 *	At the end, the minimum cost path is traced from the destination
 * 	back to the source, and the path is saved as a linked list and
 *	passed back to the calling procedure.
 *
 * Results:
 *	A linked list of tiles representing the connecting path with the
 *	fewest steps between source and destination.
 *
 * Side effects:
 *	Tile database left with non-default ClientData.  It may be necessary
 *	to re-run the search routine to return all tiles to the default
 *	ClientData value.
 * ----------------------------------------------------------------------------
 */

ExtRectList *
SelectShort(char *lab1, char *lab2)
{
    Label *selLabel, *srclab = NULL, *destlab = NULL;
    Tile *tile;
    Plane *plane;
    int pnum, best;
    PlaneMask pmask;
    ExtRectList *rlist;

    /* Step one: find the tiles containing the labels.  If not found,	*/
    /* return NULL.							*/

    for (selLabel = SelectDef->cd_labels; selLabel != NULL; selLabel =
	selLabel->lab_next)
    {
	if ((srclab == NULL) && Match(lab1, selLabel->lab_text))
	    srclab = selLabel;

	if ((destlab == NULL) && Match(lab2, selLabel->lab_text))
	    destlab = selLabel;
    }

    /* Must be able to find both labels */
    if (srclab == NULL || destlab == NULL) return NULL;

    /* Must be able to find tiles associated with each label */
    
    pmask = DBTypePlaneMaskTbl[srclab->lab_type];
    for (pnum = PL_TECHDEPBASE; pnum < DBNumPlanes; pnum++)
    {
	if (PlaneMaskHasPlane(pmask, pnum))
	{
	    plane = SelectDef->cd_planes[pnum];
	    tile = plane->pl_hint;
	    GOTOPOINT(tile, &srclab->lab_rect.r_ll)
	    if (TiGetType(tile) == srclab->lab_type) break;
	}
    }
    best = INT_MAX;
    selShortFindNext(tile, pnum, &destlab->lab_rect.r_ll, 0, &best, GEO_CENTER,
		&DBConnectTbl[srclab->lab_type]);

    /* Now see if destination has been counted */

    pmask = DBTypePlaneMaskTbl[destlab->lab_type];
    for (pnum = PL_TECHDEPBASE; pnum < DBNumPlanes; pnum++)
    {
	if (PlaneMaskHasPlane(pmask, pnum))
	{
	    plane = SelectDef->cd_planes[pnum];
	    tile = plane->pl_hint;
	    GOTOPOINT(tile, &destlab->lab_rect.r_ll)
	    if (TiGetType(tile) == destlab->lab_type) break;
	}
    }

    if (tile->ti_client == (ClientData)CLIENTDEFAULT) return NULL;

    /* Now find the shortest path between source and destination */
    rlist = NULL;
    selShortFindPath(tile, pnum, &rlist, GEO_CENTER);

    return rlist;
}


/*
 * ----------------------------------------------------------------------------
 *
 * selTransTo2 --
 *
 * 	This local procedure makes a transformed copy of the selection
 *	in Select2Def, ignoring everything that's not in the edit cell.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Select2Def gets modified to hold the transformed selection.
 *	Error messages get printed if the selection contains any
 *	non-edit material.
 *
 * ----------------------------------------------------------------------------
 */

void
selTransTo2(transform)
    Transform *transform;	/* How to transform stuff before copying
				 * it to Select2Def.
				 */
{
    int selTransPaintFunc();	/* Forward references. */
    int selTransCellFunc();
    int selTransLabelFunc();

    UndoDisable();
    DBCellClearDef(Select2Def);
    (void) SelEnumPaint(&DBAllButSpaceAndDRCBits, TRUE, (bool *) NULL,
	    selTransPaintFunc, (ClientData) transform);
    (void) SelEnumCells(TRUE, (bool *) NULL, (SearchContext *) NULL,
	    selTransCellFunc, (ClientData) transform);
    (void) SelEnumLabels(&DBAllTypeBits, TRUE, (bool *) NULL,
	    selTransLabelFunc, (ClientData) transform);
    DBReComputeBbox(Select2Def);
    UndoEnable();
}

/* Search function to copy paint.  Always return 1 to keep the search alive. */

int
selTransPaintFunc(rect, type, transform)
    Rect *rect;			/* Area of paint. */
    TileType type;		/* Type of paint. */
    Transform *transform;	/* How to change coords before painting. */
{
    Rect newarea;
    TileType loctype;

    /* Change diagonal direction according to the transform */
    if (type & TT_DIAGONAL)
    {
	loctype = DBTransformDiagonal(type, transform);
	loctype |= (loctype & TT_SIDE) ? (type & TT_LEFTMASK) << 14 :
		(type & TT_LEFTMASK);
    }
    else
	loctype = type;

    GeoTransRect(transform, rect, &newarea);
    DBPaint(Select2Def, &newarea, loctype);
    return 0;
}

/* Search function to copy subcells.  Always return 1 to keep the
 * search alive.
 */

    /* ARGSUSED */
int
selTransCellFunc(selUse, realUse, realTrans, transform)
    CellUse *selUse;		/* Use from selection. */
    CellUse *realUse;		/* Corresponding use from layout (used to
				 * get id). */
    Transform *realTrans;	/* Transform for realUse (ignored). */
    Transform *transform;	/* How to change coords of selUse before
				 * copying.
				 */
{
    CellUse  *newUse;
    Transform newTrans;

    if (selUse->cu_flags & CU_LOCKED) return 0;

    newUse = DBCellNewUse(selUse->cu_def, (char *) realUse->cu_id);
    if (!DBLinkCell(newUse, Select2Def))
    {
	freeMagic((char *) newUse->cu_id);
	newUse->cu_id = NULL;
	(void) DBLinkCell(newUse, Select2Def);
    }
    GeoTransTrans(&selUse->cu_transform, transform, &newTrans);
    DBSetArray(selUse, newUse);
    DBSetTrans(newUse, &newTrans);
    newUse->cu_expandMask = selUse->cu_expandMask;
    newUse->cu_flags = selUse->cu_flags;
    DBPlaceCell(newUse, Select2Def);

    return 0;
}

/* Search function to copy labels.  Return 0 always to avoid
 * aborting search.
 */

    /* ARGSUSED */
int
selTransLabelFunc(label, cellUse, defTransform, transform)
    Label *label;		/* Label to copy.  This points to label
				 * in cellDef.
				 */
    CellUse *cellUse;		/* (unused) */
    Transform *defTransform;	/* Transform from cellDef to root. */
    Transform *transform;	/* How to modify coords before copying to
				 * Select2Def.
				 */
{
    Rect rootArea, finalArea;
    int rootJust, finalJust;
    Point rootOffset, finalOffset;
    int rootRotate, finalRotate;

    GeoTransRect(defTransform, &label->lab_rect, &rootArea);
    rootJust = GeoTransPos(defTransform, label->lab_just);
    GeoTransPointDelta(defTransform, &label->lab_offset, &rootOffset);
    rootRotate = GeoTransAngle(defTransform, label->lab_rotate);

    GeoTransRect(transform, &rootArea, &finalArea);
    finalJust = GeoTransPos(transform, rootJust);
    GeoTransPointDelta(transform, &rootOffset, &finalOffset);
    finalRotate = GeoTransAngle(transform, rootRotate);

    (void) DBPutFontLabel(Select2Def, &finalArea, label->lab_font,
	    label->lab_size, finalRotate, &finalOffset, finalJust,
	    label->lab_text, label->lab_type, label->lab_flags);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * SelectTransform --
 *
 * 	This procedure modifies the selection by transforming
 *	it geometrically.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The selection is modified and redisplayed.
 *
 * ----------------------------------------------------------------------------
 */

void
SelectTransform(transform)
    Transform *transform;		/* How to displace the selection.
					 * The transform is in root (user-
					 * visible) coordinates.
					 */
{

    /* Copy from SelectDef to Select2Def, transforming along the way. */

    selTransTo2(transform);

    /* Now just delete the selection and recreate it from Select2Def,
     * copying into the edit cell along the way.
     */

    SelectDelete("modified", TRUE);
    SelectAndCopy2(EditRootDef);
}

/*
 * ----------------------------------------------------------------------------
 *
 * SelectExpand --
 *
 * 	Expand all of the selected cells that are unexpanded, and
 *	unexpand all of those that are expanded.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The contents of the selected cells will become visible or
 *	invisible on the display in the indicated window(s).
 *
 * ----------------------------------------------------------------------------
 */

void
SelectExpand(mask)
    int mask;			/* Bits of this word indicate which
				 * windows the selected cells will be
				 * expanded in.
				 */
{
    extern int selExpandFunc();	/* Forward reference. */

    (void) SelEnumCells(FALSE, (bool *) NULL, (SearchContext *) NULL,
	    selExpandFunc, (ClientData) mask);
}

    /* ARGSUSED */
int
selExpandFunc(selUse, use, transform, mask)
    CellUse *selUse;		/* Use from selection. */
    CellUse *use;		/* Use to expand (in actual layout). */
    Transform *transform;	/* Not used. */
    int mask;			/* Windows in which to expand. */
{
    /* Don't change expansion status of root cell:  screws up
     * DBWAreaChanged (need to always have at least top-level
     * cell be expanded).
     */

    if (use->cu_parent == NULL)
    {
	TxError("Can't unexpand root cell of window.\n");
	return 0;
    }

    /* Be sure to modify the expansion bit in the selection as well as
     * the one in the layout in order to keep them consistent.
     */

    if (DBDescendSubcell(use, mask))
    {
	DBExpand(selUse, mask, FALSE);
	DBExpand(use, mask, FALSE);
	DBWAreaChanged(use->cu_parent, &use->cu_bbox, mask,
	    (TileTypeBitMask *) NULL);
    }
    else
    {
	DBExpand(selUse, mask, TRUE);
	DBExpand(use, mask, TRUE);
	DBWAreaChanged(use->cu_parent, &use->cu_bbox, mask, &DBAllButSpaceBits);
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * SelectArray --
 *
 * 	Array everything in the selection.  Cells get turned into
 *	arrays, and paint and labels get replicated.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The edit cell is modified in a big way.  It's also redisplayed.
 *
 * ----------------------------------------------------------------------------
 */

void
SelectArray(arrayInfo)
    ArrayInfo *arrayInfo;	/* Describes desired shape of array, all in
				 * root coordinates.
				 */
{
    extern int selArrayPFunc(), selArrayCFunc(), selArrayLFunc();

    /* The way arraying is done is similar to moving:  make an
     * arrayed copy of everything in Select2Def, then delete the
     * selection, then copy everything back from Select2Def and
     * select it.
     */
    
    UndoDisable();
    DBCellClearDef(Select2Def);
    (void) SelEnumPaint(&DBAllButSpaceAndDRCBits, TRUE, (bool *) NULL,
	    selArrayPFunc, (ClientData) arrayInfo);
    (void) SelEnumCells(TRUE, (bool *) NULL, (SearchContext *) NULL,
	    selArrayCFunc, (ClientData) arrayInfo);
    (void) SelEnumLabels(&DBAllTypeBits, TRUE, (bool *) NULL,
	    selArrayLFunc, (ClientData) arrayInfo);
    DBReComputeBbox(Select2Def);
    UndoEnable();

    /* Now just delete the selection and recreate it from Select2Def,
     * copying into the edit cell along the way.
     */
    
    SelectDelete("arrayed", TRUE);
    SelectAndCopy2(EditRootDef);
}

/* Search function for paint.  Just make many copies of the paint
 * into Select2Def.  Always return 0 to keep the search alive.
 */

int
selArrayPFunc(rect, type, arrayInfo)
    Rect *rect;			/* Rectangle to be arrayed. */
    TileType type;		/* Type of tile. */
    ArrayInfo *arrayInfo;	/* How to array. */
{
    int y, nx, ny;
    Rect current;

    nx = arrayInfo->ar_xhi - arrayInfo->ar_xlo;
    if (nx < 0) nx = -nx;
    ny = arrayInfo->ar_yhi - arrayInfo->ar_ylo;
    if (ny < 0) ny = -ny;

    current = *rect;
    for ( ; nx >= 0; nx -= 1)
    {
	current.r_ybot = rect->r_ybot;
	current.r_ytop = rect->r_ytop;
	for (y = ny; y >= 0; y -= 1)
	{
	    DBPaint(Select2Def, &current, type);
	    current.r_ybot += arrayInfo->ar_ysep;
	    current.r_ytop += arrayInfo->ar_ysep;
	}
	current.r_xbot += arrayInfo->ar_xsep;
	current.r_xtop += arrayInfo->ar_xsep;
    }
    return 0;
}

/* Search function for cells.  Just make an arrayed copy of
 * each subcell found.
 */

    /* ARGSUSED */
int
selArrayCFunc(selUse, use, transform, arrayInfo)
    CellUse *selUse;		/* Use from selection (not used). */
    CellUse *use;		/* Use to be copied and arrayed. */
    Transform *transform;	/* Transform from use->cu_def to root. */
    ArrayInfo *arrayInfo;	/* Array characteristics desired. */
{
    CellUse *newUse;
    Transform tinv, newTrans;
    Rect tmp, oldBbox;

    /* When creating a new use, try to re-use the id from the old
     * one.  Only create a new one if the old id can't be used.
     */

    newUse = DBCellNewUse(use->cu_def, (char *) use->cu_id);
    if (!DBLinkCell(newUse, Select2Def))
    {
	freeMagic((char *) newUse->cu_id);
	newUse->cu_id = NULL;
	(void) DBLinkCell(newUse, Select2Def);
    }
    newUse->cu_expandMask = use->cu_expandMask;
    newUse->cu_flags = use->cu_flags;

    DBSetTrans(newUse, transform);
    GeoInvertTrans(transform, &tinv);
    DBMakeArray(newUse, &tinv, arrayInfo->ar_xlo,
	arrayInfo->ar_ylo, arrayInfo->ar_xhi, arrayInfo->ar_yhi,
	arrayInfo->ar_xsep, arrayInfo->ar_ysep);
    
    /* Set the array's transform so that its lower-left corner is in
     * the same place that it used to be.
     */
    
    GeoInvertTrans(&use->cu_transform, &tinv);
    GeoTransRect(&tinv, &use->cu_bbox, &tmp);
    GeoTransRect(transform, &tmp, &oldBbox);
    GeoTranslateTrans(&newUse->cu_transform,
	    oldBbox.r_xbot - newUse->cu_bbox.r_xbot,
	    oldBbox.r_ybot - newUse->cu_bbox.r_ybot,
	    &newTrans);
    DBSetTrans(newUse, &newTrans);

    if (DBCellFindDup(newUse, Select2Def) != NULL)
    {
	DBUnLinkCell(newUse, Select2Def);
	(void) DBCellDeleteUse(newUse);
    }
    else DBPlaceCell(newUse, Select2Def);

    return 0;
}

/* Search function for labels.  Similar to paint search function. */
/* modified by harry eaton to increment numbers in array labels */

    /* ARGSUSED */
int
selArrayLFunc(label, use, transform, arrayInfo)
    Label *label;		/* Label to be copied and replicated. */
    CellUse *use;		/* (unused) */
    Transform *transform;	/* Transform from coords of def to root. */
    ArrayInfo *arrayInfo;	/* How to replicate. */
{
    int y, nx, ny, rootJust, rootRotate;
    Point rootOffset;
    Rect original, current;
    char *astr;
    int labx, laby, xi, yi, only1;	/* numeric parts of label */

    char *nmPutNums();			/* Forward reference */

    nx = arrayInfo->ar_xhi - arrayInfo->ar_xlo;
    if (nx < 0) nx = -nx;
    ny = arrayInfo->ar_yhi - arrayInfo->ar_ylo;
    if (ny < 0) ny = -ny;

    GeoTransRect(transform, &label->lab_rect, &original);
    rootJust = GeoTransPos(transform, label->lab_just);
    rootRotate = GeoTransAngle(transform, label->lab_rotate);
    GeoTransPointDelta(transform, &label->lab_offset, &rootOffset);
    current = original;

    /* get the existing numbers in the label */
    nmGetNums(label->lab_text, &labx, &laby);

    xi = yi = 0;
    if (nx > 0 && ny > 0)
	only1 = 0;
    else
	only1 = 1;
    for ( ; nx >= 0; nx -= 1)
    {
	current.r_ybot = original.r_ybot;
	current.r_ytop = original.r_ytop;
	for (y = ny; y >= 0; y -= 1)
	{
	    /* Eliminate any duplicate labels.  Don't use type in comparing
	     * for duplicates, because the selection's type could change
	     * after it gets added to the edit cell.  Any label with
	     * the same text and position is considered a duplicate.
	     */
	    yi = ny - y;
	    astr = nmPutNums(label->lab_text, labx + xi, laby + yi);
	    DBEraseLabelsByContent(Select2Def, &current, -1, astr);
	    DBPutFontLabel(Select2Def, &current, label->lab_font,
			label->lab_size, rootRotate, &rootOffset,
			rootJust, astr, label->lab_type, label->lab_flags);
	    current.r_ybot += arrayInfo->ar_ysep;
	    current.r_ytop += arrayInfo->ar_ysep;
	    xi += only1;
	}
	xi += (1 - only1);
	current.r_xbot += arrayInfo->ar_xsep;
	current.r_xtop += arrayInfo->ar_xsep;
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *	SelectStretch --
 *
 * 	Move the selection a given amount in x (or y).  While moving,
 *	erase everything that the selection passes over, and stretch
 *	material behind the selection.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The edit cell is modified.  The selection is also modified
 *	and redisplayed.
 * ----------------------------------------------------------------------------
 */

void
SelectStretch(x, y)
    int x;			/* Amount to move in the x-direction. */
    int y;			/* Amount to move in the y-direction.  Must
				 * be zero if x is non-zero.
				 */
{
    Transform transform;
    int plane;
    Rect modifiedArea, editModified;
    extern int selStretchEraseFunc(), selStretchFillFunc();
				/* Forward declarations. */

    if ((x == 0) && (y == 0)) return;

    /* First of all, copy from SelectDef to Select2Def, moving the
     * selection along the way.
     */
    
    GeoTranslateTrans(&GeoIdentityTransform, x, y, &transform);
    selTransTo2(&transform);

    /* We're going to modify not just the old selection area or the new
     * one, but everything in-between too.  Remember this and tell the
     * displayer and DRC about it later.
     */

    modifiedArea = Select2Def->cd_extended;
    (void) GeoInclude(&SelectDef->cd_extended, &modifiedArea);
    GeoTransRect(&RootToEditTransform, &modifiedArea, &editModified);

    /* Delete the selection itself. */

    SelectDelete("stretched", TRUE);

    /* Next, delete all the material in front of each piece of paint in
     * the selection.
     */
    
    selStretchX = x;
    selStretchY = y;
    for (plane = PL_SELECTBASE; plane < DBNumPlanes; plane++)
    {
	(void) DBSrPaintArea((Tile *) NULL, Select2Def->cd_planes[plane],
		&TiPlaneRect, &DBAllButSpaceAndDRCBits, selStretchEraseFunc,
		(ClientData) &plane);
    }

    /* To achieve the stretch affect, fill in material behind the selection
     * everywhere that it used to touch other material in the edit cell.
     * This code first builds up a list of areas to paint, then paints them
     * (can't paint as we go because the new paint interacts with the
     * computation of what to stretch).
     */

    selStretchList = NULL;
    for (plane = PL_SELECTBASE; plane < DBNumPlanes; plane++)
    {
	(void) DBSrPaintArea((Tile *) NULL, Select2Def->cd_planes[plane],
		&TiPlaneRect, &DBAllButSpaceAndDRCBits, selStretchFillFunc,
		(ClientData) &plane);
    }

    /* Paint back the areas in the list. */

    while (selStretchList != NULL)
    {
	TileTypeBitMask tmask;
	TileType type = selStretchList->sa_type;

	TileType tloc;
	tloc = (type & TT_DIAGONAL) ?  ((type & TT_SIDE) ?
		(type & TT_RIGHTMASK) >> 14 : (type & TT_LEFTMASK)) : type;
	TTMaskSetOnlyType(&tmask, tloc);

	DBPaintValid(EditCellUse->cu_def, &selStretchList->sa_area, &tmask, type);
	freeMagic((char *) selStretchList);
	selStretchList = selStretchList->sa_next;
    }

    /* Paint the new translated selection back into the edit cell,
     * select it again, and tell DRC and display about what we
     * changed.
     */
    
    SelectAndCopy2(EditRootDef);
    DBWAreaChanged(EditCellUse->cu_def, &editModified, DBW_ALLWINDOWS,
	(TileTypeBitMask *) NULL);
    DRCCheckThis(EditCellUse->cu_def, TT_CHECKPAINT, &editModified);
}

/*
 * ----------------------------------------------------------------------------
 *	selStretchEraseFunc --
 *
 * 	Called by DBSrPaintArea during stretching for each tile in the
 *	new selection.  Erase the area that the tile swept out as it
 *	moved.
 *
 * Results:
 *	Always returns 0 to keep the search alive.
 *
 * Side effects:
 *	The edit cell is modified.
 * ----------------------------------------------------------------------------
 */

int
selStretchEraseFunc(tile, plane)
    Tile *tile;			/* Tile being moved in a stretch operation. */
    int *plane;			/* Plane of tiles being searched */
{
    Rect area, editArea;
    int planeNum;
    planeAndArea pa;
    TileType type, t;
    TileTypeBitMask tmpmask, mask, *residueMask;
    PaintUndoInfo ui;
    PaintResultType selStretchEraseTbl[NT];
    extern int selStretchEraseFunc2();

    if (IsSplit(tile))
	type = (SplitSide(tile)) ? SplitRightType(tile) :
			SplitLeftType(tile);
    else
	type = TiGetType(tile);

    TiToRect(tile, &area);

    /* Compute the area that this tile swept out (the current area is
     * its location AFTER moving), and erase everything that was in
     * its path.
     */

    if (selStretchX > 0)
	area.r_xbot -= selStretchX;
    else area.r_xtop -= selStretchX;
    if (selStretchY > 0)
	area.r_ybot -= selStretchY;
    else area.r_ytop -= selStretchY;

    /* Translate into edit coords and erase all material on the
     * tile's plane.
     */

    GeoTransRect(&RootToEditTransform, &area, &editArea);
 
    /* We need to erase all types that interact with "type", *not* all	*/
    /* types on "plane", due to the way stacked contacts are handled.	*/
    /* Contacts on different planes may stretch across one another	*/
    /* without effect, if the types are stackable.  Likewise, layer	*/
    /* types may stretch across contacts for which they are a residue.	*/

    planeNum = *plane;
    mask = DBPlaneTypes[planeNum];
    if (DBIsContact(type))
    {
	for (t = DBNumUserLayers; t < DBNumTypes; t++)
	{
	    if (t == type) continue;

	    /* Exclude all contact types for which this type should "pass through" */

	    if (TTMaskHasType(&mask, t))
	    {
		tmpmask = *DBResidueMask(t);
		if (TTMaskHasType(&tmpmask, type))
		{
		    TTMaskClearType(&tmpmask, type);
		    TTMaskClearMask(&mask, &tmpmask);
		}
	    }
	}
    }

    /* For stacked type tiles do not erase common residue
     * type of both contacts from layout.
     */

    if (type >= DBNumUserLayers)
    {
        t = (DBPlaneToResidue(type, planeNum));
        TTMaskClearType(&mask, t);
    }

    TTMaskAndMask(&mask, &DBActiveLayerBits);

    /* Erase all tile types specified in the processed mask.
     * Erase contacts in mask first using DBErase.  This prevents
     * contacts from leaving partial images of themselves on other
     * planes.  Then, remove the non-contact layers on this plane
     * only, such that contacts are not disturbed.
     */

    TTMaskZero(&tmpmask);
    	
    selStretchEraseTbl[TT_SPACE] = (PaintResultType)TT_SPACE;
    for (t = TT_SPACE + 1; t < DBNumUserLayers; t++)
    {
	selStretchEraseTbl[t] = (PaintResultType)t;
	if (TTMaskHasType(&mask, t))
	{
	    if (DBIsContact(t))
	    {
		if (t == type)
		    DBErase(EditCellUse->cu_def, &editArea, t);
		else
		    TTMaskSetType(&tmpmask, t);
	    }
	    else
		selStretchEraseTbl[t] = (PaintResultType)TT_SPACE;
	}
    }
    for (; t < DBNumTypes; t++)
	selStretchEraseTbl[t] = (PaintResultType)t;

    /* Remove conflicting contact types, where they exist */

    pa.pa_area = &editArea;
    pa.pa_plane = planeNum;
    pa.pa_mask = &tmpmask;
    DBSrPaintArea((Tile *)NULL, EditCellUse->cu_def->cd_planes[planeNum],
		&editArea, &tmpmask, selStretchEraseFunc2, (ClientData)&pa);

    ui.pu_pNum = planeNum;
    ui.pu_def = EditCellUse->cu_def;
    DBPaintPlane(EditCellUse->cu_def->cd_planes[planeNum], &editArea,
		selStretchEraseTbl, &ui);

    return 0;
}

int
selStretchEraseFunc2(tile, pa)
    Tile *tile;
    planeAndArea *pa;
{
    if (IsSplit(tile))
    {
	if (TTMaskHasType(pa->pa_mask, TiGetLeftType(tile)))
	    DBErase(EditCellUse->cu_def, pa->pa_area,
			DBPlaneToResidue(TiGetLeftType(tile), pa->pa_plane));
	if (TTMaskHasType(pa->pa_mask, TiGetRightType(tile)))
	    DBErase(EditCellUse->cu_def, pa->pa_area,
			DBPlaneToResidue(TiGetRightType(tile), pa->pa_plane));
    }
    else
	DBErase(EditCellUse->cu_def, pa->pa_area,
		DBPlaneToResidue(TiGetType(tile), pa->pa_plane));
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *	selStretchFillFunc --
 *
 * 	This function is invoked during stretching for each paint tile in
 *	the (new) selection.  It finds places in where the back-side of this
 *	tile borders space in the (new) selection, then looks for paint in
 *	the edit cell that borders the old location of the paint.  If the
 *	selection has been moved away from paint in the edit cell, additional
 *	material is filled in behind the selection.
 *
 * Results:
 *	Always returns 0 to keep the search alive.
 *
 * Side effects:
 *	Modifies the edit cell by painting material.
 * ----------------------------------------------------------------------------
 */

int
selStretchFillFunc(tile, plane)
    Tile *tile;			/* Tile in the old selection. */
    int *plane;			/* Plane of tile being searched */
{
    Rect area;
    extern int selStretchFillFunc2();

    TiToRect(tile, &area);

    /* Check the material just behind this paint (in the sense of the
     * stretch direction) for space in the selection and non-space in
     * the edit cell.
     */
    
    if (selStretchX > 0)
    {
	area.r_xtop = area.r_xbot;
	area.r_xbot -= 1;
    }
    else if (selStretchX < 0)
    {
	area.r_xbot = area.r_xtop;
	area.r_xtop += 1;
    }
    else if (selStretchY > 0)
    {
	area.r_ytop = area.r_ybot;
	area.r_ybot -= 1;
    }
    else
    {
	area.r_ybot = area.r_ytop;
	area.r_ytop += 1;
    }

    if (IsSplit(tile))
    {
	if (selStretchX > 0)
	    selStretchType = SplitLeftType(tile);
	else if (selStretchX < 0)
	    selStretchType = SplitRightType(tile);
	else if (selStretchY > 0)
	    selStretchType = (SplitDirection(tile) ? SplitLeftType(tile) :
			SplitRightType(tile));
	else if (selStretchY < 0)
	    selStretchType = (SplitDirection(tile) ? SplitRightType(tile) :
			SplitLeftType(tile));
	if (selStretchType == TT_SPACE) return 0;
    }
    else
	selStretchType = TiGetType(tile);

    /* The search functions invoked indirectly by the following procedure
     * call build up a list of areas to paint.
     */

    (void) DBSrPaintArea((Tile *) NULL,
	    Select2Def->cd_planes[*plane], &area,
	    &DBSpaceBits, selStretchFillFunc2, (ClientData) &area);
    
    return 0;
}

/* Second-level filling search function:  find all of the edit material
 * that intersects areas where space borders a selected paint tile.
 */

int
selStretchFillFunc2(tile, area)
    Tile *tile;				/* Space tile that borders selected
					 * paint.
					 */
    Rect *area;				/* A one-unit wide strip along the
					 * border (i.e. the area in which
					 * we're interested in space).
					 */
{
    Rect spaceArea, editArea;
    int pNum;

    extern int selStretchFillFunc3();

    TiToRect(tile, &spaceArea);

    /* Find out which portion of this space tile borders the selected
     * tile, transform it back to coords of the old selection and then
     * to edit coords, and find all the edit material that borders the
     * selected tile in this area.
     */

    GeoClip(&spaceArea, area);
    spaceArea.r_xbot -= selStretchX;
    spaceArea.r_xtop -= selStretchX;
    spaceArea.r_ybot -= selStretchY;
    spaceArea.r_ytop -= selStretchY;
    GeoTransRect(&RootToEditTransform, &spaceArea, &editArea);

    for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
    {
	if (DBTypeOnPlane(selStretchType, pNum))
	    (void) DBSrPaintArea((Tile *) NULL,
			EditCellUse->cu_def->cd_planes[pNum],
			&editArea, &DBActiveLayerBits, selStretchFillFunc3,
			(ClientData) &spaceArea);
    }
    return 0;
}

/* OK, now we've found a piece of material in the edit cell that is
 * right next to a piece of selected material that's about to move
 * away from it.  Stretch one or the other to fill the gap.  Use the
 * material that's moving as the stretch material unless it's a fixed-size
 * material and the other stuff is stretchable.
 */

int
selStretchFillFunc3(tile, area)
    Tile *tile;			/* Tile of edit material that's about to
				 * be left behind selection.
				 */
    Rect *area;			/* The border area we're interested in,
				 * in root coords.
				 */
{
    Rect editArea, rootArea;
    TileType type, stype;
    TileTypeBitMask *mask, *sMask, *tMask;
    StretchArea *sa;

    /* Compute the area to be painted. */

    TiToRect(tile, &editArea);
    GeoTransRect(&EditToRootTransform, &editArea, &rootArea);
    GeoClip(&rootArea, area);
    if (selStretchX > 0)
    {
	rootArea.r_xbot = rootArea.r_xtop;
	rootArea.r_xtop += selStretchX;
    }
    else if (selStretchX < 0)
    {
	rootArea.r_xtop = rootArea.r_xbot;
	rootArea.r_xbot += selStretchX;
    }
    else if (selStretchY > 0)
    {
	rootArea.r_ybot = rootArea.r_ytop;
	rootArea.r_ytop += selStretchY;
    }
    else
    {
	rootArea.r_ytop = rootArea.r_ybot;
	rootArea.r_ybot += selStretchY;
    }
    GeoTransRect(&RootToEditTransform, &rootArea, &editArea);

    /* Compute the material to be painted.  Be careful:  for contacts,
     * must use the master image.
     */
    
    if (IsSplit(tile))
    {
	if (selStretchX > 0)
	    type = SplitRightType(tile);
	else if (selStretchX < 0)
	    type = SplitLeftType(tile);
	else if (selStretchY > 0)
	    type = (SplitDirection(tile) ? SplitRightType(tile) :
			SplitLeftType(tile));
	else if (selStretchY < 0)
	    type = (SplitDirection(tile) ? SplitLeftType(tile) :
			SplitRightType(tile));
	if (type == TT_SPACE) return 0;   /* nothing to stretch */
    }
    else
	type = TiGetType(tile);
    sMask = DBResidueMask(selStretchType);

    /* If have type and stretch type are both contacts (fixed types)	*/
    /* and there exists a stacking contact type between them, then	*/
    /* we should be filling in the original type.			*/

    if (!DBIsContact(type) || !DBIsContact(selStretchType) ||
		(((stype = DBTechFindStacking(type, selStretchType))
		< DBNumUserLayers) && (stype >= TT_TECHDEPBASE)))
    {
	/* If type is a residue of selStretchType then we always 	*/
	/* fill with type.  Otherwise, we check the list of plow	*/
	/* types to see what is and isn't fixed.			*/

	if (!TTMaskHasType(sMask, type))
	    if (TTMaskHasType(&PlowFixedTypes, type)
			|| !TTMaskHasType(&PlowFixedTypes, selStretchType))
		type = selStretchType;
    }

    /* Otherwise, if we have two fixed types, and they both border the	*/
    /* selection area (as opposed to overlapping), and they share a	*/
    /* residue, then the type to paint is the shared residue type.	*/
    /* We must ignore the case type == selStretchType (fixed by Nishit,	*/
    /* 6/29/04.								*/

    else if (DBIsContact(type) && DBIsContact(selStretchType)
		&& (type != selStretchType))
    {
	if (((selStretchX < 0) && (editArea.r_xtop == area->r_xbot)) ||
	    ((selStretchX > 0) && (editArea.r_xbot == area->r_xtop)) ||
	    ((selStretchY < 0) && (editArea.r_ytop == area->r_ybot)) ||
	    ((selStretchY > 0) && (editArea.r_ybot == area->r_ytop)))
	{
	    TileTypeBitMask rmask;
	    tMask = DBResidueMask(type);
	    TTMaskAndMask3(&rmask, tMask, sMask);
	    for (type = TT_TECHDEPBASE; type < DBNumUserLayers; type++)
		if (TTMaskHasType(&rmask, type))
		    break;

	    if (type == DBNumUserLayers)
		return 0;	/* Types do not share any residues */
	}
    }

    /* Save around the area we just found. */

    sa = (StretchArea *) mallocMagic(sizeof(StretchArea));
    sa->sa_area = editArea;
    sa->sa_type = type;
    sa->sa_next = selStretchList;
    selStretchList = sa;

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * SelectDump --
 *
 *      Copies an area of one cell into the edit cell, selecting the
 *	copy so that it can be manipulated later.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The edit cell is modified.
 *
 * ----------------------------------------------------------------------------
 */

void
SelectDump(scx)
    SearchContext *scx;			/* Describes the cell from which
					 * material is to be copied, the
					 * area to copy, and the transform
					 * to root coordinates in the edit
					 * cell's window.
					 */
{
    /* Copy from the source cell to Select2Def while transforming,
     * then let SelectandCopy2 do the rest of the work.  Don't
     * record any of the Select2Def changes for undo-ing.
     */

    UndoDisable();
    DBCellClearDef(Select2Def);
    (void) DBCellCopyAllPaint(scx, &DBAllButSpaceAndDRCBits, CU_DESCEND_SPECIAL,
		Select2Use);
    (void) DBCellCopyAllLabels(scx, &DBAllTypeBits, CU_DESCEND_SPECIAL, Select2Use,
		(Rect *) NULL);
    (void) DBCellCopyAllCells(scx, CU_DESCEND_SPECIAL, Select2Use, (Rect *) NULL);
    DBReComputeBbox(Select2Def);
    UndoEnable();

    SelectClear();
    SelectAndCopy2(EditRootDef);
}
