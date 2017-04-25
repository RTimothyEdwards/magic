/*
 * ExtTest.c --
 *
 * Circuit extraction.
 * Interface for testing.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/extract/ExtTest.c,v 1.3 2009/05/13 15:03:16 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/utils.h"
#include "utils/geometry.h"
#include "utils/styles.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/malloc.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "dbwind/dbwind.h"
#include "utils/main.h"
#include "commands/commands.h"
#include "textio/textio.h"
#include "textio/txcommands.h"
#include "debug/debug.h"
#include "extract/extract.h"
#include "extract/extractInt.h"

int extDebAreaEnum;
int extDebArray;
int extDebHardWay;
int extDebHierCap;
int extDebHierAreaCap;
int extDebLabel;
int extDebNeighbor;
int extDebNoArray;
int extDebNoFeedback;
int extDebNoHard;
int extDebNoSubcell;
int extDebLength;
int extDebPerim;
int extDebResist;
int extDebVisOnly;
int extDebYank;

/*
 * The following are used for selective redisplay while debugging
 * the circuit extractor.
 */
Rect extScreenClip;
CellDef *extCellDef;
MagWindow *extDebugWindow;

/* The width of an edge in pixels when it is displayed */
int extEdgePixels = 4;

/* Forward declarations */
int extShowInter();
void extShowTech();
void extDispInit();
bool extShowRect();
void extMore();

void extShowTrans(char *, TileTypeBitMask *, FILE *);
void extShowMask(TileTypeBitMask *, FILE *);
void extShowPlanes(PlaneMask, FILE *);
void extShowConnect(char *, TileTypeBitMask *, FILE *);


/*
 * ----------------------------------------------------------------------------
 *
 * ExtractTest --
 *
 * Command interface for testing circuit extraction.
 * Usage:
 *	*extract
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Extracts the current cell, writing a file named
 *	currentcellname.ext.
 *
 * ----------------------------------------------------------------------------
 */

void
ExtractTest(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    extern long extSubtreeTotalArea;
    extern long extSubtreeInteractionArea;
    extern long extSubtreeClippedArea;
    static Plane *interPlane = (Plane *) NULL;
    static long areaTotal = 0, areaInteraction = 0, areaClipped = 0;
    long a1, a2;
    int n, halo, bloat;
    CellUse *selectedCell;
    Rect editArea;
    char *addr, *name;
    FILE *f;
    typedef enum {  CLRDEBUG, CLRLENGTH, DRIVER, DUMP, INTERACTIONS,
		    INTERCOUNT, EXTPARENTS, RECEIVER, SETDEBUG, SHOWDEBUG,
		    SHOWPARENTS, SHOWTECH, STATS, STEP, TIME } cmdType;
    static struct
    {
	char	*cmd_name;
	cmdType	 cmd_val;
    } cmds[] = {
	"clrdebug",		CLRDEBUG,
	"clrlength",		CLRLENGTH,
	"driver",		DRIVER,
	"dump",			DUMP,
	"interactions",		INTERACTIONS,
	"intercount",		INTERCOUNT,
	"parents",		EXTPARENTS,
	"receiver",		RECEIVER,
	"setdebug",		SETDEBUG,
	"showdebug",		SHOWDEBUG,
	"showparents",		SHOWPARENTS,
	"showtech",		SHOWTECH,
	"stats",		STATS,
	"step",			STEP,
	"times",		TIME,
	0
    };

    if (cmd->tx_argc == 1)
    {
	selectedCell = CmdGetSelectedCell((Transform *) NULL);
	if (selectedCell == NULL)
	{
	    TxError("No cell selected\n");
	    return;
	}

	extDispInit(selectedCell->cu_def, w);
	ExtCell(selectedCell->cu_def, selectedCell->cu_def->cd_name, FALSE);
	return;
    }

    n = LookupStruct(cmd->tx_argv[1], (LookupTable *) cmds, sizeof cmds[0]);
    if (n < 0)
    {
	TxError("Unrecognized subcommand: %s\n", cmd->tx_argv[1]);
	TxError("Valid subcommands:");
	for (n = 0; cmds[n].cmd_name; n++)
	    TxError(" %s", cmds[n].cmd_name);
	TxError("\n");
	return;
    }

    switch (cmds[n].cmd_val)
    {
	case STATS:
	    areaTotal += extSubtreeTotalArea;
	    areaInteraction += extSubtreeInteractionArea;
	    areaClipped += extSubtreeClippedArea;
	    TxPrintf("Extraction statistics (recent/total):\n");
	    TxPrintf("Total area of all cells = %ld / %ld\n",
			extSubtreeTotalArea, areaTotal);
	    a1 = extSubtreeTotalArea;
	    a2 = areaTotal;
	    if (a1 == 0) a1 = 1;
	    if (a2 == 0) a2 = 1;
	    TxPrintf(
	    "Total interaction area processed = %ld (%.2f%%) / %ld (%.2f%%)\n",
		extSubtreeInteractionArea,
		((double) extSubtreeInteractionArea) / ((double) a1) * 100.0,
		((double) areaInteraction) / ((double) a2) * 100.0);
	    TxPrintf(
	    "Clipped interaction area= %ld (%.2f%%) / %ld (%.2f%%)\n",
		extSubtreeClippedArea,
		((double) extSubtreeClippedArea) / ((double) a1) * 100.0,
		((double) areaClipped) / ((double) a2) * 100.0);
	    extSubtreeTotalArea = 0;
	    extSubtreeInteractionArea = 0;
	    extSubtreeClippedArea = 0;
	    break;
	case INTERACTIONS:
	    if (interPlane == NULL)
		interPlane = DBNewPlane((ClientData) TT_SPACE);
	    halo = 1, bloat = 0;
	    if (cmd->tx_argc > 2) halo = atoi(cmd->tx_argv[2]) + 1;
	    if (cmd->tx_argc > 3) bloat = atoi(cmd->tx_argv[3]);
	    ExtFindInteractions(EditCellUse->cu_def, halo, bloat, interPlane);
	    (void) DBSrPaintArea((Tile *) NULL, interPlane, &TiPlaneRect,
			&DBAllButSpaceBits, extShowInter, (ClientData) NULL);
	    DBClearPaintPlane(interPlane);
	    break;
	case INTERCOUNT:
	    f = stdout;
	    halo = 1;
	    if (cmd->tx_argc > 2)
		halo = atoi(cmd->tx_argv[2]);
	    if (cmd->tx_argc > 3)
	    {
		f = fopen(cmd->tx_argv[3], "w");
		if (f == NULL)
		{
		    perror(cmd->tx_argv[3]);
		    break;
		}
	    }
	    ExtInterCount((CellUse *) w->w_surfaceID, halo, f);
	    if (f != stdout)
		(void) fclose(f);
	    break;
	case TIME:
	    f = stdout;
	    if (cmd->tx_argc > 2)
	    {
		f = fopen(cmd->tx_argv[2], "w");
		if (f == NULL)
		{
		    perror(cmd->tx_argv[2]);
		    break;
		}
	    }
	    ExtTimes((CellUse *) w->w_surfaceID, f);
	    if (f != stdout)
		(void) fclose(f);
	    break;
	case EXTPARENTS:
	    if (ToolGetEditBox(&editArea))
		ExtParentArea(EditCellUse, &editArea, TRUE);
	    break;

	case DUMP:
	    if (cmd->tx_argc != 2 && cmd->tx_argc != 3)
	    {
		TxError("Usage: *extract dump filename|-\n");
		break;
	    }
	    ExtDumpCaps(cmd->tx_argc > 2 ? cmd->tx_argv[2] : "-");
	    break;

	case DRIVER:
	    if (cmd->tx_argc != 3)
	    {
		TxError("Usage: *extract driver terminalname\n");
		break;
	    }
	    ExtSetDriver(cmd->tx_argv[2]);
	    break;
	case RECEIVER:
	    if (cmd->tx_argc != 3)
	    {
		TxError("Usage: *extract receiver terminalname\n");
		break;
	    }
	    ExtSetReceiver(cmd->tx_argv[2]);
	    break;
	case CLRLENGTH:
	    TxPrintf("Clearing driver/receiver length list\n");
	    ExtLengthClear();
	    break;

	case SHOWPARENTS:
	    if (ToolGetEditBox(&editArea))
		ExtParentArea(EditCellUse, &editArea, FALSE);
	    break;
	case SETDEBUG:
	    DebugSet(extDebugID, cmd->tx_argc - 2, &cmd->tx_argv[2], TRUE);
	    break;
	case CLRDEBUG:
	    DebugSet(extDebugID, cmd->tx_argc - 2, &cmd->tx_argv[2], FALSE);
	    break;

	case SHOWDEBUG:
	    DebugShow(extDebugID);
	    break;
	case SHOWTECH:
	    extShowTech(cmd->tx_argc > 2 ? cmd->tx_argv[2] : "-");
	    break;
	case STEP:
	    TxPrintf("Current interaction step size is %d\n",
		    ExtCurStyle->exts_stepSize);
	    if (cmd->tx_argc > 2)
	    {
		ExtCurStyle->exts_stepSize = atoi(cmd->tx_argv[2]);
		TxPrintf("New interaction step size is %d\n",
			ExtCurStyle->exts_stepSize);
	    }
	    break;
    }
}

int
extShowInter(tile)
    Tile *tile;
{
    Rect r;

    TiToRect(tile, &r);
    DBWFeedbackAdd(&r, "interaction", EditCellUse->cu_def,
	    1, STYLE_MEDIUMHIGHLIGHTS);

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extShowTech --
 *
 * Display the technology-specific tables maintained for circuit
 * extraction in a human-readable format.  Intended mainly for
 * debugging technology files.  If the argument 'name' is "-",
 * the output is to the standard output; otherwise, it is to
 * the file whose name is 'name'.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
extShowTech(name)
    char *name;
{
    FILE *out;
    TileType t, s;
    int p;
    EdgeCap *e;

    if (strcmp(name, "-") == 0)
	out = stdout;
    else
    {
	out = fopen(name, "w");
	if (out == NULL)
	{
	    perror(name);
	    return;
	}
    }

    extShowTrans("Transistor", &ExtCurStyle->exts_transMask, out);

    fprintf(out, "\nNode resistance and capacitance:\n");
    fprintf(out, "type     R-ohm/sq  AreaC-ff/l**2\n");
    for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
	fprintf(out, "%-8.8s %8d      %9lf\n",
		    DBTypeShortName(t),
		    ExtCurStyle->exts_resistByResistClass[
			ExtCurStyle->exts_typeToResistClass[t]],
		    ExtCurStyle->exts_areaCap[t]);

    fprintf(out, "\nTypes contributing to resistive perimeter:\n");
    fprintf(out, "type     R-type boundary types\n");
    for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
    {
	fprintf(out, "%-8.8s ", DBTypeShortName(t));
	fprintf(out, "%7d ", ExtCurStyle->exts_typeToResistClass[t]);
	extShowMask(&ExtCurStyle->exts_typesResistChanged[t], out);
	fprintf(out, "\n");
    }

    fprintf(out, "\nSidewall capacitance:\n");
    for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
	for (s = 0; s < DBNumTypes; s++)
	    if (ExtCurStyle->exts_perimCap[t][s] != (CapValue) 0)
		fprintf(out, "    %-8.8s %-8.8s %8lf\n",
			DBTypeShortName(t), DBTypeShortName(s),
			ExtCurStyle->exts_perimCap[t][s]);

    fprintf(out, "\nInternodal overlap capacitance:\n");
    fprintf(out, "\n  (by plane)\n");
    for (p = PL_TECHDEPBASE; p < DBNumPlanes; p++)
    {
	if (PlaneMaskHasPlane(ExtCurStyle->exts_overlapPlanes	, p))
	{
	    fprintf(out, "    %-10.10s: types=", DBPlaneShortName(p));
	    extShowMask(&ExtCurStyle->exts_overlapTypes	[p], out);
	    fprintf(out, "\n");
	}
    }
    fprintf(out, "\n  (by type)\n");
    for (t = 0; t < DBNumTypes; t++)
	if (!TTMaskIsZero(&ExtCurStyle->exts_overlapOtherTypes[t]))
	{
	    fprintf(out, "    %-10.10s: planes=", DBTypeShortName(t));
	    extShowPlanes(ExtCurStyle->exts_overlapOtherPlanes[t], out);
	    fprintf(out, "\n      overlapped types=");
	    extShowMask(&ExtCurStyle->exts_overlapOtherTypes[t], out);
	    fprintf(out, "\n");
	    for (s = 0; s < DBNumTypes; s++)
		if (ExtCurStyle->exts_overlapCap[t][s] != (CapValue) 0)
		    fprintf(out, "              %-10.10s: %8lf\n",
				DBTypeShortName(s), ExtCurStyle->exts_overlapCap[t][s]);
	}

    fprintf(out, "\nSidewall-coupling/sidewall-overlap capacitance:\n");
    fprintf(out, "\n  (by plane)\n");
    for (p = PL_TECHDEPBASE; p < DBNumPlanes; p++)
    {
	if (PlaneMaskHasPlane(ExtCurStyle->exts_sidePlanes, p))
	{
	    fprintf(out, "    %-10.10s: ", DBPlaneShortName(p));
	    extShowMask(&ExtCurStyle->exts_sideTypes[p], out);
	    fprintf(out, "\n");
	}
    }
    fprintf(out, "\n  (by type)\n");
    for (s = 0; s < DBNumTypes; s++)
	if (!TTMaskIsZero(&ExtCurStyle->exts_sideEdges[s]))
	{
	    fprintf(out, "    %-10.10s: ", DBTypeShortName(s));
	    extShowMask(&ExtCurStyle->exts_sideEdges[s], out);
	    fprintf(out, "\n");
	    for (t = 0; t < DBNumTypes; t++)
	    {
		if (!TTMaskIsZero(&ExtCurStyle->exts_sideCoupleOtherEdges[s][t]))
		{
		    fprintf(out, "                edge mask=");
		    extShowMask(&ExtCurStyle->exts_sideCoupleOtherEdges[s][t], out);
		    fprintf(out, "\n");
		}
		if (!TTMaskIsZero(&ExtCurStyle->exts_sideOverlapOtherTypes[s][t]))
		{
		    fprintf(out, "                overlap mask=");
		    extShowMask(&ExtCurStyle->exts_sideOverlapOtherTypes[s][t],
				out);
		    fprintf(out, "\n");
		}
		if (e = ExtCurStyle->exts_sideCoupleCap[s][t])
		    for ( ; e; e = e->ec_next)
		    {
			fprintf(out, "                COUPLE: ");
			extShowMask(&e->ec_near, out);
			fprintf(out, " || ");
			extShowMask(&e->ec_far, out);
			fprintf(out, ": %lf\n", e->ec_cap);
		    }
		if (e = ExtCurStyle->exts_sideOverlapCap[s][t])
		    for ( ; e; e = e->ec_next)
		    {
			fprintf(out, "                OVERLAP: ");
			extShowMask(&e->ec_near, out);
			fprintf(out, ": %lf\n", e->ec_cap);
		    }
	    }
	}

    fprintf(out, "\n\nSidewall coupling halo = %d\n", ExtCurStyle->exts_sideCoupleHalo	);

    extShowConnect("\nNode connectivity", ExtCurStyle->exts_nodeConn, out);
    extShowConnect("\nResistive region connectivity", ExtCurStyle->exts_resistConn, out);
    extShowConnect("\nTransistor connectivity", ExtCurStyle->exts_transConn, out);

    if (out != stdout)
	(void) fclose(out);
}

void
extShowTrans(name, mask, out)
    char *name;
    TileTypeBitMask *mask;
    FILE *out;
{
    TileType t;

    fprintf(out, "%s types: ", name);
    extShowMask(mask, out);
    fprintf(out, "\n");

    for (t = 0; t < DBNumTypes; t++)
	if (TTMaskHasType(mask, t))
	{
	    fprintf(out, "    %-8.8s  %d terminals: ",
			DBTypeShortName(t), ExtCurStyle->exts_transSDCount[t]);
	    extShowMask(&ExtCurStyle->exts_transSDTypes[t][0], out);
	    fprintf(out, "\n\tcap (gate-sd/gate-ch) = %lf/%lf\n",
			ExtCurStyle->exts_transSDCap[t],
			ExtCurStyle->exts_transGateCap[t]);
	}
}

void
extShowConnect(hdr, connectsTo, out)
    char *hdr;
    TileTypeBitMask *connectsTo;
    FILE *out;
{
    TileType t;

    fprintf(out, "%s\n", hdr);
    for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
	if (!TTMaskEqual(&connectsTo[t], &DBZeroTypeBits))
	{
	    fprintf(out, "    %-8.8s: ", DBTypeShortName(t));
	    extShowMask(&connectsTo[t], out);
	    fprintf(out, "\n");
	}
}

void
extShowMask(m, out)
    TileTypeBitMask *m;
    FILE *out;
{
    TileType t;
    bool first = TRUE;

    for (t = 0; t < DBNumTypes; t++)
	if (TTMaskHasType(m, t))
	{
	    if (!first)
		fprintf(out, ",");
	    first = FALSE;
	    fprintf(out, "%s", DBTypeShortName(t));
	}
}

void
extShowPlanes(m, out)
    PlaneMask m;
    FILE *out;
{
    int pNum;
    bool first = TRUE;

    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	if (PlaneMaskHasPlane(m, pNum))
	{
	    if (!first)
		fprintf(out, ",");
	    first = FALSE;
	    fprintf(out, "%s", DBPlaneShortName(pNum));
	}
}

/*
 * ----------------------------------------------------------------------------
 *
 * extDispInit --
 *
 * Initialize the screen information to be used during
 * extraction debugging.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initializes extDebugWindow, extScreenClip, and extCellDef.
 *
 * ----------------------------------------------------------------------------
 */

void
extDispInit(def, w)
    CellDef *def;
    MagWindow *w;
{
    extDebugWindow = w;
    extCellDef = def;
    extScreenClip = w->w_screenArea;
    GeoClip(&extScreenClip, &GrScreenRect);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extShowEdge --
 *
 * Display the edge described by the Boundary 'bp' on the display,
 * with text string 's' on the text terminal.  Prompt with '--next--'
 * to allow a primitive sort of 'more' processing.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the display.
 *
 * ----------------------------------------------------------------------------
 */

void
extShowEdge(s, bp)
    char *s;
    Boundary *bp;
{
    Rect extScreenRect, edgeRect;
    int style = STYLE_PURPLE1;

    edgeRect = bp->b_segment;
    WindSurfaceToScreen(extDebugWindow, &edgeRect, &extScreenRect);
    if (extScreenRect.r_ybot == extScreenRect.r_ytop)
    {
	extScreenRect.r_ybot -= extEdgePixels/2;
	extScreenRect.r_ytop += extEdgePixels - extEdgePixels/2;
    }
    else /* extScreenRect.r_xtop == extScreenRect.r_xbot */
    {
	extScreenRect.r_xbot -= extEdgePixels/2;
	extScreenRect.r_xtop += extEdgePixels - extEdgePixels/2;
    }

    if (DebugIsSet(extDebugID, extDebVisOnly))
    {
	Rect r;

	r = extScreenRect;
	GeoClip(&r, &extScreenClip);
	if (r.r_xtop <= r.r_xbot || r.r_ytop <= r.r_ybot)
	    return;
    }

    TxPrintf("%s: ", s);
    GrLock(extDebugWindow, TRUE);
    GrClipBox(&extScreenRect, style);
    GrUnlock(extDebugWindow);
    (void) GrFlush();
    extMore();
    GrLock(extDebugWindow, TRUE);
    GrClipBox(&extScreenRect, STYLE_ORANGE1);
    GrUnlock(extDebugWindow);
    (void) GrFlush();
}

/*
 * ----------------------------------------------------------------------------
 *
 * extShowTile --
 *
 * Display the tile 'tp' on the display by highlighting it.  Also show
 * the text string 's' on the terminal.  Prompt with '--next--' to allow
 * a primitive sort of more processing.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the display.
 *
 * ----------------------------------------------------------------------------
 */

void
extShowTile(tile, s, style_index)
    Tile *tile;
    char *s;
    int style_index;
{
    Rect tileRect;
    static int styles[] = { STYLE_PALEHIGHLIGHTS, STYLE_DOTTEDHIGHLIGHTS };

    TiToRect(tile, &tileRect);
    if (!extShowRect(&tileRect, styles[style_index]))
	return;

    TxPrintf("%s: ", s);
    extMore();
    (void) extShowRect(&tileRect, STYLE_ERASEHIGHLIGHTS);
}

bool
extShowRect(r, style)
    Rect *r;
    int style;
{
    Rect extScreenRect;

    WindSurfaceToScreen(extDebugWindow, r, &extScreenRect);
    if (DebugIsSet(extDebugID, extDebVisOnly))
    {
	Rect rclip;

	rclip = extScreenRect;
	GeoClip(&rclip, &extScreenClip);
	if (rclip.r_xtop <= rclip.r_xbot || rclip.r_ytop <= rclip.r_ybot)
	    return (FALSE);
    }

    GrLock(extDebugWindow, TRUE);
    GrClipBox(&extScreenRect, style);
    GrUnlock(extDebugWindow);
    (void) GrFlush();
    return (TRUE);
}

void
extMore()
{
    char line[100];

    TxPrintf("--next--"); (void) fflush(stdout);
    (void) TxGetLine(line, sizeof line);
}

void
extNewYank(name, puse, pdef)
    char *name;
    CellUse **puse;
    CellDef **pdef;
{
    DBNewYank(name, puse, pdef);
}

/*
 * ----------------------------------------------------------------------------
 * Dump parasitic capacitance extraction information to a file
 * ----------------------------------------------------------------------------
 */

void
ExtDumpCapsToFile(f)
    FILE *f;
{
    TileType t, s, r;
    EdgeCap *e;
    int p, found;

    fprintf(f, "Parasitic extraction capacitance values\n");
    
    fprintf(f, "\n1) Area caps\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	if (ExtCurStyle->exts_areaCap[t] > 0.0)
	    fprintf(f, "%s  %3.3f\n",
			DBTypeLongNameTbl[t],
			ExtCurStyle->exts_areaCap[t]);
    }

    fprintf(f, "\n2) Perimeter caps\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	for (s = 0; s < DBNumTypes; s++)
	{
	    if (ExtCurStyle->exts_perimCap[t][s] > 0.0)
		fprintf(f, "%s | %s  %3.3f\n",
			DBTypeLongNameTbl[t],
			DBTypeLongNameTbl[s],
			ExtCurStyle->exts_perimCap[t][s]);
	}
    }

    fprintf(f, "\n3) Overlap caps\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	for (s = 0; s < DBNumTypes; s++)
	{
	    if (ExtCurStyle->exts_overlapCap[t][s] > 0.0)
		fprintf(f, "%s | %s  %3.3f\n",
			DBTypeLongNameTbl[t],
			DBTypeLongNameTbl[s],
			ExtCurStyle->exts_overlapCap[t][s]);
	}
    }

    fprintf(f, "\n4) Side coupling caps\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	for (s = 0; s < DBNumTypes; s++)
	{
	    for (e = ExtCurStyle->exts_sideCoupleCap[t][s]; e; e = e->ec_next) {
		fprintf(f, "%s | %s:  %3.3f\n",
			DBTypeLongNameTbl[t],
			DBTypeLongNameTbl[s],
			e->ec_cap);
		fprintf(f, "   near: ");
		for (r = 0; r < DBNumTypes; r++)
		    if (TTMaskHasType(&e->ec_near, r))
			fprintf(f, " %s", DBTypeLongNameTbl[r]);
		fprintf(f, "\n   far: ");
		for (r = 0; r < DBNumTypes; r++)
		    if (TTMaskHasType(&e->ec_far, r))
			fprintf(f, " %s", DBTypeLongNameTbl[r]);
		fprintf(f, "\n   planes: ");
		for (p = PL_TECHDEPBASE; p < DBNumPlanes; p++)
		    if (PlaneMaskHasPlane(e->ec_pmask, p))
			fprintf(f, " %s", DBPlaneLongNameTbl[p]);
		fprintf(f, "\n");
	    }
	}
    }

    fprintf(f, "\n5) Side overlap caps\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	for (s = 0; s < DBNumTypes; s++)
	{
	    for (e = ExtCurStyle->exts_sideOverlapCap[t][s]; e; e = e->ec_next) {
		fprintf(f, "%s | %s:  %3.3f\n",
			DBTypeLongNameTbl[t],
			DBTypeLongNameTbl[s],
			e->ec_cap);
		fprintf(f, "   near: ");
		for (r = 0; r < DBNumTypes; r++)
		    if (TTMaskHasType(&e->ec_near, r))
			fprintf(f, " %s", DBTypeLongNameTbl[r]);
		fprintf(f, "\n   far: ");
		for (r = 0; r < DBNumTypes; r++)
		    if (TTMaskHasType(&e->ec_far, r))
			fprintf(f, " %s", DBTypeLongNameTbl[r]);
		fprintf(f, "\n   planes: ");
		for (p = PL_TECHDEPBASE; p < DBNumPlanes; p++)
		    if (PlaneMaskHasPlane(e->ec_pmask, p))
			fprintf(f, " %s", DBPlaneLongNameTbl[p]);
		fprintf(f, "\n");
	    }
	}
    }

    fprintf(f, "\n6) (Check) Perimeter cap mask\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	found = 0;
	for (s = 0; s < DBNumTypes; s++)
	    if (TTMaskHasType(&ExtCurStyle->exts_perimCapMask[t], s))
	    {
		if (found == 0)
		{
		    fprintf(f, "   %s: ", DBTypeLongNameTbl[t]);
		    found = 1;
		}
		fprintf(f, " %s", DBTypeLongNameTbl[s]);
	    }
	if (found != 0) fprintf(f, "\n");
    }

    fprintf(f, "\n7) (Check) Overlap plane mask\n");
    for (p = PL_TECHDEPBASE; p < DBNumPlanes; p++)
	if (PlaneMaskHasPlane(ExtCurStyle->exts_overlapPlanes, p))
	    fprintf(f, " %s", DBPlaneLongNameTbl[p]);
    fprintf(f, "\n");

    fprintf(f, "\n8) (Check) Overlap types mask\n");
    for (p = PL_TECHDEPBASE; p < DBNumPlanes; p++)
    {
	found = 0;
	for (s = 0; s < DBNumTypes; s++)
	    if (TTMaskHasType(&ExtCurStyle->exts_overlapTypes[p], s))
	    {
		if (found == 0)
		{
		    found = 1;
		    fprintf(f, "   %s: ", DBPlaneLongNameTbl[p]);
		}
		fprintf(f, " %s", DBTypeLongNameTbl[s]);
	    }
	if (found != 0) fprintf(f, "\n");
    }

    fprintf(f, "\n9) (Check) Overlap other types mask\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	found = 0;
	for (s = 0; s < DBNumTypes; s++)
	    if (TTMaskHasType(&ExtCurStyle->exts_overlapOtherTypes[t], s))
	    {
		if (found == 0)
		{
		    fprintf(f, "   %s: ", DBTypeLongNameTbl[t]);
		    found = 1;
		}
		fprintf(f, " %s", DBTypeLongNameTbl[s]);
	     }
	if (found != 0) fprintf(f, "\n");
    }

    fprintf(f, "\n10) (Check) Overlap other planes mask\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	found = 0;
        for (p = PL_TECHDEPBASE; p < DBNumPlanes; p++)
	    if (PlaneMaskHasPlane(ExtCurStyle->exts_overlapOtherPlanes[t], p))
	    {
		if (found == 0)
		{
		    fprintf(f, "   %s: ", DBTypeLongNameTbl[t]);
		    found = 1;
		}
		fprintf(f, " %s", DBPlaneLongNameTbl[p]);
	    }
	if (found != 0) fprintf(f, "\n");
    }

    fprintf(f, "\n11) (Check) Overlap shield types mask\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	for (s = 0; s < DBNumTypes; s++)
	{
	    found = 0;
	    for (r = 0; r < DBNumTypes; r++)
		if (TTMaskHasType(&ExtCurStyle->exts_overlapShieldTypes[t][s], r))
		{
		    if (found == 0)
		    {
			fprintf(f, "   %s | %s: ",
				DBTypeLongNameTbl[t], DBTypeLongNameTbl[s]);
			found = 1;
		    }
		    fprintf(f, " %s", DBTypeLongNameTbl[r]);
		}
	    if (found != 0) fprintf(f, "\n");
	}
    }

    fprintf(f, "\n12) (Check) Overlap shield planes mask\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	for (s = 0; s < DBNumTypes; s++)
	{
	    found = 0;
	    for (p = PL_TECHDEPBASE; p < DBNumPlanes; p++)
		if (PlaneMaskHasPlane(ExtCurStyle->exts_overlapShieldPlanes[t][s], p))
		{
		    if (found == 0)
		    {
	    		fprintf(f, "   %s | %s: ",
				DBTypeLongNameTbl[t], DBTypeLongNameTbl[s]);
			found = 1;
		    }
		    fprintf(f, " %s", DBPlaneLongNameTbl[p]);
		}
	    if (found != 0) fprintf(f, "\n");
	}
    }

    fprintf(f, "\n13) (Check) Side couple other edges mask\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	for (s = 0; s < DBNumTypes; s++)
	{
	    found = 0;
	    for (r = 0; r < DBNumTypes; r++)
		if (TTMaskHasType(&ExtCurStyle->exts_sideCoupleOtherEdges[t][s], r))
		{
		    if (found == 0)
		    {
			fprintf(f, "   %s | %s: ",
				DBTypeLongNameTbl[t], DBTypeLongNameTbl[s]);
			found = 1;
		    }
		    fprintf(f, " %s", DBTypeLongNameTbl[r]);
		}
	    if (found != 0) fprintf(f, "\n");
	}
    }

    fprintf(f, "\n14) (Check) Side overlap other planes mask\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	for (s = 0; s < DBNumTypes; s++)
	{
	    found = 0;
	    for (p = PL_TECHDEPBASE; p < DBNumPlanes; p++)
		if (PlaneMaskHasPlane(ExtCurStyle->exts_sideOverlapOtherPlanes[t][s], p))
		{
		    if (found == 0)
		    {
			fprintf(f, "   %s | %s: ",
				DBTypeLongNameTbl[t], DBTypeLongNameTbl[s]);
			found = 1;
		    }
		    fprintf(f, " %s", DBPlaneLongNameTbl[p]);
		}
	    if (found != 0) fprintf(f, "\n");
	}
    }

    fprintf(f, "\n15) (Check) Side overlap other types mask\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	for (s = 0; s < DBNumTypes; s++)
	{
	    found = 0;
	    for (r = 0; r < DBNumTypes; r++)
		if (TTMaskHasType(&ExtCurStyle->exts_sideOverlapOtherTypes[t][s], r))
		{
		    if (found == 0)
		    {
			fprintf(f, "   %s | %s: ",
				DBTypeLongNameTbl[t], DBTypeLongNameTbl[s]);
			found = 1;
		    }
		    fprintf(f, " %s", DBTypeLongNameTbl[r]);
		}
	    if (found != 0) fprintf(f, "\n");
	}
    }

    fprintf(f, "\n16) (Check) Side overlap shield planes mask\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	for (s = 0; s < DBNumTypes; s++)
	{
	    found = 0;
	    for (p = PL_TECHDEPBASE; p < DBNumPlanes; p++)
		if (PlaneMaskHasPlane(ExtCurStyle->exts_sideOverlapShieldPlanes[t][s], p))
		{
		    if (found == 0)
		    {
			fprintf(f, "   %s | %s: ",
				DBTypeLongNameTbl[t], DBTypeLongNameTbl[s]);
			found = 1;
		    }
		    fprintf(f, " %s", DBPlaneLongNameTbl[p]);
		}
	    if (found != 0) fprintf(f, "\n");
	}
    }

    fprintf(f, "\n17) (Check) Side planes mask\n");
    for (p = PL_TECHDEPBASE; p < DBNumPlanes; p++)
	if (PlaneMaskHasPlane(ExtCurStyle->exts_sidePlanes, p))
	    fprintf(f, " %s", DBPlaneLongNameTbl[p]);
    fprintf(f, "\n");

    fprintf(f, "\n18) (Check) Side types mask\n");
    for (p = PL_TECHDEPBASE; p < DBNumPlanes; p++)
    {
	found = 0;
	for (s = 0; s < DBNumTypes; s++)
	    if (TTMaskHasType(&ExtCurStyle->exts_sideTypes[p], s))
	    {
		if (found == 0)
		{
		    fprintf(f, "   %s: ", DBPlaneLongNameTbl[p]);
		    found = 1;
		}
		fprintf(f, " %s", DBTypeLongNameTbl[s]);
	    }
	if (found != 0) fprintf(f, "\n");
    }

    fprintf(f, "\n19) (Check) Side edges mask\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	found = 0;
	for (s = 0; s < DBNumTypes; s++)
	    if (TTMaskHasType(&ExtCurStyle->exts_sideTypes[t], s))
	    {
		if (found == 0)
		{
		    fprintf(f, "   %s: ", DBTypeLongNameTbl[t]);
		    found = 1;
		}
		fprintf(f, " %s", DBTypeLongNameTbl[s]);
	    }
	if (found != 0) fprintf(f, "\n");
    }
}

/*
 * ----------------------------------------------------------------------------
 * ExtDumpCaps ---
 * Debugging tool.  Dump information about all parasitics
 * ----------------------------------------------------------------------------
 */

void
ExtDumpCaps(filename)
    char *filename;
{
    TileType t, s, r;
    EdgeCap *e;
    int p, found;

    if (strcmp(filename, "-"))
    {
	FILE *f;
	f = fopen(filename, "w");
	if (f == NULL)
	{
	    TxError("Cannot open file %s for writing\n", filename);
	    return;
	}
	ExtDumpCapsToFile(f);
	return;
    }

    TxPrintf("Parasitic extraction capacitance values\n");
    
    TxPrintf("\n1) Area caps\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	if (ExtCurStyle->exts_areaCap[t] > 0.0)
	    TxPrintf("%s  %3.3f\n",
			DBTypeLongNameTbl[t],
			ExtCurStyle->exts_areaCap[t]);
    }

    TxPrintf("\n2) Perimeter caps\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	for (s = 0; s < DBNumTypes; s++)
	{
	    if (ExtCurStyle->exts_perimCap[t][s] > 0.0)
		TxPrintf("%s | %s  %3.3f\n",
			DBTypeLongNameTbl[t],
			DBTypeLongNameTbl[s],
			ExtCurStyle->exts_perimCap[t][s]);
	}
    }

    TxPrintf("\n3) Overlap caps\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	for (s = 0; s < DBNumTypes; s++)
	{
	    if (ExtCurStyle->exts_overlapCap[t][s] > 0.0)
		TxPrintf("%s | %s  %3.3f\n",
			DBTypeLongNameTbl[t],
			DBTypeLongNameTbl[s],
			ExtCurStyle->exts_overlapCap[t][s]);
	}
    }

    TxPrintf("\n4) Side coupling caps\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	for (s = 0; s < DBNumTypes; s++)
	{
	    for (e = ExtCurStyle->exts_sideCoupleCap[t][s]; e; e = e->ec_next) {
		TxPrintf("%s | %s:  %3.3f\n",
			DBTypeLongNameTbl[t],
			DBTypeLongNameTbl[s],
			e->ec_cap);
		TxPrintf("   near: ");
		for (r = 0; r < DBNumTypes; r++)
		    if (TTMaskHasType(&e->ec_near, r))
			TxPrintf(" %s", DBTypeLongNameTbl[r]);
		TxPrintf("\n   far: ");
		for (r = 0; r < DBNumTypes; r++)
		    if (TTMaskHasType(&e->ec_far, r))
			TxPrintf(" %s", DBTypeLongNameTbl[r]);
		TxPrintf("\n   planes: ");
		for (p = PL_TECHDEPBASE; p < DBNumPlanes; p++)
		    if (PlaneMaskHasPlane(e->ec_pmask, p))
			TxPrintf(" %s", DBPlaneLongNameTbl[p]);
		TxPrintf("\n");
	    }
	}
    }

    TxPrintf("\n5) Side overlap caps\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	for (s = 0; s < DBNumTypes; s++)
	{
	    for (e = ExtCurStyle->exts_sideOverlapCap[t][s]; e; e = e->ec_next) {
		TxPrintf("%s | %s:  %3.3f\n",
			DBTypeLongNameTbl[t],
			DBTypeLongNameTbl[s],
			e->ec_cap);
		TxPrintf("   near: ");
		for (r = 0; r < DBNumTypes; r++)
		    if (TTMaskHasType(&e->ec_near, r))
			TxPrintf(" %s", DBTypeLongNameTbl[r]);
		TxPrintf("\n   far: ");
		for (r = 0; r < DBNumTypes; r++)
		    if (TTMaskHasType(&e->ec_far, r))
			TxPrintf(" %s", DBTypeLongNameTbl[r]);
		TxPrintf("\n   planes: ");
		for (p = PL_TECHDEPBASE; p < DBNumPlanes; p++)
		    if (PlaneMaskHasPlane(e->ec_pmask, p))
			TxPrintf(" %s", DBPlaneLongNameTbl[p]);
		TxPrintf("\n");
	    }
	}
    }

    TxPrintf("\n6) (Check) Perimeter cap mask\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	found = 0;
	for (s = 0; s < DBNumTypes; s++)
	    if (TTMaskHasType(&ExtCurStyle->exts_perimCapMask[t], s))
	    {
		if (found == 0)
		{
		    TxPrintf("   %s: ", DBTypeLongNameTbl[t]);
		    found = 1;
		}
		TxPrintf(" %s", DBTypeLongNameTbl[s]);
	    }
	if (found != 0) TxPrintf("\n");
    }

    TxPrintf("\n7) (Check) Overlap plane mask\n");
    for (p = PL_TECHDEPBASE; p < DBNumPlanes; p++)
	if (PlaneMaskHasPlane(ExtCurStyle->exts_overlapPlanes, p))
	    TxPrintf(" %s", DBPlaneLongNameTbl[p]);
    TxPrintf("\n");

    TxPrintf("\n8) (Check) Overlap types mask\n");
    for (p = PL_TECHDEPBASE; p < DBNumPlanes; p++)
    {
	found = 0;
	for (s = 0; s < DBNumTypes; s++)
	    if (TTMaskHasType(&ExtCurStyle->exts_overlapTypes[p], s))
	    {
		if (found == 0)
		{
		    found = 1;
		    TxPrintf("   %s: ", DBPlaneLongNameTbl[p]);
		}
		TxPrintf(" %s", DBTypeLongNameTbl[s]);
	    }
	if (found != 0) TxPrintf("\n");
    }

    TxPrintf("\n9) (Check) Overlap other types mask\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	found = 0;
	for (s = 0; s < DBNumTypes; s++)
	    if (TTMaskHasType(&ExtCurStyle->exts_overlapOtherTypes[t], s))
	    {
		if (found == 0)
		{
		    TxPrintf("   %s: ", DBTypeLongNameTbl[t]);
		    found = 1;
		}
		TxPrintf(" %s", DBTypeLongNameTbl[s]);
	     }
	if (found != 0) TxPrintf("\n");
    }

    TxPrintf("\n10) (Check) Overlap other planes mask\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	found = 0;
        for (p = PL_TECHDEPBASE; p < DBNumPlanes; p++)
	    if (PlaneMaskHasPlane(ExtCurStyle->exts_overlapOtherPlanes[t], p))
	    {
		if (found == 0)
		{
		    TxPrintf("   %s: ", DBTypeLongNameTbl[t]);
		    found = 1;
		}
		TxPrintf(" %s", DBPlaneLongNameTbl[p]);
	    }
	if (found != 0) TxPrintf("\n");
    }

    TxPrintf("\n11) (Check) Overlap shield types mask\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	for (s = 0; s < DBNumTypes; s++)
	{
	    found = 0;
	    for (r = 0; r < DBNumTypes; r++)
		if (TTMaskHasType(&ExtCurStyle->exts_overlapShieldTypes[t][s], r))
		{
		    if (found == 0)
		    {
			TxPrintf("   %s | %s: ",
				DBTypeLongNameTbl[t], DBTypeLongNameTbl[s]);
			found = 1;
		    }
		    TxPrintf(" %s", DBTypeLongNameTbl[r]);
		}
	    if (found != 0) TxPrintf("\n");
	}
    }

    TxPrintf("\n12) (Check) Overlap shield planes mask\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	for (s = 0; s < DBNumTypes; s++)
	{
	    found = 0;
	    for (p = PL_TECHDEPBASE; p < DBNumPlanes; p++)
		if (PlaneMaskHasPlane(ExtCurStyle->exts_overlapShieldPlanes[t][s], p))
		{
		    if (found == 0)
		    {
	    		TxPrintf("   %s | %s: ",
				DBTypeLongNameTbl[t], DBTypeLongNameTbl[s]);
			found = 1;
		    }
		    TxPrintf(" %s", DBPlaneLongNameTbl[p]);
		}
	    if (found != 0) TxPrintf("\n");
	}
    }

    TxPrintf("\n13) (Check) Side couple other edges mask\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	for (s = 0; s < DBNumTypes; s++)
	{
	    found = 0;
	    for (r = 0; r < DBNumTypes; r++)
		if (TTMaskHasType(&ExtCurStyle->exts_sideCoupleOtherEdges[t][s], r))
		{
		    if (found == 0)
		    {
			TxPrintf("   %s | %s: ",
				DBTypeLongNameTbl[t], DBTypeLongNameTbl[s]);
			found = 1;
		    }
		    TxPrintf(" %s", DBTypeLongNameTbl[r]);
		}
	    if (found != 0) TxPrintf("\n");
	}
    }

    TxPrintf("\n14) (Check) Side overlap other planes mask\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	for (s = 0; s < DBNumTypes; s++)
	{
	    found = 0;
	    for (p = PL_TECHDEPBASE; p < DBNumPlanes; p++)
		if (PlaneMaskHasPlane(ExtCurStyle->exts_sideOverlapOtherPlanes[t][s], p))
		{
		    if (found == 0)
		    {
			TxPrintf("   %s | %s: ",
				DBTypeLongNameTbl[t], DBTypeLongNameTbl[s]);
			found = 1;
		    }
		    TxPrintf(" %s", DBPlaneLongNameTbl[p]);
		}
	    if (found != 0) TxPrintf("\n");
	}
    }

    TxPrintf("\n15) (Check) Side overlap other types mask\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	for (s = 0; s < DBNumTypes; s++)
	{
	    found = 0;
	    for (r = 0; r < DBNumTypes; r++)
		if (TTMaskHasType(&ExtCurStyle->exts_sideOverlapOtherTypes[t][s], r))
		{
		    if (found == 0)
		    {
			TxPrintf("   %s | %s: ",
				DBTypeLongNameTbl[t], DBTypeLongNameTbl[s]);
			found = 1;
		    }
		    TxPrintf(" %s", DBTypeLongNameTbl[r]);
		}
	    if (found != 0) TxPrintf("\n");
	}
    }

    TxPrintf("\n16) (Check) Side overlap shield planes mask\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	for (s = 0; s < DBNumTypes; s++)
	{
	    found = 0;
	    for (p = PL_TECHDEPBASE; p < DBNumPlanes; p++)
		if (PlaneMaskHasPlane(ExtCurStyle->exts_sideOverlapShieldPlanes[t][s], p))
		{
		    if (found == 0)
		    {
			TxPrintf("   %s | %s: ",
				DBTypeLongNameTbl[t], DBTypeLongNameTbl[s]);
			found = 1;
		    }
		    TxPrintf(" %s", DBPlaneLongNameTbl[p]);
		}
	    if (found != 0) TxPrintf("\n");
	}
    }

    TxPrintf("\n17) (Check) Side planes mask\n");
    for (p = PL_TECHDEPBASE; p < DBNumPlanes; p++)
	if (PlaneMaskHasPlane(ExtCurStyle->exts_sidePlanes, p))
	    TxPrintf(" %s", DBPlaneLongNameTbl[p]);
    TxPrintf("\n");

    TxPrintf("\n18) (Check) Side types mask\n");
    for (p = PL_TECHDEPBASE; p < DBNumPlanes; p++)
    {
	found = 0;
	for (s = 0; s < DBNumTypes; s++)
	    if (TTMaskHasType(&ExtCurStyle->exts_sideTypes[p], s))
	    {
		if (found == 0)
		{
		    TxPrintf("   %s: ", DBPlaneLongNameTbl[p]);
		    found = 1;
		}
		TxPrintf(" %s", DBTypeLongNameTbl[s]);
	    }
	if (found != 0) TxPrintf("\n");
    }

    TxPrintf("\n19) (Check) Side edges mask\n");
    for (t = 0; t < DBNumTypes; t++)
    {
	found = 0;
	for (s = 0; s < DBNumTypes; s++)
	    if (TTMaskHasType(&ExtCurStyle->exts_sideTypes[t], s))
	    {
		if (found == 0)
		{
		    TxPrintf("   %s: ", DBTypeLongNameTbl[t]);
		    found = 1;
		}
		TxPrintf(" %s", DBTypeLongNameTbl[s]);
	    }
	if (found != 0) TxPrintf("\n");
    }
}
