/* CIFhier.c -
 *
 *	This module handles hierarchy as part of the CIF generator.
 *	Because of the nature of the geometrical operations used
 *	to generate CIF, such as grow and shrink, the CIF representing
 *	two nearby subcells (or elements of an array) may require
 *	more than just the combined CIF of the two subcells considered
 *	separately.  This module computes the extra CIF that may be
 *	needed.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/cif/CIFhier.c,v 1.3 2010/06/24 12:37:15 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "cif/CIFint.h"
#include "cif/cif.h"
#include "drc/drc.h"
#include "textio/textio.h"
#include "utils/undo.h"
#include "utils/malloc.h"
#include "utils/signals.h"

/* To compute CIF where there are interaction areas we do two things:
 * 1. Compute the CIF by combining all the material in all the interacting
 *    cells together.  CIFTotalUse and CIFTotalDef and CIFTotalPlanes
 *    are used to hold the flattened material and resulting CIF.
 * 2. Compute the CIF that results by considering the material in each
 *    subtree separately and also the material in the parent separately.
 *    The paint for each subtree is first flattened into CIFComponentUse
 *    and CIFComponentDef, and the resulting CIF from each subtree is
 *    OR'ed together into CIFComponentPlanes.
 */

CellUse *CIFTotalUse = NULL;
CellDef *CIFTotalDef;
CellUse *CIFComponentUse;
CellDef *CIFComponentDef;
Plane *CIFTotalPlanes[MAXCIFLAYERS];
Plane *CIFComponentPlanes[MAXCIFLAYERS];

/* The following use is just used to turn a def into a use for calling
 * procedures that want a use.
 */

CellUse *CIFDummyUse;

/* The following local variables are used to share information
 * between top-level procedures and search functions.
 */

/* Used in cifHierPaintArrayFunc: */

Plane *cifHierCurPlane;			/* Current plane. */
static int cifHierXSpacing, cifHierYSpacing, cifHierXCount, cifHierYCount;

/* Used in cifGrowSliver */
static CIFLayer *CurCifLayer;

/* Macro for scaling boxes into CIF coordinates: */
#define SCALE(src, scale, dst) \
    (dst)->r_xbot = (src)->r_xbot * scale; \
    (dst)->r_ybot = (src)->r_ybot * scale; \
    (dst)->r_xtop = (src)->r_xtop * scale; \
    (dst)->r_ytop = (src)->r_ytop * scale;


/*
 * ----------------------------------------------------------------------------
 *
 * CIFInitCells --
 *
 * 	This procedure just sets up cell definitions and uses needed
 *	for hierarchical checking and other CIF uses.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	DRCUse, DRCDef, and DRCDummyUse are set up if they're not
 *	there already.
 *
 * ----------------------------------------------------------------------------
 */

void
CIFInitCells()
{
    int i;

    if (CIFTotalUse != NULL) return;

    CIFTotalDef = DBCellLookDef("__CIF__");
    if (CIFTotalDef == (CellDef *) NULL)
    {
	CIFTotalDef = DBCellNewDef ("__CIF__",(char *) NULL);
	ASSERT(CIFTotalDef != (CellDef *) NULL, "cifMakeCell");
	DBCellSetAvail(CIFTotalDef);
	CIFTotalDef->cd_flags |= CDINTERNAL;
    }
    CIFTotalUse = DBCellNewUse (CIFTotalDef, (char *) NULL);
    DBSetTrans (CIFTotalUse, &GeoIdentityTransform);
    CIFTotalUse->cu_expandMask = CU_DESCEND_SPECIAL;	/* This is always expanded. */

    CIFComponentDef = DBCellLookDef("__CIF2__");
    if (CIFComponentDef == (CellDef *) NULL)
    {
	CIFComponentDef = DBCellNewDef ("__CIF2__",(char *) NULL);
	ASSERT(CIFComponentDef != (CellDef *) NULL, "cifMakeCell");
	DBCellSetAvail(CIFComponentDef);
	CIFComponentDef->cd_flags |= CDINTERNAL;
    }
    CIFComponentUse = DBCellNewUse (CIFComponentDef, (char *) NULL);
    DBSetTrans (CIFComponentUse, &GeoIdentityTransform);
    CIFComponentUse->cu_expandMask = CU_DESCEND_SPECIAL; /* This is always expanded. */

    /* Clear out the planes used to collect hierarchical CIF. */

    for (i = 0; i < MAXCIFLAYERS; i++)
    {
	CIFComponentPlanes[i] = NULL;
	CIFTotalPlanes[i] = NULL;
    }

    /* Also create a dummy cell use to use for passing to
     * procedures that need a use when all we've got is a def.
     */

    CIFDummyUse = DBCellNewUse(CIFTotalDef, (char *) NULL);
    DBSetTrans (CIFDummyUse, &GeoIdentityTransform);
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifHierCleanup --
 *
 * 	This procedure is called after CIF hierarchical processing
 *	to clean up the cells and tile planes use for hierarchy and
 *	release storage.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All the CIF-related uses and planes are cleaned up.
 *
 * ----------------------------------------------------------------------------
 */

void
cifHierCleanup()
{
    int i;

    /* We can't afford for this clearing out to be interrupted, or
     * it could cause the next CIF to be generated wrong.
     */

    SigDisableInterrupts();

    DBCellClearDef(CIFTotalDef);
    DBCellClearDef(CIFComponentDef);
    for (i = 0; i < MAXCIFLAYERS; i++)
    {
	if (CIFTotalPlanes[i] != NULL)
	{
	    DBFreePaintPlane(CIFTotalPlanes[i]);
	    TiFreePlane(CIFTotalPlanes[i]);
	    CIFTotalPlanes[i] = NULL;
	}
	if (CIFComponentPlanes[i] != NULL)
	{
	    DBFreePaintPlane(CIFComponentPlanes[i]);
	    TiFreePlane(CIFComponentPlanes[i]);
	    CIFComponentPlanes[i] = NULL;
	}
    }

    SigEnableInterrupts();
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifHierCopyFunc --
 *
 * 	This procedure is called to copy paint from the database into
 *	flattened areas for CIF generation.  It's important to use
 *	this procedure rather than calling DBCellCopyAllPaint.  The
 *	reason is that DBCellCopyAllPaint clips the tiles to the
 *	edges of the search area.  When generating CIF for layers like
 *	contacts, the exact location of the edge of the tile is
 *	important.  Thus, this procedure always copies WHOLE tiles.
 *	Information will be clipped to the edge of the CIF generation
 *	area later.
 *
 * Results:
 *	Always returns 0 to keep the search alive.
 *
 * Side effects:
 *	The tile is copied into the definition indicated by the
 *	client data.
 *
 * ----------------------------------------------------------------------------
 */

int
cifHierCopyFunc(tile, cxp)
    Tile *tile;			/* Pointer to tile to copy. */
    TreeContext *cxp;		/* Describes context of search, including
				 * transform and client data.
				 */
{
    TileType type = TiGetTypeExact(tile);
    Rect sourceRect, targetRect;
    int pNum;
    CellDef *def = (CellDef *) cxp->tc_filter->tf_arg;
    int dinfo = 0;

    /* Ignore tiles in vendor GDS, unless this is specifically	*/
    /* overridden by the "see-vendor" option.			*/

    if (cxp->tc_scx->scx_use->cu_def->cd_flags & CDVENDORGDS)
    {
	if (!CIFCurStyle)
	    return 0;
	else if (!(CIFCurStyle->cs_flags & CWF_SEE_VENDOR))
	    return 0;
    }

    /* Ignore space tiles, since they won't do anything anyway. */

    if (IsSplit(tile))
    {
	dinfo = DBTransformDiagonal(type, &cxp->tc_scx->scx_trans);
	type = (SplitSide(tile)) ? SplitRightType(tile) :
		SplitLeftType(tile);
    }

    if (type == TT_SPACE) return 0;

    /* Get the rectangular area, and transform to final coords. */

    TiToRect(tile, &sourceRect);
    GeoTransRect(&cxp->tc_scx->scx_trans, &sourceRect, &targetRect);

    for (pNum = PL_SELECTBASE; pNum < DBNumPlanes; pNum++)
    {
	if (DBPaintOnPlane(type, pNum))
	{
	    DBNMPaintPlane(def->cd_planes[pNum], dinfo, &targetRect,
		DBStdPaintTbl(type, pNum), (PaintUndoInfo *) NULL);
	}
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifHierCellFunc --
 *
 * 	This procedure is invoked once for each subcell overlapping
 *	an interaction area.  It flattens the subcell and its children
 *	in the area of the overlap, generates CIF for that area, and
 *	saves it in cifHierPieces.
 *
 * Results:
 *	Always returns 0 to keep the search alive.
 *
 * Side effects:
 *	A new collection of CIF planes is added to cifHierPieces.
 *
 * ----------------------------------------------------------------------------
 */

int
cifHierCellFunc(scx)
    SearchContext *scx;		/* Describes cell and area in cell. */
{
    SearchContext newscx;
    Rect rootArea;

    /* Do not call this function on cells that are vendor GDS */
    /* (handled in cifHierCopyFunc())	*/
    /* if (scx->scx_use->cu_def->cd_flags & CDVENDORGDS) return 0; */

    /* In order to generate CIF safely in the interaction area, we
     * have to yank material in a larger area:  bloats and shrinks
     * may cause this material to affect CIF in the interaction area.
     * This may actually be over-conservative, but it's safe.  Think
     * carefully before trying to optimize!
     */

    DBCellClearDef(CIFComponentDef);
    newscx = *scx;
    GEO_EXPAND(&scx->scx_area, CIFCurStyle->cs_radius, &newscx.scx_area);
    (void) DBTreeSrTiles(&newscx, &CIFCurStyle->cs_yankLayers, 0,
	cifHierCopyFunc, (ClientData) CIFComponentDef);
    
    /* Set CIFErrorDef to NULL to ignore errors here... these will
     * get reported anyway when the cell is CIF'ed non-hierarchically,
     * so there's no point in making a second report here.
     */

    CIFErrorDef = (CellDef *) NULL;
    GeoTransRect(&scx->scx_trans, &scx->scx_area, &rootArea);
    CIFGen(CIFComponentDef, &rootArea, CIFComponentPlanes,
	    &CIFCurStyle->cs_hierLayers, FALSE, TRUE);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifHierErrorFunc --
 *
 * 	This function is invoked when the combined CIF in a parent
 *	is LESS than the individual CIFs of the children.  This means
 *	there are bogus rules in the CIF rule set.
 *
 * Results:
 *	Always returns 0 to keep the search alive.
 *
 * Side effects:
 *	Error information is output.
 *
 * ----------------------------------------------------------------------------
 */

int
cifHierErrorFunc(tile, checkArea)
    Tile *tile;			/* Tile that covers area it shouldn't. */
    Rect *checkArea;		/* Intersection of this and tile is error. */
{
    Rect area;

    TiToRect(tile, &area);

    /* Space in a diagonal tile is not an error if the corner containing
     * space bounds the checkArea.
     */
    if (IsSplit(tile))
	if (((area.r_xbot == checkArea->r_xbot) && !SplitSide(tile)) ||
		((area.r_xtop == checkArea->r_xtop) && SplitSide(tile)))
	    return 0;

    GeoClip(&area, checkArea);
    CIFError(&area, "parent and child disagree on CIF");
    return 0;
}


/*
 * ----------------------------------------------------------------------------
 *
 * cifHierCheckFunc --
 *
 * 	This function is invoked once for each CIF tile coming from
 *	a subcell piece.  It makes sure there are no space tiles over
 *	its area in "plane", then deletes everything from that area
 *	in "plane".
 *
 * Results:
 *	Always returns 0 to keep the search alive.
 *
 * Side effects:
 *	Error messages may be output.
 *
 * ----------------------------------------------------------------------------
 */

int
cifHierCheckFunc(tile, plane)
    Tile *tile;			/* Tile containing CIF. */
    Plane *plane;		/* Plane to check against and modify. */
{
    Rect area;

    TiToRect(tile, &area);

    if (IsSplit(tile))
    {
        DBSrPaintNMArea((Tile *)NULL, plane, TiGetTypeExact(tile),
		&area, &DBSpaceBits, cifHierErrorFunc, (ClientData) &area);

	DBNMPaintPlane(plane, TiGetTypeExact(tile), &area, CIFEraseTable,
		(PaintUndoInfo *) NULL);
    }
    else
    {
        DBSrPaintArea((Tile *) NULL, plane, &area,
		&DBSpaceBits, cifHierErrorFunc, (ClientData) &area);
        DBPaintPlane(plane, &area, CIFEraseTable, (PaintUndoInfo *) NULL);
    }

    CIFTileOps++;
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifHierPaintFunc --
 *
 * 	Called to transfer information from one CIF plane to another.
 *
 * Results:
 *	Always returns 0 to keep the search alive.
 *
 * Side effects:
 *	The area of tile is painted into plane.
 *
 * ----------------------------------------------------------------------------
 */

int
cifHierPaintFunc(tile, plane)
    Tile *tile;
    Plane *plane;		/* Plane in which to paint CIF over tile's
				 * area.
				 */
{
    Rect area;

    TiToRect(tile, &area);
    if (CIFCurStyle->cs_flags & CWF_GROW_SLIVERS) cifGrowSliver(tile, &area);
    if (IsSplit(tile))
	DBNMPaintPlane(plane, TiGetTypeExact(tile), &area, CIFPaintTable,
		(PaintUndoInfo *) NULL);
    else
	DBPaintPlane(plane, &area, CIFPaintTable, (PaintUndoInfo *) NULL);
    CIFTileOps += 1;
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifCheckAndErase --
 *
 * 	This utility procedure processes the hierarchical pieces
 *	in two ways.  First, it makes sure that all the CIF in
 *	CIFComponentPlanes is present in CIFTotalPlanes.  Second,
 *	it erases from CIFTotalPlanes any information that's in
 *	CIFComponentPlanes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The information in CIFTotalPlanes is reduced, and error messages
 *	may be output.
 *
 * ----------------------------------------------------------------------------
 */

void
cifCheckAndErase(style)
    CIFStyle *style;		/* Describes how to make CIF. */
{
    int i;

    /* Process all of the bits of CIF in CIFComponentPlanes. */
    
    for (i=0; i<style->cs_nLayers; i++)
    {
	CIFErrorLayer = i;
	if (CIFComponentPlanes[i] == NULL) continue;
	(void) DBSrPaintArea((Tile *) NULL, CIFComponentPlanes[i],
	    &TiPlaneRect, &CIFSolidBits, cifHierCheckFunc,
	    (ClientData) CIFTotalPlanes[i]);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFGenSubcells --
 *
 * 	This procedure computes all of the CIF that must be added to
 *	a given area to compensate for interactions between subcells.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The parameter "output" is modified (by OR'ing) to hold all
 *	the CIF that was generated for subcells.
 *
 * ----------------------------------------------------------------------------
 */

void
CIFGenSubcells(def, area, output)
    CellDef *def;		/* Parent cell for which CIF is computed. */
    Rect *area;			/* All CIF in this area is interesting. */
    Plane **output;		/* Array of pointers to planes into which
				 * the CIF output will be OR'ed (real CIF
				 * only).
				 */
{
    int stepSize, x, y, i, radius, oldTileOps, oldTileOps2;
    Rect totalArea, square, interaction;
    SearchContext scx;

    UndoDisable();
    CIFInitCells();
    radius = CIFCurStyle->cs_radius;
    stepSize = CIFCurStyle->cs_stepSize;
    if (stepSize <= 0)
    {
	stepSize = 20*radius;
	if (stepSize < 50) stepSize = 50;
    }
    CIFDummyUse->cu_def = def;
    scx.scx_use = CIFDummyUse;
    scx.scx_trans = GeoIdentityTransform;

    /* Any tile operations processed here get billed to hierarchy
     * in addition to being added to the total.
     */

    oldTileOps = CIFTileOps;

    /* Divide the area of the cell up into squares, and step through
     * the chunks.  In each chunk, the first thing to do is to find
     * out if there are any subcell interactions within one
     * radius of the square.
     */
    
    totalArea = *area;
    GeoClip(&totalArea, &def->cd_bbox);
    for (y = totalArea.r_ybot; y < totalArea.r_ytop; y += stepSize)
	for (x = totalArea.r_xbot; x < totalArea.r_xtop; x += stepSize)
	{
	    square.r_xbot = x;
	    square.r_ybot = y;
	    square.r_xtop = x + stepSize;
	    square.r_ytop = y + stepSize;
	    if (square.r_xtop > totalArea.r_xtop)
		square.r_xtop = totalArea.r_xtop;
	    if (square.r_ytop > totalArea.r_ytop)
		square.r_ytop = totalArea.r_ytop;
	    GEO_EXPAND(&square, radius, &square);
	    if (!DRCFindInteractions(def, &square, radius,
		&interaction)) continue;
	    
	    /* We've found an interaction.  Flatten it into CIFTotalUse, then
	     * make CIF from what's flattened.  Yank extra material to
	     * ensure that CIF is generated correctly.
	     */
	    
	    GEO_EXPAND(&interaction, CIFCurStyle->cs_radius, &scx.scx_area);
	    (void) DBTreeSrTiles(&scx, &CIFCurStyle->cs_yankLayers, 0,
		cifHierCopyFunc, (ClientData) CIFTotalDef);
	    CIFErrorDef = def;
	    CIFGen(CIFTotalDef, &interaction, CIFTotalPlanes,
		    &CIFCurStyle->cs_hierLayers, TRUE, TRUE);

	    /* Now go through all the subcells overlapping the area
	     * and generate CIF for each subcell individually.  OR this
	     * combined CIF together into CIFComponentPlanes.
	     */
	    
	    scx.scx_area = interaction;
	    (void) DBCellSrArea(&scx, cifHierCellFunc, (ClientData) NULL);
	    
	    /* Also generate CIF for the paint in the parent alone.  Ignore
	     * CIF generation errors here, since they will already have been
	     * recorded when the parent was CIF'ed by itself.
	     */

	    CIFErrorDef = (CellDef *) NULL;
	    CIFGen(def, &interaction, CIFComponentPlanes,
		    &CIFCurStyle->cs_hierLayers, FALSE, TRUE);

	    /* Make sure everything in the pieces is also in the overall
	     * CIF, then erase the piece stuff from the overall, and
	     * throw away the pieces.
	     */
	    
	    CIFErrorDef = def;
	    cifCheckAndErase(CIFCurStyle);

	    /* Lastly, paint everything from the pieces into the result
	     * planes.
	     */
	    
	    oldTileOps2 = CIFTileOps;
	    for (i=0; i<CIFCurStyle->cs_nLayers; i++)
	    {
		CurCifLayer = CIFCurStyle->cs_layers[i]; /* for growSliver */
		(void) DBSrPaintArea((Tile *) NULL, CIFTotalPlanes[i],
		    &TiPlaneRect, &CIFSolidBits, cifHierPaintFunc,
		    (ClientData) output[i]);
	    }
	    CIFHierRects += CIFTileOps - oldTileOps2;

	    cifHierCleanup();
	}
    
    CIFHierTileOps += CIFTileOps - oldTileOps;
    UndoEnable();
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifHierElementFunc --
 *
 * 	This function is called once for each time an array element
 *	overlaps one of the four areas A, B, C, or D in cifHierArrayFunc
 *	(see below).  Its job is to yank the relevant piece of this
 *	cell into CIFTotalDef, and also to yank the same piece into
 *	CIFComponentDef and generate a piece of CIF from it.  The CIF
 *	from the piece is OR'ed into CIFComponentPlanes for later comparison
 *	with the parent.
 *
 * Results:
 *	Always returns 0 to keep the search alive.
 *
 * Side effects:
 *	The cell CIFTotalUse is modified, as is CIFComponentUse and
 *	CIFComponentPlanes.
 *
 * ----------------------------------------------------------------------------
 */

    /* ARGSUSED */
int
cifHierElementFunc(use, transform, x, y, checkArea)
    CellUse *use;			/* CellUse being array-checked. */
    Transform *transform;		/* Transform from this instance to
					 * the parent.
					 */
    int x, y;				/* Indices of this instance. */
    Rect *checkArea;			/* Area (in parent coords) to be
					 * CIF-generated.
					 */
{
    Rect defArea;
    Transform tinv;
    SearchContext scx;

    GeoInvertTrans(transform, &tinv);
    GeoTransRect(&tinv, checkArea, &defArea);

    /* Yank extra material to ensure that CIF is generated correctly. */

    GEO_EXPAND(&defArea, CIFCurStyle->cs_radius, &scx.scx_area);
    scx.scx_trans = *transform;
    scx.scx_use = use;
    (void) DBTreeSrTiles(&scx, &CIFCurStyle->cs_yankLayers, 0,
	cifHierCopyFunc, (ClientData) CIFTotalDef);

    DBCellClearDef(CIFComponentDef);
    (void) DBTreeSrTiles(&scx, &CIFCurStyle->cs_yankLayers, 0,
	cifHierCopyFunc, (ClientData) CIFComponentDef);

    CIFErrorDef = (CellDef *) NULL;
    CIFGen(CIFComponentDef, checkArea, CIFComponentPlanes,
	    &CIFCurStyle->cs_hierLayers, FALSE, TRUE);

    return 0;
}


/*
 * ---------------------------------------------------------------------------
 * cifGrowSliver() --
 *
 * This function is passed the address of a "sliver" rectangle.  It then
 * grows the shortest dimension of the sliver so that it is at least minimum 
 * width.  The minimum width is found through the variable CurCifLayer which
 * identifies the current layer being generated.  A "minwidth" command has been
 * added to the cifoutput section for which the minimum width of each layer
 * can be specified.
 *
 * Results:
 *	Always return 0
 *
 * Side effects:
 *	Returns the modified rectangle in "area" (pointer)
 * ---------------------------------------------------------------------------
 */

int
cifGrowSliver(tile, area)
    Tile *tile;
    Rect *area;
{
    int height, width, expand_up, expand_side;

    TiToRect(tile, area);
    expand_up = (TiGetType(BL(tile)) != TT_SPACE) ||
		(TiGetType(TR(tile)) != TT_SPACE);
    expand_side = (TiGetType(LB(tile)) != TT_SPACE) ||
		(TiGetType(RT(tile)) != TT_SPACE);
    if (CurCifLayer->min_width == 0)
	return 0;

    height = area->r_ytop - area->r_ybot;
    width = area->r_xtop - area->r_xbot;
    printf("got sliver %d %d %d %d [%d,%d]\n",
	area->r_xtop, area->r_xbot, area->r_ytop,
	area->r_ybot, expand_up,expand_side);

    if ((height < width) || expand_up)
    {
	if (height >= CurCifLayer->min_width) return 0;
	area->r_ytop += (CurCifLayer->min_width - height)/2;
	area->r_ybot -= (CurCifLayer->min_width - height)/2;
    }
    if ((width < height) || expand_side)
    {
	if(width >= CurCifLayer->min_width) return 0;
        area->r_xtop += (CurCifLayer->min_width - width)/2;
        area->r_xbot -= (CurCifLayer->min_width - width)/2;
    }
    printf("created sliver %d %d %d %d \n",
	area->r_xtop, area->r_xbot, area->r_ytop, area->r_ybot);

    return 0;
}


/*
 * ----------------------------------------------------------------------------
 *
 * cifHierPaintArrayFunc --
 *
 * 	This function is used to paint in interaction tiles from an
 *	array.  It is called once for each tile to be copied, and
 *	paints the tile into cifHierCurPlane.  Then the tile is
 *	offset by cifHierXSpacing and cifHierYSpacing, and copied
 *	again and again, cifHierCount times in all.  The caller must
 *	set up these shared variables.
 *
 * Results:
 *	Always returns 0 to keep the search alive.
 *
 * Side effects:
 *	The plane pointed to by cifHierCurPlane gets modified.
 *
 * ----------------------------------------------------------------------------
 */

int
cifHierPaintArrayFunc(tile)
    Tile *tile;
{
    Rect area;
    int i, j, xbot, xtop;

    TiToRect(tile, &area);
    if (CIFCurStyle->cs_flags & CWF_GROW_SLIVERS) cifGrowSliver(tile, &area);
    xbot = area.r_xbot;
    xtop = area.r_xtop;
    for (i=0; i<cifHierYCount; i++)
    {
	for (j=0; j<cifHierXCount; j++)
	{
	    DBPaintPlane(cifHierCurPlane, &area, CIFPaintTable,
		(PaintUndoInfo *) NULL);
	    CIFTileOps += 1;
	    area.r_xbot += cifHierXSpacing;
	    area.r_xtop += cifHierXSpacing;
	}
	area.r_xbot = xbot;
	area.r_xtop = xtop;
	area.r_ybot += cifHierYSpacing;
	area.r_ytop += cifHierYSpacing;
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifHierArrayFunc --
 *
 * 	This procedure is called once for each array that we
 *	are to generate interaction CIF for.  It computes all
 *	the CIF that must be present in the parent to compensate
 *	for the interactions between array elements.
 *
 * Results:
 *	Always returns 2, to skip the remaining instances in the
 *	current array.
 *
 * Side effects:
 *	CIF is added to the set of planes indicated by output.
 *
 * Design:
 *	This procedure is something like a cross between drcArrayFunc
 *	and DRCGenSubcells.  It computes interaction CIF in the four
 *	areas of the array shown below, then replicates	it over all
 *	the other elements of the array.
 *
 *	------------------------------CCCCC--------------
 *	|               |             CCCCC             |
 *	|               |             CCCCC             |
 *	|               |             CCCCC             |
 *	|               |             CCCCCDDDDDDDDDDDDDD
 *	------------------------------CCCCCDDDDDDDDDDDDDD
 *	|               |             CCCCCDDDDDDDDDDDDDD
 *	|               |               |               |
 *	|               |               |               |
 *	AAAAAAAAAAAAAAAAAAA             |               |
 *	AAAAAAAAAAAAAAAAAAA------------------------------
 *	AAAAAAAAAAAAAAAAAAA             |               |
 *	|             BBBBB             |               |
 *	|             BBBBB             |               |
 *	|             BBBBB             |               |
 *	--------------BBBBB------------------------------
 * ----------------------------------------------------------------------------
 */

int
cifHierArrayFunc(scx, output)
    SearchContext *scx;		/* Information about the search. */
    Plane **output;		/* Array of planes to hold results. */
{
    Rect childArea, parentArea, A, B, C, D, expandedArea;
    CellUse *use;
    int radius, xsep, ysep, xsize, ysize, nx, ny, i, oldTileOps;
    int xhi, yhi;
    bool anyInteractions = FALSE;

    use = scx->scx_use;
    radius = CIFCurStyle->cs_radius;
    if ((use->cu_xlo == use->cu_xhi) && (use->cu_ylo == use->cu_yhi))
	return 2;
    
    /* We only want interactions between neighboring cells, so reduce	*/
    /* the array size to at most 2x2, process, then restore the		*/
    /* original array count.						*/

    xhi = use->cu_xhi;
    yhi = use->cu_yhi;

    if (use->cu_xlo != use->cu_xhi)
	use->cu_xhi = use->cu_xlo + ((use->cu_xlo < use->cu_xhi) ? 1 : -1);
    if (use->cu_ylo != use->cu_yhi)
	use->cu_yhi = use->cu_ylo + ((use->cu_ylo < use->cu_yhi) ? 1 : -1);

    /* Compute the sizes and separations of elements, in coordinates
     * of the parent.  If the array is 1-dimensional, we set the
     * corresponding spacing to an impossibly large distance.
     */
    
    childArea.r_xbot = 0;
    childArea.r_ybot = 0;

    /* Ensure that array width or height 1 corresponds to a separation	*/
    /* large enough not to satisfy any conditions A, B, C, or D.	*/

    if (use->cu_xlo == use->cu_xhi)
	childArea.r_xtop = radius +
		(use->cu_def->cd_bbox.r_xtop - use->cu_def->cd_bbox.r_xbot);
    else childArea.r_xtop = use->cu_xsep;
    if (use->cu_ylo == use->cu_yhi)
	childArea.r_ytop = radius +
		(use->cu_def->cd_bbox.r_ytop - use->cu_def->cd_bbox.r_ybot);

    else childArea.r_ytop = use->cu_ysep;
    GeoTransRect(&use->cu_transform, &childArea, &parentArea);
    xsep = parentArea.r_xtop - parentArea.r_xbot;
    ysep = parentArea.r_ytop - parentArea.r_ybot;
    GeoTransRect(&use->cu_transform, &use->cu_def->cd_bbox, &parentArea);
    xsize = parentArea.r_xtop - parentArea.r_xbot;
    ysize = parentArea.r_ytop - parentArea.r_ybot;
    nx = (use->cu_bbox.r_xtop - use->cu_bbox.r_xbot - xsize)/xsep;
    nx += 1;
    ny = (use->cu_bbox.r_ytop - use->cu_bbox.r_ybot - ysize)/ysep;
    ny += 1;

    /* Process each of the four areas A, B, C, and D.  For each one,
     * do three things:  yank the relevant material into cell
     * __CIF__, and generate a piece of CIF corresponding to each
     * child area contributing to the overlap.  This is all handled
     * by the procedure cifHierElementFunc.  Then generate the CIF
     * for the combined information that was yanked into __CIF__.
     * Collect all of the combined CIF from all four areas together
     * into CIFTotalPlanes.  Note:  in each case we have to yank a larger
     * area than we check, in order to include material that will be
     * bloated or shrunk.
     */

    /* A */

    if (ysep < ysize + radius)
    {
	A.r_xbot = use->cu_bbox.r_xbot - radius;
	A.r_xtop = use->cu_bbox.r_xbot + xsize + radius;
	A.r_ybot = use->cu_bbox.r_ybot + ysep - radius;
	A.r_ytop = use->cu_bbox.r_ybot + ysize + radius;
	GEO_EXPAND(&A, CIFCurStyle->cs_radius, &expandedArea);
	(void) DBArraySr(use, &expandedArea, cifHierElementFunc,
		(ClientData) &A);
	CIFErrorDef = use->cu_parent;
	CIFGen(CIFTotalDef, &A, CIFTotalPlanes, &CIFCurStyle->cs_hierLayers,
		FALSE, TRUE);
	anyInteractions = TRUE;
    }

    /* C */

    if (xsep < xsize + radius)
    {
	C.r_xbot = use->cu_bbox.r_xtop - xsize - radius;
	C.r_xtop = use->cu_bbox.r_xtop - xsep + radius;
	C.r_ybot = use->cu_bbox.r_ytop - ysize - radius;
	C.r_ytop = use->cu_bbox.r_ytop + radius;
	GEO_EXPAND(&C, CIFCurStyle->cs_radius, &expandedArea);
	(void) DBArraySr(use, &expandedArea, cifHierElementFunc,
		(ClientData) &C);
	CIFErrorDef = use->cu_parent;
	CIFGen(CIFTotalDef, &C, CIFTotalPlanes, &CIFCurStyle->cs_hierLayers,
		FALSE, TRUE);
	anyInteractions = TRUE;
    }

    if ((xsep < xsize + radius) && (ysep < ysize + radius))
    {
	/* B */

	B.r_xbot = use->cu_bbox.r_xbot + xsep - radius;
	B.r_xtop = use->cu_bbox.r_xbot + xsize + radius;
	B.r_ybot = use->cu_bbox.r_ybot - radius;
	B.r_ytop = use->cu_bbox.r_ybot + ysep - radius;
	GEO_EXPAND(&B, CIFCurStyle->cs_radius, &expandedArea);
	(void) DBArraySr(use, &expandedArea, cifHierElementFunc,
		(ClientData) &B);
	CIFErrorDef = use->cu_parent;
	CIFGen(CIFTotalDef, &B, CIFTotalPlanes, &CIFCurStyle->cs_hierLayers,
		FALSE, TRUE);
	
	/* D */

	D.r_xbot = use->cu_bbox.r_xtop - xsep + radius;
	D.r_xtop = use->cu_bbox.r_xtop + radius;
	D.r_ybot = use->cu_bbox.r_ytop - ysize - radius;
	D.r_ytop = use->cu_bbox.r_ytop - ysep + radius;
	GEO_EXPAND(&D, CIFCurStyle->cs_radius, &expandedArea);
	(void) DBArraySr(use, &expandedArea, cifHierElementFunc,
		(ClientData) &D);
	CIFErrorDef = use->cu_parent;
	CIFGen(CIFTotalDef, &D, CIFTotalPlanes, &CIFCurStyle->cs_hierLayers,
		FALSE, TRUE);
    }

    if (anyInteractions)
    {

	/* Remove redundant CIF that's already in the children, and
	 * make sure everything in the kids is in the parent too.
	 */
    
	CIFErrorDef = use->cu_parent;
	cifCheckAndErase(CIFCurStyle);

	/* Lastly, paint everything back from our local planes into
	 * the planes of the caller.  In doing this, stuff has to
	 * be replicated many times over to cover each of the array
	 * interaction areas.
	 */
    
	oldTileOps = CIFTileOps;
	for (i=0; i<CIFCurStyle->cs_nLayers; i++)
	{
	    int scale = CIFCurStyle->cs_scaleFactor;
	    Rect cifArea;

	    cifHierCurPlane = output[i];
	    CurCifLayer = CIFCurStyle->cs_layers[i]; /* for growSliver */

	    /* The left edge of the array (from A). */

	    if ((ny > 1) && (ysep < ysize + radius))
	    {
		cifHierYSpacing = ysep * scale;
		cifHierXSpacing = 0;
		cifHierXCount = 1;
		cifHierYCount = ny-1;
		SCALE(&A, scale, &cifArea);
		(void) DBSrPaintArea((Tile *) NULL, CIFTotalPlanes[i],
			&cifArea, &CIFSolidBits, cifHierPaintArrayFunc,
			(ClientData) NULL);
	    }

	    /* The top edge of the array (from C). */

	    if ((nx > 1) && (xsep < xsize + radius))
	    {
		cifHierXSpacing = -xsep * scale;
		cifHierYSpacing = 0;
		cifHierXCount = nx-1;
		cifHierYCount = 1;
		SCALE(&C, scale, &cifArea);
		(void) DBSrPaintArea((Tile *) NULL, CIFTotalPlanes[i],
			&cifArea, &CIFSolidBits, cifHierPaintArrayFunc,
			(ClientData) NULL);
	    }

	    if ((nx > 1) && (ny > 1) && (xsep < xsize + radius)
	    		&& (ysep < ysize + radius))
	    {
		/* The bottom edge of the array (from B). */

		cifHierXSpacing = xsep * scale;
		cifHierYSpacing = 0;
		cifHierXCount = nx-1;
		cifHierYCount = 1;
		SCALE(&B, scale, &cifArea);
		(void) DBSrPaintArea((Tile *) NULL, CIFTotalPlanes[i],
			&cifArea, &CIFSolidBits, cifHierPaintArrayFunc,
			(ClientData) NULL);
	    
		/* The right edge of the array (from D). */

		cifHierXSpacing = 0;
		cifHierYSpacing = -ysep * scale;
		cifHierXCount = 1;
		cifHierYCount = ny-1;
		SCALE(&D, scale, &cifArea);
		(void) DBSrPaintArea((Tile *) NULL, CIFTotalPlanes[i],
			&cifArea, &CIFSolidBits, cifHierPaintArrayFunc,
			(ClientData) NULL);
	    
		/* The core of the array (from A and B).  This code is a bit
		 * tricky in order to work correctly even for arrays where
		 * radius < ysep.  The "if" statement handles this case.
		 */

		cifHierXSpacing = xsep * scale;
		cifHierYSpacing = ysep * scale;
		cifHierXCount = nx-1;
		cifHierYCount = ny-1;
		parentArea.r_xbot = A.r_xtop - xsep;
		parentArea.r_ybot = A.r_ytop - ysep;
		if (parentArea.r_ybot > B.r_ytop) parentArea.r_ybot = B.r_ytop;
		parentArea.r_xtop = A.r_xtop;
		parentArea.r_ytop = A.r_ytop;
		SCALE(&parentArea, scale, &cifArea);
		(void) DBSrPaintArea((Tile *) NULL, CIFTotalPlanes[i],
			&cifArea, &CIFSolidBits, cifHierPaintArrayFunc,
			(ClientData) NULL);
	    }
	}
	CIFHierRects += CIFTileOps - oldTileOps;
    }

    cifHierCleanup();

    /* Restore the array bounds of the array */
    use->cu_xhi = xhi;
    use->cu_yhi = yhi;

    return 2;
}


/*
 * ----------------------------------------------------------------------------
 *
 * CIFGenArrays --
 *
 * 	This procedure computes all of the CIF that must be added to
 *	a given area of a parent to compensate for interactions between
 *	elements of arrays in that area.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The parameter output is modified (by OR'ing) to hold all
 *	the CIF that was generated for array interactions.
 *
 * ----------------------------------------------------------------------------
 */

void
CIFGenArrays(def, area, output)
    CellDef *def;		/* Parent cell for which CIF is computed. */
    Rect *area;			/* All CIF in this area is interesting. */
    Plane **output;		/* Array of pointers to planes into which
				 * the CIF output will be OR'ed (real CIF
				 * only, temp layers won't appear).  If
				 * output is NULL, then CIF is stored in
				 * CIFPlanes, and the planes are initially
				 * cleared.
				 */
{
    SearchContext scx;
    int i, oldTileOps;

    UndoDisable();
    CIFInitCells();

    /* Bill all tile operations here to hierarchical processing in
     * addition to adding to the total.
     */
    
    oldTileOps = CIFTileOps;

    if (output == NULL)
    {
	output = CIFPlanes;
	for (i=0; i<CIFCurStyle->cs_nLayers; i++)
	{
	    if (output[i] == NULL)
		output[i] = DBNewPlane((ClientData) TT_SPACE);
	    else DBClearPaintPlane(output[i]);
	}
    }

    scx.scx_area = *area;
    scx.scx_use = CIFDummyUse;
    CIFDummyUse->cu_def = def;
    (void) DBCellSrArea(&scx, cifHierArrayFunc, (ClientData) output);

    CIFHierTileOps += CIFTileOps - oldTileOps;
    UndoEnable();
}
