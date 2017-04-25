/* touchingtypes.c --
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
 *		      Lawrence Livermore National Laboratory
 *		      All rights reserved.
 *
 * Function to return mask of all tiletypes touching a given point. 
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/touchtypes.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"

/* ----------------------- Structs local to this file ------------------- */

typedef struct touchingfuncparms
{
    Point 		tfp_point;
    TileTypeBitMask	tfp_types;
} TouchingFuncParms;


/*
 * ----------------------------------------------------------------------------
 *
 * TouchingTypes --
 * 
 * Generate mask of all types touching or covering a given point. 
 *
 * Results:
 *	Mask of all types touching or covering a given point in cellUse or
 *      expanded subcell.  If an unexpanded subcell is 
 *      covering or touching point TT_SUBCELL is included in the result as
 *	well.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */
TileTypeBitMask 
TouchingTypes(cellUse, expansionMask, point)
    CellUse *cellUse;
    int expansionMask;
    Point *point;
{
    int touchingTypesFunc();
    int touchingSubcellsFunc();
    TouchingFuncParms parms;
	    
    /* Search unit radius rectangle around point for paint tiles
     * (in cellUse or subcells expanded in cmd window) containing or 
     * touching point
     */
    {
	SearchContext scx;

	scx.scx_area.r_ll = *point;
	scx.scx_area.r_ur = *point;
	scx.scx_area.r_ll.p_x -= 1;
	scx.scx_area.r_ll.p_y -= 1;
	scx.scx_area.r_ur.p_x += 1;
	scx.scx_area.r_ur.p_y += 1;

	scx.scx_trans = GeoIdentityTransform;
	scx.scx_use = cellUse;

	parms.tfp_point = *point;
	TTMaskZero(&(parms.tfp_types));

	DBTreeSrTiles(
		      &scx,
		      &DBAllButSpaceAndDRCBits,
		      expansionMask,
		      touchingTypesFunc,
		      (ClientData) &parms);
    }

    /* Now check for presence of unexpanded subcells */
    {
	SearchContext scx;

	scx.scx_area.r_ll = *point;
	scx.scx_area.r_ur = *point;
	scx.scx_area.r_ll.p_x -= 1;
	scx.scx_area.r_ll.p_y -= 1;
	scx.scx_area.r_ur.p_x += 1;
	scx.scx_area.r_ur.p_y += 1;

	scx.scx_trans = GeoIdentityTransform;
	scx.scx_use = cellUse;
    
	DBTreeSrCells(
		      &scx,
		      expansionMask,
		      touchingSubcellsFunc,
		      (ClientData) &parms);
    }

    return(parms.tfp_types);
}


/*
 * ---------------------------------------------------------------------
 *
 * touchingTypesFunc --
 *
 * Called by DBTreeSrTiles on behalf of touchingTypes above, to check if
 * tile touches point.
 *
 * Results:
 *      Returns 0 to continue search.
 *
 * Side effects:
 *	Sets tile type in mask if tile touches point.
 *
 * ---------------------------------------------------------------------
 */

int
touchingTypesFunc(tile, cxp)
    Tile *tile;
    TreeContext *cxp;
{
    SearchContext *scx = cxp->tc_scx;
    Rect r, rDest;
    TouchingFuncParms *parms = (TouchingFuncParms *) (cxp->tc_filter->tf_arg);

    /* Transform to result coordinates */
    TITORECT(tile, &r);
    GEOCLIP(&r, &scx->scx_area);
    GEOTRANSRECT(&scx->scx_trans, &r, &rDest);

    if(GEO_ENCLOSE(&(parms->tfp_point), &rDest))
        TTMaskSetType(&(parms->tfp_types),TiGetType(tile));

    /* return 0 to continue search */
    return(0);
}


/*
 * ---------------------------------------------------------------------
 *
 * touchingSubcellsFunc --
 *
 * Called by DBTreeSrCells on behalf of touchingTypes above, to check if
 * subcell touches point.
 *
 * Results:
 *      Returns 0 if subcell doesn't touch point (to continue search)
 *	Returns 1 on match (to terminate search)
 *
 * Side effects:
 *	Sets TT_SUBCELL in mask if subcell touches point.
 *
 * ---------------------------------------------------------------------
 */

int
touchingSubcellsFunc(scx, cdarg)
    SearchContext *scx;
    ClientData cdarg;
{
    Rect r, rDest;
    TouchingFuncParms *parms = (TouchingFuncParms *) cdarg;

    /* Transform bounding box to result coordinates */
    r = scx->scx_use->cu_def->cd_bbox;
    GEOTRANSRECT(&scx->scx_trans, &r, &rDest);

    if(GEO_ENCLOSE(&(parms->tfp_point), &rDest))
    {
	/* touching subcell found, mark in types mask, and terminate search */
        TTMaskSetType(&(parms->tfp_types),TT_SUBCELL);
	return 1;	/* 1 = abort search */
    }
    else
    {
        /* subcell doesn't touch point after all, continue search */
        return 0;	/* 0 = continue search */
    }
}
