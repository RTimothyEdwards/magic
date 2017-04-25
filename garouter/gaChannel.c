/*
 * gaChannel.c -
 *
 * Channel management for the gate-array router.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/garouter/gaChannel.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/hash.h"
#include "utils/heap.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "windows/windows.h"
#include "utils/main.h"
#include "dbwind/dbwind.h"
#include "utils/signals.h"
#include "netmenu/netmenu.h"
#include "gcr/gcr.h"
#include "router/router.h"
#include "grouter/grouter.h"
#include "garouter/garouter.h"
#include "utils/netlist.h"
#include "textio/textio.h"
#include "utils/styles.h"
#include "debug/debug.h"

/* List of all active channels */
GCRChannel *gaChannelList = NULL;

/* Def used to hold channel plane */
CellDef *gaChannelDef = NULL;

/* Forward declarations */
int gaSplitTile();
int gaSetClient();
void gaChannelStats();
void gaPinStats();
void gaPropagateBlockages();
void gaInitRiverBlockages();

#define	CNULL	((ClientData) NULL)

int gaTotNormCross, gaTotRiverCross, gaClearNormCross, gaClearRiverCross;


/*
 * ----------------------------------------------------------------------------
 *
 * GAChannelInitOnce --
 *
 * Once-only channel initialization for the gate-array router.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates the tile plane used to hold channel information,
 *	RtrChannelPlane.
 *
 * ----------------------------------------------------------------------------
 */

void
GAChannelInitOnce()
{
    if (gaChannelDef == NULL)
	gaChannelDef = RtrFindChannelDef();
    RtrChannelPlane = gaChannelDef->cd_planes[PL_DRC_ERROR];
    GAClearChannels();
}

/*
 * ----------------------------------------------------------------------------
 *
 * GAClearChannels --
 *
 * Clear any pre-existing channels in preparation for definition of
 * new ones.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Paints TileType 1 (blocked) over the tile plane that records all
 *	available channel area, and frees all channel structures.
 *
 * ----------------------------------------------------------------------------
 */

void
GAClearChannels()
{
    GCRChannel *ch;
    Rect r;

    r.r_xbot = TiPlaneRect.r_xbot / 2;
    r.r_ybot = TiPlaneRect.r_ybot / 2;
    r.r_xtop = TiPlaneRect.r_xtop / 2;
    r.r_ytop = TiPlaneRect.r_ytop / 2;
    SigDisableInterrupts();
    (void) DBPaintPlane(RtrChannelPlane, &r, DBStdWriteTbl(1),
		    (PaintUndoInfo *) NULL);
    for (ch = gaChannelList; ch; ch = ch->gcr_next)
	GCRFreeChannel(ch);
    gaChannelList = (GCRChannel *) NULL;
    SigEnableInterrupts();
}

/*
 * ----------------------------------------------------------------------------
 *
 * GADefineChannel --
 *
 * Add a new channel definition to the list of channels we know
 * about.  First ensure that the channel boundaries are correctly
 * aligned (round down if they aren't) and also that the area of
 * this channel has not already been assigned to another channel.
 *
 * The parameter chanType is one of 0 (for an ordinary channel),
 * CHAN_HRIVER (for a horizontal river-routing channel), or CHAN_VRIVER
 * (for a vertical river-routing channel).
 *
 * The rectangle 'r' gives the area of the channel.
 *
 * Results:
 *	TRUE on success, FALSE on failure.
 *
 * Side effects:
 *	Paints the area of this channel into RtrChannelPlane (as space)
 *	to mark that this area is used.
 *
 * ----------------------------------------------------------------------------
 */

bool
GADefineChannel(chanType, r)
    int chanType;
    Rect *r;
{
    int length, width, halfGrid = RtrGridSpacing / 2;
    GCRChannel *ch;
    Point origin;
    Rect r2;

    /*
     * Make the channel boundaries lie halfway between grid lines.
     * To ensure consistent results when RtrGridSpacing is odd,
     * we always subtract halfGrid from a grid line.
     */
    r2 = *r;
    r->r_xbot = RTR_GRIDUP(r->r_xbot, RtrOrigin.p_x) - halfGrid;
    r->r_ybot = RTR_GRIDUP(r->r_ybot, RtrOrigin.p_y) - halfGrid;
    r->r_xtop = RTR_GRIDDOWN(r->r_xtop, RtrOrigin.p_x) + RtrGridSpacing
		- halfGrid;
    r->r_ytop = RTR_GRIDDOWN(r->r_ytop, RtrOrigin.p_y) + RtrGridSpacing
		- halfGrid;

    if (r2.r_xbot != r->r_xbot || r2.r_ybot != r->r_ybot
	    || r2.r_xtop != r->r_xtop || r2.r_ytop != r->r_ytop)
    {
	TxPrintf("Rounding channel to center-grid alignment: ");
	TxPrintf("ll=(%d,%d) ur=(%d,%d)\n",
		r->r_xbot, r->r_ybot, r->r_xtop, r->r_ytop);
    }

    /* Ensure no overlap */
    if (DBSrPaintArea((Tile *) NULL, RtrChannelPlane, r, &DBSpaceBits,
	    gaAlwaysOne, (ClientData) NULL))
    {
	TxError("Channel ll=(%d,%d) ur=(%d,%d) overlaps existing channels\n",
		r->r_xbot, r->r_ybot, r->r_xtop, r->r_ytop);
	return (FALSE);
    }

    if (DebugIsSet(gaDebugID, gaDebShowChans))
	DBWFeedbackAdd(r, "Channel area", EditCellUse->cu_def,
		    1, STYLE_OUTLINEHIGHLIGHTS);

    /* Paint it into the channel plane */
    SigDisableInterrupts();
    (void) DBPaintPlane(RtrChannelPlane, r, DBStdWriteTbl(TT_SPACE),
		    (PaintUndoInfo *) NULL);

    /* Allocate the new channel */
    RtrChannelBounds(r, &length, &width, &origin);
    ch = GCRNewChannel(length, width);
    ch->gcr_area = *r;
    ch->gcr_origin = origin;
    ch->gcr_type = chanType;

    /* Link it in with the pre-existing list */
    ch->gcr_next = gaChannelList;
    gaChannelList = ch;
    SigEnableInterrupts();

    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * gaChannelInit --
 *
 * Called immediately prior to routing to set up lots of useful
 * information in channels:
 *
 * Tile plane initialization:
 *	- Carve up the tile plane so each channel corresponds to a
 *	  single space tile, and unavailable areas correspond to
 *	  tiles of type 1.
 *	- Make the ti_client fields of tiles in the channel plane
 *	  point to their corresponding channels.
 *
 * Obstacle initialization:
 *	- Initialize the obstacle and hazard maps for each channel.
 *	  Identify pins too close to two-layer blockages and mark
 *	  them as already taken (by net GCR_BLOCKEDNETID).
 *	- For river-routing channels, ensure that pairs of pins
 *	  can be river-routed to each other; if not, mark both
 *	  as unavailable.
 *
 * Pin initialization (including stem assignment):
 *	- Fill in the global-routing information in the channel
 *	  structure.  Specifically, set gcr_ch, gcr_side, gcr_linked,
 *	  and gcr_point for each GCRPin.  Link available pins for
 *	  each side of the channel into a doubly-linked list so
 *	  they can be visited easily during global routing.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Fills in a lot of information in the channel structures.
 *
 * ----------------------------------------------------------------------------
 */

void
gaChannelInit(list, routeUse, netList)
    GCRChannel *list;	/* List of channels to process */
    CellUse *routeUse;	/* Cell being routed (searched for obstacles) */
    NLNetList *netList;	/* Netlist being routed */
{
    GCRChannel *ch;

    RtrMilestoneStart("Obstacle map initialization");
    for (ch = list; ch && !SigInterruptPending; ch = ch->gcr_next)
    {
	/* Ensure that no channel tiles cross channel boundaries */
	while (DBSrPaintArea((Tile *) NULL, RtrChannelPlane, &ch->gcr_area,
		&DBAllTypeBits, gaSplitTile, (ClientData) &ch->gcr_area))
	    /* Nothing */;

	/* Obstacle initialization */
	RtrMilestonePrint();
	RtrChannelObstacles(routeUse, ch);
	if (ch->gcr_type == CHAN_NORMAL)
	    RtrChannelDensity(ch);
	RtrChannelCleanObstacles(ch);
    }
    RtrMilestoneDone();

    /*
     * Set all ti_client fields in the channel plane to NULL, and then
     * for each channel set the ti_client fields of the tiles it overlaps
     * to point back to that channel.
     */
    (void) DBSrPaintArea((Tile *) NULL, RtrChannelPlane, &TiPlaneRect,
		    &DBAllTypeBits, gaSetClient, (ClientData) NULL);
    for (ch = list; ch && !SigInterruptPending; ch = ch->gcr_next)
	(void) DBSrPaintArea((Tile *) NULL, RtrChannelPlane, &ch->gcr_area,
		    &DBAllTypeBits, gaSetClient, (ClientData) ch);
    if (SigInterruptPending)
	return;

    /*
     * Pin initialization.
     * This fills in a bunch of information needed for global
     * routing in each pin along each channel boundary.
     */
    for (ch = list; ch && !SigInterruptPending; ch = ch->gcr_next)
	RtrPinsInit(ch);

    /*
     * Pick the actual pin locations for terminals.
     * This comes here because it needs the blockage and gcr_linked
     * information set above.  However, it must precede marking
     * pairs of river-routing crossings as unavailable, since
     * these pairs are only marked unavailable for routes across
     * the channel.  They may still be usable as stem tips, so
     * we can't block them until after stems have been assigned.
     */
    gaStemAssignAll(routeUse, netList);
    if (SigInterruptPending)
	return;

    /*
     * Now mark pairs of river-routing crossings as unavailable.
     * A crossing is unavailable if a straight-across from one side
     * of the channel to the other on a single layer is impossible.
     * Propagate these blockages..
     */
    for (ch = list; ch && !SigInterruptPending; ch = ch->gcr_next)
	if (ch->gcr_type != CHAN_NORMAL)
	    gaInitRiverBlockages(routeUse, ch);
    gaPropagateBlockages(list);
    if (SigInterruptPending)
	return;

    /*
     * Hazard initialization.
     * This has to happen after blockages have been propagated.
     */
    RtrMilestoneStart("Hazard initialization");
    for (ch = list; ch && !SigInterruptPending; ch = ch->gcr_next)
	if (ch->gcr_type == CHAN_NORMAL)
	{
	    RtrHazards(ch);
	    RtrMilestonePrint();
	}
    RtrMilestoneDone();

    /*
     * Link all the available pins along each side of each
     * channel into a linked list.  Only unblocked pins with
     * non-NULL gcr_linked pins are linked into this list.
     * Since pins taken as stem tips by gaAssignPins above
     * were marked, these lists don't include any pins that
     * are stem tips.
     */
    for (ch = list; ch && !SigInterruptPending; ch = ch->gcr_next)
	RtrPinsLink(ch);
    if (DebugIsSet(gaDebugID, gaDebChanStats))
	gaChannelStats(list);
}

void
gaChannelStats(list)
    GCRChannel *list;
{
    GCRChannel *ch;
    int *tot, *clear, numTot, numClear;
    double fNorm, fRiver, fTot;

    gaTotNormCross = 0;
    gaTotRiverCross = 0;
    gaClearNormCross = 0;
    gaClearRiverCross = 0;

    for (ch = list; ch; ch = ch->gcr_next)
    {
	switch (ch->gcr_type)
	{
	    case CHAN_NORMAL:
		tot = &gaTotNormCross;
		clear = &gaClearNormCross;
		break;
	    case CHAN_HRIVER:
	    case CHAN_VRIVER:
		tot = &gaTotRiverCross;
		clear = &gaClearRiverCross;
		break;
	}
	gaPinStats(ch->gcr_tPins, ch->gcr_length, tot, clear);
	gaPinStats(ch->gcr_bPins, ch->gcr_length, tot, clear);
	gaPinStats(ch->gcr_lPins, ch->gcr_width, tot, clear);
	gaPinStats(ch->gcr_rPins, ch->gcr_width, tot, clear);
    }

    numTot = gaTotRiverCross + gaTotNormCross;
    numClear = gaClearRiverCross + gaClearNormCross;
    fRiver = ((double) gaClearRiverCross / (double) gaTotRiverCross) * 100.0;
    fNorm = ((double) gaClearNormCross / (double) gaTotNormCross) * 100.0;
    fTot = ((double) numClear / (double) numTot) * 100.0;
    TxPrintf("Total pins: %d, clear: %d (%.1f%%)\n", numTot, numClear, fTot);
    TxPrintf("Norm chan pins: %d, clear: %d (%.1f%%)\n", gaTotNormCross,
		gaClearNormCross, fNorm);
    TxPrintf("River chan pins: %d, clear: %d (%.1f%%)\n", gaTotRiverCross,
		gaClearRiverCross, fRiver);
}

void
gaPinStats(pins, nPins, pTot, pClear)
    GCRPin *pins;
    int nPins;
    int *pTot, *pClear;
{
    GCRPin *pin, *pend;

    pin = &pins[1];
    pend = &pins[nPins];
    for ( ; pin <= pend; pin++)
    {
	*pTot += 1;
	if (pin->gcr_linked && pin->gcr_pId == (GCRNet *) NULL
		&& pin->gcr_linked->gcr_pId == (GCRNet *) NULL)
	{
	    *pClear += 1;
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * gaPropagateBlockages --
 *
 * If a pin is blocked on one side of a channel BOUNDARY,
 * it is blocked on the other side as well.  If a pin on
 * one side of a river-routing CHANNEL is blocked, the pin
 * on the other side gets blocked too.  Several iterations
 * may be necessary to propagate blockages across all
 * channel boundaries and river-routing channels.
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
gaPropagateBlockages(list)
    GCRChannel *list;
{
    GCRChannel *ch;
    bool changed;

    do
    {
	changed = FALSE;
	for (ch = list; ch; ch = ch->gcr_next)
	    if (RtrPinsBlock(ch))
		changed = TRUE;
    } while (changed);
}

/*
 * ----------------------------------------------------------------------------
 *
 * gaSetClient --
 *
 * Called for each tile overlapping ch->gcr_area to set the
 * client pointer for that tile to 'cdata'.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

int
gaSetClient(tile, cdata)
    Tile *tile;
    ClientData cdata;
{
    tile->ti_client = (ClientData) cdata;
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * gaSplitTile --
 *
 * Called for each tile overlapping the area of a channel (the Rect 'r'),
 * we ensure that no tiles cross the channel boundary.  If one does, we
 * split it at the boundary and return 1.
 *
 * Results:
 *	Returns 1 if tile was split, or 0 if no split occurred.
 *	We return 1 to ensure that DBSrPaintArea doesn't get
 *	confused from our having modified the tile plane.
 *
 * Side effects:
 *	May split 'tile'; if it does, we return 1.
 *
 * ----------------------------------------------------------------------------
 */

int
gaSplitTile(tile, r)
    Tile *tile;
    Rect *r;
{
    Tile *tp;
    ASSERT(TiGetType(tile) == TT_SPACE, "gaSplitTile");

    if (TOP(tile) > r->r_ytop)
    {
	tp = TiSplitY(tile, r->r_ytop);
	TiSetBody(tp, TT_SPACE);
	return (1);
    }
    if (BOTTOM(tile) < r->r_ybot)
    {
	tp = TiSplitY(tile, r->r_ybot);
	TiSetBody(tp, TT_SPACE);
	return (1);
    }
    if (LEFT(tile) < r->r_xbot)
    {
	tp = TiSplitX(tile, r->r_xbot);
	TiSetBody(tp, TT_SPACE);
	return (1);
    }
    if (RIGHT(tile) > r->r_xtop)
    {
	tp = TiSplitX(tile, r->r_xtop);
	TiSetBody(tp, TT_SPACE);
	return (1);
    }
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * gaInitRiverBlockages --
 *
 * The channel 'ch' is a river-routing channel (ch->gcr_type is either
 * CHAN_HRIVER or CHAN_VRIVER).  These channels can only be routed
 * across in a single direction (horizontally for CHAN_HRIVER, or
 * vertically for CHAN_VRIVER).
 *
 * If pins have already been taken by terminals (as stem tips), we
 * don't mark them as blocked.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Marks pairs of pins on the channel boundary as blocked if
 *	a river-route across the channel is not possible along
 *	the track that runs between the pin pair.  Blocked pins
 *	have a special gcr_pId of GCR_BLOCKEDNETID to mark them as
 *	"in use".
 *
 * ----------------------------------------------------------------------------
 */

void
gaInitRiverBlockages(routeUse, ch)
    CellUse *routeUse;
    GCRChannel *ch;
{
    GCRPin *p1, *p2;
    int n, nPins, coord;
    SearchContext scx;

    switch (ch->gcr_type)
    {
	case CHAN_HRIVER:
	    p1 = &ch->gcr_lPins[1];
	    p2 = &ch->gcr_rPins[1];
	    nPins = ch->gcr_width;
	    scx.scx_area.r_xbot = ch->gcr_area.r_xbot;
	    scx.scx_area.r_xtop = ch->gcr_area.r_xtop;
	    coord = ch->gcr_origin.p_y + RtrGridSpacing;
	    break;
	case CHAN_VRIVER:
	    p1 = &ch->gcr_tPins[1];
	    p2 = &ch->gcr_bPins[1];
	    nPins = ch->gcr_length;
	    scx.scx_area.r_ybot = ch->gcr_area.r_ybot;
	    scx.scx_area.r_ytop = ch->gcr_area.r_ytop;
	    coord = ch->gcr_origin.p_x + RtrGridSpacing;
	    break;
    }

    scx.scx_use = routeUse;
    scx.scx_trans = GeoIdentityTransform;
    for (n = 1; n <= nPins; n++, p1++, p2++, coord += RtrGridSpacing)
    {
	switch (ch->gcr_type)
	{
	    case CHAN_HRIVER:
		scx.scx_area.r_ybot = coord - RtrSubcellSepUp;
		scx.scx_area.r_ytop = coord + RtrSubcellSepDown;
		break;
	    case CHAN_VRIVER:
		scx.scx_area.r_xbot = coord - RtrSubcellSepUp;
		scx.scx_area.r_xtop = coord + RtrSubcellSepDown;
		break;
	}

	/*
	 * If obstacles to both routing layers are found,
	 * then block this pair of crossing points if they
	 * aren't already taken.
	 */
	if (DBTreeSrTiles(&scx, &RtrMetalObstacles, 0, gaAlwaysOne, CNULL)
	    && DBTreeSrTiles(&scx, &RtrPolyObstacles, 0, gaAlwaysOne, CNULL))
	{
	    if (p1->gcr_pId == (GCRNet *) NULL) p1->gcr_pId = GCR_BLOCKEDNETID;
	    if (p2->gcr_pId == (GCRNet *) NULL) p2->gcr_pId = GCR_BLOCKEDNETID;
	}
    }

}

int
gaAlwaysOne()
{
    return (1);
}
