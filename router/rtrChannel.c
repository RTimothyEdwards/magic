/* rtrChannel.c -
 *
 *	Code to handle channels and the obstacles within them.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/router/rtrChannel.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "utils/hash.h"
#include "utils/heap.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "gcr/gcr.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/signals.h"
#include "utils/main.h"
#include "router/router.h"
#include "router/rtrDcmpose.h"
#include "grouter/grouter.h"
#include "textio/textio.h"
#include "utils/styles.h"

/*
 * Maps a tile pointer to a channel structure.
 * We use this rather than the client fields of tiles because
 * they are already used to hold flags during channel decomposition.
 */
HashTable RtrTileToChannel;

/* Plane in __CHANNEL__ def holding channel tiles */
Plane *RtrChannelPlane;

/* List of channels created from decomposed tile plane */
GCRChannel *RtrChannelList = NULL;

/* Multiplier for when to make end connections */ 
#ifndef	lint
float RtrEndConst = 1.0;
#else
float RtrEndConst;	/* Sun lint brain death */
#endif	/* lint */

/* Forward declarations */
extern int rtrChannelObstacleMark();
extern void rtrChannelObstaclePins();


/*
 * ----------------------------------------------------------------------------
 *
 * RtrChannelRoute --
 *
 * This procedure invokes the channel router for the channel 'ch'.
 * If the channel is taller than it is wide, swap in x and y before
 * routing.  If the channel has more pins on the left edge than the
 * right, flip left to right before routing.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The parameter *pCount is incremented by the number of errors that
 *	occurred while routing this channel.
 *
 * ----------------------------------------------------------------------------
 */

void
RtrChannelRoute(ch, pCount)
    GCRChannel *ch;
    int *pCount;
{
    GCRChannel *flipped, *flipped_again, *copy;
    int errs1, errs2;

    /*
     * Mark unused stem-tip crossing points as unused once again.
     * (They had been marked as blocked during global routing so
     * the global router wouldn't try to use them to route over
     * cell tops, thereby making it impossible to generate a stem).
     */
    RtrPinsFixStems(ch);

    /*
     * If the channel is taller than it is long, flip it before
     * routing, in order to give the router a better chance of success.
     */
    if (ch->gcr_width < ch->gcr_length)
    {
	flipped = GCRNewChannel(ch->gcr_length, ch->gcr_width);
	GCRNoFlip(ch, flipped);
	errs1 = GCRroute(flipped);
	if (errs1 == 0)
	{
	    /* Save the routing results back in ch.  Clean up and return */
	    GCRNoFlip(flipped, ch);
	    RtrFBPaint(0);
	    goto bottom;
	}

	/* Try again with a left-right flip */
	RtrFBSwitch();
	flipped_again = GCRNewChannel(ch->gcr_length, ch->gcr_width);
	GCRFlipLeftRight(ch, flipped_again);
	errs2 = GCRroute(flipped_again);
	if (GcrDebug)
	    TxError("   Rerouting a channel with %d errors...", errs1);
	if (errs2 < errs1)
	{
	    errs1 = errs2;
	    GCRFlipLeftRight(flipped_again, ch);
	    if (GcrDebug)
		TxError(" to get %d errors\n", errs1);
	    RtrFBPaint(1);
	}
	else
	{
	    GCRNoFlip(flipped, ch);
	    if(GcrDebug)
		TxError(" unsuccessfully.\n");
	    RtrFBPaint(0);
	}
	GCRFreeChannel(flipped_again);
	goto bottom;
    }
    else
    {
	flipped = GCRNewChannel(ch->gcr_width, ch->gcr_length);
	GCRFlipXY(ch, flipped);
	errs1 = GCRroute(flipped);
	if(errs1 == 0)
	{
	    GCRFlipXY(flipped, ch);
	    RtrFBPaint(0);
	    goto bottom;
	}

	RtrFBSwitch();
	flipped_again = GCRNewChannel(flipped->gcr_length, flipped->gcr_width);
	GCRFlipXY(ch, flipped_again);
	copy = GCRNewChannel(flipped->gcr_length, flipped->gcr_width);
	GCRFlipLeftRight(flipped_again, copy);
	if(GcrDebug)
	    TxError("   Rerouting a channel with %d errors ...", errs1);
	errs2 = GCRroute(copy);
	if(errs2 < errs1)
	{
	    errs1 = errs2;
	    GCRFlipLeftRight(copy, flipped);
	    if(GcrDebug)
		TxError(" successfully, with %d errors\n", errs1);
	    RtrFBPaint(1);
	}
	else
	{
	    RtrFBPaint(0);
	    if(GcrDebug)
		TxError(" unsuccessfully\n");
	}

	GCRFlipXY(flipped, ch);
	GCRFreeChannel(flipped_again);
    }

bottom:
    GCRFreeChannel(flipped);
    if (errs1 > 0)
	gcrSaveChannel(ch);
    *pCount += errs1;
    RtrMilestonePrint();
}

/*
 * ----------------------------------------------------------------------------
 *
 * RtrChannelBounds --
 *
 * Figure out the dimensions of the given channel.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The parameters plength and pwidth are filled in with the number
 *	of usable columns and rows in channel.  The Point pointed to
 *	by 'origin' is filled in with x and y coords to go in
 *	ch->origin.
 *
 * ----------------------------------------------------------------------------
 */

void
RtrChannelBounds(loc, pLength, pWidth, origin)
    Rect *loc;		/* Area the channel is to occupy */
    int *pLength;	/* Filled in with # columns in channel */
    int *pWidth;	/* Filled in with # rows in channel */
    Point *origin;	/* Filled in with coords of (0,0) grid point
			 * (one grid line below and to left of first
			 * usable grid point)
			 */
{
    char mesg[256];
    int hi, lo;

    /* Find rightmost and leftmost grid contained within channel area */
    hi = RTR_GRIDDOWN(loc->r_xtop, RtrOrigin.p_x);
    lo = RTR_GRIDUP(loc->r_xbot, RtrOrigin.p_x);
    origin->p_x = lo - RtrGridSpacing;
    if (hi < lo)
    {
	(void) sprintf(mesg, "Degenerate channel at (%d, %d) (%d, %d)",
		loc->r_xbot, loc->r_ybot, loc->r_xtop, loc->r_ytop);
	DBWFeedbackAdd(loc, mesg, EditCellUse->cu_def, 1, STYLE_PALEHIGHLIGHTS);
	TxError("%s\n", mesg);
    }
    *pLength = (hi - lo) / RtrGridSpacing + 1;

    /* Find topmost and bottommost grid contained within channel area */
    hi = RTR_GRIDDOWN(loc->r_ytop, RtrOrigin.p_y);
    lo = RTR_GRIDUP(loc->r_ybot, RtrOrigin.p_y);
    origin->p_y = lo - RtrGridSpacing;
    if (hi < lo)
    {
	(void) sprintf(mesg, "Degenerate channel at (%d, %d) (%d, %d)",
		loc->r_xbot, loc->r_ybot, loc->r_xtop, loc->r_ytop);
	DBWFeedbackAdd(loc, mesg, EditCellUse->cu_def, 1, STYLE_PALEHIGHLIGHTS);
	TxError("%s\n", mesg);
    }
    *pWidth = (hi - lo) / RtrGridSpacing + 1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * RtrChannelObstacles --
 *
 * Searches a channel area for obstacles.  Sets the channel obstacle map
 * to indicate which grid locations have obstacles.  Uses DBTreeSrTiles()
 * to search for obstacles, rather than DBSrPaintArea, since channels can
 * lie over existing cells.
 *
 * Mark obstacles to channel crossings.  Reserve blocked pins for net
 * GCR_BLOCKEDNETID.  Flag obstructed pins to allow the size of their
 * accompanying obstacle to be set later (by RtrHazards()).
 *
 * Only obstacles in this channel are considered; GLBlockPins() must be
 * called later to propagate the obstacle information from the border of
 * this channel to adjacent channels.
 *
 * NOTE: RtrChannelObstacles() and those procedures it calls are heavily
 * tuned for speed, since DBTreeSrTiles can cause flat searching of
 * large portions of a design hierarchy if invoked over the tops of cells.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets the channel obstacle map.
 *
 * ----------------------------------------------------------------------------
 */

void
RtrChannelObstacles(use, ch)
    CellUse *use;
    GCRChannel * ch;
{
    int l, w, up = RtrSubcellSepUp, down = RtrSubcellSepDown;
    TileTypeBitMask allObs;
    SearchContext scx;

    /*
     * Set the obstacle maps in the metal and poly planes.
     * Ensure that the combination of this channel and its
     * neighbors are sufficient to cover all obstacles in
     * the area between the outermost tracks/columns of this
     * channel and those of its neighbors.
     */
    if (RtrSubcellSepUp + RtrSubcellSepDown < RtrGridSpacing)
    {
	/* Shouldn't happen; this is just insurance */
	up = RtrGridSpacing - RtrSubcellSepDown;
    }
    l = ch->gcr_length + 1;
    w = ch->gcr_width + 1;
    scx.scx_area.r_ll = scx.scx_area.r_ur = ch->gcr_origin;
    scx.scx_area.r_xbot -= up;
    scx.scx_area.r_ybot -= up;
    scx.scx_area.r_xtop += l * RtrGridSpacing + down;
    scx.scx_area.r_ytop += w * RtrGridSpacing + down;
    scx.scx_use = use;
    scx.scx_trans = GeoIdentityTransform;
    TTMaskSetMask3(&allObs, &RtrMetalObstacles, &RtrPolyObstacles);
    (void) DBTreeSrTiles(&scx, &allObs, 0, rtrChannelObstacleMark, (ClientData) ch);

    rtrChannelObstaclePins(ch);
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrChannelObstaclePins --
 *
 * Mark obstacles to channel crossings.  An obstacle affects a channel
 * crossing if it lies within the area from the first track of this
 * channel to the first track of the next channel.  Reserve blocked pins
 * for net GCR_BLOCKEDNETID (ILLEGAL).  Flag obstructed pins to allow the
 * size of their accompanying obstacle to be set later.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets the channel obstacle map.
 *
 * ----------------------------------------------------------------------------
 */

void
rtrChannelObstaclePins(ch)
    GCRChannel *ch;
{
    short **res;
    int row, col, end;

    res = ch->gcr_result;
    end = ch->gcr_length + 1;
    for (row = 1; row <= ch->gcr_width; row++)
    {
	if (BLOCK(res[0][row]))
	{
	    ch->gcr_lPins[row].gcr_pId = GCR_BLOCKEDNETID;
	    ch->gcr_lPins[row].gcr_pFlags = GCRBLK;
	}
	else if (!CLEAR(res[0][row])) ch->gcr_lPins[row].gcr_pFlags = GCROBST;

	if (BLOCK(res[end][row]))
	{
	    ch->gcr_rPins[row].gcr_pId = GCR_BLOCKEDNETID;
	    ch->gcr_rPins[row].gcr_pFlags = GCRBLK;
	}
	else if (!CLEAR(res[end][row])) ch->gcr_rPins[row].gcr_pFlags = GCROBST;
    }

    end = ch->gcr_width+1;
    for (col = 1; col <= ch->gcr_length; col++)
    {
	if (BLOCK(res[col][0]))
	{
	    ch->gcr_bPins[col].gcr_pId = GCR_BLOCKEDNETID;
	    ch->gcr_bPins[col].gcr_pFlags = GCRBLK;
	}
	else if (!CLEAR(res[col][0])) ch->gcr_bPins[col].gcr_pFlags = GCROBST;

	if (BLOCK(res[col][end]))
	{
	    ch->gcr_tPins[col].gcr_pId = GCR_BLOCKEDNETID;
	    ch->gcr_tPins[col].gcr_pFlags = GCRBLK;
	}
	else if (!CLEAR(res[col][end])) ch->gcr_tPins[col].gcr_pFlags = GCROBST;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrChannelObstacleMark --
 *
 * Search routine called by DBTreeSrTiles() from RtrChannelObstacles
 * for each tile found in the given area.  If the tile is an obstacle,
 * mark locations in the channel flag map to indicate their presence.
 *
 * In addition to setting the GCRBLKM/GCRBLKP flags, we set the bits
 * GCRBLKT/GCRBLKC to indicate whether the obstacle blocks a track or
 * a column.  These latter bits are used only for initializing the
 * density information for global routing, and should be reset to
 * zero prior to calling the channel router (since they conflict
 * with the bits used to show routing).
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	Sets the obstacle map for obstacles in a given channel.
 *
 * ----------------------------------------------------------------------------
 */

int
rtrChannelObstacleMark(tile, cxp)
    Tile *tile;
    TreeContext *cxp;
{
    short **mcol, *mrow, *mrowend, mask;
    GCRChannel *ch = (GCRChannel *) cxp->tc_filter->tf_arg;
    TileType type = TiGetType(tile);
    int loX, numX, loY;
    short **mcolend;
    int n;
    Rect r, r2;

    mask = 0;
    if (TTMaskHasType(&RtrMetalObstacles, type)) mask |= GCRBLKM;
    if (TTMaskHasType(&RtrPolyObstacles, type)) mask |= GCRBLKP;
    if (mask == 0)
	return (0);

    TITORECT(tile, &r);
    GEOTRANSRECT(&cxp->tc_scx->scx_trans, &r, &r2);

    /*
     * Determine the range of columns in this channel that are blocked
     * by the obstacle: find the grid coordinates enclosing the tile.
     * Clip to a grid 1 larger than the size of the channel.
     */
    n = r2.r_xbot - RtrPaintSepsDown[type] + 1;
    n = (RTR_GRIDUP(n, RtrOrigin.p_x) - ch->gcr_origin.p_x) / RtrGridSpacing;
    loX = MAX(n, 0);
    n = r2.r_xtop + RtrPaintSepsUp[type] - 1;
    n = (RTR_GRIDUP(n, RtrOrigin.p_x) - ch->gcr_origin.p_x) / RtrGridSpacing;
    numX = MIN(n, ch->gcr_length + 1) - loX;

    /* Do the same thing for the rows (n will be the number of rows) */
    n = r2.r_ybot - RtrPaintSepsDown[type] + 1;
    n = (RTR_GRIDUP(n, RtrOrigin.p_y) - ch->gcr_origin.p_y) / RtrGridSpacing;
    loY = MAX(n, 0);
    n = r2.r_ytop + RtrPaintSepsUp[type] - 1;
    n = (RTR_GRIDDOWN(n, RtrOrigin.p_y) - ch->gcr_origin.p_y) / RtrGridSpacing;
    n = MIN(n, ch->gcr_width + 1) - loY;

    /*
     * Figure out whether tracks or columns are being blocked,
     * for purposes of the density initialization.
     */
    if ((mask & (GCRBLKM|GCRBLKP)) == (GCRBLKM|GCRBLKP))
    {
	/* 2-layer obstacles block both tracks and columns */
	mask |= GCRBLKT|GCRBLKC;
    }
    else if (numX < n)
    {
	/* Tall and narrow obstacles block columns */
	mask |= GCRBLKC;
    }
    else
    {
	/* Short and wide obstacles block tracks */
	mask |= GCRBLKT;
    }

    /*
     * Now set the flags in the channel.
     * The following loop is tuned for speed.
     */
    mcol = &ch->gcr_result[loX];
    mcolend = &mcol[numX];
    while (mcol <= mcolend)
    {
	mrow = &(*mcol++)[loY];
	mrowend = &mrow[n];
	while (mrow <= mrowend)
	    *mrow++ |= mask;
    }

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * RtrChannelDensity --
 *
 * Adjusts the density variables gcr_dRowsByCol and gcr_dColsByRow
 * to reflect the commitment of tracks or columns to existing wiring;
 * for each column or track blocked by material, the density for
 * that track is incremented.  Also updates gcr_dMaxByCol and
 * gcr_dMaxByRow, the maximum column and track densities.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initializes ch->gcr_dRowsByCol, ch->gcr_dColsByRow arrays
 *	and the corresponding maximum values ch->gcr_dMaxByCol
 *	and ch->gcr_dMaxByRow.
 *
 * ----------------------------------------------------------------------------
 */

void
RtrChannelDensity(ch)
    GCRChannel *ch;
{
    short *hdens, *vdens, *rptr;
    int col, density;
    short *hdend, *vdend;

    /*
     * Adjust the density information for global routing.
     * Whether a column or track is blocked is indicated by the
     * presence of the GCRBLKC/GCRBLKT flags in the result array.
     * If both bits are set, then both a column and a track are
     * blocked.
     */
    hdens = &ch->gcr_dRowsByCol[1];
    hdend = &ch->gcr_dRowsByCol[ch->gcr_length];
    for (col = 1; hdens <= hdend; hdens++, col++)
    {
	vdens = &ch->gcr_dColsByRow[1];
	vdend = &ch->gcr_dColsByRow[ch->gcr_width];
	rptr = &ch->gcr_result[col][1];
	for ( ; vdens <= vdend; vdens++, rptr++)
	{
	    if (*rptr & GCRBLKT) *hdens += 1;
	    if (*rptr & GCRBLKC) *vdens += 1;
	}
    }

#ifdef	IDENSITY
    bcopy((char *) ch->gcr_dColsByRow, (char *) ch->gcr_iColsByRow,
		(ch->gcr_width + 2) * sizeof (short));
    bcopy((char *) ch->gcr_dRowsByCol, (char *) ch->gcr_iRowsByCol,
		(ch->gcr_length + 2) * sizeof (short));
#endif	/* IDENSITY */

    /* Compute the maximum row and column density */

	/* Column density */
    density = 0;
    hdens = &ch->gcr_dRowsByCol[1];
    hdend = &ch->gcr_dRowsByCol[ch->gcr_length];
    for ( ; hdens <= hdend; hdens++)
	if (*hdens > density) density = *hdens;
    ch->gcr_dMaxByCol = density;

	/* Row density */
    density = 0;
    vdens = &ch->gcr_dColsByRow[1];
    vdend = &ch->gcr_dColsByRow[ch->gcr_width];
    for ( ; vdens <= vdend; vdens++)
	if (*vdens > density) density = *vdens;
    ch->gcr_dMaxByRow = density;
}

/*
 * ----------------------------------------------------------------------------
 *
 * RtrChannelCleanObstacles --
 *
 * Clears the GCRBLKC/GCRBLKT bits in the result array of 'ch'.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Resets the GCRBLKC/GCRBLKT flags in the result array,
 *	since these bits are also used to mean something else
 *	by the channel router.
 *
 * ----------------------------------------------------------------------------
 */

void
RtrChannelCleanObstacles(ch)
    GCRChannel *ch;
{
    short *rptr;
    int row, rtop;
    int col, ctop;

    ctop = ch->gcr_length + 1;
    rtop = ch->gcr_width + 1;
    for (col = 0; col <= ctop; col++)
    {
	rptr = &ch->gcr_result[col][0];
	for (row = 0; row <= rtop; row++)
	    *rptr++ &= ~(GCRBLKT|GCRBLKC);
    }
}
