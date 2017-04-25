
/* windDebug.c -
 *
 *	Print out the window package's internal data structures.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/windows/windDebug.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "windows/windows.h"
#include "graphics/glyphs.h"
#include "windows/windInt.h"
#include "textio/textio.h"
#include "textio/txcommands.h"



/*
 * ----------------------------------------------------------------------------
 * windPrintWindow --
 *
 *	Print out the window data structure.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Text appears on the text terminal.
 * ----------------------------------------------------------------------------
 */

void
windPrintWindow(w)
    MagWindow *w;	
{
    LinkedRect *lr;

    TxPrintf("\nWindow %d: '%s'\n", w->w_wid, w->w_caption);
    TxPrintf("  Client %x  Surface %x \n", w->w_client, w->w_surfaceID);

    TxPrintf("  All area (%d, %d) (%d, %d)\n",
	w->w_allArea.r_xbot, w->w_allArea.r_ybot,
	w->w_allArea.r_xtop, w->w_allArea.r_ytop);
    TxPrintf("  Screen area (%d, %d) (%d, %d)\n",
	w->w_screenArea.r_xbot, w->w_screenArea.r_ybot,
	w->w_screenArea.r_xtop, w->w_screenArea.r_ytop);
    TxPrintf("  Frame area (%d, %d) (%d, %d)\n",
	w->w_frameArea.r_xbot, w->w_frameArea.r_ybot,
	w->w_frameArea.r_xtop, w->w_frameArea.r_ytop);

    if (w->w_clipAgainst == NULL)
	TxPrintf("  No areas obscure the window.\n");
    else
	TxPrintf("  These areas obscure the window:\n");
    for (lr = w->w_clipAgainst; lr != NULL; lr = lr->r_next)
    {
	TxPrintf("    (%d, %d) (%d, %d) \n", lr->r_r.r_xbot, lr->r_r.r_ybot,
		lr->r_r.r_xtop, lr->r_r.r_ytop);
    }

    TxPrintf("  Surface area (%d, %d) (%d, %d) \n", 
	    w->w_surfaceArea.r_xbot, w->w_surfaceArea.r_ybot,
	    w->w_surfaceArea.r_xtop, w->w_surfaceArea.r_ytop);
    TxPrintf("  Origin (%d, %d)\n", w->w_origin.p_x, w->w_origin.p_y);
    TxPrintf("  Scale %d\n", w->w_scale);
}

/*
 * ----------------------------------------------------------------------------
 * windDump --
 *
 *	Print out all the tables and windows.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Lots of text is dumped to the text terminal.
 * ----------------------------------------------------------------------------
 */

void
windDump()
{
    MagWindow *w;
    clientRec *rc;

    TxPrintf("\n\n------------ Clients ----------\n");
    for (rc = windFirstClientRec; rc != (clientRec * ) NULL; 
	rc = rc->w_nextClient)
    {
	TxPrintf("'%10s'  %x %x %x %x\n", rc->w_clientName, 
	    rc->w_create, rc->w_delete,
	    rc->w_redisplay, rc->w_command);
    }

    TxPrintf("\n");
    for (w = windTopWindow; w != (MagWindow *) NULL; w = w->w_nextWindow)
    {
	windPrintWindow(w);
    }
    
}


/*
 * ----------------------------------------------------------------------------
 * windPrintCommand --
 *
 *	Print out a command.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Text appears on the text terminal.
 * ----------------------------------------------------------------------------
 */

void
windPrintCommand(cmd)
    TxCommand *cmd;
{
    if (cmd->tx_button == TX_NO_BUTTON)
    {
	int i;

	for (i = 0; i < cmd->tx_argc; i++)
	{
	    TxPrintf(" '%s'", cmd->tx_argv[i]);
	}
    }
    else
    {
	switch (cmd->tx_button)
	{
	    case TX_LEFT_BUTTON:
		TxPrintf("Left");
		break;
	    case TX_RIGHT_BUTTON:
		TxPrintf("Right");
		break;
	    case TX_MIDDLE_BUTTON:
		TxPrintf("Middle");
		break;
	    default:
		TxPrintf("STRANGE");
		break;
	}
	TxPrintf(" button ");
	switch (cmd->tx_buttonAction)
	{
	    case TX_BUTTON_DOWN:
		TxPrintf("down");
		break;
	    case TX_BUTTON_UP:
		TxPrintf("up");
		break;
	}
    }
    TxPrintf(" at (%d, %d)\n", cmd->tx_p.p_x, cmd->tx_p.p_y);
}
