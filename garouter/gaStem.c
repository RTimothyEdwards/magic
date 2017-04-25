/*
 * gaStem.c -
 *
 * Assignment of routing-grid pin locations to terminals.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/garouter/gaStem.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
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
#include "gaInternal.h"
#include "utils/netlist.h"
#include "textio/textio.h"
#include "utils/styles.h"
#include "utils/malloc.h"
#include "drc/drc.h"
#include "debug/debug.h"

/*
 * If TRUE, leave feedback for each location for a terminal that
 * is unusable.  If FALSE, only leave feedback if ALL locations
 * for a particular terminal are unusable.
 */
bool GAStemWarn = FALSE;

/* Used locally */
int gaMaxAbove;		/* The maximum distance any routing material extends
			 * above (or to the right of) a routing grid line.
			 */
int gaMaxBelow;		/* Same as above, but for material below or to the
			 * left of routing grid lines.
			 */
int gaMinAbove;		/* Minimum distance any routing material extends
			 * above (or to the right of) a routing grid line.
			 */

/*
 * The following are conservative distances around each of
 * metal, poly, and contact types that must be clear of material.
 */
int gaMetalClear, gaPolyClear, gaContactClear;

/* Statistics */
int gaNumDegenerate;
int gaNumLocs;
int gaNumPairs;
int gaNumInt, gaNumExt, gaNumNoChan;
int gaNumInNorm;
int gaNumOverlap;
int gaNumNetBlock;
int gaNumPinBlock;
int gaNumMazeStem, gaNumSimpleStem;
int gaNumSimplePaint, gaNumMazePaint, gaNumExtPaint;

/* Forward declarations */
int gaStemContainingChannelFunc();
bool gaStemAssign();
void gaStemGridRange();
void gaStemPaint();
bool gaStemNetClear();
bool gaStemInternalFunc();
bool gaStemInternal();
bool gaStemGrow();


/*
 * ----------------------------------------------------------------------------
 *
 * gaStemAssignAll --
 *
 * Assign one or two crossing points to each terminal location.
 * Terminal locations must lie inside river-routing channels;
 * they are assigned crossings on either side of the channel
 * as long as those crossings are reachable by river-routing.
 *
 * If terminal locations aren't grid-aligned, this code takes
 * care of figuring out which grid-aligned crossing point to
 * use and ensuring that this point is reachable from the terminal.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds one additional NLTermLoc to the netlist for each
 *	terminal location that is given two crossing points.
 *	When it isn't possible to assign any crossing points
 *	to a NLTermLoc, it is deleted from the list and we
 *	leave feedback.
 *
 * ----------------------------------------------------------------------------
 */

void
gaStemAssignAll(routeUse, netList)
    CellUse *routeUse;
    NLNetList *netList;
{
    TileType t;

    /* Statistics */
    gaNumDegenerate = 0;
    gaNumLocs = 0;
    gaNumInt = 0;
    gaNumExt = 0;
    gaNumNoChan = 0;
    gaNumPairs = 0;
    gaNumInNorm = 0;
    gaNumOverlap = 0;
    gaNumNetBlock = 0;
    gaNumPinBlock = 0;
    gaNumMazeStem = 0;
    gaNumSimpleStem = 0;

    /*
     * Compute the distance around metal, poly, and contact that
     * must be clear of material.
     */
    gaMetalClear = gaPolyClear = 0;
    for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
    {
	if (RtrMetalSeps[t] > gaMetalClear) gaMetalClear = RtrMetalSeps[t];
	if (RtrPolySeps[t] > gaPolyClear) gaPolyClear = RtrPolySeps[t];
    }
    gaContactClear = MAX(gaMetalClear + RtrMetalSurround,
			 gaPolyClear + RtrPolySurround);

    /* Max distance any routing material extends above a grid line */
    gaMaxAbove = MAX(RtrMetalWidth, RtrPolyWidth);
    gaMaxAbove = MAX(gaMaxAbove, RtrContactWidth - RtrContactOffset);

    /* Min distance any routing material extends above a grid line */
    gaMinAbove = MIN(RtrMetalWidth, RtrPolyWidth);
    gaMinAbove = MIN(gaMinAbove, RtrContactWidth - RtrContactOffset);

    /* Max distance any routing material extends below a grid line */
    gaMaxBelow = RtrContactOffset;

    /* Assign the stems (gaStemAssign() does the real work) */
    RtrStemProcessAll(routeUse, netList, GAStemWarn, gaStemAssign);

    if (DebugIsSet(gaDebugID, gaDebVerbose))
    {
	TxPrintf("%d terminals processed.\n", gaNumLocs);
	TxPrintf("%d internal, %d external, %d no channel.\n",
		gaNumInt, gaNumExt, gaNumNoChan);
	TxPrintf("%d paired internal stems.\n", gaNumPairs);
	TxPrintf("%d degenerate.\n", gaNumDegenerate);
	TxPrintf("%d discarded because inside normal channels.\n", gaNumInNorm);
	TxPrintf("%d discarded because overlapped channel boundaries.\n",
		gaNumOverlap);
	TxPrintf("%d possible stems blocked by other terminals.\n",
		gaNumNetBlock);
	TxPrintf("%d possible stems to blocked pins.\n", gaNumPinBlock);
	TxPrintf("%d simple paths, %d maze paths.\n",
		gaNumSimpleStem, gaNumMazeStem);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * gaStemAssign --
 *
 * Do the real work of gaStemAssignAll() above -- assign one or two
 * crossing points to the terminal location 'loc'.
 *
 * Results:
 *	TRUE if successful, FALSE if no locations could be assigned.
 *
 * Side effects:
 *	May add one additional NLTermLoc to the list for 'loc',
 *	in the position immediately following loc, if this location
 *	is given two crossing points.  The caller should be aware
 *	of this in traversing a linked list of NLTermLocs.
 *
 * ----------------------------------------------------------------------------
 */

bool
gaStemAssign(routeUse, doWarn, loc, term, net, netList)
    CellUse *routeUse;	/* Cell being routed */
    bool doWarn;	/* If TRUE, leave feedback for each error */
    NLTermLoc *loc;	/* Location considered */
    NLTerm *term;	/* Terminal of which loc is a location */
    NLNet *net;		/* Net to which it belongs */
    NLNetList *netList;	/* Netlist (searched for blocking terminals) */
{
    GCRChannel *ch;

    gaNumLocs++;

    /*
     * See if this location lies inside a river-routing channel.
     * If it does, return the channel containing it.
     */
    if (ch = gaStemContainingChannel(routeUse, doWarn, loc))
    {
	if (ch->gcr_type != CHAN_HRIVER && ch->gcr_type != CHAN_VRIVER)
	    goto fail;
	gaNumInt++;
	return (gaStemInternal(routeUse, doWarn, loc, net, ch, netList));
    }

    /* Not inside a river-routing channel.  Try for a nearby normal channel */
    if (RtrStemAssignExt(routeUse, doWarn, loc, term, net))
    {
	gaNumExt++;
	return (TRUE);
    }

    if (doWarn)
    {
	DBWFeedbackAdd(&loc->nloc_rect,
	    "No crossing reachable from terminal",
	    routeUse->cu_def, 1, STYLE_PALEHIGHLIGHTS);
    }

fail:
    gaNumNoChan++;
    return (FALSE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * gaStemContainingChannel --
 *
 * Find the channel containing 'loc' if there is one.  This channel,
 * if it exists, should be a river-routing channel.  If it isn't,
 * we leave feedback if 'doWarn' is TRUE.
 *
 * If loc is degenerate, it can't lie on the border of a channel
 * (this restriction may be lifted later; it's just a pain to
 * deal with now).
 *
 * It is illegal for loc to straddle a channel boundary.
 *
 * Results:
 *	Returns the channel as described above, or NULL if loc
 *	didn't lie in a channel or it violated one of the above
 *	conditions.
 *
 * Side effects:
 *	Leaves feedback in the event of errors, if 'doWarn' is TRUE.
 *
 * ----------------------------------------------------------------------------
 */

GCRChannel *
gaStemContainingChannel(routeUse, doWarn, loc)
    CellUse *routeUse;	/* For errors */
    bool doWarn;	/* If TRUE, leave feedback if error */
    NLTermLoc *loc;	/* Find a channel for this terminal */
{
    GCRChannel *ch;
    Rect area;

    /* Special handling is required for degenerate locs */
    area = loc->nloc_rect;
    if (GEO_RECTNULL(&area))
	if (!gaStemGrow(&area))
	    goto overlap;

    /* Ensure that only one channel is overlapped */
    ch = (GCRChannel *) NULL;
    if (DBSrPaintArea((Tile *) NULL, RtrChannelPlane, &area,
	    &DBAllTypeBits, gaStemContainingChannelFunc, (ClientData) &ch))
	goto overlap;

    /* Illegal to be inside a normal channel */
    if (ch && ch->gcr_type == CHAN_NORMAL)
    {
	gaNumInNorm++;
	if (doWarn)
	{
	    DBWFeedbackAdd(&area,
		"Terminal is inside a normal routing channel",
		routeUse->cu_def, 1, STYLE_PALEHIGHLIGHTS);
	}
    }

    return (ch);

overlap:
    gaNumOverlap++;
    if (doWarn)
    {
	DBWFeedbackAdd(&area,
	    "Terminal overlaps a channel boundary",
	    routeUse->cu_def, 1, STYLE_PALEHIGHLIGHTS);
    }
    return ((GCRChannel *) NULL);
}

/*
 * gaStemContainingChannelFunc --
 *
 * Called via DBSrPaintArea on behalf of gaStemContainingChannel above
 * for each tile overlapping the area of a terminal, to determine
 * whether more than one channel is overlapped by that terminal.
 *
 * Results:
 *	Returns 0 normally, or 1 if *pCh was non-zero and
 *	the channel associated with tile was different from
 *	*pCh.
 *
 * Side effects:
 *	Sets *pCh to the channel associated with tile if there
 *	was one.
 *
 * ----------------------------------------------------------------------------
 */

int
gaStemContainingChannelFunc(tile, pCh)
    Tile *tile;
    GCRChannel **pCh;
{
    GCRChannel *ch;

    if (ch = (GCRChannel *) tile->ti_client)
    {
	if (*pCh)
	{
	    if (ch != *pCh)
		return (1);
	}
	else *pCh = ch;
    }

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * gaStemGrow --
 *
 * The terminal being processed (described by the initial value of *area)
 * is degenerate, so figure out which side is of interest and grow *area
 * in that direction.
 *
 * If *area is entirely contained by a channel or empty space,
 * we don't have to do anything.  If it lies on the border
 * of a channel, then currently we reject it.
 *
 * Results:
 *	TRUE if area is entirely contained in either channel or empty
 *	space; FALSE otherwise.
 *
 * Side effects:
 *	Modifies *area as described above.
 *
 * ----------------------------------------------------------------------------
 */

bool
gaStemGrow(area)
    Rect *area;
{
    Rect r;
    GCRChannel *ch;

    r = *area;
    if (r.r_xbot == r.r_xtop) r.r_xbot--, r.r_xtop++;
    if (r.r_ybot == r.r_ytop) r.r_ybot--, r.r_ytop++;
    gaNumDegenerate++;

    /* OK if either zero or one channel is overlapped */
    ch = (GCRChannel *) NULL;
    if (DBSrPaintArea((Tile *) NULL, RtrChannelPlane, &r,
	    &DBAllTypeBits, gaStemContainingChannelFunc, (ClientData) &ch) == 0)
	return (TRUE);

    return (FALSE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * gaStemInternal --
 *
 * Having determined that 'loc' lies entirely within a river-routing
 * channel, try to bring it out to both sides of the channel.  
 *
 * If the terminal permits a grid aligned route,  "simple" routes are
 * tried first.  A simple route is a grid aligned straight line from
 * terminal to pin with a possible layer change at the terminal and
 * another at the grid point just prior to the pin.  If simple routes
 * are not possible, the mzrouter() is called.
 *
 * Marks the pins in the channel OUTSIDE the river-routing channel with
 * net (the ones INSIDE the river-routing channel are left blank,
 * so the channel router won't try connecting them).  Also sets gcr_pSeg
 * for the outside pins to GCR_STEMSEGID to tell the global router that
 * they are stem tips; the global router will reassign a real segment
 * number to the crossing point when the stem tip is used.
 *
 * Each NLTermLoc has nloc_chan set to the channels on the outside
 * of 'ch', rather than 'ch' itself, in order to be consistent with
 * gaStemExternal.
 *
 * Results:
 *	TRUE if we succeeded in assigning one (or two) crossing points;
 *	FALSE if none could be assigned.
 *
 * Side effects:
 *	See above.
 *	May add one additional NLTermLoc to the list for 'loc',
 *	in the position immediately following loc, if this location
 *	is given two crossing points.
 *
 * ----------------------------------------------------------------------------
 */

bool
gaStemInternal(routeUse, doWarn, loc, net, ch, netList)
    CellUse *routeUse;	/* Cell being routed */
    bool doWarn;	/* If TRUE, leave feedback on all errors */
    NLTermLoc *loc;	/* Terminal being processed */
    NLNet *net;		/* Used for marking pins */
    GCRChannel *ch;	/* Channel containing loc */
    NLNetList *netList;	/* Netlist (searched for blocking terminals) */
{
    int min, max, start, lo, hi;

    /* Compute the grid lines that can be used for this terminal */
    gaStemGridRange(ch->gcr_type, &loc->nloc_rect, &min, &max, &start);

    /*
     * Try each possibility until we find a grid line that is usable,
     * or until we've run out of possibilities.  Work outward from
     * the starting point.  We don't make any attempt to pick the
     * best grid line, so if we see one early that can only be routed
     * to one side, and one later that can be routed to both, we still
     * pick the earlier.  The greedy strategy is simpler, and also we
     * expect that usually there will only be a single grid line per
     * terminal.
     */
    if (gaStemInternalFunc(routeUse, loc, net, ch, start, netList))
	return (TRUE);
    for (lo = start - RtrGridSpacing, hi = start + RtrGridSpacing; 
	 lo >= min || hi <= max; 
	 lo -= RtrGridSpacing, hi += RtrGridSpacing)
    {
	if (lo >= min)
	    if (gaStemInternalFunc(routeUse, loc, net, ch, lo, netList))
		return (TRUE);
	if (hi <= max)
	    if (gaStemInternalFunc(routeUse, loc, net, ch, hi, netList))
		return (TRUE);
    }

    if (doWarn)
    {
	DBWFeedbackAdd(&loc->nloc_rect,
	    "Terminal can't be brought out to either channel boundary",
	    routeUse->cu_def, 1, STYLE_PALEHIGHLIGHTS);
    }
    return (FALSE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * gaStemInternalFunc --
 *
 * Try "simple" (straight line) routes and failing that mzroutes from
 * terminal to pins on both sides of the channel.
 * If it is possible to connect to one or both sides of the channel
 * mark loc->nloc_stem,
 * loc->nloc_chan, and loc->nloc_dir with the location of the
 * crossing chosen (add a new loc to the list if two crossings were
 * chosen), and mark the pins in the channels OUTSIDE of the crossings
 * as described in the comments to gaStemInternal() above.  The pin
 * used for the crossing is stored in loc->nloc_pin.
 *
 * NOTE: loc->nloc_chan is the channel on the OUTSIDE of 'ch',
 * rather than 'ch' itself.
 *
 * Results:
 *	TRUE on success, FALSE on failure.
 *
 * Side effects:
 *	See above, and also the comments to gaStemInternal().
 *
 * ----------------------------------------------------------------------------
 */

bool
gaStemInternalFunc(routeUse, loc, net, ch, gridLine, netList)
    CellUse *routeUse;
    NLTermLoc *loc;
    NLNet *net;
    GCRChannel *ch;
    int gridLine;
    NLNetList *netList;
{
    GCRPin *pin1, *pin2;
    NLTermLoc *loc2;
    Point p1, p2;
    int dir1, dir2;

    /*
     * Compute the areas that must be searched to ensure that
     * river-routes to the outside of the channel are possible.
     */
    switch (ch->gcr_type)
    {
	case CHAN_HRIVER:
	    p1.p_x = ch->gcr_area.r_xbot;
	    p1.p_y = gridLine;
	    p2.p_x = ch->gcr_area.r_xtop;
	    p2.p_y = gridLine;
	    dir1 = GEO_WEST;
	    dir2 = GEO_EAST;
	    break;
	case CHAN_VRIVER:
	    p1.p_x = gridLine;
	    p1.p_y = ch->gcr_area.r_ybot;
	    p2.p_x = gridLine;
	    p2.p_y = ch->gcr_area.r_ytop;
	    dir1 = GEO_SOUTH;
	    dir2 = GEO_NORTH;
	    break;
    }

    if (DebugIsSet(gaDebugID, gaDebStems))
    {
	TxPrintf("Loc: ll=(%d,%d) ur=(%d,%d)\n",
		loc->nloc_rect.r_xbot, loc->nloc_rect.r_ybot,
		loc->nloc_rect.r_xtop, loc->nloc_rect.r_ytop);
	TxPrintf("Try crossings: L=(%d,%d) and R=(%d,%d)\n",
		p1.p_x, p1.p_y, p2.p_x, p2.p_y);
    }

    pin1 = gaStemCheckPin(routeUse, loc, ch, dir1, &p1, netList);
    pin2 = gaStemCheckPin(routeUse, loc, ch, dir2, &p2, netList);

    if (DebugIsSet(gaDebugID, gaDebStems))
    {
	if (pin1) TxPrintf("Success L=(%d,%d)\n", p1.p_x, p1.p_y);
	if (pin2) TxPrintf("Success R=(%d,%d)\n", p2.p_x, p2.p_y);
	if (pin1 == NULL && pin2 == NULL)
	    TxPrintf("FAILURE ON BOTH CROSSINGS\n");
	TxMore("--------");
    }

    if (pin1 == (GCRPin *) NULL && pin2 == (GCRPin *) NULL)
	return (FALSE);

    if (pin1)
    {
	loc->nloc_dir = dir1;
	loc->nloc_stem = p1;
	loc->nloc_chan = pin1->gcr_linked->gcr_ch;
	loc->nloc_pin = pin1->gcr_linked;

	/* Mark the crossing OUTSIDE ch */
	pin1->gcr_linked->gcr_pId = (GCRNet *) net;
	pin1->gcr_linked->gcr_pSeg = GCR_STEMSEGID;
    }
    if (pin2)
    {
	if (pin1)
	{
	    /* Allocate a new NLTermLoc to hold the second crossing */
	    loc2 = (NLTermLoc *) mallocMagic(sizeof (NLTermLoc));
	    *loc2 = *loc;
	    loc->nloc_next = loc2;
	    loc = loc2;
	    gaNumPairs++;
	}
	loc->nloc_dir = dir2;
	loc->nloc_stem = p2;
	loc->nloc_chan = pin2->gcr_linked->gcr_ch;
	loc->nloc_pin = pin2->gcr_linked;

	/* Mark the crossing OUTSIDE ch */
	pin2->gcr_linked->gcr_pId = (GCRNet *) net;
	pin2->gcr_linked->gcr_pSeg = GCR_STEMSEGID;
    }

    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * gaStemCheckPin --
 *
 * Determine if the crossing point at 'point' is reachable from 'loc'.
 * Simple connections are tried first.  If these fail, the mzrouter module
 * is called.
 *
 * It must normally be possible to connect to either layer in the
 * adjacent channel (channel routing isn't guaranteed to leave a
 * signal on a particular layer since the layer used depends on the
 * channel orientation).  However, if one layer is blocked in the
 * channel, then we only need to connect to the other layer.
 *
 * Results:
 *	Returns the pin belonging to 'ch' and using the crossing
 *	point at 'point' if a river-route is possible, or NULL
 *	if no river-route is possible.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

GCRPin *
gaStemCheckPin(routeUse, terminalLoc, ch, side, gridPoint, netList)
    CellUse *routeUse;		/* Cell being routed */
    NLTermLoc *terminalLoc;	/* Terminal */
    GCRChannel *ch;		/* Channel whose pin we are to use */
    int side;		/* Direction 'gridPoint' lies relative to 'loc' */
    Point *gridPoint;		/* Crossing point to use */
    NLNetList *netList;		/* Searched for obstructing terminals */
{
    TileTypeBitMask destMask;
    GCRPin *pin;
    SimpleStem simple;
    short code;
    
    /*
     * Discard this pin immediately if any of the following are true:
     *	- it is already occupied by a net
     *	- it isn't adjacent to a usable channel (i.e, it has no gcr_linked
     *	  pin or its gcr_linked pin is blocked)
     *	- there's another terminal from this netlist in the way
     */
    pin = RtrPointToPin(ch, side, gridPoint);
    if (pin->gcr_pId != (GCRNet *) NULL
	    || pin->gcr_linked == (GCRPin *) NULL
	    || pin->gcr_linked->gcr_pId != (GCRNet *) NULL)
    {
	gaNumPinBlock++;
	return ((GCRPin *) NULL);
    }

    if (!gaStemNetClear(&terminalLoc->nloc_rect, gridPoint, side, netList))
    {
	gaNumNetBlock++;
	return ((GCRPin *) NULL);
    }

    /*
     * What types are present at the pin location?
     * Only worry about connecting to layers that aren't
     * present in the channel.  The code above assumes
     * that two-layer blockages at channel boundaries
     * will cause their corresponding pins to be marked
     * as blocked.
     */
    code = pin->gcr_ch->gcr_result[pin->gcr_x][pin->gcr_y];
    destMask = DBZeroTypeBits;
    if ((code & GCRBLKM) == 0) TTMaskSetType(&destMask, RtrMetalType);
    if ((code & GCRBLKP) == 0) TTMaskSetType(&destMask, RtrPolyType);

    ASSERT(!TTMaskEqual(&destMask, &DBZeroTypeBits), "gaStemCheckPin");

    /*
     * Try to do things the simple way first, considering routes
     * of a very stylized nature: straight across with at most one
     * layer change over the terminal, and another on the grid line
     * just before the channel boundary.  This avoids having to
     * call the maze router if we don't need to.
     */
    if (DebugIsSet(gaDebugID, gaDebNoSimple))
	goto hardway;
    if (!gaStemSimpleInit(routeUse, terminalLoc, gridPoint, side, &simple))
	goto hardway;
    if (TTMaskHasType(&destMask, RtrMetalType)
	&& !gaStemSimpleRoute(&simple, RtrMetalType, (CellDef *) NULL))
    {
	goto hardway;
    }
    if (TTMaskHasType(&destMask, RtrPolyType)
	&& !gaStemSimpleRoute(&simple, RtrPolyType, (CellDef *) NULL))
    {
	goto hardway;
    }

    /* Win */
    gaNumSimpleStem++;
    return pin;

hardway:
    /* Simple connections have failed, so try using mzrouter.  A connection
     * must be possible from EITHER ROUTING LAYER, so we know we can connect
     * no matter how the channel is routed.
     * 
     * NULL writeUse is used so no paint is generated, we just check
     * if the connections are possible, deferring the actual stem generation
     * until after channel routing, so we know what layers to connect to.
     */
    {
        bool writeFlag = FALSE;
	TileTypeBitMask polyMask, metalMask;
	TTMaskSetOnlyType(&polyMask, RtrPolyType);
	TTMaskSetOnlyType(&metalMask, RtrMetalType);

	if(gaMazeRoute(routeUse, terminalLoc, gridPoint, polyMask, side, 
		       writeFlag)  &&
	   gaMazeRoute(routeUse, terminalLoc, gridPoint, metalMask, side, 
		       writeFlag))
	{
	    gaNumMazeStem++;
	    return pin;
	}
	else
	{
	    return ((GCRPin *) NULL);
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * gaStemNetClear --
 *
 * Determine whether the area between termArea and the crossing point
 * 'point' is clear of other terminals that might want to share the
 * same grid line.  The purpose of this procedure is primarily to avoid
 * doing something silly in the following situation (both terminals share
 * the same horizontal grid line in a CHAN_HRIVER river-routing channel):
 *
 *	+---------------------------+
 *	|			    |
 *	|	+---+	+---+	    |
 *	|	| A |	| B |	    |
 *	|	+---+	+---+	    |
 *	|			    |
 *	+---------------------------+
 *
 * Here, terminal "A" should only be brought out to the left, and "B"
 * only to the right of the channel, since to do otherwise would block
 * a terminal completely.
 *
 * +++ Currently, we consider a path from termArea to point blocked
 * +++ by another terminal T only if T can use a single grid line,
 * +++ and this grid line is the same as that of point.
 *
 * Results:
 *	TRUE if no terminals share the same grid line, FALSE otherwise.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
gaStemNetClear(termArea, point, side, netList)
    Rect *termArea;	/* Area of the starting terminal */
    Point *point;		/* Crossing point where we want to end up */
    int side;			/* Direction of point relative to termArea */
    NLNetList *netList;		/* Netlist to check for terminals to avoid */
{
    int min, max, start, type, grid;
    NLTermLoc *loc;
    NLTerm *term;
    NLNet *net;
    Rect r;

    /* 
     * First cut: determine a search area large enough so that any
     * terminals lying entirely outside of it can't possibly conflict
     * with this one.
     */
    switch (side)
    {
	case GEO_NORTH:
	    r.r_xbot = point->p_x - RtrSubcellSepUp;
	    r.r_xtop = point->p_x + RtrSubcellSepDown;
	    r.r_ybot = termArea->r_ytop;
	    r.r_ytop = point->p_y + RtrSubcellSepDown;
	    type = CHAN_VRIVER;
	    break;
	case GEO_SOUTH:
	    r.r_xbot = point->p_x - RtrSubcellSepUp;
	    r.r_xtop = point->p_x + RtrSubcellSepDown;
	    r.r_ybot = point->p_y - RtrSubcellSepUp;
	    r.r_ytop = termArea->r_ybot;
	    type = CHAN_VRIVER;
	    break;
	case GEO_WEST:
	    r.r_ybot = point->p_y - RtrSubcellSepUp;
	    r.r_ytop = point->p_y + RtrSubcellSepDown;
	    r.r_xbot = point->p_x - RtrSubcellSepUp;
	    r.r_xtop = termArea->r_xbot;
	    type = CHAN_HRIVER;
	    break;
	case GEO_EAST:
	    r.r_ybot = point->p_y - RtrSubcellSepUp;
	    r.r_ytop = point->p_y + RtrSubcellSepDown;
	    r.r_xbot = termArea->r_xtop;
	    r.r_xtop = point->p_x + RtrSubcellSepDown;
	    type = CHAN_HRIVER;
	    break;
    }
    grid = (type == CHAN_HRIVER) ? point->p_y : point->p_x;

    /* Only need to do work if a terminal lies inside this search area */
    for (net = netList->nnl_nets; net; net = net->nnet_next)
    {
	if (!GEO_OVERLAP(&net->nnet_area, &r))
	    continue;
	for (term = net->nnet_terms; term; term = term->nterm_next)
	{
	    for (loc = term->nterm_locs; loc; loc = loc->nloc_next)
		if (GEO_OVERLAP(&loc->nloc_rect, &r))
		{
		    /* Only reject if both prefer same grid location */
		    gaStemGridRange(type, &loc->nloc_rect, &min,&max,&start);
		    if (start == grid)
			return (FALSE);
		}
	}
    }

    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * gaStemGridRange --
 *
 * Determine the range of grid lines usable by the terminal whose
 * area is 'r', for a river-routing channel whose type is 'type'
 * (one of CHAN_HRIVER or CHAN_VRIVER).  Usable grid lines are at
 * most one grid line away from the relevant boundary of 'r'.
 * Pick a starting grid line that is as close to the center of the
 * range of usable grid points and that is also between the top
 * and bottom (for CHAN_HRIVER) or left and right (for CHAN_VRIVER)
 * of 'r'.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets *pMinGrid, *pMaxGrid, and *pStart.
 *
 * ----------------------------------------------------------------------------
 */

void
gaStemGridRange(type, r, pMinGrid, pMaxGrid, pStart)
    int type;
    Rect *r;
    int *pMinGrid, *pMaxGrid, *pStart;
{
    int min, max, start;

    switch (type)
    {
	case CHAN_HRIVER:
	{
	    min = RTR_GRIDDOWN(r->r_ybot, RtrOrigin.p_y);
	    max = RTR_GRIDDOWN(r->r_ytop - gaMaxAbove, RtrOrigin.p_y);
	    start = (max + min)/2;
	    start = RTR_GRIDDOWN(start, RtrOrigin.p_y);
	    if (start < r->r_ybot && (start + RtrGridSpacing) < r->r_ytop)
	    {
		start += RtrGridSpacing;
	    }
	    break;
	}
	case CHAN_VRIVER:
	{
	    min = RTR_GRIDDOWN(r->r_xbot, RtrOrigin.p_x);
	    max = RTR_GRIDDOWN(r->r_xtop - gaMaxAbove, RtrOrigin.p_x);
	    start = (max + min)/2;
	    start = RTR_GRIDDOWN(start, RtrOrigin.p_x);
	    if (start < r->r_xbot && (start + RtrGridSpacing) < r->r_xtop)
	    {
		start += RtrGridSpacing;
	    }
	    break;
	}
	default:
	{
	    ASSERT(FALSE, "Bad channel type in gaStemGridRange");
	    break;
	}
    }
    max = MAX(max, start);
    min= MIN(min, start);
    *pMaxGrid = max;
    *pMinGrid = min;
    *pStart = start;
}

/*
 * ----------------------------------------------------------------------------
 *
 * gaStemPaintAll --
 *
 * Paint each stem that turned out to be connected.
 * See gaStemPaint() for details.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds paint to 'routeUse->cu_def'.
 *
 * ----------------------------------------------------------------------------
 */

void
gaStemPaintAll(routeUse, netList)
    CellUse *routeUse;
    NLNetList *netList;
{
    NLTermLoc *loc;
    NLTerm *term;
    int numInt;
    NLNet *net;

    gaNumSimplePaint = 0;
    gaNumMazePaint = 0;
    gaNumExtPaint = 0;
    RtrMilestoneStart("Painting stems");
    for (net = netList->nnl_nets; net; net = net->nnet_next)
    {
	for (term = net->nnet_terms; term; term = term->nterm_next)
	    for (loc = term->nterm_locs; loc; loc = loc->nloc_next)
	    {
		if (SigInterruptPending)
		    goto out;
		if (loc->nloc_dir > 0)
		    gaStemPaint(routeUse, loc);
	    }
	RtrMilestonePrint();
    }

out:
    RtrMilestoneDone();
    if (DebugIsSet(gaDebugID, gaDebVerbose))
    {
	numInt = gaNumSimplePaint + gaNumMazePaint;
	TxPrintf("%d simple, %d maze, %d total internal stems.\n",
	    gaNumSimplePaint, gaNumMazePaint, numInt);
	TxPrintf("%d external stems painted.\n", gaNumExtPaint);
	TxPrintf("%d total stems painted.\n", numInt + gaNumExtPaint);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * gaStemPaint --
 *
 * Paint a single stem.
 * Terminals that don't lie inside a river-routing channel are "external"
 * and are handled by the standard router stem-generator.  Terminals that
 * lie inside a river-routing channel are "internal"; they are connected
 * to their stem tips on the channel boundary using simple routes if
 * possible, and failing that using the Magic maze-router (mzrouter).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds paint to 'routeUse->cu_def'.
 *
 * ----------------------------------------------------------------------------
 */

void
gaStemPaint(routeUse, terminalLoc)
    CellUse *routeUse;
    NLTermLoc *terminalLoc;
{
    TileTypeBitMask terminalLayerMask;	/* Possible layers for stem at 
					   terminal */
    TileTypeBitMask pinLayerMask;	/* Possible layers for stem at pin */
    Rect  errArea;
    char *errReason;
    GCRPin *pin;
    short flags;

    /*
     * Find the pin used for this stem.  If this pin wasn't actually
     * used by the global router, there's no point in routing a stem
     * to it so we return.
     */
    pin = terminalLoc->nloc_pin;
    if (pin->gcr_pId == (GCRNet *) NULL)
	return;

    /*
     * Figure out which layers are OK to start routing and which
     * layers are OK to finish up on.  Perform a sanity check:
     * if the destination layer isn't one of the legal routing
     * layers, just return.  (This will catch pins that were
     * supposed to be routed by the channel router, but which
     * it was unable to connect to).
     */
    flags = pin->gcr_ch->gcr_result[pin->gcr_x][pin->gcr_y];
    if (!rtrStemMask(routeUse, terminalLoc, flags, 
		     &terminalLayerMask, &pinLayerMask))
    {
	errReason = "Terminal is not on a legal routing layer";
	GEO_EXPAND(&terminalLoc->nloc_rect, 1, &errArea);
	goto failure;
    }

    if (!TTMaskHasType(&pinLayerMask, RtrMetalType)
	    && !TTMaskHasType(&pinLayerMask, RtrPolyType))
    {
	/* To do:  connect down to pin layer with contacts. */
	/* Or, just let the maze router handle this. */
    }

    /*
     * The terminal could be internal (terminalLoc->nloc_rect lies
     * inside a river-routing channel) or external (terminalLoc->nloc_rect 
     * lies outside of a channel but the stem tip borders on a normal
     * routing channel).  In the latter case, use the old stem
     * generation code (temporary measure until we get our own
     * code working).
     */

    if (!RtrMazeStems && (pin->gcr_linked == (GCRPin *) NULL))
    {
	/* Terminal didn't lie in a river-routing channel */
  	(void) RtrStemPaintExt(routeUse, terminalLoc);
  	gaNumExtPaint++;
  	return;
    }

    ASSERT(pin->gcr_linked->gcr_ch->gcr_type != CHAN_NORMAL, "gaStemPaint");

    /* 
     * Try a simple stem.
     */

    if (!RtrMazeStems)
    {
	SimpleStem simple;
	Point *gridPoint = &(terminalLoc->nloc_stem);
	int side = terminalLoc->nloc_dir;

	if (gaStemSimpleInit(routeUse, terminalLoc, gridPoint, side, &simple))
	{
	    /* try ending on "metal" */
	    if (TTMaskHasType(&pinLayerMask, RtrMetalType))
	    {
		if(gaStemSimpleRoute(&simple,RtrMetalType,routeUse->cu_def))
		{
   		    gaNumSimplePaint++;
		    return;
		}
	    }
	    /* didn't work, so try ending on "poly" */
	    if (TTMaskHasType(&pinLayerMask, RtrPolyType))
	    {
		if(gaStemSimpleRoute(&simple,RtrPolyType,routeUse->cu_def))
		{
   		    gaNumSimplePaint++;
		    return;
		}
	    }
	}
    }
	 
    /*
     * Try the maze router. 
     */
    if (RtrMazeStems)
    {
	Point *pinPoint = &(terminalLoc->nloc_stem);
	int side = terminalLoc->nloc_dir;
	bool writeResult = TRUE;

	extern CellDef *gaMazeTopDef;	/* Forward declaration quick hack. */


	/* Note that by not running the gate array router, we may not	*/
	/* have initialized the maze router at this point.		*/

	if ((gaMazeTopDef == NULL) && (EditCellUse != NULL))
	    if (gaMazeInit(EditCellUse) == FALSE)
		goto totalLoss;

    
	if(gaMazeRoute(routeUse, terminalLoc, pinPoint, pinLayerMask, side, 
		       writeResult))
	{
	    gaNumMazePaint++;
	    
	    if(DebugIsSet(gaDebugID,gaDebShowMaze))
	    /* Feedback all maze routes so we can check them 
	     * (this will cause all maze routes to be reported as 
	     *  routing errors) 
	     */
	    {
		Rect area;

		area = terminalLoc->nloc_rect;
		GeoIncludePoint(&terminalLoc->nloc_stem, &area);
		if (GEO_RECTNULL(&area))
		{
		    GEO_EXPAND(&area, 1, &area);
		}
    
		DBWFeedbackAdd(&area, 
			       "MAZE ROUTE", 
			       routeUse->cu_def, 
			       1,
			       STYLE_PALEHIGHLIGHTS);
	    }

	    return;
	}

    }

    /*
     * Total loss.
     * This should never happen!  But there are a few cases when it can,
     * such as if we have given a difficult path to route from the channel,
     * over obstructions, to the pin, requiring the maze router, and the
     * maze router is not set up in the technology file.
     */

totalLoss:
    errReason = "Couldn't maze route final connection";
    errArea = terminalLoc->nloc_rect;
    GeoIncludePoint(&terminalLoc->nloc_stem, &errArea);
    if (GEO_RECTNULL(&errArea))
    {
	GEO_EXPAND(&errArea, 1, &errArea);
    }
    
failure:
    DBWFeedbackAdd(&errArea, errReason, routeUse->cu_def, 1,
		   STYLE_PALEHIGHLIGHTS);
}
