/*
 * irRoute.c --
 *
 * Sets up route and calls maze router to do it.
 *
 *     ********************************************************************* 
 *     * Copyright (C) 1987, 1990 Michael H. Arnold, Walter S. Scott, and  *
 *     * the Regents of the University of California.                      *
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/irouter/irRoute.c,v 1.3 2008/12/11 04:20:08 tim Exp $";
#endif  /* not lint */

/* --- Includes --- */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/hash.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "drc/drc.h"
#include "textio/textio.h"
#include "windows/windows.h"
#include "utils/main.h"
#include "dbwind/dbwind.h"
#include "debug/debug.h"
#include "utils/undo.h"
#include "utils/signals.h"
#include "utils/malloc.h"
#include "utils/list.h"
#include "utils/geofast.h"
#include "utils/touchingtypes.h"
#include "select/select.h"
#include "../mzrouter/mzrouter.h"
#include "irouter/irouter.h"
#include "irouter/irInternal.h"

/* --- Routines local to this file that are referenced before they are 
 *     defined --- */
List *irChooseEndPtLayers();

/* -------------------- Structures Local to this File -------------------- */

/* clientdata structure passed to filter functions when searching for
 * start and destination labels.
 */
typedef struct labelSearchData
{
    Rect 	lsd_locRect;	/* set to labels location by filter func */
    char	*lsd_name;	/* label name to search for */
    TileType	lsd_type;	/* layer that label is attached to */
    int 	lsd_result;	/* code giving result of search */
} LabelSearchData;

/* result codes for lsd_result above */
#define LSR_NOTFOUND	10
#define LSR_NOTUNIQUE	20
#define LSR_FOUND	30


/*
 * ----------------------------------------------------------------------------
 *
 * irRoute --
 *
 * Top level procedure for the routing code.  Initializes things, and
 * calls the maze router to make a connection.
 *
 * Results:
 *	Passes back the result code of MZRoute() (see mzrouter.h for codes)
 *
 * Side effects:
 *	Paint route into editcell.
 *
 * ----------------------------------------------------------------------------
 */

int
irRoute(cmdWindow, startType, argStartPt, argStartLabel, argStartLayers, 
		destType, argDestRect, argDestLabel, argDestLayers)
    MagWindow *cmdWindow;	/* window route command issued to */
    int startType;	/* how start is specified */
    Point *argStartPt;	/* location to route from (in edit cell coords) */
    char *argStartLabel;	/* label to route from */
    List *argStartLayers;	/* OK route layers to start route on */
    int destType;	/* how dest is specified */
    Rect *argDestRect;	/* location to route to (in edit cell coords) */
    char *argDestLabel; /* label to route to */
    List *argDestLayers;	/* OK route layers to end route on */
{
    CellUse *routeUse;		/* Toplevel cell visible during routing */
    int expansionMask;		/* Subcell expansion modes to use during 
				 * routing */
    Point startPt; 		/* start and dest terminals */
    List *startLayers = NULL;
    Rect destRect;
    List *destLayers = NULL;
    RoutePath *path = NULL;	/* resulting path */
    TileType startLayer = TT_SPACE;
    int mzResult = MZ_NO_ACTION;

    /* determine routeUse and expansionMask for this route. */
    {
	MagWindow *window = NULL;

	/* find global reference window */
	if(irRouteWid>=0)
	{
	    window = WindSearchWid(irRouteWid);
	    if (window == NULL)
	    {
		TxError("Couldn't find route window (%d),", irRouteWid);
	        TxError("using command window as reference.\n");
	    }
	}

	/* if no global reference window, use window command issued from */
	if(window == NULL)
	{
	    window = cmdWindow;
	}

	/* If reference window is nil, complain and exit */
	if(window==NULL)
	{
	    TxError("Point to a layout window first.\n");
	    return mzResult;
	}

	/* Set expansion mask to window route cmd issued to.  Used 
	 * during searches.  Subcells are treated as expanded if expanded in
	 * window cmd issued to.
	 */
        expansionMask = ((DBWclientRec *)(window->w_clientData))->dbw_bitmask;

	/* Set routeUse to rootuse of reference window - 
	 *  everything "visible" in 
	 *  the reference window is visible
	 *  during routing.  
	 * But resulting route
	 * is painted into edit cell.  This distinction is important only
	 * in the case of a subedit.  If the user subedits a cell, the
	 * context of the parent(s) will guide the route, but the route 
	 * will be painted into the edit cell.
	 */
	routeUse = (CellUse *) (window->w_surfaceID);

	/* make sure cmd issued from window in which edit cell 
	 * is being edited */
	if (!EditCellUse || EditRootDef != routeUse->cu_def)
	{
	    TxError("Nothing being edited in route window.\n");
	    return mzResult;
	}
    }

    /* initialize mzrouter */
    MZInitRoute(irMazeParms, routeUse, expansionMask);

    /* Figure out start coordinates */
    {
	Point irGetStartPoint();

	startPt = irGetStartPoint(startType, 
				  argStartPt, 
				  argStartLabel,
				  &startLayer,
				  routeUse);

	/* check for failure */
	if(startPt.p_x == MINFINITY) goto abort;
    }

    /* Set maze router dest area(s) */

    if(destType == DT_SELECTION)
    /* add destination area for each selected area on an appropriate layer */
    {
	int irSelectedTileFunc();

	if(argDestLayers == NULL)
	/* no layer arg specified, generate dest areas for each active
	 * route layer
	 */
	{
	    RouteLayer *rL;
		
	    for (rL = irRouteLayers; rL != NULL; rL = rL->rl_next)
	    {
		if(rL->rl_routeType.rt_active)
		{
		    /* set dest area for each selected tile 
		     * of type connecting to rL
		     */
		    SelEnumPaint(
				 &(DBConnectTbl[rL->rl_routeType.rt_tileType]),
				 FALSE,	/* TRUE = restricted to edit cell */
				 NULL, /* not used */
				 irSelectedTileFunc, 
				 (ClientData *) rL /* type of destarea */
				 );
		}
	    }
	}
	else
	/* generate dest areas for layers that are both specified and active */
	{
	    List *l;

	    for(l=argDestLayers; l!=NULL; l=LIST_TAIL(l))
	    {
		RouteLayer *rL = (RouteLayer *) LIST_FIRST(l);

		if(rL->rl_routeType.rt_active)
		{
		    /* set dest area for each selected tile 
		     * of type connecting to rL
		     */
		    SelEnumPaint(
				 &(DBConnectTbl[rL->rl_routeType.rt_tileType]),
				 FALSE,	/* TRUE = restricted to edit cell */
				 NULL, /* not used */
				 irSelectedTileFunc, 
				 (ClientData *) rL /* type of destarea */
				 );
		}
	    }
	}
    }
    else
    /* dest is defined by rectangle */
    {
	Rect irGetDestRect();
	TileType destLayer = TT_SPACE;

	destRect = irGetDestRect(destType, 
				 argDestRect, 
				 argDestLabel,
				 &destLayer,
				 routeUse);

	/* check for failure */
	if(destRect.r_xtop == MINFINITY) goto abort;

	/* set a maze router dest area for extent of destRect
	 * on each permitted dest layer.
	 */
	if (destLayer != TT_SPACE)
	{
	    RouteLayer *rL;

	    /* layer type returned by irGetDestRect */

	    for(rL = irRouteLayers; rL != NULL; rL = rL->rl_next)
	    {
		if (rL->rl_routeType.rt_active &&
			TTMaskHasType(&(DBConnectTbl[destLayer]),
                                   rL->rl_routeType.rt_tileType))
		{
		    MZAddDest(&destRect, rL->rl_routeType.rt_tileType);
		    break;
		}
	    }
	}
	else if (argDestLayers == NULL)
	{
	    /* no layer arg specified, permit all active route layers */
	    RouteLayer *rL;
	    
	    for (rL = irRouteLayers; rL != NULL; rL = rL->rl_next)
	    {
		if(rL->rl_routeType.rt_active)
		{
		    MZAddDest(&destRect,rL->rl_routeType.rt_tileType);
		}
	    }
	}
	else
	/* permit only layers that are both specified and active */
	{
	    List *l;
	    
	    for(l=argDestLayers; l!=NULL; l=LIST_TAIL(l))
	    {
		RouteLayer *rL = (RouteLayer *) LIST_FIRST(l);
		
		if(rL->rl_routeType.rt_active)
		{
		    MZAddDest(&destRect,rL->rl_routeType.rt_tileType);
		    
		}
	    }
	}	    
    }

    if (startLayer != TT_SPACE)
    {
	RouteLayer *rL;

	/* layer type returned by irGetStartPoint */

	for (rL = irRouteLayers; rL != NULL; rL = rL->rl_next)
	{
	    if (rL->rl_routeType.rt_active &&
			TTMaskHasType(&(DBConnectTbl[startLayer]),
                                   rL->rl_routeType.rt_tileType))

	    {
		MZAddStart(&startPt, rL->rl_routeType.rt_tileType);
		break;
	    }
	}
    }
    else
    {
	/* Determine OK start layers */

	List *l;

	startLayers = irChooseEndPtLayers(
					  routeUse, 
					  expansionMask, 
					  &startPt,
					  argStartLayers,
					  "start");
	if(SigInterruptPending) goto abort;

	if(DebugIsSet(irDebugID,irDebEndPts))
	{
	    TxPrintf("----- startLayers:\n");
	    MZPrintRLListNames(startLayers);
	}

	/* Set maze router start point(s) - one for each ok layer */

	for(l=startLayers; l!=NULL;l=LIST_TAIL(l))
	{
	    RouteLayer *rL = (RouteLayer *)LIST_FIRST(l);
	    TileType type = rL->rl_routeType.rt_tileType;

	    MZAddStart(&startPt, type);
	}
    }

    /* Do the Route */
    path = MZRoute(&mzResult);
		      /* If MZRoute is interrupted it returns best path
		       * found so far.
		       */

    if(SigInterruptPending)
    {
	if(path==NULL)
	{
	    goto abort;
	}
	else
	{
	    TxError("Search Interrupted!\n");
	    TxPrintf("Using best path found prior to interrupt.\n");

	    /* Clear interrupt to allow paint back of path */
	    SigInterruptPending = FALSE;
	}
    }

    /* paint route back into edit cell */
    if (path) 
    {
	
	RouteLayer *finalRL = path->rp_rLayer;
	CellUse *resultUse;

        /* Have MazeRouter paint path into resultCell */
        resultUse = MZPaintPath(path);
	if(SigInterruptPending) goto abort;

	/* Copy to edit cell transforming from root to edit
	 * coords.
	 * Also select the entire route.
	 * This paint job is undoable.
	 */
	{
	    SearchContext scx;

	    scx.scx_use = resultUse;
	    scx.scx_area = resultUse->cu_def->cd_bbox;
	    scx.scx_trans = RootToEditTransform;
	    (void) DBCellCopyPaint(&scx, &DBAllButSpaceBits, 0, EditCellUse);
	    DBReComputeBbox(EditCellUse->cu_def);
	}

	/* Select the route */
	{
	    SearchContext scx;

	    /* Clear selection, and set selection display for reference
	     * window and other windows containing routeUse as root.
	     */
	    SelectClear();
	    if(SelectRootDef != routeUse->cu_def)
	    {
		SelectRootDef = routeUse->cu_def;
		SelSetDisplay(SelectUse, SelectRootDef);
	    }

	    /* Copy route to selection cell, notifying undo of change */
	    scx.scx_use = resultUse;
	    scx.scx_area = resultUse->cu_def->cd_bbox;
	    scx.scx_trans = GeoIdentityTransform;
	    SelRememberForUndo(TRUE, (CellDef *) NULL, (Rect *) NULL);
	    (void) DBCellCopyPaint(&scx, &DBAllButSpaceBits, 0, SelectUse);
	    SelRememberForUndo(FALSE, SelectRootDef, &(scx.scx_area));

	    /* Setup redisplay */
	    DBReComputeBbox(SelectDef);
	    DBWHLRedraw(SelectRootDef, &(scx.scx_area), TRUE);
	    DBWAreaChanged(SelectDef, &SelectDef->cd_bbox, DBW_ALLWINDOWS,
			   &DBAllButSpaceBits);
	}

	/* Notify dbwind module (for redisplay), and DRC module 
	 * of changed area */
	{
	    Rect changedArea;

	    GeoTransRect(
			 &RootToEditTransform,
			 &(resultUse->cu_def->cd_bbox),
			 &changedArea);
	    DBWAreaChanged(EditCellUse->cu_def, &changedArea, DBW_ALLWINDOWS,
			   &DBAllButSpaceBits);
	    DRCCheckThis(EditCellUse->cu_def, TT_CHECKPAINT, &changedArea);

	}

	/* Make sure we got here without interruption */
	if(SigInterruptPending) goto abort;

	TxPrintf("Done Routing.\n");
	TxFlushOut();
    }
    else
    {
	TxError("Route Failed.\n");
    }

abort:
    if(SigInterruptPending)
    {
	TxError("Route Interrupted!\n");
    }

    ListDealloc(startLayers);
    ListDealloc(destLayers);

    /* reclaim storage used by mzrouter */
    if(!DebugIsSet(irDebugID, irDebNoClean))
    {
	MZClean();
    }

    return mzResult;
}


/*
 * ---------------------------------------------------------------------
 *
 * irGetStartPoint --
 *
 * Compute start point.
 *
 * Results:
 *	Returns point.
 *
 * Side effects:
 *	Returns start layer type in startLayerPtr.
 *
 * ---------------------------------------------------------------------
 */

Point
irGetStartPoint(startType, argStartPt, argStartLabel, startLayerPtr, routeUse)
    int startType;		/* how start is specified */
    Point *argStartPt;		/* location to route from 
				 * (in edit cell coords) */
    char *argStartLabel;	/* label to route from */
    TileType *startLayerPtr;	/* layer type (returned value) */
    CellUse *routeUse;		/* toplevel cell visible to router */
{
    Point startPt;

    switch (startType)
    {
	case ST_POINT:
	/* start point coords given */
	{
	    /* convert from edit to routeUse coords (= root coordinates) */
	    GeoTransPoint(&EditToRootTransform,argStartPt,&startPt);
	}
	break;

	case ST_CURSOR:
	/* use cursor */
	{
	    MagWindow *pointWindow;

	    pointWindow = ToolGetPoint(&startPt, (Rect *) NULL);

	    if (pointWindow == NULL)
	    {
		TxError("Can not use cursor as start:");
		TxError("  cursor not in layout window.\n");
		goto abort;
	    }

	    if (routeUse->cu_def  != 
		((CellUse *)pointWindow->w_surfaceID)->cu_def)
	    {
		TxError("Can not use cursor as start:");
		TxError("cursor not in routecell.\n");
		goto abort;
	    }
	}
	break;

	case ST_LABEL:
	/* label name given */
	{   
	    int irSelLabelsFunc();
	    int irAllLabelsFunc();
	    LabelSearchData lSD;
	    lSD.lsd_name = argStartLabel;   /* name to match */
	    lSD.lsd_result = LSR_NOTFOUND;

	    /* first search selection */
	    (void) SelEnumLabels(&DBAllTypeBits,
				 FALSE,	/* TRUE = search only edit cell */
				 (bool *) NULL,
				 irSelLabelsFunc,
				 (ClientData) &lSD);

	    if(SigInterruptPending) goto abort;

	    if (lSD.lsd_result == LSR_NOTUNIQUE)
	    {
		TxError("Warning: Start label '%s' not unique.\n", argStartLabel);
	    }
	    else if (lSD.lsd_result == LSR_NOTFOUND)
	    /* No selected label matched, so search all labels */
	    {

		(void) DBSrLabelLoc(routeUse,
				    argStartLabel, 
				    irAllLabelsFunc, 
				    (ClientData) &lSD);

		if(SigInterruptPending) goto abort;
	
		if (lSD.lsd_result == LSR_NOTUNIQUE)
		{
		    TxError("Warning: Start label '%s' not unique.\n", argStartLabel);
		}
		else if (lSD.lsd_result == LSR_NOTFOUND)
		{
		    TxError("Start label '%s' not found.\n",
			    argStartLabel);
		    goto abort;
		}
	    }

	    startPt = lSD.lsd_locRect.r_ll;
	    if (startLayerPtr) *startLayerPtr = lSD.lsd_type;
	}
	break;

	default:
	/* shouldn't happen */
	{
	    ASSERT(FALSE,"irGetStartPoint");
	}
	break;
    }

    return startPt;

abort:
    startPt.p_x = MINFINITY;
    startPt.p_y = MINFINITY;
    return startPt;
}


/*
 * ---------------------------------------------------------------------
 *
 * irGetDestRect --
 *
 * Compute destination rectangle.
 *
 * Results:
 *	Returns rect.
 *
 * Side effects:
 *	Returns layer type in destLayerPtr.
 *
 * ---------------------------------------------------------------------
 */

Rect
irGetDestRect(destType, argDestRect, argDestLabel, destLayerPtr, routeUse)
    int destType;		/* how dest is specified */
    Rect *argDestRect;		/* location to route to 
				 * (in edit cell coords) */
    char *argDestLabel;		/* label to route to */
    TileType *destLayerPtr;	/* layer type (returned value) */
    CellUse *routeUse;		/* toplevel cell visible to router */
{
    Rect destRect;

    switch (destType)
    {
	case DT_RECT:
	/* dest rect coords given */
	{
	    /* convert from edit to routeUse coords (= root coordinates) */
	    GeoTransRect(&EditToRootTransform,argDestRect,&destRect);
	}
	break;

	case DT_LABEL:
	/* dest rect given as label */
	{   
	    int irSelLabelsFunc();
	    int irAllLabelsFunc();
	    LabelSearchData lSD;
	    lSD.lsd_name = argDestLabel;   /* name to match */
	    lSD.lsd_result = LSR_NOTFOUND;

	    /* first search selection */
	    (void) SelEnumLabels(&DBAllTypeBits,
				 FALSE,	/* TRUE = search only edit cell */
				 (bool *) NULL,
				 irSelLabelsFunc,
				 (ClientData) &lSD);
	    if(SigInterruptPending) goto abort;

	    if (lSD.lsd_result == LSR_NOTUNIQUE)
	    {
		TxError("Warning: Destination label '%s' not unique.\n", argDestLabel);
	    }
	    else if (lSD.lsd_result == LSR_NOTFOUND)
	    {
		/* No selected label matched, so search all labels */
		(void) DBSrLabelLoc(routeUse,
				    argDestLabel, 
				    irAllLabelsFunc, 
				    (ClientData) &lSD);
		if(SigInterruptPending) goto abort;

		if (lSD.lsd_result == LSR_NOTUNIQUE)
		{
		    TxError("Warning: Destination label '%s' not unique.\n",
				argDestLabel);
		}
		else if (lSD.lsd_result == LSR_NOTFOUND)
		{
		    TxError("Destination label '%s' not found.\n",
			    argDestLabel);
		    goto abort;
		}
	    }

	    destRect = lSD.lsd_locRect;
	    if (destLayerPtr) *destLayerPtr = lSD.lsd_type;
	}
	break;

	case DT_BOX:
	/* use box as dest rect */
	{
	    CellDef *boxDef;
	    Rect box;

	    if(!ToolGetBox(&boxDef,&box))
	    {
		TxError("Can not use box for dest:  No Box.\n");
		goto abort;
	    }

	    if (boxDef != routeUse->cu_def)
	    {
		TxError("Can not use box for dest:  ");
		TxError("box not in route cell.\n");
		goto abort;
	    }

	    destRect = box;
	}
	break;

	default:
	/* shouldn't happen */
	{
	    ASSERT(FALSE,"irGetDestRect");
	}
	break;
    }

    return destRect;

abort:
    destRect.r_xbot = MINFINITY;
    destRect.r_ybot = MINFINITY;
    destRect.r_xtop = MINFINITY;
    destRect.r_ytop = MINFINITY;
    return destRect;
}

/*
 * ---------------------------------------------------------------------
 *
 * irSelLabelsFunc --
 *
 * Called by SelEnumLabels on behalf of irRoute above, to find selected
 * label of given name.
 *
 * Results:
 *	Returns 0 on first match, 1 on second match (to terminate search)
 *
 * Side effects:
 *	Sets locRect in clientdata arg location off matching label.
 *
 * ---------------------------------------------------------------------
 */

int
irSelLabelsFunc(label, cellUse, transform, clientData)
    Label *label;
    CellUse *cellUse;
    Transform *transform;
    ClientData clientData;
{
    LabelSearchData *lsd = (LabelSearchData *)clientData;
    CellDef *cellDef = cellUse->cu_def;

    if (strcmp(lsd->lsd_name, label->lab_text) != 0)
    {
	/* this label doesn't match, continue search */
	return 0;
    }
    else if (lsd->lsd_result == LSR_FOUND)
    {
	/* second match, set result and terminate search */
	lsd->lsd_result = LSR_NOTUNIQUE;
	return 1;
    }
    else
    {
	/* first match, set location, result, and continue search */
	GeoTransRect(transform,
		&(label->lab_rect), &(lsd->lsd_locRect));
	lsd->lsd_result = LSR_FOUND;
	lsd->lsd_type = label->lab_type;
	return 0;
    }
}


/*
 * ---------------------------------------------------------------------
 *
 * irAllLabelsFunc --
 *
 * Called by DBSrLabelLoc on behalf of irRoute above, to convert labelName
 * to location of label with matching name.
 *
 * Results:
 *	Returns 0 on first match, 1 on second match (to terminate search)
 *
 * Side effects:
 *	Sets locRect in clientdata arg to location of matching label.
 *
 * ---------------------------------------------------------------------
 */

int
irAllLabelsFunc(rect, name, label, clientData)
    Rect *rect;
    char *name;
    Label *label;
    ClientData clientData;
{
    LabelSearchData *lsd = (LabelSearchData *)clientData;

    if (lsd->lsd_result == LSR_FOUND)
    {
	if (GEO_SAMERECT(lsd->lsd_locRect, *rect)) return 0;

	/* second match, so set result and terminate search */
	lsd->lsd_result = LSR_NOTUNIQUE;
	return 1;
    }
    else
    {
	/* first match, so set location, result, and continue search */
	lsd->lsd_locRect = *rect;
	lsd->lsd_type = label->lab_type;
	lsd->lsd_result = LSR_FOUND;
	return 0;
    }
}


/*
 * ---------------------------------------------------------------------
 *
 * irSelectedTileFunc --
 *
 * Called by SelEnumPaint on behalf of irRoute above, to process tile
 * associated with selection rect.
 *
 * Results:
 *	Always returns 0 to continue search.
 *
 * Side effects:
 *	Call MzAddDest on selected area.
 *
 * ---------------------------------------------------------------------
 */

int
irSelectedTileFunc(rect, type, c)
    Rect *rect;
    TileType type;
    ClientData c;
{
    RouteLayer *rL = (RouteLayer *) c;
    MZAddDest(rect, rL->rl_routeType.rt_tileType);
    
    /* return 0 to continue search */
    return 0;
}


/*
 * ----------------------------------------------------------------------------
 *
 * LayerInTouchingContact --
 *
 * A Predicate.  Checks whether the given routeLayer is a component of a 
 * contact type in touchingTypes.  Used by irChooseEndPtLayers below.
 *
 * Results:
 *	TRUE if the RouteLayer is a component of a contact type in 
 *	touchingTypes, else FALSE.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
LayerInTouchingContact(rL,touchingTypes)
    RouteLayer *rL;
    TileTypeBitMask touchingTypes;
{
    RouteContact *rC;

    for(rC=irRouteContacts; rC!=NULL; rC=rC->rc_next)
    {
	if(TTMaskHasType(&touchingTypes,rC->rc_routeType.rt_tileType) &&
	        (rC->rc_rLayer1==rL || rC->rc_rLayer2==rL))
	    return(TRUE);
    }

    return(FALSE);
}


/*
 * ----------------------------------------------------------------------------
 *
 * irChooseEndPtLayers --
 *
 * Figure out what layers are ok at an endpoint - if ambiguous ask user.
 *
 * Results:
 *	List of ok layers at endpoint.
 *
 * Side effects:
 *	May query user about intended layers at an endpoint.
 *
 * ----------------------------------------------------------------------------
 */

List *
irChooseEndPtLayers(routeUse,expansionMask,endPt,argLayers,endPtName)
    CellUse *routeUse;
    int expansionMask; /* mask of expanded subcells */
    Point *endPt;
    List *argLayers;
    char *endPtName;
{
    List *activeLayers;
    List *presentLayers;
    List *presentContacts;
    List *presentContactLayers;

    static char *actionNames[] = { "no", "yes", 0 };

    List *l;
    RouteLayer *rL;

    if (DebugIsSet(irDebugID, irDebEndPts))
    {
	TxPrintf("----- argLayers:\n");
	MZPrintRLListNames(argLayers);
    }

    /* find activeLayers among argLayers (or irRouteLayers if argLayers NULL) */
    activeLayers = NULL;
    if(argLayers)
    {
	/* layers given in arg, search these */
	for(l=argLayers; l!=NULL; l=LIST_TAIL(l))
	{
	    rL = (RouteLayer*) LIST_FIRST(l);
	    if(rL->rl_routeType.rt_active)
	    {
		LIST_ADD(rL,activeLayers);
	    }
	}
    }
    else
    {
	/* no layers given as arg, so search all route layers */
        for(rL=irRouteLayers; rL!=NULL; rL=rL->rl_next)
	    if(rL->rl_routeType.rt_active)
	    {
		LIST_ADD(rL,activeLayers);
	    }
    }

    if (DebugIsSet(irDebugID, irDebEndPts))
    {
	TxPrintf("----- activeLayers:\n");
	MZPrintRLListNames(activeLayers);
    }

    /* make lists of contacts (connecting two active layers) and
     * active layers which are present at the end point.
     */
    {
	TileTypeBitMask touchingTypes;
	RouteContact *rC;
	
	touchingTypes = TouchingTypes(routeUse, expansionMask, endPt);

        /* Make list of present and active contacts */
	presentContacts = NULL;
	presentContactLayers = NULL;
	for(rC =irRouteContacts; rC!=NULL; rC=rC->rc_next)
	{
	    if(TTMaskHasType(&touchingTypes,rC->rc_routeType.rt_tileType) &&
		    ListContainsP(rC->rc_rLayer1, activeLayers) &&
		    ListContainsP(rC->rc_rLayer2, activeLayers))
	    {
		LIST_ADD(rC, presentContacts);
		LIST_ADD(rC->rc_rLayer1, presentContactLayers);
		LIST_ADD(rC->rc_rLayer2, presentContactLayers);
	    }
	}

	if (DebugIsSet(irDebugID, irDebEndPts))
	{
	    TxPrintf("----- presentContacts:\n");
	    MZPrintRCListNames(presentContacts);

	    TxPrintf("----- presentContactLayers:\n");
	    MZPrintRLListNames(presentContactLayers);
	}

	    
	/* make list of present layers that are not constituents of contacts
	 * above.  If a contact is touching the endpt but one of 
	 * its constituent layers in not active, the other constituent
	 * layer is treated as a presentLayer.
	 */
	presentLayers = NULL;
	for(l=activeLayers; l!=NULL; l=LIST_TAIL(l))
	{
	    rL = (RouteLayer *) LIST_FIRST(l);
	    if((TTMaskHasType(&touchingTypes,rL->rl_routeType.rt_tileType) ||
		    LayerInTouchingContact(rL,touchingTypes)) &&
		    !ListContainsP(rL, presentContactLayers))
	    {
		LIST_ADD(rL,presentLayers);
	    }
	}

	if (DebugIsSet(irDebugID, irDebEndPts))
	{
	    TxPrintf("----- presentLayers:\n");
	    MZPrintRLListNames(presentLayers);
	}
    }

    /* return appropriate layer list. */
    {
	int numContacts, numLayers;

	numContacts = ListLength(presentContacts);
	numLayers = ListLength(presentLayers);

	if(numLayers == 0 && numContacts == 0)
	{
	    /* No Layers present at endpt, return list of all active layers */
	    ListDealloc(presentLayers);
	    ListDealloc(presentContacts);
	    ListDealloc(presentContactLayers);

	    return(activeLayers);
	}
	else if(numLayers == 1 && numContacts == 0)
	{
	    /* Exactly one layer is both active and present, return list
	     * containing only this layer.
	     */
	    ListDealloc(activeLayers);
	    ListDealloc(presentContacts);
	    ListDealloc(presentContactLayers);

	    return(presentLayers); 
	}
	else if(numLayers == 0 && numContacts == 1)
	{
	    /* Just one active contact under endpoint, 
	     * return layers connecting to that contact.
	     */
	    List *l;
	    RouteContact *rC;

	    rC = (RouteContact *) LIST_FIRST(presentContacts);
	    l = (List *) NULL;
	    LIST_ADD(rC->rc_rLayer1,l);
	    LIST_ADD(rC->rc_rLayer2,l);

	    ListDealloc(activeLayers);
	    ListDealloc(presentLayers);
	    ListDealloc(presentContacts);
	    ListDealloc(presentContactLayers);

	    return(l);
	}
	else
	{
	    /* Multiple nodes active and present, ask user which one
	     * he wants to route to.
	     */
	    char answer[100];
	    RouteLayer *rL;
	    RouteLayer *pickedRL;
	    RouteContact *rC;
	    RouteContact *pickedRC;

	    TxPrintf("Multiple nodes present at %s point:",
		    endPtName);
	    for(l=presentContacts; l!=NULL; l=LIST_TAIL(l))
	    {
		rC=(RouteContact *) LIST_FIRST(l);
		TxPrintf("  %s", 
			DBTypeLongNameTbl[rC->rc_routeType.rt_tileType]);
	    }
	    for(l=presentLayers; l!=NULL; l=LIST_TAIL(l))
	    {
		rL=(RouteLayer *) LIST_FIRST(l);
		TxPrintf("  %s", 
			DBTypeLongNameTbl[rL->rl_routeType.rt_tileType]);
	    }
	    TxPrintf("\n");

	    for(pickedRC=FALSE,l=presentContacts; l && !pickedRC; l=LIST_TAIL(l))
	    {
		rC = (RouteContact *) LIST_FIRST(l);
		if (!LIST_TAIL(l) && !presentLayers)
		{
		    /* last choice, so take it */
		    pickedRC = rC;
		}
		else
		{
		    /* ask user */
		    TxPrintf("Connect to %s? [yes] ",
			    DBTypeLongNameTbl[rC->rc_routeType.rt_tileType]);
		    if (TxGetLine(answer, sizeof answer) == NULL || 
			    answer[0] == '\0')
			(void) strcpy(answer,"yes");

		    if(Lookup(answer, actionNames) == 1)
		    {
			/* Yes */
			pickedRC = rC;
		    }
		}
	    }

	    if(pickedRC)
	    {
		List *l;

		l=NULL;
		LIST_ADD(rC->rc_rLayer1,l);
		LIST_ADD(rC->rc_rLayer2,l);
		ListDealloc(activeLayers);
		ListDealloc(presentLayers);
		ListDealloc(presentContacts);
		ListDealloc(presentContactLayers);

		return(l);
	    }

	    for(pickedRL=NULL,l=presentLayers; l && !pickedRL; l=LIST_TAIL(l))
	    {
		rL = (RouteLayer *) LIST_FIRST(l);
	        if(!LIST_TAIL(l))
		{
		    /* Last choice so choose it automatically */
		    pickedRL=rL;
		}
		else
		{
		    /* Ask user */
		    TxPrintf("Connect to %s? [yes] ",
			    DBTypeLongNameTbl[rL->rl_routeType.rt_tileType]);
		    if (TxGetLine(answer, sizeof answer) == NULL || 
			    answer[0] == '\0')
			(void) strcpy(answer,"yes");

		    if(Lookup(answer, actionNames) == 1)
		    {
			/* Yes */
			pickedRL = rL;
		    }
		}
	    }

	    if(pickedRL)
	    {
		l=NULL;
		LIST_ADD(rL,l);
		ListDealloc(activeLayers);
		ListDealloc(presentLayers);
		ListDealloc(presentContacts);
		ListDealloc(presentContactLayers);

		return(l);
	    }

	    /* User didn't pick anything, return null list */
	    {
		ListDealloc(activeLayers);
		ListDealloc(presentLayers);
		ListDealloc(presentContacts);
		ListDealloc(presentContactLayers);

		return(NULL);
	    }

	}
    }
}
