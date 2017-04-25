/* gcrFeas.c -
 *
 *	The greed router:  Making feasible top and bottom connections.
 *		This includes the initial attempt and widening if a
 *		connection could not be made.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/gcr/gcrFeas.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "gcr/gcr.h"
#include "utils/hash.h"
#include "database/database.h"
#include "router/router.h"

extern int gcrStandalone;
extern int gcrRouterErrors;

/* Forward declarations */
void gcrMakeFeasible();

/*
 * ----------------------------------------------------------------------------
 *	gcrFeasible --
 *
 * 	Make feasible top and bottom connections in a minimal manner, running
 *	to the first track that is either empty or already assigned to the net.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	*failedTop and *failedBot get set true or false, depending on whether
 *	the connections from the top and bottom are successful or not.
 *	Updates the structs for the nets to delete the pins.
 *	Updates the column data structure to reflect the track assignment.
 *	column[0].gcr_hi=column[width+1].gcr_lo=EMPTY at conclusion.
 * ----------------------------------------------------------------------------
 */

void
gcrFeasible(ch, col)
    GCRChannel * ch;
    int 	 col;		/* Column for feasible connections	*/
{
    GCRColEl * column;
    GCRNet   * topNet, *botNet;
    int        i, topTarget, botTarget;
    int	       failedTop;
    int	       failedBot;

    topNet=ch->gcr_tPins[col].gcr_pId;
    botNet=ch->gcr_bPins[col].gcr_pId;
    column=ch->gcr_lCol;

/* The order in which pins are unlinked is important.  This is because
 * the unlinker checks to see that pins are unlinked in order of their
 * position on the list.
 */
    gcrUnlinkPin(&ch->gcr_bPins[col]);
    gcrUnlinkPin(&ch->gcr_tPins[col]);

    if (topNet)
    {
	/* There is a top pin at this column */
	failedTop=TRUE;

    /* Look for the first track to which this net can run.  It must either
     * be empty or else already assigned to the topNet.
     */
        for(i=ch->gcr_width; i>0; i--)
	    if(gcrBlocked(column, i, topNet, 0))
		break;
	    else
	    if((column[i].gcr_h==(GCRNet *) NULL) || (column[i].gcr_h==topNet))
	    {
		topTarget=i;
		failedTop=FALSE;
		break;
	    }
    }
    else
	failedTop=FALSE;

    if(botNet!=(GCRNet *) NULL)
    {
	failedBot=TRUE;
	column[0].gcr_lo=column[0].gcr_hi= EMPTY;
        for(i=1; i<=ch->gcr_width; i++)
	    if(gcrBlocked(column, i, botNet, ch->gcr_width))
		break;
	    else
	    if((column[i].gcr_h==(GCRNet *) NULL) || (column[i].gcr_h==botNet))
	    {
		botTarget= i;
		failedBot=FALSE;
		break;
	    }
    }
    else
	failedBot=FALSE;

/* If there are no empty tracks and net topNet==botNet!=0 is a net which
 * has connections in this column only, then run a vertical wire from top
 * to bottom of this column.
 */
    if( failedTop && failedBot &&
	    (topNet==botNet) && (topNet!=(GCRNet *) NULL) &&
	    (topNet->gcr_lPin==(GCRPin *) NULL) )
    {
    /* ... but only do this if there isn't some block */
	for(i = 1; i <= ch->gcr_width; i++)
	    if(gcrBlocked(column, i, botNet, ch->gcr_width)) return;
	gcrMoveTrack(column, topNet, ch->gcr_width+1, 0);
	failedBot= failedTop=FALSE;
    }
    else

/* If there is a conflict in the wiring, then just wire the one with the
 * shortest connection.
 */
    if( !(failedBot) && (topNet!=(GCRNet *) NULL) &&
	!(failedTop) && (botNet!=(GCRNet *) NULL) && (botTarget>=topTarget) )
	if(topNet==botNet)
	{
	    gcrMakeFeasible(column, topNet, ch->gcr_width+1, topTarget,
		    ch->gcr_width);
	    gcrMakeFeasible(column, botNet, 0, botTarget, ch->gcr_width);
	    gcrWanted(ch, topTarget, col);
	}
	else
	if( botTarget > (ch->gcr_width-topTarget-1) )
	{
	    gcrMakeFeasible(column, topNet, ch->gcr_width+1, topTarget,
		    ch->gcr_width);
	    failedBot=TRUE;
	    gcrWanted(ch, topTarget, col);
	}
	else
	{
	    gcrMakeFeasible(column, botNet, 0, botTarget, ch->gcr_width);
	    failedTop=TRUE;
	    gcrWanted(ch, botTarget, col);
	}
    else

/* Make successful connections.  Leave unsuccessful connections for later.  */
    {
	if( (! failedTop) && (topNet!=(GCRNet *) NULL) )
	{
	    gcrMakeFeasible(column, topNet, ch->gcr_width+1, topTarget, 
		ch->gcr_width);
	    gcrWanted(ch, topTarget, col);
	}
	if( (! failedBot) && (botNet!=(GCRNet *) NULL) )
	{
	    gcrMakeFeasible(column, botNet, 0, botTarget, ch->gcr_width);
	    gcrWanted(ch, botTarget, col);
	}
    }
    if(failedTop)
    {
	RtrChannelError(ch, col, ch->gcr_width,
	    "Can't make top connection",
	    ch->gcr_tPins[col].gcr_pId->gcr_Id);
	gcrRouterErrors++;
    }
    if(failedBot)
    {
	RtrChannelError(ch, col, 1, "Can't make bottom connection",
	    ch->gcr_bPins[col].gcr_pId->gcr_Id);
	gcrRouterErrors++;
    }
}

/*
 * ----------------------------------------------------------------------------
 *	gcrMakeFeasible --
 *
 * 	Run a feasible connection from the bottom or top to the given target
 *	track.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Have to fiddle around with column[0] and column[width+1].
 * ----------------------------------------------------------------------------
 */

void
gcrMakeFeasible(col, net, from, target, width)
    GCRColEl *  col;		/* Describes current column structure. */
    GCRNet   *  net;		/* Net being brought into column. */
    int		from;		/* Pin location for net (0 or width+1). */
    int		target;		/* Target track for net. */
    int		width;		/* Width of column. */
{
    int i;

/* Find the index of the first track occupied by net.  Pass it to
 * gcrMoveTrack through column[from].gcr_lo or gcr_hi.
 */
    col[from].gcr_lo=col[from].gcr_hi= EMPTY;
    if(col[target].gcr_h==net)	/*run to occupied track*/
    {
	col[from].gcr_lo= col[target].gcr_lo;
	col[from].gcr_hi= col[target].gcr_hi;
    }
    else
    if(from!=0)	/*Run to empty track.  Connect from the top*/
	for(i=target-1; i>0; i--)
	    if(col[i].gcr_h==net)
	    {
		col[from].gcr_lo=i;
		col[i].gcr_hi=target;
		break;
	    }
	    else /*nothing*/;
    else /*Run to empty track.  Connect from bottom.*/
	for(i=target+1; i<=width; i++)
	    if(col[i].gcr_h==net)
	    {
		col[0].gcr_hi=i;
		col[i].gcr_lo=target;
		break;
	    }
    gcrMoveTrack(col, net, from, target);
    col[from].gcr_lo=col[from].gcr_hi= EMPTY;
}
