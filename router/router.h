/*
 * router.h --
 *
 * This file defines the interface provided by the router module,
 * which is the top-level module that controls routing.
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
 *
 *
 * rcsid="$Header: /usr/cvsroot/magic-8.0/router/router.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $"
 */

#ifndef _ROUTER_H
#define _ROUTER_H

#include "database/database.h"
#include "utils/geometry.h"

/* Masks of directions */
#define	DIRTOMASK(d)		(1 << (d))
#define	DIRMASKHASDIR(m, d)	((m) & DIRTOMASK(d))
#define	DIR_EAST	DIRTOMASK(GEO_EAST)
#define	DIR_WEST	DIRTOMASK(GEO_WEST)
#define	DIR_NORTH	DIRTOMASK(GEO_NORTH)
#define	DIR_SOUTH	DIRTOMASK(GEO_SOUTH)

/*
 * The following describes the structure used during stem generation.
 * It represents one side of a group of one or more cells; we are
 * considering terminals coming out of this side into nearby channels.
 */
typedef struct side
{
    int		 side_side;	/* GEO_NORTH, etc: space lies to this side */
    Transform	 side_trans;	/* Transform from coords of this side back
				 * to original def coordinates if this Side
				 * was transformed.
				 */

    /* The following are in transformed space */
    Rect	 side_line;	/* Coordinates of the boundary: this is either
				 * a zero-width or a zero-height rectangle.
				 */
    Rect	 side_search;	/* Search this area to find all cells that
				 * belong to this Side.
				 */
    Rect	 side_used;	/* Area reserved for stems; channels cannot
				 * be created over this area.  At a minimum
				 * this extends to the next grid point outside
				 * a cell that is far enough away to ensure
				 * that no design-rule violations are possible
				 * with material in the inside of the cell.
				 */
    struct side	*side_next;	/* Next Side in a list of these structs */
} Side;

/*
 * The following information describes the two layers used for routing.
 * The terms "metal" and "poly" are used for "layer1" and "layer2"
 * respectively.  They may not actually end up being metal and poly
 * (in two-layer metal processes, "poly" will actually be routed in
 * the second metal layer).  "Metal" is considered to be the preferred
 * layer.
 */
extern TileType RtrMetalType;		/* Tile type to paint for "metal" */
extern TileType RtrPolyType;		/* Tile type to paint for "poly" */
/* extern int RtrMetalWidth; */		/* Widths of wires on each layer */
/* extern int RtrPolyWidth; */		/* (see dbwind/dbwind.h)	 */

/* Contacts to connect the two routing layers */
extern TileType RtrContactType;		/* Tile type to use for contacts */
/* extern int RtrContactWidth; */	/* Size of contacts (square)	*/
					/* (see dbwind/dbwind.h)	*/
extern int RtrContactOffset;		/* Distance between grid line and
					 * bottom of contact.
					 */
extern int RtrMetalSurround;		/* After painting a contact, paint
					 * additional metal for this distance
					 * around contact.
					 */
extern int RtrPolySurround;		/* After painting a contact, paint
					 * additional poly for this distance
					 * around contact.
					 */

/* Grid spacing for routing */
extern int RtrGridSpacing;

/*
 * The following stuff describes how far separated the routing must
 * be from other material, including both paint and subcells.
 */
extern int RtrSubcellSepUp;		/* This is the closest that a routing
					 * grid line may be above a subcell.
					 */
extern int RtrSubcellSepDown;		/* The closest a grid line may be
					 * below a subcell.
					 */
extern TileTypeBitMask RtrMetalObstacles;/* Paint layers that the "metal"
					 * routing layer cannot run across.
					 */
extern TileTypeBitMask RtrPolyObstacles;/* Same for "poly" layer. */
extern int RtrPaintSepsUp[];		/* Array giving, for each tile type,
					 * how far above material of that
					 * type the nearest useable routing
					 * grid line is.
					 */
extern int RtrPaintSepsDown[];		/* Array telling how far below paint
					 * the nearest useable routing grid
					 * line is.
					 */
extern int RtrMetalSeps[];		/* Array giving, for each tile type,
					 * how away from material of that
					 * type the nearest metal can be.
					 */
extern int RtrPolySeps[];		/* Array telling how far away from poly
					 * each type of material can be.
					 */

/* Used for creating over-cell channels */
extern HashTable rtrChannelSplitTbl;

/* Private procedures */
int rtrMakeChannel();

/* Globals shared by the various pieces of routing code */
extern int RtrViaLimit;
extern bool RtrDoMMax;
extern bool RtrMazeStems;
extern Rect RouteArea;
extern Plane * RtrChannelPlane;
extern HashTable RtrTileToChannel;
extern float RtrEndConst;
extern struct chan *RtrChannelList;	/* Use (struct chan *) to allow this
					 * file to before after gcr.h
					 */
/*
 * The origin point for the routing grid: may not
 * necessarily be (0,0).
 */
extern Point RtrOrigin;

/*
 * The following macros are used to locate the next higher or
 * lower grid line from a given point.  The 'o' parameter is
 * the coordinate of the origin.
 */
#define RTR_GRIDUP(x, o) ( \
		( ((x)-(o)) % RtrGridSpacing ) \
			? ( (x) + (((x) > (o)) ? RtrGridSpacing : 0) \
			        - ( ((x)-(o)) % RtrGridSpacing ) ) \
			: (x))
#define RTR_GRIDDOWN(x, o) ( \
		( ((x)-(o)) % RtrGridSpacing) \
			? ((x) - (((x) > (o)) ? 0 : RtrGridSpacing) \
			       - ( ((x)-(o)) % RtrGridSpacing)) \
			: (x))



/* Determine whether or not a tile corresponds to empty space */
#define RtrIsSpaceTile(tile) (TiGetBody(tile) == (ClientData) NULL)

/* Technology file reading */
extern void RtrTechInit(), RtrTechFinal();
extern bool RtrTechLine();

/* Overall procedure to do routing */
extern void Route();

/* Painting and locating stems */
extern Side *rtrAssignStems();
extern void RtrPaintAllStems();
extern bool RtrPaintStem();
extern void RtrFBPaint();
extern void RtrFBSwitch();
extern void rtrFBAdd();
extern void RtrPaintContact();
extern void RtrPaintStats();
extern CellDef *RtrFindChannelDef();
extern struct pin *RtrPointToPin();

#endif /* _ROUTER_H */
