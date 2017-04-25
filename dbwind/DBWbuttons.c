/*
 * DBWbuttons.c --
 *
 * This file provides a general facility whereby clients that are
 * willing to provide handlers for button presses in layout windows
 * can themselves, and the current handler can be switched
 * between them.  This file also provides the default button handler,
 * which is used to move the box.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/dbwind/DBWbuttons.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "dbwind/dbwind.h"
#include "utils/styles.h"
#include "textio/textio.h"
#include "textio/txcommands.h"
#include "utils/utils.h"

/* The arrays below are used to store information about the various
 * button handlers that have registered themselves.
 */

#define MAXBUTTONHANDLERS 10

static char *dbwButtonHandlers[MAXBUTTONHANDLERS];
			/* Name of each button handler:  used to select
			 * that handler as the current one.  A NULL entry
			 * here means that this handler slot isn't in use.
			 */
static char *dbwButtonDoc[MAXBUTTONHANDLERS];
			/* A documentation string for each handler:  tells
			 * what the button pushes and releases mean.
			 */
static void (*dbwButtonProcs[MAXBUTTONHANDLERS])();
			/* A procedure for each handler that is invoked
			 * on button presses and releases when that handler
			 * is the current one.
			 */
static int dbwButtonCursors[MAXBUTTONHANDLERS];
			/* Cursor shape to use for each handler. */

static int dbwButtonCurrentIndex;
			/* Index of current handler. */
void (*DBWButtonCurrentProc)();
			/* Current button-handling procedure. */

static int buttonCorner = TOOL_ILG;	/* Nearest corner when button went
					 * down.
					 */

/*
 * ----------------------------------------------------------------------------
 *
 * DBWAddButtonHandler --
 *
 * 	This procedure is called by would-be button handlers to register
 *	themselves.  After a client has called this procedure, it may
 *	make itself the current button handler by calling the procedure
 *	DBWChangeButtonHandler.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The client's information is added to the registry of potential
 *	button handlers.  When the handler is made the current one (by
 *	a call to DBWChangeButtonHandler) each button press or release
 *	in a layout window causes proc to be invoked as follows:
 *
 *	int	
 *	proc(w, cmd)
 *	    MagWindow *w;
 *	    TxCommand *cmd;
 *	{
 *	}
 *
 *	W is the window in which the button was pushed, and cmd describes
 *	exactly what happened.
 *
 * ----------------------------------------------------------------------------
 */

void
DBWAddButtonHandler(name, proc, cursor, doc)
    char *name;			/* Name of this button handler.  This name
				 * is what's passed to DBWChangeButtonHandler
				 * to activate the handler.
				 */
    void (*proc)();		/* Procedure to call on button actions when
				 * this handler is active.
				 */
    int cursor;			/* Cursor shape (e.g. STYLE_CURS_NORMAL) to
				 * use when this handler is active.
				 */
    char *doc;			/* A documentation string for this handler:
				 * describes what the button pushes do when
				 * this handler is active.
				 */
{
    int i;

    for (i = 0; i < MAXBUTTONHANDLERS; i++)
    {
	if (dbwButtonHandlers[i] != NULL) continue;
	(void) StrDup(&dbwButtonHandlers[i], name);
	(void) StrDup(&dbwButtonDoc[i], doc);
	dbwButtonProcs[i] = proc;
	dbwButtonCursors[i] = cursor;
	return;
    }

    TxError("Can't add tool \"%s\":  no space in button handler\n",
	    name);
    TxError("    table.  Get your Magic wizard to increase the size of\n");
    TxError("    MAXBUTTONHANDLERS in DBWbuttons.c\n");
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBWChangeButtonHandler --
 *
 * 	Change the active button handler.
 *
 * Results:
 *	The return value is the name of the previous button handler, in
 *	case the caller should wish to restore it.
 *
 * Side effects:
 *	If name is NULL, then the "next" button handler is activated, in a
 *	circular fashion.  If name isn't NULL, then it is the name of a
 *	handler, which is activated.  If the name doesn't match a handler
 *	then a message is printed and the handler isn't changed.
 *
 * ----------------------------------------------------------------------------
 */

char *
DBWChangeButtonHandler(name)
    char *name;			/* Name of new handler.  Must be a unique
				 * abbreviation of a name passed previously
				 * to DBAddButtonHandler, or NULL.
				 */
{
    char *oldName = dbwButtonHandlers[dbwButtonCurrentIndex];
    static int firstTime = TRUE;

    if (name == NULL)
    {
	/* Just rotate to the next available client. */

	while (TRUE)
	{
	    dbwButtonCurrentIndex += 1;
	    if (dbwButtonCurrentIndex >= MAXBUTTONHANDLERS)
		dbwButtonCurrentIndex = 0;
	    if (dbwButtonHandlers[dbwButtonCurrentIndex] == NULL)
		continue;
	    if (firstTime)
	    {
		firstTime = FALSE;
		TxPrintf("Switching to \"%s\" tool.",
		    dbwButtonHandlers[dbwButtonCurrentIndex]);
		TxPrintf("  If you didn't really want to switch,\n");
		TxPrintf("    type \":tool box\" to");
		TxPrintf(" switch back to the box tool.\n");
	    }
	    else
	    {
		TxPrintf("Switching to \"%s\" tool.\n",
			dbwButtonHandlers[dbwButtonCurrentIndex]);
	    }
	    break;
	}
    }
    else
    {
	int i, match, length;

	match = -1;
	length = strlen(name);
	for (i = 0; i < MAXBUTTONHANDLERS; i++)
	{
	    if (dbwButtonHandlers[i] == NULL) continue;
	    if (strncmp(name, dbwButtonHandlers[i], length) != 0) continue;
	    if (match >= 0)
	    {
		TxError("\"%s\" is an ambiguous tool name.", name);
		match = -2;
		break;
	    }
	    match = i;
	}

	if (match == -1)
	    TxError("\"%s\" isn't a tool name.", name);
	if (match < 0)
	{
	    TxError("  The legal names are:\n");
	    for (i = 0; i < MAXBUTTONHANDLERS; i++)
	    {
		if (dbwButtonHandlers[i] == NULL) continue;
		TxError("    %s\n", dbwButtonHandlers[i]);
	    }
	    return oldName;
	}
	dbwButtonCurrentIndex = match;
    }

    GrSetCursor(dbwButtonCursors[dbwButtonCurrentIndex]);
    DBWButtonCurrentProc = dbwButtonProcs[dbwButtonCurrentIndex];
    return oldName;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBWPrintButtonDoc --
 *
 * 	This procedure prints out documentation for the current
 *	button handler.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stuff gets printed on the tty, ostensibly describing what
 *	the current buttons do.
 *
 * ----------------------------------------------------------------------------
 */

void
DBWPrintButtonDoc()
{
    TxPrintf("%s", dbwButtonDoc[dbwButtonCurrentIndex]);
}


/*
 * ----------------------------------------------------------------------------
 *	dbwButtonSetCursor --
 *
 * 	Used to set the programmable cursor for a particular
 *	button state.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Selects and sets a programmable cursor based on the given
 *	button (for sizing or moving) and corner.
 * ----------------------------------------------------------------------------
 */

void
dbwButtonSetCursor(button, corner)
    int button;			/* Button that is down. */
    int corner;			/* Corner to be displayed in cursor. */

{
    switch (corner)
    {
	case TOOL_BL:
	    if (button == TX_LEFT_BUTTON)
		GrSetCursor(STYLE_CURS_LLBOX);
	    else
		GrSetCursor(STYLE_CURS_LLCORNER);
	    break;
	case TOOL_BR:
	    if (button == TX_LEFT_BUTTON)
		GrSetCursor(STYLE_CURS_LRBOX);
	    else
		GrSetCursor(STYLE_CURS_LRCORNER);
	    break;
	case TOOL_TL:
	    if (button == TX_LEFT_BUTTON)
		GrSetCursor(STYLE_CURS_ULBOX);
	    else
		GrSetCursor(STYLE_CURS_ULCORNER);
	    break;
	case TOOL_TR:
	    if (button == TX_LEFT_BUTTON)
		GrSetCursor(STYLE_CURS_URBOX);
	    else
		GrSetCursor(STYLE_CURS_URCORNER);
	    break;
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * DBWBoxHandler --
 *
 * 	This procedure is called to handle button actions in layout
 *	windows when the "box" handler is active.  It adjusts the box
 *	position and size.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Left button:  used to move the whole box by the lower-left corner.
 *	Right button: used to re-size the box by its upper-right corner.
 *		If one of the left or right buttons is pushed, then the
 *		other is pushed, the corner is switched to the nearest
 *		one to the cursor.  This corner is remembered for use
 *		in box positioning/sizing when both buttons have gone up.
 *	Middle button: used to paint whatever layers are underneath the
 *		crosshair.
 *
 * ----------------------------------------------------------------------------
 */

void
DBWBoxHandler(w, cmd)
    MagWindow *w;			/* Window containing cursor. */
    TxCommand *cmd;		/* Describes what happened. */
{
    int button = cmd->tx_button;

    if (button == TX_MIDDLE_BUTTON)
    {
	if (cmd->tx_buttonAction == TX_BUTTON_DOWN)
	    CmdPaintEraseButton(w, &cmd->tx_p, TRUE);
	return;
    }

    if (cmd->tx_buttonAction == TX_BUTTON_DOWN)
    {
	if ((WindNewButtons & (TX_LEFT_BUTTON|TX_RIGHT_BUTTON))
		== (TX_LEFT_BUTTON|TX_RIGHT_BUTTON))
	{
	    /* Both buttons are now down.  In this case, the FIRST
	     * button pressed determines whether we move or size,
	     * and the second button is just used as a signal to pick
	     * the closest corner.
	     */

	    buttonCorner = ToolGetCorner(&cmd->tx_p);
	    if (button == TX_LEFT_BUTTON) button = TX_RIGHT_BUTTON;
	    else button = TX_LEFT_BUTTON;
	}
	else if (button == TX_LEFT_BUTTON) buttonCorner = TOOL_BL;
	else buttonCorner = TOOL_TR;
	dbwButtonSetCursor(button, buttonCorner);
    }
    else
    {
	/* A button has just come up.  If both buttons are down and one
	 * is released, we just change the cursor to reflect the current
	 * corner and the remaining button (i.e. move or size box).
	 */

	if (WindNewButtons != 0)
	{
	    if (button == TX_LEFT_BUTTON)
		dbwButtonSetCursor(TX_RIGHT_BUTTON, buttonCorner);
	    else dbwButtonSetCursor(TX_LEFT_BUTTON, buttonCorner);
	    return;
	}

	/* The last button has been released.  Reset the cursor to normal
	 * form and then move or size the box.
	 */

	GrSetCursor(STYLE_CURS_NORMAL);
	switch (button)
	{
	    case TX_LEFT_BUTTON:
		ToolMoveBox(buttonCorner, &cmd->tx_p, TRUE, (CellDef *) NULL);
		break;
	    case TX_RIGHT_BUTTON:
		ToolMoveCorner(buttonCorner, &cmd->tx_p, TRUE,
			(CellDef *) NULL);
	}
    }
}
