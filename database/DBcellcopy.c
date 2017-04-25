/*
 * DBcellcopy.c --
 *
 * Cell copying (yank and stuff)
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/database/DBcellcopy.c,v 1.13 2010/06/24 12:37:15 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "utils/malloc.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "database/databaseInt.h"
#include "textio/textio.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "commands/commands.h"

/*
 * The following variable points to the tables currently used for
 * painting.  The paint tables are occasionally switched, by clients
 * like the design-rule checker, by calling DBNewPaintTable.  This
 * paint table applies only to the routine in this module.
 */
static PaintResultType (*dbCurPaintTbl)[NT][NT] = DBPaintResultTbl;

/*
 * The following variable points to the version of DBPaintPlane used
 * for painting during yanks.  This is occasionally switched by clients
 * such as the design-rule checker that need to use, for example,
 * DBPaintPlaneMark instead of the standard version.
 */
static void (*dbCurPaintPlane)() = DBPaintPlaneWrapper;

    /* Structure passed to DBTreeSrTiles() */
struct copyAllArg
{
    TileTypeBitMask	*caa_mask;	/* Mask of tile types to be copied */
    Rect		 caa_rect;	/* Clipping rect in target coords */
    CellUse		*caa_targetUse;	/* Use to which tiles are copied */
    Rect		*caa_bbox;	/* Bbox of material copied (in
					 * targetUse coords).  Used only when
					 * copying cells.
					 */
};

    /* Structure passed to DBSrPaintArea() */
struct copyArg
{
    TileTypeBitMask	*ca_mask;	/* Mask of tile types to be copied */
    Rect		 ca_rect;	/* Clipping rect in source coords */
    CellUse		*ca_targetUse;	/* Use to which tiles are copied */
    Transform		*ca_trans;	/* Transform to target */
};

    /* Structure passed to DBTreeSrLabels to hold information about
     * copying labels.
     */

struct copyLabelArg
{
    CellUse *cla_targetUse;		/* Use to which labels are copied. */
    Rect *cla_bbox;			/* If non-NULL, points to rectangle
					 * to be filled in with total area of
					 * all labels copied.
					 */
};

/*
 * ----------------------------------------------------------------------------
 *
 * DBPaintPlaneWrapper --
 *
 *    Simple wrapper to DBPaintPlane.
 *    Note that this function is passed as a pointer on occasion, so
 *    it cannot be replaced with a macro!
 *
 * ----------------------------------------------------------------------------
 */

void
DBPaintPlaneWrapper(def, pNum, type, area, undo)
    CellDef *def;
    int pNum;
    TileType type;
    Rect *area;
    PaintUndoInfo *undo;
{
    TileType loctype = type & TT_LEFTMASK;
    Rect expand;

    undo->pu_pNum = pNum;
    DBNMPaintPlane(def->cd_planes[pNum], type, area,
		dbCurPaintTbl[pNum][loctype], undo);
    GEO_EXPAND(area, 1, &expand);
    DBMergeNMTiles(def->cd_planes[pNum], &expand, undo);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBPaintPlaneMark --
 *
 *    Another wrapper function to DBPaintPlane.  This one is used for
 *    hierarchical DRC, and ensures that tiles are never painted twice
 *    on the same pass, so as not to cause false overlap errors.
 *
 * ----------------------------------------------------------------------------
 */

void
DBPaintPlaneMark(def, pNum, type, area, undo)
    CellDef *def;
    int pNum;
    TileType type;
    Rect *area;
    PaintUndoInfo *undo;
{
    TileType loctype = type & TT_LEFTMASK;

    undo->pu_pNum = pNum;
    DBNMPaintPlane0(def->cd_planes[pNum], type, area,
		dbCurPaintTbl[pNum][loctype], undo, (unsigned char)PAINT_MARK);
}

/*
 * ----------------------------------------------------------------------------
 *
 * ----------------------------------------------------------------------------
 */

void
DBPaintPlaneXor(def, pNum, type, area, undo)
    CellDef *def;
    int pNum;
    TileType type;
    Rect *area;
    PaintUndoInfo *undo;
{
    TileType loctype = type & TT_LEFTMASK;

    undo->pu_pNum = pNum;
    DBNMPaintPlane0(def->cd_planes[pNum], type, area,
		dbCurPaintTbl[pNum][loctype], undo, (unsigned char)PAINT_XOR);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBPaintPlaneActive ---
 *
 *    This function calls DBPaintPlane, but first checks if the type
 *    being painted is an active layer.  If the type is a contact,
 *    then the residues are checked to see if they are active layers. 
 *    Painting proceeds accordingly.
 *
 * ----------------------------------------------------------------------------
 */

void
DBPaintPlaneActive(def, pNum, type, area, undo)
    CellDef *def;
    int pNum;
    TileType type;
    Rect *area;
    PaintUndoInfo *undo;
{
    TileType loctype = type & TT_LEFTMASK;
    TileType t;

    if (DBIsContact(loctype))
    {
	TileTypeBitMask tmask, *rMask;

	rMask = DBResidueMask(loctype);
	TTMaskAndMask3(&tmask, rMask, &DBActiveLayerBits);
	if (!TTMaskEqual(&tmask, rMask))
	{
	    if (!TTMaskIsZero(&tmask))
		for (t = TT_TECHDEPBASE; t < DBNumUserLayers; t++)
		    if (TTMaskHasType(&tmask, t))
			DBPaintPlaneWrapper(def, pNum, t | (type &
				(TT_SIDE | TT_DIRECTION | TT_DIAGONAL)),
				area, undo);
	    return;
	}
    }
    if (TTMaskHasType(&DBActiveLayerBits, loctype))
	DBPaintPlaneWrapper(def, pNum, type, area, undo);
}

/*
 *-----------------------------------------------------------------------------
 *
 * DBCellCopyManhattanPaint --
 *
 * Copy paint from the tree rooted at scx->scx_use to the paint planes
 * of targetUse, transforming according to the transform in scx.
 * Only the types specified by typeMask are copied, and only Manhattan
 * geometry is copied.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the paint planes in targetUse.
 *
 *-----------------------------------------------------------------------------
 */

void
DBCellCopyManhattanPaint(scx, mask, xMask, targetUse)
    SearchContext *scx;		/* Describes root cell to search, area to
				 * copy, transform from root cell to coords
				 * of targetUse.
				 */
    TileTypeBitMask *mask;	/* Types of tiles to be yanked/stuffed */
    int xMask;			/* Expansion state mask to be used in search */
    CellUse *targetUse;		/* Cell into which material is to be stuffed */
{
    struct copyAllArg arg;
    int dbCopyManhattanPaint();

    arg.caa_mask = mask;
    arg.caa_targetUse = targetUse;
    GEOTRANSRECT(&scx->scx_trans, &scx->scx_area, &arg.caa_rect);

    (void) DBTreeSrTiles(scx, mask, xMask, dbCopyManhattanPaint, (ClientData) &arg);
}


/*
 *-----------------------------------------------------------------------------
 *
 * DBCellCopyAllPaint --
 *
 * Copy paint from the tree rooted at scx->scx_use to the paint planes
 * of targetUse, transforming according to the transform in scx.
 * Only the types specified by typeMask are copied.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the paint planes in targetUse.
 *
 *-----------------------------------------------------------------------------
 */

void
DBCellCopyAllPaint(scx, mask, xMask, targetUse)
    SearchContext *scx;		/* Describes root cell to search, area to
				 * copy, transform from root cell to coords
				 * of targetUse.
				 */
    TileTypeBitMask *mask;	/* Types of tiles to be yanked/stuffed */
    int xMask;			/* Expansion state mask to be used in search */
    CellUse *targetUse;		/* Cell into which material is to be stuffed */
{
    TileTypeBitMask locMask;
    struct copyAllArg arg;
    int dbCopyAllPaint();

    arg.caa_mask = mask;
    arg.caa_targetUse = targetUse;
    GEOTRANSRECT(&scx->scx_trans, &scx->scx_area, &arg.caa_rect);

    /* Add any stacking types for the search (but not to mask passed as arg!) */
    locMask = *mask;
    DBMaskAddStacking(&locMask);

    DBTreeSrTiles(scx, &locMask, xMask, dbCopyAllPaint, (ClientData) &arg);
}

/*
 *-----------------------------------------------------------------------------
 *
 * DBCellCopyAllLabels --
 *
 * Copy labels from the tree rooted at scx->scx_use to targetUse,
 * transforming according to the transform in scx.  Only labels
 * attached to layers of the types specified by mask are copied.
 * The area to be copied is determined by GEO_LABEL_IN_AREA.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Copies labels to targetUse, clipping against scx->scx_area.
 *	If pArea is given, store in it the bounding box of all the
 *	labels copied.
 *
 *-----------------------------------------------------------------------------
 */

void
DBCellCopyAllLabels(scx, mask, xMask, targetUse, pArea)
    SearchContext *scx;		/* Describes root cell to search, area to
				 * copy, transform from root cell to coords
				 * of targetUse.
				 */
    TileTypeBitMask *mask;	/* Only labels of these types are copied */
    int xMask;			/* Expansion state mask to be used in search */
    CellUse *targetUse;		/* Cell into which labels are to be stuffed */
    Rect *pArea;		/* If non-NULL, points to a box that will be
				 * filled in with bbox (in targetUse coords)
				 * of all labels copied.  Will be degenerate
				 * if nothing was copied.
				 */
{
    int dbCopyAllLabels();
    struct copyLabelArg arg;

    /* DBTeeSrLabels finds all the labels that we want plus some more.
     * We'll filter out the ones that we don't need.
     */
    
    arg.cla_targetUse = targetUse;
    arg.cla_bbox = pArea;
    if (pArea != NULL)
    {
	pArea->r_xbot = 0;
	pArea->r_xtop = -1;
    }
    (void) DBTreeSrLabels(scx, mask, xMask, (TerminalPath *) 0,
			TF_LABEL_ATTACH, dbCopyAllLabels,
			(ClientData) &arg);
}

    /*ARGSUSED*/
int
dbCopyAllLabels(scx, lab, tpath, arg)
    SearchContext *scx;
    Label *lab;
    TerminalPath *tpath;
    struct copyLabelArg *arg;
{
    Rect labTargetRect;
    Point labOffset;
    int targetPos, labRotate;
    CellDef *def;

    def = arg->cla_targetUse->cu_def;
    if (!GEO_LABEL_IN_AREA(&lab->lab_rect, &(scx->scx_area))) return 0;
    GeoTransRect(&scx->scx_trans, &lab->lab_rect, &labTargetRect);
    targetPos = GeoTransPos(&scx->scx_trans, lab->lab_just);
    GeoTransPointDelta(&scx->scx_trans, &lab->lab_offset, &labOffset);
    labRotate = GeoTransAngle(&scx->scx_trans, lab->lab_rotate);

    /* Eliminate duplicate labels.  Don't pay any attention to layers
     * in deciding on duplicates:  if text and position match, it's a
     * duplicate.
     */

    DBEraseLabelsByContent(def, &labTargetRect, -1, lab->lab_text);
    DBPutFontLabel(def, &labTargetRect, lab->lab_font,
		lab->lab_size, labRotate, &labOffset, targetPos,
		lab->lab_text, lab->lab_type, lab->lab_flags);
    if (arg->cla_bbox != NULL)
    {
	GeoIncludeAll(&labTargetRect, arg->cla_bbox);

	/* Rendered font labels include the bounding box of the text itself */
	if (lab->lab_font >= 0)
	{
	    GeoTransRect(&scx->scx_trans, &lab->lab_bbox, &labTargetRect);
	    GeoIncludeAll(&labTargetRect, arg->cla_bbox);
	}
    }
    return 0;
}

/*
 *-----------------------------------------------------------------------------
 *
 * DBCellCopyPaint --
 *
 * Copy paint from the paint planes of scx->scx_use to the paint planes
 * of targetUse, transforming according to the transform in scx.
 * Only the types specified by typeMask are copied.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the paint planes in targetUse.
 *
 *-----------------------------------------------------------------------------
 */

void
DBCellCopyPaint(scx, mask, xMask, targetUse)
    SearchContext *scx;		/* Describes cell to search, area to
				 * copy, transform from cell to coords
				 * of targetUse.
				 */
    TileTypeBitMask *mask;	/* Types of tiles to be yanked/stuffed */
    int xMask;			/* Expansion state mask to be used in search */
    CellUse *targetUse;		/* Cell into which material is to be stuffed */
{
    int pNum;
    PlaneMask planeMask;
    TreeContext cxp;
    TreeFilter filter;
    struct copyAllArg arg;
    int dbCopyAllPaint();

    if (!DBDescendSubcell(scx->scx_use, xMask))
	return;

    arg.caa_mask = mask;
    arg.caa_targetUse = targetUse;
    GeoTransRect(&scx->scx_trans, &scx->scx_area, &arg.caa_rect);

    /* Build dummy TreeContext */
    cxp.tc_scx = scx;
    cxp.tc_filter = &filter;
    filter.tf_arg = (ClientData) &arg;

    /* tf_func, tf_mask, tf_xmask, tf_planes, and tf_tpath are unneeded */

    planeMask = DBTechTypesToPlanes(mask);
    for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
	if (PlaneMaskHasPlane(planeMask, pNum))
	{
	    cxp.tc_plane = pNum; /* not used? */
	    (void) DBSrPaintArea((Tile *) NULL,
		scx->scx_use->cu_def->cd_planes[pNum], &scx->scx_area,
		mask, dbCopyAllPaint, (ClientData) &cxp);
	}
}

/*
 *-----------------------------------------------------------------------------
 *
 * DBCellCopyLabels --
 *
 * Copy labels from scx->scx_use to targetUse, transforming according to
 * the transform in scx.  Only labels attached to layers of the types
 * specified by mask are copied.  If mask contains the L_LABEL bit, then
 * all labels are copied regardless of their layer.  The area copied is 
 * determined by GEO_LABEL_IN_AREA.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the labels in targetUse.  If pArea is given, it will
 *	be filled in with the bounding box of all labels copied.
 *
 *-----------------------------------------------------------------------------
 */

void
DBCellCopyLabels(scx, mask, xMask, targetUse, pArea)
    SearchContext *scx;		/* Describes root cell to search, area to
				 * copy, transform from root cell to coords
				 * of targetUse.
				 */
    TileTypeBitMask *mask;	/* Only labels of these types are copied */
    int xMask;			/* Expansion state mask to be used in search */
    CellUse *targetUse;		/* Cell into which labels are to be stuffed */
    Rect *pArea;		/* If non-NULL, points to rectangle to be
				 * filled in with bbox (in targetUse coords)
				 * of all labels copied.  Will be degenerate
				 * if no labels are copied.
				 */
{
    Label *lab;
    CellDef *def = targetUse->cu_def;
    Rect labTargetRect;
    Rect *rect = &scx->scx_area;
    int targetPos, labRotate;
    Point labOffset;
    CellUse *sourceUse = scx->scx_use;

    if (pArea != NULL)
    {
	pArea->r_xbot = 0;
	pArea->r_xtop = -1;
    }

    if (!DBDescendSubcell(sourceUse, xMask))
	return;

    for (lab = sourceUse->cu_def->cd_labels; lab; lab = lab->lab_next)
	if (GEO_LABEL_IN_AREA(&lab->lab_rect, rect) &&
		(TTMaskHasType(mask, lab->lab_type)
		|| TTMaskHasType(mask, L_LABEL)))
	{
	    GeoTransRect(&scx->scx_trans, &lab->lab_rect, &labTargetRect);
	    targetPos = GeoTransPos(&scx->scx_trans, lab->lab_just);
	    GeoTransPointDelta(&scx->scx_trans, &lab->lab_offset, &labOffset);
	    labRotate = GeoTransAngle(&scx->scx_trans, lab->lab_rotate);


	    /* Eliminate duplicate labels.  Don't pay any attention to
	     * type when deciding on duplicates, since types can change
	     * later and then we'd have a duplicate.
	     */

	    DBEraseLabelsByContent(def, &labTargetRect, -1, lab->lab_text);
	    DBPutFontLabel(def, &labTargetRect, lab->lab_font,
			lab->lab_size, labRotate, &labOffset, targetPos,
			lab->lab_text, lab->lab_type, lab->lab_flags);
	    if (pArea != NULL)
		(void) GeoIncludeAll(&labTargetRect, pArea);
	}
}

/***
 *** Filter function for paint: Ignores diagonal (split) tiles for
 *** purposes of selection searches.
 ***/

int
dbCopyManhattanPaint(tile, cxp)
    Tile *tile;	/* Pointer to tile to copy */
    TreeContext *cxp;		/* Context from DBTreeSrTiles */
{
    SearchContext *scx = cxp->tc_scx;
    struct copyAllArg *arg;
    Rect sourceRect, targetRect;
    PaintUndoInfo ui;
    CellDef *def;
    TileType type;
    int pNum = cxp->tc_plane;

    /*
     * Don't copy space tiles -- this copy is additive.
     * We should never get passed a space tile, though, because
     * the caller will be using DBSrPaintArea, so this is just
     * a sanity check.
     */

    type = TiGetTypeExact(tile);
    if (type == TT_SPACE || (type & TT_DIAGONAL))
	return 0;

    arg = (struct copyAllArg *) cxp->tc_filter->tf_arg;

    /* Construct the rect for the tile in source coordinates */
    TITORECT(tile, &sourceRect);

    /* Transform to target coordinates */
    GEOTRANSRECT(&scx->scx_trans, &sourceRect, &targetRect);

    ui.pu_def = def = arg->caa_targetUse->cu_def;
    def->cd_flags |= CDMODIFIED|CDGETNEWSTAMP;

    /* Clip against the target area */
    GEOCLIP(&targetRect, &arg->caa_rect);

    (*dbCurPaintPlane)(def, pNum, type, &targetRect, &ui);
    return (0);
}


/***
 *** Filter function for paint
 ***/

int
dbCopyAllPaint(tile, cxp)
    Tile *tile;	/* Pointer to tile to copy */
    TreeContext *cxp;		/* Context from DBTreeSrTiles */
{
    SearchContext *scx = cxp->tc_scx;
    struct copyAllArg *arg;
    Rect sourceRect, targetRect;
    PaintUndoInfo ui;
    CellDef *def;
    TileType type = TiGetTypeExact(tile);
    int pNum = cxp->tc_plane;
    TileTypeBitMask *typeMask;

    /*
     * Don't copy space tiles -- this copy is additive.
     * We should never get passed a space tile, though, because
     * the caller will be using DBSrPaintArea, so this is just
     * a sanity check.
     */

    bool splittile = FALSE;
    TileType dinfo = 0;
    
    if (IsSplit(tile))
    {
	splittile = TRUE;
	dinfo = DBTransformDiagonal(type, &scx->scx_trans);
	type = (SplitSide(tile)) ? SplitRightType(tile) :
			SplitLeftType(tile);
    }

    if (type == TT_SPACE)
	return 0;

    arg = (struct copyAllArg *) cxp->tc_filter->tf_arg;
    typeMask = arg->caa_mask;

    /* Resolve what type we're going to paint, based on the type and mask */
    if (!TTMaskHasType(typeMask, type))
    {
	TileTypeBitMask rMask, *tmask;

	/* Simple case---typeMask has a residue of type on pNum */
	tmask = DBResidueMask(type);
	TTMaskAndMask3(&rMask, typeMask, tmask);
	TTMaskAndMask(&rMask, &DBPlaneTypes[pNum]);
	if (!TTMaskIsZero(&rMask))
	{
	    for (type = TT_TECHDEPBASE; type < DBNumUserLayers; type++)
		if (TTMaskHasType(&rMask, type))
		    break;
	    if (type == DBNumUserLayers) return 0;	/* shouldn't happen */

	    /* Hopefully there's always just one type here---sanity check */
	    TTMaskClearType(&rMask, type);
	    if (!TTMaskIsZero(&rMask))
	    {
		/* Diagnostic */
		TxError("Bad assumption:  Multiple types to paint!  Fix me!\n");
	    }
	}
	else
	{
	    type = DBPlaneToResidue(type, pNum);
	    if (!TTMaskHasType(typeMask, type)) return 0;
	}
    }

    /* Construct the rect for the tile in source coordinates */
    TITORECT(tile, &sourceRect);

    /* Transform to target coordinates */
    GEOTRANSRECT(&scx->scx_trans, &sourceRect, &targetRect);

    ui.pu_def = def = arg->caa_targetUse->cu_def;

    def->cd_flags |= CDMODIFIED|CDGETNEWSTAMP;

    /* Nonmanhattan geometry requires slightly different handling. */
    /* Paint the whole tile and clip by erasing areas outside the  */
    /* clipping rectangle.					   */
    if (splittile)
    {
	Point points[5];
	Rect rrect, orect;
	int np, i, j;

	GrClipTriangle(&targetRect, &arg->caa_rect, TRUE, dinfo, points, &np);
	
	if (np == 0)
	   return(0);

	if (np >= 3)
	{
	    for (i = 0; i < np; i++)
	    {
		j = (i + 1) % np;
		if (points[i].p_x != points[j].p_x && points[i].p_y !=
				points[j].p_y)
		{
		    /* Break out the triangle */
		    rrect.r_xbot = points[i].p_x;
		    rrect.r_xtop = points[j].p_x;
		    rrect.r_ybot = points[i].p_y;
		    rrect.r_ytop = points[j].p_y;
		    GeoCanonicalRect(&rrect, &targetRect);
		    break;
		}
	    }
	    if (i == np)  /* Exactly one Manhattan rectangle */
	    {
		rrect.r_xbot = points[0].p_x;
		rrect.r_xtop = points[2].p_x;
		rrect.r_ybot = points[0].p_y;
		rrect.r_ytop = points[2].p_y;
		GeoCanonicalRect(&rrect, &targetRect);
		dinfo = 0;
	    }
	    else if (np >= 4) /* Process extra rectangles in the area */
	    {
		/* "orect" is the bounding box of the polygon returned	*/
		/* by ClipTriangle.					*/

		orect.r_xtop = orect.r_xbot = points[0].p_x;
		orect.r_ytop = orect.r_ybot = points[0].p_y;
		for (i = 0; i < np; i++)
		    GeoIncludePoint(&points[i], &orect);

		/* Rectangle to left or right */
		rrect.r_ybot = orect.r_ybot;
		rrect.r_ytop = orect.r_ytop;
		if (targetRect.r_xbot > orect.r_xbot)
		{
		    rrect.r_xbot = orect.r_xbot;
		    rrect.r_xtop = targetRect.r_xbot;
		}
		else if (targetRect.r_xtop < orect.r_xtop)
		{
		    rrect.r_xtop = orect.r_xtop;
		    rrect.r_xbot = targetRect.r_xtop;
		}
		else
		    goto topbottom;

		(*dbCurPaintPlane)(def, pNum, type, &rrect, &ui);

topbottom:
		/* Rectangle to top or bottom */
		rrect.r_xbot = targetRect.r_xbot;
		rrect.r_xtop = targetRect.r_xtop;
		if (targetRect.r_ybot > orect.r_ybot)
		{
		    rrect.r_ybot = orect.r_ybot;
		    rrect.r_ytop = targetRect.r_ybot;
		}
		else if (targetRect.r_ytop < orect.r_ytop)
		{
		    rrect.r_ytop = orect.r_ytop;
		    rrect.r_ybot = targetRect.r_ytop;
		}
		else
		    goto splitdone;

		(*dbCurPaintPlane)(def, pNum, type, &rrect, &ui);
	    }
	}
    }
    else
	/* Clip against the target area */
	GEOCLIP(&targetRect, &arg->caa_rect);

splitdone:

    (*dbCurPaintPlane)(def, pNum, dinfo | type, &targetRect, &ui);

    return (0);
}

/*
 *-----------------------------------------------------------------------------
 *
 * DBCellCopyAllCells --
 *
 * Copy unexpanded subcells from the tree rooted at scx->scx_use
 * to the subcell plane of targetUse, transforming according to
 * the transform in scx.
 *
 * This effectively "flattens" a cell hierarchy in the sense that
 * all unexpanded subcells in a region (which would appear in the
 * display as bounding boxes) are copied into targetUse without
 * regard for their original location in the hierarchy of scx->scx_use.
 * If an array is unexpanded, it is copied as an array, not as a
 * collection of individual cells.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the cell plane in targetUse.  If pArea is given, it
 *	will be filled in with the total area of all cells copied.
 *
 *-----------------------------------------------------------------------------
 */

void
DBCellCopyAllCells(scx, xMask, targetUse, pArea)
    SearchContext *scx;		/* Describes root cell to search, area to
				 * copy, transform from root cell to coords
				 * of targetUse.
				 */
    CellUse *targetUse;		/* Cell into which material is to be stuffed */
    int xMask;			/* Expansion state mask to be used in
				 * searching.  Cells not expanded according
				 * to this mask are copied.  To copy everything
				 * in the subtree under scx->scx_use without
				 * regard to expansion, pass a mask of 0.
				 */
    Rect *pArea;		/* If non-NULL, points to a rectangle to be
				 * filled in with bbox (in targetUse coords)
				 * of all cells copied.  Will be degenerate
				 * if nothing was copied.
				 */
{
    struct copyAllArg arg;
    int dbCellCopyCellsFunc();

    arg.caa_targetUse = targetUse;
    arg.caa_bbox = pArea;
    if (pArea != NULL)
    {
	pArea->r_xbot = 0;		/* Make bounding box empty initially. */
	pArea->r_xtop = -1;
    }
    GeoTransRect(&scx->scx_trans, &scx->scx_area, &arg.caa_rect);

    (void) DBTreeSrCells(scx, xMask, dbCellCopyCellsFunc, (ClientData) &arg);
}

/*
 *-----------------------------------------------------------------------------
 *
 * DBCellCopyCells --
 *
 * Copy all subcells that are immediate children of scx->scx_use->cu_def
 * into the subcell plane of targetUse, transforming according to
 * the transform in scx.  Arrays are copied as arrays, not as a
 * collection of individual cells.  If a cell is already present in
 * targetUse that would be exactly duplicated by a new cell, the new
 * cell isn't copied.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the cell plane in targetUse.  If pArea is given, it will
 *	be filled in with the bounding box of all cells copied.
 *
 *-----------------------------------------------------------------------------
 */

void
DBCellCopyCells(scx, targetUse, pArea)
    SearchContext *scx;		/* Describes root cell to search, area to
				 * copy, transform from coords of
				 * scx->scx_use->cu_def to coords of targetUse.
				 */
    CellUse *targetUse;		/* Cell into which material is to be stuffed */
    Rect *pArea;		/* If non-NULL, points to rectangle to be
				 * filled in with bbox (in targetUse coords)
				 * of all cells copied.  Will be degenerate
				 * if nothing was copied.
				 */
{
    struct copyAllArg arg;
    int dbCellCopyCellsFunc();

    arg.caa_targetUse = targetUse;
    arg.caa_bbox = pArea;
    if (pArea != NULL)
    {
	pArea->r_xbot = 0;
	pArea->r_xtop = -1;
    }
    GeoTransRect(&scx->scx_trans, &scx->scx_area, &arg.caa_rect);

    (void) DBCellSrArea(scx, dbCellCopyCellsFunc, (ClientData) &arg);
}

/*
 *-----------------------------------------------------------------------------
 *
 * dbCellCopyCellsFunc --
 *
 * Do the actual work of yanking cells for DBCellCopyAllCells() and
 * DBCellCopyCells() above.
 *
 * Results:
 *	Always return 2.
 *
 * Side effects:
 *	Updates the cell plane in arg->caa_targetUse->cu_def.
 *
 *-----------------------------------------------------------------------------
 */

int
dbCellCopyCellsFunc(scx, arg)
    SearchContext *scx;	/* Pointer to search context containing
					 * ptr to cell use to be copied,
					 * and transform to the target def.
					 */
    struct copyAllArg *arg;	/* Client data from caller */
{
    CellUse *use, *newUse;
    CellDef *def;
    int xsep, ysep, xbase, ybase;
    Transform newTrans;

    use = scx->scx_use;
    def = use->cu_def;

    /* Don't allow circular structures! */

    if (DBIsAncestor(def, arg->caa_targetUse->cu_def))
    {
	TxPrintf("Copying %s would create a circularity in the",
	    def->cd_name);
	TxPrintf(" cell hierarchy \n(%s is already its ancestor)",
	    arg->caa_targetUse->cu_def->cd_name);
	TxPrintf(" so cell not copied.\n");
	return 2;
    }

    /* When creating a new use, try to re-use the id from the old
     * one.  Only create a new one if the old id can't be used.
     */

    newUse = DBCellNewUse(def, (char *) use->cu_id);
    if (!DBLinkCell(newUse, arg->caa_targetUse->cu_def))
    {
	freeMagic((char *) newUse->cu_id);
	newUse->cu_id = NULL;
	(void) DBLinkCell(newUse, arg->caa_targetUse->cu_def);
    }
    newUse->cu_expandMask = use->cu_expandMask;
    newUse->cu_flags = use->cu_flags;

    /* The translation stuff is funny, since we got one element of
     * the array, but not necessarily the lower-left element.  To
     * get the transform for the array as a whole, subtract off fo
     * the index of the element.  The easiest way to see how this
     * works is to look at the code in dbCellSrFunc;  the stuff here
     * is the opposite.
     */

    if (use->cu_xlo > use->cu_xhi) xsep = -use->cu_xsep;
    else xsep = use->cu_xsep;
    if (use->cu_ylo > use->cu_yhi) ysep = -use->cu_ysep;
    else ysep = use->cu_ysep;
    xbase = xsep * (scx->scx_x - use->cu_xlo);
    ybase = ysep * (scx->scx_y - use->cu_ylo);
    GeoTransTranslate(-xbase, -ybase, &scx->scx_trans, &newTrans);
    DBSetArray(use, newUse);
    DBSetTrans(newUse, &newTrans);
    if (DBCellFindDup(newUse, arg->caa_targetUse->cu_def) != NULL)
    {
	if (!(arg->caa_targetUse->cu_def->cd_flags & CDINTERNAL))
	{
	    TxError("Cell \"%s\" would end up on top of an identical copy\n",
		newUse->cu_id);
	    TxError("    of itself.  I'm going to forget about the");
	    TxError(" new copy.\n");
	}
	DBUnLinkCell(newUse, arg->caa_targetUse->cu_def);
	(void) DBCellDeleteUse(newUse);
    }
    else
    {
	DBPlaceCell(newUse, arg->caa_targetUse->cu_def);
	if (arg->caa_bbox != NULL)
	    (void) GeoIncludeAll(&newUse->cu_bbox, arg->caa_bbox);
    }
    return 2;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBNewPaintTable --
 *
 * 	This procedure changes the paint table to be used by the
 *	DBCellCopyPaint and DBCellCopyAllPaint procedures.
 *
 * Results:
 *	The return value is the address of the paint table that used
 *	to be in effect.  It is up to the client to restore this
 *	value with another call to this procedure.
 *
 * Side effects:
 *	A new paint table takes effect.  However, if newTable is NULL,
 *	then the old paint table remains active.  This allows one to
 *	get a pointer to the active paint table without altering it.
 *
 * ----------------------------------------------------------------------------
 */

PaintResultType (*
DBNewPaintTable(newTable))[NT][NT]
    PaintResultType (*newTable)[NT][NT];  /* Address of new paint table. */
{
    PaintResultType (*oldTable)[NT][NT] = dbCurPaintTbl;
    if (newTable) dbCurPaintTbl = newTable;
    return oldTable;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBNewPaintPlane --
 *
 * 	This procedure changes the painting procedure to be used by the
 *	DBCellCopyPaint and DBCellCopyAllPaint procedures.
 *
 * Results:
 *	The return value is the address of the paint procedure that
 *	used to be in effect.  It is up to the client to restore this
 *	value with another call to this procedure.
 *
 * Side effects:
 *	A new paint procedure takes effect.
 *
 * ----------------------------------------------------------------------------
 */

VoidProc
DBNewPaintPlane(newProc)
    void (*newProc)();		/* Address of new procedure */
{
    void (*oldProc)() = dbCurPaintPlane;
    dbCurPaintPlane = newProc;
    return (oldProc);
}
