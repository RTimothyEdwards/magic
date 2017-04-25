/*
 * gaTest.c --
 *
 * Testing code for the gate-array router.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/garouter/gaTest.c,v 1.2 2009/05/01 18:59:44 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/hash.h"
#include "utils/heap.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "utils/signals.h"
#include "textio/textio.h"
#include "debug/debug.h"
#include "gcr/gcr.h"
#include "router/router.h"
#include "grouter/grouter.h"
#include "graphics/graphics.h"
#include "garouter/garouter.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "textio/txcommands.h"
#include "utils/main.h"
#include "utils/utils.h"
#include "commands/commands.h"
#include "utils/styles.h"

bool gaInitialized = FALSE;

ClientData gaDebugID = 0;

int gaDebChanOnly = 0;
int gaDebChanStats = 0;
int gaDebMaze = 0;
int gaDebNoSimple = 0;
int gaDebPaintStems = 0;
int gaDebShowChans = 0;
int gaDebShowMaze = 0;
int gaDebStems = 0;
int gaDebVerbose = 0;
int gaDebNoClean = 0;

/* Used in the "*garoute split" command */
PlaneMask gaSplitPlaneMask;
void (*gaSplitPaintPlane)();
Rect gaSplitArea;
int gaSplitType;

/* Forward declarations */

void GAInit();


/*
 * ----------------------------------------------------------------------------
 *
 * GATest --
 *
 * Command interface for testing the gate-array router.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Depends on the command; see below.
 *
 * ----------------------------------------------------------------------------
 */

void
GATest(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int n;
    typedef enum { CLRDEBUG, SETDEBUG, SHOWDEBUG} cmdType;
    static struct
    {
	char	*cmd_name;
	cmdType	 cmd_val;
    } cmds[] = {
	"clrdebug",		CLRDEBUG,
	"setdebug",		SETDEBUG,
	"showdebug",		SHOWDEBUG,
	0
    };

    GAInit();
    if (cmd->tx_argc == 1)
    {
	TxError("Must give subcommand\n");
	goto badCmd;
    }

    n = LookupStruct(cmd->tx_argv[1], (LookupTable *) cmds, sizeof cmds[0]);
    if (n < 0)
    {
	TxError("Unrecognized subcommand: %s\n", cmd->tx_argv[1]);
badCmd:
	TxError("Valid subcommands:");
	for (n = 0; cmds[n].cmd_name; n++)
	    TxError(" %s", cmds[n].cmd_name);
	TxError("\n");
	return;
    }

    switch (cmds[n].cmd_val)
    {
	case SETDEBUG:
	    DebugSet(gaDebugID, cmd->tx_argc - 2, &cmd->tx_argv[2], TRUE);
	    break;
	case CLRDEBUG:
	    DebugSet(gaDebugID, cmd->tx_argc - 2, &cmd->tx_argv[2], FALSE);
	    break;
	case SHOWDEBUG:
	    DebugShow(gaDebugID);
	    break;
    }
    return;
}

/*
 * ----------------------------------------------------------------------------
 *
 * GAGenChans --
 *
 * Generate gate-array channels over the area 'area'.  These channels
 * will be one of two types: CHAN_NORMAL, for channels over empty space,
 * or chanType (either CHAN_HRIVER or CHAN_VRIVER) for channels over
 * existing subcells.  The output is a collection of "garoute channel"
 * commands on the file 'f'.
 * 
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes to 'f'.
 *
 * ----------------------------------------------------------------------------
 */

void
GAGenChans(chanType, area, f)
    int chanType;
    Rect *area;
    FILE *f;
{
    extern void DBPaintPlane0(), DBPaintPlaneVert();
    int gaSplitFunc(), gaSplitOut();
    static CellDef *genDef = (CellDef *) NULL;
    static CellUse *genUse = (CellUse *) NULL;
    TileTypeBitMask obstacleMask;
    int halfUp, halfDown;
    SearchContext scx;
    Plane *plane;

    if (genDef == NULL)
	DBNewYank("__GENCHANNEL__", &genUse, &genDef);

    /*
     * Round the appropriate side of area down to the nearest
     * center-grid line.
     */
    halfDown = RtrGridSpacing / 2;
    halfUp = RtrGridSpacing - halfDown;
    switch (chanType)
    {
	case CHAN_HRIVER:
	    gaSplitPaintPlane = DBPaintPlane0;
	    area->r_ytop = RTR_GRIDDOWN(area->r_ytop - halfUp, RtrOrigin.p_y)
				+ halfUp;
	    area->r_ybot = RTR_GRIDUP(area->r_ybot + halfDown, RtrOrigin.p_y)
				- halfDown;
	    break;
	case CHAN_VRIVER:
	    gaSplitPaintPlane = DBPaintPlaneVert;
	    area->r_xtop = RTR_GRIDDOWN(area->r_xtop - halfUp, RtrOrigin.p_x)
				+ halfUp;
	    area->r_xbot = RTR_GRIDUP(area->r_xbot + halfDown, RtrOrigin.p_x)
				- halfDown;
	    break;
    }

    /* Make sure everything in 'area' is read */
    (void) DBCellReadArea(EditCellUse, area);
    DBFixMismatch();

    /* Start with a clean slate */
    DBCellClearDef(genDef);

    /*
     * Basic algorithm:
     * Find all cells in 'area'.  Compute the bounding rectangle for
     * each plane that can affect routing.  Bloat by RtrSubcellSepUp or
     * RtrSubcellSepDown (appropriate direction).  Extend this to the top
     * and bottom of 'area' for CHAN_HRIVER channels, or to the left and
     * right of 'area' for CHAN_VRIVER ones, and paint into the DRC error
     * plane of genDef.
     */
    TTMaskSetMask3(&obstacleMask, &RtrPolyObstacles, &RtrMetalObstacles);
    TTMaskSetType(&obstacleMask, RtrMetalType);
    TTMaskSetType(&obstacleMask, RtrPolyType);
    TTMaskSetType(&obstacleMask, RtrContactType);
    gaSplitPlaneMask = DBTechTypesToPlanes(&obstacleMask);
    gaSplitArea = *area;
    gaSplitType = chanType;
    scx.scx_use = EditCellUse;
    scx.scx_area = gaSplitArea;
    scx.scx_trans = GeoIdentityTransform;
    plane = genDef->cd_planes[PL_DRC_ERROR];
    (void) DBCellSrArea(&scx, gaSplitFunc, (ClientData) plane);

    /* Output all the tiles that lie inside 'area' in 'plane */
    (void) DBSrPaintArea((Tile *) NULL, plane, &gaSplitArea, &DBAllTypeBits,
		gaSplitOut, (ClientData) f);
}

/*
 * ----------------------------------------------------------------------------
 *
 * gaSplitOut --
 *
 * Called for each tile inside the area that was processed by GAGenChans()
 * above.  Outputs each tile as the appropriate type of channel (space
 * tiles are CHAN_NORMAL, non-space are either CHAN_HRIVER or CHAN_VRIVER)
 * after first clipping to the area gaSplitArea.
 * 
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	Writes to 'f'.
 *
 * ----------------------------------------------------------------------------
 */

int
gaSplitOut(tile, f)
    Tile *tile;
    FILE *f;
{
    Rect r;

    TITORECT(tile, &r);
    GeoClip(&r, &gaSplitArea);
    if (GEO_RECTNULL(&r))
	return (0);

    fprintf(f, "garoute channel %d %d %d %d",
		r.r_xbot, r.r_ybot, r.r_xtop, r.r_ytop);
    if (TiGetType(tile) != TT_SPACE)
	fprintf(f, " %s", gaSplitType == CHAN_HRIVER ? "h" : "v");
    fprintf(f, "\n");
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * gaSplitFunc --
 *
 * Called for each cell in the area being processed by GAGenChans()
 * above.  Computes a "true" bounding box for the cell (only looking
 * at the layers that are obstacles to routing), and then extends the
 * box as follows:
 *
 *	If producing CHAN_HRIVER channels, we extend the box to the
 *	top and bottom of gaSplitArea, and bloat it to the right and
 *	left to the next farthest-out line between two grid lines.
 *
 *	If producing CHAN_VRIVER channels, we extend the box to the
 *	left and right of gaSplitArea, and bloat it to the top and
 *	bottom to the next farthest-out line between two grid lines.
 * 
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	Paints the bloated area into the DRC error plane of 'plane'
 *	with a TileType of 1.
 *
 * ----------------------------------------------------------------------------
 */

int
gaSplitFunc(scx, plane)
    SearchContext *scx;
    Plane *plane;
{
    int halfUp, halfDown;
    CellDef *def = scx->scx_use->cu_def;
    Rect r, rAll, rTrans;
    int pNum;

    /* Compute the bounding rect for the interesting planes */
    rAll = GeoNullRect;
    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	if (PlaneMaskHasPlane(gaSplitPlaneMask, pNum))
	{
	    if (DBBoundPlane(def->cd_planes[pNum], &r))
		(void) GeoInclude(&r, &rAll);
	}

    GeoTransRect(&scx->scx_trans, &rAll, &rTrans);
    GeoClip(&rTrans, &gaSplitArea);

    /* Skip if no area */
    if (GEO_RECTNULL(&rTrans))
	return (0);

    /* Extend in the appropriate direction and bloat in the other one */
    halfDown = RtrGridSpacing / 2;
    halfUp = RtrGridSpacing - halfDown;

    switch (gaSplitType)
    {
	case CHAN_HRIVER:
	    rTrans.r_ytop = gaSplitArea.r_ytop;
	    rTrans.r_ybot = gaSplitArea.r_ybot;
	    rTrans.r_xtop += RtrSubcellSepUp;
	    rTrans.r_xbot -= RtrSubcellSepDown;
	    rTrans.r_xtop = RTR_GRIDUP(rTrans.r_xtop + halfDown, RtrOrigin.p_x)
				- halfUp;
	    rTrans.r_xbot = RTR_GRIDDOWN(rTrans.r_xbot - halfUp, RtrOrigin.p_x)
				+ halfDown;
	    break;
	case CHAN_VRIVER:
	    rTrans.r_xtop = gaSplitArea.r_xtop;
	    rTrans.r_xbot = gaSplitArea.r_xbot;
	    rTrans.r_ytop += RtrSubcellSepUp;
	    rTrans.r_ybot -= RtrSubcellSepDown;
	    rTrans.r_ytop = RTR_GRIDUP(rTrans.r_ytop + halfDown, RtrOrigin.p_y)
				- halfUp;
	    rTrans.r_ybot = RTR_GRIDDOWN(rTrans.r_ybot - halfUp, RtrOrigin.p_y)
				+ halfDown;
	    break;
    }

    /* Paint into DRC error plane */
    (*gaSplitPaintPlane)(plane, &rTrans, DBStdWriteTbl(1),
		(PaintUndoInfo *) NULL, PAINT_NORMAL);
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * GAInit --
 *
 * One-time-only initialization for the gate-array router.
 * Called after technology initialization.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Register ourself with the debug module and list all
 *	debugging flags.  Also initialize channel information.
 *
 * ----------------------------------------------------------------------------
 */

void
GAInit()
{
    int n;
    static struct
    {
	char	*di_name;
	int	*di_id;
    } dflags[] = {
	"chanonly",	&gaDebChanOnly,
	"chanstats",	&gaDebChanStats,
	"maze",		&gaDebMaze,
	"nosimple",	&gaDebNoSimple,
	"paintstems",	&gaDebPaintStems,
	"showchans",	&gaDebShowChans,
	"showmaze",	&gaDebShowMaze,
	"stems",	&gaDebStems,
	"verbose",	&gaDebVerbose,
	"noclean",	&gaDebNoClean,
	0
    };

    if (gaInitialized)
	return;

    gaInitialized = TRUE;

    /* Register ourselves with the debugging module */
    gaDebugID = DebugAddClient("garouter", sizeof dflags/sizeof dflags[0]);
    for (n = 0; dflags[n].di_name; n++)
	*(dflags[n].di_id) = DebugAddFlag(gaDebugID, dflags[n].di_name);

    /* Initialize channel information */
    GAChannelInitOnce();
}
