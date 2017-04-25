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
    Plane     * plane;
};

#define		TOPLEFT			10
#define		TOPLEFTRIGHT		11
#define		TOPBOTTOM		12
#define         TOPBOTTOMLEFT   	14
#define 	TOPBOTTOMLEFTRIGHT	15

int dbCellDebug = 0;

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
    Tile *tile;
    CellTileBody *body;
    CellUse *checkUse;

    tile = TiSrPoint((Tile *) NULL, parent->cd_planes[PL_CELL],
	    &use->cu_bbox.r_ll);
    
    for (body = (CellTileBody *) TiGetBody(tile);
	body != NULL;
	body = body->ctb_next)
    {
	checkUse = body->ctb_use;
	if (use->cu_def != checkUse->cu_def) continue;
	if ((use->cu_bbox.r_xbot != checkUse->cu_bbox.r_xbot)
	    || (use->cu_bbox.r_xtop != checkUse->cu_bbox.r_xtop)
	    || (use->cu_bbox.r_ybot != checkUse->cu_bbox.r_ybot)
	    || (use->cu_bbox.r_ytop != checkUse->cu_bbox.r_ytop))
	    continue;
	return checkUse;
    }
    return (CellUse *) NULL;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBPlaceCell --
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
DBPlaceCell (celluse, targetcell)
    CellUse   * celluse;	/* new celluse to add to subcell tile plane */
    CellDef   * targetcell;     /* parent cell's definition */
{
    Rect             rect;	/* argument to TiSrArea(), placeCellFunc() */
    Plane          * plane;     /* argument to TiSrArea(), placeCellFunc() */
    struct searchArg arg;       /* argument to placeCellFunc() */

    ASSERT(celluse != (CellUse *) NULL, "DBPlaceCell");
    celluse->cu_parent = targetcell;
    plane = targetcell->cd_planes[PL_CELL];                   /* assign plane */
    rect = celluse->cu_bbox;
    /* rect = celluse->cu_extended; */
    arg.rect = &rect;
    arg.celluse = celluse;
    arg.plane = plane;

    /* Be careful not to permit interrupts during this, or the
     * database could be left in a trashed state.
     */

    SigDisableInterrupts();
    (void) TiSrArea((Tile *) NULL, plane, &rect, placeCellFunc,
	(ClientData) &arg);
    targetcell->cd_flags |= CDMODIFIED|CDGETNEWSTAMP;
    if (UndoIsEnabled())
	DBUndoCellUse(celluse, UNDO_CELL_PLACE);
    SigEnableInterrupts();
}

/*
 * ----------------------------------------------------------------------------
 * DBDeleteCell --
 *
 * Remove a CellUse from the subcell tile plane of a CellDef.
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
DBDeleteCell (celluse)
    CellUse * celluse;
{
    Rect             rect;	/* argument to TiSrArea(), deleteCellFunc() */
    Plane          * plane;     /* argument to TiSrArea(), deleteCellFunc() */
    struct searchArg arg;	/* argument to deleteCellFunc() */

    ASSERT(celluse != (CellUse *) NULL, "DBDeleteCell");
    plane = celluse->cu_parent->cd_planes[PL_CELL];           /* assign plane */
    rect = celluse->cu_bbox;
    arg.rect = &rect;
    arg.plane = plane;
    arg.celluse = celluse;

    /* It's important that this code run with interrupts disabled,
     * or else we could leave the subcell tile plane in a weird
     * state.
     */

    SigDisableInterrupts();
    (void) TiSrArea((Tile *) NULL, plane, &rect, deleteCellFunc,
	(ClientData) &arg);
    celluse->cu_parent->cd_flags |= CDMODIFIED|CDGETNEWSTAMP;
    if (UndoIsEnabled())
	DBUndoCellUse(celluse, UNDO_CELL_DELETE);
    celluse->cu_parent = (CellDef *) NULL;
    SigEnableInterrupts();
}

/*
 * ----------------------------------------------------------------------------
 * placeCellFunc --
 *
 * Add a new subcell to a tile.
 * Clip the tile with respect to the subcell's bounding box.
 * Insert the new CellTileBody into the linked list in ascending order
 *      based on the celluse pointer.
 * This function is passed to TiSrArea.
 *
 * Results:
 *	0 is always returned.
 *
 * Side effects:
 *	Modifies the subcell tile plane of the appropriate CellDef.
 *      Allocates a new CellTileBody.
 * ----------------------------------------------------------------------------
 */

int
placeCellFunc (tile, arg)
    Tile             * tile;		/* target tile */
    struct searchArg * arg;		/* celluse, rect, plane */
{
    Tile         * tp;
    CellTileBody * body, * ctp, * ctplast;

#ifdef	CELLDEBUG
    if (dbCellDebug) TxPrintf("placeCellFunc called %x\n",tile);
#endif	/* CELLDEBUG */

    tp = clipCellTile (tile, arg->plane, arg->rect);

    body = (CellTileBody *) mallocMagic((unsigned) (sizeof (CellTileBody)));
    body->ctb_use = arg->celluse;

    ctp = (CellTileBody *) tp->ti_body;
    ctplast = ctp;
    while ((ctp != (CellTileBody *) NULL) && (ctp->ctb_use > body->ctb_use))
    {
	ctplast = ctp;
	ctp = ctp->ctb_next;
    }
    body->ctb_next = ctp;

    if (ctp == (CellTileBody *) tp->ti_body)   /* empty list or front of list */
	TiSetBody(tp, body);
    else			   	   /* after at least one CellTileBody */
	ctplast->ctb_next = body;

/* merge tiles back into the the plane */
/* requires that TiSrArea visit tiles in NW to SE wavefront */

    if ( RIGHT(tp) == arg->rect->r_xtop)
    {
        if (BOTTOM(tp) == arg->rect->r_ybot)
            cellTileMerge (tp, arg->plane, TOPBOTTOMLEFTRIGHT);
        else
            cellTileMerge (tp, arg->plane, TOPLEFTRIGHT);
    }
    else if (BOTTOM(tp) == arg->rect->r_ybot)
            cellTileMerge (tp, arg->plane, TOPBOTTOMLEFT);
    else
    	    cellTileMerge (tp, arg->plane, TOPLEFT);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 * deleteCellFunc --
 *
 * Remove a subcell from a tile.
 * This function is passed to TiSrArea.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	Modifies the subcell tile plane of the appropriate CellDef.
 *      Deallocates a CellTileBody.
 * ----------------------------------------------------------------------------
 */

int
deleteCellFunc (tile, arg)
    Tile             * tile;
    struct searchArg * arg;			/* plane, rect */
{
    CellTileBody * ctp;			/* CellTileBody to be freed */
    CellTileBody * ctplast;		/* follows one behind ctp */
    CellUse      * celluse;

#ifdef	CELLDEBUG
    if (dbCellDebug) TxPrintf("deleteCellFunc called %x\n",tile);
#endif	/* CELLDEBUG */

    celluse = arg->celluse;

		/* find the appropriate CellTileBody in the linked list */

    ctp = (CellTileBody *) tile->ti_body;
    ctplast = ctp;
    while ((ctp != (CellTileBody *) NULL) && (ctp->ctb_use != celluse))
    {
	ctplast = ctp;
	ctp = ctp->ctb_next;
    }
				/* there should have been a match */
    if (ctp == (CellTileBody *) NULL)
    {
	ASSERT (ctp != (CellTileBody *) NULL, "deleteCellFunc");
	return 0;
    }
	

			/* relink the list with one CellTileBody deleted */
    if (ctp == ctplast) 			/* front of list */
	TiSetBody(tile, ctp->ctb_next);
    else					/* beyond front of list */
	ctplast->ctb_next = ctp->ctb_next;

    freeMagic((char *) ctp);

/* merge tiles back into the the plane */
/* requires that TiSrArea visit tiles in NW to SE wavefront */

    if ( RIGHT(tile) == arg->rect->r_xtop)
    {
        if (BOTTOM(tile) == arg->rect->r_ybot)
            cellTileMerge (tile, arg->plane, TOPBOTTOMLEFTRIGHT);
        else
            cellTileMerge (tile, arg->plane, TOPLEFTRIGHT);
    }
    else if (BOTTOM(tile) == arg->rect->r_ybot)
            cellTileMerge (tile, arg->plane, TOPBOTTOMLEFT);
    else
    	    cellTileMerge (tile, arg->plane, TOPLEFT);
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 * clipCellTile -- 
 *
 * Clip the given tile against the given rectangle.
 *
 * Results:
 *	Returns a pointer to the clipped tile.
 *
 * Side effects:
 *	Modifies the database plane that contains the given tile.
 * ----------------------------------------------------------------------------
 */

Tile *
clipCellTile (tile, plane, rect)
    Tile     * tile;
    Plane    * plane;
    Rect     * rect;
{
    Tile     * newtile;

    if (TOP(tile) > rect->r_ytop)
    {
#ifdef	CELLDEBUG
	if (dbCellDebug) TxPrintf("clipCellTile calls TiSplitY TOP\n");
#endif	/* CELLDEBUG */

        newtile = TiSplitY (tile, rect->r_ytop);	/* no merge */
	dupTileBody (tile, newtile);
    }
    if (BOTTOM(tile) < rect->r_ybot)
    {
#ifdef	CELLDEBUG
	if (dbCellDebug) TxPrintf("clipCellTile calls TiSplitY BOTTOM\n");
#endif	/* CELLDEBUG */

	newtile = tile;
        tile = TiSplitY (tile, rect->r_ybot);		/* no merge */
	dupTileBody (newtile, tile);
    }
    if (RIGHT(tile) > rect->r_xtop)
    {
#ifdef	CELLDEBUG
	if (dbCellDebug) TxPrintf("clipCellTile calls TiSplitX RIGHT\n");
#endif	/* CELLDEBUG */

        newtile = TiSplitX (tile, rect->r_xtop);
	dupTileBody (tile, newtile);
        cellTileMerge (newtile, plane, TOPBOTTOM);
    }
    if (LEFT(tile) < rect->r_xbot)
    {
#ifdef	CELLDEBUG
	if (dbCellDebug) TxPrintf("clipCellTile calls TiSplitX LEFT\n");
#endif	/* CELLDEBUG */

	newtile = tile;
        tile = TiSplitX (tile, rect->r_xbot);
	dupTileBody (newtile, tile);
        cellTileMerge (newtile, plane, TOPBOTTOM);
    }
    return (tile);
} /* clipCellTile */

/*
 * ----------------------------------------------------------------------------
 * dupTileBody -- 
 *
 * Duplicate the body of an old tile as the body for a new tile.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Allocates new CellTileBodies unless the old tile was a space tile.
 * ----------------------------------------------------------------------------
 */

void
dupTileBody (oldtp, newtp)
    Tile * 	oldtp;
    Tile *      newtp;
{
    CellTileBody * oldctb, * newctb, * newctblast;

    oldctb = (CellTileBody *) oldtp->ti_body;
    if (oldctb != (CellTileBody *) NULL)
    {
	newctb = (CellTileBody *) mallocMagic((unsigned) (sizeof (CellTileBody)));
	TiSetBody(newtp, newctb);
	newctb->ctb_use = oldctb->ctb_use;

	oldctb = oldctb->ctb_next;
	newctblast = newctb;

	while (oldctb != (CellTileBody *) NULL)
	{
	    newctb = (CellTileBody *) mallocMagic((unsigned) (sizeof (CellTileBody)));
	    newctblast->ctb_next = newctb;
	    newctb->ctb_use = oldctb->ctb_use;

            oldctb = oldctb->ctb_next;
	    newctblast = newctb;
	}
	newctblast->ctb_next = (CellTileBody *) NULL;
    }
    else TiSetBody(newtp, NULL);
} /* dupTileBody */

/*
 * ----------------------------------------------------------------------------
 * cellTileMerge -- 
 *
 * Merge the given tile with its plane in the directions specified.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the database plane that contains the given tile.
 * ----------------------------------------------------------------------------
 */

void
cellTileMerge (tile, plane, direction)
    Tile      * tile;
    Plane     * plane;
    int		direction; 	/* TOP = 8, BOTTOM = 4, LEFT = 2, RIGHT = 1 */
{
    Point       topleft, bottomright;
    Tile      * dummy, * tpleft, * tpright, * tp1, * tp2;

#ifdef	CELLDEBUG
    if (dbCellDebug) TxPrintf("cellTileMerge %x\n",tile);
#endif	/* CELLDEBUG */

    topleft.p_x = LEFT(tile);
    topleft.p_y = TOP(tile);
    bottomright.p_x = RIGHT(tile);
    bottomright.p_y = BOTTOM(tile);

    if ((direction >> 1) % 2)			/* LEFT */
    {
	tpright = tile;
        tpleft = BL(tpright);

#ifdef	CELLDEBUG
	if (dbCellDebug) TxPrintf("LEFT %x %x\n",tpleft,tpright);
#endif	/* CELLDEBUG */

	while (BOTTOM(tpleft) < topleft.p_y)	/* go up left edge */
	{
	    if (ctbListMatch (tpleft, tpright))
	    {
	        if (BOTTOM(tpleft) < BOTTOM(tpright))
		{
		    dummy = tpleft;
		    tpleft = TiSplitY (tpleft, BOTTOM (tpright));
		    dupTileBody (dummy, tpleft);
		}
		else if (BOTTOM(tpleft) > BOTTOM(tpright))
		{
		    dummy = tpright;
		    tpright = TiSplitY (tpright, BOTTOM (tpleft));
		    dupTileBody (dummy, tpright);
		}

		if (TOP(tpleft) > TOP(tpright))
		{
		    dummy = TiSplitY (tpleft, TOP(tpright));
		    dupTileBody (tpleft, dummy);
		}
		else if (TOP(tpright) > TOP(tpleft))
		{
		    dummy = TiSplitY (tpright, TOP(tpleft));
		    dupTileBody (tpright, dummy);
		}

		freeCTBList (tpright);
		TiJoinX (tpleft, tpright, plane);  /* tpright disappears */

		tpright = RT(tpleft);
		if (BOTTOM(tpright) < topleft.p_y) tpleft = BL(tpright);
		else tpleft = tpright;	/* we're off the top of the tile */
					/* this will break the while loop */
	    } /* if (ctbListMatch (tpleft, tpright)) */

	    else tpleft = RT(tpleft);
	} /* while */
	tile = tpleft;		/* for TiSrPoint in next IF statement */
    }

    if (direction % 2)				/* RIGHT */
    {
	tpright = TiSrPoint (tile, plane, &bottomright);
	--(bottomright.p_x);
	tpleft = TiSrPoint (tpright, plane, &bottomright);
	++(bottomright.p_x);
   
#ifdef	CELLDEBUG
	if (dbCellDebug) TxPrintf("RIGHT %x %x\n",tpleft,tpright);
#endif	/* CELLDEBUG */

	while (BOTTOM(tpright) < topleft.p_y)	/* go up right edge */
	{
	    if (ctbListMatch (tpleft, tpright))
	    {
	        if (BOTTOM(tpright) < BOTTOM(tpleft))
		{
		    dummy = tpright;
		    tpright = TiSplitY (tpright, BOTTOM(tpleft));
		    dupTileBody (dummy, tpright);
		}
		else if (BOTTOM(tpleft) < BOTTOM(tpright))
		{
		    dummy = tpleft;
		    tpleft = TiSplitY (tpleft, BOTTOM(tpright));
		    dupTileBody (dummy, tpleft);
		}

		if (TOP(tpright) > TOP(tpleft))
		{
		    dummy = TiSplitY (tpright, TOP(tpleft));
		    dupTileBody (tpright, dummy);
		}
		else if (TOP(tpleft) > TOP(tpright))
		{
   		    dummy = TiSplitY (tpleft, TOP(tpright));
		    dupTileBody (tpleft, dummy);
		}

		freeCTBList (tpright);
		TiJoinX (tpleft, tpright, plane);  /* tpright disappears */

		tpright = RT(tpleft);
		while (LEFT(tpright) > bottomright.p_x) tpright = BL(tpright);

		/* tpleft can be garbage if we're off the top of the loop, */
		/* but it doesn't matter since the expression tests tpright */

		tpleft = BL(tpright);
	    } /* if (ctbListMatch (tpleft, tpright)) */

	    else
	    {
		tpright = RT(tpright);
	        while (LEFT(tpright) > bottomright.p_x) tpright = BL(tpright);
		tpleft = BL(tpright);		/* left side merges may have */
						/* created more tiles */
	    }
	} /* while */
	tile = tpright;		/* for TiSrPoint in next IF statement */
    }

    if ((direction >> 3) % 2)			/* TOP */
    {
	tp1 = TiSrPoint (tile, plane, &topleft);	/* merge across top */

	--(topleft.p_y);
	tp2 = TiSrPoint (tile, plane, &topleft);/* top slice of original tile */
	++(topleft.p_y);

#ifdef	CELLDEBUG
	if (dbCellDebug) TxPrintf("TOP %x %x\n",tp1,tp2);
#endif	/* CELLDEBUG */

	if ((LEFT(tp1) == LEFT(tp2)  ) &&
	    (RIGHT(tp1) == RIGHT(tp2)) &&
	    (ctbListMatch (tp1, tp2) ))
	{
	    freeCTBList (tp2);
	    TiJoinY (tp1, tp2, plane);
	}

	tile = tp1;		/* for TiSrPoint in next IF statement */
    }

    if ((direction >> 2) % 2)			/* BOTTOM */
    {
	--(bottomright.p_x);
	                                       /* bottom slice of orig tile */
	tp1 = TiSrPoint (tile, plane, &bottomright);

	--(bottomright.p_y);
	tp2 = TiSrPoint (tile, plane, &bottomright); /* merge across bottom */

#ifdef	CELLDEBUG
	if (dbCellDebug) TxPrintf("BOTTOM %x %x\n",tp1,tp2);
#endif	/* CELLDEBUG */

	if ((LEFT(tp1) == LEFT(tp2)  ) &&
	    (RIGHT(tp1) == RIGHT(tp2)) &&
	    (ctbListMatch (tp1, tp2) ))
	{
	    freeCTBList (tp2);
	    TiJoinY (tp1, tp2, plane);
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 * freeCTBList -- 
 *
 * Free all CellTileBodies attached to the give tile.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees CellTileBodies.
 * ----------------------------------------------------------------------------
 */

void
freeCTBList (tile)
    Tile      * tile;
{
    CellTileBody * ctp, * ctplast;

    ctp = (CellTileBody *) tile->ti_body;
    while (ctp != (CellTileBody *) NULL)
    {
	ctplast = ctp;
	ctp = ctp->ctb_next;
	freeMagic((char *) ctplast);
    }
    TiSetBody(tile, NULL);
}

/*
 * ----------------------------------------------------------------------------
 * ctbListMatch -- 
 *
 * Compare two linked lists of CellTileBodies, assuming that they are
 * sorted in ascending order by celluse pointers.
 *
 * Results:
 *	True if the tiles have identical lists of CellTileBodies.
 *
 * Side effects:
 *      None.
 * ----------------------------------------------------------------------------
 */

bool
ctbListMatch (tp1, tp2)
    Tile      * tp1, * tp2;
{
    CellTileBody * ctp1, * ctp2;

    ctp1 = (CellTileBody *) tp1->ti_body;
    ctp2 = (CellTileBody *) tp2->ti_body;
    while (ctp1 && ctp2 && (ctp1->ctb_use == ctp2->ctb_use))
	ctp1 = ctp1->ctb_next, ctp2 = ctp2->ctb_next;

    return ((ctp1 == (CellTileBody *) NULL) && (ctp2 == (CellTileBody *) NULL));
}
