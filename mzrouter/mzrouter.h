/*
 * mzrouter.h --
 *
 * This file defines the interface provided by the maze router
 * module to the rest of Magic.
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
 *
 * rcsid $Header: /usr/cvsroot/magic-8.0/mzrouter/mzrouter.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 */

#ifndef _MZROUTER_H
#define _MZROUTER_H

#include "database/database.h"
#include "utils/geometry.h"
#include "utils/list.h"

/* This module does not support a command level interface: it is accessed
 * by other modules (such as the irouter and the garouter) via procedure
 * calls.
 *
 * NOTE: a wizard command interface (`*mzroute') IS provided for testing.  
 * (see mzTestCmd.c)
 */

/* PARAMETER STRUCTURES */
/*-----------------------------  RouteType ----------------------------------*/

/*
 *  Contains information about a tile type used by the router.
 *
 *  This structure is contained in both RouteLayer and RouteContacts strucs.
 *  It contains information that is relevant to both route layers and layers
 *  used for contacts by the router.
 *
 *  *DERIVED* = member computed by MZInitRoute() prior to each route.
 *
 */

typedef struct 
routetype
{
    /* IDENTIFICATION */
    TileType rt_tileType;	/* "Home" tile type of this layer */

    /* STATUS */
    bool rt_active;		/* Maze router uses type only if this flag
				   set */

    /* DESIGN RULES */
    int rt_width;			/* Width of path or contact */
    int rt_length;			/* Length of contact or minimum */
					/* length of a path to satisfy	*/
					/* minimum area requirements	*/
    int rt_spacing[TT_MAXTYPES + 1]; 	/* spacings to various types 
  				           last entry is SUBCELL spacing */

    /* DERIVATIVE DESIGN RULES - computed from DESIGN RULES above.
     * for contact routeTypes, max design rules for components used, so
     * that a contact will not be placed unless there is space for wires
     * on both connected layers.
     *
     * bloatTop = spacing
     * bloatBot = spacing + width - 1
     */
    int rt_effWidth;			/* *DERIVED* - for contact, 
					 * max of component width */
    int rt_bloatBot[TT_MAXTYPES + 1];   /* *DERIVED* - bloat distance 
					 * to bottom and left */
    int rt_bloatTop[TT_MAXTYPES + 1];   /* *DERIVED* - bloat distance to 
					 * top and right */

    /* BLOCKAGE PLANES */
    Plane *rt_hBlock;	/* Blockage plane for layer organized into maximal
			   horizontal strips */
    Plane *rt_vBlock;	/* Blockage plane for layer organized into maximal
			   vertical strips */

    struct routetype *rt_next;	/* For convenience, all route types are 
				 * threaded
				 * together.  (This threading is in addition
				 * to the routeLayers list and the 
				 * routeContacts list.
				 */
    struct routetype *rt_nextActive;  /* *DERIVED* - This list built in 
				       * MZInitRoute() */
} RouteType;

/*---------------------------  RouteLayer ----------------------------------*/

/*
 *  The RouteLayers list contains one of these structures for each
 *  layer on which routing is permitted.  The structure is a
 *  handle for all information relevant to a route layer.
 *
 *  *DERIVED* = member computed by MZInitRoute() prior to each route.
 *
 */

typedef struct routelayer
{
    /* TYPE */
    RouteType rl_routeType;	/* Contains info. relevant to both
                                    route layers and contact types */

    /* PLANE NUMBER */
    int	rl_planeNum;		/* Plane number of layer */

    /* CONTACTS */
    List *rl_contactL;	/* list of contact types that connect to this layer */

    /* COST */
    int rl_hCost; 	/* cost per unit length for horizontal segments */
    int rl_vCost;	/* cost per unit length for vertical segments */
    int rl_jogCost;	/* cost of a jog */
    int rl_hintCost;	/* cost per unit area for deviation from hint */
    int rl_overCost;	/* cost per unit length for crossing another route layer */

    /* NEXT ROUTE LAYER */
    struct routelayer *rl_next;
    struct routelayer *rl_nextActive;  /* *DERIVED* - Only accurate after 
					* MZInitRoute() */
} RouteLayer;

/*---------------------------- RouteContact ------------------------------*/
/* 
 * This sturcture describes a type of contact to be used during routing.
 * Contacts connect two route layers.
 */

typedef struct routecontact
{
    /* TYPE */
    RouteType rc_routeType;	/* Contains info. relevant to both
                                    route layers and contact types */

    /* LAYERS CONNECTED */
    RouteLayer *rc_rLayer1;	/* Layers connected by this type of contact */
    RouteLayer *rc_rLayer2;

    /* COST */
    int rc_cost;

    /* NEXT ROUTE CONTACT */
    struct routecontact *rc_next;	/* next in RouteContacts list */

} RouteContact;

/* ----------------------------- Paths  ----------------------------------- */

/* 
 * Zero-width path segment structure.  Paths and partial-paths built from
 * these structures during search. 
 *
 * Can be flushed out to paint via MZPaintPath().
 * 
 */
typedef struct rpath
{
    struct rpath	*rp_back;	/* Pointer to previous leg of path */
    RouteLayer		*rp_rLayer;	/* Route layer of this segment */
    int		 	rp_orient;	/* orientation of this segment,
					 * 'H' = hor, 'V' = vert, 'O' = contact
					 * or start point.
					 */
    Point	 	rp_entry;	/* Cost was computed to this point */
    int		 	rp_extendCode;	/* directions to extend */
    dlong   	 	rp_cost;	/* estimated total cost */
    dlong	 	rp_togo;	/* estimated cost to completion */
} RoutePath;

/* extension codes */
#define EC_RIGHT	1
#define EC_LEFT		2
#define EC_UP		4
#define EC_DOWN		8
#define EC_UDCONTACTS	16
#define EC_LRCONTACTS	32
#define EC_ALL		63

#define EC_WALKRIGHT	64
#define EC_WALKLEFT	128
#define EC_WALKUP	256
#define EC_WALKDOWN	512
#define EC_WALKUDCONTACT 1024
#define EC_WALKLRCONTACT 2048

#define EC_COMPLETE	4096

/*----------------------- Soft Floating Point ----------------------------- */
/* (Floating Point Format for our own software-implemented floating-point.
 *  Used for the penalty factor for costs outside the window.) 
 */

/* Note: nExponent must be >=0 
 * 	To multiply dlong by routeFloat, first multiply by mantissa, then
 *	shift right nExponent number of bits.
 */
typedef struct routeFloat
{
    int 	rf_mantissa;
    int 	rf_nExponent;
} RouteFloat;

/*---------------------------- MazeParameters ------------------------------*/

/* 
 * This sturcture contains all maze router parameters, including design rules.
 */
typedef struct mazeparameters
{
    RouteLayer *mp_rLayers;	/* list of route layers */
    RouteContact *mp_rContacts;	/* list of route contacts */
    RouteType *mp_rTypes;	/* list of all route types */

    RouteFloat mp_penalty; 	/* Penalty for lagging behind window */
    /* BY NP---changed from DoubleInt to dlong */
    dlong mp_wWidth;		/* Window width */
    dlong mp_wRate;		/* Rate of motion for window */
    dlong mp_bloomDeltaCost;	/* Max increment. in cost while blooming */
    int mp_boundsIncrement;	/* min radius of blockage info required
				 * around point being extended - twice the
				 * increment is generated whenever gen. is
				 * necessary.
				 */
    bool mp_estimate;		/* If set, nontrivial estimation of cost
				 * to completion is used - factoring in
				 * blocks due to subcells and fences.
				 */

    bool mp_expandEndpoints;	/* If set, routes may start or terminate anywhere
				 * that is electrically connected
				 * to specified start or dest regions.
				 */

    bool mp_topHintsOnly;	/* If set, only hints in the top cell presented
				 * to the router are recognized - used by
				 * garouter to speed up processing.
				 */

    int mp_maxWalkLength;	/* max distance into blocked area route
				 * will extend in order to connect to
				 * a destination terminal.  If set to -1,
				 * max distance is computed as a function
				 * of design rules for active layers prior
				 * to each route.
				 */

    Rect *mp_boundsHint;        /* If nonnull, improves perfomrnace by limiting
				 * estimation, bounds generation etc to this
				 * area.
				 * NOTE: IF SET IT IS THE USERS RESPONSIBILITY
				 *       TO CONTAIN THE ROUTE WITHIN THIS AREA
				 *       VIA FENCES - ELSE BIZARRE BEHAVIOUR 
				 *       IS POSSIBLE.
				 */

    int mp_verbosity;		/* amount of messages printed:
				 *    0 = errors and warnings only,
				 *    1 = brief 
				 *    2 = lots of statistics.
				 */

    int mp_bloomLimit;		/* if positive, puts upper limit on number of
				 * blooms in maze search before router
				 * terminates.
				 * 
				 * If negative or 0, no limit is imposed.
				 */
} MazeParameters;
#define VERB_WARNONLY	0 
#define VERB_BRIEF	1
#define	VERB_STATS	2

/* Return codes for MZRoute() */

#define MZ_NO_ACTION	       -1	/* Never ran MZRoute() */
#define MZ_SUCCESS		0	/* Successful route */
#define MZ_CURRENT_BEST		1	/* Interrupted, returned best choice so far */
#define MZ_ALREADY_ROUTED	2	/* Start node = Dest node already */
#define MZ_FAILURE	 	3	/* Route failed for some reason */
#define MZ_UNROUTABLE		4	/* Failed to generate a walk to the dest node */
#define MZ_INTERRUPTED		5	/* Interrupted, no route was found */

/* INTERFACE PROCEDURES */
/* Call sequence for routing:
 *	  1. MZInitRoute()
 *	  2. MZAddStart()'s and MZAddDest()'s
 *	  3. MZRoute()
 *	  4. MZPaintPath()
 *	  5. MZClean()
 *
 * NOTE: IF THE SEQUENCE IS ABORTED PART WAY THROUGH BE SURE AND CALL MZClean()
 *       PRIOR TO RETURNING TO MAGIC COMMAND PROCESSOR.  THIS IS NECESSARY TO
 *	 RESTORE TILE CLIENTDATA TO INITIAL STATE.
 */
extern MazeParameters *MZFindStyle();	/* return parms of given style */
extern MazeParameters *MZCopyParms();	/* Create new MazeParameters */

extern void MZInitRoute();	/* Initialize route */
extern void MZAddStart();       /* After MzInitRoute, to add start points */
extern void MZAddDest();	/* After MZInitRoute, to add dest area */
extern RoutePath *MZRoute();	/* After MZAddStart, and MZAddDest 
				 * to do search */
extern CellUse *MZPaintPath();	/* Turns path into actual paint */
extern void MZClean();		/* Reclaim storage */
extern void MZTest();		/* Wizard command interface (`*mzroute') */
extern RouteType *MZFindRouteType();
extern RouteLayer *MZFindRouteLayer();
extern RouteContact *MZGetContact();
extern RouteContact *MZRouteContact();
extern void MZPrintRLs();	/* Allows clients to dump maze parms */
extern void MZPrintRCs();
extern void MZPrintRLListNames();
extern void MZPrintRCListNames();
extern void MZFreeParameters(MazeParameters *);

extern void MZInit();
extern void MZAfterTech();

/* TECHNOLOGY FILE PROCESSING PROCEDURES */
    /* "mzrouter" section */
extern void MZTechInit();
extern bool MZTechLine();
extern void MZTechFinal();

    /* plane scaling pointer recovery */

extern void MZAttachHintPlanes();

/* EXPORTED VARIABLES */

/* Major cost unit */
extern int mzMajorCostUnit;

#endif /* _MZROUTER_H */
