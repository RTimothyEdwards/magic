/*
 * mzDebug.c --
 *
 * Routines for debugging.
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
static char rcsid[] __attribute__ ((unused)) = "$$";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/signals.h"
#include "textio/textio.h"
#include "utils/hash.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "utils/malloc.h"
#include "utils/list.h"
#include "utils/heap.h"
#include "mzrouter/mzrouter.h"
#include "mzrouter/mzInternal.h"

/* extra variables for use with dbx */
RoutePath *mzDPath;
Tile *mzTile;
int mz;

/* Forward declarations */
extern void mzPrintRL();
extern void mzPrintRT();
extern void mzPrintRC();
extern void mzPrintRP();
extern void mzPrintPathHead();
 

/*
 * ----------------------------------------------------------------------------
 *
 * MZPrintRCListNames --
 *
 * Print names of route contacts linked together by external "list"
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None
 *
 * ----------------------------------------------------------------------------
 */

void
MZPrintRCListNames(l)
    List *l;
{
    RouteContact *rC;

    TxPrintf("\t");

    for(;l!=NULL; l=LIST_TAIL(l))
    {
	rC = (RouteContact *) LIST_FIRST(l);
        TxPrintf("%s ",DBTypeLongNameTbl[rC->rc_routeType.rt_tileType]);
    }

    TxPrintf("\n");
    return;
}

/*
 * ----------------------------------------------------------------------------
 *
 * MZPrintRLListNames --
 *
 * Print names of route Layers linked together by external "list"
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None
 *
 * ----------------------------------------------------------------------------
 */

void
MZPrintRLListNames(l)
    List *l;
{
    RouteLayer *rL;

    TxPrintf("\t");

    for(;l!=NULL; l=LIST_TAIL(l))
    {
	rL = (RouteLayer *) LIST_FIRST(l);
        TxPrintf("%s ",DBTypeLongNameTbl[rL->rl_routeType.rt_tileType]);
    }

    TxPrintf("\n");
    return;
}

/*
 * ----------------------------------------------------------------------------
 *
 * MZPrintRLs --
 *
 * Print list of RouteLayer strucs.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None
 *
 * ----------------------------------------------------------------------------
 */

void
MZPrintRLs(rL)
    RouteLayer *rL;
{
    while(rL!=NULL)
    {
	mzPrintRL(rL);
	rL = rL->rl_next;

	if(rL!=NULL) TxMore("");
    }
    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzPrintRL --
 *
 * Print single RouteLayer struc.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None
 *
 * ----------------------------------------------------------------------------
 */

void
mzPrintRL(rL)
    RouteLayer *rL;
{
    List *cL;

    TxPrintf("ROUTE LAYER:\n");
    mzPrintRT(&(rL->rl_routeType)); 
    TxPrintf("\tplaneNum = %d (%s)\n",rL->rl_planeNum, 
	DBPlaneLongNameTbl[rL->rl_planeNum]);

    TxPrintf("\tcontactL = ");
    for (cL=rL->rl_contactL; cL!=NULL; cL=LIST_TAIL(cL))
    {
	TxPrintf("%s", 
	    DBTypeLongNameTbl[
		((RouteContact*) LIST_FIRST(cL))->
		    rc_routeType.rt_tileType]);

        if(((RouteContact*) LIST_FIRST(cL))->rc_rLayer1 == rL)
	   TxPrintf("(to %s) ", DBTypeLongNameTbl[
	       ((RouteContact*) LIST_FIRST(cL))->
	       rc_rLayer2->rl_routeType.rt_tileType]);
	else
	   TxPrintf("(to %s) ", DBTypeLongNameTbl[
	       ((RouteContact*) LIST_FIRST(cL))->
	       rc_rLayer1->rl_routeType.rt_tileType]);
    }
    TxPrintf("\n");
     
    TxPrintf("\thCost = %d\n",
	     rL->rl_hCost);
    TxPrintf("\tvCost = %d\n",
	     rL->rl_vCost);
    TxPrintf("\tjogCost = %d\n",
	     rL->rl_jogCost);
    TxPrintf("\thintCost = %d\n",rL->rl_hintCost);
    
    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzPrintRouteType --
 *
 * Print routeType struc.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None
 *
 * ----------------------------------------------------------------------------
 */

void
mzPrintRT(rT)
    RouteType *rT;
{
    int i;

    TxPrintf("\tROUTETYPE:\n");
    TxPrintf("\t\ttileType = %s\n", DBTypeLongNameTbl[rT->rt_tileType]);
    TxPrintf("\t\tactive = %s\n", (rT->rt_active ? "TRUE" : "FALSE"));
    TxPrintf("\t\twidth = %d\n",rT->rt_width);
    
    TxPrintf("\t\tspacing = ");
    for (i=0;i<TT_MAXTYPES;i++)
	if(rT->rt_spacing[i]>=0)
	    TxPrintf("%s(%d) ",DBTypeLongNameTbl[i],rT->rt_spacing[i]);
    if(rT->rt_spacing[TT_SUBCELL]>=0)
	TxPrintf("%s(%d) ","SUBCELL",rT->rt_spacing[TT_SUBCELL]);
    TxPrintf("\n");

    TxPrintf("\t\teffWidth = %d\n",rT->rt_effWidth);

    for (i=0;i<TT_MAXTYPES;i++)
	if(rT->rt_bloatBot[i]>=0)
	    TxPrintf("%s(%d) ",DBTypeLongNameTbl[i],rT->rt_bloatBot[i]);
    if(rT->rt_spacing[TT_SUBCELL]>=0)
	TxPrintf("%s(%d) ","SUBCELL",rT->rt_bloatBot[TT_SUBCELL]);
    TxPrintf("\n");

    for (i=0;i<TT_MAXTYPES;i++)
	if(rT->rt_bloatTop[i]>=0)
	    TxPrintf("%s(%d) ",DBTypeLongNameTbl[i],rT->rt_bloatTop[i]);
    if(rT->rt_spacing[TT_SUBCELL]>=0)
	TxPrintf("%s(%d) ","SUBCELL",rT->rt_bloatTop[TT_SUBCELL]);
    TxPrintf("\n");

    TxPrintf("\t\tnext = %s\n", 
	(rT->rt_next ? DBTypeLongNameTbl[rT->rt_next->rt_tileType] : "(nil)"));

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * MZPrintRCs --
 *
 * Print list of RouteLayer strucs.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None
 *
 * ----------------------------------------------------------------------------
 */

void
MZPrintRCs(rC)
    RouteContact *rC;
{
    while(rC!=NULL)
    {
	mzPrintRC(rC);
	rC = rC->rc_next;
	if(rC!=NULL) TxMore("");
    }
    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzPrintRC --
 *
 * Print single RouteContact struc.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None
 *
 * ----------------------------------------------------------------------------
 */

void
mzPrintRC(rC)
    RouteContact *rC;
{
    TxPrintf("ROUTE CONTACT:\n");
    mzPrintRT(&(rC->rc_routeType));

    TxPrintf("\trLayer1 = %s\n", 
	DBTypeLongNameTbl[rC->rc_rLayer1->rl_routeType.rt_tileType]);
    TxPrintf("\trLayer2 = %s\n", 
	DBTypeLongNameTbl[rC->rc_rLayer2->rl_routeType.rt_tileType]);

    TxPrintf("\tcost = %d\n",
	     rC->rc_cost);

    return;
}



/*
 * ----------------------------------------------------------------------------
 *
 * mzPrintRPs --
 *
 * Print list of RoutePath strucs.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None
 *
 * ----------------------------------------------------------------------------
 */

void
mzPrintRPs(path)
    RoutePath *path;
{
    while(path!=NULL)
    {
	mzPrintRP(path);
	path = path->rp_back;
    }
    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzPrintRP --
 *
 * Print single RoutePath struc.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None
 *
 * ----------------------------------------------------------------------------
 */

void
mzPrintRP(path)
    RoutePath *path;
{
    TxPrintf("ROUTE PATH:");
    TxPrintf("  layer = %s",
	DBTypeLongNameTbl[path->rp_rLayer->rl_routeType.rt_tileType]);
    TxPrintf(" entry = (%d, %d)", path->rp_entry.p_x, path->rp_entry.p_y);
    TxPrintf(" cost = %.0f", 
	    							 (double)(path->rp_cost));
    TxPrintf(" extCode = { ");
    if (path->rp_extendCode & EC_RIGHT)
    {
        TxPrintf("right ");
    }
    if (path->rp_extendCode & EC_LEFT)
    {
        TxPrintf("left ");
    }
    if (path->rp_extendCode & EC_UP)
    {
        TxPrintf("up");
    }
    if (path->rp_extendCode & EC_DOWN)
    {
        TxPrintf("down ");
    }
    if (path->rp_extendCode  & (EC_UDCONTACTS | EC_LRCONTACTS))
    {
        TxPrintf("contacts ");
    }

    TxPrintf("}\n");

    return;
}

/* mzPrintPathHead -- */
void 
mzPrintPathHead(path)
    RoutePath *path;
{

    if(path==NULL)
    {
	TxPrintf("  NULL Path.\n");
    }
    else
    {
	TxPrintf("  point=(%d,%d), layer=%s, orient = '%c'", 
		 path->rp_entry.p_x,
		 path->rp_entry.p_y,
		 DBTypeLongNameTbl[path->rp_rLayer->rl_routeType.rt_tileType],
		 path->rp_orient);
	TxPrintf(", togo=%.0f",
		 									(double)(path->rp_togo));
	TxPrintf(", cost=%.0f\n",
		 (double)(path->rp_cost));

	TxPrintf("    extendCode = { ");
	if (path->rp_extendCode & EC_RIGHT)
	{
	    TxPrintf("right ");
	}
	if (path->rp_extendCode & EC_LEFT)
	{
	    TxPrintf("left ");
	}
	if (path->rp_extendCode & EC_UP)
	{
	    TxPrintf("up ");
	}
	if (path->rp_extendCode & EC_DOWN)
	{
	    TxPrintf("down ");
	}
	if (path->rp_extendCode  & (EC_LRCONTACTS | EC_UDCONTACTS))
	{
	    TxPrintf("contacts ");
	}

	TxPrintf("}\n");
    }
    return;
}

/*
 * ----------------------------------------------------------------------------
 *
 * mzDumpTags -- 
 *
 * Dump tags on data tiles (for debugging).
 *
 * Results:  
 *	None.
 *
 * Side effects:  
 *	info written to file or via TxPrintf()
 *
 * ----------------------------------------------------------------------------
 */

void
mzDumpTags(area)
    Rect *area;
{
    int mzDumpTagsFunc();
    SearchContext scx;

    /* mzke sure mzRouteUse is initialed */
    if(mzRouteUse == NULL)
    {
	TxPrintf("Can not dump tags, until mzRouteUse is initialed.\n");
	TxPrintf("(Do an iroute first.)\n");
	return;
    }

    /* look at all data tiles under box. */
    scx.scx_area = *area;
    scx.scx_trans = GeoIdentityTransform;
    scx.scx_use = mzRouteUse;
    
    (void) DBTreeSrTiles(
	&scx, 
	&DBAllTypeBits, 
	0, 	/* look inside all subcells */
	mzDumpTagsFunc, 
	(ClientData) NULL);

    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzDumpTagsFunc --
 *
 * Filter function called above, dumps tag info for all tiles in search area.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Dumps info associated with tile.
 *
 * ----------------------------------------------------------------------------
 */

int
mzDumpTagsFunc(tile, cxp)
    Tile *tile;
    TreeContext *cxp;
{
    SearchContext *scx = cxp->tc_scx;
    Rect r;

    /* if tile has no client data attached, skip it */
    if (tile->ti_client == (ClientData)CLIENTDEFAULT)
        return 0;
	
    /* Get boundary of tile */
    TITORECT(tile, &r);

    /* print tile bounds */
    TxPrintf("tile %x  (x: %d to %d, y: %d to %d)\n",
	     (pointertype) tile, r.r_xbot, r.r_xtop, r.r_ybot, r.r_ytop);

    /* dump rects attached to client field */
    {
	List *l;
        for(l=(List *) (tile->ti_client); l!=NULL; l=LIST_TAIL(l))
        {
	    Rect *rTerm = (Rect *) LIST_FIRST(l);
	    
	    TxPrintf("\tattached dest area (x: %d to %d, y: %d to %d)\n",
		     rTerm->r_xbot, 
		     rTerm->r_xtop, 
		     rTerm->r_ybot, 
		     rTerm->r_ytop);
	}
    }
		
    /* continue search */
    return 0;
}

