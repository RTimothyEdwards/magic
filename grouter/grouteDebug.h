/*
 * grouteDebug.h --
 *
 * Definitions of debugging flags for the global router.
 * This is a separate include file so that new debugging flags
 * can be added to it without forcing recompilation of the
 * entire module.
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
 *			and the	Regents of the University of California
 *
 * rcsid $Header: /usr/cvsroot/magic-8.0/grouter/grouteDebug.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 */

    /* Flags */
extern int glDebAllPoints;
extern int glDebChan;
extern int glDebCross;
extern int glDebFast;
extern int glDebGreedy;
extern int glDebHeap;
extern int glDebHisto;
extern int glDebLog;
extern int glDebMaze;
extern int glDebNet;
extern int glDebNewHeaps;
extern int glDebPen;
extern int glDebShowPins;
extern int glDebStemsOnly;
extern int glDebStraight;
extern int glDebTiles;
extern int glDebVerbose;

    /* Arguments to glShowCross */
#define	CROSS_TEMP	0	/* Crossing point considered but not granted */
#define	CROSS_PERM	1	/* Crossing point permanently taken */
#define	CROSS_ERASE	2	/* Crossing point to be erased */

typedef struct glNetHisto
{
    int			 glh_frontier;	/* Total frontier points visited */
    int			 glh_heap;	/* Points removed from top of heap */
    int			 glh_start;	/* Starting points */
    struct glNetHisto	*glh_next;	/* Next entry in list */
} GlNetHisto;

extern char *glOnlyNet;
extern GlNetHisto *glNetHistoList;
extern FILE *glLogFile;
extern int glNumTries;
