/* NMcmd.c -
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/netmenu/NMcmdAK.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "textio/txcommands.h"
#include "netmenu/nmInt.h"
#include "netmenu/netmenu.h"
#include "utils/main.h"
#include "textio/textio.h"
#include "utils/malloc.h"

/*
 * ----------------------------------------------------------------------------
 *
 * NMCmdAdd --
 *
 *	Add a terminal to another terminal's net, removing it from
 *	its old net (if it was in one).
 *
 * Usage:
 *	add term1 term2
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The current netlist is modified.
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
void
NMCmdAdd(w, cmd)
    MagWindow *w;			/* Netlist window. */
    TxCommand *cmd;		/* Contains the command's argc and argv. */
{
    if (cmd->tx_argc != 3)
    {
	TxError("Usage: add term1 term2\n");
	return;
    }
    if (!NMHasList())
    {
	TxError("Select a netlist first.\n");
	return;
    }
    (void) NMAddTerm(cmd->tx_argv[1], cmd->tx_argv[2]);
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMCmdCleanup --
 *
 *	Check the current netlist for terminals that aren't present in
 *	the design and for nets with only one terminal.  When found,
 *	tell the user and try to clean up automatically wherever
 *	possible.
 *
 * Usage:
 *	cleanup
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The current netlist is modified.
 *
 * ----------------------------------------------------------------------------
 */

/* The following definitions and variables are shared between the
 * procedures that implement the cleanup command.
 */

struct nmcleanup
{
    char *nmcl_name;		/* Name of terminal. */
    int nmcl_problem;		/* Problem with this net:  see defs. below. */
    struct nmcleanup *nmcl_next;/* Troubles are linked together into list. */
};

#define NMCL_ONETERM 1
#define NMCL_NOLABEL 2

static struct nmcleanup *nmCleanupList;
				/* List of problem terminals, formulated
				 * during first pass.  These are processed
				 * during the second pass.
				 */
static int nmCleanupCount;	/* How many terminals have been seen in the
				 * current net.
				 */
static char *nmCleanupTerm;	/* Current (or previous) terminal in list. */

extern int nmCleanupFunc1(), nmCleanupFunc2();
extern void nmCleanupNet();

	/* ARGSUSED */
void
NMCmdCleanup(w, cmd)
    MagWindow *w;			/* Netlist window. */
    TxCommand *cmd;		/* Contains the command's argc and argv. */
{
    extern int nmCleanupFunc1();	/* Forward reference. */
    struct nmcleanup *p;

    if (cmd->tx_argc != 1)
    {
	TxError("Usage: cleanup\n");
	return;
    }
    if (!NMHasList())
    {
	TxError("Select a netlist first.\n");
	return;
    }

    /* Pass 1: collect information about problem areas. */

    nmCleanupList = NULL;
    nmCleanupCount = 0;
    nmCleanupTerm = NULL;
    (void) NMEnumNets(nmCleanupFunc1, (ClientData) NULL);
    nmCleanupNet();

    /* Pass 2: go over the list and ask the user what to do.  This
     * needs to be done in two passes because it may modify the
     * netlist, and that is a dangerous thing to do while a search
     * is in progress.
     */
    
    for (p = nmCleanupList; p != NULL; p = p->nmcl_next)
    {
	char answer[30];
	int indx, defaultValue;
	static char *options[] = {"abort", "dnet", "dterm", "skip", NULL};

	if (p->nmcl_problem == NMCL_ONETERM)
	{
	    TxPrintf("Net \"%s\" has less than two terminals.\n", p->nmcl_name);
	    defaultValue = 1;
	}
	else
	{
	    TxPrintf("\"%s\" doesn't exist in the circuit.\n", p->nmcl_name);
	    defaultValue = 2;
	}
	do
	{
	    TxPrintf("Delete terminal (dterm), delete net (dnet), ");
	    TxPrintf("skip, or abort command? [%s] ", options[defaultValue]);
	    if (TxGetLine(answer, sizeof answer) == NULL) continue;
	    if (answer[0] == 0) indx = defaultValue;
	    else indx = Lookup(answer, options);
	} while (indx < 0);
        switch (indx)
	{
	    case 0:
		while (p != NULL)
		{
		    freeMagic((char *) p);
		    p = p->nmcl_next;
		}
		return;
	    case 1:
		NMDeleteNet(p->nmcl_name);
		break;
	    case 2:
		NMDeleteTerm(p->nmcl_name);
		break;
	}
	freeMagic((char *) p);
    }

    if (nmCleanupList == NULL)
	TxPrintf("No problems found.\n");
}

/* Search function for NMCmdCleanup.  This one is called for each
 * terminal name.  Always return 0 to keep the search from aborting.
 */

	/* ARGSUSED */
int
nmCleanupFunc1(name, firstInNet, cdarg)
    char *name;			/* Name of terminal. */
    bool firstInNet;		/* TRUE means first terminal of new net. */
    ClientData cdarg;		/* Not used. */
{
    int count;
    struct nmcleanup *p;

    if (firstInNet)
    {
	nmCleanupNet();
	nmCleanupCount = 0;
    }
    count = 0;
    nmCleanupTerm = name;
    (void) DBSrLabelLoc(EditCellUse, name, nmCleanupFunc2,
	    (ClientData) &count);
    if (count == 0)
    {
	p = (struct nmcleanup *) mallocMagic(sizeof(struct nmcleanup));
	p->nmcl_name = name;
	p->nmcl_problem = NMCL_NOLABEL;
	p->nmcl_next = nmCleanupList;
	nmCleanupList = p;
    }
    else nmCleanupCount += count;
    return 0;
}

/* Another search function for NMCleanup, called by DBSrLabelLoc.
 * This function just counts the terminal instances for a net.
 */

	/* ARGSUSED */
int
nmCleanupFunc2(rect, name, label, pCount)
    Rect *rect;			/* Not used. */
    char *name;			/* Not used. */
    Label *label;		/* Not used. */
    int *pCount;		/* Pointer to word to be incremented. */
{
    *pCount += 1;
    return 0;
}

/* Called after each net has been seen, to make sure there were at
 * least two terminal instances for the net.
 */

void
nmCleanupNet()
{
    struct nmcleanup *p;

    if ((nmCleanupTerm != NULL) && (nmCleanupCount < 2))
    {
	p = (struct nmcleanup *) mallocMagic(sizeof(struct nmcleanup));
	p->nmcl_name = nmCleanupTerm;
	p->nmcl_problem = NMCL_ONETERM;
	p->nmcl_next = nmCleanupList;
	nmCleanupList = p;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMCmdCull --
 *
 *	Check the current netlist against routing in the edit cell
 *	to remove nets that are already wired correctly.
 *
 * Usage:
 *	cull
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
NMCmdCull(w, cmd)
    MagWindow *w;			/* Netlist window. */
    TxCommand *cmd;		/* Contains the command's argc and argv. */
{
    if (cmd->tx_argc != 1)
    {
	TxError("Usage: cull\n");
	return;
    }
    if (!NMHasList())
    {
	TxError("Select a netlist first.\n");
	return;
    }
    NMCull();
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMCmdDnet --
 *
 *	Delete the net containing a particular named terminal.
 *
 * Usage:
 *	dnet name name ...
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The current netlist is modified.
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
void
NMCmdDnet(w, cmd)
    MagWindow *w;			/* Netlist window. */
    TxCommand *cmd;		/* Contains the command's argc and argv. */
{
    int i;

    if (!NMHasList())
    {
	TxError("Select a netlist first.\n");
	return;
    }
    if (cmd->tx_argc < 2)
    {
	if (NMCurNetName != NULL)
	{
	    char *name;
	    name = NMCurNetName;
	    NMSelectNet((char *) NULL);
	    NMDeleteNet(name);
	}
	return;
    }
    for (i = 1; i < cmd->tx_argc; i++)
    {
	if (NMTermInList(cmd->tx_argv[i]) != NULL)
	    NMDeleteNet(cmd->tx_argv[i]);
	else
	{
	    TxError("\"%s\" isn't in the current netlist.", cmd->tx_argv[i]);
	    TxError("  Do you have the right netlist?.\n");
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMCmdDterm --
 *
 *	Delete a particular terminal from its net.
 *
 * Usage:
 *	dterm name name ...
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The given terminal is removed from its net (if it's in a net).
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
void
NMCmdDterm(w, cmd)
    MagWindow *w;			/* Netlist window. */
    TxCommand *cmd;		/* Contains the command's argc and argv. */
{
    int i;
    if (cmd->tx_argc < 2)
    {
	TxError("Usage: dterm name name ...\n");
	return;
    }
    if (!NMHasList())
    {
	TxError("Select a netlist first.\n");
	return;
    }
    for (i = 1; i < cmd->tx_argc; i++)
    {
	if (NMTermInList(cmd->tx_argv[i]) != NULL)
	    NMDeleteTerm(cmd->tx_argv[i]);
	else
	{
	    TxError("\"%s\" isn't in the current netlist.", cmd->tx_argv[i]);
	    TxError("  Do you have the right netlist?.\n");
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMCmdExtract --
 *
 *	Starting from paint underneath the box, chase out all
 *	electrically-connected material in the edit cell, locate
 *	terminals it touches, and put the terminals into a new
 *	net.
 *
 * Usage:
 *	extract
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The current netlist is modified.
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
void
NMCmdExtract(w, cmd)
    MagWindow *w;			/* Netlist window. */
    TxCommand *cmd;		/* Contains the command's argc and argv. */
{
    if (cmd->tx_argc != 1)
    {
	TxError("Usage: extract\n");
	return;
    }
/*
    if (!NMHasList())
    {
	TxError("Select a netlist first.\n");
	return;
    }
*/
    NMExtract();
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMCmdFindLabels --
 *
 * 	Use the current label as a search pattern and create feedback
 *	areas for all instances of labels with that pattern that lie
 *	beneath the box.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	New feedback areas get created.
 *
 * ----------------------------------------------------------------------------
 */

	/*ARGSUSED*/
void
NMCmdFindLabels(w, cmd)
    MagWindow *w;			/* Netlist window. */
    TxCommand *cmd;		/* Contains the command's argc and argv. */
{
    TileTypeBitMask mask, *pMask;
    char *pattern;

    if (cmd->tx_argc < 2 || cmd->tx_argc > 3)
    {
	TxError("Usage: find pattern [layers]\n");
	return;
    }

    pattern = cmd->tx_argv[1];
    pMask = (TileTypeBitMask *) NULL;
    if (cmd->tx_argc == 3)
    {
	if (!CmdParseLayers(cmd->tx_argv[2], &mask))
	    return;
	pMask = &mask;
    }
    NMShowLabel(pattern, pMask);
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMCmdFlush --
 *
 * 	Flush a netlist, replacing it once again with the contents
 *	from disk.
 *
 * Usage:
 *	flush [netlist]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes made to netlist "netlist" may be lost.
 *
 * ----------------------------------------------------------------------------
 */
	/* ARGSUSED */
void
NMCmdFlush(w, cmd)
    MagWindow *w;			/* Netlist window. */
    TxCommand *cmd;		/* Contains the command's argc and argv. */
{
    char *name;

    if (cmd->tx_argc >= 3)
    {
	TxError("Usage: flush [netlist]\n");
	return;
    }
    if (cmd->tx_argc == 1)
    {
	name = NMNetListButton.nmb_text;
	if (name[0] == 0)
	{
	    TxError("There's no current netlist to flush.\n");
	    return;
	}
    }
    else name = cmd->tx_argv[1];

    NMFlushNetlist(name);
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMCmdJoinNets --
 *
 *	Join two nets together into a single net.
 *
 * Usage:
 *	joinnets term1 term2
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The current netlist is modified.
 *
 * Notes
 *	This command was previously called "join" but conflicts with
 *	the Tcl command of the same name, and for which the syntax is
 *	not distinct.
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
void
NMCmdJoinNets(w, cmd)
    MagWindow *w;			/* Netlist window. */
    TxCommand *cmd;		/* Contains the command's argc and argv. */
{
    if (cmd->tx_argc != 3)
    {
	TxError("Usage: joinnets term1 term2\n");
	return;
    }
    if (!NMHasList())
    {
	TxError("Select a netlist first.\n");
	return;
    }
    if (NMTermInList(cmd->tx_argv[1]) == (char *) NULL)
    {
	TxError("\"%s\" isn't in a net, so can't join it.\n",
		cmd->tx_argv[1]);
	return;
    }
    if (NMTermInList(cmd->tx_argv[2]) == (char *) NULL)
    {
	TxError("\"%s\" isn't in a net, so can't join it.\n",
		cmd->tx_argv[2]);
	return;
    }
    NMJoinNets(cmd->tx_argv[1], cmd->tx_argv[2]);
}
