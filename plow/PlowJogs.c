/*
 * PlowJogs.c --
 *
 * Jog cleanup.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/plow/PlowJogs.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/undo.h"
#include "plow/plow.h"
#include "plow/plowInt.h"
#include "utils/malloc.h"
#include "debug/debug.h"

/* Passed to filter functions */
Edge *jogEdge;			/* Points to RHS edge of jog */
Rect *jogArea;			/* Area in which jogs are eliminated, in
				 * plowYankDef coordinates.
				 */
int jogTopDir, jogBotDir;	/* Direction of top and bottom of the outline
				 * at the top and bottom of the jog candidate.
				 */
Point jogTopPoint, jogBotPoint;	/* Location of top and bot of above outline */

/* Used when applying plowing rules */
Rect *plowJogLHS;		/* If non-NULL, it's also OK for any edge
				 * along this rectangle's LHS to move as
				 * a result of applying the plowing rules.
				 */
bool plowJogMoved;		/* TRUE if any other edges moved */
LinkedRect *plowJogEraseList;	/* List of areas to erase */
Rect plowJogChangedArea;	/* Area changed */

/*
 * Codes for jogTopDir, jogBotDir above.
 * For jogBotDir, we use the same codes, except "up" really means "down".
 */
#define	J_N	0	/* Left via top of clip area */
#define	J_NE	1	/* Left via RHS of clip area */
#define	J_NW	2	/* Went up then turned left */
#define	J_NES	3	/* Went up, turned right, then turned down */
#define	J_NEN	4	/* Went up, turned right, then turned up */

/* Imports from PlowMain.c */
extern int plowInitialPaint();

/* Forward declarations */
int plowJogPropagateLeft();
int plowProcessJogFunc();
int plowJogMoveFunc();
int plowJogDragLHS();
int plowJogTopProc();
int plowJogBotProc();

extern void plowProcessJog();


/*
 * ----------------------------------------------------------------------------
 *
 * plowCleanupJogs --
 *
 * Clean up all jogs in the interior of the Rect 'area', in the CellDef
 * plowYankDef, by scanning from right to left and eliminating jogs if
 * we can do so without moving any new edges.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the geometry of plowYankDef in the area 'area'.
 *	Updates changedArea via GeoIncludes to reflect the area
 *	modified by jog reduction.
 *
 * ----------------------------------------------------------------------------
 */

void
plowCleanupJogs(area, changedArea)
    Rect *area;
    Rect *changedArea;
{
    Edge edge;

    /*
     * We use our own procedure for propagating the effects of
     * moving an edge.  Instead of adding more edges to a queue,
     * our procedure simply keeps track of whether any edges moved
     * at all, since we won't want to eliminate a jog if it causes
     * other stuff to move.
     */
    plowPropagateProcPtr = plowJogMoveFunc;

    /* Initialize the queue of edges to move */
    plowQueueInit(area, area->r_xtop - area->r_xbot);

    /* We store the area changed in a global for easy access */
    plowJogChangedArea = *changedArea;

    /*
     * Process as though the RHS of the area were an edge.
     * This means searching leftward for edges with material
     * on their LHS and space on their RHS.
     */
    edge.e_x = edge.e_newx = area->r_xtop;
    edge.e_ybot = area->r_ybot;
    edge.e_ytop = area->r_ytop;
    edge.e_use = (CellUse *) NULL;
    edge.e_flags = 0;
    for (edge.e_pNum = PL_TECHDEPBASE; edge.e_pNum < DBNumPlanes; edge.e_pNum++)
	plowProcessJog(&edge, area);

    /* While edges remain, process them, and propagate leftward */
    while (plowQueueRightmost(&edge))
	plowProcessJog(&edge, area);

    /* Clean up */
    plowQueueDone();
    *changedArea = plowJogChangedArea;
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowProcessJog --
 *
 * Process an edge between space and material for jog elimination.
 * The idea is to search to the left of this edge for jogs that
 * can be straightened without forcing other material to move.
 * We repeat this, searching for a jog and eliminating it, until
 * we can move no more jogs.
 *
 * When finally done, we search leftward for any edges between
 * space and material, and add these to the queue to be processed.
 *
 * The argument 'area' bounds the area in which jog elimination
 * will take place.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the geometry of plowYankDef.
 *
 * ----------------------------------------------------------------------------
 */

void
plowProcessJog(edge, area)
    Edge *edge;
    Rect *area;
{
    Rect r;

    if (DebugIsSet(plowDebugID, plowDebJogs))
	plowDebugEdge(edge, (RuleTableEntry *) NULL, "plowProcessJog");

    /* Scan left from this edge to the LHS of the jog reduction area */
    r.r_xbot = area->r_xbot;
    r.r_xtop = edge->e_x;
    r.r_ybot = edge->e_ybot;
    r.r_ytop = edge->e_ytop;

    /*
     * Scan leftward for edges between material and space.  Such edges
     * are potentially jogs that can be removed.  If plowProcessJogFunc
     * does in fact remove a jog containing one of the edges it finds,
     * it returns 1 and aborts.  We must therefore iterate until no more
     * jogs are eliminated.
     */
    while (plowSrShadowBack(edge->e_pNum, &r, DBSpaceBits,
		plowProcessJogFunc, (ClientData) area))
	/* Nothing */;

    /* Scan to next edge between space and material */
    (void) plowSrShadowBack(edge->e_pNum, &r, DBAllButSpaceBits,
		plowJogPropagateLeft, (ClientData) NULL);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowJogPropagateLeft --
 *
 * Called by plowSrShadowBack(), we add edges between space and material
 * to the queue for further processing.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	Adds the edge to the queue of edges to move via plowQueueAdd()
 *	if e_ltype == TT_SPACE and e_rtype != TT_SPACE.
 *	Leaves edge->e_newx == edge->e_x.
 *
 * ----------------------------------------------------------------------------
 */

int
plowJogPropagateLeft(edge)
    Edge *edge;
{
    if (DebugIsSet(plowDebugID, plowDebJogs))
	plowDebugEdge(edge, (RuleTableEntry *) NULL, "plowJogPropagateLeft");

    edge->e_newx = edge->e_x;
    if (edge->e_ltype == TT_SPACE && edge->e_rtype != TT_SPACE)
	(void) plowQueueAdd(edge);
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowProcessJogFunc --
 *
 * Do the real work of eliminating a jog.
 * This procedure is called via plowSrShadowBack() for each non-space
 * edge in the shadow of a space edge.  Each edge we find will have
 * e_ltype != TT_SPACE and e_rtype == TT_SPACE.
 *
 * We start following the outline of this edge; if it turns out that we
 * can eliminate a jog, we do so and return 1 to stop the search; otherwise,
 * we return 0 to force the search to continue.
 *
 * Results:
 *	Returns 1 if we eliminated a jog, 0 otherwise.
 *
 * Side effects:
 *	May update the geometry of plowYankDef.
 *
 * ----------------------------------------------------------------------------
 */

int
plowProcessJogFunc(edge, area)
    Edge *edge;		/* Edge found by shadow search */
    Rect *area;		/* Area in which jogs are being eliminated */
{
    LinkedRect *lr;
    Rect r, lhs;
    TileTypeBitMask mask;
    Point startPoint;
    int width, ret;
    Edge newedge;
    Plane *plane;

    if (DebugIsSet(plowDebugID, plowDebJogs))
	plowDebugEdge(edge, (RuleTableEntry *) NULL, "plowProcessJogFunc");

    TTMaskSetOnlyType(&mask, edge->e_ltype);
    startPoint.p_x = edge->e_x;
    jogEdge = edge;
    jogArea = area;

    /* Walk up the outline of which this edge is a part */
    startPoint.p_y = edge->e_ytop;
    jogTopPoint = startPoint;
    jogTopDir = J_N;
    plowSrOutline(edge->e_pNum, &startPoint, mask, GEO_NORTH,
		GMASK_NORTH|GMASK_WEST|GMASK_EAST,
		plowJogTopProc, (ClientData) NULL);

    /* Walk down the outline of which this edge is a part */
    TTMaskCom(&mask);
    startPoint.p_y = edge->e_ybot;
    jogBotPoint = startPoint;
    jogBotDir = J_N;
    plowSrOutline(edge->e_pNum, &startPoint, mask, GEO_SOUTH,
		GMASK_SOUTH|GMASK_WEST|GMASK_EAST,
		plowJogBotProc, (ClientData) NULL);

    /* Try to reject this jog based solely on the geometry */
    if (jogTopDir == J_N || jogBotDir == J_N) return (0);
    if (jogTopDir != J_NEN && jogBotDir != J_NEN)
	return (0);

    /*
     * More geometry-based rejection.
     * Reject if we found either of the following configurations:
     *
     *		+-----+				|
     *		|     |				|
     *		|				|
     *		+-------+		+-------+
     *			|		|
     *			|		|     |
     *			|		+-----+
     */
    if (jogTopDir == J_NES && jogTopPoint.p_x <= jogBotPoint.p_x)
	return (0);
    if (jogBotDir == J_NES && jogBotPoint.p_x <= jogTopPoint.p_x)
	return (0);

    /*
     * Extend the edge to the full height of the jog.
     * The jog will move as far as the leftmost of the two endpoints
     * (jogTopPoint.p_x, jogBotPoint.p_x) if it is a C-shaped jog, or the
     * farther of the two if it is a Z-shaped jog.
     */
    newedge = *edge;
    newedge.e_ybot = jogBotPoint.p_y;
    newedge.e_ytop = jogTopPoint.p_y;
    newedge.e_newx = (jogTopDir == J_NW || jogBotDir == J_NW)
		? MAX(jogTopPoint.p_x, jogBotPoint.p_x)
		: MIN(jogTopPoint.p_x, jogBotPoint.p_x);
    jogEdge = &newedge;
    if (DebugIsSet(plowDebugID, plowDebJogs))
	plowDebugEdge(&newedge, (RuleTableEntry *) NULL, "jog extended edge");

    /* Reject if this jog extends outside of the area */
    if (!GEO_SURROUND(area, &newedge.e_rect))
	return (0);

    /*
     * Apply the plowing rules.
     * Don't do anything if moving the RHS edge will cause
     * other edges to move.
     */
    plowJogMoved = FALSE;
    plowJogLHS = (Rect *) NULL;
    plowApplySearchRules(&newedge);
    if (plowJogMoved)
	return (0);

    /*
     * Now handle the LHS.
     * Find the width of this wire (looking leftward) and
     * search for edges 'width' to the left of the RHS.
     * Extend the search area by 'width' to the top or bottom
     * depending on the shape of this jog, to be sure to catch
     * all of the LHS of the jog.
     */
    TTMaskSetOnlyType(&mask, edge->e_ltype);
    width = plowFindWidthBack(&newedge, mask, area, (Rect *) NULL);
    r.r_xtop = newedge.e_x;
    r.r_xbot = newedge.e_x - width - 1;
    r.r_ytop = newedge.e_ytop;
    r.r_ybot = newedge.e_ybot;
    if (jogTopDir != J_NW) r.r_ytop += width;	/* Extend */
    if (jogBotDir != J_NW) r.r_ybot -= width;	/* Extend */

    /* Reject if the LHS extends outside of area */
    if (!GEO_SURROUND(area, &r))
	return (0);

    /* Also OK to move parts of the LHS (e.g, sliver elimination) */
    lhs = r;
    lhs.r_xbot++;
    plowJogLHS = &lhs;

    ret = 0;
    plowJogEraseList = (LinkedRect *) NULL;
    if (plowSrShadowBack(newedge.e_pNum, &r, mask,
		plowJogDragLHS, (ClientData) newedge.e_newx - width) == 0)
    {
	/* Success: first paint to extend the RHS of the jog */
	plane = plowYankDef->cd_planes[newedge.e_pNum];
	DBPaintPlane(plane, &newedge.e_rect,
		    DBWriteResultTbl[newedge.e_ltype], (PaintUndoInfo *) NULL);
	(void) GeoInclude(&newedge.e_rect, &plowJogChangedArea);

	/* Now erase to extend the pieces of the LHS of the jog */
	for (lr = plowJogEraseList; lr; lr = lr->r_next)
	{
	    DBPaintPlane(plane, &lr->r_r,
		    DBWriteResultTbl[TT_SPACE], (PaintUndoInfo *) NULL);
	    (void) GeoInclude(&lr->r_r, &plowJogChangedArea);
	}
	ret = 1;
    }

    /* Free the erase list we built in plowJogDragLHS */
    for (lr = plowJogEraseList; lr; lr = lr->r_next)
	freeMagic((char *) lr);
    plowJogEraseList = (LinkedRect *) NULL;
    return (ret);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowJogTopProc --
 * plowJogBotProc --
 *
 * Procedures called by plowSrOutline() to trace the outline of
 * a potential jog.
 *
 * Results:
 *	Both procedures return 1 to stop the search, 0 to continue it.
 *
 * Side effects:
 *	plowJogTopProc fills in jogTopDir, jogTopPoint.
 *	plowJogBotProc fills in jogBotDir, jogBotPoint.
 *
 * ----------------------------------------------------------------------------
 */

int
plowJogTopProc(outline)
    Outline *outline;
{
    /* Stop if we're no longer adjacent to space */
    if (TiGetTypeExact(outline->o_outside) != TT_SPACE)
	return (1);

    switch (outline->o_currentDir)
    {
	case GEO_WEST:
	    jogTopDir = J_NW;
	    return (1);
	case GEO_NORTH:
	    jogTopPoint = outline->o_rect.r_ur;
	    jogTopDir = J_N;
	    if (outline->o_rect.r_ytop > jogArea->r_ytop)
	    {
		jogTopPoint.p_y = jogArea->r_ytop;
		jogTopDir = J_N;
		return (1);
	    }
	    break;
	case GEO_EAST:
	    jogTopPoint = outline->o_rect.r_ur;
	    jogTopDir = J_NE;
	    if (outline->o_rect.r_xtop >= jogArea->r_xtop)
	    {
		jogTopPoint.p_x = jogArea->r_xtop;
		jogTopDir = J_NE;
		return (1);
	    }
	    switch (outline->o_nextDir)
	    {
		case GEO_NORTH:
		    jogTopDir = J_NEN;
		    return (1);
		case GEO_SOUTH:
		    jogTopDir = J_NES;
		    return (1);
	    }
	    break;
    }

    return (0);
}

int
plowJogBotProc(outline)
    Outline *outline;
{
    /* Stop if we're no longer adjacent to space */
    if (TiGetTypeExact(outline->o_inside) != TT_SPACE)
	return (1);

    switch (outline->o_currentDir)
    {
	case GEO_WEST:
	    jogBotDir = J_NW;
	    return (1);
	case GEO_SOUTH:
	    jogBotPoint = outline->o_rect.r_ll;
	    jogBotDir = J_N;
	    if (outline->o_rect.r_ybot < jogArea->r_ybot)
	    {
		jogBotPoint.p_y = jogArea->r_ybot;
		jogBotDir = J_N;
		return (1);
	    }
	    break;
	case GEO_EAST:
	    jogBotPoint = outline->o_rect.r_ur;
	    jogBotDir = J_NE;
	    if (outline->o_rect.r_xtop >= jogArea->r_xtop)
	    {
		jogBotPoint.p_x = jogArea->r_xtop;
		jogBotDir = J_NE;
		return (1);
	    }
	    /* Note that these directions are reversed from plowJogTopProc */
	    switch (outline->o_nextDir)
	    {
		case GEO_NORTH:
		    jogBotDir = J_NES;
		    return (1);
		case GEO_SOUTH:
		    jogBotDir = J_NEN;
		    return (1);
	    }
	    break;
    }

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowJogDragLHS --
 *
 * Procedure called via plowSrShadowBack() from the RHS of a potential
 * jog, to see if dragging each of the pieces of the LHS will cause
 * any other edges to move.  We ignore 'edge' if it doesn't have space
 * to its left.
 *
 * Results:
 *	Returns 1 if we detect that something else must move, 0 otherwise.
 *
 * Side effects:
 *	Prepends a LinkedRect to plowJogEraseList if we return 0.
 *	This LinkedRect has a rectangle equal to the Edge 'edge'
 *	with e_newx == newx.
 *
 * ----------------------------------------------------------------------------
 */

int
plowJogDragLHS(edge, newx)
    Edge *edge;		/* Edge potentially on the LHS of the jog */
    int newx;		/* Move the edge to this position */
{
    LinkedRect *lr;

    /* Ignore edges that are not to space */
    if (edge->e_ltype != TT_SPACE) return (0);

    /* See what will happen if we move this edge right to newx */
    edge->e_newx = newx;
    plowJogMoved = FALSE;
    plowApplySearchRules(edge);
    if (plowJogMoved)
	return (1);

    /*
     * Success: nothing else (except perhaps the original RHS edge) moved.
     * Append this edge to the list of linked rectangles.
     */
    lr = (LinkedRect *) mallocMagic((unsigned) (sizeof (LinkedRect)));
    lr->r_r = edge->e_rect;
    lr->r_next = plowJogEraseList;
    plowJogEraseList = lr;

    /* Keep going */
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowJogMoveFunc --
 *
 * Procedure called via (*plowPropagateProcPtr)() if we find that
 * eliminating a jog might cause other edges to move.  If our argument
 * edge is not a sub-piece of the RHS of the jog (jogEdge),
 * then we set plowJogMoved to TRUE.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Sets plowJogMoved as described above.
 *
 * ----------------------------------------------------------------------------
 */

int
plowJogMoveFunc(edge)
    Edge *edge;
{
    Edge *origEdge = jogEdge;

    if (DebugIsSet(plowDebugID, plowDebJogs))
	plowDebugEdge(edge, (RuleTableEntry *) NULL, "plowJogMoveFunc");

    if (origEdge->e_pNum == edge->e_pNum)
    {
	/* It's OK to move the original edge */
	if (origEdge->e_x == edge->e_x
		&& origEdge->e_ytop >= edge->e_ytop
		&& origEdge->e_ybot <= edge->e_ybot)
	{
	    return (0);
	}

	/* It's OK to move other parts of the LHS, e.g, slivers */
	if (plowJogLHS
		&& edge->e_x == plowJogLHS->r_xbot
		&& edge->e_ybot >= plowJogLHS->r_ybot
		&& edge->e_ytop <= plowJogLHS->r_ytop
		&& edge->e_ltype == TT_SPACE
		&& edge->e_rtype == origEdge->e_ltype)
	{
	    return (0);
	}
    }

    plowJogMoved = TRUE;
    return (0);
}
