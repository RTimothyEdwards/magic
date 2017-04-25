/*
 * mzWalk.c --
 *
 * Code for Completing final legs of route within the blocked areas ajacent to
 * dest areas.
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


/*
 * ----------------------------------------------------------------------------
 *
 * mzWalkRight --
 *
 * Extend path inside a to-the-right walk.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	mzAddPoint() called to add extended path to appropriate queue.
 *
 * ----------------------------------------------------------------------------
 */
void
mzWalkRight(path)
    RoutePath *path;
{
    Point pOrg;		/* point to extend from */
    Point pNew;		/* next interesting point in direction of extension */
    dlong segCost; 	/* cost of segment between pOrg and pNew */
    int extendCode;	/* Interesting directions to extend in */
    Tile *tpThis;	/* Tile containing org point */

    /* DEBUG - trace calls to this routine. */
    if (DebugIsSet(mzDebugID, mzDebMaze))
	TxPrintf("WALKING RIGHT\n");

    /* pOrg = current end of path */
    pOrg = path->rp_entry;

    /* get blockage plane tile under pOrg */
    tpThis = TiSrPointNoHint(path->rp_rLayer->rl_routeType.rt_hBlock,&pOrg);

    /* org point should be in walk to left of dest area */
    ASSERT(TiGetType(tpThis)==TT_LEFT_WALK,"mzWalkRight");

    /* traverse to right edge of this walk to get to dest area */
    pNew.p_x = RIGHT(tpThis);
    pNew.p_y = pOrg.p_y;

    /* mark as complete path */
    extendCode = EC_COMPLETE;

    /* compute cost of path segment from pOrg to pNew */
    {
	Tile *tp;
	bool rotate;

	tp = TiSrPointNoHint(mzVRotatePlane, &pOrg);
	rotate = (TiGetType(tp) != TT_SPACE);

	if (rotate)
	    segCost = (dlong) ((pNew.p_x - pOrg.p_x) *
			path->rp_rLayer->rl_vCost);
	else
	    segCost = (dlong) ((pNew.p_x - pOrg.p_x) *
			path->rp_rLayer->rl_hCost);
    }

    /* Compute additional cost for paralleling nearest hint segment */
    /* (Start at low end of segment and move to high end computing hint cost
     *  as we go)
     */
    {
	Tile *tp;
	dlong hintCost;
	int deltaUp, deltaDown, delta;
	Point lowPt;

	for(lowPt = pOrg; lowPt.p_x < pNew.p_x; lowPt.p_x = RIGHT(tp))
	{
	    /* find tile in hint plane containing lowPt */
	    tp = TiSrPointNoHint(mzVHintPlane,&lowPt);

	    /* find nearest hint segment and add appropriate cost */
	    if(TiGetType(tp) != TT_MAGNET)
	    {
		deltaUp = (TiGetType(RT(tp)) == TT_MAGNET) ?
		TOP(tp) - lowPt.p_y : -1;
		deltaDown = (TiGetType(LB(tp)) == TT_MAGNET) ?
		lowPt.p_y - BOTTOM(tp) : -1;

		/* delta = distance to nearest hint */
		if (deltaUp < 0) 
		{
		    if (deltaDown < 0)
		    delta = 0;
		    else
		    delta = deltaDown;
		}
		else
		{
		    if (deltaDown < 0)
		    delta = deltaUp;
		    else
		    delta = MIN(deltaUp,deltaDown);
		}

		if(delta>0)
		{
		    hintCost = (dlong) ((MIN(RIGHT(tp),pNew.p_x) - lowPt.p_x) *
				    path->rp_rLayer->rl_hintCost);
		    hintCost *= delta;
		    segCost += hintCost;
		}
	    }
	}
    }

    /* Process the new point */
    mzAddPoint(path, &pNew, path->rp_rLayer, 'H', extendCode, &segCost);

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzWalkLeft --
 *
 * Extend path inside a to-the-left walk.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	mzAddPoint() called to add extended path to appropriate queue.
 *
 * ----------------------------------------------------------------------------
 */
void
mzWalkLeft(path)
    RoutePath *path;
{
    Point pOrg;		/* point to extend from */
    Point pNew;		/* next interesting point in direction of extension */
    dlong segCost; 	/* cost of segment between pOrg and pNew */
    int extendCode;	/* Interesting directions to extend in */
    Tile *tpThis;	/* Tile containing org point */
    RouteType *rtype;	/* Structure of material type at destination */

    /* DEBUG - trace calls to this routine. */
    if (DebugIsSet(mzDebugID, mzDebMaze))
	TxPrintf("WALKING LEFT\n");

    /* pOrg = current end of path */
    pOrg = path->rp_entry;

    /* get blockage plane tile under pOrg */
    tpThis = TiSrPointNoHint(path->rp_rLayer->rl_routeType.rt_hBlock,&pOrg);
    rtype = &path->rp_rLayer->rl_routeType;

    /* org point should be in walk to right of dest area */
    ASSERT(TiGetType(tpThis)==TT_RIGHT_WALK,"mzWalkLeft");

    /* traverse just past left edge of this walk to get to dest area */

    pNew.p_x = LEFT(tpThis) - 1;
    pNew.p_y = pOrg.p_y;

    /* mark as complete path */
    extendCode = EC_COMPLETE;

    /* compute cost of path segment from pOrg to pNew */
    {
	Tile *tp;
        bool rotate;

	tp = TiSrPointNoHint(mzVRotatePlane, &pOrg);
	rotate = (TiGetType(tp) != TT_SPACE);

	if (rotate)
	    segCost = (dlong) ((pOrg.p_x - pNew.p_x) *
 			path->rp_rLayer->rl_vCost);
	else
	    segCost = (dlong) ((pOrg.p_x - pNew.p_x) *
			path->rp_rLayer->rl_hCost);
    }

    /* Compute additional cost for paralleling nearest hint segment */
    /* (Start at low end of segment and move to high end computing hint cost
     *  as we go)
     */
    {
	Tile *tp;
	dlong hintCost;
	int deltaUp, deltaDown, delta;
	Point lowPt;

	for(lowPt = pNew; lowPt.p_x < pOrg.p_x; lowPt.p_x = RIGHT(tp))
	{
	    /* find tile in hint plane containing lowPt */
	    tp = TiSrPointNoHint(mzVHintPlane,&lowPt);

	    /* find nearest hint segment and add appropriate cost */
	    if(TiGetType(tp) != TT_MAGNET)
	    {
		deltaUp = (TiGetType(RT(tp)) == TT_MAGNET) ?
		    TOP(tp) - lowPt.p_y : -1;
		deltaDown = (TiGetType(LB(tp)) == TT_MAGNET) ?
		    lowPt.p_y - BOTTOM(tp) : -1;

		/* delta = distance to nearest hint */
		if (deltaUp < 0) 
		{
		    if (deltaDown < 0)
			delta = 0;
		    else
			delta = deltaDown;
		}
		else
		{
		    if (deltaDown < 0)
			delta = deltaUp;
		    else
			delta = MIN(deltaUp,deltaDown);
		}

		if(delta>0)
		{
		    hintCost = (dlong) ((MIN(RIGHT(tp),pOrg.p_x) - lowPt.p_x) *
					path->rp_rLayer->rl_hintCost);
		    hintCost *= delta;
		    segCost  += hintCost;
		}
	    }
	}
    }

    /* Process the new point */
    mzAddPoint(path, &pNew, path->rp_rLayer, 'H', extendCode, &segCost);

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzWalkUp --
 *
 * Extend path inside a up-walk.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	mzAddPoint() called to add extended path to appropriate queue.
 *
 * ----------------------------------------------------------------------------
 */
void
mzWalkUp(path)
    RoutePath *path;
{
    Point pOrg;		/* point to extend from */
    Point pNew;		/* next interesting point in direction of extension */
    dlong segCost; 	/* cost of segment between pOrg and pNew */
    int extendCode;	/* Interesting directions to extend in */
    Tile *tpThis;	/* Tile containing org point */

    /* DEBUG - trace calls to this routine. */
    if (DebugIsSet(mzDebugID, mzDebMaze))
	TxPrintf("WALKING UP\n");

    /* pOrg = current end of path */
    pOrg = path->rp_entry;

    /* get blockage plane tile under pOrg */
    tpThis = TiSrPointNoHint(path->rp_rLayer->rl_routeType.rt_vBlock,&pOrg);

    /* org point should be in walk to left of dest area */
    ASSERT(TiGetType(tpThis)==TT_BOTTOM_WALK,"mzWalkUp");

    /* traverse to top edge of this walk to get to dest area */
    pNew.p_x = pOrg.p_x;
    pNew.p_y = TOP(tpThis);

    /* mark as complete path */
    extendCode = EC_COMPLETE;

    /* compute cost of path segment from pOrg to pNew */
    {
	Tile *tp;
        bool rotate;

	tp = TiSrPointNoHint(mzHRotatePlane, &pOrg);
	rotate = (TiGetType(tp) != TT_SPACE);

	if (rotate)
	    segCost = (dlong) ((pNew.p_y - pOrg.p_y) *
			path->rp_rLayer->rl_hCost);
	else
	    segCost = (dlong) ((pNew.p_y - pOrg.p_y) *
													path->rp_rLayer->rl_vCost);
    }

    /* Compute additional cost for paralleling nearest hint segment */
    /* (Start at low end of segment and move to high end computing hint cost
     *  as we go)
     */
    {
	Tile *tp;
	dlong hintCost;
	int deltaRight, deltaLeft, delta;
	Point lowPt;

	for(lowPt = pOrg; lowPt.p_y < pNew.p_y; lowPt.p_y = TOP(tp))
	{
	    /* find tile in hint plane containing lowPt */
	    tp = TiSrPointNoHint(mzHHintPlane,&lowPt);

	    /* find nearest hint segment and add appropriate cost */
	    if(TiGetType(tp) != TT_MAGNET)
	    {
		deltaRight = (TiGetType(TR(tp)) == TT_MAGNET) ?
		    RIGHT(tp) - lowPt.p_x : -1;
		deltaLeft = (TiGetType(BL(tp)) == TT_MAGNET) ?
				lowPt.p_x - LEFT(tp) : -1;

		/* delta = distance to nearest hint */
		if (deltaRight < 0) 
		{
		    if (deltaLeft < 0)
			delta = 0;
		    else
			delta = deltaLeft;
		}
		else
		{
		    if (deltaLeft < 0)
			delta = deltaRight;
		    else
			delta = MIN(deltaRight,deltaLeft);
		}

		if(delta>0)
		{
		    hintCost = (dlong) ((MIN(TOP(tp),pNew.p_y) - lowPt.p_y) *
				path->rp_rLayer->rl_hintCost);
		    hintCost *= delta;
		    segCost  += hintCost;
		}
	    }
	}
    }

    /* Process the new point */
    mzAddPoint(path, &pNew, path->rp_rLayer, 'V', extendCode, &segCost);

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzWalkDown --
 *
 * Extend path inside a down-walk.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	mzAddPoint() called to add extended path to appropriate queue.
 *
 * ----------------------------------------------------------------------------
 */
void
mzWalkDown(path)
    RoutePath *path;
{
    Point pOrg;		/* point to extend from */
    Point pNew;		/* next interesting point in direction of extension */
    dlong segCost; 	/* cost of segment between pOrg and pNew */
    int extendCode;	/* Interesting directions to extend in */
    Tile *tpThis;	/* Tile containing org point */
    RouteType *rtype;	/* Structure of material at destination */

    /* DEBUG - trace calls to this routine. */
    if (DebugIsSet(mzDebugID, mzDebMaze))
	TxPrintf("WALKING DOWN\n");

    /* pOrg = current end of path */
    pOrg = path->rp_entry;

    /* get blockage plane tile under pOrg */
    tpThis = TiSrPointNoHint(path->rp_rLayer->rl_routeType.rt_vBlock,&pOrg);
    rtype = &path->rp_rLayer->rl_routeType;

    /* org point should be in walk to left of dest area */
    ASSERT(TiGetType(tpThis)==TT_TOP_WALK,"mzWalkDown");

    /* traverse to just past edge of this walk to get to dest area */
    pNew.p_x = pOrg.p_x;
    pNew.p_y = BOTTOM(tpThis) - 1;

    /* mark as complete path */
    extendCode = EC_COMPLETE;

    /* compute cost of path segment from pOrg to pNew */
    {
	Tile *tp;
        bool rotate;

	tp = TiSrPointNoHint(mzHRotatePlane, &pOrg);
	rotate = (TiGetType(tp) != TT_SPACE);

	if (rotate)
	    segCost = (dlong) ((pOrg.p_y - pNew.p_y) *
			path->rp_rLayer->rl_hCost);
	else
	    segCost = (dlong) ((pOrg.p_y - pNew.p_y) *
			path->rp_rLayer->rl_vCost);
    }

    /* Compute additional cost for paralleling nearest hint segment */
    /* (Start at low end of segment and move to high end computing hint cost
     *  as we go)
     */
    {
	Tile *tp;
	dlong hintCost;
	int deltaRight, deltaLeft, delta;
	Point lowPt;

	for(lowPt = pNew; lowPt.p_y < pOrg.p_y; lowPt.p_y = TOP(tp))
	{
	    /* find tile in hint plane containing lowPt */
	    tp = TiSrPointNoHint(mzHHintPlane,&lowPt);

	    /* find nearest hint segment and add appropriate cost */
	    if(TiGetType(tp) != TT_MAGNET)
	    {
		deltaRight = (TiGetType(TR(tp)) == TT_MAGNET) ?
		    RIGHT(tp) - lowPt.p_x : -1;
		deltaLeft = (TiGetType(BL(tp)) == TT_MAGNET) ?
		    lowPt.p_x - LEFT(tp) : -1;

		/* delta = distance to nearest hint */
		if (deltaRight < 0) 
		{
		    if (deltaLeft < 0)
			delta = 0;
		    else
			delta = deltaLeft;
		}
		else
		{
		    if (deltaLeft < 0)
			delta = deltaRight;
		    else
			delta = MIN(deltaRight,deltaLeft);
		}

		if(delta>0)
		{
		    hintCost = (dlong) ((MIN(TOP(tp),pOrg.p_y) - lowPt.p_y) *
				path->rp_rLayer->rl_hintCost);
		    hintCost *= delta;
		    segCost  += hintCost;
		}
	    }
	}
    }

    /* Process the new point */
    mzAddPoint(path, &pNew, path->rp_rLayer, 'V', extendCode, &segCost);

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzWalkLRContact --
 *
 * Extend path to dest area (above or below) via contact.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	mzAddPoint() called to add extended path to appropriate queue.
 *
 * ----------------------------------------------------------------------------
 */
void
mzWalkLRContact(path)
    RoutePath *path;
{
    Point pOrg;		/* point to extend from */
    int extendCode;	/* Interesting directions to extend in */
    RouteContact *rC;   /* Route contact to make connection with */
    RouteLayer *newRL;	/* Route layer of dest area */ 
    dlong conCost;	/* Cost of final contact */
    int walkType;	/* TT_ABOVE_LR_WALK or TT_BELOW_LR_WALK */
    Tile *tpThis;	/* Tile containing org point */
    Tile *tpCont;	/* Tile allowing contact placement */

    /* DEBUG - trace calls to this routine. */
    if (DebugIsSet(mzDebugID, mzDebMaze))
	TxPrintf("WALKING HOME VIA LR CONTACT\n");

    /* pOrg = current end of path */
    pOrg = path->rp_entry;

    /* get blockage plane tile under pOrg.  Contact walks were painted	*/
    /* into the hBlock plane.						*/
    tpThis = TiSrPointNoHint(path->rp_rLayer->rl_routeType.rt_hBlock,&pOrg);

    walkType = TiGetType(tpThis);

    /* find contact type that connects to route layer. */
    for(rC=mzRouteContacts; rC!=NULL; rC=rC->rc_next)
    {
	/* if not active, skip it */
        if(!(rC->rc_routeType.rt_active)) continue;

	/* if it doesn't connect to both current and dest layers, skip it */
	if((walkType == TT_BELOW_LR_WALK) && (rC->rc_rLayer1 != path->rp_rLayer))
	    continue;

	if((walkType == TT_ABOVE_LR_WALK) && (rC->rc_rLayer2 != path->rp_rLayer))
	    continue;
 
	/* if contact blocked, skip it */
	tpCont = TiSrPointNoHint(rC->rc_routeType.rt_hBlock, &pOrg);
	if (TiGetType(tpCont) == TT_BLOCKED)
  	    continue;

	/* if contact is non-square and doesn't fit, skip it */
	if (RIGHT(tpThis) - pOrg.p_x <= rC->rc_routeType.rt_length
			- rC->rc_routeType.rt_width)
	    continue;

	/* if we got this far we found our contact, break out of the loop */
	break;
    }

    /* There should always be an rC that works */
    ASSERT(rC!=NULL,"mzWalkLRContact");

    if (rC == NULL) return;	/* For now, non-square contacts may cause this
				 * point to be reached.  Fix in mzBlock.c?
				 */

    /* Compute the new route layer */
    newRL = (rC->rc_rLayer1 != path->rp_rLayer) ?  rC->rc_rLayer1 : rC->rc_rLayer2;

    /* compute contact cost */
    conCost = (dlong) rC->rc_cost;

    /* Add final point */
    mzAddPoint(path, &pOrg, newRL, 'O', EC_COMPLETE, &conCost);

    return;
}

/*
 * ----------------------------------------------------------------------------
 *
 * mzWalkUDContact --
 *
 * Extend path to dest area (above or below) via contact.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	mzAddPoint() called to add extended path to appropriate queue.
 *
 * ----------------------------------------------------------------------------
 */
void
mzWalkUDContact(path)
    RoutePath *path;
{
    Point pOrg;		/* point to extend from */
    int extendCode;	/* Interesting directions to extend in */
    RouteContact *rC;   /* Route contact to make connection with */
    RouteLayer *newRL;	/* Route layer of dest area */ 
    dlong conCost;	/* Cost of final contact */
    int walkType;	/* TT_ABOVE_UD_WALK or TT_BELOW_UD_WALK */
    Tile *tpThis;	/* Tile containing org point */
    Tile *tpCont;	/* Tile allowing contact placement */

    /* DEBUG - trace calls to this routine. */
    if (DebugIsSet(mzDebugID, mzDebMaze))
	TxPrintf("WALKING HOME VIA UD CONTACT\n");

    /* pOrg = current end of path */
    pOrg = path->rp_entry;

    /* get blockage plane tile under pOrg.  Contact walks were painted	*/
    /* into the hBlock plane.						*/
    tpThis = TiSrPointNoHint(path->rp_rLayer->rl_routeType.rt_vBlock,&pOrg);

    walkType = TiGetType(tpThis);

    /* find contact type that connects to route layer. */
    for(rC=mzRouteContacts; rC!=NULL; rC=rC->rc_next)
    {
	/* if not active, skip it */
        if(!(rC->rc_routeType.rt_active)) continue;

	/* if it doesn't connect to both current and dest layers, skip it */
	if((walkType == TT_BELOW_UD_WALK) && (rC->rc_rLayer1 != path->rp_rLayer))
	    continue;

	if((walkType == TT_ABOVE_UD_WALK) && (rC->rc_rLayer2 != path->rp_rLayer))
	    continue;
 
	/* if contact blocked, skip it */
	tpCont = TiSrPointNoHint(rC->rc_routeType.rt_vBlock, &pOrg);
	if (TiGetType(tpCont) == TT_BLOCKED)
  	    continue;

	/* if contact is non-square and doesn't fit, skip it */
	if (TOP(tpThis) - pOrg.p_y <= rC->rc_routeType.rt_length
			- rC->rc_routeType.rt_width)
	    continue;

	/* if we got this far we found our contact, break out of the loop */
	break;
    }

    /* There should always be an rC that works */
    ASSERT(rC!=NULL,"mzWalkUDContact");

    if (rC == NULL) return;	/* For now, non-square contacts may cause this
				 * point to be reached.  Fix in mzBlock.c?
				 */

    /* Compute the new route layer */
    newRL = (rC->rc_rLayer1 != path->rp_rLayer) ?  rC->rc_rLayer1 : rC->rc_rLayer2;

    /* compute contact cost */
    conCost = (dlong) rC->rc_cost;

    /* Add final point */
    mzAddPoint(path, &pOrg, newRL, 'X', EC_COMPLETE, &conCost);

    return;
}
