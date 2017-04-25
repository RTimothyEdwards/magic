/*
 * DRCmain.c --
 *
 * This file provides global routines that are invoked at
 * command-level.  They do things like give information about
 * errors and print statistics.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/drc/DRCmain.c,v 1.4 2010/06/24 12:37:16 tim Exp $";
#endif	/* not lint */

#include <sys/types.h>
#include <stdio.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/malloc.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "drc/drc.h"
#include "utils/undo.h"

/* The global variables defined below are parameters between
 * the DRC error routines (drcPaintError and drcPrintError)
 * and the higher-level routines that start up DRC error checks.
 * It seemed simpler to do the communication this way rather
 * than creating a special new record that is passed down as
 * ClientData.  Any routine invoking DRCBasicCheck with drcPaintError
 * or drcPrintError as action routine should fill in the relevant
 * variables.
 */

/* The following points to a list of all the DRC styles currently
 * understood:
 */

DRCKeep *DRCStyleList = NULL;

DRCStyle *DRCCurStyle = NULL;

/* Used by both routines: */

int DRCErrorCount;		/* Incremented by each call to either routine.
				 */

/* Used by drcPaintError: */

CellDef *DRCErrorDef;		/* Place to paint error tiles. */
TileType DRCErrorType;		/* Type of error tile to paint. */

/* Used by drcPrintError: */

HashTable DRCErrorTable;	/* Hash table used to eliminate duplicate
				 * error strings.
				 */

/* Global variables used by all DRC modules to record statistics.
 * For each statistic we keep two values, the count since stats
 * were last printed (in DRCstatXXX), and the total count (in
 * drcTotalXXX).
 */

int DRCstatSquares = 0;		/* Number of DRCStepSize-by-DRCStepSize
				 * squares processed by continuous checker.
				 */
int DRCstatTiles = 0;		/* Number of tiles processed by basic
				 * checker.
				 */
int DRCstatEdges = 0;		/* Number of "atomic" edges processed
				 * by basic checker.
				 */
int DRCstatRules = 0;		/* Number of rules processed by basic checker
				 * (rule = one constraint for one edge).
				 */
int DRCstatSlow = 0;		/* Number of places where constraint doesn't
				 * all fall in a single tile.
				 */
int DRCstatInteractions = 0;	/* Number of times drcInt is called to check
				 * an interaction area.
				 */
int DRCstatIntTiles = 0;	/* Number of tiles processed as part of
				 * subcell interaction checks.
				 */
int DRCstatCifTiles = 0;	/* Number of tiles processed as part of
				 * cif checks.
				 */
int DRCstatArrayTiles = 0;	/* Number of tiles processed as part of
				 * array interaction checks.
				 */

#ifdef	DRCRULESHISTO
int DRCstatVRulesHisto[DRC_MAXRULESHISTO];
int DRCstatHRulesHisto[DRC_MAXRULESHISTO];
#endif	/* DRCRULESHISTO */

static int drcTotalSquares = 0;
static int drcTotalTiles = 0;
static int drcTotalEdges = 0;
static int drcTotalRules = 0;
static int drcTotalSlow = 0;
static int drcTotalInteractions = 0;
static int drcTotalIntTiles = 0;
static int drcTotalArrayTiles = 0;

#ifdef	DRCRULESHISTO
static int drcTotalVRulesHisto[DRC_MAXRULESHISTO];
static int drcTotalHRulesHisto[DRC_MAXRULESHISTO];
#endif	/* DRCRULESHISTO */


/*
 * ----------------------------------------------------------------------------
 * drcPaintError --
 *
 * Action function that paints error tiles for each violation found.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A tile of type DRCErrorType is painted over the area of
 *	the error, in the plane given by "plane".  Also, DRCErrorCount
 *	is incremented.
 * ----------------------------------------------------------------------------
 */

void
drcPaintError(celldef, rect, cptr, plane)
    CellDef   * celldef;		/* CellDef being checked */
    Rect      * rect;			/* Area of error */
    DRCCookie * cptr;  			/* Design rule violated -- not used */
    Plane     * plane;			/* Where to paint error tiles. */
{
    PaintUndoInfo ui;

    ui.pu_def = celldef;
    ui.pu_pNum = PL_DRC_ERROR;
    DBPaintPlane(plane, rect, DBStdPaintTbl(DRCErrorType,
	PL_DRC_ERROR), &ui);
    DRCErrorCount += 1;
}


/*
 * ----------------------------------------------------------------------------
 * drcPrintError --
 *
 * Action function that prints the error message associated with each
 * violation found.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	DRCErrorCount is incremented.  The text associated with
 *	the error is entered into DRCErrorTable, and, if this is
 *	the first time that entry has been seen, then the error
 *	text is printed.  If the area parameter is non-NULL, then
 *	only errors intersecting that area are considered.
 * ----------------------------------------------------------------------------
 */

void
drcPrintError (celldef, rect, cptr, scx)
    CellDef   * celldef;	/* CellDef being checked -- not used here */
    Rect      * rect;		/* Area of error */
    DRCCookie * cptr;  		/* Design rule violated */
    SearchContext * scx;	/* Only errors in scx->scx_area get reported. */
{
    HashEntry *h;
    int i;
    Rect *area, r;

    ASSERT (cptr != (DRCCookie *) NULL, "drcPrintError");

    area = &scx->scx_area;
    if ((area != NULL) && (!GEO_OVERLAP(area, rect))) return;
    DRCErrorCount += 1;
    h = HashFind(&DRCErrorTable, cptr->drcc_why);
    i = (spointertype) HashGetValue(h);
    if (i == 0)
	TxPrintf("%s\n", cptr->drcc_why);
    i += 1;
    HashSetValue(h, (spointertype)i);
}

/* Same routine as above, but output goes to a Tcl list and is appended	*/
/* to the interpreter result.						*/

#ifdef MAGIC_WRAPPER

void
drcListError (celldef, rect, cptr, scx)
    CellDef   * celldef;	/* CellDef being checked -- not used here */
    Rect      * rect;		/* Area of error */
    DRCCookie * cptr;  		/* Design rule violated */
    SearchContext * scx;	/* Only errors in scx->scx_area get reported */
{
    HashEntry *h;
    int i;
    Rect *area;

    ASSERT (cptr != (DRCCookie *) NULL, "drcListError");

    area = &scx->scx_area;
    if ((area != NULL) && (!GEO_OVERLAP(area, rect))) return;
    DRCErrorCount += 1;
    h = HashFind(&DRCErrorTable, cptr->drcc_why);
    i = (spointertype) HashGetValue(h);
    if (i == 0)
    {
	Tcl_Obj *lobj;
	lobj = Tcl_GetObjResult(magicinterp);
	Tcl_ListObjAppendElement(magicinterp, lobj,
			Tcl_NewStringObj(cptr->drcc_why, -1));
	Tcl_SetObjResult(magicinterp, lobj);
    }
    i += 1;
    HashSetValue(h, (spointertype)i);
}

/* Same routine as above, but output for every single error is recorded	*/
/* along with position information.					*/

void
drcListallError (celldef, rect, cptr, scx)
    CellDef   * celldef;	/* CellDef being checked -- not used here */
    Rect      * rect;		/* Area of error */
    DRCCookie * cptr;  		/* Design rule violated */
    SearchContext * scx;	/* Only errors in scx->scx_area get reported. */
{
    Tcl_Obj *lobj, *pobj;
    HashEntry *h;
    Rect *area, r;

    ASSERT (cptr != (DRCCookie *) NULL, "drcListallError");

    // Report in top-level coordinates
    GeoTransRect(&scx->scx_trans, rect, &r);
    area = &scx->scx_area;
    if ((area != NULL) && (!GEO_OVERLAP(area, rect))) return;
    DRCErrorCount += 1;
    h = HashFind(&DRCErrorTable, cptr->drcc_why);
    lobj = (Tcl_Obj *) HashGetValue(h);
    if (lobj == NULL)
       lobj = Tcl_NewListObj(0, NULL);
    
    pobj = Tcl_NewListObj(0, NULL);

    Tcl_ListObjAppendElement(magicinterp, pobj, Tcl_NewIntObj(r.r_xbot));
    Tcl_ListObjAppendElement(magicinterp, pobj, Tcl_NewIntObj(r.r_ybot));
    Tcl_ListObjAppendElement(magicinterp, pobj, Tcl_NewIntObj(r.r_xtop));
    Tcl_ListObjAppendElement(magicinterp, pobj, Tcl_NewIntObj(r.r_ytop));
    Tcl_ListObjAppendElement(magicinterp, lobj, pobj);

    HashSetValue(h, lobj);
}

#else

#define drcListError drcPrintError

#endif

/*
 * ----------------------------------------------------------------------------
 *
 * DRCPrintStats --
 *
 * 	Prints out statistics gathered by the DRC checking routines.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Statistics are printed.  Two values are printed for each
 *	statistic:  the number since statistics were last printed,
 *	and the total to date.	The own variables used to keep
 *	track of statistics are updated.
 *
 * ----------------------------------------------------------------------------
 */

void
DRCPrintStats()
{
#ifdef	DRCRULESHISTO
    int n;
#endif	/* DRCRULESHISTO */

    TxPrintf("Design-rule checker statistics (recent/total):\n");
    drcTotalSquares += DRCstatSquares;
    TxPrintf("    Squares processed: %d/%d\n", DRCstatSquares,
	drcTotalSquares);
    DRCstatSquares = 0;
    drcTotalTiles += DRCstatTiles;
    TxPrintf("    Tiles processed: %d/%d\n", DRCstatTiles, drcTotalTiles);
    DRCstatTiles = 0;
    drcTotalEdges += DRCstatEdges;
    TxPrintf("    Edges pieces processed: %d/%d\n", DRCstatEdges,
	drcTotalEdges);
    DRCstatEdges = 0;
    drcTotalRules += DRCstatRules;
    TxPrintf("    Constraint areas checked: %d/%d\n", DRCstatRules,
	drcTotalRules);
    DRCstatRules = 0;
    drcTotalSlow += DRCstatSlow;
    TxPrintf("    Multi-tile constraints: %d/%d\n", DRCstatSlow,
	drcTotalSlow);
    DRCstatSlow = 0;
    drcTotalInteractions += DRCstatInteractions;
    TxPrintf("    Interaction areas processed: %d/%d\n",
	DRCstatInteractions, drcTotalInteractions);
    DRCstatInteractions = 0;
    drcTotalIntTiles += DRCstatIntTiles;
    TxPrintf("    Tiles processed for interactions: %d/%d\n",
	DRCstatIntTiles, drcTotalIntTiles);
    DRCstatIntTiles = 0;
    drcTotalArrayTiles += DRCstatArrayTiles;
    TxPrintf("    Tiles processed for arrays: %d/%d\n",
	DRCstatArrayTiles, drcTotalArrayTiles);
    DRCstatArrayTiles = 0;

#ifdef	DRCRULESHISTO
    TxPrintf("    Number of rules applied per edge:\n");
    TxPrintf("    # rules         Horiz freq            Vert freq\n");
    for (n = 0; n < DRC_MAXRULESHISTO; n++)
    {
	drcTotalHRulesHisto[n] += DRCstatHRulesHisto[n];
	drcTotalVRulesHisto[n] += DRCstatVRulesHisto[n];
	if (drcTotalHRulesHisto[n] == 0 && drcTotalVRulesHisto[n] == 0)
	    continue;
	TxPrintf("      %3d      %10d/%10d  %10d/%10d\n",
		n,
		DRCstatHRulesHisto[n], drcTotalHRulesHisto[n],
		DRCstatVRulesHisto[n], drcTotalVRulesHisto[n]);
	DRCstatHRulesHisto[n] = DRCstatVRulesHisto[n] = 0;
    }
#endif	/* DRCRULESHISTO */
}

/*
 * ----------------------------------------------------------------------------
 *
 * DRCWhy --
 *
 * 	This procedure finds all errors within an area and prints messages
 *	about each distinct kind of violation found.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None, except that error messages are printed.  The given
 *	area is DRC'ed for both paint and subcell violations in every
 *	cell of def's tree that it intersects.
 *
 * ----------------------------------------------------------------------------
 */

void
DRCWhy(dolist, use, area)
    bool dolist;			/*
					 * Generate Tcl list for value
					 */
    CellUse *use;			/* Use in whose definition to start
					 * the hierarchical check.
					 */
    Rect *area;				/* Area, in def's coordinates, that
					 * is to be checked.
					 */
{
    SearchContext scx;
    Rect box;
    extern int drcWhyFunc();		/* Forward reference. */

    /* Create a hash table to used for eliminating duplicate messages. */

    HashInit(&DRCErrorTable, 16, HT_STRINGKEYS);
    DRCErrorCount = 0;
    box = DRCdef->cd_bbox;

    /* Undo will only slow things down in here, so turn it off. */

    UndoDisable();
    scx.scx_use = use;
    scx.scx_x = use->cu_xlo;
    scx.scx_y = use->cu_ylo;
    scx.scx_area = *area;
    scx.scx_trans = GeoIdentityTransform;
    drcWhyFunc(&scx, (pointertype)dolist);
    UndoEnable();

    /* Delete the hash table now that we're finished (otherwise there
     * will be a core leak.
     */
	
    HashKill(&DRCErrorTable);

    /* Redisplay the DRC yank definition in case anyone is looking
     * at it.
     */
    
    DBReComputeBbox(DRCdef);
    (void) GeoInclude(&DRCdef->cd_bbox, &box);
    DBWAreaChanged(DRCdef, &box, DBW_ALLWINDOWS, &DBAllButSpaceBits);

    if (DRCErrorCount == 0) TxPrintf("No errors found.\n");
}

#ifdef MAGIC_WRAPPER

void
DRCWhyAll(use, area, fout)
    CellUse *use;			/* Use in whose definition to start
					 * the hierarchical check.
					 */
    Rect *area;				/* Area, in def's coordinates, that
					 * is to be checked.
					 */
    FILE *fout;				/*
					 * Write formatted output to fout
					 */
{
    SearchContext scx;
    Rect box;
    extern int drcWhyAllFunc();		/* Forward reference. */
    HashSearch	hs;
    HashEntry	*he;
    Tcl_Obj *lobj, *robj;

    /* Create a hash table to used for eliminating duplicate messages. */

    HashInit(&DRCErrorTable, 16, HT_STRINGKEYS);
    DRCErrorCount = 0;
    box = DRCdef->cd_bbox;

    /* Undo will only slow things down in here, so turn it off. */

    UndoDisable();
    scx.scx_use = use;
    scx.scx_x = use->cu_xlo;
    scx.scx_y = use->cu_ylo;
    scx.scx_area = *area;
    scx.scx_trans = GeoIdentityTransform;
    drcWhyAllFunc(&scx, NULL);
    UndoEnable();

    /* Generate results */

    robj = Tcl_NewListObj(0, NULL);
    
    HashStartSearch(&hs);
    while ((he = HashNext(&DRCErrorTable, &hs)) != (HashEntry *)NULL)
    {
	lobj = (Tcl_Obj *)HashGetValue(he);
	if (lobj != NULL)
	{
	    Tcl_ListObjAppendElement(magicinterp, robj, 
			Tcl_NewStringObj((char *)he->h_key.h_name, -1));
	    Tcl_ListObjAppendElement(magicinterp, robj, lobj);
	}
    }
    Tcl_SetObjResult(magicinterp, robj);

    /* Delete the hash table now that we're finished (otherwise there
     * will be a core leak.
     */
	
    HashKill(&DRCErrorTable);

    /* Redisplay the DRC yank definition in case anyone is looking
     * at it.
     */
    
    DBReComputeBbox(DRCdef);
    (void) GeoInclude(&DRCdef->cd_bbox, &box);
    DBWAreaChanged(DRCdef, &box, DBW_ALLWINDOWS, &DBAllButSpaceBits);

    if (DRCErrorCount == 0) TxPrintf("No errors found.\n");
}

#endif	/* MAGIC_WRAPPER */

/*
 * ----------------------------------------------------------------------------
 *
 * drcWhyFunc --
 *
 * 	This function is invoked underneath DrcWhy.  It's called once
 *	for each subcell instance of the current cell.  If the subcell
 *	is expanded, then it computes errors in that subcell and
 *	searches the subcell recursively.
 *
 * Results:
 *	Always returns 0 to keep the search alive.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
int
drcWhyFunc(scx, cdarg)
    SearchContext *scx;		/* Describes current state of search. */
    ClientData cdarg;		/* Used to hold boolean value "dolist" */
{
    CellDef *def = scx->scx_use->cu_def;
    bool dolist = (bool)((pointertype)cdarg);

    /* Check paint and interactions in this subcell. */
    
//  (void) DRCBasicCheck(def, &haloArea, &scx->scx_area,
//		(dolist) ? drcListError : drcPrintError,
//		(ClientData) scx);
    (void) DRCInteractionCheck(def, &scx->scx_area, &scx->scx_area,
		(dolist) ? drcListError : drcPrintError,
		(ClientData) scx);
    (void) DRCArrayCheck(def, &scx->scx_area,
		(dolist) ? drcListError : drcPrintError,
		(ClientData) scx);
    
    /* Also search children. */

    (void) DBCellSrArea(scx, drcWhyFunc, (ClientData)cdarg);

    return 0;
}

#ifdef MAGIC_WRAPPER

int
drcWhyAllFunc(scx, cdarg)
    SearchContext *scx;		/* Describes current state of search. */
    ClientData cdarg;		/* Unused */
{
    CellDef *def = scx->scx_use->cu_def;

    /* Check paint and interactions in this subcell. */
    
    (void) DRCInteractionCheck(def, &scx->scx_area, &scx->scx_area,
		drcListallError, (ClientData)scx);
    (void) DRCArrayCheck(def, &scx->scx_area,
		drcListallError, (ClientData)scx);
    
    /* Also search children. */

    (void) DBCellSrArea(scx, drcWhyAllFunc, (ClientData)cdarg);

    return 0;
}

#endif /* MAGIC_WRAPPER */

/*
 * ----------------------------------------------------------------------------
 *
 * DRCCheck --
 *
 * 	Marks all areas underneath the cursor, forcing them to be
 *	rechecked by the DRC.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Check tiles are painted.
 *
 * ----------------------------------------------------------------------------
 */

void
DRCCheck(use, area)
    CellUse *use;		/* Top-level use of hierarchy. */
    Rect *area;			/* This area is rechecked everywhere in the
				 * hierarchy underneath use.
				 */
{
    SearchContext scx;
    extern int drcCheckFunc();	/* Forward reference. */

    DBCellReadArea(use, area);

    scx.scx_use = use;
    scx.scx_x = use->cu_xlo;
    scx.scx_y = use->cu_ylo;
    scx.scx_area = *area;
    scx.scx_trans = GeoIdentityTransform;
    (void) drcCheckFunc(&scx, (ClientData) NULL);
}

	/* ARGSUSED */
int
drcCheckFunc(scx, cdarg)
    SearchContext *scx;
    ClientData cdarg;		/* Not used. */
{
    Rect cellArea;
    CellDef *def;

    /* Clip the area to the size of the cell, then recheck that area.
     * The recheck is handled by painting the check info directly
     * and then calling DRCCheckThis only to add the cell to the
     * list of those to be checked.  This avoids the hierarchical
     * upwards search that would normally be made by DRCCheckThis,
     * but which is unwelcome (and slow) here.
     */

    cellArea = scx->scx_area;
    def = scx->scx_use->cu_def;
    GeoClip(&cellArea, &def->cd_bbox);
    GEO_EXPAND(&cellArea, DRCTechHalo, &cellArea);

    DBPaintPlane(def->cd_planes[PL_DRC_CHECK], &cellArea,
		DBStdPaintTbl(TT_CHECKPAINT, PL_DRC_CHECK),
		(PaintUndoInfo *) NULL);

    DRCCheckThis(def, TT_CHECKPAINT, (Rect *) NULL);

    /* Check child cells also. */

    (void) DBCellSrArea(scx, drcCheckFunc, (ClientData) NULL);

    /* As a special performance hack, if the complete cell area is
     * handled here, don't bother to look at any more array elements.
     */
    
    if (GEO_SURROUND(&cellArea, &def->cd_bbox))
	return 2;
    else return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DRCCount --
 *
 * 	Searches the entire hierarchy underneath the given area.
 *	For each cell found, counts design-rule violations in
 *	that cell and outputs the counts.
 *
 * Results:
 *	Return linked list of cell definitions and their error counts.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

DRCCountList *
DRCCount(use, area)
    CellUse *use;		/* Top-level use of hierarchy. */
    Rect *area;			/* Area in which violations are counted. */
{
    DRCCountList  *dcl, *newdcl;
    HashTable	  dupTable;
    HashEntry	  *he;
    HashSearch	  hs;
    int		  count;
    SearchContext scx;
    extern int    drcCountFunc();	/* Forward reference. */

    /* Use a hash table to make sure that we don't output information
     * for any cell more than once.
     */

    HashInit(&dupTable, 16, HT_WORDKEYS);

    scx.scx_use = use;
    scx.scx_x = use->cu_xlo;
    scx.scx_y = use->cu_ylo;
    scx.scx_area = *area;
    scx.scx_trans = GeoIdentityTransform;
    (void) drcCountFunc(&scx, &dupTable);

    /* Create the list from the hash table */

    dcl = NULL;
    if (dupTable.ht_table != (HashEntry **) NULL)
    {
	HashStartSearch(&hs);
	while ((he = HashNext(&dupTable, &hs)) != (HashEntry *)NULL)
	{
            count = (spointertype)HashGetValue(he);
	    if (count > 1)
	    {
		newdcl = (DRCCountList *)mallocMagic(sizeof(DRCCountList));
		newdcl->dcl_count = count - 1;
		newdcl->dcl_def = (CellDef *)he->h_key.h_ptr;
		newdcl->dcl_next = dcl;
		dcl = newdcl;
	    }
	}
    }
    HashKill(&dupTable);
    return dcl;
}

int
drcCountFunc(scx, dupTable)
    SearchContext *scx;
    HashTable *dupTable;		/* Passed as client data, used to
					 * avoid searching any cell twice.
					 */
{
    int count;
    HashEntry *h;
    CellDef *def;
    extern int drcCountFunc2();

    /* If we've already seen this cell definition before, then skip it
     * now.
     */

    def = scx->scx_use->cu_def;
    h = HashFind(dupTable, (char *)def);
    if (HashGetValue(h) != 0) goto done;
    HashSetValue(h, 1);

    /* Count errors in this cell definition by scanning the error plane. */

    count = 0;
    (void) DBSrPaintArea((Tile *) NULL, def->cd_planes[PL_DRC_ERROR],
	&def->cd_bbox, &DBAllButSpaceBits, drcCountFunc2, (ClientData) &count);
    HashSetValue(h, (spointertype)count + 1);

    /* Ignore children that have not been loaded---we will only report	*/
    /* errors that can be seen.  This avoids immediately loading and	*/
    /* drc processing large layouts simply because we asked for an	*/
    /* error count.  When the cell is loaded, drc will be checked	*/
    /* anyway, and the count can be updated in response to that check.	*/

    if ((scx->scx_use->cu_def->cd_flags & CDAVAILABLE) == 0) return 0;

    /* Scan children recursively. */

    (void) DBCellSrArea(scx, drcCountFunc, (ClientData) dupTable);

    /* As a special performance hack, if the complete cell area is
     * handled here, don't bother to look at any more array elements.
     */
    
    done: if (GEO_SURROUND(&scx->scx_area, &def->cd_bbox)) return 2;
    else return 0;
}

int
drcCountFunc2(tile, pCount)
    Tile *tile;			/* Tile found in error plane. */
    int *pCount;		/* Address of count word. */
{
    if (TiGetType(tile) != (TileType) TT_SPACE) *pCount += 1;
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DRCCatchUp--
 *
 * 	This procedure just runs the background checker, regardless
 *	of whether it's enabled or not, and waits for it to complete.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Error and check tiles get painted and erased by the checker.
 *
 * ----------------------------------------------------------------------------
 */

void
DRCCatchUp()
{
    int background;

    background = DRCBackGround;
    DRCBackGround = DRC_SET_ON;
    DRCContinuous();
    DRCBackGround = background;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DRCFind --
 *
 * 	Locates the next violation tile in the cell pointed to by
 *	"use" and its children.
 *	Successive calls will located successive violations, in
 *	circular order.
 *
 * Results:
 *	If an error tile was found in def, returns the indx of
 *	the error tile (> 0).  Returns 0 if there were no error
 *	tiles in def.  Returns -1 if indx was out-of-range
 *	(not that many errors in def).
 *
 * Side effects:
 *	Rect is filled with the location of the tile, if one is found.
 *
 * ----------------------------------------------------------------------------
 */

typedef struct {
    int current;	/* count of current error */
    int target;		/* count of target error */
    Rect *rect;		/* Return rectangle */
    Transform trans;	/* Return transform */
    HashTable *deft;	/* Table of cell definitions to avoid duplicates */
} Sindx;

int
DRCFind(use, area, rect, indx)
    CellUse *use;		/* Cell use to check. */
    Rect *area;			/* Area of search */
    Rect *rect;			/* Rectangle to fill in with tile location. */
    int indx;			/* Go to this error. */
{
    SearchContext scx;
    Sindx finddata;
    Rect trect;	
    int result;
    int drcFindFunc();
    HashTable defTable;

    scx.scx_use = use;
    scx.scx_x = use->cu_xlo;
    scx.scx_y = use->cu_ylo;
    scx.scx_area = *area;
    scx.scx_trans = GeoIdentityTransform;

    HashInit(&defTable, 16, HT_WORDKEYS);

    finddata.current = 0;
    finddata.target = indx;
    finddata.rect = &trect;
    finddata.trans = scx.scx_trans;
    finddata.deft = &defTable;

    result = drcFindFunc(&scx, &finddata);

    HashKill(&defTable);

    if (result == 0)
    {
	if (finddata.current == 0)
	    return 0;
	else
	    return -1;
    }

    /* Translate rectangle back into coordinate system of "use" */
    GeoTransRect(&finddata.trans, &trect, rect);
    return indx;
}

int
drcFindFunc(scx, finddata)
    SearchContext *scx;
    Sindx *finddata;
{
    CellDef *def;
    HashEntry *h;
    int drcFindFunc2();

    def = scx->scx_use->cu_def;
    h = HashFind(finddata->deft, (char *)def);
    if (HashGetValue(h) != 0) return 0;
    HashSetValue(h, 1);

    (void) DBCellRead(def, (char *) NULL, TRUE, NULL);

    if (DBSrPaintArea((Tile *) NULL, def->cd_planes[PL_DRC_ERROR],
	    &def->cd_bbox, &DBAllButSpaceBits, drcFindFunc2,
	    (ClientData)finddata) != 0)
    {
	finddata->trans = scx->scx_trans;
	return 1;
    }
    
    /* Recursively search children */
    return DBCellSrArea(scx, drcFindFunc, (ClientData)finddata);
}

int
drcFindFunc2(tile, finddata)
    Tile *tile;			/* Tile in error plane. */
    Sindx *finddata;		/* Information about error to find */

{
    if (TiGetType(tile) == (TileType) TT_SPACE) return 0;
    if (++finddata->current == finddata->target)
    {
	TiToRect(tile, finddata->rect);
	return 1;
    }
    return 0;
}
