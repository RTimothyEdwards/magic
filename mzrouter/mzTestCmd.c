/*
 * mzTestCmd.c --
 *
 * Code to process the `*mzroute' command.
 * `*mzroute' is a wizard command for debugging and testing the maze router.
 *
 *     ********************************************************************* 
 *     * Copyright (C) 1988, 1990 Michael H. Arnold and the Regents of the *
 *     * University of California.                                         *
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/mzrouter/mzTestCmd.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
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
#include "utils/heap.h"
#include "mzrouter/mzrouter.h"
#include "mzrouter/mzInternal.h"

/* Subcommand table - declared here since its referenced before defined */
typedef struct
{
    char	*sC_name;	/* name of iroute subcommand */
    void	(*sC_proc)();	/* Procedure implementing this 
				       subcommand */
    char 	*sC_commentString;
    char	*sC_usage;
} TestCmdTableE;
extern TestCmdTableE mzTestCommands[];


/*
 * ----------------------------------------------------------------------------
 *
 * mzDebugTstCmd --
 *
 * mzrouter wizard command (`:*mzroute') to set/clear debug flags.
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
mzDebugTstCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int result;
    bool value;

    if (cmd->tx_argc > 4)
    {
	TxPrintf("Too many args on '*mzroute debug'\n");
	return;
    }
    else if (cmd->tx_argc == 4)
    {
	/* two args, set or clear first arg according to second */

	result = SetNoisyBool(&value,cmd->tx_argv[3], NULL);
	if (result == 0)
	{
	    TxPrintf("\n");
	    DebugSet(mzDebugID,1,&(cmd->tx_argv[2]),(bool) value);
	}
	else
	    TxError("Bad boolean value %s---try true or false.\n",
			cmd->tx_argv[3]);
    }
    else
    {
	/* list current values of flags */
	DebugShow(mzDebugID);
    }

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzDumpEstimatesTstCmd --
 *
 * mzrouter wizard command (`:*mzroute') to dump estimate plane info 
 * associated with tiles under the box.
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
mzDumpEstimatesTstCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    if (cmd->tx_argc > 2)
    {
	TxPrintf("Too many args on '*mzroute dumpEstimates'\n");
	return;
    }
    else
    {
	CellDef *boxDef;
	Rect box;

	/* Use box for dump area */
	if(!ToolGetBox(&boxDef,&box))
	{
	   TxError("No Box.\n");
	   return;
	}

	/* Call dump routine to do the real work */
	mzDumpEstimates(&box,(FILE *) NULL);
    }

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzDumpTagsTstCmd --
 *
 * mzrouter wizard command (`:*mzroute') to dump tag info 
 * associated with tiles under the box.
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
mzDumpTagsTstCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    if (cmd->tx_argc > 2)
    {
	TxPrintf("Too many args on '*mzroute dumpTags'\n");
	return;
    }
    else
    {
	CellDef *boxDef;
	Rect box;

	/* Use box for dump area */
	if(!ToolGetBox(&boxDef,&box))
	{
	   TxError("No Box.\n");
	   return;
	}

	/* Call dump routine to do the real work */
	mzDumpTags(&box);
    }

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzHelpTstCmd --
 *
 * mzrouter wizard command (`:*iroute') to print help info on mzrouter wizard
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
mzHelpTstCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int n;
    int which;

    if(cmd->tx_argc == 2)
    {
	/* No arg, so print summary of commands */
	for(n=0; mzTestCommands[n].sC_name!=NULL; n++)
	{
	    TxPrintf("*mzroute %s - %s\n",
		    mzTestCommands[n].sC_name,
		    mzTestCommands[n].sC_commentString);
	}
	TxPrintf("\n*mzroute help [subcmd] - ");
	TxPrintf("Print usage info for subcommand.\n");
    }
    else
    {
	/* Lookup subcommand in table, and printed associated help info */
	which = LookupStruct(
	    cmd->tx_argv[2], 
	    (LookupTable *) mzTestCommands, 
	    sizeof mzTestCommands[0]);

        /* Process result of lookup */
	if (which >= 0)
	{
	    /* subcommand found - print out its comment string and usage */
	    TxPrintf("*mzroute %s - %s\n",
		    mzTestCommands[which].sC_name,
		    mzTestCommands[which].sC_commentString);
	    TxPrintf("Usage:  *mzroute %s\n",
		    mzTestCommands[which].sC_usage);
	}
	else if (which == -1)
	{
	    /* ambiguous subcommand - complain */
	    TxError("Ambiguous *mzroute subcommand: \"%s\"\n", 
		    cmd->tx_argv[2]);
	}
	else
	{
	    /* unrecognized subcommand - complain */
	    TxError("Unrecognized iroute subcommand: \"%s\"\n", 
		    cmd->tx_argv[2]);
	    TxError("Valid *mzroute subcommands are:  ");
	    for (n = 0; mzTestCommands[n].sC_name; n++)
		TxError(" %s", mzTestCommands[n].sC_name);
	    TxError("\n");
	}
    }

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzNumberLineTstCmd --
 *
 * mzrouter wizard command (`:*mzroute') to exercise numberline code.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	fiddle with some number lines.
 *	
 * ----------------------------------------------------------------------------
 */

void
mzNumberLineTstCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    NumberLine myLine;
    int *result;
    
    mzNLInit(&myLine, 2);

    TxPrintf("Inserting 10\n");
    mzNLInsert(&myLine, 10);

    TxPrintf("Inserting 10\n");
    mzNLInsert(&myLine, 10);

    TxPrintf("Inserting -10\n");
    mzNLInsert(&myLine, -10);

    TxPrintf("Inserting 0\n");
    mzNLInsert(&myLine, 0);

    TxPrintf("Inserting 20\n");
    mzNLInsert(&myLine, 20);

    TxPrintf("Inserting -20\n");
    mzNLInsert(&myLine, -20);

    TxPrintf("Inserting 0\n");
    mzNLInsert(&myLine, 0);

    result = mzNLGetContainingInterval(&myLine, 35);
    TxPrintf("query = 35,  result = (%d, %d)\n",
	     *result, *(result+1));

    result = mzNLGetContainingInterval(&myLine, -35);
    TxPrintf("query = -35,  result = (%d, %d)\n",
	     *result, *(result+1));

    result = mzNLGetContainingInterval(&myLine, 0);
    TxPrintf("query = 0,  result = (%d, %d)\n",
	     *result, *(result+1));

    result = mzNLGetContainingInterval(&myLine, 5);
    TxPrintf("query = 5,  result = (%d, %d)\n",
	     *result, *(result+1));

    result = mzNLGetContainingInterval(&myLine, 12);
    TxPrintf("query = 12,  result = (%d, %d)\n",
	     *result, *(result+1));

    result = mzNLGetContainingInterval(&myLine, -12);
    TxPrintf("query = -12,  result = (%d, %d)\n",
	     *result, *(result+1));

    result = mzNLGetContainingInterval(&myLine, 20);
    TxPrintf("query = 20,  result = (%d, %d)\n",
	     *result, *(result+1));

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzParmsTstCmd --
 *
 * mzrouter wizard command (`:*mzroute') to dump current route types.
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
mzParmsTstCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{

    MZPrintRLs(mzRouteLayers);
    TxMore("");
    MZPrintRCs(mzRouteContacts);

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzPlaneTstCmd --
 *
 * mzrouter wizard command (`:*mzroute') to display internal tile structure
 * of a blockage plane.
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
mzPlaneTstCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    TileType t;
    RouteType *rT;
    char *layerName;

    /* check number of args */
    if(cmd->tx_argc != 3)
    {
	TxError("Usage:  *mzroute plane route-layer");
	TxError("makes corresponding blockage plane visible)\n ");
	return;
    }

    /* Get layer name from args*/
    layerName = cmd->tx_argv[2];

    /* convert name to type */
    t = DBTechNameType(layerName);
    if(t == -1) 
    {
	TxPrintf("`%s' is ambiguous\n",layerName);
	return;
    }
    if(t == -2) 
    {
	TxPrintf("`%s' type not recognized\n",layerName);
	return;
    }

    /* convert type to route type */
    rT = mzFindRouteType(t);
    if(rT == NULL)
    {
	TxPrintf("`%s' is not a routeType ", layerName);
	TxPrintf("- so there is no associated blockage plane.\n");
	return;
    }

    /* Attach Blockage plane of routeType to "__BLOCK" cell for display */
    mzBlockDef->cd_planes[PL_M_HINT] = rT->rt_hBlock;
    
    /* Display it */
    DBWAreaChanged(mzBlockDef, 
		   &TiPlaneRect, 
		   DBW_ALLWINDOWS,
		   &DBAllButSpaceBits);
    WindUpdate();

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzVersionCmd --
 *
 * mzrouter wizard subcommand (`*mzroute version') to display mzrouter 
 * version string.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Displays version string.
 *	
 * ----------------------------------------------------------------------------
 */

void
mzVersionCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{

    if(cmd->tx_argc == 2)
    {
	/* Print out version string */
        TxPrintf("\tMzrouter version %s\n", MZROUTER_VERSION);
    }
    else
    {
	TxPrintf("Too many args on 'mzroute version'\n");
    }

    return;
}



/*
 * ----------------------------------------------------------------------------
 *
 * MZTest --
 *
 * Command interface for testing the maze router.
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

/*--------------------------- Command Table ------------------------------ */

TestCmdTableE mzTestCommands[] = {
    "debug",	mzDebugTstCmd,
    "set or clear debug flags",
    "debug [flag] [value]",

    "dumpEstimates", mzDumpEstimatesTstCmd,
    "print global estimate info for tiles under box",
    "dumpEstimates",

    "dumpTags", mzDumpTagsTstCmd,
    "print tag info on data tiles under box",
    "dumpTags",

    "help",	mzHelpTstCmd,
    "summarize *mzroute subcommands",
    "help [subcommand]",

    "numberLine", mzNumberLineTstCmd,
    "exercise numberline code",
    "numberLine",

    "parms",	mzParmsTstCmd,
    "print internal data structures",
    "parms",

    "plane",	mzPlaneTstCmd,
    "make internal tile plane visible",
    "plane [plane]",

    "version",	mzVersionCmd,
    "identify mzrouter version",
    "version",

     0
    }, *mzTestCmdP;

void
MZTest(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int n;
    int which;


    if(cmd->tx_argc == 1)
    {
	/* No subcommand specified.  */
	TxPrintf("Must specify subcommand.");
	TxPrintf("  (type '*mzroute help' for summary)\n");
    }
    else
    {
	/* Lookup subcommand in table */
	which = LookupStruct(
	    cmd->tx_argv[1], 
	    (LookupTable *) mzTestCommands, 
	    sizeof mzTestCommands[0]);

        /* Process result of lookup */
	if (which >= 0)
	{
	    /* subcommand found - call proc that implements it */
	    mzTestCmdP = &mzTestCommands[which];
	    (*mzTestCmdP->sC_proc)(w,cmd);
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
	    for (n = 0; mzTestCommands[n].sC_name; n++)
		TxError(" %s", mzTestCommands[n].sC_name);
	    TxError("\n");
	}
    }

    return;
}
