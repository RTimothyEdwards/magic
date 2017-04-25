/*
 * mzStart.c --
 *
 * Code for making initial legs of route within the blocked areas ajacent to
 * start areas.
 *
 *     ********************************************************************* 
 *     * Copyright (C) 1988, 1990 Michael H. Arnold and the Regents of the *
 *     * University of California.                                         *
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
static char rcsid[] __attribute__ ((unused)) = "$Header:";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "utils/heap.h"
#include "database/database.h"
#include "utils/signals.h"
#include "textio/textio.h"
#include "utils/list.h"
#include "debug/debug.h"
#include "mzrouter/mzrouter.h"
#include "mzrouter/mzInternal.h"

/* Forward declarations */

extern bool mzExtendInitPath(RoutePath *, RouteLayer *, Point, dlong, int, int);

extern bool mzAddInitialContacts();

/*
 * ----------------------------------------------------------------------------
 *
 * Simple search function for start tiles.  This function is called only if
 * a tile of type TT_SAMENODE is found in the search area.
 *
 * Results:
 *	Always return 1 (break on the first acceptable tile found)
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
mzFindSamenodeFunc(Tile *tile, Point *point)
{
    *point = tile->ti_ll;
    return 1;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzStart --
 *
 * Establish initial path segments from start term, considering inital 
 * contacts and leading out of any SAMENODE blocks
 * present at start point.
 *
 * Results:
 *      TRUE normally, FALSE if mzExtendInitPath discovered that the
 *	start node is already connected to the destination node.
 *
 * Side effects:
 *	mzAddPoint() called to add paths to appropriate queue.
 *
 * ----------------------------------------------------------------------------
 */

#define EC_ALL_DIRECTIONS  (EC_RIGHT | EC_LEFT | EC_UP | EC_DOWN)

bool
mzStart(term)
    ColoredRect *term;
{
    RouteLayer *rL;
    RouteContact *rC;
    Tile *tp;
    bool returnCode = TRUE;
    Point point;
    int result;
    Rect srect;
    
    /* Find routelayer corresponding to type */
    for(rL = mzActiveRLs; rL != NULL; rL = rL->rl_nextActive)
    {
	if(rL->rl_routeType.rt_tileType == term->cr_type) break;
    }

    /* Expand area to bottom and left to make sure we can find	*/
    /* SAMENODE tiles to the bottom and left of the terminal.	*/

    srect = term->cr_rect;
    srect.r_xbot--;
    srect.r_ybot--;
    
    /* Added by Tim 8/2/06---for start terminals on contact layers, */
    /* run mzExtendInitPath for layer1 and set rL to layer2.	    */

    if ((rL == NULL) && DBIsContact(term->cr_type))
    {
	for (rC = mzRouteContacts ; rC != NULL; rC = rC->rc_next)
	{
	    if (!(rC->rc_routeType.rt_active)) continue;

	    if (TTMaskHasType(&(DBConnectTbl[term->cr_type]),
			rC->rc_rLayer1->rl_routeType.rt_tileType) &&
			TTMaskHasType(&(DBConnectTbl[term->cr_type]),
			rC->rc_rLayer2->rl_routeType.rt_tileType))
	    {
		/* Search block plane for first unblocked tile	*/
		result = DBSrPaintArea((Tile *)NULL,
			rC->rc_rLayer1->rl_routeType.rt_hBlock,
			&srect, &mzStartTypesMask,
			mzFindSamenodeFunc, &point);

		if (result == 1)
		{
		    returnCode = mzExtendInitPath(NULL, rC->rc_rLayer1,
				point, (dlong)0, 0, EC_ALL_DIRECTIONS);
		    rL = rC->rc_rLayer2;
		    break;
		}
	    }
	}
    }

    /* If no corresponding route layer, check for layers that connect */

    if (rL == NULL)
    {
	for (rL = mzActiveRLs; rL != NULL; rL = rL->rl_nextActive)
	{
	    if (TTMaskHasType(&(DBConnectTbl[term->cr_type]),
                                   rL->rl_routeType.rt_tileType))
		break;
	}
    }

    /* If no corresponding route layer, return. 			  */
    /* This does not need a warning.  The search for connected tiles adds */
    /* tile types that may not correspond to an active route layer.  Just */
    /* ignore such tiles.						  */

    if (rL == NULL)
	return returnCode;

    /* Find a valid start point in this terminal */

    result = DBSrPaintArea((Tile *)NULL, rL->rl_routeType.rt_hBlock,
		&srect, &mzStartTypesMask, mzFindSamenodeFunc, &point);

    if (result == 1)
    {
	/* call mzExtendInitPath to do the real work */

	returnCode = mzExtendInitPath(NULL, 	/* path so far */
			 rL, 			/* layer of new point */
			 point, 		/* start point */
			 (dlong)0, 		/* cost of new segment */
			 0, 			/* length of path so far */
			 EC_ALL_DIRECTIONS);	/* how to extend init path */
    }
    return returnCode;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzExtendInitPath --
 *
 * Central routine for recursively building up initial path inside 
 * SAMENODE.  Adds specified point to an initial path, if resulting
 * path end is outside block, mzAddPoint() is called to add init path
 * to appropriate queue.
 *
 * Results:
 *      TRUE normally, FALSE if a zero-length route was detected (start
 *	node and destination node are already connected).
 *
 * Side effects:
 *      Initial paths built up.
 *	mzAddPoint() eventually called to add init paths to appropriate queue.
 *
 * ----------------------------------------------------------------------------
 */
bool
mzExtendInitPath(path, rL, point, cost, length, directions)
    RoutePath *path;	/* Initial Path, being extended */
    RouteLayer *rL;     /* routelayer of new point */
    Point point;	/* new point for  initPath */
    dlong cost;		/* cost of new segment */
    int length;		/* length of path (excluding new segment) */
    int directions;	/* directions to extend init path in */
{
    Tile *tp;
    bool returnCode = TRUE;
    int orient;
    int extendCode = 0;

    /* Get tile in rL blockage plane under new point */

    tp = TiSrPointNoHint(rL->rl_routeType.rt_hBlock, &point);

    /* If new point blocked by a different node, just return */

    if (TiGetType(tp) == TT_BLOCKED)
	return returnCode;

    /* Consider initial contacts */

    if (path == NULL)
	returnCode = mzAddInitialContacts(rL, point);

    /* If no SAMENODE block, call mzAddPoint() to and initial path to 
     * appropriate queue.
     */

    switch (TiGetType(tp))
    {
	/* Use standard extend code on TT_SAMENODE areas.	*/
	/* (Tim, 10/4/06)					*/

	case TT_SAMENODE:
	case TT_SPACE:
	    extendCode = EC_ALL_DIRECTIONS | EC_UDCONTACTS | EC_LRCONTACTS;
	    break;

	case TT_LEFT_WALK:
	    extendCode = EC_WALKRIGHT;
	    break;
			
	case TT_RIGHT_WALK:
	    extendCode = EC_WALKLEFT;
	    break;
			
	case TT_TOP_WALK:
	    extendCode = EC_WALKDOWN;
	    break;
			
	case TT_BOTTOM_WALK:
	    extendCode = EC_WALKUP;
	    break;

	case TT_ABOVE_LR_WALK:
	case TT_BELOW_LR_WALK:
	    extendCode = EC_WALKLRCONTACT;
	    break;
			
	case TT_ABOVE_UD_WALK:
	case TT_BELOW_UD_WALK:
	    extendCode = EC_WALKUDCONTACT;
	    break;
			
	case TT_DEST_AREA:
	    TxError("Zero length route!\n");
	    extendCode = EC_COMPLETE;
	    returnCode = FALSE;
	    break;
    }

    if (extendCode == 0) return FALSE;	/* This shouldn't happen */

    /* determine orientation of new segment */

    if (path == NULL)
	orient = 'O';
    else if (path->rp_rLayer != rL)
    {
	if (path->rp_entry.p_x == point.p_x)
	    orient = 'X';
	else
	    orient = 'O';
    }
    else if (path->rp_entry.p_x == point.p_x)
 	orient = 'V';
    else 
    {
	ASSERT(path->rp_entry.p_y==point.p_y,"mzExtendInitPath");
	orient = 'H';
    }

    /* Add initial path to appropriate queue */
    mzAddPoint(path, &point, rL, orient, extendCode, &cost);

    return returnCode;
}

/*
 * ----------------------------------------------------------------------------
 *
 * mzAddInitialContacts --
 *
 *
 * Results:
 *      TRUE normally, FALSE if zero-length route
 *
 * Side effects:
 *	Calls mzExtendInitPath to add contact to initial path.
 *
 * ----------------------------------------------------------------------------
 */

bool
mzAddInitialContacts(rL, point)
    RouteLayer *rL;     /* routelayer of initial point */
    Point point;	/* initial point */
{
    List *cL;
    Tile *tp;
    RouteContact *rC;
    RouteLayer *newRLayer;
    dlong conCost;
    RoutePath *initPath;
    bool returnCode = TRUE;

    /* Loop through contacts that connect to current rLayer */
    for (cL=rL->rl_contactL; cL!=NULL; cL=LIST_TAIL(cL))
    {
	rC = (RouteContact *) LIST_FIRST(cL);

	/* Don't use inactive contacts */
	if (!(rC->rc_routeType.rt_active)) continue;

	/* Get "other" route Layer contact connects to */
	if (rC->rc_rLayer1 == rL)
	{
	    newRLayer = rC->rc_rLayer2;
	}
	else
	{
	    ASSERT(rC->rc_rLayer2 == rL,"mzStart");
	    newRLayer = rC->rc_rLayer1;
	}

	/* Don't spread to inactive layers */
	if (!(newRLayer->rl_routeType.rt_active)) continue;

	/* Don't place contact if blocked */

	tp = TiSrPointNoHint(rC->rc_routeType.rt_hBlock, &point);
	if (TiGetType(tp) == TT_SAMENODE)
	{
	    /* Check if the size of the tile meets min length requirement */
	    if (RIGHT(tp) - point.p_x <= rC->rc_routeType.rt_length
			- rC->rc_routeType.rt_width)
	    {

		/* compute cost of contact */

		conCost = (dlong) rC->rc_cost;

		/* build path consisting of initial point */

		initPath = NEWPATH();
		initPath->rp_rLayer = rL;
		initPath->rp_entry = point;
		initPath->rp_orient = 'O'; 
		initPath->rp_cost = 0;
		initPath->rp_back = NULL;   

		/* Extend thru new point */
		returnCode = mzExtendInitPath(initPath, newRLayer, point,
			conCost, 0, EC_ALL_DIRECTIONS);
	    }
	}

	/* Check vertical planes for contact points, too */
	tp = TiSrPointNoHint(rC->rc_routeType.rt_vBlock, &point);
	if (TiGetType(tp) != TT_SAMENODE)
	    continue;

	/* Check if the size of the tile meets min length requirement */
	if (TOP(tp) - point.p_y <= rC->rc_routeType.rt_length
			- rC->rc_routeType.rt_width)
	    continue;

	/* compute cost of contact */

	conCost = (dlong) rC->rc_cost;

	/* build path consisting of initial point */

	initPath = NEWPATH();
	initPath->rp_rLayer = rL;
	initPath->rp_entry = point;
	initPath->rp_orient = 'X'; 
	initPath->rp_cost = 0;
	initPath->rp_back = NULL;   

	/* Extend thru new point */
	returnCode = mzExtendInitPath(initPath, newRLayer, point, conCost,
		0, EC_ALL_DIRECTIONS);
    }
    return returnCode;
}

