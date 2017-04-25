/*
 * DRCbasic.c --
 *
 * This file provides routines that make perform basic design-rule
 * checking:  given an area of a cell definition, this file will
 * find all of the rule violations and call a client procedure for
 * each one.
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

#ifndef	lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/drc/DRCbasic.c,v 1.7 2010/09/20 21:13:22 tim Exp $";
#endif	/* not lint */

#include <sys/types.h>
#include <stdio.h>
#include <string.h>		// for memcpy()
#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "drc/drc.h"
#include "utils/signals.h"
#include "utils/maxrect.h"
#include "utils/malloc.h"

int dbDRCDebug = 0;

/* The following DRC cookie is used when there are tiles of type
 * TT_ERROR_S found during the basic DRC.  These arise during
 * hierarchical checking when there are illegal overlaps.
 */

static DRCCookie drcOverlapCookie = {
    0, 0, 0, 0,
    { 0 }, { 0 },
    0, 0, 0,
    "Can't overlap those layers",
    (DRCCookie *) NULL
};

/* Forward references: */

extern int areaCheck();
extern int drcTile();
extern MaxRectsData *drcCanonicalMaxwidth();

/*
 *-----------------------------------------------------------------------
 *
 * point_to_segment
 *
 *	Euclidean-distance point-to-segment distance (squared)
 *	calculation (borrowed from XCircuit)
 *
 * Results:
 *	Squared Euclidean distance of the closest approach of the
 *	line segment to the point (long result).
 *
 * Side Effects:
 *	None.
 *
 *-----------------------------------------------------------------------
 */

long
point_to_segment(px, py, s1x, s1y, s2x, s2y)
    int px, py;		/* The position of the point */
    int s1x, s1y;	/* One endpoint of the line segment */
    int s2x, s2y;	/* The other endpoint of the line segment */
{
    long x, y;
    long a, b, c, frac;
    float protod;

    x = (long)s2x - (long)s1x;
    y = (long)s2y - (long)s1y;
    c = (x * x + y * y);

    x = (long)px - (long)s1x;
    y = (long)py - (long)s1y;
    a = (x * x + y * y);

    x = (long)px - (long)s2x;
    y = (long)py - (long)s2y;
    b = (x * x + y * y);

    frac = a - b;
    if (frac >= c) return b;
    else if (-frac >= c) return a;
    else
    {
	protod = (float)(c + a - b);
	return (a - (long)((protod * protod) / (float)(c << 2)));
    }
}

/* Define Euclidean distance checks */

#define RADIAL_NW	0x1000
#define RADIAL_NE	0x8000
#define RADIAL_SW	0x2000
#define RADIAL_SE	0x4000


/*
 * ----------------------------------------------------------------------------
 *
 * areaCheck -- 
 *
 * Call the function passed down from DRCBasicCheck() if the current tile
 * violates the rule in the given DRCCookie.  If the rule's connectivity
 * flag is set, then make sure the violating material isn't connected
 * to what's on the initial side of the edge before calling the client
 * error function.
 *
 * This function is called from DBSrPaintArea().
 *
 * Results:
 *	Zero (so that the search will continue).
 *
 * Side effects:
 *      Applies the function passed as an argument.
 *
 * ----------------------------------------------------------------------------
 */

int
areaCheck(tile, arg) 
    Tile *tile;
    struct drcClientData *arg;
{
    Rect rect;		/* Area where error is to be recorded. */

    TiToRect(tile, &rect);

    /* Only consider the portion of the suspicious tile that overlaps
     * the clip area for errors, unless this is a trigger rule.
     */

    if (!(arg->dCD_cptr->drcc_flags & DRC_TRIGGER))
	GeoClip(&rect, arg->dCD_clip);

    GeoClip(&rect, arg->dCD_constraint);
    if ((rect.r_xbot >= rect.r_xtop) || (rect.r_ybot >= rect.r_ytop))
	return 0;

    /* 
     * When Euclidean distance checks are enabled, check for error tiles
     * outside of the perimeter of the circle in the corner extension area
     * that extends "sdist" from the corner of the edge.
     *
     * Also check the relatively rare case where the tile is inside the
     * circle perimeter, but only the corner of the triangle projects into
     * the error check rectangle, and is outside of the circle.
     */

    if (arg->dCD_radial != 0)
    {
	unsigned int i;
	int sqx, sqy;
	int sdist = arg->dCD_radial & 0xfff;
	long sstest, ssdist = sdist * sdist;
	
	if ((arg->dCD_radial & RADIAL_NW) != 0)
	{
	    if (((sqx = arg->dCD_constraint->r_xbot + sdist
			 - rect.r_xtop) >= 0) && ((sqy = rect.r_ybot
			- arg->dCD_constraint->r_ytop + sdist) >= 0)
			&& ((sqx * sqx + sqy * sqy) >= ssdist))
		return 0;
	    else if (IsSplit(tile) && !SplitDirection(tile) && !SplitSide(tile))
	    {
		sstest = point_to_segment(arg->dCD_constraint->r_xbot + sdist,
			arg->dCD_constraint->r_ytop - sdist,
			LEFT(tile), BOTTOM(tile), RIGHT(tile), TOP(tile));
		if (sstest > ssdist) return 0;
	    }
	}
	if ((arg->dCD_radial & RADIAL_NE) != 0)
	{
	    if (((sqx = rect.r_xbot - arg->dCD_constraint->r_xtop
			+ sdist) >= 0) && ((sqy = rect.r_ybot
			- arg->dCD_constraint->r_ytop + sdist) >= 0)
			&& ((sqx * sqx + sqy * sqy) >= ssdist))
		return 0;
	    else if (IsSplit(tile) && SplitDirection(tile) && SplitSide(tile))
	    {
		sstest = point_to_segment(arg->dCD_constraint->r_xtop - sdist,
			arg->dCD_constraint->r_ytop - sdist,
			LEFT(tile), TOP(tile), RIGHT(tile), BOTTOM(tile));
		if (sstest > ssdist) return 0;
	    }
	}
	if ((arg->dCD_radial & RADIAL_SW) != 0)
	{
	    if (((sqx = arg->dCD_constraint->r_xbot + sdist
			- rect.r_xtop) >= 0) &&
			((sqy = arg->dCD_constraint->r_ybot
			+ sdist - rect.r_ytop) >= 0)
			&& ((sqx * sqx + sqy * sqy) >= ssdist))
		return 0;
	    else if (IsSplit(tile) && SplitDirection(tile) && !SplitSide(tile))
	    {
		sstest = point_to_segment(arg->dCD_constraint->r_xbot + sdist,
			arg->dCD_constraint->r_ybot + sdist,
			LEFT(tile), TOP(tile), RIGHT(tile), BOTTOM(tile));
		if (sstest > ssdist) return 0;
	    }
	}
	if ((arg->dCD_radial & RADIAL_SE) != 0)
	{
	    if (((sqx = rect.r_xbot - arg->dCD_constraint->r_xtop
			+ sdist) >= 0) &&
			((sqy = arg->dCD_constraint->r_ybot
			+ sdist - rect.r_ytop) >= 0)
			&& ((sqx * sqx + sqy * sqy) >= ssdist))
		return 0;
	    else if (IsSplit(tile) && !SplitDirection(tile) && SplitSide(tile))
	    {
		sstest = point_to_segment(arg->dCD_constraint->r_xtop - sdist,
			arg->dCD_constraint->r_ybot + sdist,
			LEFT(tile), BOTTOM(tile), RIGHT(tile), TOP(tile));
		if (sstest > ssdist) return 0;
	    }
	}
    }

    if (arg->dCD_cptr->drcc_flags & DRC_TRIGGER)
    {
	Rect *newrlist;
	int entries = arg->dCD_entries;

	/* The following code allows the rect list to be expanded by	*/
	/* multiples of 8, when necessary.				*/

	arg->dCD_entries++;
	if (arg->dCD_rlist == NULL)
	    arg->dCD_rlist = (Rect *)mallocMagic(8 * sizeof(Rect));
	else if ((arg->dCD_entries & ~(entries | 7)) == arg->dCD_entries)
	{
	    newrlist = (Rect *)mallocMagic((arg->dCD_entries << 1) * sizeof(Rect));
	    memcpy((void *)newrlist, (void *)arg->dCD_rlist, (size_t)entries *
			sizeof(Rect));
	    freeMagic(arg->dCD_rlist);
	    arg->dCD_rlist = newrlist;
	}
	arg->dCD_rlist[arg->dCD_entries - 1] = rect;
    }
    else
    {
	(*(arg->dCD_function))(arg->dCD_celldef, &rect, arg->dCD_cptr,
			arg->dCD_clientData);
	(*(arg->dCD_errors))++;
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DRCBasicCheck --
 *
 * This is the top-level routine for basic design-rule checking.
 *
 * Results:
 *	Number of errors found.
 *
 * Side effects:
 *	Calls function for each design-rule violation in celldef
 *	that is triggered by an edge in rect and whose violation
 *	area falls withing clipRect.  This routine makes a flat check:
 *	it considers only information in the paint planes of celldef,
 *	and does not expand children.  Function should have the form:
 *	void
 *	function(def, area, rule, cdarg)
 *	    CellDef *def;
 *	    Rect *area;
 *	    DRCCookie *rule;
 *	    ClientData cdarg;
 *	{
 *	}
 *
 *	In the call to function, def is the definition containing the
 *	basic area being checked, area is the actual area where a
 *	rule is violated, rule is the rule being violated, and cdarg
 *	is the client data passed through all of our routines.
 *
 * Note:
 *	If an interrupt occurs (SigInterruptPending gets set), then
 *	the basic will be aborted immediately.  This means the check
 *	may be incomplete.
 *
 * ----------------------------------------------------------------------------
 */

int
DRCBasicCheck (celldef, checkRect, clipRect, function, cdata)
    CellDef *celldef;	/* CellDef being checked */
    Rect *checkRect;	/* Check rules in this area -- usually two Haloes
			 * larger than the area where changes were made.
			 */
    Rect *clipRect;	/* Clip error tiles against this area. */
    void (*function)();	/* Function to apply for each error found. */
    ClientData cdata;	/* Passed to function as argument. */
{
    struct drcClientData arg;
    int	errors;
    int planeNum;

    if (DRCCurStyle == NULL) return 0;	/* No DRC, no errors */

    /*  Insist on top quality rectangles. */

    if ((checkRect->r_xbot >= checkRect->r_xtop)
	    || (checkRect->r_ybot >= checkRect->r_ytop))
	 return (0);

    errors = 0;

    arg.dCD_celldef = celldef;
    arg.dCD_rect = checkRect;
    arg.dCD_errors = &errors;
    arg.dCD_function = function;
    arg.dCD_clip = clipRect;
    arg.dCD_clientData = cdata;
    arg.dCD_rlist = NULL;
    arg.dCD_entries = 0;

    for (planeNum = PL_TECHDEPBASE; planeNum < DBNumPlanes; planeNum++)
    {
        arg.dCD_plane = planeNum;
	DBResetTilePlane(celldef->cd_planes[planeNum], DRC_UNPROCESSED);
        (void) DBSrPaintArea ((Tile *) NULL, celldef->cd_planes[planeNum],
		checkRect, &DBAllTypeBits, drcTile, (ClientData) &arg);
    }
    drcCifCheck(&arg);
    if (arg.dCD_rlist != NULL) freeMagic(arg.dCD_rlist);
    return (errors);
}

/*
 * ----------------------------------------------------------------------------
 *
 * drcTile --
 *
 * This is a search function invoked once for each tile in
 * the area to be checked.  It checks design rules along the left
 * and bottom of the given tile.  If the tile extends beyond the
 * clipping rectangle in any direction, then the boundary on that
 * side of the tile will be skipped.
 *
 * Results:
 *	Zero (so that the search will continue), unless an interrupt
 *	occurs, in which case 1 is returned to stop the check.
 *
 * Side effects:
 *	Calls the client's error function if errors are found.
 *
 * ----------------------------------------------------------------------------
 */

int
drcTile (tile, arg)
    Tile *tile;	/* Tile being examined */
    struct drcClientData *arg;
{
    DRCCookie *cptr;	/* Current design rule on list */
    Rect *rect = arg->dCD_rect;	/* Area being checked */
    Rect errRect;		/* Area checked for an individual rule */
    MaxRectsData *mrd;		/* Used by widespacing rule */
    TileTypeBitMask tmpMask, *rMask;
    bool trigpending;		/* Hack for widespacing rule */
    bool firsttile;
    int triggered;
    int cdist, dist, ccdist, result;

    arg->dCD_constraint = &errRect;

    /*
     * If we were interrupted, we want to
     * abort the check as quickly as possible.
     */
    if (SigInterruptPending) return 1;
    DRCstatTiles++;

    /* If this tile is an error tile, it arose because of an illegal
     * overlap between things in adjacent cells.  This means that
     * there's an automatic violation over the area of the tile.
     */
    
    if (TiGetType(tile) == TT_ERROR_S)
    {
	TiToRect(tile, &errRect);
	GeoClip(&errRect, rect);
        (*(arg->dCD_function)) (arg->dCD_celldef, &errRect,
	    &drcOverlapCookie, arg->dCD_clientData);
        (*(arg->dCD_errors))++;
    }

    if (IsSplit(tile))
    {
	/* Check rules for DRC_ANGLES rule and process */
	TileType tt = TiGetLeftType(tile);
	if (tt != TT_SPACE)
	{
	    for (cptr = DRCCurStyle->DRCRulesTbl[TT_SPACE][tt];
			cptr != (DRCCookie *) NULL; cptr = cptr->drcc_next)
		if (cptr->drcc_flags & DRC_ANGLES)
		{
		    drcCheckAngles(tile, arg, cptr);
		    break;
		}
	}
	tt = TiGetRightType(tile);
	if (tt != TT_SPACE)
	{
	    for (cptr = DRCCurStyle->DRCRulesTbl[TT_SPACE][tt];
			cptr != (DRCCookie *) NULL; cptr = cptr->drcc_next)
		if (cptr->drcc_flags & DRC_ANGLES)
		{
		    drcCheckAngles(tile, arg, cptr);
		    break;
		}
	}

        /* This drc is only for the left edge of the tile */
	if (SplitSide(tile)) goto checkbottom;
    }

    /*
     * Check design rules along a vertical boundary between two tiles.
     *
     *			      1 | 4
     *				T
     *				|
     *			tpleft	|  tile
     *				|
     *				B
     *			      2 | 3
     *
     * The labels "T" and "B" indicate pointT and pointB respectively.
     *
     * If a rule's direction is FORWARD, then check from left to right.
     *
     *	    * Check the top right corner if the 1x1 lambda square
     *	      on the top left corner (1) of pointT matches the design
     *	      rule's "corner" mask.
     *
     *	    * Check the bottom right corner if the rule says check
     *	      BOTHCORNERS and the 1x1 lambda square on the bottom left
     *	      corner (2) of pointB matches the design rule's "corner" mask.
     *
     * If a rule's direction is REVERSE, then check from right to left.
     *
     *	    * Check the bottom left corner if the 1x1 lambda square
     *	      on the bottom right corner (3) of pointB matches the design
     *	      rule's "corner" mask.
     *
     *	    * Check the top left corner if the rule says check BOTHCORNERS
     *	      and the 1x1 lambda square on the top right corner (4) of
     *	      pointT matches the design rule's "corner" mask.
     */

    if (LEFT(tile) >= rect->r_xbot)		/* check tile against rect */
    {
	Tile *tpleft, *tpl, *tpr;
	TileType tt, to;
	int edgeTop, edgeBot;
        int top = MIN(TOP(tile), rect->r_ytop);
        int bottom = MAX(BOTTOM(tile), rect->r_ybot);
	int edgeX = LEFT(tile);

	firsttile = TRUE;
        for (tpleft = BL(tile); BOTTOM(tpleft) < top; tpleft = RT(tpleft))
        {
	    /* Get the tile types to the left and right of the edge */

	    tt = TiGetLeftType(tile);
	    to = TiGetRightType(tpleft);

	    /* Don't check synthetic edges, i.e. edges with same type on
             * both sides.  Such "edges" have no physical significance, and
	     * depend on internal-details of how paint is spit into tiles.
	     * Thus checking them just leads to confusion.  (When edge rules
	     * involving such edges are encountered during technology read-in
	     * the user is warned that such edges are not checked).
	     */

	    if (tt == to) continue;

	    /*
	     * Go through list of design rules triggered by the
	     * left-to-right edge.
	     */
	    edgeTop = MIN(TOP (tpleft), top);
	    edgeBot = MAX(BOTTOM(tpleft), bottom);
	    if (edgeTop <= edgeBot)
		continue;

	    triggered = 0;
	    for (cptr = DRCCurStyle->DRCRulesTbl[to][tt]; cptr != (DRCCookie *) NULL;
			cptr = cptr->drcc_next)
	    {
		if (cptr->drcc_flags & DRC_ANGLES) continue;

		/* Find the rule distances according to the scale factor */
		dist = cptr->drcc_dist;
		cdist = cptr->drcc_cdist;
		trigpending = (cptr->drcc_flags & DRC_TRIGGER) ? TRUE : FALSE;

		/* drcc_edgeplane is used to avoid checks on edges	*/
		/* in more than one plane				*/

		if (arg->dCD_plane != cptr->drcc_edgeplane)
		{
		    if (trigpending) cptr = cptr->drcc_next;
		    continue;
		}

		DRCstatRules++;

		if (cptr->drcc_flags & DRC_AREA)
		{
		    if (firsttile)
			drcCheckArea(tile, arg, cptr);
		    continue;
		}

		if ((cptr->drcc_flags & (DRC_MAXWIDTH | DRC_BENDS)) ==
				(DRC_MAXWIDTH | DRC_BENDS))
		{
		    /* New algorithm --- Tim 3/6/05 */
		    Rect *lr;
		    int i;

		    if (!trigpending) cptr->drcc_dist++;

		    if (cptr->drcc_flags & DRC_REVERSE)
			mrd = drcCanonicalMaxwidth(tpleft, GEO_WEST, arg, cptr);
		    else if (firsttile)
			mrd = drcCanonicalMaxwidth(tile, GEO_EAST, arg, cptr);
		    else
			mrd = NULL;
		    if (!trigpending) cptr->drcc_dist--;
		    if (trigpending)
		    {
			if (mrd)
			    triggered = mrd->entries;
			else
			    cptr = cptr->drcc_next;
		    }
		    else if (mrd)
		    {
			for (i = 0; i < mrd->entries; i++)
			{
			    lr = &mrd->rlist[i];
			    GeoClip(lr, arg->dCD_clip);
			    if (!GEO_RECTNULL(lr))  
			    {
				(*(arg->dCD_function)) (arg->dCD_celldef,
					lr, cptr, arg->dCD_clientData);
				(*(arg->dCD_errors))++;
			    }
			}
		    }
		    continue;
		}
		else if (cptr->drcc_flags & DRC_MAXWIDTH)
		{
		    /* bends_illegal option only */
		    if (firsttile)
			drcCheckMaxwidth(tile, arg, cptr);
		    continue;
		}
		else if (!triggered) mrd = NULL;

		if (cptr->drcc_flags & DRC_RECTSIZE)
		{
		    /* only checked for bottom-left tile in a rect area */
		    if (firsttile && !TTMaskHasType(&cptr->drcc_mask,
				TiGetRightType(BL(tile))) &&
				!TTMaskHasType(&cptr->drcc_mask,
				TiGetTopType(LB(tile))))
			drcCheckRectSize(tile, arg, cptr);
		    continue;
		}

		result = 0;
		arg->dCD_radial = 0; 
		do {
		    if (triggered)
		    {
			/* For triggered rules, we want to look	at the	*/
			/* clipped region found by the triggering rule	*/

			if (mrd)
			    errRect = mrd->rlist[--triggered];
			else
			    errRect = arg->dCD_rlist[--triggered];
			errRect.r_ytop += cdist;
			errRect.r_ybot -= cdist;
			if (errRect.r_ytop > edgeTop) errRect.r_ytop = edgeTop;
			if (errRect.r_ybot < edgeBot) errRect.r_ybot = edgeBot;
		    }
		    else
		    {
			errRect.r_ytop = edgeTop;
			errRect.r_ybot = edgeBot;
		    }

		    if (cptr->drcc_flags & DRC_REVERSE)
		    {
			/*
			 * Determine corner extensions.
			 */

			/* Find the point (3) to the bottom right of pointB */
			if (BOTTOM(tile) >= errRect.r_ybot) tpr = LB(tile);
			else tpr = tile;

			/* Also find point (2) to check for edge continuation */
			if (BOTTOM(tpleft) >= errRect.r_ybot)
			    for (tpl = LB(tpleft); RIGHT(tpl) < edgeX; tpl = TR(tpl));
			else tpl = tpleft;

			/* Make sure the edge stops at edgeBot */
			if ((TiGetTopType(tpl) != TiGetBottomType(tpleft)) ||
				(TiGetTopType(tpr) != TiGetBottomType(tile)))
			{
			    if (TTMaskHasType(&cptr->drcc_corner, TiGetTopType(tpr)))
		 	    {
				errRect.r_ybot -= cdist;
				if (DRCEuclidean)
				    arg->dCD_radial |= RADIAL_SW;
			    }
			}

			if (cptr->drcc_flags & DRC_BOTHCORNERS)
			{
			    /*
			     * Check the other corner
			     */

			    /* Find point (4) to the top right of pointT */
			    if (TOP(tile) <= errRect.r_ytop)
				for (tpr = RT(tile); LEFT(tpr) > edgeX; tpr = BL(tpr));
			    else tpr = tile;

			    /* Also find point (1) to check for edge continuation */
			    if (TOP(tpleft) <= errRect.r_ytop) tpl = RT(tpleft);
			    else tpl = tpleft;

			    /* Make sure the edge stops at edgeTop */
			    if ((TiGetBottomType(tpl) != TiGetTopType(tpleft)) ||
					(TiGetBottomType(tpr) != TiGetTopType(tile)))
			    {
			        if (TTMaskHasType(&cptr->drcc_corner,
					TiGetBottomType(tpr)))
				{
				    errRect.r_ytop += cdist;
				    if (DRCEuclidean)
					arg->dCD_radial |= RADIAL_NW;
				}
			    }
			}

			/*
			 * Just for grins, see if we could avoid a messy search
			 * by looking only at tpleft.
			 */
			errRect.r_xbot = edgeX - dist;
			if (cptr->drcc_flags & DRC_OUTSIDE) errRect.r_xbot--;
			if (LEFT(tpleft) <= errRect.r_xbot
				&& BOTTOM(tpleft) <= errRect.r_ybot
				&& TOP(tpleft) >= errRect.r_ytop
				&& arg->dCD_plane == cptr->drcc_plane
				&& TTMaskHasType(&cptr->drcc_mask,
				TiGetRightType(tpleft)))
			    continue;

			errRect.r_xtop = edgeX;
			if (cptr->drcc_flags & DRC_OUTSIDE) errRect.r_xtop -= dist;
			arg->dCD_initial = tile;
		    }
		    else  /* FORWARD */
		    {
			/*
			 * Determine corner extensions.
			 */

			/* Find the point (1) to the top left of pointT */
			if (TOP(tpleft) <= errRect.r_ytop) tpl = RT(tpleft);
			else tpl = tpleft;

			/* Also find point (4) to check for edge continuation */
			if (TOP(tile) <= errRect.r_ytop)
			    for (tpr = RT(tile); LEFT(tpr) > edgeX; tpr = BL(tpr));
			else tpr = tile;

			/* Make sure the edge stops at edgeTop */
			if ((TiGetBottomType(tpl) != TiGetTopType(tpleft)) ||
				(TiGetBottomType(tpr) != TiGetTopType(tile)))
			{
			    if (TTMaskHasType(&cptr->drcc_corner, TiGetBottomType(tpl)))
			    {
				errRect.r_ytop += cdist;
				if (DRCEuclidean)
				    arg->dCD_radial |= RADIAL_NE;
			    }
			}

			if (cptr->drcc_flags & DRC_BOTHCORNERS)
			{
			    /*
			     * Check the other corner
			     */

			    /* Find point (2) to the bottom left of pointB. */
			    if (BOTTOM(tpleft) >= errRect.r_ybot)
				for (tpl = LB(tpleft); RIGHT(tpl) < edgeX; tpl = TR(tpl));
			    else tpl = tpleft;

			    /* Also find point (3) to check for edge continuation */
			    if (BOTTOM(tile) >= errRect.r_ybot) tpr = LB(tile);
			    else tpr = tile;

			    /* Make sure the edge stops at edgeBot */
			    if ((TiGetTopType(tpl) != TiGetBottomType(tpleft)) ||
					(TiGetTopType(tpr) != TiGetBottomType(tile)))
			    {
				if (TTMaskHasType(&cptr->drcc_corner, TiGetTopType(tpl)))
				{
				    errRect.r_ybot -= cdist;
				    if (DRCEuclidean)
					arg->dCD_radial |= RADIAL_SE;
				}
			    }
			}

			/*
			 * Just for grins, see if we could avoid a messy search
			 * by looking only at tile.
			 */
			errRect.r_xtop = edgeX + dist;
			if (cptr->drcc_flags & DRC_OUTSIDE) errRect.r_xtop++;
			if (RIGHT(tile) >= errRect.r_xtop
				&& BOTTOM(tile) <= errRect.r_ybot
				&& TOP(tile) >= errRect.r_ytop
				&& arg->dCD_plane == cptr->drcc_plane
				&& TTMaskHasType(&cptr->drcc_mask,
				TiGetLeftType(tile)))
			    continue;

			errRect.r_xbot = edgeX;
			if (cptr->drcc_flags & DRC_OUTSIDE) errRect.r_xbot += dist;
			arg->dCD_initial= tpleft;
		    }
		    if (arg->dCD_radial)
		    {
			arg->dCD_radial &= 0xf000;
			arg->dCD_radial |= (0xfff & cdist);
		    }

		    DRCstatSlow++;
		    arg->dCD_cptr = cptr;
		    arg->dCD_entries = 0;
		    TTMaskCom2(&tmpMask, &cptr->drcc_mask);
		    TTMaskClearType(&tmpMask, TT_ERROR_S);
		    DBSrPaintArea((Tile *) NULL,
				arg->dCD_celldef->cd_planes[cptr->drcc_plane],
				&errRect, &tmpMask, areaCheck, (ClientData) arg);
		} while (triggered);

		if (arg->dCD_entries == 0)
		{
		    /* Trigger rule:  If rule check found errors,	*/
		    /* do the next rule.  Otherwise, skip it.		*/

		    if (trigpending)
			cptr = cptr->drcc_next;
		}
		else
		    triggered = arg->dCD_entries;
	    }
	    DRCstatEdges++;
	    firsttile = FALSE;
        }
    }

    /* This drc is only for the bottom edge of the tile */

checkbottom:
    if (IsSplit(tile))
	if (SplitSide(tile) == SplitDirection(tile)) return 0;

    /*
     * Check design rules along a horizontal boundary between two tiles.
     *
     *			 4	tile	    3
     *			--L----------------R--
     *			 1	tpbot	    2
     *
     * The labels "L" and "R" indicate pointL and pointR respectively.
     * If a rule's direction is FORWARD, then check from bottom to top.
     *
     *      * Check the top left corner if the 1x1 lambda square on the bottom
     *        left corner (1) of pointL matches the design rule's "corner" mask.
     *
     *      * Check the top right corner if the rule says check BOTHCORNERS and
     *        the 1x1 lambda square on the bottom right (2) corner of pointR
     *	      matches the design rule's "corner" mask.
     *
     * If a rule's direction is REVERSE, then check from top to bottom.
     *
     *	    * Check the bottom right corner if the 1x1 lambda square on the top
     *	      right corner (3) of pointR matches the design rule's "corner"
     *	      mask.
     *
     *	    * Check the bottom left corner if the rule says check BOTHCORNERS
     *	      and the 1x1 lambda square on the top left corner (4) of pointL
     *	      matches the design rule's "corner" mask.
     */

    if (BOTTOM(tile) >= rect->r_ybot)
    {
	Tile *tpbot, *tpx;
	TileType tt, to;
	int edgeLeft, edgeRight;
        int left = MAX(LEFT(tile), rect->r_xbot);
        int right = MIN(RIGHT(tile), rect->r_xtop);
	int edgeY = BOTTOM(tile);

	/* Go right across bottom of tile */
	firsttile = TRUE;
        for (tpbot = LB(tile); LEFT(tpbot) < right; tpbot = TR(tpbot))
        {
	    /* Get the tile types to the top and bottom of the edge */

	    tt = TiGetBottomType(tile);
	    to = TiGetTopType(tpbot);

	    /* Don't check synthetic edges, i.e. edges with same type on
             * both sides.  Such "edges" have no physical significance, and
	     * depend on internal-details of how paint is spit into tiles.
	     * Thus checking them just leads to confusion.  (When edge rules
	     * involving such edges are encountered during technology readin
	     * the user is warned that such edges are not checked).
	     */

	    if (tt == to) continue;

	    /*
	     * Check to insure that we are inside the clip area.
	     * Go through list of design rules triggered by the
	     * bottom-to-top edge.
	     */
	    edgeLeft = MAX(LEFT(tpbot), left);
	    edgeRight = MIN(RIGHT(tpbot), right);
	    if (edgeLeft >= edgeRight)
		continue;

	    triggered = 0;
	    for (cptr = DRCCurStyle->DRCRulesTbl[to][tt]; cptr != (DRCCookie *) NULL;
				cptr = cptr->drcc_next)
	    {
		if (cptr->drcc_flags & DRC_ANGLES) continue;

		/* Find the rule distances according to the scale factor */
		dist = cptr->drcc_dist;
		cdist = cptr->drcc_cdist;
		trigpending = (cptr->drcc_flags & DRC_TRIGGER) ? TRUE : FALSE;

		/* drcc_edgeplane is used to avoid checks on edges	*/
		/* in more than one plane				*/

		if (arg->dCD_plane != cptr->drcc_edgeplane)
		{
		    if (trigpending) cptr = cptr->drcc_next;
		    continue;
		}

		DRCstatRules++;

		/* top to bottom */

		if ((cptr->drcc_flags & (DRC_MAXWIDTH | DRC_BENDS)) ==
				(DRC_MAXWIDTH | DRC_BENDS))
		{
		    /* New algorithm --- Tim 3/6/05 */
		    Rect *lr;
		    int i;

		    if (!trigpending) cptr->drcc_dist++;

		    if (cptr->drcc_flags & DRC_REVERSE)
			mrd = drcCanonicalMaxwidth(tpbot, GEO_SOUTH, arg, cptr);
		    else if (firsttile)
			mrd = drcCanonicalMaxwidth(tile, GEO_NORTH, arg, cptr);
		    else
			mrd = NULL;
		    if (!trigpending) cptr->drcc_dist--;
		    if (trigpending)
		    {
			if (mrd)
			    triggered = mrd->entries;
			else
			    cptr = cptr->drcc_next;
		    }
		    else if (mrd)
		    {
			for (i = 0; i < mrd->entries; i++)
			{
			    lr = &mrd->rlist[i];
			    GeoClip(lr, arg->dCD_clip);
			    if (!GEO_RECTNULL(lr))  
			     {
				(*(arg->dCD_function)) (arg->dCD_celldef,
					lr, cptr, arg->dCD_clientData);
				(*(arg->dCD_errors))++;
			    }           
			}
		    }
		    continue;
		}
		else if (cptr->drcc_flags & (DRC_AREA | DRC_RECTSIZE
				| DRC_MAXWIDTH))
		{
		    /* only have to do these checks in one direction */
		    if (trigpending) cptr = cptr->drcc_next;
		    continue;
		}
		else if (!triggered) mrd = NULL;

		result = 0;
		arg->dCD_radial = 0; 
		do {
		    if (triggered)
		    {
			/* For triggered rules, we want to look	at the	*/
			/* clipped region found by the triggering rule	*/

			if (mrd)
			    errRect = mrd->rlist[--triggered];
			else
			    errRect = arg->dCD_rlist[--triggered];
			errRect.r_xtop += cdist;
			errRect.r_xbot -= cdist;
			if (errRect.r_xtop > edgeRight) errRect.r_xtop = edgeRight;
			if (errRect.r_xbot < edgeLeft) errRect.r_xbot = edgeLeft;
		    }
		    else
		    {
			errRect.r_xbot = edgeLeft;
			errRect.r_xtop = edgeRight;
		    }

		    if (cptr->drcc_flags & DRC_REVERSE)
		    {
			/*
			 * Determine corner extensions.
			 * Find the point (3) to the top right of pointR
			 */
			if (RIGHT(tile) <= errRect.r_xtop)
			    for (tpx = TR(tile); BOTTOM(tpx) > edgeY; tpx = LB(tpx));
			else tpx = tile;

		 	if (TTMaskHasType(&cptr->drcc_corner, TiGetLeftType(tpx)))
			{
			    errRect.r_xtop += cdist;
			    if (DRCEuclidean)
				arg->dCD_radial |= RADIAL_SE;
			}

			if (cptr->drcc_flags & DRC_BOTHCORNERS)
			{
			    /*
			     * Check the other corner by finding the
			     * point (4) to the top left of pointL.
			     */

			    if (LEFT(tile) >= errRect.r_xbot) tpx = BL(tile);
			    else tpx = tile;

			    if (TTMaskHasType(&cptr->drcc_corner, TiGetRightType(tpx)))
			    {
				errRect.r_xbot -= cdist;
				if (DRCEuclidean)
				    arg->dCD_radial |= RADIAL_SW;
			    }
			}

			/*
			 * Just for grins, see if we could avoid
			 * a messy search by looking only at tpbot.
			 */
			errRect.r_ybot = edgeY - dist;
			if (cptr->drcc_flags & DRC_OUTSIDE) errRect.r_ybot--;
			if (BOTTOM(tpbot) <= errRect.r_ybot
				&& LEFT(tpbot) <= errRect.r_xbot
				&& RIGHT(tpbot) >= errRect.r_xtop
				&& arg->dCD_plane == cptr->drcc_plane
				&& TTMaskHasType(&cptr->drcc_mask,
				TiGetTopType(tpbot)))
			    continue;

			errRect.r_ytop = edgeY;
			if (cptr->drcc_flags & DRC_OUTSIDE) errRect.r_ytop -= dist;
			arg->dCD_initial = tile;
		    }
		    else  /* FORWARD */
		    {
			/*
			 * Determine corner extensions.
			 * Find the point (1) to the bottom left of pointL
			 */

			if (LEFT(tpbot) >= errRect.r_xbot)
			    for (tpx = BL(tpbot); TOP(tpx) < edgeY; tpx = RT(tpx));
			else tpx = tpbot;

			if (TTMaskHasType(&cptr->drcc_corner, TiGetRightType(tpx)))
			{
			    errRect.r_xbot -= cdist;
			    if (DRCEuclidean)
				arg->dCD_radial |= RADIAL_NW;
			}

			if (cptr->drcc_flags & DRC_BOTHCORNERS)
			{
			    /*
			     * Check the other corner by finding the
			     * point (2) to the bottom right of pointR.
			     */
			    if (RIGHT(tpbot) <= errRect.r_xtop) tpx = TR(tpbot);
			    else tpx = tpbot;

			    if (TTMaskHasType(&cptr->drcc_corner, TiGetLeftType(tpx)))
			    {
				errRect.r_xtop += cdist;
				if (DRCEuclidean)
				    arg->dCD_radial |= RADIAL_NE;
			    }
			}

			/*
			 * Just for grins, see if we could avoid
			 * a messy search by looking only at tile.
			 */
			errRect.r_ytop = edgeY + dist;
			if (cptr->drcc_flags & DRC_OUTSIDE) errRect.r_ytop++;
			if (TOP(tile) >= errRect.r_ytop
				&& LEFT(tile) <= errRect.r_xbot
				&& RIGHT(tile) >= errRect.r_xtop
				&& arg->dCD_plane == cptr->drcc_plane
				&& TTMaskHasType(&cptr->drcc_mask,
				TiGetBottomType(tile)))
			    continue;

			errRect.r_ybot = edgeY;
			if (cptr->drcc_flags & DRC_OUTSIDE) errRect.r_ybot += dist;
			arg->dCD_initial = tpbot;
		    }
		    if (arg->dCD_radial)
		    {
			arg->dCD_radial &= 0xf000;
			arg->dCD_radial |= (0xfff & cdist);
		    }

		    DRCstatSlow++;
		    arg->dCD_cptr = cptr;
		    arg->dCD_entries = 0;
		    TTMaskCom2(&tmpMask, &cptr->drcc_mask);
		    TTMaskClearType(&tmpMask, TT_ERROR_S);
		    DBSrPaintArea((Tile *) NULL,
				arg->dCD_celldef->cd_planes[cptr->drcc_plane],
				&errRect, &tmpMask, areaCheck, (ClientData) arg);
		} while (triggered);

		if (arg->dCD_entries == 0)
		{
		    /* Trigger rule:  If rule check found errors,	*/
		    /* do the next rule.  Otherwise, skip it.	*/

		    if (trigpending)
			cptr = cptr->drcc_next;
		}
		else
		    triggered = arg->dCD_entries;
	    }
	    DRCstatEdges++;
	    firsttile = FALSE;
        }
    }
    return (0);
}
