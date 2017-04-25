/*
 * wireUndo.c --
 *
 * This file contains procedures that implement undo for wiring procedures
 * such as setting the current wire width.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/wiring/wireUndo.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/tech.h"
#include "wiring/wiring.h"
#include "wiring/wireInt.h"
#include "textio/textio.h"
#include "utils/undo.h"

/* The following declaration is for records used to hold undo
 * information for wiring commands such as changing the current
 * layer.  Only the wire-module-specific stuff needs to be undone
 * here:  other stuff, such as selecting the current wire leg
 * and actually painting the wires, is undone in other modules.
 */

typedef struct
{
    TileType wue_oldType;	/* Previous type of wiring material. */
    TileType wue_newType;	/* New type of wiring material. */
    int wue_oldWidth;		/* Previous width of wiring material. */
    int wue_newWidth;		/* New width of wiring material. */
    int wue_oldDir;		/* Previous direction for wiring. */
    int wue_newDir;		/* New direction for wiring. */
} WireUndoEvent;

/* Identifier for wiring undo records. */

UndoType WireUndoClientID;

/* The following statics are used to remember the last values for the
 * wiring variables that were remembered by the undo package, so we
 * know what the values USED to be before the current round of changes.
 */

static TileType wireOldType = TT_SPACE;	/* Last type that we remembered. */
static int wireOldWidth = 2;		/* Last width that we remembered. */
static int wireOldDir = GEO_NORTH;		/* Last direction */


/*
 * ----------------------------------------------------------------------------
 *	WireUndoInit --
 *
 * 	Adds us as a client to the undo package.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds a new client to the undo package, and sets WireUndoClientID.
 * ----------------------------------------------------------------------------
 */

void
WireUndoInit()
{
    extern void WireUndoForw(), WireUndoBack();

    WireUndoClientID = UndoAddClient((void (*)()) NULL, (void (*)()) NULL,
	    (UndoEvent *(*)()) NULL, (int (*)()) NULL, WireUndoForw,
	    WireUndoBack, "wiring parameters");
    if (WireUndoClientID < (UndoType) 0)
	TxError("Couldn't add wiring as an undo client!\n");
}

/*
 * ----------------------------------------------------------------------------
 *	WireRememberForUndo --
 *
 * 	Whenever anybody in the wiring module changes the wiring parameters
 *	(the static variables declared at the beginning of wireOps.c),
 *	they're supposed to call this routine just after the changes so
 *	that we can record information for undoing.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds information to the undo list.  The stuff that's remembered
 *	is just exactly the stuff defined
 * ----------------------------------------------------------------------------
 */

void
WireRememberForUndo()
{
    WireUndoEvent *wue;

    wue = (WireUndoEvent *) UndoNewEvent(WireUndoClientID,
	    sizeof(WireUndoEvent));
    if (wue == NULL) return;

    wue->wue_oldType = wireOldType;
    wue->wue_newType = wireOldType = WireType;
    wue->wue_oldWidth = wireOldWidth;
    wue->wue_newWidth = wireOldWidth = WireWidth;
    wue->wue_oldDir = wireOldDir;
    wue->wue_newDir = wireOldDir = WireLastDir;
}

/*
 * ----------------------------------------------------------------------------
 *	WireUndoForw --
 *	WireUndoBack --
 *
 * 	These two routines are called by the undo package to process
 *	undo events.  WireUndoForw processes events during redo's and
 *	WireUndoBack processes events during undo's
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Wiring parameters are modified.
 * ----------------------------------------------------------------------------
 */

void
WireUndoForw(wue)
    WireUndoEvent *wue;			/* Event to be redone. */
{
    WireType = wireOldType = wue->wue_newType;
    WireWidth = wireOldWidth = wue->wue_newWidth;
    WireLastDir = wireOldDir = wue->wue_newDir;
}

void
WireUndoBack(wue)
    WireUndoEvent *wue;			/* Event to be undone. */
{
    WireType = wireOldType = wue->wue_oldType;
    WireWidth = wireOldWidth = wue->wue_oldWidth;
    WireLastDir = wireOldDir = wue->wue_oldDir;
}
