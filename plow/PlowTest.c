/*
 * PlowTest.c --
 *
 * Plowing.
 * Debugging and testing.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/plow/PlowTest.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>

#include "utils/magic.h"
#include "utils/styles.h"
#include "utils/utils.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "utils/undo.h"
#include "database/database.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "dbwind/dbwind.h"
#include "utils/main.h"
#include "commands/commands.h"
#include "drc/drc.h"
#include "debug/debug.h"
#include "plow/plowInt.h"
#include "textio/textio.h"
#include "textio/txcommands.h"

/* Forward declarations */
extern void plowShowShadow();
extern int plowShowOutline();
extern void plowTestJog();
extern void plowDebugMore();

/* Imports from PlowMain.c */
extern CellDef *plowYankDef;
extern CellUse *plowDummyUse;

/* Debugging flags */
ClientData plowDebugID;

int plowDebAdd;
int plowDebMove;
int plowDebNext;
int plowDebTime;
int plowDebWidth;
int plowDebJogs;
int plowDebYankAll;

/* Other debugging info */
bool plowWhenTop, plowWhenBot;
Point plowWhenTopPoint, plowWhenBotPoint;

/* Imports */
extern CellDef *plowSpareDef;

/*
 * ----------------------------------------------------------------------------
 *
 * PlowTest --
 *
 * Real command interface for debugging plowing.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See individual commands.
 *
 * ----------------------------------------------------------------------------
 */

typedef enum
{
    PC_HELP, PC_ERROR,
    PC_PLOW,
    PC_JOG,
    PC_RANDOM,
    PC_TECHSHOW,
    PC_OUTLINE, PC_LSHADOW, PC_SHADOW, PC_WIDTH, PC_LWIDTH,
    PC_SPLIT, PC_MERGEUP, PC_MERGEDOWN, PC_MOVE, PC_TRAIL, PC_PRINT,
    PC_WHENTOP, PC_WHENBOT,
    PC_SETD, PC_CLRD, PC_SHOWD
} pCmd;
struct
{
    char *p_name;
    pCmd  p_cmd;
    char *p_help;
} plowCmds[] = {
    "clrdebug",		PC_CLRD,	"flags",
    "help",		PC_HELP,	"",
    "jogreduce",	PC_JOG,		"",
    "lwidth",		PC_LWIDTH,	"layers",
    "lshadow",		PC_LSHADOW,	"layers",
    "mergedown",	PC_MERGEDOWN,	"",
    "mergeup",		PC_MERGEUP,	"",
    "move",		PC_MOVE,	"",
    "outline",		PC_OUTLINE,	"direction layers",
    "plow",		PC_PLOW,	"direction [layers]",
    "print",		PC_PRINT,	"",
    "random",		PC_RANDOM,	"",
    "setdebug",		PC_SETD,	"flags",
    "shadow",		PC_SHADOW,	"layers",
    "showdebug",	PC_SHOWD,	"",
    "split",		PC_SPLIT,	"",
    "techshow",		PC_TECHSHOW,	"[file]",
    "trail",		PC_TRAIL,	"[value]",
    "whenbot",		PC_WHENBOT,	"[xbot ybot]",
    "whentop",		PC_WHENTOP,	"[xtop ytop]",
    "width",		PC_WIDTH,	"layers",
    0,
};

void
PlowTest(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    pCmd plowCmd, plowGetCommand();
    Rect editArea, dummyRect, rootBox, area2;
    CellDef *def, *rootBoxDef, *saveDef;
    TileTypeBitMask okTypes;
    int dir, n, trail;
    Point editPoint;
    Plane *plane;
    Edge edge;
    Tile *tp;
    FILE *f;

    if (!ToolGetEditBox(&editArea) || !ToolGetBox(&rootBoxDef, &rootBox))
	return;
    (void) CmdGetEditPoint(&editPoint, &dummyRect);

    if ((plowCmd = plowGetCommand(cmd)) == PC_ERROR)
	return;

    def = EditCellUse->cu_def;
    plane = def->cd_planes[PL_TECHDEPBASE];
    switch (plowCmd)
    {
	case PC_HELP:
	    TxPrintf("Usage: *plow cmd [args]\n");
	    TxPrintf("Valid plowing command are:\n");
	    for (n = 0; plowCmds[n].p_name; n++)
		TxPrintf("%-15s %s\n", plowCmds[n].p_name, plowCmds[n].p_help);
	    break;
	case PC_RANDOM:
	    PlowRandomTest(def);
	    break;
	case PC_JOG:
	    plowTestJog(def, &editArea);
	    break;
	case PC_SETD:
	    DebugSet(plowDebugID, cmd->tx_argc - 2, &cmd->tx_argv[2], TRUE);
	    break;
	case PC_CLRD:
	    DebugSet(plowDebugID, cmd->tx_argc - 2, &cmd->tx_argv[2], FALSE);
	    break;
	case PC_SHOWD:
	    DebugShow(plowDebugID);
	    break;
	case PC_OUTLINE:
	    if (cmd->tx_argc < 4)
	    {
		TxError("Usage: *plow outline direction layers\n");
		return;
	    }
	    dir = GeoNameToPos(cmd->tx_argv[2], TRUE, TRUE);
	    if (dir != GEO_NORTH && dir != GEO_SOUTH)
	    {
		TxError("Only legal starting directions are north or south\n");
		return;
	    }
	    if (!CmdParseLayers(cmd->tx_argv[3], &okTypes))
		return;
	    (void) plowSrOutline(PL_TECHDEPBASE, &editArea.r_ll, okTypes, dir,
			GMASK_NORTH|GMASK_SOUTH|GMASK_EAST|GMASK_WEST,
			plowShowOutline, (ClientData) &editArea);
	    break;
	case PC_PLOW:
	    if (cmd->tx_argc < 3)
	    {
		TxError("Usage: *plow plow direction [layers]\n");
		return;
	    }
	    dir = GeoNameToPos(cmd->tx_argv[2], TRUE, TRUE);
	    okTypes = DBAllTypeBits;
	    if (cmd->tx_argc > 3 && !CmdParseLayers(cmd->tx_argv[3], &okTypes))
		return;
	    if (!Plow(def, &editArea, okTypes, dir))
	    {
		TxPrintf("Reduced plow size since we ran into the barrier.\n");
		GeoTransRect(&EditToRootTransform, &editArea, &rootBox);
		ToolMoveBox(TOOL_BL, &rootBox.r_ll, FALSE, rootBoxDef);
		ToolMoveCorner(TOOL_TR, &rootBox.r_ur, FALSE, rootBoxDef);
	    }
	    break;
	case PC_LSHADOW:
	    if (cmd->tx_argc < 3)
	    {
		TxError("Usage: *plow lshadow layers\n");
		return;
	    }
	    if (!CmdParseLayers(cmd->tx_argv[2], &okTypes))
		return;
	    saveDef = plowYankDef;
	    plowYankDef = def;
	    (void) plowSrShadowBack(PL_TECHDEPBASE, &editArea,
			okTypes, plowShowShadow, (ClientData) def);
	    plowYankDef = saveDef;
	    break;
	case PC_SHADOW:
	    if (cmd->tx_argc < 3)
	    {
		TxError("Usage: *plow shadow layers\n");
		return;
	    }
	    if (!CmdParseLayers(cmd->tx_argv[2], &okTypes))
		return;
	    saveDef = plowYankDef;
	    plowYankDef = def;
	    (void) plowSrShadow(PL_TECHDEPBASE, &editArea,
			okTypes, plowShowShadow, (ClientData) def);
	    plowYankDef = saveDef;
	    break;
	case PC_TECHSHOW:
	    f = stdout;
	    if (cmd->tx_argc >= 3)
	    {
		f = fopen(cmd->tx_argv[2], "w");
		if (f == NULL)
		{
		    perror(cmd->tx_argv[2]);
		    break;
		}
	    }
	    plowTechShow(f);
	    if (f != stdout)
		(void) fclose(f);
	    break;
	case PC_WIDTH:
	    if (cmd->tx_argc < 3)
	    {
		TxError("Usage: *plow width layers\n");
		return;
	    }
	    if (!CmdParseLayers(cmd->tx_argv[2], &okTypes))
		return;
	    edge.e_rect = editArea;
	    edge.e_pNum = PL_TECHDEPBASE;
	    TxPrintf("Box: X: %d .. %d  Y: %d .. %d\n",
		editArea.r_xbot, editArea.r_xtop,
		editArea.r_ybot, editArea.r_ytop);
	    saveDef = plowYankDef;
	    plowYankDef = def;
	    (void) plowFindWidth(&edge, okTypes, &def->cd_bbox, &editArea);
	    plowYankDef = saveDef;
	    GeoTransRect(&EditToRootTransform, &editArea, &rootBox);
	    ToolMoveBox(TOOL_BL, &rootBox.r_ll, FALSE, rootBoxDef);
	    ToolMoveCorner(TOOL_TR, &rootBox.r_ur, FALSE, rootBoxDef);
	    break;
	case PC_LWIDTH:
	    if (cmd->tx_argc < 3)
	    {
		TxError("Usage: *plow lwidth layers\n");
		return;
	    }
	    if (!CmdParseLayers(cmd->tx_argv[2], &okTypes))
		return;
	    edge.e_rect = editArea;
	    edge.e_pNum = PL_TECHDEPBASE;
	    TxPrintf("Box: X: %d .. %d  Y: %d .. %d\n",
		editArea.r_xbot, editArea.r_xtop,
		editArea.r_ybot, editArea.r_ytop);
	    saveDef = plowYankDef;
	    plowYankDef = def;
	    (void) plowFindWidthBack(&edge, okTypes, &def->cd_bbox, &editArea);
	    plowYankDef = saveDef;
	    GeoTransRect(&EditToRootTransform, &editArea, &rootBox);
	    ToolMoveBox(TOOL_BL, &rootBox.r_ll, FALSE, rootBoxDef);
	    ToolMoveCorner(TOOL_TR, &rootBox.r_ur, FALSE, rootBoxDef);
	    break;
	case PC_TRAIL:
	    if (cmd->tx_argc > 3)
	    {
		TxError("Usage: *plow trail [value]\n");
		return;
	    }
	    tp = TiSrPointNoHint(plane, &editArea.r_ll);
	    if (cmd->tx_argc == 3) trail = atoi(cmd->tx_argv[2]);
	    else trail = editArea.r_xtop;
	    TxPrintf("Trailing coordinate of tile 0x%x updated from %d to %d\n",
			tp, TRAILING(tp), trail);
	    plowSetTrailing(tp, trail);
	    break;
	case PC_MOVE:
	    edge.e_pNum = PL_TECHDEPBASE;
	    edge.e_rect = editArea;
	    TxPrintf("Moving edge from %d to %d\n",
			editArea.r_xbot, editArea.r_xtop);
	    plowMoveEdge(&edge);
	    editArea.r_xbot--;
	    DBWAreaChanged(def, &editArea, DBW_ALLWINDOWS, &DBAllButSpaceBits);
	    break;
	case PC_SPLIT:
	    tp = TiSrPointNoHint(plane, &editArea.r_ll);
	    if (editArea.r_ybot == BOTTOM(tp) || editArea.r_ybot == TOP(tp))
	    {
		TxError("Can't split at top or bottom of tile\n");
		return;
	    }
	    TiToRect(tp, &area2);
	    TxPrintf("Splitting tile 0x%x at y=%d yielding 0x%x\n",
			tp, editArea.r_ybot, plowSplitY(tp, editArea.r_ybot));
	    DBWAreaChanged(def, &area2, DBW_ALLWINDOWS, &DBAllButSpaceBits);
	    break;
	case PC_MERGEDOWN:
	    tp = TiSrPointNoHint(plane, &editArea.r_ll);
	    TxPrintf("Merging tile 0x%x below\n", tp);
	    TiToRect(tp, &editArea);
	    TiToRect(RT(tp), &area2);
	    (void) GeoInclude(&area2, &editArea);
	    plowMergeBottom(tp, plane);
	    DBWAreaChanged(def, &editArea, DBW_ALLWINDOWS, &DBAllButSpaceBits);
	    break;
	case PC_MERGEUP:
	    tp = TiSrPointNoHint(plane, &editArea.r_ll);
	    TxPrintf("Merging tile 0x%x above\n", tp);
	    TiToRect(tp, &editArea);
	    TiToRect(RT(tp), &area2);
	    (void) GeoInclude(&area2, &editArea);
	    plowMergeTop(tp, plane);
	    DBWAreaChanged(def, &editArea, DBW_ALLWINDOWS, &DBAllButSpaceBits);
	    break;
	case PC_PRINT:
	    tp = TiSrPointNoHint(plane, &editArea.r_ll);
	    TxPrintf("Tile 0x%x  LEFT=%d RIGHT=%d BOTTOM=%d TOP=%d\n",
		tp, LEFT(tp), RIGHT(tp), BOTTOM(tp), TOP(tp));
	    TxPrintf("    TRAILING=%d LEADING=%d TYPE=%s\n",
		TRAILING(tp), LEADING(tp), DBTypeLongName(TiGetTypeExact(tp)));
	    break;
	case PC_WHENTOP:
	    if (cmd->tx_argc == 2)
	    {
		plowWhenTop = FALSE;
		break;
	    }
	    if (cmd->tx_argc != 4)
	    {
		TxError("Usage: *plow whentop xtop ytop\n");
		break;
	    }
	    plowWhenTopPoint.p_x = atoi(cmd->tx_argv[2]);
	    plowWhenTopPoint.p_y = atoi(cmd->tx_argv[3]);
	    plowWhenTop = TRUE;
	    break;
	case PC_WHENBOT:
	    if (cmd->tx_argc == 2)
	    {
		plowWhenBot = FALSE;
		break;
	    }
	    if (cmd->tx_argc != 4)
	    {
		TxError("Usage: *plow whenbot xbot ybot\n");
		break;
	    }
	    plowWhenBotPoint.p_x = atoi(cmd->tx_argv[2]);
	    plowWhenBotPoint.p_y = atoi(cmd->tx_argv[3]);
	    plowWhenBot = TRUE;
	    break;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowGetCommand --
 *
 * Pull off the subcommand from the TxCommand passed to PlowTest()
 * above.  Verify that it is a valid command.
 *
 * Results:
 *	Returns PC_ERROR on error; otherwise, returns the command
 *	code (pCmd) corresponding to the command in cmd->tx_argv[1].
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

pCmd
plowGetCommand(cmd)
    TxCommand *cmd;
{
    int plowIndex;

    if (cmd->tx_argc <= 1)
    {
	TxError("Usage: *plow cmd [args]\n");
	return (PC_ERROR);
    }

    plowIndex = LookupStruct(cmd->tx_argv[1],
			(LookupTable *) plowCmds, sizeof plowCmds[0]);
    if (plowIndex < 0)
    {
	TxError("Bad plowing command '%s'.\n", cmd->tx_argv[1]);
	TxError("Try '*plow help' for a list of commands.\n");
	return (PC_ERROR);
    }

    return (plowCmds[plowIndex].p_cmd);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowDebugInit --
 *
 * Initialize debugging flags.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Registers various debugging flags with the debugging module.
 *
 * ----------------------------------------------------------------------------
 */

void
plowDebugInit()
{
    int n;
    static struct
    {
	char	*di_name;
	int	*di_id;
    } debug[] = {
	"addedge",	&plowDebAdd,
	"jogs",		&plowDebJogs,
	"moveedge",	&plowDebMove,
	"nextedge",	&plowDebNext,
	"time",		&plowDebTime,
	"width",	&plowDebWidth,
	"yankall",	&plowDebYankAll,
	0
    };

    /* Register ourselves with the debugging module */
    plowDebugID = DebugAddClient("plow", sizeof debug/sizeof debug[0]);
    for (n = 0; debug[n].di_name; n++)
	*(debug[n].di_id) = DebugAddFlag(plowDebugID, debug[n].di_name);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowShowShadow --
 *
 * Debug shadow search.  Called for each edge found, we display
 * feedback over the area of the edge.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Leaves feedback.
 *
 * ----------------------------------------------------------------------------
 */

void
plowShowShadow(edge, def)
    Edge *edge;
    CellDef *def;
{
    char mesg[512];
    int scaleFactor = 10;
    Rect edgeArea;

    (void) sprintf(mesg, "Edge between %s and %s",
		DBTypeLongName(edge->e_ltype), DBTypeLongName(edge->e_rtype));

    edgeArea.r_xbot = edge->e_x * scaleFactor - 1;
    edgeArea.r_xtop = edge->e_x * scaleFactor + 1;
    edgeArea.r_ybot = edge->e_ybot * scaleFactor;
    edgeArea.r_ytop = edge->e_ytop * scaleFactor;
    DBWFeedbackAdd(&edgeArea, mesg, def, scaleFactor, STYLE_SOLIDHIGHLIGHTS);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowTestJog --
 *
 * Test jog elimination.
 * Yank the area enclosed by the box, plus a TechHalo-lambda halo around it,
 * into plowYankDef, and then call plowCleanupJogs() to reduce jogs in this
 * yank buffer.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies plowYankDef.
 *	Redisplays.
 *
 * ----------------------------------------------------------------------------
 */

void
plowTestJog(def, area)
    CellDef *def;
    Rect *area;
{
    extern CellUse *plowYankUse;
    extern Rect plowYankedArea;
    extern CellDef *plowYankDef;
    Rect changedArea;
    SearchContext scx;
    PaintUndoInfo ui;

    /* Make sure the yank buffers exist */
    plowYankCreate();

    /* Yank into yank buffer */
    UndoDisable();
    DBCellClearDef(plowYankDef);
    plowDummyUse->cu_def = def;
    scx.scx_use = plowDummyUse;
    scx.scx_trans = GeoIdentityTransform;
    scx.scx_area.r_xbot = area->r_xbot - DRCTechHalo;
    scx.scx_area.r_ybot = area->r_ybot - DRCTechHalo;
    scx.scx_area.r_xtop = area->r_xtop + DRCTechHalo;
    scx.scx_area.r_ytop = area->r_ytop + DRCTechHalo;
    (void) DBCellCopyPaint(&scx, &DBAllButSpaceAndDRCBits, 0, plowYankUse);
    DBReComputeBbox(plowYankDef);
    DBWAreaChanged(plowYankDef,&TiPlaneRect,DBW_ALLWINDOWS,&DBAllButSpaceBits);
    plowYankedArea = TiPlaneRect;

    /* Reduce jogs */
    changedArea.r_xbot = changedArea.r_xtop = 0;
    changedArea.r_ybot = changedArea.r_ytop = 0;
    plowCleanupJogs(area, &changedArea);
    DBReComputeBbox(plowYankDef);
    DBWAreaChanged(plowYankDef,&changedArea,DBW_ALLWINDOWS,&DBAllButSpaceBits);
    UndoEnable();

    /* Erase area in original def */
    ui.pu_def = def;
    for (ui.pu_pNum = PL_TECHDEPBASE; ui.pu_pNum < DBNumPlanes; ui.pu_pNum++)
	DBPaintPlane(def->cd_planes[ui.pu_pNum], area,
			DBWriteResultTbl[TT_SPACE], &ui);

    /* Stuff from yank buffer back into original def */
    scx.scx_area = *area;
    scx.scx_use = plowYankUse;
    scx.scx_trans = GeoIdentityTransform;
    (void) DBCellCopyPaint(&scx, &DBAllButSpaceAndDRCBits, 0, plowDummyUse);
    DBReComputeBbox(def);
    DBWAreaChanged(def, area, DBW_ALLWINDOWS, &DBAllButSpaceBits);
    DRCCheckThis(def, TT_CHECKPAINT, area);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowDebugEdge --
 *
 * Display an edge for debugging purposes.
 * This consists of showing the edge, and its final position, highlighted
 * with feedback, then erasing the feedback after requesting "more" from
 * the user.
 *
 * If the user responds with "d" to the "more" request, everything is forcibly
 * redisplayed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None permanent.
 *
 * ----------------------------------------------------------------------------
 */

void
plowDebugEdge(edge, rtePtr, mesg)
    Edge *edge;
    RuleTableEntry *rtePtr;
    char *mesg;
{
    int scaleFactor = 10;
    Rect edgeArea;

    if (rtePtr)
	TxPrintf("Rule being applied: \"%s\"\n", rtePtr->rte_name);
    TxPrintf("%s edge (%s :: %s) YL=%d YH=%d X=%d XNEW=%d", mesg,
		DBTypeShortName(edge->e_ltype), DBTypeShortName(edge->e_rtype),
		edge->e_ybot, edge->e_ytop, edge->e_x, edge->e_newx);

    /* LHS */
    edgeArea.r_xbot = edge->e_x * scaleFactor - 1;
    edgeArea.r_xtop = edge->e_x * scaleFactor + 1;
    edgeArea.r_ybot = edge->e_ybot * scaleFactor;
    edgeArea.r_ytop = edge->e_ytop * scaleFactor;
    DBWFeedbackAdd(&edgeArea, "", plowYankDef,
		scaleFactor, STYLE_SOLIDHIGHLIGHTS);

    /* RHS */
    edgeArea.r_xbot = edge->e_newx * scaleFactor - 1;
    edgeArea.r_xtop = edge->e_newx * scaleFactor + 1;
    edgeArea.r_ybot = edge->e_ybot * scaleFactor;
    edgeArea.r_ytop = edge->e_ytop * scaleFactor;
    DBWFeedbackAdd(&edgeArea, "", plowYankDef,
		scaleFactor, STYLE_MEDIUMHIGHLIGHTS);

    /* TOP */
    edgeArea.r_xbot = edge->e_x * scaleFactor;
    edgeArea.r_xtop = edge->e_newx * scaleFactor;
    edgeArea.r_ybot = edge->e_ytop * scaleFactor - 1;
    edgeArea.r_ytop = edge->e_ytop * scaleFactor + 1;
    DBWFeedbackAdd(&edgeArea, "", plowYankDef,
		scaleFactor, STYLE_MEDIUMHIGHLIGHTS);

    /* BOTTOM */
    edgeArea.r_xbot = edge->e_x * scaleFactor;
    edgeArea.r_xtop = edge->e_newx * scaleFactor;
    edgeArea.r_ybot = edge->e_ybot * scaleFactor - 1;
    edgeArea.r_ytop = edge->e_ybot * scaleFactor + 1;
    DBWFeedbackAdd(&edgeArea, "", plowYankDef,
		scaleFactor, STYLE_MEDIUMHIGHLIGHTS);
    WindUpdate();

    plowDebugMore();
    DBWFeedbackClear(NULL);
    WindUpdate();
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowDebugMore --
 *
 * Request "more" from the user.
 * If the user responds with "d" to the "more" request,
 * everything is forcibly redisplayed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May cause redisplay.
 *
 * ----------------------------------------------------------------------------
 */

void
plowDebugMore()
{
    char answer[100];

again:
    if (TxGetLinePrompt(answer, sizeof answer, " -- more -- ") == NULL)
	return;

    if (answer[0] == 'd')
    {
	DBWAreaChanged(plowYankDef, &TiPlaneRect,
			DBW_ALLWINDOWS, &DBAllButSpaceBits);
	WindUpdate();
	goto again;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowShowOutline --
 *
 * Debug outline search.  Called for each outline segment found, we display
 * feedback over the area of the segment and then ask for more.
 *
 * Results:
 *	Normally returns 0, but will return 1 if the boundary segment
 *	leaves the clipping rectangle clipArea, or if its endpoint is
 *	the lower-left of the clipping rectangle.
 *
 * Side effects:
 *	Leaves feedback.
 *
 * ----------------------------------------------------------------------------
 */

int
plowShowOutline(outline, clipArea)
    Outline *outline;
    Rect *clipArea;
{
    static char *dirNames[] = {
	"center", "north", "northeast", "east",
	"southeast", "south", "southwest", "west",
	"northwest", "eastnorth", "eastsouth", "westnorth",
	"westsouth"
    };
    char mesg[512], prompt[612], answer[128];
    int scaleFactor = 10;
    Rect edgeArea;

    (void) sprintf(mesg, "%s/%s/%s segment in=%s out=%s",
		dirNames[outline->o_prevDir],
		dirNames[outline->o_currentDir],
		dirNames[outline->o_nextDir],
		DBTypeLongName(TiGetTypeExact(outline->o_inside)),
		DBTypeLongName(TiGetTypeExact(outline->o_outside)));

    switch (outline->o_currentDir)
    {
	case GEO_NORTH:
	case GEO_SOUTH:
	    edgeArea.r_xbot = outline->o_rect.r_xbot * scaleFactor - 1;
	    edgeArea.r_xtop = outline->o_rect.r_xbot * scaleFactor + 1;
	    edgeArea.r_ybot = outline->o_rect.r_ybot * scaleFactor;
	    edgeArea.r_ytop = outline->o_rect.r_ytop * scaleFactor;
	    break;
	case GEO_EAST:
	case GEO_WEST:
	    edgeArea.r_xbot = outline->o_rect.r_xbot * scaleFactor;
	    edgeArea.r_xtop = outline->o_rect.r_xtop * scaleFactor;
	    edgeArea.r_ybot = outline->o_rect.r_ybot * scaleFactor - 1;
	    edgeArea.r_ytop = outline->o_rect.r_ytop * scaleFactor + 1;
	    break;
    }

    DBWFeedbackAdd(&edgeArea, mesg, EditCellUse->cu_def,
		scaleFactor, STYLE_SOLIDHIGHLIGHTS);
    WindUpdate();
    (void) sprintf(prompt, "%s --more--", mesg);
    (void) TxGetLinePrompt(answer, sizeof answer, prompt);
    if (answer[0] == 'n')
	return (1);

#ifdef	notdef
    /* Does this segment cross the clipping area? */
    if (outline->o_rect.r_xtop >= clipArea->r_xtop
	    || outline->o_rect.r_ytop >= clipArea->r_ytop
	    || outline->o_rect.r_xbot < clipArea->r_xbot
	    || outline->o_rect.r_ybot < clipArea->r_ybot)
	return (1);
#endif	/* notdef */

    /* Are we back at the starting point? */
    switch (outline->o_currentDir)
    {
	case GEO_NORTH:
	case GEO_EAST:
	    if (outline->o_rect.r_xtop == clipArea->r_xbot
		    && outline->o_rect.r_ytop == clipArea->r_ybot)
		return (1);
	    break;
	case GEO_SOUTH:
	case GEO_WEST:
	    if (outline->o_rect.r_xbot == clipArea->r_xbot
		    && outline->o_rect.r_ybot == clipArea->r_ybot)
		return (1);
	    break;
    }

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowDisplay --
 *
 * Debugging procedure to redisplay the yank and spare buffers.
 * If 'dodef' is TRUE, we also redisplay the original cell.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Forces redisplay.
 *
 * ----------------------------------------------------------------------------
 */

void
plowDisplay(dodef)
    bool dodef;
{
    if (dodef)
	DBWAreaChanged(plowDummyUse->cu_def, &TiPlaneRect,
		    DBW_ALLWINDOWS, &DBAllButSpaceBits);

    DBWAreaChanged(plowYankDef, &TiPlaneRect,
		DBW_ALLWINDOWS, &DBAllButSpaceBits);
    DBWAreaChanged(plowSpareDef, &TiPlaneRect,
		DBW_ALLWINDOWS, &DBAllButSpaceBits);
    WindUpdate();
}
