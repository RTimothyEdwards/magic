/* windSend.c -
 *
 *	Send button pushes and commands to the window's command
 *	interpreters.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/windows/windSend.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "windows/windows.h"
#include "graphics/glyphs.h"
#include "windows/windInt.h"
#include "utils/stack.h"
#include "utils/utils.h"
#include "utils/signals.h"
#include "textio/txcommands.h"

clientRec *windClient = NULL;

bool windPrintCommands = FALSE;	/* debugging flag */

global TxCommand *WindCurrentCmd; /* The current command.
				   */
global MagWindow *WindCurrentWindow; /* The window at which the current command
				   * was invoked.
				   */

global int WindOldButtons;	/* The buttons for the last command */
global int WindNewButtons;	/* The buttons this time */

static WindClient windGrabber =  (WindClient) NULL;
				/* If this variable is non-null then send
				 * all commands to it
				 */

Stack *windGrabberStack = NULL;

/* Forward declarations */

extern void WindGrabInput();
extern void WindReleaseInput();
extern void windHelp();


/*
 * ----------------------------------------------------------------------------
 * WindSendCommand --
 *
 *	Send a command to a window to be executed.  If the window passed is
 *	NULL then whatever window is at the point given in the command is
 *	used.
 *
 * Results:
 *	 0 if the command was able to be processed.
 *	-1 on an ambiguous command error.
 *	-2 on an unknown command error.
 *	-3 on other error.
 *
 * Side effects:
 *	Whatever the window wishes to do with the command.
 * ----------------------------------------------------------------------------
 */

int
WindSendCommand(w, cmd, quiet)
    MagWindow *w;
    TxCommand *cmd;	/* A pointer to a command */
    bool quiet;		/* Don't print error/warning messages if this is set */
{
    int windCmdNum, clientCmdNum;
    clientRec *rc;
    bool inside;	/* tells us if we are inside of a window */

    /* This thing is a horrendous mess.  Changing WindClient to	*/
    /* MagWindow in the arguments list is a big help, but the	*/
    /* following code should be simplified.  Namely, w == 0	*/
    /* only when the command comes from the command line.	*/
    /* Tcl/Tk commands and graphics are set up to supply a	*/
    /* valid w.  Under normal conditions, we should not need to	*/
    /* guess what the client is.				*/

    /* The big problem here is that all windows are expected	*/
    /* to take windClient commands---but that prevents setting	*/
    /* up windows which don't.  This should be handled better,	*/
    /* probably at the low level of the MagWindow structure	*/
    /* itself.							*/

    if (windClient == (clientRec *) NULL)
	windClient = (clientRec *) WindGetClient(WINDOW_CLIENT, TRUE);

    /* ignore no-op commands */
    if ( (cmd->tx_button == TX_NO_BUTTON) && (cmd->tx_argc == 0) )
    {
	return 0;
    }

    inside = FALSE;
    ASSERT( (cmd->tx_button == TX_NO_BUTTON) || (cmd->tx_argc == 0), 
	"WindSendCommand");

    WindOldButtons = WindNewButtons;
    if (cmd->tx_button == TX_NO_BUTTON)
    {
	if (windClient == (clientRec *)NULL) return -2;

	/* If window commands are disallowed by the client (set by */
	/* the client's WIND_COMMANDS flag), report no command.	   */

	if ((w != NULL) && !(w->w_flags & WIND_COMMANDS))
	    windCmdNum = -2;
	else
	    windCmdNum = Lookup(cmd->tx_argv[0], windClient->w_commandTable);
    }
    else
    {
	if (cmd->tx_buttonAction == TX_BUTTON_DOWN)
	    WindNewButtons |= cmd->tx_button;
	else
	    WindNewButtons &= ~(cmd->tx_button);
    }

    /* If we were passed a NULL MagWindow pointer, try to determine the */
    /* window from the command's window ID number.			*/

    if (w == (MagWindow *)NULL)
    {
	if (cmd->tx_wid == WIND_UNKNOWN_WINDOW)
	{
	    w = windSearchPoint( &(cmd->tx_p), &inside);
	    if (w != NULL)
		cmd->tx_wid = w->w_wid;
	}
	else if (cmd->tx_wid >= 0)
	    w = WindSearchWid(cmd->tx_wid);
    }

    if (w != (MagWindow *) NULL) 
    {
	inside = GEO_ENCLOSE(&cmd->tx_p, &w->w_screenArea);
	if ((!inside) && (w->w_flags & WIND_COMMANDS))
	    rc = windClient;	/* Handles border regions */
	else
	    rc = (clientRec *) w->w_client;
    }
    else
	/* Can't determine a window---assume a windowless layout client */
	rc = (clientRec *) WindGetClient(DEFAULT_CLIENT, TRUE);

    if (windGrabber != (WindClient) NULL)
    {
	/* this client wants to hog all commands */
	rc = (clientRec *) windGrabber;
    }

    /* At this point, the command is all set up and ready to send to
     * the client.
     */
    ASSERT(rc != (clientRec *) NULL, "WindSendCommand");
    if (windPrintCommands)
    {
	TxPrintf("Sending command:\n");
	windPrintCommand(cmd);
    }
    WindCurrentCmd = cmd;
    WindCurrentWindow = w;

    if (cmd->tx_button == TX_NO_BUTTON) 
    {
	clientCmdNum = Lookup(cmd->tx_argv[0], rc->w_commandTable);

	if ((clientCmdNum == -1) || (windCmdNum == -1))
	{
	    if (quiet == FALSE)
		TxError("That command abbreviation is ambiguous.\n");
	    return -1;
	}
	if ((windCmdNum == -2) && (clientCmdNum == -2))
	{
	    /* Not a valid command.  Help the user out by telling him
	     * what might be wrong. And also print out the command!
	     */
	    if (quiet == FALSE)
	    {
		TxError("Unknown command:");
		windPrintCommand(cmd);
		if (WindNewButtons != 0) 
		{
		    char *bname = "unknown";
		    if (WindNewButtons & TX_LEFT_BUTTON) bname = "left";
		    else if (WindNewButtons & TX_RIGHT_BUTTON) bname = "right";
		    else if (WindNewButtons & TX_MIDDLE_BUTTON) bname = "middle";
		 
		    TxError( "'%s' window is waiting for %s button to be released.\n",
		    		rc->w_clientName, bname);
		}
		return -3;
	    }
	    else if (windGrabber != (WindClient) NULL)
	    {
		if (quiet == FALSE)
		    TxError( "'%s' window is grabbing all input.\n", rc->w_clientName);
		return -3;
	    }

	    if (quiet == FALSE)
		TxError("Did you point to the correct window?\n");
	    return -2;
	}

	/* intercept 'help' */
	if ((windCmdNum >= 0) &&  
		(strncmp(windClient->w_commandTable[windCmdNum], 
		"help", 4) == 0) )
	{
	    TxUseMore();
	    windHelp(cmd, "Global", windClient->w_commandTable);
	    if (rc != windClient)
		windHelp(cmd, rc->w_clientName, rc->w_commandTable);
	    TxStopMore();
	    return 0;
	}

	/* If both command tables point to window commands,	*/
	/* only do the command once.				*/

	if (rc == windClient) clientCmdNum = -2;

	/* Ambiguity resolution.  If only one client reports a	*/
	/* valid command, then execute it.  If both clients	*/
	/* report a valid command, then compare them against	*/
	/* each other so that a full command name will take	*/
	/* precedence over an ambiguous command abbreviation.	*/
	/* Finally, if this doesn't resolve the command, the	*/
	/* registered client takes precedence over the general-	*/
	/* purpose window client, allowing clients to override	*/
	/* the general-purpose functions (like "zoom" and	*/
	/* "view", for instance).  This is the reverse of how	*/
	/* it was implemented prior to magic-7.3.61, but that	*/
	/* makes no sense.					*/

	if ((windCmdNum < 0) && (clientCmdNum >= 0))
	    (*(rc->w_command))(w, cmd);
	else if ((windCmdNum >= 0) && (clientCmdNum < 0))
	    (*(windClient->w_command))(w, cmd);
	else if ((windCmdNum >= 0) && (clientCmdNum >= 0))
	{
	    char *(ownTable[3]);
	    int ownCmdNum;

	    ownTable[0] = rc->w_commandTable[clientCmdNum];
	    ownTable[1] = windClient->w_commandTable[windCmdNum];
	    ownTable[2] = NULL;
	    ownCmdNum = Lookup(cmd->tx_argv[0], ownTable);
	    ASSERT(ownCmdNum != -2, "WindSendCommand");
	    if (ownCmdNum == -1)
	    {
		if (quiet == FALSE)
		    TxError("That command abbreviation is ambiguous\n");
		return -1;
	    }
	    if (ownCmdNum == 0)
		(*(rc->w_command))(w, cmd);
	    else
		(*(windClient->w_command))(w, cmd);
	}
    }
    else
    {
	/* A button has been pushed.
	 * If there were no buttons pressed on the last command
	 * now there are, and direct all future button pushes to this
	 * client until all buttons are up again.
	 */

	/* windClient is responsible for processing actions in the	*/
	/* window border, assuming that the window handles its own	*/
	/* borders.  Scrollbars are only available on the layout	*/
	/* window, which is the "default" client.  So we check for a	*/
	/* position in the border region, and launch the window client	*/
	/* if it is.							*/

        if (WindOldButtons == 0) WindGrabInput((WindClient) rc);
	else if (WindNewButtons == 0) WindReleaseInput((WindClient) rc);
	(*(rc->w_command))(w, cmd);
    }

    /* A client may modify WindNewButtons & WindOldButtons in rare cases,
     * so we better check again.
     */
    if ((WindNewButtons == 0) && (windGrabber != (WindClient) NULL))
	WindReleaseInput((WindClient) rc);

    return 0;
}




/*
 * ----------------------------------------------------------------------------
 * WindGrabInput --
 *
 *	Grab all input -- that is, send all further commands to the
 *	specified client.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	pushes old grabber onto a stack.
 * ----------------------------------------------------------------------------
 */

void
WindGrabInput(client)
    WindClient client;
{
    ASSERT( client != NULL, "WindGrabInput");
    StackPush( (ClientData) windGrabber, windGrabberStack);
    windGrabber = client;
}



/*
 * ----------------------------------------------------------------------------
 * WindReleaseInput --
 *
 *	Stop grabbing the input (the inverse of WindGrabInput).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The previous grabber (if any) is restored.
 * ----------------------------------------------------------------------------
 */

void
WindReleaseInput(client)
    WindClient client;
{
      ASSERT( client == windGrabber, "WindReleaseInput");
      windGrabber = (WindClient) StackPop(windGrabberStack);
}


/*
 * ----------------------------------------------------------------------------
 * windHelp --
 *
 *	Print out help information for a client.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

void
windHelp(cmd, name, table)
    TxCommand *cmd;		/* Information about command options. */
    char *name;			/* Name of client for whom help is being
				 * printed.
				 */
    char *table[];		/* Client's command table. */
{
    static char *capName = NULL;
    static char patString[200], *pattern;
    bool wiz;
    char **tp;
#define WIZARD_CHAR	'*'

    if (cmd->tx_argc > 2)
    {
	TxError("Usage:  help [pattern]\n");
	return;
    }

    if (SigInterruptPending) return;
    (void) StrDup(&capName, name);
    if (islower(capName[0])) capName[0] += 'A' - 'a';

    TxPrintf("\n");
    if ((cmd->tx_argc == 2) && strcmp(cmd->tx_argv[1], "wizard") == 0)
    {
	pattern = "*";
	wiz = TRUE;
	TxPrintf("Wizard %s Commands\n", capName);
	TxPrintf("----------------------\n");
    }
    else 
    {
	if (cmd->tx_argc == 2)
	{
	    pattern = patString;
	    (void) sprintf(patString, "*%.195s*", cmd->tx_argv[1]);
	}
	else
	    pattern = "*";
	wiz = FALSE;
	TxPrintf("%s Commands\n", capName);
	TxPrintf("---------------\n");
    }
    for (tp = table; *tp != (char *) NULL; tp++)
    {
	if (SigInterruptPending) return;
	if (Match(pattern, *tp) && (wiz ^ (**tp != WIZARD_CHAR)) )
	    TxPrintf("%s\n", *tp);
    }
}
