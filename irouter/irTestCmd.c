/*
 * irTestCmd.c --
 *
 * Code to process the `*iroute' command.
 * `*iroute' is a wizard command for debugging and testing.
 *
 *     ********************************************************************* 
 *     * Copyright (C) 1987, 1990 Michael H. Arnold, Walter S. Scott, and  *
 *     * the Regents of the University of California.                      *
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/irouter/irTestCmd.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/hash.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "utils/signals.h"
#include "textio/textio.h"
#include "irouter/irouter.h"
#include "graphics/graphics.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "dbwind/dbwtech.h"
#include "textio/txcommands.h"
#include "utils/main.h"
#include "utils/utils.h"
#include "commands/commands.h"
#include "utils/styles.h"
#include "utils/malloc.h"
#include "utils/list.h"
#include "../mzrouter/mzrouter.h"
#include "irouter/irInternal.h"

/* Subcommand table - declared here since its referenced before defined */
typedef struct
{
    char	*sC_name;	/* name of iroute subcommand */
    void	(*sC_proc)();	/* Procedure implementing this subcommand */
    char 	*sC_commentString;
    char	*sC_usage;
} TestCmdTableE;
extern TestCmdTableE irTestCommands[];


/*
 * ----------------------------------------------------------------------------
 *
 * irDebugTstCmd --
 *
 * irouter wizard command (`:*iroute') to set/clear debug flags.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modify debug flags.
 *	
 * ----------------------------------------------------------------------------
 */

void
irDebugTstCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int result;
    bool value;

    if (cmd->tx_argc > 4)
    {
	TxPrintf("Too many args on '*iroute debug'\n");
	return;
    }
    else if (cmd->tx_argc == 4)
    {
	/* two args, set or clear first arg according to second */

	result = SetNoisyBool(&value,cmd->tx_argv[3], (FILE *) NULL);
	if (result == 0)
	{
	    TxPrintf("\n");
	    DebugSet(irDebugID,1,&(cmd->tx_argv[2]),(bool) value);
	}
	else
	    TxError("Unknown boolean value %s\n", cmd->tx_argv[2]);
    }
    else
    {
	/* list current values of flags */
	DebugShow(irDebugID);
    }

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * irHelpTstCmd --
 *
 * irouter wizard command (`:*iroute') to print help info on irouter wizard
 * commands.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See above.
 *	
 * ----------------------------------------------------------------------------
 */

void
irHelpTstCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int n;
    int which;

    if(cmd->tx_argc == 2)
    {
	/* No arg, so print summary of commands */
	for(n=0; irTestCommands[n].sC_name!=NULL; n++)
	{
	    TxPrintf("*iroute %s - %s\n",
		    irTestCommands[n].sC_name,
		    irTestCommands[n].sC_commentString);
	}
	TxPrintf("\n*iroute help [subcmd] - ");
	TxPrintf("Print usage info for subcommand.\n");
    }
    else
    {
	/* Lookup subcommand in table, and printed associated help info */
	which = LookupStruct(
	    cmd->tx_argv[2], 
	    (char **) irTestCommands, 
	    sizeof irTestCommands[0]);

        /* Process result of lookup */
	if (which >= 0)
	{
	    /* subcommand found - print out its comment string and usage */
	    TxPrintf("*iroute %s - %s\n",
		    irTestCommands[which].sC_name,
		    irTestCommands[which].sC_commentString);
	    TxPrintf("Usage:  *iroute %s\n",
		    irTestCommands[which].sC_usage);
	}
	else if (which == -1)
	{
	    /* ambiguous subcommand - complain */
	    TxError("Ambiguous *iroute subcommand: \"%s\"\n", cmd->tx_argv[2]);
	}
	else
	{
	    /* unrecognized subcommand - complain */
	    TxError("Unrecognized iroute subcommand: \"%s\"\n", 
		    cmd->tx_argv[2]);
	    TxError("Valid *iroute subcommands are:  ");
	    for (n = 0; irTestCommands[n].sC_name; n++)
		TxError(" %s", irTestCommands[n].sC_name);
	    TxError("\n");
	}
    }

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * irParmsTstCmd --
 *
 * irouter wizard command (`:*iroute') to dump parms.
 * commands.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Dump routelayers and routecontacts.
 *	
 * ----------------------------------------------------------------------------
 */

void
irParmsTstCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{

    MZPrintRLs(irRouteLayers);
    TxMore("");
    MZPrintRCs(irRouteContacts);

    return;
}

/*--------------------------- Command Table ------------------------------ */
TestCmdTableE irTestCommands[] = {
    "debug",	irDebugTstCmd,
    "set or clear debug flags",
    "debug [flag] [value]",

    "help",	irHelpTstCmd,
    "summarize *iroute subcommands",
    "help [subcommand]",

    "parms",	irParmsTstCmd,
    "print internal data structures",
    "parms",

	0
    }, *irTestCmdP;


/*
 * ----------------------------------------------------------------------------
 *
 * IRTest --
 *
 * Command interface for testing the interactive router.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Depends on the command; see below.
 *
 * Organization:
 *	We select a procedure based on the first keyword (argv[0])
 *	and call it to do the work of implementing the rule.  Each
 *	such procedure is of the following form:
 *
 *	int
 *	proc(argc, argv)
 *	    int argc;
 *	    char *argv[];
 *	{
 *	}
 *
 * ----------------------------------------------------------------------------
 */

void
IRTest(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int n;
    int which;


    if(cmd->tx_argc == 1)
    {
	/* No subcommand specified.  */
	TxPrintf("Must specify subcommand.");
	TxPrintf("  (type '*iroute help' for summary)\n");
    }
    else
    {
	/* Lookup subcommand in table */
	which = LookupStruct(
	    cmd->tx_argv[1], 
	    (char **) irTestCommands, 
	    sizeof irTestCommands[0]);

        /* Process result of lookup */
	if (which >= 0)
	{
	    /* subcommand found - call proc that implements it */
	    irTestCmdP = &irTestCommands[which];
	    (*irTestCmdP->sC_proc)(w,cmd);
	}
	else if (which == -1)
	{
	    /* ambiguous subcommand - complain */
	    TxError("Ambiguous subcommand: \"%s\"\n", cmd->tx_argv[1]);
	}
	else
	{
	    /* unrecognized subcommand - complain */
	    TxError("Unrecognized subcommand: \"%s\"\n", cmd->tx_argv[1]);
	    TxError("Valid subcommands:");
	    for (n = 0; irTestCommands[n].sC_name; n++)
		TxError(" %s", irTestCommands[n].sC_name);
	    TxError("\n");
	}
    }

    return;
}
