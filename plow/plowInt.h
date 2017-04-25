/*
 * plowInt.h --
 *
 * Internal definitions for the plow module.
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
 * Needs to include: magic.h, geometry.h, tile.h
 *
 * rcsid $Header: /usr/cvsroot/magic-8.0/plow/plowInt.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 */

#ifndef _PLOWINT_H
#define	_PLOWINT_H

#include "tiles/tile.h"

/*
 * The following is a TileType used by plowing to refer to an
 * edge on the subcell plane.
 */
#define	PLOWTYPE_CELL	(TT_MAXTYPES - 1)

/* --------------------------- Plowing rules -------------------------- */

/*
 * Plowing rules tables.  (These are not the plowing forms of
 * design rules, but rather the procedures that apply those
 * rules).
 *
 * These are initialized by PlowInit after the technology
 * file has been read.  Each entry in a rules table contains
 * two masks: rte_ltypes and rte_rtypes.  If the type on the
 * LHS of an edge is in rte_ltypes, and the type on the RHS
 * is in rte_rtypes, the procedure rte_proc is applied.
 */
typedef struct
{
    TileTypeBitMask	 rte_ltypes;	/* Apply if LHS type is in this set */
    TileTypeBitMask	 rte_rtypes;	/* Apply if RHS type is in this set */
    int			 rte_whichRules;/* See below */
    int		       (*rte_proc)();	/* Procedure implementing rule */
    char		*rte_name;	/* Name of rule (for debugging) */
} RuleTableEntry;

#define	MAXRULES	100	/* Ridiculously high */

#define	RTE_NULL	0	/* Don't use any rules at all */
#define	RTE_MINWIDTH	1	/* Use minimum width */
#define	RTE_REALWIDTH	2	/* Use computed real widths */
#define	RTE_SPACING	3	/* Use spacing rules */
#define	RTE_NOSPACING	4	/* Only apply rule if there are no spacing
				 * rules across this edge.
				 */

extern RuleTableEntry *plowCurrentRule;	/* For debugging */

/* ----------------------------- Edges -------------------------------- */

/*
 * An Edge describes the boundary between tiles of two different types.
 * It is used by shadow search to describe an edge it found, and also
 * to store edges in the edge queue (these latter along with their final
 * positions).
 *
 * If e_use is non-NULL, the "edge" is really the right-hand side of
 * a cell use.
 */
typedef struct edge
{
    Rect	 e_rect;	/* Rectangle describing the edge (see below) */
    int		 e_pNum;	/* Plane # in plowYankDef */
    TileType	 e_ltype;	/* Type of LHS of edge */
    TileType	 e_rtype;	/* Type of RHS of edge */
    int		 e_flags;	/* Miscellaneous flags: see below */

    /* Non-NULL if this is a cell moving instead of a piece of geometry */
    CellUse	*e_use;		/* Cell use to be moved */

    /* Only used in the queue of edges to move */
    struct edge	*e_next;	/* Next edge in list */
} Edge;

    /* Flags from above */
#define	E_ISINITIAL	0x01	/* Edge was added from initial plow */

    /* When an edge moves, it has an initial and final X position */
#define	e_x	e_rect.r_xbot	/* Initial X coordinate */
#define	e_newx	e_rect.r_xtop	/* Final X coordinate */

    /* The top and bottom of an edge don't change during plowing */
#define	e_ytop	e_rect.r_ytop
#define	e_ybot	e_rect.r_ybot

extern CellDef *plowYankDef;	/* Cell def containing planes above */

/* ----------------------- Outline searching -------------------------- */

/*
 * The following describes the outline of a boundary followed by
 * plowSrOutline().
 *
 * The following structure also reflects the directions associated
 * with a particular edge in the outline of a collection of types.
 * It gives the direction from which we turned to follow the current
 * edge, the direction along which we are following the current edge,
 * and the direction we will turn for following the next edge along
 * the outline.
 *
 * All directions are one of: GEO_NORTH, GEO_WEST, GEO_SOUTH, GEO_EAST.
 * When facing the indicated direction, the material on the inside of
 * the region being followed is always to the left.
 */
typedef struct
{
    Rect		 o_rect;	/* Degenerate rectangle defining the
					 * outline segment being processed.
					 */
    Tile		*o_inside;	/* Tile on inside of outline */
    Tile		*o_outside;	/* Tile on outside of outline */
    int			 o_pNum;	/* Plane # on which outline was found */
    int			 o_prevDir;	/* Previous direction */
    int			 o_currentDir;	/* Direction following this segment */
    int			 o_nextDir;	/* Direction to be followed next */

    /* Used only by plowSrOutline() */
    TileTypeBitMask	 o_insideTypes;	/* Mask of types inside the outline */
    Tile		*o_nextIn;	/* Next inside tile */
    Tile		*o_nextOut;	/* Next outside tile */
    Rect		 o_nextRect;	/* Next segment of boundary */
} Outline;

/* Masks of directions */
#define	GMaskHasDir(m, d)	((m & DirToGMask(d)) != 0)
#define	DirToGMask(d)		(1 << (d))

#define	GMASK_NORTH		DirToGMask(GEO_NORTH)
#define	GMASK_SOUTH		DirToGMask(GEO_SOUTH)
#define	GMASK_EAST		DirToGMask(GEO_EAST)
#define	GMASK_WEST		DirToGMask(GEO_WEST)

/* ------------------- Leading and trailing edges --------------------- */

/*
 * The leading and trailing edge of a tile are the "new" positions
 * as determined by plowing, as opposed to the "initial" positions
 * still stored in LEFT and RIGHT.
 *
 * As with LEFT and RIGHT, only the trailing coordinate of a tile is
 * stored explicitly; the leading coordinate is the trailing coordinate
 * of the tile to its right.
 */
#define	TRAIL_UNINIT	CLIENTDEFAULT
#define	TRAILING(tp)	(((tp)->ti_client == (ClientData)TRAIL_UNINIT) \
				? LEFT(tp) : ((int)(tp)->ti_client))
#define	LEADING(tp)	TRAILING(TR(tp))

#define	plowSetTrailing(tp, n)	((tp)->ti_client = (ClientData) (n))

/* ------------------ Design rules used by plowing -------------------- */

/*
 * The following structure is used both for spacing rules and
 * for width rules.
 */
typedef struct prule
{
    TileTypeBitMask	 pr_ltypes;	/* Material to the left of the
					 * outline of the LHS of the
					 * penumbra.
					 */
    TileTypeBitMask	 pr_oktypes;	/* Anything in the umbra or penumbra
					 * not of this type must be pr_dist
					 * away from the moving edge.
					 */
    int			 pr_dist;	/* Distance associated with this
					 * design rule.
					 */
    short		 pr_pNum;	/* Plane on which to apply rule
					 * (for spacing rules only).
					 */
    short		 pr_flags;	/* See below */
    struct prule	*pr_next;	/* Next rule in bucket */
} PlowRule;

/* Flags */
#define	PR_WIDTH	0x01	/* Spacing has this bit clear */
#define	PR_PENUMBRAONLY	0x02	/* Apply rule only in penumbra */
#define	PR_EDGE		0x04	/* Debugging: came from "edge" rule */
#define	PR_EDGE4WAY	0x08	/* Debugging: came from "edge4way" rule */
#define	PR_EDGEBACK	0x10	/* Debugging: backward part of "edge4way" */

extern PlowRule *plowSpacingRulesTbl[TT_MAXTYPES][TT_MAXTYPES];
extern PlowRule *plowWidthRulesTbl[TT_MAXTYPES][TT_MAXTYPES];

/*
 * Entry [t] of the following table is the maximum distance associated
 * with any design rules in a bucket with type 't' on the LHS.
 */
extern int plowMaxDist[TT_MAXTYPES];

/* --------------- Structure when applying plowing rules -------------- */

/*
 * Structure used by rules procedures for passing information
 * down to their filter functions.  Not all rules fill in all
 * the information in this structure; see the comments in each
 * rule for details.
 */
struct applyRule
{
    Edge	*ar_moving;	/* Edge being moved */
    PlowRule	*ar_rule;	/* Plowing rule being applied */

    /* Only used in penumbra */
    Point	 ar_clip;	/* Boundary of clipping rectangle */

    /* Only used in slivers */
    TileType	 ar_slivtype;	/* Material in middle of sliver sandwich */
    int		 ar_lastx;	/* Rightmost X we've seen so far */
    int		 ar_mustmove;	/* Must move slivers this far right */

    /* Only used with cells */
    int		 ar_pNum;	/* Plane being searched (area search only) */
    Rect	 ar_search;	/* Area being searched */
};

/* ----------------------- Internal procedures ------------------------ */

/* Procedure called when we've found a new edge to add */
extern int (*plowPropagateProcPtr)();

/* Other exports */
extern int plowQueueAdd();
extern bool plowQueueLeftmost();
extern bool plowQueueRightmost();
extern Tile *plowSplitY();

/* ------------------------- Debugging flags -------------------------- */

/*
 * The following come from a separate file so we can add more
 * debugging flags without forcing the entire system to be
 * recompiled.
 */
#include "plowDebugInt.h"

#endif	/* _PLOWINT_H */
