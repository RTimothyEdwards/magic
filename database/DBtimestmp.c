/* DBtimestamp.c --
 *
 *	Provides routines to help manage the timestamps stored in
 *	cell definitions.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/database/DBtimestmp.c,v 1.2 2009/05/30 03:13:59 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <sys/types.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "database/databaseInt.h"
#include "windows/windows.h"
#include "textio/textio.h"
#include "drc/drc.h"
#include "utils/signals.h"
#include "utils/malloc.h"

/* Overall comments:
 *
 * Cell timestamps are kept around primarily for the benefit
 * of the design rule checker.  In order for the hierarchical
 * and continuous checker to work, we have to make sure that
 * we know whenever cells have changed, even if a cell is
 * edited out of context.  Thus, we record a timestamp value
 * in every cell, which is the time when the cell was last
 * modified.  The timestamp for a cell should always be at
 * least as large as the latest timestamp for any of that
 * cell's descendants (larger timestamps correspond to later
 * times).  If a child is edited out of context, this condition
 * may be violated, so each parent keeps an expected timestamp
 * for each of its children.  If the actual child timestamp is
 * ever found to be different, we must re-check interactions
 * between that child and other pieces of the parent, and then
 * update the parent's timestamp.  If a child changes, then
 * timestamps must be changed in all parents of the child, their
 * parents, and so on, so that we're guaranteed to see interactions
 * coming even from several levels up the tree.
 */

/* The structure below is used to keep track of cells whose timestamps
 * mismatched.  The DBStampMismatch routine creates this structure.
 * at convenient times (between commands), the DBFixMismatch routine
 * is called to process the entries.  It updates bounding boxes (the
 * bounding box update cannot be done at arbitrary times, because it
 * can reorganize tile planes that are pending in searches.  The
 * indentation below is necessary to keep lintpick happy.
 */

    typedef struct mm {
	CellDef *mm_cellDef;		/* CellDef whose stamp was wrong. */
	Rect mm_oldArea;		/* The old area that the cellDef used
					 * to occupy.
					 */
	struct mm *mm_next;		/* Next mismatch record in list. */
    } Mismatch;

    Mismatch *mismatch = NULL;		/* List head. */


/* The time value below is passed to the dbStampFunct so it
 * uses the same value everywhere, and doesn't have to call
 * the system routine repeatedly.
 */

int timestamp;


/*
 * ----------------------------------------------------------------------------
 *	DBFixMismatch --
 *
 * 	This procedure is called when it safe to recompute bounding
 *	boxes in order to fix timestamp mismatches.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Bounding boxes get recomputed and information gets passed to
 *	the DRC module.  The DRC module will recheck both the old and
 *	new areas of the cell whose timestamp mismatched.
 * ----------------------------------------------------------------------------
 */

void
DBFixMismatch()
{
    CellDef *cellDef;
    CellUse *parentUse;
    Rect oldArea, parentArea, tmp;
    int redisplay;
    int firstOne = TRUE;
    Mismatch *tmpm;

    /* It's very important to disable interrupts during this section!
     * Otherwise, we may not paint the recheck tiles properly.
     */

    redisplay = FALSE;
    if (mismatch == NULL) return;
    TxPrintf("Processing timestamp mismatches:");
    SigDisableInterrupts();

    for (tmpm = mismatch; tmpm; tmpm = tmpm->mm_next)
	tmpm->mm_cellDef->cd_flags &= (~CDPROCESSED);

    while (mismatch != NULL)
    {
	/* Be careful to remove the front element from the mismatch
	 * list before processing it, because while processing it we
	 * may add new elements to the list.
	 */

	cellDef = mismatch->mm_cellDef;
	oldArea = mismatch->mm_oldArea;
	freeMagic((char *) mismatch);
	mismatch = mismatch->mm_next;
	if (cellDef->cd_flags & CDPROCESSED) continue;

	(void) DBCellRead(cellDef, (char *) NULL, TRUE, NULL);

	/* Jimmy up the cell's current bounding box, so the following
	 * procedure call will absolutely and positively know that
	 * the bbox has changed.  This is necessary because not all
	 * the uses necessarily reflect the current def bounding box,
	 * and we want DBReComputeArea to be sure to update all of
	 * the uses.
	 */

	cellDef->cd_bbox.r_xtop = cellDef->cd_bbox.r_xbot - 1;
	cellDef->cd_extended.r_xtop = cellDef->cd_extended.r_xbot - 1;
	DBReComputeBbox(cellDef);

	/* Now, for each parent, recheck the parent in both the
	 * old area of the child and the new area.
	 */

	for (parentUse = cellDef->cd_parents; parentUse != NULL;
	    parentUse = parentUse->cu_nextuse)
	{
	    if (parentUse->cu_parent == NULL) continue;
	    DBComputeArrayArea(&oldArea, parentUse, parentUse->cu_xlo,
		parentUse->cu_ylo, &parentArea);
	    DBComputeArrayArea(&oldArea, parentUse, parentUse->cu_xhi,
		parentUse->cu_yhi, &tmp);
	    (void) GeoInclude(&parentArea, &tmp);
	    GeoTransRect(&parentUse->cu_transform, &tmp, &parentArea);
	    DRCCheckThis(parentUse->cu_parent, TT_CHECKSUBCELL, &parentArea);
	    DRCCheckThis(parentUse->cu_parent, TT_CHECKSUBCELL,
		&(parentUse->cu_bbox));
	    redisplay = TRUE;
	}
	cellDef->cd_flags |= CDPROCESSED;
	if (firstOne)
	{
	    TxPrintf(" %s", cellDef->cd_name);
	    firstOne = FALSE;
	}
	else TxPrintf(", %s", cellDef->cd_name);
	TxFlush();	/* This is needed to prevent _doprnt screwups */
    }
    SigEnableInterrupts();
    TxPrintf(".\n");
    TxFlush();
    if (redisplay) WindAreaChanged((MagWindow *) NULL, (Rect *) NULL);
}


/*
 * ----------------------------------------------------------------------------
 *	DBUpdateStamps --
 *
 * 	Updates all timestamps in all cells in the database.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	For every cell that has been modified, its timestamp and
 *	the timestamps of all its parents, grandparents, etc. are
 *	updated.  The result is that the timestamp in any given cell is
 *	at least as great as the timestamps in any of its descendants.
 * ----------------------------------------------------------------------------
 */

void
DBUpdateStamps()
{
    extern int dbStampFunc();
    extern time_t time();

    DBFixMismatch();
    timestamp = time((time_t *) 0);
    (void) DBCellSrDefs(CDGETNEWSTAMP, dbStampFunc, (ClientData) NULL);
}

int
dbStampFunc(cellDef)
    CellDef *cellDef;
{
    CellUse *cu;
    CellDef *cd;

    /* The following check keeps us from making multiple recursive
     * scans of any cell.
     */

    if (cellDef->cd_timestamp == timestamp) return 0;

    cellDef->cd_timestamp = timestamp;
    cellDef->cd_flags &= ~CDGETNEWSTAMP;

    /* printf("Writing new timestamp %d for %s.\n",
	timestamp, cellDef->cd_name); */

    /* If a child's stamp has changed, then the stamps must change
     * in all its parents too.  When the hierarchical DRC becomes
     * completely operational, this recursive update may be unnecessary
     * since the DRC will have painted check tiles in all ancestors.
     */

    for (cu = cellDef->cd_parents; cu != NULL; cu = cu->cu_nextuse)
    {
	cd = cu->cu_parent;
	if (cd == NULL) continue;
	cd->cd_flags |= CDSTAMPSCHANGED;
	(void) dbStampFunc(cd);
    }
    return 0;
}


/*
 * ----------------------------------------------------------------------------
 *	DBStampMismatch --
 *	
 * 	This routine is invoked when a mismatch is discovered for a
 *	cell.  The parameter wrongArea tells what the cell's bounding
 *	box used to be (but the timestamp mismatch means this was
 *	probably wrong, so that area has to be re-design-rule-checked).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	We record the definition on a list of mismatches for later
 *	processing.  When DBFixMismatch is called, it will notify
 *	the design-rule checker to recheck both wrongArea, and
 *	the cell's eventual correct area.
 * ----------------------------------------------------------------------------
 */

void
DBStampMismatch(cellDef, wrongArea)
    CellDef *cellDef;
    Rect *wrongArea;			/* Guess of cell's bounding box that
					 * was wrong.
					 */
{
    Mismatch *mm;
    CellUse *parentUse;

    mm = (Mismatch *) mallocMagic((unsigned) (sizeof (Mismatch)));
    mm->mm_cellDef = cellDef;
    mm->mm_oldArea = *wrongArea;
    mm->mm_next = mismatch;
    mismatch = mm;

    for (parentUse = cellDef->cd_parents; parentUse != NULL;
	parentUse = parentUse->cu_nextuse)
    {
	if (parentUse->cu_parent == NULL) continue;
	parentUse->cu_parent->cd_flags |= CDSTAMPSCHANGED;
    }
}
