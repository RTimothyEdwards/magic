/*
 * DBcell.c --
 *
 * Place and Delete subcells
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/database/DBcell.c,v 1.2 2008/12/11 04:20:04 tim Exp $";
#endif	/* not lint */

#include <sys/types.h>
#include <stdio.h>

#include "utils/magic.h"
#include "utils/malloc.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "database/databaseInt.h"
#include "utils/undo.h"
#include "utils/signals.h"

int placeCellFunc();
int deleteCellFunc();
Tile * clipCellTile();
void dupTileBody();
void cellTileMerge();
bool ctbListMatch();
void freeCTBList();

struct searchArg
{
    CellUse   * celluse;
    Rect      * rect;
    BPlane    * bplane;
};

#define		TOPLEFT			10
#define		TOPLEFTRIGHT		11
#define		TOPBOTTOM		12
#define         TOPBOTTOMLEFT   	14
#define 	TOPBOTTOMLEFTRIGHT	15

int dbCellDebug = 0;

void
dbInstanceUnplace(CellUse *use)
{
    ASSERT(use != (CellUse *) NULL, "dbInstanceUnplace");

    /* It's important that this code run with interrupts disabled,
     * or else we could leave the subcell tile plane in a weird
     * state.
     */

    BPDelete(use->cu_parent->cd_cellPlane, use);
}



/*
 * ----------------------------------------------------------------------------
 *
 * DBCellFindDup --
 *
 * 	This procedure indicates whether a particular cell is already
 *	present at a particular point in a particular parent.  It is
 *	used to avoid placing duplicate copies of a cell on top of
 *	each other.
 *
 * Results:
 *	The return value is NULL if there is not already a CellUse in parent
 *	that is identical to use (same bbox and def).  If there is a duplicate
 *	already in parent, then the return value is a pointer to its CellUse.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

CellUse *
DBCellFindDup(use, parent)
    CellUse *use;		/* Use that is about to be placed in parent.
				 * Is it a duplicate?
				 */
    CellDef *parent;		/* Parent definiton:  does it already have
				 * something identical to use?
				 */
{
    BPEnum bpe;
    CellUse *dupUse;

    BPEnumInit(&bpe, parent->cd_cellPlane, &use->cu_bbox, BPE_EQUAL,
		"DBCellFindDup");
    while (dupUse = BPEnumNext(&bpe))
	if (dupUse->cu_def == use->cu_def)
	    /* Transforms must be equal---Aligned bounding boxes are
	     * an insufficient measure of exact overlap.
	     */
	    if ((dupUse->cu_transform.t_a == use->cu_transform.t_a) &&
			(dupUse->cu_transform.t_b == use->cu_transform.t_b) &&
			(dupUse->cu_transform.t_c == use->cu_transform.t_c) &&
			(dupUse->cu_transform.t_d == use->cu_transform.t_d) &&
			(dupUse->cu_transform.t_e == use->cu_transform.t_e) &&
			(dupUse->cu_transform.t_f == use->cu_transform.t_f))
		break;

    BPEnumTerm(&bpe);
    return dupUse;
}


/*
 * ----------------------------------------------------------------------------
 *
 * DBPlaceCell --
 * DBPlaceCellNoModify --
 *
 * Add a CellUse to the subcell tile plane of a CellDef.
 * Assumes prior check that the new CellUse is not an exact duplicate
 *     of one already in place.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the subcell tile plane of the given CellDef.
 *	Resets the plowing delta of the CellUse to 0.  Sets the
 *	CellDef's parent pointer to point to the parent def.
 *
 * ----------------------------------------------------------------------------
 */

void
DBPlaceCell (use, def)
    CellUse   * use;	/* new celluse to add to subcell tile plane */
    CellDef   * def;    /* parent cell's definition */
{
    Rect             rect;    /* argument to DBSrCellPlaneArea(), placeCellFunc() */
    BPlane          *bplane;  /* argument to DBSrCellPlaneArea(), placeCellFunc() */
    struct searchArg arg;     /* argument to placeCellFunc() */

    ASSERT(use != (CellUse *) NULL, "DBPlaceCell");
    ASSERT(def, "DBPlaceCell");

    /* To do:  Check non-duplicate placement, check non-duplicate ID */

    use->cu_parent = def;

    /* Be careful not to permit interrupts during this, or the
     * database could be left in a trashed state.
     */

    SigDisableInterrupts();
    BPAdd(def->cd_cellPlane, use);
    def->cd_flags |= CDMODIFIED|CDGETNEWSTAMP;
    if (UndoIsEnabled())
	DBUndoCellUse(use, UNDO_CELL_PLACE);
    SigEnableInterrupts();
}

/* Like DBPlaceCell(), but don't change the flags of the parent cell.	*/
/* This is needed by the bounding box recalculation routine, which may	*/
/* cause the cell to be deleted and replaced for the purpose of		*/
/* capturing the bounding box information in the BPlane structure, but	*/
/* this does not mean that anything in the parent cell has changed.	*/

void
DBPlaceCellNoModify (use, def)
    CellUse   * use;	/* new celluse to add to subcell tile plane */
    CellDef   * def;    /* parent cell's definition */
{
    Rect             rect;    /* argument to DBSrCellPlaneArea(), placeCellFunc() */
    BPlane          *bplane;  /* argument to DBSrCellPlaneArea(), placeCellFunc() */
    struct searchArg arg;     /* argument to placeCellFunc() */

    ASSERT(use != (CellUse *) NULL, "DBPlaceCell");
    ASSERT(def, "DBPlaceCell");

    /* To do:  Check non-duplicate placement, check non-duplicate ID */

    use->cu_parent = def;

    /* Be careful not to permit interrupts during this, or the
     * database could be left in a trashed state.
     */

    SigDisableInterrupts();
    BPAdd(def->cd_cellPlane, use);
    if (UndoIsEnabled())
	DBUndoCellUse(use, UNDO_CELL_PLACE);
    SigEnableInterrupts();
}

/*
 * ----------------------------------------------------------------------------
 * DBDeleteCell --
 *
 * Remove a CellUse from the subcell tile plane of a CellDef.
 * If "nomodify" is TRUE, then don't set the parent cell's CDMODIFIED flag.
 * This is needed when recomputing the bounding box, which should not by
 * itself change the modified state.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the subcell tile plane of the CellDef, sets the
 * 	parent pointer of the deleted CellUse to NULL.
 * ----------------------------------------------------------------------------
 */

void
DBDeleteCell (use)
    CellUse * use;
{
    ASSERT(use != (CellUse *) NULL, "DBDeleteCell");

    /* It's important that this code run with interrupts disabled,
     * or else we could leave the subcell tile plane in a weird
     * state.
     */

    SigDisableInterrupts();
    dbInstanceUnplace(use);
    use->cu_parent->cd_flags |= CDMODIFIED|CDGETNEWSTAMP;
    if (UndoIsEnabled())
	DBUndoCellUse(use, UNDO_CELL_DELETE);
    use->cu_parent = (CellDef *) NULL;
    SigEnableInterrupts();
}

/*
 * ----------------------------------------------------------------------------
 * DBDeleteCellNoModify --
 *
 * Remove a CellUse from the subcell tile plane of a CellDef, as above,
 * but don't set the parent cell's CDMODIFIED flag.  This is needed when
 * recomputing the bounding box, which should not by itself change the
 * modified state.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the subcell tile plane of the CellDef, sets the
 * 	parent pointer of the deleted CellUse to NULL.
 * ----------------------------------------------------------------------------
 */

void
DBDeleteCellNoModify (use)
    CellUse * use;
{
    ASSERT(use != (CellUse *) NULL, "DBDeleteCell");

    /* It's important that this code run with interrupts disabled,
     * or else we could leave the subcell tile plane in a weird
     * state.
     */

    SigDisableInterrupts();
    dbInstanceUnplace(use);
    if (UndoIsEnabled())
	DBUndoCellUse(use, UNDO_CELL_DELETE);
    use->cu_parent = (CellDef *) NULL;
    SigEnableInterrupts();
}
