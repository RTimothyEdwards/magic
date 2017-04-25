/*
 * PlowMain.c --
 *
 * Plowing.
 * Main loop: set everything up, and then repeatedly remove an
 * edge from the queue of pending edges and process it.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/plow/PlowMain.c,v 1.2 2008/12/11 04:20:12 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/undo.h"
#include "debug/debug.h"
#include "plow/plow.h"
#include "plow/plowInt.h"
#include "textio/textio.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "drc/drc.h"
#include "utils/styles.h"
#include "utils/malloc.h"
#include "utils/signals.h"
#include "utils/main.h"
#include "select/select.h"
#include "graphics/graphics.h"

#if defined(SYSV) || defined(__APPLE__)
# define	NO_RUSAGE
#endif

/* Plowing jog horizon: see PlowExtendJogHorizon() for an explanation */
global int PlowJogHorizon = 0;

/* TRUE if we should straighten jogs automatically after each plow */
global bool PlowDoStraighten = FALSE;

/*
 * Search rule table.  These rules are used to search from a moving
 * edge to find the other edges it causes to move.  The procedure
 * implementing each rule should be of the form:
 *
 *	(*proc)(edge, rules)
 *	    Edge *edge;
 *	    PlowRule *rules;
 *	{
 *	}
 *
 * It may not modify the Edge pointed to by 'edge'.
 * The edge is in the cell plowYankDef.
 */
RuleTableEntry plowSearchRulesTbl[MAXRULES];
RuleTableEntry *plowSearchRulesPtr = plowSearchRulesTbl;

/*
 * Cell rules.
 * Same as above.
 */
RuleTableEntry plowCellRulesTbl[MAXRULES];
RuleTableEntry *plowCellRulesPtr = plowCellRulesTbl;

    /* Imported rules */
extern int prClearUmbra();
extern int prUmbra();
extern int prPenumbraTop(), prPenumbraBot();
extern int prFixedPenumbraTop(), prFixedPenumbraBot();
extern int prSliverTop(), prSliverBot();
extern int prInSliver();
extern int prIllegalTop(), prIllegalBot();
extern int prCoverTop(), prCoverBot();
extern int prFixedLHS(), prFixedRHS(), prFixedDragStubs();
extern int prContactLHS(), prContactRHS();
extern int prFindCells();
extern int prCell();

    /* Defined elsewhere in this module */
extern CellDef *plowYankDef;
extern CellUse *plowYankUse;
extern CellDef *plowSpareDef;
extern CellUse *plowSpareUse;
extern Rect plowYankedArea;
extern int plowYankHalo;

    /* Plow boundary information */
typedef struct pb
{
    CellDef	*pb_editDef;	/* Cell to which this applies */
    Rect	 pb_editArea;	/* Area of boundary in edit cell coords */

    /* The following exist for redisplay only */
    CellDef	*pb_rootDef;	/* Display in all windows with this root */
    Rect	 pb_rootArea;	/* Area of boundary in root cell coords */
    struct pb	*pb_next;	/* Next record in chain */
} PlowBoundary;

    /* If the following is TRUE, enable checking of boundaries */
bool plowCheckBoundary = FALSE;

    /* List of PlowBoundary records above */
PlowBoundary *plowBoundaryList = NULL;

    /* TRUE if labels or cells were changed by plowing */
bool plowLabelsChanged;

    /* Transform information to yank cell coordinates */
Transform plowYankTrans;	/* Transform from original cell to yank cell */
Transform plowInverseTrans;	/* Transform from yank cell to original cell */
Rect plowCellBbox;		/* Transformed initial cell bounding box */
int plowDirection;		/* Direction of plowing (GEO_*) */

    /* Dummy whose cu_def pointer is reset to point to the def being plowed */
CellUse *plowDummyUse = (CellUse *) NULL;

    /* Debugging */
RuleTableEntry *plowCurrentRule;/* Rule currently being applied */
RuleTableEntry plowRuleInitial;	/* Dummy rule for debugging */

    /* Procedure called for each edge affected by the one being moved */
int (*plowPropagateProcPtr)() = (int (*)()) NULL;

    /* Statistics */
int plowQueuedEdges;	/* Number of edges passed to plowQueueAdd */
int plowProcessedEdges;	/* Number of times plowProcessEdge called */
int plowMovedEdges;	/* Number of edges actually moved */

    /* Forward declarations */
int plowInitialPaint(), plowInitialCell();
int plowUpdatePaintTile(), plowUpdateCell();
bool plowPastBoundary();
bool plowPropagateSel();
bool plowPropagateRect();
PlowRule *plowBuildWidthRules();

void plowMergeBottom(Tile *, Plane *);
void plowInitRule();

extern void PlowRedrawBound();
extern void PlowClearBound();
extern void plowUpdate();
extern void plowSetTrans();
extern void plowProcessEdge();
extern void plowMoveEdge();
extern void plowMergeTop();
extern void plowYankCreate();


/*
 * ----------------------------------------------------------------------------
 *
 * PlowSetBound --
 *
 * Set the bounding box for plowing.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

void
PlowSetBound(def, area, rootDef, rootArea)
    CellDef *def;	/* Def in which bounding area applies */
    Rect *area;		/* Area in 'def' coordinates */
    CellDef *rootDef;	/* Display bounding area in windows with this root */
    Rect *rootArea;	/* Area in 'rootDef' coordinates */
{
    static bool firstTime = TRUE;
    PlowBoundary *pb;

    /* May eventually support a list, but for now, there's just one */
    PlowClearBound();
    pb = (PlowBoundary *) mallocMagic(sizeof (PlowBoundary));

    pb->pb_rootDef = rootDef;
    pb->pb_rootArea = *rootArea;
    pb->pb_editDef = def;
    pb->pb_editArea = *area;
    pb->pb_next = (PlowBoundary *) NULL;
    plowBoundaryList = pb;
    plowCheckBoundary = TRUE;

    /* Add ourselves as a client of the highlight handler */
    if (firstTime)
	DBWHLAddClient(PlowRedrawBound), firstTime = FALSE;

    /* Redisplay the highlight we just added */
    DBWHLRedraw(rootDef, rootArea, FALSE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlowClearBound --
 *
 * Eliminate boundary checking for plowing.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Eliminates the highlight of the plowing bounding box.
 *
 * ----------------------------------------------------------------------------
 */

void
PlowClearBound()
{
    PlowBoundary *pb;

    pb = plowBoundaryList;
    plowCheckBoundary = FALSE;
    plowBoundaryList = (PlowBoundary *) NULL;
    for ( ; pb; pb = pb->pb_next)
    {
	DBWHLRedraw(pb->pb_rootDef, &pb->pb_rootArea, TRUE);
	freeMagic((char *) pb);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlowRedrawBound --
 *
 * This procedure is called by the highlight manager to redisplay
 * plowing highlights.  The window is locked before entry.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Plowing highlight information is redrawn, if there is any
 *	that needs redisplaying.
 *
 * ----------------------------------------------------------------------------
 */

void
PlowRedrawBound(window, plane)
    MagWindow *window;		/* Window in which to redraw. */
    Plane *plane;		/* Non-space tiles on this plane indicate
				 * areas where highlights need to be
				 * redisplayed.
				 */
{
    Rect worldArea, screenArea;
    CellDef *windowRoot;
    PlowBoundary *pb;
    extern int plowBoundAlways1();	/* Forward reference. */

    /* Nothing to do if no boundaries */
    if (!plowCheckBoundary)
	return;

    windowRoot = ((CellUse *) (window->w_surfaceID))->cu_def;

    GrSetStuff(STYLE_DOTTEDHIGHLIGHTS);
    WindSurfaceToScreen(window, &window->w_surfaceArea, &worldArea);
    for (pb = plowBoundaryList; pb; pb = pb->pb_next)
    {
	/* Nothing to do if not in the right window */
	if (windowRoot != pb->pb_rootDef)
	    continue;

	/* See if the current area needs to be redisplayed */
	if (!DBSrPaintArea((Tile *) NULL, plane, &pb->pb_rootArea,
		&DBAllButSpaceBits, plowBoundAlways1, (ClientData) NULL))
	    continue;

	WindSurfaceToScreen(window, &pb->pb_rootArea, &screenArea);
	GeoClip(&screenArea, &worldArea);
	GrFastBox(&screenArea);
    }
}

int
plowBoundAlways1()
{
    return 1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlowStraighten --
 *
 * Straighten all jogs in the interior of the Rect 'area' in the CellDef 'def'
 * by pulling them all in 'direction' (one of GEO_NORTH, GEO_SOUTH, GEO_EAST,
 * or GEO_WEST) in such a way as to avoid moving any edges other than the jogs.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the geometry of def in the area 'area'.
 *
 * ----------------------------------------------------------------------------
 */

void
PlowStraighten(def, area, direction)
    CellDef *def;	/* Def whose jogs we should straighten */
    Rect *area;		/* Area in which jogs are to be straightened */
    int direction;	/* Pull all jogs in this direction to straighten them */
{
    Rect changedArea, changedUserArea, yankArea;
    bool saveCheckBoundary;
    int saveJogHorizon;
    SearchContext scx;
    PaintUndoInfo ui;

    /* Make sure the yank buffers exist */
    plowYankCreate();

    /* Set the yank transforms plowYankTrans and plowInverseTrans */
    plowSetTrans(direction);

    /* Set the bounding box of this cell in yanked coordinates */
    GeoTransRect(&plowYankTrans, &def->cd_bbox, &plowCellBbox);

    /* Transform the straightening area into yank def coordinates */
    GeoTransRect(&plowYankTrans, area, &yankArea);

    /*
     * Yank into yank buffer.
     * Yank at least a tech halo around the area to make sure
     * we detect all potential design-rule violations.
     */
    plowDummyUse->cu_def = def;
    UndoDisable();
    DBCellClearDef(plowYankDef);
    plowYankedArea.r_xbot = yankArea.r_xbot - DRCTechHalo;
    plowYankedArea.r_ybot = yankArea.r_ybot - DRCTechHalo;
    plowYankedArea.r_xtop = yankArea.r_xtop + DRCTechHalo;
    plowYankedArea.r_ytop = yankArea.r_ytop + DRCTechHalo;
    scx.scx_use = plowDummyUse;
    scx.scx_trans = plowYankTrans;
    GeoTransRect(&plowInverseTrans, &plowYankedArea, &scx.scx_area);
    (void) DBCellCopyPaint(&scx, &DBAllButSpaceAndDRCBits, 0, plowYankUse);
    (void) DBCellCopyCells(&scx, plowYankUse, (Rect *) NULL);
    DBReComputeBbox(plowYankDef);
    UndoEnable();

    /* Temporarily disable boundary checking and jog horizon for the plow */
    saveCheckBoundary = plowCheckBoundary;
    saveJogHorizon = PlowJogHorizon;
    plowCheckBoundary = FALSE;
    PlowJogHorizon = 0;

    /* Reduce jogs */
    UndoDisable();
    changedArea.r_xbot = changedArea.r_xtop = 0;
    changedArea.r_ybot = changedArea.r_ytop = 0;
    plowCleanupJogs(&yankArea, &changedArea);
    UndoEnable();

    /* Debugging */
    DBWAreaChanged(plowYankDef,&TiPlaneRect,DBW_ALLWINDOWS,&DBAllButSpaceBits);
    DBReComputeBbox(plowYankDef);

    /* Restore previous state of boundary checking and jog horizon */
    plowCheckBoundary = saveCheckBoundary;
    PlowJogHorizon = saveJogHorizon;

    /* Done if nothing was changed */
    if (GEO_RECTNULL(&changedArea))
	return;

    /* Erase area in original def */
    ui.pu_def = def;
    GeoTransRect(&plowInverseTrans, &changedArea, &changedUserArea);
    GeoClip(&changedUserArea, &TiPlaneRect);
    for (ui.pu_pNum = PL_TECHDEPBASE; ui.pu_pNum < DBNumPlanes; ui.pu_pNum++)
	DBPaintPlane(def->cd_planes[ui.pu_pNum], &changedUserArea,
			DBWriteResultTbl[TT_SPACE], &ui);

    /* Stuff from yank buffer back into original def */
    scx.scx_area = changedArea;
    scx.scx_use = plowYankUse;
    scx.scx_trans = plowInverseTrans;
    (void) DBCellCopyPaint(&scx, &DBAllButSpaceAndDRCBits, 0, plowDummyUse);
    DBReComputeBbox(def);
    DBWAreaChanged(def, &changedUserArea, DBW_ALLWINDOWS, &DBAllButSpaceBits);
    DRCCheckThis(def, TT_CHECKPAINT, &changedUserArea);
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlowSelection --
 *
 * Plow the entire selection by the distance indicated.
 *
 * Results:
 *	Returns FALSE if *pdistance had to be modified in order to keep the
 *	effects of plowing entirely within the limits specified by
 *	plowBoundaryList, or TRUE otherwise.
 *
 * Side effects:
 *	Plows.
 *	If we return FALSE, then *pdistance will be modified as described above.
 *
 * ----------------------------------------------------------------------------
 */

bool
PlowSelection(def, pdistance, direction)
    CellDef *def;	/* Cell being plowed */
    int *pdistance;	/* Distance to plow */
    int direction;	/* One of GEO_NORTH, GEO_SOUTH, GEO_WEST, or GEO_EAST */
{
    Rect changedArea;
    bool firstTime;

    /* Create the dummy yank buffers if they don't already exist */
    plowYankCreate();

    /* Set plowYankTrans and plowInverseTrans */
    plowSetTrans(direction);

    /* Set the bounding box of this cell in yanked coordinates */
    GeoTransRect(&plowYankTrans, &def->cd_bbox, &plowCellBbox);

    /*
     * If boundary checking is enabled, the following loop may be
     * executed several times because the original plow affected too
     * much of the circuit.  In this case, userRect gets updated to
     * the amount the plow finally did move.
     */
    firstTime = TRUE;
    while (plowPropagateSel(def, pdistance, &changedArea))
	firstTime = FALSE;

    if (!GEO_RECTNULL(&changedArea))
	plowUpdate(def, direction, &changedArea);

    return (firstTime);
}

/*
 * ----------------------------------------------------------------------------
 *
 * Plow --
 *
 * Plow a given collection of layers.
 *
 * Results:
 *	Returns FALSE if userRect had to be modified in order to keep the
 *	effects of plowing entirely within the limits specified by
 *	plowBoundaryList, or TRUE otherwise.
 *
 * Side effects:
 *	Plows.
 *	If we return FALSE, then userRect will be modified as described above.
 *
 * ----------------------------------------------------------------------------
 */

bool
Plow(def, userRect, layers, direction)
    CellDef *def;		/* Cell being plowed */
    Rect *userRect;		/* The plow.  Interpreted as per direction
				 * below.
				 */
    TileTypeBitMask layers;	/* The initial plow only sees these layers */
    int direction;		/* One of GEO_NORTH, GEO_SOUTH, GEO_WEST,
				 * or GEO_EAST.
				 */
{
#ifdef	COUNTWIDTHCALLS
    extern int plowWidthNumCalls;
    extern int plowWidthNumChoices;
#endif	/* COUNTWIDTHCALLS */
    TileTypeBitMask lc;
    Rect changedArea;
    bool firstTime;

    /* Create the dummy yank buffers if they don't already exist */
    plowYankCreate();

    /* Set plowYankTrans and plowInverseTrans */
    plowSetTrans(direction);

    /* Set the bounding box of this cell in yanked coordinates */
    GeoTransRect(&plowYankTrans, &def->cd_bbox, &plowCellBbox);

    /*
     * If boundary checking is enabled, the following loop may be
     * executed several times because the original plow affected too
     * much of the circuit.  In this case, userRect gets updated to
     * the amount the plow finally did move.
     */
    firstTime = TRUE;
    TTMaskCom2(&lc, &layers);
    while (plowPropagateRect(def, userRect, lc, &changedArea))
	firstTime = FALSE;

    if (!GEO_RECTNULL(&changedArea))
	plowUpdate(def, direction, &changedArea);

#ifdef	COUNTWIDTHCALLS
    TxPrintf("Choices = %d Calls = %d Choices/Call = %.1f\n",
	plowWidthNumChoices, plowWidthNumCalls,
	((double) plowWidthNumChoices) / ((double) plowWidthNumCalls));
#endif	/* COUNTWIDTHCALLS */
    return (firstTime);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowUpdate --
 *
 * Update the original def after plowing.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the layout from plowYankDef.
 *
 * ----------------------------------------------------------------------------
 */

void
plowUpdate(def, direction, pChangedArea)
    CellDef *def;
    int direction;
    Rect *pChangedArea;
{
    Rect changedUserArea;
    TileTypeBitMask *m;
    PaintUndoInfo ui;

    if (SigInterruptPending)
	goto done;

	/* Mark cell as modified */
    def->cd_flags |= CDMODIFIED|CDGETNEWSTAMP;

	/* Bloat the changed area to catch edges on the LHS */
    pChangedArea->r_xbot--, pChangedArea->r_ybot--;
    pChangedArea->r_xtop++, pChangedArea->r_ytop++;
    GeoTransRect(&plowInverseTrans, pChangedArea, &changedUserArea);
    GeoClip(&changedUserArea, &TiPlaneRect);	/* SANITY */
    plowLabelsChanged = FALSE;

    /*
     * Update the cells.  Find whether each cell in the plowed
     * planes has moved, and if so, move the corresponding cell
     * in the original def.
     */
    (void) DBCellEnum(plowYankDef, plowUpdateCell, (ClientData) def);

    /*
     * Update the labels.  This consists of changing the positions
     * of each label that was dragged along with its paint.  The
     * labels come from the original def, since they didn't have
     * to be yanked.
     */
    plowUpdateLabels(plowYankDef, def, &changedUserArea);

    /*
     * Update the paint.  Erase the changed area from the original
     * layout, and then paint back from the new layout.  Use the
     * transform plowInverseTrans to transform back from the yanked
     * planes into coordinates of the original def.
     */
    ui.pu_def = def;
    for (ui.pu_pNum = PL_TECHDEPBASE; ui.pu_pNum < DBNumPlanes; ui.pu_pNum++)
    {
	/* Erase area in original def */
	DBPaintPlane(def->cd_planes[ui.pu_pNum], &changedUserArea,
			DBWriteResultTbl[TT_SPACE], &ui);

	/* Update from yanked def */
	(void) DBSrPaintArea((Tile *) NULL, plowYankDef->cd_planes[ui.pu_pNum],
			pChangedArea, &DBAllButSpaceBits,
			plowUpdatePaintTile, (ClientData) &ui);
    }

    /* Ashes to ashes */
done:
    DBAdjustLabels(def, &changedUserArea);
    DBReComputeBbox(plowYankDef);
    DBReComputeBbox(def);
    m = &DBAllButSpaceBits;
    if (plowLabelsChanged) m = (TileTypeBitMask *) NULL;
    DBWAreaChanged(def, &changedUserArea, DBW_ALLWINDOWS, m);
    DRCCheckThis(def, TT_CHECKSUBCELL, &changedUserArea);

    /*
     * Final postpass: straighten any jogs in the area
     * affected by this plow operation.
     */
    if (PlowDoStraighten && !SigInterruptPending)
	PlowStraighten(def, &changedUserArea, direction);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowPropagateRect --
 *
 * Do the actual work of plowing a single plow, propagating the
 * effects of the initial plow to all geometry eventually affected by it.
 *
 * If the global bool plowCheckBoundary is TRUE, then we check against
 * the boundaries in the list plowBoundaryList for edges that violate
 * the limits set by those boundaries.  If the effects of plowing extend
 * outside of this area, we adjust userRect to the largest plowing rectangle
 * that will not cause propagation into illegal areas.  If userRect had to
 * be adjusted, we return TRUE.
 *
 * Results:
 *	Returns TRUE if we had to adjust userRect, FALSE if not.
 *	If plowCheckBoundary is FALSE, we never return TRUE.
 *
 * Side effects:
 *	See above.
 *	Sets the rectangle pointed to by 'changedArea' to be a
 *	bounding box around the area modified by plowing.
 *
 * ----------------------------------------------------------------------------
 */

bool
plowPropagateRect(def, userRect, lc, changedArea)
    CellDef *def;	/* Def being plowed */
    Rect *userRect;	/* User-specified plow (we transform this) */
    TileTypeBitMask lc;	/* Complement of set of layers to plow */
    Rect *changedArea;	/* Set to bounding box around area modified */
{
    Rect cellPlowRect, plowRect, r;
#ifndef	NO_RUSAGE
    struct rusage t1, t2;
#endif
    int tooFar, pNum;
    SearchContext scx;
    Edge edge;

    changedArea->r_xbot = changedArea->r_xtop = 0;
    changedArea->r_ybot = changedArea->r_ytop = 0;

    /*
     * Back off by one lambda to catch edges underneath the plow.
     * If no work to do, then we return FALSE.
     */
    GeoTransRect(&plowYankTrans, userRect, &plowRect);
    if (plowRect.r_xbot == plowRect.r_xtop)
	return (FALSE);

    cellPlowRect = plowRect;
    plowRect.r_xbot--;

	/* Clear the yank buffer for this iteration */
    DBCellClearDef(plowYankDef);

    /*
     * Part 0.
     * Yank the area of the plow, plus plowYankHalo, into a separate
     * set of tile planes.
     */
    plowDummyUse->cu_def = def;
    UndoDisable();
    scx.scx_use = plowDummyUse;
    scx.scx_trans = plowYankTrans;
    if (DebugIsSet(plowDebugID, plowDebYankAll))
    {
	scx.scx_area.r_xbot = def->cd_bbox.r_xbot - 1;
	scx.scx_area.r_ybot = def->cd_bbox.r_ybot - 1;
	scx.scx_area.r_xtop = def->cd_bbox.r_xtop + 1;
	scx.scx_area.r_ytop = def->cd_bbox.r_ytop + 1;
	GeoTransRect(&plowYankTrans, &scx.scx_area, &plowYankedArea);
    }
    else
    {
	plowYankedArea.r_xbot = plowRect.r_xbot - plowYankHalo;
	plowYankedArea.r_xtop = plowRect.r_xtop + plowYankHalo;
	plowYankedArea.r_ybot = plowRect.r_ybot - plowYankHalo;
	plowYankedArea.r_ytop = plowRect.r_ytop + plowYankHalo;
	GeoTransRect(&plowInverseTrans, &plowYankedArea, &scx.scx_area);
    }
    (void) DBCellCopyPaint(&scx, &DBAllButSpaceAndDRCBits, 0, plowYankUse);
    (void) DBCellCopyCells(&scx, plowYankUse, (Rect *) NULL);
    UndoEnable();

    /*
     * Part 1.
     * Searching.  This finds all the edges in the layout that have
     * to move, and marks them with the distance they must move.
     * Everything here works with the transformed cell.  We initialize
     * plowCurrentRule to null here so the debugging output for plowQueueAdd
     * will reflect the fact that the edges found below are "initial" edges
     * (those found by the plow itself, as opposed to by other edges).
     */
#ifndef NO_RUSAGE
    if (DebugIsSet(plowDebugID, plowDebTime))
	getrusage(RUSAGE_SELF, &t1);
#endif
    plowMovedEdges = plowProcessedEdges = plowQueuedEdges = 0;
    plowQueueInit(&plowCellBbox, plowRect.r_xtop - plowRect.r_xbot);

	/* Queue each edge found by the plowing rules */
    plowPropagateProcPtr = plowQueueAdd;

	/* Debugging */
    plowCurrentRule = &plowRuleInitial;

	/* Add the initial edges */
    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	(void) plowSrShadowInitial(pNum, &plowRect,
		    lc, plowInitialPaint, (ClientData) plowRect.r_xtop);

	/* Find any subcells crossed by the plow */
    (void) TiSrArea((Tile *) NULL, plowYankDef->cd_planes[PL_CELL],
		    &cellPlowRect, plowInitialCell, (ClientData) &cellPlowRect);

	/* While edges remain, process them */
    tooFar = 0;
    while (plowQueueLeftmost(&edge))
    {
	/* Ignore edges that don't move (sanity check) */
	if (edge.e_x == edge.e_newx)
	    continue;

	/*
	 * If we are doing boundary checking, don't add edges to the right of
	 * the boundary; just record how far they move.  Edges whose original
	 * position is to the left of the boundary but cross it must still be
	 * queued for processing, since they can affect edges on the right of
	 * the boundary.
	 */
	if (plowCheckBoundary && plowPastBoundary(def, &edge, &tooFar))
	    continue;
	if (!SigInterruptPending)
	    plowProcessEdge(&edge, changedArea);
    }

	/* Clean up */
    plowQueueDone();
#ifndef NO_RUSAGE
    if (DebugIsSet(plowDebugID, plowDebTime))
    {
	getrusage(RUSAGE_SELF, &t2);
	plowShowTime(&t1, &t2, plowQueuedEdges,
			plowProcessedEdges, plowMovedEdges);
    }
#endif
    /*
     * If geometry to the right of the boundary moved, adjust
     * the user's plow.
     */
    if (tooFar)
    {
	GeoTransRect(&plowYankTrans, userRect, &r);
	r.r_xtop -= tooFar;
	GeoTransRect(&plowInverseTrans, &r, userRect);
	return (TRUE);
    }

    /* Successful plow */
    return (FALSE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowPropagateSel --
 *
 * Do the actual work of plowing the selection, propagating the
 * effects of each initial plow to all geometry eventually affected
 * by them.
 *
 * If the global bool plowCheckBoundary is TRUE, then we check against
 * the boundaries in the list plowBoundaryList for edges that violate
 * the limits set by those boundaries.  If the effects of plowing extend
 * outside of this area, we adjust *pdistance to the largest plowing distance
 * that will not cause propagation into illegal areas.  If *pdistance had to
 * be adjusted, we return TRUE.
 *
 * Results:
 *	Returns TRUE if we had to adjust *pdistance, FALSE if not.
 *	If plowCheckBoundary is FALSE, we never return TRUE.
 *
 * Side effects:
 *	See above.
 *	Sets the rectangle pointed to by 'changedArea' to be a
 *	bounding box around the area modified by plowing.
 *
 * ----------------------------------------------------------------------------
 */

bool
plowPropagateSel(def, pdistance, changedArea)
    CellDef *def;	/* Def being plowed */
    int *pdistance;	/* Distance to plow */
    Rect *changedArea;	/* Set to bounding box around area modified */
{
#ifndef NO_RUSAGE
    struct rusage t1, t2;
#endif
    int plowSelPaintBox(), plowSelCellBox();
    int plowSelPaintPlow(), plowSelCellPlow();
    Rect selBox;
    int tooFar;
    SearchContext scx;
    bool dummy;
    Edge edge;

    changedArea->r_xbot = changedArea->r_xtop = 0;
    changedArea->r_ybot = changedArea->r_ytop = 0;

    if (*pdistance <= 0)
	return (FALSE);

    /*
     * Find the bounding box for all material in the selection
     * that lies in the edit cell.
     */
    selBox.r_xbot = selBox.r_ybot = INFINITY;
    selBox.r_xtop = selBox.r_ytop = MINFINITY;
    SelEnumPaint(&DBAllButSpaceBits, TRUE, &dummy,
		plowSelPaintBox, (ClientData) &selBox);
    SelEnumCells(TRUE, &dummy, (SearchContext *) NULL,
		plowSelCellBox, (ClientData) &selBox);

    if (GEO_RECTNULL(&selBox))
	return (FALSE);

	/* Clear the yank buffer for this iteration */
    DBCellClearDef(plowYankDef);

    /*
     * Yank the area of the selection, plus plowYankHalo,
     * into a separate set of tile planes.
     */
    plowDummyUse->cu_def = def;
    UndoDisable();
    scx.scx_use = plowDummyUse;
    scx.scx_trans = plowYankTrans;
    if (DebugIsSet(plowDebugID, plowDebYankAll))
    {
	scx.scx_area.r_xbot = def->cd_bbox.r_xbot - 1;
	scx.scx_area.r_ybot = def->cd_bbox.r_ybot - 1;
	scx.scx_area.r_xtop = def->cd_bbox.r_xtop + 1;
	scx.scx_area.r_ytop = def->cd_bbox.r_ytop + 1;
	GeoTransRect(&plowYankTrans, &scx.scx_area, &plowYankedArea);
    }
    else
    {
	/* Note selBox is in parent def coordinates */
	GeoTransRect(&plowYankTrans, &selBox, &plowYankedArea);
	plowYankedArea.r_xtop += *pdistance + plowYankHalo;
	plowYankedArea.r_xbot -= plowYankHalo;
	plowYankedArea.r_ybot -= plowYankHalo;
	plowYankedArea.r_ytop += plowYankHalo;
	GeoTransRect(&plowInverseTrans, &plowYankedArea, &scx.scx_area);
    }
    (void) DBCellCopyPaint(&scx, &DBAllButSpaceAndDRCBits, 0, plowYankUse);
    (void) DBCellCopyCells(&scx, plowYankUse, (Rect *) NULL);
    UndoEnable();

    /*
     * Searching.  This finds all the edges in the layout that have
     * to move, and marks them with the distance they must move.
     * Everything here works with the transformed cell.  We initialize
     * plowCurrentRule to null here so the debugging output for plowQueueAdd
     * will reflect the fact that the edges found below are "initial" edges
     * (those found by the plow itself, as opposed to by other edges).
     */
#ifndef NO_RUSAGE
    if (DebugIsSet(plowDebugID, plowDebTime))
	getrusage(RUSAGE_SELF, &t1);
#endif
    plowMovedEdges = plowProcessedEdges = plowQueuedEdges = 0;
    plowQueueInit(&plowCellBbox, *pdistance);

	/* Queue each edge found by the plowing rules */
    plowPropagateProcPtr = plowQueueAdd;

	/* Debugging */
    plowCurrentRule = &plowRuleInitial;

	/* Add everything in the selection */
    SelEnumPaint(&DBAllButSpaceBits, TRUE, &dummy,
		plowSelPaintPlow, (ClientData) *pdistance);
    SelEnumCells(TRUE, &dummy, (SearchContext *) NULL,
		plowSelCellPlow, (ClientData) *pdistance);

	/* While edges remain, process them */
    tooFar = 0;
    while (plowQueueLeftmost(&edge))
    {
	/* Ignore edges that don't move (sanity check) */
	if (edge.e_x == edge.e_newx)
	    continue;

	/*
	 * If we are doing boundary checking, don't add edges to the right of
	 * the boundary; just record how far they move.  Edges whose original
	 * position is to the left of the boundary but cross it must still be
	 * queued for processing, since they can affect edges on the right of
	 * the boundary.
	 */
	if (plowCheckBoundary && plowPastBoundary(def, &edge, &tooFar))
	    continue;
	if (!SigInterruptPending)
	    plowProcessEdge(&edge, changedArea);
    }

	/* Clean up */
    plowQueueDone();
#ifndef NO_RUSAGE
    if (DebugIsSet(plowDebugID, plowDebTime))
    {
	getrusage(RUSAGE_SELF, &t2);
	plowShowTime(&t1, &t2, plowQueuedEdges,
			plowProcessedEdges, plowMovedEdges);
    }
#endif
    /*
     * If geometry to the right of the boundary moved,
     * adjust the plow distance.
     */
    if (tooFar)
    {
	*pdistance -= tooFar;
	return (TRUE);
    }

    /* Successful plow */
    return (FALSE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowSelPaintBox --
 * plowSelCellBox --
 *
 * Called on behalf of plowPropagateSel() above to find the bounding
 * box for the material in the selection.  Each is called for an element
 * in the selection: a paint tile for plowSelPaintBox, or a subcell for
 * plowSelCellBox.  The bounding box *pSelBox is updated to include the
 * area of the element.
 *
 * Results:
 *	Both return 0 always.
 *
 * Side effects:
 *	Both adjust *pSelBox;
 *
 * ----------------------------------------------------------------------------
 */

int
plowSelPaintBox(rect, type, pSelBox)
    Rect *rect;
    TileType type;
    Rect *pSelBox;
{
    Rect editRect;

    GeoTransRect(&RootToEditTransform, rect, &editRect);
    GeoInclude(&editRect, pSelBox);
    return (0);
}

int
plowSelCellBox(selUse, realUse, transform, pSelBox)
    CellUse *selUse;
    CellUse *realUse;
    Transform *transform;
    Rect *pSelBox;
{
    GeoInclude(&realUse->cu_bbox, pSelBox);
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowSelPaintPlow --
 *
 * Called on behalf of plowPropagateSel() above to queue the initial
 * edges belonging to material in the selection.  For paint, we create
 * a plow at both the right and left edges of the tile.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Queues edges marked as initial (e_flags has E_ISINITIAL set).
 *
 * ----------------------------------------------------------------------------
 */

int
plowSelPaintPlow(rect, type, distance)
    Rect *rect;
    TileType type;
    int distance;
{
    int plowSelPaintAdd();
    Rect editRect, plowRect, plowLHS, plowRHS;
    TileTypeBitMask mask;

    GeoTransRect(&RootToEditTransform, rect, &editRect);
    GeoTransRect(&plowYankTrans, &editRect, &plowRect);
    plowLHS = plowRHS = plowRect;

    /* Queue the LHS */
    plowLHS.r_xtop = plowLHS.r_xbot + distance;
#ifdef	notdef
    plowAtomize(DBPlane(type), &plowLHS, plowSelPaintAdd, (ClientData) NULL);
#endif	/* notdef */
    plowLHS.r_xbot--;
    plowSrShadow(DBPlane(type), &plowLHS, DBZeroTypeBits,
			plowInitialPaint, (ClientData) plowLHS.r_xtop);

    /* Queue the RHS */
    plowRHS.r_xbot = plowRHS.r_xtop;
    plowRHS.r_xtop += distance;
#ifdef	notdef
    plowAtomize(DBPlane(type), &plowRHS, plowSelPaintAdd, (ClientData) NULL);
#endif	/* notdef */
    plowRHS.r_xbot--;
    TTMaskSetOnlyType(&mask, type);
    plowSrShadow(DBPlane(type), &plowRHS, mask,
			plowInitialPaint, (ClientData) plowRHS.r_xtop);

    return (0);
}

int
plowSelPaintAdd(edge)
    Edge *edge;
{
    int saveFlags = edge->e_flags;

    edge->e_flags |= E_ISINITIAL;
    plowQueueAdd(edge);
    edge->e_flags = saveFlags;
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowSelCellPlow --
 *
 * Called on behalf of plowPropagateSel() above to queue the initial
 * edges belonging to material in the selection.  For subcells, we
 * have to look for the subcell with the same use-id in our yank
 * buffer, and then move it by its leading edge.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Queues edges.
 *
 * ----------------------------------------------------------------------------
 */

int
plowSelCellPlow(selUse, realUse, transform, distance)
    CellUse *selUse;		/* Cell in selection */
    CellUse *realUse;		/* Corresponding cell in def being plowed */
    Transform *transform;	/* UNUSED */
    int distance;		/* Plow distance */
{
    int plowFindSelCell();
    ClientData save;

    /* Find the cell in the yanked def that has the same use-id as this one */
    save = realUse->cu_client;
    realUse->cu_client = (ClientData)distance;
    (void) DBCellEnum(plowYankDef, plowFindSelCell, (ClientData)realUse);
    realUse->cu_client = save;

    return (0);
}

int
plowFindSelCell(yankUse, editUse)
    CellUse *yankUse;	/* Cell in the plow yank buffer */
    CellUse *editUse;	/* Cell from the original cell def */
{
    Edge edge;

    if (strcmp(yankUse->cu_id, editUse->cu_id) != 0)
	return (0);

    edge.e_flags = 0;
    edge.e_pNum = PL_CELL;
    edge.e_use = yankUse;
    edge.e_ytop = yankUse->cu_bbox.r_ytop;
    edge.e_ybot = yankUse->cu_bbox.r_ybot;
    edge.e_x = yankUse->cu_bbox.r_xtop;
    edge.e_newx = yankUse->cu_bbox.r_xtop + (int)editUse->cu_client;
    edge.e_ltype = PLOWTYPE_CELL;
    edge.e_rtype = PLOWTYPE_CELL;
    (void) plowQueueAdd(&edge);
    return (1);
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlowExtendJogHorizon --
 *
 * Search above and below 'edge' for the closest "natural" jogs within
 * plowJogHorizon of the top or bottom of the edge, and extend 'edge'
 * to these jogs if they are found.  If no jog is found in a particular
 * direction, we leave that end of 'edge' (top/bottom) alone.
 *
 * A "natural" jog is nothing more than a change in the direction of
 * an edge being followed.  The following diagram gives an example, with
 * the vertical arrows indicating the jog horizons.  Note that the edge
 * is extended up, because there is a natural jog there, but not down
 * because the jog on the bottom is outside the horizon.
 *
 *		+--------   ^		E--------
 *		|	    |		E
 *		|	    |		E
 *		|	    v		E
 *		E			E
 *		E			E
 *		E			E
 *		|	    ^		|
 *		|	    |		|
 *		|	    |		|
 *		|	    v		|
 *		|			|
 *	--------+		--------+
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May modify its argument 'edge' by extending it vertically.
 *	May also add additional edges via plowAtomizeEdge().
 *
 * ----------------------------------------------------------------------------
 */

void
PlowExtendJogHorizon(edge)
    Edge *edge;			/* Edge being moved */
{
    int horizonTop, horizonBot, eTop, eBot;
    Tile *tpR, *tpL;
    Point startPoint;
    bool rhsChanged;
    Rect r, newEdgeR;

    if (PlowJogHorizon == 0)
	return;

    horizonTop = edge->e_ytop + PlowJogHorizon;
    horizonBot = edge->e_ybot - PlowJogHorizon;
    r.r_xbot = edge->e_x - 1;
    r.r_xtop = edge->e_x + 1;
    newEdgeR = edge->e_rect;

    /* Extend to the top */
restarttop:
    startPoint.p_x = edge->e_x - 1;
    startPoint.p_y = edge->e_ytop;
    tpL = TiSrPointNoHint(plowYankDef->cd_planes[edge->e_pNum], &startPoint);
    r.r_ybot = r.r_ytop = edge->e_ytop;

    /*
     * Walk upwards.
     * The loop terminates with r.r_ytop equal to the Y coordinate
     * of the closest jog to our top, and eTop equal to the smaller
     * of r.r_ytop and the Y coordinate of the closest point at which
     * the RHS of the edge changed type.
     */
    rhsChanged = FALSE;
    while (RIGHT(tpL) == edge->e_x
	    && TiGetTypeExact(tpL) == edge->e_ltype
	    && BOTTOM(tpL) < horizonTop)
    {
	r.r_ytop = TOP(tpL);
	if (plowYankMore(&r, 1, 1))
	    goto restarttop;

	/*
	 * Walk along RHS to see if its type changed.
	 * If so, make eTop record the lowest point
	 * at which this happened.
	 */
	if (!rhsChanged)
	    for (tpR = TR(tpL); TOP(tpR) > r.r_ybot; tpR = LB(tpR))
		if (TiGetTypeExact(tpR) != edge->e_rtype)
		    rhsChanged = TRUE, eTop = BOTTOM(tpR);
	tpL = RT(tpL);
	r.r_ybot = r.r_ytop;
    }

    /* Update if within the jog horizon */
    if (r.r_ytop <= horizonTop && r.r_ytop > edge->e_ytop)
    {
	newEdgeR.r_ytop = r.r_ytop;
	edge->e_ytop = (rhsChanged) ? eTop : r.r_ytop;
    }


    /* Extend to the bottom */
restartbot:
    startPoint.p_x = edge->e_x;
    startPoint.p_y = edge->e_ybot - 1;
    tpR = TiSrPointNoHint(plowYankDef->cd_planes[edge->e_pNum], &startPoint);
    r.r_ybot = r.r_ytop = edge->e_ybot;

    /*
     * Walk down.
     * The loop terminates with r.r_ybot equal to the Y coordinate
     * of the closest jog to our bottom, and eBot equal to the larger
     * of r.r_ytop and the Y coordinate of the closest point at which
     * the RHS of the edge changed type.
     */
    rhsChanged = FALSE;
    while (LEFT(tpR) == edge->e_x && TOP(tpR) > horizonBot)
    {
	r.r_ybot = BOTTOM(tpR);
	if (plowYankMore(&r, 1, 1))
	    goto restartbot;

	/* Record where the RHS type changed if it did */
	if (!rhsChanged && TiGetTypeExact(tpR) != edge->e_rtype)
	    rhsChanged = TRUE, eBot = TOP(tpR);

	/* Walk up the LHS */
	for (tpL = BL(tpR); BOTTOM(tpL) < r.r_ytop; tpL = RT(tpL))
	    if (TiGetTypeExact(tpL) != edge->e_ltype)
		r.r_ybot = TOP(tpL);

	if (r.r_ybot > BOTTOM(tpR))
	    break;

	tpR = LB(tpR);
	r.r_ytop = r.r_ybot;
    }

    /* Update if within the jog horizon */
    if (r.r_ybot >= horizonBot && r.r_ybot < edge->e_ybot)
    {
	newEdgeR.r_ybot = r.r_ybot;
	edge->e_ybot = (rhsChanged) ? eBot : r.r_ybot;
    }

    if (newEdgeR.r_ytop > edge->e_ytop)
    {
	r = newEdgeR;
	r.r_ybot = edge->e_ytop;
	(void) plowAtomize(edge->e_pNum, &r, plowQueueAdd, (ClientData) NULL);
    }
    if (newEdgeR.r_ybot < edge->e_ybot)
    {
	r = newEdgeR;
	r.r_ytop = edge->e_ybot;
	(void) plowAtomize(edge->e_pNum, &r, plowQueueAdd, (ClientData) NULL);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowSetTrans --
 *
 * Set up the transforms based on the direction we will be plowing.
 * Just use simple rotations, since we always have the inverse
 * transform around for copying stuff back.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets plowYankTrans and plowInverseTrans.
 *
 * ----------------------------------------------------------------------------
 */

void
plowSetTrans(direction)
    int direction;
{
    plowDirection = direction;
    switch (direction)
    {
	case GEO_NORTH:
	    plowYankTrans = Geo90Transform;
	    break;
	case GEO_SOUTH:
	    plowYankTrans = Geo270Transform;
	    break;
	case GEO_WEST:
	    plowYankTrans = Geo180Transform;
	    break;
	case GEO_EAST:
	    plowYankTrans = GeoIdentityTransform;
	    break;
    }
    GeoInvertTrans(&plowYankTrans, &plowInverseTrans);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowPastBoundary --
 *
 * Check to see if the edge is in an illegal region according to the
 * boundaries on the list plowBoundaryList.
 *
 * Results:
 *	TRUE if we should not even bother to process this edge (because
 *	its initial position was in an invalid area), FALSE otherwise.
 *
 * Side effects:
 *	Updates *pmove to the farthest distance by which anything in an
 *	illegal area moves.
 *
 * ----------------------------------------------------------------------------
 */

bool
plowPastBoundary(def, edge, pmove)
    CellDef *def;		/* Def being plowed */
    Edge *edge;	/* Edge being moved */
    int *pmove;	/* Updated to be the maximum distance by
				 * which something moves in an illegal area.
				 */
{
    PlowBoundary *pb;
    int delta;
    bool ret;
    Rect r;

    ret = FALSE;
    delta = 0;
    for (pb = plowBoundaryList; pb; pb = pb->pb_next)
    {
	if (pb->pb_editDef != def)
	    continue;

	GeoTransRect(&plowYankTrans, &pb->pb_editArea, &r);
	if (edge->e_x < r.r_xbot)
	{
	    /* To the left of the boundary */
	    delta = MAX(edge->e_newx, r.r_xbot) - edge->e_x;
	}
	else if (edge->e_newx > r.r_xtop)
	{
	    /* To the right of the boundary */
	    delta = edge->e_newx - MAX(edge->e_x, r.r_xtop);
	    if (edge->e_x > r.r_xtop) ret = TRUE;
	}
	else if (edge->e_ytop > r.r_ytop || edge->e_ybot < r.r_ybot)
	{
	    /* Above or below the boundary */
	    delta = edge->e_newx - edge->e_x;
	}

	if (delta > *pmove) *pmove = delta;
    }

    return (ret);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowInitialPaint --
 *
 * Add one of the edges found initially by the plow to the queue
 * of edges to move.  The edge will move as far as 'xnew'.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	Adds the edge to the queue of edges to move via plowQueueAdd().
 *
 * ----------------------------------------------------------------------------
 */

int
plowInitialPaint(edge, xnew)
    Edge *edge;
    int xnew;
{
    edge->e_newx = xnew;
    edge->e_flags = E_ISINITIAL;
    (void) plowQueueAdd(edge);
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowInitialCell --
 *
 * Add a cell to the queue of edges to move.  The cell will move as far
 * as 'plowRect->r_xtop'.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	Adds an edge to the queue of edges to move via plowQueueAdd().
 *
 * ----------------------------------------------------------------------------
 */

int
plowInitialCell(cellTile, plowRect)
    Tile *cellTile;
    Rect *plowRect;
{
    CellTileBody *ctb;
    CellUse *use;
    int xmove;
    Edge edge;

    edge.e_pNum = PL_CELL;
    for (ctb = (CellTileBody *) TiGetBody(cellTile); ctb; ctb = ctb->ctb_next)
    {
	use = ctb->ctb_use;
	if (use->cu_bbox.r_xbot < plowRect->r_xbot)
	{
	    if (use->cu_bbox.r_xtop >= plowRect->r_xtop)
		continue;

	    /* Dragging this cell by its front edge */
	    xmove = plowRect->r_xtop - use->cu_bbox.r_xtop;
	}
	else
	{
	    /* Pushing this cell by its back edge */
	    xmove = plowRect->r_xtop - use->cu_bbox.r_xbot;
	}

	edge.e_use = use;
	edge.e_flags = E_ISINITIAL;
	edge.e_ytop = use->cu_bbox.r_ytop;
	edge.e_ybot = use->cu_bbox.r_ybot;
	edge.e_x = use->cu_bbox.r_xtop;
	edge.e_newx = use->cu_bbox.r_xtop + xmove;
	edge.e_ltype = PLOWTYPE_CELL;
	edge.e_rtype = PLOWTYPE_CELL;
	(void) plowQueueAdd(&edge);
    }

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowProcessEdge --
 *
 * Process a single edge from the queue.
 * Plowing is rule-based, so processing an edge consists of applying
 * a sequence of rules.  The overall algorithm is:
 *
 *	- Yank more of the original cell if necessary.
 *	- Check to see if the edge has already moved; if so, we don't do
 *	  anything further.
 *	- Apply extension rules.  These are allowed to extend the edge
 *	  but shouldn't search for new edges to be added to the edge queue.
 *	- Compute the real width rules for this material.
 *	- Apply search rules to each remaining segment of the clipped edge.
 *	  These look for other edges to be added to the edge queue.
 *	- Update the edge's position.  This consists of modifying the
 *	  LEADING coordinates in the tiles along the edge, possibly
 *	  splitting the tiles at the top and bottom of the edge.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May split and merge tiles.
 *	May cause additional area to be yanked from the original cell.
 *	Updates changedArea to include any additional area modified.
 *
 * ----------------------------------------------------------------------------
 */

void
plowProcessEdge(edge, changedArea)
    Edge *edge;		/* Edge to be processed (in plowYankDef) */
    Rect *changedArea;	/* Include any additional area changed in this area */
{
    int amountToMove = edge->e_newx - edge->e_x;
    RuleTableEntry *rte;
    Tile *tp;
    Point p;
    Rect r;

    /* Debugging */
    if ((plowWhenTop && edge->e_x == plowWhenTopPoint.p_x
		     && edge->e_ytop == plowWhenTopPoint.p_y)
	|| (plowWhenBot && edge->e_x == plowWhenBotPoint.p_x
			&& edge->e_ybot == plowWhenBotPoint.p_y))
    {
	plowDebugEdge(edge, (RuleTableEntry *) NULL, "matched edge");
    }

    /*
     * Process cells specially.
     * These don't get clipped, but are either processed
     * completely or not at all.
     */
    plowProcessedEdges++;
    if (edge->e_use)
    {
	if (amountToMove > (int)edge->e_use->cu_client)
	{
	    /* Update area modified by plowing */
	    (void) GeoInclude(&edge->e_rect, changedArea);

	    /*
	     * See if the cell's bbox plus the distance to move
	     * forces us to yank more from the original cell.
	     */
	    r = edge->e_use->cu_bbox;
	    r.r_xtop = edge->e_newx;
	    (void) plowYankMore(&r, plowYankHalo, 1);

	    /*
	     * Update the cell's position.
	     * We do this here so we don't see the cell a
	     * second time.  The whole cell moves, so we have
	     * to update the area changed by the area of the
	     * cell PLUS the area swept out by its RHS.
	     */
	    edge->e_use->cu_client = (ClientData)amountToMove;
	    r = edge->e_use->cu_bbox;
	    r.r_xbot += amountToMove;
	    r.r_xtop += amountToMove;
	    (void) GeoInclude(&r, changedArea);

	    /* Apply cell rules */
	    for (rte = plowCellRulesTbl; rte < plowCellRulesPtr; rte++)
	    {
		if (TTMaskHasType(&rte->rte_ltypes, edge->e_ltype)
			&& TTMaskHasType(&rte->rte_rtypes, edge->e_rtype))
		{
		    plowCurrentRule = rte;
		    (*rte->rte_proc)(edge, (PlowRule *) NULL);
		}
	    }
	    plowMovedEdges++;
	}
	return;
    }

    /*
     * Check to see if any of this edge needs to move.  If it has
     * already moved far enough, we can just return; otherwise,
     * we process the entire edge as a whole.  This check is necessary
     * to avoid infinite loops.
     */
    p.p_x = edge->e_x, p.p_y = edge->e_ytop - 1;
    tp = TiSrPointNoHint(plowYankDef->cd_planes[edge->e_pNum], &p);
    for ( ; TOP(tp) > edge->e_ybot; tp = LB(tp))
	if (TRAILING(tp) < edge->e_newx)
	    goto worktodo;
    return;

    /*
     * Some or all of this edge must be moved.
     * Extend it if necessary.
     * Yank more if necessary.
     * Compute its width.
     * Apply the search rules.
     * Update the coordinates of all tiles along this edge.
     */
worktodo:

    /*
     * Extend this edge if we're reducing jogs.
     * May cause other edges to be added.
     */
    plowMovedEdges++;
    if (PlowJogHorizon > 0)
	PlowExtendJogHorizon(edge);

    /*
     * Update area modified by plowing.
     * This must happen after applying the extension rules above.
     */
    (void) GeoInclude(&edge->e_rect, changedArea);

    /* Apply search rules */
    plowApplySearchRules(edge);

    /* Update edge position */
    plowMoveEdge(edge);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowApplySearchRules --
 *
 * Apply the search rules to an edge.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May call (*plowPropagateProcPtr)() if the search rules
 *	determine that edges must move.  May yank more.
 *	May cause additional area to be yanked from the original cell.
 *
 * ----------------------------------------------------------------------------
 */

int
plowApplySearchRules(edge)
    Edge *edge;
{
    PlowRule *widthRules, *rules;
    RuleTableEntry *rte;
    int halo;

    /*
     * Build list of width rules using computed (instead of min) width.
     * This can also yank more.
     */
    halo = plowYankHalo;
    widthRules = plowBuildWidthRules(edge, &plowCellBbox, &halo);

    /* Yank more if necessary */
    (void) plowYankMore(&edge->e_rect, halo, 1);

    /*
     * Search rules.
     * These generally won't need to yank more unless they deal
     * with entire tiles.
     */
    for (rte = plowSearchRulesTbl; rte < plowSearchRulesPtr; rte++)
    {
	if (TTMaskHasType(&rte->rte_ltypes, edge->e_ltype)
		&& TTMaskHasType(&rte->rte_rtypes, edge->e_rtype))
	{
	    plowCurrentRule = rte;
	    switch (rte->rte_whichRules)
	    {
		/* Apply no rules */
		case RTE_NULL:
		    rules = (PlowRule *) NULL;
		    break;

		/* Apply width rules, but use actual widths */
		case RTE_REALWIDTH:
		    rules = widthRules;
		    break;

		/* Apply minimum-width rules */
		case RTE_MINWIDTH:
		    rules = plowWidthRulesTbl[edge->e_ltype][edge->e_rtype];
		    break;

		/* Apply spacing rules */
		case RTE_SPACING:
		    rules = plowSpacingRulesTbl[edge->e_ltype][edge->e_rtype];
		    break;

		/* Only apply rule if no spacing rules apply */
		case RTE_NOSPACING:
		    rules = plowSpacingRulesTbl[edge->e_ltype][edge->e_rtype];
		    if (rules)
			continue;
		    break;
	    }
	    (*rte->rte_proc)(edge, rules);
	}
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowBuildWidthRules --
 *
 * Find the real width of the material being plowed, and construct
 * a list of width rules that is the same as the normal list for
 * this type of edge, but with the minimum distance replaced by the
 * actual width if the actual width is greater than the minimum
 * distance.  (If the actual width is less than or equal to the
 * minimum distance, we use the minimum distance instead).
 *
 * If the minimum-width rectangle about the starting edge touches the
 * boundary of the yanked area, yank more and retry.
 *
 * Results:
 *	Returns a pointer to the statically constructed rules list,
 *	or NULL if no width rules apply.
 *
 * Side effects:
 *	Since the width rules list is statically allocated, subsequent
 *	calls to plowBuildWidthRules() will trash the results of previous
 *	calls.
 *
 *	May cause more of the original cell to be yanked.
 *
 * ----------------------------------------------------------------------------
 */

PlowRule *
plowBuildWidthRules(edge, bbox, phalo)
    Edge *edge;		/* Edge being moved */
    Rect *bbox;		/* Bounding box of def being plowed */
    int *phalo;		/* Update *phalo to be the max of its initial value
			 * and each of the widths we compute for the rules
			 * we return.
			 */
{
    extern char *maskToPrint();
    static PlowRule widthRuleList[MAXRULES];
    PlowRule *prMin, *prReal;
    Rect maxBox;
    int dist;

retry:
    prMin = plowWidthRulesTbl[edge->e_ltype][edge->e_rtype];
    if (prMin == NULL)
	return ((PlowRule *) NULL);

    /* At this point, know there will be at least one rule in the list */
    for (prReal = widthRuleList;
	    prMin && prReal < &widthRuleList[MAXRULES];
	    prMin = prMin->pr_next, prReal++)
    {
	*prReal = *prMin;
	prReal->pr_next = prReal + 1;
	dist = plowFindWidth(edge, prMin->pr_oktypes, bbox, &maxBox);

	/* Conservative test of whether we need to yank more */
	if (plowYankMore(&maxBox, 1, 1))
	{
	    if (DebugIsSet(plowDebugID, plowDebWidth))
		TxPrintf("width: yank more and retry\n");
	    goto retry;
	}
	prReal->pr_dist = MAX(dist, prReal->pr_dist);
	*phalo = MAX(*phalo, dist);
	if (DebugIsSet(plowDebugID, plowDebWidth))
	    TxPrintf("width: %d types: %s\n",
			prReal->pr_dist, maskToPrint(&prReal->pr_oktypes));
    }

    (--prReal)->pr_next = (PlowRule *) NULL;
    if (DebugIsSet(plowDebugID, plowDebWidth))
	plowDebugEdge(edge, (RuleTableEntry *) NULL, "find width");

    return (widthRuleList);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowMoveEdge --
 *
 * Do the actual work of updating the coordinates of an edge.  In general,
 * the edge may have more than one tile on either side, as below:
 *
 *		|
 *		|
 *	--------+
 *		+---------
 *		|
 *		|
 *	--------+
 *		|---------
 *		|
 *	--------+
 *		|
 *
 * Updating the coordinates consists of first clipping the tiles on
 * either side so they do not extend vertically past the edge, then
 * updating the TRAILING coordinates of all tiles along the RHS, and
 * then merging vertically where possible.
 *
 * We only update TRAILING coordinates if they are not already far
 * enough to the right.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May split and merge tiles.
 *
 * ----------------------------------------------------------------------------
 */

void
plowMoveEdge(edge)
    Edge *edge;	/* Edge to be moved */
{
    Plane *plane = plowYankDef->cd_planes[edge->e_pNum];
    Tile *tp, *tpL;
    Point p;

    /*
     * Find topmost tile along LHS of edge.
     * The goal is to clip the topmost LHS and topmost RHS tiles
     * so their tops are equal to the top of the edge.
     */
    p.p_x = edge->e_x - 1;
    p.p_y = edge->e_ytop - 1;
    tp = TiSrPointNoHint(plane, &p);
    ASSERT(RIGHT(tp) == edge->e_x, "plowMoveEdge");

    /* Only clip top tiles if we must update their coordinates */
    if (LEADING(tp) < edge->e_newx)
    {
	if (TOP(tp) > edge->e_ytop)
	    (void) plowSplitY(tp, edge->e_ytop);	/* Tp is bottom tile */
	tp = TR(tp);					/* Top tile on RHS */
	if (TOP(tp) > edge->e_ytop)
	    (void) plowSplitY(tp, edge->e_ytop);	/* Tp is bottom tile */
    }
    else for (tp = TR(tp); BOTTOM(tp) >= edge->e_ytop; tp = LB(tp))
	/* Nothing */;

    /*
     * Now 'tp' is the top-right tile.  Walk down the RHS, updating TRAILING
     * coordinates if necessary, and merging each tile with its upper neighbor.
     * Stop one short of the last tile.
     */
    for (; BOTTOM(tp) > edge->e_ybot; tp = LB(tp))
    {
	if (TRAILING(tp) < edge->e_newx)
	    plowSetTrailing(tp, edge->e_newx);
	plowMergeTop(tp, plane);
    }

    /*
     * Now 'tp' is the bottom-right tile.
     * Clip it if necessary, and update its TRAILING coordinate.  Merge it
     * with both its upper and lower neighbors.  If tp is the only tile on
     * the RHS, its TRAILING coordinate doesn't get updated until here.
     */
    if (TRAILING(tp) < edge->e_newx)
    {
	if (BOTTOM(tp) < edge->e_ybot)
	{
	    /* Clip (no merging will be necessary) */
	    tp = plowSplitY(tp, edge->e_ybot);
	    plowSetTrailing(tp, edge->e_newx);
	    tpL = BL(tp);
	}
	else	/* BOTTOM(tp) == edge->e_ybot */
	{
	    /* Merge (no clipping was necessary) */
	    tpL = BL(tp);
	    plowSetTrailing(tp, edge->e_newx);
	    plowMergeBottom(tp, plane);
	}

	/* Split the bottom-left tile if necessary; otherwise, merge down */
	if (BOTTOM(tpL) < edge->e_ybot)
	    tpL = plowSplitY(tpL, edge->e_ybot);	/* TpL is upper tile */
	else
	    plowMergeBottom(tpL, plane);
    }
    else for (tpL = BL(tp); TOP(tpL) <= edge->e_ybot; tpL = RT(tpL))
	/* Nothing */;
    plowMergeTop(tp, plane);

    /*
     * Now 'tpL' is the bottom-left tile, which has already been merged
     * with its lower neighbor.  Walk up the rest of the LHS, merging
     * each tile with its lower neighbor.
     */
    for (tp = RT(tpL); BOTTOM(tp) < edge->e_ytop; tp = RT(tp))
	plowMergeBottom(tp, plane);

    /*
     * If tp now extends above edge->e_ytop, then it must not have been split
     * way up in the first step in this procedure, which means that its LEADING
     * coordinate was already far enough to the right, which means that it was
     * not changed.  Hence, we needn't try to merge to its bottom.
     */
    if (BOTTOM(tp) == edge->e_ytop)
	plowMergeBottom(tp, plane);

    if (DebugIsSet(plowDebugID, plowDebMove))
	plowDebugEdge(edge, (RuleTableEntry *) NULL, "move");
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowSplitY --
 *
 * Split a tile vertically in two, returning a pointer to
 * the newly created upper tile.
 *
 * The new tile has its ti_client edge positions set to that
 * of the original tile.
 *
 * Results:
 *	Returns a pointer to the newly created tile.
 *
 * Side effects:
 *	Splits the tile 'tp'.
 *
 * ----------------------------------------------------------------------------
 */

Tile *
plowSplitY(tp, y)
    Tile *tp;
{
    Tile *newTile;

    newTile = TiSplitY(tp, y);
    newTile->ti_client = tp->ti_client;
    TiSetBody(newTile, TiGetBody(tp));

    return (newTile);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowMergeTop --
 * plowMergeBottom --
 *
 * Merge the given tile with its upper/lower neighbor if appropriate:
 * plowMergeTop merges with the upper neighbor, plowMergeBottom with
 * the lower.  This may happen only if the types are the same, the
 * LEADING/TRAILING coordinate the same, and the LEFT and RIGHT
 * coordinates the same.
 *
 * Guarantees that the tile pointer passed it as an argument will
 * remain valid after the call.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May cause two tiles to be joined.
 *
 * ----------------------------------------------------------------------------
 */

void
plowMergeTop(tp, plane)
    Tile *tp;
    Plane *plane;
{
    Tile *tpRT = RT(tp);

    if (TiGetTypeExact(tp) == TiGetTypeExact(tpRT)
	    && LEFT(tp) == LEFT(tpRT) && RIGHT(tp) == RIGHT(tpRT)
	    && LEADING(tp) == LEADING(tpRT) && TRAILING(tp) == TRAILING(tpRT))
    {
	TiJoinY(tp, tpRT, plane);
    }
}

void
plowMergeBottom(tp, plane)
    Tile *tp;
    Plane *plane;
{
    Tile *tpLB = LB(tp);

    if (TiGetTypeExact(tp) == TiGetTypeExact(tpLB)
	    && LEFT(tp) == LEFT(tpLB) && RIGHT(tp) == RIGHT(tpLB)
	    && LEADING(tp) == LEADING(tpLB) && TRAILING(tp) == TRAILING(tpLB))
    {
	TiJoinY(tp, tpLB, plane);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlowInit --
 *
 * Initialize the rule tables for plowing.  This gets called after
 * the technology file has been read, since we need to know about
 * fixed-width objects.
 *
 * Also initialize the debugging information.
 *
 * Sets plowYankHalo to be the size of the halo around each edge that
 * we use when checking to see if plowProcessEdge must yank more area
 * from the original cell.  If this halo extends outside plowYankedArea
 * or touches it, we yank more.  The size of the halo should be such
 * that most of the plowing rules are guaranteed not to visit any area
 * outside of it.  Currently, DRCTechHalo is sufficient; only the rules
 * moving whole tiles need anything bigger than this, and they are
 * responsible for doing their own yanking if they need it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

void
PlowInit()
{
    RuleTableEntry *rp, *re;
    TileTypeBitMask allButSpace, allBits;
    TileTypeBitMask cellTypes;
    TileTypeBitMask widthL, widthR;
    TileTypeBitMask spaceL, spaceR;
    TileTypeBitMask mask;
    TileType i, j;

    /* Set the masks we will use for all the rules below */
    allButSpace = DBAllButSpaceAndDRCBits;
    allBits = DBAllTypeBits;
    TTMaskSetOnlyType(&cellTypes, PLOWTYPE_CELL);
    TTMaskZero(&widthL);
    TTMaskZero(&widthR);
    TTMaskZero(&spaceL);
    TTMaskZero(&spaceR);
    for (i = 0; i < DBNumTypes; i++)
    {
	for (j = 0; j < DBNumTypes; j++)
	{
	    if (plowWidthRulesTbl[i][j])
	    {
		TTMaskSetType(&widthL, i);
		TTMaskSetType(&widthR, j);
	    }
	    if (plowSpacingRulesTbl[i][j])
	    {
		TTMaskSetType(&spaceL, i);
		TTMaskSetType(&spaceR, j);
	    }
	}
    }

    /* Dummy rule for debugging */
    plowInitRule(&plowRuleInitial, (&plowRuleInitial) + 1, RTE_NULL,
				(int (*)()) NULL,
				"initial edge",
				DBZeroTypeBits, DBZeroTypeBits);

    /* Cell rules */
    rp = plowCellRulesPtr;
    re = &plowCellRulesTbl[MAXRULES];
	/* Drag geometry with cells */
    plowInitRule(rp++, re, RTE_NULL, prCell,
				"drag paint with cells",
				allBits, cellTypes);
    if (rp >= re) rp = re;
    plowCellRulesPtr = rp;

    /* Search rules */
    rp = plowSearchRulesPtr;
    re = &plowSearchRulesTbl[MAXRULES];
	/* Clear the umbra */
    plowInitRule(rp++, re, RTE_NULL, prClearUmbra, "clear umbra",
				allBits, allButSpace);
    plowInitRule(rp++, re, RTE_REALWIDTH, prUmbra, "umbra width",
				widthL, widthR);
    plowInitRule(rp++, re, RTE_SPACING, prUmbra, "umbra spacing",
				spaceL, spaceR);

	/* Clear the penumbra */
    plowInitRule(rp++, re, RTE_REALWIDTH, prPenumbraTop,
				"top penumbra width",
				widthL, widthR);
    plowInitRule(rp++, re, RTE_SPACING, prPenumbraTop,
				"top penumbra spacing",
				spaceL, spaceR);
    plowInitRule(rp++, re, RTE_REALWIDTH, prPenumbraBot,
				"bottom penumbra width",
				widthL, widthR);
    plowInitRule(rp++, re, RTE_SPACING, prPenumbraBot,
				"bottom penumbra spacing",
				spaceL, spaceR);

	/* Special penumbra searching when RHS is fixed */
    plowInitRule(rp++, re, RTE_NOSPACING, prFixedPenumbraTop,
				"top penumbra spacing (RHS fixed-width)",
				allBits, PlowFixedTypes);
    plowInitRule(rp++, re, RTE_NOSPACING, prFixedPenumbraBot,
				"bottom penumbra spacing (RHS fixed-width)",
				allBits, PlowFixedTypes);

	/* Avoid introducing slivers */
    plowInitRule(rp++, re, RTE_MINWIDTH, prSliverTop,
				"top width slivers",
				widthL, widthR);
    plowInitRule(rp++, re, RTE_SPACING, prSliverTop,
				"top spacing slivers",
				spaceL, spaceR);
    plowInitRule(rp++, re, RTE_MINWIDTH, prSliverBot,
				"bottom width slivers",
				widthL, widthR);
    plowInitRule(rp++, re, RTE_SPACING, prSliverBot,
				"bottom spacing slivers",
				spaceL, spaceR);

	/* Inside slivers (plow too small) */
    TTMaskCom2(&mask, &PlowFixedTypes);
    plowInitRule(rp++, re, RTE_NULL, prInSliver,
				"inside slivers",
				mask, mask);

	/* Avoid introducing illegal edges */
    plowInitRule(rp++, re, RTE_NULL, prIllegalTop,
				"top illegal edges",
				allBits, allBits);
    plowInitRule(rp++, re, RTE_NULL, prIllegalBot,
				"bottom illegal edges",
				allBits, allBits);

	/* Avoid uncovering "covered" materials (e.g, fets) */
    plowInitRule(rp++, re, RTE_NULL, prCoverTop,
				"top covering",
				PlowCoveredTypes, allBits);
    plowInitRule(rp++, re, RTE_NULL, prCoverBot,
				"bottom covering",
				PlowCoveredTypes, allBits);

	/* Preserve fixed-width objects */
    plowInitRule(rp++, re, RTE_NULL, prFixedLHS,
				"LHS is fixed",
				PlowFixedTypes, allBits);
    plowInitRule(rp++, re, RTE_NULL, prFixedRHS,
				"RHS is fixed",
				allBits, PlowFixedTypes);

	/* Fixed-width objects drag trailing stubs */
    TTMaskCom2(&mask, &PlowDragTypes);
    TTMaskClearType(&mask, TT_SPACE);
    plowInitRule(rp++, re, RTE_NULL, prFixedDragStubs,
				"RHS fixed dragging stubs",
				mask, PlowDragTypes);

	/* Couple contacts */
    plowInitRule(rp++, re, RTE_NULL, prContactLHS,
				"LHS is contact",
				PlowContactTypes, allBits);
    plowInitRule(rp++, re, RTE_NULL, prContactRHS,
				"RHS is contact",
				allBits, PlowContactTypes);

	/* Move cells out of the way */
    plowInitRule(rp++, re, RTE_NULL, prFindCells,
				"find cells",
				allBits, allBits);

    if (rp >= re) rp = re;
    plowSearchRulesPtr = rp;

    /* Initialize debugging flags */
    plowDebugInit();

    /* Initialize the yank halo */
    plowYankHalo = DRCTechHalo;
}

void
plowInitRule(rtePtr, rteEnd, whichRules, proc, name, ltypes, rtypes)
    RuleTableEntry *rtePtr;	/* Pointer to entry to be added */
    RuleTableEntry *rteEnd;	/* Pointer to one past last entry in table */
    int whichRules;		/* Which rules to use (RTE_* from earlier) */
    int (*proc)();		/* Procedure implementing the rule */
    char *name;			/* Name of this rule */
    TileTypeBitMask ltypes, rtypes;
{
    if (rtePtr >= rteEnd)
    {
	TxError("Too many rules in PlowMain.c (maximum %d)\n", MAXRULES);
	return;
    }
    rtePtr->rte_whichRules = whichRules;
    rtePtr->rte_proc = proc;
    rtePtr->rte_name = name;
    rtePtr->rte_ltypes = ltypes;
    rtePtr->rte_rtypes = rtypes;
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowYankCreate --
 *
 * Create the yank buffers used for plowing.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Creates the cells __PLOWYANK__ and __PLOWINCR__ if they
 *	don't already exist, and initializes plowYankDef, plowYankUse,
 *	and plowDummyUse.
 *
 * ----------------------------------------------------------------------------
 */

void
plowYankCreate()
{
    if (plowYankDef == NULL)
    {
	DBNewYank("__PLOWYANK__", &plowYankUse, &plowYankDef);
	DBNewYank("__PLOWYANK__", &plowDummyUse, &plowYankDef);
	DBNewYank("__PLOWINCR__", &plowSpareUse, &plowSpareDef);
    }
}

#ifndef	NO_RUSAGE
plowShowTime(t1, t2, nqueued, nprocessed, nmoved)
    struct rusage *t1, *t2;
    int nqueued, nprocessed, nmoved;
{
    double secs, usecs;

    secs = t2->ru_utime.tv_sec - t1->ru_utime.tv_sec;
    usecs = (secs * 1000000.) + (t2->ru_utime.tv_usec - t1->ru_utime.tv_usec);

    printf("%.2f sec, %d queued, %d processed, %d moved\n",
		usecs/1000000., nqueued, nprocessed, nmoved);
}
#endif
