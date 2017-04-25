/*
 * PlowYank.c --
 *
 * Plowing.
 * Incremental yanking, and painting the results back into the
 * original cell when done.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/plow/PlowYank.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/undo.h"
#include "debug/debug.h"
#include "plow/plow.h"
#include "plow/plowInt.h"
#include "textio/textio.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "drc/drc.h"

    /* Yank buffer used to hold the actual geometry being plowed */
CellDef *plowYankDef;
CellUse *plowYankUse;

    /* Incremental yanking information */
CellDef *plowSpareDef;		/* Def into which we do each incremental yank */
CellUse *plowSpareUse;		/* Use of above */
Rect plowYankedArea;		/* Area yanked so far.  Cells may extend outside
				 * this area, so when we do incremental yanking,
				 * we need to avoid copying any cells that were
				 * already yanked on a previous pass.
				 */
int plowYankHalo;		/* Halo around each edge.  If this halo extends
				 * outside the area already yanked, or touches
				 * its border, we yank more.
				 */

    /* Imports from the rest of this module */
extern Rect plowCellBbox;
extern Transform plowYankTrans;
extern Transform plowInverseTrans;
extern int plowDirection;
extern bool plowLabelsChanged;

    /* Dummy whose cu_def pointer is reset to point to the def being plowed */
extern CellUse *plowDummyUse;


    /* Forward declarations */
int plowYankUpdateCell();
int plowYankUpdatePaint();
int plowCheckLabel();

/*
 * ----------------------------------------------------------------------------
 *
 * plowYankMore --
 *
 * Yank more area from the original cell.  This is a little tricky in
 * the case of subcells, because we don't always update plowYankedArea
 * to enclose all subcell bounding boxes.  This means that a subcell
 * may appear in more than a single yank area.  To combat this, we do
 * not actually copy a cell if it already has been copied (which we
 * determine based on the use-id).
 *
 * The rectangle causing us to yank more area is 'area'.  To try to
 * make the cost of yanking stay roughly linear in the area plowed,
 * we try to make the area grow by the same factor each time we yank
 * more.
 *
 * We don't do anything if we don't have to yank any more area; this
 * is the case if 'area' plus its halo lies entirely inside plowYankedArea.
 *
 * Results:
 *	TRUE if we yanked more, FALSE if not.
 *
 * Side effects:
 *	May yank more area from the original cell.
 *	The spare cell (plowSpareDef) is always left cleared.
 *
 * ----------------------------------------------------------------------------
 */

bool
plowYankMore(area, halo, back)
    Rect *area;
    int halo;			/* Distance to right, top, bottom of area that
				 * must have been yanked.
				 */
    int back;			/* Distance to left that must have been
				 * yanked.
				 */
{
    Rect grownR, newArea, oldArea;
    SearchContext scx;
    int xsize, ysize;
    CellDef tmpDef;
    int pNum;

    grownR.r_xbot = area->r_xbot - back;
    grownR.r_xtop = area->r_xtop + halo;
    grownR.r_ybot = area->r_ybot - halo;
    grownR.r_ytop = area->r_ytop + halo;
    GEOCLIP(&grownR, &plowCellBbox);

    /* Don't have to yank if entirely within the yanked area */
    if (GEO_SURROUND_STRONG(&plowYankedArea, &grownR))
	return (FALSE);

    /* Figure out the additional area to yank */
    xsize = (plowYankedArea.r_xtop - plowYankedArea.r_xbot) >> 1;
    ysize = (plowYankedArea.r_ytop - plowYankedArea.r_ybot) >> 1;
    newArea = plowYankedArea;
    if (grownR.r_xbot <= plowYankedArea.r_xbot) newArea.r_xbot -= xsize >> 1;
    if (grownR.r_xtop >= plowYankedArea.r_xtop) newArea.r_xtop += xsize;
    if (grownR.r_ybot <= plowYankedArea.r_ybot) newArea.r_ybot -= ysize;
    if (grownR.r_ytop >= plowYankedArea.r_ytop) newArea.r_ytop += ysize;
    (void) GeoInclude(&grownR, &newArea);

    /* Clip to the cell bbox; if inside the existing yanked area, we're done */
    GEOCLIP(&newArea, &plowCellBbox);
    if (GEO_SURROUND(&plowYankedArea, &newArea))
	return (FALSE);
    oldArea = plowYankedArea;
    plowYankedArea = newArea;

    /*
     * Work to do.
     * Right now, we use a very simple approach:
     *
     *		Yank the complete area into a separate set of tile planes.
     *		Use the original set to update cu_clients and trailing tile
     *			coordinates.
     *
     * Run without undo since we're mucking with yank cells.
     */
    
	/* Yank the larger area into the spare cell */
    UndoDisable();
    scx.scx_use = plowDummyUse;
    scx.scx_trans = plowYankTrans;
    GeoTransRect(&plowInverseTrans, &plowYankedArea, &scx.scx_area);
    (void) DBCellCopyPaint(&scx, &DBAllButSpaceAndDRCBits, 0, plowSpareUse);
    (void) DBCellCopyCells(&scx, plowSpareUse, (Rect *) NULL);

	/*
	 * Update cell positions in the new cell.
	 * The following loop executes as many times as there
	 * are cells in the original yank def; each cell gets
	 * deleted and replaces its counterpart in the new yank
	 * buffer.
	 */
    while (DBCellEnum(plowYankDef, plowYankUpdateCell, (ClientData) NULL))
	/* Nothing */;

	/*
	 * Update paint edges in the new cell.
	 * We extend the search one lambda to the right to catch the
	 * trailing edges of space tiles in the original yank buffer.
	 */
    oldArea.r_xtop++;
    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
    {
	(void) DBSrPaintArea((Tile *) NULL, plowYankDef->cd_planes[pNum],
			&oldArea, &DBAllTypeBits, plowYankUpdatePaint,
			(ClientData) pNum);
    }

	/* Switch the yank cell and the spare cell */
    DBCellClearDef(plowYankDef);
    DBCellSetAvail(plowYankDef);
    DBCellCopyDefBody(plowYankDef, &tmpDef);
    DBCellCopyDefBody(plowSpareDef, plowYankDef);
    DBCellCopyDefBody(&tmpDef, plowSpareDef);

    UndoEnable();

    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowYankUpdateCell --
 *
 * Called for each cell in the old yank buffer.  We search the
 * new yank buffer for the corresponding cell (same child def,
 * same use-id) delete the cell from the new yank buffer, and
 * replace it with the cell from the old one.
 *
 * We have to do this, rather than simply updating the deltas
 * in the new yank buffer, because edges in the queue may have
 * pointers to the uses from the old yank buffer.
 *
 * Results:
 *	Always returns 1.
 *
 * Side effects:
 *	Moves its argument cell over to plowSpareUse, replacing
 *	the cell by the same name in plowSpareUse.
 *
 * ----------------------------------------------------------------------------
 */

int
plowYankUpdateCell(yankChildUse)
    CellUse *yankChildUse;	/* Use in the yank cell */
{
    CellUse *spareChildUse;
    ClientData savedelta;

    savedelta = yankChildUse->cu_client;
    for (spareChildUse = yankChildUse->cu_def->cd_parents;
	    spareChildUse;
	    spareChildUse = spareChildUse->cu_nextuse)
    {
	if (spareChildUse->cu_parent == plowSpareDef
		&& strcmp(spareChildUse->cu_id, yankChildUse->cu_id) == 0)
	{
	    /* Delete the use from the new yank def */
	    DBDeleteCell(spareChildUse);

	    /* Move the use from the old yank def to the new yank def */
	    DBDeleteCell(yankChildUse);
	    DBPlaceCell(yankChildUse, plowSpareDef);

	    /* Restore the delta since DBPlaceCell re-initializes it to 0 */
	    yankChildUse->cu_client = savedelta;

	    /* Return 1 so TiSrArea doesn't bomb */
	    return (1);
	}
    }

    TxError("Couldn't find use %s in spare yank buffer\n", yankChildUse->cu_id);
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowYankUpdatePaint --
 *
 * Called for each paint tile in the old yank buffer.  We search the new
 * yank buffer for the corresponding tile.  The new yank buffer's tile
 * may need to be clipped at its top and bottom if it is bigger than the
 * old yank buffer's tile.  We set the TRAILING coordinate of the clipped
 * tile to that of the old yank buffer's tile.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	May update the TRAILING coordinate of the corresponding tile
 *	in plowSpareUse.
 *
 * ----------------------------------------------------------------------------
 */

int
plowYankUpdatePaint(yankTp, pNum)
    Tile *yankTp;
    int pNum;
{
    Tile *spareTp;
    Point startPoint;
    Plane *plane;

    /*
     * Walk down the inside of the LHS of yankTp, finding all tiles
     * spareTp along that LHS and updating their client values.
     * There may be more than one spareTp along this edge, owing
     * to additional material added outside the original yank buffer
     * that caused re-merging into maximal horizontal strips.
     */
    startPoint.p_x = LEFT(yankTp);
    startPoint.p_y = TOP(yankTp) - 1;
    plane = plowSpareDef->cd_planes[pNum];
    spareTp = (Tile *) NULL;
    do
    {
	spareTp = TiSrPoint(spareTp, plane, &startPoint);
	/*
	 * Only update if both tiles are of the same type.
	 * This should always be true except when yankTp is a space tile
	 * from the RHS of the original yank buffer that lies over a
	 * non-space tile in the new yank buffer (spare).
	 */
	if (TiGetTypeExact(spareTp) == TiGetTypeExact(yankTp))
	{
	    if (TOP(spareTp) > TOP(yankTp))
		(void) plowSplitY(spareTp, TOP(yankTp));
	    if (BOTTOM(spareTp) < BOTTOM(yankTp))
		spareTp = plowSplitY(spareTp, BOTTOM(yankTp));
	    spareTp->ti_client = yankTp->ti_client;
	}

	startPoint.p_y = BOTTOM(spareTp) - 1;
    } while (startPoint.p_y >= BOTTOM(yankTp));

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowUpdateLabels --
 *
 * For each label in the original cell that is attached to geometry that
 * has moved in the plow yank cell, determine how far the label must move
 * and move it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Moves labels.
 *	Sets plowLabelsChanged if we actually move a label.
 *
 * ----------------------------------------------------------------------------
 */

struct labelUpdate
{
    Rect	 lu_rect;	/* Label itself */
    int		 lu_adjust;	/* How much to adjust it rightwards */
};

void
plowUpdateLabels(yankDef, origDef, origArea)
    CellDef *yankDef;	/* Def containing plowed paint */
    CellDef *origDef;	/* Original def whose labels are to be adjusted */
    Rect *origArea;	/* Area in original def that was modified */
{
    extern void DBUndoPutLabel(), DBUndoEraseLabel();
    Rect yankSearch;
    TileTypeBitMask typeBits;
    struct labelUpdate lu;
    Label *origLab;
    int pNum;

    for (origLab = origDef->cd_labels; origLab; origLab = origLab->lab_next)
    {
	/* Labels attached to space don't ever move */
	if (origLab->lab_type == TT_SPACE)
	    continue;

	/* Labels outside the area changed don't move either */
	if (!GEO_TOUCH(&origLab->lab_rect, origArea))
	    continue;

	/*
	 * If any of the tiles to which this label "belongs"
	 * moved far enough to touch or pass this label, drag
	 * it along with them.
	 */
	pNum = DBPlane(origLab->lab_type);
	GeoTransRect(&plowYankTrans, &origLab->lab_rect, &lu.lu_rect);
	lu.lu_adjust = 0;
	yankSearch = lu.lu_rect;
	yankSearch.r_xbot--, yankSearch.r_xtop++;
	yankSearch.r_ybot--, yankSearch.r_ytop++;
	TTMaskSetOnlyType(&typeBits, origLab->lab_type);
	(void) DBSrPaintArea((Tile *) NULL, yankDef->cd_planes[pNum],
			&yankSearch, &typeBits, plowCheckLabel,
			(ClientData) &lu);
	if (lu.lu_adjust)
	{
	    lu.lu_rect.r_xbot += lu.lu_adjust;
	    lu.lu_rect.r_xtop += lu.lu_adjust;
	    DBUndoEraseLabel(origDef, origLab);
	    GeoTransRect(&plowInverseTrans, &lu.lu_rect, &origLab->lab_rect);
	    DBUndoPutLabel(origDef, origLab);
	    plowLabelsChanged = TRUE;
	}
    }
}

int
plowCheckLabel(tile, lu)
    Tile *tile;
    struct labelUpdate *lu;
{
    int adjust;

    /*
     * If the RHS of the label touches the RHS of this tile, move
     * the label with the leading edge; otherwise, move it with the
     * trailing edge.
     */
    if (lu->lu_rect.r_xtop == RIGHT(tile))
	adjust = LEADING(tile) - lu->lu_rect.r_xtop;
    else
	adjust = TRAILING(tile) - lu->lu_rect.r_xbot;

    if (adjust > lu->lu_adjust)
	lu->lu_adjust = adjust;

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowUpdateCell --
 *
 * Called by DBCellEnum().
 * Update the positions of each cell moved by plowing.  The cu_client
 * fields of various cells in the cell plane of yankDef have been updated
 * as a result of plowing.  For each cell with a non-zero cu_client, we
 * find the corresponding one in the parent (the use with the same
 * instance-id), rip it up, and replace it.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Sets plowLabelsChanged if we have to move a cell.
 *	May cause the cell corresponding to 'use' to be re-placed
 *	in the original def, 'origDef'.
 *
 * ----------------------------------------------------------------------------
 */

int
plowUpdateCell(use, origDef)
    CellUse *use;	/* Use from yank def; corresponding use in 'def' will
			 * be moved if use->cu_client is non-zero.
			 */
    CellDef *origDef;	/* Original def whose cells are to be adjusted based
			 * on the corresponding cells in yankDef.
			 */
{
    CellUse *origUse;
    Transform newTrans;
    int x, y;

    if (use->cu_client == (ClientData)CLIENTDEFAULT || use->cu_client == (ClientData)0)
	return (0);

    /*
     * Find the corresponding use in the original parent.
     * This will be a use of the same def with the same
     * instance identifier; if we can't find it, something
     * very strange is going on.
     */
    for (origUse = use->cu_def->cd_parents;
	    origUse;
	    origUse = origUse->cu_nextuse)
    {
	if (origUse->cu_parent == plowDummyUse->cu_def
		&& strcmp(origUse->cu_id, use->cu_id) == 0)
	    break;
    }
    if (origUse == NULL)
    {
	TxError("Oops!  Can't find cell use %s in parent\n", use->cu_id);
	return (0);
    }

    plowLabelsChanged = TRUE;

    /* Figure out how much to translate by based on the plowing direction */
    x = y = 0;
    switch (plowDirection)
    {
	case GEO_NORTH:
	    y = (int)use->cu_client;
	    break;
	case GEO_SOUTH:
	    y = -(int)use->cu_client;
	    break;
	case GEO_WEST:
	    x = -(int)use->cu_client;
	    break;
	case GEO_EAST:
	    x = (int)use->cu_client;
	    break;
    }
    GeoTranslateTrans(&origUse->cu_transform, x, y, &newTrans);
    DBDeleteCell(origUse);
    DBWAreaChanged(origDef, &origUse->cu_bbox,
			DBW_ALLWINDOWS, (TileTypeBitMask *) NULL);
    DBSetTrans(origUse, &newTrans);
    DBPlaceCell(origUse, origDef);
    DBWAreaChanged(origDef, &origUse->cu_bbox,
			DBW_ALLWINDOWS, (TileTypeBitMask *) NULL);
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowUpdatePaintTile --
 *
 * Copy back a tile from the plowed planes into the orignal cell def.
 * Instead of using the normal left and right coordinates of this tile,
 * however, we use the leading/trailing edges.
 *
 * Results:
 *	Always return 0 to keep the search alive.
 *
 * Side effects:
 *	Paints a tile of the same type as 'tile' into the
 *	original def, after first transforming by the inverse
 *	transform of that used to rotate the original def
 *	into the plowing yank buffer.
 *
 * ----------------------------------------------------------------------------
 */

int
plowUpdatePaintTile(tile, ui)
    Tile *tile;		/* Tile in yanked, plowed def */
    PaintUndoInfo *ui;	/* Identifies original cell and plane being searched */
{
    Rect r, rtrans;
    TileType type = TiGetTypeExact(tile);

    r.r_ybot = BOTTOM(tile);
    r.r_ytop = TOP(tile);
    r.r_xbot = TRAILING(tile);
    r.r_xtop = LEADING(tile);
    GeoTransRect(&plowInverseTrans, &r, &rtrans);
    DBPaintPlane(ui->pu_def->cd_planes[DBPlane(type)],
		    &rtrans, DBWriteResultTbl[type], ui);
    return (0);
}
