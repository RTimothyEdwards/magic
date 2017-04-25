/*
 * PlowRules2.c --
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/plow/PlowRules2.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
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

/* Imports from other rules files */
extern int plowApplyRule();

/* Forward declarations */
int plowFoundCell();
int plowCellDragPaint(), plowCellPushPaint();
int plowCoverTopProc(), plowCoverBotProc();
int plowIllegalTopProc(), plowIllegalBotProc();
int plowDragEdgeProc();

/*
 * ----------------------------------------------------------------------------
 *
 * prFixedLHS --
 * prFixedRHS --
 *
 * The type on the LHS or RHS of an edge is a fixed-width.
 * Make sure that the opposite edge of the tile also moves.
 * When processing an edge whose RHS is fixed-width, we also
 * walk along the top and bottom of each tilef making sure all
 * fixed-width tiles move by the same amount.  This is necessary
 * in order to preserve transistor geometries.
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
prFixedLHS(edge)
    Edge *edge;			/* Edge being moved */
{
    int distance = edge->e_newx - edge->e_x;
    Tile *tpL;
    Point startPoint;
    Rect atomRect;
    Plane *plane;

restart:
    startPoint.p_x = edge->e_x - 1;
    startPoint.p_y = edge->e_ybot;
    plane = plowYankDef->cd_planes[edge->e_pNum];
    for (tpL = TiSrPointNoHint(plane, &startPoint);
	    BOTTOM(tpL) < edge->e_ytop;
	    tpL = RT(tpL))
    {
	/* Add the entire LHS of each of the tiles comprising this edge */
	atomRect.r_xbot = LEFT(tpL);
	atomRect.r_xtop = LEFT(tpL) + distance;
	atomRect.r_ybot = BOTTOM(tpL);
	atomRect.r_ytop = TOP(tpL);
	if (plowYankMore(&atomRect, 1, 1))
	    goto restart;

	/* Only queue if it hasn't already moved far enough */
	if (TRAILING(tpL) < LEFT(tpL) + distance)
	    (void) plowAtomize(edge->e_pNum, &atomRect,
			    plowPropagateProcPtr, (ClientData) NULL);
    }
}

int
prFixedRHS(edge)
    Edge *edge;			/* Edge being moved */
{
    int distance = edge->e_newx - edge->e_x;
    Tile *tpR, *tp;
    Point startPoint;
    Rect atomRect;
    Plane *plane;

restart:
    startPoint.p_x = edge->e_x;
    startPoint.p_y = edge->e_ytop - 1;
    plane = plowYankDef->cd_planes[edge->e_pNum];
    tpR = TiSrPointNoHint(plane, &startPoint);

    /* Move the RHS of all the tiles comprising this edge */
    for ( ; TOP(tpR) > edge->e_ybot; tpR = LB(tpR))
    {
	/* Queue the RHS of this tile */
	atomRect.r_xbot = RIGHT(tpR);
	atomRect.r_xtop = RIGHT(tpR) + distance;
	atomRect.r_ybot = BOTTOM(tpR);
	atomRect.r_ytop = TOP(tpR);
	if (plowYankMore(&atomRect, 1, 1))
	    goto restart;

	    /* Only queue RHS if it hasn't already moved far enough */
	if (LEADING(tpR) < RIGHT(tpR) + distance)
	    (void) plowAtomize(edge->e_pNum, &atomRect,
			plowPropagateProcPtr, (ClientData) NULL);

	/* Move all fixed-width tiles along the top */
	for (tp = RT(tpR); RIGHT(tp) > LEFT(tpR); tp = BL(tp))
	{
	    if (TTMaskHasType(&PlowFixedTypes, TiGetTypeExact(tp)))
	    {
		atomRect.r_xbot = LEFT(tp);
		atomRect.r_xtop = LEFT(tp) + distance;
		atomRect.r_ybot = BOTTOM(tp);
		atomRect.r_ytop = TOP(tp);
		if (plowYankMore(&atomRect, 1, 1))
		    goto restart;

		/* Only queue if it hasn't moved far enough */
		if (TRAILING(tp) < LEFT(tp) + distance)
		    (void) plowAtomize(edge->e_pNum, &atomRect,
				plowPropagateProcPtr, (ClientData) NULL);
	    }
	}

	/* Move all fixed-width tiles along the bottom */
	for (tp = LB(tpR); LEFT(tp) < RIGHT(tpR); tp = TR(tp))
	{
	    if (TTMaskHasType(&PlowFixedTypes, TiGetTypeExact(tp)))
	    {
		atomRect.r_xbot = LEFT(tp);
		atomRect.r_xtop = LEFT(tp) + distance;
		atomRect.r_ybot = BOTTOM(tp);
		atomRect.r_ytop = TOP(tp);
		if (plowYankMore(&atomRect, 1, 1))
		    goto restart;

		/* Only queue if it hasn't moved far enough */
		if (TRAILING(tp) < LEFT(tp) + distance)
		    (void) plowAtomize(edge->e_pNum, &atomRect,
			    plowPropagateProcPtr, (ClientData) NULL);
	    }
	}
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * prFixedPenumbraTop --
 * prFixedPenumbraBot --
 *
 * When the RHS material is fixed-width and no spacing rules apply
 * across the edge, these rules get applied.  The case handled is
 * the following (E is the edge):
 *
 *		OOOOOOOOOOOOOOOOO
 *		O		O
 *		O ------------>	O
 *	    top	O		O
 *		OOOOOOOOOOOOOOOOO
 *	=========
 *		E
 *	  ltype	E rtype ------>
 *		E
 *
 * where spacing rules DO apply across the ltype -- top edge.
 * For each such spacing rule, we search the area O above, where
 * the height of O is the distance of the spacing rule.  All
 * edges not in the 'oktypes' for the spacing rule are moved as
 * far as the edge E.
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
prFixedPenumbraTop(edge)
    Edge *edge;	/* Edge being moved */
{
    struct applyRule ar;
    PlowRule *pr;
    Tile *tp;
    Rect searchRect;
    Point p;

    p.p_x = edge->e_x - 1;
    p.p_y = edge->e_ytop;
    tp = TiSrPointNoHint(plowYankDef->cd_planes[edge->e_pNum], &p);
    pr = plowSpacingRulesTbl[edge->e_ltype][TiGetTypeExact(tp)];
    if (pr == (PlowRule *) NULL)
	return;

    searchRect.r_xbot = edge->e_x - 1;
    searchRect.r_ybot = edge->e_ytop;
    searchRect.r_xtop = edge->e_newx;
    ar.ar_rule = (PlowRule *) NULL;
    ar.ar_moving = edge;
    for ( ; pr; pr = pr->pr_next)
    {
	searchRect.r_ytop = edge->e_ytop + pr->pr_dist;
	(void) plowSrShadow(pr->pr_pNum, &searchRect, pr->pr_oktypes,
		plowApplyRule, (ClientData) &ar);
    }
}

int
prFixedPenumbraBot(edge)
    Edge *edge;	/* Edge being moved */
{
    struct applyRule ar;
    PlowRule *pr;
    Tile *tp;
    Rect searchRect;
    Point p;

    p.p_x = edge->e_x - 1;
    p.p_y = edge->e_ybot - 1;
    tp = TiSrPointNoHint(plowYankDef->cd_planes[edge->e_pNum], &p);
    pr = plowSpacingRulesTbl[edge->e_ltype][TiGetTypeExact(tp)];
    if (pr == (PlowRule *) NULL)
	return 0;

    searchRect.r_xbot = edge->e_x - 1;
    searchRect.r_ytop = edge->e_ybot;
    searchRect.r_xtop = edge->e_newx;
    ar.ar_rule = (PlowRule *) NULL;
    ar.ar_moving = edge;
    for ( ; pr; pr = pr->pr_next)
    {
	searchRect.r_ybot = edge->e_ybot - pr->pr_dist;
	(void) plowSrShadow(pr->pr_pNum, &searchRect, pr->pr_oktypes,
		plowApplyRule, (ClientData) &ar);
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * prFixedDragStubs --
 *
 * The type on the RHS of an edge is a fixed-width, and the type on the
 * LHS is neither fixed-width nor space.  Our job is to drag alone the
 * left-hand side of the LHS tiles if they are minimum-width or less.
 *
 * The purpose of this rule is mainly to prevent transistors from
 * leaving their gates trailing behind.
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
prFixedDragStubs(edge)
    Edge *edge;	/* Edge being moved; RHS is fixed-width */
{
    int distance = edge->e_newx - edge->e_x;
    Tile *tpL;
    Point startPoint;
    Rect atomRect;
    Plane *plane;

restart:
    startPoint.p_x = edge->e_x - 1;
    startPoint.p_y = edge->e_ybot;
    plane = plowYankDef->cd_planes[edge->e_pNum];
    for (tpL = TiSrPointNoHint(plane, &startPoint);
	    BOTTOM(tpL) < edge->e_ytop;
	    tpL = RT(tpL))
    {
	/*
	 * Add the entire LHS of each of the tiles comprising this edge,
	 * if the LHS is minimum-width and exposed to space.
	 */
	atomRect.r_xbot = LEFT(tpL);
	atomRect.r_xtop = LEFT(tpL) + distance;
	atomRect.r_ybot = BOTTOM(tpL);
	atomRect.r_ytop = TOP(tpL);
	if (plowYankMore(&atomRect, 1, 1))
	    goto restart;

	if (TRAILING(tpL) < atomRect.r_xtop)
	    (void) plowAtomize(edge->e_pNum, &atomRect,
			plowDragEdgeProc, (ClientData) edge);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowDragEdgeProc --
 *
 * Called for each segment along the LHS of the tiles processed by
 * prFixedDragStubs() above.  If lhsEdge->e_ltype is space, and
 * lhsEdge is the minimum-distance from movingEdge, we queue it
 * to move by the same amount as movingEdge.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	May add edges to the queue of edges to be processed.
 *
 * ----------------------------------------------------------------------------
 */

int
plowDragEdgeProc(lhsEdge, movingEdge)
    Edge *lhsEdge;	/* Edge on LHS; the caller has already
				 * determined that this edge has not
				 * already moved far enough.
				 */
    Edge *movingEdge;	/* RHS of this edge is fixed-width */
{
    PlowRule *pr;
    int xsep, width;

    /* Don't move the edge if it isn't to space */
    if (lhsEdge->e_ltype != TT_SPACE)
	return (0);

    /* Don't bother doing any more work if it's too far to the left */
    if (lhsEdge->e_x + DRCTechHalo < movingEdge->e_x)
	return (0);

    /*
     * Try to guess at minimum width.
     * Then, if lhsEdge is less than minimum width to the left
     * of movingEdge, we queue lhsEdge to move by the same
     * amount.
     */
    width = INFINITY;

	/* Apply width rules from lhsEdge rightward */
    for (pr = plowWidthRulesTbl[lhsEdge->e_ltype][lhsEdge->e_rtype];
		pr; pr = pr->pr_next)
	width = MIN(width, pr->pr_dist);

	/* Apply spacing rules from movingEdge leftward */
    for (pr = plowSpacingRulesTbl[movingEdge->e_rtype][movingEdge->e_ltype];
		pr; pr = pr->pr_next)
	if (!TTMaskHasType(&pr->pr_oktypes, TT_SPACE))
	    width = MIN(width, pr->pr_dist);

	/* No width, so assume we don't move it */
    if (width == INFINITY)
	return (0);

	/* Only move if minimum width or less */
    xsep = movingEdge->e_x - lhsEdge->e_x;
    if (xsep <= width)
	(*plowPropagateProcPtr)(lhsEdge);

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * prContactLHS --
 * prContactRHS --
 *
 * The type on the LHS or RHS of an edge is a contact.
 * Couple to each of the planes connected by the contact.
 * Contacts must be fixed-width.
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
prContactLHS(edge)
    Edge *edge;	/* Edge being moved (LHS is contact) */
{
    int pNum, pMax;
    PlaneMask connPlanes = DBConnPlanes[edge->e_ltype];

    /* Add the edges of the contact on its other planes */
    connPlanes &= ~(edge->e_pNum);
    pMax = DBPlane(edge->e_ltype) + 1;
    for (pNum = pMax - 2; pNum <= pMax; pNum++)
	if (PlaneMaskHasPlane(connPlanes, pNum))
	    (void) plowAtomize(pNum, &edge->e_rect,
				plowPropagateProcPtr, (ClientData) NULL);
}

int
prContactRHS(edge)
    Edge *edge;	/* Edge being moved (RHS is contact) */
{
    int pNum, pMax;
    PlaneMask connPlanes = DBConnPlanes[edge->e_rtype];

    /* Add the edges of the contact on its other planes */
    connPlanes &= ~(edge->e_pNum);
    pMax = DBPlane(edge->e_ltype) + 1;
    for (pNum = pMax - 2; pNum <= pMax; pNum++)
	if (PlaneMaskHasPlane(connPlanes, pNum))
	    (void) plowAtomize(pNum, &edge->e_rect,
				plowPropagateProcPtr, (ClientData) NULL);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * prCoverTop --
 * prCoverBot --
 *
 * SEARCH RULE.
 * The material on the LHS of this edge must always stay covered.
 * To insure this, we drag along the material attached to its top
 * right and bottom right corners.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May add edges to the queue of edges to be moved.
 *
 * ----------------------------------------------------------------------------
 */

void
prCoverTop(edge)
    Edge *edge;	/* Edge being moved */
{
    TileType ltype, rtype;
    PlowRule *pr;
    Tile *tp;
    struct applyRule ar;
    Point startPoint;
    Rect searchArea;

    startPoint.p_x = edge->e_x - 1;
    startPoint.p_y = edge->e_ytop;
    tp = TiSrPointNoHint(plowYankDef->cd_planes[edge->e_pNum], &startPoint);
    if (TiGetTypeExact(tp) == TT_SPACE)
	return;

    ltype = edge->e_ltype;
    rtype = TiGetTypeExact(tp);
    ar.ar_moving = edge;
    ar.ar_rule = (PlowRule *) NULL;
    searchArea.r_xbot = edge->e_x - 1;
    searchArea.r_xtop = edge->e_newx;
    searchArea.r_ybot = edge->e_ytop;
    for (pr = plowWidthRulesTbl[ltype][rtype]; pr; pr = pr->pr_next)
    {
	searchArea.r_ytop = edge->e_ytop + pr->pr_dist;
	(void) plowSrShadow(edge->e_pNum, &searchArea, pr->pr_oktypes,
		plowApplyRule, (ClientData) &ar);
    }
    for (pr = plowSpacingRulesTbl[ltype][rtype]; pr; pr = pr->pr_next)
    {
	searchArea.r_ytop = edge->e_ytop + pr->pr_dist;
	(void) plowSrShadow(edge->e_pNum, &searchArea, pr->pr_oktypes,
		plowApplyRule, (ClientData) &ar);
    }
}

int
prCoverBot(edge)
    Edge *edge;	/* Edge being moved */
{
    TileType ltype, rtype;
    PlowRule *pr;
    Tile *tp;
    struct applyRule ar;
    Point startPoint;
    Rect searchArea;

    startPoint.p_x = edge->e_x - 1;
    startPoint.p_y = edge->e_ybot - 1;
    tp = TiSrPointNoHint(plowYankDef->cd_planes[edge->e_pNum], &startPoint);
    if (TiGetTypeExact(tp) == TT_SPACE)
	return 0;

    ltype = edge->e_ltype;
    rtype = TiGetTypeExact(tp);
    ar.ar_moving = edge;
    ar.ar_rule = (PlowRule *) NULL;
    searchArea.r_xbot = edge->e_x - 1;
    searchArea.r_xtop = edge->e_newx;
    searchArea.r_ytop = edge->e_ybot;
    for (pr = plowWidthRulesTbl[ltype][rtype]; pr; pr = pr->pr_next)
    {
	searchArea.r_ybot = edge->e_ybot - pr->pr_dist;
	(void) plowSrShadow(edge->e_pNum, &searchArea, pr->pr_oktypes,
		plowApplyRule, (ClientData) &ar);
    }
    for (pr = plowSpacingRulesTbl[ltype][rtype]; pr; pr = pr->pr_next)
    {
	searchArea.r_ybot = edge->e_ybot - pr->pr_dist;
	(void) plowSrShadow(edge->e_pNum, &searchArea, pr->pr_oktypes,
		plowApplyRule, (ClientData) &ar);
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * prIllegalTop --
 * prIllegalBot --
 *
 * Insure that the material on the LHS of 'edge' doesn't form an illegal
 * edge with material to its top or bottom, as it slides to the right.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May add edges to the queue of edges to be moved.
 *
 * ----------------------------------------------------------------------------
 */

void
prIllegalTop(edge)
    Edge *edge;
{
    TileTypeBitMask insideTypes;
    struct applyRule ar;
    Point startPoint;

    ar.ar_moving = edge;
    startPoint.p_x = edge->e_x;
    startPoint.p_y = edge->e_ytop;
    TTMaskSetOnlyType(&insideTypes, edge->e_rtype);
    TTMaskCom(&insideTypes);
    ar.ar_slivtype = (TileType) -1;
    ar.ar_clip.p_x = edge->e_newx;

    plowSrOutline(edge->e_pNum, &startPoint, insideTypes, GEO_NORTH,
		GMASK_EAST|GMASK_WEST|GMASK_NORTH|GMASK_SOUTH,
		plowIllegalTopProc, (ClientData) &ar);
    if (ar.ar_slivtype == (TileType) -1)
	return;

    startPoint.p_x = ar.ar_mustmove;
    TTMaskSetOnlyType(&insideTypes, ar.ar_slivtype);
    TTMaskCom(&insideTypes);
    plowSrOutline(edge->e_pNum, &startPoint, insideTypes, GEO_NORTH,
		GMASK_WEST|GMASK_NORTH|GMASK_SOUTH,
		plowCoverTopProc, (ClientData) &ar);
}

int
prIllegalBot(edge)
    Edge *edge;
{
    TileTypeBitMask insideTypes;
    struct applyRule ar;
    Point startPoint;

    ar.ar_moving = edge;
    startPoint.p_x = edge->e_x;
    startPoint.p_y = edge->e_ybot;
    TTMaskSetOnlyType(&insideTypes, edge->e_rtype);
    ar.ar_slivtype = (TileType) -1;
    ar.ar_clip.p_x = edge->e_newx;

    plowSrOutline(edge->e_pNum, &startPoint, insideTypes, GEO_SOUTH,
		GMASK_EAST|GMASK_WEST|GMASK_NORTH|GMASK_SOUTH,
		plowIllegalBotProc, (ClientData) &ar);
    if (ar.ar_slivtype == (TileType) -1)
	return 0;

    startPoint.p_x = ar.ar_mustmove;
    TTMaskSetOnlyType(&insideTypes, ar.ar_slivtype);
    plowSrOutline(edge->e_pNum, &startPoint, insideTypes, GEO_SOUTH,
		GMASK_WEST|GMASK_NORTH|GMASK_SOUTH,
		plowCoverBotProc, (ClientData) &ar);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowCoverTopProc --
 * plowCoverBotProc --
 *
 * Called by plowSrOutline() on behalf of prIllegalTop() or prIllegalBot()
 * above.  Move all vertical edges found along the outline being searched
 * until we reach either ar->ar_clip.p_x in the X direction, or ar->ar_clip.p_y
 * in the Y direction, or turn left.
 *
 * Results:
 *	Returns 0 if we will keep going, or 1 if any of the termination
 *	conditions above are met.
 *
 * Side effects:
 *	May add edges to the queue of edges to be moved.
 *
 * ----------------------------------------------------------------------------
 */

int
plowCoverTopProc(outline, ar)
    Outline *outline;
    struct applyRule *ar;
{
    Edge edge;
    int ret = 0;

    /* Done if not headed north */
    if (outline->o_currentDir != GEO_NORTH)
	return (1);

    /* Done if outside of the clip area */
    if (outline->o_rect.r_xbot >= ar->ar_clip.p_x)
	return (1);

    /* Done after this time if we touch the clip area */
    edge.e_rect = outline->o_rect;
    if (edge.e_ytop >= ar->ar_clip.p_y)
	edge.e_ytop = ar->ar_clip.p_y, ret = 1;

    if (edge.e_ytop > edge.e_ybot
	    && TRAILING(outline->o_outside) < ar->ar_moving->e_newx)
    {
	edge.e_newx = ar->ar_moving->e_newx;
	edge.e_pNum = ar->ar_moving->e_pNum;
	edge.e_use = (CellUse *) NULL;
	edge.e_flags = 0;
	edge.e_ltype = TiGetTypeExact(outline->o_inside);
	edge.e_rtype = TiGetTypeExact(outline->o_outside);
	(void) (*plowPropagateProcPtr)(&edge);
    }

    return (ret);
}

int
plowCoverBotProc(outline, ar)
    Outline *outline;
    struct applyRule *ar;
{
    Edge edge;
    int ret = 0;

    /* Done if not headed south */
    if (outline->o_currentDir != GEO_SOUTH)
	return (1);

    /* Done if outside of the clip area */
    if (outline->o_rect.r_xbot >= ar->ar_clip.p_x)
	return (1);

    /* Done after this time if we touch the clip area */
    edge.e_rect = outline->o_rect;
    if (edge.e_ybot <= ar->ar_clip.p_y)
	edge.e_ybot = ar->ar_clip.p_y, ret = 1;

    if (edge.e_ytop > edge.e_ybot
	    && TRAILING(outline->o_inside) < ar->ar_moving->e_newx)
    {
	edge.e_newx = ar->ar_moving->e_newx;
	edge.e_pNum = ar->ar_moving->e_pNum;
	edge.e_use = (CellUse *) NULL;
	edge.e_flags = 0;
	edge.e_ltype = TiGetTypeExact(outline->o_outside);
	edge.e_rtype = TiGetTypeExact(outline->o_inside);
	(void) (*plowPropagateProcPtr)(&edge);
    }

    return (ret);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowIllegalTopProc --
 * plowIllegalBotProc --
 *
 * Called by plowSrOutline() on behalf of prIllegalTop() and prIllegalBot()
 * above.  We walk along the outline of the RHS of the edge ar->ar_moving.
 * If the outline turns up, down, or west, we're done.  If the outline
 * segment is to the right of ar->ar_clip.p_x, we're also done.  Otherwise,
 * we keep going until the type to the top/bottom of the outline is one
 * that cannot be adjacent to ar->ar_moving->e_ltype.  At this point,
 * set ar->ar_slivtype to this type and ar->ar_mustmove to the LHS of this
 * segment.  Set ar->ar_clip.p_y to 'width' above the top of the edge
 * ar->ar_moving or below its bottom, where 'width' is the minimum spacing
 * between the LHS material of ar->ar_moving and the illegal type.
 *
 * Results:
 *	Returns 0 to continue, or 1 if the above termination conditions
 *	are met.
 *
 * Side effects:
 *	Sets ar_slivtype and ar_clip.p_y.
 *
 * ----------------------------------------------------------------------------
 */

int
plowIllegalTopProc(outline, ar)
    Outline *outline;
    struct applyRule *ar;
{
    TileType badType = TiGetTypeExact(outline->o_inside), leftType;
    Edge *movingEdge = ar->ar_moving;
    DRCCookie *dp;
    PlowRule *pr;
    int width;

    if (outline->o_currentDir != GEO_EAST
	    || outline->o_rect.r_xbot >= ar->ar_clip.p_x)
	return (1);

    /* Ignore if this is a legal edge */
    for (dp = DRCCurStyle->DRCRulesTbl[movingEdge->e_ltype][badType];
	    dp; dp = dp->drcc_next)
    {
	if (!TTMaskHasType(&dp->drcc_mask, badType))
	    goto found_bad;
    }
    return (0);

    /* Found a bad type */
found_bad:

    /* Don't do anything if there was already a design-rule violation */
    if (LEFT(outline->o_inside) < movingEdge->e_x)
	return (0);

    ar->ar_slivtype = badType;
    ar->ar_mustmove = outline->o_rect.r_xbot;
    leftType = TiGetTypeExact(BL(outline->o_inside));
    width = 1;
    for (pr = plowSpacingRulesTbl[movingEdge->e_ltype][leftType];
	    pr; pr = pr->pr_next)
    {
	if (!TTMaskHasType(&pr->pr_oktypes, badType))
	    width = MAX(width, pr->pr_dist);
    }
    ar->ar_clip.p_y = movingEdge->e_ytop + width;
    return (1);
}

int
plowIllegalBotProc(outline, ar)
    Outline *outline;
    struct applyRule *ar;
{
    TileType badType = TiGetTypeExact(outline->o_outside), leftType;
    Edge *movingEdge = ar->ar_moving;
    DRCCookie *dp;
    PlowRule *pr;
    Tile *tp;
    int width;

    if (outline->o_currentDir != GEO_EAST
	    || outline->o_rect.r_xbot >= ar->ar_clip.p_x)
	return (1);

    /* Ignore if this is a legal edge */
    for (dp = DRCCurStyle->DRCRulesTbl[movingEdge->e_ltype][badType];
	    dp; dp = dp->drcc_next)
    {
	if (!TTMaskHasType(&dp->drcc_mask, badType))
	    goto found_bad;
    }
    return (0);

    /* Found a bad type */
found_bad:

    /* Don't do anything if there was already a design-rule violation */
    if (LEFT(outline->o_outside) < movingEdge->e_x)
	return (0);

    ar->ar_slivtype = badType;
    ar->ar_mustmove = outline->o_rect.r_xbot;
    for (tp = BL(outline->o_outside);
	    TOP(tp) < outline->o_rect.r_ybot; tp = RT(tp))
	/* Nothing */;
    leftType = TiGetTypeExact(tp);

    width = 1;
    for (pr = plowSpacingRulesTbl[movingEdge->e_ltype][leftType];
	    pr; pr = pr->pr_next)
    {
	if (!TTMaskHasType(&pr->pr_oktypes, badType))
	    width = MAX(width, pr->pr_dist);
    }
    ar->ar_clip.p_y = movingEdge->e_ybot - width;
    return (1);
}

/*
 * ----------------------------------------------------------------------------
 *
 * prFindCells --
 *
 * Find any cells within a DRC halo of the swath cut by the edge 'edge'
 * as it moves, and move them.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May add cells to the queue of edges to be processed.
 *
 * ----------------------------------------------------------------------------
 */

void
prFindCells(edge)
    Edge *edge;	/* Edge being moved */
{
    Plane *cellPlane = plowYankDef->cd_planes[PL_CELL];
    Tile *cellTile = cellPlane->pl_hint;
    struct applyRule ar;
    Rect searchArea;

    searchArea.r_xbot = edge->e_x - 1;
    searchArea.r_ybot = edge->e_ybot - DRCTechHalo;
    searchArea.r_ytop = edge->e_ytop + DRCTechHalo;
    searchArea.r_xtop = edge->e_newx + DRCTechHalo;
    ar.ar_moving = edge;

    /*
     * Don't bother doing anything if there is a single space tile
     * beneath the plow.
     */
    if (TiGetBody(cellTile) == (ClientData) NULL
	    && LEFT(cellTile) <= searchArea.r_xbot
	    && BOTTOM(cellTile) <= searchArea.r_ybot
	    && RIGHT(cellTile) >= searchArea.r_xtop
	    && TOP(cellTile) >= searchArea.r_ytop)
	return;

    (void) TiSrArea(cellTile, cellPlane, &searchArea, plowFoundCell,
		(ClientData) &ar);
}

/*
 * ----------------------------------------------------------------------------
 *
 * prCell --
 *
 * The Edge 'edge' corresponds to a cell that is moving.  Search the
 * area of the cell plus a DRCTechHalo around it for geometry to move,
 * and queue each edge found.  Also, search to its right for other
 * cells to move and move them.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May add cells or edges to the queue of edges to be processed.
 *
 * ----------------------------------------------------------------------------
 */

void
prCell(edge)
    Edge *edge;			/* Cell edge being moved */
{
    Rect cellArea, shadowArea;
    CellUse *use = edge->e_use;
    struct applyRule ar;
    int pNum;

    ar.ar_moving = edge;

    /* Search area for paint to drag */
    ar.ar_search.r_xbot = use->cu_bbox.r_xbot - 1;
    ar.ar_search.r_xtop = use->cu_bbox.r_xtop + DRCTechHalo;
    ar.ar_search.r_ybot = edge->e_ybot - DRCTechHalo;
    ar.ar_search.r_ytop = edge->e_ytop + DRCTechHalo;

    /* Shadow search area for the paint planes */
    shadowArea.r_xbot = edge->e_x - 1;
    shadowArea.r_xtop = edge->e_newx + DRCTechHalo;
    shadowArea.r_ybot = edge->e_ybot - DRCTechHalo;
    shadowArea.r_ytop = edge->e_ytop + DRCTechHalo;

    /* Search all the paint planes */
    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
    {
	ar.ar_pNum = pNum;
	(void) DBSrPaintArea((Tile *) NULL, plowYankDef->cd_planes[pNum],
		&ar.ar_search, &DBAllTypeBits,
		plowCellDragPaint, (ClientData) &ar);
	(void) plowSrShadow(pNum, &shadowArea, DBZeroTypeBits,
		plowCellPushPaint, (ClientData) &ar);
    }

    /*
     * Search for cells to move.
     * We could do a shadow search, but just use area search instead
     * since we don't expect there to be too many cells in the path.
     * Cells to the left of the RHS of use get dragged by the same
     * amount as use is moving; cells to its right stay DRCTechHalo away
     * (or closer if they were originally closer).
     */
    cellArea.r_xbot = use->cu_bbox.r_xbot - 1;
    cellArea.r_xtop = edge->e_newx + DRCTechHalo;
    cellArea.r_ybot = edge->e_ybot - DRCTechHalo;
    cellArea.r_ytop = edge->e_ytop + DRCTechHalo;
    (void) TiSrArea((Tile *) NULL, plowYankDef->cd_planes[edge->e_pNum],
		&cellArea, plowFoundCell,
		(ClientData) &ar);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowCellDragPaint --
 *
 * Called by DBSrPaintArea() on behalf of prCell() above for each tile
 * overlapping the cell being moved (ar->ar_moving->e_use), or a DRCTechHalo
 * wide halo around it to the right, top, or bottom.
 *
 * If the LHS of the tile lies to the right of the LHS of the cell, we
 * move it; otherwise, we move the RHS of the tile.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	May add edges to the queue of edges to be processed.
 *
 * ----------------------------------------------------------------------------
 */

int
plowCellDragPaint(tile, ar)
    Tile *tile;
    struct applyRule *ar;
{
    Edge *movingEdge = ar->ar_moving;
    int distance = movingEdge->e_newx - movingEdge->e_x;
    Rect atomRect;

    if (LEFT(tile) <= ar->ar_search.r_xbot)
    {
	if (LEADING(tile) >= ar->ar_search.r_xtop)
	    return (0);
	atomRect.r_xtop = RIGHT(tile) + distance;
	if (LEADING(tile) >= atomRect.r_xtop)
	    return (0);
	atomRect.r_xbot = RIGHT(tile);
    }
    else
    {
	atomRect.r_xtop = LEFT(tile) + distance;
	if (TRAILING(tile) >= atomRect.r_xtop)
	    return (0);
	atomRect.r_xbot = LEFT(tile);
    }
    atomRect.r_ybot = MAX(BOTTOM(tile), ar->ar_search.r_ybot);
    atomRect.r_ytop = MIN(TOP(tile), ar->ar_search.r_ytop);
    (void) plowAtomize(ar->ar_pNum, &atomRect,
		plowPropagateProcPtr, (ClientData) NULL);

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowCellPushPaint --
 *
 * Filter procedure called by plowSrShadow() on behalf of prCell above.
 * Each paint tile in the shadow of the RHS of the cell being moved
 * gets pushed DRCTechHalo in front of the cell when it moves.  (If the
 * paint was already closer than DRCTechHalo to the front of the cell,
 * it stays that close).
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	May add edges to the queue of edges to be processed.
 *
 * ----------------------------------------------------------------------------
 */

int
plowCellPushPaint(impactedEdge, ar)
    Edge *impactedEdge;		/* Edge found by shadow search */
    struct applyRule *ar;	/* Describes edge being moved and search area */
{
    Edge *movingEdge = ar->ar_moving;
    int xsep, newx;

    xsep = impactedEdge->e_x - movingEdge->e_x;
    if (xsep > DRCTechHalo) xsep = DRCTechHalo;

    /* Queue the edge if it hasn't already moved far enough */
    newx = movingEdge->e_newx + xsep;
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
 * plowFoundCell --
 *
 * Called for each cell tile found by an area enumeration of the umbra of
 * a moving edge, plus a DRCTechHalo-wide halo above, below, and to its right.
 * Determine how far each use associated with that cell tile must move.  If
 * that cell has not already moved far enough, queue it.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	May add cells to the queue of edges to be processed.
 *
 * ----------------------------------------------------------------------------
 */

int
plowFoundCell(cellTile, ar)
    Tile *cellTile;
    struct applyRule *ar;
{
    Edge *movingEdge = ar->ar_moving;
    CellTileBody *ctb;
    int xmove, xsep;
    CellUse *use;
    Edge edge;

    edge.e_pNum = PL_CELL;
    for (ctb = (CellTileBody *) TiGetBody(cellTile); ctb; ctb = ctb->ctb_next)
    {
	use = ctb->ctb_use;
	if (use->cu_bbox.r_xbot <= movingEdge->e_x)
	{
	    /*
	     * If dragging the cell, move it by as much as this edge.
	     */
	    xmove = movingEdge->e_newx - movingEdge->e_x;
	}
	else
	{
	    /*
	     * If pushing the cell, keep it DRCTechHalo in front of the edge
	     * unless it was already closer.
	     */
	    xsep = use->cu_bbox.r_xbot - movingEdge->e_x;
	    if (xsep > DRCTechHalo) xsep = DRCTechHalo;
	    xmove = movingEdge->e_newx + xsep - use->cu_bbox.r_xbot;
	}

	/* Only queue the edge if the cell has not moved far enough */
	if ((use->cu_client != (ClientData)CLIENTDEFAULT) &&
		((int)(use->cu_client) < xmove))
	{
	    edge.e_use = use;
	    edge.e_flags = 0;
	    edge.e_ytop = use->cu_bbox.r_ytop;
	    edge.e_ybot = use->cu_bbox.r_ybot;
	    edge.e_x = use->cu_bbox.r_xtop;
	    edge.e_newx = use->cu_bbox.r_xtop + xmove;
	    edge.e_ltype = PLOWTYPE_CELL;
	    edge.e_rtype = PLOWTYPE_CELL;
	    (void) (*plowPropagateProcPtr)(&edge);
	}
    }

    return (0);
}
