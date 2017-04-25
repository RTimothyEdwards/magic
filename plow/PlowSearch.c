/*
 * PlowSearch.c --
 *
 * Plowing.
 * Shadow and other searches.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/plow/PlowSearch.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "plow/plowInt.h"
#include "utils/stack.h"
#include "textio/textio.h"

/* Argument used in shadow search */
struct shadow
{
    Rect		 s_area;	/* Area being searched */
    TileTypeBitMask	 s_okTypes;	/* Complement of this set is the RHS
					 * boundary of the shadow.
					 */
    Edge		 s_edge;	/* Edge being built up */
    int		       (*s_proc)();	/* Filter procedure */
    ClientData		 s_cdata;	/* Additional argument to (*s_proc)() */
};

/* Stack used by plowSrOutline */
Stack *plowOutlineStack = NULL;

#define	FLUSHSTACK(s)	while (STACKLOOK(s)) (void) STACKPOP(s)

/* Outline tracing: is tile inside the outline */
#define	IsInside(tp, out) TTMaskHasType(&(out)->o_insideTypes, TiGetTypeExact(tp))

/* Forward declarations */

extern void plowSrOutlineInit();
extern void plowSrOutlineNext();

/*
 * ----------------------------------------------------------------------------
 *
 * EXTENDOUTLINE --
 *
 * EXTENDOUTLINE(outline)
 *	Outline *outline;
 * {
 * }
 *
 * Used to extend outline->o_nextRect in the direction being followed,
 * based on the tiles outline->o_nextIn and outline->o_nextOut.
 *
 * Assumes that outline->o_nextRect is initially a degenerate box equal
 * to the starting point of the segment.  We update exactly one coordinate
 * of outline->o_nextRect, depending on the direction outline->o_nextDir:
 *
 *	GEO_NORTH	r_ytop
 *	GEO_SOUTH	r_ybot
 *	GEO_EAST	r_xtop
 *	GEO_WEST	r_xbot
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates outline->o_nextRect as described above.
 *
 * ----------------------------------------------------------------------------
 */

#define	EXTENDOUTLINE(o) { \
Tile *in = (o)->o_nextIn; \
Tile *out = (o)->o_nextOut; \
switch ((o)->o_nextDir) \
{ \
    case GEO_NORTH: (o)->o_nextRect.r_ytop=MIN(TOP(out),TOP(in)); break; \
    case GEO_SOUTH: (o)->o_nextRect.r_ybot=MAX(BOTTOM(out),BOTTOM(in)); break; \
    case GEO_EAST: (o)->o_nextRect.r_xtop=MIN(RIGHT(in),RIGHT(out)); break; \
    case GEO_WEST: (o)->o_nextRect.r_xbot=MAX(LEFT(in),LEFT(out)); break; \
} \
}

/*
 * ----------------------------------------------------------------------------
 *
 * STACKOUTLINE --
 *
 * STACKOUTLINE(outline)
 *	Outline *outline;
 *
 * Called whenever we advance to a new inside/outside tile to
 * load the tile stack with the tiles on the opposite side of
 * the tile we just advanced to.
 *
 * The tile stack contains all the tiles along the outside of
 * outline->o_nextIn if we are going up or down, or all the
 * tiles along the inside of outline->o_nextOut if we are going
 * going left or right.
 *
 * Flushes the tile stack before we push any tiles of our own.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May push tiles on plowOutlineStack.
 *	Sets outline->o_nextIn if going east or west.
 *	Sets outline->o_nextOut if going north or south.
 *
 * ----------------------------------------------------------------------------
 */

#define	STACKOUTLINE(o) { \
    Tile *tp; \
\
    FLUSHSTACK(plowOutlineStack); \
    switch ((o)->o_nextDir) \
    { \
	case GEO_NORTH: \
	    for (tp = TR((o)->o_nextIn); \
		    BOTTOM(tp) > (o)->o_nextRect.r_ybot; tp = LB(tp)) \
		STACKPUSH((ClientData) tp, plowOutlineStack); \
	    (o)->o_nextOut = tp; \
	    break; \
	case GEO_SOUTH: \
	    for (tp = BL((o)->o_nextIn); \
		    TOP(tp) < (o)->o_nextRect.r_ytop; tp = RT(tp)) \
		STACKPUSH((ClientData) tp, plowOutlineStack); \
	    (o)->o_nextOut = tp; \
	    break; \
	case GEO_EAST: \
	    for (tp = RT((o)->o_nextOut); \
		    LEFT(tp) > (o)->o_nextRect.r_xbot; tp = BL(tp)) \
		STACKPUSH((ClientData) tp, plowOutlineStack); \
	    (o)->o_nextIn = tp; \
	    break; \
	case GEO_WEST: \
	    for (tp = LB((o)->o_nextOut); \
		    RIGHT(tp) < (o)->o_nextRect.r_xtop; tp = TR(tp)) \
		STACKPUSH((ClientData) tp, plowOutlineStack); \
	    (o)->o_nextIn = tp; \
	    break; \
    } \
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowSrShadow --
 *
 * Shadow search.
 * Searches area from left to right for edges whose far side is not in
 * okTypes.  The edges we find will have exactly one tile on each side.
 *
 * This is similar to an area search, except we call the procedure with
 * edges instead of tiles:
 *
 *	(*proc)(edge, cdata)
 *	    Edge *edge;
 *	    ClientData cdata;
 *	{
 *	}
 *
 * The Edge specifies the initial X location of the edge (e_x), the final
 * X location (e_newx, taken from the TRAILING coordinate of the tile on the
 * edge's right-hand side), and the edge's top and bottom (e_ytop, e_ybot).
 * It also contains the plane on which this edge was found (our argument
 * 'plane'), and the types of the tiles on each side of the edge (e_ltype
 * and e_rtype).
 *
 * The client is allowed to modify e_newx, but none of the other fields
 * of the Edge.
 *
 * If the procedure returns 1, we abort the shadow search.
 *
 * Results:
 *	Returns 1 if aborted, 0 if the search completed normally.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
plowSrShadow(pNum, area, okTypes, proc, cdata)
    int pNum;			/* Plane from plowYankDef to search */
    Rect *area;			/* Area to search.  Edges coincident with the
				 * right-hand side of this area are not seen;
				 * they must lie to the left of area->r_xtop.
				 */
    TileTypeBitMask okTypes;
    int (*proc)();		/* Function to apply at each edge */
    ClientData cdata;		/* Additional argument to pass to (*proc)() */
{
    Plane *plane = plowYankDef->cd_planes[pNum];
    struct shadow s;
    Tile *tp;
    int bottom, ret;
    Point p;

    ret = 0;

    /* Copy our arguments into 's' */
    s.s_area = *area;
    s.s_okTypes = okTypes;
    s.s_proc = proc;
    s.s_cdata = cdata;
    s.s_edge.e_use = (CellUse *) NULL;
    s.s_edge.e_flags = 0;
    s.s_edge.e_pNum = pNum;
    s.s_edge.e_ytop = s.s_area.r_ytop;

    /* Walk along the LHS of the sweep area from top to bottom */
    p.p_x = s.s_area.r_xbot;
    p.p_y = s.s_area.r_ytop - 1;
    tp = plane->pl_hint;
    while (p.p_y >= s.s_area.r_ybot)
    {
	/* Find the next tile along the LHS of the sweep area */
	GOTOPOINT(tp, &p);
	p.p_y = BOTTOM(tp) - 1;

	bottom = MAX(BOTTOM(tp), s.s_area.r_ybot);
	if (RIGHT(tp) >= s.s_area.r_xtop)
	{
	    s.s_edge.e_ytop = bottom;
	}
	else if (plowShadowRHS(tp, &s, bottom))
	{
	    ret = 1;
	    break;
	}
    }

    plane->pl_hint = tp;
    return (ret);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowShadowRHS --
 *
 * Walk along the right-hand side of tile 'tp', enumerating all edges formed
 * with tiles whose RHS is not a member of s->s_okTypes, and whose tops are
 * larger than 'bottomLeft'.
 *
 * Results:
 *	Returns 0 normally, but returns 1 if a client decided to
 *	interrupt the search by returning 1.
 *
 * Side effects:
 *	May call the filter function.
 *	Modifies s->s_edge.
 *
 * ----------------------------------------------------------------------------
 */

int
plowShadowRHS(tp, s, bottomLeft)
    Tile *tp;		/* Tile whose RHS is to be followed */
    struct shadow *s;	/* Shadow search argument */
    int bottomLeft;		/* Bottom of 'tp', clipped to area */
{
    Tile *tpR;
    int bottom, left;

    /* Walk along the RHS of 'tp' from top to bottom */
    tpR = TR(tp), left = LEFT(tpR);
    do
    {
	/*
	 * Only process tiles between s->s_edge.e_ytop (on the top)
	 * and bottomLeft (on the bottom).
	 */
	bottom = MAX(bottomLeft, BOTTOM(tpR));
	if (bottom < s->s_edge.e_ytop)
	{
	    /* If tpR is not in okTypes, pass the edge to the client function */
	    if (!TTMaskHasType(&s->s_okTypes, TiGetTypeExact(tpR)))
	    {
		/* Left, right hand types */
		s->s_edge.e_ltype = TiGetTypeExact(tp);
		s->s_edge.e_rtype = TiGetTypeExact(tpR);

		/* Coordinates */
		s->s_edge.e_x = left;
		s->s_edge.e_newx = TRAILING(tpR);
		s->s_edge.e_ybot = bottom;

		if ((*s->s_proc)(&s->s_edge, s->s_cdata))
		    return (1);

		/* Skip to bottom of the edge we just processed */
		s->s_edge.e_ytop = s->s_edge.e_ybot;
	    }
	    else if (RIGHT(tpR) >= s->s_area.r_xtop)
	    {
		/* Skip this edge segment */
		s->s_edge.e_ytop = bottom;
	    }
	    else
	    {
		if (plowShadowRHS(tpR, s, bottom))
		    return (1);
		/* Here s->s_edge.e_ytop == bottom */
	    }
	}
	tpR = LB(tpR);
    }
    while (TOP(tpR) > bottomLeft);

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowSrShadowInitial --
 *
 * Shadow search.
 * Searches area from left to right for edges whose far side is not in
 * okTypes, or whose near side is not in okTypes, and for which both
 * sides are different types.
 *
 * See plowSrShadow() above for more details.
 *
 * Results:
 *	Returns 1 if aborted, 0 if the search completed normally.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
plowSrShadowInitial(pNum, area, okTypes, proc, cdata)
    int pNum;			/* Plane from plowYankDef to search */
    Rect *area;			/* Area to search.  Edges coincident with the
				 * right-hand side of this area are not seen;
				 * they must lie to the left of area->r_xtop.
				 */
    TileTypeBitMask okTypes;
    int (*proc)();		/* Function to apply at each edge */
    ClientData cdata;		/* Additional argument to pass to (*proc)() */
{
    Plane *plane = plowYankDef->cd_planes[pNum];
    struct shadow s;
    Tile *tp;
    int bottom, ret;
    Point p;

    ret = 0;

    /* Copy our arguments into 's' */
    s.s_area = *area;
    s.s_okTypes = okTypes;
    s.s_proc = proc;
    s.s_cdata = cdata;
    s.s_edge.e_use = (CellUse *) NULL;
    s.s_edge.e_flags = 0;
    s.s_edge.e_pNum = pNum;
    s.s_edge.e_ytop = s.s_area.r_ytop;

    /* Walk along the LHS of the sweep area from top to bottom */
    p.p_x = s.s_area.r_xbot;
    p.p_y = s.s_area.r_ytop - 1;
    tp = plane->pl_hint;
    while (p.p_y >= s.s_area.r_ybot)
    {
	/* Find the next tile along the LHS of the sweep area */
	GOTOPOINT(tp, &p);
	p.p_y = BOTTOM(tp) - 1;

	bottom = MAX(BOTTOM(tp), s.s_area.r_ybot);
	if (RIGHT(tp) >= s.s_area.r_xtop)
	{
	    s.s_edge.e_ytop = bottom;
	}
	else if (plowShadowInitialRHS(tp, &s, bottom))
	{
	    ret = 1;
	    break;
	}
    }

    plane->pl_hint = tp;
    return (ret);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowShadowInitialRHS --
 *
 * Walk along the right-hand side of tile 'tp', enumerating all edges formed
 * with tiles whose RHS is not a member of s->s_okTypes, or whose LHS is not
 * a member of s->s_okTypes, and whose LHS and RHS are different, and whose
 * tops are larger than 'bottomLeft'.
 *
 * Results:
 *	Returns 0 normally, but returns 1 if a client decided to
 *	interrupt the search by returning 1.
 *
 * Side effects:
 *	May call the filter function.
 *	Modifies s->s_edge.
 *
 * ----------------------------------------------------------------------------
 */

int
plowShadowInitialRHS(tp, s, bottomLeft)
    Tile *tp;		/* Tile whose RHS is to be followed */
    struct shadow *s;	/* Shadow search argument */
    int bottomLeft;		/* Bottom of 'tp', clipped to area */
{
    Tile *tpR;
    int bottom, left;

    /* Walk along the RHS of 'tp' from top to bottom */
    tpR = TR(tp), left = LEFT(tpR);
    do
    {
	/*
	 * Only process tiles between s->s_edge.e_ytop (on the top)
	 * and bottomLeft (on the bottom).
	 */
	bottom = MAX(bottomLeft, BOTTOM(tpR));
	if (bottom < s->s_edge.e_ytop)
	{
	    /* If tpR is not in okTypes, pass the edge to the client function */
	    if (TiGetTypeExact(tp) != TiGetTypeExact(tpR)
		&& (!TTMaskHasType(&s->s_okTypes, TiGetTypeExact(tpR))
			|| !TTMaskHasType(&s->s_okTypes, TiGetTypeExact(tp))))
	    {
		/* Left, right hand types */
		s->s_edge.e_ltype = TiGetTypeExact(tp);
		s->s_edge.e_rtype = TiGetTypeExact(tpR);

		/* Coordinates */
		s->s_edge.e_x = left;
		s->s_edge.e_newx = TRAILING(tpR);
		s->s_edge.e_ybot = bottom;

		if ((*s->s_proc)(&s->s_edge, s->s_cdata))
		    return (1);

		/* Skip to bottom of the edge we just processed */
		s->s_edge.e_ytop = s->s_edge.e_ybot;
	    }
	    else if (RIGHT(tpR) >= s->s_area.r_xtop)
	    {
		/* Skip this edge segment */
		s->s_edge.e_ytop = bottom;
	    }
	    else
	    {
		if (plowShadowRHS(tpR, s, bottom))
		    return (1);
		/* Here s->s_edge.e_ytop == bottom */
	    }
	}
	tpR = LB(tpR);
    }
    while (TOP(tpR) > bottomLeft);

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowSrShadowBack --
 *
 * Shadow search, backwards.
 * Searches area from right to left for edges whose far side is not in
 * okTypes.  The edges we find will have exactly one tile on each side.
 *
 * See the comments in plowSrShadow() for details on the client procedure.
 *
 * Results:
 *	Returns 1 if aborted, 0 if the search completed normally.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
plowSrShadowBack(pNum, area, okTypes, proc, cdata)
    int pNum;			/* Plane from plowYankDef to search */
    Rect *area;			/* Area to search.  Edges coincident with the
				 * left-hand side of this area are not seen;
				 * they must lie to the right of area->r_xbot.
				 */
    TileTypeBitMask okTypes;
    int (*proc)();		/* Function to apply at each edge */
    ClientData cdata;		/* Additional argument to pass to (*proc)() */
{
    Plane *plane = plowYankDef->cd_planes[pNum];
    struct shadow s;
    Tile *tp;
    int top, ret;
    Point p;

    ret = 0;

    /* Copy our arguments into 's' */
    s.s_area = *area;
    s.s_okTypes = okTypes;
    s.s_proc = proc;
    s.s_cdata = cdata;
    s.s_edge.e_use = (CellUse *) NULL;
    s.s_edge.e_flags = 0;
    s.s_edge.e_pNum = pNum;
    s.s_edge.e_ybot = s.s_area.r_ybot;

    /* Walk along the RHS of the sweep area from bottom to top */
    p.p_x = s.s_area.r_xtop - 1;
    p.p_y = s.s_area.r_ybot;
    tp = plane->pl_hint;
    while (p.p_y < s.s_area.r_ytop)
    {
	/* Find the next tile along the RHS of the sweep area */
	GOTOPOINT(tp, &p);

	p.p_y = TOP(tp);

	top = MIN(TOP(tp), s.s_area.r_ytop);
	if (LEFT(tp) <= s.s_area.r_xbot)
	{
	    s.s_edge.e_ybot = top;
	}
	else if (plowShadowLHS(tp, &s, top))
	{
	    ret = 1;
	    break;
	}
    }

    /* if ret = 1 then tp may point to an invalid tile --- Tim 6/15/01 */
    if (!ret) plane->pl_hint = tp;
    return (ret);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowShadowLHS --
 *
 * Walk along the left-hand side of tile 'tp', enumerating all edges formed
 * with tiles whose LHS is not a member of s->s_okTypes, and whose bottoms are
 * less than 'topRight'.
 *
 * Results:
 *	Returns 0 normally, but returns 1 if a client decided to
 *	interrupt the search by returning 1.
 *
 * Side effects:
 *	May call the filter function.
 *	Modifies s->s_edge.
 *
 * ----------------------------------------------------------------------------
 */

int
plowShadowLHS(tp, s, topRight)
    Tile *tp;		/* Tile whose LHS is to be followed */
    struct shadow *s;	/* Shadow search argument */
    int topRight;		/* Top of 'tp', clipped to area */
{
    Tile *tpL;
    int top, right;

    /* Walk along the LHS of 'tp' from bottom to top */
    tpL = BL(tp), right = RIGHT(tpL);
    do
    {
	/*
	 * Only process tiles between s->s_edge.e_ybot (on the bottom)
	 * and topRight (on the top).
	 */
	top = MIN(topRight, TOP(tpL));
	if (top > s->s_edge.e_ybot)
	{
	    /* If tpL is not in okTypes, pass the edge to the client function */
	    if (!TTMaskHasType(&s->s_okTypes, TiGetTypeExact(tpL)))
	    {
		/* Left, right hand types */
		s->s_edge.e_ltype = TiGetTypeExact(tpL);
		s->s_edge.e_rtype = TiGetTypeExact(tp);

		/* Coordinates */
		s->s_edge.e_x = right;
		s->s_edge.e_newx = TRAILING(tp);
		s->s_edge.e_ytop = top;

		if ((*s->s_proc)(&s->s_edge, s->s_cdata))
		    return (1);

		/* Skip to top of the edge we just processed */
		s->s_edge.e_ybot = s->s_edge.e_ytop;
	    }
	    else if (LEFT(tpL) <= s->s_area.r_xbot)
	    {
		/* Skip this edge segment */
		s->s_edge.e_ybot = top;
	    }
	    else
	    {
		if (plowShadowLHS(tpL, s, top))
		    return (1);
		/* Here s->s_edge.e_ybot == top */
	    }
	}
	tpL = RT(tpL);
    }
    while (BOTTOM(tpL) < topRight);

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowAtomize --
 *
 * Given a geometrical edge (the LHS of the rectangle 'rect'), call
 * (*proc)() for each real Edge (between two tiles) found along
 * the geometrical edge.  Each Edge added will have a new x value of
 * rect->r_xtop.
 *
 * The procedure (*proc)() should be the same kind (in arguments and
 * return value) as that used in plowSrShadow() above.
 *
 * Don't call the procedure if the final coordinate of the tiles along
 * a given Edge is already farther right than rect->r_xtop.
 *
 * Results:
 *	Returns 1 if (*proc)() returned 1 to abort the atomizing;
 *	otherwise, returns 0.
 *
 * Side effects:
 *	Whatever the client procedure has.
 *
 * ----------------------------------------------------------------------------
 */

int
plowAtomize(pNum, rect, proc, cdata)
    int pNum;			/* Plane from plowYankDef to search */
    Rect *rect;	/* LHS is the geometrical edge we search; each
				 * Edge found will have an e_newx coordinate
				 * equal to the RHS of this rect.
				 */
    int (*proc)();		/* Procedure to apply to each Edge */
    ClientData cdata;		/* Additional argument to (*proc)() */
{
    Tile *tpL, *tpR;
    Plane *plane = plowYankDef->cd_planes[pNum];
    Point startPoint;
    Edge edge;
    int ytop;

    edge.e_x = rect->r_xbot;
    edge.e_newx = rect->r_xtop;
    edge.e_use = (CellUse *) NULL;
    edge.e_flags = 0;
    edge.e_pNum = pNum;

    ytop = rect->r_ytop;
    startPoint.p_x = rect->r_xbot;
    startPoint.p_y = rect->r_ytop - 1;

    /* Walk down the RHS of the edge */
    tpR = plane->pl_hint;
    GOTOPOINT(tpR, &startPoint);
    plane->pl_hint = tpR;
    for ( ; TOP(tpR) > rect->r_ybot; tpR = LB(tpR))
    {
	/* Only process edges that haven't moved far enough */
	if (TRAILING(tpR) < rect->r_xtop)
	{
	    edge.e_rtype = TiGetTypeExact(tpR);
	    edge.e_ybot = MAX(BOTTOM(tpR), rect->r_ybot);

	    /* Walk up the LHS of the edge */
	    for (tpL = BL(tpR); BOTTOM(tpL) < ytop; tpL = RT(tpL))
	    {
		/* Only process edges above the bottom of the clip area */
		if (TOP(tpL) > edge.e_ybot)
		{
		    edge.e_ytop = MIN(ytop, TOP(tpL));
		    edge.e_ltype = TiGetTypeExact(tpL);
		    if ((*proc)(&edge, cdata))
			return (1);
		    edge.e_ybot = edge.e_ytop;
		}
	    }
	}

	/* Advance the top of the clipping are downward */
	ytop = BOTTOM(tpR);
    }

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowSrOutline --
 *
 * Outline enumeration.
 * This procedure follows the outline of a collection of materials in a
 * counter-clockwise direction.  For each edge found while moving in a
 * direction specified in dirMask, call a supplied client procedure:
 *
 *	(*proc)(outline, cdata)
 *	    Outline *outline;
 *	    ClientData cdata;
 *	{
 *	}
 *
 * The bits in dirMask are GMASK_SOUTH, GMASK_NORTH, GMASK_EAST,
 * or GMASK_WEST.  They may be or'd together in any combination.
 *
 * The Outline gives a degenerate rectangle for this segment of the
 * outline.  Either this rectangle has zero height, or zero width.
 * It also contains the plane on which this segment was found (our
 * argument 'plane'), and pointers to the tiles on each side of the
 * outline (o_inside, o_outside).  Finally, it contains the direction
 * followed before this segment (o_prevDir), the direction being
 * followed along this segment (o_currentDir), and the direction to
 * be followed after this segment (o_nextDir).
 *
 * If the procedure returns 1, we abort the outline enumeration.
 * Otherwise, we keep going indefinitely.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Whatever occur as a result of applying the supplied client
 *	procedure.
 *
 * Assumptions:
 *	The interior of the region whose perimeter is being traced
 *	cannot extend to infinity in any direction.  Hence it is unwise
 *	to try tracing the perimeter of a region consisting of empty
 *	space, unless the client can be counted on to abort the search
 *	by returning 1.
 *
 * ----------------------------------------------------------------------------
 */

void
plowSrOutline(pNum, startPoint, insideTypes, initialDir, dirMask, proc, cdata)
    int pNum;				/* Plane # in plowYankDef to search */
    Point *startPoint;			/* Point on boundary; material of types
					 * in insideTypes should be to the
					 * inside (as determined by initialDir
					 * below).
					 */
    TileTypeBitMask insideTypes;	/* Mask of types inside the region
					 * whose outline is being traced.
					 */
    int initialDir;			/* Initial direction to go from the
					 * starting point.  One of GEO_NORTH,
					 * or GEO_SOUTH.
					 */
    int dirMask;			/* Mask of those directions for which
					 * we will call the client procedure.
					 */
    int (*proc)();			/* Client procedure */
    ClientData cdata;			/* Argument to client procedure */
{
    Outline outline;

    /*
     * Initialize the stack used for processing tiles
     * along the outside of the perimeter.
     */
    if (plowOutlineStack == (Stack *) NULL)
	plowOutlineStack = StackNew(50);

    /*
     * Push a bottom marker on the stack.
     * This allows plowSrOutline to be re-entrant.
     */
    STACKPUSH((ClientData) NULL, plowOutlineStack);

    /*
     * Start out going in the direction specified by initialDir.
     * This direction is really just a hint; we may go either 90 degrees
     * to the left or to the right if appropriate.
     */
    outline.o_pNum = pNum;
    outline.o_insideTypes = insideTypes;
    outline.o_currentDir = initialDir;
    outline.o_rect.r_ll = outline.o_rect.r_ur = *startPoint;
    plowSrOutlineInit(&outline);
    EXTENDOUTLINE(&outline);

    /*
     * Now we have found the initial segment and are ready for the main loop.
     * On entry to each iteration of this loop, o_currentDir and o_nextDir
     * are set from the previous iteration; we advance by one (current becomes
     * prev, next becomes current) and find the next direction, etc. before
     * calling the client procedure.
     */
    for (;;)
    {
	/* Advance along the boundary */
	outline.o_prevDir = outline.o_currentDir;
	outline.o_currentDir = outline.o_nextDir;
	outline.o_inside = outline.o_nextIn;
	outline.o_outside = outline.o_nextOut;
	outline.o_rect = outline.o_nextRect;

	/* Find outline.o_nextDir and next two tiles along boundary */
	plowSrOutlineNext(&outline);
	EXTENDOUTLINE(&outline);

	/* Now we know the next direction: apply the filter procedure */
	if (GMaskHasDir(dirMask, outline.o_currentDir))
	    if ((*proc)(&outline, cdata))
		break;
    }

    /* Flush anything remaining on the stack */
    while (STACKPOP(plowOutlineStack) != (ClientData) NULL)
	/* Nothing */;
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowSrOutlineInit --
 *
 * Initialization for outline enumeration.
 * Find the first edge in outline enumeration.
 * On entry, o_rect should be degenerate (ll == ur) around the starting
 * point for the search.  This starting point should be on the outline.
 * The direction o_currentDir should be one of GEO_NORTH or GEO_SOUTH; we
 * pretend as though we arrived at the starting point from this direction.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Fills in fields of 'outline':
 *		o_nextIn, o_nextOut	-- tiles on inside and outside of
 *					   current edge.
 *		o_nextDir		-- direction of current edge; this will
 *					   differ by at most 90 degrees from
 *					   o_currentDir.
 *		o_nextRect		-- current edge itself.
 *
 * ----------------------------------------------------------------------------
 */

void
plowSrOutlineInit(outline)
    Outline *outline;
{
    Plane *plane = plowYankDef->cd_planes[outline->o_pNum];
    Tile *in, *out;
    Point p;

    outline->o_nextDir = outline->o_currentDir;
    outline->o_nextRect = outline->o_rect;
    switch (outline->o_nextDir)
    {
	case GEO_NORTH:
	    /*
	     * Find the tiles on either side of the starting point (+):
	     *
	     *			|
	     *		    in	|  out
	     *		--------+--------
	     *			|
	     *			|
	     *
	     * It is possible for 'in' and 'out' to be the same tile.
	     */
	    out = plane->pl_hint;
	    p = outline->o_rect.r_ll;
	    GOTOPOINT(out, &p);
	    p.p_x--;
	    in = out;
	    GOTOPOINT(in, &p);

	    if (!IsInside(in, outline))
	    {
		/*
		 * Tile 'in' is really outside the outline, so go left.
		 * We know that BOTTOM(in) == outline->o_rect.r_xbot because
		 * the starting point is on the outline.  We want:
		 *			|
		 *		   out	|
		 *		--------+--------
		 *		    in	|
		 *			|
		 */
		outline->o_nextDir = GEO_WEST;
		outline->o_nextOut = in;
	    }
	    else if (IsInside(out, outline))
	    {
		/*
		 * Tile 'out' is really inside the outline, so go right.
		 * We know that TOP(out) == outline->o_rect.r_xbot because
		 * the starting point is on the outline.  We want:
		 *			|
		 *			|  in
		 *		--------+--------
		 *			|  out
		 *			|
		 */
		outline->o_nextDir = GEO_EAST;
		for (out = LB(out);
			RIGHT(out) <= outline->o_rect.r_xbot; out = TR(out))
		    /* Nothing */;
		outline->o_nextOut = out;
	    }
	    else
	    {
		/*
		 * Tile 'in' is really inside the outline, and tile 'out'
		 * is really outside, so we already are where we want to be.
		 */
		outline->o_nextIn = in;
	    }
	    break;
	case GEO_SOUTH:
	    /*
	     * Find the tiles on either side of the starting point (+):
	     *
	     *			|
	     *			|
	     *		--------+--------
	     *		   out	|  in
	     *			|
	     *
	     * It is possible for 'in' and 'out' to be the same tile.
	     */
	    p.p_x = outline->o_rect.r_xbot - 1;
	    p.p_y = outline->o_rect.r_ybot - 1;
	    out = plane->pl_hint;
	    GOTOPOINT(out, &p);
	    p.p_x++;
	    in = out;
	    GOTOPOINT(in, &p);

	    if (!IsInside(in, outline))
	    {
		/*
		 * Tile 'in' is really outside the outline, so go right.
		 * We know that TOP(in) == outline->o_rect.r_xbot because
		 * the starting point is on the outline.  We want:
		 *			|
		 *			|  in
		 *		--------+--------
		 *			|  out
		 *			|
		 */
		outline->o_nextDir = GEO_EAST;
		outline->o_nextOut = in;
	    }
	    else if (IsInside(out, outline))
	    {
		/*
		 * Tile 'out' is really inside the outline, so go left.
		 * We know that TOP(out) == outline->o_rect.r_xbot because
		 * the starting point is on the outline.  We want:
		 *			|
		 *		   out	|
		 *		--------+--------
		 *		    in	|
		 *			|
		 */
		outline->o_nextDir = GEO_WEST;
		for (out = RT(out);
			LEFT(out) >= outline->o_rect.r_xbot; out = BL(out))
		    /* Nothing */;
		outline->o_nextOut = out;
	    }
	    else
	    {
		/*
		 * Tile 'in' is really inside the outline, and tile 'out'
		 * is really outside, so we already are where we want to be.
		 */
		outline->o_nextIn = in;
	    }
	    break;
	default:
	    TxError("Illegal initialDir (%d) for plowSrOutline\n",
		    outline->o_nextDir);
	    niceabort();
	    return;
    }

    STACKOUTLINE(outline);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowSrOutlineNext --
 *
 * Find the next edge in outline enumeration.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Fills in fields of 'outline':
 *		o_nextIn, o_nextOut	-- tiles on inside and outside of
 *					   next edge.
 *		o_nextDir		-- direction of next edge.
 *		o_nextRect		-- next edge's rectangle
 *
 * ----------------------------------------------------------------------------
 */

void
plowSrOutlineNext(outline)
    Outline *outline;
{
    Tile *tpL, *tpR;

    /*
     * Initialize the "next" segment to be the degenerate
     * endpoint of the current segment.
     */
    outline->o_nextDir = outline->o_currentDir;
    switch (outline->o_currentDir)
    {
	case GEO_NORTH:
	case GEO_EAST:
	    outline->o_nextRect.r_ur = outline->o_rect.r_ur;
	    outline->o_nextRect.r_ll = outline->o_rect.r_ur;
	    break;
	case GEO_SOUTH:
	case GEO_WEST:
	    outline->o_nextRect.r_ur = outline->o_rect.r_ll;
	    outline->o_nextRect.r_ll = outline->o_rect.r_ll;
	    break;
    }

    /*
     * If the tile stack was not yet empty, we do things the
     * easy way.  Advance to the next tile along the side of
     * the one inside (if going north/south) or the one outside
     * (if going east/west).
     */
    if (STACKLOOK(plowOutlineStack))
    {
	switch (outline->o_currentDir)
	{
	    /*
	     * Heading north.
	     *
	     *		| nextOut
	     *	 nextIn	+--------
	     *		| outside
	     *
	     * Turn east if nextOut is in o_insideTypes.
	     * Otherwise, keep going north.
	     */
	    case GEO_NORTH:
		outline->o_nextOut = (Tile *) STACKPOP(plowOutlineStack);
		if (IsInside(outline->o_nextOut, outline))
		{
		    outline->o_nextOut = LB(outline->o_nextOut);
		    outline->o_nextDir = GEO_EAST;
		}
		break;
	    /*
	     * Heading south.
	     *
	     *	outside	|
	     *	--------+ nextIn
	     *	nextOut	|
	     *
	     * Turn west if nextOut is in o_insideTypes.
	     * Otherwise, keep going south.
	     */
	    case GEO_SOUTH:
		outline->o_nextOut = (Tile *) STACKPOP(plowOutlineStack);
		if (IsInside(outline->o_nextOut, outline))
		{
		    outline->o_nextOut = RT(outline->o_nextOut);
		    outline->o_nextDir = GEO_WEST;
		}
		break;
	    /*
	     * Heading east.
	     *
	     *	 inside	| nextIn
	     *	--------+--------
	     *	     nextOut
	     *
	     * Turn north if nextIn is not in o_insideTypes.
	     * Otherwise, keep going east.
	     */
	    case GEO_EAST:
		outline->o_nextIn = (Tile *) STACKPOP(plowOutlineStack);
		if (!IsInside(outline->o_nextIn, outline))
		{
		    outline->o_nextIn = BL(outline->o_nextIn);
		    outline->o_nextDir = GEO_NORTH;
		}
		break;
	    /*
	     * Heading west.
	     *
	     *	     nextOut
	     *	--------+--------
	     *	 nextIn	| inside
	     *
	     * Turn south if nextIn is not in o_insideTypes.
	     * Otherwise, keep going west.
	     */
	    case GEO_WEST:
		outline->o_nextIn = (Tile *) STACKPOP(plowOutlineStack);
		if (!IsInside(outline->o_nextIn, outline))
		{
		    outline->o_nextIn = TR(outline->o_nextIn);
		    outline->o_nextDir = GEO_SOUTH;
		}
		break;
	}

	/*
	 * Stack a new set of tiles along the opposite side if
	 * we changed direction, and set the appropriate one of
	 * o_nextIn (if going east/west) or o_nextOut (if going
	 * north/south).
	 */
	if (outline->o_nextDir != outline->o_currentDir)
	    STACKOUTLINE(outline);
	return;
    }

    /*
     * The tile stack was empty, so we must advance the primary
     * tile (inside for north/south, outside for east/west).
     * This code is more complex than that above, because we
     * have three choices: continue going straight ahead, turn
     * left, or turn right.
     */
    switch (outline->o_currentDir)
    {
	/*
	 * Heading north.
	 *
	 *	   tpL	    tpR
	 *	--------+
	 *	 nextIn	|
	 *
	 * If tpL is outside, turn west.
	 * If tpR is inside, turn east.
	 * Otherwise, continue north.
	 */
	case GEO_NORTH:
	    tpL = RT(outline->o_nextIn);
	    if (!IsInside(tpL, outline))
	    {
		outline->o_nextOut = tpL;
		outline->o_nextDir = GEO_WEST;
		break;
	    }

	    /* Find tpR */
	    if (RIGHT(tpL) > outline->o_nextRect.r_xbot) tpR = tpL;
	    else for (tpR = TR(tpL);
			BOTTOM(tpR) > outline->o_nextRect.r_ybot; tpR = LB(tpR))
		/* Nothing */;
	    if (IsInside(tpR, outline))
	    {
		outline->o_nextOut = TR(outline->o_nextIn);
		outline->o_nextDir = GEO_EAST;
	    }
	    else outline->o_nextIn = tpL;
	    break;
	/*
	 * Heading south.
	 *
	 *		| nextIn
	 *	--------+--------
	 *	   tpL	   tpR
	 *
	 * If tpR is outside, turn east.
	 * If tpL is inside, turn west.
	 * Otherwise, continue south.
	 */
	case GEO_SOUTH:
	    tpR = LB(outline->o_nextIn);
	    if (!IsInside(tpR, outline))
	    {
		outline->o_nextOut = tpR;
		outline->o_nextDir = GEO_EAST;
		break;
	    }

	    /* Find tpL */
	    if (LEFT(tpR) < outline->o_nextRect.r_xbot) tpL = tpR;
	    else for (tpL = BL(tpR);
			TOP(tpL) < outline->o_nextRect.r_ytop; tpL = RT(tpL))
		/* Nothing */;
	    if (IsInside(tpL, outline))
	    {
		outline->o_nextOut = BL(outline->o_nextIn);
		outline->o_nextDir = GEO_WEST;
	    }
	    else outline->o_nextIn = tpR;
	    break;
	/*
	 * Heading east.
	 *
	 *		  tpL
	 *	--------+
	 *	nextOut	| tpR
	 *
	 * If tpR is inside, turn south.
	 * If tpL is outside, turn north.
	 * Otherwise, continue east.
	 */
	case GEO_EAST:
	    tpR = TR(outline->o_nextOut);
	    if (TOP(tpR) > outline->o_nextRect.r_ybot) tpL = tpR;
	    else for (tpL = RT(tpR);
		    LEFT(tpL) > outline->o_nextRect.r_xbot; tpL = BL(tpL))
		/* Nothing */;

	    if (!IsInside(tpL, outline))
	    {
		outline->o_nextIn = RT(outline->o_nextOut);
		outline->o_nextDir = GEO_NORTH;
	    }
	    else if (IsInside(tpR, outline))
	    {
		outline->o_nextIn = tpR;
		outline->o_nextDir = GEO_SOUTH;
	    }
	    else outline->o_nextOut = tpR;
	    break;
	/*
	 * Heading west.
	 *
	 *	    tpL	  nextOut
	 *		+--------
	 *	    tpR
	 *
	 * If tpL is inside, turn north.
	 * If tpR is outside, turn south.
	 * Otherwise, continue west.
	 */
	case GEO_WEST:
	    tpL = BL(outline->o_nextOut);
	    if (BOTTOM(tpL) < outline->o_nextRect.r_ybot) tpR = tpL;
	    else for (tpR = LB(tpL);
		    RIGHT(tpR) < outline->o_nextRect.r_xbot; tpR = TR(tpR))
		/* Nothing */;
	    if (!IsInside(tpR, outline))
	    {
		outline->o_nextIn = LB(outline->o_nextOut);
		outline->o_nextDir = GEO_SOUTH;
	    }
	    else if (IsInside(tpL, outline))
	    {
		outline->o_nextIn = tpL;
		outline->o_nextDir = GEO_NORTH;
	    }
	    else outline->o_nextOut = tpL;
	    break;
    }

    /*
     * Stack a new set of tiles along the opposite side always,
     * since we either advanced to the next tile or changed
     * direction.  Set the appropriate one of o_nextIn (if
     * going east/west) or o_nextOut (if going north/south).
     */
    STACKOUTLINE(outline);
}
