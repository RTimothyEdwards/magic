/*
 * DRCarray.c --
 *
 * This file provides routines that check arrays to be sure
 * there are no unpleasant interactions between adjacent
 * elements.  Note:  the routines in this file are NOT generally
 * re-entrant.
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

#ifndef	lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/drc/DRCarray.c,v 1.2 2009/05/01 18:59:44 tim Exp $";
#endif	/* not lint */

#include <sys/types.h>
#include <stdio.h>
#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "drc/drc.h"
#include "windows/windows.h"
#include "commands/commands.h"

/* Forward references: */

extern int drcArrayYankFunc(), drcArrayOverlapFunc();

/* Dummy DRC cookie used to pass the error message to DRC error
 * routines.
 */

static DRCCookie drcArrayCookie = {
    0, 0, 0, 0,
    { 0 }, { 0 },
    0, 0, 0,
    "This layer can't abut or partially overlap between array elements",
    (DRCCookie *) NULL
};

/* Static variables used to pass information between DRCArrayCheck
 * and drcArrayFunc:
 */

static int drcArrayCount;		/* Count of number of errors found. */
static void (*drcArrayErrorFunc)();	/* Function to call on violations. */
static ClientData drcArrayClientData;	/* Extra parameter to pass to func. */


/*
 * ----------------------------------------------------------------------------
 *
 * drcArrayFunc --
 *
 * 	This procedure is invoked by DBCellSrArea once for each cell
 *	overlapping the area being checked.  If the celluse is for
 *	an array, then it is checked for array correctness.
 *
 * Results:
 *	Always returns 2, to skip the remaining instances in the
 *	current array.
 *
 * Side effects:
 *	Design rules are checked for the subcell, if it is an array,
 *	and the count of errors is added into drcSubCount.
 *
 * Design:
 *	To verify that an array is correct, we only have to check
 *	four interaction areas, shaded as A, B, C, and D in the diagram
 *	below.  The exact size of the interaction areas depends on
 *	how much overlap there is.  In the extreme cases, there may be
 *	no areas to check at all (instances widely separated), or there
 *	may even be areas with more than four instances overlapping
 *	(spacing less than half the size of the instance).
 *
 * 	--------------DDDDD------------------------------
 *	|             DDDDD             |               |
 *	|               |               |               |
 *	|               |               |               |
 *	|               |               |               |
 * 	-------------------------------------------------
 *	|               |               |               |
 *	|               |               |               |
 *	|               |               |               |
 *	AAAAAAAAAAAAAAAAAAA             |             CCC
 * 	AAAAAAAAAAAAAAAAAAA---------------------------CCC
 *	AAAAAAAAAAAAAAAAAAA             |             CCC
 *	|             BBBBB             |               |
 *	|             BBBBB             |               |
 *	|             BBBBB             |               |
 * 	--------------BBBBB------------------------------
 *
 * ----------------------------------------------------------------------------
 */

int
drcArrayFunc(scx, area)
    SearchContext *scx;		/* Information about the search. */
    Rect *area;			/* Area in which errors are to be
				 * regenerated.
				 */
{
    int xsep, ysep;
    int xsize, ysize;
    Rect errorArea, yankArea, tmp, tmp2;
    CellUse *use = scx->scx_use;
    struct drcClientData arg;

    if ((use->cu_xlo == use->cu_xhi) && (use->cu_ylo == use->cu_yhi))
	return 2;
    
    /* Set up the client data that will be passed down during
     * checks for exact overlaps.
     */
    
    arg.dCD_celldef = DRCdef;
    arg.dCD_errors = &drcArrayCount;
    arg.dCD_clip = &errorArea;
    arg.dCD_cptr = &drcArrayCookie;
    arg.dCD_function = drcArrayErrorFunc;
    arg.dCD_clientData = drcArrayClientData;

    /* Compute the sizes and separations of elements, in coordinates
     * of the parend.  If the array is 1-dimensional, we set the
     * corresponding spacing to an impossibly large distance.
     */
    
    tmp.r_xbot = 0;
    tmp.r_ybot = 0;
    if (use->cu_xlo == use->cu_xhi)
	tmp.r_xtop = DRCTechHalo + use->cu_def->cd_bbox.r_xtop
	    - use->cu_def->cd_bbox.r_xbot;
    else tmp.r_xtop = use->cu_xsep;
    if (use->cu_ylo == use->cu_yhi)
	tmp.r_ytop = DRCTechHalo + use->cu_def->cd_bbox.r_ytop
	    - use->cu_def->cd_bbox.r_ybot;
    else tmp.r_ytop = use->cu_ysep;
    GeoTransRect(&use->cu_transform, &tmp, &tmp2);
    xsep = tmp2.r_xtop - tmp2.r_xbot;
    ysep = tmp2.r_ytop - tmp2.r_ybot;
    GeoTransRect(&use->cu_transform, &use->cu_def->cd_bbox, &tmp2);
    xsize = tmp2.r_xtop - tmp2.r_xbot;
    ysize = tmp2.r_ytop - tmp2.r_ybot;

    /* Check each of the four areas A, B, C, and D.  Remember that
     * absolutely arbitrary overlaps between cells are allowed.
     * Skip some or all of the areas if the cell isn't arrayed in
     * that direction or if the instances are widely spaced.
     */
    
    if (ysep < ysize + DRCTechHalo)
    {
	/* A */

	errorArea.r_xbot = use->cu_bbox.r_xbot;
	errorArea.r_xtop = use->cu_bbox.r_xbot + xsize + DRCTechHalo;
	errorArea.r_ybot = use->cu_bbox.r_ybot + ysep - DRCTechHalo;
	errorArea.r_ytop = use->cu_bbox.r_ybot + ysize + DRCTechHalo;
	GeoClip(&errorArea, area);
	if (!GEO_RECTNULL(&errorArea))
	{
	    GEO_EXPAND(&errorArea, DRCTechHalo, &yankArea);
	    DBCellClearDef(DRCdef);
	    (void) DBArraySr(use, &yankArea, drcArrayYankFunc,
		(ClientData) &yankArea);
	    drcArrayCount += DRCBasicCheck(DRCdef, &yankArea, &errorArea,
		drcArrayErrorFunc, drcArrayClientData);
	    (void) DBArraySr(use, &errorArea, drcArrayOverlapFunc,
		(ClientData) &arg);
	}

	/* C */

	errorArea.r_xtop = use->cu_bbox.r_xtop;
	errorArea.r_xbot = use->cu_bbox.r_xtop - DRCTechHalo;
	GeoClip(&errorArea, area);
	if (!GEO_RECTNULL(&errorArea))
	{
	    GEO_EXPAND(&errorArea, DRCTechHalo, &yankArea);
	    DBCellClearDef(DRCdef);
	    (void) DBArraySr(use, &yankArea, drcArrayYankFunc,
		(ClientData) &yankArea);
	    drcArrayCount += DRCBasicCheck(DRCdef, &yankArea, &errorArea,
		drcArrayErrorFunc, drcArrayClientData);
	    (void) DBArraySr(use, &errorArea, drcArrayOverlapFunc,
		(ClientData) &arg);
	}
    }

    if (xsep < xsize + DRCTechHalo)
    {
	/* B */

	errorArea.r_xbot = use->cu_bbox.r_xbot + xsep - DRCTechHalo;
	errorArea.r_xtop = use->cu_bbox.r_xbot + xsize + DRCTechHalo;
	errorArea.r_ybot = use->cu_bbox.r_ybot;
	errorArea.r_ytop = errorArea.r_ybot + ysep - DRCTechHalo;
	GeoClip(&errorArea, area);
	if (!GEO_RECTNULL(&errorArea))
	{
	    GEO_EXPAND(&errorArea, DRCTechHalo, &yankArea);
	    DBCellClearDef(DRCdef);
	    (void) DBArraySr(use, &yankArea, drcArrayYankFunc,
		(ClientData) &yankArea);
	    drcArrayCount += DRCBasicCheck(DRCdef, &yankArea, &errorArea,
		drcArrayErrorFunc, drcArrayClientData);
	    (void) DBArraySr(use, &errorArea, drcArrayOverlapFunc,
		(ClientData) &arg);
	}

	/* D */

	errorArea.r_ytop = use->cu_bbox.r_ytop;
	errorArea.r_ybot = use->cu_bbox.r_ytop - DRCTechHalo;
	GeoClip(&errorArea, area);
	if (!GEO_RECTNULL(&errorArea))
	{
	    GEO_EXPAND(&errorArea, DRCTechHalo, &yankArea);
	    DBCellClearDef(DRCdef);
	    (void) DBArraySr(use, &yankArea, drcArrayYankFunc,
		(ClientData) &yankArea);
	    drcArrayCount += DRCBasicCheck(DRCdef, &yankArea, &errorArea,
		drcArrayErrorFunc, drcArrayClientData);
	    (void) DBArraySr(use, &errorArea, drcArrayOverlapFunc,
		(ClientData) &arg);
	}
    }
    
    return 2;
}

/*
 * ----------------------------------------------------------------------------
 * DRCArrayCheck --
 *
 *	This procedure finds all DRC errors in a given area of
 *	a given cell that stem from array formation errors in
 *	children of that cell.  Func is called for each violation
 *	found.  Func should have the same form as in DRCBasicCheck.
 *	Note: the def passed to func is the dummy DRC definition,
 *	and the errors are all expressed in coordinates of celluse.
 *
 * Results:
 *	The number of errors found.
 *
 * Side effects:
 *      Whatever is done by func.
 *
 * ----------------------------------------------------------------------------
 */

int
DRCArrayCheck(def, area, func, cdarg)
    CellDef *def;		/* Parent cell containing the arrays to
				 * be rechecked.
				 */
    Rect *area;			/* Area, in def's coordinates, where all
				 * array violations are to be regenerated.
				 */
    void (*func)();		/* Function to call for each error. */
    ClientData cdarg;		/* Client data to be passed to func. */

{
    SearchContext scx;
    int oldTiles;
    PaintResultType (*savedPaintTable)[NT][NT];
    PaintResultType (*savedEraseTable)[NT][NT];
    void (*savedPaintPlane)();

    /* Use DRCDummyUse to fake up a celluse for searching purposes. */

    DRCDummyUse->cu_def = def;

    drcArrayErrorFunc = func;
    drcArrayClientData = cdarg;
    drcArrayCount = 0;
    oldTiles = DRCstatTiles;

    scx.scx_area = *area;
    scx.scx_use = DRCDummyUse;
    scx.scx_trans = GeoIdentityTransform;

    /* During array processing, switch the paint table to catch
     * illegal overlaps.
     */

    savedPaintTable = DBNewPaintTable(DRCCurStyle->DRCPaintTable);
    savedPaintPlane = DBNewPaintPlane(DBPaintPlaneMark);
    (void) DBCellSrArea(&scx, drcArrayFunc, (ClientData) area);
    (void) DBNewPaintTable(savedPaintTable);
    (void) DBNewPaintPlane(savedPaintPlane);

    /* Update count of array tiles processed. */

    DRCstatArrayTiles += DRCstatTiles - oldTiles;
    return drcArrayCount;
}


/*
 * ----------------------------------------------------------------------------
 *
 * drcArrayYankFunc --
 *
 * 	Search action function called while yanking pieces of an array.
 *
 * Results:
 *	Always returns 0, to keep the search going.
 *
 * Side effects:
 *	Yanks from an array element into the DRC yank buffer.
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
int
drcArrayYankFunc(use, transform, x, y, yankArea)
    CellUse *use;			/* CellUse being array-checked. */
    Transform *transform;		/* Transform from instance to parent.*/
    int x, y;				/* Element indices (not used). */
    Rect *yankArea;			/* Area to yank (in parent coords). */

{
    SearchContext scx;
    Transform tinv;

    GeoInvertTrans(transform, &tinv);
    GeoTransRect(&tinv, yankArea, &scx.scx_area);
    scx.scx_use = use;
    scx.scx_trans = *transform;
    (void) DBCellCopyAllPaint(&scx, &DBAllButSpaceBits, 0, DRCuse);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * drcArrayOverlapFunc --
 *
 * 	This is a search action function called while checking pieces
 *	of an array to be sure that there aren't any illegal partial
 *	overlaps.  It just invokes overlap checking facilities in
 *	DRCsubcell.c
 *
 * Results:
 *	Always returns 0 to keep the search alive.
 *
 * Side effects:
 *	The client's error function may be invoked.
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
int
drcArrayOverlapFunc(use, transform, x, y, arg)
    CellUse *use;		/* CellUse for array element. */
    Transform *transform;	/* Transform from use to parent. */
    int x, y;			/* Indices of element. */
    struct drcClientData *arg;	/* Information used in overlap
				 * checking.  See drcExactOverlapTile.
				 */
{
    Transform tinv;
    SearchContext scx;

    GeoInvertTrans(transform, &tinv);
    GeoTransRect(&tinv, arg->dCD_clip, &scx.scx_area);
    scx.scx_use = use;
    scx.scx_trans = *transform;
    (void) DBTreeSrTiles(&scx, &DRCCurStyle->DRCExactOverlapTypes, 0,
	drcExactOverlapTile, (ClientData) arg);
    return 0;
}
