/* NMcmdLZ.c -
 *
 *	This file contains routines to interpret commands typed inside
 *	netlist windows.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/netmenu/NMcmdLZ.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "textio/txcommands.h"
#include "netmenu/nmInt.h"
#include "netmenu/netmenu.h"
#include "dbwind/dbwind.h"
#include "utils/styles.h"
#include "utils/main.h"
#include "textio/textio.h"


#ifdef ROUTE_MODULE

/*
 * ----------------------------------------------------------------------------
 *
 * NMCmdMeasure --
 *
 * 	Measure the metal, poly, and contacts on the selected net.  If "all"
 *	is specified, then measure for all nets.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Prints the routing statistics.
 *
 * ----------------------------------------------------------------------------
 */
	/* ARGSUSED */
void
NMCmdMeasure(w, cmd)
    MagWindow *w;			/* Netlist window. */
    TxCommand *cmd;		/* Contains the command's argc and argv. */
{
    FILE * fp, * fopen();

    if (cmd->tx_argc > 3)
    {
	TxError("Usage: measure [all [filename]]\n");
	return;
    }
    if (cmd->tx_argc == 1)
	NMMeasureNet();
    else
    if (NMNetlistName() == (char *) NULL)
    {
	TxError("First select a net list!\n");
	return;
    }
    else
    if (strcmp(cmd->tx_argv[1], "all") != 0)
    {
	TxError("Unknown option \"%s\"\n", cmd->tx_argv[1]);
	return;
    }
    else
    if (cmd->tx_argc == 2)
	NMMeasureAll((FILE *) NULL);
    else
    {
	if((fp = fopen(cmd->tx_argv[2], "w"))==NULL)
	{
	    TxError("Can't open %s\n", cmd->tx_argv[2]);
	    return;
	}
	TxPrintf("Log file is %s\n", cmd->tx_argv[2]);
	NMMeasureAll(fp);
	(void) fclose(fp);
    }
}

#endif


/*
 * ----------------------------------------------------------------------------
 *
 * NMCmdNetlist --
 *
 *	Select a particular netlist for working on.
 *
 * Usage:
 *	netlist [name]
 *
 *	(name defaults to the name of the edit cell)
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The netlist name.net is read from disk (if it hasn't already
 *	been loaded before) and is made the current netlist.
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
void
NMCmdNetlist(w, cmd)
    MagWindow *w;			/* Netlist window. */
    TxCommand *cmd;		/* Contains the command's argc and argv. */
{
    if (cmd->tx_argc > 2)
    {
	TxError("Usage: netlist [name]\n");
	return;
    }
    if (cmd->tx_argc < 2)
	NMNewNetlist(EditCellUse->cu_def->cd_name);
    else NMNewNetlist(cmd->tx_argv[1]);
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMCmdPrint --
 *
 *	Prints out the terminals in the current net.
 *
 * Usage:
 *	print
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stuff is printed.
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
void
NMCmdPrint(w, cmd)
    MagWindow *w;			/* Netlist window. */
    TxCommand *cmd;		/* Contains the command's argc and argv. */
{
    extern int nmCmdPrintFunc();
    int gotAny;
    char *name;

    if (cmd->tx_argc == 1)
    {
	if (NMCurNetName == NULL)
	{
	    TxError("Can't print current net:  there's nothing selected!\n");
	    return;
	}
	name = NMCurNetName;
    }
    else
    {
	name = cmd->tx_argv[1];
	if (cmd->tx_argc != 2)
	{
	    TxError("Usage: print [name]\n");
	    return;
	}
    }

    gotAny = FALSE;
    (void) NMEnumTerms(name, nmCmdPrintFunc, (ClientData) &gotAny);
    if (gotAny == FALSE)
	TxError("There's nothing in the current net!\n");
}

int
nmCmdPrintFunc(name, pGotAny)
    char *name;			/* Name of terminal. */
    int *pGotAny;		/* Pointer to integer, initially FALSE. */
{
    if (*pGotAny == FALSE)
    {
	TxPrintf("Nodes in net:\n");
	*pGotAny = TRUE;
    }
    TxPrintf("    %s\n", name);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMCmdPushButton --
 *
 *	Perform the default button action for the netlist window.
 *	This allows button actions to be bound to keys or buttons
 *	arbitrarily, rather than being hardwired into the source.
 *
 * Usage:
 *	pushbutton button
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Invokes the command associated with the button action.
 *
 * ----------------------------------------------------------------------------
 */

void
NMCmdPushButton(w, cmd)
    MagWindow *w;			/* Netlist window. */
    TxCommand *cmd;		/* Contains the command's argc and argv. */
{
    int button, action;
    static char *NMButton[] = {"left", "middle", "right", NULL};

    if (cmd->tx_argc != 2)
    {
	TxError("Usage: pushbutton <button>\n");
	return;
    }
    button = Lookup(cmd->tx_argv[1], NMButton);
    if (button < 0)
    {
	TxError("Argument \"button\" must be one of "
		"\"left\", \"middle\", or \"right\".\n");
	return;
    }
    else switch(button)
    {
	case 0:
	    cmd->tx_button = TX_LEFT_BUTTON;
	    break;
	case 1:
	    cmd->tx_button = TX_MIDDLE_BUTTON;
	    break;
	case 2:
	    cmd->tx_button = TX_RIGHT_BUTTON;
	    break;
    }

    cmd->tx_buttonAction = TX_BUTTON_DOWN;
    NMcommand(w, cmd);
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMRipup --
 *
 *	Ripup the wiring attached to paint under the cursor, or,
 *	if the argument "netlist" is supplied, ripup all the wiring
 *	in the current netlist.
 *
 * Usage:
 *	ripup [list]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Paint is erased from the edit cell.
 *
 * ----------------------------------------------------------------------------
 */

void
NMCmdRipup(w, cmd)
    MagWindow *w;			/* Netlist window. */
    TxCommand *cmd;		/* Contains the command's argc and argv. */
{
    if (cmd->tx_argc == 1)
	NMRipup();
    else if (cmd->tx_argc == 2)
    {
	if (strcmp(cmd->tx_argv[1], "netlist") == 0)
	    NMRipupList();
	else TxError("The only permissible argument to \"ripup\" is \"netlist\".\n");
    }
    else TxError("Usage: ripup [list]\n");
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMCmdSavenetlist --
 *
 *	Save the current netlist on disk, either to a particular
 *	file, or to its "home" (where it was read) if no file is
 *	given.
 *
 * Usage:
 *	savenetlist [file]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The current netlist is written to disk.
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
void
NMCmdSavenetlist(w, cmd)
    MagWindow *w;			/* Netlist window. */
    TxCommand *cmd;		/* Contains the command's argc and argv. */
{
    if ((cmd->tx_argc != 2) && (cmd->tx_argc != 1))
    {
	TxError("Usage: savenetlist [file]\n");
	return;
    }
    if (!NMHasList())
    {
	TxError("Select a netlist first.\n");
	return;
    }
    if (cmd->tx_argc == 1)
	NMWriteNetlist((char *) NULL);
    else NMWriteNetlist(cmd->tx_argv[1]);
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMCmdShownet --
 *
 *	Highlight all the paint connected to stuff under the box.
 *
 * Usage:
 *	shownet [erase]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Highlights are added to the screen.
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
void
NMCmdShownet(w, cmd)
    MagWindow *w;			/* Netlist window. */
    TxCommand *cmd;		/* Contains the command's argc and argv. */
{
    if (cmd->tx_argc != 1)
    {
	if (!strncmp(cmd->tx_argv[1], "erase", 5))
	    NMUnsetCell();
	else
	    TxError("Usage: shownet [erase]\n");
	return;
    }
    NMShowUnderBox();
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMCmdShowterms --
 *
 *	Use feedback to highlight all the terminals in the current
 *	netlist.
 *
 * Usage:
 *	showterms
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Feedback is added to the screen.
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
void
NMCmdShowterms(w, cmd)
    MagWindow *w;			/* Netlist window. */
    TxCommand *cmd;		/* Contains the command's argc and argv. */
{
    extern int nmShowtermsFunc1();	/* Forward reference. */

    if (cmd->tx_argc != 1)
    {
	TxError("Usage: showterms\n");
	return;
    }
    if (!NMHasList())
    {
	TxError("Select a netlist first.\n");
	return;
    }
    (void) NMEnumNets(nmShowtermsFunc1, (ClientData) NULL);
}

/* Search function for NMCmdShowterms.  This one is called for each
 * terminal name.  It just calls another search to find each terminal
 * instance.  Always return 0 to keep the search from aborting.
 */

	/* ARGSUSED */
int
nmShowtermsFunc1(name, firstInNet, cdarg)
    char *name;					/* Name of terminal. */
    bool firstInNet;				/* Not used. */
    ClientData cdarg;				/* Not used. */
{
    extern int nmShowtermsFunc2();		/* Forward reference. */

    (void) DBSrLabelLoc(EditCellUse, name, nmShowtermsFunc2,
	    (ClientData) NULL);
    return 0;
}

/* Another search function for NMCmdShowterms, called by DBSrLabelLoc.
 * This function just places feedback to identify the terminal location.
 */

	/* ARGSUSED */
int
nmShowtermsFunc2(rect, name, label, cdarg)
    Rect *rect;			/* Area of terminal's label (edit coords). */
    char *name;			/* Not used. */
    Label *label;		/* Not used. */
    ClientData cdarg;		/* Not used. */
{
    Rect expanded;
    GEO_EXPAND(rect, 1, &expanded);
    DBWFeedbackAdd(&expanded, "\"Showterms\" result", EditCellUse->cu_def,
	    1, STYLE_PALEHIGHLIGHTS);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMCmdTrace --
 *
 *	Trace out where a net goes in the routing that has been
 *	done by Magic.
 *
 * Usage:
 *	trace
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Feedback is added to the screen.
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
void
NMCmdTrace(w, cmd)
    MagWindow *w;			/* Netlist window. */
    TxCommand *cmd;		/* Contains the command's argc and argv. */
{
    if ((cmd->tx_argc != 1) && (cmd->tx_argc != 2))
    {
	TxError("Usage: trace [name]\n");
	return;
    }
    if (cmd->tx_argc == 1)
	NMShowRoutedNet((char *) NULL);
    else NMShowRoutedNet(cmd->tx_argv[1]);
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMCmdVerify --
 *
 *	Check the current netlist against routing in the edit cell
 *	to ensure that all the nets are implemented exactly as
 *	specified by the netlist.
 *
 * Usage:
 *	verify
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Feedback is created where there are problems with the routing.
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
void
NMCmdVerify(w, cmd)
    MagWindow *w;			/* Netlist window. */
    TxCommand *cmd;		/* Contains the command's argc and argv. */
{
    if (cmd->tx_argc != 1)
    {
	TxError("Usage: verify\n");
	return;
    }
    if (!NMHasList())
    {
	TxError("Select a netlist first.\n");
	return;
    }
    NMVerify();
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMCmdWriteall--
 *
 *	Scan through all the netlists that are loaded, find any that
 *	have been modified, and give the user a chance to write them
 *	out.
 *
 * Usage:
 *	writeall
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Netlists may be written to disk.
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
void
NMCmdWriteall(w, cmd)
    MagWindow *w;			/* Netlist window. */
    TxCommand *cmd;		/* Contains the command's argc and argv. */
{
    if (cmd->tx_argc != 1)
    {
	TxError("Usage: writeall\n");
	return;
    }
    NMWriteAll();
}
