/*
 * mzHint.c --
 *
 * Builds global hint fence and rotate planes from hint info in mask data.
 *
 *     ********************************************************************* 
 *     * Copyright (C) 1988, 1990 Michael H. Arnold and the Regents of the *
 *     * University of California.                                         *
 *     * Permission to use, copy, modify, and distribute this              * 
 *     * software and its documentation for any purpose and without        * 
 *     * fee is hereby granted, provided that the above copyright          * 
 *     * notice appear in all copies.  The University of California        * 
 *     * makes no representations about the suitability of this            * 
 *     * software for any purpose.  It is provided "as is" without         * 
 *     * express or implied warranty.  Export of this software outside     * 
 *     * of the United States of America may require an export license.    * 
 *     *********************************************************************
 *
 * There are two global hint planes.  One is merged into
 * maximal horizontal strips, and the other
 * into maximal vertical strips.
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/mzrouter/mzHint.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/signals.h"
#include "textio/textio.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/malloc.h"
#include "utils/list.h"
#include "debug/debug.h"
#include "textio/textio.h"
#include "utils/heap.h"
#include "mzrouter/mzrouter.h"
#include "mzrouter/mzInternal.h"


/*
 * ----------------------------------------------------------------------------
 *
 * mzBuildHFR -- 
 *
 * Traverse cells in database, building global Hint, Fence and Rotate
 * planes.  This serves two functions:
 *     1.  It flattens the hierarchy.
 *     2.  It creates max-vertical strip versions of hint and rotate.
 *	   (VFencePlane not needed since fence info is translated into
 *	    blockages in block planes)
 * Results:
 *	None.
 *
 * Side effects:
 *	Global Hint, Fence and Rotate planes built up.
 *
 * ----------------------------------------------------------------------------
 */

void
mzBuildHFR(srcUse, area)
    CellUse *srcUse;	/* Search this cell and children for mask info */
    Rect *area;		/* Area over which planes will be built */
{
    int mzBuildHFRFunc();
    SearchContext scx;

    /* Clear global hint planes */
    {
	/* Clear Hint Planes */
	DBClearPaintPlane(mzHHintPlane);
	DBClearPaintPlane(mzVHintPlane);

	/* Clear Fence Plane 
	 * (only one plane since this info is converted to info in blockage 
	 *  planes prior to maze routing) */
	DBClearPaintPlane(mzHFencePlane);

	/* Clear Rotate Planes */
	DBClearPaintPlane(mzHRotatePlane);
	DBClearPaintPlane(mzVRotatePlane);
    }

    /* set up search context */
    scx.scx_area = *area;
    scx.scx_trans = GeoIdentityTransform;
    scx.scx_use = srcUse;

    /* clip search area to bounding box to avoid overflow during transfroms */
    GEOCLIP(&(scx.scx_area),&(srcUse->cu_def->cd_bbox));
    
    if(mzTopHintsOnly)
    /* Search the TOP LEVEL cell ONLY, processing each tile on hint plane */
    {
	(void) DBNoTreeSrTiles(&scx, 
			       &mzHintTypesMask,	
			       mzCellExpansionMask,
			       mzBuildHFRFunc,
			       (ClientData) NULL);
    }
    else
    /* Search the cell tree, processing each tile on hint plane 
     * in expanded cell.
     */
    {
    	(void) DBTreeSrTiles(&scx, 
			     &mzHintTypesMask,	
			     mzCellExpansionMask,
			     mzBuildHFRFunc, 
			     (ClientData) NULL);
    }

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzBuildHFRFunc --
 *
 * Called by DBTreeSrTiles for each solid tile found on hint planes.
 * "Copies" tiles into global hint, fence and rotate planes.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Paints into global hint, fence and rotate planes.
 *
 * ----------------------------------------------------------------------------
 */

int
mzBuildHFRFunc(tile, cxp)
    Tile *tile;
    TreeContext *cxp;
{
    SearchContext *scx = cxp->tc_scx;
    Rect r, rDest;

    /* Transform to result coordinates */
    TITORECT(tile, &r);
    GEOCLIP(&r, &scx->scx_area);
    GEOTRANSRECT(&scx->scx_trans, &r, &rDest);

    if(TiGetType(tile)==TT_MAGNET)
    {
	/* Paint into global hint planes */
	DBPaintPlane(mzHHintPlane,
	    &rDest, 
	    DBStdPaintTbl(TT_MAGNET, PL_M_HINT),
	    (PaintUndoInfo *) NULL);
	DBPaintPlaneVert(mzVHintPlane,
	    &rDest, 
	    DBStdPaintTbl(TT_MAGNET, PL_M_HINT),
	    (PaintUndoInfo *) NULL);
    }
    else if(TiGetType(tile)==TT_FENCE)
    {
	/* Paint into global fence plane
	 * (no vert plane required for fence, since fence
	 * translated to blocks in blockage planes)
	 */
	DBPaintPlane(mzHFencePlane,
	    &rDest, 
	    DBStdPaintTbl(TT_FENCE, PL_F_HINT),
	    (PaintUndoInfo *) NULL);
    }
    else 
    {
        ASSERT(TiGetType(tile)==TT_ROTATE,"mzBuildHFRFunc");

	/* Paint into global rotate planes */
	DBPaintPlane(mzHRotatePlane,
	    &rDest, 
	    DBStdPaintTbl(TT_ROTATE, PL_R_HINT),
	    (PaintUndoInfo *) NULL);
	DBPaintPlaneVert(mzVRotatePlane,
	    &rDest, 
	    DBStdPaintTbl(TT_ROTATE, PL_R_HINT),
	    (PaintUndoInfo *) NULL);
    }
   
    /* return 0 - to continue search */
    return(0);
}
