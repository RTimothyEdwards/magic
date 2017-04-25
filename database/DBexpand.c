/*
 * DBexpand.c --
 *
 * Expansion and unexpansion of cells
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/database/DBexpand.c,v 1.3 2010/08/15 14:35:47 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "database/databaseInt.h"
#include "textio/textio.h"
#include "utils/utils.h"
#include "utils/stack.h"

    /*
     * Argument passed down to search functions when searching for
     * cells to expand or unexpand.
     */
struct expandArg
{
    int		ea_xmask;	/* Expand mask. */
    int		(*ea_func)();	/* Function to call for each cell whose
				 * status is changed.
				 */
    ClientData	ea_arg;		/* Argument to pass to func. */
};

/*
 * ----------------------------------------------------------------------------
 *
 * DBExpand --
 *
 * Expand/unexpand a single CellUse.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If expandFlag is TRUE, sets all the bits of expandMask in
 *	the flags of the given cellUse expandFlag is FALSE, clears
 *	all bits of expandMask.
 *
 *	If expandFlag is TRUE and the cell being expanded has not
 *	been read in, reads it in from disk.
 *
 * ----------------------------------------------------------------------------
 */

void
DBExpand(cellUse, expandMask, expandFlag)
    CellUse *cellUse;
    int expandMask;
    bool expandFlag;
{
    CellDef *def;

    if (DBDescendSubcell(cellUse, expandMask) == expandFlag)
	return;

    if (expandFlag)
    {
	def = cellUse->cu_def;
	if ((def->cd_flags & CDAVAILABLE) == 0)
	{
	    if (!DBCellRead(def, (char *) NULL, TRUE, NULL))
		return;
	    /* Note:  we don't have to recompute the bbox here, because
	     * if it changed, then a timestamp violation must have occurred
	     * and the timestamp manager will take care of recomputing
	     * the bbox.
	     */
	}
	cellUse->cu_expandMask |= expandMask;
    }
    else
	cellUse->cu_expandMask &= ~expandMask;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBExpandAll --
 *
 * Recursively expand/unexpand all cells which intersect or are
 * contained within the given rectangle.  Furthermore, if func is
 * non-NULL, it is invoked for each cell whose status has changed,
 * just after the change has been made.  The calling sequence is
 *
 *     int
 *     func(cellUse, cdarg)
 *	   CellUse *cellUse;
 *	   ClientData cdarg;
 *     {
 *     }
 *
 * In the calls to func, cellUse is the use whose expand bit has just
 * been changed, and cdarg is the argument that the caller gave to us.
 * Func should normally return 0.  If it returns a non-zero value, then
 * the call terminates immediately and no more cells are expanded.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If expandFlag is TRUE, sets all the bits specified by
 *	expandMask in the flags of each CellUse found to intersect
 *	the given rectangle.  If expandFlag is FALSE, clears all bits
 *	of expandMask.
 * ----------------------------------------------------------------------------
 */

void
DBExpandAll(rootUse, rootRect, expandMask, expandFlag, func, cdarg)
    CellUse *rootUse;	/* Root cell use from which search begins */
    Rect *rootRect;	/* Area to be expanded, in root coordinates */
    int expandMask;	/* Window mask in which cell is to be expanded */
    bool expandFlag;	/* TRUE => expand, FALSE => unexpand */
    int (*func)();	/* Function to call for each cell whose expansion
			 * status is modified.  NULL means don't call anyone.
			 */
    ClientData cdarg;	/* Argument to pass to func. */
{
    int dbExpandFunc(), dbUnexpandFunc();
    SearchContext scontext;
    struct expandArg arg;

    if ((rootUse->cu_def->cd_flags & CDAVAILABLE) == 0)
	(void) DBCellRead(rootUse->cu_def, (char *) NULL, TRUE, NULL);

    /*
     * Walk through the area and set the expansion state
     * appropriately.
     */

    arg.ea_xmask = expandMask;
    arg.ea_func = func;
    arg.ea_arg = cdarg;

    scontext.scx_use = rootUse;
    scontext.scx_trans = GeoIdentityTransform;
    scontext.scx_area = *rootRect;
    if (expandFlag)
	DBCellSrArea(&scontext, dbExpandFunc, (ClientData) &arg);
    else
	DBCellSrArea(&scontext, dbUnexpandFunc, (ClientData) &arg);
}

/*
 * dbExpandFunc --
 *
 * Filter function called by DBCellSrArea on behalf of DBExpandAll above
 * when cells are being expanded.
 */

int
dbExpandFunc(scx, arg)
    SearchContext *scx;	/* Pointer to search context containing
					 * child use, search area in coor-
					 * dinates of the child use, and
					 * transform back to "root".
					 */
    struct expandArg *arg;	/* Client data from caller */
{
    CellUse *childUse = scx->scx_use;
    int n = DBLambda[1];

    /*
     * Change the expansion status of this cell if necessary.  Call the
     * client's function if the expansion status has changed.
     */

    if (!DBDescendSubcell(childUse, arg->ea_xmask))
    {
	/* If the cell is unavailable, then don't expand it.
	 */
	if ((childUse->cu_def->cd_flags & CDAVAILABLE) == 0)
	    if(!DBCellRead(childUse->cu_def, (char *) NULL, TRUE, NULL))
	    {
		TxError("Cell %s is unavailable.  It could not be expanded.\n",
			childUse->cu_def->cd_name);
		return 2;
	    }

	childUse->cu_expandMask |= arg->ea_xmask;
	if (arg->ea_func != NULL)
	{
	    if ((*arg->ea_func)(childUse, arg->ea_arg) != 0) return 1;
	}
    }

    if (DBCellSrArea(scx, dbExpandFunc, (ClientData) arg))
	return 1;
    return 2;
}

/*
 * dbUnexpandFunc --
 *
 * Filter function called by DBCellSrArea on behalf of DBExpandAll above
 * when cells are being unexpanded.
 */

int
dbUnexpandFunc(scx, arg)
    SearchContext *scx;	/* Pointer to search context containing
					 * child use, search area in coor-
					 * dinates of the child use, and
					 * transform back to "root".
					 */
    struct expandArg *arg;	/* Client data from caller */
{
    CellUse *childUse = scx->scx_use;

    /*
     * Change the expansion status of this cell if necessary.
     */

    if (DBDescendSubcell(childUse, arg->ea_xmask))
    {
	if (!GEO_SURROUND(&childUse->cu_def->cd_bbox, &scx->scx_area)
	    || GEO_SURROUND(&scx->scx_area, &childUse->cu_def->cd_bbox))
	{
	    childUse->cu_expandMask &= ~arg->ea_xmask;

	    /* Call the client's function, if there is one. */

	    if (arg->ea_func != NULL)
	    {
		if ((*arg->ea_func)(childUse, arg->ea_arg) != 0) return 1;
	    }
	}
    }

    /* Don't recursively search things that aren't already expanded. */

    else return 2;

    if (DBCellSrArea(scx, dbUnexpandFunc, (ClientData) arg))
	return 1;
    return 2;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBCellReadArea --
 *
 * Recursively read all cells which intersect or are contained within
 * the given rectangle.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May make new cells known to the database.  Sets the CDAVAILABLE
 *	bit in all cells intersecting the search area.
 *
 * ----------------------------------------------------------------------------
 */

void
DBCellReadArea(rootUse, rootRect)
    CellUse *rootUse;	/* Root cell use from which search begins */
    Rect *rootRect;	/* Area to be read, in root coordinates */
{
    int dbReadAreaFunc();
    SearchContext scontext;

    scontext.scx_use = rootUse;
    scontext.scx_trans = GeoIdentityTransform;
    scontext.scx_area = *rootRect;
    (void) dbReadAreaFunc(&scontext);
}

int
dbReadAreaFunc(scx)
    SearchContext *scx;	/* Pointer to context specifying
					 * the cell use to be read in, and
					 * an area to be recursively read in
					 * coordinates of the cell use's def.
					 */
{
    CellDef *def = scx->scx_use->cu_def;

    if ((def->cd_flags & CDAVAILABLE) == 0)
    {
	(void) DBCellRead(def, (char *) NULL, TRUE, NULL);
	/* Note: we don't have to invoke DBReComputeBbox here because
	 * if the bbox changed then there was a timestamp mismatch and
	 * the timestamp code will take care of the bounding box later.
	 */
    }

    (void) DBCellSrArea(scx, dbReadAreaFunc, (ClientData) NULL);

    /* Be clever about handling arrays:  if the search area covers this
     * whole definition, then there's no need to look at any other
     * array elements, since we've already expanded the entire area
     * of the definition.
     */

    if (GEO_SURROUND(&scx->scx_area, &scx->scx_use->cu_def->cd_bbox))
	return 2;
    return 0;
}
