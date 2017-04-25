/*
 * PlowRules1.c --
 *
 * Plowing rules.
 * These are applied by plowProcessEdge() for each edge that is to be moved.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/plow/PlowRules1.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
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
#include "drc/drc.h"

/* Forward declarations */
int plowPenumbraTopProc();
int plowPenumbraBotProc();
int plowSliverTopExtent(), plowSliverTopMove();
int plowSliverBotExtent(), plowSliverBotMove();
int plowApplyRule();
int plowPenumbraRule();
bool plowSliverApplyRules();

/*
 * ----------------------------------------------------------------------------
 *
 * prClearUmbra --
 *
 * SEARCH RULE.
 * Sweep all edges out of the umbra.
 * This rule preserves horizontal edge orderings within a plane.
 * Two segments that overlap in Y cannot cross each other in X:
 *
 *
 *	1		2
 *	1 ------------>	2	1 will force 2 to move
 *	1		2	regardless of design rules
 *
 *
 * Below, this rule doesn't apply, so 1 can slide past 2.
 *
 *	1
 *	1 ------------------>
 *	1
 *			2
 *			2
 *			2
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May add edges to the queue of edges to be processed.
 *
 * ----------------------------------------------------------------------------
 */

void
prClearUmbra(edge)
    Edge *edge;	/* Edge being moved */
{
    TileTypeBitMask rhsTypes;
    struct applyRule ar;

    TTMaskSetOnlyType(&rhsTypes, edge->e_rtype);
    ar.ar_moving = edge;
    ar.ar_rule = (PlowRule *) NULL;
    (void) plowSrShadow(edge->e_pNum, &edge->e_rect,
		rhsTypes, plowApplyRule, (ClientData) &ar);
}

/*
 * ----------------------------------------------------------------------------
 *
 * prUmbra --
 *
 * SEARCH RULE.
 * Apply width or spacing rules in the umbra.
 * The umbra of an edge is the area through which it moves from its
 * initial position (I) to its final position (F):
 *
 *	I ======================F-------D
 *	I			F	D
 *	I	  umbra		F  halo	D
 *	I			F	D
 *	I ======================F-------D
 *
 *	  --------------------> + --d-->
 *
 * For each rule in the list 'rules', we shadow-search the umbra plus an
 * additional area of width 'd' to the right (ending at (D) above), where
 * 'd' is the distance associated with the rule.  We queue to be moved the
 * edges of any material found that is not in the rule's pr_oktypes set.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May add edges to the queue of edges to be processed.
 *
 * ----------------------------------------------------------------------------
 */

void
prUmbra(edge, rules)
    Edge *edge;		/* Edge being moved */
    PlowRule *rules;	/* List of rules */
{
    PlowRule *pr;
    struct applyRule ar;
    Rect searchArea;

    ar.ar_moving = edge;
    searchArea = edge->e_rect;
    for (pr = rules; pr; pr = pr->pr_next)
    {
	ar.ar_rule = pr;
	searchArea.r_xtop = edge->e_newx + pr->pr_dist;
	(void) plowSrShadow(pr->pr_pNum, &searchArea,
			pr->pr_oktypes, plowApplyRule, (ClientData) &ar);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * prPenumbraTop --
 * prPenumbraBot --
 *
 * Search the penumbra of the supplied edge for each rule in the list
 * 'list', moving all edges necessary to insure that width or spacing
 * rules are satisfied.  The upper part of the penumbra is searched by
 * plowPenumbraTop, and the lower part by plowPenumbraBot.
 *
 * The penumbra of an edge (E) is that region above and below it, in the
 * direction the edge is moving.  The left-hand border of the penumbra,
 * and the distance it extends to the right of the final position of
 * the edge (F), are equal to the distance associated with the design
 * rule (d).
 *
 *   ^		T===============|
 *   |		T		|
 *   d		T		|
 *   |	TTTTTTTTT    upper	|
 *   v	T=======================|
 *	E		F
 *	E		F
 *	E ------------>	F <--d-->
 *	E		F
 *	E		F
 *   ^	BBBBBBBBB===============|
 *   |		B    lower	|
 *   d		BBBBBBBBB	|
 *   |			B	|
 *   v			B=======|
 *
 * The left-hand border of the penumbra (T, B) for a rule is not a
 * simple extension of the original edge E, but is instead the result
 * of following the outline of the material in the pr_ltypes set
 * associated with that rule.
 *
 * If the outline (O) turns left before reaching the top (bottom) of the
 * clip area, the remainder of the left-hand border (X) of the penumbra
 * is considered to extend from where the outline turned left, out to
 * the top (bottom) of the clip area:
 *
 *	     ^	X=======================+
 *  extension|	X
 *	     v	X
 *	OOOOOOOOO
 *		O
 *		O
 *	OOOOOOOOO=======================
 *	E
 *	E
 *	E
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May add edges to the queue of edges to be processed.
 *
 * Algorithm notes:
 *	We use plowSrOutline() to trace the penumbra outlines.  Since
 *	it moves counterclockwise around the outline of a collection of
 *	types, this is ideal for following the top penumbra's boundary.
 *	Ideally, we wish to follow the bottom penumbra's boundary in a
 *	clockwise direction instead.  To achieve this, we follow the
 *	outline of the complementary set of types in a counterclockwise
 *	direction.
 *
 * ----------------------------------------------------------------------------
 */

void
prPenumbraTop(edge, rules)
    Edge *edge;		/* Edge being moved */
    PlowRule *rules;	/* Rules to apply (must be non-NULL) */
{
    PlowRule *pr;
    struct applyRule ar;
    Point startPoint;

    ar.ar_moving = edge;
    startPoint.p_x = edge->e_x;
    startPoint.p_y = edge->e_ytop;
    for (pr = rules; pr; pr = pr->pr_next)
    {
	ar.ar_rule = pr;
	ar.ar_clip.p_x = edge->e_newx + pr->pr_dist;
	ar.ar_clip.p_y = edge->e_ytop + pr->pr_dist;
	plowSrOutline(edge->e_pNum, &startPoint, pr->pr_ltypes, GEO_NORTH,
		GMASK_WEST|GMASK_NORTH|GMASK_SOUTH,
		plowPenumbraTopProc, (ClientData) &ar);
    }
}

int
prPenumbraBot(edge, rules)
    Edge *edge;		/* Edge being moved */
    PlowRule *rules;	/* Rules to apply (must be non-NULL) */
{
    TileTypeBitMask insideTypes;
    PlowRule *pr;
    struct applyRule ar;
    Point startPoint;

    ar.ar_moving = edge;
    startPoint.p_x = edge->e_x;
    startPoint.p_y = edge->e_ybot;
    for (pr = rules; pr; pr = pr->pr_next)
    {
	ar.ar_rule = pr;
	ar.ar_clip.p_x = edge->e_newx + pr->pr_dist;
	ar.ar_clip.p_y = edge->e_ybot - pr->pr_dist;
	TTMaskCom2(&insideTypes, &pr->pr_ltypes);
	plowSrOutline(edge->e_pNum, &startPoint, insideTypes, GEO_SOUTH,
		GMASK_WEST|GMASK_NORTH|GMASK_SOUTH,
		plowPenumbraBotProc, (ClientData) &ar);
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowPenumbraTopProc --
 * plowPenumbraBotProc --
 *
 * Process a vertical (GEO_NORTH or GEO_SOUTH) segment of the left-hand
 * boundary of the penumbra, for purposes of applying a width or spacing
 * rule.  Width and spacing rules for the upper part of the penumbra are
 * processed by plowPenumbraTopProc(); rules for the lower part are
 * processed by plowPenumbraBotProc().
 *
 * We expect the following fields of the applyRule struct 'ar' to be filled in:
 *
 *	ar_clip		-- boundary of the penumbra.  If a segment of the
 *			   penumbra's outline extends to the right of this
 *			   boundary point, we stop.  If a segment extends
 *			   above this point (if plowPenumbraTopProc), or
 *			   below this point (if plowPenumbraBotProc), we stop.
 *	ar_moving	-- edge being moved that caused this search.
 *	ar_rule		-- design rule (width or spacing) being applied.
 *
 * When processing the upper penumbra, the "inside" material is to the left,
 * and the "outside" to the right.  When processing the lower penumbra, the
 * situation is reversed because we are following the complementary material:
 * "inside" is to the right and "outside" to the left.  See the algorithm
 * notes for prPenumbraTop() and prPenumbraBot() above.
 *
 * Results:
 *	0 if plowSrOutline should keep going, 1 if not.
 *
 * Side effects:
 *	May add edges to the queue of edges to be processed.
 *
 * ----------------------------------------------------------------------------
 */

int
plowPenumbraTopProc(outline, ar)
    Outline *outline;		/* Segment along penumbra border */
    struct applyRule *ar;	/* Info needed for shadow search */
{
    Edge *movingEdge = ar->ar_moving;
    PlowRule *pr = ar->ar_rule;
    Rect searchArea;
    int ret = 0;

    /*
     * Test for immediate termination conditions:
     *		- a southward-bound edge
     *		- a northward-bound edge to the right of the clip point
     */
    if (outline->o_currentDir == GEO_SOUTH
	    || outline->o_rect.r_xbot >= ar->ar_clip.p_x)
	return (1);

    /*
     * Test for termination after processing this edge:
     *		- a northward-bound edge touching or crossing the top
     *		  of the clip area.
     * If this is true, clip the search area to the top of the clip area.
     */
    searchArea = outline->o_rect;
    if (searchArea.r_ytop >= ar->ar_clip.p_y)
	ret = 1, searchArea.r_ytop = ar->ar_clip.p_y;
    searchArea.r_xtop = movingEdge->e_newx + pr->pr_dist;

    /*
     * If we just turned left and haven't reached the outside of
     * the clip area, extend for one last shadow search.
     */
    if (outline->o_currentDir == GEO_WEST)
    {
	if (outline->o_rect.r_ytop < ar->ar_clip.p_y)
	{
	    searchArea.r_xbot = outline->o_rect.r_xtop - 1;
	    searchArea.r_ybot = outline->o_rect.r_ytop;
	    searchArea.r_ytop = ar->ar_clip.p_y;
	    (void) plowSrShadow(pr->pr_pNum,
		&searchArea, pr->pr_oktypes, plowPenumbraRule,
		(ClientData) ar);
	}
	return (1);
    }

    /* Shadow search to right of this segment of the penumbra boundary */
    (void) plowSrShadow(pr->pr_pNum, &searchArea,
		pr->pr_oktypes, plowApplyRule, (ClientData) ar);
    return (ret);
}

int
plowPenumbraBotProc(outline, ar)
    Outline *outline;		/* Segment along penumbra border */
    struct applyRule *ar;	/* Info needed for shadow search */
{
    Edge *movingEdge = ar->ar_moving;
    PlowRule *pr = ar->ar_rule;
    Rect searchArea;
    int ret = 0;

    /*
     * Test for immediate termination conditions:
     *		- a northward-bound edge
     *		- a southward-bound edge to the right of the clip point
     */
    if (outline->o_currentDir == GEO_NORTH
	    || outline->o_rect.r_xbot >= ar->ar_clip.p_x)
	return (1);

    /*
     * Test for termination after processing this edge:
     *		- a southward-bound edge touching or crossing the bottom
     *		  of the clip area.
     * If this is true, clip the search area to the bottom of the clip area.
     */
    searchArea = outline->o_rect;
    if (searchArea.r_ybot <= ar->ar_clip.p_y)
	ret = 1, searchArea.r_ybot = ar->ar_clip.p_y;
    searchArea.r_xtop = movingEdge->e_newx + pr->pr_dist;

    /*
     * If we just turned left and haven't reached the outside of
     * the clip area, extend for one last shadow search.
     */
    if (outline->o_currentDir == GEO_WEST)
    {
	if (outline->o_rect.r_ybot > ar->ar_clip.p_y)
	{
	    searchArea.r_xbot = outline->o_rect.r_xtop - 1;
	    searchArea.r_ybot = ar->ar_clip.p_y;
	    searchArea.r_ytop = outline->o_rect.r_ybot;
	    (void) plowSrShadow(pr->pr_pNum,
		&searchArea, pr->pr_oktypes, plowPenumbraRule,
		(ClientData) ar);
	}
	return (1);
    }

    /* Shadow search to right of this segment of the penumbra boundary */
    (void) plowSrShadow(pr->pr_pNum, &searchArea,
		pr->pr_oktypes, plowApplyRule, (ClientData) ar);

    return (ret);
}


/*
 * plowPenumbraRule --
 *
 * Like plowApplyRule, except we don't queue an edge whose LHS
 * is not in ar->ar_rule->pr_oktypes.  This should prevent the
 * penumbra extension search from finding bad edges.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	May queue the edge.
 */

int
plowPenumbraRule(impactedEdge, ar)
    Edge *impactedEdge;	/* Edge found by shadow search */
    struct applyRule *ar;	/* Edge causing the shadow search, and
					 * the design rule to apply.
					 */
{
    PlowRule *pr;
    Edge *movingEdge = ar->ar_moving;
    int newsep, oldsep, newx;

    oldsep = impactedEdge->e_x - movingEdge->e_x;
    if (pr = ar->ar_rule)
    {
	if (!TTMaskHasType(&pr->pr_oktypes, impactedEdge->e_ltype))
	    return (0);
	newsep = pr->pr_dist;
    }
    else newsep = 0;
    if (oldsep < newsep)
	newsep = oldsep;

    /* Queue the edge if it hasn't already moved far enough */
    newx = movingEdge->e_newx + newsep;
    if (newx > impactedEdge->e_newx)
    {
	impactedEdge->e_newx = newx;
	(void) (*plowPropagateProcPtr)(impactedEdge);
    }

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * prSliverTop --
 * prSliverBot --
 *
 * Avoid introducing slivers due to width or spacing rule violations.
 * For each design rule in 'rules', we perform two passes.  The first
 * pass determines which slivers must move, and how far they must move.
 * The second pass actually queues the edges to be moved.
 *
 * Results:
 *	Return 0 always
 *
 * Side effects:
 *	May add an edge to the queue of edges to be processed.
 *
 * ----------------------------------------------------------------------------
 */

int
prSliverTop(edge, rules)
    Edge *edge;
    PlowRule *rules;
{
    PlowRule *pr;
    struct applyRule ar;
    Point startPoint;

    /*
     * The rules in the list 'rules' only determine the outline that will be
     * traced, not the rules that will be applied to detect slivers.  Hence
     * the halo will depend on edge->e_ltype and the "sliver type" that will
     * be stored in ar.ar_type.  Since we don't know ar.ar_type at the start,
     * we play conservative and just use the maximum halo size.
     */
    if (plowMaxDist[edge->e_ltype] == 0)
	return 0;
    ar.ar_clip.p_x = edge->e_newx;
    ar.ar_clip.p_y = edge->e_ytop + plowMaxDist[edge->e_ltype];
    startPoint.p_x = edge->e_x;
    startPoint.p_y = edge->e_ytop;
    ar.ar_moving = edge;
    /* We don't use ar.ar_rule */

    for (pr = rules; pr; pr = pr->pr_next)
    {
	/*
	 * Pass 1.
	 * Find out how far slivers will have to move to the right.
	 * After the call to plowSrOutline, ar.ar_mustmove is set to
	 * the distance rightward we will have to move each sliver.
	 */
	ar.ar_slivtype = (TileType) -1;
	ar.ar_lastx = ar.ar_mustmove = edge->e_x;
	plowSrOutline(edge->e_pNum, &startPoint, pr->pr_ltypes, GEO_NORTH,
		GMASK_NORTH|GMASK_EAST|GMASK_SOUTH,
		plowSliverTopExtent, (ClientData) &ar);

	/*
	 * Pass 2.
	 * No work is required if there aren't any potential slivers.
	 * If we must do any work, move each sliver as far rightward
	 * as ar.ar_mustmove.  This second pass doesn't use ar_slivtype,
	 * ar_lastx, or ar_clip.
	 */
	if (ar.ar_mustmove > edge->e_x)
	    plowSrOutline(edge->e_pNum, &startPoint, pr->pr_ltypes, GEO_NORTH,
		    GMASK_SOUTH|GMASK_NORTH,
		    plowSliverTopMove, (ClientData) &ar);
    }
    return 0;
}

int
prSliverBot(edge, rules)
    Edge *edge;
    PlowRule *rules;
{
    TileTypeBitMask insideTypes;
    PlowRule *pr;
    struct applyRule ar;
    Point startPoint;

    /*
     * The rules in the list 'rules' only determine the outline that will be
     * traced, not the rules that will be applied to detect slivers.  Hence
     * the halo will depend on edge->e_ltype and the "sliver type" that will
     * be stored in ar.ar_type.  Since we don't know ar.ar_type at the start,
     * we play conservative and just use the maximum halo size.
     */
    if (plowMaxDist[edge->e_ltype] == 0)
	return 0;
    ar.ar_clip.p_x = edge->e_newx;
    ar.ar_clip.p_y = edge->e_ybot - plowMaxDist[edge->e_ltype];
    startPoint.p_x = edge->e_x;
    startPoint.p_y = edge->e_ybot;
    ar.ar_moving = edge;
    /* We don't use ar.ar_rule */

    for (pr = rules; pr; pr = pr->pr_next)
    {
	/*
	 * Pass 1.
	 * Find out how far slivers will have to move to the right.
	 * After the call to plowSrOutline, ar.ar_mustmove is set to
	 * the distance rightward we will have to move each sliver.
	 */
	ar.ar_slivtype = (TileType) -1;
	ar.ar_lastx = ar.ar_mustmove = edge->e_x;
	TTMaskCom2(&insideTypes, &pr->pr_ltypes);
	plowSrOutline(edge->e_pNum, &startPoint, insideTypes, GEO_SOUTH,
		GMASK_NORTH|GMASK_EAST|GMASK_SOUTH,
		plowSliverBotExtent, (ClientData) &ar);

	/*
	 * Pass 2.
	 * No work is required if there aren't any potential slivers.
	 * If we must do any work, move each sliver as far rightward
	 * as ar.ar_mustmove.  This second pass doesn't use ar_slivtype,
	 * ar_lastx, or ar_clip.
	 */
	if (ar.ar_mustmove > edge->e_x)
	    plowSrOutline(edge->e_pNum, &startPoint, insideTypes, GEO_SOUTH,
		    GMASK_SOUTH|GMASK_NORTH,
		    plowSliverBotMove, (ClientData) &ar);
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowSliverTopMove --
 * plowSliverBotMove --
 *
 * After a first pass through plowSrOutline using plowSliverTopExtent()
 * or plowSliverBotExtent() has filled in ar->ar_mustmove, we get called
 * by a second pass of plowSrOutline to eliminate slivers by moving
 * vertical edges in the outline.
 *
 * All slivers must be moved as far as ar->ar_mustmove.  If we see a vertical
 * edge as far as or farther right than ar->ar_mustmove, we are done.
 * (We know from plowSliverTopExtent() or plowSliverBotExtent() that the final
 * X coordinates of all vertical edges passed to this procedure are
 * monotonically nondecreasing up to ar->ar_mustmove).
 *
 * If the vertical edge is going GEO_SOUTH instead of GEO_NORTH (in the
 * case of plowSliverTopMove), or going GEO_NORTH instead of GEO_SOUTH
 * (in the case of plowSliverBotMove), this is also a stopping condition.
 *
 * Results:
 *	Returns 1 to stop plowSrOutline from following the outline
 *	any more, or 0 to continue.
 *
 * Side effects:
 *	May add an edge to the queue of edges to be processed.
 *
 * ----------------------------------------------------------------------------
 */

int
plowSliverTopMove(outline, ar)
    Outline *outline;		/* Segment of outline being followed */
    struct applyRule *ar;
{
    int howfar = ar->ar_moving->e_newx - ar->ar_moving->e_x;
    Edge edge;

    /* Done if we turned south or if this edge is far enough to the right */
    if (outline->o_currentDir == GEO_SOUTH
	    || TRAILING(outline->o_outside) >= ar->ar_mustmove)
	return (1);

    /* Queue the edge to be moved */
    edge.e_rect = outline->o_rect;
    edge.e_newx = ar->ar_mustmove;
    edge.e_ltype = TiGetTypeExact(outline->o_inside);
    edge.e_rtype = TiGetTypeExact(outline->o_outside);
    if (TTMaskHasType(&PlowFixedTypes, edge.e_rtype)
	    && edge.e_newx > edge.e_x + howfar)
	edge.e_newx = edge.e_x + howfar;
    edge.e_pNum = outline->o_pNum;
    edge.e_use = (CellUse *) NULL;
    edge.e_flags = 0;
    (void) (*plowPropagateProcPtr)(&edge);

    /* Keep going */
    return (0);
}

int
plowSliverBotMove(outline, ar)
    Outline *outline;		/* Segment of outline being followed.
					 * The sense of "inside" and "outside"
					 * is reversed from that when handling
					 * the top half of the penumbra.
					 */
    struct applyRule *ar;
{
    int howfar = ar->ar_moving->e_newx - ar->ar_moving->e_x;
    Edge edge;

    /* Done if we turned north or if this edge is far enough to the right */
    if (outline->o_currentDir == GEO_NORTH
	    || TRAILING(outline->o_inside) >= ar->ar_mustmove)
	return (1);

    /* Queue the edge to be moved */
    edge.e_rect = outline->o_rect;
    edge.e_newx = ar->ar_mustmove;
    edge.e_ltype = TiGetTypeExact(outline->o_outside);
    edge.e_rtype = TiGetTypeExact(outline->o_inside);
    if (TTMaskHasType(&PlowFixedTypes, edge.e_rtype)
	    && edge.e_newx > edge.e_x + howfar)
	edge.e_newx = edge.e_x + howfar;
    edge.e_pNum = outline->o_pNum;
    edge.e_use = (CellUse *) NULL;
    edge.e_flags = 0;
    (void) (*plowPropagateProcPtr)(&edge);

    /* Keep going */
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowSliverTopExtent --
 * plowSliverBotExtent --
 *
 * Called by plowSrOutline() to follow the upper/lower penumbra's
 * outline(s), and see how much of that outline must be moved to
 * avoid introducing slivers.
 *
 * Results:
 *	Returns 1 to stop plowSrOutline from following the outline
 *	any more, or 0 to continue.
 *
 * Side effects:
 *	Modifies the following fields of the applyRule struct pointed
 *	to by 'ar':
 *
 *		ar_lastx	-- continuously updated to the rightmost
 *				   final X coordinate of any vertical
 *				   edge.  When a vertical edge is seen
 *				   with a final X coordinate less than
 *				   ar_lastx, we know we're done.
 *		ar_mustmove	-- new X to which each sliver must be moved.
 *		ar_slivtype	-- material forming the sliver itself.
 *
 * ----------------------------------------------------------------------------
 */

int
plowSliverTopExtent(outline, ar)
    Outline *outline;		/* Segment of outline being followed */
    struct applyRule *ar;
{
    Edge *movingEdge = ar->ar_moving;
    int newx, xmove, ret = 0;

    /*
     * The direction in which we are following an outline
     * segment generally determines how it is processed.
     *
     *	If heading south, we are definitely done.
     *	If heading north, we check (by comparing with ar->ar_lastx)
     *	    to make sure the outline's X coordinate is monotonically
     *	    nondecreating.
     *	If heading east, we check to see if this segment of the outline
     *	    causes a design-rule violation (i.e, if there is a sliver
     *	    between movingEdge->e_ltype and outline->o_inside).
     *
     * A few exceptions arise because we are considering final instead
     * of initial coordinates.  These are both when heading north.  See
     * the code for details.
     */
    switch (outline->o_currentDir)
    {
	case GEO_SOUTH:
	    /* Done if we turn back down */
	    return (1);

	case GEO_NORTH:
	    /* Done if we turned left in final coordinates */
	    if (TRAILING(outline->o_outside) < ar->ar_lastx)
		return (1);

	    /* Almost done if we exit the right or top of the clip area */
	    newx = TRAILING(outline->o_outside);
	    if (newx >= ar->ar_clip.p_x
		    || outline->o_rect.r_ytop >= ar->ar_clip.p_y)
		ret = 1;

	    /* Set the type to be used in applying the design rules */
	    if (outline->o_rect.r_ybot == movingEdge->e_ytop)
		ar->ar_slivtype = TiGetTypeExact(outline->o_outside);

	    /*
	     * If this is not a special case where we have to check for
	     * a sliver, we're done.
	     *
	     * Normally, slivers are only checked for when going east.
	     * There are two exceptions, both because we are looking at
	     * final coordinates.
	     *
	     *	1. If we were going west and then turned north, check
	     *	   for a sliver with the bottom of the inside material.
	     *
	     *	initial | ------------>	| final
	     *		+-------+
	     *			|
	     *
	     *	2. If we were going north and kept going, and this segment
	     *	   is further right than the last one (in final coordinates),
	     *	   check for a sliver with the bottom of the inside material.
	     *
	     *	initial | ------------>	| final
	     *		|
	     */
	    if (outline->o_prevDir == GEO_WEST
		    || (outline->o_prevDir == GEO_NORTH && newx > ar->ar_lastx))
	    {
		ar->ar_lastx = newx;
		xmove = MIN(newx, ar->ar_clip.p_x);
		break;
	    }

	    /* We know newx >= ar->ar_lastx, so update the latter */
	    ar->ar_lastx = newx;
	    return (ret);

	case GEO_EAST:
	    /* Almost done if we leave the clipping area */
	    if (outline->o_rect.r_xtop >= ar->ar_clip.p_x)
		ret = 1;

	    /*
	     * Return if we haven't yet processed a vertical segment of
	     * the outline (i.e, no potential slivers have been seen yet).
	     */
	    if (ar->ar_slivtype == (TileType) -1)
		return (ret);

	    /* How far the penumbra LHS will have to move to avoid a sliver */
	    xmove = ar->ar_clip.p_x;
	    if (outline->o_nextDir == GEO_NORTH
		    && TRAILING(outline->o_nextOut) < xmove)
		xmove = TRAILING(outline->o_nextOut);
	    break;
    }

    /* Apply the plowing rules to see if this is a sliver */
    if (plowSliverApplyRules(ar, TiGetTypeExact(outline->o_inside),
		outline->o_rect.r_ybot - movingEdge->e_ytop))
	ar->ar_mustmove = xmove;
    return (ret);
}

int
plowSliverBotExtent(outline, ar)
    Outline *outline;		/* Segment of outline being followed */
    struct applyRule *ar;
{
    Edge *movingEdge = ar->ar_moving;
    int newx, xmove, ret = 0;

    /*
     * The direction in which we are following an outline
     * segment generally determines how it is processed.
     *
     *	If heading north, we are definitely done.
     *	If heading south, we check (by comparing with ar->ar_lastx)
     *	    to make sure the outline's X coordinate is monotonically
     *	    nondecreating.
     *	If heading east, we check to see if this segment of the outline
     *	    causes a design-rule violation (i.e, if there is a sliver
     *	    between movingEdge->e_ltype and outline->o_outside).
     *
     * A few exceptions arise because we are considering final instead
     * of initial coordinates.  These are both when heading south.  See
     * the code for details.
     */
    switch (outline->o_currentDir)
    {
	case GEO_NORTH:
	    /* Done if we turn back up */
	    return (1);

	case GEO_SOUTH:
	    /* Done if we turned left in final coordinates */
	    if (TRAILING(outline->o_inside) < ar->ar_lastx)
		return (1);

	    /* Almost done if we exit the right or bottom of the clip area */
	    newx = TRAILING(outline->o_inside);
	    if (newx >= ar->ar_clip.p_x
		    || outline->o_rect.r_ybot <= ar->ar_clip.p_y)
		ret = 1;

	    /* Set the type to be used in applying the design rules */
	    if (outline->o_rect.r_ytop == movingEdge->e_ybot)
		ar->ar_slivtype = TiGetTypeExact(outline->o_inside);

	    /*
	     * If this is not a special case where we have to check for
	     * a sliver, we're done.
	     *
	     * Normally, slivers are only checked for when going east.
	     * There are two exceptions, both because we are looking at
	     * final coordinates.
	     *
	     *	1. If we were going west and then turned south, check
	     *	   for a sliver with the top of the outside material.
	     *
	     *			|
	     *		+-------+
	     *	initial | ------------>	| final
	     *
	     *	2. If we were going south and kept going, and this segment
	     *	   is further right than the last one (in final coordinates),
	     *	   check for a sliver with the top of the outside material.
	     *
	     *		|
	     *	initial | ------------>	| final
	     */
	    if (outline->o_prevDir == GEO_WEST
		    || (outline->o_prevDir == GEO_SOUTH && newx > ar->ar_lastx))
	    {
		ar->ar_lastx = newx;
		xmove = MIN(newx, ar->ar_clip.p_x);
		break;
	    }

	    /* We know newx >= ar->ar_lastx, so update the latter */
	    ar->ar_lastx = newx;
	    return (ret);

	case GEO_EAST:
	    /* Almost done if we leave the clipping area */
	    if (outline->o_rect.r_xtop >= ar->ar_clip.p_x)
		ret = 1;

	    /*
	     * Return if we haven't yet processed a vertical segment of
	     * the outline (i.e, no potential slivers have been seen yet).
	     */
	    if (ar->ar_slivtype == (TileType) -1)
		return (ret);

	    /* How far the penumbra LHS will have to move to avoid a sliver */
	    xmove = ar->ar_clip.p_x;
	    if (outline->o_nextDir == GEO_SOUTH
		    && TRAILING(outline->o_nextIn) < xmove)
		xmove = TRAILING(outline->o_nextIn);
	    break;
    }

    /* Apply the plowing rules to see if this is a sliver */
    if (plowSliverApplyRules(ar, TiGetTypeExact(outline->o_outside),
		movingEdge->e_ybot - outline->o_rect.r_ytop))
	ar->ar_mustmove = xmove;
    return (ret);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowSliverApplyRules --
 *
 * Determine whether a sliver exists that must be moved.  The configuration
 * we are evaluating is:
 *
 *
 *	   far
 *	--------
 *		  ^
 *		  |
 *		  | farDist
 *		  |
 *	    s	  v
 *	--------
 *	  near
 *
 * where s = ar->ar_slivtype and near = ar->ar_moving->e_ltype.
 * We apply all the plowing rules triggered by the near|s edge
 * (both width and spacing rules).
 *
 * Results:
 *	TRUE if the sliver must move, FALSE if not.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
plowSliverApplyRules(ar, far, farDist)
    struct applyRule *ar;
    TileType far;
    int farDist;
{
    TileType near = ar->ar_moving->e_ltype;
    PlowRule *pr;

    for (pr = plowWidthRulesTbl[near][ar->ar_slivtype]; pr; pr = pr->pr_next)
	if (pr->pr_dist > farDist && !TTMaskHasType(&pr->pr_oktypes, far))
	    return (TRUE);

    for (pr = plowSpacingRulesTbl[near][ar->ar_slivtype]; pr; pr = pr->pr_next)
	if (pr->pr_dist > farDist && !TTMaskHasType(&pr->pr_oktypes, far))
	    return (TRUE);
    return (FALSE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowApplyRule --
 *
 * Add an edge ('impactedEdge') found by shadow search to the edge queue.
 * Normally, this edge will be added at a new position equal to the new
 * position of the original moving edge (ar->ar_moving), plus the distance
 * associated with the design rule (ar->ar_rule->pr_dist).  However, if
 * the impacted edge were already closer than ar->ar_rule->pr_dist to the
 * moving edge (i.e, there was a design-rule violation in the original
 * layout), we use the original separation instead of the minimum separation.
 *
 * If the impacted edge has already moved far enough (impactedEdge->e_newx
 * is far enough to the right), we don't add it.
 *
 * If ar->ar_rule is NULL, we assume a distance of zero.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	May add an edge to the queue of edges to be processed.
 *
 * ----------------------------------------------------------------------------
 */

int
plowApplyRule(impactedEdge, ar)
    Edge *impactedEdge;	/* Edge found by shadow search */
    struct applyRule *ar;	/* Edge causing the shadow search, and
					 * the design rule to apply.
					 */
{
    Edge *movingEdge = ar->ar_moving;
    int newsep, oldsep, newx;

    oldsep = impactedEdge->e_x - movingEdge->e_x;
    newsep = ar->ar_rule ? ar->ar_rule->pr_dist : 0;
    if (oldsep < newsep)
	newsep = oldsep;

    /* Queue the edge if it hasn't already moved far enough */
    newx = movingEdge->e_newx + newsep;
    if (newx > impactedEdge->e_newx)
    {
	impactedEdge->e_newx = newx;
	(void) (*plowPropagateProcPtr)(impactedEdge);
    }

    return (0);
}
