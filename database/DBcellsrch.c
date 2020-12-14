/*
 * DBcellsearch.c --
 *
 * Area searching which spans cell boundaries.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/database/DBcellsrch.c,v 1.7 2010/09/15 21:53:22 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/malloc.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "database/databaseInt.h"
#include "dbwind/dbwind.h"
#include "textio/textio.h"
#include "utils/signals.h"
#include "windows/windows.h"
#include "utils/main.h"
#include "mzrouter/mzrouter.h"

/* Quick hack for dbScalePlanes() access to the CIF/GDS paint table */

#ifdef CIF_MODULE
extern PaintResultType CIFPaintTable[];
#endif

/*
 * The following structure is used to accumulate information about
 * the types of tiles visible underneath a given point in the database.
 */
struct seeTypesArg
{
    TileTypeBitMask *saa_mask;	/* Mask of tile types seen in search */
    Rect *saa_rect;		/* Search area in root coordinates */
};

/*
 *-----------------------------------------------------------------------------
 *
 * DBSrCellPlaneArea --
 *
 * Searches a CellDef's cell plane and calls function func() for each
 * cell use found.
 *
 * func() must be in the form:
 *
 *	int func(CellUse *use, ClientData cdata)
 *
 * and must return 0 to keep the search running, or 1 to end the search.
 *
 * Replaces the original TiSrArea() routine, but with the function's
 * first argument as a CellUse pointer rather than a Tile pointer,
 * since the tile plane has been replaced with the BPlane method.
 *
 * Returns 1 if the func() returns 1;  otherwise returns 0 to keep the
 * search alive.
 *
 *-----------------------------------------------------------------------------
 */

int
DBSrCellPlaneArea(BPlane *plane, Rect *rect, int (*func)(), ClientData arg)
{
    BPEnum bpe;
    CellUse *use;
    int rval = 0;

    BPEnumInit(&bpe, plane, rect, BPE_OVERLAP, "DBSrCellPlaneArea");

    while (use = BPEnumNext(&bpe))
    {
	if ((*func)(use, arg))
	{
	    rval = 1;
	    break;
	}
    }

    BPEnumTerm(&bpe);
    return rval;
}

/*
 *-----------------------------------------------------------------------------
 *
 * DBTreeSrTiles --
 *
 * Recursively search downward from the supplied CellUse for
 * all visible paint tiles matching the supplied type mask.
 *
 * The procedure should be of the following form:
 *	int
 *	func(tile, cxp)
 *	    Tile *tile;
 *	    TreeContext *cxp;
 *	{
 *	}
 *
 * The SearchContext is stored in cxp->tc_scx, and the user's arg is stored
 * in cxp->tc_filter->tf_arg.
 *
 * In the above, the scx transform is the net transform from the coordinates
 * of tile to "world" coordinates (or whatever coordinates the initial
 * transform supplied to DBTreeSrTiles was a transform to).  Func returns
 * 0 under normal conditions.  If 1 is returned, it is a request to
 * abort the search.
 *
 *			*** WARNING ***
 *
 * The client procedure should not modify any of the paint planes in
 * the cells visited by DBTreeSrTiles, because we use DBSrPaintArea
 * as our paint-tile enumeration function.
 *
 * Results:
 *	0 is returned if the search finished normally.  1 is returned
 *	if the search was aborted.
 *
 * Side effects:
 *	Whatever side effects are brought about by applying the
 *	procedure supplied.
 *
 *-----------------------------------------------------------------------------
 */

int
DBTreeSrTiles(scx, mask, xMask, func, cdarg)
    SearchContext *scx;		/* Pointer to search context specifying
				 * a cell use to search, an area in the
				 * coordinates of the cell's def, and a
				 * transform back to "root" coordinates.
				 */
    TileTypeBitMask *mask;	/* Only tiles with a type for which
				 * a bit in this mask is on are processed.
				 */
    int xMask;			/* All subcells are visited recursively
				 * until we encounter uses whose flags,
				 * when anded with xMask, are not
				 * equal to xMask.
				 */
    int (*func)();		/* Function to apply at each qualifying tile */
    ClientData cdarg;		/* Client data for above function */
{
    int dbCellPlaneSrFunc();
    TreeFilter filter;

    /* Set up the filter and call the recursive filter function */

    filter.tf_func = func;
    filter.tf_arg = cdarg;
    filter.tf_mask = mask;
    filter.tf_xmask = xMask;
    filter.tf_dinfo = 0;
    filter.tf_planes = DBTechTypesToPlanes(mask);

    return dbCellPlaneSrFunc(scx, &filter);
}

/*
 * DBTreeSrNMTiles --
 *	This is a variant of the above in which the search is over
 *	a non-Manhattan area.
 */

int
DBTreeSrNMTiles(scx, dinfo, mask, xMask, func, cdarg)
    SearchContext *scx;		/* Pointer to search context specifying
				 * a cell use to search, an area in the
				 * coordinates of the cell's def, and a
				 * transform back to "root" coordinates.
				 */
    TileType dinfo;		/* Type containing information about the
				 * diagonal area to search.
				 */
    TileTypeBitMask *mask;	/* Only tiles with a type for which
				 * a bit in this mask is on are processed.
				 */
    int xMask;			/* All subcells are visited recursively
				 * until we encounter uses whose flags,
				 * when anded with xMask, are not
				 * equal to xMask.
				 */
    int (*func)();		/* Function to apply at each qualifying tile */
    ClientData cdarg;		/* Client data for above function */
{
    int dbCellPlaneSrFunc();
    TreeFilter filter;

    /* Set up the filter and call the recursive filter function */

    filter.tf_func = func;
    filter.tf_arg = cdarg;
    filter.tf_mask = mask;
    filter.tf_xmask = xMask;
    filter.tf_dinfo = dinfo;
    filter.tf_planes = DBTechTypesToPlanes(mask);

    return dbCellPlaneSrFunc(scx, &filter);
}

/*
 * dbCellPlaneSrFunc --
 *
 * Recursive filter procedure applied to the cell by DBTreeSrTiles().
 */

int
dbCellPlaneSrFunc(scx, fp)
    SearchContext *scx;
    TreeFilter *fp;
{
    TreeContext context;
    CellDef *def = scx->scx_use->cu_def;
    int pNum;

    ASSERT(def != (CellDef *) NULL, "dbCellPlaneSrFunc");
    if (!DBDescendSubcell(scx->scx_use, fp->tf_xmask))
	return 0;
    if ((def->cd_flags & CDAVAILABLE) == 0)
    {
	bool dereference = (def->cd_flags & CDDEREFERENCE) ? TRUE : FALSE;
	if (!DBCellRead(def, (char *) NULL, TRUE, dereference, NULL)) return 0;
    }

    context.tc_scx = scx;
    context.tc_filter = fp;

    /*
     * Apply the function first to any of the tiles in the planes
     * for this CellUse's CellDef that match the mask.
     */

    for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
	if (PlaneMaskHasPlane(fp->tf_planes, pNum))
	{
	    context.tc_plane = pNum;
	    if (fp->tf_dinfo & TT_DIAGONAL)
	    {
		// TileType dinfo = DBTransformDiagonal(fp->tf_dinfo, &scx->scx_trans);
		TileType dinfo = DBInvTransformDiagonal(fp->tf_dinfo, &scx->scx_trans);
		if (DBSrPaintNMArea((Tile *) NULL, def->cd_planes[pNum],
			dinfo, &scx->scx_area, fp->tf_mask,
			fp->tf_func, (ClientData) &context))
		    return 1;
	    }
	    else if (DBSrPaintArea((Tile *) NULL, def->cd_planes[pNum],
		    &scx->scx_area, fp->tf_mask,
		    fp->tf_func, (ClientData) &context))
		return 1;
	}

    /*
     * Now apply ourselves recursively to each of the CellUses
     * in our cell plane.
     */

    if (DBCellSrArea(scx, dbCellPlaneSrFunc, (ClientData) fp))
	return 1;
    else return 0;
}


/*
 *-----------------------------------------------------------------------------
 *
 * DBTreeSrUniqueTiles --
 *
 * Recursively search downward from the supplied CellUse for
 * all visible paint tiles matching the supplied type mask.
 * This routine is like DBTreeSrTiles, above, except that it will
 * only call the specified routine ONCE for each contact type, in
 * the home plane of that contact type.  Stacked contact types will
 * only be processed if the current search plane matches the home
 * plane of one of the stacked contact type's residues.
 *
 * Results:
 *	0 is returned if the search finished normally.  1 is returned
 *	if the search was aborted.
 *
 * Side effects:
 *	Whatever side effects are brought about by applying the
 *	procedure supplied.
 *
 *-----------------------------------------------------------------------------
 */

int
DBTreeSrUniqueTiles(scx, mask, xMask, func, cdarg)
    SearchContext *scx;		/* Pointer to search context specifying
				 * a cell use to search, an area in the
				 * coordinates of the cell's def, and a
				 * transform back to "root" coordinates.
				 */
    TileTypeBitMask *mask;	/* Only tiles with a type for which
				 * a bit in this mask is on are processed.
				 */
    int xMask;			/* All subcells are visited recursively
				 * until we encounter uses whose flags,
				 * when anded with xMask, are not
				 * equal to xMask.
				 */
    int (*func)();		/* Function to apply at each qualifying tile */
    ClientData cdarg;		/* Client data for above function */
{
    int dbCellPlaneSrFunc();
    TreeFilter filter;

    /* Set up the filter and call the recursive filter function */

    filter.tf_func = func;
    filter.tf_arg = cdarg;
    filter.tf_mask = mask;
    filter.tf_xmask = xMask;
    filter.tf_planes = DBTechTypesToPlanes(mask);

    return dbCellUniqueTileSrFunc(scx, &filter);
}

/*
 * dbCellUniqueTileSrFunc --
 *
 * Recursive filter procedure applied to the cell by DBTreeSrUniqueTiles().
 * This is similar to dbCellPlaneSrFunc, except that for each plane searched,
 * only the tile types having that plane as their home plane will be passed
 * to the filter function.  Contacts will therefore be processed only once.
 */

int
dbCellUniqueTileSrFunc(scx, fp)
    SearchContext *scx;
    TreeFilter *fp;
{
    TreeContext context;
    TileTypeBitMask uMask;
    CellDef *def = scx->scx_use->cu_def;
    int pNum;

    ASSERT(def != (CellDef *) NULL, "dbCellUniqueTileSrFunc");
    if (!DBDescendSubcell(scx->scx_use, fp->tf_xmask))
	return 0;
    if ((def->cd_flags & CDAVAILABLE) == 0)
    {
	bool dereference = (def->cd_flags & CDDEREFERENCE) ? TRUE : FALSE;
	if (!DBCellRead(def, (char *) NULL, TRUE, dereference, NULL)) return 0;
    }

    context.tc_scx = scx;
    context.tc_filter = fp;

    /*
     * Apply the function first to any of the tiles in the planes
     * for this CellUse's CellDef that match the mask.
     */

    for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
	if (PlaneMaskHasPlane(fp->tf_planes, pNum))
	{
	    uMask = DBHomePlaneTypes[pNum];
	    TTMaskAndMask(&uMask, fp->tf_mask);
	    if (!TTMaskIsZero(&uMask))
	    {
		context.tc_plane = pNum;
	        if (DBSrPaintArea((Tile *) NULL, def->cd_planes[pNum],
		    	&scx->scx_area, &uMask, fp->tf_func,
			(ClientData) &context))
		    return 1;
	    }
	}

    /*
     * Now apply ourselves recursively to each of the CellUses
     * in our tile plane.
     */

    if (DBCellSrArea(scx, dbCellUniqueTileSrFunc, (ClientData) fp))
	return 1;
    else return 0;
}


/*
 *-----------------------------------------------------------------------------
 *
 * DBNoTreeSrTiles --
 *
 * NOTE: THIS PROCEDURE IS EXACTLY LIKE DBTreeSrTiles EXCEPT THAT IT DOES
 * NOT SEARCH SUBCELLS.
 *
 * Searches the supplied CellUse (if expanded) for
 * all visible paint tiles matching the supplied type mask.
 *
 * The procedure should be of the following form:
 *	int
 *	func(tile, cxp)
 *	    Tile *tile;
 *	    TreeContext *cxp;
 *	{
 *	}
 *
 * The SearchContext is stored in cxp->tc_scx, and the user's arg is stored
 * in cxp->tc_filter->tf_arg.
 *
 * In the above, the scx transform is the net transform from the coordinates
 * of tile to "world" coordinates (or whatever coordinates the initial
 * transform supplied to DBTreeSrTiles was a transform to).  Func returns
 * 0 under normal conditions.  If 1 is returned, it is a request to
 * abort the search.
 *
 *			*** WARNING ***
 *
 * The client procedure should not modify any of the paint planes in
 * the cells visited by DBTreeSrTiles, because we use DBSrPaintArea
 * as our paint-tile enumeration function.
 *
 * Results:
 *	0 is returned if the search finished normally.  1 is returned
 *	if the search was aborted.
 *
 * Side effects:
 *	Whatever side effects are brought about by applying the
 *	procedure supplied.
 *
 *-----------------------------------------------------------------------------
 */
int
DBNoTreeSrTiles(scx, mask, xMask, func, cdarg)
    SearchContext *scx;		/* Pointer to search context specifying
				 * a cell use to search, an area in the
				 * coordinates of the cell's def, and a
				 * transform back to "root" coordinates.
				 */
    TileTypeBitMask *mask;	/* Only tiles with a type for which
				 * a bit in this mask is on are processed.
				 */
    int xMask;			/* All subcells are visited recursively
				 * until we encounter uses whose flags,
				 * when anded with xMask, are not
				 * equal to xMask.
				 */
    int (*func)();		/* Function to apply at each qualifying tile */
    ClientData cdarg;		/* Client data for above function */
{
    TreeContext context;
    TreeFilter filter;
    CellUse *cellUse = scx->scx_use;
    CellDef *def = cellUse->cu_def;
    int pNum;

    ASSERT(def != (CellDef *) NULL, "DBNoTreeSrTiles");
    if (!DBDescendSubcell(cellUse, xMask))
	return 0;

    if ((def->cd_flags & CDAVAILABLE) == 0)
    {
	bool dereference = (def->cd_flags & CDDEREFERENCE) ? TRUE : FALSE;
	if (!DBCellRead(def, (char *) NULL, TRUE, dereference, NULL)) return 0;
    }

    filter.tf_func = func;
    filter.tf_arg = cdarg;
    filter.tf_mask = mask;
    filter.tf_xmask = xMask;
    filter.tf_planes = DBTechTypesToPlanes(mask);

    context.tc_scx = scx;
    context.tc_filter = &filter;

    /*
     * Apply the function first to any of the tiles in the planes
     * for this CellUse's CellDef that match the mask.
     */

    for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
	if (PlaneMaskHasPlane(filter.tf_planes, pNum))
	{
	    if (DBSrPaintArea((Tile *) NULL, def->cd_planes[pNum],
		    &scx->scx_area, mask, func, (ClientData) &context))
		return 1;
	}

    /* Return normally */
    return 0;
}


/*
 *-----------------------------------------------------------------------------
 *
 * DBTreeSrLabels --
 *
 * Recursively search downward from the supplied CellUse for
 * all visible labels attached to layers matching the supplied
 * type mask.
 *
 * The procedure should be of the following form:
 *	int
 *	func(scx, label, tpath, cdarg)
 *	    SearchContext *scx;
 *	    Label *label;
 *	    TerminalPath *tpath;
 *	    ClientData cdarg;
 *	{
 *	}
 *
 * In the above, the use associated with scx is the parent of the
 * CellDef containing the tile which contains the label, and the
 * transform associated is the net transform from the coordinates
 * of the tile to "root" coordinates.  Func normally returns 0.  If
 * func returns 1, it is a request to abort the search without finding
 * any more labels.
 *
 * Results:
 *	0 is returned if the search terminated normally.  1 is returned
 *	if the search was aborted.
 *
 * Side effects:
 *	Whatever side effects are brought about by applying the
 *	procedure supplied.
 *
 *-----------------------------------------------------------------------------
 */

int
DBTreeSrLabels(scx, mask, xMask, tpath, flags, func, cdarg)
    SearchContext *scx;		/* Pointer to search context specifying
				 * a cell use to search, an area in the
				 * coordinates of the cell's def, and a
				 * transform back to "root" coordinates.
				 * The area may have zero size.  Labels
				 * need only touch the area.
				 */
    TileTypeBitMask * mask;	/* Only visit labels attached to these types */
    int xMask;			/* All subcells are visited recursively
				 * until we encounter uses whose flags,
				 * when anded with xMask, are not
				 * equal to xMask.
				 */
    TerminalPath *tpath;	/* Pointer to a structure describing a
				 * partially filled in terminal pathname.
				 * If this pointer is NULL, we don't bother
				 * filling it in further; otherwise, we add
				 * new pathname components as we encounter
				 * them.
				 */
    unsigned char flags;	/* Flags to denote whether labels should be
				 * searched according to the area of the
				 * attachment, the area of the label itself,
				 * or both.
				 */
    int (*func)();		/* Function to apply at each qualifying tile */
    ClientData cdarg;		/* Client data for above function */
{
    SearchContext scx2;
    Label *lab;
    Rect *r = &scx->scx_area;
    CellUse *cellUse = scx->scx_use;
    CellDef *def = cellUse->cu_def;
    TreeFilter filter;
    bool is_touching;
    int dbCellLabelSrFunc();

    ASSERT(def != (CellDef *) NULL, "DBTreeSrLabels");
    if (!DBDescendSubcell(cellUse, xMask)) return 0;
    if ((def->cd_flags & CDAVAILABLE) == 0)
    {
	bool dereference = (def->cd_flags & CDDEREFERENCE) ? TRUE : FALSE;
	if (!DBCellRead(def, (char *) NULL, TRUE, dereference, NULL)) return 0;
    }

    for (lab = def->cd_labels; lab; lab = lab->lab_next)
    {
	if (SigInterruptPending) break;
	is_touching = FALSE;

	if ((lab->lab_font < 0) || (flags & TF_LABEL_ATTACH))
	{
	    /* For non-manhattan searches, label must be in or	*/
	    /* touch the triangle.  (to-do:  needs a proper	*/
	    /* insideness test)					*/

	    if (flags & TF_LABEL_ATTACH_CORNER)
	    {
		Rect r1 = *r;
		Rect r2 = *r;
		if (flags & TF_LABEL_ATTACH_NOT_NE)
		{
		    r1.r_ytop = r->r_ybot;
		    r2.r_xtop = r->r_xbot;
		}
		else if (flags & TF_LABEL_ATTACH_NOT_NW)
		{
		    r1.r_ytop = r->r_ybot;
		    r2.r_xbot = r->r_xtop;
		}
		else if (flags & TF_LABEL_ATTACH_NOT_SE)
		{
		    r1.r_ybot = r->r_ytop;
		    r2.r_xtop = r->r_xbot;
		}
		else if (flags & TF_LABEL_ATTACH_NOT_SW)
		{
		    r1.r_ybot = r->r_ytop;
		    r2.r_xbot = r->r_xtop;
		}
		is_touching = GEO_TOUCH(&lab->lab_bbox, &r1) ||
			  GEO_TOUCH(&lab->lab_bbox, &r2);
	    }
	    else
		is_touching = GEO_TOUCH(&lab->lab_rect, r);
	}
	if (!is_touching && (flags & TF_LABEL_DISPLAY) && lab->lab_font >= 0)
	{
	    /* Check against bounds of the rendered label text */
	    is_touching = GEO_TOUCH(&lab->lab_bbox, r);
	}

	if (is_touching && TTMaskHasType(mask, lab->lab_type))
	    if ((*func)(scx, lab, tpath, cdarg))
		return (1);
    }

    filter.tf_func = func;
    filter.tf_arg = cdarg;
    filter.tf_mask = mask;
    filter.tf_xmask = xMask;
    filter.tf_tpath = tpath;
    filter.tf_flags = flags;
    /* filter.tf_planes is unused */

    /* Visit each child CellUse recursively.
     * This code is a bit tricky because the area can have zero size.
     * This would cause subcells never to be examined.  What we do is
     * to expand the area by 1 here, then require the labels to OVERLAP
     * instead of just TOUCH.  Be careful when expanding:  can't expand
     * any coordinate past infinity.
     */

    scx2 = *scx;
    if (scx2.scx_area.r_xbot > TiPlaneRect.r_xbot) scx2.scx_area.r_xbot -= 1;
    if (scx2.scx_area.r_ybot > TiPlaneRect.r_ybot) scx2.scx_area.r_ybot -= 1;
    if (scx2.scx_area.r_xtop < TiPlaneRect.r_xtop) scx2.scx_area.r_xtop += 1;
    if (scx2.scx_area.r_ytop < TiPlaneRect.r_ytop) scx2.scx_area.r_ytop += 1;
    if (DBCellSrArea(&scx2, dbCellLabelSrFunc, (ClientData) &filter))
	return 1;

    return 0;
}


/*
 * dbCellLabelSrFunc --
 *
 * Filter procedure applied to subcells by DBTreeSrLabels().
 */

int
dbCellLabelSrFunc(scx, fp)
    SearchContext *scx;
    TreeFilter *fp;
{
    Label *lab;
    Rect *r = &scx->scx_area;
    TileTypeBitMask *mask = fp->tf_mask;
    CellDef *def = scx->scx_use->cu_def;
    char *tnext;
    int result;
    bool has_overlap;

    ASSERT(def != (CellDef *) NULL, "dbCellLabelSrFunc");
    if (!DBDescendSubcell(scx->scx_use, fp->tf_xmask)) return 0;
    if ((def->cd_flags & CDAVAILABLE) == 0)
    {
	bool dereference = (def->cd_flags & CDDEREFERENCE) ? TRUE : FALSE;
	if (!DBCellRead(def, (char *) NULL, TRUE, dereference, NULL)) return 0;
    }

    if (fp->tf_tpath != (TerminalPath *) NULL)
    {
	TerminalPath *tp = fp->tf_tpath;

	tnext = tp->tp_next;
	tp->tp_next = DBPrintUseId(scx, tp->tp_next, tp->tp_last-tp->tp_next,
			FALSE);
	if (tp->tp_next < tp->tp_last)
	{
	    *(tp->tp_next++) = '/';
	    *(tp->tp_next) = '\0';
	}
    }

    /* Apply the function first to any of the labels in this def. */

    result = 0;
    for (lab = def->cd_labels; lab; lab = lab->lab_next)
    {
	has_overlap = FALSE;
	if ((lab->lab_font < 0) || (fp->tf_flags & TF_LABEL_ATTACH))
	    has_overlap = GEO_OVERLAP(&lab->lab_rect, r);
	if (!has_overlap && (fp->tf_flags & TF_LABEL_DISPLAY) && (lab->lab_font >= 0))
	    has_overlap = GEO_OVERLAP(&lab->lab_bbox, r);

	if (has_overlap && TTMaskHasType(mask, lab->lab_type))
	{
	    if ((*fp->tf_func)(scx, lab, fp->tf_tpath, fp->tf_arg))
	    {
		result = 1;
		goto cleanup;
	    }
	}
    }

    /* Now visit each child use recursively */
    if (DBCellSrArea(scx, dbCellLabelSrFunc, (ClientData) fp))
	result = 1;

cleanup:
    /* Remove the trailing pathname component from the TerminalPath */
    if (fp->tf_tpath != (TerminalPath *) NULL)
    {
	fp->tf_tpath->tp_next = tnext;
	*tnext = '\0';
    }

    return (result);
}

/*
 *-----------------------------------------------------------------------------
 *
 * DBTreeSrCells --
 *
 * Recursively search downward from the supplied CellUse for
 * all CellUses whose parents are expanded but which themselves
 * are unexpanded.
 *
 * The procedure should be of the following form:
 *	int
 *	func(scx, cdarg)
 *	    SearchContext *scx;
 *	    ClientData cdarg;
 *	{
 *	}
 *
 * In the above, the transform scx->scx_trans is from coordinates of
 * the def of scx->scx_use to the "root".  The array indices
 * scx->scx_x and scx->scx_y identify this element if it is a
 * component of an array.  Func normally returns 0.  If func returns
 * 1, then the search is aborted.  If func returns 2, then all
 * remaining elements of the current array are skipped, but the
 * search is not aborted.
 *
 * Each element of an array is returned separately.
 *
 * Results:
 *	0 is returned if the search terminated normally.  1 is
 *	returned if it was aborted.
 *
 * Side effects:
 *	Whatever side effects are brought about by applying the
 *	procedure supplied.
 *
 *-----------------------------------------------------------------------------
 */

int
DBTreeSrCells(scx, xMask, func, cdarg)
    SearchContext *scx;	/* Pointer to search context specifying a cell use to
			 * search, an area in the coordinates of the cell's
			 * def, and a transform back to "root" coordinates.
			 */
    int xMask;		/* All subcells are visited recursively until we
			 * encounter uses whose flags, when anded with
			 * xMask, are not equal to xMask.  Func is called
			 * for these cells.  A zero mask means all cells in
			 * the root use are considered not to be expanded,
			 * and hence are passed to func.
			 */
    int (*func)();	/* Function to apply to each qualifying cell */
    ClientData cdarg;	/* Client data for above function */
{
    int dbTreeCellSrFunc();
    CellUse *cellUse = scx->scx_use;
    TreeContext context;
    TreeFilter filter;

    if (!DBDescendSubcell(cellUse, xMask))
	return 0;
    if ((cellUse->cu_def->cd_flags & CDAVAILABLE) == 0)
    {
	bool dereference = (cellUse->cu_def->cd_flags & CDDEREFERENCE) ? TRUE : FALSE;
	if (!DBCellRead(cellUse->cu_def, (char *) NULL, TRUE, dereference, NULL))
	    return 0;
    }

    context.tc_scx = scx;
    context.tc_filter = &filter;

    filter.tf_func = func;
    filter.tf_arg = cdarg;
    filter.tf_xmask = xMask;

    if (DBCellSrArea(scx, dbTreeCellSrFunc, (ClientData) &filter))
	return 1;
    else return 0;
}

/*
 * dbTreeCellSrFunc --
 *
 * Filter procedure applied to subcells by DBTreeSrCells().
 */

    /*ARGSUSED*/
int
dbTreeCellSrFunc(scx, fp)
    SearchContext *scx;	/* Pointer to context containing a
					 * CellUse and a transform from coord-
					 * inates of the def of the use to the
					 * "root" of the search.
					 */
    TreeFilter *fp;
{
    CellUse *use = scx->scx_use;
    int result;

    /* DBDescendSubcell treats a zero expand mask as "expanded everywhere",
     * whereas we want it to mean "expanded nowhere".  Handle specially.
     */

    if ((fp->tf_xmask == CU_DESCEND_NO_LOCK) && (use->cu_flags & CU_LOCKED))
	return 2;
    else if ((!DBDescendSubcell(use, fp->tf_xmask)) ||
		(fp->tf_xmask == CU_DESCEND_ALL))
	result = (*fp->tf_func)(scx, fp->tf_arg);
    else
    {
	if ((use->cu_def->cd_flags & CDAVAILABLE) == 0)
	{
	    bool dereference = (use->cu_def->cd_flags & CDDEREFERENCE) ? TRUE : FALSE;
	    if (!DBCellRead(use->cu_def, (char *) NULL, TRUE, dereference, NULL))
		return 0;
	}
	result = DBCellSrArea(scx, dbTreeCellSrFunc, (ClientData) fp);
    }
    return result;
}

/*
 *-----------------------------------------------------------------------------
 *
 * DBSeeTypesAll --
 *
 * Set a TileTypeBitMask of all visible tiles beneath the given rectangle.
 * "Beneath" in this case means "completely containing".
 * The search takes place recursively down to all expanded cells beneath
 * rootUse.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets the TileTypeBitMask pointed to by 'mask' to all types beneath
 *	the rectangle.
 *
 *-----------------------------------------------------------------------------
 */

void
DBSeeTypesAll(rootUse, rootRect, xMask, mask)
    CellUse *rootUse;	/* CellUse from which to begin search */
    Rect *rootRect;	/* Clipping rectangle in coordinates of CellUse's def */
    int xMask;		/* Expansion mask for DBTreeSrTiles() */
    TileTypeBitMask *mask;	/* Mask to set */
{
    int dbSeeTypesAllSrFunc();
    SearchContext scontext;

    scontext.scx_use = rootUse;
    scontext.scx_trans = GeoIdentityTransform;
    scontext.scx_area = *rootRect;

    TTMaskZero(mask);
    (void) DBTreeSrTiles(&scontext, &DBAllTypeBits, xMask,
		dbSeeTypesAllSrFunc, (ClientData) mask);
}

/*
 * dbSeeTypesAllSrFunc --
 *
 * Filter procedure applied to tiles by DBSeeTypesAll() above.
 */

int
dbSeeTypesAllSrFunc(tile, cxp)
    Tile *tile;
    TreeContext *cxp;
{
    Rect tileRect;
    TileTypeBitMask *mask = (TileTypeBitMask *) cxp->tc_filter->tf_arg;
    Rect *area = &cxp->tc_scx->scx_area;

    TiToRect(tile, &tileRect);
    if (GEO_OVERLAP((&tileRect), area))
    {
	if (IsSplit(tile))
	    TTMaskSetType(mask, SplitSide(tile) ?
			SplitRightType(tile) : SplitLeftType(tile));
        else
	    TTMaskSetType(mask, TiGetType(tile));
    }
    return 0;
}

/*
 *-----------------------------------------------------------------------------
 *
 * DBSrRoots --
 *
 * Apply the supplied procedure to each CellUse that is a root of
 * the given base CellDef.  A root is a CellUse with no parent def.
 *
 * The procedure should be of the following form:
 *	int
 *	func(cellUse, transform, cdarg)
 *	    CellUse *cellUse;
 *	    Transform *transform;
 *	    ClientData cdarg;
 *	{
 *	}
 *
 * Transform is from coordinates of baseDef to those of the def of cellUse.
 * Func normally returns 0.  If it returns 1 then the search is aborted.
 *
 * Results:
 *	0 is returned if the search terminated normally.  1 is returned
 *	if it was aborted.
 *
 * Side effects:
 *	Whatever side effects are brought about by applying the
 *	procedure supplied.
 *
 *-----------------------------------------------------------------------------
 */

int
DBSrRoots(baseDef, transform, func, cdarg)
    CellDef *baseDef;		/* Base CellDef, all of whose ancestors are
				 * searched for.
				 */
    Transform *transform;	/* Transform from original baseDef to current
				 * baseDef.
				 */
    int (*func)();		/* Function to apply at each root cellUse */
    ClientData cdarg;		/* Client data for above function */
{
    CellUse *parentUse;
    int xoff, yoff, x, y;
    Transform baseToParent, t;

    if (baseDef == (CellDef *) NULL)
	return 0;

    for (parentUse = baseDef->cd_parents;  parentUse != NULL;
	parentUse = parentUse->cu_nextuse)
    {
	if (SigInterruptPending) return 1;
	if (parentUse->cu_parent == (CellDef *) NULL)
	{
	    GeoTransTrans(transform, &parentUse->cu_transform, &baseToParent);
	    if ((*func)(parentUse, &baseToParent, cdarg)) return 1;
	}
	else
	{
	    for (x = parentUse->cu_xlo; x <= parentUse->cu_xhi; x++)
		for (y = parentUse->cu_ylo; y <= parentUse->cu_yhi; y++)
		{
		    if (SigInterruptPending)
			return 1;

		    xoff = (x - parentUse->cu_xlo) * parentUse->cu_xsep;
		    yoff = (y - parentUse->cu_ylo) * parentUse->cu_ysep;
		    GeoTranslateTrans(transform, xoff, yoff, &t);
		    GeoTransTrans(&t, &parentUse->cu_transform, &baseToParent);
		    if (DBSrRoots(parentUse->cu_parent, &baseToParent,
			func, cdarg)) return 1;
		}
	}
    }
    return 0;
}

/*
 *-----------------------------------------------------------------------------
 *
 * DBIsAncestor --
 *
 * Determine if cellDef1 is an ancestor of cellDef2.
 *
 * Results:
 *	TRUE if cellDef1 is an ancestor of cellDef2, FALSE if not.
 *
 * Side effects:
 *	None.
 *
 *-----------------------------------------------------------------------------
 */

bool
DBIsAncestor(cellDef1, cellDef2)
    CellDef *cellDef1;		/* Potential ancestor */
    CellDef *cellDef2;		/* Potential descendant -- this is where we
				 * start the search.
				 */
{
    CellUse *parentUse;
    CellDef *parentDef;

    if (cellDef1 == cellDef2)
	return (TRUE);

    for (parentUse = cellDef2->cd_parents;  parentUse != NULL;
	parentUse = parentUse->cu_nextuse)
    {
	if ((parentDef = parentUse->cu_parent) != (CellDef *) NULL)
	    if (DBIsAncestor(cellDef1, parentDef))
		return (TRUE);
    }
    return (FALSE);
}

/*
 *-----------------------------------------------------------------------------
 *
 * DBCellSrArea --
 *
 * Apply the supplied procedure to each of the cellUses found in the
 * given area in the subcell plane of the child def of the supplied
 * search context.
 *
 * The procedure is applied to each array element in each cell use that
 * overlaps the clipping rectangle.  The scx_x and scx_y parts of
 * the SearchContext passed to the filter function correspond to the
 * array element being visited.  The same CellUse is, of course, passed
 * as scx_use for all elements of the array.
 *
 * The array elements are visited by varying the X coordinate fastest.
 *
 * The procedure should be of the following form:
 *	int
 *	func(scx, cdarg)
 *	    SearchContext *scx;
 *	    ClientData cdarg;
 *	{
 *	}
 *
 * Func normally returns 0.  If it returns 1 then the search is
 * aborted.  If it returns 2, then any remaining elements in the
 * current array are skipped.
 *
 * Results:
 *	0 is returned if the search terminated normally.  1 is
 *	returned if it was aborted.
 *
 * Side effects:
 *	Whatever side effects are brought about by applying the
 *	procedure supplied.
 *
 *-----------------------------------------------------------------------------
 */

int
DBCellSrArea(scx, func, cdarg)
    SearchContext *scx;
			/* Pointer to search context specifying a cell use to
			 * search, an area in the coordinates of the cell's
			 * def, and a transform back to "root" coordinates.
			 * The area may have zero size.
			 */
    int (*func)();	/* Function to apply at every tile found */
    ClientData cdarg;	/* Argument to pass to function */
{
    TreeFilter filter;
    TreeContext context;
    int dbCellSrFunc();

    filter.tf_func = func;
    filter.tf_arg = cdarg;
    context.tc_filter = &filter;
    context.tc_scx = scx;

    if ((scx->scx_use->cu_def->cd_flags & CDAVAILABLE) == 0)
    {
	bool dereference = (scx->scx_use->cu_def->cd_flags & CDDEREFERENCE) ?
		TRUE : FALSE;
	if (!DBCellRead(scx->scx_use->cu_def, (char *) NULL, TRUE, dereference, NULL))
	    return 0;
    }

    if (DBSrCellPlaneArea(scx->scx_use->cu_def->cd_cellPlane,
		&scx->scx_area, dbCellSrFunc, (ClientData) &context))
	return 1;
    return 0;
}

/*
 *-----------------------------------------------------------------------------
 *
 * dbCellSrFunc --
 *
 * Filter procedure for DBCellSrArea.  Applies the procedure given
 * to DBCellSrArea to any of the CellUses in the given area.
 *
 * Results:
 *	0 is normally returned, and 1 is returned if an abort occurred.
 *
 * Side effects:
 *	Whatever side effects are brought about by applying the
 *	procedure supplied.
 *
 *-----------------------------------------------------------------------------
 */

int
dbCellSrFunc(use, cxp)
    CellUse *use;
    TreeContext *cxp;
{
    TreeFilter *fp = cxp->tc_filter;
    SearchContext *scx = cxp->tc_scx;
    Rect *bbox;
    SearchContext newScx;
    Transform t, tinv;
    int xlo, xhi, ylo, yhi, xbase, ybase, xsep, ysep, clientResult;

    bbox = &use->cu_bbox;
    newScx.scx_use = use;

    /* If not an array element, life is much simpler */
    if (use->cu_xlo == use->cu_xhi && use->cu_ylo == use->cu_yhi)
    {
	newScx.scx_x = use->cu_xlo, newScx.scx_y = use->cu_yhi;
	if (SigInterruptPending) return 1;
	GEOINVERTTRANS(&use->cu_transform, &tinv);
	GeoTransTrans(&use->cu_transform, &scx->scx_trans,
				&newScx.scx_trans);
	GEOTRANSRECT(&tinv, &scx->scx_area, &newScx.scx_area);
	if ((*fp->tf_func)(&newScx, fp->tf_arg) == 1)
	    return 1;
	return 0;
    }

    /*
     * More than a single array element;
     * check to see which ones overlap our search area.
     */
    DBArrayOverlap(use, &scx->scx_area, &xlo, &xhi, &ylo, &yhi);
    xsep = (use->cu_xlo > use->cu_xhi) ? -use->cu_xsep : use->cu_xsep;
    ysep = (use->cu_ylo > use->cu_yhi) ? -use->cu_ysep : use->cu_ysep;
    for (newScx.scx_y = ylo; newScx.scx_y <= yhi; newScx.scx_y++)
        for (newScx.scx_x = xlo; newScx.scx_x <= xhi; newScx.scx_x++)
	{
	    if (SigInterruptPending) return 1;
	    xbase = xsep * (newScx.scx_x - use->cu_xlo);
	    ybase = ysep * (newScx.scx_y - use->cu_ylo);
	    GeoTransTranslate(xbase, ybase, &use->cu_transform, &t);
	    GEOINVERTTRANS(&t, &tinv);
	    GeoTransTrans(&t, &scx->scx_trans, &newScx.scx_trans);
	    GEOTRANSRECT(&tinv, &scx->scx_area, &newScx.scx_area);
	    clientResult = (*fp->tf_func)(&newScx, fp->tf_arg);
	    if (clientResult == 2) return 0;
	    else if (clientResult == 1) return 1;
	}

    return 0;
}

/*
 *-----------------------------------------------------------------------------
 *
 * DBCellEnum --
 *
 * Apply the supplied procedure once to each CellUse in the subcell
 * plane of the supplied CellDef.  This procedure is not a geometric
 * search, but rather a hierarchical enumeration.  Use DBSrCellPlaneArea()
 * for geometric searches over an area.
 *
 * The procedure should be of the following form:
 *	int
 *	func(use, cdarg)
 *	    CellUse *use;
 *	    ClientData cdarg;
 *	{
 *	}
 *
 * Func returns 0 normally, 1 to abort the search.
 *
 * Results:
 *	0 if search terminated normally, 1 if it aborted.
 *
 * Side effects:
 *	Whatever side effects are brought about by applying the
 *	procedure supplied.
 *
 *-----------------------------------------------------------------------------
 */

int
DBCellEnum(cellDef, func, cdarg)
    CellDef *cellDef;	/* Def whose subcell plane is to be searched */
    int (*func)();	/* Function to apply at every tile found */
    ClientData cdarg;	/* Argument to pass to function */
{
    TreeFilter filter;
    int dbEnumFunc();

    filter.tf_func = func;
    filter.tf_arg = cdarg;
    if ((cellDef->cd_flags & CDAVAILABLE) == 0)
    {
	bool dereference = (cellDef->cd_flags & CDDEREFERENCE) ? TRUE : FALSE;
	if (!DBCellRead(cellDef, (char *) NULL, TRUE, dereference, NULL)) return 0;
    }
    if (DBSrCellPlaneArea(cellDef->cd_cellPlane,
		&TiPlaneRect, dbEnumFunc, (ClientData) &filter))
	return 1;
    else return 0;
}

/*
 *-----------------------------------------------------------------------------
 *
 * dbEnumFunc --
 *
 * Filter procedure for DBCellEnum.  Applies the procedure given
 * to DBCellEnum to the visited CellUse.
 *
 * Results:
 *	0 normally, 1 if abort occurred.
 *
 * Side effects:
 *	Whatever side effects are brought about by applying the
 *	procedure supplied.
 *
 *-----------------------------------------------------------------------------
 */

int
dbEnumFunc(use, fp)
    CellUse *use;
    TreeFilter *fp;
{
    Rect *bbox;

    bbox = &use->cu_bbox;
    if ((*fp->tf_func)(use, fp->tf_arg)) return 1;
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBArraySr --
 *
 * 	Finds all elements of an array that fall in a particular area
 *	of the parent, and calls func for each element found.
 *
 *	The procedure should be of the following form:
 *	int
 *	func(cellUse, trans, x, y, cdarg)
 *	    CellUse *celluse;
 *	    Transform *trans;
 *	    int x, y;
 *	    ClientData cdarg;
 *	{}
 *
 *	In the above, cellUse is the original cellUse, trans is
 *	a transformation from the coordinates of the cell def to
 *	the coordinates of the use (for this array element), x and
 *	y are the indices of this array element, and cdarg is
 *	the ClientData supplied to us.	If 1 is returned by func,
 *	it is a signal to abort the search.
 *
 * Results:
 *	0 is returned if the search finished normally.  1 is returned
 *	if the search was aborted.
 *
 * Side effects:
 *	Whatever func does.
 *
 * ----------------------------------------------------------------------------
 */

int
DBArraySr(use, searchArea, func, cdarg)
    CellUse *use;		/* CellUse of array to be searched. */
    Rect *searchArea;		/* Area of interest, given in the
				 * coordinates of the parent (i.e. the
				 * cell use, not def).  Must overlap
				 * the array bounding box.
				 */
    int (*func)();		/* Function to apply for each overlapping
				 * array element.
				 */
    ClientData cdarg;		/* Client-specific info to give to func. */
{
    int xlo, xhi, ylo, yhi, x, y;
    int xsep, ysep, xbase, ybase;
    Transform t;

    DBArrayOverlap(use, searchArea, &xlo, &xhi, &ylo, &yhi);
    if (use->cu_xlo > use->cu_xhi) xsep = -use->cu_xsep;
    else xsep = use->cu_xsep;
    if (use->cu_ylo > use->cu_yhi) ysep = -use->cu_ysep;
    else ysep = use->cu_ysep;
    for (y = ylo; y <= yhi; y++)
	for (x = xlo; x <= xhi; x++)
	{
	    if (SigInterruptPending) return 1;
	    xbase = xsep * (x - use->cu_xlo);
	    ybase = ysep * (y - use->cu_ylo);
	    GeoTransTranslate(xbase, ybase, &use->cu_transform, &t);
	    if ((*func)(use, &t, x, y, cdarg)) return 1;
	}
    return 0;
}

/* Linked list structures for tile, celldef, and celluse enumeration */

typedef struct LT1		/* A linked tile record */
{
    Tile *tile;			/* Pointer to a tile */
    struct LT1 *t_next;		/* Pointer to another linked tile record */
} LinkedTile;

typedef struct LCD1		/* A linked celldef record */
{
    CellDef *cellDef;		/* Pointer to a celldef */
    struct LCD1 *cd_next;	/* Pointer to another linked celldef record */
} LinkedCellDef;

typedef struct LCU1		/* A linked celluse record */
{
    CellUse *cellUse;		/* Pointer to a celluse */
    struct LCU1 *cu_next;	/* Pointer to another linked celluse record */
} LinkedCellUse;

/*
 * ----------------------------------------------------------------------------
 *
 * DBMovePoint ---
 *
 *    Move a point by a position (origx, origy).  Ignore positions which
 *    are marked as (M)INFINITY.
 *
 * Results:
 *	TRUE if the point was moved (point position was not infinite)
 *
 * Side effects:
 *	Point structure pointed to by the first argument is repositioned.
 *
 * ----------------------------------------------------------------------------
 */

bool
DBMovePoint(p, origx, origy)
    Point *p;
    int origx, origy;
{
    int result = FALSE;
    if ((p->p_x < (INFINITY - 2)) && (p->p_x > (MINFINITY + 2)))
    {
	p->p_x -= origx;
	result = TRUE;
    }
    if ((p->p_y < (INFINITY + 2)) && (p->p_y > (MINFINITY + 2)))
    {
	p->p_y -= origy;
	result = TRUE;
    }
    return result;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBScaleValue ---
 *
 *    Scale an integer by a factor of (scalen / scaled).  Always round down.
 *    this requires correcting integer arithmetic for negative numbers on
 *    the division step.  Values representing INFINITY are not scaled.
 *
 * Results:
 *	TRUE if the value does not scale exactly, FALSE otherwise.
 *
 * Side effects:
 *	Integer value passed as pointer is scaled, unless it represents
 *	an INFINITY measure.
 *
 * ----------------------------------------------------------------------------
 */

bool
DBScaleValue(v, n, d)
    int *v, n, d;
{
    dlong llv = (dlong)(*v);

    if ((llv < (dlong)(INFINITY - 2)) && (llv > (dlong)(MINFINITY + 2)))
    {
	llv *= (dlong)n;

	if (llv > 0)
	    llv /= (dlong)d;
	else if (llv < 0)
	    llv = ((llv + 1) / (dlong)d) - 1;
	*v = (int)llv;

	/* Hopefully we do not reach this error.  If we do, there's	*/
	/* not much we can do about it except to increase Magic's	*/
	/* internal Point structure to hold 8-byte integer values.	*/

	if ((dlong)(*v) != llv)
	    TxError("ERROR: ARITHMETIC OVERFLOW in DBScaleValue()!\n");
    }
    return (((*v) % d) != 0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBScalePoint ---
 *
 *    Scale a point by a factor of (scalen / scaled).  Always round down.
 *    this requires correcting integer arithmetic for negative numbers on
 *    the division step.
 *
 * Results:
 *	TRUE if the point does not scale exactly, FALSE otherwise.
 *
 * Side effects:
 *	Point structure pointed to by the first argument is scaled.
 *
 * ----------------------------------------------------------------------------
 */

bool
DBScalePoint(p, n, d)
    Point *p;
    int n, d;
{
    bool result;
    result = DBScaleValue(&p->p_x, n, d);
    result |= DBScaleValue(&p->p_y, n, d);
    return result;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBScaleEverything ---
 *
 *    Scale all geometry by a factor of (scalen / scaled).  This procedure
 *    cannot be undone!
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Every single tile, label, and cell is altered.
 *	Geometry may be irrevocably distorted if transformed measurements
 *	would be sub-integer.  The MODIFIED flag is set on any cell for
 *	which any internal geometry does not scale precisely.
 *
 * ----------------------------------------------------------------------------
 */

void
DBScaleEverything(scalen, scaled)
    int scalen, scaled;
{
    void ToolScaleBox();

    int dbCellDefEnumFunc();
    LinkedCellDef *lhead, *lcd;

    // DBUpdateStamps();

    SigDisableInterrupts();

    lhead = NULL;
    (void) DBCellSrDefs(0, dbCellDefEnumFunc, (ClientData) &lhead);

    /* Apply scaling function to each CellDef */

    lcd = lhead;
    while (lcd != NULL)
    {
	dbScaleCell(lcd->cellDef, scalen, scaled);
	lcd = lcd->cd_next;
    }

    /* Free the linked CellDef list */

    lcd = lhead;
    while (lcd != NULL)
    {
	freeMagic((char *)lcd);
	lcd = lcd->cd_next;
    }

    /* Scale all elements */
    DBWScaleElements(scalen, scaled);

    /* Recovery of global plane pointers */
    MZAttachHintPlanes();

    /* Modify root box */
    ToolScaleBox(scalen, scaled);

    /* Modify crosshair position */
    DBWScaleCrosshair(scalen, scaled);

    SigEnableInterrupts();
}

/* Structure needed to hold information about the plane to copy */

struct scaleArg {
   int scalen;
   int scaled;
   int pnum;
   Plane *ptarget;
   bool doCIF;
   bool modified;
};

struct moveArg {
   int origx;
   int origy;
   int pnum;
   Plane *ptarget;
   bool modified;
};

/*
 * ----------------------------------------------------------------------------
 *
 * dbScalePlane --
 *
 *   Scaling procedure called on a single plane.  Copies paint into the
 *   new plane at a scalefactor according to ratio (scalen / scaled)
 *
 * ----------------------------------------------------------------------------
 */

bool
dbScalePlane(oldplane, newplane, pnum, scalen, scaled, doCIF)
    Plane *oldplane, *newplane;
    int pnum;
    int scalen, scaled;
    bool doCIF;
{
    int dbTileScaleFunc();		/* forward declaration */
    struct scaleArg arg;

    arg.scalen = scalen;
    arg.scaled = scaled;
    arg.ptarget = newplane;
    arg.pnum = pnum;
    arg.doCIF = doCIF;
    arg.modified = FALSE;
    (void) DBSrPaintArea((Tile *) NULL, oldplane, &TiPlaneRect,
		&DBAllButSpaceBits, dbTileScaleFunc, (ClientData) &arg);

    return arg.modified;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbTileScaleFunc --
 *
 *   Scaling procedure called on each tile being copied from one plane to
 *   another.  Scaling ratio (scalen / scaled) is stored inside the struct
 *   scaleArg before calling.
 *
 * ----------------------------------------------------------------------------
 */

int
dbTileScaleFunc(tile, scvals)
    Tile *tile;
    struct scaleArg *scvals;
{
    TileType type;
    Rect targetRect;
    TileType exact;

    TiToRect(tile, &targetRect);

    if (DBScalePoint(&targetRect.r_ll, scvals->scalen, scvals->scaled))
	scvals->modified = TRUE;
    if (DBScalePoint(&targetRect.r_ur, scvals->scalen, scvals->scaled))
	scvals->modified = TRUE;

    /* Tile scaled out of existence! */
    if ((targetRect.r_xtop - targetRect.r_xbot == 0) ||
		(targetRect.r_ytop - targetRect.r_ybot == 0))
    {
	TxPrintf("Tile 0x%x at (%d, %d) has zero area after scaling:  Removed.\n",
		tile, targetRect.r_xbot, targetRect.r_ybot);
	return 0;
    }

    type = TiGetTypeExact(tile);
    exact = type;
    if (IsSplit(tile))
	type = (SplitSide(tile)) ? SplitRightType(tile) : SplitLeftType(tile);
    DBNMPaintPlane(scvals->ptarget, exact, &targetRect,
		((scvals->doCIF) ? CIFPaintTable :
		DBStdPaintTbl(type, scvals->pnum)),
		(PaintUndoInfo *)NULL);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbMovePlane --
 *
 *   Relocation procedure called on a single plane.  Copies paint into the
 *   new plane at a delta position (-origx, -origy)
 *
 * ----------------------------------------------------------------------------
 */

bool
dbMovePlane(oldplane, newplane, pnum, origx, origy)
    Plane *oldplane, *newplane;
    int pnum;
    int origx, origy;
{
    int dbTileMoveFunc();		/* forward declaration */
    struct moveArg arg;

    arg.origx = origx;
    arg.origy = origy;
    arg.ptarget = newplane;
    arg.pnum = pnum;
    arg.modified = FALSE;
    (void) DBSrPaintArea((Tile *) NULL, oldplane, &TiPlaneRect,
		&DBAllButSpaceBits, dbTileMoveFunc, (ClientData) &arg);

    return arg.modified;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbTileMoveFunc --
 *
 *   Repositioning procedure called on each tile being copied from one plane
 *   to another.  Delta position (-origx, -origy) is stored inside the struct
 *   moveArg before calling.
 *
 * ----------------------------------------------------------------------------
 */

int
dbTileMoveFunc(tile, mvvals)
    Tile *tile;
    struct moveArg *mvvals;
{
    TileType type;
    Rect targetRect;
    TileType exact;

    TiToRect(tile, &targetRect);

    mvvals->modified = TRUE;
    DBMovePoint(&targetRect.r_ll, mvvals->origx, mvvals->origy);
    DBMovePoint(&targetRect.r_ur, mvvals->origx, mvvals->origy);

    type = TiGetTypeExact(tile);
    exact = type;
    if (IsSplit(tile))
	type = (SplitSide(tile)) ? SplitRightType(tile) : SplitLeftType(tile);
    DBNMPaintPlane(mvvals->ptarget, exact, &targetRect,
		DBStdPaintTbl(type, mvvals->pnum),
		(PaintUndoInfo *)NULL);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBSrCellUses --
 *
 *   Do function "func" for each cell use in cellDef, passing "arg" as
 *   client data.  This routine first collects a linked list of cell uses,
 *   then performs the function on the list, so that the search cannot be
 *   corrupted by (specifically) removing the use structure from the cell
 *   def's bplane.  Function "func" takes 2 arguments:
 *
 *	int func(Celluse *use, ClientData arg) {}
 *
 * Results:
 *	0 one successful completion of the search, 1 on error.
 *
 * Side Effects:
 *	Whatever "func" does.
 *
 * ----------------------------------------------------------------------------
 */

int
DBSrCellUses(cellDef, func, arg)
    CellDef *cellDef;	/* Pointer to CellDef to search for uses. */
    int (*func)();	/* Function to apply for each cell use.	  */
    ClientData arg;	/* data to be passed to function func().  */
{
    int dbCellUseEnumFunc();
    int retval;
    CellUse *use;
    LinkedCellUse *luhead, *lu;

    /* DBCellEnum() attempts to read unavailable celldefs.  We don't	*/
    /* want to do that here, so check CDAVAILABLE flag first.	  	*/

    if ((cellDef->cd_flags & CDAVAILABLE) == 0) return 0;

    /* Enumerate all unique cell uses, and scale their position,	*/
    /* transform, and array information.				*/

    luhead = NULL;

    retval = DBCellEnum(cellDef, dbCellUseEnumFunc, (ClientData) &luhead);

    lu = luhead;
    while (lu != NULL)
    {
	use = lu->cellUse;
	if ((*func)(use, arg)) {
	   retval = 1;
	   break;
	}
	lu = lu->cu_next;
    }

    /* Free this linked cellUse structure */
    lu = luhead;
    while (lu != NULL)
    {
	freeMagic((char *)lu);
	lu = lu->cu_next;
    }
    return retval;
}

/* Structure used by dbScaleProp() and dbMoveProp() */
typedef struct _cellpropstruct {
    Point cps_point;
    CellDef *cps_def;
} CellPropStruct;

/*
 * ----------------------------------------------------------------------------
 *
 * dbScaleProp --
 *
 *  Callback function for dbScaleCell.  Finds properties that represent
 *  internal geometry (FIXED_BBOX and MASKHINTS_*) and scale the values
 *  by the numerator / denominator values passed as a pointer to a Point
 *  structure, where p_x is the numerator value and p_y is the denominator
 *  value.
 *
 * ----------------------------------------------------------------------------
 */

int dbScaleProp(name, value, cps)
    char *name;
    char *value;
    CellPropStruct *cps;
{
    int scalen, scaled;
    char *newvalue;
    Rect r;

    if (!strcmp(name, "FIXED_BBOX") || !strncmp(name, "MASKHINTS_", 10))
    {
	if (sscanf(value, "%d %d %d %d", &r.r_xbot, &r.r_ybot,
			&r.r_xtop, &r.r_ytop) == 4)
	{
	    /* Scale numerator held in point X value, */
	    /* scale denominator held in point Y value */

	    scalen = cps->cps_point.p_x;
	    scaled = cps->cps_point.p_y;

	    DBScalePoint(&r.r_ll, scalen, scaled);
	    DBScalePoint(&r.r_ur, scalen, scaled);

	    newvalue = (char *)mallocMagic(40);
	    sprintf(newvalue, "%d %d %d %d", r.r_xbot, r.r_ybot,
			r.r_xtop, r.r_ytop);
	    DBPropPut(cps->cps_def, name, newvalue);
	}
    }
    return 0;	/* Keep enumerating through properties */
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbMoveProp --
 *
 *  Callback function for ??.  Finds properties that represent
 *  internal geometry (FIXED_BBOX and MASKHINTS_*) and modifies the values
 *  by the X, Y values passed as a pointer to a Point structure in ClientData.
 *
 * ----------------------------------------------------------------------------
 */

int dbMoveProp(name, value, cps)
    char *name;
    char *value;
    CellPropStruct *cps;
{
    int origx, origy;
    char *newvalue;
    Rect r;

    if (!strcmp(name, "FIXED_BBOX") || !strncmp(name, "MASKHINTS_", 10))
    {
	if (sscanf(value, "%d %d %d %d", &r.r_xbot, &r.r_ybot,
			&r.r_xtop, &r.r_ytop) == 4)
	{
	    origx = cps->cps_point.p_x;
	    origy = cps->cps_point.p_y;

	    DBMovePoint(&r.r_ll, origx, origy);
	    DBMovePoint(&r.r_ur, origx, origy);

	    newvalue = (char *)mallocMagic(40);
	    sprintf(newvalue, "%d %d %d %d", r.r_xbot, r.r_ybot,
			r.r_xtop, r.r_ytop);
	    DBPropPut(cps->cps_def, name, newvalue);
	}
    }
    return 0;	/* Keep enumerating through properties */
}


/*
 * ----------------------------------------------------------------------------
 *
 * dbScaleCell --
 *
 *   Scaling procedure called on each cell encountered in the search.
 *
 * ----------------------------------------------------------------------------
 */

int
dbScaleCell(cellDef, scalen, scaled)
    CellDef *cellDef;	/* Pointer to CellDef to be saved.  This def might
			 * be an internal buffer; if so, we ignore it.
			 */
    int scalen, scaled; /* scale numerator and denominator. */
{
    int dbCellScaleFunc(), dbCellUseEnumFunc();
    Label *lab;
    int pNum;
    LinkedTile *lhead, *lt;
    LinkedCellUse *luhead, *lu;
    Plane *newplane;
    BPlane *cellPlane, *cellPlaneOrig;
    CellPropStruct cps;

    /* DBCellEnum() attempts to read unavailable celldefs.  We don't	*/
    /* want to do that here, so check CDAVAILABLE flag first.	  	*/

    if ((cellDef->cd_flags & CDAVAILABLE) == 0)
	goto donecell;
    else
	cellDef->cd_flags |= CDBOXESCHANGED;

    /* Enumerate all unique cell uses, and scale their position,	*/
    /* transform, and array information.				*/

    luhead = NULL;

    (void) DBCellEnum(cellDef, dbCellUseEnumFunc, (ClientData) &luhead);

    cellPlane = BPNew();
    lu = luhead;
    while (lu != NULL)
    {
	CellUse *use;
	Rect *bbox;

	use = lu->cellUse;
	bbox = &use->cu_bbox;

	/* TxPrintf("CellUse: BBox is ll (%d, %d), transform [%d %d %d %d %d %d]\n",
		bbox->r_xbot, bbox->r_ybot,
		use->cu_transform.t_a, use->cu_transform.t_b, use->cu_transform.t_c,
		use->cu_transform.t_d, use->cu_transform.t_e, use->cu_transform.t_f); */

	DBScalePoint(&bbox->r_ll, scalen, scaled);
	DBScalePoint(&bbox->r_ur, scalen, scaled);

	bbox = &use->cu_extended;
	DBScalePoint(&bbox->r_ll, scalen, scaled);
	DBScalePoint(&bbox->r_ur, scalen, scaled);

	DBScaleValue(&use->cu_transform.t_c, scalen, scaled);
	DBScaleValue(&use->cu_transform.t_f, scalen, scaled);

	DBScaleValue(&use->cu_array.ar_xsep, scalen, scaled);
	DBScaleValue(&use->cu_array.ar_ysep, scalen, scaled);

	BPAdd(cellPlane, use);

	lu = lu->cu_next;
    }

    /* Swap the CellDef's cell plane */
    cellPlaneOrig = cellDef->cd_cellPlane;
    cellDef->cd_cellPlane = cellPlane;
    BPFree(cellPlaneOrig);

    /* Free this linked cellUse structure */
    lu = luhead;
    while (lu != NULL)
    {
	freeMagic((char *)lu);
	lu = lu->cu_next;
    }

    /* Scale all of the paint tiles in this cell by creating a new plane */
    /* and copying all tiles into the new plane at scaled dimensions.	 */

    for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
    {
	if (cellDef->cd_planes[pNum] == NULL) continue;
	newplane = DBNewPlane((ClientData) TT_SPACE);
	DBClearPaintPlane(newplane);
	if (dbScalePlane(cellDef->cd_planes[pNum], newplane, pNum,
		scalen, scaled, FALSE))
	    cellDef->cd_flags |= (CDMODIFIED | CDGETNEWSTAMP);
        DBFreePaintPlane(cellDef->cd_planes[pNum]);
	TiFreePlane(cellDef->cd_planes[pNum]);
        cellDef->cd_planes[pNum] = newplane;
    }

    /* Also scale the position of all labels. */
    /* If labels are the rendered-font type, scale the size as well */

    if (cellDef->cd_labels)
    {
	int i;
	for (lab = cellDef->cd_labels; lab; lab = lab->lab_next)
	{
	    DBScalePoint(&lab->lab_rect.r_ll, scalen, scaled);
	    DBScalePoint(&lab->lab_rect.r_ur, scalen, scaled);

	    if (lab->lab_font >= 0)
	    {
		DBScalePoint(&lab->lab_offset, scalen, scaled);
		DBScaleValue(&lab->lab_size, scalen, scaled);

		DBScalePoint(&lab->lab_bbox.r_ll, scalen, scaled);
		DBScalePoint(&lab->lab_bbox.r_ur, scalen, scaled);

		for (i = 0; i < 4; i++)
		    DBScalePoint(&lab->lab_corners[i], scalen, scaled);
	    }
	}
    }

donecell:

    /* The cellDef bounding box gets expanded to match the new scale. */

    DBScalePoint(&cellDef->cd_bbox.r_ll, scalen, scaled);
    DBScalePoint(&cellDef->cd_bbox.r_ur, scalen, scaled);
    DBScalePoint(&cellDef->cd_extended.r_ll, scalen, scaled);
    DBScalePoint(&cellDef->cd_extended.r_ur, scalen, scaled);

    /* If the cell is an abstract view with a fixed bounding box, then  */
    /* adjust the bounding box property to match the new scale.         */

    if ((cellDef->cd_flags & CDFIXEDBBOX) != 0)
    {
	Rect r;
	bool found;
	char *propval;

	propval = (char *)DBPropGet(cellDef, "FIXED_BBOX", &found);
	if (found)
	{
	    if (sscanf(propval, "%d %d %d %d", &r.r_xbot, &r.r_ybot,
			&r.r_xtop, &r.r_ytop) == 4)
	    {
		DBScalePoint(&r.r_ll, scalen, scaled);
		DBScalePoint(&r.r_ur, scalen, scaled);

		propval = (char *)mallocMagic(40);
		sprintf(propval, "%d %d %d %d", r.r_xbot, r.r_ybot,
			r.r_xtop, r.r_ytop);
		DBPropPut(cellDef, "FIXED_BBOX", propval);
	    }
	}
    }

    /* Check all properties for ones with keys beginning with "MASKHINTS_"
     * or the key "FIXED_BBOX", and scale them by the same amount as all
     * the geometry.
     */


    cps.cps_point.p_x = scalen;
    cps.cps_point.p_y = scaled;
    cps.cps_def = cellDef;
    DBPropEnum(cellDef, dbScaleProp, &cps);

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbCellDefEnumFunc --
 *
 *   Enumeration procedure called on each CellDef encountered in the search of
 *   cells in the hierarchy.  Adds the CellDef to a linked list of celldefs.
 *
 * ----------------------------------------------------------------------------
 */

int
dbCellDefEnumFunc(cellDef, arg)
    CellDef *cellDef;
    LinkedCellDef **arg;
{
    LinkedCellDef *lcd;

    lcd = (LinkedCellDef *) mallocMagic(sizeof(LinkedCellDef));

    lcd->cellDef = cellDef;
    lcd->cd_next = (*arg);
    (*arg) = lcd;

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbCellUseEnumFunc --
 *
 *   Enumeration procedure called on each CellDef encountered in the search of
 *   cells in the hierarchy.  Adds the CellDef to a linked list of celldefs.
 *
 * ----------------------------------------------------------------------------
 */

int
dbCellUseEnumFunc(cellUse, arg)
    CellUse *cellUse;
    LinkedCellUse **arg;
{
    LinkedCellUse *lcu;

    lcu = (LinkedCellUse *) mallocMagic(sizeof(LinkedCellUse));

    lcu->cellUse = cellUse;
    lcu->cu_next = (*arg);
    (*arg) = lcu;

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBMoveCell --
 *
 *   Reposition a cell to a different origin.  This routine is equivalent to
 *   unexpanding all contents of a cell, selecting everything, and issuing a
 *   move command.  However, for very large layouts this becomes memory- and
 *   compute- intensive.  The process of reorienting an entire layout to a
 *   new position can be done much more efficiently.  This routine is
 *   essentially a copy of dbScaleCell() but changes only position and not
 *   scale.
 *
 * ----------------------------------------------------------------------------
 */

int
DBMoveCell(cellDef, origx, origy)
    CellDef *cellDef;	/* Pointer to CellDef to be saved.  This def might
			 * be an internal buffer; if so, we ignore it.
			 */
    int origx, origy; /* Internal unit coordinates which will become the new origin */
{
    int dbCellTileEnumFunc(), dbCellUseEnumFunc();
    Label *lab;
    int pNum;
    LinkedTile *lhead, *lt;
    LinkedCellUse *luhead, *lu;
    Plane *newplane;
    BPlane *cellPlane, *cellPlaneOrig;
    CellPropStruct cps;

    /* Unlike dbScaleCell(), this routine is only run on valid edit defs */

    cellDef->cd_flags |= CDBOXESCHANGED;

    /* Enumerate all unique cell uses, and move their position in the	*/
    /* bounding box and transform.					*/

    luhead = NULL;

    (void) DBCellEnum(cellDef, dbCellUseEnumFunc, (ClientData) &luhead);

    cellPlane = BPNew();
    lu = luhead;
    while (lu != NULL)
    {
	CellUse *use;
	Rect *bbox;

	use = lu->cellUse;
	bbox = &use->cu_bbox;

	/* TxPrintf("CellUse: BBox is ll (%d, %d), transform [%d %d %d %d %d %d]\n",
		bbox->r_xbot, bbox->r_ybot,
		use->cu_transform.t_a, use->cu_transform.t_b, use->cu_transform.t_c,
		use->cu_transform.t_d, use->cu_transform.t_e, use->cu_transform.t_f); */

	DBMovePoint(&bbox->r_ll, origx, origy);
	DBMovePoint(&bbox->r_ur, origx, origy);

	bbox = &use->cu_extended;
	DBMovePoint(&bbox->r_ll, origx, origy);
	DBMovePoint(&bbox->r_ur, origx, origy);

	use->cu_transform.t_c -= origx;
	use->cu_transform.t_f -= origy;

	BPAdd(cellPlane, use);

	lu = lu->cu_next;
    }

    /* Swap the CellDef's cell plane */
    cellPlaneOrig = cellDef->cd_cellPlane;
    cellDef->cd_cellPlane = cellPlane;
    BPFree(cellPlaneOrig);

    /* Free this linked cellUse structure */
    lu = luhead;
    while (lu != NULL)
    {
	freeMagic((char *)lu);
	lu = lu->cu_next;
    }

    /* Move all of the paint tiles in this cell by creating a new plane */
    /* and copying all tiles into the new plane at the new position.	 */

    for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
    {
	if (cellDef->cd_planes[pNum] == NULL) continue;
	newplane = DBNewPlane((ClientData) TT_SPACE);
	DBClearPaintPlane(newplane);
	if (dbMovePlane(cellDef->cd_planes[pNum], newplane, pNum,
		origx, origy, FALSE))
	    cellDef->cd_flags |= (CDMODIFIED | CDGETNEWSTAMP);
        DBFreePaintPlane(cellDef->cd_planes[pNum]);
	TiFreePlane(cellDef->cd_planes[pNum]);
        cellDef->cd_planes[pNum] = newplane;
    }

    /* Also move the position of all labels. */

    if (cellDef->cd_labels)
    {
	for (lab = cellDef->cd_labels; lab; lab = lab->lab_next)
	{
	    DBMovePoint(&lab->lab_rect.r_ll, origx, origy);
	    DBMovePoint(&lab->lab_rect.r_ur, origx, origy);

	    if (lab->lab_font >= 0)
	    {
		DBMovePoint(&lab->lab_bbox.r_ll, origx, origy);
		DBMovePoint(&lab->lab_bbox.r_ur, origx, origy);
	    }
	}
    }

donecell:

    /* The cellDef bounding box gets moved to match the new position. */

    DBMovePoint(&cellDef->cd_bbox.r_ll, origx, origy);
    DBMovePoint(&cellDef->cd_bbox.r_ur, origx, origy);
    DBMovePoint(&cellDef->cd_extended.r_ll, origx, origy);
    DBMovePoint(&cellDef->cd_extended.r_ur, origx, origy);

    /* Check all properties for ones with keys beginning with "MASKHINTS_"
     * or the key "FIXED_BBOX", and move them by the same amount as all
     * the geometry.
     */

    cps.cps_point.p_x = origx;
    cps.cps_point.p_y = origy;
    cps.cps_def = cellDef;
    DBPropEnum(cellDef, dbMoveProp, &cps);
    return 0;
}

