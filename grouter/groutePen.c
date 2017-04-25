/*
 * groutePen.c
 *
 * Computation of the penalties to be assigned for each net using
 * certain congested regions.
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

#ifndef	lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/grouter/groutePen.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif	/* lint */

#include <stdio.h>
#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/hash.h"
#include "utils/heap.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "utils/netlist.h"
#include "gcr/gcr.h"
#include "router/router.h"
#include "grouter/grouter.h"
#include "utils/signals.h"
#include "textio/textio.h"
#include "utils/malloc.h"
#include "utils/styles.h"
#include "utils/list.h"

/* Forward declarations */
void glPenSavePath();
int glPenSortNetSet();
int glPenFindCrossingFunc();
int glPenDeleteFunc();
int glPenRouteCost();
int glPenRerouteNetCost();
NetSet *glPenFindCrossingNets();
CZone *glPenScanDens();
CZone *glPenFindCZones();


/*
 * ----------------------------------------------------------------------------
 *
 * glPenSetPerChan --
 * glPenClearPerChan --
 *
 * Visit all of the channels on the CZone list for 'net'
 * (this list is ((NetClient *) net->nnet_cdata)->nc_pens);
 * glPenSetPerChan will prepend a copy of each CZone
 * to the penalty list for the appropriate channel, while
 * glPenClearPerChan will free the list for each channel.
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
glPenSetPerChan(net)
    NLNet *net;
{
    CZone *czNet, *czChan;
    GlobChan *gc;

    for (czNet = ((NetClient *) net->nnet_cdata)->nc_pens;
	    czNet;
	    czNet = czNet->cz_next)
    {
	gc = (GlobChan *) czNet->cz_chan->gcr_client;
	czChan = (CZone *) mallocMagic((unsigned) (sizeof (CZone)));
	*czChan = *czNet;
	czChan->cz_next = gc->gc_penList;
	gc->gc_penList = czChan;
    }
}

int
glPenClearPerChan(net)
    NLNet *net;
{
    CZone *czNet, *czChan;
    GlobChan *gc;

    for (czNet = ((NetClient *) net->nnet_cdata)->nc_pens;
	    czNet;
	    czNet = czNet->cz_next)
    {
	gc = (GlobChan *) czNet->cz_chan->gcr_client;
	for (czChan = gc->gc_penList; czChan; czChan = czChan->cz_next)
	    freeMagic((char *) czChan);
	gc->gc_penList = (CZone *) NULL;
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glPenCompute --
 *
 * Some routing regions will have many nets passing through them.
 * It may be, however, that some of these nets need a given region
 * much more than others: the cost to them of going around the
 * region is very high.  This procedure attempts to identify
 * congested regions (CZones), and furthermore to determine the
 * nets that should be penalized for using those regions and the
 * amount of the penalty.
 *
 * On input, the gc_prevDens and gc_postDens fields of each channel's
 * associated GlobChan structure should be set up to reflect the
 * presence of blocking information.
 *
 * Algorithm:
 *	Route each net as though it were the first net to be routed.
 *	Avoid routing through any portion of a channel that is already
 *	at density (from pre-existing wiring), but otherwise don't
 *	bother computing penalties.  Choose crossing points to give
 *	locally (i.e, per-channel) optimal lengths.  This enables
 *	us to avoid looking at most crossing points so we can route
 *	very quickly.
 *
 *	Remember each of the paths that comprised each net by storing
 *	a permanent copy of each in the list nc_paths in the NetClient
 *	structure for each net.
 *
 *	After all nets have been processed, update density in the
 *	gc_postDens density map of all channels to reflect the routes
 *	assigned to all nets.  For each region of a channel where density
 *	has been exceeded, we look at all the nets that pass through the
 *	region and see how expensive it would be to route each one if it
 *	had to avoid that region entirely.  (It is infinitely expensive
 *	if the region contains either the starting or ending point of the
 *	route!)
 *
 *	While the costs computed above are just an estimate (since they
 *	consider each net independently), the hope is that they will
 *	identify fairly well those nets that can easily avoid congested
 *	areas with only slight detours, as well as those nets for which
 *	certain regions of channels are critical.
 *
 *	We pick those nets whose cost increases the least by having to
 *	avoid the congested region as those that will be penalized; the
 *	penalty for each net is the additional cost of avoiding the region
 *	for the net for which this cost is highest.  (In effect, this gives
 *	us a balanced tradeoff: the net with low cost for not using the
 *	channel won't use it unless it has otherwise to make at least as
 *	big a detour as our estimate for the most expensive one).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stores a pointer to a list of CZone structures in
 *	the nnet_cdata->nc_pens field of each net in netList.
 *	(The list may be NULL).  Each element in the list identifies
 *	a channel and a penalty; the interpretation is that this
 *	net should be penalized by the indicated penalty if it
 *	has to go through this channel.
 *	Also uses nnet_cdata->nc_paths as temporary working storage.
 *
 * ----------------------------------------------------------------------------
 */

void
glPenCompute(chanList, netList)
    GCRChannel *chanList;	/* All the channels in the routing problem */
    NLNetList *netList;	/* Netlist being routed */
{
#ifdef	notdef
    CZone *czones, *cz;
    GlobChan *gc;
    GCRChannel *ch;
    NLNet *net;

    /*
     * Process nets in the order they appear, since this routing
     * is order-independent.  After all done, we have remembered
     * the GlPoints for all a net's segments in the NetClient nc_paths
     * list for that net.  Also, the density map gc_postDens in each
     * channel's GlobChan has been set.
     */
    for (net = netList->nnl_nets; net; net = net->nnet_next)
	glMultiSteiner((CellUse *) NULL, net, glProcessLoc, glPenSavePath,
		(ClientData) TRUE, (ClientData) NULL);

    /*
     * Set the density map gc_postDens in all channels.
     * The initial contents of gc_postDens should have been set
     * to the same as gc_prevDens by the caller; we just add
     * all the nets routed in the step above.
     */
    for (net = netList->nnl_nets; net; net = net->nnet_next)
	glPenDensitySet(net);

    /*
     * Find all the congested zones in the circuit and store them in
     * a list of CZone structures.
     */
    czones = glPenFindCZones(chanList);

    /*
     * Process each CZone to determine which nets should be assigned
     * penalties for crossing that zone.  The penalties assigned to
     * a net for a CZone in an earlier iteration apply to attempts
     * to route that net around other CZones in later iterations.
     * The final result of this step is to build the nc_pens list
     * for each net's NetClient structure.
     */
    for (cz = czones; cz; cz = cz->cz_next)
	glPenAssignCosts(cz, netList);

    /*
     * Final cleanup: free the GlPoints in each net's nc_paths list,
     * since they're no longer needed.  Also reset gc_postDens in
     * each channel to its initial value, gc_prevDens.
     */
    for (net = netList->nnl_nets; net; net = net->nnet_next)
	glPenCleanNet(net);
    for (ch = chanList; ch; ch = ch->gcr_next)
    {
	gc = (GlobChan *) ch->gcr_client;
	glDMCopy(&gc->gc_prevDens[CZ_COL], &gc->gc_postDens[CZ_COL]);
	glDMCopy(&gc->gc_prevDens[CZ_ROW], &gc->gc_postDens[CZ_ROW]);
    }
#endif	/* notdef */
}

/*
 * ----------------------------------------------------------------------------
 *
 * glPenFindCZones --
 *
 * Build up a list of all congested zones in all channels.
 * See grouter.h for a definition of a congested zone.
 * The density map we search is gc_postDens.
 *
 * Results:
 *	Returns a pointer to the list.
 *
 * Side effects:
 *	Allocates memory.
 *
 * ----------------------------------------------------------------------------
 */

CZone *
glPenFindCZones(chanList)
    GCRChannel *chanList;	/* All the channels in the routing problem */
{
    CZone *czList;
    DensMap *dmap;
    GCRChannel *ch;

    czList = (CZone *) NULL;
    for (ch = chanList; ch; ch = ch->gcr_next)
    {
	dmap = ((GlobChan *) ch->gcr_client)->gc_postDens;
	czList = glPenScanDens(czList, ch, &dmap[CZ_COL], CZ_COL);
	czList = glPenScanDens(czList, ch, &dmap[CZ_ROW], CZ_ROW);
    }

    return czList;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glPenScanDens --
 *
 * Scan for portions of a density map where the density exceeds the
 * capacity, and allocate a CZone for each such range.
 *
 * Results:
 *	Returns a pointer to a CZone list with the new elements
 *	described above prepended to czList.
 *
 * Side effects:
 *	Allocates memory.
 *
 * ----------------------------------------------------------------------------
 */

CZone *
glPenScanDens(czList, ch, dm, type)
    CZone *czList;		/* Prepend to this list */
    GCRChannel *ch;		/* Which channel is being processed */
    DensMap *dm;	/* Map to search */
    int type;			/* Type of zone: CZ_ROW or CZ_COL */
{
    short *val = dm->dm_value;
    CZone *cz;
    int i;

    /* Nothing to do if the capacity is never exceeded */
    if (dm->dm_max <= dm->dm_cap)
	return czList;

    /*
     * Simple state machine to find the start and end of
     * each zone of excess density.  If cz is NULL then
     * we're currently out of a zone; otherwise, we're
     * building one up.  Consider elements of the density
     * map from 1 .. dm->dm_size.
     */
    cz = (CZone *) NULL;
    for (i = 1; i < dm->dm_size; i++)
    {
	if (cz)
	{
	    if (val[i] <= dm->dm_cap)
	    {
		/* End of congested zone */
		cz->cz_hi = i - 1;
		cz = (CZone *) NULL;
	    }
	}
	else
	{
	    if (val[i] > dm->dm_cap)
	    {
		/* Beginning of congested zone */
		cz = (CZone *) mallocMagic((unsigned) (sizeof (CZone)));
		cz->cz_chan = ch;
		cz->cz_type = type;
		cz->cz_lo = i;
		cz->cz_penalty = 0;
		cz->cz_nets = (NetSet *) NULL;
		cz->cz_next = czList;
		czList = cz;
	    }
	}
    }

    /* Take care of case when a CZone extends all the way to the end */
    if (cz) cz->cz_hi = dm->dm_size - 1;

    return czList;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glPenAssignCosts --
 *
 * Find all nets that cross a congested zone and determine how expensive
 * it would be to reroute each if they had to avoid that zone.  Once this
 * cost is determined for all nets affected, pick the N least-cost nets,
 * where N is the number of nets that would have to be re-routed in order
 * to reduce the number running through the congested zone to be within
 * its capacity (N should always be greater than zero).  Prepend a CZone
 * to the np_pens list of each of these N nets NetClients, with a cz_penalty
 * equal to the cost of the most-expensive-to-reroute net.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates memory for the CZones placed on each NetClient's np_pens
 *	list; see above for details.
 *
 * ----------------------------------------------------------------------------
 */

void
glPenAssignCosts(cz, netList)
    CZone *cz;		/* A single CZone being processed */
    NLNetList *netList;	/* List of all nets; we look for nets that cross
			 * this zone.
			 */
{
    int oldCost, newCost, maxCost, numCross, density;
    NetSet *crossNets, *ns, **crossArray, **nsap;
    GlobChan *gc;
    DensMap *dm;
    CZone *czNew;
    NetClient *nc;
    List *list;

    /* Find the nets that use this congested zone */
    crossNets = glPenFindCrossingNets(cz, netList);

    /*
     * For each net in the set, determine how expensive it would be
     * if it couldn't use this congested zone at all.
     */
    maxCost = 0;
    numCross = 0;
    for (ns = crossNets; ns; ns = ns->ns_next)
    {
	oldCost = 0;
	nc = (NetClient *) ns->ns_net->nnet_cdata;

	for (list = nc->nc_paths; list; list = LIST_TAIL(list))
	    oldCost += ((GlPoint *) LIST_FIRST(list))->gl_cost;
	newCost = glPenRerouteNetCost(cz, ns->ns_net);
	ns->ns_cost = newCost - oldCost;
	if (ns->ns_cost > maxCost) maxCost = ns->ns_cost;
	numCross++;
    }

    /*
     * Sort the NetSets on the crossNets list in order of increasing
     * ns_cost, leaving crossArray[i] pointing to the i'th smallest
     * NetSet.
     */
    crossArray = (NetSet **) mallocMagic((unsigned) (numCross * sizeof (NetSet *)));
    for (ns = crossNets, nsap = crossArray; ns; ns = ns->ns_next) *nsap++ = ns;
    qsort(crossArray, numCross, sizeof (NetSet *), glPenSortNetSet);

    /*
     * Now comes the fun part.
     * We must select a group of nets to assign a penalty of maxCost.
     * The best nets to receive this penalty are those whose cost
     * to reroute around 'cz' is least.  However, we also want to
     * assign this penalty to the minimum number of nets necessary
     * to eliminate the congestion at 'cz' (i.e, to reduce the
     * density to equal the channel capacity).
     *
     * The approach below is to start pulling nets out of the
     * congested zone, in order of increasing ns_cost, until
     * the congestion disappears.
     */
    gc = (GlobChan *) cz->cz_chan->gcr_client;
    dm = &gc->gc_postDens[cz->cz_type];
    density = glDMMaxInRange(dm, cz->cz_lo, cz->cz_hi);
    nsap = crossArray;
    while (density > dm->dm_cap)
    {
	ns = *nsap++;
	nc = (NetClient *) ns->ns_net->nnet_cdata;
	czNew = (CZone *) mallocMagic((unsigned) (sizeof (CZone)));
	*czNew = *cz;
	czNew->cz_penalty = maxCost;
	czNew->cz_nets = (NetSet *) NULL;
	czNew->cz_next = nc->nc_pens;
	nc->nc_pens = czNew;

	/* Update 'dm' to reflect the absence of the net */
	density = glPenDeleteNet(dm, nc->nc_paths, cz);
    }

    /* Cleanup */
    for (ns = crossNets; ns; ns = ns->ns_next)
	freeMagic((char *) ns);
    freeMagic((char *) crossArray);
}

/*
 * glPenSortNetSet --
 *
 * Called by qsort() to compare the ns_cost values of two NetSets
 * pointed to by *ns1 and *ns2 respectively.
 *
 * Results:
 *	(*ns1)->ns_cost ? (*ns2)->ns_cost	returns
 *	---------------------------------	-------
 *			>			   1
 *			<			  -1
 *			=			   0
 *
 * Side effects:
 *	None.
 */

int
glPenSortNetSet(ns1, ns2)
    NetSet **ns1, **ns2;
{
    if ((*ns1)->ns_cost > (*ns2)->ns_cost) return 1;
    if ((*ns1)->ns_cost < (*ns2)->ns_cost) return -1;
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glPenFindCrossingNets --
 *
 * Find all the nets in 'netList' whose routes cross through the
 * congested zone 'cz'.
 *
 * Results:
 *	Returns a list of NetSet structs pointing to the nets
 *	found to cross through 'cz'.
 *
 * Side effects:
 *	Allocates memory.
 *
 * ----------------------------------------------------------------------------
 */

    /* Structure passed to glPenFindCrossingFunc() via glPenEnumCross() */
struct glCrossClient
{
    NLNet	*rcc_net;	/* Net whose segments are being enumerated */
    NetSet	*rcc_set;	/* NetSet being built up */
};

NetSet *
glPenFindCrossingNets(cz, netList)
    CZone *cz;	/* A single CZone being processed */
    NLNetList *netList;	/* List of all nets; we look for nets that cross
			 * this zone.
			 */
{
    struct glCrossClient rcc;
    List *list;
    NLNet *net;

    rcc.rcc_set = (NetSet *) NULL;
    for (net = netList->nnl_nets; net; net = net->nnet_next)
    {
	rcc.rcc_net = net;
	for (list = ((NetClient *) net->nnet_cdata)->nc_paths;
		list;
		list = LIST_TAIL(list))
	{
	    if (glPenEnumCross(cz, (GlPoint *) LIST_FIRST(list),
			glPenFindCrossingFunc, (ClientData) &rcc))
		break;
	}
    }

    return rcc.rcc_set;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glPenFindCrossingFunc --
 *
 * Filter function for glPenFindCrossingNets() above, called by
 * glPenEnumCross() for each segment on an GlPoint that crosses
 * through the CZone 'cz'.
 *
 * Results:
 *	Always returns 1.
 *
 * Side effects:
 *	Allocates a NetSet for rcc->rcc_net and prepends it
 *	to the NetSet list rcc->rcc_set.
 *
 * ----------------------------------------------------------------------------
 */

    /*ARGSUSED*/
int
glPenFindCrossingFunc(cz, srcPin, dstPin, rcc)
    CZone *cz;			/* UNUSED */
    GCRPin *srcPin, *dstPin;	/* UNUSED */
    struct glCrossClient *rcc;
{
    NetSet *ns;

    ns = (NetSet *) mallocMagic((unsigned) (sizeof (NetSet)));
    ns->ns_net = rcc->rcc_net;
    ns->ns_cost = 0;
    ns->ns_next = rcc->rcc_set;
    rcc->rcc_set = ns;
    return 1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glPenEnumCross --
 *
 * Find all the segments on the GlPoint 'rp' that pass through the CZone 'cz'.
 * For each segment, call the supplied procedure (*func)(), which should be
 * of the following form:
 *
 *	int
 *	(*func)(cz, srcPin, dstPin, cdata)
 *	    CZone *cz;			--- same as our argument 'cz'
 *	    GCRPin *srcPin, *dstPin;	--- two pins in cz's channel
 *	    ClientData cdata;		--- same as our argument 'cdata'
 *	{
 *	}
 *
 * This procedure should return 0 if glPenEnumCross() is to continue
 * visiting further segments of the GlPoint, or a non-zero value if
 * we are to stop visiting further segments.
 *
 * Results:
 *	Returns 0 if the end of the GlPoint was reached without
 *	(*func)() returning a non-zero value (or if no segments
 *	crossed through 'cz').  Returns 1 if (*func)() returned
 *	a non-zero value for some segment.
 *
 * Side effects:
 *	Whatever (*func)() does.
 *
 * ----------------------------------------------------------------------------
 */

int
glPenEnumCross(cz, rp, func, cdata)
    CZone *cz;		/* Look for segments passing through here */
    GlPoint *rp;	/* List of GlPoints (linked by gl_path ptrs) */
    int (*func)();		/* Apply to each segment passing through cz */
    ClientData cdata;		/* Passed to (*func)() */
{
    GCRPin *srcPin, *dstPin;
    int cSrc, cDst;

    for ( ; rp->gl_path; rp = rp->gl_path)
    {
	/* Only interested if this segment is in cz_chan */
	srcPin = rp->gl_path->gl_pin;
	if (srcPin->gcr_ch != cz->cz_chan)
	    continue;
	dstPin = rp->gl_pin;
	if (dstPin->gcr_ch != srcPin->gcr_ch)
	    dstPin = dstPin->gcr_linked;

	if (cz->cz_type == CZ_ROW)
	{
	    cSrc = srcPin->gcr_y;
	    cDst = dstPin->gcr_y;
	}
	else
	{
	    cSrc = srcPin->gcr_x;
	    cDst = dstPin->gcr_x;
	}

	if ((cSrc >= cz->cz_lo && cSrc <= cz->cz_hi)
		|| (cDst >= cz->cz_lo && cDst <= cz->cz_hi))
	{
	    if ((*func)(cz, srcPin, dstPin, cdata))
		return 1;
	}
    }

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glPenRerouteNetCost --
 *
 * Determine how expensive it would be for net 'net' if it couldn't
 * be routed through 'cz'.
 *
 * Results:
 *	Cost value.
 *
 * Side effects:
 *	Frees memory; resets the nc_paths list to NULL.
 *
 * ----------------------------------------------------------------------------
 */

int
glPenRerouteNetCost(cz, net)
    CZone *cz;
    NLNet *net;	/* Net to be rerouted */
{
    NetClient *nc = (NetClient *) net->nnet_cdata;
    CZone fakeCZ;
    int cost;

    /* Prepend a fake CZone with infinite cost */
    fakeCZ = *cz;
    fakeCZ.cz_penalty = INFINITY;
    fakeCZ.cz_next = nc->nc_pens;
    nc->nc_pens = &fakeCZ;

    /*
     * Set the channel penalties and perform a normal routing,
     * but just sum the cost of the resultant path instead of
     * storing it anywhere.
     */
    cost = 0;
    glPenSetPerChan(net);
    glMultiSteiner((CellUse *) NULL, net, glProcessLoc, glPenRouteCost,
	    (ClientData) TRUE, (ClientData) &cost);
    glPenClearPerChan(net);

    /* Remove the fake CZone */
    nc->nc_pens = nc->nc_pens->cz_next;

    return cost;
}

    /*ARGSUSED*/
int
glPenRouteCost(rootUse, bestPath, pNetId, pCost)
    CellUse *rootUse;	/* UNUSED */
    GlPoint *bestPath;	/* Best path for this segment */
    NetId *pNetId;	/* UNUSED */
    int *pCost;		/* Add bestPath->gl_cost to this */
{
    *pCost += bestPath->gl_cost;
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glPenDeleteNet --
 *
 * Modify the density map 'dm' to reflect the absence of all
 * segments on 'list' (a List each of whose elements is the first
 * GlPoints on a path of GlPoints) that pass through the CZone 'cz'.
 * Only the entries of the density map that lie between cz->cz_lo
 * and cz->cz_hi (inclusive) are affected.
 *
 * Results:
 *	Returns the new maximum density in 'dm' in the range
 *	cz->cz_lo through cz->cz_hi inclusive.
 *
 * Side effects:
 *	Modifies 'dm' as described above.
 *
 * ----------------------------------------------------------------------------
 */

int
glPenDeleteNet(dm, list, cz)
    DensMap *dm;	/* Update this map */
    List *list;		/* List of paths */
    CZone *cz;		/* Remove all segments passing through 'cz' from 'dm' */
{
    for ( ; list; list = LIST_TAIL(list))
	(void) glPenEnumCross(cz, (GlPoint *) LIST_FIRST(list),
			glPenDeleteFunc, (ClientData) dm);

    return glDMMaxInRange(dm, cz->cz_lo, cz->cz_hi);
}

/*
 * glPenDeleteFunc --
 *
 * Do the real work of glPenDeleteNet() above.  Called by glPenEnumCross()
 * for each segment of the global routing of a net that passes through 'cz'.
 * Updates 'dm' to reflect the ripping up of that portion of the segment
 * that actually passes through 'cz'; portions of 'dm' that lie outside
 * of 'cz' (cz->cz_lo through cz->cz_hi inclusive) are unaffected.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	Modifies 'dm' as described in the comments for glPenDeleteNet().
 */

int
glPenDeleteFunc(cz, srcPin, dstPin, dm)
    CZone *cz;			/* Being passed through by srcPin..dstPin */
    GCRPin *srcPin, *dstPin;	/* Two pins in cz->cz_chan */
    DensMap *dm;	/* Remove srcPin..dstPin segment from 'dm' */
{
    int n;
    int lo, hi;

    if (cz->cz_type == CZ_COL)
    {
	/* Find range of columns spanned by the net */
	lo = MIN(srcPin->gcr_x, dstPin->gcr_x);
	hi = MAX(srcPin->gcr_x, dstPin->gcr_x);
    }
    else
    {
	/* Find range of rows spanned by the net */
	lo = MIN(srcPin->gcr_y, dstPin->gcr_y);
	hi = MAX(srcPin->gcr_y, dstPin->gcr_y);
    }

    /* Clip so we don't modify entries of 'dm' outside of 'cz' */
    lo = MAX(lo, cz->cz_lo);
    lo = MIN(lo, cz->cz_hi);
    hi = MIN(hi, cz->cz_hi);
    hi = MAX(hi, cz->cz_lo);

    /* Rip up this segment */
    for (n = lo; n <= hi; n++)
	dm->dm_value[n]--;

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glPenCleanNet --
 *
 * Free the GlPoints in a net's nc_paths list.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees memory; resets the nc_paths list to NULL.
 *
 * ----------------------------------------------------------------------------
 */

void
glPenCleanNet(net)
    NLNet *net;
{
    List *list;
    NetClient *nc;

    nc = (NetClient *) net->nnet_cdata;
    for (list = nc->nc_paths; list; list = LIST_TAIL(list))
	glPathFreePerm((GlPoint *) LIST_FIRST(list));
    ListDealloc(list);
    nc->nc_paths = (List *) NULL;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glPenSavePath --
 *
 * Make a permanent copy of the GlPoint list 'path', and prepend the
 * first element to the list being maintained for pNetId->netid_net's
 * NetClient nc_paths field.
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
glPenSavePath(rootUse, path, pNetId)
    CellUse *rootUse;	/* UNUSED */
    GlPoint *path;	/* Path linked via gl_path pointers */
    NetId *pNetId;	/* Net and segment identifier */
{
    GlPoint *newpath;
    NetClient *nc;

    nc = (NetClient *) pNetId->netid_net->nnet_cdata;

    /* Keep a permanent copy of the path */
    newpath = glPathCopyPerm(path);
    LIST_ADD(newpath, nc->nc_paths);
}

/*
 * ----------------------------------------------------------------------------
 *
 * glPenDensitySet --
 *
 * Updates the density in the gc_postDens map in each channel
 * reflect all of the paths for 'net'.
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
glPenDensitySet(net)
    NLNet *net;
{
    NetClient *nc = (NetClient *) net->nnet_cdata;
    GCRPin *srcPin, *dstPin;
    GlPoint *rp;
    GlPoint *path;
    List *list;
    NetId netid;

    netid.netid_net = net;
    netid.netid_seg = 0;

    for (list = ((NetClient *) net->nnet_cdata)->nc_paths;
	    list;
	    list = LIST_TAIL(list))
    {
	path = (GlPoint *) LIST_FIRST(list);
	for (rp = path; rp->gl_path; rp = rp->gl_path)
	{
	    srcPin = rp->gl_path->gl_pin;
	    dstPin = rp->gl_pin;
	    if (dstPin->gcr_ch != srcPin->gcr_ch) dstPin = dstPin->gcr_linked;
	    glDensAdjust(((GlobChan *) srcPin->gcr_ch->gcr_client)->gc_postDens,
		    srcPin, dstPin, netid);
	}
    }
}
