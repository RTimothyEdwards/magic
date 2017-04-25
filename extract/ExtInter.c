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
    (void) extCellSrArea(&scx, extInterSubtree, (ClientData) NULL);

    /*
     * Process parent paint if there were any subcells.
     * We compare each paint rectangle with all the paint in
     * all the subtrees, to see if there is an overlap.
     */
    if (extInterUse)
    {
	extInterUse = (CellUse *) NULL;
	(void) extCellSrArea(&scx, extInterSubtreePaint, (ClientData) def);
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
	(void) extCellSrArea(&parentScx, extInterSubtreeClip, (ClientData) scx);
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
    (void) extCellSrArea(&newscx, extInterOverlapSubtree, (ClientData) NULL);
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
 * instead of TiSrArea as our paint-tile enumeration function.
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
	if (!DBCellRead(def, (char *) NULL, TRUE, NULL)) return 0;

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
    return (extCellSrArea(scx, extTreeSrFunc, (ClientData) &filter));
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
	if (!DBCellRead(def, (char *) NULL, TRUE, NULL)) return (0);

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
    return (extCellSrArea(scx, extTreeSrFunc, (ClientData) fp));
}

/*
 *-----------------------------------------------------------------------------
 *
 * extCellSrArea --
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
extCellSrArea(scx, func, cdarg)
    SearchContext *scx;
			/* Pointer to search context specifying a cell use to
			 * search, an area in the coordinates of the cell's
			 * def, and a transform back to "root" coordinates.
			 * The area may have zero size.
			 */
    int (*func)();	/* Function to apply at every tile found */
    ClientData cdarg;	/* Argument to pass to function */
{
    int xlo, xhi, ylo, yhi, xbase, ybase, xsep, ysep, clientResult;
    int srchBot, srchRight;
    Plane *plane = scx->scx_use->cu_def->cd_planes[PL_CELL];
    Tile *tp, *tpnew;
    Rect *rect, *bbox;
    CellUse *use;
    SearchContext newScx;
    CellTileBody *body;
    Transform t, tinv;
    TreeFilter filter;
    Rect expanded;
    Point start;

    filter.tf_func = func;
    filter.tf_arg = cdarg;

    if ((scx->scx_use->cu_def->cd_flags & CDAVAILABLE) == 0)
	if (!DBCellRead(scx->scx_use->cu_def, (char *) NULL, TRUE, NULL))
	    return 0;
    
    /*
     * In order to make this work with zero-size areas, we first expand
     * the area by before searching the tile plane.  extCellSrFunc will
     * check carefully to throw out things that don't overlap the original
     * area.  The expansion is tricky because we mustn't expand infinities.
     */

    expanded = scx->scx_area;
    if (expanded.r_xbot > TiPlaneRect.r_xbot) expanded.r_xbot -= 1;
    if (expanded.r_ybot > TiPlaneRect.r_ybot) expanded.r_ybot -= 1;
    if (expanded.r_xtop < TiPlaneRect.r_xtop) expanded.r_xtop += 1;
    if (expanded.r_ytop < TiPlaneRect.r_ytop) expanded.r_ytop += 1;
    rect = &expanded;

    /* Start along the top of the LHS of the search area */
    start.p_x = rect->r_xbot;
    start.p_y = rect->r_ytop - 1;
    tp = plane->pl_hint;
    GOTOPOINT(tp, &start);

    /* Each iteration visits another tile on the LHS of the search area */
    while (TOP(tp) > rect->r_ybot)
    {
	/* Each iteration enumerates another tile */
enumerate:
	plane->pl_hint = tp;
	if (SigInterruptPending)
	    return (1);

	/*
	 * Since subcells are allowed to overlap, a single tile body may
	 * refer to many subcells and a single subcell may be referred to
	 * by many tile bodies.  To insure that each CellUse is enumerated
	 * exactly once, the procedure given to DBCellSrArea is only applied
	 * to a CellUse when its lower right corner is contained in the
	 * tile to dbCellSrFunc (or otherwise at the last tile encountered
	 * in the event the lower right corner of the CellUse is outside the
	 * search rectangle).
	 */
	srchBot = scx->scx_area.r_ybot;
	srchRight = scx->scx_area.r_xtop;
	for (body = (CellTileBody *) TiGetBody(tp);
		body != NULL;
		body = body->ctb_next)
	{
	    use = newScx.scx_use = body->ctb_use;
	    ASSERT(use != (CellUse *) NULL, "dbCellSrFunc");

	    /*
	     * The check below is to ensure that we only enumerate each
	     * cell once, even though it appears in many different tiles
	     * in the subcell plane.
	     */
	    bbox = &use->cu_bbox;
	    if (   (BOTTOM(tp) <= bbox->r_ybot ||
		    (BOTTOM(tp) <= srchBot && bbox->r_ybot < srchBot))
		&& (RIGHT(tp) >= bbox->r_xtop ||
		    (RIGHT(tp) >= srchRight && bbox->r_xtop >= srchRight)))
	    {
		/*
		 * Make sure that this cell really does overlap the
		 * search area (it could be just touching because of
		 * the expand-by-one in DBCellSrArea).
		 */
		if (!GEO_OVERLAP(&scx->scx_area, bbox)) continue;

		/* If not an array element, it's much simpler */
		if (use->cu_xlo == use->cu_xhi && use->cu_ylo == use->cu_yhi)
		{
		    newScx.scx_x = use->cu_xlo, newScx.scx_y = use->cu_yhi;
		    if (SigInterruptPending) return 1;
		    GEOINVERTTRANS(&use->cu_transform, &tinv);
		    GEOTRANSTRANS(&use->cu_transform, &scx->scx_trans,
				    &newScx.scx_trans);
		    GEOTRANSRECT(&tinv, &scx->scx_area, &newScx.scx_area);
		    if ((*func)(&newScx, filter.tf_arg) == 1)
			return 1;
		    continue;
		}

		/*
		 * More than a single array element;
		 * check to see which ones overlap our search area.
		 */
		DBArrayOverlap(use, &scx->scx_area, &xlo, &xhi, &ylo, &yhi);
		xsep = (use->cu_xlo > use->cu_xhi)
				? -use->cu_xsep : use->cu_xsep;
		ysep = (use->cu_ylo > use->cu_yhi)
				? -use->cu_ysep : use->cu_ysep;
		for (newScx.scx_y = ylo; newScx.scx_y<=yhi; newScx.scx_y++)
		    for (newScx.scx_x = xlo; newScx.scx_x<=xhi; newScx.scx_x++)
		    {
			if (SigInterruptPending) return 1;
			xbase = xsep * (newScx.scx_x - use->cu_xlo);
			ybase = ysep * (newScx.scx_y - use->cu_ylo);
			GEOTRANSTRANSLATE(xbase, ybase, &use->cu_transform, &t);
			GEOINVERTTRANS(&t, &tinv);
			GEOTRANSTRANS(&t, &scx->scx_trans, &newScx.scx_trans);
			GEOTRANSRECT(&tinv, &scx->scx_area, &newScx.scx_area);
			clientResult = (*func)(&newScx, filter.tf_arg);
			if (clientResult == 2) goto skipArray;
			else if (clientResult == 1) return 1;
		    }
	    }
	    skipArray: continue;
	}

	tpnew = TR(tp);
	if (LEFT(tpnew) < rect->r_xtop)
	{
	    while (BOTTOM(tpnew) >= rect->r_ytop) tpnew = LB(tpnew);
	    if (BOTTOM(tpnew) >= BOTTOM(tp) || BOTTOM(tp) <= rect->r_ybot)
	    {
		tp = tpnew;
		goto enumerate;
	    }
	}

	/* Each iteration returns one tile further to the left */
	while (LEFT(tp) > rect->r_xbot)
	{
	    if (BOTTOM(tp) <= rect->r_ybot)
		return (0);
	    tpnew = LB(tp);
	    tp = BL(tp);
	    if (BOTTOM(tpnew) >= BOTTOM(tp) || BOTTOM(tp) <= rect->r_ybot)
	    {
		tp = tpnew;
		goto enumerate;
	    }
	}

	/* At left edge -- walk down to next tile along the left edge */
	for (tp = LB(tp); RIGHT(tp) <= rect->r_xbot; tp = TR(tp))
	    /* Nothing */;
    }
    return (0);
}
