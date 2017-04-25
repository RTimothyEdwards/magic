/*
 * irMain.c --
 *
 * Global data, and initialization code for the irouter.
 *
 * OTHER ENTRY POINTS FOR MODULE  (not in this file):
 *     `:iroute' command  - IRCommand() in irCommand.c
 *     `:*iroute' command - IRTest() in irTestCmd.c
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/irouter/irMain.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

/*--- includes --- */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "drc/drc.h"
#include "select/select.h"
#include "utils/signals.h"
#include "textio/textio.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/styles.h"
#include "debug/debug.h"
#include "utils/undo.h"
#include "textio/txcommands.h"
#include "utils/malloc.h"
#include "utils/list.h"
#include "../mzrouter/mzrouter.h"
#include "irouter/irouter.h"
#include "irouter/irInternal.h"

/*------------------------------ Global Data ------------------------------*/

/* Debug flags */
ClientData irDebugID;
int irDebEndPts;	/* values identify flags to debug module */
int irDebNoClean;
int irRouteWid = -1;	/* if >=0, wid of window to use for determining
			 * subcell expansion, and route cell.
			 */

MazeParameters *irMazeParms = NULL;  /* parameter settings passed to maze router */

/* the following RouteEtc pointers should have the same value as the
 * corresponding fileds in irMazeParms.  They exist for historical
 * reasons.
 */
RouteLayer *irRouteLayers;   
RouteContact *irRouteContacts;
RouteType *irRouteTypes;


/*
 * ----------------------------------------------------------------------------
 *
 * IRDebugInit --
 *
 * This procedure is called when Magic starts up, and should not be
 * called again.  
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Register ourselves with debug module
 *	Setup some internal datastructures.
 *	
 * ----------------------------------------------------------------------------
 */

void
IRDebugInit()
{
    int n;
    /* debug struct */
    static struct
    {
	char	*di_name;
	int	*di_id;
    } dflags[] = {
	"endpts",	&irDebEndPts,
	"noclean",	&irDebNoClean,
	0
    };

    /* Register with debug module */

    irDebugID = DebugAddClient("irouter", sizeof dflags/sizeof dflags[0]);
    for (n = 0; dflags[n].di_name; n++)
	*(dflags[n].di_id) = DebugAddFlag(irDebugID, dflags[n].di_name);


    return;
}

/*
 * ----------------------------------------------------------------------------
 *
 * IRAfterTech --
 *
 * This routine should be called after technology initialization or reloading,
 * and should be called *after* the maze routere has been initialized.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Register ourselves with debug module
 *	Setup some internal datastructures.
 *	
 * ----------------------------------------------------------------------------
 */

void
IRAfterTech()
{
    /* Initialize the irouter maze parameters with a copy of the "irouter"
     * style (default) parameters.
     */

    if (irMazeParms != NULL)
    {
	/* Free any existing parameters */
	MZFreeParameters(irMazeParms);
	irMazeParms = NULL;
    }
    irMazeParms = MZCopyParms(MZFindStyle("irouter"));

    if (irMazeParms != NULL)
    {
        /* set global type lists from current irouter parms.
         * These lists are often referenced directly rather than through
         * the parameter structure for historical reasons.
         */

        irRouteLayers = irMazeParms->mp_rLayers;
        irRouteContacts = irMazeParms->mp_rContacts;
        irRouteTypes = irMazeParms->mp_rTypes;
    }
}
