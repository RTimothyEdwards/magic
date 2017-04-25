/* selUndo.c -
 *
 *	This file provides routines for undo-ing and redo-ing the
 *	the selection.  Most of the undo-ing is handled automatically
 *	by enabling undo-ing when the selection cell is modified.
 *	All this file does is record things to be redisplayed, since
 *	the normal undo package won't handle that.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/select/selUndo.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "dbwind/dbwind.h"
#include "utils/undo.h"
#include "commands/commands.h"
#include "textio/textio.h"
#include "select/select.h"
#include "select/selInt.h"

/* Each selection modification causes two records of the following
 * format to be added to the undo event list.  The first record
 * is added BEFORE the modification (sue_before is TRUE), and the
 * second is added afterwards.  The reason for doubling the events
 * is that we can't redisplay selection information until after the
 * selection has been modified.  This requires events to be in
 * different places depending on whether we're undo-ing or redo-ing.
 */

typedef struct
{
    CellDef *sue_def;		/* Definition in which selection must be
				 * redisplayed.
				 */
    Rect sue_area;		/* Area of sue_def in which selection info
				 * must be redisplayed.
				 */
    bool sue_before;		/* TRUE means this entry was made before
				 * the selection modifications.  FALSE
				 * means afterwards.
				 */
} SelUndoEvent;

/* Identifier for selection undo records: */

UndoType SelUndoClientID;

/* Network selection differs from other selection modes in two fundamental
 * ways: 1) it is almost always done for informational purposes (that is,
 * one does not usually copy, move, or stretch the entire network), and
 * 2) it usually involves a LOT of tiles.  Consequently, it makes sense
 * to define a separate "undo" operation for net selection, in which only
 * the information needed to reconstruct the event is retained.  This
 * eliminates the worst problem with selections (as previously handled
 * by the SelUndoClient, above), in which querying the power or ground
 * network of a large chip gobbles vast amount of memory as magic
 * remembers every single tile paint operation.  This memory is kept
 * "forever" (at least as far as the undo stack depth, which is pretty
 * large), and so continued queries on large networks quickly fills up
 * memory.  SelUndoNetClient is a much more efficient way to handle
 * the problem.  Although the "undo" command takes time to regenerate
 * the network from scratch, use of "undo" on network selections is
 * actually rather rare in practice, and the memory savings far outweighs
 * the difference in time to regenerate the net.  Eliminating the need
 * to malloc and fill giant chunks of memory reduces the time required to
 * select the network, anyway.
 *
 * SelUndoNetClient method added by Nishit, July 7-9, 2004
 */

typedef struct
{
    CellDef *sue_def;		/* Definition in which net selection
				 * must be redisplayed
				 */
    Point sue_startpoint;	/* One valid coordinate for the net, to
				 * use as the startpoint for net regeneration
				 */
    TileType sue_type;		/* Type of tile; in conjunction with the
				 * startpoint, uniquely identifies the net.
				 */
    bool sue_less;		/* value of "less" passed to SelectNet */
    bool sue_before;		/* TRUE means this entry was made before
				 * the net selection.  FALSE means it was
				 * made afterwards.
				 */
} SelUndoNetEvent;

/* Identifier for network selection undo records */

UndoType SelUndoNetClientID;


/*
 * ----------------------------------------------------------------------------
 *
 * SelUndoInit --
 *
 * 	Adds us as a client to the undo package.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds a new client to the undo package, and sets SelUndoClientID.
 *
 * ----------------------------------------------------------------------------
 */

void
SelUndoInit()
{
    extern void SelUndoForw(), SelUndoBack();
    extern void SelUndoNetForw(), SelUndoNetBack();

    SelUndoClientID = UndoAddClient((void (*)()) NULL, (void (*)()) NULL,
		(UndoEvent *(*)()) NULL, (int (*)()) NULL, SelUndoForw,
		SelUndoBack, "selection");

    if (SelUndoClientID < (UndoType) 0)
	TxError("Couldn't add selection as an undo client!\n");

    /* Special undo client for net selection (Nishit, July 8, 2004) */
    SelUndoNetClientID = UndoAddClient((void (*)()) NULL, (void (*)()) NULL,
		(UndoEvent *(*)()) NULL, (int (*)()) NULL, SelUndoNetForw,
		SelUndoNetBack, "net selection");

    if (SelUndoNetClientID < (UndoType) 0)
	TxError("Couldn't add net selection as an undo client!\n");
}

/*
 * ----------------------------------------------------------------------------
 *
 * SelRememberForUndo--
 *
 * 	This routine is called twice whenever the selection is modified.
 *	It must be called once before modifying the selection.  In this
 *	case before is TRUE and the other arguments are arbitrary.  It
 *	must also be called again after the selection is modified.  In
 *	this case before is FALSE and the other fields indicate exactly
 *	which area was modified.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds information to the undo list.
 *
 * ----------------------------------------------------------------------------
 */

void
SelRememberForUndo(before, def, area)
    bool before;		/* TRUE means caller is about to modify
				 * the given area of the selection.  FALSE
				 * means the caller has just modified
				 * the area.
				 */
    CellDef *def;		/* Root definition on top of whom selection
				 * information was just modified.
				 */
    Rect *area;			/* The area of def where selection info
				 * changed.  This pointer may be NULL, even
				 * on the second call, if there's no need
				 * to do redisplay during undo's.  This is
				 * the case if layout information is being
				 * modified over the area of the selection:
				 * when layout is redisplayed, selection info
				 * will automatically be redisplayed too.
				 */
{
    SelUndoEvent *sue;
    static SelUndoEvent *beforeEvent = NULL;
    static Rect nullRect = {0, 0, -1, -1};

    sue = (SelUndoEvent *) UndoNewEvent(SelUndoClientID, sizeof(SelUndoEvent));
    if (sue == NULL) return;

    /* We don't have complete information when the "before" event is
     * created, so save around its address and fill in the event when
     * the "after" event is created.
     */
    
    if (before)
    {
	sue->sue_before = TRUE;
	sue->sue_def = NULL;

	beforeEvent = sue;
    }
    else
    {
	if (area == NULL) area = &nullRect;
	sue->sue_def = def;
	sue->sue_area = *area;
	sue->sue_before = before;

	beforeEvent->sue_def = def;
	beforeEvent->sue_area = *area;
	beforeEvent = NULL;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * SelUndoForw --
 * SelUndoBack --
 *
 * 	Called to process undo redisplay events.  The two procedures
 *	are identical except that each one looks at different events.
 *	The idea is to do the selection redisplay only AFTER the selection
 *	has actually been modified.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Highlights (including the selection) are redisplayed.
 *
 * ----------------------------------------------------------------------------
 */

void
SelUndoForw(sue)
    SelUndoEvent *sue;		/* Event to be redone. */
{
    if (sue->sue_before) return;
    if (sue->sue_def == NULL) return;
    SelSetDisplay(SelectUse, sue->sue_def);
    SelectRootDef = sue->sue_def;
    DBReComputeBbox(SelectDef);
    if (sue->sue_area.r_xbot <= sue->sue_area.r_xtop)
	DBWHLRedraw(sue->sue_def, &sue->sue_area, TRUE);
    DBWAreaChanged(SelectDef, &sue->sue_area, DBW_ALLWINDOWS,
	(TileTypeBitMask *) NULL);
}

void
SelUndoBack(sue)
    SelUndoEvent *sue;		/* Event to be undone. */
{
    if (!sue->sue_before) return;
    if (sue->sue_def == NULL) return;
    SelSetDisplay(SelectUse, sue->sue_def);
    SelectRootDef = sue->sue_def;
    DBReComputeBbox(SelectDef);
    if (sue->sue_area.r_xbot <= sue->sue_area.r_xtop)
	DBWHLRedraw(sue->sue_def, &sue->sue_area, TRUE);
    DBWAreaChanged(SelectDef, &sue->sue_area, DBW_ALLWINDOWS,
	(TileTypeBitMask *) NULL);
}


/* BY NP */
/* The following methods are created for Net selection undo/redo-ing. */

/*
 * ----------------------------------------------------------------------------
 *
 * SelNetRememberForUndo--
 *
 * 	This routine is called once whenever a net selection is created.
 *	It is called once before the modifications (with "before" TRUE)
 *	and once afterward ("before" FALSE).  In the second case,
 *	arguments (def, startpoint, type, and less) are ignored.
 *
 *	This routine is called from within a routine (SelectNet) that
 *	is invoked by the undo mechanism.  To avoid corrupting the undo
 *	stack records, return immediately if selections have been disabled.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds information to the undo list.
 *
 * ----------------------------------------------------------------------------
 */
void
SelNetRememberForUndo(def, startpoint, type, less, before)
    CellDef *def;		/* Root definition on top of which selection
				 * information was just modified.
				 */
    Point *startpoint;		/* Starting point, from where we start
				 * selecting the network.
    				 */
    TileType type;		/* Tile type that uniquely identifies the
				 * network node at the above startpoint.
				 */
    bool less;			/* Value of "less" passed to SelectNet */
    bool before;		/* Does this mark the beginning or end
				 * of the net selection.
				 */
{
    static SelUndoNetEvent *beforeEvent = NULL;
    SelUndoNetEvent *sue;

    if (!UndoIsEnabled()) return;

    sue = (SelUndoNetEvent *) UndoNewEvent(SelUndoNetClientID,
		sizeof(SelUndoNetEvent));
    if (sue == NULL) return;

    if (before)
    {
	sue->sue_before = TRUE;
	sue->sue_def = def;
	sue->sue_startpoint = *startpoint;
	sue->sue_less = less;
	sue->sue_type = type;

	ASSERT(beforeEvent == NULL, "Forgot to call SelRememberForUndo after");
	beforeEvent = sue;
    }
    else
    {
	sue->sue_before = FALSE;
	ASSERT(beforeEvent != NULL, "Forgot to call SelRememberForUndo before");

	sue->sue_def = beforeEvent->sue_def;
	sue->sue_startpoint = beforeEvent->sue_startpoint;
	sue->sue_less = beforeEvent->sue_less;
	sue->sue_type = beforeEvent->sue_type;

	/* Don't set to NULL;  it is possible to run through	*/
	/* this code twice in a row if we "undo" back to, but	*/
	/* not over, the SelectNet record, and then start	*/
	/* again.  beforeEvent will still be pointing to the	*/
	/* correct event.					*/
	/* beforeEvent = NULL; */
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * SelUndoCreateNet --
 *
 * ----------------------------------------------------------------------------
 */

void
SelUndoCreateNet(sue)
    SelUndoNetEvent *sue;		/* Event description */
{
    SearchContext scx;
    DBWclientRec *crec;
    MagWindow *window;

    scx.scx_area.r_xbot = sue->sue_startpoint.p_x;
    scx.scx_area.r_ybot = sue->sue_startpoint.p_y;
    scx.scx_area.r_xtop = scx.scx_area.r_xbot + 1;
    scx.scx_area.r_ytop = scx.scx_area.r_ybot + 1;
	
    // window = CmdGetRootPoint((Point *) NULL, &scx.scx_area);

    window = CmdGetRootPoint((Point *) NULL, (Rect *) NULL);
    if (window == NULL) return;
    scx.scx_use = (CellUse *) window->w_surfaceID;
    scx.scx_trans = GeoIdentityTransform;
    crec = (DBWclientRec *) window->w_clientData;

    UndoDisable();
    SelectClear();	// Selection should already be clear at this point.
    SelectNet(&scx, sue->sue_type, crec->dbw_bitmask, (Rect *) NULL,
		sue->sue_less);
    UndoEnable();
}

/*
 * ----------------------------------------------------------------------------
 *
 * SelUndoDestroyNet --
 *
 * ----------------------------------------------------------------------------
 */
 
void
SelUndoDestroyNet()
{
    UndoDisable();
    SelectClear();
    UndoEnable();
}

/*
 * ----------------------------------------------------------------------------
 *
 * SelUndoNetForw --
 * SelUndoNetBack --
 *
 * 	Called to process redo(undo) redisplay events.  These routines
 *	are symmetric:  SelUndoNetForw creates the net on a "before"
 *	event and erases it on an "after" event.  SelUndoNetBack erases
 *	the net on a "before" event and creates it on an "after" event.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Highlights (including the selection) are redisplayed.
 *
 * ----------------------------------------------------------------------------
 */

void
SelUndoNetForw(sue)
    SelUndoNetEvent *sue;		/* Event to be redone. */
{
    if (sue->sue_def == NULL) return;

    if (sue->sue_before)
	SelUndoCreateNet(sue);
    else
	SelUndoDestroyNet();
}

void
SelUndoNetBack(sue)
    SelUndoNetEvent *sue;		/* Event to be redone. */
{
    if (sue->sue_def == NULL) return;

    if (sue->sue_before)
	SelUndoDestroyNet();
    else
	SelUndoCreateNet(sue);
}
