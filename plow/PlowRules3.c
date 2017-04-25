/*
 * PlowRules3.c --
 *
 * Plowing rules: new sliver-avoidance rules.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/plow/PlowRules3.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
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
int plowInSliverProc();
int scanDown(), scanUp();
int scanDownError(), scanUpError();

/* Argument passed to above filter functions */
struct inarg
{
    Rect	 ina_area;		/* Area to search for violations */
    Edge	*ina_moving;		/* Edge causing this search */
    TileType	 ina_t0;		/* See comments in the procedures */
    int	       (*ina_proc)();		/* Apply to look for rule violations */

    /* Used while appling design rules */
    PlowRule	*ina_rule;		/* Plowing design rule being applied */
    int		 ina_incursion;		/* Height of biggest DRC error */
    bool	 ina_cantMove;		/* TRUE if couldn't fix some error */
};

/*
 * ----------------------------------------------------------------------------
 *
 * prInSliver --
 *
 * Avoid introducing slivers because the plow is too small.
 * This rule only applies if the plow height is less than the
 * maximum design-rule distance DRCTechHalo.
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
prInSliver(edge)
    Edge *edge;			/* Edge being moved */
{
    struct inarg inarg;
    Rect edgeBorder;
    Plane *plane;

    if ((edge->e_flags & E_ISINITIAL) == 0
	    || edge->e_ytop - edge->e_ybot >= DRCTechHalo)
	return;

    /* Look down from the top of the edge */
    edgeBorder.r_xbot = edge->e_x - 1;
    edgeBorder.r_xtop = edge->e_newx;
    edgeBorder.r_ybot = edge->e_ytop;
    edgeBorder.r_ytop = edge->e_ytop + 1;
    plane = plowYankDef->cd_planes[edge->e_pNum];

    inarg.ina_moving = edge;
    inarg.ina_t0 = (TileType) -1;
    inarg.ina_area.r_ytop = edge->e_ybot;
    inarg.ina_proc = scanDown;
    plowSrFinalArea(plane, &edgeBorder, &DBAllTypeBits, plowInSliverProc,
		(ClientData) &inarg);

    /* Look up from the bottom of the edge */
    edgeBorder.r_ybot = edge->e_ybot - 1;
    edgeBorder.r_ytop = edge->e_ybot;

    inarg.ina_t0 = (TileType) -1;
    inarg.ina_area.r_ybot = edge->e_ytop;
    inarg.ina_proc = scanUp;
    plowSrFinalArea(plane, &edgeBorder, &DBAllTypeBits, plowInSliverProc,
		(ClientData) &inarg);
}

int
plowInSliverProc(tile, inarg)
    Tile *tile;
    struct inarg *inarg;
{
    Edge *movingEdge = inarg->ina_moving;
#ifdef	notdef
    Edge newEdge;
#endif	/* notdef */
    TileType t1;
    int xtop;

    /* Is this the first tile we've seen? */
    if (inarg->ina_t0 == (TileType) -1)
    {
	inarg->ina_t0 = TiGetType(tile);
	inarg->ina_area.r_xbot = movingEdge->e_x;
	inarg->ina_area.r_xtop = MIN(movingEdge->e_newx, LEADING(tile));
	if (LEADING(tile) >= movingEdge->e_newx)
	{
	    (*inarg->ina_proc)(inarg, inarg->ina_t0, FALSE);
	    return (1);
	}
	return (0);
    }

    /* Is this still the same material? */
    if (TiGetType(tile) == inarg->ina_t0)
    {
	/* Extend the edge to the right */
	xtop = MIN(movingEdge->e_newx, LEADING(tile));
	if (xtop > inarg->ina_area.r_xtop) inarg->ina_area.r_xtop = xtop;

	/* Is this the last tile? */
	if (LEADING(tile) >= movingEdge->e_newx)
	{
	    (*inarg->ina_proc)(inarg, inarg->ina_t0, FALSE);
	    return (1);
	}

	/* Keep looking */
	return (0);
    }

    /* New type of material */
    t1 = TiGetType(tile);

    /* Can we not slide past the t0 | t1 edge? */
    if ((movingEdge->e_ltype != TT_SPACE && movingEdge->e_rtype != TT_SPACE)
	    || TTMaskHasType(&PlowCoveredTypes, inarg->ina_t0)
	    || TTMaskHasType(&PlowCoveredTypes, t1)
	    || inarg->ina_t0 != movingEdge->e_ltype
	    || t1 != movingEdge->e_rtype)
    {
#ifdef	notdef
	/* Move a 1-lambda high strip of the t0 | t1 edge */
	newEdge.e_x = LEFT(tile);
	newEdge.e_newx = movingEdge->e_newx;
	newEdge.e_ybot = movingEdge->e_ytop;
	newEdge.e_ytop = movingEdge->e_ytop + 1;
	newEdge.e_pNum = movingEdge->e_pNum;
	newEdge.e_ltype = inarg->ina_t0;
	newEdge.e_rtype = t1;
	newEdge.e_use = (CellUse *) NULL;
	(*plowPropagateProcPtr)(&newEdge);
#endif	/* notdef */
	(*inarg->ina_proc)(inarg, inarg->ina_t0, FALSE);
	return (1);
    }

    /*
     * Search the t0 edge up to the LHS of t1.
     * We can only move violation edges to eliminate the errors we find.
     */
    (*inarg->ina_proc)(inarg, inarg->ina_t0, FALSE);

    /*
     * Search the t1 edge as well.
     * Move violation edges if possible, but we can move the
     * t0 | t1 edge if we can't fix the violations.
     */
    inarg->ina_area.r_xbot = inarg->ina_area.r_xtop;
    inarg->ina_area.r_xtop = movingEdge->e_newx;
    (*inarg->ina_proc)(inarg, t1, TRUE);
    return (1);
}

int
scanDown(inarg, type, canMoveInargEdge)
    struct inarg *inarg;
    TileType type;
    bool canMoveInargEdge;
{
    TileType ltype = inarg->ina_moving->e_ltype;
    Edge *movingEdge = inarg->ina_moving;
    TileTypeBitMask badTypes;
    PlowRule *pr;
    int height;

    inarg->ina_incursion = 0;
    inarg->ina_cantMove = FALSE;
    height = movingEdge->e_ytop - movingEdge->e_ybot;
    for (pr = plowSpacingRulesTbl[type][ltype]; pr; pr = pr->pr_next)
    {
	if ((pr->pr_flags & PR_PENUMBRAONLY) || pr->pr_dist <= height)
	    continue;
	inarg->ina_area.r_ybot = movingEdge->e_ytop - pr->pr_dist;
	inarg->ina_rule = pr;
	TTMaskCom2(&badTypes, &pr->pr_oktypes);
	plowSrFinalArea(plowYankDef->cd_planes[pr->pr_pNum], &inarg->ina_area,
		    &badTypes, scanDownError, (ClientData) inarg);
    }

    for (pr = plowWidthRulesTbl[type][ltype]; pr; pr = pr->pr_next)
    {
	if ((pr->pr_flags & PR_PENUMBRAONLY) || pr->pr_dist <= height)
	    continue;
	inarg->ina_area.r_ybot = movingEdge->e_ytop - pr->pr_dist;
	inarg->ina_rule = pr;
	TTMaskCom2(&badTypes, &pr->pr_oktypes);
	plowSrFinalArea(plowYankDef->cd_planes[pr->pr_pNum], &inarg->ina_area,
		    &badTypes, scanDownError, (ClientData) inarg);
    }

#ifdef	notdef
    /* Move the top edge if necessary */
    if (canMoveInargEdge && inarg->ina_cantMove)
    {
	struct applyRule ar;
	Rect shadowRect;

	shadowRect.r_xbot = inarg->ina_area.r_xbot - 1;
	shadowRect.r_ybot = movingEdge->e_ytop;
	shadowRect.r_xtop = movingEdge->e_newx;
	shadowRect.r_ytop = movingEdge->e_ytop + inarg->ina_incursion;
	ar.ar_moving = movingEdge;
	ar.ar_rule = (PlowRule *) NULL;
	plowSrShadow(movingEdge->e_pNum, &shadowRect,
		DBZeroTypeBits, plowApplyRule, (ClientData) &ar);
    }
#endif	/* notdef */
    return 0;
}

int
scanDownError(tile, inarg)
    Tile *tile;
    struct inarg *inarg;
{
    Rect atomRect;
    int incursion;

    incursion = MIN(TOP(tile), inarg->ina_area.r_ytop) - inarg->ina_area.r_ybot;
    if (incursion > inarg->ina_incursion)
	inarg->ina_incursion = incursion;

    /*
     * The following relies on maximal horizontal strips.
     * If the violating tile extends to the left of the area
     * we're checking, we can't eliminate the violation by
     * moving its LHS.
     */
    if (LEFT(tile) < inarg->ina_area.r_xbot)
    {
	inarg->ina_cantMove = TRUE;
	return (0);
    }

    /*
     * Eliminate the violation by moving the LHS of
     * the violating tile.
     */
    atomRect.r_xbot = LEFT(tile);
    atomRect.r_xtop = inarg->ina_moving->e_newx;
    atomRect.r_ybot = MAX(BOTTOM(tile), inarg->ina_area.r_ybot);
    atomRect.r_ytop = MIN(TOP(tile), inarg->ina_area.r_ytop);
    (void) plowAtomize(inarg->ina_rule->pr_pNum, &atomRect,
			plowPropagateProcPtr, (ClientData) NULL);

    return (0);
}

int
scanUp(inarg, type, canMoveInargEdge)
    struct inarg *inarg;
    TileType type;
    bool canMoveInargEdge;
{
    TileType ltype = inarg->ina_moving->e_ltype;
    Edge *movingEdge = inarg->ina_moving;
    TileTypeBitMask badTypes;
    PlowRule *pr;
    int height;

    inarg->ina_incursion = 0;
    inarg->ina_cantMove = FALSE;
    height = movingEdge->e_ytop - movingEdge->e_ybot;
    for (pr = plowSpacingRulesTbl[type][ltype]; pr; pr = pr->pr_next)
    {
	if ((pr->pr_flags & PR_PENUMBRAONLY) || pr->pr_dist <= height)
	    continue;
	inarg->ina_area.r_ytop = movingEdge->e_ybot + pr->pr_dist;
	inarg->ina_rule = pr;
	TTMaskCom2(&badTypes, &pr->pr_oktypes);
	plowSrFinalArea(plowYankDef->cd_planes[pr->pr_pNum], &inarg->ina_area,
		    &badTypes, scanUpError, (ClientData) inarg);
    }

    for (pr = plowWidthRulesTbl[type][ltype]; pr; pr = pr->pr_next)
    {
	if ((pr->pr_flags & PR_PENUMBRAONLY) || pr->pr_dist <= height)
	    continue;
	inarg->ina_area.r_ytop = movingEdge->e_ybot + pr->pr_dist;
	inarg->ina_rule = pr;
	TTMaskCom2(&badTypes, &pr->pr_oktypes);
	plowSrFinalArea(plowYankDef->cd_planes[pr->pr_pNum], &inarg->ina_area,
		    &badTypes, scanUpError, (ClientData) inarg);
    }

#ifdef	notdef
    /* Move the bottom edge if necessary */
    if (canMoveInargEdge && inarg->ina_cantMove)
    {
	struct applyRule ar;
	Rect shadowRect;

	shadowRect.r_xbot = inarg->ina_area.r_xbot - 1;
	shadowRect.r_ybot = movingEdge->e_ybot - inarg->ina_incursion;
	shadowRect.r_xtop = movingEdge->e_newx;
	shadowRect.r_ytop = movingEdge->e_ybot;
	ar.ar_moving = movingEdge;
	ar.ar_rule = (PlowRule *) NULL;
	plowSrShadow(movingEdge->e_pNum, &shadowRect,
		DBZeroTypeBits, plowApplyRule, (ClientData) &ar);
    }
#endif	/* notdef */
    return 0;
}

int
scanUpError(tile, inarg)
    Tile *tile;
    struct inarg *inarg;
{
    Rect atomRect;
    int incursion;

    incursion = inarg->ina_area.r_ytop;
    incursion -= MAX(BOTTOM(tile), inarg->ina_area.r_ybot);
    if (incursion > inarg->ina_incursion)
	inarg->ina_incursion = incursion;

    /*
     * The following relies on maximal horizontal strips.
     * If the violating tile extends to the left of the area
     * we're checking, we can't eliminate the violation by
     * moving its LHS.
     */
    if (LEFT(tile) < inarg->ina_area.r_xbot)
    {
	inarg->ina_cantMove = TRUE;
	return (0);
    }

    /*
     * Eliminate the violation by moving the LHS of
     * the violating tile.
     */
    atomRect.r_xbot = LEFT(tile);
    atomRect.r_xtop = inarg->ina_moving->e_newx;
    atomRect.r_ybot = MAX(BOTTOM(tile), inarg->ina_area.r_ybot);
    atomRect.r_ytop = MIN(TOP(tile), inarg->ina_area.r_ytop);
    (void) plowAtomize(inarg->ina_rule->pr_pNum, &atomRect,
			plowPropagateProcPtr, (ClientData) NULL);

    return (0);
}

int
plowSrFinalArea(plane, area, okTypes, proc, cdata)
    Plane *plane;
    Rect *area;
    TileTypeBitMask *okTypes;
    int (*proc)();
    ClientData cdata;
{
    return (DBSrPaintArea((Tile *) NULL, plane, area, okTypes, proc, cdata));
}
