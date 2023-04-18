/*
 * ExtInteraction.c --
 *
 * Circuit extraction.
 * Finds interaction areas.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/extract/ExtInter.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "utils/undo.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/malloc.h"
#include "textio/textio.h"
#include "debug/debug.h"
#include "extract/extract.h"
#include "extract/extractInt.h"
#include "utils/signals.h"
#include "utils/styles.h"

    /* Local data */
CellUse *extInterUse = (CellUse *) NULL;	/* Subtree being processed */
Plane *extInterPlane;				/* Paint into this plane */
int extInterHalo;				/* Elements closer than this
						 * constitute an interaction.
						 */
int extInterBloat;				/* Bloat by this much when
						 * painting into result plane.
						 */

    /* Forward declarations */
int extInterOverlapSubtree();
int extInterOverlapTile();
int extInterSubtree();
int extInterSubtreeClip();
int extInterSubtreeElement();
int extInterSubtreeTile();
int extInterSubtreePaint();

#define	BLOATBY(r, h) ( (r)->r_xbot -= (h), (r)->r_ybot -= (h), \
			(r)->r_xtop += (h), (r)->r_ytop += (h) )

/*
 * ----------------------------------------------------------------------------
 *
 * ExtFindInteractions --
 *
 * Paint into the supplied tile plane 'resultPlane' TT_ERROR_P tiles
 * for each area in the CellDef 'def' that must be processed for
 * interactions.
 *
 * Each interaction arises from paint in two different subtrees
 * being less than (but not equal to) 'halo' units away from
 * each other.  In this definition, a subtree refers to a single
 * CellUse, which may be either a single cell or an entire array.
 *
 * If 'bloat' is non-zero, each interaction area is bloated by
 * this amount when being painted into the result plane.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Paints into the plane 'resultPlane'.
 *
 * ----------------------------------------------------------------------------
 */

void
ExtFindInteractions(def, halo, bloatby, resultPlane)
    CellDef *def;	/* Find interactions among children of def */
    int halo;		/* Interaction is elements closer than halo */
    int bloatby;	/* Bloat each interaction area by this amount when
			 * painting into resultPlane.
			 */
    Plane *resultPlane;	/* Paint interaction areas into this plane */
{
    SearchContext scx;

    UndoDisable();
    extInterPlane = resultPlane;
    extInterHalo = halo;
    extInterBloat = bloatby;
    extParentUse->cu_def = def;
    scx.scx_use = extParentUse;
    scx.scx_trans = GeoIdentityTransform;
    scx.scx_area = def->cd_bbox;

    /*
     * Process each child subtree.
     * This involves comparing all the paint in the subtree
     * with all the paint in all other subtrees up to, but
     * not including, the subtree under consideration.
     */
    extInterUse = (CellUse *) NULL;
    (void) DBCellSrArea(&scx, extInterSubtree, (ClientData) NULL);

    /*
     * Process parent paint if there were any subcells.
     * We compare each paint rectangle with all the paint in
     * all the subtrees, to see if there is an overlap.
     */
    if (extInterUse)
    {
	extInterUse = (CellUse *) NULL;
	(void) DBCellSrArea(&scx, extInterSubtreePaint, (ClientData) def);
    }
    UndoEnable();
}

int
extInterSubtreePaint(scx, def)
    SearchContext *scx;
    CellDef *def;
{
    Rect r;
    int pNum;

    r = scx->scx_use->cu_bbox;
    BLOATBY(&r, extInterHalo);
    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	(void) DBSrPaintArea((Tile *) NULL, def->cd_planes[pNum], &r,
	    &DBAllButSpaceAndDRCBits, extInterSubtreeTile, (ClientData) NULL);

    return (2);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extInterSubtree --
 *
 * Called for each immediate child use of the cell being processed
 * for interactions.  Our job is to process all the paint in this
 * use against all other subtrees overlapping this one.
 *
 * Results:
 *	Returns 2 to abort after the first array element.
 *
 * Side effects:
 *	Sets extInterUse to scx->scx_use.
 *	Children may paint into extInterPlane.
 *
 * ----------------------------------------------------------------------------
 */

int
extInterSubtree(scx)
    SearchContext *scx;
{
    CellUse *oldUse = extInterUse;
    SearchContext parentScx;

    extInterUse = scx->scx_use;
    if (oldUse)
    {
	/* Find all other subtrees overlapping this cell */
	parentScx.scx_area = scx->scx_use->cu_bbox;
	BLOATBY(&parentScx.scx_area, extInterHalo);
	parentScx.scx_trans = GeoIdentityTransform;
	parentScx.scx_use = extParentUse;
	(void) DBCellSrArea(&parentScx, extInterSubtreeClip, (ClientData) scx);
    }
    return (2);
}

int
extInterSubtreeClip(overlapScx, scx)
    SearchContext *overlapScx, *scx;
{
    Rect r, r2;

    /* Only search as far as extInterUse */
    if (overlapScx->scx_use == extInterUse)
	return (2);

    /*
     * Only process the overlap between overlapScx and scx,
     * bloating both by extInterHalo.
     */
    r = overlapScx->scx_use->cu_bbox;
    BLOATBY(&r, extInterHalo);
    r2 = scx->scx_use->cu_bbox;
    BLOATBY(&r2, extInterHalo);
    GEOCLIP(&r, &r2);

    (void) DBArraySr(scx->scx_use, &r, extInterSubtreeElement,
		(ClientData) &r);
    return (2);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extInterSubtreeElement --
 *
 * Called for each element in the array forming the use passed to
 * extInterSubtree().  See extInterSubtree() for comments.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	See ExtFindInteractions.
 *
 * ----------------------------------------------------------------------------
 */

int
extInterSubtreeElement(use, trans, x, y, r)
    CellUse *use;
    Transform *trans;
    int x, y;
    Rect *r;
{
    SearchContext scx;
    Transform tinv;

    scx.scx_use = use;
    scx.scx_trans = *trans;
    scx.scx_x = x;
    scx.scx_y = y;
    GEOINVERTTRANS(trans, &tinv);
    GEOTRANSRECT(&tinv, r, &scx.scx_area);
    (void) DBTreeSrTiles(&scx, &DBAllButSpaceAndDRCBits, 0,
		extInterSubtreeTile, (ClientData) NULL);
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extInterSubtreeTile --
 *
 * Called for each tile in the subtree being processed by
 * extInterSubtree().  Transform this tile to root coordinates,
 * bloating by extInterHalo, and then call extInterOverlapSubtree
 * to process all the other subtrees for paint overlapping
 * this bloated area.  If the argument 'cxp' is non-NULL, we
 * use cxp->tc_scx->scx_trans to transform the area of tile to
 * root coordinates; otherwise, we don't transform it at all.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	See extInterOverlapTile.
 *
 * ----------------------------------------------------------------------------
 */

int
extInterSubtreeTile(tile, cxp)
    Tile *tile;
    TreeContext *cxp;
{
    SearchContext newscx;
    Rect r;

    TITORECT(tile, &r);
    BLOATBY(&r, extInterHalo);
    if (cxp)
    {
	GEOTRANSRECT(&cxp->tc_scx->scx_trans, &r, &newscx.scx_area);
    }
    else newscx.scx_area = r;
    newscx.scx_trans = GeoIdentityTransform;
    newscx.scx_use = extParentUse;
    (void) DBCellSrArea(&newscx, extInterOverlapSubtree, (ClientData) NULL);
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extInterOverlapSubtree --
 *
 * Called for each subcell of the root that overlaps the piece
 * of paint found by extInterSubtreeTile() above.  We stop
 * as soon as we see extInterUse; otherwise, search all the
 * cells in the subtree rooted at scx->scx_use for paint
 * overlapping scx->scx_area.
 *
 * Results:
 *	Returns 2 if we see extInterUse; otherwise, returns 0.
 *
 * Side effects:
 *	Paints into the plane 'resultPlane'; see extInterOverlapTile.
 *
 * ----------------------------------------------------------------------------
 */

int
extInterOverlapSubtree(scx)
    SearchContext *scx;
{
    if (extInterUse == scx->scx_use)
	return (2);

    (void) extTreeSrPaintArea(scx, extInterOverlapTile, (ClientData) NULL);
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extInterOverlapTile --
 *
 * Called for each piece of paint overlapping the piece found
 * by extInterSubtreeTile().  Bloat the found piece by extInterHalo,
 * then clip to the area of the overlapping piece of paint in root
 * coordinates.  If the result is non-empty, paint it into the
 * plane extInterPlane.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Paints into the plane 'resultPlane'.
 *
 * ----------------------------------------------------------------------------
 */

int
extInterOverlapTile(tile, cxp)
    Tile *tile;
    TreeContext *cxp;
{
    SearchContext *scx = cxp->tc_scx;
    Rect r, rootr;

    TITORECT(tile, &r);
    BLOATBY(&r, extInterHalo);
    GEOCLIP(&r, &scx->scx_area);
    if (GEO_RECTNULL(&r))
	return (0);

    GEOTRANSRECT(&scx->scx_trans, &r, &rootr);
    BLOATBY(&rootr, extInterBloat);
    DBPaintPlane(extInterPlane, &rootr, DBStdWriteTbl(TT_ERROR_P),
		    (PaintUndoInfo *) NULL);

    return (0);
}

/*
 *-----------------------------------------------------------------------------
 *
 * extTreeSrPaintArea --
 *
 * Recursively search downward from the supplied CellUse for
 * all paint tiles.
 *
 * The procedure should be of the following form:
 *
 *	int
 *	func(tile, scx, cdata)
 *	    Tile *tile;
 *	    SearchContext *scx;
 *	    ClientData cdata;
 *	{
 *	}
 *
 * The SearchContext is stored in cxp->tc_scx, and the user's arg is stored
 * in cxp->tc_filter->tf_arg.
 *
 * In the above, the scx transform is the net transform from the coordinates
 * of tile to "world" coordinates (or whatever coordinates the initial
 * transform supplied to extTreeSrTiles was a transform to).  Func returns
 * 0 under normal conditions.  If 1 is returned, it is a request to
 * abort the search.
 *
 *			*** WARNING ***
 *
 * The client procedure should not modify any of the paint planes in
 * the cells visited by extTreeSrTiles, because we use DBSrPaintArea
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
extTreeSrPaintArea(scx, func, cdarg)
    SearchContext *scx;		/* Pointer to search context specifying
				 * a cell use to search, an area in the
				 * coordinates of the cell's def, and a
				 * transform back to "root" coordinates.
				 */
    int (*func)();		/* Function to apply at each qualifying tile */
    ClientData cdarg;		/* Client data for above function */
{
    int extTreeSrFunc();
    CellDef *def = scx->scx_use->cu_def;
    TreeContext context;
    TreeFilter filter;
    int pNum;

    if ((def->cd_flags & CDAVAILABLE) == 0)
	if (!DBCellRead(def, TRUE, TRUE, NULL))
	    return 0;

    filter.tf_func = func;
    filter.tf_arg = cdarg;
    context.tc_scx = scx;
    context.tc_filter = &filter;

    /*
     * Apply the function first to any of the tiles in the planes
     * for this CellUse's CellDef that match the mask.
     */
    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	if (DBSrPaintArea((Tile *) NULL, def->cd_planes[pNum],
		&scx->scx_area, &DBAllButSpaceAndDRCBits, func,
		(ClientData) &context))
	    return (1);

    /* Visit our children recursively */
    return (DBCellSrArea(scx, extTreeSrFunc, (ClientData) &filter));
}

/*
 * extTreeSrFunc --
 *
 * Filter procedure applied to subcells by extTreeSrPaintArea().
 */

int
extTreeSrFunc(scx, fp)
    SearchContext *scx;
    TreeFilter *fp;
{
    CellDef *def = scx->scx_use->cu_def;
    TreeContext context;
    int pNum;

    if ((def->cd_flags & CDAVAILABLE) == 0)
	if (!DBCellRead(def, TRUE, TRUE, NULL))
	    return 0;

    context.tc_scx = scx;
    context.tc_filter = fp;

    /*
     * Apply the function first to any of the tiles in the planes
     * for this CellUse's CellDef that match the mask.
     */
    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	if (DBSrPaintArea((Tile *) NULL, def->cd_planes[pNum],
		&scx->scx_area, &DBAllButSpaceAndDRCBits,
		fp->tf_func, (ClientData) &context))
	    return (1);

    /* Visit our children recursively */
    return (DBCellSrArea(scx, extTreeSrFunc, (ClientData) fp));
}

