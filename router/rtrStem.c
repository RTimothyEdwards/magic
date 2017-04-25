/*
 * rtrStem.c -
 *
 *	This file contains procedures associated with stems.  Stems
 *	are little pieces of paint used to make connections between
 *	non-grid-aligned terminals in cells and grid lines at the
 *	edges of channels.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/router/rtrStem.c,v 1.2 2008/12/04 16:21:44 tim Exp $";
#endif  /* not lint */


#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/hash.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/styles.h"
#include "router/router.h"
#include "gcr/gcr.h"
#include "utils/heap.h"
#include "grouter/grouter.h"
#include "utils/netlist.h"
#include "textio/textio.h"
#include "utils/main.h"
#include "utils/malloc.h"
#include "debug/debug.h"
#include "utils/signals.h"
#include "utils/maxrect.h"

/* Used when searching for stems */
typedef struct
{
    int		 stem_dist;		/* Distance of nearest channel */
    int		 stem_dir;		/* Direction from loc */
    Point	 stem_start;		/* Best candidate stem tip point */
    int		 stem_lo, stem_hi;	/* Range of stem points to consider
					 * (one coordinate will be the same
					 * as stem_start; the other will vary
					 * down to stem_lo and up to stem_hi).
					 */
} StemInfo;

static struct dirs			/* List of directions for stems */
{
    int		dr_dir;			/* Direction */
}
    dirs[] = { GEO_NORTH, GEO_SOUTH, GEO_EAST, GEO_WEST, 0 };

#define MAKEBOX(p, r, width, offset) { \
    (r)->r_xbot = (p)->p_x + (offset); \
    (r)->r_xtop = (r)->r_xbot + (width); \
    (r)->r_ybot = (p)->p_y + (offset); \
    (r)->r_ytop = (r)->r_ybot + (width);}


/* Forward declarations */
extern GCRChannel *rtrStemSearch();
extern GCRPin *rtrStemTip();
extern GCRPin *rtrStemTryPin();
extern void rtrStemRange();

bool rtrTreeSrArea();
bool rtrSrArea();
bool rtrStemMask();
bool RtrComputeJogs();


/*
 * ----------------------------------------------------------------------------
 *
 * RtrStemProcessAll --
 *
 * Actually iterate over a netlist assigning crossing points.
 * The real work is done by the caller-supplied procedure (*func)(),
 * which should be of the following form:
 *
 *	bool
 *	(*func)(use, doWarn, loc, term, net, netList)
 *	    CellUse *use;
 *	    bool doWarn;
 *	    NLTermLoc *loc;
 *	    NLTerm *term;
 *	    NLNet *net;
 *	    NLNetList *netList;
 *	{
 *	}
 *
 * It should return TRUE if it was possible to assign a stem tip
 * to 'loc', or FALSE if no location was possible.  It may also
 * append new NLTermLocs to 'loc', with the last appended NLTermLoc
 * pointing to loc->nloc_next.
 *
 * If the argument 'doWarn' to RtrStemProcessAll() is TRUE, then
 * feedback is left for each NLTermLoc for a given NLTerm that cannot
 * be assigned a stem tip; if doWarn is FALSE, then feedback is only
 * left if ALL NLTermLocs for a NLTerm can't be assigned a stem tip.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Whatever (*func)() does.
 *	Sets the nloc_stem, nloc_dir, nloc_chan fields
 *	for each NLTermLoc assigned a stem tip.  When it isn't
 *	possible to assign any crossing points to a NLTermLoc,
 *	it is deleted from the list and we leave feedback.
 *
 * ----------------------------------------------------------------------------
 */

void
RtrStemProcessAll(use, netList, doWarn, func)
    CellUse *use;
    NLNetList *netList;
    bool doWarn;
    bool (*func)();
{
    NLTermLoc *loc, *locFirst, *locPrev, *locNext;
    Rect errArea;
    bool gotAny;
    NLTerm *term;
    NLNet *net;

    RtrMilestoneStart("Assigning stems");
    for (net = netList->nnl_nets; net; net = net->nnet_next)
    {
	for (term = net->nnet_terms; term; term = term->nterm_next)
	{
	    /*
	     * First pass: walk through the list assigning stem tips to
	     * locations.  Because (*func)() can append new NLTermLocs
	     * to 'loc', we remember loc->nloc_next BEFORE calling
	     * (*func)() so we only hit unassigned NLTermLocs in
	     * the list.
	     */
	    gotAny = FALSE;
	    for (loc = term->nterm_locs; loc; loc = locNext)
	    {
		if (SigInterruptPending)
		    goto out;
		locNext = loc->nloc_next;
		if ((*func)(use, doWarn, loc, term, net, netList))
		    gotAny = TRUE;
	    }

	    /*
	     * Go through the list nuking NLTermLocs for which no channel
	     * and crossing could be assigned (i.e, which were unusable).
	     * If we weren't generating feedback for each unusable location
	     * in the first pass, leave feedback here for all locations if
	     * they all turned out to be unusable.
	     */
	    locPrev = locFirst = (NLTermLoc *) NULL;
	    for (loc = term->nterm_locs; loc; loc = loc->nloc_next)
	    {
		if (loc->nloc_chan == (GCRChannel *) NULL)
		{
		    if (!gotAny && !doWarn)
		    {
			GEO_EXPAND(&loc->nloc_rect, 1, &errArea);
			DBWFeedbackAdd(&errArea,
			    "No crossing reachable from terminal",
			    use->cu_def, 1, STYLE_PALEHIGHLIGHTS);
		    }

		    /* Nuke it */
		    if (locPrev) locPrev->nloc_next = loc->nloc_next;
		    freeMagic((char *) loc);
		    continue;
		}
		locPrev = loc;
		if (locFirst == (NLTermLoc *) NULL)
		    locFirst = loc;
	    }

	    /* Nuke any leading elements */
	    term->nterm_locs = locFirst;
	}

	RtrMilestonePrint();
    }

out:
    RtrMilestoneDone();
}

/*
 * ----------------------------------------------------------------------------
 *
 * RtrStemAssignExt --
 *
 * Assign a stem tip to a NLTermLoc 'loc' that lies outside of any
 * routing channels.  We do this by picking a direction from 'loc'
 * in which a channel lies, and then assigning a stem tip on the
 * boundary of that channel as the crossing point.
 *
 * Results:
 *	TRUE if the terminal is okay.
 *
 * Side effects:
 *	Sets loc->nloc_chan, loc->nloc_stem, and loc->nloc_dir.
 *	Marks the pin at the crossing point with 'net' and a
 *	segment-id of GCR_STEMSEGID.  (This segment-id is recognized
 *	by the global router as an unrouted stem).
 *
 * Note:
 *	It is the responsibility of the caller to make sure that
 *	information about routing channels has been set up in
 *	RtrChannelPlane before this procedure is called.
 *
 * ----------------------------------------------------------------------------
 */

bool
RtrStemAssignExt(use, doWarn, loc, term, net)
    CellUse *use;	/* Cell being routed (for feedback) */
    bool doWarn;	/* If TRUE, leave feedback for each bad loc */
    NLTermLoc *loc;	/* Location being assigned */
    NLTerm *term;	/* For nterm_name */
    NLNet *net;		/* For marking pin */
{
    TileType type = loc->nloc_label->lab_type;
    int dirMask, termWidth, pins;
    Rect r, errorArea;
    char errorMesg[200];
    struct dirs *dr;
    GCRPin *pin;
    int rtrStemExpandFunc();

    dirMask = 0;

    /* Compute initial range of directions we'll try */
    r = loc->nloc_rect;
    termWidth = MAX(r.r_xtop - r.r_xbot, r.r_ytop - r.r_ybot);

    if (termWidth == 0)
    {
	/* Expand degenerate pins into the layers on which they're connected */
	int result;
	Rect rdegen = r;
	TileTypeBitMask lmask;
	SearchContext scx;

	rdegen.r_xbot--;
	rdegen.r_ybot--;
	rdegen.r_xtop++;
	rdegen.r_ytop++;

	scx.scx_use = use;
	scx.scx_area = rdegen;
	scx.scx_trans = GeoIdentityTransform;

	TTMaskSetOnlyType(&lmask, type);
	result = DBTreeSrTiles(&scx, &lmask, 0, rtrStemExpandFunc,
			(ClientData)&r);

	termWidth = MAX(r.r_xtop - r.r_xbot, r.r_ytop - r.r_ybot);

	if (result == 0 || termWidth == 0)
	{
	    sprintf(errorMesg, "Terminal is degenerate");
	    goto fail;
	}
    }

    /*
     * Ensure that the terminal is at least wide enough and on
     * a legal layer.
     */
    termWidth = MAX(r.r_xtop - r.r_xbot, r.r_ytop - r.r_ybot);
    if (TTMaskHasType(&DBConnectTbl[RtrMetalType], type))
    {
	if (termWidth < RtrMetalWidth)
	{
	    sprintf(errorMesg, "Terminal must be %d wide to connect to %s",
		RtrMetalWidth, DBTypeLongName(RtrMetalType));
	    goto fail;
	}
	if ( (r.r_xtop - r.r_xbot) >= RtrMetalWidth )
	    dirMask |= (DIR_NORTH|DIR_SOUTH);
	if ( (r.r_ytop - r.r_ybot) >= RtrMetalWidth )
	    dirMask |= (DIR_EAST|DIR_WEST);
    }
    else if (TTMaskHasType(&DBConnectTbl[RtrPolyType], type))
    {
	if (termWidth < RtrPolyWidth)
	{
	    sprintf(errorMesg, "Terminal must be %d wide to connect to %s",
		RtrMetalWidth, DBTypeLongName(RtrMetalType));
	    goto fail;
	}
	if ( (r.r_xtop - r.r_xbot) >= RtrPolyWidth )
	    dirMask |= (DIR_NORTH|DIR_SOUTH);
	if ( (r.r_ytop - r.r_ybot) >= RtrPolyWidth )
	    dirMask |= (DIR_EAST|DIR_WEST);
    }
    else if (RtrMazeStems)
    {
	/* Modified by Tim 7/27/06 ---				 */
	/* Try to find a contact type that connects to this type */
	/* for now, we just set type to be RtrPolyType.  To be	 */
	/* done:  mark this pin as requiring a contact, or draw	 */
	/* it here.						 */

	type = RtrPolyType;

	if (termWidth < RtrPolyWidth)
	{
	    sprintf(errorMesg, "Terminal must be %d wide to connect to %s",
		RtrMetalWidth, DBTypeLongName(RtrMetalType));
	    goto fail;
	}
	if ( (r.r_xtop - r.r_xbot) >= RtrPolyWidth )
	    dirMask |= (DIR_NORTH|DIR_SOUTH);
	if ( (r.r_ytop - r.r_ybot) >= RtrPolyWidth )
	    dirMask |= (DIR_EAST|DIR_WEST);

    }
    else
    {
	sprintf(errorMesg, "Can't have terminal on %s layer:  must connect "
			"to %s or %s (try setting mazestems option?)",
			DBTypeLongName(type),
			DBTypeLongName(RtrMetalType),
			DBTypeLongName(RtrPolyType));
	goto fail;
    }

    /*
     * Overall algorithm: find the nearest channel in 
     * allowable directions and try to assign a stem tip
     * in that direction.
     */
    
    pins = 0;
    loc->nloc_chan == (GCRChannel *) NULL;
    for ( dr = dirs; dr->dr_dir; dr++)
    {
	StemInfo si;

	/*
	 * Try in turn each direction.
	 */
	if ( DIRMASKHASDIR(dirMask, dr->dr_dir) )
	{
	    si.stem_dir  = -1;
	    si.stem_dist = INFINITY;
	    rtrStemRange(loc, dr->dr_dir, &si);
	    if (si.stem_dir != -1)
	    {
		if (pin = rtrStemTip(loc, &si, use))
		{
		    /* Mark the pin as taken */
		    pins++;
		    pin->gcr_pId = (GCRNet *) net;
		    pin->gcr_pSeg = GCR_STEMSEGID;
		}
	    }
	}
    }

    if (pins)
	return (TRUE);

    /* Complain if no stem tip could be assigned */

    (void) sprintf(errorMesg, "Can't find a channel in any direction from terminal");

fail:
    if (doWarn)
    {
	GEO_EXPAND(&r, 1, &errorArea);
	DBWFeedbackAdd(&errorArea, errorMesg, use->cu_def,
		1, STYLE_PALEHIGHLIGHTS);
    }
    return (FALSE);
}

/* Routine to expand rectangle into touching tiles of a label's type. */

int
rtrStemExpandFunc(Tile *t, TreeContext *cxp)
{
    SearchContext *scx = cxp->tc_scx;
    Rect rsrc;
    Rect *r = (Rect *)cxp->tc_filter->tf_arg;
    CellDef *def = scx->scx_use->cu_def;
    Rect *rtest;
    TileType ttype;
    Point p;

    TiToRect(t, &rsrc);
    ttype = TiGetType(t);

    p.p_x = (rsrc.r_xtop + rsrc.r_xbot) / 2;
    p.p_y = (rsrc.r_ytop + rsrc.r_ybot) / 2;

    rtest = FindMaxRectangle(&TiPlaneRect, def->cd_planes[cxp->tc_plane], &p,
			&DBConnectTbl[ttype]);
    if (rtest)
    {
        GeoTransRect(&scx->scx_trans, rtest, r); /* Copy the final rectangle */
	return 1;
    }
    else
	return 0;	/* Probably should report an error and stop */
}


/*
 * ----------------------------------------------------------------------------
 *
 * rtrStemTip --
 *
 * Given a terminal, this procedure finds the channel crossing point
 * to which its stem will extend.  Ensures that the pin at that grid
 * point isn't blocked, and hasn't already been assigned to another
 * terminal.
 *
 * Results:
 *	Returns a pointer to the pin at the crossing if successful,
 *	or NULL on a failure.
 *
 * Side effects:
 *	Fills in loc->nloc_stem, loc->nloc_dir, and loc->nloc_chan
 *	with the location of a channel crossing point, the direction to
 *	reach it, and the channel containing it respectively.  The
 *	crossing point is the nearest place where a grid-line crosses
 *	into a channel.  One, but probably not both, of the point's
 *	coordinates will be grid-aligned.  If FALSE is returned, then
 *	point is not defined.  Rectangular labels and those inside cells
 *	are handled by searching outward for the nearest channel boundary.
 *
 *	The value of loc_dir is a direction, or -1.  For example, GEO_NORTH
 *	means that the stem runs northward (the terminal is on the top
 *	of a cell).  A value of -1 means that something was wrong with
 *	the terminal (not on a routable layer, too small, wrong shape,
 *	etc.)
 *
 * ----------------------------------------------------------------------------
 */

GCRPin *
rtrStemTip(loc, si, use)
    NLTermLoc *loc;	/* Location whose stem tip is being found */
    StemInfo *si;		/* Stem information */
    CellUse *use;
{
    Point plo, phi;
    int *lo, *hi;
    GCRPin *pin;

    /*
     * Try each crossing point in the best direction, starting from the
     * stem_start point and working outward toward stem_lo and stem_hi.
     */
    if (pin = rtrStemTryPin(loc, si->stem_dir, &si->stem_start, use))
	return (pin);

    plo = phi = si->stem_start;
    switch (si->stem_dir)
    {
	case GEO_NORTH: case GEO_SOUTH:	lo = &plo.p_x; hi = &phi.p_x; break;
	case GEO_EAST: case GEO_WEST:	lo = &plo.p_y; hi = &phi.p_y; break;
    }

    for ( ; *lo >= si->stem_lo || *hi <= si->stem_hi;
	    *lo -= RtrGridSpacing, *hi += RtrGridSpacing)
    {
	if (*lo >= si->stem_lo &&
	    (pin = rtrStemTryPin(loc, si->stem_dir, &plo, use)))
		return (pin);
	if (*hi >= si->stem_hi &&
	    (pin = rtrStemTryPin(loc, si->stem_dir, &phi, use)))
		return (pin);
    }
    return ((GCRPin *) NULL);
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrAbort --
 *	DBTreeSrTile function.  Called to abort tile search
 *	if a tile is found in the stem bounding box.
 *
 * Results:
 *	Always returns 1 to abort DBTreeSrTiles.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
rtrAbort(tile)
    Tile *tile;
{
    return 1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrStemTryPin --
 *
 * Check to see if a particular crossing point is available.  If it is,
 * assign it to loc.
 *
 * Results:
 *	Returns a pointer to the GCRPin for the crossing if successful,
 *	or NULL on failure.
 *
 * Side effects:
 *	Fills in loc->nloc_stem, loc->nloc_dir, and loc->nloc_chan
 *	with the location of a channel crossing point, the direction to
 *	reach it, and the channel containing it respectively.
 *
 * ----------------------------------------------------------------------------
 */

GCRPin *
rtrStemTryPin(loc, dir, p, use)
    NLTermLoc *loc;	/* Try to assign the GCRPin for p to this loc */
    int dir;		/* Direction away from loc that p lies */
    Point *p;		/* Crossing point to try */
    CellUse *use;
{
    Point pSearch;
    GCRChannel *ch;
    GCRPin *pin;
    Tile *tp;

    pSearch = *p;
    if (dir == GEO_SOUTH) pSearch.p_y--;
    if (dir == GEO_WEST) pSearch.p_x--;

    /* Make sure there's a channel there */
    tp = TiSrPointNoHint(RtrChannelPlane, &pSearch);
    if (TiGetType(tp) != TT_SPACE)
	return ((GCRPin *) NULL);
    ch = (GCRChannel *) tp->ti_client;
    if (ch == (GCRChannel *) NULL || ch->gcr_type != CHAN_NORMAL)
	return ((GCRPin *) NULL);

    /*
     * Ensure that the pin is on the proper channel boundary.
     * Remember, dir is the direction in which 'ch' lies from 'loc',
     * so the actual SIDE of 'ch' we want to compare with 'p' is
     * opposite from the direction (e.g, dir == GEO_NORTH means
     * look at the TOP of ch).
     */
    switch (dir)
    {
	case GEO_NORTH:
	    if (p->p_y != ch->gcr_area.r_ybot) return ((GCRPin *) NULL);
	    break;
	case GEO_SOUTH:
	    if (p->p_y != ch->gcr_area.r_ytop) return ((GCRPin *) NULL);
	    break;
	case GEO_EAST:
	    if (p->p_x != ch->gcr_area.r_xbot) return ((GCRPin *) NULL);
	    break;
	case GEO_WEST:
	    if (p->p_x != ch->gcr_area.r_xtop) return ((GCRPin *) NULL);
	    break;
    }

    /* Find the pin for the crossing */
    pin = RtrPointToPin(ch, GeoOppositePos[dir], p);
    if (pin == (GCRPin *) NULL || pin->gcr_pId)
	return ((GCRPin *) NULL);
    ASSERT(pin->gcr_point.p_x == p->p_x, "rtrStemTryPin");
    ASSERT(pin->gcr_point.p_y == p->p_y, "rtrStemTryPin");

    /*
     * Verify stem extending to channel will not overlap
     * material in the edit cell or nested subcells.
     */
    
    if ( rtrTreeSrArea(loc, dir, p, use) )
	return (GCRPin *) NULL;

    /*
     * Assign crossing to loc.
     * If a previous loc has been assigned,
     * allocate a new location for the terminal and link into list.
     */

    if ( loc->nloc_chan )
    {
	NLTermLoc *nloc;

	nloc = (NLTermLoc *) mallocMagic((unsigned) (sizeof (NLTermLoc)));
	*nloc = *loc;
	loc->nloc_next = nloc;
	loc = nloc;
    }

    loc->nloc_stem = *p;
    loc->nloc_dir = dir;
    loc->nloc_chan = ch;
    loc->nloc_pin = pin;
    return (pin);
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrTreeSrArea --
 *	Search edit cell and nested subcells
 *	for material which might short or cause design rule constraints
 *	with a generated stem.
 *	The checking is primitive and just looks for tiles in
 *	the area of the stem bounding box using upper limits for
 *	tile separations.
 *
 * Results:
 *	Returns TRUE if conflicting material is present.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
rtrTreeSrArea(loc, dir, p, use)
    NLTermLoc *loc;	/* Terminal location under consideration */
    int dir;		/* Direction away from loc that p lies */
    Point *p;		/* Point at channel boundary */
    CellUse *use;	/* Parent cell use */
{
    Rect tmp, tmp1, tmp2;
    Point contact, jog, start;
    int i, width = MAX(RtrMetalWidth, RtrPolyWidth);
    int delta;

    /*
     * Compute intersection points for a jogged stem.
     */
    RtrComputeJogs(loc, p, dir, &contact, &jog, &start, width);

    /*
     * Compute maximum separation.
     */
    delta = 0;
    for ( i = 0; i < TT_MAXTYPES; i++)
    {
	delta = MAX(delta,RtrMetalSeps[i]);
	delta = MAX(delta,RtrPolySeps[i]);
    }

    /*
     * Compute bounding boxes for stem sgements and check each area.
     */
    MAKEBOX(&start, &tmp1, width, 0);
    MAKEBOX(&jog, &tmp, width, 0);
    GeoInclude(&tmp1, &tmp);
    if ( rtrSrArea(dir,use, &tmp,delta) )
	return TRUE;

    MAKEBOX(&jog, &tmp1, width, 0);
    MAKEBOX(&contact, &tmp, width, 0);
    GeoInclude(&tmp1, &tmp);
    if ( rtrSrArea(dir,use, &tmp,delta) )
	return TRUE;

    MAKEBOX(&contact, &tmp1, width, 0);
    MAKEBOX(p, &tmp, width, 0);
    GeoInclude(&tmp1, &tmp);
    if ( rtrSrArea(dir,use, &tmp,delta) )
	return TRUE;

    /*
     * If debug, display stem bounding box as feedback.
     */

    if (DebugIsSet(glDebugID, glDebStemsOnly))
    {
	char errorMesg[256];

	MAKEBOX(&start, &tmp1, width, 0);
	MAKEBOX(p, &tmp, width, 0);
	GeoInclude(&tmp1, &tmp);
	sprintf(errorMesg,
	    "Stem tip for terminal %s", loc->nloc_term->nterm_name);
	DBWFeedbackAdd(&tmp, errorMesg, use->cu_def,
	    1, STYLE_PALEHIGHLIGHTS);
    }

    return FALSE;
}

bool
rtrSrArea(dir,use,tmp,delta)
    int	dir;
    CellUse *use;
    Rect *tmp;
    int delta;
{
    SearchContext scx;
    TileTypeBitMask r1mask, r2mask;

    /*
     * Expand box in appropriate direction.
     */
    switch ( dir )
    {
	case GEO_NORTH:
	    tmp->r_xbot -= delta;
	    tmp->r_xtop += delta;
	    tmp->r_ytop += delta;
	    break;
	case GEO_SOUTH:
	    tmp->r_xbot -= delta;
	    tmp->r_xtop += delta;
	    tmp->r_ybot -= delta;
	    break;
	case GEO_EAST:
	    tmp->r_ytop += delta;
	    tmp->r_ybot -= delta;
	    tmp->r_xtop += delta;
	    break;
	case GEO_WEST:
	    tmp->r_ytop += delta;
	    tmp->r_ybot -= delta;
	    tmp->r_xbot -= delta;
	    break;
    }

    /*
     * Search edit cell and nested subcells for tiles.
     */
    scx.scx_use = use;
    scx.scx_area = *tmp;
    scx.scx_trans = GeoIdentityTransform;

    /* Modified by Tim 7/27/06---
     *
     * Search ONLY those planes containing route materials types 1 and 2.
     * If either plane (or both) is free of material, we can create a
     * stem.  Only do this if we have chosen to maze route the stem.  The
     * standard stem generator doesn't know how to contact an internal
     * pin from a different plane.
     */

    if (RtrMazeStems)
    {

	TTMaskClearMask3(&r1mask, &DBPlaneTypes[DBPlane(RtrPolyType)], &DBSpaceBits);
	TTMaskClearMask3(&r2mask, &DBPlaneTypes[DBPlane(RtrMetalType)], &DBSpaceBits);

	if (DBTreeSrTiles(&scx, &r1mask, 0, rtrAbort, (ClientData)0) == 0)
	    return FALSE;

	if (DBTreeSrTiles(&scx, &r2mask, 0, rtrAbort, (ClientData)0) == 0)
	    return FALSE;

	return TRUE;	/* Can't create stem---both planes blocked */
    }
    else	/* Traditional behavior */
    {
	if (DBTreeSrTiles(&scx, &DBAllButSpaceAndDRCBits, 0, rtrAbort, (ClientData)0) )
            return TRUE;
	return FALSE; 
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrStemRange --
 *
 * Search outward from 'loc' in direction 'dir' for the nearest space tile
 * that lies inside the routing area.  If one is found, and it's closer
 * than si->stem_dist, set the following information in *si:
 *
 *	stem_dist	distance to the new channel
 *	stem_dir	dir
 *	stem_start	the closest crossing point on the boundary of
 *			the space tile we just found.
 *	stem_lo,
 *	stem_hi		the range of crossing points to consider;
 *			chosen to extend a certain number of grid
 *			lines to either side of the terminal.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May modify *si as described above.
 *
 * ----------------------------------------------------------------------------
 */

void
rtrStemRange(loc, dir, si)
    NLTermLoc *loc;	/* Terminal we're trying to find a stem for */
    int dir;		/* Direction away from loc that we're searching */
    StemInfo *si;	/* Fill this in if this direction looks best so far */
{
    Rect *area = &loc->nloc_rect;
    Point start, near, center;
    int dist, halfGrid;
    GCRChannel *ch;

    /*
     * Pick the X or Y grid lines such that a contact on that grid line
     * will have its center closest to the center of the terminal.
     * (One of these values will determine the grid line on which
     * the stem tip lies; the other coordinate of the stem tip will
     * be that of the channel boundary, determined below).
     */
    center.p_x = rtrStemContactLine(area->r_xbot, area->r_xtop, RtrOrigin.p_x);
    center.p_y = rtrStemContactLine(area->r_ybot, area->r_ytop, RtrOrigin.p_y);

    /* Search outward in 'dir' for the nearest channel */
    ch = rtrStemSearch(&center, dir, &start);
    if (ch == (GCRChannel *) NULL)
	return;

    /* Find the nearest point of 'area' to 'start' */
    near = start;
    GeoClipPoint(&near, area);

    /*
     * If the distance between near and start is less than for
     * previously visited crossings, remember this side in 'si'.
     */
    dist = ABSDIFF(near.p_x, start.p_x) + ABSDIFF(near.p_y, start.p_y);

    si->stem_dir = dir;
    si->stem_dist = dist;
    si->stem_start = start;

    /* Set the range of grid lines that we will visit */
    halfGrid = RtrGridSpacing/2;
    switch (dir)
    {
	case GEO_NORTH:
	case GEO_SOUTH:
	    si->stem_lo = RTR_GRIDUP(area->r_xbot - halfGrid,
				    RtrOrigin.p_x);
	    si->stem_hi = RTR_GRIDDOWN(area->r_xtop + halfGrid,
				    RtrOrigin.p_x);
	    break;
	case GEO_EAST:
	case GEO_WEST:
	    si->stem_lo = RTR_GRIDUP(area->r_ybot - halfGrid,
				    RtrOrigin.p_y);
	    si->stem_hi = RTR_GRIDDOWN(area->r_ytop + halfGrid,
				    RtrOrigin.p_y);
	    break;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrStemContactLine --
 *
 * Pick the X or Y grid lines such that a contact on that grid line
 * will have its center closest to the center of the terminal.
 *
 * Results:
 *	Returns the grid line described above.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
rtrStemContactLine(lo, hi, origin)
    int lo, hi;		/* Bottom and top, or left and right of terminal */
    int origin;		/* Coordinate of routing grid origin */
{
    int center;

    /*
     * The following code is tricky because we want to round DOWN always
     * in the division by 2, and C rounds towards zero.
     */
    center = lo + hi + RtrGridSpacing - RtrContactWidth;
    if (center < 0) center -= 1;
    center = RTR_GRIDDOWN(center / 2 + RtrContactOffset, origin);

    return (center);
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrStemSearch --
 *
 * Search outward in direction 'dir' from the Point 'center' for
 * a channel inside the routing area.  If one is found, set
 * *point to the crossing on the boundary of that channel that
 * is grid-aligned with 'center'.
 *
 * Results:
 *	Returns the channel found, or NULL if none could be found.
 *
 * Side effects:
 *	Modifies *point.
 *
 * ----------------------------------------------------------------------------
 */

GCRChannel *
rtrStemSearch(center, dir, point)
    Point *center;
    int dir;
    Point *point;
{
    Tile *tile;
    GCRChannel *ch;

    *point = *center;
    for (;;)
    {
	if (!GEO_ENCLOSE(point, &RouteArea))
	    return ((GCRChannel *) NULL);
	tile = TiSrPointNoHint(RtrChannelPlane, point);
	if (TiGetType(tile) == TT_SPACE)
	{
	    if (ch = (GCRChannel *) tile->ti_client)
		break;
	    return ((GCRChannel *) NULL);
	}
	switch (dir)
	{
	    case GEO_NORTH:	point->p_y = TOP(tile); break;
	    case GEO_SOUTH:	point->p_y = BOTTOM(tile) - 1; break;
	    case GEO_EAST:	point->p_x = RIGHT(tile); break;
	    case GEO_WEST:	point->p_x = LEFT(tile) - 1; break;
	}

    }

    /* Pick a crossing point on the channel boundary */
    switch (dir)
    {
	case GEO_NORTH:	point->p_y = ch->gcr_area.r_ybot; break;
	case GEO_SOUTH:	point->p_y = ch->gcr_area.r_ytop; break;
	case GEO_EAST:	point->p_x = ch->gcr_area.r_xbot; break;
	case GEO_WEST:	point->p_x = ch->gcr_area.r_xtop; break;
    }

    return (ch);
}

/*
 * ----------------------------------------------------------------------------
 *
 * RtrStemPaintExt --
 *
 * This procedure places paint in the cell 'use->cu_def' to wire up a stem.
 * The location of the stem has been pre-determined when the
 * NLNetList was built; all we do is implement this decision.
 *
 * Results:
 *	TRUE is returned if the stem was wired successfully.  FALSE
 *	is returned if there was something wrong with the terminal
 *	that prevented a stem from being created.  This includes
 *	the case where no routing material was present at the
 *	stem target location, as might result when the stem was
 *	not used by the global router.
 *
 * Side effects:
 *	Paint is added to cell 'use->cu_def'.  This procedure actually does
 *	two things.  First, it routes the terminal out to a point where
 *	the router can connect to it.  Second, it adds a contact along
 *	the way if one is needed to change layers to the routing material
 *	at the tip of the stem.  Note:  this procedure must not be called
 *	until AFTER the routing paint has been placed, since it uses
 *	the type of routing paint to determine whether or not to place
 *	a contact.  If there's no routing paint, then it doesn't wire
 *	a stem at all.
 *
 *	Leaves feedback if there was an error.
 *
 * ----------------------------------------------------------------------------
 */

bool
RtrStemPaintExt(use, loc)
    CellUse *use;
    NLTermLoc *loc;	/* Terminal whose stem is to be painted */
{
    TileTypeBitMask startMask;	/* Possible layers for first part of stem */
    TileTypeBitMask finalMask;	/* Possible layers for last part of stem */
    TileType startType;		/* Layer actually chosen for start */
    TileType finalType;		/* Layer actually chosen for end */
    Point start;		/* Somewhere along terminal area */
    Point jog;			/* Where the stem crosses the first usable
				 * grid line as it runs out from the cell.
				 */
    Point contact;		/* A second grid point, where a contact can
				 * be placed if necessary.  This is a the
				 * nearest grid crossing to crossing outside
				 * the channel.
				 */
    Rect tmp, paintArea, feedback;
    char *reason, buf[256];
    GCRPin *pin;
    short flags;
    int width;

    /*
     * Find the pin in the channel adjacent to loc to which this
     * stem is being routed.  If this pin wasn't used, there's no
     * point in routing a stem to it so we return.
     */
    pin = loc->nloc_pin;
    if (pin->gcr_pId == (GCRNet *) NULL)
	return (TRUE);

    /* Use crossing already computed */
    if (loc->nloc_dir < 0)
    {
	reason = "Couldn't find crossing point for stem";
	goto failure;
    }

    /* Figure out what kind of material the stem must connect to */
    flags = pin->gcr_ch->gcr_result[pin->gcr_x][pin->gcr_y];
    if (!rtrStemMask(use, loc, flags, &startMask, &finalMask))
    {
	reason = "Terminal is not on a legal routing layer";
	goto failure;
    }

    /* Don't complain if no routing material is present */
    if (!TTMaskHasType(&finalMask, RtrMetalType)
	    && !TTMaskHasType(&finalMask, RtrPolyType))
        return (FALSE);

    rtrStemTypes(&startMask, &finalMask, &startType, &finalType);

    width = (startType == RtrPolyType) ? RtrPolyWidth : RtrMetalWidth;

    /*
     * Compute jog points.
     */

    if ( RtrComputeJogs(loc, &loc->nloc_stem, loc->nloc_dir,
	    &contact, &jog, &start, width) )
    {
	(void) sprintf(buf,
	    "Internal error: bad direction (%d) loc->nloc_dir",
		loc->nloc_dir);
	reason = buf;
	goto failure;
    }

    /* Now just connect the dots! */

    /* The first segment runs from the terminal out towards the channel */
    MAKEBOX(&start, &tmp, width, 0);
    MAKEBOX(&jog, &paintArea, width, 0);
    (void) GeoInclude(&tmp, &paintArea);
    RtrPaintStats(startType, start.p_x-jog.p_x+start.p_y-jog.p_y);
    DBPaint(use->cu_def, &paintArea, startType);

    /*
     * For the second segment (over to the contact position), be careful.
     * If there's going to be a contact, widen this segment so there can't
     * be any design-rule-violating slivers left between the segment above
     * and the contact.  Also place the contact.
     */
    MAKEBOX(&jog, &tmp, width, 0);
    if (startType != finalType)
    {
	MAKEBOX(&contact, &paintArea, RtrContactWidth, RtrContactOffset);
	RtrPaintContact(use->cu_def, &paintArea);
    }
    else MAKEBOX(&contact, &paintArea, width, 0);
    (void) GeoInclude(&tmp, &paintArea);
    RtrPaintStats(startType, jog.p_x-contact.p_x+jog.p_y-contact.p_y);
    DBPaint(use->cu_def, &paintArea, startType);

    /* Figure out what type to enter the channel with */
    width = (finalType == RtrMetalType) ? RtrMetalWidth : RtrPolyWidth;

    /* Make the last run down to the channel */
    MAKEBOX(&contact, &tmp, width, 0);
    MAKEBOX(&loc->nloc_stem, &paintArea, width, 0);
    (void) GeoInclude(&tmp, &paintArea);
    RtrPaintStats(finalType,
	    contact.p_x-loc->nloc_stem.p_x+contact.p_y-loc->nloc_stem.p_y);
    DBPaint(use->cu_def, &paintArea, finalType);
    return (TRUE);

failure:
    GEO_EXPAND(&loc->nloc_rect, 1, &feedback);
    DBWFeedbackAdd(&feedback, reason, use->cu_def, 1, STYLE_PALEHIGHLIGHTS);
    return (FALSE);
}

/*
 * Figure out what kind of material the stem must connect to.
 * If BOTH routing layers are present at the stem tip, select the
 * vertical or horizontal layer, depending on the direction of
 * the stem.  If there's a contact out there, then either layer
 * is OK.
 */

bool
rtrStemMask(routeUse, loc, flags, startMask, finalMask)
    CellUse *routeUse;		/* Cell being routed */
    NLTermLoc *loc;		/* Terminal */
    int flags;			/* Blockage flags in the channel at the
				 * crossing point.  If a layer is marked
				 * as blocked in these flags, it is excluded
				 * from finalMask since the channel router
				 * will not have used it for routing a
				 * signal.
				 */
    TileTypeBitMask *startMask;	/* Possible types for terminal */
    TileTypeBitMask *finalMask;	/* Possible types for stem tip */
{
    Rect tmp;

    /*
     * Figure out what kind of material the stem must connect to.
     * If BOTH routing layers are present at the stem tip, select the
     * vertical or horizontal layer, depending on the direction of
     * the stem.  If there's a contact out there, then either layer
     * is OK.
     */
    tmp.r_xbot = loc->nloc_stem.p_x - 1;
    tmp.r_xtop = loc->nloc_stem.p_x + 1;
    tmp.r_ybot = loc->nloc_stem.p_y - 1;
    tmp.r_ytop = loc->nloc_stem.p_y + 1;
    DBSeeTypesAll(routeUse, &tmp, 0, finalMask);
    if (TTMaskHasType(finalMask, RtrMetalType) &&
            TTMaskHasType(finalMask, RtrPolyType))
    {
        if (loc->nloc_dir == GEO_NORTH || loc->nloc_dir == GEO_SOUTH)
            TTMaskClearType(finalMask, RtrMetalType);
        else TTMaskClearType(finalMask, RtrPolyType);
    }
    if (flags & GCRBLKM) TTMaskClearType(finalMask, RtrMetalType);
    if (flags & GCRBLKP) TTMaskClearType(finalMask, RtrPolyType);

    if (TTMaskHasType(finalMask, RtrContactType))
    {
	TTMaskSetType(finalMask, RtrMetalType);
	TTMaskSetType(finalMask, RtrPolyType);
    }

    /* Now figure out what kind of material we'll be using for the
     * first and last parts of the stem.  Use the same material in
     * both places if possible.
     */
    *startMask = DBConnectTbl[loc->nloc_label->lab_type];
    if (!TTMaskHasType(startMask, RtrMetalType)
	    && !TTMaskHasType(startMask, RtrPolyType))
    {
	return (FALSE);
    }

    return (TRUE);
}

int
rtrStemTypes(startMask, finalMask, startType, finalType)
    TileTypeBitMask *startMask, *finalMask;
    TileType *startType, *finalType;
{
    if (!TTMaskHasType(finalMask, RtrMetalType))
    {
	*finalType = RtrPolyType;
	if (TTMaskHasType(startMask, RtrPolyType))
	    *startType = RtrPolyType;
	else *startType = RtrMetalType;
    }
    else if (!TTMaskHasType(finalMask, RtrPolyType))
    {
	*finalType = RtrMetalType;
	if (TTMaskHasType(startMask, RtrMetalType))
	    *startType = RtrMetalType;
	else *startType = RtrPolyType;
    }
    else
    {
	/* Both types are present in the channel; either will do */
	if (TTMaskHasType(startMask, RtrMetalType))
	    *startType = *finalType = RtrMetalType;
	else *startType = *finalType = RtrPolyType;
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * RtrComputeJogs --
 *	Compute points defining the jog required to connect a
 *	terminal to a grid line in an adjacent channel.
 *	This can be used to determine the stem bounding box
 *	or to actually paint the stem.
 *
 * Results:
 *	TRUE on success, FALSE on failure.
 *
 * Side effects:
 *	Returns contact, jog, and start points, passed via pointer.
 *
 * ----------------------------------------------------------------------------
 */

bool
RtrComputeJogs(loc, stem, dir, contact, jog, start, width)
    NLTermLoc *loc;	/* Terminal whose stem is to be painted */
    Point *stem;		/* Point intersecting channel*/
    Point *start;		/* Somewhere along terminal area */
    Point *jog;			/* Where the stem crosses the first usable
				 * grid line as it runs out from the cell.
				 */
    Point *contact;		/* A second grid point, where a contact can
				 * be placed if necessary.  This is a the
				 * nearest grid crossing to crossing outside
				 * the channel.
				 */
{
    Rect *area;
    area = &loc->nloc_rect;

    /* In the following code, we compute two jog points in the stem.
     * The stem will run out from the terminal to the first usable
     * grid line, then jog over to a grid line in the other direction,
     * then jog down to the channel boundary.  The second jog is called
     * the contact point, since we change layers here if that is
     * necessary.
     */
    switch (dir)
    {
	case GEO_NORTH:
	    contact->p_y = RTR_GRIDDOWN(stem->p_y, RtrOrigin.p_y);
	    contact->p_x = stem->p_x;
	    *jog = *contact;
	    if (jog->p_x < area->r_xbot)
		jog->p_x = area->r_xbot;
	    else if (jog->p_x > (area->r_xtop - width))
		jog->p_x = area->r_xtop - width;
	    start->p_x = jog->p_x;
	    start->p_y = area->r_ytop;
	    break;

	case GEO_SOUTH:
	    contact->p_y = RTR_GRIDUP(stem->p_y, RtrOrigin.p_y);
	    contact->p_x = stem->p_x;
	    *jog = *contact;
	    if (jog->p_x < area->r_xbot)
		jog->p_x = area->r_xbot;
	    else if (jog->p_x > (area->r_xtop - width))
		jog->p_x = area->r_xtop - width;
	    start->p_x = jog->p_x;
	    start->p_y = area->r_ybot - width;
	    break;
	
	case GEO_EAST:
	    contact->p_x = RTR_GRIDDOWN(stem->p_x, RtrOrigin.p_x);
	    contact->p_y = stem->p_y;
	    *jog = *contact;
	    if (jog->p_y < area->r_ybot)
		jog->p_y = area->r_ybot;
	    else if (jog->p_y > (area->r_ytop - width))
		jog->p_y = area->r_ytop - width;
	    start->p_y = jog->p_y;
	    start->p_x = area->r_xtop;
	    break;
	
	case GEO_WEST:
	    contact->p_x = RTR_GRIDUP(stem->p_x, RtrOrigin.p_x);
	    contact->p_y = stem->p_y;
	    *jog = *contact;
	    if (jog->p_y < area->r_ybot)
		jog->p_y = area->r_ybot;
	    else if (jog->p_y > (area->r_ytop - width))
		jog->p_y = area->r_ytop - width;
	    start->p_y = jog->p_y;
	    start->p_x = area->r_xbot - width;
	    break;
	
	default:
	    return TRUE;
    }
    return FALSE;
}
