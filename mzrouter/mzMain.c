/*
 * mzMain.c --
 *
 * Global Data Definitions and interface procedures for the Maze Router.
 * 
 * OTHER ENTRY POINTS (not in this file):
 *    Technology readin - mzTech.c
 *    Initialization (after tech readin) - mzInit.c  
 *    Test command interface - TestCmd.c
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/mzrouter/mzMain.c,v 1.3 2010/06/24 12:37:19 tim Exp $";
#endif  /* not lint */

/*--- includes --- */
#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "drc/drc.h"
#include "select/select.h"
#include "utils/signals.h"
#include "textio/textio.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/styles.h"
#include "debug/debug.h"
#include "utils/undo.h"
#include "textio/txcommands.h"
#include "utils/malloc.h"
#include "utils/main.h"
#include "utils/geofast.h"
#include "../utils/list.h"
#include "utils/heap.h"
#include "utils/touchingtypes.h"
#include "mzrouter/mzrouter.h"
#include "mzrouter/mzInternal.h"

/*---- Global Data Definitions ----*/
/* (Actual storage for static global structures for maze router defined here)*/

/* Debug flags */
ClientData mzDebugID;
int mzDebMaze;		/* identify flags to debug module */
int mzDebStep;

/* parameter sets - from tech file */
MazeStyle *mzStyles = NULL;

/* Toplevel cell visible to the router */
CellUse *mzRouteUse;	

/* Route types */
/* (Specifies what types are permitted during routing, and related design
 * rules.)
 */
RouteType *mzRouteTypes;
RouteType *mzActiveRTs;		/* Only route types that are turned on */
RouteLayer *mzRouteLayers;
RouteLayer *mzActiveRLs;	/* Only route layers that are turned on */
RouteContact *mzRouteContacts;

/* Routing is confined to this rectangle */
Rect mzBoundingRect;

/* Expansion mask - defines which subcells to treat as expanded */
int mzCellExpansionMask;

/* If reset, degenerate estimation plane used (just 4 tiles - one for each 
 * quadrant with respect to destination point).
 */
int mzEstimate;

/* If set dest areas are expanded to include all electrically 
 * connected geometry.
 */
int mzExpandEndpoints;
/* If set only hints in toplevel cell are recognized */
int mzTopHintsOnly;

/* Maximum distance route will extend into blocked area to connect to dest.
 * terminal.  If set to -1, a max value is computed as a function of the
 * design rules for the active route types prior to each route.
 */
int mzMaxWalkLength;

/* if nonnull, limits area of search for performance.  
 * (NOTE: USER MUST LIMIT ROUTE TO THIS AREA WITH FENCES - OTHERWISE
 *        RESULT IS UNPREDICTABLE).
 */
Rect *mzBoundsHint;

/* how many messages to print */
int mzVerbosity;
/* if positive, upper bound on number of blooms */
int mzBloomLimit;

/* maskdata unexpanded subcells, marked because they are part of 
 * dest. nodes. */
List *mzMarkedCellsList;

/* start terminals */
List *mzStartTerms;

/* internal cell for dest areas */
CellDef *mzDestAreasDef = (CellDef *) NULL;
CellUse *mzDestAreasUse = (CellUse *) NULL;

/* Fence parity */
bool mzInsideFence;

/* largest design rule distance - used during incremental blockage gen. */
int mzContextRadius; 

/* Internal cell for completed route */
CellDef *mzResultDef = (CellDef *) NULL;
CellUse *mzResultUse = (CellUse *) NULL;

/* HINT PLANES */
TileTypeBitMask mzHintTypesMask;
Plane *mzHHintPlane;
Plane *mzVHintPlane;

/* FENCE PLANE */
TileTypeBitMask mzFenceTypesMask;
Plane *mzHFencePlane;

/* ROTATE PLANES */
TileTypeBitMask mzRotateTypesMask;
Plane *mzHRotatePlane;
Plane *mzVRotatePlane;

/* BOUNDS PLANES */
PaintResultType mzBoundsPaintTbl[TT_MAXROUTETYPES][TT_MAXROUTETYPES];
Plane *mzHBoundsPlane;
Plane *mzVBoundsPlane;

/* BLOCKAGE PLANES */
TileTypeBitMask mzStartTypesMask;
PaintResultType mzBlockPaintTbl[TT_MAXROUTETYPES][TT_MAXROUTETYPES];

/* ESTIMATE PLANE */
PaintResultType mzEstimatePaintTbl[TT_MAXROUTETYPES][TT_MAXROUTETYPES];
Plane *mzEstimatePlane;

/* dest terminal alignment coordinates */
NumberLine mzXAlignNL;
NumberLine mzYAlignNL;

/* minimum radius of blockage plane info required around point being expanded.
 * (Areas twice this size are gened. whenever the minimum is not met.)
 */
int mzBoundsIncrement;

/* minimum estimated total cost for initial path */
dlong mzMinInitialCost;

/* where current path came from */
int mzPathSource;

/* Parameters controlling search */
RouteFloat mzPenalty;
dlong mzWRate;
dlong mzBloomDeltaCost;
dlong mzWWidth;

/* Statistics */
dlong mzInitialEstimate;  /* Initial estimated cost of route */
int mzNumBlooms;
int mzNumOutsideBlooms;	/* num blooms from outside window */
int mzNumComplete;	/* number of complete paths so far */
int mzBlockGenCalls;	/* # of calls to blockage gen. code */
double mzBlockGenArea;   /* area over which blockage planes 
			    have been gened. */
int mzNumPathsGened;	/* number of partial paths added to heap */
int mzNumPaths;		/* number of paths processed */
int mzReportInterval;    /* frequency that # of paths etc. 
				 * is reported. */
int mzPathsTilReport;	/* counts down to next path report */

/* Variables controlling search */
dlong mzWInitialMinToGo;
dlong mzWInitialMaxToGo;
dlong mzBloomMaxCost;	

/* Search status */
dlong mzWindowMinToGo; /* Window location */
dlong mzWindowMaxToGo;

/* Hash table to avoid repeated expansion from same point during search */
HashTable mzPointHash;

/* Queues for partial paths */
Heap mzMaxToGoHeap;		/* paths nearer destination than WINDOW */
Heap mzMinCostHeap;		/* paths in WINDOW */
Heap mzMinAdjCostHeap;	 	/* paths farther from dest than WINDOW*/
List *mzBloomStack;		/* paths in current local focus */
List *mzStraightStack;		/* focus paths expanded in a straight line */
List *mzDownHillStack;		/* focus paths expanded as long as
				 * estimated total cost doesn't increase.
				 */
List *mzWalkStack;		/* paths in walks, i.e. blocks adjacent
				 * to dest areas.
				 */
Heap mzMinCostCompleteHeap;	/* completed paths */

/*----------- static data - referenced only in this file ------------------- */

/* set when storage that needs to be reclaimed has been allocated by router */
bool mzDirty = FALSE;

/* set when path queues and hast table have been allocated */
bool mzPathsDirty = FALSE;

/* macro for adding address pairs to translation table */
#define ADDR_TBL_EQUIV(a1,a2) \
if(TRUE) \
{ \
   HashSetValue(HashFind(&aT, (char *) (a1)), (char *) (a2)); \
   HashSetValue(HashFind(&aT, (char *) (a2)), (char *) (a1)); \
} else

/* macro for translating address to address paired with it in address table */
#define ADDR_TBL(type,a) \
if (TRUE) \
{ \
  HashEntry *he = HashLookOnly(&aT, (char *) (a)); \
  if(he) \
  { \
      a = (type) HashGetValue(he); \
  } \
} else


/*
 * ----------------------------------------------------------------------------
 *
 * MZCopyParms --
 *
 * Allocate and setup duplicate maze parameters with same values.
 * (Duplicates substructures as well)
 *
 * Results:
 *	Pointer to new parameters.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

MazeParameters *
MZCopyParms(oldParms)
    MazeParameters *oldParms;	/* Maze routing parameters */
{    
    MazeParameters *newParms;
    HashTable aT;	/* Address translation hash table */

    /* If passed NULL parms, just return NULL */
    if(oldParms==NULL)
    {
	return NULL;
    }
	
    /* Initialize address translation table */
    HashInit(&aT, 1000, HT_WORDKEYS);

    /* allocate new structure and copy MazeParameters */
    {
	newParms = (MazeParameters *) mallocMagic((unsigned)(sizeof(MazeParameters)));
	*newParms = *oldParms;
    }

    /* Copy RouteLayers (and sub-structures) */
    {
	RouteLayer *rLOld;

	for(rLOld = oldParms->mp_rLayers; 
	    rLOld != NULL; 
	    rLOld = rLOld->rl_next)
	{
	    RouteLayer *rLNew;
	    
	    /* allocate and equivalence new rL and its rT */
	    {
		rLNew = (RouteLayer *) mallocMagic((unsigned)(sizeof(RouteLayer)));
		ADDR_TBL_EQUIV(rLOld, rLNew);
		ADDR_TBL_EQUIV(&(rLOld->rl_routeType),&(rLNew->rl_routeType));
	    }

	    /* copy the rL */
	    *rLNew = *rLOld;

	    /* make a copy of the routeLayer contact list */
	    LIST_COPY(rLOld->rl_contactL, rLNew->rl_contactL);

	    /* allocate new blockage planes */
	    rLNew->rl_routeType.rt_hBlock = DBNewPlane((ClientData) TT_SPACE);
	    rLNew->rl_routeType.rt_vBlock = DBNewPlane((ClientData) TT_SPACE);
	}
    }

    /* Copy RouteContacts (and sub-structures) */
    {
	RouteContact *rCOld;

	for(rCOld = oldParms->mp_rContacts; 
	    rCOld != NULL; 
	    rCOld = rCOld->rc_next)
	{
	    RouteContact *rCNew;
	    
	    /* allocate and equivalence new rC and its rT */
	    {
		rCNew = (RouteContact *) mallocMagic((unsigned)(sizeof(RouteContact)));
		ADDR_TBL_EQUIV(rCOld, rCNew);
		ADDR_TBL_EQUIV(&(rCOld->rc_routeType),&(rCNew->rc_routeType));
	    }

	    /* copy the rC */
	    *rCNew = *rCOld;

	    rCNew->rc_routeType.rt_hBlock = DBNewPlane((ClientData) TT_SPACE);
	    rCNew->rc_routeType.rt_vBlock = DBNewPlane((ClientData) TT_SPACE);
	}
    }

    /* Translate addresses in MazeParameters */
    ADDR_TBL(RouteLayer *, newParms->mp_rLayers);
    ADDR_TBL(RouteContact *, newParms->mp_rContacts);
    ADDR_TBL(RouteType *, newParms->mp_rTypes);

    /* Translate addresses in RouteLayers (and sub-structures) */
    {
	RouteLayer *rLOld;

	for(rLOld = oldParms->mp_rLayers; 
	    rLOld != NULL; 
	    rLOld = rLOld->rl_next)
	{
	    RouteLayer *rLNew = rLOld;
	    ADDR_TBL(RouteLayer *, rLNew);

	    ADDR_TBL(RouteLayer *, rLNew->rl_next);
	    ADDR_TBL(RouteType *, rLNew->rl_routeType.rt_next);
	    
	    /* translate RouteContact addresses in contact list */
	    {
		List *l;
		for(l = rLNew->rl_contactL; l!=NULL; l=LIST_TAIL(l))
		{
		    ADDR_TBL(ClientData, LIST_FIRST(l));
		}
	    }

	}
    }

    /* Translate addresses in RouteContacts (and sub-structures) */
    {
	RouteContact *rCOld;

	for(rCOld = oldParms->mp_rContacts; 
	    rCOld != NULL; 
	    rCOld = rCOld->rc_next)
	{
	    RouteContact *rCNew =rCOld;
	    ADDR_TBL(RouteContact *, rCNew);

	    ADDR_TBL(RouteLayer *, rCNew->rc_rLayer1);
	    ADDR_TBL(RouteLayer *, rCNew->rc_rLayer2);
	    ADDR_TBL(RouteContact *, rCNew->rc_next);
	    ADDR_TBL(RouteType *, rCNew->rc_routeType.rt_next);
	}
    }
	    
    HashKill(&aT);
    return newParms;
}


/*
 * ----------------------------------------------------------------------------
 *
 * MZFindStyle --
 *
 * Find style of given name.
 *
 * Results:
 *	Pointer to maze parameters for given style, or NULL if not found.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

MazeParameters *
MZFindStyle(name)
char *name;	/* name of style we are looking for */
{    
    MazeStyle *style = mzStyles;
    
    while(style!=NULL && strcmp(name,style->ms_name)!=0)
    {
	style = style->ms_next;
    }

    if(style==NULL)
    {
	return NULL;
    }
    else
    {
	return &(style->ms_parms);
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * MZInitRoute --
 *
 *
 * Set up datastructures for maze route, and initialize per/route statistics
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See above.
 *
 * NOTE: RouteUse supplied as parm to MZInitRoute is toplevel cell visible
 * to router.  However resulting route is painted to current edit cell. 
 *
 * ----------------------------------------------------------------------------
 */

void
MZInitRoute(parms, routeUse, expansionMask)
    MazeParameters *parms;	/* Maze routing parameters */
    CellUse *routeUse;		/* toplevel cell router sees */
    int expansionMask;		/* which subcells are expanded - NOTE: the
				 * maze router interpets a 0 mask to mean
				 * all cells are expanded
				 */
{    
    /* Disable undo to avoid overhead on paint operations to internal planes */
    UndoDisable();

    /* Clean up after last route - if necessary */
    if(mzDirty)
    {
	MZClean();
    }

    /* Set dirty flag - since we are about to alloc storage for this route */
    mzDirty = TRUE;

    /* initial source of paths is initialization routine */
    mzPathSource = SOURCE_INIT;

    /* initial estimated cost is infinity */
    mzMinInitialCost = COST_MAX;

    /* initialize per-route statistics */
    mzBlockGenCalls = 0;
    mzBlockGenArea = 0.0;
    mzNumComplete = 0;
    mzNumPathsGened = 0;
    mzNumPaths = 0;
    mzNumBlooms = 0;
    mzNumOutsideBlooms = 0;
    mzPathsTilReport = mzReportInterval;

    /* Make supplied parms current */

    mzRouteLayers = parms->mp_rLayers;
    mzRouteContacts = parms->mp_rContacts;
    mzRouteTypes = parms->mp_rTypes;

    mzPenalty = parms->mp_penalty;
    mzWWidth = parms->mp_wWidth;
    mzWRate = parms->mp_wRate;
    mzBloomDeltaCost = parms->mp_bloomDeltaCost;

    mzBoundsIncrement = parms->mp_boundsIncrement;
    mzEstimate = parms->mp_estimate;
    mzExpandEndpoints = parms->mp_expandEndpoints;
    mzTopHintsOnly = parms->mp_topHintsOnly;

    mzMaxWalkLength = parms->mp_maxWalkLength;
    mzBoundsHint = parms->mp_boundsHint;
    mzVerbosity = parms->mp_verbosity;
    mzBloomLimit = parms->mp_bloomLimit;

    /* Some parms are computed from the supplied ones */
    mzComputeDerivedParms();

    /* set route cell (toplevel cell visible during routing */
    mzRouteUse = routeUse;

    /* set expansion mask */
    mzCellExpansionMask = expansionMask; 

    /* Build hint fence and rotate planes */
    mzBuildHFR(mzRouteUse, &mzBoundingRect);

    /* Initialize Blockage Planes */
    {
	RouteType *rT;

	/* Clear bounds planes = regions for which blockage 
           has been generated */
	DBClearPaintPlane(mzHBoundsPlane);
	DBClearPaintPlane(mzVBoundsPlane);

	/* Clear blockage planes */
	for (rT=mzRouteTypes; rT!=NULL; rT=rT->rt_next)
	{
	    DBClearPaintPlane(rT->rt_hBlock);
	    DBClearPaintPlane(rT->rt_vBlock);
	}
    }

    /* Initialize Dest Area Cell */
    DBCellClearDef(mzDestAreasUse->cu_def);
    /* take our hold off undo */
    UndoEnable();

    return;
}

/*
 * ----------------------------------------------------------------------------
 *
 * MZAddStart --
 *
 * Add a starting terminal for the maze router.
 * 
 * Results:
 *	None.
 *
 * Side effects:
 *	Builds mzStartTerms list.
 *
 * ----------------------------------------------------------------------------
 */

void
MZAddStart(point, type)
    Point *point;
    TileType type;
{
    /* Disable undo to avoid overhead on paint operations to internal planes */
    UndoDisable();

    /* check fence parity */
    if(mzStartTerms == NULL)
    {
	/* This is first start terminal, set fence parity by it, i.e.
	 * whether route is inside or outside of fence
	 */
	Tile *tFencePlane = TiSrPointNoHint(mzHFencePlane, point);
	mzInsideFence = (TiGetType(tFencePlane) != TT_SPACE);

	/* If inside fence, clip mzBounds to fence bounding box
	 * to save processing.
	 */
	if(mzInsideFence)
	{
	    Rect r;

	    DBBoundPlane(mzHFencePlane, &r);
	    r.r_xbot -= 2*mzContextRadius;
	    r.r_ybot -= 2*mzContextRadius;
	    r.r_xtop += 2*mzContextRadius;
	    r.r_ytop += 2*mzContextRadius;
	    GEOCLIP(&mzBoundingRect, &r);
	}
    }
    else
    {
	/* not first start terminal, check for consistency with respect
	 * to fence parity.
	 */
	Tile *tFencePlane = TiSrPointNoHint(mzHFencePlane, point);
	int newInside = (TiGetType(tFencePlane) != TT_SPACE);

	if(newInside != mzInsideFence)
	{
	    TxPrintf("Start points on both sides of fence.  ");
	    TxPrintf("Arbitrarily choosing those %s fence.\n",
		     (mzInsideFence ? "inside" : "outside"));

	    return;
	}
    }

    /* Mark tiles connected to start point */

    /* Comment added by Tim  8/5/06 */
    /* TO DO:  If mzExpandEndpoints is FALSE, mzMarkConnectedTiles	*/
    /* should still add all tiles immediately under the point to the	*/
    /* start list.							*/

    {
	Rect rect;

	/* build degenerate rect containing point to initiate the
	 * marking process;
	 */
	rect.r_ll = *point;
	rect.r_ur = *point;

	mzMarkConnectedTiles(&rect, type, (mzExpandEndpoints) ?
			MZ_EXPAND_START : MZ_EXPAND_NONE);
    }

    /* Take our hold off undo */
    UndoEnable();

    /* and return */
    return;
}

/*
 * ----------------------------------------------------------------------------
 *
 * MZAddDest --
 *
 * Add a destination terminal.  
 * 
 * Results:
 *	none.
 *
 * Side effects:
 *      Paints dest area into mzDestAreasDef.
 *
 *	Marks mask data tiles connected to supplied dest area (rect and type
 *      passed to this func), also keeps list of marked tiles for cleanup. 
 *
 *      Tiles are marked with TRUE on the clientdata field.  The default 
 *      clientdata value of CLIENTDEFAULT should be restored by the router 
 *      before it returns.
 *
 * ----------------------------------------------------------------------------
 */

void
MZAddDest(rect, type)
    Rect *rect;
    TileType type;
{
    ColoredRect *dTerm;

    UndoDisable();

    /* If we're not marking all connected tiles, we need to paint */
    /* this specific rectangle into the mzDestAreasUse cell.	  */

    if (!mzExpandEndpoints)
    {
	RouteLayer *rL;

	for(rL = mzRouteLayers; rL != NULL; rL = rL->rl_next)
	{
	    if (rL->rl_routeType.rt_active &&
			TTMaskHasType(&(DBConnectTbl[type]),
			rL->rl_routeType.rt_tileType))
		DBPaint(mzDestAreasUse->cu_def, rect,
				rL->rl_routeType.rt_tileType);
        }
    }

    /* Mark all tiles connected to dest terminal and paint them into	*/
    /* the mzDestAreasUse cell.						*/

    mzMarkConnectedTiles(rect, type,
			 (mzExpandEndpoints) ?  MZ_EXPAND_DEST : MZ_EXPAND_NONE);

    UndoEnable();
    return;
}

/*
 * ----------------------------------------------------------------------------
 *
 * MZRoute --
 *
 * Do the route.
 * 
 * Results:
 *	Zero-width route path.  NOTE: route path is allocated from temporary
 *      storage that will be reused for next route.
 *
 * Side effects:
 *	
 *
 * ----------------------------------------------------------------------------
 */

RoutePath *
MZRoute(mzResult)
    int *mzResult;	/* Place to put result code */
{
    RoutePath *path;	/* handle for result of search */
    ColoredRect *term;
    List *terms;

    /* Disable undo to avoid overhead on paint operations to internal planes */
    UndoDisable();

    /* Clear result cell */
    DBCellClearDef(mzResultDef);

    /* 1st pass over start terminals:			*/
    /* paint TT_SAMENODE on each start terminal		*/

    for(terms = mzStartTerms; terms != NULL; terms = LIST_TAIL(terms))
    {
	term = (ColoredRect *) LIST_FIRST(terms);
	mzPaintBlockType(&term->cr_rect, term->cr_type, &mzBoundingRect,
			TT_SAMENODE);
    }

    /* Generate dest areas and walks in blockage planes.
     * (also adds alignment coords for dest areas to alignment structs.)
     */
    mzBuildDestAreaBlocks();

    /* Check that there is an unblocked destination */
    if (mzXAlignNL.nl_sizeUsed == 2)
    {
	/* No alignment marks, so no destination areas */
	TxPrintf("No reachable destination area!\n");
	if (mzResult) *mzResult = MZ_UNROUTABLE;
	goto abort;
    }

    /* Build Estimate Plane. 
     * (allowing for end points in unexpanded subcells)
     */
    mzBuildEstimate();
    if (SigInterruptPending)
    {
	if (mzResult) *mzResult = MZ_INTERRUPTED;
	goto abort;
    }

    /* allocating queues and hashtable so set dirty flag */
    mzPathsDirty = TRUE;

    /*
     * Initialize queues (actually heaps and lists) for partial paths
     * Double Precision Integer keys used in cost keyed heaps
     * to avoid overflow.
     */
    HeapInitType(&mzMaxToGoHeap, INITHEAPSIZE, TRUE, FALSE, HE_DLONG);
    HeapInitType(&mzMinCostHeap, INITHEAPSIZE, FALSE, FALSE, HE_DLONG);
    HeapInitType(&mzMinAdjCostHeap, INITHEAPSIZE, FALSE, FALSE, HE_DLONG);
    HeapInitType(&mzMinCostCompleteHeap, INITHEAPSIZE, FALSE, FALSE, HE_DLONG);
    mzBloomStack = NULL;
    mzStraightStack = NULL;
    mzDownHillStack = NULL;
    mzWalkStack = NULL;

    /*
     * A hash table is used to hold all points reached,
     * so we can avoid redundant expansion.
     */
    HashInit(&mzPointHash, INITHASHSIZE, HashSize(sizeof (PointKey)));

    /* Build blockage planes at start points and create initial
     * partial paths 
     */

    /* set bloom threshold to zero, so that initial points are placed
     * on Max ToGo heap.
     */
    mzBloomMaxCost = 0;

    /* 2nd pass over start terminals:  generate initial paths	*/
    /* for each start point					*/

    for(terms = mzStartTerms; terms != NULL; terms = LIST_TAIL(terms))
    {
	term = (ColoredRect *) LIST_FIRST(terms);
	mzExtendBlockBounds(&(term->cr_rect.r_ll));
	
	if (mzStart(term) == FALSE)
	{
	    if (mzResult) *mzResult = MZ_ALREADY_ROUTED;
	    goto abort;
	}
    }

    /* initialize search window */
    /* estimated total cost is min estimated cost for initial paths */

    mzInitialEstimate = mzMinInitialCost;

    mzWInitialMinToGo = mzInitialEstimate;
    mzWInitialMaxToGo = mzWInitialMinToGo + mzWWidth;

    /* Make sure we got here without interruption */
    if (SigInterruptPending) goto abort;

    /* Do the route */
    path = mzSearch(mzResult);
			/* On interruption mzSearch returns best complete path
			 * found prior to interruption 
			 */

    UndoEnable();
    return path;

abort:
    UndoEnable();
    return NULL;
}

/*
 * ----------------------------------------------------------------------------
 *
 * MZCleanupPath --
 *
 * Given a RoutePath constructed by mzRoute, check for conditions that
 * result in DRC errors, and correct them.  Conditions checked and
 * the method to fix are as follows:
 *
 * 1) contact1->(any)->contact2 where contacts are the same type and
 *    would overlap to produce a non-rectangular area.
 *    FIX:  replace second contact with its residues.
 *
 * 2) contact1->(any)->contact2 where contacts are the same type and
 *    would be spaced closer than the allowed contact->contact minimum
 *    DRC space.
 *    FIX:  fill the area between the two contacts with the contact
 *    residue types.
 *
 * 3a) route1-(bend)->route2->contact where length(route2) is less than
 *    the spacing rule type(route2)->type(contact).
 *    FIX:  pull route2 segment toward the inside corner of the bend
 *	until the route segment edge aligns with the contact edge.
 *
 * 3b) contact->route2-(bend)->route1, the reverse of the above.
 *
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Messes with the path list.
 *
 * ----------------------------------------------------------------------------
 */

void
MZCleanupPath(pathList)
    RoutePath *pathList;
{
    RoutePath *path, *n1path, *n2path, *n3path;
    RoutePath *spath, *cpath, *mpath;
    RouteType *rT;
    RouteContact *rC, *rC1, *rC2;
    TileType ctype, ctype1, ctype2;
    int pathlength, hdist, vdist, cdist1, cdist2;


    /* 1st pass:  Consolidate multiple V or H routes	*/
    for (path = pathList; path != NULL; path = path->rp_back)
    {
	n1path = path->rp_back;
	while (n1path && (((n1path->rp_orient == 'V') && (path->rp_orient == 'V')) ||
		((n1path->rp_orient == 'H') && (path->rp_orient == 'H'))))
	{
	    /* NOTE:  Route paths are allocated by a special procedure;	*/
	    /* DON'T use freeMagic() on them!				*/ 
	    path->rp_back = n1path->rp_back;
	    n1path = path->rp_back;
	}
    }

    /* 2nd pass:  Look for route paths causing DRC errors */
    for (path = pathList; path != NULL; path = path->rp_back)
    {
	/* Pick up the next two path segments, if they exist */
	n1path = path->rp_back;
	n2path = (n1path) ? n1path->rp_back : NULL;

	if (n2path && (n1path->rp_rLayer != n2path->rp_rLayer))
	{
	    /* Search backward until we reach the next contact */
	    for (spath = n2path->rp_back; spath && spath->rp_back;
			spath = spath->rp_back)
	    {
		cpath = spath->rp_back;
		if (spath->rp_rLayer != cpath->rp_rLayer)
		{
		    rC1 = MZGetContact(n1path, n2path);
		    rC2 = MZGetContact(spath, cpath);
		    hdist = abs(n1path->rp_entry.p_x - spath->rp_entry.p_x);
		    vdist = abs(n1path->rp_entry.p_y - spath->rp_entry.p_y);
		    ctype1 = rC1->rc_routeType.rt_tileType;
		    ctype2 = rC2->rc_routeType.rt_tileType;
		    cdist1 = rC1->rc_routeType.rt_width;
		    cdist2 = rC2->rc_routeType.rt_width;

		    /* To-do: split into cases based on ctype1 vs. ctype2 */
		    if ((cpath->rp_rLayer == n1path->rp_rLayer) &&
				(hdist < cdist1 && vdist < cdist1) &&
				(hdist > 0) && (vdist > 0))
		    {
			/* Case 1 */
			TxPrintf("Diagnostic:  Overlapping contacts (%d:%d) at %d %d\n",
				hdist, vdist,
				path->rp_entry.p_x, path->rp_entry.p_y);

			/* Replace orient code of one contact with 'C', */
			/* to be handled by mzPaintContact		*/

			if (n1path->rp_extendCode > EC_ALL
					&& n1path->rp_orient != 'C')
			    spath->rp_orient = 'C';
			else
			    n1path->rp_orient = 'C';
				
			break;
		    }
		    hdist += rC1->rc_routeType.rt_width;
		    vdist += rC1->rc_routeType.rt_width;
		    cdist1 = rC1->rc_routeType.rt_spacing[ctype1];
		    if (hdist < cdist1 && vdist < cdist1 && hdist > 0 && vdist > 0)
		    {
			/* Case 2 */
			TxPrintf("Diagnostic:  Contacts too close (%d:%d) at %d %d\n",
				hdist, vdist,
				n1path->rp_entry.p_x, n1path->rp_entry.p_y);

			/* Replace orient code of route with 'M' if contacts	*/
			/* are the same type, 'N' if they're different.		*/
			/* To be handled by MZPaintPath				*/

			for (mpath = n1path; mpath != spath; mpath = mpath->rp_back)
			    if (mpath->rp_orient != 'O')
			    {
		    		if (cpath->rp_rLayer == n1path->rp_rLayer)
				    mpath->rp_orient = 'M';
				else
				    mpath->rp_orient = 'N';
			    }
			break;
		    }

		    break;	/* Stop searching after 1st contact found */
		}
	    }
	}

	/* Pick up the following path segment, if it exists */
	n3path = (n2path) ? n2path->rp_back : NULL;

	/* Cases 3a and 3b */
	if (n3path != NULL)
	{
	    /* Cases 3a: route1->route2->contact */

	    if (n2path->rp_orient == 'O' &&
		n1path->rp_orient != 'O' &&
		path->rp_orient != 'O' &&
		n1path->rp_orient != path->rp_orient)
	    {
		rT = &(n1path->rp_rLayer->rl_routeType);
		rC = MZGetContact(n2path, n3path);
		ctype = rC->rc_routeType.rt_tileType;
	
		if (n1path->rp_orient == 'V')
		{
		    if (n1path->rp_entry.p_y > n2path->rp_entry.p_y)
		    {
			/* Case 3a.1: route down to contact */
			pathlength = n1path->rp_entry.p_y - n2path->rp_entry.p_y
				- rC->rc_routeType.rt_width;
			if (pathlength > 0 && pathlength < rT->rt_bloatTop[ctype])
			{
			    TxPrintf("Diagnostic:  Path needs fix for type "
					"3a.1 DRC error at (%d, %d) dist %d\n",
					path->rp_entry.p_x, path->rp_entry.p_y,
					pathlength);
			}
		    } 
		    else
		    {
			/* Case 3a.2: route up to contact */
			pathlength = n2path->rp_entry.p_y - n1path->rp_entry.p_y
				- rT->rt_width;
			if (pathlength > 0 && pathlength < rT->rt_bloatTop[ctype])
			{
			    TxPrintf("Diagnostic:  Path needs fix for type "
					"3a.2 DRC error at (%d, %d) dist %d\n",
					path->rp_entry.p_x, path->rp_entry.p_y,
					pathlength);
			}
		    }
		}
		else
		{
		    if (n1path->rp_entry.p_x > n2path->rp_entry.p_x)
		    {
			/* Case 3a.3: route left to contact */
			pathlength = n1path->rp_entry.p_x - n2path->rp_entry.p_x
				- rC->rc_routeType.rt_width;
			if (pathlength > 0 && pathlength < rT->rt_bloatTop[ctype])
			{
			    TxPrintf("Diagnostic:  Path needs fix for type "
					"3a.3 DRC error at (%d, %d) dist %d\n",
					path->rp_entry.p_x, path->rp_entry.p_y,
					pathlength);
			}
		    } 
		    else
		    {
			/* Case 3a.4: route right to contact */
			pathlength = n2path->rp_entry.p_x - n1path->rp_entry.p_x
				- rT->rt_width;
			if (pathlength > 0 && pathlength < rT->rt_bloatTop[ctype])
			{
			    TxPrintf("Diagnostic:  Path needs fix for type "
					"3a.4 DRC error at (%d, %d) dist %d\n",
					path->rp_entry.p_x, path->rp_entry.p_y,
					pathlength);
			}
		    }
		}
	    }

	    /* Cases 3b: contact->route1->route2 */

	    if (n1path->rp_orient == 'O' &&
		n2path->rp_orient != 'O' &&
		n3path->rp_orient != 'O' &&
		n2path->rp_orient != n3path->rp_orient)
	    {
		rT = &(n2path->rp_rLayer->rl_routeType);
		rC = MZGetContact(n1path, path);
		ctype = rC->rc_routeType.rt_tileType;

		if (n2path->rp_orient == 'V')
		{
		    if (n2path->rp_entry.p_y > n1path->rp_entry.p_y)
		    {
			/* Case 3b.1: route down from contact */
			pathlength = n2path->rp_entry.p_y - n1path->rp_entry.p_y
				- rC->rc_routeType.rt_width;
			if (pathlength > 0 && pathlength < rT->rt_bloatTop[ctype])
			{
			    TxPrintf("Diagnostic:  Path needs fix for type "
					"3b.1 DRC error at (%d, %d) dist %d\n",
					path->rp_entry.p_x, path->rp_entry.p_y,
					pathlength);
			}
		    } 
		    else
		    {
			/* Case 3b.2: route up from contact */
			pathlength = n1path->rp_entry.p_y - n2path->rp_entry.p_y
				- rT->rt_width;
			if (pathlength > 0 && pathlength < rT->rt_bloatTop[ctype])
			{
			    TxPrintf("Diagnostic:  Path needs fix for type "
					"3b.2 DRC error at (%d, %d) dist %d\n",
					path->rp_entry.p_x, path->rp_entry.p_y,
					pathlength);
			}
		    }
		}
		else
		{
		    if (n2path->rp_entry.p_x > n1path->rp_entry.p_x)
		    {
			/* Case 3b.3: route left from contact */
			pathlength = n2path->rp_entry.p_x - n1path->rp_entry.p_x
				- rC->rc_routeType.rt_width;
			if (pathlength > 0 && pathlength < rT->rt_bloatTop[ctype])
			{
			    TxPrintf("Diagnostic:  Path needs fix for type "
					"3b.3 DRC error at (%d, %d) dist %d\n",
					path->rp_entry.p_x, path->rp_entry.p_y,
					pathlength);
			}
		    } 
		    else
		    {
			/* Case 3b.4: route right from contact */
			pathlength = n1path->rp_entry.p_x - n2path->rp_entry.p_x
				- rT->rt_width;
			if (pathlength > 0 && pathlength < rT->rt_bloatTop[ctype])
			{
			    TxPrintf("Diagnostic:  Path needs fix for type "
					"3b.4 DRC error at (%d, %d) dist %d\n",
					path->rp_entry.p_x, path->rp_entry.p_y,
					pathlength);
			}
		    }
		}
	    }
	}
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * MZPaintPath --
 *
 * Given a RoutePath constructed by mzRoute, convert it to paint.
 * The input RoutePath specifies a sequence of points completely, so
 * each leg can be painted as we go.
 *
 * Results:
 *	Pointer to result cell containing painted path.
 *
 * Side effects:
 *	Paints into result cell.
 *
 * ----------------------------------------------------------------------------
 */

CellUse *
MZPaintPath(pathList)
    RoutePath *pathList;
{
    RoutePath *path, *prev;
    RouteLayer *last_rL = NULL;
    int cwidth = 0;

    /*
     * First, check the path for common problems causing DRC errors
     */
    MZCleanupPath(pathList);

    /*
     * Each segment of the path contains no bends, so is
     * either horizontal, vertical, or a contact.
     */
    for (path = pathList; 
	 (prev = path->rp_back)!= NULL && !SigInterruptPending; 
	 path = prev)
    {
	RouteLayer *rL;
	Rect r;
	int t;

	/*
	 * Special handling for a contact if different planes.
	 * In this case, no x- or y- motion is allowed.
	 */
	if (path->rp_rLayer != prev->rp_rLayer)
	{
	    ASSERT(path->rp_entry.p_x == prev->rp_entry.p_x, "MZPaintPath");
	    ASSERT(path->rp_entry.p_y == prev->rp_entry.p_y, "MZPaintPath");
	    cwidth = mzPaintContact(path, prev);
	    last_rL = path->rp_rLayer;
	    continue;
	}

	/*
	 * Leg on the same plane.
	 * Generate a box between the start and end points
	 * with the width specified for this layer.
	 * Flip the rectangle as necessary to ensure that
	 * LL <= UR.
	 */
	r.r_ll = path->rp_entry;
	r.r_ur = prev->rp_entry;
	if (r.r_xbot > r.r_xtop)
	    t = r.r_xbot, r.r_xbot = r.r_xtop, r.r_xtop = t;
	if (r.r_ybot > r.r_ytop)
	    t = r.r_ybot, r.r_ybot = r.r_ytop, r.r_ytop = t;
	if (path->rp_orient == 'M' || path->rp_orient == 'N')
	{
	    r.r_xtop += cwidth;
	    r.r_ytop += cwidth;
	}
	else
	{
	    r.r_xtop += path->rp_rLayer->rl_routeType.rt_width;
	    r.r_ytop += path->rp_rLayer->rl_routeType.rt_width;
	}

	rL = path->rp_rLayer;
	DBPaintPlane(mzResultDef->cd_planes[rL->rl_planeNum], &r,
		DBStdPaintTbl(rL->rl_routeType.rt_tileType,
		rL->rl_planeNum), (PaintUndoInfo *) NULL);

	/* Routes between close contacts of the same type should paint	*/
	/* both residue types.						*/

	if ((path->rp_orient == 'M') && (last_rL != NULL))
	{
	    DBPaintPlane(mzResultDef->cd_planes[last_rL->rl_planeNum], &r,
		    DBStdPaintTbl(last_rL->rl_routeType.rt_tileType,
		    last_rL->rl_planeNum), (PaintUndoInfo *) NULL);
	}
    }

    /* Update bounding box of result cell */
    DBReComputeBbox(mzResultDef);

    /* return pointer to result cell use */
    return mzResultUse;

}


/*
 * ----------------------------------------------------------------------------
 *
 * MZClean --
 *
 * Reclaim storage space gobbled up during route, and reset tile client
 * fields.  After a MZInitRoute() has been issued, MZClean() should always
 * be called prior to returning from Magic command.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

void
MZClean()
{
    if(mzDirty)
    {
	/* clear estimate plane */
	mzCleanEstimate();

	/* Reclaim storage and reset mzStartList */
	{
	    ListDeallocC(mzStartTerms);
	    mzStartTerms = NULL;
	}

	/* Reset dest alignment structures */
	mzNLClear(&mzXAlignNL);
	mzNLClear(&mzYAlignNL);

	/* Unmark marked tiles, and cells and dealloc marked lists */
	{
	    List *l;

	    /* Reset Marked subcell client fields to CLIENTDEFAULT */
	    for(l=mzMarkedCellsList; l!=NULL; l=LIST_TAIL(l))
	    {
		CellUse *cu = (CellUse *) LIST_FIRST(l);
		
		/* Restore celluse client field to its "unmarked" value */
		cu->cu_client = (ClientData) CLIENTDEFAULT;
	    }

	    /* Dealloc list of marked cells */
	    ListDealloc(mzMarkedCellsList);
	    mzMarkedCellsList = NULL;
	}

	/* Free up route-path queues */
	if(mzPathsDirty)
	{
	    HeapKill(&mzMaxToGoHeap, (void (*)()) NULL);
	    HeapKill(&mzMinCostHeap, (void (*)()) NULL);
	    HeapKill(&mzMinAdjCostHeap, (void (*)()) NULL);
	    HeapKill(&mzMinCostCompleteHeap, (void (*)()) NULL);
	    ListDealloc(mzBloomStack);
	    ListDealloc(mzStraightStack);
	    ListDealloc(mzDownHillStack);
	    ListDealloc(mzWalkStack);

	    /* Free up hash table */
	    HashKill(&mzPointHash);

	    /* Reclaims route path entries */
	    mzFreeAllRPaths();
	    
	    /* reset flag */
	    mzPathsDirty = FALSE;
	}

	/* reset flag */
	mzDirty = FALSE;
    }

    return;
}
