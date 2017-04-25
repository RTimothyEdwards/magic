/*
 * gcrRiver.c -
 *
 * The greedy router: river-routing across the tops of channels.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/gcr/gcrRiver.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "gcr/gcr.h"
#include "textio/textio.h"
#include "utils/malloc.h"

/* Forward declarations */
bool gcrOverCellVert();
bool gcrOverCellHoriz();


/*
 * ----------------------------------------------------------------------------
 *
 * gcrRiverRoute --
 *
 * Determine if a channel should be routed by a simple river-router;
 * if so, then route it.  Currently, river-routing channels are identified
 * during channel decomposition.
 *
 * Results:
 *	Returns TRUE on success, FALSE on failure (in which case
 *	the caller should try to route the channel by other means.
 *
 * Side effects:
 *	If successful, sets flags in the channel structure to show
 *	where routing is to be placed.
 *
 * ----------------------------------------------------------------------------
 */

bool
gcrRiverRoute(ch)
    GCRChannel *ch;
{
    switch (ch->gcr_type)
    {
	case CHAN_HRIVER:
	    if (gcrOverCellHoriz(ch))
		return (TRUE);
	    break;
	case CHAN_VRIVER:
	    if (gcrOverCellVert(ch))
		return (TRUE);
	    break;
    }

    return (FALSE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * gcrOverCellHoriz --
 * gcrOverCellVert --
 *
 * Route an over-cell channel if possible.  These channels should be
 * river-routable.  We perform a few quick checks to ensure that there
 * are terminals only on opposite sides in pairs.  Channels routed
 * horizontally are processed by gcrOverCellHoriz(); those routed
 * vertically are processed by gcrOverCellVert().
 *
 * Results:
 *	TRUE if the channel met our requirements and we routed it
 *	successfully; FALSE if we couldn't route it.
 *
 * Side effects:
 *	Sets flags in the channel structure to show where routing
 *	is to be placed.
 *
 * ----------------------------------------------------------------------------
 */

#define	USED(pin) \
	((pin)->gcr_pId != (GCRNet *) 0 && (pin)->gcr_pId != (GCRNet *) -1)

bool
gcrOverCellHoriz(ch)
    GCRChannel *ch;
{
    short **result = ch->gcr_result;
    int col, row;

    /* Ensure top and bottom pins aren't used */
    for (col = 1; col <= ch->gcr_length; col++)
	if (USED(&ch->gcr_tPins[col]) || USED(&ch->gcr_bPins[col]))
	{
	    TxPrintf("Failing because top or bottom pins are used\n");
	    return (FALSE);
	}

    /* Ensure left and right pins match */
    for (row = 1; row <= ch->gcr_width; row++)
	if (USED(&ch->gcr_lPins[row]) && USED(&ch->gcr_rPins[row]))
	{
	    if (ch->gcr_lPins[row].gcr_pId != ch->gcr_rPins[row].gcr_pId
		|| ch->gcr_lPins[row].gcr_pSeg != ch->gcr_rPins[row].gcr_pSeg)
	    {
		TxPrintf("Failing because left and right pins don't match\n");
		return (FALSE);
	    }
	}

    /*
     * Channel is routable by a simple river-router:
     * zoom across for each row that is to be connected
     * across the channel.
     */
    for (row = 1; row <= ch->gcr_width; row++)
	if (USED(&ch->gcr_lPins[row]))
	    for (col = 0; col <= ch->gcr_length; col++)
		result[col][row] |= GCRR;

    return (TRUE);
}

bool
gcrOverCellVert(ch)
    GCRChannel *ch;
{
    short **result = ch->gcr_result;
    int col, row;

    /* Ensure left and right pins aren't used */
    for (row = 1; row <= ch->gcr_width; row++)
	if (USED(&ch->gcr_lPins[row]) || USED(&ch->gcr_rPins[row]))
	{
	    TxPrintf("Failing because left or right pins are used\n");
	    return (FALSE);
	}

    /* Ensure top and bottom pins match */
    for (col = 1; col <= ch->gcr_length; col++)
	if (USED(&ch->gcr_tPins[col]) && USED(&ch->gcr_bPins[col]))
	{
	    if (ch->gcr_tPins[col].gcr_pId != ch->gcr_bPins[col].gcr_pId
		|| ch->gcr_tPins[col].gcr_pSeg != ch->gcr_bPins[col].gcr_pSeg)
	    {
		TxPrintf("Failing because top and bottom pins don't match\n");
		return (FALSE);
	    }
	}

    /*
     * Channel is routable by a simple river-router:
     * zoom across for each column that is to be connected
     * across the channel.
     */
    for (col = 1; col <= ch->gcr_length; col++)
	if (USED(&ch->gcr_tPins[col]))
	    for (row = 0; row <= ch->gcr_width; row++)
		result[col][row] |= GCRU;

    return (TRUE);
}
