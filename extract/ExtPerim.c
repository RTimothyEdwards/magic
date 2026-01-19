/*
 * ExtPerim.c --
 *
 * Circuit extraction.
 * Functions for tracing the perimeter of a tile or of a
 * connected region.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/extract/ExtPerim.c,v 1.5 2010/09/12 20:32:33 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <math.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/malloc.h"
#include "utils/stack.h"
#include "debug/debug.h"
#include "extract/extract.h"
#include "extract/extractInt.h"

#define	POINTEQUAL(p, q)	(((p)->p_x == (q)->p_x) && ((p)->p_y == (q)->p_y))

/*
 * ----------------------------------------------------------------------------
 *
 * extEnumTilePerim --
 *
 * Visit all the tiles along the perimeter of 'tpIn' whose types are in
 * the mask 'mask'.  For each such tile, call the supplied function 'func'
 * if it is non-null:
 *
 *	(*func)(bp, cdata)
 *	    Boundary *bp;	/# bp->b_inside is tpIn, bp->b_outside
 *				 # is the tile along the boundary of tpIn,
 *				 # and bp->b_segment is the segment of the
 *				 # boundary in common.
 *				 #/
 *	    ClientData cdata;	/# Supplied in call to extEnumTilePerim #/
 *	{
 *	}
 *
 * The value returned by the callback function is ignored.
 *
 * Results:
 *	Returns the total length of the portion of the perimeter of
 *	'tpIn' that borders tiles whose types are in 'mask'.
 *
 * Side effects:
 *	None directly, but applies the client's filter function
 *	to each qualifying segment of the boundary.
 *
 * Note:
 *	The width/length calculation method is manhattan-only, and will
 *	likely need correcting.  This routine computes the true perimeter
 *	length and calls the callback function on the perimeter tiles of
 *	the correct tile type.
 *
 * Non-interruptible.
 *
 * ----------------------------------------------------------------------------
 */

int
extEnumTilePerim(
    Tile *tpIn,
    TileType dinfo,
    const TileTypeBitMask *maskp,
    int pNum,			/* Plane of perimeter */
    int (*func)(),
    ClientData cdata)
{
    TileTypeBitMask mask = *maskp;
    Tile *tpOut;
    int perimCorrect;
    Boundary b;
    unsigned char sides = 0;	/* Sides to be ignored */

    b.b_inside = tpIn;
    b.b_plane = pNum;
    perimCorrect = 0;

    if (IsSplit(tpIn))
    {
	/* Handle a diagonal boundary across a split tile.
	 * "dinfo" determines which side of the split tile is considered "inside" the
	 * boundary and which is "outside".  Invoke the callback function for the
	 * diagonal, then determine which two sides don't need to be searched and
	 * set the corresponding boundary direction bit in "sides".
	 */

	TileType otype = (dinfo & TT_SIDE) ? SplitLeftType(tpIn): SplitRightType(tpIn);
	TileType itype = (dinfo & TT_SIDE) ? SplitRightType(tpIn): SplitLeftType(tpIn);

	if (TTMaskHasType(&mask, otype))
	{
	    int width = RIGHT(tpIn) - LEFT(tpIn);
	    int height = TOP(tpIn) - BOTTOM(tpIn);
	    perimCorrect = width * width + height * height;
	    perimCorrect = (int)sqrt((double)perimCorrect);
	}

	/* Invoke the callback function on diagonal boundaries */

	b.b_outside = tpIn;	/* Same tile on both sides of the boundary */
	TiToRect(tpIn, &b.b_segment);
	
	if (SplitDirection(tpIn))
	{
	    if (dinfo & TT_SIDE)
		b.b_direction = BD_NE;
	    else
		b.b_direction = BD_SW;
	}
	else
	{
	    if (dinfo & TT_SIDE)
		b.b_direction = BD_SE;
	    else
		b.b_direction = BD_NW;
	}
	if (func) (*func)(&b, cdata);

	/* Flag which two sides of the tile don't need searching */

	sides = (dinfo & TT_SIDE) ? BD_LEFT : BD_RIGHT;
	sides |= (((dinfo & TT_SIDE) ? 1 : 0) == SplitDirection(tpIn)) ?
		BD_BOTTOM : BD_TOP;
    }
    else
	sides = 0;

    /* Top */
    if (!(sides & BD_TOP))
    {
	b.b_segment.r_ybot = b.b_segment.r_ytop = TOP(tpIn);
	b.b_direction = BD_TOP;
	for (tpOut = RT(tpIn); RIGHT(tpOut) > LEFT(tpIn); tpOut = BL(tpOut))
	{
	    if (TTMaskHasType(&mask, TiGetBottomType(tpOut)))
	    {
		b.b_segment.r_xbot = MAX(LEFT(tpIn), LEFT(tpOut));
		b.b_segment.r_xtop = MIN(RIGHT(tpIn), RIGHT(tpOut));
		b.b_outside = tpOut;
		if (func) (*func)(&b, cdata);
	    }
	}
    }

    /* Bottom */
    if (!(sides & BD_BOTTOM))
    {
	b.b_segment.r_ybot = b.b_segment.r_ytop = BOTTOM(tpIn);
	b.b_direction = BD_BOTTOM;
	for (tpOut = LB(tpIn); LEFT(tpOut) < RIGHT(tpIn); tpOut = TR(tpOut))
	{
	    if (TTMaskHasType(&mask, TiGetTopType(tpOut)))
	    {
		b.b_segment.r_xbot = MAX(LEFT(tpIn), LEFT(tpOut));
		b.b_segment.r_xtop = MIN(RIGHT(tpIn), RIGHT(tpOut));
		b.b_outside = tpOut;
		if (func) (*func)(&b, cdata);
	    }
	}
    }

    /* Left */
    if (!(sides & BD_LEFT))
    {
	b.b_segment.r_xbot = b.b_segment.r_xtop = LEFT(tpIn);
	b.b_direction = BD_LEFT;
	for (tpOut = BL(tpIn); BOTTOM(tpOut) < TOP(tpIn); tpOut = RT(tpOut))
	{
	    if (TTMaskHasType(&mask, TiGetRightType(tpOut)))
	    {
		b.b_segment.r_ybot = MAX(BOTTOM(tpIn), BOTTOM(tpOut));
		b.b_segment.r_ytop = MIN(TOP(tpIn), TOP(tpOut));
		b.b_outside = tpOut;
		if (func) (*func)(&b, cdata);
	    }
	}
    }

    /* Right */
    if (!(sides & BD_RIGHT))
    {
	b.b_segment.r_xbot = b.b_segment.r_xtop = RIGHT(tpIn);
	b.b_direction = BD_RIGHT;
	for (tpOut = TR(tpIn); TOP(tpOut) > BOTTOM(tpIn); tpOut = LB(tpOut))
	{
	    if (TTMaskHasType(&mask, TiGetLeftType(tpOut)))
	    {
		b.b_segment.r_ybot = MAX(BOTTOM(tpIn), BOTTOM(tpOut));
		b.b_segment.r_ytop = MIN(TOP(tpIn), TOP(tpOut));
		b.b_outside = tpOut;
		if (func) (*func)(&b, cdata);
	    }
	}
    }

    return (perimCorrect);
}
