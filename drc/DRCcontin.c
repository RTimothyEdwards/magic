/*
 * DRCcontinuous.c --
 *
 * This file provides the facilities for continuously keeping
 * design-rule violation information up to date in Magic.  It
 * records areas that need to be rechecked, and provides a
 * routine to perform those checks in background.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/drc/DRCcontin.c,v 1.7 2010/03/12 17:19:45 tim Exp $";
#endif	/* not lint */

#include <stdio.h>
#include <sys/types.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "dbwind/dbwtech.h"
#include "utils/main.h"
#include "commands/commands.h"
#include "drc/drc.h"
#include "utils/signals.h"
#include "graphics/graphics.h"
#include "utils/undo.h"
#include "utils/malloc.h"

#ifdef MAGIC_WRAPPER

/* Global variable which indicates that the background checker is 
 * still registered as an idle process.
 */

global unsigned char DRCStatus = DRC_NOT_RUNNING;

#endif

/* Global variable, settable by outside world, that enables
 * and disables the background checker.  If disabled, check
 * tiles pile up but nothing is checked.
 */

global unsigned char DRCBackGround = DRC_NOT_SET;

/* Global variable, settable by outside world, that enables
 * and disables Euclidean DRC checking.  If enabled, magic
 * computes corner-to-corner distances using a Euclidean metric.
 * If disabled, magic computes corner-to-corner distances using
 * a Manhattan metric.
 *
 * Note:  should this feature be settable from the technology
 * file, rather than from the command line?
 */

global bool DRCEuclidean = FALSE;

/* Global variable that defines the size of chunks into which
 * to decompose large DRC areas.  Also used to guarantee a
 * canonical determination of interaction areas.  May be
 * set either while reading the tech file, or automatically
 * when DRCInit() is called.
 */

/*------- Things used by other DRC modules but not outside world. -------*/

/* Base of linked list of CellDefs waiting for background DRC.
 * Can be read by outside world to see if there's any work
 * to do. */

global DRCPendingCookie * DRCPendingRoot = (DRCPendingCookie *) NULL;

/*  Pointers to yank buffer's use and def on the heap */

CellDef * DRCdef = (CellDef *) NULL;
CellUse * DRCuse = (CellUse *) NULL;

/* Boolean indicating whether DRC initialization has been done. */

bool DRCInitialized = FALSE;

/* Boolean indicating whether or not check tiles are being displayed. */

bool DRCDisplayCheckTiles = FALSE;

/* The following use is a dummy, provided because the DBCellCopyAll
 * routine requires a cellUse, and we don't generally have one.
 * As a kludge, we just keep around this dummy, with identity
 * transform and no arraying, and just change the def pointer
 * in it before calling DBCellCopyAll.
 */

CellUse *DRCDummyUse = (CellUse *) NULL;

/*------- Things used only within this module. -------*/

/* Layer mask for the DRC layers. */

TileTypeBitMask DRCLayers;

/* In order to reduce the amount of DRC redisplay, whenever an area
 * is rechecked we log all the previous violations in that area,
 * XOR them with all of the new violations in the area, and only
 * redisplay the bounding box of the changes.  The plane and paint
 * tables below are used for storing the information and doing the
 * XOR.
 */

static Plane *drcDisplayPlane;

#define DRC_SOLID 1

static PaintResultType drcXorTable[] = {DRC_SOLID, TT_SPACE};

/* When computing DRC violations, they don't get painted directly
 * into the database.  Instead, they get painted first into a
 * temporary plane.  That way, if DRC is interrupted we can exit
 * quickly without leaving error information in a weird state.
 * Once everything has been computed, then it's painted into the
 * cell in one fell swoop.
 */

static Plane *drcTempPlane;

/*  Forward declarations and imports from other drc modules: */

extern int drcCheckTile();
extern CellDef *DRCErrorDef;
extern TileType DRCErrorType;


/*
 * ----------------------------------------------------------------------------
 * DRCCheckThis --
 *
 * Mark an area of a CellDef so that it will be checked by continuous
 * DRC.  Add the CellDef to the list of Defs to be checked.  If the
 * pointer to the area rectangle is NULL, just add the CellDef to the
 * list.
 *
 * Two types of check tiles (CHECKPAINT and CHECKSUBCELL) are used
 * to distinguish areas of new paint in the CellDef from areas where
 * subcells of the CellDef have changed.  Painted areas will be
 * checked for internal consistency and their interactions with
 * subcells.  Areas where a subcell has changed will be checked
 * for interactions with paint, interactions with other subcells, and
 * correct array structure (if the subcell is used as an array).
 *
 * Called by paint, erase and cell operations in the commands module.
 * Also calls itself recursively in order to propagate check areas up
 * through a hierarchy of CellDefs and CellUses.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the PL_DRC_CHECK plane of given CellDef.  The area
 *	recorded on the plane is one halo larger than the area modified,
 *	i.e. the area where errors must be regenerated.  Inserts the
 *	CellDef into the list of Defs waiting to be checked.
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
void
DRCCheckThis (celldef, operation, area)
    CellDef * celldef;			/* Allows check areas to propagate
					 * up from EditCell.
					 */
    TileType  operation;		/* TT_CHECKPAINT or TT_CHECKSUBCELL */
    Rect    * area;			/* Area that changed. */
{
    CellUse	     * cu;		/* Ptr to uses of the given CellDef */
    Rect	       transRect;	/* Area in coords of parent CellDefs,
					 * expanded if the use is in an array
					 */
    Rect               dummyRect, dummyRect2;
    DRCPendingCookie * p, ** pback;	/* Used to insert new element in list
					 *  of CellDefs waiting for DRC
					 */

    /* Ignore read-only, internal, and vendor GDS cells.  None of these	*/
    /* can contain DRC errors that could be fixed in magic.		*/

    if (celldef->cd_flags & (CDVENDORGDS | CDNOEDIT | CDINTERNAL)) return;

    /* Insert celldef into list of Defs waiting to be checked, unless	*/
    /* it is already there.						*/

    pback = &DRCPendingRoot;
    p = DRCPendingRoot;

    while (p != (DRCPendingCookie *) NULL)
    {
	if (p->dpc_def == celldef)
	{
	    *pback = p->dpc_next;
	    break;
	}
	pback = &(p->dpc_next);
	p = p->dpc_next;
    }
    if (p == (DRCPendingCookie *) NULL)
    {
	p = (DRCPendingCookie *) mallocMagic(sizeof (DRCPendingCookie));
	p->dpc_def = celldef;
    }
    p->dpc_next = DRCPendingRoot;
    DRCPendingRoot = p;

    /* Mark the area in this celldef (but don't worry about this stuff
     * for undo purposes).  Also, it's important to disable interrupts
     * in here, or the paint operation could get aborted underneath us.
     */

    if (area != (Rect *) NULL)
    {
	GEO_EXPAND(area, DRCTechHalo, &dummyRect);

	SigDisableInterrupts();
	DBPaintPlane(celldef->cd_planes[PL_DRC_CHECK], &dummyRect,
		DBStdPaintTbl(TT_CHECKPAINT, PL_DRC_CHECK),
		(PaintUndoInfo *) NULL);
	SigEnableInterrupts();

    }
    else return;

				/*  Call recursively for each use of this
				 *  CellDef in a parent.
				 */
    for (cu = celldef->cd_parents; cu != (CellUse *) NULL; cu = cu->cu_nextuse)
    {
	if (cu->cu_parent == (CellDef *) NULL)		/* use for a Window */
	    continue;
				/* area in coordinates of the parent Def */
	GeoTransRect (&(cu->cu_transform), area, &transRect);

	if ((cu->cu_xlo != cu->cu_xhi) || (cu->cu_ylo != cu->cu_yhi))
	{
				/* transRect needs to include all images
				 * of "area" in the array, in coordinates
				 * of the parent Def
				 */
	    DBComputeArrayArea(area, cu, cu->cu_xhi, cu->cu_yhi, &dummyRect);
	    GeoTransRect(&cu->cu_transform, &dummyRect, &dummyRect2);
	    (void) GeoInclude (&dummyRect2, &transRect);
	}

	DRCCheckThis (cu->cu_parent, TT_CHECKSUBCELL, &transRect);
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * DRCRemovePending --
 *
 *	Remove a cell def from the DRC pending list.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	DRCPendingRoot linked list modified
 *
 * ----------------------------------------------------------------------------
 */

void
DRCRemovePending(def)
   CellDef *def;
{
    DRCPendingCookie *p, *plast;

    p = DRCPendingRoot;
    plast = NULL;

    while (p != NULL)
    {
	if (p->dpc_def == def)
	{
	    if (plast == NULL)
		DRCPendingRoot = p->dpc_next;
	    else
		plast->dpc_next = p->dpc_next;

	    freeMagic(p);
	    return;
	}
	plast = p;
	p = p->dpc_next;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * DRCInit --
 *
 * 	This procedure initializes DRC data.  It must be called after
 *	the technology file has been read.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Owns shared between the DRC modules are initialized.
 *
 * ----------------------------------------------------------------------------
 */

void
DRCInit()
{
    int i;
    TileTypeBitMask displayedTypes;

    /* Only do initialization once. */

    if (DRCInitialized) return;
    DRCInitialized = TRUE;

    /* Create a cell use to use for holding yank data in interaction checks. */

    DRCdef = DBCellLookDef(DRCYANK);
    if (DRCdef == (CellDef *) NULL)
    {
	DRCdef = DBCellNewDef (DRCYANK,(char *) NULL);
	ASSERT(DRCdef != (CellDef *) NULL, "DRCInit");
	DBCellSetAvail(DRCdef);
	DRCdef->cd_flags |= CDINTERNAL;
    }
    DRCuse = DBCellNewUse (DRCdef, (char *) NULL);
    DBSetTrans (DRCuse, &GeoIdentityTransform);
    DRCuse->cu_expandMask = CU_DESCEND_SPECIAL;	/* This is always expanded. */

    /* Create a dummy cell use to use for passing to procedures
     * that need a use when all we've got is a def.
     */

    DRCDummyUse = DBCellNewUse(DRCdef, (char *) NULL);
    DBSetTrans (DRCDummyUse, &GeoIdentityTransform);

    /* See if check tiles are being displayed. */

    TTMaskZero(&displayedTypes);
    for (i = 0; i < DBWNumStyles; i++)
	TTMaskSetMask(&displayedTypes, DBWStyleToTypes(i));

    DRCDisplayCheckTiles = TTMaskHasType(&displayedTypes, TT_CHECKPAINT)
			|| TTMaskHasType(&displayedTypes, TT_CHECKSUBCELL);

    /* Initialize mask of DRC layer types. */

    TTMaskZero(&DRCLayers);
    TTMaskSetType(&DRCLayers, TT_ERROR_P);
    TTMaskSetType(&DRCLayers, TT_ERROR_S);
    TTMaskSetType(&DRCLayers, TT_ERROR_PS);

    /* Create planes to hold error areas to redisplay and to hold
     * temporary error information.
     */

    drcDisplayPlane = DBNewPlane((ClientData) TT_SPACE);
    drcTempPlane = DBNewPlane((ClientData) TT_SPACE);
}

#ifdef MAGIC_WRAPPER
/* Needs to be global so the DRCBreak() routine can use the value */
Rect drc_orig_bbox;

/*
 * ----------------------------------------------------------------------------
 * DRCBreak --
 *
 *	This is called by "window" or "file" event callbacks from magic
 *	which would invalidate the current DRC process.  This routine
 *	is just a copy of the exit code for DRCContinuous.  Because Tcl
 *	doesn't have a "peek" routine for examining its event queue, we
 *	can only execute pending events and break the DRC process from
 *	the event callback code.
 *	This is called prior to running commands which do their own
 *	WindUpdate() call, so it is not necessary to call it here.
 * ----------------------------------------------------------------------------
 */

void
DRCBreak()
{
    if (DRCHasWork && (DRCStatus == DRC_IN_PROGRESS))
    {
	UndoEnable();
	
	/* fprintf(stderr, "DRC breaking. . .\n"); fflush(stderr); */

	/* As a convenience for debugging DRC stuff, we pretend the DRC
	 * cell is a real one and recompute its bounding box, and redisplay
	 * both its old area (currently in box) and its current area.
	 */

	DBReComputeBbox(DRCdef);
	(void) GeoInclude(&DRCdef->cd_bbox, &drc_orig_bbox);
	DBWAreaChanged(DRCdef, &drc_orig_bbox, DBW_ALLWINDOWS, &DBAllButSpaceBits);
	DRCStatus = DRC_BREAK_PENDING;
    }
}

#endif

/*
 * ----------------------------------------------------------------------------
 * DRCContinuous --
 *
 * Called by WindDispatch() before it goes to sleep waiting for user input.
 * This routine checks to see if there are any areas of the layout that
 * need to be design-rule-checked.  If so, it does the appropriate checks.
 * This procedure will abort itself at the earliest convenient moment
 * after the user types a new command.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the DRC_CHECK and DRC_ERROR planes 
 *	of the CellDefs on the DRCPending list.
 * ----------------------------------------------------------------------------
 */

void
DRCContinuous()
{
#ifndef MAGIC_WRAPPER
    Rect drc_orig_bbox;			/* Area of DRC def that changed. */
#endif

    if (DRCHasWork == FALSE)
    {
#ifdef MAGIC_WRAPPER
	DRCStatus = DRC_NOT_RUNNING;
#endif
	return;
    }

#ifdef MAGIC_WRAPPER
    if (DRCStatus != DRC_NOT_RUNNING) return;	/* Avoid infinitely recursive loop */
    GrFlush();
    DRCStatus = DRC_IN_PROGRESS;
    Tcl_EvalEx(magicinterp, "after idle magic::drcstate busy", -1, 0);
    if (TxInputRedirect != TX_INPUT_REDIRECTED) TxSetPrompt(']');
    /* fprintf(stderr, "Starting DRC\n"); fflush(stderr); */
#endif

    UndoDisable();			/* Don't want to undo error info. */
    drc_orig_bbox = DRCdef->cd_bbox;

    while (DRCPendingRoot != (DRCPendingCookie *) NULL)
    {
				/*  DBSrPaintArea() returns 1 if drcCheckTile()
				 *  returns 1, meaning that a CHECK tile
				 *  was found and processed.
				 */
	while ((DRCPendingRoot != (DRCPendingCookie *)NULL) &&
	    DBSrPaintArea ((Tile *) NULL,
	    DRCPendingRoot->dpc_def->cd_planes[PL_DRC_CHECK],
	    &TiPlaneRect, &DBAllButSpaceBits, drcCheckTile, (ClientData) NULL))
	{
			     /* check for new user command (without blocking) */

#ifdef MAGIC_WRAPPER
	    /* Execute pending Tcl events, so the DRC process doesn't block.	*/
	    /* NOTE:  Exclude file events, or else "drc catchup" will not work	*/
	    /* in batch mode.							*/
	    UndoEnable();
	    while (Tcl_DoOneEvent(TCL_DONT_WAIT))
	    {
		if (DRCStatus == DRC_BREAK_PENDING)
	        {
	            /* fprintf(stderr, "DRC exiting loop. . .\n"); fflush(stderr); */
		    DRCStatus = DRC_NOT_RUNNING;
		    return;
	        }
	    }
	    UndoDisable();
	    /* fprintf(stderr, "DRC continuing internally. . .\n"); fflush(stderr); */
#else
#ifndef USE_IO_PROBE
	    if (SigInterruptPending) goto checkDone;
#else
	    if (TxGetInputEvent (FALSE, FALSE) == TRUE) goto checkDone;
#endif
#endif
	}

	/* No check tiles were found, so knock this cell off the list. */

	if (DRCPendingRoot != (DRCPendingCookie *)NULL) {
	    DBReComputeBbox(DRCPendingRoot->dpc_def);
	    freeMagic((char *) DRCPendingRoot);
	    DRCPendingRoot = DRCPendingRoot->dpc_next;
	}

	/* Give the timestamp manager a chance to update any mismatches. */

	DBFixMismatch();
    }

#ifdef MAGIC_WRAPPER
    DRCStatus = DRC_NOT_RUNNING;
    Tcl_EvalEx(magicinterp, "after idle magic::drcstate idle", -1, 0);
    /* fprintf(stderr, "DRC is finished\n"); fflush(stderr); */
    if (TxInputRedirect != TX_INPUT_REDIRECTED) TxSetPrompt('%');
#endif

checkDone:

    UndoEnable();

    /* As a convenience for debugging DRC stuff, we pretend the DRC
     * cell is a real one and recompute its bounding box, and redisplay
     * both its old area (currently in box) and its current area.
     */

    DBReComputeBbox(DRCdef);
    (void) GeoInclude(&DRCdef->cd_bbox, &drc_orig_bbox);
    DBWAreaChanged(DRCdef, &drc_orig_bbox, DBW_ALLWINDOWS, &DBAllButSpaceBits);

#ifdef MAGIC_WRAPPER
    WindUpdate();
    GrFlush();
#endif
}


/*
 * ----------------------------------------------------------------------------
 * drcCheckTile --
 *
 *	This procedure is called when a check tile is found in
 *	the DRC_CHECK plane of the CellDef at the front of the
 *	DRCPending list.  This procedure is the heart of Magic's
 *	continuous checker.
 *
 *	For DRC purposes, each cell is divided up checkerboard-style
 *	into areas DRCStepSize on each side.  All checking is done
 *	in terms of these squares.  When a check tile is found, we
 *	find the outer area of all check tiles in the square containing
 *	the original check tile's lower-left corner.  Errors within
 *	this area are regenerated, then all check tiles in that area
 *	are erased.  The checkerboard approach serves three purposes.
 *	First, it allows nearby small tiles to be combined for checking
 *	purposes.  Second, it limits the maximum amount of work that
 *	is done at once, so if we're getting interrupted by new commands
 *	there is still some hope of eventually getting the DRC caught up.
 *	And third, it provides a canonical form for the checks, particularly
 *	those involving subcells, so the same results are produced no
 *	matter what the original check area is.
 *
 *	The three DRC meta-rules are:
 *		(1) paint in one CellDef must be consistent by itself,
 *			that is, without regard to subcells
 *		(2) subtrees must be consistent -- this includes both
 *			paint interacting with subcells, and subcells
 *			interacting with each other.
 *		(3) an arrayed use of a CellDef must be consistent by itself,
 *			that is, without regard to paint or other subcells
 *			in the parent.  This check happens automatically as
 *			part of the subtree check.
 *
 *	Two types of error tiles are kept independently in the
 *	DRC_ERROR plane:	(1) paint
 *				(2) subtree
 *
 *	This function is passed to DBSrPaintArea().
 *
 * Results:
 *	Always returns one.  This function is only called on non-space
 *	(CHECK) tiles, so if it is called a CHECK tile must have been
 *	processed and we want DBSrPaintArea to abort the search.
 *
 * Side effects:
 *	Modifies both DRC planes of the CellDef at the front of the
 *	DRCPending list.
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
int
drcCheckTile(tile, arg)
    Tile        * tile;			/* tile in DRC_CHECK plane */
    ClientData 	  arg;			/* Not used. */
{
    Rect square;		/* Square area of the checkerboard
				 * being processed right now.
				 */
    Rect erasebox;		/* erase old ERROR tiles in this
				 * region and clip new ERRORs to it
				 */
    CellDef * celldef;		/* First CellDef on DRCPending list. */
    Rect redisplayArea;		/* Area to be redisplayed. */
    extern int drcXorFunc();	/* Forward declarations. */
    extern int drcPutBackFunc();

    celldef = DRCPendingRoot->dpc_def;
    DRCErrorDef = celldef;

    /* Find the checkerboard square containing the lower-left corner
     * of the check tile, then find all check tiles within that square.
     */
    
    DRCstatSquares += 1;
    square.r_xbot = (LEFT(tile)/DRCStepSize) * DRCStepSize;
    if (square.r_xbot > LEFT(tile)) square.r_xbot -= DRCStepSize;
    square.r_ybot = (BOTTOM(tile)/DRCStepSize) * DRCStepSize;
    if (square.r_ybot > BOTTOM(tile)) square.r_ybot -= DRCStepSize;
    square.r_xtop = square.r_xbot + DRCStepSize;
    square.r_ytop = square.r_ybot + DRCStepSize;
    erasebox = GeoNullRect;
    (void) DBSrPaintArea((Tile *) NULL, celldef->cd_planes[PL_DRC_CHECK],
	&square, &DBAllButSpaceBits, drcIncludeArea, (ClientData) &erasebox);
    GeoClip(&erasebox, &square);

    /* TxPrintf("Check area = (%d, %d) (%d, %d)\n",
	erasebox.r_xbot, erasebox.r_ybot,
	erasebox.r_xtop, erasebox.r_ytop);
    */

    /* Use drcDisplayPlane to save all the current errors in the
     * area we're about to recheck.
     */

    DBClearPaintPlane(drcDisplayPlane);
    (void) DBSrPaintArea((Tile *) NULL, celldef->cd_planes[PL_DRC_ERROR],
	&square, &DBAllButSpaceBits, drcXorFunc, (ClientData) NULL);
    
    /* Check #1:  recheck the paint of the cell, ignoring subcells. */

    DRCErrorType = TT_ERROR_P;
    DBClearPaintPlane(drcTempPlane);

    /* May 4, 2008:  Moved DRCBasicCheck into DRCInteractionCheck
     * to avoid requiring DRC rules to be satisfied independently
     * of subcells (checkbox was [erasebox + DRCTechHalo], now
     * computed within DRCInteractionCheck()).
     */

    /* DRCBasicCheck (celldef, &checkbox, &erasebox, drcPaintError,
		(ClientData) drcTempPlane); */

    /* Check #2:  check interactions between paint and subcells, and
     * also between subcells and other subcells.  If any part of a
     * square is rechecked for interactions, the whole thing has to
     * be rechecked.  We use TT_ERROR_S tiles for this so that we
     * don't have to recheck paint and array errors over the whole
     * square.
     */

    DRCErrorType = TT_ERROR_S;
    (void) DRCInteractionCheck(celldef, &erasebox, drcPaintError,
		(ClientData) drcTempPlane);
    
    /* Check #3:  check for array formation errors in the area. */

    DRCErrorType = TT_ERROR_P;
    (void) DRCArrayCheck(celldef, &erasebox, drcPaintError,
	(ClientData) drcTempPlane);

    /* If there was an interrupt, return without modifying the cell
     * at all.
     */

    if (SigInterruptPending) return 1;

    /* Erase the check tile from the check plane, erase the pre-existing
     * error tiles, and paint back in the new error tiles.  Do this all
     * with interrupts disabled to be sure that it won't be aborted.
     */

    SigDisableInterrupts();

    DBPaintPlane(celldef->cd_planes[PL_DRC_CHECK], &erasebox,
	DBStdEraseTbl(TiGetType(tile), PL_DRC_CHECK),
	(PaintUndoInfo *) NULL);
    DBPaintPlane(celldef->cd_planes[PL_DRC_ERROR], &erasebox,
	DBStdEraseTbl(TT_ERROR_P, PL_DRC_ERROR),
	(PaintUndoInfo *) NULL);
    DBPaintPlane(celldef->cd_planes[PL_DRC_ERROR], &square,
	DBStdEraseTbl(TT_ERROR_S, PL_DRC_ERROR),
	(PaintUndoInfo *) NULL);
    (void) DBSrPaintArea((Tile *) NULL, drcTempPlane, &TiPlaneRect,
	&DBAllButSpaceBits, drcPutBackFunc, (ClientData) celldef);

    /* XOR the new errors in the tile with the old errors we
     * saved in drcDisplayPlane.  Where information has changed,
     * clip to square and redisplay.  If check tiles are being
     * displayed, then always redisplay the entire area.
     */
    
    (void) DBSrPaintArea((Tile *) NULL, celldef->cd_planes[PL_DRC_ERROR],
	&square, &DBAllButSpaceBits, drcXorFunc, (ClientData) NULL);
    if (DBBoundPlane(drcDisplayPlane, &redisplayArea))
    {
	GeoClip(&redisplayArea, &square);
	if (!GEO_RECTNULL(&redisplayArea))
	    DBWAreaChanged (celldef, &redisplayArea, DBW_ALLWINDOWS,
		&DRCLayers);
    }
    if (DRCDisplayCheckTiles)
	DBWAreaChanged(celldef, &square, DBW_ALLWINDOWS, &DRCLayers);
    DBCellSetModified (celldef, TRUE);
    SigEnableInterrupts();

    return (1);		/* stop the area search: we modified the database! */
}

/* The utility function below gets called for each error tile in a
 * region.  It just XOR's the area of the tile into drcDisplayPlane.
 */

int
drcXorFunc(tile)
    Tile *tile;
{
    Rect area;

    TiToRect(tile, &area);
    DBPaintPlane(drcDisplayPlane, &area, drcXorTable, (PaintUndoInfo *) NULL);
    return 0;
}

/* This procedure is the one that actually paints error tiles into the
 * database cells.
 */

int
drcPutBackFunc(tile, cellDef)
    Tile *tile;			/* Error tile, from drcTempPlane. */
    CellDef *cellDef;		/* Celldef in which to paint error. */
{
    Rect area;

    TiToRect(tile, &area);
    DBPaintPlane(cellDef->cd_planes[PL_DRC_ERROR], &area,
	DBStdPaintTbl(TiGetType(tile), PL_DRC_ERROR),
	(PaintUndoInfo *) NULL);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * drcIncludeArea --
 *
 * 	This is a drc utility procedure called by DBSrPaintArea.  It
 *	merely computes the total area of non-space tiles in the
 *	given area of the plane.  It is only called for non-space
 *	tiles.
 *
 * Results:
 *	Always returns 0 so the search continues.
 *
 * Side effects:
 *	The client data must be a pointer to a rectangle.  The
 *	rectangle is enlarged to include the area of this tile.
 *
 * ----------------------------------------------------------------------------
 */

int
drcIncludeArea(tile, rect)
    Tile *tile;
    Rect *rect;			/* Rectangle in which to record total area. */
{
    Rect dum;

    TiToRect(tile, &dum);
    (void) GeoInclude(&dum, rect);
    return 0;
}
