/* gcrRoute.c -
 *
 * The greedy router:  Top level procedures.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/gcr/gcrRoute.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <sys/types.h>
#include <sys/times.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "gcr/gcr.h"
#include "utils/signals.h"
#include "utils/malloc.h"
#include "utils/styles.h"

int gcrRouterErrors;
extern int gcrStandalone;

/* Forward declarations */
void gcrRouteCol();
void gcrExtend();


/*
 * ----------------------------------------------------------------------------
 *
 * GCRroute --
 *
 * Top level for the greedy channel router.
 * Routes are already set up channel routing problem.
 *
 * Results:
 *	The return value is the number of errors found while routing
 *	this channel.
 *
 * Side effects:
 *	Modifies flag bits in the channel to show the presence of routing.
 *	Calls RtrChannelError when there are errors.
 *
 * ----------------------------------------------------------------------------
 */

int
GCRroute(ch)
    GCRChannel *ch;
{
    int i, density, netId;
    char mesg[256];
    GCRColEl *col;
    GCRPin *pin;
    GCRNet *net;

    /* Try river-routing across the channel if possible */
    gcrRouterErrors = 0;
    if (gcrRiverRoute(ch))
	return (gcrRouterErrors);

    gcrBuildNets(ch);
    if (ch->gcr_nets == (GCRNet *) NULL)
	return (gcrRouterErrors);

    gcrSetEndDist(ch);
    density = gcrDensity(ch);
/*  gcrPrDensity(ch, density);	/* Debugging */
    if (density > ch->gcr_width)
    {
	(void) sprintf(mesg, "Density (%d) > channel size (%d)",
	    density, ch->gcr_width);
	RtrChannelError(ch, ch->gcr_width, ch->gcr_length, mesg, NULL);
    }

    gcrInitCollapse(ch->gcr_width + 2);
    gcrSetFlags(ch);

    /* Process the first column */
    gcrInitCol(ch, ch->gcr_lPins);
    gcrExtend(ch, 0);
    gcrPrintCol(ch, 0, GcrShowResult);

    /* Process subsequent columns */
    for (i = 1; i <= ch->gcr_length; i++)
    {
	if (SigInterruptPending)
	    goto bottom;
	gcrRouteCol(ch, i);
    }

    /* Process errors at the end */
    col = ch->gcr_lCol;
    pin = ch->gcr_rPins;
    for (i = 1; i <= ch->gcr_width; i++, col++, pin++)
	if (col->gcr_h != pin->gcr_pId)
	{
	    netId = col->gcr_h ? col->gcr_h->gcr_Id : pin->gcr_pId->gcr_Id;
	    RtrChannelError(ch, ch->gcr_length, i,
			"Can't make end connection", netId);
	    gcrRouterErrors++;
	}

bottom:
    /* For debugging: print channel on screen */
    gcrDumpResult(ch, GcrShowEnd);

    /*
     * We have to free up the nets here, since callers may re-arrange
     * the channel and cause the net structure to become invalid
     * anyway.
     */
    for (net = ch->gcr_nets; net; net = net->gcr_next)
	freeMagic((char *) net);
    ch->gcr_nets = NULL;

    return (gcrRouterErrors);
}

/*
 * ----------------------------------------------------------------------------
 *
 * gcrRouteCol --
 *
 * Route the given column in the channel.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets flags in the channel structure to show where routing
 *	is to be placed.
 *
 * ----------------------------------------------------------------------------
 */

void
gcrRouteCol(ch, indx)
    GCRChannel *ch;
    int indx;		/* Index of column being routed. */
{
    GCRNet **gcrClassify(), **list;
    GCRColEl *col;
    int count;

    /* Make feasible top and bottom connections */
    gcrCheckCol(ch, indx, "Start of gcrRouteCol");
    gcrFeasible(ch, indx);
    gcrCheckCol(ch, indx, "After feasible connections");

    /* Here I should vacate terminating tracks */
    if (GCRNearEnd(ch, indx) &&
	    (GCREndDist < ch->gcr_length || !GCRNearEnd(ch, indx - 1)))
	gcrMarkWanted(ch);

    /* Collapse split nets in the pattern that frees the most tracks */
    gcrCollapse(&ch->gcr_lCol, ch->gcr_width, 1, ch->gcr_width, 0);
    gcrPickBest(ch);
    gcrCheckCol(ch, indx, "After collapse");

    col = ch->gcr_lCol;

    /* Reduce the range of split nets */
    gcrReduceRange(col, ch->gcr_width);
    gcrCheckCol(ch, indx, "After reducing range of split nets");

    /* Vacate obstructed tracks.  Split to make multiple end connections */
    gcrVacate(ch, indx);

    /* Raise rising and lower falling nets */
    list = gcrClassify(ch, &count);
    gcrCheckCol(ch, indx, "After classifying nets");
    gcrMakeRuns(ch, indx, list, count, TRUE);
    gcrCheckCol(ch, indx, "After making rising/falling runs");

    gcrCheckCol(ch, indx, "After vacating");
    if (GCRNearEnd(ch, indx))
    {
	gcrUncollapse(ch, &ch->gcr_lCol, ch->gcr_width, 1, ch->gcr_width, 0);
	gcrPickBest(ch);
    }
    gcrCheckCol(ch, indx, "After uncollapse");

    /* Extend active tracks to the next column.  Place contacts */
    gcrExtend(ch, indx);
    gcrCheckCol(ch, indx, "After widen and extend");
    gcrPrintCol(ch, indx, GcrShowResult);
}

/*
 * ----------------------------------------------------------------------------
 *
 * gcrExtend --
 *
 * Extend dangling wires to the next column.
 * Don't extend off the end of the channel if the wrong connection
 * would be made.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets bits in the result array for the channel.  Where there
 *	are blockages in the next column, adds contacts to blocked
 *	tracks for a layer switch.  Clears the vertical wiring for
 *	the new column.
 *
 * ----------------------------------------------------------------------------
 */

void
gcrExtend(ch, currentCol)
    GCRChannel *ch;	/* Channel being routed */
    int currentCol;	/* Column that has just been completed */
{
    short *res = ch->gcr_result[currentCol];
    GCRColEl *col = ch->gcr_lCol;
    short *prev = (short *) NULL, *next = (short *) NULL;
    bool hasNext, hasPrev;
    int i;

    ASSERT(ch->gcr_result, "gcrExtend: ");
    if (currentCol > 0) prev = ch->gcr_result[currentCol - 1];
    if (currentCol <= ch->gcr_length) next = ch->gcr_result[currentCol + 1];

    /*
     * Consider each track, including the pseudo-track at the
     * bottom (0) of the channel, but not the one at the top
     * (ch->gcr_width).
     */
    for (i = 0; i <= ch->gcr_width; i++)
    {
	if (col[1].gcr_v == col->gcr_v && col->gcr_v)
	{
	    /* Track extends upwards */
	    res[0] |= GCRU;
	    if (i == ch->gcr_width) res[1] |= GCRU;
	    if (col->gcr_flags & GCRCC) res[0] |= GCRX;
	    if (col[1].gcr_flags & GCRCC) res[1] |= GCRX;
	}

	/* Don't process track if not occupied by a real net */
	hasPrev = prev && (*prev & GCRR);
	if (col->gcr_h == (GCRNet *) NULL)
	{
	    if (currentCol == 0) res[0] &= ~GCRR;
	    if (hasPrev) res[0] |= GCRX;
	    col->gcr_v = 0;
	}
	else
	{
	    /* Extend net if split or another pin exists in this channel */
	    hasNext =  col->gcr_hi != EMPTY
		    || col->gcr_lo != EMPTY
		    || GCRPin1st(col->gcr_h);

	    if (col->gcr_v == col->gcr_h && (hasPrev || hasNext))
		res[0] |= GCRX;

	    /* Clear vertical wiring */
	    col->gcr_v = 0;

	    /* Terminate unsplit nets with no pins after the current column */
	    if (!hasNext) col->gcr_h = (GCRNet *) NULL;
	    else if (col->gcr_flags & GCRTE)
	    {
		/*
		 * If the track should be extended but can't due to a
		 * hard obstacle, then print a message and terminate it.
		 */
		RtrChannelError(ch, currentCol, i,
		    "Can't extend track through obstacle", col->gcr_h->gcr_Id);
		gcrRouterErrors++;
		col->gcr_h = (GCRNet *) NULL;
	    }
	    else if (currentCol == ch->gcr_length && i
			&& ch->gcr_rPins[i].gcr_pId == (GCRNet *) NULL)
	    {
		/* If track about to make a bad connection, don't extend */
		RtrChannelError(ch, currentCol, i,
		    "Can't extend track to bad connection", col->gcr_h->gcr_Id);
		col->gcr_h = (GCRNet *) NULL;
		gcrRouterErrors++;
	    }
	    else
	    {
		/* Extend the net into the next column */
		res[0] |= GCRR;
		if (currentCol == ch->gcr_length) *next |= GCRR;
	    }

	    /* Contact in next col if GCRTC */
	    if (*next & GCRTC) col->gcr_v = col->gcr_h;
	}

	if (prev) prev++;
	if (next) col->gcr_flags = *next++;
	else col->gcr_flags= 0;
	res++;
	col++;
    }

    col->gcr_v = 0;
    col->gcr_flags = 0;
}
