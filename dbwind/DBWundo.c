/*
 * DBWundo.c --
 *
 *	Procedures for undoing/redoing operations associated
 *	with the dbwind module.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/dbwind/DBWundo.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "windows/windows.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/main.h"
#include "dbwind/dbwind.h"
#include "utils/undo.h"
#include "textio/textio.h"
#include "commands/commands.h"

/*
 * Client identifiers returned by the undo package
 * in dbwUndoInit().
 */
UndoType dbwUndoIDOldEdit, dbwUndoIDNewEdit, dbwUndoIDBox;

/*
 * Function to play events forward/backward.
 */
void dbwUndoChangeEdit(), dbwUndoBoxForw(), dbwUndoBoxBack();

/*
 * Structure to hold all the information needed to switch
 * to a new edit cell.  We rely upon the fact that no CellDef
 * is ever deleted, so it is safe to retain pointers to defs.
 * The defs, transform, and use identifier are used to identify
 * uniquely the cell use affected.
 */
typedef struct
{
    Transform	 e_editToRoot;	/* Transform to root coordinates from edit */
    Transform	 e_rootToEdit;	/* Transform to edit coordinates from root */
    CellDef	*e_rootDef;	/* Root def in the edit cell's home window */
    CellDef	*e_editDef;	/* Edit cell def itself */
    CellDef	*e_parentDef;	/* Parent def of the editcell, or NULL if the
				 * edit cell was a root itself.
				 */
    char	 e_useId[4];	/* Use identifier.  This is a place holder
				 * only; the actual structure is allocated to
				 * hold all the bytes in the use id, plus the
				 * null byte.
				 */
} editUE;

#define	editSize(n)	(sizeof (editUE) - 3 + (n))

/* Structure used for undo-ing changes in the box.  It just holds the
 * box's old and new locations.
 */

typedef struct
{
    CellDef *bue_oldDef;
    Rect bue_oldArea;
    CellDef *bue_newDef;
    Rect bue_newArea;
} BoxUndoEvent;


/*
 * ----------------------------------------------------------------------------
 *
 * dbwUndoInit --
 *
 *	Initialize handling of undo for the dbwind module.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Calls the undo package to add several clients.
 *
 * ----------------------------------------------------------------------------
 */

void
dbwUndoInit()
{
    void (*nullProc)() = NULL;

    dbwUndoIDOldEdit = UndoAddClient(nullProc, nullProc,
	    (UndoEvent *(*)()) NULL, (int (*)()) NULL, nullProc,
	    dbwUndoChangeEdit, "change edit cell");

    dbwUndoIDNewEdit = UndoAddClient(nullProc, nullProc,
	    (UndoEvent *(*)()) NULL, (int (*)()) NULL, dbwUndoChangeEdit,
	    nullProc, "change edit cell");
    
    dbwUndoIDBox = UndoAddClient(nullProc, nullProc,
	    (UndoEvent *(*)()) NULL, (int (*)()) NULL, dbwUndoBoxForw,
	    dbwUndoBoxBack, "box change");
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBWUndoOldEdit --
 * DBWUndoNewEdit --
 *
 *	Record the old and new edit cells when the edit cell changes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Each creates a single undo list entry.
 *
 * ----------------------------------------------------------------------------
 */

void
DBWUndoOldEdit(editUse, editRootDef, editToRootTrans, rootToEditTrans)
    CellUse *editUse;
    CellDef *editRootDef;
    Transform *editToRootTrans, *rootToEditTrans;
{
    char *useid = editUse->cu_id;
    editUE *ep;

    ep = (editUE *) UndoNewEvent(dbwUndoIDOldEdit,
	    (unsigned) editSize(strlen(useid)));
    if (ep == (editUE *) NULL)
	return;

    ep->e_editToRoot = *editToRootTrans;
    ep->e_rootToEdit = *rootToEditTrans;
    ep->e_rootDef = editRootDef;
    ep->e_editDef = editUse->cu_def;
    ep->e_parentDef = editUse->cu_parent;
    (void) strcpy(ep->e_useId, useid);
}

void
DBWUndoNewEdit(editUse, editRootDef, editToRootTrans, rootToEditTrans)
    CellUse *editUse;
    CellDef *editRootDef;
    Transform *editToRootTrans, *rootToEditTrans;
{
    char *useid = editUse->cu_id;
    editUE *ep;

    ep = (editUE *) UndoNewEvent(dbwUndoIDNewEdit,
	    (unsigned) editSize(strlen(useid)));
    if (ep == (editUE *) NULL)
	return;

    ep->e_editToRoot = *editToRootTrans;
    ep->e_rootToEdit = *rootToEditTrans;
    ep->e_rootDef = editRootDef;
    ep->e_editDef = editUse->cu_def;
    ep->e_parentDef = editUse->cu_parent;
    (void) strcpy(ep->e_useId, useid);
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbwUndoChangeEdit
 *
 *	Change the edit cell.
 *	The UndoEvent passed as an argument contains a pointer to
 *	the root cell def of the new edit cell and the parent def
 *	of the edit cell.  Both pointers are safe because we never
 *	delete a CellDef.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes the edit cell.
 *	Redisplays the old edit cell and the new one.
 *
 * ----------------------------------------------------------------------------
 */

void
dbwUndoChangeEdit(ep)
    editUE *ep;
{
    Rect area;
    CellUse *use;
    CellDef *editDef, *parent;
    static Rect origin = {-1, -1, 1, 1};

    /* Redisplay the old edit cell */
    GeoTransRect(&EditToRootTransform, &(EditCellUse->cu_def->cd_bbox), &area);
    DBWAreaChanged(EditRootDef, &area, DBW_ALLWINDOWS, &DBAllButSpaceBits);
    GeoTransRect(&EditToRootTransform, &origin, &area);
    DBWAreaChanged(EditRootDef, &area, DBW_ALLWINDOWS, &DBAllButSpaceBits);

    /* Set up the transforms for the new edit cell */
    EditToRootTransform = ep->e_editToRoot;
    RootToEditTransform = ep->e_rootToEdit;

    /*
     * Search for the use uniquely identified by the parent cell
     * def 'parent' (which may be NULL) and the use identifier
     * 'ep->e_useId'.
     *
     * It's gotta be there.
     */
    EditRootDef = ep->e_rootDef;
    editDef = ep->e_editDef;
    parent = ep->e_parentDef;
    for (use = editDef->cd_parents; use != NULL; use = use->cu_nextuse)
	if (use->cu_parent == parent && strcmp(use->cu_id, ep->e_useId) == 0)
	    break;

    ASSERT(use != (CellUse *) NULL, "dbwUndoChangeEdit");

    TxPrintf("Edit cell is now %s (%s)\n", editDef->cd_name, use->cu_id);
    EditCellUse = use;
    GeoTransRect(&EditToRootTransform, &(EditCellUse->cu_def->cd_bbox), &area);
    DBWAreaChanged(EditRootDef, &area, DBW_ALLWINDOWS, &DBAllButSpaceBits);
    GeoTransRect(&EditToRootTransform, &origin, &area);
    DBWAreaChanged(EditRootDef, &area, DBW_ALLWINDOWS, &DBAllButSpaceBits);
    CmdSetWindCaption(EditCellUse, EditRootDef);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBWUndoBox --
 *
 * 	Remember a box change for later undo-ing.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	An entry is added to the undo list.
 *
 * ----------------------------------------------------------------------------
 */

void
DBWUndoBox(oldDef, oldArea, newDef, newArea)
    CellDef *oldDef;		/* Celldef containing old box. */
    Rect *oldArea;		/* Area of old box in oldDef coords. */
    CellDef *newDef;		/* Celldef containing new box. */
    Rect *newArea;		/* Area of new box in newDef coords. */
{
    BoxUndoEvent *bue;

    bue = (BoxUndoEvent *) UndoNewEvent(dbwUndoIDBox, sizeof(BoxUndoEvent));
    if (bue == NULL) return;

    bue->bue_oldDef = oldDef;
    bue->bue_oldArea = *oldArea;
    bue->bue_newDef = newDef;
    bue->bue_newArea = *newArea;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbwUndoBoxForw --
 * dbwUndoBoxBack --
 *
 * 	This routines are called to undo a change to the box.  They
 *	are invoked by the undo package.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The box's location is modified.
 *
 * ----------------------------------------------------------------------------
 */

void
dbwUndoBoxForw(bue)
    BoxUndoEvent *bue;			/* Event to be redone. */
{
    DBWSetBox(bue->bue_newDef, &bue->bue_newArea);
}

void
dbwUndoBoxBack(bue)
    BoxUndoEvent *bue;			/* Event to be undone. */
{
    DBWSetBox(bue->bue_oldDef, &bue->bue_oldArea);
}
