/*
 * selEnum.c --
 *
 * This file contains routines to enumerate various pieces of the
 * selection, e.g. find all subcells that are in the selection and
 * also in the edit cell.  The procedures here are used as basic
 * building blocks for the selection commands like copy or delete.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/select/selEnum.c,v 1.13 2010/06/24 12:37:56 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "database/database.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "windows/windows.h"
#include "utils/main.h"
#include "select/select.h"
#include "select/selInt.h"
#include "utils/malloc.h"
#include "textio/textio.h"

/* Structure passed from top-level enumeration procedures to lower-level
 * ones:
 */

struct searg
{
    int (*sea_func)();		/* Client function to call. */
    ClientData sea_cdarg;	/* Client data to pass to sea_func. */
    char sea_flags;		/* See flag definitions below */
    bool *sea_nonEdit;		/* Word to set if non-edit stuff is found. */
    int sea_plane;		/* Index of plane currently being searched. */
    Rect sea_rect;		/* Area of tile being searched */
    TileType sea_type;		/* Type of current piece of selected paint. */
    ExtRectList *sea_rectList;	/* List of rectangles found in edit cell. */
    CellUse *sea_use;		/* Use that we're looking for an identical
				 * copy of in the layout.
				 */
    CellUse *sea_foundUse;	/* Use that was found to match sea_use, or else
				 * NULL.  Also used to hold use for foundLabel.
				 */
    Transform sea_foundTrans;	/* Transform from coords of foundUse to root. */
    Label *sea_label;		/* Label that we're trying to match in the
				 * layout.
				 */
    Label *sea_foundLabel;	/* Matching label that was found, or NULL.
				 * If non_NULL, foundUse and foundTrans
				 * describe its containing cell.
				 */
};

/* sea_flags bit definitions */

#define SEA_EDIT_ONLY	0x01	/* When set, only consider stuff that is in
				 * the edit cell.
				 */
#define SEA_CHUNK	0x02	/* When set, treat the selection as a chunk
				 * (affects behavior of SelEnumPaint())
				 */
#define SEA_AREA	0x04	/* When set, treat the selection as an area
				 * selection with a possibly limited number
				 * of named types.
				 */

/*
 * ----------------------------------------------------------------------------
 *
 * SelEnumPaint --
 *
 * 	Find all selected paint, and call the client's procedure for
 *	all the areas of paint that are found.  Only consider paint
 *	on "layers", and if "editOnly" is TRUE, then only consider
 *	paint that it is the edit cell.  The client procedure must
 *	be of the form
 *
 *	int
 *	func(rect, type, clientData)
 *	    Rect *rect;
 *	    TileType type;
 *	    ClientData clientData;
 *	{
 *	}
 *
 *	The rect and type parameters identify the paint that was found,
 *	in root coordinates, and clientData is just the clientData
 *	argument passed to this	procedure.  Func should normally return
 *	0.  If it returns a non-zero return value, then the search
 *	will be aborted.
 *
 *	Note that the "type" value passed to func() always contains a
 *	tiletype in the TT_LEFTMASK field regardless of the upper bits
 *	describing the diagonal of a non-Manhattan tile.  The function
 *	is responsible for shifting the type field up into TT_RIGHTMASK
 *	if the TT_SIDE bit is set.  This method is obscure but usually
 *	simplifies processing of non-Manhattan tiles in func().
 *
 * Results:
 *	Returns 0 if the search finished normally.  Returns 1 if the
 *	search was aborted.
 *
 * Side effects:
 *	If foundNonEdit is non-NULL, its target is set to indicate
 *	whether there was selected paint from outside the edit cell.
 *	Otherwise, the only side effects are those of func.
 *
 * ----------------------------------------------------------------------------
 */

int
SelEnumPaint(layers, editOnly, foundNonEdit, func, clientData)
    TileTypeBitMask *layers;	/* Mask layers to find. */
    bool editOnly;		/* TRUE means only find material that is
				 * both selected and in the edit cell.
				 */
    bool *foundNonEdit;		/* If non-NULL, this word is set to TRUE
				 * if there's selected paint that's not in
				 * the edit cell, FALSE otherwise.
				 */
    int (*func)();		/* Function to call for paint that's found. */
    ClientData clientData;	/* Argument to pass through to func. */
{
    int plane;
    struct searg arg;
    extern int selEnumPFunc1();	/* Forward declaration. */

    arg.sea_func = func;
    arg.sea_cdarg = clientData;
    arg.sea_flags = (editOnly) ? SEA_EDIT_ONLY : 0;
    arg.sea_nonEdit = foundNonEdit;
    arg.sea_rectList = NULL;
    if (foundNonEdit != NULL) *foundNonEdit = FALSE;

    /* First, find all the paint in the selection that has the right
     * layers.  Use the same procedure as "dbCellUniqueTileSrFunc()"
     * so that contacts are not double-counted.
     */
    
    for (plane = PL_SELECTBASE; plane < DBNumPlanes; plane++)
    {
	arg.sea_plane = plane;
	if (DBSrPaintArea((Tile *) NULL, SelectDef->cd_planes[plane],
		&TiPlaneRect, layers, selEnumPFunc1,
		(ClientData) &arg) != 0)
	    return 1;
    }
    return 0;
}

/* Search function invoked for each piece of paint on the right layers
 * in the select cell.  Collect all of the sub-areas of this piece that
 * are also in the edit cell, then call the client function for each
 * one of them.  It's important to collect the pieces first, then call
 * the client:  if we call the client while the edit cell search is
 * underway, the client might trash the tile plane underneath us.
 */

int
selEnumPFunc1(tile, arg)
    Tile *tile;			/* Tile of matching type. */
    struct searg *arg;		/* Describes the current search. */
{
    Rect editRect, rootRect;
    TileType loctype;
    TileTypeBitMask uMask;
    extern int selEnumPFunc2();

    TiToRect(tile, &arg->sea_rect);

    if (IsSplit(tile))
    {
	arg->sea_type = TiGetTypeExact(tile) & (TT_DIAGONAL | TT_SIDE | TT_DIRECTION);
	if (SplitSide(tile))
	    loctype = SplitRightType(tile);
	else
	    loctype = SplitLeftType(tile);
    }
    else 
	loctype = TiGetType(tile);

    if (IsSplit(tile))
	arg->sea_type |= loctype;
    else
	arg->sea_type = loctype;

    /* If the paint doesn't have to be in the edit cell, life's pretty
     * simple:  just call the client and quit.
     */
    
    if (!(arg->sea_flags & SEA_EDIT_ONLY))
    {
	if ((*arg->sea_func)(&arg->sea_rect, arg->sea_type, arg->sea_cdarg) != 0)
	    return 1;
	return 0;
    }

    /* From here to the end of the routine gets tricky.  To apply the	*/
    /* function only to what's in the edit cell, we use the selection	*/
    /* paint to compare against the edit cell, and apply the function	*/
    /* to matching paint, ignoring paint from non-edit cells.  Because	*/
    /* the function may alter the edit cell, we must collect a list of	*/
    /* areas to paint, and apply them one by one without reference to	*/
    /* any actual tiles.						*/

    GeoTransRect(&RootToEditTransform, &arg->sea_rect, &editRect);
    arg->sea_rectList = NULL;

    /* If we have selected a chunk, set sea_flags to mark	*/
    /* this condition (see comments in selEnumPFunc2, below)	*/

    if (SelectUse->cu_flags & CU_SELECT_CHUNK)
	arg->sea_flags |= SEA_CHUNK;
    if (!TTMaskIsZero(&SelectDef->cd_types))
	arg->sea_flags |= SEA_AREA;

    if (IsSplit(tile))
	DBSrPaintNMArea((Tile *)NULL,
		EditCellUse->cu_def->cd_planes[arg->sea_plane],
		arg->sea_type, &editRect, &DBAllTypeBits, selEnumPFunc2,
		(ClientData)arg);
    else
	DBSrPaintArea((Tile *)NULL,
		EditCellUse->cu_def->cd_planes[arg->sea_plane],
		&editRect, &DBAllTypeBits, selEnumPFunc2,
		(ClientData)arg);

    /* Each rectangle found represents paint that is both in	*/
    /* the selection and also in the edit cell.  Call the	*/
    /* client for each such area.				*/

    while (arg->sea_rectList != NULL)
    {
	GeoTransRect(&EditToRootTransform, &arg->sea_rectList->r_r, &rootRect);
	GeoClip(&rootRect, &arg->sea_rect);

	if ((*arg->sea_func)(&rootRect, arg->sea_rectList->r_type, arg->sea_cdarg) != 0)
	    return 1;
	freeMagic((char *)arg->sea_rectList);
	arg->sea_rectList = arg->sea_rectList->r_next;
    }
    return 0;
}

/* Second-level paint search function:  save around (in edit coords)
 * each tile that has the same type as requested in arg.  Record if
 * any wrong-type tiles are found.
 */

int
selEnumPFunc2(tile, arg)
    Tile *tile;			/* Tile found in the edit cell */ 
    struct searg *arg;		/* Describes our search */
{
    ExtRectList *lr;
    int ttype;
    TileType seltype;

    if (IsSplit(tile))
	ttype = SplitSide(tile) ? SplitRightType(tile) : SplitLeftType(tile);
    else
	ttype = TiGetTypeExact(tile);
    seltype = arg->sea_type & TT_LEFTMASK;

    /* Handle stacked contact types */
    if ((ttype != seltype) && (ttype >= DBNumUserLayers))
    {
	TileTypeBitMask *rmask = DBResidueMask(ttype);
	TileType rtype;
	for (rtype = TT_TECHDEPBASE; rtype < DBNumUserLayers; rtype++)
	    if (TTMaskHasType(rmask, rtype))
		if (arg->sea_plane == DBPlane(rtype))
		{
		    ttype = rtype;
		    break;
		}
    }

    /* For chunk selection, the types have to match exactly, so we don't
     * pick up bits of compatible material in another cell that were not
     * intended (a chunk can only exist in one cell)
     */
    if (arg->sea_flags & SEA_CHUNK)
	if (ttype != seltype)
	    return 0;

    /* If we selected with "select area <types>", then the type		*/
    /* must belong to <types> (after decomposing stacked types).	*/

    if (arg->sea_flags & SEA_AREA)
	if (ttype != seltype)
	    if (!TTMaskHasType(&SelectDef->cd_types, ttype))
		return 0;

    /* Check for compatible types.  That is:  paint X (in use) over Y	*/
    /* (in select) yields Y means that it is okay to add X to the list.	*/
    /* Also okay if the X over Y yields a stacking type whose residues	*/
    /* are X and Y.							*/

    if ((ttype != seltype) && (((ttype == TT_SPACE) &&
		TTMaskHasType(&DBHomePlaneTypes[arg->sea_plane], seltype))) ||
		(DBPaintResultTbl[arg->sea_plane][ttype][seltype] != seltype))
    {
	TileType chktype = DBPaintResultTbl[arg->sea_plane][ttype][seltype];
	TileTypeBitMask *cmask = DBResidueMask(chktype);

	if (chktype < DBNumUserLayers || ((ttype != chktype)
			&& !TTMaskHasType(cmask, ttype))
			|| !TTMaskHasType(cmask, seltype))
	{
	    if (arg->sea_nonEdit != NULL) *(arg->sea_nonEdit) = TRUE;
	}
	return 0;
    }

    /* Only process contacts once:  only process the home plane	*/
    /* image of any contact type.  Stacked contacts are treated	*/
    /* as whichever residue (contact) is in its home plane.	*/

    if (DBIsContact(ttype))
    {
	if (ttype < DBNumUserLayers)
	{
	    if (arg->sea_plane != DBPlane(ttype)) return 0;
	}
	else	/* stacked contact type */
	{
	    TileTypeBitMask *rmask = DBResidueMask(ttype);
	    TileType rtype;
	    for (rtype = TT_TECHDEPBASE; rtype < DBNumUserLayers; rtype++)
		if (TTMaskHasType(rmask, rtype) &&
				arg->sea_plane == DBPlane(rtype))
		    break;
	    if (rtype == DBNumUserLayers) return 0;

	    /* Continue, with the tile type recast as rtype */

	    ttype = rtype;
	}
    }

    lr = (ExtRectList *)mallocMagic(sizeof(ExtRectList));
    TiToRect(tile, &lr->r_r);

    if (arg->sea_type & TT_DIAGONAL)
    {
	Point points[5];
	int np, i, j;
	Rect area, orect;

	if (!IsSplit(tile))
	{
	    /* Things get messy here.  Clip against the select triangle	*/
	    /* and decompose the result into rectangles and triangles.	*/ 

	    GrClipTriangle(&arg->sea_rect, &lr->r_r, TRUE, arg->sea_type, points, &np);

	    if (np < 3)
	    {
		freeMagic((char *)lr);
		return 0;	/* Clipped out of existence */
	    }
	    else
	    {
		for (i = 0; i < np; i++)
		{
		    j = (i + 1) % np;
		    if (points[i].p_x != points[j].p_x && points[i].p_y !=
				points[j].p_y)
		    {
			/* Break out the triangle */
			lr->r_r.r_xbot = points[i].p_x;
			lr->r_r.r_xtop = points[j].p_x;
			lr->r_r.r_ybot = points[i].p_y;
			lr->r_r.r_ytop = points[j].p_y;
			GeoCanonicalRect(&lr->r_r, &area);
			break;
		    }
		}
		if (i == np)	/* Exactly one Manhattan rectangle */
		{
		    lr->r_r.r_xbot = points[0].p_x;
		    lr->r_r.r_xtop = points[2].p_x;
		    lr->r_r.r_ybot = points[0].p_y;
		    lr->r_r.r_ytop = points[2].p_y;
		    GeoCanonicalRect(&lr->r_r, &area);
		    lr->r_type = ttype;
		}
		else if (np >= 4) /* Process extra rectangles */
		{
		    orect.r_xtop = orect.r_xbot = points[0].p_x;
		    orect.r_ytop = orect.r_ybot = points[0].p_y;
		    for (i = 0; i < np; i++)
			GeoIncludePoint(&points[i], &orect);

		    /* Rectangle to right or left */
		    lr->r_r.r_ybot = orect.r_ybot;
		    lr->r_r.r_ytop = orect.r_ytop;
		    if (area.r_xbot > orect.r_xbot)
		    {
			lr->r_r.r_xbot = orect.r_xbot;
			lr->r_r.r_xtop = area.r_xbot;
		    }
		    else if (area.r_xtop < orect.r_xtop)
		    {
			lr->r_r.r_xtop = orect.r_xtop;
			lr->r_r.r_xbot = area.r_xtop;
		    }
		    else
			goto topbottom;

		    lr->r_type = ttype;
		    lr->r_next = arg->sea_rectList;
		    arg->sea_rectList = lr;
		    lr = (ExtRectList *)mallocMagic(sizeof(ExtRectList));
		    lr->r_r = arg->sea_rectList->r_r;
topbottom:
		    /* Rectangle to top or bottom */
		    lr->r_r.r_xbot = area.r_xbot;
		    lr->r_r.r_xtop = area.r_xtop;
		    if (area.r_ybot > orect.r_ybot)
		    {
			lr->r_r.r_ybot = orect.r_ybot;
			lr->r_r.r_ytop = area.r_ybot;
		    }
		    else if (area.r_ytop < orect.r_ytop)
		    {
			lr->r_r.r_ytop = orect.r_ytop;
			lr->r_r.r_ybot = area.r_ytop;
		    }
		    else
		    {
			lr->r_type = ttype;
			goto splitdone;
		    }

		    lr->r_type = ttype;
		    lr->r_next = arg->sea_rectList;
		    arg->sea_rectList = lr;
		    lr = (ExtRectList *)mallocMagic(sizeof(ExtRectList));
		    lr->r_r = area;
		    lr->r_type = arg->sea_type;
		}
		else
		{
		    lr->r_r = area;
		    lr->r_type = arg->sea_type;
		}
	    }
	}
	else
	{
	    /* NOTE:  Need general-purpose triangle-triangle	*/
	    /* intersection routine here!  However, for most	*/
	    /* purposes, it suffices to copy the tile if the	*/
	    /* triangles are similar and to compute the		*/
	    /* rectangular union are if opposite.		*/
	    if ((arg->sea_type & TT_SIDE) == (TiGetTypeExact(tile) & TT_SIDE))
		lr->r_type = ttype | (arg->sea_type &
			(TT_DIAGONAL | TT_SIDE | TT_DIRECTION));
	    else
		lr->r_type = ttype;
	}
    }
    else
	lr->r_type = ttype;

splitdone:
    lr->r_next = arg->sea_rectList;
    arg->sea_rectList = lr;
    return 0;
}


/*
 * ----------------------------------------------------------------------------
 *
 * SelEnumCells --
 *
 * 	Call a client-supplied procedure for each selected subcell.
 *	If "editOnly" is TRUE, then only consider selected subcells
 *	that are children of the edit cell.  The client procedure
 *	must be of the form
 *
 *	int
 *	func(selUse, realUse, transform, clientData)
 *	    CellUse *selUse;
 *	    CellUse *realUse;
 *	    Transform *transform;
 *	    ClientData clientData;
 *	{
 *	}
 *
 *	SelUse is a pointer to a cellUse that's in the selection cell.
 *	RealUse is a pointer to the corresponding cell that's part of
 *	the layout.  Transform is a transform from the coordinates of
 *	RealUse to root coordinates.  If the cell is an array, only one
 *	call is made for the entire array, and transform is the transform
 *	for the root element of the array (array[xlo, ylo]).  Func should
 *	normally return 0.  If it returns a non-zero return value, then
 *	the search will be aborted.
 *
 * Results:
 *	Returns 0 if the search finished normally.  Returns 1 if the
 *	search was aborted.
 *
 * Side effects:
 *	If foundNonEdit is non-NULL, its target is set to indicate
 *	whether there were selected cells that weren't children of
 *	the edit cell. 	Otherwise, the only side effects are those
 *	of func.
 *
 * ----------------------------------------------------------------------------
 */

int
SelEnumCells(editOnly, foundNonEdit, scx, func, clientData)
    bool editOnly;		/* TRUE means only find material that is
				 * both selected and in the edit cell.
				 */
    bool *foundNonEdit;		/* If non-NULL, this word is set to TRUE
				 * if there are one or more selected cells
				 * that aren't children of the edit cell,
				 * FALSE otherwise.
				 */
    SearchContext *scx;		/* Most clients will provide a NULL value
				 * here, in which case all the subcells in
				 * the selection are enumerated.  If this
				 * is non-NULL, it describes a different
				 * area in which to enumerate subcells.  This
				 * feature is intended primarily for internal
				 * use within this module.
				 */
    int (*func)();		/* Function to call for subcells found. */
    ClientData clientData;	/* Argument to pass through to func. */
{
    struct searg arg;
    SearchContext scx2;
    extern int selEnumCFunc1();	/* Forward reference. */

    arg.sea_func = func;
    arg.sea_cdarg = clientData;
    arg.sea_flags = (editOnly) ? SEA_EDIT_ONLY : 0;
    arg.sea_nonEdit = foundNonEdit;
    if (foundNonEdit != NULL) *foundNonEdit = FALSE;

    /* Find all the subcells that are in the selection. */

    if (scx != NULL)
	scx2 = *scx;
    else
    {
	scx2.scx_use = SelectUse;
	scx2.scx_area = TiPlaneRect;
	scx2.scx_trans = GeoIdentityTransform;
    }
    if (DBCellSrArea(&scx2, selEnumCFunc1, (ClientData) &arg) != 0)
	return 1;
    return 0;
}

/* The first-level search function:  called for each subcell in the
 * selection.
 */

int
selEnumCFunc1(scx, arg)
    SearchContext *scx;		/* Describes cell that was found. */
    struct searg *arg;		/* Describes our search. */
{
    SearchContext scx2;
    extern int selEnumCFunc2();	/* Forward reference. */
    CellUse dummy;

    /* If this cell is the top-level one in its window, we have to
     * handle it specially:  just look for any use that's a top-level
     * use, then call the client for it.
     */
    
    if (scx->scx_use->cu_def == SelectRootDef)
    {
	CellUse *parent;

	/* A root use can't ever be a child of the edit cell. */

	if (arg->sea_flags & SEA_EDIT_ONLY)
	{
	    if (arg->sea_nonEdit != NULL) *(arg->sea_nonEdit) = TRUE;
	    return 2;
	}
	
	/* Find a top-level use (one with no parent). */

	for (parent = SelectRootDef->cd_parents;
	     parent != NULL;
	     parent = parent->cu_nextuse)
	{
	    if (parent->cu_parent == NULL) break;
	}

	if (parent == NULL)
	{
	    TxError("Internal error:  couldn't find selected root cell %s.\n",
		SelectRootDef->cd_name);
	    return 2;
	}

	/* Call the client. */

	if ((*arg->sea_func)(scx->scx_use, parent, &GeoIdentityTransform,
		arg->sea_cdarg) != 0)
	    return 1;
	return 2;
    }

    /* This isn't a top-level cell.  Find the instance corresponding
     * to this one in the layout.  Only search a 1-unit square at the
     * cell's lower-left corner in order to cut down the work that
     * has to be done.  Unfortunately
     * we can't use DBTreeSrCells for this, because we don't want to
     * look at expanded/unexpanded information.
     */
    
    scx2.scx_use = &dummy;
    dummy.cu_def = SelectRootDef;
    dummy.cu_id = NULL;
    GeoTransRect(&scx->scx_use->cu_transform, &scx->scx_use->cu_def->cd_bbox,
	    &scx2.scx_area);
    scx2.scx_area.r_xtop = scx2.scx_area.r_xbot + 1;
    scx2.scx_area.r_ytop = scx2.scx_area.r_ybot + 1;
    scx2.scx_trans = GeoIdentityTransform;
    arg->sea_use = scx->scx_use;
    arg->sea_foundUse = NULL;
    (void) DBCellSrArea(&scx2, selEnumCFunc2, (ClientData) arg);
    if (arg->sea_foundUse == NULL)
    {
	TxError("Internal error:  couldn't find selected cell %s.\n",
	    arg->sea_use->cu_id);
	return 2;
    }

    /* See whether the cell is a child of the edit cell and
     * call the client's procedure if everything's OK.  We do the
     * call here rather than in selEnumCFunc2 because the client
     * could modify the edit cell in a way that would cause the
     * search in progress to core dump.  By the time we get back
     * here, the search is complete so there's no danger.
     */

    if (arg->sea_flags & SEA_EDIT_ONLY)
    {
	if (!EditCellUse) return 1;
	if (arg->sea_foundUse->cu_parent != EditCellUse->cu_def)
	{
	    if (arg->sea_nonEdit != NULL) *(arg->sea_nonEdit) = TRUE;
	    return 2;
	}
    }
    if ((*arg->sea_func)(scx->scx_use, arg->sea_foundUse,
	    &arg->sea_foundTrans, arg->sea_cdarg) != 0)
	return 1;
    return 2;
}

/* Second-level cell search function:  called for each cell in the
 * tree of SelectRootDef that touches the lower-left corner of
 * the subcell in the selection that we're trying to match.  If
 * this use is for the same subcell, and has the same transformation
 * and array structure, then remember the cell use for the caller
 * and abort the search.
 */

int
selEnumCFunc2(scx, arg)
    SearchContext *scx;		/* Describes child of edit cell. */
    struct searg *arg;		/* Describes what we're looking for. */
{
    CellUse *use, *selUse;

    use = scx->scx_use;
    selUse = arg->sea_use;
    if (use->cu_def != selUse->cu_def) goto checkChildren;
    if ((scx->scx_trans.t_a != selUse->cu_transform.t_a)
	    || (scx->scx_trans.t_b != selUse->cu_transform.t_b)
	    || (scx->scx_trans.t_c != selUse->cu_transform.t_c)
	    || (scx->scx_trans.t_d != selUse->cu_transform.t_d)
	    || (scx->scx_trans.t_e != selUse->cu_transform.t_e)
	    || (scx->scx_trans.t_f != selUse->cu_transform.t_f))
	goto checkChildren;
    if ((use->cu_array.ar_xlo != selUse->cu_array.ar_xlo)
	    || (use->cu_array.ar_ylo != selUse->cu_array.ar_ylo)
	    || (use->cu_array.ar_xhi != selUse->cu_array.ar_xhi)
	    || (use->cu_array.ar_yhi != selUse->cu_array.ar_yhi)
	    || (use->cu_array.ar_xsep != selUse->cu_array.ar_xsep)
	    || (use->cu_array.ar_ysep != selUse->cu_array.ar_ysep))
	goto checkChildren;
    
    arg->sea_foundUse = use;
    arg->sea_foundTrans = scx->scx_trans;
    return 1;

    /* This cell didn't match... see if any of its children do. */

    checkChildren:
    if (DBCellSrArea(scx, selEnumCFunc2, (ClientData) arg) != 0)
	return 1;
    else return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * SelEnumLabels --
 *
 * 	Find all selected labels, and call the client's procedure for
 *	each label found.  Only consider labels attached to "layers",
 *	and if "editOnly" is TRUE, then only consider labels that
 *	are in the edit cell.  The client procedure must be of the
 *	form
 *
 *	int
 *	func(label, cellUse, transform, clientData)
 *	    Label *label;
 *	    CellUse *cellUse;
 *	    Transform *transform;
 *	    ClientData clientData;
 *	{
 *	}
 *
 *	Label is a pointer to a selected label.  It refers to the label
 *	in cellUse, and transform gives the transform from that
 *	cell's coordinates to root coordinates.  ClientData is just
 *	the clientData argument passed to this procedure.  Func
 *	should normally return 0.  If it returns a non-zero return
 *	value, then the search will be aborted.
 *
 * Results:
 *	Returns 0 if the search finished normally.  Returns 1 if the
 *	search was aborted.
 *
 * Side effects:
 *	If foundNonEdit is non-NULL, its target is set to indicate
 *	whether there was at least one selected label that was not
 *	in the edit cell.  Otherwise, the only side effects are
 *	those of func.
 *
 * ----------------------------------------------------------------------------
 */

int
SelEnumLabels(layers, editOnly, foundNonEdit, func, clientData)
    TileTypeBitMask *layers;	/* Find labels on these layers. */
    bool editOnly;		/* TRUE means only find labels that are
				 * both selected and in the edit cell.
				 */
    bool *foundNonEdit;		/* If non-NULL, this word is set to TRUE
				 * if there are selected labels that aren't
				 * in the edit cell, FALSE otherwise.
				 */
    int (*func)();		/* Function to call for each label found. */
    ClientData clientData;	/* Argument to pass through to func. */
{
    Label *selLabel;
    CellUse dummy;
    SearchContext scx;
    struct searg arg;
    extern int selEnumLFunc();	/* Forward reference. */
    extern int selEnumLFunc2();

    if (foundNonEdit != NULL) *foundNonEdit = FALSE;

    /* First of all, search through all of the selected labels. */

    for (selLabel = SelectDef->cd_labels; selLabel != NULL;
	    selLabel = selLabel->lab_next)
    {
	if (!TTMaskHasType(layers, selLabel->lab_type)) continue;

	/* Find the label corresponding to this one in the design. */
	
	scx.scx_use = &dummy;
	dummy.cu_def = SelectRootDef;
	dummy.cu_id = NULL;
	GEO_EXPAND(&selLabel->lab_rect, 1, &scx.scx_area);
	scx.scx_trans = GeoIdentityTransform;
	arg.sea_label = selLabel;
	arg.sea_foundLabel = NULL;
	(void) DBTreeSrLabels(&scx, &DBAllTypeBits, 0, (TerminalPath *) NULL,
	    TF_LABEL_ATTACH, selEnumLFunc, (ClientData) &arg);
	if (arg.sea_foundLabel == NULL)
	{
	    /* Take a 2nd try with relaxed criteria.  We may have	*/
	    /* altered the text or label justification.			*/

	    DBTreeSrLabels(&scx, &DBAllTypeBits, 0, (TerminalPath *) NULL,
			TF_LABEL_ATTACH, selEnumLFunc2, (ClientData) &arg);
	    if (arg.sea_foundLabel == NULL)
	    {
		TxError("Internal error:  couldn't find selected label %s.\n",
				selLabel->lab_text);
		continue;
	    }
	}

	/* If only edit-cell labels are wanted, check this label's
	 * parentage.
	 */
	
	if (editOnly && (arg.sea_foundUse->cu_def != EditCellUse->cu_def))
	{
	    if (foundNonEdit != NULL) *foundNonEdit = TRUE;
	    continue;
	}

	if ((*func)(arg.sea_foundLabel, arg.sea_foundUse,
	    &arg.sea_foundTrans, clientData) != 0) return 1;
    }

    return 0;
}

/* Search function for label enumeration:  make sure that this label
 * matches the one we're looking for.  If it does, then record
 * information about it and return right away.
 */

	/* ARGSUSED */
int
selEnumLFunc(scx, label, tpath, arg)
    SearchContext *scx;		/* Describes current cell for search. */
    Label *label;		/* Describes label that is in right area
				 * and has right type.
				 */
    TerminalPath *tpath;	/* Ignored. */
    struct searg *arg;		/* Indicates what we're looking for. */
{
    Rect *want, got;

    GeoTransRect(&scx->scx_trans, &label->lab_rect, &got);
    want = &arg->sea_label->lab_rect;
    if (want->r_xbot != got.r_xbot) return 0;
    if (want->r_ybot != got.r_ybot) return 0;
    if (want->r_xtop != got.r_xtop) return 0;
    if (want->r_ytop != got.r_ytop) return 0;
    if (arg->sea_label->lab_just
	!= GeoTransPos(&scx->scx_trans, label->lab_just)) return 0;
    if (strcmp(label->lab_text, arg->sea_label->lab_text) != 0) return 0;

    arg->sea_foundLabel = label;
    arg->sea_foundUse = scx->scx_use;
    arg->sea_foundTrans = scx->scx_trans;
    return 1;
}

/*
 * selEnumLFunc2() is like selEnumLFunc(), but it allows one of the justification
 * or the text string to differ between the source and target labels.  This
 * prevents magic from failing to find the label if "setlabel text" or
 * "setlabel just" is applied to the selection.  Both functions are a lousy
 * substitute for what ought to be an identifier tag. . .
 */

int
selEnumLFunc2(scx, label, tpath, arg)
    SearchContext *scx;		/* Describes current cell for search. */
    Label *label;		/* Describes label that is in right area
				 * and has right type.
				 */
    TerminalPath *tpath;	/* Ignored. */
    struct searg *arg;		/* Indicates what we're looking for. */
{
    Rect *want, got;
    int mismatch = 0;

    GeoTransRect(&scx->scx_trans, &label->lab_rect, &got);
    want = &arg->sea_label->lab_rect;
    if (want->r_xbot != got.r_xbot) return 0;
    if (want->r_ybot != got.r_ybot) return 0;
    if (want->r_xtop != got.r_xtop) return 0;
    if (want->r_ytop != got.r_ytop) return 0;
    if (arg->sea_label->lab_just != GeoTransPos(&scx->scx_trans, label->lab_just))
	mismatch++;
    if (strcmp(label->lab_text, arg->sea_label->lab_text) != 0)
	mismatch++;

    if (mismatch == 2) return 0;

    arg->sea_foundLabel = label;
    arg->sea_foundUse = scx->scx_use;
    arg->sea_foundTrans = scx->scx_trans;
    return 1;
}
