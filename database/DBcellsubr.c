/*
 * DBcellsubr.c --
 *
 * Low-level support for cell operations.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/database/DBcellsubr.c,v 1.2 2010/09/15 20:26:08 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/malloc.h"
#include "utils/hash.h"
#include "utils/utils.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "database/databaseInt.h"
#include "utils/signals.h"

    /* Forward declarations */
extern void dbSetPlaneTile();

/*
 * ----------------------------------------------------------------------------
 * DBDescendSubcell --
 *
 * Determine from the supplied mask if a tree search should descend into
 * a cell.  Note that the mask must be one of the following:  1) a single
 * bit representing a window dbw_bitmask, 2) CU_DESCEND_ALL (== 0), or
 * 3) a number that is not a power of two that is defined in database.h
 * as a valid type for xMask (e.g., CU_DESCEND_SPECIAL).
 *
 * Results:
 *	TRUE if we should descend, FALSE if not.
 *
 * Side effects:
 * 	None.
 *
 * Notes:
 *	This routine can be expanded as necessary, adding flags for various
 *	modes of determining whether or not to descend into a subcell.
 *
 * ----------------------------------------------------------------------------
 */

bool
DBDescendSubcell(use, xMask)
    CellUse *use;
    unsigned int xMask;
{
    /* Check single bit (window ID) or zero */
    if (((xMask - 1) & xMask) == 0)
	return ((use->cu_expandMask & xMask) == xMask);

    else switch (xMask) {
	case CU_DESCEND_SPECIAL:
	    return (use->cu_expandMask == CU_DESCEND_SPECIAL);

	case CU_DESCEND_NO_SUBCKT:
	    if ((use->cu_def->cd_flags & CDAVAILABLE) == 0)
		if (!DBCellRead(use->cu_def, (char *) NULL, TRUE, NULL))
		    return FALSE;
	    return (DBIsSubcircuit(use->cu_def)) ? FALSE : TRUE;

	case CU_DESCEND_NO_LOCK:
	    if (use->cu_flags & CU_LOCKED)
		return FALSE;
	    else
		return (use->cu_expandMask == CU_DESCEND_SPECIAL);

	case CU_DESCEND_NO_VENDOR:
	    return (use->cu_def->cd_flags & CDVENDORGDS) ? FALSE : TRUE;

	case CU_DESCEND_NONE:
	    return FALSE;
    }
    return TRUE;	/* in case CU_DESCEND_ALL is not defined as 0 */
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBCellCopyDefBody --
 *
 * Copies the contents of the CellDef pointed to by sourceDef into the
 * CellDef pointed to by destDef.  Only the planes, labels, flags,
 * use-id hash table, and bounding box are copied.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Switches the contents of the bodies of the two CellDefs.
 *
 * ----------------------------------------------------------------------------
 */

void
DBCellCopyDefBody(sourceDef, destDef)
    CellDef *sourceDef;	/* Pointer to CellDef copied from */
    CellDef *destDef;		/* Pointer to CellDef copied to */
{
    int i;
    int dbCopyDefFunc();

    destDef->cd_flags = sourceDef->cd_flags;
    destDef->cd_bbox = sourceDef->cd_bbox;
    destDef->cd_labels = sourceDef->cd_labels;
    destDef->cd_lastLabel = sourceDef->cd_lastLabel;
    destDef->cd_idHash = sourceDef->cd_idHash;
    for (i = 0; i < MAXPLANES; i++)
	destDef->cd_planes[i] = sourceDef->cd_planes[i];
    
    /* Be careful to update parent pointers in the children of dest.
     * Don't allow interrupts to wreck this.
     */
    
    SigDisableInterrupts();
    (void) TiSrArea((Tile *) NULL, destDef->cd_planes[PL_CELL],
	&TiPlaneRect, dbCopyDefFunc, (ClientData) destDef);
    SigEnableInterrupts();
}

int
dbCopyDefFunc(tile, def)
    Tile *tile;			/* Tile to search for subcell uses. */
    CellDef *def;		/* Set parent pointer in each use to this. */
{
    CellTileBody *ctb;

    for (ctb = (CellTileBody *) tile->ti_body;  ctb != NULL;
	ctb = ctb->ctb_next)
    {
	ctb->ctb_use->cu_parent = def;
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBCellClearDef --
 *
 * Empties out all tile planes of the indicated CellDef, making it
 * as though the def had been newly allocated.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The paint and subcells stored in the CellDef are all deleted.
 *	Sets the bounding box to the degenerate (0,0)::(1,1) box.
 *
 * ----------------------------------------------------------------------------
 */

void
DBCellClearDef(cellDef)
    CellDef *cellDef;		/* Pointer to CellDef to be deleted */
{
    int pNum;
    Plane *plane;
    Label *lab;
    Tile *tile;

    /*
     * We want the following searching to be non-interruptible
     * to guarantee that everything gets cleared.
     */

    SigDisableInterrupts();

    /*
     * We use a simple optimization to avoid trying
     * to clear an already empty plane.
     */
    plane = cellDef->cd_planes[PL_CELL];
    tile = TR(plane->pl_left);
    if (TiGetBody(tile) != (ClientData) NULL
	    || LB(tile) != plane->pl_bottom
	    || TR(tile) != plane->pl_right
	    || RT(tile) != plane->pl_top)
    {
	DBClearCellPlane(plane);
    }

    /* Reduce clutter by reinitializing the id hash table */
    HashKill(&cellDef->cd_idHash);
    HashInit(&cellDef->cd_idHash, 16, HT_STRINGKEYS);

    for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
    {
	plane = cellDef->cd_planes[pNum];
	tile = TR(plane->pl_left);
	if (TiGetBody(tile) != TT_SPACE
		|| LB(tile) != plane->pl_bottom
		|| TR(tile) != plane->pl_right
		|| RT(tile) != plane->pl_top)
	    DBClearPaintPlane(plane);
    }
    cellDef->cd_bbox.r_xbot = cellDef->cd_bbox.r_ybot = 0;
    cellDef->cd_bbox.r_xtop = cellDef->cd_bbox.r_ytop = 1;
    cellDef->cd_extended.r_xbot = cellDef->cd_extended.r_ybot = 0;
    cellDef->cd_extended.r_xtop = cellDef->cd_extended.r_ytop = 1;
    for (lab = cellDef->cd_labels; lab; lab = lab->lab_next)
	freeMagic((char *) lab);
    cellDef->cd_labels = (Label *) NULL;
    cellDef->cd_lastLabel = (Label *) NULL;
    SigEnableInterrupts();
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBClearCellPlane --
 *
 * Remove all cell uses contained in the given cell tile plane.
 * Deallocates the Tiles and CellTileBodies contained in the plane,
 * and constructs a new plane containing a single tile with a null
 * tile body.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the database plane given.
 *
 * ----------------------------------------------------------------------------
 */

void
DBClearCellPlane(plane)
    Plane *plane;
{
    Tile *newCenterTile;

    /* Free all tiles from plane, and delete all uses */
    DBFreeCellPlane(plane);

    /* Allocate a new central space tile with a NULL body */
    newCenterTile = TiAlloc();
    plane->pl_hint = newCenterTile;
    TiSetBody(newCenterTile, NULL);
    dbSetPlaneTile(plane, newCenterTile);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBClearPaintPlane --
 *
 * Similar in effect to painting space over an entire tile plane, but
 * much faster.  The resultant tile plane is guaranteed to contain a
 * single central space tile, exactly as though it had been newly allocated.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the database plane given.
 *
 * ----------------------------------------------------------------------------
 */

void
DBClearPaintPlane(plane)
    Plane *plane;
{
    Tile *newCenterTile;

    /* Eliminate all the tiles from this plane */
    DBFreePaintPlane(plane);

    /* Allocate a new central space tile */
    newCenterTile = TiAlloc();
    plane->pl_hint = newCenterTile;
    TiSetBody(newCenterTile, TT_SPACE);
    dbSetPlaneTile(plane, newCenterTile);
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbSetPlaneTile --
 *
 * Set the single central tile of a plane to be that specified.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the plane given.
 *
 * ----------------------------------------------------------------------------
 */

void
dbSetPlaneTile(plane, newCenterTile)
    Plane *plane;
    Tile *newCenterTile;
{
    /*
     * Set the stitches of the newly created center tile
     * to point to the four boundaries of the plane.
     */

    RT(newCenterTile) = plane->pl_top;
    TR(newCenterTile) = plane->pl_right;
    LB(newCenterTile) = plane->pl_bottom;
    BL(newCenterTile) = plane->pl_left;

    /*
     * Set the stitches for the four boundaries of the plane
     * all to point to the newly created center tile.
     */

    RT(plane->pl_bottom) = newCenterTile;
    LB(plane->pl_top) = newCenterTile;
    TR(plane->pl_left) = newCenterTile;
    BL(plane->pl_right) = newCenterTile;

    LEFT(newCenterTile) = TiPlaneRect.r_xbot;
    BOTTOM(newCenterTile) = TiPlaneRect.r_ybot;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBNewPlane --
 *
 * Allocates and initializes a new tile plane for a cell.
 * The new plane contains a single tile whose body is specified by
 * the caller.  The tile extends from minus infinity to plus infinity.
 *
 * Results:
 *	Returns a pointer to a new tile plane.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

Plane *
DBNewPlane(body)
    ClientData body;	/* Body of initial, central tile */
{
    Tile *newtile;

    newtile = TiAlloc();
    TiSetBody(newtile, body);
    LEFT(newtile) = TiPlaneRect.r_xbot;
    BOTTOM(newtile) = TiPlaneRect.r_ybot;

    return (TiNewPlane(newtile));
}
