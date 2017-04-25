/*
 * gaSimple.c -
 *
 * Code to try very simple stems 
 * (straight line connection from pin to terminal 
 *  with a possible contact at or near each end)
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/garouter/gaSimple.c,v 1.2 2009/05/13 15:03:16 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
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
#include "utils/malloc.h"
#include "drc/drc.h"
#include "debug/debug.h"
#include "extract/extract.h"
#include "extract/extractInt.h"

bool gaIsClear();

/*--- Externals referenced in this file and not declared in include files ---*/
extern int gaMetalClear;
extern int gaPolyClear;
extern int gaContactClear;


/*
 * ----------------------------------------------------------------------------
 *
 * gaStemSimpleInit --
 *
 * Initialize the SimpleStem struct to contain information on what 
 * simple routes are possible.
 *
 * Simple routes are grid aligned straight routes from the terminal 
 * to the pin, with a possible contact at the terminal and another near
 * the pin on the grid line just before the channel edge.
 *
 * If this routine returns TRUE, gaStemSimpleRoute() must still be called
 * to determine if a simple route is possible (and optionally to paint the
 * route into a cell).
 *
 * Results:
 *	TRUE if successful, FALSE if it was not possible to make a
 *	grid-aligned route.
 *
 * Side effects:
 *	Fills in the fields of *simple.
 *
 * ----------------------------------------------------------------------------
 */

bool
gaStemSimpleInit(routeUse, term, pinPoint, pinSide, simple)
    CellUse *routeUse;			/* Search this cell for obstacles */
    NLTermLoc *term;			/* Route from term->nloc_rect */
    Point *pinPoint;		/* Crossing point to reach */
    int pinSide;			/* Direction of pin from term. */
    SimpleStem *simple;	/* Fill this in */
{

    SimpleWire *sMetal = &simple->ss_metalWire;
    SimpleWire *sPoly = &simple->ss_polyWire;
    Rect *cTerm = &simple->ss_cTerm;
    Rect *cPin = &simple->ss_cPin;
    Rect *termLoc = &term->nloc_rect;

    /*
     * Check that the start terminal is big enough for the route to be
     * grid-aligned.  The start terminal has to be big enough
     * to hold a grid-aligned wire on each of the routing types.
     */
    {
	int bot, top;

	switch (pinSide)
	{
	    case GEO_NORTH:
	    case GEO_SOUTH:
	    {
		bot = pinPoint->p_x;
		top = termLoc->r_xtop;
		if (bot < termLoc->r_xbot)
		return (FALSE);
		break;
	    }

	    case GEO_EAST:
	    case GEO_WEST:
	    {
		bot = pinPoint->p_y;
		top = termLoc->r_ytop;
		if (bot < termLoc->r_ybot)
		return (FALSE);
		break;
	    }
	}
	if (bot + RtrMetalWidth > top || bot + RtrPolyWidth > top)
	return (FALSE);
    }

    /* Misc initialization */
    {	
	simple->ss_dir = pinSide;
	simple->ss_pinPoint = *pinPoint;
	simple->ss_termArea = term->nloc_rect;
	simple->ss_termType = term->nloc_label->lab_type;

	sMetal->sw_type = RtrMetalType;
	sPoly->sw_type = RtrPolyType;
    }

    /* Determine which types connect to terminal directly 
     * (without adding a contact) 
     */
    if (simple->ss_termType == RtrContactType)
    {
	TTMaskSetOnlyType(&(simple->ss_termMask), RtrMetalType);
	TTMaskSetType(&(simple->ss_termMask), RtrPolyType);
    }
    else 
    {
	TTMaskSetOnlyType(&(simple->ss_termMask), simple->ss_termType);
    }

    /*
     * Compute contact and wire areas.
     * Defn of areas:  
     *    - long wires run from terminal to pin,
     *    - short wires run from terminal to contact near pin, 
     *    - pinStubs run from pinContact to pin.
     */
    {
	switch (pinSide)
	{
	    case GEO_NORTH:	
	    /* pin is directly above terminal */
	    {
		/* compute terminal contact location */
		cTerm->r_xbot = pinPoint->p_x + RtrContactOffset;
		cTerm->r_ybot = termLoc->r_ytop - RtrContactWidth;
		cTerm->r_xtop = cTerm->r_xbot + RtrContactWidth;
		cTerm->r_ytop = cTerm->r_ybot + RtrContactWidth;

		/* compute pin contact location */
		cPin->r_xbot = pinPoint->p_x + RtrContactOffset;
		cPin->r_ybot = (RTR_GRIDDOWN(pinPoint->p_y, RtrOrigin.p_y) 
			      + RtrContactOffset); 
		cPin->r_xtop = cPin->r_xbot + RtrContactWidth;
		cPin->r_ytop = cPin->r_ybot + RtrContactWidth;

		/* compute poly pin stub location */
		sPoly->sw_pinStub.r_xbot = pinPoint->p_x;
		sPoly->sw_pinStub.r_ybot = cPin->r_ytop;
		sPoly->sw_pinStub.r_xtop = pinPoint->p_x + RtrPolyWidth;
		sPoly->sw_pinStub.r_ytop = pinPoint->p_y;

		/* compute metal pin stub location */
		sMetal->sw_pinStub.r_xbot = pinPoint->p_x;
		sMetal->sw_pinStub.r_ybot = cPin->r_ytop;
		sMetal->sw_pinStub.r_xtop = pinPoint->p_x + RtrMetalWidth;
		sMetal->sw_pinStub.r_ytop = pinPoint->p_y;

		/* compute short metal wire */
		sMetal->sw_short.r_xbot = pinPoint->p_x;
		sMetal->sw_short.r_ybot = termLoc->r_ytop;
		sMetal->sw_short.r_xtop = pinPoint->p_x + RtrMetalWidth;
		sMetal->sw_short.r_ytop = cPin->r_ybot;

		/* compute short poly wire */
		sPoly->sw_short.r_xbot = pinPoint->p_x;
		sPoly->sw_short.r_xtop = pinPoint->p_x + RtrPolyWidth;
		sPoly->sw_short.r_ybot = termLoc->r_ytop;
		sPoly->sw_short.r_ytop = cPin->r_ybot;

		/* compute long metal wire */
		sMetal->sw_long = sMetal->sw_short;
		sMetal->sw_long.r_ytop = pinPoint->p_y;

		/* compute long poly wire */
		sPoly->sw_long = sPoly->sw_short;
		sPoly->sw_long.r_ytop = pinPoint->p_y;
		break;
	    }

	    case GEO_SOUTH: 
	    /* pin is directly below terminal */
	    {
		/* compute terminal contact location */
		cTerm->r_xbot = pinPoint->p_x + RtrContactOffset;
		cTerm->r_ybot = termLoc->r_ybot; 
		cTerm->r_xtop = cTerm->r_xbot + RtrContactWidth;
		cTerm->r_ytop = cTerm->r_ybot + RtrContactWidth;

		/* compute pin contact location */
		cPin->r_xbot = pinPoint->p_x + RtrContactOffset;
		cPin->r_ybot = (RTR_GRIDUP(pinPoint->p_y, RtrOrigin.p_y) 
			      + RtrContactOffset);
		cPin->r_xtop = cPin->r_xbot + RtrContactWidth;
		cPin->r_ytop = cPin->r_ybot + RtrContactWidth;

		/* compute poly pin stub location */
		sPoly->sw_pinStub.r_xbot = pinPoint->p_x;
		sPoly->sw_pinStub.r_ybot = pinPoint->p_y;
		sPoly->sw_pinStub.r_xtop = pinPoint->p_x + RtrPolyWidth;
		sPoly->sw_pinStub.r_ytop = cPin->r_ybot;

		/* compute metal pin stub location */
		sMetal->sw_pinStub.r_xbot = pinPoint->p_x;
		sMetal->sw_pinStub.r_ybot = pinPoint->p_y;
		sMetal->sw_pinStub.r_xtop = pinPoint->p_x + RtrMetalWidth;
		sMetal->sw_pinStub.r_ytop = cPin->r_ybot;

		/* compute short metal wire */
		sMetal->sw_short.r_xbot = pinPoint->p_x;
		sMetal->sw_short.r_ybot = cPin->r_ytop;
		sMetal->sw_short.r_xtop = pinPoint->p_x + RtrMetalWidth;
		sMetal->sw_short.r_ytop = termLoc->r_ybot;

		/* compute short poly wire */
		sPoly->sw_short.r_xbot = pinPoint->p_x;
		sPoly->sw_short.r_ybot = cPin->r_ytop;
		sPoly->sw_short.r_xtop = pinPoint->p_x + RtrPolyWidth;
		sPoly->sw_short.r_ytop = termLoc->r_ybot;

		/* long metal wire */
		sMetal->sw_long = sMetal->sw_short;
		sMetal->sw_long.r_ybot = pinPoint->p_y;

		/* long poly wire */
		sPoly->sw_long = sPoly->sw_short;
		sPoly->sw_long.r_ybot = pinPoint->p_y;

		break;
	    }

	    case GEO_EAST:	
	    /* pin is directly to right of terminal */
	    {
		/* compute terminal contact location */
		cTerm->r_xbot = termLoc->r_xtop - RtrContactWidth;
		cTerm->r_ybot = pinPoint->p_y + RtrContactOffset;
		cTerm->r_xtop = cTerm->r_xbot + RtrContactWidth;
		cTerm->r_ytop = cTerm->r_ybot + RtrContactWidth;

		/* compute pin contact location */
		cPin->r_xbot = (RTR_GRIDDOWN(pinPoint->p_x, RtrOrigin.p_x)
			       + RtrContactOffset);
		cPin->r_ybot = pinPoint->p_y + RtrContactOffset;
		cPin->r_xtop = cPin->r_xbot + RtrContactWidth;
		cPin->r_ytop = cPin->r_ybot + RtrContactWidth;

		/* compute poly pin stub location */
		sPoly->sw_pinStub.r_xbot = cPin->r_xtop;
		sPoly->sw_pinStub.r_ybot = pinPoint->p_y;
		sPoly->sw_pinStub.r_xtop = pinPoint->p_x;
		sPoly->sw_pinStub.r_ytop = pinPoint->p_y + RtrPolyWidth;

		/* compute metal pin stub location */
		sMetal->sw_pinStub.r_xbot = cPin->r_xtop;
		sMetal->sw_pinStub.r_ybot = pinPoint->p_y;
		sMetal->sw_pinStub.r_xtop = pinPoint->p_x;
		sMetal->sw_pinStub.r_ytop = pinPoint->p_y + RtrMetalWidth;

		/* compute short metal wire */
		sMetal->sw_short.r_xbot = termLoc->r_xtop;
		sMetal->sw_short.r_ybot = pinPoint->p_y;
		sMetal->sw_short.r_xtop = cPin->r_xbot;
		sMetal->sw_short.r_ytop = pinPoint->p_y + RtrMetalWidth;

		/* compute short poly wire */
		sPoly->sw_short.r_xbot = termLoc->r_xtop;
		sPoly->sw_short.r_ybot = pinPoint->p_y;
		sPoly->sw_short.r_xtop = cPin->r_xbot;
		sPoly->sw_short.r_ytop = pinPoint->p_y + RtrPolyWidth;

		/* compute long metal wire */
		sMetal->sw_long = sMetal->sw_short;
		sMetal->sw_long.r_xtop = pinPoint->p_x;

		/* compute long poly wire */
		sPoly->sw_long = sPoly->sw_short;
		sPoly->sw_long.r_xtop = pinPoint->p_x;
		break;
	    }

	    case GEO_WEST: 
	    /* pin is directly to left of terminal */
	    {
		/* compute terminal contact location */
		cTerm->r_xbot = termLoc->r_xbot;
		cTerm->r_ybot = pinPoint->p_y + RtrContactOffset;
		cTerm->r_xtop = cTerm->r_xbot + RtrContactWidth;
		cTerm->r_ytop = cTerm->r_ybot + RtrContactWidth;

		/* compute pin contact location */
		cPin->r_xbot = (RTR_GRIDUP(pinPoint->p_x, RtrOrigin.p_x) 
			      + RtrContactOffset);
		cPin->r_ybot = pinPoint->p_y + RtrContactOffset;
		cPin->r_xtop = cPin->r_xbot + RtrContactWidth;
		cPin->r_ytop = cPin->r_ybot + RtrContactWidth;

		/* compute poly pin stub location */
		sPoly->sw_pinStub.r_xbot = pinPoint->p_x;
		sPoly->sw_pinStub.r_ybot = pinPoint->p_y;
		sPoly->sw_pinStub.r_xtop = cPin->r_xbot;
		sPoly->sw_pinStub.r_ytop = pinPoint->p_y + RtrPolyWidth;

		/* compute metal pin stub location */
		sMetal->sw_pinStub.r_xbot = pinPoint->p_x;
		sMetal->sw_pinStub.r_ybot = pinPoint->p_y;
		sMetal->sw_pinStub.r_xtop = cPin->r_xbot;
		sMetal->sw_pinStub.r_ytop = pinPoint->p_y + RtrMetalWidth;

		/* compute short metal wire */
		sMetal->sw_short.r_xbot = cPin->r_xtop;
		sMetal->sw_short.r_ybot = pinPoint->p_y;
		sMetal->sw_short.r_xtop = termLoc->r_xbot;
		sMetal->sw_short.r_ytop = pinPoint->p_y + RtrMetalWidth;

		/* compute short poly wire */
		sPoly->sw_short.r_xbot = cPin->r_xtop;
		sPoly->sw_short.r_ybot = pinPoint->p_y;
		sPoly->sw_short.r_xtop = termLoc->r_xbot;
		sPoly->sw_short.r_ytop = pinPoint->p_y + RtrPolyWidth;

		/* long metal wire */
		sMetal->sw_long = sMetal->sw_short;
		sMetal->sw_long.r_xbot = pinPoint->p_x;

		/* long poly wire */
		sPoly->sw_long = sPoly->sw_short;
		sPoly->sw_long.r_xbot = pinPoint->p_x;

		break;
	    }

	} /* end switch */
    }

    /* check which wires and contacts are blocked */
    {
	Rect rTerm, rPin, rShortMetal, rShortPoly, rLongMetal, rLongPoly;
	Rect rStubMetal, rStubPoly;

	/*
	 * bloat wire and contact areas to produce the areas that must
	 * be clear of obstacles in order for a route to be possible.
	 */
	{
	    GEO_EXPAND(cTerm, gaContactClear, &rTerm);
	    GEO_EXPAND(cPin, gaContactClear, &rPin);
	    GEO_EXPAND(&sMetal->sw_short, gaMetalClear, &rShortMetal);
	    GEO_EXPAND(&sMetal->sw_long, gaMetalClear, &rLongMetal);
	    GEO_EXPAND(&sMetal->sw_pinStub, gaMetalClear, &rStubMetal);
	    GEO_EXPAND(&sPoly->sw_short, gaPolyClear, &rShortPoly);
	    GEO_EXPAND(&sPoly->sw_long, gaPolyClear, &rLongPoly);
	    GEO_EXPAND(&sPoly->sw_pinStub, gaPolyClear, &rStubPoly);
	}

	/*
	 * Prune check areas at terminal and pin to allow prexisting
	 * material at the connection points.
	 *
	 * (NOTE the terminal contact area is not trimmed, but will
	 *  only be checked on the layer (if any) that is not part 
	 *  of the terminal)
	 */
	{
	    Rect pruneR;

	    /* compute prune area */
	    {
		pruneR = routeUse->cu_bbox;
		switch (pinSide)
		{
		    case GEO_NORTH:
		    {
			pruneR.r_ybot = termLoc->r_ytop;
			pruneR.r_ytop = pinPoint->p_y;
			break;
		    }

		    case GEO_SOUTH:
		    {
			pruneR.r_ybot = pinPoint->p_y;
			pruneR.r_ytop = termLoc->r_ybot;
			break;
		    }

		    case GEO_EAST:
		    {
			pruneR.r_xbot = termLoc->r_xtop;
			pruneR.r_xtop = pinPoint->p_x;
			break;
		    }

		    case GEO_WEST:
		    {
			pruneR.r_xbot = pinPoint->p_x;
			pruneR.r_xtop = termLoc->r_xbot;
		    }
		}
	    }

	    /* prune the areas (accept for rTerm) */
	    {
		GEOCLIP(&rPin, &pruneR);
		GEOCLIP(&rShortMetal, &pruneR);
		GEOCLIP(&rLongMetal, &pruneR);
		GEOCLIP(&rStubMetal, &pruneR);
		GEOCLIP(&rShortPoly, &pruneR);
		GEOCLIP(&rLongPoly, &pruneR);
		GEOCLIP(&rStubPoly, &pruneR);
	    }
	}

	/* check areas for obstacles */
	{
	    /* terminal contact */
	    {
		if(!TTMaskHasType(&simple->ss_termMask, RtrMetalType))
		{
		    simple->ss_cTermOK = gaIsClear(routeUse, 
						   &rTerm, 
						   &RtrMetalObstacles);
		}
		if(!TTMaskHasType(&simple->ss_termMask, RtrPolyType))
		{
		    simple->ss_cTermOK = gaIsClear(routeUse, 
						   &rTerm, 
						   &RtrPolyObstacles);
		}
	    }

	    /* pin contact */
	    {
		TileTypeBitMask allObs;

		TTMaskSetMask3(&allObs, &RtrPolyObstacles, &RtrMetalObstacles);
		simple->ss_cPinOK = gaIsClear(routeUse, &rPin, &allObs);
	    }

	    /* short metal wire 
	     * (a short wire is only ok if both the short wire itself and
             * the complementary stub are clear)
	     */
	    sMetal->sw_shortOK = (gaIsClear(routeUse, 
					    &rShortMetal, 
					    &RtrMetalObstacles)
				  &&
				  gaIsClear(routeUse,
					   &rStubPoly,
					   &RtrPolyObstacles));


	    /* long metal wire */
	    sMetal->sw_longOK = gaIsClear(routeUse, 
					  &rLongMetal, 
					  &RtrMetalObstacles);

	    /* short poly wire */
	    sPoly->sw_shortOK = (gaIsClear(routeUse, 
					  &rShortPoly,
					  &RtrPolyObstacles)
				 &&
				 gaIsClear(routeUse,
					   &rStubMetal,
					   &RtrMetalObstacles));

	    /* long poly wire */
	    sPoly->sw_longOK = gaIsClear(routeUse, 
					 &rLongPoly, 
					 &RtrPolyObstacles);
	}
    }

    return (TRUE);
}


/*
 * ----------------------------------------------------------------------------
 *
 * gaIsClear --
 *
 * 
 * Check that r is clear of all material of types in mask.
 *
 * Results:
 *	TRUE if the area is clear, FALSE if not.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
gaIsClear(use, r, mask)
    CellUse *use;
    Rect *r;
    TileTypeBitMask *mask;
{
    int gaIsClearFunc();
    SearchContext scx;

    scx.scx_use = use;
    scx.scx_trans = GeoIdentityTransform;
    scx.scx_area = *r;
    if (DebugIsSet(gaDebugID, gaDebStems))
    {
	ShowRect(use->cu_def, r, STYLE_OUTLINEHIGHLIGHTS);
	TxMore("Checking clear area");
	ShowRect(use->cu_def, r, STYLE_ERASEHIGHLIGHTS);
    }
    if (DBTreeSrTiles(&scx, mask, 0, gaIsClearFunc, (ClientData) NULL))
	return (FALSE);

    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * gaIsClearFunc --
 *
 * Called for tiles whose type matches 'mask' underneath the area
 * 'r' in gaIsClear() above.  
 *
 * Results:
 *	Always returns 1 since we only need a single obstacle to not have
 *      a clear area.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
gaIsClearFunc(tile, cxp)
    Tile *tile;
    TreeContext *cxp;
{
    return 1;
}


/*
 * ----------------------------------------------------------------------------
 *
 * gaStemSimpleRoute --
 *
 * See if a simple type of route is possible given the pinLayer.
 * All other information describing the simple route is
 * present in *simple.
 *
 * Results:
 *	TRUE if a route was possible, FALSE if not.
 *
 * Side effects:
 *	Paints the path into 'def' if 'def' is non-NULL.
 *
 * ----------------------------------------------------------------------------
 */

bool
gaStemSimpleRoute(simple, pinLayer, def)
    SimpleStem *simple;	/* Describes the route */
    TileType pinLayer;			/* Connect to pin on this layer */
    CellDef *def;			/* If non-NULL, paint to this def */
{
    SimpleWire *wPin, *wOther;

    /*
     * Determine which wire corresponds to pin
     * and which corresponds to the other route layer.
     */
    if (pinLayer == RtrPolyType)
    {
	wPin = &simple->ss_polyWire;
	wOther = &simple->ss_metalWire;
    }
    else if (pinLayer == RtrMetalType)
    {
	wPin = &simple->ss_metalWire;
	wOther = &simple->ss_polyWire;
    }
    else
    {
	ASSERT(FALSE, "gaStemSimpleRoute: bad destType");
    }

    if(TTMaskHasType(&simple->ss_termMask, pinLayer))
    /* layer of pin also OK for final connection at terminal */
    {
	if(wPin->sw_longOK)
	/* No contact connection */
	{
	    if(def)
	    {
		DBPaint(def, &wPin->sw_long, wPin->sw_type);
	    }
	    return TRUE;
	}
	/* contact at pin only */
	else if (TTMaskHasType(&simple->ss_termMask, wOther->sw_type) &&
		 wOther->sw_shortOK && 
		 simple->ss_cPinOK)
	{
	    if(def)
	    {
		DBPaint(def, &wOther->sw_short, wOther->sw_type);
		RtrPaintContact(def, &simple->ss_cPin);
		DBPaint(def, &wPin->sw_pinStub, wPin->sw_type);
	    }
	    return TRUE;
	}
	else if (simple->ss_cTermOK &&
		 wOther->sw_shortOK &&
		 simple->ss_cPinOK)
	/* two contact route */
	{
	    if(def)
	    {
		RtrPaintContact(def, &simple->ss_cTerm);
		DBPaint(def, &wOther->sw_short, wOther->sw_type);
		RtrPaintContact(def, &simple->ss_cPin);
		DBPaint(def, &wPin->sw_pinStub, wPin->sw_type);
	    }
	    return TRUE;
	}
	else
	/* No simple route */
	{
	    return FALSE;
	}
    }
    else
    /* pin and terminal connections on opposite layers */
    {
	if(simple->ss_cTermOK &&
	   wPin->sw_longOK)
	/* contact at terminal only */
	{
	    if(def)
	    {
		RtrPaintContact(def, &simple->ss_cTerm);
		DBPaint(def, &wPin->sw_long, wPin->sw_type);
	    }
	    return TRUE;
	}
	else if(wOther->sw_shortOK &&
		simple->ss_cPinOK)
	/* contact at pin only */
	{
	    if(def)
	    {
		DBPaint(def, &wOther->sw_short, wOther->sw_type);
		RtrPaintContact(def, &simple->ss_cPin);
		DBPaint(def, &wPin->sw_pinStub, wPin->sw_type);
	    }
	    return TRUE;
	}
	else
	/* No simple route */
	{
	    return FALSE;
	}
    }
}
