
/* gcrEdge.c -
 *
 * The greedy router:
 * Functions to make connections at the far end of the channel.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/gcr/gcrEdge.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "gcr/gcr.h"

/*
 * ----------------------------------------------------------------------------
 *
 * gcrWanted --
 *
 * Reserve tracks needed to make connections at the end of the channel
 * for net occupying track.  Net must be unsplit.  If very close then
 * reserve all tracks needed by net; otherwise just 1.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Mark the desired tracks for such nets as "wanted" by that net.
 *
 * ----------------------------------------------------------------------------
 */

void
gcrWanted(ch, track, column)
    GCRChannel *ch;
    int track, column;
{
    GCRColEl *col = ch->gcr_lCol;
    GCRPin *pin, *next;
    GCRNet *net;

    net = col[track].gcr_h;
    if (net == (GCRNet *) NULL)
	return;

    /* Done if the net is split */
    if (col[track].gcr_hi != EMPTY || col[track].gcr_lo != EMPTY)
	return;

    /* Done if no pins */
    pin = GCRPin1st(net);
    if (pin == (GCRPin *) NULL)
	return;

    /* Done if interior pin */
    if (pin->gcr_x != ch->gcr_length + 1)
	return;

    next = GCRPinNext(pin);
    if (next == (GCRPin *) NULL) col[pin->gcr_y].gcr_wanted = net;
    else if (GCRNearEnd(ch, column))
    {
	for (col[pin->gcr_y].gcr_wanted = net; next; next = GCRPinNext(next))
	    col[next->gcr_y].gcr_wanted = net;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * gcrMarkWanted --
 *
 * Mark tracks near the end of the channel that are wanted to make some
 * connection at the end of the channel.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes the wanted field in column elements.
 *
 * ----------------------------------------------------------------------------
 */

void
gcrMarkWanted(ch)
    GCRChannel *ch;
{
    GCRColEl *col = ch->gcr_lCol;
    GCRPin *pin = ch->gcr_rPins;
    int track;

    for (track = 1; track <= ch->gcr_width; track++)
	if (pin[track].gcr_pId)
	    col[track].gcr_wanted = pin[track].gcr_pId;
}

/*
 * ----------------------------------------------------------------------------
 *
 * gcrUncollapse  --
 *
 * Add vertical segments to this column to split nets making more than
 * one connection at the end of the channel.  Pick a pattern that will
 * create the most split.  Generate all legal combinations and pick the
 * best.  This differs from gcrCollapse in that there are no links
 * between tracks to be followed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If the evaluated column is the best seen, then gcrEvalPat saves it
 *	away.  In any event, gcrEvalPat sets its col argument to NULL.
 *
 * ----------------------------------------------------------------------------
 */

void
gcrUncollapse(ch, col, width, bot, top, split)
    GCRChannel *ch;	/* Channel being processed */
    GCRColEl **col;
    int width;		/* Size of array pointed to by *col */
    int bot, top;	/* Consider tracks between bot .. top inclusive */
    int split;		/* Somewhere between 0 and width inclusive */
{
    int i, to, type, extra, flags;
    GCRColEl *newCol, *gcrCopyCol();
    GCRNet *net, *hnet;

    ASSERT(split <= width, "gcrUncollapse.");
    for (i = bot; i <= top; i++)
    {
	/*
	 * If the track is free but needs to get assigned a net for
	 * a right edge connection...
	 */
	net = (*col)[i].gcr_h;
	if (net == (GCRNet *) NULL
		&& (*col)[i].gcr_wanted
		&& ((*col)[i].gcr_v == (GCRNet *) NULL
			|| (*col)[i].gcr_v == net))
	{
	    /*
	     * Look upwards for the next track that is either assigned to
	     * the net or wants to get assigned the net.  If blocked
	     * vertically then give up.  If found, weight the worth of
	     * the potential connection.
	     */
	    for (to = i + 1; to <= width; to++)
	    {
		flags = (*col)[to].gcr_flags;
		hnet  = (*col)[to].gcr_h;

		/*
		 * Abort if blocked.  The last argument indicates that it is
		 * okay to stop at this location only if this location holds
		 * the net to which (*col)[i] wants to connect.
		 */
		if (gcrBlocked(*col, to, net, hnet != (*col)[i].gcr_wanted))
		{
		    to = width + 1;
		    break;
		}
		else if (hnet == (*col)[i].gcr_wanted)
		{
		    /* Connect to a track already assigned to the net. */
		    type = 1;
		    extra = 2;
		    break;
		}
		else if ((*col)[to].gcr_wanted == (*col)[i].gcr_wanted
			&& hnet == (GCRNet *) NULL)
		{
		    /* Connect to another track not yet part of the net. */
		    extra = 1;
		    type = 2;
		    break;
		}
		else if (flags & GCRCE)
		{
		    to = width + 1;
		    break;
		}
	    }
	}
	else if (net)
	{
	    /*
	     * If the track is assigned to a net and this net has more
	     * connections to make at the end, look upward for the next
	     * track that is empty and wants to connect to this net.
	     * No collapses of split tracks should occur here, since
	     * this function comes after collapsing split nets.
	     */
	    for (to = i + 1; to <= width; to++)
	    {
		/*
		 * Skip if the track ends in the next column.  The last
		 * argument indicates that it is okay to stop here if
		 * this location wants to get hooked up to "net".
		 */
		flags = (*col)[to].gcr_flags;
		if (gcrBlocked(*col, to, net, net == (*col)[to].gcr_wanted))
		{
		    to = width + 1;
		    break;
		}
		else if( (*col)[to].gcr_wanted == net
			&& (*col)[to].gcr_h == (GCRNet *) NULL)
		{
		    /*
		     * Link track "i" (already assigned to the net) to
		     * track "to" (track being uncollapsed).
		     */
		    extra = 2;
		    type = 3;
		    break;
		}
		else if (flags & GCRCE)
		{
		    to = width + 1;
		    break;
		}
	    }
	}
	else continue;

	if (to <= width)
	{
	    /* Found something to connect */
	    newCol = gcrCopyCol( *col, width);
	    switch (type)
	    {
	        case 1:
		    gcrMoveTrack(newCol, net, to, i);
		    break;
		case 2:
		    net = newCol[to].gcr_wanted;
		    gcrLinkTrack(newCol, net, to, width);
		    gcrMoveTrack(newCol, net, to, i);
		    break;
		case 3:
		    gcrMoveTrack(newCol, net, i, to);
		    break;
	    }

	    ASSERT(i < to, "gcrEdge.c, gcrUncollapse");
	    gcrUncollapse(ch, &newCol, width, to, top, split+extra);

	    /* Don't bother to recheck stuff above the last jog */
	    if (top > to) top = to-1;
	}

    }

    gcrEvalPat(col, split, width);
    *col = (GCRColEl *) NULL;
}
