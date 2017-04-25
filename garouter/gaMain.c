/*
 * gaMain.c -
 *
 * Top-level of the gate-array router.
 *
 * For an overview of how routing systems work in general, see
 * the papers:
 *
 *	"Magic's Obstacle-Avoiding Global Router", G.T. Hamachi and
 *	J.K. Ousterhout, Proceedings of the 1985 Chapel Hill Conference
 *	on VLSI, May 1985, pp. 393-418
 *
 *	"A Switchbox Router with Obstacle Avoidance", G.T. Hamachi
 *	and J.K. Ousterhout, Proc 21st Design Automation Conference,
 *	June 1984, pp. 173-179.
 *
 *	"The PI (Placement and Interconnect) System", R.L. Rivest,
 *	Proc 19th Design Automation Conference, June 1982, pp. 475-481.
 *
 * This router shares much code with the standard router in the "router"
 * module, and also makes use of the "grouter" (global-router) "gcr"
 * (channel-router), and "mzrouter" (maze-router) modules.
 *
 * The primary differences between this module and the "router" module
 * are that "garouter" allows the user to define channels explicitly
 * (rather than having them be generated automatically as they are in
 * the "router" module).  These channels can overlap subcells arbitrarily,
 * unlike automatically generated channels which only appear in areas
 * where there are no subcells.
 *
 * User-specified channels can be specially identified as "river-routing"
 * channels.  Unlike ordinary channels, these may contain routing terminals.
 * However, the routing across a river-routing channel is simpler than
 * that in an ordinary channel: tracks run either straight across horizontally,
 * for horizontal river routing channels, or vertically, for vertical
 * river routing channels, but never both.  (See gcr.h for a description
 * of normal channels and the two kinds of river routing channels).
 *
 * Router organization:
 * -------------------
 *
 * Routing is composed of a number of steps.  First, the channels are
 * defined (by the user in this module) by calls to GADefineChannel().
 * The remainder of the work is done in GARoute(), which:
 *
 *	- reads the list of intended connections into a NLNetList
 *	  structure,
 *	- generates "stems" for all terminals,
 *	- sets up the channels for global routing,
 *	- calls the global router to determine the sequence of channel
 *	  boundary points through which the route for each net will pass, 
 *	- calls the channel router to fill in the detailed routes for
 *	  each net in each channel structure, and then paint this
 *	  information back into the edit cell,
 *	- generates paint for each stem generated at the start of routing
 *
 * The following sections briefly describe each of the above tasks in
 * a bit more detail:
 *
 * Nets and terminals:
 * ------------------
 *
 * The connections that the router must make are described to it in
 * the form of a netlist, which comes from a text file that is either
 * generated outside of Magic or created using the netlist editor in
 * the "netmenu" module of Magic.  Each "net" is a collection of named
 * terminals that must be connected.  These terminals are described by
 * the hierarchical names of labels.  Each terminal appears in exactly
 * one net.  A given label may appear in several places in the same cell,
 * so a terminal may have several locations.  The router assumes that all
 * of these locations are already electrically connected.
 * 
 *
 * Channels and pins:
 * -----------------
 *
 * This router is grid-based, meaning that wires are generated so that
 * their left-hand side and bottom align with evenly-spaced grid lines
 * in x and y.  Two key structures, defined in gcr.h, represent the
 * routing problem in this grid-based world: the channel (GCRChannel)
 * and pin (GCRPin).  Channels are rectangular areas with pins along
 * their boundaries:
 *
 *	  . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .
 *	  .         .	      .	        .         .         .         .
 *	  . 	+---X---------X---------X---------X---------X-----+   .
 *	  . 	|   .         .         .         .         .     |   .
 *	  . 	|   .         .         .         .         .     |   .
 *	  . . . X . P---------P---------P---------P---------P . . X . .
 *	  . 	|   |         .         .         .         |	  |   .
 *	  . 	|   |         .         .         .         |	  |   .
 *	  . 	|   |         .         .         .         |	  |   .
 *	  . . . X . P . . . . . . . . . . . . . . . . . . . P . . X . .
 *	  . 	|   |         .         .         .         |	  |   .
 *	  . 	|   |         .         .         .         |	  |   .
 *	  . 	|   |         .         .         .         |	  |   .
 *	  . . . X . P . . . . . . . . . . . . . . . . . . . P . . X . .
 *	  . 	|   |         .         .         .         |	  |   .
 *	  . 	|   |         .         .         .         |	  |   .
 *	  . 	|   |         .         .         .         |	  |   .
 *	  . . . X . P---------P---------P---------P---------P . . X . .
 *	  . 	|   .         .         .         .         .     |   .
 *	  . 	+---X---------X---------X---------X---------X-----+   .
 *	  . 	    .	      .	        .         .         .         .
 *	  . 	    .	      .	        .         .         .         .
 *	  O . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .
 *
 * The dots indicate the grid lines.  The set of grid points enclosed
 * by the channel is the inner dashed line.  The actual channel boundary
 * lies between these grid lines and the next grid lines out.  (To handle
 * the case where the grid spacing is odd, the boundary actually lies
 * at a location that is half the grid spacing (rounded down to the nearest
 * integer) DOWN and to the LEFT of a grid line, hence the asymmetry in
 * the diagram above.
 *
 * "Crossing points" lie along the channel boundary and are marked by X's.
 * Each crossing point has a pin associated with it; these pins are marked
 * by P's.  (Note that the pins in the corners occupy the same grid
 * points but different crossing points; this reflects the fact that
 * the routing for a column and a track can overlap).
 * Each channel structure has four arrays of pins: one along each of its
 * LHS, RHS, and top and bottom (indexed as 1, 2, ... in order of
 * increasing x or y as appropriate; the 0th element of the pin
 * arrays is not used).
 *
 * When two channels share a common boundary, a single crossing
 * point may be associated with two pins: one in each channel.
 * During channel initialization (below), those pairs of pins
 * that share a crossing point are made to point to each other.
 *
 *
 * Stem generation:
 * ---------------
 *
 * Although the router is grid based, terminals in cells need not be
 * aligned to the routing grid.  Stem generation is the process of
 * finding one (or two) channel pin(s) that are reachable from each
 * terminal in the routing problem, and marking these pins with the
 * net to which the terminal belongs.  If a terminal lies in the middle
 * of a river-routing channel, up to two pins will be assigned to it
 * (on opposite sides of the channel).  Otherwise, (e.g., if the terminal
 * is inside a cell but not in any channel) only one pin will be assigned,
 * in the channel closest to the terminal.
 *
 * Stem generation actually has two parts.  The first only determines
 * if a stem CAN be run from a terminal to a pin, but doesn't generate
 * any paint, since not all terminals will necessarily be used by
 * the global router.  (For example, if a terminal appears in more
 * than one place in a cell, it may be that only one of these places
 * is actually connected by the router).  The second part of stem
 * generation, invoked after channel routing is complete, actually
 * paints the stems into the edit cell.
 *
 * Since the stem generator has no way of knowing which layer the
 * channel router will use for a signal it routes to a pin, it must
 * be possible for the final stems to connect to either routing
 * layer.  The stem generator checks to ensure that it can connect
 * to either layer during the first part, and rejects pins it can't
 * reach on either layer (unless the area of the pin itself is covered
 * by one layer, in which case the stem only has to be able to connect
 * to the free layer).
 *
 * The code for stem generation is split between this module (for
 * stems inside river-routing channels), the "router" module (for
 * stems for terminals outside of any channel), and the "mzrouter"
 * module (used to determine when stems can be routed, if it isn't
 * obvious).
 *
 * Global routing:
 * --------------
 *
 * Initially, all the pins in each channel are marked as not belonging
 * to any net, except for the pins that were assigned to terminals by
 * stem generation.  The job of the global router is to find paths
 * (as sequences of pins) for all nets, and to mark the pins on a
 * given net's path with that net's identifier.  These channels
 * and marked pins will be used as the input to the channel router.
 *
 * Channel routing:
 * ---------------
 *
 * Each channel structure contains a two-dimensional array with one
 * element for each internal grid point in the channel.  The channel
 * router accepts a channel whose pins have been marked with net
 * identifiers, and fills in this internal array with flags indicating
 * the type of material/contact to place at that grid location.  After
 * all channels have been routed, we use the information in these
 * internal arrays to generate actual paint for the edit cell.  Since
 * channels are routed independently of what material is in adjacent
 * channels, sometimes it is necessary during this paintback to add
 * contacts at channel boundaries to switch routing layers.
 * 
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/garouter/gaMain.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
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
#include "debug/debug.h"
#include "drc/drc.h"


/*
 * ----------------------------------------------------------------------------
 *
 * GARouteCmd --
 *
 * Top-level procedure for the gate-array router.
 * Routes the cell 'routeUse->cu_def' using the netlist 'netListName'.
 *
 * Results:
 *	Returns the number of routing errors left as feedback.
 *	Returning -1 means that routing was unable to commence
 *	because of other errors (e.g, the netlist couldn't be
 *	read) which were already reported to the user.
 *
 * Side effects:
 *	Leaves paint in routeUse->cu_def.  Leaves feedback where routing
 *	errors occurred.  Frees all the memory associated with channel
 *	structures when done.
 *
 * ----------------------------------------------------------------------------
 */

int
GARouteCmd(routeUse, netListName)
    CellUse *routeUse;
    char *netListName;
{
    int errs;
    NLNetList netList;
    GCRChannel *ch;
    NLNet *net;

    /* Initialize ga maze routing */
    if(gaMazeInit(routeUse)==FALSE)
    {
	TxError("Could not initialize maze router.\n");
	return(-1);
    }

    /* Ensure that there are channels defined */
    if (gaChannelList == (GCRChannel *) NULL)
    {
	TxError("Must define channels before routing.\n");
	return (-1);
    }

    /* Read the netlist */
    if (gaBuildNetList(netListName, routeUse, &netList) < 0)
	return (-1);
    if (SigInterruptPending)
	goto done;

    /* Figure out the size of the routing area */
    RouteArea.r_xbot = RouteArea.r_ybot = INFINITY;
    RouteArea.r_xtop = RouteArea.r_ytop = MINFINITY;
    for (ch = gaChannelList; ch && !SigInterruptPending; ch = ch->gcr_next)
	(void) GeoIncludeAll(&ch->gcr_area, &RouteArea);
    for (net = netList.nnl_nets; net; net = net->nnet_next)
	(void) GeoIncludeAll(&net->nnet_area, &RouteArea);

    errs = GARoute(gaChannelList, routeUse, &netList);

done:
    /* Cleanup */
    NLFree(&netList);
    GAClearChannels();

    return (errs);
}

/*
 * ----------------------------------------------------------------------------
 *
 * GARoute --
 *
 * Routes the cell 'routeUse->cu_def' using the netlist 'netList'.
 * The list of channels is 'list'.  These channels are fresh from
 * being allocated by GCRNewChannel().
 *
 * Results:
 *	Returns the number of routing errors left as feedback.
 *	Returning -1 means that routing was unable to commence
 *	because of other errors (e.g, the netlist couldn't be
 *	read) which were already reported to the user.
 *
 * Side effects:
 *	Leaves paint in routeUse->cu_def.  Leaves feedback where routing
 *	errors occurred.  Frees all the memory associated with channel
 *	structures when done.
 *
 * ----------------------------------------------------------------------------
 */

int
GARoute(list, routeUse, netList)
    GCRChannel *list;	/* List of channels */
    CellUse *routeUse;	/* Cell being routed */
    NLNetList *netList;	/* List of nets to route */
{
    int feedCount = DBWFeedbackCount, errs;
    GCRChannel *ch;

    /*
     * Initialize all the pointers in the channel structure (stem assignment),
     * and also determine which tracks are available for river-routing
     * in channels so marked (CHAN_HRIVER or CHAN_VRIVER).
     */
    gaChannelInit(list, routeUse, netList);
    if (SigInterruptPending
	    || DebugIsSet(gaDebugID, gaDebChanOnly)
	    || DebugIsSet(glDebugID, glDebStemsOnly))
	goto done;

    /* Perform global routing */
    RtrMilestoneStart("Global routing");
    GlGlobalRoute(list, netList);
    RtrMilestoneDone();
    if (SigInterruptPending || DebugIsSet(glDebugID, glDebGreedy))
	goto done;

    /* Channel routing */
    errs = 0;
    RtrMilestoneStart("Channel routing");
    for (ch = list; ch && !SigInterruptPending; ch = ch->gcr_next)
	RtrChannelRoute(ch, &errs);
    RtrMilestoneDone();
    if (errs > 0)
	TxError("%d bad connection%s.\n", errs, errs != 1 ? "s" : "");
    if (SigInterruptPending)
	goto done;

    /* Paint results in a separate pass */
    RtrMilestoneStart("Painting results");
    for (ch = list; ch && !SigInterruptPending; ch = ch->gcr_next)
    {
	RtrMilestonePrint();
	RtrPaintBack(ch, routeUse->cu_def);
	/* update bounding box NOW so searches during stem gen. 
	 * work correctly.
	 */
	DBReComputeBbox(routeUse->cu_def);  
    }
    RtrMilestoneDone();
    if (SigInterruptPending)
	goto done;

    if (DebugIsSet(gaDebugID, gaDebPaintStems))
    {
	DRCCheckThis(routeUse->cu_def, TT_CHECKPAINT, &RouteArea);
	DBWAreaChanged(routeUse->cu_def, &RouteArea, DBW_ALLWINDOWS,
		    &DBAllButSpaceBits);
	WindUpdate();
	TxMore("After channel paintback");
    }

    gaStemPaintAll(routeUse, netList);

    /*
     * Mark the areas modified.
     * Only areas inside the routing channels are actually affected.
     */
    SigDisableInterrupts();
    /* recomp bbox again just in case stem gen changed it (unlikely) */
    DBReComputeBbox(routeUse->cu_def);
    DRCCheckThis(routeUse->cu_def, TT_CHECKPAINT, &RouteArea);
    DBWAreaChanged(routeUse->cu_def, &RouteArea, DBW_ALLWINDOWS,
		&DBAllButSpaceBits);
    SigEnableInterrupts();

done:
    /* Return number of errors */
    return (DBWFeedbackCount - feedCount);
}

/*
 * ----------------------------------------------------------------------------
 *
 * gaBuildNetList --
 *
 * Construct the netlist that will be used for routing.
 * Use netListName if supplied; otherwise use the name of
 * the current netlist; if neither are set, use the name
 * of the cell 'routeUse->cu_def'.
 *
 * Leaves the constructed netlist in 'netList'.
 *
 * Results:
 *	Returns the number of nets in netList.  If there were
 *	no nets, returns 0.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

int
gaBuildNetList(netListName, routeUse, netList)
    char *netListName;
    CellUse *routeUse;
    NLNetList *netList;
{
    CellDef *routeDef = routeUse->cu_def;
    int numNets;

    /* Select the netlist */
    if (netListName == NULL)
    {
	if (!NMHasList())
	{
	    netListName = routeDef->cd_name;
	    TxPrintf("No netlist selected yet; using \"%s\".\n", netListName);
	    NMNewNetlist(netListName);
	}
	else netListName = NMNetlistName();
    }
    else NMNewNetlist(netListName);

    if (DebugIsSet(gaDebugID, gaDebVerbose))
	TxPrintf("Reading netlist %s.\n", netListName);
    RtrMilestoneStart("Building netlist");
    numNets = NLBuild(routeUse, netList);
    RtrMilestoneDone();
    if (numNets == 0)
	TxError("No nets to route.\n");
    if (DebugIsSet(gaDebugID, gaDebVerbose))
	TxPrintf("Read %d nets.\n", numNets);

    return (numNets);
}
