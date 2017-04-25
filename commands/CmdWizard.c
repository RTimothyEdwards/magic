/*
 * CmdWizard.c --
 *
 * *** Wizard commands ***
 *
 * These commands are not intended to be used by the ordinary magic
 * user, but are provided for the benefit of system maintainers/implementors.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/commands/CmdWizard.c,v 1.2 2008/02/10 19:30:19 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/times.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/malloc.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/main.h"
#include "commands/commands.h"
#include "utils/runstats.h"
#include "textio/textio.h"
#include "graphics/graphics.h"
#include "utils/signals.h"
#include "utils/utils.h"
#include "textio/txcommands.h"

/* Forward declarations */

extern void cmdPsearchStats();

void cmdStatsHier(CellDef *, int, CellDef *);


/*
 * ----------------------------------------------------------------------------
 *
 * CmdCoord --
 *
 * Show the coordinates of various things:
 *	Point tool		edit coords, root coords, curr coords
 *	Box tool		edit coords, root coords, curr coords
 *	Edit cell bounding box	edit coords, root coords
 *	Root cell bounding box	edit coords, root coords
 *	Curr cell bounding box	curr coords, root coords
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
CmdCoord(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    MagWindow *pointW = (MagWindow *) NULL;
    Rect editRect, rootRect;
    Transform tinv;
    CellDef *rootDef;

    if ((w = ToolGetPoint((Point *) NULL, &rootRect)) != (MagWindow *) NULL)
    {
	pointW = w;
	rootDef = ((CellUse *)w->w_surfaceID)->cu_def;
	TxPrintf("Point:\tr=(%d,%d)::(%d,%d)",
			rootRect.r_xbot, rootRect.r_ybot,
			rootRect.r_xtop, rootRect.r_ytop);
	if (EditRootDef == rootDef)
	{
	    GeoTransRect(&RootToEditTransform, &rootRect, &editRect);
	    TxPrintf("\te=(%d,%d)::(%d,%d)",
			editRect.r_xbot, editRect.r_ybot,
			editRect.r_xtop, editRect.r_ytop);
	}
	TxPrintf("\n");
    }

    if (ToolGetBox(&rootDef, &rootRect))
    {
	TxPrintf("Box:\tr=(%d,%d)::(%d,%d)",
			rootRect.r_xbot, rootRect.r_ybot,
			rootRect.r_xtop, rootRect.r_ytop);
	if (EditRootDef == rootDef)
	{
	    GeoTransRect(&RootToEditTransform, &rootRect, &editRect);
	    TxPrintf("\te=(%d,%d)::(%d,%d)",
			editRect.r_xbot, editRect.r_ybot,
			editRect.r_xtop, editRect.r_ytop);
	}
	TxPrintf("\n");
    }

    if (pointW == (MagWindow *) NULL)
    {
	rootRect.r_xbot = rootRect.r_ybot = 0;
	rootRect.r_xtop = rootRect.r_ytop = 1;
	rootDef = EditRootDef;
    }
    else
    {
	rootDef = ((CellUse *) pointW->w_surfaceID)->cu_def;
	rootRect = ((CellUse *) pointW->w_surfaceID)->cu_bbox;
    }

    TxPrintf("Root cell:\tr=(%d,%d)::(%d,%d)",
		    rootRect.r_xbot, rootRect.r_ybot,
		    rootRect.r_xtop, rootRect.r_ytop);
    if (EditRootDef == rootDef)
    {
	GeoTransRect(&RootToEditTransform, &rootRect, &editRect);
	TxPrintf("\te=(%d,%d)::(%d,%d)",
		    editRect.r_xbot, editRect.r_ybot,
		    editRect.r_xtop, editRect.r_ytop);
    }
    TxPrintf("\n");

    GeoInvertTrans(&EditCellUse->cu_transform, &tinv);
    GeoTransRect(&tinv, &EditCellUse->cu_bbox, &editRect);
    TxPrintf("Edit cell:");
    if (EditRootDef == rootDef)
    {
	GeoTransRect(&EditToRootTransform, &editRect, &rootRect);
	TxPrintf("\tr=(%d,%d)::(%d,%d)",
		    rootRect.r_xbot, rootRect.r_ybot,
		    rootRect.r_xtop, rootRect.r_ytop);
    }
    TxPrintf("\te=(%d,%d)::(%d,%d)",
		    editRect.r_xbot, editRect.r_ybot,
		    editRect.r_xtop, editRect.r_ytop);
    TxPrintf("\n");
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdExtractTest --
 *
 * Debugging of circuit extraction.
 *
 * Usage:
 *	*extract cmd [args]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See comments in ExtractTest() in extract/ExtTest.c for details.
 *
 * ----------------------------------------------------------------------------
 */

#ifndef NO_EXT
void
CmdExtractTest(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    ExtractTest(w, cmd);
}
#endif


/*
 * ----------------------------------------------------------------------------
 *
 * showTech --
 * CmdShowtech --
 *
 * Usage:
 *
 *	showtech [outfile]
 *
 * Display all the internal technology tables.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May write to a disk file.
 *
 * ----------------------------------------------------------------------------
 */

void
showTech(outf, verbose)
    FILE *outf;		/* File to which information is to be output */
    bool verbose;	/* If TRUE, output detailed erase table */
{
    int i, j;
    int pNum;
    bool first, any;
    TileTypeBitMask *rMask;

    fprintf(outf, "Technology %s\n", DBTechName);
    fprintf(outf, "%d tile planes, %d tile types\n\n",
		DBNumPlanes, DBNumTypes);
    fprintf(outf, "Planes:\n");
    for (i = 0; i < DBNumPlanes; i++)
	fprintf(outf, "%s\t%s\n", DBPlaneShortName(i),
		DBPlaneLongName(i));

    fprintf(outf, "\n");
    fprintf(outf, "Types:\n");
    for (i = 0; i < DBNumTypes; i++) {
	int pl ; char *spl ;

	pl = DBPlane(i);
	spl = ( pl <= 0 || pl > DBNumPlanes ) ? "??" : DBPlaneLongName(pl);
	fprintf(outf, "%s\t%s\t%s\n", spl,
		DBTypeShortName(i), DBTypeLongName(i));
	}

    fprintf(outf, "\n");
    fprintf(outf, "\014Connectivity:\n");
    for (j = 0; j < DBNumTypes; j++)
	for (i = 0; i < j; i++)
	    if (DBConnectsTo(i, j))
		fprintf(outf, "%s :: %s\n",
			DBTypeLongName(j), DBTypeLongName(i));
    fprintf(outf, "\n");

    fprintf(outf, "\n\014Component Layers:\n");
    for (i = 0; i < DBNumUserLayers; i++)
	for (j = 0; j < DBNumUserLayers; j++)
	{
	    rMask = DBResidueMask(j);
	    if ((j != i) && TTMaskHasType(rMask, i))
		fprintf(outf, "%s is a component of %s\n",
		    DBTypeLongName(i), DBTypeLongName(j));
	}
    fprintf(outf, "\n");

    fprintf(outf, "\014Planes affected by painting:\n");
    fprintf(outf, "Type                  Planes\n");
    fprintf(outf, "----                  ------\n");
    for (i = 0; i < DBNumTypes; i++)
    {
	fprintf(outf, "%-22.22s", DBTypeLongName(i));
	first = TRUE;
	for (pNum = 0; pNum < DBNumPlanes; pNum++)
	{
	    if (DBPaintOnPlane(i, pNum))
	    {
		if (first)
		    first = FALSE;
		else
		    fprintf(outf, ", ");
		fprintf(outf, "%s", DBPlaneLongName(pNum));
	    }
	}
	fprintf(outf, "\n");
    }

    fprintf(outf, "\014Planes affected by erasing:\n");
    fprintf(outf, "Type                  Planes\n");
    fprintf(outf, "----                  ------\n");
    for (i = 0; i < DBNumTypes; i++)
    {
	fprintf(outf, "%-22.22s", DBTypeLongName(i));
	first = TRUE;
	for (pNum = 0; pNum < DBNumPlanes; pNum++)
	{
	    if (DBEraseOnPlane(i, pNum))
	    {
		if (!first)
		    fprintf(outf, ", ");
		first = FALSE;
		fprintf(outf, "%s", DBPlaneLongName(pNum));
	    }
	}
	fprintf(outf, "\n");
    }

    for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
    {
	fprintf(outf, "\014Paint: %s\n", DBPlaneLongName(pNum));
	fprintf(outf, "=======================================\n");
	for (i = 0; i < DBNumTypes; i++)
	{
	    if (i == TT_SPACE || DBPlane(i) == pNum)
	    {
		any = FALSE;
		for (j = 0; j < DBNumTypes; j++)
		{
		    if (!verbose && (i == TT_SPACE || j == TT_SPACE))
			continue;
		    if (DBStdPaintEntry(i, j, pNum) != i)
		    {
			fprintf(outf, "%s + %s --> %s\n",
				DBTypeLongName(i), DBTypeLongName(j),
				DBTypeLongName(DBStdPaintEntry(i, j, pNum)));
			any = TRUE;
		    }
		}
		if (any)
		    fprintf(outf,
				"--------------------------------------\n");
	    }
	}
    }

    for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
    {
	fprintf(outf, "\014Erase: %s\n", DBPlaneLongName(pNum));
	fprintf(outf, "=======================================\n");
	for (i = 0; i < DBNumTypes; i++)
	{
	    if (i == TT_SPACE || DBPlane(i) == pNum)
	    {
		any = FALSE;
		for (j = 0; j < DBNumTypes; j++)
		{
		    if (!verbose && i == j)
			continue;
		    if (DBStdEraseEntry(i, j, pNum) != i)
		    {
			fprintf(outf, "%s - %s --> %s\n",
				DBTypeLongName(i), DBTypeLongName(j),
				DBTypeLongName(DBStdEraseEntry(i, j, pNum)));
			any = TRUE;
		    }
		}
		if (any)
		    fprintf(outf,
				"--------------------------------------\n");
	    }
	}
    }
}

void
CmdShowtech(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    FILE *outf;
    bool verbose;
    char **av;
    int ac;

    if (cmd->tx_argc > 3)
    {
	TxError("Usage: showtech [-v] [file]\n");
	return;
    }

    verbose = FALSE;
    av = &cmd->tx_argv[1];
    ac = cmd->tx_argc - 1;

    outf = stdout;
    if (ac > 0 && strcmp(av[0], "-v") == 0)
    {
	verbose = TRUE;
	av++, ac--;
    }

    if (ac > 0)
    {
	outf = fopen(av[0], "w");
	if (outf == (FILE *) NULL)
	{
	    perror(av[0]);
	    TxError("Nothing written\n");
	    return;
	}
    }

    showTech(outf, verbose);
    if (outf != stdout)
	(void) fclose(outf);
}


/*
 * ----------------------------------------------------------------------------
 *
 * CmdTilestats --
 *
 * Generate statistics on tile utilization.
 * The output is either to the terminal or to the file supplied.
 * Usage:
 *	*tilestats -a [file]	to generate statistics for all cells
 *	*tilestats [file]	to generate statistics for the currently
 *				selected cell.
 *
 * If the argument 'file' is specified, it is created to hold the
 * output of the *tilestats command.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May create a disk file.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdTilestats(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    CellUse *selectedUse;
    FILE *outf = stdout;
    bool allDefs = FALSE;
    char **av = cmd->tx_argv + 1;
    int ac = cmd->tx_argc - 1;
    int cmdStatsFunc();

    if (ac > 2)
    {
	TxError("Usage: tilestats [-a] [outputfile]\n");
	return;
    }

    if (ac > 0 && strcmp(av[0], "-a") == 0)
	allDefs = TRUE, ac--, av++;

    if (ac > 0 && (outf = fopen(av[0], "w")) == NULL)
    {
	perror(av[0]);
	return;
    }

    selectedUse = CmdGetSelectedCell((Transform *) NULL);
    if (allDefs)
	(void) DBCellSrDefs(0, cmdStatsFunc, (ClientData) outf);
    else if (selectedUse != NULL)
	(void) cmdStatsFunc(selectedUse->cu_def, outf);
    else
	TxError("No cell selected.\n");
    if (outf != stdout)
	(void) fclose(outf);
}


/* Stored with each CellDef in the cd_client field */
struct cellInfo
{
    int		ci_count[TT_MAXTYPES];		/* Count of tiles of each
						 * type in this cell.
						 */
    int		ci_hierCount[TT_MAXTYPES];	/* Count of tiles of each
						 * type in all subtrees,
						 * weighted by the number
						 * of times each subtree is
						 * used.
						 */
    bool	ci_countedHier;			/* TRUE if ci_hierCount has
						 * yet been computed.
						 */
};

/* Passed by DBTreeCountPaint to the clients */
struct countClient
{
    FILE	*cc_outFile;	/* Output statistics to this file */
    CellDef	*cc_rootDef;	/* Root definition for which we're computing
				 * all the statistics.
				 */
};

/* Records the total number of drawn tiles of each type */
int totalTiles[TT_MAXTYPES];

/*
 * ----------------------------------------------------------------------------
 *
 * cmdStatsFunc --
 *
 * Generate the hierarchical statistics for a single cell def.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Writes to the file outf.
 *
 * ----------------------------------------------------------------------------
 */

int
cmdStatsFunc(def, outf)
    CellDef *def;
    FILE *outf;
{
    int cmdStatsCount(), cmdStatsOutput();
    struct countClient cc;
    int total;
    TileType t;

    cc.cc_outFile = outf;
    cc.cc_rootDef = def;
    for (t = 0; t < DBNumTypes; t++)
	totalTiles[t] = 0;

    DBTreeCountPaint(def, cmdStatsCount, cmdStatsHier,
		cmdStatsOutput, (ClientData) &cc);

    total = 0;
    for (t = TT_SPACE; t < DBNumTypes; t++)
    {
	if (totalTiles[t])
	{
	    fprintf(outf, "%s\tTOTAL\t%s\t%d\n",
		    def->cd_name, DBTypeLongName(t), totalTiles[t]);
	    total += totalTiles[t];
	}
    }

    fprintf(outf, "%s\tTOTAL\tALL\t%d\n", def->cd_name, total);
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * cmdStatsCount --
 *
 * Count the number of tiles in a single cell.
 * If def->cd_client has already been filled in, we just return;
 * otherwise, we make def->cd_client point to a newly allocated
 * cellInfo struct, and fill in the ci_count field.
 *
 * Results:
 *	Returns 1 if def->cd_client had already been filled in,
 *	0 otherwise.
 *
 * Side effects:
 *	May modify def->cd_client.
 *
 * ----------------------------------------------------------------------------
 */

int
cmdStatsCount(def, cc)
    CellDef *def;
    struct countClient *cc;
{
    int cmdStatsCountTile();
    int pNum;
    struct cellInfo *ci;
    TileType t;

    if (def->cd_client)
	return (1);

    /* Allocate a new cellInfo struct for this CellDef */
    ci = (struct cellInfo *) mallocMagic(sizeof (struct cellInfo));
    def->cd_client = (ClientData) ci;
    for (t = TT_SPACE; t < DBNumTypes; t++)
    {
	ci->ci_count[t] = ci->ci_hierCount[t] = 0;
	ci->ci_countedHier = FALSE;
    }

    /* Visit all tiles */
    for (pNum = PL_SELECTBASE; pNum < DBNumPlanes; pNum++)
	(void) DBSrPaintArea((Tile *) NULL, def->cd_planes[pNum],
		&TiPlaneRect, &DBAllTypeBits,
		cmdStatsCountTile, def->cd_client);

    return (0);
}

int
cmdStatsCountTile(tile, ci)
    Tile *tile;
    struct cellInfo *ci;
{
    TileType type = TiGetType(tile);

    /*
     * Count this tile both toward the cell being visited,
     * and the overall total.
     */
    ci->ci_count[type]++;
    totalTiles[type]++;

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * cmdStatsHier --
 *
 * Add to the hierarchical statistics for a given CellDef.
 * If parent's cd_client cellInfo struct has ci_countedHier
 * set, we just return.  (It means that the subtree we are now
 * visiting has already been visited, but since we are called
 * in bottom-up order, there's not much we can do about it).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds the hierarchical statistics for child, plus the
 *	per-cell statistics for child, to the hierarchical
 *	statistics for 'parent'.
 *
 *	Since we are guaranteed to be called only after all
 *	children of 'child' have been visited, we know that
 *	we can mark 'child' as having ci_countedHier TRUE.
 *
 * ----------------------------------------------------------------------------
 */

void
cmdStatsHier(parent, nuses, child)
    CellDef *parent, *child;
    int nuses;
{
    struct cellInfo *pi, *ci;
    TileType t;

    pi = (struct cellInfo *) parent->cd_client;
    if (pi->ci_countedHier)
	return;

    ci = (struct cellInfo *) child->cd_client;
    ci->ci_countedHier = TRUE;
    for (t = TT_SPACE; t < DBNumTypes; t++)
	pi->ci_hierCount[t] += nuses * (ci->ci_hierCount[t] + ci->ci_count[t]);
}

/*
 * ----------------------------------------------------------------------------
 *
 * cmdStatsOutput --
 *
 * Output the hierarchical statistics for a single cell def.
 * If 'def' has not yet had its statistics output, we output
 * for each tile type having non-zero counts:
 *	- the number of tiles of this type in this cell, plus
 *	  hierarchically in all of its children, pretending
 *	  that the entire subtree was flattened (so each tile
 *	  is counted as many times as it logically appears in
 *	  the hierarchy).
 *	- the number of tiles of this type in this cell alone.
 * These numbers are also output for the total number of tiles
 * of all types.
 *
 * Results:
 *	If we had already output statistics for this cell, we
 *	return 1; otherwise we return 0.
 *
 * Side effects:
 *	Writes to the file outf.
 *	If def->cd_client points to a cellInfo struct, we free it
 *	and clear def->cd_client.
 *
 * ----------------------------------------------------------------------------
 */

int
cmdStatsOutput(def, cc)
    CellDef *def;
    struct countClient *cc;
{
    TileType t;
    struct cellInfo *ci;
    int count, hiercount;

    if (def->cd_client == (ClientData) NULL)
	return (1);

    ci = (struct cellInfo *) def->cd_client;
    def->cd_client = (ClientData) NULL;

    count = hiercount = 0;
    for (t = TT_SPACE; t < DBNumTypes; t++)
    {
	if (ci->ci_hierCount[t] | ci->ci_count[t])
	{
	    /* Root-def this-def type-name num-flat num-paint */
	    fprintf(cc->cc_outFile, "%s\t%s\t%s\t%d\t%d\n",
		    cc->cc_rootDef->cd_name, def->cd_name,
		    DBTypeLongName(t),
		    ci->ci_hierCount[t] + ci->ci_count[t], ci->ci_count[t]);
	    count += ci->ci_count[t];
	    hiercount += ci->ci_hierCount[t];
	}
    }

    /* Root-def this-def ALL num-flat num-paint */
    if (hiercount | count)
    {
	fprintf(cc->cc_outFile, "%s\t%s\tALL\t%d\t%d\n",
		cc->cc_rootDef->cd_name, def->cd_name,
		hiercount + count, count);
    }

    freeMagic((char *) ci);
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdPsearch --
 *
 * Run point search a number of times the point at the lower-left
 * corner of the box tool to each point in the edit cell.
 *
 * Usage:
 *	psearch plane count
 *
 * Where plane is the name of the plane on which the search is to be
 * carried out, and count is the number of searches to be performed.
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
CmdPsearch(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    char *RunStats();
    static struct tms tlast, tdelta;
    Point p;
    Plane *plane;
    Rect rtool;
    Rect *ebox;
    Tile *tp;
    Tile *TiSrPointNew();
    int i, pNum, count;

    if (cmd->tx_argc != 3)
    {
	TxError("Usage: psearch plane count\n");
	return;
    }

    pNum = DBTechNamePlane(cmd->tx_argv[1]);
    if (pNum < 0)
    {
	TxError("Unrecognized plane: %s\n", cmd->tx_argv[1]);
	return;
    }

    if (!StrIsInt(cmd->tx_argv[2]))
    {
	TxError("Count must be numeric\n");
	return;
    }

    count = atoi(cmd->tx_argv[2]);

    ebox = &EditCellUse->cu_def->cd_bbox;
    if (!ToolGetEditBox(&rtool)) return;

    plane = EditCellUse->cu_def->cd_planes[pNum];

    tp = TiSrPoint((Tile *) NULL, plane, &rtool.r_ll);
    (void) RunStats(RS_TINCR, &tlast, &tdelta);

#define	BUMP(p, b)	\
	if (++((p).p_x) >= (b)->r_xtop) { (p).p_y++; (p).p_x = (b)->r_xbot; } \
	if ((p).p_y >= (b)->r_ytop) (p) = (b)->r_ll;

    /* Procedural search */
    for (p = ebox->r_ll, i = count; i-- > 0; )
    {
	BUMP(p, ebox);
	(void) TiSrPoint(tp, plane, &p);
    }
    cmdPsearchStats("proc", &tlast, &tdelta, count);

    /* Macro search */
    for (p = ebox->r_ll, i = count; i-- > 0; )
    {
	Tile *txp = tp;
	BUMP(p, ebox);
	GOTOPOINT(txp, &p);
    }
    cmdPsearchStats("macro", &tlast, &tdelta, count);
}

void
cmdPsearchStats(str, tl, td, count)
    char *str;
    struct tms *tl, *td;
    int count;
{
    char *RunStats();
    char *rstatp;
    int us, ups;

    rstatp = RunStats(RS_TINCR, tl, td);
    us = td->tms_utime * (1000000 / 60);
    ups = us / count;
    TxPrintf("%s: %d searches, %d us/search [%s]\n", str, count, ups, rstatp);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdTsearch --
 *
 * Call TiSrArea() a number of times over an area the size and shape of
 * that specified by the box, each time over a different area in the
 * edit cell.
 *
 * Usage:
 *	tsearch plane count mask searchroutine
 *
 * Where plane is the name of the plane on which the search is to be
 * carried out, and count is the number of searches to be performed.
 * If 'searchroutine' is 'mayo', use Bob's routine.  If it is 'new', use
 * Walter's routine.  Else use old routine.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int numTilesFound;
bool cmdTsearchDebug = FALSE;

void
CmdTsearch(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int cmdTsrFunc();
    char *RunStats(), *rstatp;
    static TileTypeBitMask mask;
    static struct tms tlast, tdelta;
    Rect rtool, rsearch;
    /**** Rect *ebox; ****/
    Plane *plane;
    int i, pNum, count;
    int usPerSearch, usPerTile, usPerL2, us, boxarea;

    if (cmd->tx_argc < 3 || cmd->tx_argc > 5)
    {
	TxError("Usage: tsearch plane count [mask [new|mayo]]\n");
	return;
    }

    pNum = DBTechNamePlane(cmd->tx_argv[1]);
    if (pNum < 0)
    {
	TxError("Unrecognized plane: %s\n", cmd->tx_argv[1]);
	return;
    }

    if (!StrIsInt(cmd->tx_argv[2]))
    {
	TxError("Count must be numeric\n");
	return;
    }

    count = atoi(cmd->tx_argv[2]);

    if (!ToolGetEditBox(&rtool)) return;

    /*****
    ebox = &EditCellUse->cu_def->cd_bbox;
    if (rtool.r_xtop - rtool.r_xbot >= ebox->r_xtop - ebox->r_xbot
	    || rtool.r_ytop - rtool.r_ybot >= ebox->r_ytop - ebox->r_ybot)
    {
	TxError("Box must be smaller than edit cell\n");
	return;
    }
    *****/

    rsearch = rtool;
    plane = EditCellUse->cu_def->cd_planes[pNum];

    (void) RunStats(RS_TINCR, &tlast, &tdelta);
    if (cmd->tx_argc >= 4)
	(void) CmdParseLayers(cmd->tx_argv[3], &mask);
    else mask = DBAllTypeBits;

    if (!TTMaskEqual(&mask, &DBZeroTypeBits))
	numTilesFound = 0;
	
    for (i = 0; i < count; i++)
    {
	/*****
	rsearch.r_xtop++;
	rsearch.r_xbot++;
	if (rsearch.r_xtop >= ebox->r_xtop)
	{
	    rsearch.r_ybot--;
	    rsearch.r_ytop--;
	    rsearch.r_xbot = ebox->r_xbot;
	    rsearch.r_xtop = rsearch.r_xbot + (rtool.r_xtop - rtool.r_xbot);
	}

	if (rsearch.r_ybot <= ebox->r_ybot)
	{
	    rsearch.r_ytop = ebox->r_ytop;
	    rsearch.r_ybot = rsearch.r_ytop - (rtool.r_ytop - rtool.r_ybot);
	    rsearch.r_xbot = ebox->r_xbot;
	    rsearch.r_xtop = rsearch.r_xbot + (rtool.r_xtop - rtool.r_xbot);
	}
	*****/

	if (cmdTsearchDebug)
	    TxPrintf("----- (%d,%d) :: (%d,%d) -----\n",
		rsearch.r_xbot, rsearch.r_ybot, rsearch.r_xtop, rsearch.r_ytop);

	if (cmd->tx_argc < 5)
	{
	    (void) TiSrArea((Tile *) NULL, plane, &rsearch,
		cmdTsrFunc, (ClientData) 0);
	}
	else
	{
	    /****
	    if (strcmp(cmd->tx_argv[4], "mayo") == 0)
		(void) TiSrAreaNR2((Tile *) NULL, plane, &rsearch, &mask,
		    cmdTsrFunc, (ClientData) 0);
	    else
	    ****/
		(void) DBSrPaintArea((Tile *) NULL, plane, &rsearch, &mask,
		    cmdTsrFunc, (ClientData) 0);
	}
    }

    if (numTilesFound == 0) numTilesFound = 1;
    rstatp = RunStats(RS_TINCR, &tlast, &tdelta);
    boxarea = (rsearch.r_xtop-rsearch.r_xbot)*(rsearch.r_ytop-rsearch.r_ybot);
    us = tdelta.tms_utime * (1000000 / 60);
    usPerL2 = us / (boxarea * count);
    usPerTile = us / numTilesFound;
    usPerSearch = us / count;
    TxPrintf("[%s]: box = %dh x %dw  (area=%d l**2)\n", rstatp,
		rsearch.r_ytop-rsearch.r_ybot,
		rsearch.r_xtop-rsearch.r_xbot,
		boxarea);
    TxPrintf("%d searches, %d tiles, %d us/l**2, %d us/tile, %d us/search\n",
		count, numTilesFound, usPerL2, usPerTile, usPerSearch);
}

int
cmdTsrFunc(tp)
    Tile *tp;
{
    if (cmdTsearchDebug)
	TxPrintf("%x\n", tp);
    numTilesFound++;
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdWatch --
 *
 * Enable/disable watching of tile planes in the given window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Causes the display package to display the actual tile structure
 *	for a given plane, or disables such display.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdWatch(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    DBWclientRec *crec;
    int pNum;
    int i,flags=0;

    if (w == (MagWindow *) NULL)
    {
	TxError("Gee, you don't seem like a wizard!\n");
	TxError("Cursor not in a layout window.\n");
        return;
    }
    crec = (DBWclientRec *) w->w_clientData;
    for (i =2 ; i < cmd->tx_argc;i++)
    {
    	 if (strcmp("demo", cmd->tx_argv[i]) ==0)
	 {
	      flags |= DBW_WATCHDEMO;
	 }
	 else
    	 if (strcmp("types", cmd->tx_argv[i]) ==0)
	 {
	      flags |= DBW_SEETYPES;
	 }
	 else
	 {
	      TxError("Gee, you don't sound like a wizard!\n");
	      TxError("Usage: %s [plane] [demo] [types]\n", cmd->tx_argv[0]);
	      return;
	 }
    }
    if (cmd->tx_argc == 1)
    {
	pNum = -1;
	crec->dbw_watchDef = NULL;
    }
    else
    {
	pNum = DBTechNamePlane(cmd->tx_argv[1]);
	if (pNum < 0)
	{
	    char *cp;
	    TxError("Unrecognized plane: %s.  Legal names are:\n",
		    cmd->tx_argv[1]);
	    for(pNum=0; pNum < PL_MAXTYPES; pNum++) {
		cp = DBPlaneLongName(pNum);
		if (cp != NULL)
		    TxError("	%s\n", cp);
	    };
	    return;
	}
	crec->dbw_watchDef = EditCellUse->cu_def;
	crec->dbw_watchTrans = EditToRootTransform;
    }

    crec->dbw_watchPlane = pNum;
    crec->dbw_flags &= ~(DBW_WATCHDEMO|DBW_SEETYPES);
    crec->dbw_flags |= flags;

    WindAreaChanged(w, (Rect *) NULL);
}
