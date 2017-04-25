/*
 * grouteTest.c --
 *
 * Testing code for the global router.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/grouter/grouteTest.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "textio/txcommands.h"
#include "utils/main.h"
#include "utils/utils.h"
#include "commands/commands.h"
#include "utils/malloc.h"
#include "utils/styles.h"
#include "utils/netlist.h"

bool glInitialized = FALSE;

ClientData glDebugID = 0;

int glDebAllPoints = 0;
int glDebChan = 0;
int glDebCross = 0;
int glDebFast = 0;
int glDebHeap = 0;
int glDebHisto = 0;
int glDebLog = 0;
int glDebGreedy = 0;
int glDebMaze = 0;
int glDebNet = 0;
int glDebNewHeaps = 0;
int glDebPen = 0;
int glDebShowPins = 0;
int glDebStemsOnly = 0;
int glDebStraight = 0;
int glDebTiles = 0;
int glDebVerbose = 0;

/* Statistics */
int glCrossingsSeen;		/* Total crossings seen */
int glCrossingsAdded;		/* Total crossings added to heap */
int glCrossingsExpanded;	/* Total crossings expanded from */
int glCrossingsUsed;		/* Total crossings used */
int glGoodRoutes;		/* Good point to point routes */
int glBadRoutes;		/* Bad point to point routes */
int glNoRoutes;

/* Used in debugging; if set, we only route a single net */
char *glOnlyNet = NULL;

/*
 * Used to remember the number of frontier points visited, and the
 * number of points removed from the top of the heap, for each two
 * point net routed.
 */
GlNetHisto *glNetHistoList = NULL;

/* Forward declarations */
void GlInit();
void glShowCross();


/*
 * ----------------------------------------------------------------------------
 *
 * GlTest --
 *
 * Command interface for testing the global router.
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
GlTest(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int glDebugSides();
    typedef enum { CLRDEBUG, ONLYNET, SETDEBUG, SHOWDEBUG, SIDES } cmdType;
    Rect editArea;
    int n;
    static struct
    {
	char	*cmd_name;
	cmdType	 cmd_val;
    } cmds[] = {
	"clrdebug",		CLRDEBUG,
	"onlynet",		ONLYNET,
	"setdebug",		SETDEBUG,
	"showdebug",		SHOWDEBUG,
	"sides",		SIDES,
	0
    };

    if (!glInitialized)
	GlInit();

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
	    DebugSet(glDebugID, cmd->tx_argc - 2, &cmd->tx_argv[2], TRUE);
	    break;
	case CLRDEBUG:
	    DebugSet(glDebugID, cmd->tx_argc - 2, &cmd->tx_argv[2], FALSE);
	    break;
	case SHOWDEBUG:
	    DebugShow(glDebugID);
	    break;
	case SIDES:
	    if (!ToolGetEditBox(&editArea)) return;
	    n = -1;
	    if (cmd->tx_argc > 2)
	    {
		if (!StrIsInt(cmd->tx_argv[2]))
		{
		    TxError("Minimum channel width must be numeric\n");
		    break;
		}
		n = atoi(cmd->tx_argv[2]);
	    }
	    (void) rtrEnumSides(EditCellUse, &editArea, n,
			glDebugSides, (ClientData) NULL);
	case ONLYNET:
	    if (cmd->tx_argc == 2)
	    {
		if (glOnlyNet)
		    TxPrintf("Routing only net: %s\n", glOnlyNet);
		else
		    TxPrintf("Routing all nets.\n");
	    }
	    else if (cmd->tx_argc == 3)
	    {
		if (strcmp(cmd->tx_argv[2], "-") == 0)
		{
		    if (glOnlyNet)
		    {
			freeMagic(glOnlyNet);
			glOnlyNet = NULL;
		    }
		    TxPrintf("Routing all nets.\n");
		}
		else
		{
		    (void) StrDup(&glOnlyNet, cmd->tx_argv[2]);
		    TxPrintf("Routing only net: %s\n", glOnlyNet);
		}
	    }
	    else TxError("Usage: *groute onlynet [net | -]\n");
	    break;
    }
}

int
glDebugSides(side)
    Side *side;
{
    char mesg[256];
    CellDef *def = EditCellUse->cu_def;
    Rect r;

    GeoTransRect(&side->side_trans, &side->side_search, &r);
    ShowRect(def, &r, STYLE_SOLIDHIGHLIGHTS);
    (void) sprintf(mesg, "SEARCH %d %d %d %d\n",
		r.r_xbot, r.r_ybot, r.r_xtop, r.r_ytop);
    TxMore(mesg);
    ShowRect(def, &r, STYLE_ERASEHIGHLIGHTS);

    GeoTransRect(&side->side_trans, &side->side_used, &r);
    ShowRect(def, &r, STYLE_MEDIUMHIGHLIGHTS);
    (void) sprintf(mesg, "USED   %d %d %d %d\n",
		r.r_xbot, r.r_ybot, r.r_xtop, r.r_ytop);
    TxMore(mesg);
    ShowRect(def, &r, STYLE_ERASEHIGHLIGHTS);

    (void) TxPrintf("--------\n");

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * GlInit --
 *
 * One-time-only initialization for the global router.
 * Called after technology initialization.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Register ourself with the debug module and list all
 *	debugging flags.
 *
 * ----------------------------------------------------------------------------
 */

void
GlInit()
{
    int n;
    static struct
    {
	char	*di_name;
	int	*di_id;
    } dflags[] = {
	"allpoints",	&glDebAllPoints,
	"chan",		&glDebChan,
	"cross",	&glDebCross,
	"fast",		&glDebFast,
	"heap",		&glDebHeap,
	"histo",	&glDebHisto,
	"log",		&glDebLog,
	"greedy",	&glDebGreedy,
	"maze",		&glDebMaze,
	"net",		&glDebNet,
	"newheaps",	&glDebNewHeaps,
	"penalties",	&glDebPen,
	"showpins",	&glDebShowPins,
	"stemsonly",	&glDebStemsOnly,
	"straight",	&glDebStraight,
	"tiles",	&glDebTiles,
	"verbose",	&glDebVerbose,
	0
    };

    if (glInitialized)
	return;

    glInitialized = TRUE;

    /* Register ourselves with the debugging module */
    glDebugID = DebugAddClient("grouter", sizeof dflags/sizeof dflags[0]);
    for (n = 0; dflags[n].di_name; n++)
	*(dflags[n].di_id) = DebugAddFlag(glDebugID, dflags[n].di_name);
}

/*
 * ----------------------------------------------------------------------------
 *
 * glShowPath --
 *
 * Display the crossing points along an entire path for debugging.
 * The type of crossing point is indicated by 'kind'.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Causes redisplay.
 *
 * ----------------------------------------------------------------------------
 */

void
glShowPath(dest, root, kind)
    GlPoint *dest, *root;
    int kind;
{
    static NetId dummyId = { 0, 0 };
    GlPoint *temp;

    for (temp = dest; temp != root; temp = temp->gl_path)
	glShowCross(temp->gl_pin, dummyId, kind);
}

/*
 * ----------------------------------------------------------------------------
 *
 * glShowCross --
 *
 * Display a crossing point for debugging.
 * Two kinds of crossing points can be shown: CROSS_TEMP, which
 * is only being considered, and CROSS_PERM, which has been permanently
 * assigned to a net.  In addition, CROSS_ERASE will cause the crossing
 * point to be erased.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Causes redisplay.
 *
 * ----------------------------------------------------------------------------
 */

void
glShowCross(pin, netId, kind)
    GCRPin *pin;	/* Pin itself */
    NetId netId;	/* Identifies net and segment for this pin */
    int kind;		/* Determines kind of display; see above */
{
    char *name, name1[1024], name2[1024];
    int style;
    Rect r;

    switch (kind)
    {
	case CROSS_TEMP:
	    name = "temp";
	    style = STYLE_MEDIUMHIGHLIGHTS;
	    break;
	case CROSS_PERM:
	    name = "PERM";
	    style = STYLE_SOLIDHIGHLIGHTS;
	    break;
	case CROSS_ERASE:
	    name = (char *) NULL;
	    style = STYLE_ERASEHIGHLIGHTS;
	    break;
    }

    if (name && DebugIsSet(glDebugID, glDebMaze))
    {
	(void) strcpy(name1, NLNetName(pin->gcr_pId));
	(void) strcpy(name2, NLNetName(netId.netid_net));
	TxPrintf("%s (%d,%d), Net %s/%d->%s/%d, Ch %d\n",
		name, pin->gcr_point.p_x, pin->gcr_point.p_y,
		name1, pin->gcr_pSeg, name2, netId.netid_seg, pin->gcr_ch);
    }

    r.r_ll = r.r_ur = pin->gcr_point;
    r.r_xtop += RtrMetalWidth;
    r.r_ytop += RtrMetalWidth;
    ShowRect(EditCellUse->cu_def, &r, style);
}

/*
 * ----------------------------------------------------------------------------
 *
 * glHistoAdd --
 *
 * Remember the number of heap, frontier, and starting points
 * used during a given route by adding them to the GlNetHisto
 * list.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates memory; prepends a new entry to glNetHistoList.
 *
 * ----------------------------------------------------------------------------
 */

void
glHistoAdd(heapPtsBefore, frontierPtsBefore, startPtsBefore)
    int heapPtsBefore, frontierPtsBefore, startPtsBefore;
{
    GlNetHisto *gh;

    gh = (GlNetHisto *) mallocMagic((unsigned) (sizeof (GlNetHisto)));
    gh->glh_heap = glCrossingsExpanded - heapPtsBefore;
    gh->glh_frontier = glCrossingsAdded - frontierPtsBefore;
    gh->glh_start = startPtsBefore;
    gh->glh_next = glNetHistoList;
    glNetHistoList = gh;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glHistoDump --
 *
 * Dump the information accumulated by glHistoAdd() above
 * to the file HISTO.out in the current directory.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Creates HISTO.out.
 *
 * ----------------------------------------------------------------------------
 */

void
glHistoDump()
{
    static char hname[] = "HISTO.out";
    int lastsize, count, total;
    GlNetHisto *gh;
    Heap histoHeap;
    HeapEntry entry;
    FILE *fp;

    fp = fopen(hname, "w");
    if (fp == NULL)
    {
	perror(hname);
	return;
    }

    /* Raw data */
    fprintf(fp, "--------- raw data ---------\n");
    fprintf(fp, "%9s %9s %9s\n", "HEAP", "FRONTIER", "START");
    for (gh = glNetHistoList; gh; gh = gh->glh_next)
    {
	fprintf(fp, "%9d %9d %9d\n",
		gh->glh_heap, gh->glh_frontier, gh->glh_start);
    }

    /* Output sorted by number of heap points */
    fprintf(fp, "--------- by heap points ---------\n");
    HeapInit(&histoHeap, 128, FALSE, FALSE);
    for (gh = glNetHistoList; gh; gh = gh->glh_next)
	HeapAddInt(&histoHeap, gh->glh_heap, (char *) gh);
    count = lastsize = total = 0;
    while (HeapRemoveTop(&histoHeap, &entry))
    {
	gh = (GlNetHisto *) entry.he_id;
	if (gh->glh_heap != lastsize)
	{
	    if (count > 0)
		fprintf(fp, "%d: %d\n", lastsize, count);
	    lastsize = gh->glh_heap;
	    count = 0;
	}
	count++, total++;
    }
    HeapKill(&histoHeap, (void (*)()) NULL);
    if (count > 0)
	fprintf(fp, "%d: %d\n", lastsize, count);
    fprintf(fp, "TOTAL: %d\n", total);

    /* Output sorted by number of frontier points */
    fprintf(fp, "--------- by frontier points ---------\n");
    HeapInit(&histoHeap, 128, FALSE, FALSE);
    for (gh = glNetHistoList; gh; gh = gh->glh_next)
	HeapAddInt(&histoHeap, gh->glh_frontier, (char *) gh);
    count = lastsize = total = 0;
    while (HeapRemoveTop(&histoHeap, &entry))
    {
	gh = (GlNetHisto *) entry.he_id;
	if (gh->glh_frontier != lastsize)
	{
	    if (count > 0)
		fprintf(fp, "%d: %d\n", lastsize, count);
	    lastsize = gh->glh_frontier;
	    count = 0;
	}
	count++, total++;
    }
    HeapKill(&histoHeap, (void (*)()) NULL);
    if (count > 0)
	fprintf(fp, "%d: %d\n", lastsize, count);
    fprintf(fp, "TOTAL: %d\n", total);

    /* Free memory */
    for (gh = glNetHistoList; gh; gh = gh->glh_next)
	freeMagic((char *) gh);
    glNetHistoList = NULL;

    /* Done */
    (void) fclose(fp);
}

/*
 * ----------------------------------------------------------------------------
 *
 * glStatsInit --
 *
 * Initialize statistics and debugging info for this
 * global routing session.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Zeroes lots of global counters.
 *	Zeroes glNetHistoList.
 *	May create the crossings log file ("CROSSINGS.log") and
 *	    leave glLogFile pointing to the open file.
 *
 * ----------------------------------------------------------------------------
 */

void
glStatsInit()
{
    char *logFileName = "CROSSINGS.log";

    glCrossingsSeen = 0;
    glCrossingsAdded = 0;
    glCrossingsExpanded = 0;
    glCrossingsUsed = 0;
    glGoodRoutes = 0;
    glBadRoutes = 0;
    glNoRoutes = 0;
    glNumTries = 0;

    glNetHistoList = NULL;
    if (DebugIsSet(glDebugID, glDebLog))
    {
	glLogFile = fopen(logFileName, "w");
	if (glLogFile == (FILE *) NULL)
	    perror(logFileName);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * glStatsDone --
 *
 * Print statistics accumulated during this routing session.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Prints statistics.
 *
 * ----------------------------------------------------------------------------
 */

void
glStatsDone(numNets, numTerms)
    int numNets;
    int numTerms;
{

    if (DebugIsSet(glDebugID, glDebVerbose))
    {
	TxPrintf("\n");
	TxPrintf("    %d nets, %d terminals.\n", numNets, numTerms);
	TxPrintf("    %d good, %d bad two-point routes,\n",
		glGoodRoutes, glBadRoutes);
	TxPrintf("    %d failed when considering penalties,\n", glNoRoutes);
	TxPrintf("    %d total connections.\n", glGoodRoutes + glBadRoutes);
	TxPrintf("    %d crossings seen, %d added to heap.\n",
		glCrossingsSeen, glCrossingsAdded);
	TxPrintf("    %d crossings %d used.\n",
		glCrossingsExpanded, glCrossingsUsed);
    }

    if (DebugIsSet(glDebugID, glDebLog) && glLogFile)
	(void) fclose(glLogFile);
    if (DebugIsSet(glDebugID, glDebHisto))
	glHistoDump();
}
