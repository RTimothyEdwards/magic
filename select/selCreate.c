/* selCreate.c -
 *
 *	This file provides routines to make selections by copying
 *	things into a special cell named "__SELECT__".
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/select/selCreate.c,v 1.10 2010/06/24 12:37:56 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/malloc.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/undo.h"
#include "commands/commands.h"
#include "select/selInt.h"
#include "select/select.h"
#include "drc/drc.h"
#include "utils/main.h"
#include "utils/signals.h"

/* Two cells worth of information are kept around by the selection
 * module.  SelectDef and SelectUse are for the cells whose contents
 * are the current selection.  Select2Def and Select2Use provide a
 * temporary working space for procedures that manipulate the selection.
 * for example, Select2Def is used to hold nets or regions while they
 * are being extracted by SelectRegion or SelectNet.  Once completely
 * extracted, information is copied to SelectDef.  Changes to
 * SelectDef are undo-able and redo-able (so that the undo package
 * can deal with selection changes), but changes to Select2Def are
 * not undo-able (undoing is always disabled when the cell is modified).
 */

global CellDef *SelectDef, *Select2Def;
global CellUse *SelectUse, *Select2Use;

/* The CellDef below points to the definition FROM which the selection
 * is extracted.  This is the root definition of a window.  Everything
 * in the selection must have come from the same place, so we clear the
 * selection whenever the user tries to select from a new hierarchy.
 */

CellDef *SelectRootDef = NULL;

/* The CellUse below is the last use selected by SelectUse.  It is
 * kept around to support the "replace" feature of SelectUse.
 *
 * Procedures which deselect a cell must reset this to null if they
 * happen to deselect this usage.  (Danger Will Robinson)
 */

global CellUse *selectLastUse = NULL;


/*
 * ----------------------------------------------------------------------------
 *
 * SelectInit --
 *
 * 	This procedure initializes the selection code by creating
 *	the selection cells.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The select cells are created if they don't already exist.
 *	Selection undo-ing is also initialized.
 *
 * ----------------------------------------------------------------------------
 */

void
SelectInit()
{
    static bool initialized = FALSE;

    if (initialized) return;
    else initialized = TRUE;

    /* Create the working cells used internally to this module to
     * hold selected information.  Don't allow any of this to be
     * undone, or else it could invalidate all the pointers we
     * keep around to the cells.
     */

    UndoDisable();
    SelectDef = DBCellLookDef("__SELECT__");
    if (SelectDef == (CellDef *) NULL)
    {
	SelectDef = DBCellNewDef("__SELECT__",(char *) NULL);
	ASSERT(SelectDef != (CellDef *) NULL, "SelectInit");
	DBCellSetAvail(SelectDef);
	SelectDef->cd_flags |= CDINTERNAL;
	TTMaskZero(&SelectDef->cd_types);
    }
    SelectUse = DBCellNewUse(SelectDef, (char *) NULL);
    DBSetTrans(SelectUse, &GeoIdentityTransform);
    SelectUse->cu_expandMask = CU_DESCEND_SPECIAL;	/* This is always expanded. */
    SelectUse->cu_flags = 0;	/* never locked down */

    Select2Def = DBCellLookDef("__SELECT2__");
    if (Select2Def == (CellDef *) NULL)
    {
	Select2Def = DBCellNewDef("__SELECT2__",(char *) NULL);
	ASSERT(Select2Def != (CellDef *) NULL, "SelectInit");
	DBCellSetAvail(Select2Def);
	Select2Def->cd_flags |= CDINTERNAL;
    }
    Select2Use = DBCellNewUse(Select2Def, (char *) NULL);
    DBSetTrans(Select2Use, &GeoIdentityTransform);
    Select2Use->cu_expandMask = CU_DESCEND_SPECIAL;	/* This is always expanded. */
    Select2Use->cu_flags = 0;	/* never locked down */
    UndoEnable();

    SelUndoInit();
}

/*
 * ----------------------------------------------------------------------------
 *
 * SelectClear --
 *
 * 	This procedure clears the current selection.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All information is removed from the select cell, and selection
 *	information is also taken off the screen.
 *
 * ----------------------------------------------------------------------------
 */

/* The variables below are used to record information about subcells that
 * must be cleared from the select cell.
 */

#define MAXUSES 30
static CellUse *(selDeleteUses[MAXUSES]);
static int selNDelete;

void
SelectClear()
{
    SearchContext scx;
    Rect r, expand;
    extern int selClearFunc();		/* Forward declaration. */

    if (SelectRootDef == NULL) return;

    scx.scx_area = SelectDef->cd_bbox;
    expand = scx.scx_area;

    /* Special handling when the cell is flagged as a net selection */

    if (SelectUse->cu_flags & CU_SELECT_NET)
    {
	SelNetRememberForUndo((CellDef *)NULL, (Point *)NULL,
			TT_SPACE, FALSE, FALSE);
	SelectUse->cu_flags = 0;
	DBCellClearDef(SelectDef);
    }
    else
    {
	SelRememberForUndo(TRUE, (CellDef *) NULL, (Rect *) NULL);
	SelectUse->cu_flags = 0;

	/* Erase all the paint from the select cell. */
	DBEraseMask(SelectDef, &TiPlaneRect, &DBAllButSpaceBits);

	/* For the line below to work, paint tables need to be set up correctly! */
	/* DBErase(SelectDef, &TiPlaneRect, TT_SPACE); */

	/* Erase all of the labels from the select cell. */

	(void) DBEraseLabel(SelectDef, &TiPlaneRect, &DBAllTypeBits, &expand);

	/* Erase all of the subcells from the select cell.  This is a bit tricky,
	 * because we can't erase the subcells while searching for them (it will
	 * cause problems for the database).  The code below first grabs up a
	 * few subcells, then deletes them, then grabs up a few more, then deletes
	 * them, and so on until done.
	 */

	scx.scx_use = SelectUse;
	scx.scx_trans = GeoIdentityTransform;
	while (TRUE)
	{
	    int i;

	    selNDelete = 0;
	    (void) DBCellSrArea(&scx, selClearFunc, (ClientData) NULL);
	    for (i = 0; i < selNDelete; i += 1)
	    {
		DBUnLinkCell(selDeleteUses[i], SelectDef);
		DBDeleteCell(selDeleteUses[i]);
		(void) DBCellDeleteUse(selDeleteUses[i]);
	    }
	    if (selNDelete < MAXUSES) break;
	}
	selectLastUse = NULL;

	SelRememberForUndo(FALSE, SelectRootDef, &scx.scx_area);
    }

    TTMaskZero(&SelectDef->cd_types);

    /* Reset the transform, if we have been moving the selection around */
    GeoTransRect(&SelectUse->cu_transform, &expand, &r);
    SelectUse->cu_transform = GeoIdentityTransform;

    /* Erase the selection from the screen. */
    DBWHLRedraw(SelectRootDef, &r, TRUE);

    DBReComputeBbox(SelectDef);
    DBWAreaChanged(SelectDef, &expand, DBW_ALLWINDOWS,
	(TileTypeBitMask *) NULL);
}

/* Search function to help clear subcells from the selection.  It just
 * records information about several subcells (up to MAXUSES).
 */

int
selClearFunc(scx)
    SearchContext *scx;		/* Describes a cell that was found. */
{
    selDeleteUses[selNDelete] = scx->scx_use;
    selNDelete += 1;
    if (selNDelete == MAXUSES) return 1;
    else return 2;
}


/*
 * ----------------------------------------------------------------------------
 *
 * SelectArea --
 *
 * 	This procedure selects all information of given types that
 *	falls in a given area.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The indicated information is added to the select cell, and
 *	outlined on the screen.  Only information of particular
 *	types, and in expanded cells (according to xMask) is
 *	selected.
 *
 * ----------------------------------------------------------------------------
 */

void
SelectArea(scx, types, xMask)
    SearchContext *scx;		/* Describes the area in which material
				 * is to be selected.  The resulting
				 * coordinates should map to the coordinates
				 * of EditRootDef.  The cell use should be
				 * the root of a window.
				 */
    TileTypeBitMask *types;	/* Indicates which layers to select.  Can
				 * include L_CELL and L_LABELS to select
				 * labels and unexpanded subcells.  If L_LABELS
				 * is specified then all labels touching the
				 * area are selected.  If L_LABELS isn't
				 * specified, then only labels attached to
				 * selected material are selected.
				 */
    int xMask;			/* Indicates window (or windows) where cells
				 * must be expanded for their contents to be
				 * considered.  0 means treat everything as
				 * expanded.
				 */
{
    Rect labelArea, cellArea;

    /* If the source definition is changing, clear the old selection. */

    if (SelectRootDef != scx->scx_use->cu_def)
    {
	if (SelectRootDef != NULL)
	    SelectClear();
	SelectRootDef = scx->scx_use->cu_def;
	SelSetDisplay(SelectUse, SelectRootDef);
    }

    SelRememberForUndo(TRUE, (CellDef *) NULL, (Rect *) NULL);

    /* Select paint. */

    (void) DBCellCopyAllPaint(scx, types, xMask, SelectUse);

    SelectDef->cd_types = *types;	/* Remember what types were requested */

    /* Select labels. */

    if (TTMaskHasType(types, L_LABEL))
        (void) DBCellCopyAllLabels(scx, &DBAllTypeBits, xMask,
		SelectUse, &labelArea);
    else (void) DBCellCopyAllLabels(scx, types, xMask, SelectUse, &labelArea);

    /* Select unexpanded cell uses. */

    if (TTMaskHasType(types, L_CELL))
        (void) DBCellCopyAllCells(scx, xMask, SelectUse, &cellArea);
    else
    {
	cellArea.r_xbot = 0;
	cellArea.r_xtop = -1;
    }

    /* Display the new selection. */

    (void) GeoIncludeAll(&scx->scx_area, &labelArea);
    (void) GeoIncludeAll(&cellArea, &labelArea);
    SelRememberForUndo(FALSE, SelectRootDef, &labelArea);
    DBReComputeBbox(SelectDef);
    DBWHLRedraw(SelectRootDef, &labelArea, TRUE);
    DBWAreaChanged(SelectDef, &SelectDef->cd_extended, DBW_ALLWINDOWS,
	&DBAllButSpaceBits);
}


/*
 * ----------------------------------------------------------------------------
 *
 * selFindChunk --
 *
 * 	This is a recursive procedure to find the largest chunk of
 *	material in a particular area.  It locates a rectangular
 *	area of given materials whose minimum dimension is as
 *	large as possible, and whose maximum dimension is also as
 *	large as possible (but minimum dimension is more important).
 *	Furthermore, the chunk must lie within a particular area and
 *	must contain a given area.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

#define MAX_RECURSION_LEVEL 22

void
selFindChunk(plane, wrongTypes, searchArea, containedArea, bestMin,
	bestMax, bestChunk, level)
    Plane *plane;			/* Plane on which to hunt for chunk. */
    TileTypeBitMask *wrongTypes;	/* Types that are not allowed to be
					 * part of the chunk.
					 */
    Rect *searchArea;			/* Largest allowable size for the
					 * chunk.  Note:  don't overestimate
					 * this or the procedure will take a
					 * long time!  (it processes every
					 * tile in this area).
					 */
    Rect *containedArea;		/* The chunk returned must contain
					 * this entire area.
					 */
    int *bestMin;			/* Largest minimum dimension seen so
					 * far: skip any chunks that can't
					 * match this.  Updated by this
					 * procedure.
					 */
    int *bestMax;			/* Largest maximum dimension seen
					 * so far.
					 */
    Rect *bestChunk;			/* Filled in with largest chunk seen
					 * so far, if we find one better than
					 * bestMin and bestMax.
					 */
    int level;				/* Recursion level (to avoid worst-
					 * case scenarios)
					 */
{
    Rect smaller;
    int min, max;
    extern int selChunkFunc();
    Rect wrong;

    if (level == MAX_RECURSION_LEVEL) return;

    /* If the search area is already smaller than the chunk to beat,
     * there's no point in even examining this chunk.
     */

    min = searchArea->r_xtop - searchArea->r_xbot;
    max = searchArea->r_ytop - searchArea->r_ybot;
    if (min > max)
    {
	int tmp;
	tmp = min; min = max; max = tmp;
    }

    if (min < *bestMin) return;
    if ((min == *bestMin) && (max <= *bestMax)) return;

    /* At each stage, search the area that's left for material of the
     * wrong type.
     */

    if (DBSrPaintArea((Tile *) NULL, plane, searchArea, wrongTypes,
	    selChunkFunc, (ClientData) &wrong) == 0)
    {
	/* The area contains nothing but material of the right type,
	 * so it is now the "chunk to beat".
	 */

	*bestMin = min;
	*bestMax = max;
	*bestChunk = *searchArea;
	return;
    }

    if (SigInterruptPending)
	return;

    /* At this point the current search area contains some material of
     * the wrong type.  We have to reduce the search area to exclude this
     * material.  There are two ways that this can be done while still
     * producing areas that contain containedArea.  Try both of those,
     * and repeat the whole thing recursively on the smaller areas.
     */

    /* First, try reducing the x-range. */

    smaller = *searchArea;
    if (wrong.r_xbot >= containedArea->r_xtop)
	smaller.r_xtop = wrong.r_xbot;
    else if (wrong.r_xtop <= containedArea->r_xbot)
	smaller.r_xbot = wrong.r_xtop;
    else goto tryY;  /* Bad material overlaps containedArea in x. */
    selFindChunk(plane, wrongTypes, &smaller, containedArea,
	    bestMin, bestMax, bestChunk, level + 1);
    

    /* Also try reducing the y-range to see if that works better. */

    tryY: smaller = *searchArea;
    if (wrong.r_ybot >= containedArea->r_ytop)
	smaller.r_ytop = wrong.r_ybot;
    else if (wrong.r_ytop <= containedArea->r_ybot)
	smaller.r_ybot = wrong.r_ytop;
    else return;  /* Bad material overlaps containedArea in y. */
    selFindChunk(plane, wrongTypes, &smaller, containedArea,
	    bestMin, bestMax, bestChunk, level + 1);
}

/* Search function to find split tiles in the area of interest.
 */

int
selSplitFunc(tile, cxp)
   Tile *tile;
   TreeContext *cxp;
{
    SearchContext *scx = cxp->tc_scx;
    Rect *area = (Rect *) cxp->tc_filter->tf_arg;
    Rect locarea;

    if (IsSplit(tile))
    {
        TiToRect(tile, &locarea);  
	GeoTransRect(&scx->scx_trans, &locarea, area);
        return 1;
    }
    return 0;
}

/* This procedure is called for each tile of the wrong type in an
 * area that is supposed to contain only tiles of other types.  It
 * just returns the area of the wrong material and aborts the search.
 */

int
selChunkFunc(tile, wrong)
    Tile *tile;			/* The offending tile. */
    Rect *wrong;		/* Place to store the tile's area. */
{
    TiToRect(tile, wrong);
    return 1;			/* Abort the search. */
}


/*
 * ----------------------------------------------------------------------------
 *
 * SelectChunk --
 *
 * 	This procedure selects a single rectangular chunk of 
 *	homogeneous material.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	More material is added to the select cell and displayed
 *	on the screen.  This procedure finds the largest rectangular
 *	chunk of material "type" that contains the area given in
 *	in scx.  The material need not all be in one cell, but it
 *	must all be in cells that are expanded according to "xMask".
 *	If pArea is given, the rectangle it points to is filled in
 *	with the area of the chunk that was selected.
 *
 * ----------------------------------------------------------------------------
 */

void
SelectChunk(scx, type, xMask, pArea, less)
    SearchContext *scx;		/* Area to tree-search for material.  The
				 * transform must map to root coordinates
				 * of the edit cell.
				 */
    TileType type;		/* The type of material to be considered. */
    int xMask;			/* Indicates window (or windows) where cells
				 * must be expanded for their contents to be
				 * considered.  0 means treat everything as
				 * expanded.
				 */
    Rect *pArea;		/* If non-NULL, gets filled in with the area
				 * of the selection.
				 */
    bool less;
{
#define INITIALSIZE 10
    SearchContext newscx;
    TileTypeBitMask wrongTypes, typeMask;
    TileType ttype;
    Rect bestChunk;
    int bestMin, bestMax, width, height;
    extern int selSplitFunc();		/* Forward reference. */

    /* If the source definition is changing, clear the old selection. */

    if (SelectRootDef != scx->scx_use->cu_def)
    {
	if (SelectRootDef != NULL)
	    SelectClear();
	SelectRootDef = scx->scx_use->cu_def;
	SelSetDisplay(SelectUse, SelectRootDef);
    }

    /* The chunk is computed iteratively.  First extract a small
     * region (defined by INITIALSIZE) into Select2Def.  Then find
     * the largest chunk in the region.  If the chunk touches a
     * side of the region, then extract a larger region and try
     * again.  Keep making the region larger and larger until we
     * eventually find a region that completely contains the chunk
     * with space left over around the edges.
     */

    UndoDisable();
    TTMaskSetOnlyType(&typeMask, type);

    /* Stacked types containing "type" need to be added to the mask */
    if (DBIsContact(type)) DBMaskAddStacking(&typeMask);

    TTMaskCom2(&wrongTypes, &typeMask);

    newscx = *scx;

    /* If the tile under the area of interest is a split tile, 	*/
    /* then we use that tile as the chunk.  This routine should	*/
    /* work like the while() loop underneath, looking for the	*/
    /* largest split tile, but this is a minor enhancement.	*/
    /* *However*, if it is done, the functions			*/
    /* DBCellCopyManhattanPaint()/dbCopyManhattanPaint() should */
    /* be reconsidered.						*/

    if (DBTreeSrTiles(&newscx, &typeMask, 0, selSplitFunc,
	    (ClientData)&bestChunk) != 0)
    {
        goto chunkdone;
    }

    bestMin = bestMax = 0;
    bestChunk = GeoNullRect;

    GEO_EXPAND(&newscx.scx_area, INITIALSIZE, &newscx.scx_area);
    while (TRUE)
    {
	/* Extract a bunch of junk. */

	DBCellClearDef(Select2Def);
	DBCellCopyManhattanPaint(&newscx, &typeMask, xMask, Select2Use);

	/* Now find the best chunk in the area. */

	selFindChunk(Select2Def->cd_planes[DBPlane(type)],
	    &wrongTypes, &newscx.scx_area, &scx->scx_area,
	    &bestMin, &bestMax, &bestChunk, 0);
	if (GEO_RECTNULL(&bestChunk))
	{
	    /* No chunk was found, so return. */

	    UndoEnable();
	    if (pArea != NULL) *pArea = bestChunk;
	    return;
	}

	/* If the chunk is completely inside the area we yanked, then we're
	 * done.
	 */
	
	if (GEO_SURROUND_STRONG(&newscx.scx_area, &bestChunk)) break;

	/* The chunk extends to the edge of the area.  Any place that the
	 * chunk touches an edge, move that edge out by a factor of two.
	 * Any place it doesn't touch, move the edge in to be just one
	 * unit out from the chunk.
	 */
	
	width = newscx.scx_area.r_xtop - newscx.scx_area.r_xbot;
	height = newscx.scx_area.r_ytop - newscx.scx_area.r_ybot;

	if (bestChunk.r_xbot == newscx.scx_area.r_xbot)
	    newscx.scx_area.r_xbot -= width;
	else newscx.scx_area.r_xbot = bestChunk.r_xbot - 1;
	if (bestChunk.r_ybot == newscx.scx_area.r_ybot)
	    newscx.scx_area.r_ybot -= height;
	else newscx.scx_area.r_ybot= bestChunk.r_ybot - 1;
	if (bestChunk.r_xtop == newscx.scx_area.r_xtop)
	    newscx.scx_area.r_xtop += width;
	else newscx.scx_area.r_xtop = bestChunk.r_xtop + 1;
	if (bestChunk.r_ytop == newscx.scx_area.r_ytop)
	    newscx.scx_area.r_ytop += height;
	else newscx.scx_area.r_ytop = bestChunk.r_ytop + 1;
    }

chunkdone:

    /* Flag the cell as a chunk selection so we can treat	*/
    /* the contents differently when doing a move op.		*/

    SelectUse->cu_flags |= CU_SELECT_CHUNK;
    UndoEnable();

    if (less)
      {
	SelRemoveArea(&bestChunk, &typeMask);
      }
    else
      {
	newscx.scx_area = bestChunk;

	/* Remove any stacked contact types for SelectArea */
	if (DBIsContact(type))
	    TTMaskSetOnlyType(&typeMask, type);

	SelectArea(&newscx, &typeMask, xMask);
      }

    if (pArea != NULL) *pArea = bestChunk;
}

/*
 * ----------------------------------------------------------------------------
 *
 * SelectRegion --
 *
 * 	Select an entire region of material, no matter what its
 *	shape.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	This procedure traces out the region consisting entirely
 *	of type "type", and selects all that material.  The search
 *	starts from "type" material under scx and continues outward
 *	to get all material in all cells connected to the area under
 *	scx by material of type "type".  If pArea is specified, then
 *	the rectangle that it points to is filled in with the bounding
 *	box of the region that was selected.
 *
 * ----------------------------------------------------------------------------
 */

void
SelectRegion(scx, type, xMask, pArea, less)
    SearchContext *scx;		/* Area to tree-search for material.  The
				 * transform must map to EditRoot coordinates.
				 */
    TileType type;		/* The type of material to be considered. */
    int xMask;			/* Indicates window (or windows) where cells
				 * must be expanded for their contents to be
				 * considered.  0 means treat everything as
				 * expanded.
				 */
    Rect *pArea;		/* If non-NULL, points to rectangle to be
				 * filled in with region's bounding box.
				 */
    bool less;
{
    TileTypeBitMask connections[TT_MAXTYPES];
    int i;
    SearchContext scx2;

    /* If the source definition is changing, clear the old selection. */

    if (SelectRootDef != scx->scx_use->cu_def)
    {
	if (SelectRootDef != NULL)
	    SelectClear();
	SelectRootDef = scx->scx_use->cu_def;
	SelSetDisplay(SelectUse, SelectRootDef);
    }

    /* Set up a connection table that allows only a particular type
     * of material to be considered in the region.
     */

    for (i=0; i<DBNumTypes; i+=1)
	TTMaskZero(&connections[i]);
    TTMaskSetType(&connections[type], type);

    /* Clear out the temporary selection cell and yank all of the
     * connected paint into it.
     */

    UndoDisable();
    DBCellClearDef(Select2Def);
    DBTreeCopyConnect(scx, &connections[type], xMask, connections,
	    &TiPlaneRect, Select2Use);
    UndoEnable();

    /* Now transfer what we found into the main selection cell.  Pick
     * up all the labels that correspond to the selected material.
     */
    
    SelRememberForUndo(TRUE, (CellDef *) NULL, (Rect *) NULL);
    if (less)
      {
	(void) SelRemoveSel2();
      }
    else
      {
	scx2.scx_use = Select2Use;
	scx2.scx_area = Select2Def->cd_bbox;
	scx2.scx_trans = GeoIdentityTransform;
	DBCellCopyAllPaint(&scx2, &DBAllButSpaceAndDRCBits,
			0, SelectUse);
	DBCellCopyAllLabels(&scx2, &DBAllTypeBits, CU_DESCEND_SPECIAL, SelectUse,
			(Rect *) NULL);
      }

    /* Display the new selection. */

    SelRememberForUndo(FALSE, SelectRootDef, &Select2Def->cd_bbox);

    DBReComputeBbox(SelectDef);
    DBComputeUseBbox(SelectUse);

    DBWHLRedraw(SelectRootDef, &Select2Def->cd_extended, TRUE);
    DBWAreaChanged(SelectDef, &Select2Def->cd_extended, DBW_ALLWINDOWS,
	 &DBAllButSpaceBits);

    if (pArea != NULL) *pArea = Select2Def->cd_extended;
}

/*
 * ----------------------------------------------------------------------------
 *
 * SelectNet --
 *
 * 	This procedure selects an entire electrically-connected net.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Starting from material of type "type" under scx, this procedure
 *	finds and highlights all material in all expanded cells that
 *	is electrically-connected to the starting material through a
 *	chain of expanded cells.  If pArea is specified, then the
 *	rectangle that it points to is filled in with the bounding box
 *	of the net that was selected.
 *
 * ----------------------------------------------------------------------------
 */

void
SelectNet(scx, type, xMask, pArea, less)
    SearchContext *scx;		/* Area to tree-search for material.  The
				 * transform must map to EditRoot coordinates.
				 */
    TileType type;		/* The type of material to be considered. */
    int xMask;			/* Indicates window (or windows) where cells
				 * must be expanded for their contents to be
				 * considered.  0 means treat everything as
				 * expanded.
				 */
    Rect *pArea;		/* If non-NULL, points to rectangle to be
				 * filled in with net's bounding box.
				 */
    bool less;			/* Whether to remove material from the
				 * selection using SelDeleteSel2
				 */
{
    TileTypeBitMask mask;
    SearchContext scx2;
    Point savePoint = scx->scx_area.r_ll;

    /* If the source definition is changing, clear the old selection. */

    if (SelectRootDef != scx->scx_use->cu_def)
    {
	if (SelectRootDef != NULL)
	    SelectClear();
	SelectRootDef = scx->scx_use->cu_def;
	SelSetDisplay(SelectUse, SelectRootDef);
    }

    TTMaskZero(&mask);

    // Make sure that SelectNet() matches connection-compatible
    // types with the type passed to the routine.

    // TTMaskSetType(&mask, type);
    TTMaskSetMask(&mask, &DBConnectTbl[type]);

    UndoDisable();
    DBCellClearDef(Select2Def);
    DBTreeCopyConnect(scx, &mask, xMask, DBConnectTbl,
	    &TiPlaneRect, Select2Use);
    UndoEnable();

    /* Network undo method added by Nishit and Tim, July 8-10, 2004 */
    SelNetRememberForUndo(SelectRootDef, &savePoint, type, less, TRUE);

    /* Now transfer what we found into the main selection cell.  Pick
     * up all the labels that correspond to the selected material.
     */
    
    UndoDisable();
    if (less)
    {
	(void) SelRemoveSel2();
    }
    else
    {
	scx2.scx_use = Select2Use;
	scx2.scx_area = Select2Def->cd_bbox;
	scx2.scx_trans = GeoIdentityTransform;
	DBCellCopyAllPaint(&scx2, &DBAllButSpaceAndDRCBits,
			   0, SelectUse);
	DBCellCopyAllLabels(&scx2, &DBAllTypeBits, CU_DESCEND_SPECIAL, SelectUse,
			(Rect *) NULL);
    }

    /* Set the cell use flags to mark this as a net selection,	*/
    /* so we can treat it differently when it comes time to	*/
    /* unselect it.						*/

    SelectUse->cu_flags |= CU_SELECT_NET;
    UndoEnable(); 

    DBReComputeBbox(SelectDef);
    DBComputeUseBbox(SelectUse);

    DBWHLRedraw(SelectRootDef, &Select2Def->cd_extended, TRUE);
    DBWAreaChanged(SelectDef, &Select2Def->cd_extended, DBW_ALLWINDOWS,
	&DBAllButSpaceBits);

    if (pArea != NULL) *pArea = Select2Def->cd_extended;
}

/*
 * ----------------------------------------------------------------------------
 *
 * SelectCell --
 *
 * 	Select a subcell by making a copy of it in the __SELECT__ cell.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The given use is copied into the selection.  If replace is TRUE,
 *	then the last subcell to be selected via this procedure is
 *	deselected.
 *
 * ----------------------------------------------------------------------------
 */

void
SelectCell(use, rootDef, trans, replace)
    CellUse *use;		/* Cell use to be selected. */
    CellDef *rootDef;		/* Root definition of window in which selection
				 * is being made.
				 */
    Transform *trans;		/* Transform from the coordinates of use's
				 * definition to the coordinates of rootDef.
				 */
    bool replace;		/* TRUE means deselect the last cell selected
				 * by this procedure, if it's still selected.
				 */
{
    CellUse *newUse;

    /* If the source definition is changing, clear the old selection. */

    if (SelectRootDef != rootDef)
    {
	if (SelectRootDef != NULL)
	    SelectClear();
	SelectRootDef = rootDef;
	SelSetDisplay(SelectUse, SelectRootDef);
    }

    /* Deselect the last cell selected, if requested. */

    if (replace && (selectLastUse != NULL))
    {
	Rect area;

	SelRememberForUndo(TRUE, (CellDef *) NULL, (Rect *) NULL);
	area = selectLastUse->cu_bbox;
	DBUnLinkCell(selectLastUse, SelectDef);
	DBDeleteCell(selectLastUse);
	(void) DBCellDeleteUse(selectLastUse);
	SelRememberForUndo(FALSE, SelectRootDef, &area);
	DBWHLRedraw(SelectRootDef, &area, TRUE);
	selectLastUse = (CellUse *)NULL;
    }

    /* When creating a new use, try to re-use the id from the old
     * one.  Only create a new one if the old id can't be used.
     */

    newUse = DBCellNewUse(use->cu_def, (char *) use->cu_id);
    if (!DBLinkCell(newUse, SelectDef))
    {
	freeMagic((char *) newUse->cu_id);
	newUse->cu_id = NULL;
	(void) DBLinkCell(newUse, SelectDef);
    }

    DBSetArray(use, newUse);
    DBSetTrans(newUse, trans);
    newUse->cu_expandMask = use->cu_expandMask;
    newUse->cu_flags = use->cu_flags;

    /* If this cell is already selected, there's nothing more to do.
     * Since we didn't change the selection here, be sure NOT to remember
     * it for future deselection!
     */

    if (DBCellFindDup(newUse, SelectDef) != NULL)
    {
	DBUnLinkCell(newUse, SelectDef);
	(void) DBCellDeleteUse(newUse);
	selectLastUse = (CellUse *) NULL;
	return;
    }

    SelRememberForUndo(TRUE, (CellDef *) NULL, (Rect *) NULL);
    DBPlaceCell(newUse, SelectDef);
    selectLastUse = newUse;

    SelRememberForUndo(FALSE, SelectRootDef, &newUse->cu_bbox);
    DBReComputeBbox(SelectDef);
    DBWHLRedraw(SelectRootDef, &newUse->cu_bbox, TRUE);
    DBWAreaChanged(SelectDef, &newUse->cu_bbox, DBW_ALLWINDOWS,
	&DBAllButSpaceBits);
}

/*
 * ----------------------------------------------------------------------------
 *
 * SelectAndCopy1 --
 *
 *	This procedure takes the contents of SelectDef,  and makes a
 *	copy of them in the edit cell.  Unlike SelectAndCopy2, the
 *	original selection is unchanged, and SelectDef is not cleared.
 *	This allows the implementation of "drag and drop" selections.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The selection is augmented with what's in Select2Def.  The caller
 *	should normally have cleared the selection before calling us.
 *	The edit cell is modified to include everything that was in
 *	Select2Def.
 *
 * ----------------------------------------------------------------------------
 */

void
SelectAndCopy1()
{
    SearchContext scx;
    Rect editArea;
    TileTypeBitMask mask;

    /* Just copy the information in Select2Def twice, once into the
     * edit cell and once into the main selection cell.
     */
    
    scx.scx_use = SelectUse;
    scx.scx_area = SelectUse->cu_bbox;
    GeoTransTrans(&SelectUse->cu_transform, &RootToEditTransform, &scx.scx_trans);
    TTMaskAndMask3(&mask, &DBAllButSpaceAndDRCBits, &DBActiveLayerBits);
    (void) DBCellCopyAllPaint(&scx, &mask, CU_DESCEND_SPECIAL, EditCellUse);
    (void) DBCellCopyAllLabels(&scx, &DBActiveLayerBits, CU_DESCEND_SPECIAL,
		EditCellUse, (Rect *) NULL);
    (void) DBCellCopyAllCells(&scx, CU_DESCEND_SPECIAL, EditCellUse, (Rect *) NULL);
    GeoTransRect(&scx.scx_trans, &scx.scx_area, &editArea);
    DBAdjustLabels(EditCellUse->cu_def, &editArea);
    DBWAreaChanged(EditCellUse->cu_def, &editArea, DBW_ALLWINDOWS,
	(TileTypeBitMask *) NULL);
    DRCCheckThis(EditCellUse->cu_def, TT_CHECKPAINT, &editArea);
    DBReComputeBbox(EditCellUse->cu_def);
}

/*
 * ----------------------------------------------------------------------------
 *
 * SelectAndCopy2 --
 *
 * 	This procedure is intended for use only within the selection
 *	module.  It takes what's in Select2Def, makes a copy of it in the
 *	edit cell, and makes the copy the selection.  It's used, for
 *	example, by the transformation and copying routines.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The selection is augmented with what's in Select2Def.  The caller
 *	should normally have cleared the selection before calling us.
 *	The edit cell is modified to include everything that was in
 *	Select2Def.
 *
 * ----------------------------------------------------------------------------
 */

void
SelectAndCopy2(newSourceDef)
    CellDef *newSourceDef;		/* The new selection is to be
					 * associated with this cell in the
					 * user's layout.
					 */
{
    SearchContext scx;
    Rect editArea, labelArea, expanded;
    int plane;
    void (*savedPaintPlane)();
    extern int selACPaintFunc();	/* Forward reference. */
    extern int selACCellFunc();

    /* Just copy the information in Select2Def twice, once into the
     * edit cell and once into the main selection cell.
     */
    
    scx.scx_use = Select2Use;
    scx.scx_area = Select2Use->cu_bbox;
    scx.scx_trans = RootToEditTransform;
    savedPaintPlane = DBNewPaintPlane(DBPaintPlaneActive);
    (void) DBCellCopyAllPaint(&scx, &DBAllButSpaceAndDRCBits, CU_DESCEND_SPECIAL,
		EditCellUse);
    DBNewPaintPlane(savedPaintPlane);
    (void) DBCellCopyAllLabels(&scx, &DBAllTypeBits, CU_DESCEND_SPECIAL, EditCellUse,
		(Rect *) NULL);
    (void) DBCellCopyAllCells(&scx, CU_DESCEND_SPECIAL, EditCellUse, (Rect *) NULL);
    GeoTransRect(&scx.scx_trans, &scx.scx_area, &editArea);

    DBAdjustLabels(EditCellUse->cu_def, &editArea);
    DBWAreaChanged(EditCellUse->cu_def, &editArea, DBW_ALLWINDOWS,
	(TileTypeBitMask *) NULL);
    DRCCheckThis(EditCellUse->cu_def, TT_CHECKPAINT, &editArea);
    DBReComputeBbox(EditCellUse->cu_def);

    SelectRootDef = newSourceDef;
    SelSetDisplay(SelectUse, SelectRootDef);

    SelRememberForUndo(TRUE, (CellDef *) NULL, (Rect *) NULL);
    scx.scx_trans = GeoIdentityTransform;

    /* In copying stuff into SelectUse, we have to be careful.  The problem
     * is that the stuff now in the edit cell may have switched layers.
     * (for example, Select2Def might have diff, which got painted
     * over poly in the edit cell to form transistor).  As a result, we
     * use Select2Def to figure out what areas of what planes to put into
     * SelectUse, but use the actual tile types from the edit cell.
     */
    
    for (plane = PL_SELECTBASE; plane < DBNumPlanes; plane++)
    {
	(void) DBSrPaintArea((Tile *) NULL, Select2Def->cd_planes[plane],
		&TiPlaneRect, &DBAllButSpaceAndDRCBits, selACPaintFunc,
		(ClientData) plane);
	DBMergeNMTiles(Select2Def->cd_planes[plane], &TiPlaneRect,
		(PaintUndoInfo *)NULL);
    }

    (void) DBCellCopyAllLabels(&scx, &DBAllTypeBits, CU_DESCEND_SPECIAL,
		SelectUse, NULL);

    /* We also have to be careful about copying subcells into the
     * main selection cell.  It might not have been possible to copy
     * a subcell into the edit cell (above), because the copying
     * would have formed a circularity.  In that case, we need to
     * drop that subcell from the new selection.  The code below just
     * copies those that are still in the edit cell.
     */

    (void) SelEnumCells(TRUE, (bool *) NULL, &scx, selACCellFunc,
	    (ClientData) NULL);

    DBReComputeBbox(SelectDef);
    DBComputeUseBbox(SelectUse);
    
    /* A little hack here:  don't do explicit redisplay of the selection,
     * or record a very large redisplay area for undo-ing.  It's not
     * necessary since the layout redisplay also redisplays the highlights.
     * If we do it too, then we're just double-displaying and wasting
     * time.  (note: must record something for undo-ing in order to get
     * SelectRootDef set right... just don't pass a redisplay area).
     */

    SelRememberForUndo(FALSE, SelectRootDef, (Rect *) NULL);
    DBWAreaChanged(SelectDef, &SelectDef->cd_extended, DBW_ALLWINDOWS,
	(TileTypeBitMask *) NULL);
}

/* Structure to hold information about an edit area, which may be
 * a triangular region.
 */
typedef struct {
    Rect *editClip;
    TileType ttype;
} acparg;

/* Utility function: for each tile, copy information over its area from
 * the given edit cell plane to SelectDef.  Always return 0 to keep the
 * search alive.
 */

int
selACPaintFunc(tile, plane)
    Tile *tile;			/* Tile in Select2Def. */
    int plane;			/* Index of plane this tile came from. */
{
    Rect area, editArea;
    acparg selACarg;
    int selACPaintFunc2();	/* Forward reference. */

    TiToRect(tile, &area);
    /* we want editClip in root coordinates. . . */
    selACarg.editClip = &area;
    GeoTransRect(&RootToEditTransform, &area, &editArea);

    selACarg.ttype = TiGetTypeExact(tile);

    if (IsSplit(tile))
    {
	DBSrPaintNMArea((Tile *) NULL, EditCellUse->cu_def->cd_planes[plane],
		selACarg.ttype, &editArea, &DBAllButSpaceAndDRCBits,
		selACPaintFunc2, (ClientData) &selACarg);
    }
    else
	(void) DBSrPaintArea((Tile *) NULL, EditCellUse->cu_def->cd_planes[plane],
		&editArea, &DBAllButSpaceAndDRCBits, selACPaintFunc2,
		(ClientData) &selACarg);
    return 0;
}

/* Second-level paint function:  just paint the overlap between
 * tile and editClip into SelectDef.
 *
 * This function is like dbCopyAllPaint() but differs just enough
 * that a separate function is required.  However, much of the code 
 * could be shared between the two functions if it were properly
 * broken out into subroutines.
 */

int
selACPaintFunc2(tile, selACarg)
    Tile *tile;			/* Tile in edit cell. */
    acparg *selACarg;		/* Contains edit-cell area to clip to
				 * before painting into selection.
				 */
{
    Rect *editClip = selACarg->editClip;
    Rect area, selArea;
    TileType type = TiGetTypeExact(tile);
    TileTypeBitMask tmask, *rmask;
    TileType ttype, rtype;
    TileType dinfo = selACarg->ttype & (TT_DIAGONAL | TT_DIRECTION | TT_SIDE);

    TiToRect(tile, &area);
    GeoTransRect(&EditToRootTransform, &area, &selArea);

    if ((dinfo & TT_DIAGONAL) || (type & TT_DIAGONAL))
    {
	/* If the select area is triangular, then we need to	*/
	/* clip in a more complicated manner.			*/
	/* Likewise if the edit cell tile is triangular and	*/
	/* the select area not.					*/

	Point points[5];
	Rect rrect, orect;
	int np, i, j;

	ttype = (selACarg->ttype & TT_SIDE) ? ((ttype & TT_RIGHTMASK) >> 14) :
		ttype & TT_LEFTMASK;

	if (type & TT_DIAGONAL)
	    rtype = (type & TT_SIDE) ? SplitRightType(tile) :
			SplitLeftType(tile);
	else
	    rtype = type;

	if (rtype >= DBNumUserLayers)
	{
	    rmask = DBResidueMask(rtype);
	    if (TTMaskHasType(rmask, ttype))
		rtype = ttype;
	}

	TTMaskSetOnlyType(&tmask, rtype);

	type = (dinfo & TT_SIDE) ? (rtype << 14) : rtype;
	type |= dinfo;

	if (dinfo & TT_DIAGONAL)
	    GrClipTriangle(editClip, &selArea, TRUE, dinfo, points, &np);
	else
	    GrClipTriangle(&selArea, editClip, TRUE, type, points, &np);

	if (np == 0)
	    return 0;
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
		    GeoCanonicalRect(&rrect, &selArea);
		    break;
		}
	    }
	    if (i == np)  /* Exactly one Manhattan rectangle */
	    {
		rrect.r_xbot = points[0].p_x;
		rrect.r_xtop = points[2].p_x;
		rrect.r_ybot = points[0].p_y;
		rrect.r_ytop = points[2].p_y;
		GeoCanonicalRect(&rrect, &selArea);
		type = rtype;
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
		if (selArea.r_xbot > orect.r_xbot)
		{
		    rrect.r_xbot = orect.r_xbot;
		    rrect.r_xtop = selArea.r_xbot;
		}
		else if (selArea.r_xtop < orect.r_xtop)
		{
		    rrect.r_xtop = orect.r_xtop;
		    rrect.r_xbot = selArea.r_xtop;
		}
		else
		    goto topbottom;

		DBPaintValid(SelectDef, &rrect, &tmask, 0);
topbottom:
		/* Rectangle to top or bottom */
		rrect.r_xbot = selArea.r_xbot;
		rrect.r_xtop = selArea.r_xtop;
		if (selArea.r_ybot > orect.r_ybot)
		{
		    rrect.r_ybot = orect.r_ybot;
		    rrect.r_ytop = selArea.r_ybot;
		}
		else if (selArea.r_ytop < orect.r_ytop)
		{
		    rrect.r_ytop = orect.r_ytop;
		    rrect.r_ybot = selArea.r_ytop;
		}
		else
		    goto splitdone;

		DBPaintValid(SelectDef, &rrect, &tmask, 0);
	    }
	}
    }
    else
    {
	ttype = selACarg->ttype;

	if (type & TT_DIAGONAL)
	    rtype = (type & TT_SIDE) ? SplitRightType(tile) : SplitLeftType(tile);
	else
	    rtype = type;

	if (rtype >= DBNumUserLayers)
	{
	    rmask = DBResidueMask(rtype);
	    if (TTMaskHasType(rmask, ttype))
		rtype = ttype;
	}
	TTMaskSetOnlyType(&tmask, rtype);
    }
    GeoClip(&selArea, editClip);	/* editClip already in root coords */

splitdone:
    DBPaintValid(SelectDef, &selArea, &tmask, type);
    return 0;
}

/* Cell search function:  invoked for each subcell in Select2Def that's
 * also in the edit cell.  Make a copy of the cell in SelectDef.
 */

int
selACCellFunc(selUse, realUse)
    CellUse *selUse;		/* Use to be copied into SelectDef.  This
				 * is the instance inside Select2Def.
				 */
    CellUse *realUse;		/* The cellUse (in the edit cell) corresponding
				 * to selUse.  We need this in order to use its
				 * instance id and expand mask in the selection.
				 */
{
    CellUse *newUse;

    newUse = DBCellNewUse(selUse->cu_def, realUse->cu_id);
    if (!DBLinkCell(newUse, SelectDef))
    {
	freeMagic((char *) newUse->cu_id);
	newUse->cu_id = NULL;
	(void) DBLinkCell(newUse, SelectDef);
    }
    newUse->cu_expandMask = realUse->cu_expandMask;
    newUse->cu_flags = realUse->cu_flags;
    DBSetArray(selUse, newUse);
    DBSetTrans(newUse, &selUse->cu_transform);
    DBPlaceCell(newUse, SelectDef);
    return 0;
}

