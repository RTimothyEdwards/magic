/*
 * debugFlags.c --
 *
 * Debugging module.
 * The debugging module provides a standard collection of
 * procedures for setting, examining, and testing debugging flags.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/debug/debugFlags.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include "utils/magic.h"
#include "debug/debug.h"
#include "textio/textio.h"
#include "utils/malloc.h"
#include "utils/utils.h"

struct debugClient debugClients[MAXDEBUGCLIENTS];
int debugNumClients = 0;


/*
 * ----------------------------------------------------------------------------
 *
 * DebugAddClient --
 *
 * Add a client to the debugging module.
 * The argument 'name' is used to identify the client, and the
 * argument 'maxflags' indicates the maximum number of flags
 * that will be added for that client.
 *
 * Results:
 *	Returns a word of ClientData that identifies the
 *	client just added.  This word must be passed to
 *	DebugAddFlag, DebugSet(), or DebugShow() to identify
 *	the client being referred to.
 *
 * Side effects:
 *	Updates the list of known debugging clients.
 *
 * ----------------------------------------------------------------------------
 */

ClientData
DebugAddClient(name, maxflags)
    char *name;
    int maxflags;
{
    struct debugClient *dc;

    if (debugNumClients >= MAXDEBUGCLIENTS)
    {
	TxError("No room for debugging client '%s'.\n", name);
	TxError("Maximum number of clients is %d\n", MAXDEBUGCLIENTS);
	return ((ClientData) (MAXDEBUGCLIENTS-1));
    }

    dc = &debugClients[debugNumClients];
    dc->dc_name = name;
    dc->dc_maxflags = maxflags;
    dc->dc_nflags = 0;
    dc->dc_flags = (struct debugFlag *) mallocMagic((unsigned)
		(sizeof (struct debugFlag) * maxflags));

    while (--maxflags > 0)
    {
	dc->dc_flags[maxflags].df_name = (char *) NULL;
	dc->dc_flags[maxflags].df_value = FALSE;
    }

    return ((ClientData) debugNumClients++);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DebugAddFlag --
 *
 * Add a debugging flag for a particular client.
 * This flag can be set when DebugSet() is called with 'clientID',
 * and will appear in the display of DebugShow().
 *
 * WARNING:
 *	The order in which flags appear for purposes of setting them
 *	with DebugSet(), and when being displayed with DebugShow(),
 *	will be the same as the order in which they are passed to
 *	DebugAddFlag().  To make LookupStruct() work best for DebugSet(),
 *	the flag names should be ordered monotonically.
 *
 * Results:
 *	Returns the index of the debugging flag in the array
 *	debugFlags[].
 *
 * Side effects:
 *	Updates the array debugFlags[].
 *
 * ----------------------------------------------------------------------------
 */

int
DebugAddFlag(clientID, name)
    ClientData clientID;	/* Client identifier from DebugAddClient */
    char *name;			/* Name of debugging flag */
{
    int id = (int) clientID;
    struct debugClient *dc;

    if (id < 0 || id >= debugNumClients)
    {
	TxError("DebugAddFlag: bad client id %d (flag %s)\n", clientID, name);
	return (0);
    }

    dc = &debugClients[id];
    if (dc->dc_nflags >= dc->dc_maxflags)
    {
	TxError("Too many flags for client %s (maximum was set to %d)\n",
		dc->dc_name, dc->dc_maxflags);
	return (dc->dc_nflags);
    }

    dc->dc_flags[dc->dc_nflags].df_name = name;
    dc->dc_flags[dc->dc_nflags].df_value = FALSE;
    return (dc->dc_nflags++);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DebugShow --
 *
 * Show all the debugging flags and their values for a particular
 * client.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes to the terminal.
 *
 * ----------------------------------------------------------------------------
 */

void
DebugShow(clientID)
    ClientData clientID;
{
    int id = (int) clientID;
    struct debugClient *dc;
    int n;

    if (id < 0 || id >= debugNumClients)
    {
	TxError("DebugShow: bad client id %d\n", clientID);
	return;
    }
    dc = &debugClients[id];
    for (n = 0; n < dc->dc_nflags; n++)
	TxPrintf("%-5.5s %s\n", dc->dc_flags[n].df_value ? "TRUE" : "FALSE",
		dc->dc_flags[n].df_name);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DebugSet --
 *
 * Allow debugging flags to be set or cleared for the client 'clientID'.
 * The argument 'argv' contains an array of 'argc' string pointers,
 * each of which is the name of a flag that will be set to 'value'
 * (either TRUE or FALSE).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the debugging flags specified in (argc, argv).
 *	Will complain about any unrecognized flag names.
 *
 * ----------------------------------------------------------------------------
 */

void
DebugSet(clientID, argc, argv, value)
    ClientData clientID;
    int argc;
    char *argv[];
    bool value;
{
    bool badFlag = FALSE;
    int id = (int) clientID;
    struct debugClient *dc;
    int n;

    if (id < 0 || id >= debugNumClients)
    {
	TxError("DebugSet: bad client id %d\n", clientID);
	return;
    }
    dc = &debugClients[id];
    for (; argc-- > 0; argv++)
    {
	n = LookupStruct(*argv, (LookupTable *) dc->dc_flags,
			sizeof dc->dc_flags[0]);
	if (n < 0)
	{
	    TxError("Unrecognized flag '%s' for client '%s' (ignored)\n",
		*argv, dc->dc_name);
	    badFlag = TRUE;
	    continue;
	}
	dc->dc_flags[n].df_value = value;
    }
    /* if badFlag passed, give list of valid flags */
    if(badFlag)
    {
	int n;
	TxError("Valid flags are:  ");
	for (n = 0; n < dc->dc_nflags; n++)
	    TxError("%s ", dc->dc_flags[n].df_name);
	TxError("\n");
    }
}
