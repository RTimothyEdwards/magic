/*
 * ExtTimes.c --
 *
 * Circuit extraction timing.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/extract/ExtTimes.c,v 1.2 2009/05/13 15:03:16 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#ifdef SYSV
#include <sys/param.h>
#include <sys/times.h>
#endif
#include <math.h>		/* for sqrt() function */

#include "utils/magic.h"
#include "utils/utils.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/malloc.h"
#include "textio/textio.h"
#include "extract/extract.h"
#include "extract/extractInt.h"

/*
 * One of the following structures gets allocated for each cell.
 * It holds the statistics we accumulate while extracting that cell.
 */
struct cellStats
{
    CellDef		*cs_def;	/* Which cell */
    struct timeval	 cs_tpaint;	/* Paint-only extraction time */
    struct timeval	 cs_tcell;	/* Extract just this cell */
    struct timeval	 cs_thier;	/* Extract complete tree */
    struct timeval	 cs_tincr;	/* Incremental extraction time */
    int			 cs_fets;	/* Transistor count in this cell */
    int			 cs_rects;	/* Non-space tile count in this cell */
    int			 cs_hfets;	/* Hierarchical transistor count */
    int			 cs_hrects;	/* Hierarchical tile count */
    int			 cs_ffets;	/* Total flat transistor count */
    int			 cs_frects;	/* Total flat tile count */
    long		 cs_area;	/* Total area of cell */
    long		 cs_interarea;	/* Interaction areas sum */
    long		 cs_cliparea;	/* Interaction area, counting each
					 * overlap of interaction areas only
					 * once instead of once for each
					 * interaction area.
					 */
};

/* Hash table of all the above structs, keyed by CellDef */
HashTable cellStatsTable;

/*
 * Cumulative statistics
 */
struct cumStats
{
    double	 cums_min;	/* Smallest value */
    double	 cums_max;	/* Largest value */
    double	 cums_sum;	/* Sum of values */
    double	 cums_sos;	/* Sum of squares */
    int		 cums_n;	/* Number values */
};

struct cumStats cumFetsPerSecPaint;
struct cumStats cumRectsPerSecPaint;
struct cumStats cumFetsPerSecFlat;
struct cumStats cumRectsPerSecFlat;
struct cumStats cumFetsPerSecHier;
struct cumStats cumRectsPerSecHier;
struct cumStats cumIncrTime;
struct cumStats cumPercentClipped;
struct cumStats cumPercentInteraction;
struct cumStats cumTotalArea, cumInteractArea, cumClippedArea;

FILE *extDevNull = NULL;

/* Forward declarations */

struct cellStats *extGetStats();
void extTimesCellFunc();
void extTimesIncrFunc();
void extTimesSummaryFunc();
void extTimesParentFunc();
void extTimeProc();
void extCumInit();
void extCumOutput();
void extCumAdd();
void extPaintOnly(CellDef *);
void extHierCell(CellDef *);
int  extCountTiles();

extern int extDefInitFunc();


/*
 * ----------------------------------------------------------------------------
 *
 * ExtTimes --
 *
 * Time the extractor.
 * All cells in the tree rooted at 'rootUse' are extracted.
 * We report the following times for each cell (seconds of CPU
 * time, accurate to 10 milliseconds).
 *
 *	Time to extract just its paint.
 *	Time to extract it completely.
 *	Time to perform incremental re-extraction if just this cell changed.
 *
 * In addition, we report:
 *
 *	Fets/second paint extraction speed
 *	Fets/second cell extraction speed
 *	Fets/second hierarchical extraction speed
 *	Rects/second paint extraction speed
 *	Rects/second cell extraction speed
 *	Rects/second hierarchical extraction speed
 *
 * Also for each cell, we report the number of transistors, number
 * of rectangles, and rectangles per transistor.
 *
 * In addition, we report the following cumulative information, as
 * means, standard deviation, min, and max:
 *
 *	Fets/second flat extraction speed
 *	Fets/second complete extraction speed
 *	Rects/second flat extraction speed
 *	Rects/second complete extraction speed
 *	Incremental extraction time after changing one cell.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes to the file 'f'.
 *
 * ----------------------------------------------------------------------------
 */

void
ExtTimes(rootUse, f)
    CellUse *rootUse;
    FILE *f;
{
    double clip, inter;
    HashSearch hs;
    HashEntry *he;

    /* Make sure this cell is read in */
    DBCellReadArea(rootUse, &rootUse->cu_def->cd_bbox);

    /* Initialize cumulative statistics */
    extCumInit(&cumFetsPerSecPaint);
    extCumInit(&cumRectsPerSecPaint);
    extCumInit(&cumFetsPerSecFlat);
    extCumInit(&cumRectsPerSecFlat);
    extCumInit(&cumFetsPerSecHier);
    extCumInit(&cumRectsPerSecHier);
    extCumInit(&cumIncrTime);
    extCumInit(&cumPercentClipped);
    extCumInit(&cumPercentInteraction);
    extCumInit(&cumTotalArea);
    extCumInit(&cumInteractArea);
    extCumInit(&cumClippedArea);

    /* Open to /dev/null */
    extDevNull = fopen("/dev/null", "w");
    if (extDevNull == NULL)
    {
	perror("/dev/null");
	return;
    }

    /* Mark all defs as unvisited */
    (void) DBCellSrDefs(0, extDefInitFunc, (ClientData) 0);

    /* Recursively visit all defs in the tree and store in hash table */
    HashInit(&cellStatsTable, 128, 1);
    (void) extTimesInitFunc(rootUse);

    /*
     * Now visit every cell in the hash table and compute
     * its individual statistics.
     */
    TxPrintf("Computing individual cell statistics:\n"); TxFlush();
    HashStartSearch(&hs);
    while (he = HashNext(&cellStatsTable, &hs))
	extTimesCellFunc((struct cellStats *) HashGetValue(he));

    /*
     * Now visit every cell in the hash table and compute
     * its incremental time (ancestors) and hierarchical
     * time (children).
     */
    TxPrintf("Computing hierarchical and incremental statistics:\n"); TxFlush();
    HashStartSearch(&hs);
    while (he = HashNext(&cellStatsTable, &hs))
	extTimesIncrFunc((struct cellStats *) HashGetValue(he));

    /*
     * Compute the summary statistics and output everything.
     * Free each entry in the table as we go.
     */
    TxPrintf("Computing summary statistics:\n"); TxFlush();
    HashStartSearch(&hs);
    while (he = HashNext(&cellStatsTable, &hs))
    {
	extTimesSummaryFunc((struct cellStats *) HashGetValue(he), f);
	freeMagic((char *) HashGetValue(he));
    }

    /* Output the summary statistics */
    fprintf(f, "\n\nSummary statistics:\n\n");
    fprintf(f, "%s %8s %8s %8s %8s\n",
	      "               ", "min", "max", "mean", "std.dev");
    extCumOutput("fets/sec paint ", &cumFetsPerSecPaint, f);
    extCumOutput("fets/sec hier  ", &cumFetsPerSecHier, f);
    extCumOutput("fets/sec flat  ", &cumFetsPerSecFlat, f);
    extCumOutput("rects/sec paint", &cumRectsPerSecPaint, f);
    extCumOutput("rects/sec hier ", &cumRectsPerSecHier, f);
    extCumOutput("rects/sec flat ", &cumRectsPerSecFlat, f);
    extCumOutput("tot incr time  ", &cumIncrTime, f);
    extCumOutput("% cell clipped ", &cumPercentClipped, f);
    extCumOutput("% cell interact", &cumPercentInteraction, f);

    /* Fix up average value to be weighted */
    clip = inter = 0.0;
    if (cumTotalArea.cums_sum > 0)
    {
	clip = 100.0 * cumClippedArea.cums_sum / cumTotalArea.cums_sum;
	inter = 100.0 * cumInteractArea.cums_sum / cumTotalArea.cums_sum;
    }
    fprintf(f, "Mean %% clipped area = %.2f\n", clip);
    fprintf(f, "Mean %% interaction area = %.2f\n", inter);

    /* Free the hash table */
    HashKill(&cellStatsTable);

    (void) fclose(extDevNull);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extTimesInitFunc --
 *
 * Called to add the cellStats struct for use->cu_def to the hash table
 * cellStatsTable if it is not already present, and to initialize this
 * structure.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

int
extTimesInitFunc(use)
    CellUse *use;
{
    CellDef *def = use->cu_def;
    struct cellStats *cs;
    HashEntry *he;

    he = HashFind(&cellStatsTable, (char *) def);
    if (HashGetValue(he))
	return (0);

    /* Not yet visited: add it to the hash table */
    cs = (struct cellStats *) mallocMagic(sizeof (struct cellStats));
    cs->cs_def = def;
    cs->cs_tpaint.tv_sec = cs->cs_tpaint.tv_usec = 0;
    cs->cs_tcell.tv_sec = cs->cs_tcell.tv_usec = 0;
    cs->cs_thier.tv_sec = cs->cs_thier.tv_usec = 0;
    cs->cs_tincr.tv_sec = cs->cs_tincr.tv_usec = 0;
    cs->cs_fets = cs->cs_rects = 0;
    cs->cs_ffets = cs->cs_frects = 0;
    cs->cs_hfets = cs->cs_hrects = 0;
    cs->cs_area = cs->cs_interarea = cs->cs_cliparea = 0;
    HashSetValue(he, (ClientData) cs);

    /* Visit our children */
    (void) DBCellEnum(def, extTimesInitFunc, (ClientData) 0);
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extTimesCellFunc --
 *
 * Called for each cellStats structure in the hash table, we extract
 * the paint of cs->cs_def and then both the paint and subcells, timing
 * both.  Also count the number of fets in this cell and the total number
 * of tiles.  Store this information in:
 *
 *	cs->cs_tpaint		Time to extract paint only
 *	cs->cs_tcell		Time to extract paint and subcells
 *	cs->cs_fets		Number of fets
 *	cs->cs_rects		Number of non-space tiles
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
extTimesCellFunc(cs)
    struct cellStats *cs;
{
    extern long extSubtreeTotalArea;
    extern long extSubtreeInteractionArea;
    extern long extSubtreeClippedArea;
    TransRegion *transList, *tl;
    CellDef *def = cs->cs_def;
    int pNum;

    TxPrintf("Processing %s\n", def->cd_name); TxFlush();

    /* Count the number of transistors */
    transList = (TransRegion *) ExtFindRegions(def, &TiPlaneRect,
		    &ExtCurStyle->exts_transMask, ExtCurStyle->exts_transConn,
		    extUnInit, extTransFirst, extTransEach);
    ExtResetTiles(def, extUnInit);
    for (tl = transList; tl; tl = tl->treg_next)
	cs->cs_fets++;
    ExtFreeLabRegions((LabRegion *) transList);

    /* Count the number of tiles */
    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	(void) DBSrPaintArea((Tile *) NULL, def->cd_planes[pNum], &TiPlaneRect,
		&DBAllButSpaceBits, extCountTiles, (ClientData) cs);

    /* Measure the flat extraction time */
    extTimeProc(extPaintOnly, def, &cs->cs_tpaint);

    /* Measure the total extraction time */
    extSubtreeTotalArea = 0;
    extSubtreeInteractionArea = 0;
    extSubtreeClippedArea = 0;
    extTimeProc(extHierCell, def, &cs->cs_tcell);
    cs->cs_area = extSubtreeTotalArea;
    cs->cs_interarea = extSubtreeInteractionArea;
    cs->cs_cliparea = extSubtreeClippedArea;
}

/*
 * extCountTiles --
 *
 * Count the number of tiles in a cell.
 * Called via DBSrPaintArea by extTimesCellFunc() above.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Increments cs->cs_rects.
 */

int
extCountTiles(tile, cs)
    Tile *tile;			/* UNUSED */
    struct cellStats *cs;
{
    cs->cs_rects++;
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extTimesIncrFunc --
 *
 * All cells have been visited by extTimesCellFunc above.  This procedure
 * accumulates the hierarchical information from both above and below
 * in the tree.  The cs_tcell times for all ancestors, plus this cell,
 * are summed into cs->cs_tincr.  The cs_tcell times for all children,
 * counting each child once, are summed into cs->cs_thier.  The cs_fets
 * and cs_rects for all children, also counting each child once, are
 * summed into cs_hfets and cs_hrects.  Finally, the cs_fets and cs_rects
 * for all children, counting each child as many times as it appears,
 * are summed into cs_ffets and cs_frects.
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
extTimesIncrFunc(cs)
    struct cellStats *cs;
{
    /*
     * Visit all of our parents recursively.
     * This sums the incremental times.
     */
    extTimesParentFunc(cs->cs_def, cs);

    /* Reset all defs */
    (void) DBCellSrDefs(0, extDefInitFunc, (ClientData) 0);

    /*
     * Visit all of our children recursively, visiting
     * each child only once.  This sums the number of fets
     * and rectangles for cs_hfets and cs_hrects.
     */
    (void) extTimesHierFunc(cs->cs_def, cs);

    /* Reset all defs */
    (void) DBCellSrDefs(0, extDefInitFunc, (ClientData) 0);

    /*
     * Visit all of our children recursively, visiting each
     * child as many times as it appears.  This sums the
     * number of fets and rectangles as cs_ffets and cs_frects.
     */
    (void) extTimesFlatFunc(cs->cs_def, cs);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extTimesSummaryFunc --
 *
 * Write the statistics contained in the cellStats structure 'cs'
 * out to the FILE 'f'.  Add them to the summary structures being
 * maintained (cumFetsPerSecPaint, etc).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes to the FILE f.
 *
 * ----------------------------------------------------------------------------
 */

void
extTimesSummaryFunc(cs, f)
    struct cellStats *cs;
    FILE *f;
{
    double tpaint, tcell, thier, tincr;
    double fetspaint, rectspaint;
    double fetshier, rectshier;
    double fetsflat, rectsflat;
    double pctinter, pctclip;

    pctinter = pctclip = 0.0;
    if (cs->cs_area > 0)
    {
	pctinter = ((double)cs->cs_interarea) / ((double)cs->cs_area) * 100.0;
	pctclip = ((double)cs->cs_cliparea) / ((double)cs->cs_area) * 100.0;
    }

    tpaint = cs->cs_tpaint.tv_usec;
    tpaint = (tpaint / 1000000.) + cs->cs_tpaint.tv_sec;

    tcell = cs->cs_tcell.tv_usec;
    tcell = (tcell / 1000000.) + cs->cs_tcell.tv_sec;

    thier = cs->cs_thier.tv_usec;
    thier = (thier / 1000000.) + cs->cs_thier.tv_sec;

    tincr = cs->cs_tincr.tv_usec;
    tincr = (tincr / 1000000.) + cs->cs_tincr.tv_sec;

    fetspaint = fetsflat = fetshier = 0.0;
    rectspaint = rectsflat = rectshier = 0.0;

    if (tpaint > 0.01)
    {
	fetspaint = cs->cs_fets / tpaint;
	rectspaint = cs->cs_rects / tpaint;
    }

    if (thier > 0.01)
    {
	fetshier = cs->cs_hfets / thier;
	rectshier = cs->cs_hrects / thier;
	fetsflat = cs->cs_ffets / thier;
	rectsflat = cs->cs_frects / thier;
    }

    /* Cell name */
    fprintf(f, "\n%8s %8s %s\n", "", "", cs->cs_def->cd_name);

    /* Sizes */
    fprintf(f, "%8d %8d (paint) fets rects\n",
			cs->cs_fets, cs->cs_rects);
    fprintf(f, "%8d %8d (hier) fets rects\n",
			cs->cs_hfets, cs->cs_hrects);
    fprintf(f, "%8d %8d (flat) fets rects\n",
			cs->cs_ffets, cs->cs_frects);

    /* Times */
    fprintf(f, "%8.2f %8.2f Tpaint, Tcell\n", tpaint, tcell);
    fprintf(f, "%8.2f %8.2f Thier, Tincr\n", thier, tincr);

    /* Speeds */
    fprintf(f, "%8.2f %8.2f (paint) fets/sec rects/sec\n",
			    fetspaint, rectspaint);
    fprintf(f, "%8.2f %8.2f (hier)  fets/sec rects/sec\n",
			    fetshier, rectshier);
    fprintf(f, "%8.2f %8.2f (flat)  fets/sec rects/sec\n",
			    fetsflat, rectsflat);

    /* Fraction of area in subtree processing */
    fprintf(f, "%8.2f %8.2f         clip %%  inter %%\n",
			    pctclip, pctinter);

    /* Accumulate statistics */
    if (cs->cs_fets > 0) extCumAdd(&cumFetsPerSecPaint, fetspaint);
    if (cs->cs_rects > 0) extCumAdd(&cumRectsPerSecPaint, rectspaint);
    if (cs->cs_hfets > 0) extCumAdd(&cumFetsPerSecHier, fetshier);
    if (cs->cs_hrects > 0) extCumAdd(&cumRectsPerSecHier, rectshier);
    if (cs->cs_ffets > 0) extCumAdd(&cumFetsPerSecFlat, fetsflat);
    if (cs->cs_frects > 0) extCumAdd(&cumRectsPerSecFlat, rectsflat);
    if (pctclip > 0.0) extCumAdd(&cumPercentClipped, pctclip);
    if (pctinter > 0.0) extCumAdd(&cumPercentInteraction, pctinter);
    extCumAdd(&cumTotalArea, (double) cs->cs_area);
    extCumAdd(&cumInteractArea, (double) cs->cs_interarea);
    extCumAdd(&cumClippedArea, (double) cs->cs_cliparea);
    extCumAdd(&cumIncrTime, tincr);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extTimesParentFunc --
 *
 * Function to visit all the parents of 'def' and add their
 * times to that of 'cs'.  We only add the times if the def
 * being visited is unmarked, ie, its cd_client field is 0.
 * After adding the times for a def, we mark it by setting
 * its cd_client field to 1.
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
extTimesParentFunc(def, cs)
    CellDef *def;
    struct cellStats *cs;
{
    struct cellStats *csForDef;
    CellUse *parent;

    if (def->cd_client)
	return;

    /* Mark this def as visited */
    def->cd_client = (ClientData) 1;

    /* Find its statistics in the table */
    if ((csForDef = extGetStats(def)) == NULL)
	return;

    /* Add the time */
    cs->cs_tincr.tv_sec += csForDef->cs_tcell.tv_sec;
    cs->cs_tincr.tv_usec += csForDef->cs_tcell.tv_usec;
    if (cs->cs_tincr.tv_usec > 1000000)
    {
	cs->cs_tincr.tv_usec -= 1000000;
	cs->cs_tincr.tv_sec += 1;
    }

    /* Visit all our parents */
    for (parent = def->cd_parents; parent; parent = parent->cu_nextuse)
	if (parent->cu_parent)
	    extTimesParentFunc(parent->cu_parent, cs);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extTimesHierFunc --
 *
 * Function to visit all the children of 'def' and add their
 * times to that of 'cs'.  We only add the times if the def
 * being visited is unmarked, ie, its cd_client field is 0.
 * After adding the times for a def, we mark it by setting
 * its cd_client field to 1.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

int
extTimesHierFunc(def, cs)
    CellDef *def;
    struct cellStats *cs;
{
    int extTimesHierUse();
    struct cellStats *csForDef;

    if (def->cd_client)
	return (0);

    /* Mark this def as visited */
    def->cd_client = (ClientData) 1;

    /* Find its statistics in the table */
    if ((csForDef = extGetStats(def)) == NULL)
	return (0);

    /* Add the time */
    cs->cs_thier.tv_sec += csForDef->cs_tcell.tv_sec;
    cs->cs_thier.tv_usec += csForDef->cs_tcell.tv_usec;
    if (cs->cs_thier.tv_usec > 1000000)
    {
	cs->cs_thier.tv_usec -= 1000000;
	cs->cs_thier.tv_sec += 1;
    }

    /* Add the fets and rectangles */
    cs->cs_hfets += csForDef->cs_fets;
    cs->cs_hrects += csForDef->cs_rects;

    /* Visit our children */
    (void) DBCellEnum(def, extTimesHierUse, (ClientData) cs);
    return (0);
}

int
extTimesHierUse(use, cs)
    CellUse *use;
    struct cellStats *cs;
{
    return (extTimesHierFunc(use->cu_def, cs));
}

/*
 * ----------------------------------------------------------------------------
 *
 * extTimesFlatFunc --
 *
 * Function to visit all the children of 'def' and add their
 * fet and rect counts to those of 'cs'.  This is a fully
 * instantiated count, rather than a drawn count, so we count
 * each cell as many times as it appears in an array or as
 * a subcell.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

int
extTimesFlatFunc(def, cs)
    CellDef *def;
    struct cellStats *cs;
{
    struct cellStats *csForDef;
    int extTimesFlatUse();

    /* Find this cell's statistics in the table */
    if ((csForDef = extGetStats(def)) == NULL)
	return (0);

    /* Add the fets and rectangles */
    cs->cs_ffets += csForDef->cs_fets;
    cs->cs_frects += csForDef->cs_rects;

    /* Visit our children */
    (void) DBCellEnum(def, extTimesFlatUse, (ClientData) cs);
    return (0);
}

int
extTimesFlatUse(use, cs)
    CellUse *use;
    struct cellStats *cs;
{
    struct cellStats dummyCS;
    int nx, ny, nel;

    /* Compute statistics for this cell and its children */
    bzero((char *) &dummyCS, sizeof dummyCS);
    (void) extTimesFlatFunc(use->cu_def, &dummyCS);

    /* Scale by number of elements in this array */
    if (use->cu_xlo < use->cu_xhi) nx = use->cu_xhi - use->cu_xlo + 1;
    else nx = use->cu_xlo - use->cu_xhi + 1;
    if (use->cu_ylo < use->cu_yhi) ny = use->cu_yhi - use->cu_ylo + 1;
    else ny = use->cu_ylo - use->cu_yhi + 1;

    nel = nx * ny;

    /* Fets and rects */
    cs->cs_ffets += dummyCS.cs_ffets * nel;
    cs->cs_frects += dummyCS.cs_frects * nel;
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extTimeProc --
 *
 * Time a procedure applied to the CellDef 'def', storing the time
 * in the timeval pointed to by 'tv'.  The procedure should be of
 * the form:
 *
 *	(*proc)(def)
 *	    CellDef *def;
 *	{
 *	}
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See above.
 *
 * Algorithm:
 *	If (*proc)() takes less than a second to run, we run it ten
 *	times to get a better time estimate, then divide by ten.
 *
 * ----------------------------------------------------------------------------
 */

void
extTimeProc(proc, def, tv)
    int (*proc)();
    CellDef *def;
    struct timeval *tv;
{
    int secs, usecs, i;

#ifdef SYSV
    tv->tv_sec = 0;
    tv->tv_usec = 0;
#else
    extern int getrusage();
    struct rusage r1, r2;

    (void) getrusage(RUSAGE_SELF, &r1);
    (*proc)(def);
    (void) getrusage(RUSAGE_SELF, &r2);

    tv->tv_sec = r2.ru_utime.tv_sec - r1.ru_utime.tv_sec;
    tv->tv_usec = r2.ru_utime.tv_usec - r1.ru_utime.tv_usec;
    if (tv->tv_usec < 0)
    {
	tv->tv_usec += 1000000;
	tv->tv_sec -= 1;
    }

    if (tv->tv_sec < 1)
    {
	(void) getrusage(RUSAGE_SELF, &r1);
	for (i = 0; i < 10; i++)
	    (*proc)(def);
	(void) getrusage(RUSAGE_SELF, &r2);
	secs = r2.ru_utime.tv_sec - r1.ru_utime.tv_sec;
	usecs = r2.ru_utime.tv_usec - r1.ru_utime.tv_usec;
	usecs = (usecs + (secs * 1000000)) / 10;
	tv->tv_sec = usecs / 1000000;
	tv->tv_usec = usecs % 1000000;
    }
#endif
}

/*
 * ----------------------------------------------------------------------------
 *
 * extPaintOnly --
 *
 * Called via extTimeProc() above.  Extract just the paint in a cell.
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
extPaintOnly(def)
    CellDef *def;
{
    NodeRegion *reg;

    reg = extBasic(def, extDevNull);
    if (reg) ExtFreeLabRegions((LabRegion *) reg);
    ExtResetTiles(def, extUnInit);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extHierCell --
 *
 * Called via extTimeProc() above.  Extract a cell normally, but
 * send the extracted output to /dev/null.
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
extHierCell(def)
    CellDef *def;
{
    extCellFile(def, extDevNull, FALSE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extCumInit --
 *
 * Initialize a cumulative statistics structure.
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
extCumInit(cum)
    struct cumStats *cum;
{
    cum->cums_min = (double) INFINITY;
    cum->cums_max = (double) MINFINITY;
    cum->cums_sum = 0.0;
    cum->cums_sos = 0.0;
    cum->cums_n = 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extCumOutput --
 *
 * Output a cumulative statistics structure.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes to the FILE 'f'.
 *
 * ----------------------------------------------------------------------------
 */

void
extCumOutput(str, cum, f)
    char *str;			/* Prefix string */
    struct cumStats *cum;
    FILE *f;
{
    double mean, var;

    mean = var = 0.0;
    if (cum->cums_n != 0)
    {
	mean = cum->cums_sum / (double) cum->cums_n;
	var = (cum->cums_sos / (double) cum->cums_n) - (mean * mean);
    }

    fprintf(f, "%s", str);
    if (cum->cums_min < INFINITY) 
	fprintf(f, " %8.2f", cum->cums_min);
    else
	fprintf(f, "   <none>");
    if (cum->cums_max > MINFINITY) 
	fprintf(f, " %8.2f", cum->cums_max);
    else
	fprintf(f, "   <none>");
    fprintf(f, " %8.2f %8.2f\n", mean, sqrt(var));
}

/*
 * ----------------------------------------------------------------------------
 *
 * extCumAdd --
 *
 * Add the value v to the cumulative statistics structure,
 * updating the statistics.
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
extCumAdd(cum, v)
    struct cumStats *cum;
    double v;
{
    if (v < cum->cums_min) cum->cums_min = v;
    if (v > cum->cums_max) cum->cums_max = v;
    cum->cums_sum += v;
    cum->cums_sos += v*v;
    cum->cums_n++;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extGetStats --
 *
 * Return the cellStats record for the CellDef 'def'.
 *
 * Results:
 *	See above.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

struct cellStats *
extGetStats(def)
    CellDef *def;
{
    HashEntry *he;

    he = HashLookOnly(&cellStatsTable, (char *) def);
    if (he == (HashEntry *) NULL)
	return ((struct cellStats *) NULL);

    return ((struct cellStats *) HashGetValue(he));
}

/*
 * ----------------------------------------------------------------------------
 *
 * ExtInterCount --
 *
 * Find all interaction areas in an entire design, and count
 * the fraction of the total area that is really an interaction
 * area.  Report this for each cell in the design, and as a
 * fraction of the total area.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes to the FILE 'f'.
 *
 * ----------------------------------------------------------------------------
 */

int extInterCountHalo;
CellDef *extInterCountDef;

void
ExtInterCount(rootUse, halo, f)
    CellUse *rootUse;
    int halo;
    FILE *f;
{
    double inter;

    /* Make sure this cell is read in */
    DBCellReadArea(rootUse, &rootUse->cu_def->cd_bbox);

    /* Initialize cumulative statistics */
    extCumInit(&cumPercentInteraction);
    extCumInit(&cumTotalArea);
    extCumInit(&cumInteractArea);

    /* Mark all defs as unvisited */
    (void) DBCellSrDefs(0, extDefInitFunc, (ClientData) 0);

    /*
     * Recursively visit all defs in the tree and compute
     * their interaction area.
     */
    extInterCountHalo = halo;
    (void) extInterAreaFunc(rootUse, f);

    /* Mark all defs as unvisited */
    (void) DBCellSrDefs(0, extDefInitFunc, (ClientData) 0);

    /* Output the summary statistics */
    fprintf(f, "\n\nSummary statistics:\n\n");
    fprintf(f, "%s %8s %8s %8s %8s\n",
	      "               ", "min", "max", "mean", "std.dev");
    extCumOutput("% cell interact", &cumPercentInteraction, f);

    /* Fix up average value to be weighted */
    inter = 0.0;
    if (cumTotalArea.cums_sum > 0)
	inter = 100.0 * cumInteractArea.cums_sum / cumTotalArea.cums_sum;
    fprintf(f, "Mean %% interaction area = %.2f\n", inter);
}

int
extInterAreaFunc(use, f)
    CellUse *use;
    FILE *f;
{
    static Plane *interPlane = (Plane *) NULL;
    CellDef *def = use->cu_def;
    int extInterCountFunc();
    int area, interarea;
    double pctinter;

    if (interPlane == NULL)
	interPlane = DBNewPlane((ClientData) TT_SPACE);

    /* Skip if already visited */
    if (def->cd_client)
	return (0);

    /* Mark as visited */
    def->cd_client = (ClientData) 1;

    /* Compute interaction area */
    extInterCountDef = def;
    ExtFindInteractions(def, extInterCountHalo, 0, interPlane);
    interarea = 0;
    (void) DBSrPaintArea((Tile *) NULL, interPlane, &TiPlaneRect,
	    &DBAllButSpaceBits, extInterCountFunc, (ClientData) &interarea);
    DBClearPaintPlane(interPlane);
    area = (def->cd_bbox.r_xtop - def->cd_bbox.r_xbot)
	*  (def->cd_bbox.r_ytop - def->cd_bbox.r_ybot);

    pctinter = 0.0;
    if (area > 0)
	pctinter = ((double) interarea) / ((double) area) * 100.0;
    if (pctinter > 0.0) extCumAdd(&cumPercentInteraction, pctinter);
    extCumAdd(&cumTotalArea, (double) area);
    extCumAdd(&cumInteractArea, (double) interarea);

    fprintf(f, "%7.2f%%  %s\n", pctinter, def->cd_name);

    /* Visit our children */
    (void) DBCellEnum(def, extInterAreaFunc, (ClientData) f);
    return (0);
}

int
extInterCountFunc(tile, pArea)
    Tile *tile;
    int *pArea;
{
    Rect r;

    TITORECT(tile, &r);
    GEOCLIP(&r, &extInterCountDef->cd_bbox);
    *pArea += (r.r_xtop - r.r_xbot) * (r.r_ytop - r.r_ybot);
    return (0);
}
