/* grClip.c -
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
 * This file contains additional functions to manipulate a
 * color display.  Included here are rectangle clipping and
 * drawing routines.
 */


#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/graphics/grClip.c,v 1.4 2010/06/24 12:37:18 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "utils/styles.h"
#include "database/database.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "graphics/graphicsInt.h"
#include "utils/malloc.h"

/* Forward declaration: */

extern bool GrDisjoint();
extern void GrClipTriangle();

/* The following rectangle defines the size of the cross drawn for
 * zero-size rectangles.  This must be all on one line to keep
 * lintpick happy!
 */

global Rect GrCrossRect = {-GR_CROSSSIZE, -GR_CROSSSIZE, GR_CROSSSIZE, GR_CROSSSIZE};
global int GrNumClipBoxes = 0;	/* for benchmarking */
global int grCurDStyle;
global unsigned char GrGridMultiple = 1;

/* A rectangle that is one square of the grid */
static Rect *grGridRect;

/*
 * ----------------------------------------------------------------------------
 * GrSetStuff --
 *
 *	Set up current drawing style.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Variables are changed.  If anything is drawn, it will appear in the
 *	specified style.
 * ----------------------------------------------------------------------------
 */

/* Current state for rectangle and text drawing */
static int grCurWMask, grCurStipple;
#ifndef MAGIC_WRAPPER
static 				/* Used in grTkCommon.c, so don't make static */
#endif
int grCurOutline, grCurFill, grCurColor;

/* Has the device driver been informed of the above?  We only inform it
 * when we actually need to draw something -- this makes it harmless (in terms
 * of graphics bandwidth) to call GrSetStuff extra times.
 */
bool grDriverInformed = TRUE;

void
GrSetStuff(style)
    int style;
{
    grCurDStyle = style;
    grCurWMask = GrStyleTable[style].mask;
    grCurColor = GrStyleTable[style].color;
    grCurOutline = GrStyleTable[style].outline;
    grCurStipple = GrStyleTable[style].stipple;
    grCurFill = GrStyleTable[style].fill;
    grDriverInformed = FALSE;
}

/*---------------------------------------------------------------------------
 * grInformDriver:
 *
 *	Inform the driver about the last GrSetStuff call.
 *
 * Results:	
 *	None.
 *
 * Side Effects:
 *	None.
 *
 *----------------------------------------------------------------------------
 */

void
grInformDriver()
{
    /* Now let the device drivers know */
    (*grSetWMandCPtr)(grCurWMask, grCurColor);
    (*grSetLineStylePtr)(grCurOutline);
    (*grSetStipplePtr)(grCurStipple);
    grDriverInformed = TRUE;
}


/*
 * ----------------------------------------------------------------------------
 * grClipAgainst --
 *
 *	Clip a linked list of rectangles against a single rectangle.  This
 *	may result in the list getting longer or shorter.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The original list may change.
 * ----------------------------------------------------------------------------
 */

void
grClipAgainst(startllr, clip)
    LinkedRect **startllr;	/* A pointer to the pointer that heads 
				 * the list .
				 */
    Rect *clip;		  	/* The rectangle to clip against */
{
    extern bool grClipAddFunc();	/* forward declaration */
    LinkedRect **llr, *lr;

    for (llr = startllr; *llr != (LinkedRect *) NULL; /*nop*/ )
    {
	if ( GEO_TOUCH(&(*llr)->r_r, clip) )
	{
	    lr = *llr;
	    *llr = lr->r_next;
	    /* this will modify the list that we are traversing! */
	    (void) GrDisjoint(&lr->r_r, clip, grClipAddFunc, 
		    (ClientData) &llr);
	    freeMagic( (char *) lr);
	}
	else
	    llr = &((*llr)->r_next);
    }
}


/* Add a box to our linked list, and advance
 * our pointer into the list
 */

bool grClipAddFunc(box, cd)  
    Rect *box;
    ClientData cd;
{
    LinkedRect ***lllr, *lr;

    lllr = (LinkedRect ***) cd;

    lr = (LinkedRect *) mallocMagic((unsigned) (sizeof (LinkedRect)));
    lr->r_r = *box;
    lr->r_next = **lllr;
    **lllr = lr;
    *lllr = &lr->r_next;

    return TRUE;
}


void
grObsBox(r)
    Rect *r;
{
    LinkedRect *ob;
    LinkedRect *ar;
    LinkedRect **areas;

    ar = (LinkedRect *) mallocMagic((unsigned) (sizeof (LinkedRect)));
    ar->r_r = *r;
    ar->r_next = NULL;
    areas = &ar;

    /* clip against obscuring areas */
    for (ob = grCurObscure; ob != NULL; ob = ob->r_next)
    {
	if ( GEO_TOUCH(r, &(ob->r_r)) )
	    grClipAgainst(areas, &(ob->r_r));
    }

    while (*areas != NULL)
    {
	LinkedRect *oldarea;
	if (grCurFill == GR_STGRID)
	    (*grDrawGridPtr)(grGridRect, grCurOutline, &((*areas)->r_r));
	else
	    (*grFillRectPtr)(&((*areas)->r_r));
	oldarea = *areas;
	*areas = (*areas)->r_next;
	freeMagic( (char *) oldarea );
    }
}


/*---------------------------------------------------------
 * grClipPoints:
 *	This routine computes the 0, 1, or 2 intersection points
 *	between a line and a box.
 *
 * Results:
 *	FALSE if the line is completely outside of the box.
 *
 * Side Effects:
 *---------------------------------------------------------
 */

bool
grClipPoints(line, box, p1, p1OK, p2, p2OK)
    Rect *line;		/* Actually a line from line->r_ll to
			 * line->r_ur.  It is assumed that r_ll is to
			 * the left of r_ur, but we don't assume that
			 * r_ll is below r_ur.
			 */
    Rect *box;		/* A box to check intersections with */
    Point *p1, *p2;	/* To be filled in with 0, 1, or 2 points
			 * that are on the border of the box as well as
			 * on the line.
			 */
    bool *p1OK, *p2OK;	/* Says if the point was filled in */
{
    int tmp, delx, dely;
    bool delyneg;
    int x1, x2, y1, y2;
    bool ok1, ok2;

    if (p1OK != NULL) *p1OK = FALSE;
    ok1 = FALSE;
    if (p2OK != NULL) *p2OK = FALSE;
    ok2 = FALSE;

    x1 = line->r_xbot;
    x2 = line->r_xtop;
    y1 = line->r_ybot;
    y2 = line->r_ytop;

    delx = x2-x1;
    dely = y2-y1;

    /* We have to be careful because of machine-dependent problems
     * with rounding during division by negative numbers.
     */

    if (dely<0)
    {
	dely = -dely;
	delyneg = TRUE;
    }
    else 
	delyneg = FALSE;
    /* we know that delx is nonnegative if this is a real (non-empty) line */
    if (delx < 0) return FALSE;

    if (x1 < box->r_xbot)
    {
	if (delx == 0) return FALSE;
	tmp = (((box->r_xbot-x1)*dely) + (delx>>1))/delx;
	if (delyneg) y1 -= tmp;
	else y1 += tmp;
	x1 = box->r_xbot;
    }
    else 
	if (x1 > box->r_xtop) return FALSE;

    if (x2 > box->r_xtop)
    {
	if (delx == 0) return FALSE;
	tmp = ((x2-box->r_xtop)*dely + (delx>>1))/delx;
	if (delyneg) y2 += tmp;
	else y2 -= tmp;
	x2 = box->r_xtop;
    }
    else 
	if (x2 < box->r_xbot) return FALSE;

    if (y2 > y1)
    {
	if (y1 < box->r_ybot)
	{
	    x1 += (((box->r_ybot-y1)*delx) + (dely>>1))/dely;
	    y1 = box->r_ybot;
	}
	else if (y1 > box->r_ytop) return FALSE;
	if (y2 > box->r_ytop)
	{
	    x2 -= (((y2 - box->r_ytop)*delx) + (dely>>1))/dely;
	    y2 = box->r_ytop;
	}
	else if (y2 < box->r_ybot) return FALSE;
    }
    else
    {
	if (y1 > box->r_ytop)
	{
	    if (dely == 0) return FALSE;
	    x1 += (((y1-box->r_ytop)*delx) + (dely>>1))/dely;
	    y1 = box->r_ytop;
	}
	else if (y1 < box->r_ybot) return FALSE;
	if (y2 < box->r_ybot)
	{
	    if (dely == 0) return FALSE;
	    x2 -= (((box->r_ybot-y2)*delx) + (dely>>1))/dely;
	    y2 = box->r_ybot;
	}
	else if (y2 > box->r_ytop) return FALSE;
    }

    if ( (x1 == box->r_xbot) || (y1 == box->r_ybot) || (y1 == box->r_ytop) )
    {
	if (p1 != NULL)
	{
	    p1->p_x = x1;
	    p1->p_y = y1;
	}
	if (p1OK != NULL) *p1OK = TRUE;
	ok1 = TRUE;
    }
    if ( (x2 == box->r_xtop) || (y2 == box->r_ybot) || (y2 == box->r_ytop) )
    {
	if (p2 != NULL)
	{
	    p2->p_x = x2;
	    p2->p_y = y2;
	}
	if (p2OK != NULL) *p2OK = TRUE;
	ok2 = TRUE;
    }
    /* is part of the line in the box? */
    return ok1 || ok2 || 
	    ((x1 >= box->r_xbot) && (x1 <= box->r_xtop) && (y1 >= box->r_ybot)
	    && (y1 <= box->r_ytop));
}


#define NEWAREA(lr,x1,y1,x2,y2)	{LinkedRect *tmp; \
    tmp = (LinkedRect *) mallocMagic((unsigned) (sizeof (LinkedRect))); \
    tmp->r_r.r_xbot = x1; tmp->r_r.r_xtop = x2; \
    tmp->r_r.r_ybot = y1; tmp->r_r.r_ytop = y2; tmp->r_next = lr; lr = tmp;}

/*---------------------------------------------------------
 * GrClipLine:
 *	GrClipLine will draw a line on the screen in the current
 *	style and clip stuff.
 *
 * Results:	None.
 *
 * Side Effects:
 *	The line is drawn in the current style.
 *---------------------------------------------------------
 */

void
GrClipLine(x1, y1, x2, y2)
    int x1, y1, x2, y2;
{
    LinkedRect **ar;
    LinkedRect *ob;
    LinkedRect *areas;

    GR_CHECK_LOCK();
    if (!grDriverInformed) grInformDriver();

    /* we will pretend the the ll corner of a rectangle is the
     * left endpoint of a line, and the ur corner the right endpoint
     * of the line.
     */
    areas = (LinkedRect *) mallocMagic((unsigned) (sizeof (LinkedRect)));
    areas->r_next = NULL;
    if (x1 < x2)
    {
	areas->r_r.r_xbot = x1;
	areas->r_r.r_ybot = y1;
	areas->r_r.r_xtop = x2;
	areas->r_r.r_ytop = y2;
    }
    else
    {
	areas->r_r.r_xtop = x1;
	areas->r_r.r_ytop = y1;
	areas->r_r.r_xbot = x2;
	areas->r_r.r_ybot = y2;
    }

    /* clip against the clip box */
    for (ar = &areas; *ar != NULL; )
    {
	Rect *l;
	Rect canonRect;
	l = &((*ar)->r_r);
	GeoCanonicalRect(l, &canonRect);
	if (!GEO_TOUCH(&canonRect, &grCurClip))
	{
	    /* line is totally outside of clip area */
	    goto deleteit;
	}
	else
	{
	    /* is there some intersection with clip area? */
	    if (!grClipPoints(l, &grCurClip, &(l->r_ll), (bool *) NULL,
		    &(l->r_ur), (bool*) NULL))
	    {
		/* no intersection */
		goto deleteit;
	    }

	    /* clip against obscuring areas */
	    for (ob = grCurObscure; ob != NULL; ob = ob->r_next)
	    {
		Point p1, p2;
		Rect c;
		bool ok1, ok2;
		c = ob->r_r;
		c.r_xbot--;  c.r_ybot--;
		c.r_xtop++;  c.r_ytop++;
		if (grClipPoints(l, &c, &p1, &ok1, &p2, &ok2) &&
			!ok1 && !ok2)
		{
		    /* Line is not completely outside of the box,
		     * nor does it intersect it.
		     * Therefore, line is completely obscured.
		     */
		     goto deleteit;
		}

		if (ok1 && 
		   ( ((l->r_xbot == p1.p_x) && (l->r_ybot == p1.p_y)) ||
		     ((l->r_xtop == p1.p_x) && (l->r_ytop == p1.p_y)) ) )
		{
		    ok1 = FALSE;  /* do not split or clip at an endpoint */
		}
		if (ok2 && 
		   ( ((l->r_xbot == p2.p_x) && (l->r_ybot == p2.p_y)) ||
		     ((l->r_xtop == p2.p_x) && (l->r_ytop == p2.p_y)) ) )
		{
		    ok2 = FALSE;  /* do not split or clip at an endpoint */
		}

		if (ok1 ^ ok2)
		{
		    /* one segment to deal with */
		    if (ok1)
			l->r_ur = p1;
		    else
			l->r_ll = p2;
		}
		else if (ok1 && ok2)
		{
		    /* clip both sides */
		    LinkedRect *new;
		    new = (LinkedRect *) mallocMagic((unsigned) (sizeof (LinkedRect)));
		    new->r_r.r_ur = l->r_ur;
		    new->r_r.r_ll = p2;
		    new->r_next = (*ar);
		    l->r_ur = p1;
		    (*ar) = new;
		}
	    }

	}

	ar = &((*ar)->r_next);
	continue;

deleteit: {
	    LinkedRect *reclaim;
	    reclaim = (*ar);
	    *ar = reclaim->r_next;
	    freeMagic( (char *) reclaim);
	}

    } /* for ar */


    /* draw the lines */
    while (areas != NULL)
    {
	LinkedRect *oldarea;
	(*grDrawLinePtr)(areas->r_r.r_xbot, areas->r_r.r_ybot,
		areas->r_r.r_xtop, areas->r_r.r_ytop);
	oldarea = areas;
	areas = areas->r_next;
	freeMagic( (char *) oldarea );
    }
}

/*
 *---------------------------------------------------------
 * grAddSegment:
 *	Add a segment to a linked list of rectangles
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates memory;  appends LinkedRect structure to
 *	list "segments".
 *
 * Notes:
 *	INLINE this for speedup?
 *---------------------------------------------------------
 */

void
grAddSegment(llx, lly, urx, ury, segments)
    int llx, lly, urx, ury;
    LinkedRect **segments;
{
    LinkedRect *curseg;

    curseg = (LinkedRect *)mallocMagic(sizeof(LinkedRect));
    curseg->r_r.r_xbot = llx;
    curseg->r_r.r_ybot = lly;
    curseg->r_r.r_xtop = urx;
    curseg->r_r.r_ytop = ury;
    curseg->r_next = *segments;
    *segments = curseg;
}

/*---------------------------------------------------------
 * GrBoxOutline:
 *	For box outlines, works around the boundary of
 *	the box according to the associated tile structure,
 *	and returns a linked rect list defining all the
 *	segments which need to be drawn.
 *
 * Results:
 *	TRUE if tile is isolated (GrFastBox can be used).
 *	Otherwise, result is FALSE.
 *
 * Side Effects:
 *	May allocate memory for a linked rect structure,
 *	with pointer returned in tilesegs.  If non-NULL,
 *	the calling function needs to free memory for the
 *	linked rect structure.
 *
 * Implementation notes:
 *	Standard tech files define most bordered styles as
 *	contact types, and most if not all contacts are
 *	required to be square.  So we want the case of an
 *	isolated tile to go fast, avoiding any calls to
 *	allocate memory.  This complicates the routine but
 *	keeps the routine from slowing down the layout
 *	rendering.
 *---------------------------------------------------------
 */

bool
GrBoxOutline(tile, tilesegs)
    Tile *tile;
    LinkedRect **tilesegs;
{
    Rect rect;
    TileType ttype;
    LinkedRect *curseg;
    Tile *tpleft, *tpright, *tptop, *tpbot;
    int edgeTop, edgeBot, edgeRight, edgeLeft;
    int isolate = 0;
    bool sense;

    *tilesegs = NULL;
    TiToRect(tile, &rect);

    if (IsSplit(tile) && SplitSide(tile))
	isolate |= 0x1;
    else
    {
	ttype = TiGetLeftType(tile);
	edgeBot = rect.r_ybot;
	sense = TRUE;
	for (tpleft = BL(tile); BOTTOM(tpleft) < rect.r_ytop; tpleft = RT(tpleft))
	{
	    if (TiGetRightType(tpleft) != ttype)
	    {
		if (!sense)
		{
		    edgeBot = BOTTOM(tpleft);
		    if (TOP(tpleft) >= rect.r_ytop)
			grAddSegment(rect.r_xbot, edgeBot, rect.r_xbot, rect.r_ytop,
					tilesegs);
		    sense = TRUE;
		}
	    }
	    else
	    {
		if (sense)
		{
		    edgeTop = BOTTOM(tpleft);
		    if (edgeTop > edgeBot)
			grAddSegment(rect.r_xbot, edgeBot, rect.r_xbot, edgeTop,
				tilesegs);
		    isolate |= 0x1;
		    sense = FALSE;
		}
	    }
	}
    }
    if (IsSplit(tile) && !SplitSide(tile))
	isolate |= 0x2;
    else
    {
	ttype = TiGetRightType(tile);
	edgeTop = rect.r_ytop;
	sense = TRUE;
	for (tpright = TR(tile); TOP(tpright) > rect.r_ybot; tpright = LB(tpright))
	{
	    if (TiGetLeftType(tpright) != ttype)
	    {
		if (!sense)
		{
		    edgeTop = TOP(tpright);
		    if (BOTTOM(tpright) <= rect.r_ybot)
			grAddSegment(rect.r_xtop, rect.r_ybot, rect.r_xtop, edgeTop,
					tilesegs);
		    sense = TRUE;
		}
	    }
	    else
	    {
		if (sense)
		{
		    edgeBot = TOP(tpright);
		    if (edgeBot < edgeTop)
			grAddSegment(rect.r_xtop, edgeBot, rect.r_xtop, edgeTop,
					tilesegs);
		    isolate |= 0x2;
		    sense = FALSE;
		}
	    }
	}
    }

    if (IsSplit(tile) &&
		(SplitSide(tile) == SplitDirection(tile)))
	isolate |= 0x4;
    else
    {
	ttype = TiGetBottomType(tile);
	edgeLeft = rect.r_xbot;
	sense = TRUE;
	for (tpbot = LB(tile); LEFT(tpbot) < rect.r_xtop; tpbot = TR(tpbot))
	{
	    if (TiGetTopType(tpbot) != ttype)
	    {
		if (!sense)
		{
		    edgeLeft = LEFT(tpbot);
		    if (RIGHT(tpbot) >= rect.r_xtop)
			grAddSegment(edgeLeft, rect.r_ybot, rect.r_xtop, rect.r_ybot,
					tilesegs);
		    sense = TRUE;
		}
	    }
	    else
	    {
		if (sense)
		{
		    edgeRight = LEFT(tpbot);
		    if (edgeRight > edgeLeft)
			grAddSegment(edgeLeft, rect.r_ybot, edgeRight, rect.r_ybot,
					tilesegs);
		    isolate |= 0x4;
		    sense = FALSE;
		}
	    }
	}
    }

    if (IsSplit(tile) &&
		(SplitSide(tile) != SplitDirection(tile)))
	isolate |= 0x8;
    else
    {
	ttype = TiGetTopType(tile);
	edgeRight = rect.r_xtop;
	sense = TRUE;
	for (tptop = RT(tile); RIGHT(tptop) > rect.r_xbot; tptop = BL(tptop))
	{
	    if (TiGetBottomType(tptop) != ttype)
	    {
		if (!sense)
		{
		    edgeRight = RIGHT(tptop);
		    if (LEFT(tptop) <= rect.r_xbot)
			grAddSegment(rect.r_xbot, rect.r_ytop, edgeRight, rect.r_ytop,
					tilesegs);
		    sense = TRUE;
		}
	    }
	    else
	    {
		if (sense)
		{
		    edgeLeft = RIGHT(tptop);
		    if (edgeLeft < edgeRight)
			grAddSegment(edgeLeft, rect.r_ytop, edgeRight, rect.r_ytop,
					tilesegs);
		    isolate |= 0x8;
		    sense = FALSE;
		}
	    }
	}
    }

    if (isolate == 0)	/* Common case */
	return TRUE;
    else
    {
	/* Need to malloc segments for isolated sides */
	if (!(isolate & 0x1))	/* Left */
	    grAddSegment(rect.r_xbot, rect.r_ybot, rect.r_xbot, rect.r_ytop,
			tilesegs);
	if (!(isolate & 0x2))	/* Right */
	    grAddSegment(rect.r_xtop, rect.r_ybot, rect.r_xtop, rect.r_ytop,
			tilesegs);
	if (!(isolate & 0x4))	/* Bottom */
	    grAddSegment(rect.r_xbot, rect.r_ybot, rect.r_xtop, rect.r_ybot,
			tilesegs);
	if (!(isolate & 0x8))	/* Top */
	    grAddSegment(rect.r_xbot, rect.r_ytop, rect.r_xtop, rect.r_ytop,
			tilesegs);
	return FALSE;
    }
}

/* Threshold for determining if outlines are too small to draw */
#define GR_THRESH	4

/*---------------------------------------------------------
 * GrBox --
 *
 *	GrBox draws a rectangle on the screen in the style
 *	set by the previous call to GrSetStuff.   It will
 *	be clipped against the rectangles passed to GrSetStuff.
 *
 *	Unlike GrFastBox (see below), GrBox makes no assumptions
 *	about how the outline will be drawn.  If there is no
 *	outline, GrFastBox is called.  The tile border is checked,
 *	and if the tile outline is simple, GrFastBox is called.
 *	Otherwise, we do a fast fill a la GrFastBox but draw the
 *	outline segment by segment.
 *
 * Results:	None.
 *
 * Side Effects:
 *	The rectangle is drawn in the style specified.
 *	The rectangle may be clipped before being drawn.
 *---------------------------------------------------------
 */
void
GrBox(MagWindow *mw, Transform *trans, Tile *tile)
{
    Rect r, r2, clipr;
    bool needClip, needObscure, simpleBox;
    LinkedRect *ob, *tilesegs, *segptr;
    Point polyp[5];
    int np;

    r.r_xbot = LEFT(tile);
    r.r_ybot = BOTTOM(tile);
    r.r_xtop = RIGHT(tile);
    r.r_ytop = TOP(tile);

    GeoTransRect(trans, &r, &r2);
    if (IsSplit(tile))
	WindSurfaceToScreenNoClip(mw, &r2, &r);
    else
	WindSurfaceToScreen(mw, &r2, &r);

    GR_CHECK_LOCK();
    if (!grDriverInformed) grInformDriver();
    GrNumClipBoxes++;

    if (!GEO_TOUCH(&r, &grCurClip)) return;

    /* Do a quick check to make the (very common) special case of
     * no clipping go fast.
     */
    needClip = !GEO_SURROUND(&grCurClip, &r);
    needObscure = FALSE;
    for (ob = grCurObscure; ob != NULL; ob = ob->r_next)
	needObscure |= GEO_TOUCH(&r, &(ob->r_r));

    /* Nonmanhattan tiles: */
    /* Expects one of the two tile types to be masked out before */
    /* this procedure is called.                                 */

    if (IsSplit(tile))
    {
	/* Perform matrix transformations on split tiles */
	TileType dinfo;
	Rect fullr;

	dinfo = DBTransformDiagonal(TiGetTypeExact(tile), trans);
	clipr = fullr = r;

	if (needClip)
	    GeoClip(&clipr, &grCurClip);

	GrClipTriangle(&fullr, &clipr, needClip, dinfo, polyp, &np);

	if ((grCurFill == GR_STSOLID) || 
	(grCurFill == GR_STSTIPPLE) || (grCurFill == GR_STGRID) )
	{
	    if (needObscure)
		grObsBox(&clipr);
	    else if (grFillPolygonPtr)
		(void) (*grFillPolygonPtr)(polyp, np);
	} 
    }
    else
    {

	/* do solid areas (same as GrFastBox) */

	if ((grCurFill == GR_STSOLID) || (grCurFill == GR_STSTIPPLE))
	{
	    /* We have a filled area to deal with */
	    clipr = r;
	    if (needClip)
		GeoClip(&clipr, &grCurClip);
	    if (needObscure)
		grObsBox(&clipr);
	    else
		(void) (*grFillRectPtr)(&clipr);
	} 
    }

    /* return if outline is too small to be worth drawing */

    if ((r.r_xtop - r.r_xbot < GR_THRESH)
		&& (r.r_ytop - r.r_ybot < GR_THRESH)
		&& (grCurFill != GR_STOUTLINE))
	return;

    /* do diagonal lines for contacts */

    if (grCurFill == GR_STCROSS)
    {
	Rect rnc;
	/* don't clip contact diagonals */
	if (needClip || needObscure)
	{
	    WindSurfaceToScreenNoClip(mw, &r2, &rnc);
/*
	    (*grDrawLinePtr)(rnc.r_xbot, rnc.r_ybot, rnc.r_xtop, rnc.r_ytop);
	    (*grDrawLinePtr)(rnc.r_xbot, rnc.r_ytop, rnc.r_xtop, rnc.r_ybot);
*/
	    if (!IsSplit(tile))
	    {
		GrClipLine(rnc.r_xbot, rnc.r_ybot, rnc.r_xtop, rnc.r_ytop);
		GrClipLine(rnc.r_xbot, rnc.r_ytop, rnc.r_xtop, rnc.r_ybot);
	    }
	}
	else
	{
	    if (!IsSplit(tile))
	    {
		(*grDrawLinePtr)(r.r_xbot, r.r_ybot, r.r_xtop, r.r_ytop);
		(*grDrawLinePtr)(r.r_xbot, r.r_ytop, r.r_xtop, r.r_ybot);
	    }
	}
    }

    /* draw outlines */

    if (grCurOutline != 0)
    {
	if (GrBoxOutline(tile, &tilesegs))
	{
	    /* simple box (from GrFastBox)*/

	    if (needClip || needObscure)
	    {
		GrClipLine(r.r_xbot, r.r_ytop, r.r_xtop, r.r_ytop);
		GrClipLine(r.r_xbot, r.r_ybot, r.r_xtop, r.r_ybot);
		GrClipLine(r.r_xbot, r.r_ybot, r.r_xbot, r.r_ytop);
		GrClipLine(r.r_xtop, r.r_ybot, r.r_xtop, r.r_ytop);
	    }
	    else
	    {
		(*grDrawLinePtr)(r.r_xbot, r.r_ytop, r.r_xtop, r.r_ytop);
		(*grDrawLinePtr)(r.r_xbot, r.r_ybot, r.r_xtop, r.r_ybot);
		(*grDrawLinePtr)(r.r_xbot, r.r_ybot, r.r_xbot, r.r_ytop);
		(*grDrawLinePtr)(r.r_xtop, r.r_ybot, r.r_xtop, r.r_ytop);
	    }
	}
	else
	{
	    /* non-rectangular box; requires drawing segments */
	    for (segptr = tilesegs; segptr != NULL; segptr = segptr->r_next)
	    {
		GeoTransRect(trans, &segptr->r_r, &r2);
		WindSurfaceToScreen(mw, &r2, &r);

		if (needClip || needObscure)
		    GrClipLine(r.r_xbot, r.r_ybot, r.r_xtop, r.r_ytop);
		else
		    (*grDrawLinePtr)(r.r_xbot, r.r_ybot, r.r_xtop, r.r_ytop);

		/* Free memory, if it was allocated for outline segments */
		freeMagic(segptr);
	    }

	    /* For non-manhattan tiles, the manhattan parts of the	*/
	    /* boundary have already been drawn.  The diagonal boundary */
	    /* is guaranteed to be continuous by definition, and it has	*/
	    /* already undergone clipping with GrClipTriangle.  So just	*/
	    /* draw it, now.						*/

	    if (IsSplit(tile))
	    {
		int cp;
		for (cp = 0; cp < np - 1; cp++)
		{
		    if ((polyp[cp].p_x != polyp[cp + 1].p_x) &&
				(polyp[cp].p_y != polyp[cp + 1].p_y))
		    {
			(*grDrawLinePtr)(polyp[cp].p_x, polyp[cp].p_y,
				polyp[cp + 1].p_x, polyp[cp + 1].p_y);
			break;	/* only 1 diagonal line to draw */
		    }
		}
		if (cp == (np - 1))
		{
		    if ((polyp[cp].p_x != polyp[0].p_x) &&
				(polyp[cp].p_y != polyp[0].p_y))
			(*grDrawLinePtr)(polyp[cp].p_x, polyp[cp].p_y,
				polyp[0].p_x, polyp[0].p_y);
		}
	    }
	}
    }
}

/*---------------------------------------------------------
 * GrDrawFastBox:
 *	GrDrawFastBox will draw a rectangle on the screen in the
 *	style set by the previous call to GrSetStuff.  It will also
 *	be clipped against the rectangles passed to GrSetStuff.
 *
 *	If GrClipBox is called between GrDrawFastBox calls then GrSetStuff
 *	must be called to set the parameters back.
 *
 *	Usually this is called as GrFastBox, defined as GrDrawFastBox(p, 0).
 *	The "scale" is only used to reduce the size of crosses drawn at
 *	point positions, to prevent point labels from dominating a layout
 *	in top-level views. 
 *
 * Results:	None.
 *
 * Side Effects:
 *	The rectangle is drawn in the style specified.
 *	The rectangle is clipped before being drawn.
 *---------------------------------------------------------
 */

void
GrDrawFastBox(prect, scale)
    Rect *prect;	/* The rectangle to be drawn, given in
			 * screen coordinates.
			 */
    int scale;		/* If < 0, we reduce the cross size for
			 * points according to the (negative) scale,
			 * so point labels don't dominate a top-level
			 * layout.
			 */
{
    Rect *r;
    bool needClip, needObscure;
    LinkedRect *ob;

    GR_CHECK_LOCK();
    if (!grDriverInformed) grInformDriver();
    GrNumClipBoxes++;
    if (grCurFill == GR_STGRID)
    {
	r = &grCurClip;
	grGridRect = prect;
    }
    else
    {
	r = prect;
	if (!GEO_TOUCH(r, &grCurClip)) return;
    }

    /* Do a quick check to make the (very common) special case of
     * no clipping go fast.
     */
    needClip = !GEO_SURROUND(&grCurClip, r);
    needObscure = FALSE;
    for (ob = grCurObscure; ob != NULL; ob = ob->r_next)
	needObscure |= GEO_TOUCH(r, &(ob->r_r));

    /* do solid areas */
    if ( (grCurFill == GR_STSOLID) || 
	(grCurFill == GR_STSTIPPLE) || (grCurFill == GR_STGRID) )
    {
	Rect clipr;
	/* We have a filled area to deal with */
	clipr = *r;
	if (needClip)
	    GeoClip(&clipr, &grCurClip);
	if (needObscure)
	    grObsBox(&clipr);
	else
	{
	    if (grCurFill == GR_STGRID)
		(*grDrawGridPtr)(grGridRect, grCurOutline, &clipr);
	    else
		(void) (*grFillRectPtr)(&clipr);
	}
    } 

    /* return if rectangle is too small to see */
    if ((r->r_xtop - r->r_xbot < GR_THRESH)
		&& (r->r_ytop - r->r_ybot < GR_THRESH)
		&& (grCurFill != GR_STOUTLINE))
	return;

    /* draw outlines */

    if ( (grCurOutline != 0) && (grCurFill != GR_STGRID) )
    {
	if ( (grCurFill == GR_STOUTLINE) && (r->r_xbot == r->r_xtop) &&
		(r->r_ybot == r->r_ytop) )
	{
	    int crossSize = GR_CROSSSIZE;

	    if (scale < 0)
	    {
		crossSize += scale;
		if (crossSize < 0)
		    goto endit;
	    }

	    /* turn the outline into a cross */
	    if (needClip || needObscure) 
		goto clipit;
	    else
	    {
		bool crossClip, crossObscure;
		Rect crossBox;

		/* check the larger cross area for clipping */
		crossBox.r_xbot = r->r_xbot - crossSize;
		crossBox.r_ybot = r->r_ybot - crossSize;
		crossBox.r_xtop = r->r_xtop + crossSize;
		crossBox.r_ytop = r->r_ytop + crossSize;

		crossClip = !GEO_SURROUND(&grCurClip, &crossBox);
		crossObscure = FALSE;
		for (ob = grCurObscure; ob != NULL; ob = ob->r_next)
		    crossObscure |= GEO_TOUCH(&crossBox, &(ob->r_r));

		if (crossClip || crossObscure) 
		    goto clipit;
		else
		    goto noclipit;
	    }

	    clipit:
		GrClipLine(r->r_xbot, r->r_ybot - crossSize,
		    r->r_xtop, r->r_ytop + crossSize - 1 + GrPixelCorrect);
		GrClipLine(r->r_xbot - crossSize, r->r_ybot,
		    r->r_xtop + crossSize - 1 + GrPixelCorrect, r->r_ytop);
		goto endit;

	    noclipit:
		(*grDrawLinePtr)(r->r_xbot, r->r_ybot - crossSize,
		    r->r_xtop, r->r_ytop + crossSize - 1 + GrPixelCorrect);
		(*grDrawLinePtr)(r->r_xbot - crossSize, r->r_ybot,
		    r->r_xtop + crossSize - 1 + GrPixelCorrect, r->r_ytop);

	    endit:  ;
	} 
	else
	{
	    if (needClip || needObscure)
	    {
		GrClipLine(r->r_xbot, r->r_ytop, r->r_xtop, r->r_ytop);
		GrClipLine(r->r_xbot, r->r_ybot, r->r_xtop, r->r_ybot);
		GrClipLine(r->r_xbot, r->r_ybot, r->r_xbot, r->r_ytop);
		GrClipLine(r->r_xtop, r->r_ybot, r->r_xtop, r->r_ytop);
	    }
	    else
	    {
		(*grDrawLinePtr)(r->r_xbot, r->r_ytop, r->r_xtop, r->r_ytop);
		(*grDrawLinePtr)(r->r_xbot, r->r_ybot, r->r_xtop, r->r_ybot);
		(*grDrawLinePtr)(r->r_xbot, r->r_ybot, r->r_xbot, r->r_ytop);
		(*grDrawLinePtr)(r->r_xtop, r->r_ybot, r->r_xtop, r->r_ytop);
	    }
	}
    }

    /* do diagonal lines for contacts */
    if (grCurFill == GR_STCROSS)
    {
	if (needClip || needObscure)
	{
	    GrClipLine(r->r_xbot, r->r_ybot, r->r_xtop, r->r_ytop);
	    GrClipLine(r->r_xbot, r->r_ytop, r->r_xtop, r->r_ybot);
	}
	else
	{
	    (*grDrawLinePtr)(r->r_xbot, r->r_ybot, r->r_xtop, r->r_ytop);
	    (*grDrawLinePtr)(r->r_xbot, r->r_ytop, r->r_xtop, r->r_ybot);
	}
    }
}

/*---------------------------------------------------------
 * GrClipTriangle:
 *	Returns an array of points representing a clipped
 *	triangle.  These are always arranged in counter-
 *	clockwise order.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Returns array of points and modifies int * np
 *	to hold the number of points in the array.
 *---------------------------------------------------------
 */

#define xround(d) (int)(((((d % height) << 1) >= height) ? 1 : 0) + (d / height))
#define yround(d) (int)(((((d % width) << 1) >= width) ? 1 : 0) + (d / width))

void
GrClipTriangle(r, c, clipped, dinfo, points, np)
    Rect *r;   /* Bounding box of triangle, in screen coords	*/
    Rect *c;		/* Clipping rectangle 				*/
    bool clipped;	/* Boolean, if bounding box is clipped		*/
    TileType dinfo;	/* Split side and direction information		*/
    Point *points;	/* Point array (up to 5 points) to fill		*/
    int *np;		/* Number of points in the clipped polygon	*/
{
    if (!(dinfo & TT_SIDE))
    {
	points[1].p_x = r->r_xbot;
	points[0].p_y = r->r_ytop;
	points[2].p_y = r->r_ybot;
	points[0].p_x = points[2].p_x = r->r_xtop;
    }
    else
    {
	points[1].p_x = r->r_xtop;
	points[0].p_y = r->r_ybot;
	points[2].p_y = r->r_ytop;
	points[0].p_x = points[2].p_x = r->r_xbot;
    }

    if (!(dinfo & TT_DIRECTION))
    {
	points[1].p_y = points[0].p_y;
	points[2].p_x = points[1].p_x;
    }
    else
    {
	points[0].p_x = points[1].p_x;
	points[1].p_y = points[2].p_y;
    }

    *np = 3;

    /* Clip the triangle to the clipping rectangle.  Result is	*/
    /* a 3- to 5-sided polygon, or empty.			*/ 

    if (clipped)
    {
        dlong delx, dely;
        dlong width = (dlong)(r->r_xtop - r->r_xbot);
	dlong height = (dlong)(r->r_ytop - r->r_ybot);

	switch(dinfo & (TT_DIAGONAL | TT_DIRECTION | TT_SIDE))
	{
	    case TT_DIAGONAL:	/* nw */
		if (c->r_ytop < r->r_ytop)	/* clip top */
		{
		    delx = (dlong)(points[1].p_y - c->r_ytop) * width;
		    points[1].p_y = c->r_ytop;
		    points[0].p_y = c->r_ytop;
		    points[0].p_x -= xround(delx);
		}
		if (c->r_xbot > r->r_xbot)	/* clip left */
		{
		    dely = (dlong)(c->r_xbot - points[2].p_x) * height;
		    points[1].p_x = c->r_xbot;
		    points[2].p_x = c->r_xbot;
		    points[2].p_y += yround(dely);
		}
		if (c->r_ybot > points[2].p_y)	/* clip bottom */
		{
		    delx = (dlong)(c->r_ybot - points[2].p_y) * width;
		    points[2].p_y = c->r_ybot;
		    points[3].p_y = c->r_ybot;
		    points[3].p_x = points[2].p_x + xround(delx);
		    *np = 4;

		    if (c->r_xtop < points[3].p_x)  /* clip right, rectangle */
		    {
			points[3].p_x = c->r_xtop;
			points[0].p_x = c->r_xtop;
		    }
		    else if (c->r_xtop < points[0].p_x) /* clip right, pentagon */
		    {
			dely = (dlong)(points[0].p_x - c->r_xtop) * height;
			points[0].p_x = c->r_xtop;
			points[4].p_x = c->r_xtop;
			points[4].p_y = points[0].p_y - yround(dely);
		        *np = 5;
		    }
		}
		else if (c->r_xtop < points[0].p_x) /* clip right, quadrangle */
		{
		    dely = (dlong)(points[0].p_x - c->r_xtop) * height;
		    points[0].p_x = c->r_xtop;
		    points[3].p_x = c->r_xtop;
		    points[3].p_y = points[0].p_y - yround(dely);
		    *np = 4;
		}
		if (points[1].p_x > points[0].p_x || points[2].p_y > points[1].p_y)
		    *np = 0;	/* clipped out of existence */
		break;

	    case TT_DIAGONAL | TT_DIRECTION:	/* sw */
		if (c->r_xbot > r->r_xbot)	/* clip LEFT */
		{
		    dely = (dlong)(c->r_xbot - points[1].p_x) * height;
		    points[1].p_x = c->r_xbot;
		    points[0].p_x = c->r_xbot;
		    points[0].p_y -= yround(dely);
		}
		if (c->r_ybot > r->r_ybot)	/* clip BOTTOM */
		{
		    delx = (dlong)(c->r_ybot - points[2].p_y) * width;
		    points[1].p_y = c->r_ybot;
		    points[2].p_y = c->r_ybot;
		    points[2].p_x -= xround(delx);
		}
		if (c->r_xtop < points[2].p_x)	/* clip RIGHT */
		{
		    dely = (dlong)(points[2].p_x - c->r_xtop) * height;
		    points[2].p_x = c->r_xtop;
		    points[3].p_x = c->r_xtop;
		    points[3].p_y = points[1].p_y + yround(dely);
		    *np = 4;

		    if (c->r_ytop < points[3].p_y)  /* clip TOP, rectangle */
		    {
			points[3].p_y = c->r_ytop;
			points[0].p_y = c->r_ytop;
		    }
		    else if (c->r_ytop < points[0].p_y) /* clip TOP, pentagon */
		    {
			delx = (dlong)(points[0].p_y - c->r_ytop) * width;
			points[0].p_y = c->r_ytop;
			points[4].p_y = c->r_ytop;
			points[4].p_x = points[0].p_x + xround(delx);
		        *np = 5;
		    }
		}
		else if (c->r_ytop < points[0].p_y) /* clip TOP, quadrangle */
		{
		    delx = (dlong)(points[0].p_y - c->r_ytop) * width;
		    points[0].p_y = c->r_ytop;
		    points[3].p_y = c->r_ytop;
		    points[3].p_x = points[0].p_x + xround(delx);
		    *np = 4;
		}
		if (points[1].p_y > points[0].p_y || points[2].p_x < points[1].p_x)
		    *np = 0;	/* clipped out of existence */
		break;

	    case TT_DIAGONAL | TT_SIDE:	/* se */
		/* order: (bottom, right), (top, left) */
		if (c->r_ybot > r->r_ybot)	/* clip BOTTOM */
		{
		    delx = (dlong)(c->r_ybot - points[1].p_y) * width;
		    points[1].p_y = c->r_ybot;
		    points[0].p_y = c->r_ybot;
		    points[0].p_x += xround(delx);
		}
		if (c->r_xtop < r->r_xtop)	/* clip RIGHT */
		{
		    dely = (dlong)(points[2].p_x - c->r_xtop) * height;
		    points[1].p_x = c->r_xtop;
		    points[2].p_x = c->r_xtop;
		    points[2].p_y -= yround(dely);
		}
		if (c->r_ytop < points[2].p_y)	/* clip TOP */
		{
		    delx = (dlong)(points[2].p_y - c->r_ytop) * width;
		    points[2].p_y = c->r_ytop;
		    points[3].p_y = c->r_ytop;
		    points[3].p_x = points[2].p_x - xround(delx);
		    *np = 4;

		    if (c->r_xbot > points[3].p_x)  /* clip LEFT, rectangle */
		    {
			points[3].p_x = c->r_xbot;
			points[0].p_x = c->r_xbot;
		    }
		    else if (c->r_xbot > points[0].p_x) /* clip LEFT, pentagon */
		    {
			dely = (dlong)(c->r_xbot - points[0].p_x) * height;
			points[0].p_x = c->r_xbot;
			points[4].p_x = c->r_xbot;
			points[4].p_y = points[0].p_y + yround(dely);
		        *np = 5;
		    }
		}
		else if (c->r_xbot > points[0].p_x) /* clip LEFT, triangle */
		{
		    dely = (dlong)(c->r_xbot - points[0].p_x) * height;
		    points[0].p_x = c->r_xbot;
		    points[3].p_x = c->r_xbot;
		    points[3].p_y = points[0].p_y + yround(dely);
		    *np = 4;
		}
		if (points[0].p_x > points[1].p_x || points[1].p_y > points[2].p_y)
		    *np = 0;	/* clipped out of existence */
		break;

	    case TT_DIAGONAL | TT_SIDE | TT_DIRECTION:	/* ne */
		/* order: (top, right), (bottom, left) */
		if (c->r_xtop < r->r_xtop)	/* clip RIGHT */
		{
		    dely = (dlong)(points[1].p_x - c->r_xtop) * height;
		    points[1].p_x = c->r_xtop;
		    points[0].p_x = c->r_xtop;
		    points[0].p_y += yround(dely);
		}
		if (c->r_ytop < r->r_ytop)	/* clip TOP */
		{
		    delx = (dlong)(points[2].p_y - c->r_ytop) * width;
		    points[1].p_y = c->r_ytop;
		    points[2].p_y = c->r_ytop;
		    points[2].p_x += xround(delx);
		}
		if (c->r_xbot > points[2].p_x)	/* clip LEFT */
		{
		    dely = (dlong)(c->r_xbot - points[2].p_x) * height;
		    points[2].p_x = c->r_xbot;
		    points[3].p_x = c->r_xbot;
		    points[3].p_y = points[2].p_y - yround(dely);
		    *np = 4;

		    if (c->r_ybot >= points[3].p_y)  /* clip BOTTOM, rectangle */
		    {
			points[3].p_y = c->r_ybot;
			points[0].p_y = c->r_ybot;
		    }
		    else if (c->r_ybot > points[0].p_y) /* clip BOTTOM, pentagon */
		    {
			delx = (dlong)(c->r_ybot - points[0].p_y) * width;
			points[0].p_y = c->r_ybot;
			points[4].p_y = c->r_ybot;
			points[4].p_x = points[0].p_x - xround(delx);
		        *np = 5;
		    }
		}
		else if (c->r_ybot > points[0].p_y) /* clip BOTTOM, quadrangle */
		{
		    delx = (dlong)(c->r_ybot - points[0].p_y) * width;
		    points[0].p_y = c->r_ybot;
		    points[3].p_y = c->r_ybot;
		    points[3].p_x = points[0].p_x - xround(delx);
		    *np = 4;
		}
		if (points[1].p_y < points[0].p_y || points[2].p_x > points[1].p_x)
		    *np = 0;	/* clipped out of existence */
		break;
	}
    }
}

/*---------------------------------------------------------
 * GrDrawTriangleEdge --
 *	Draws the diagonal line in a triangle clipped by
 *	grCurClip.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws nonmanhattan layout geometry.
 *
 *---------------------------------------------------------
 */

void
GrDrawTriangleEdge(r, dinfo)
    Rect *r;   /* Bounding box of triangle, in screen coords	*/
    TileType dinfo;
{
    Point tpoints[5];
    int tnum, i, j;

    GrClipTriangle(r, &grCurClip, TRUE, dinfo, tpoints, &tnum);

    for (i = 0; i < tnum; i++)
    { 
	j = (i + 1) % tnum;
	if (tpoints[i].p_x != tpoints[j].p_x &&  
		tpoints[i].p_y != tpoints[j].p_y)
	{   
	    GrClipLine(tpoints[i].p_x, tpoints[i].p_y,
			tpoints[j].p_x, tpoints[j].p_y);
	    break;
	}
    }
}

/*---------------------------------------------------------
 * GrDiagonal:
 *	GrDiagonal will draw a triangle on the screen in the
 *	style set by the previous call to GrSetStuff.  It will also
 *	be clipped against the rectangles passed to GrSetStuff.
 *
 *	If GrDiagonal is called between GrFastBox calls then GrSetStuff
 *	must be called to set the parameters back.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	The rectangle is drawn in the style specified.
 *	The rectangle is clipped before being drawn.
 *---------------------------------------------------------
 */

void
GrDiagonal(prect, dinfo)
    Rect *prect;	/* The rectangle to be drawn, given in
				 * screen coordinates.
			         */
    TileType dinfo;	/* split and direction information */
{
    Rect *r;
    bool needClip, needObscure;
    LinkedRect *ob;
    int cp, np;
    Rect clipr, fullr;
    Point polyp[5];

    GR_CHECK_LOCK();
    if (!grDriverInformed) grInformDriver();
    GrNumClipBoxes++;
    if (grCurFill == GR_STGRID)
    {
	r = &grCurClip;
	grGridRect = prect;
    }
    else
    {
	r = prect;
	if (!GEO_TOUCH(r, &grCurClip)) return;
    }

    /* Do a quick check to make the (very common) special case of
     * no clipping go fast.
     */
    needClip = !GEO_SURROUND(&grCurClip, r);
    needObscure = FALSE;
    for (ob = grCurObscure; ob != NULL; ob = ob->r_next)
	needObscure |= GEO_TOUCH(r, &(ob->r_r));

    /* Generate points for the triangle and do clipping if necessary */

    clipr = fullr = *r;
    if (needClip)
	GeoClip(&clipr, &grCurClip);

    GrClipTriangle(&fullr, &clipr, needClip, dinfo, polyp, &np);

    /* do solid areas */
    if ( (grCurFill == GR_STSOLID) || 
	(grCurFill == GR_STSTIPPLE) || (grCurFill == GR_STGRID) )
    {
	if (needObscure)
	    grObsBox(&clipr);
	else if (grFillPolygonPtr)
	    (void) (*grFillPolygonPtr)(polyp, np);
    } 

    /* return if rectangle is too small to see */

    if ((r->r_xtop - r->r_xbot < GR_THRESH) && 
	    (r->r_ytop - r->r_ybot < GR_THRESH) && (grCurFill != GR_STOUTLINE))
	return;

    /* draw outlines */
    if ( (grCurOutline != 0) && (grCurFill != GR_STGRID) )
    {
	/* TO DO: Check for boundary with shared tile type */
	for (cp = 0; cp < np - 1; cp++)
	    (*grDrawLinePtr)(polyp[cp].p_x, polyp[cp].p_y,
			polyp[cp + 1].p_x, polyp[cp + 1].p_y);

	(*grDrawLinePtr)(polyp[cp].p_x, polyp[cp].p_y,
			polyp[0].p_x, polyp[0].p_y);
    }
}

/*---------------------------------------------------------
 * GrFillPolygon --
 *	This routine is simply a call to the locally-defined
 *	grFillPolygonPtr routine.
 *
 *---------------------------------------------------------
 */

void
GrFillPolygon(polyp, np)
    Point *polyp;		/* Array of points defining polygon */
    int np;			/* number of points in array polyp  */
{
    if (grFillPolygonPtr != NULL)
    {
	(*grFillPolygonPtr)(polyp, np);
    }
}


/*---------------------------------------------------------
 * GrClipBox:
 *	GrClipBox will draw a rectangle on the screen in one
 *	of several possible styles, except that the rectangle will
 *	be clipped against a list of obscuring rectangles.
 *
 * Results:	None.
 *
 * Side Effects:
 *	The rectangle is drawn in the style specified.
 *	The rectangle is clipped before being drawn.
 *---------------------------------------------------------
 */

void
GrClipBox(prect, style)
    Rect *prect;		/* The rectangle to be drawn, given in
				 * screen coordinates.
			         */
    int style;			/* The style to be used in drawing it. */
{
    GrSetStuff(style);
    GrFastBox(prect);
}


/*
 * ----------------------------------------------------------------------------
 *	GrDisjoint --
 *
 * 	Clip a rectanglular area against a clipping box, applying the
 *	supplied procedure to each rectangular region in "area" which
 *	falls outside "clipbox".  This works in pixel space, where a
 *	rectangle is contains its lower x- and y-coordinates AND ALSO
 *	its upper coordinates.  This means that if the clipping box
 *	occupies a given pixel, the things being clipped must not occupy
 *	that pixel.  This procedure will NOT work in tile space.
 *
 *	The procedure should be of the form:
 *		bool func(box, cdarg)
 *			Rect	   * box;
 *			ClientData   cdarg;
 *
 * Results:
 *	Return TRUE unless the supplied function returns FALSE.
 *
 * Side effects:
 *	The side effects of the invoked procedure.
 * ----------------------------------------------------------------------------
 */
bool
GrDisjoint(area, clipBox, func, cdarg)
    Rect	* area;
    Rect	* clipBox;
    bool 	(*func) ();
    ClientData	  cdarg;
{
    Rect 	  ok, rArea;
    bool	  result;

#define NULLBOX(R) ((R.r_xbot>R.r_xtop)||(R.r_ybot>R.r_ytop))

    ASSERT((area!=(Rect *) NULL), "GrDisjoint");
    if((clipBox==(Rect *) NULL)||(!GEO_TOUCH(area, clipBox)))
    {
    /* Since there is no overlap, all of "area" may be processed. */

	result= (*func)(area, cdarg);
	return(result);
    }

    /* Do the disjoint operation in four steps, one for each side
     * of clipBox.  In each step, divide the area being clipped
     * into one piece that is DEFINITELY outside clipBox, and one
     * piece left to check some more.
     */
    
    /* Top edge of clipBox: */

    rArea = *area;
    result = TRUE;
    if (clipBox->r_ytop < rArea.r_ytop)
    {
	ok = rArea;
	ok.r_ybot = clipBox->r_ytop + 1;
	rArea.r_ytop = clipBox->r_ytop;
	if (!(*func)(&ok, cdarg)) result = FALSE;
    }

    /* Bottom edge of clipBox: */

    if (clipBox->r_ybot > rArea.r_ybot)
    {
	ok = rArea;
	ok.r_ytop = clipBox->r_ybot - 1;
	rArea.r_ybot = clipBox->r_ybot;
	if (!(*func)(&ok, cdarg)) result = FALSE;
    }

    /* Right edge of clipBox: */

    if (clipBox->r_xtop < rArea.r_xtop)
    {
	ok = rArea;
	ok.r_xbot = clipBox->r_xtop + 1;
	rArea.r_xtop = clipBox->r_xtop;
	if (!(*func)(&ok, cdarg)) result = FALSE;
    }

    /* Left edge of clipBox: */

    if (clipBox->r_xbot > rArea.r_xbot)
    {
	ok = rArea;
	ok.r_xtop = clipBox->r_xbot - 1;
	rArea.r_xbot = clipBox->r_xbot;
	if (!(*func)(&ok, cdarg)) result = FALSE;
    }

    /* Just throw away what's left of the area being clipped, since
     * it overlaps the clipBox.
     */

    return result;
} /*GrDisjoint*/
