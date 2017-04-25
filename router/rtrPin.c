/*
 * rtrPin.c --
 *
 * Code for linking together the pins in adjacent channels.
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
 *		      Lawrence Livermore National Laboratory
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/router/rtrPin.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/styles.h"
#include "utils/hash.h"
#include "utils/heap.h"
#include "debug/debug.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "gcr/gcr.h"
#include "windows/windows.h"
#include "utils/main.h"
#include "dbwind/dbwind.h"
#include "utils/signals.h"
#include "router/router.h"
#include "grouter/grouter.h"
#include "textio/textio.h"

/* Forward declarations */
bool rtrPinArrayBlock();
void rtrPinShow(GCRPin *);


/*
 * ----------------------------------------------------------------------------
 *
 * RtrPinsInit --
 *
 * Initialize the global-router specific information for the pins
 * around the periphery of the channel 'ch':
 *
 *	gcr_ch		Points back to the channel.
 *	gcr_cost	Set to INFINITY.
 *	gcr_side	Set to the side of the channel to which this pin
 *			belongs (GEO_NORTH, GEO_SOUTH, etc).
 *	gcr_point	Set to the absolute (edit cell) coordinates
 *			of the crossing point associated with this pin.
 *	gcr_linked	Points to the pin on the other side of this pin's
 *			crossing, or is NULL if there is no other pin.
 *			The gcr_linked field is set to NULL if crossing
 *			from a pin to the neighboring channel would exit
 *			a river-routing channel from an illegal side.
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
RtrPinsInit(ch)
    GCRChannel *ch;
{
    rtrPinArrayInit(ch, GEO_NORTH, ch->gcr_tPins, ch->gcr_length);
    rtrPinArrayInit(ch, GEO_SOUTH, ch->gcr_bPins, ch->gcr_length);
    rtrPinArrayInit(ch, GEO_WEST, ch->gcr_lPins, ch->gcr_width);
    rtrPinArrayInit(ch, GEO_EAST, ch->gcr_rPins, ch->gcr_width);
}

int
rtrPinArrayInit(ch, side, pins, nPins)
    GCRChannel *ch;
    GCRPin *pins;
    int nPins;
{
    GCRPin *pin, *linked;
    GCRChannel *adjacent;
    GCRPin *lastPin;
    Point point, p;
    bool markLinked;
    int otherSide;
    Tile *tp;

    /*
     * If exiting this channel from an illegal side, all gcr_linked fields
     * are set to NULL.
     */
    markLinked = TRUE;
    switch (side)
    {
	case GEO_EAST:
	case GEO_WEST:
	    if (ch->gcr_type == CHAN_VRIVER)
		markLinked = FALSE;
	    break;
	case GEO_NORTH:
	case GEO_SOUTH:
	    if (ch->gcr_type == CHAN_HRIVER)
		markLinked = FALSE;
	    break;
    }

    lastPin = &pins[nPins + 1];
    for (pin = pins; pin <= lastPin; pin++)
    {
	pin->gcr_ch = ch;
	pin->gcr_side = side;
	pin->gcr_cost = INFINITY;
	pin->gcr_linked = (GCRPin *) NULL;
	otherSide = GeoOppositePos[side];

	/* Figure out the crossing point */
	switch (side)
	{
	    case GEO_WEST:
		point.p_y = ch->gcr_origin.p_y + pin->gcr_y * RtrGridSpacing;
		point.p_x = ch->gcr_area.r_xbot;
		break;
	    case GEO_SOUTH:
		point.p_x = ch->gcr_origin.p_x + pin->gcr_x * RtrGridSpacing;
		point.p_y = ch->gcr_area.r_ybot;
		break;
	    case GEO_EAST:
		point.p_y = ch->gcr_origin.p_y + pin->gcr_y * RtrGridSpacing;
		point.p_x = ch->gcr_area.r_xtop;
		break;
	    case GEO_NORTH:
		point.p_x = ch->gcr_origin.p_x + pin->gcr_x * RtrGridSpacing;
		point.p_y = ch->gcr_area.r_ytop;
		break;
	    default:
		ASSERT(FALSE, "bad pin side in rtrInitPinArray");
		break;
	}
	pin->gcr_point = point;

	/* Don't set gcr_linked if exiting the channel from an illegal side */
	if (!markLinked)
	    continue;

	if (pin > pins && pin < lastPin)
	{
	    /*
	     * Find the adjacent channel at that point.
	     * If one exists, find the pin in it that shares
	     * this pin's crossing.
	     */
	    p = point;
	    if (side == GEO_WEST) p.p_x--;
	    if (side == GEO_SOUTH) p.p_y--;
	    tp = TiSrPointNoHint(RtrChannelPlane, &p);
	    if (adjacent = (GCRChannel *) tp->ti_client)
	    {
		/* Only link if entering the linked channel from a legal side */
		linked = RtrPointToPin(adjacent, otherSide, &point);
		switch (side)
		{
		    case GEO_EAST:
		    case GEO_WEST:
			if (adjacent->gcr_type != CHAN_VRIVER)
			    pin->gcr_linked = linked;
			break;
		    case GEO_NORTH:
		    case GEO_SOUTH:
			if (adjacent->gcr_type != CHAN_HRIVER)
			    pin->gcr_linked = linked;
			break;
		}
	    }
	}
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * RtrPointToPin --
 *
 * Given a point somewhere on the perimeter of a channel, determine
 * which pin it refers to.
 *
 * Results:
 *	Pointer to the pin struct whose location is given by pt.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

GCRPin *
RtrPointToPin(ch, side, point)
    GCRChannel *ch;	/* The channel containing the point */
    int side;			/* Side of ch that point lies on */
    Point *point;	/* The point to be converted to a pin */
{
    int coord;

    switch (side)
    {
	case GEO_NORTH:
	    coord = (point->p_x - ch->gcr_origin.p_x) / RtrGridSpacing;
	    ASSERT(coord <= ch->gcr_length && coord >= 1, "RtrPointToPin");
	    return &ch->gcr_tPins[coord];
	    break;

	case GEO_SOUTH:
	    coord = (point->p_x - ch->gcr_origin.p_x) / RtrGridSpacing;
	    ASSERT(coord <= ch->gcr_length && coord >= 1, "RtrPointToPin");
	    return &ch->gcr_bPins[coord];
	    break;

	case GEO_EAST:
	    coord = (point->p_y - ch->gcr_origin.p_y) / RtrGridSpacing;
	    ASSERT(coord <= ch->gcr_width && coord >= 1, "RtrPointToPin");
	    return &ch->gcr_rPins[coord];
	    break;

	case GEO_WEST:
	    coord = (point->p_y - ch->gcr_origin.p_y) / RtrGridSpacing;
	    ASSERT(coord <= ch->gcr_width && coord >= 1, "RtrPointToPin");
	    return &ch->gcr_lPins[coord];
	    break;
    }

    /*
     * The pin is not on a side of the channel.
     * Recover by returning a strange value which should
     * not hurt anything.
     */
    ASSERT(FALSE, "Pin not on side of channel in RtrPointToPin");
    return &ch->gcr_lPins[0];
}

/*
 * ----------------------------------------------------------------------------
 *
 * RtrPinsBlock --
 *
 * Propagate blocked pin information.  If a pin is blocked on one side
 * of a channel BOUNDARY, it is blocked on the other side as well.  If
 * a pin on one side of a river-routing CHANNEL is blocked, the pin on
 * the other side gets blocked too.  Several iterations may be necessary
 * to propagate blockages across all channel boundaries and river-routing
 * channels.
 *
 * Also propagate obstacle flags for pins.  If a pin's obstacle flags
 * contain GCROBST, then set this bit in gcr_linked's flags as well.
 * 
 * Results:
 *	TRUE if the blockage state of any pins changed, FALSE
 *	otherwise.  The caller should iterate over all channels
 *	until no pins change any more.
 *
 * Side effects:
 *	Propagating a blockage to a pin consists of marking its
 *	gcr_pId with GCR_BLOCKEDNETID, and setting the GCRBLK flag
 *	in its gcr_pFlags (this latter only happens for gcr_linked
 *	pins, not for pins on the opposite side of a river-routing
 *	channel from a blocked pin).
 *
 * ----------------------------------------------------------------------------
 */

bool
RtrPinsBlock(ch)
    GCRChannel *ch;
{
    bool changed;

    changed = FALSE;
    if (rtrPinArrayBlock(ch, ch->gcr_tPins, ch->gcr_bPins, ch->gcr_length))
	changed = TRUE;
    if (rtrPinArrayBlock(ch, ch->gcr_bPins, ch->gcr_tPins, ch->gcr_length))
	changed = TRUE;
    if (rtrPinArrayBlock(ch, ch->gcr_lPins, ch->gcr_rPins, ch->gcr_width))
	changed = TRUE;
    if (rtrPinArrayBlock(ch, ch->gcr_rPins, ch->gcr_lPins, ch->gcr_width))
	changed = TRUE;

    return changed;
}

bool
rtrPinArrayBlock(ch, pins, opins, nPins)
    GCRChannel *ch;	/* Channel pins belong to */
    GCRPin *pins;	/* Processing this side of channel */
    GCRPin *opins;	/* Pins on opposite side; used only if ch is a
			 * river-routing channel.
			 */
    int nPins;		/* Number of internal pins (not counting pins[0]) */
{
    bool changed, isRiver = (ch->gcr_type != CHAN_NORMAL);
    GCRPin *pin, *opin, *linked;
    GCRPin *lastPin;

    changed = FALSE;
    lastPin = &pins[nPins];
    for (pin = pins + 1, opin = opins + 1; pin <= lastPin; pin++, opin++)
    {
	linked = pin->gcr_linked;

	/* Propagate blockages */
	if (pin->gcr_pId == GCR_BLOCKEDNETID)
	{
	    if (linked && linked->gcr_pId == (GCRNet *) NULL)
	    {
		linked->gcr_pFlags |= GCRBLK;
		linked->gcr_pId = GCR_BLOCKEDNETID;
		changed = TRUE;
	    }
	    if (isRiver && opin->gcr_pId == (GCRNet *) NULL)
		opin->gcr_pId = GCR_BLOCKEDNETID, changed = TRUE;
	}

	/* Propagate obstacle flags */
	if ((pin->gcr_pFlags & GCROBST) && linked)
	    linked->gcr_pFlags |= GCROBST;
    }

    return changed;
}

/*
 * ----------------------------------------------------------------------------
 *
 * RtrPinsLink --
 *
 * Link the "usable" pins along each side of a channel into a doubly-linked
 * list headed by the zero-th pin along that side.  Usable pins (for purposes
 * of global routing) are those that are unassigned (gcr_pId == 0) and that
 * have gcr_linked != NULL.
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
RtrPinsLink(ch)
    GCRChannel *ch;
{
    rtrPinArrayLink(ch->gcr_tPins, ch->gcr_length);
    rtrPinArrayLink(ch->gcr_bPins, ch->gcr_length);
    rtrPinArrayLink(ch->gcr_lPins, ch->gcr_width);
    rtrPinArrayLink(ch->gcr_rPins, ch->gcr_width);
}

int
rtrPinArrayLink(pins, nPins)
    GCRPin *pins;
    int nPins;
{
    GCRPin *pin, *lastPin, *lastValid;

    lastPin = &pins[nPins];
    lastValid = pins;
    lastValid->gcr_pPrev = lastValid->gcr_pNext = (GCRPin *) NULL;
    for (pin = pins + 1; pin <= lastPin; pin++)
    {
	pin->gcr_pNext = pin->gcr_pPrev = (GCRPin *) NULL;
	if (pin->gcr_linked && pin->gcr_pId == (GCRNet *) NULL)
	{
	    lastValid->gcr_pNext = pin;
	    pin->gcr_pPrev = lastValid;
	    lastValid = pin;
	}
	if (DebugIsSet(glDebugID, glDebShowPins))
	    rtrPinShow(pin);
    }
    return 0;
}

void rtrPinShow(pin)
    GCRPin *pin;
{
    char mesg[256];
    Rect r, area;
    Point p;

    p = pin->gcr_point;
    switch (pin->gcr_side)
    {
	case GEO_NORTH:
	    p.p_y = RTR_GRIDDOWN(p.p_y, RtrOrigin.p_y);
	    break;
	case GEO_SOUTH:
	    p.p_y = RTR_GRIDUP(p.p_y, RtrOrigin.p_y);
	    break;
	case GEO_EAST:
	    p.p_x = RTR_GRIDDOWN(p.p_x, RtrOrigin.p_x);
	    break;
	case GEO_WEST:
	    p.p_x = RTR_GRIDUP(p.p_x, RtrOrigin.p_x);
	    break;
    }

    r.r_ll = r.r_ur = p;
    r.r_xtop += 4, r.r_ytop += 4;

    area = pin->gcr_ch->gcr_area;
    (void) sprintf(mesg,
	"ChanType=%d grid=(%d,%d) point=(%d,%d) Net=%"DLONG_PREFIX"d, linked=%p",
		pin->gcr_ch->gcr_type,
		pin->gcr_x, pin->gcr_y,
		pin->gcr_point.p_x, pin->gcr_point.p_y,
		(dlong) pin->gcr_pId, pin->gcr_linked);
    if (pin->gcr_pId || pin->gcr_linked == NULL)
	(void) strcat(mesg, " **BLOCKED**");
    else DBWFeedbackAdd(&r, mesg, EditCellUse->cu_def, 1, STYLE_PALEHIGHLIGHTS);
#ifdef	notdef
    ShowRect(EditCellUse->cu_def, &r, STYLE_SOLIDHIGHLIGHTS);
    TxMore(mesg);
    ShowRect(EditCellUse->cu_def, &r, STYLE_ERASEHIGHLIGHTS);
#endif	/* notdef */
}

/*
 * ----------------------------------------------------------------------------
 *
 * RtrPinsFixStems --
 *
 * Only some of the crossing points marked during stem assignment were
 * actually used during global routing.  The unused ones have gcr_pSeg
 * equal to GCR_STEMSEGID and must be unmarked here (otherwise the channel
 * router will try to route them!).
 * 
 * Results:
 *	None.
 *
 * Side effects:
 *	For each pin on the boundary of 'ch' that is marked as in use
 *	(gcr_pId != 0 && gcr_pId != GCR_BLOCKEDNETID) and that has a segment
 *	id of GCR_STEMSEGID (gcr_pSeg == GCR_STEMSEGID), mark it as unused
 *	(gcr_pId == 0).
 *
 * ----------------------------------------------------------------------------
 */

void
RtrPinsFixStems(ch)
    GCRChannel *ch;
{
    rtrPinArrayFixStems(ch->gcr_tPins, ch->gcr_length);
    rtrPinArrayFixStems(ch->gcr_bPins, ch->gcr_length);
    rtrPinArrayFixStems(ch->gcr_lPins, ch->gcr_width);
    rtrPinArrayFixStems(ch->gcr_rPins, ch->gcr_width);
}

int
rtrPinArrayFixStems(pins, nPins)
    GCRPin *pins;
    int nPins;
{
    GCRPin *pin, *lastPin;

    lastPin = &pins[nPins];
    for (pin = pins + 1; pin <= lastPin; pin++)
	if (pin->gcr_pId
		&& pin->gcr_pId != GCR_BLOCKEDNETID
		&& pin->gcr_pSeg == GCR_STEMSEGID)
	{
	    pin->gcr_pId = (GCRNet *) NULL;
	}
    return 0;
}
