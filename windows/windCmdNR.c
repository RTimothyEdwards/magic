/* windCmdNR.c -
 *
 *	This file contains Magic command routines for those commands
 *	that are valid in all windows.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/windows/windCmdNR.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/times.h>

#include "utils/magic.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "windows/windows.h"
#include "graphics/glyphs.h"
#include "windows/windInt.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/main.h"
#include "utils/tech.h"
#include "utils/runstats.h"
#include "utils/undo.h"
#include "utils/utils.h"
#include "graphics/graphics.h"
#include "textio/txcommands.h"
#include "dbwind/dbwind.h"


/*
 * ----------------------------------------------------------------------------
 * windOpenCmd --
 *
 *	Open a new window at the cursor position.  Give it a default size,
 *	and default client, and pass the command line args off to the client.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

void
windOpenCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    Rect area;
    Point frame;
    WindClient wc;

    if ( w == (MagWindow *) NULL )
    {
	frame.p_x = (GrScreenRect.r_xtop - GrScreenRect.r_xbot) / 2;
	frame.p_y = (GrScreenRect.r_ytop - GrScreenRect.r_ybot) / 2;
    }
    else
	windScreenToFrame(w, &cmd->tx_p, &frame);

    area.r_xbot = frame.p_x - CREATE_WIDTH/2;
    area.r_xtop = frame.p_x + CREATE_WIDTH/2;
    area.r_ybot = frame.p_y - CREATE_HEIGHT/2;
    area.r_ytop = frame.p_y + CREATE_HEIGHT/2;

    wc = WindGetClient(DEFAULT_CLIENT, TRUE);
    ASSERT(wc != (WindClient) NULL, "windOpenCmd");

    if (WindCreate(wc, &area, TRUE, cmd->tx_argc - 1, cmd->tx_argv + 1) == 
	    (MagWindow *) NULL)
    {
	TxError("Could not create window\n");
    }
}


/*
 * ----------------------------------------------------------------------------
 * windOverCmd --
 *
 *	Move a window over (on top of) the other windows.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Screen updates.
 * ----------------------------------------------------------------------------
 */

void
windOverCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    if (cmd->tx_argc != 1)
    {
	TxError("Usage: %s\n", cmd->tx_argv[0]);
    }
    if (w == (MagWindow *) NULL)
    {
	TxError("Point to a window first\n");
	return;
    }
    WindOver(w);
}


/*
 * ----------------------------------------------------------------------------
 *
 * windPauseCmd --
 *
 *	Print out the arguments and wait for <cr> or a command.
 *
 * Usage:
 *	*pause [args]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	prints its args.
 *
 * ----------------------------------------------------------------------------
 */

void
windPauseCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int i;
    static char ssline[TX_MAX_CMDLEN];

    WindUpdate();	
    GrFlush();

    for (i = 1; i < cmd->tx_argc; i++)
    {
	TxPrintf(cmd->tx_argv[i]);
	TxPrintf(" ");
	if (i+1 == cmd->tx_argc) TxPrintf(" ");
    }
    
    TxPrintf("Pausing: type <cr> to continue: ");
    (void) TxGetLine(ssline, 98);
}


char *butTable[] =
{
    "left",
    "middle",
    "right",
    0
};

char *actTable[] =
{
    "down",
    "up",
    0
};

/*
 * ----------------------------------------------------------------------------
 * windPushbuttonCmd --
 *
 *	Pretend that a button was pushed
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

void
windPushbuttonCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int but, act;
    static TxCommand txcmd;

    if (cmd->tx_argc != 3) goto badusage;

    but = Lookup(cmd->tx_argv[1], butTable);
    if (but < 0) goto badusage;
    act = Lookup(cmd->tx_argv[2], actTable);
    if (act < 0) goto badusage;

    switch (but)
    {
	case 0:
	    txcmd.tx_button = TX_LEFT_BUTTON;
	    break;
	case 1:
	    txcmd.tx_button = TX_MIDDLE_BUTTON;
	    break;
	case 2:
	    txcmd.tx_button = TX_RIGHT_BUTTON;
	    break;
    }

    if (act == 0)
	txcmd.tx_buttonAction = TX_BUTTON_DOWN;
    else
	txcmd.tx_buttonAction = TX_BUTTON_UP;

    txcmd.tx_argc = 0;
    txcmd.tx_p = cmd->tx_p;
    txcmd.tx_wid = cmd->tx_wid;
    (void) WindSendCommand(w, &txcmd, FALSE);
    return;

badusage:
    TxError("Usage: %s button action\n", cmd->tx_argv[0]);
    return;
}


/*
 * ----------------------------------------------------------------------------
 * WindQuitCmd --
 *
 *	Leave the system, but first notify all clients.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	the system may exit.
 * ----------------------------------------------------------------------------
 */

void
windQuitCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    clientRec *cr;
    bool checkfirst = TRUE;

    if (cmd->tx_argc == 2)
	if (!strcmp(cmd->tx_argv[1], "-noprompt"))
	    checkfirst = FALSE;

    if (checkfirst)
	for (cr = windFirstClientRec; cr != (clientRec *) NULL; 
		cr = cr->w_nextClient)
	    if (cr->w_exit != NULL)
		if (!(*(cr->w_exit))())
		    return;

    MainExit(0);
}


/*
 * ----------------------------------------------------------------------------
 *
 * windRedoCmd
 *
 * Implement the "redo" command.
 *
 * Usage:
 *	redo [count]
 *
 * If a count is supplied, the last count events are redone.  The default
 * count if none is given is 1.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Calls the undo module.
 *
 * ----------------------------------------------------------------------------
 */

void
windRedoCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int count;

    if (cmd->tx_argc > 3)
    {
	TxError("Usage: redo [count]\n");
	return;
    }
    else if (cmd->tx_argc == 3)
    {
	if (strncmp(cmd->tx_argv[1], "print", 5))
	{
	    TxError("Usage: redo print count\n");
	    return;
	}
	else if (!StrIsInt(cmd->tx_argv[2]))
	{
	    TxError("Usage: redo print count\n");
	    return;
	}
	else
	{
	    /* Implement redo stack trace */
	    UndoStackTrace(atoi(cmd->tx_argv[2]));
	    return;
	}
    }
    else if (cmd->tx_argc == 2)
    {
	if (!StrIsInt(cmd->tx_argv[1]))
	{
	    TxError("Count must be numeric\n");
	    return;
	}
	count = atoi(cmd->tx_argv[1]);
	if (count < 0)
	{
	    TxError("Count must be a positive integer\n");
	    return;
	}
    }
    else
	count = 1;

    if (count == 0)
    {
	UndoDisable();
    }
    else
    {
	if (UndoForward(count) == 0)
	    TxPrintf("Nothing more to redo\n");
    }
}


/*
 * ----------------------------------------------------------------------------
 * windRedrawCmd --
 *
 *	Redraw the screen.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The screen is redrawn by calling the window package's clients.
 * ----------------------------------------------------------------------------
 */

void
windRedrawCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
   WindAreaChanged((MagWindow *) NULL, (Rect *) NULL);
}


/*
 * ----------------------------------------------------------------------------
 * windResetCmd --
 *
 *	Re-initialize the graphics device.
 *
 * Usage:
 *	reset
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The graphics file is closed and reopened, and the display is reset.
 * ----------------------------------------------------------------------------
 */

void
windResetCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    if (cmd->tx_argc != 1)
    {
	TxError("Usage: %s\n", cmd->tx_argv[0]);
	return;
    }

    if (WindPackageType != WIND_MAGIC_WINDOWS)
    {
	TxError("The :reset command doesn't make sense unless you are\nusing a serial-line graphics terminal.\n");
	return;
    }

    GrClose();

    /* open files */
    if (!GrSetDisplay(MainDisplayType, MainGraphicsFile, MainMouseFile))
    {
	TxError("Unable to set up graphics display.\n");
	return;
    }

    if (GrReadCMap(DBWStyleType, (char *)NULL, MainMonType, ".",
		SysLibPath) == 0) return;
    if (GrLoadStyles(DBWStyleType, ".", SysLibPath) != 0) return;

    DBWTechInitStyles();

    if (!GrLoadCursors(".", SysLibPath)) return;

    GrSetCursor(0);

    WindAreaChanged((MagWindow *) NULL, (Rect *) NULL);
}

