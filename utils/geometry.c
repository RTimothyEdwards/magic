/* geometry.c --
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
 * This file contains a bunch of utility routines for manipulating
 * boxes, points, and transforms.
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/geometry.c,v 1.5 2008/12/11 14:11:46 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <math.h>	/* For atan2() function */

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/utils.h"
#include "textio/textio.h"

#include "tiles/tile.h"	/* test only! */

/*
 *-------------------------------------------------------------------
 *	Declarations of exported transforms:
 *-------------------------------------------------------------------
 */

global Transform GeoIdentityTransform	= {  1,  0,  0,  0,  1,  0 };
global Transform GeoUpsideDownTransform	= {  1,  0,  0,  0, -1,  0 };
global Transform GeoSidewaysTransform	= { -1,  0,  0,  0,  1,  0 };
global Transform Geo90Transform		= {  0,  1,  0, -1,  0,  0 };
global Transform Geo180Transform	= { -1,  0,  0,  0, -1,  0 };
global Transform Geo270Transform	= {  0, -1,  0,  1,  0,  0 };

/*
 * Additional Transforms (Reflections at 45 and 135 degrees)
 */

global Transform GeoRef45Transform	= {  0,  1,  0,  1,  0,  0 };
global Transform GeoRef135Transform	= {  0, -1,  0, -1,  0,  0 };

/*
 *-------------------------------------------------------------------
 *	Declaration of the table of opposite directions:
 *-------------------------------------------------------------------
 */
global int GeoOppositePos[] =
{
	GEO_CENTER,	/* GEO_CENTER */
	GEO_SOUTH,	/* GEO_NORTH */
	GEO_SOUTHWEST,	/* GEO_NORTHEAST */
	GEO_WEST,	/* GEO_EAST */
	GEO_NORTHWEST,	/* GEO_SOUTHEAST */
	GEO_NORTH,	/* GEO_SOUTH */
	GEO_NORTHEAST,	/* GEO_SOUTHWEST */
	GEO_EAST,	/* GEO_WEST */
	GEO_SOUTHEAST,	/* GEO_NORTHWEST */
};

/*
 *-------------------------------------------------------------------
 *	Declarations of exported variables:
 *-------------------------------------------------------------------
 */

global Rect  GeoNullRect = { 0, 0, 0, 0 };
global Point GeoOrigin = { 0, 0 };


/*-------------------------------------------------------------------
 *	GeoTransPoint --
 *	Transforms a point from one coordinate system to another.
 *
 *	Results:	None.
 *
 *	Side Effects:
 *	P2 is set to contain the coordinates that result from transforming
 *	p1 by t.
 *-------------------------------------------------------------------
 */

void
GeoTransPoint(t, p1, p2)
    Transform *t;		/* A description of the mapping from the
				 * coordinate system of p1 to that of p2.
				 */
    Point *p1, *p2;		/* Pointers to two points; p1 is the old
				 * point, and p2 will contain the transformed
				 * point.
				 */

{
    p2->p_x = p1->p_x*t->t_a + p1->p_y*t->t_b + t->t_c;
    p2->p_y = p1->p_x*t->t_d + p1->p_y*t->t_e + t->t_f;
}

/*
 *-------------------------------------------------------------------
 *
 * GeoTransPointDelta --
 *
 *	Transforms a point from one coordinate system to another.  This
 *	differs from GeoTransPoint in that translation is ignored.  It
 *	applies flips and rotations, and so is appropriate to calculate
 *	how an offset value (delta distance) transforms through the
 *	hierarchy.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	P2 is set to contain the coordinates that result from transforming
 *	p1 by t.
 *-------------------------------------------------------------------
 */
void
GeoTransPointDelta(t, p1, p2)
    Transform *t;		/* A description of the mapping from the
				 * coordinate system of p1 to that of p2.
				 */
    Point *p1, *p2;		/* Pointers to two points; p1 is the old
				 * point, and p2 will contain the transformed
				 * point.
				 */

{
    p2->p_x = p1->p_x * t->t_a + p1->p_y * t->t_b;
    p2->p_y = p1->p_x * t->t_d + p1->p_y * t->t_e;
}

/*
 *-------------------------------------------------------------------
 *  Determine how an angle changes through transformation via a
 *  tranformation matrix.  Expects the transformations to be in
 *  multiples of 90 degrees, plus possible flipping.  Expects an
 *  angle between 0 and 360 and returns an angle between 0 and
 *  360.
 *-------------------------------------------------------------------
 */

int
GeoTransAngle(t, a)
    Transform *t;	/* Transformation matrix */
    int a;		/* Angle to transform */
{
    bool flip = FALSE;
    int asave = a;

    /* Rotate according to the standard transforms */

    if (t->t_a == 0 && t->t_e == 0)
    {
	if (t->t_b > 0)
	    a += 90;
	else
	    a += 270;
	if (t->t_b == t->t_d)
	    flip = TRUE;
    }
    else
    {
	if (t->t_a < 0)
	    a += 180;
	if (t->t_a != t->t_e)
	    flip = TRUE;
    }
    if (a > 360) a -= 360;
    if (flip)
    {
	if (asave > 90 && asave < 270)
	    a = 360 - a;
	else
	    a = -a;
    }
    if (a < 0) a += 360;
    return a;
}


/*-------------------------------------------------------------------
 *	GeoTransRect --
 *	Transforms a rectangle from one coordinate system to another.
 *
 *	Results:	None.
 *
 *	Side Effects:
 *	R2 is set to contain the coordinates that result from transforming
 *	r1 by t.
 *-------------------------------------------------------------------
 */

void
GeoTransRect(t, r1, r2)
    Transform *t;		/* A description of the mapping from the
				 * coordinate system of r1 to that of r2.
				 */
    Rect *r1, *r2;		/* Pointers to two rectangles, r1 is the old
				 * rectangle, r2 will contain the transformed
				 * rectangle.
				 */

{
    int x1, y1, x2, y2;
    x1 = r1->r_xbot*t->t_a + r1->r_ybot*t->t_b + t->t_c;
    y1 = r1->r_xbot*t->t_d + r1->r_ybot*t->t_e + t->t_f;
    x2 = r1->r_xtop*t->t_a + r1->r_ytop*t->t_b + t->t_c;
    y2 = r1->r_xtop*t->t_d + r1->r_ytop*t->t_e + t->t_f;

    /* Because of rotations, xbot and xtop may have to be switched, and
     * the same for ybot and ytop.
     */

    if (x1 < x2)
    {
	r2->r_xbot = x1;
	r2->r_xtop = x2;
    }
    else
    {
	r2->r_xbot = x2;
	r2->r_xtop = x1;
    }
    if (y1 < y2)
    {
	r2->r_ybot = y1;
	r2->r_ytop = y2;
    }
    else
    {
	r2->r_ybot = y2;
	r2->r_ytop = y1;
    }
}

/*-------------------------------------------------------------------
 *	GeoTranslateTrans --
 *	Translate a transform by the indicated (x, y) amount.
 *
 *	Results:	None.
 *
 *	Side Effects:
 *	Trans2 is set to the result of transforming trans1 by
 *	a translation of (x, y).
 *-------------------------------------------------------------------
 */

void
GeoTranslateTrans(trans1, x, y, trans2)
    Transform *trans1;	/* Transform to be translated */
    int x, y;		/* Amount by which to translated */
    Transform *trans2;	/* Result transform */
{
    trans2->t_a = trans1->t_a;
    trans2->t_b = trans1->t_b;
    trans2->t_d = trans1->t_d;
    trans2->t_e = trans1->t_e;

    trans2->t_c = trans1->t_c + x;
    trans2->t_f = trans1->t_f + y;
}

/*-------------------------------------------------------------------
 *	GeoTransTranslate --
 *	Transform a translation by the indicated (x, y) amount.
 *
 *	This is the dual of GeoTranslateTrans, in that if
 *	Tinv is the inverse of T,
 *
 *	GeoTransTranslate(T, x, y) * GeoTranslateTrans(Tinv, -x, -y)
 *	is the identity transform.
 *
 *	Results:	None.
 *
 *	Side Effects:
 *	Trans2 is set to the result of transforming a translation
 *	of (x, y) by trans1.
 *-------------------------------------------------------------------
 */

void
GeoTransTranslate(x, y, trans1, trans2)
    int x, y;		/* Amount of translation */
    Transform *trans1;	/* Transform to be applied to translation */
    Transform *trans2;	/* Result transform */
{
    trans2->t_a = trans1->t_a;
    trans2->t_b = trans1->t_b;
    trans2->t_d = trans1->t_d;
    trans2->t_e = trans1->t_e;

    trans2->t_c = x*trans1->t_a + y*trans1->t_b + trans1->t_c;
    trans2->t_f = x*trans1->t_d + y*trans1->t_e + trans1->t_f;
}


/*-------------------------------------------------------------------
 *	GeoTransTrans --
 *	This routine transforms a transform.
 *
 *	Results:	None.
 *
 *	Side Effects:
 *	The transform referred to by net is set to produce a geometrical
 *	transformation equivalent in effect to the application of transform
 *	first, followed by the application of transform second.
 *-------------------------------------------------------------------
 */

void
GeoTransTrans(first, second, net)
    Transform *first;		/* Pointers to three transforms */
    Transform *second;
    Transform *net;
{
    net->t_a = first->t_a*second->t_a + first->t_d*second->t_b;
    net->t_b = first->t_b*second->t_a + first->t_e*second->t_b;
    net->t_c = first->t_c*second->t_a + first->t_f*second->t_b + second->t_c;
    net->t_d = first->t_a*second->t_d + first->t_d*second->t_e;
    net->t_e = first->t_b*second->t_d + first->t_e*second->t_e;
    net->t_f = first->t_c*second->t_d + first->t_f*second->t_e + second->t_f;
}


/*-------------------------------------------------------------------
 *	GeoNameToPos --
 *	Map the name of a position into an integer position parameter.
 *	Position names may be unique abbreviations for direction names.
 *
 *	Results:
 *	Returns a position parameter (0 - 8, corresponding to GEO_CENTER
 *	through GEO_NORTHWEST), -1 if the position name was ambiguous,
 *	and -2 if it was unrecognized.
 *
 *	Side Effects:	None.
 *-------------------------------------------------------------------
 */

int
GeoNameToPos(name, manhattan, verbose)
    char *name;
    bool manhattan;	/* If TRUE, only Manhattan directions (up, down,
			 * left, right, and their synonyms) are allowed.
			 */
    bool verbose;	/* If TRUE, we print an error message and list
			 * valid directions.
			 */
{
    static struct pos
    {
	char	*pos_name;
	int	 pos_value;
	bool	 pos_manhattan;
    }
    positions[] =
    {
	"bl",		GEO_SOUTHWEST,		FALSE,
	"bottom",	GEO_SOUTH,		TRUE,
	"br",		GEO_SOUTHEAST,		FALSE,
	"center",	GEO_CENTER,		FALSE,
	"d",		GEO_SOUTH,		TRUE,
	"dl",		GEO_SOUTHWEST,		FALSE,
	"down",		GEO_SOUTH,		TRUE,
	"dr",		GEO_SOUTHEAST,		FALSE,
	"e",		GEO_EAST,		TRUE,
	"east",		GEO_EAST,		TRUE,
	"left",		GEO_WEST,		TRUE,
	"n",		GEO_NORTH,		TRUE,
	"ne",		GEO_NORTHEAST,		FALSE,
	"north",	GEO_NORTH,		TRUE,
	"northeast",	GEO_NORTHEAST,		FALSE,
	"northwest",	GEO_NORTHWEST,		FALSE,
	"nw",		GEO_NORTHWEST,		FALSE,
	"right",	GEO_EAST,		TRUE,
	"s",		GEO_SOUTH,		TRUE,
	"se",		GEO_SOUTHEAST,		FALSE,
	"south",	GEO_SOUTH,		TRUE,
	"southeast",	GEO_SOUTHEAST,		FALSE,
	"southwest",	GEO_SOUTHWEST,		FALSE,
	"sw",		GEO_SOUTHWEST,		FALSE,
	"tl",		GEO_NORTHWEST,		FALSE,
	"top",		GEO_NORTH,		TRUE,
	"tr",		GEO_NORTHEAST,		FALSE,
	"u",		GEO_NORTH,		TRUE,
	"ul",		GEO_NORTHWEST,		FALSE,
	"up",		GEO_NORTH,		TRUE,
	"ur",		GEO_NORTHEAST,		FALSE,
	"w",		GEO_WEST,		TRUE,
	"west",		GEO_WEST,		TRUE,
	0
    };
    struct pos *pp;
    char *fmt;
    int pos;

    pos = LookupStruct(name, (LookupTable *) positions, sizeof positions[0]);

    if ((pos >= 0) && (!manhattan || positions[pos].pos_manhattan))
	return positions[pos].pos_value;
    if (!verbose)
    {
	if (pos < 0) return pos;
	else return -2;
    }
    if (pos < 0)
    {
	switch (pos)
	{
	    case -1:
		TxError("\"%s\" is ambiguous.\n", name);
		break;
	    case -2:
		TxError("\"%s\" is not a valid direction or position.\n",
		    name);
		break;
	}
    }
    else
    {
	TxError("\"%s\" is not a Manhattan direction or position.\n", name);
	pos = -2;
    }
    TxError("Legal directions/positions are:\n\t");
    for (fmt = "%s", pp = positions; pp->pos_name; pp++)
    {
	if (manhattan && !pp->pos_manhattan)
	    continue;
	TxError(fmt, pp->pos_name);
	fmt = ",%s";
    }
    TxError("\n");
    return (pos);
}


/*
 * ----------------------------------------------------------------------------
 *
 * GeoPosToName --
 *
 * 	Given a geometric name, return its position name.
 *
 * Results:
 *	Pointer to a static string holding the position name.
 *	NOTE: you'd better not try to alter the returned string!
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */
char *
GeoPosToName(pos)
    int pos;
{
    switch(pos)
    {
        case GEO_CENTER:    return("CENTER");
        case GEO_NORTH:     return("NORTH");
        case GEO_NORTHEAST: return("NORTHEAST");
        case GEO_EAST:      return("EAST");
        case GEO_SOUTHEAST: return("SOUTHEAST");
        case GEO_SOUTH:     return("SOUTH");
        case GEO_SOUTHWEST: return("SOUTHWEST");
        case GEO_WEST:      return("WEST");
        case GEO_NORTHWEST: return("NORTHWEST");
	default:            return("*ILLEGAL*");
    }
}

/*-------------------------------------------------------------------
 *	GeoTransPos --
 *	This routine computes the transform of a relative position.
 *
 *	Results:
 *	The return value is a position equal to the position parameter
 *	transformed by t.
 *
 *	Side Effects:	None.
 *-------------------------------------------------------------------
 */

int
GeoTransPos(t, pos)
    Transform *t;		/* Transform to be applied. */
    int pos;			/* Position to which it is to be applied. */

{
    if ((pos <= 0) || (pos > 8)) return pos;

    /* Handle rotation first, using modulo arithmetic. */

    pos -= 1;
    if (t->t_a <= 0)
    {
	if (t->t_a < 0) pos += 4;
	else if (t->t_b < 0) pos += 6;
	else pos += 2;
    }
    while (pos >= 8) pos -= 8;
    pos += 1;

    /* Handle mirroring across the x-axis on a case-by-case basis. */

    if ((t->t_a != t->t_e) || ((t->t_a == 0) && (t->t_b == t->t_d)))
    {
	switch (pos)
	{
	    case GEO_NORTH:	pos = GEO_SOUTH; break;
	    case GEO_NORTHEAST:	pos = GEO_SOUTHEAST; break;
	    case GEO_EAST:	break;
	    case GEO_SOUTHEAST:	pos = GEO_NORTHEAST; break;
	    case GEO_SOUTH:	pos = GEO_NORTH; break;
	    case GEO_SOUTHWEST:	pos = GEO_NORTHWEST; break;
	    case GEO_WEST:	break;
	    case GEO_NORTHWEST: pos = GEO_SOUTHWEST; break;
	}
    }
    return pos;
}


/*-------------------------------------------------------------------
 *	GeoInvertTrans --
 *	This routine computes the inverse of a transform.
 *
 *	Results:	None.
 *
 *	Side Effects:
 *	The transform pointed to by inverse is overwritten with
 *	the inverse transform of t.  Note:  this method of inversion
 *	only works for rotations that are multiples of 90 degrees with
 *	unit scale factor.  Beware any changes to this!
 *-------------------------------------------------------------------
 */

void
GeoInvertTrans(t, inverse)
    Transform *t;			/* Pointer to a transform */
    Transform *inverse;			/* Place to store the inverse */

{
    Transform t2, t3;
    t2.t_a = t2.t_e = 1;
    t2.t_b = t2.t_d = 0;
    t2.t_c = -t->t_c;
    t2.t_f = -t->t_f;
    t3.t_a = t->t_a;
    t3.t_b = t->t_d;
    t3.t_d = t->t_b;
    t3.t_e = t->t_e;
    t3.t_c = t3.t_f = 0;
    GeoTransTrans(&t2, &t3, inverse);
}


/*-------------------------------------------------------------------
 *	GeoInclude --
 *	This routine includes one rectangle into another by expanding
 *	the second.
 *
 *	Results:
 *	TRUE is returned if the destination had to be enlarged.
 *
 *	Side Effects:
 *	The destination is enlarged (if necessary) so that it completely
 *	contains the area of both the original src and dst rectangles.
 *-------------------------------------------------------------------
 */

bool
GeoInclude(src, dst)
    Rect *src, *dst;
{
    int value;

    if (GEO_RECTNULL(src)) return FALSE;
    else if (GEO_RECTNULL(dst))
    {
	*dst = *src;
	return TRUE;
    }
    
    value = FALSE;
    if (dst->r_xbot > src->r_xbot)
    {
	dst->r_xbot = src->r_xbot;
	value = TRUE;
    }
    if (dst->r_ybot > src->r_ybot)
    {
	dst->r_ybot = src->r_ybot;
	value = TRUE;
    }
    if (dst->r_xtop < src->r_xtop)
    {
	dst->r_xtop = src->r_xtop;
	value = TRUE;
    }
    if (dst->r_ytop < src->r_ytop)
    {
	dst->r_ytop = src->r_ytop;
	value = TRUE;
    }
    return value;
}


/*-------------------------------------------------------------------
 *	GeoIncludeAll --
 *	This routine includes one rectangle into another by expanding
 *	the second.  This routine differs from GeoInclude in that zero-
 *	size source rectangles are processed.  The source or destination
 *	rectangle is considered to be NULL only if its lower-left corner
 *	is above or to the right of its upper right corner.  In this
 *	case, the other rectangle is the result.
 *
 *	Results:
 *	TRUE is returned if the destination is enlarged; otherwise FALSE.
 *
 *	Side Effects:
 *	The destination is enlarged (if necessary) so that it completely
 *	contains the area of both the original src and dst rectangles.
 *-------------------------------------------------------------------
 */

bool
GeoIncludeAll(src, dst)
    Rect *src, *dst;
{
    bool value;

    if ((dst->r_xbot > dst->r_xtop) || (dst->r_ybot > dst->r_ytop))
    {
	*dst = *src;
	return TRUE;
    }

    if ((src->r_xbot > src->r_xtop) || (src->r_ybot > src->r_ytop))
	return FALSE;
    
    value = FALSE;
    if (dst->r_xbot > src->r_xbot)
    {
	dst->r_xbot = src->r_xbot;
	value = TRUE;
    }
    if (dst->r_ybot > src->r_ybot)
    {
	dst->r_ybot = src->r_ybot;
	value = TRUE;
    }
    if (dst->r_xtop < src->r_xtop)
    {
	dst->r_xtop = src->r_xtop;
	value = TRUE;
    }
    if (dst->r_ytop < src->r_ytop)
    {
	dst->r_ytop = src->r_ytop;
	value = TRUE;
    }
    return value;
}


/*-------------------------------------------------------------------
 *	GeoIncludePoint --
 *	This routine includes a point into a rectangle by expanding
 *	the rectangle if necessary.  If the destination rectangle has
 *	its lower left corner above or to the right of its upper right
 *	corner, then use the source point to initialize the destination
 *	rectangle.
 *
 *	Results:
 *	None.
 *
 *	Side Effects:
 *	The destination is enlarged (if necessary) so that it completely
 *	contains the area of both the original src and dst.
 *-------------------------------------------------------------------
 */

void
GeoIncludePoint(src, dst)
    Point *src;
    Rect  *dst;
{
    if ((dst->r_xbot > dst->r_xtop) || (dst->r_ybot > dst->r_ytop))
    {
	dst->r_ll = *src;
	dst->r_ur = *src;
    }
    else
    {
	if (dst->r_xbot > src->p_x)
	    dst->r_xbot = src->p_x;
	if (dst->r_ybot > src->p_y)
	    dst->r_ybot = src->p_y;
	if (dst->r_xtop < src->p_x)
	    dst->r_xtop = src->p_x;
	if (dst->r_ytop < src->p_y)
	    dst->r_ytop = src->p_y;
    }
}


/*-------------------------------------------------------------------
 *	GeoClip --
 *	clips one rectangle against another.
 *
 *	Results:	None.
 *
 *	Side Effects:
 *	Rectangle r is clipped so that it includes only the
 *	intersection area between r and area.  The rectangle
 *	may end up being turned inside out (xbot>xtop) if
 *	there was absolutely no intersection between the two
 *	boxes.
 *-------------------------------------------------------------------
 */

void
GeoClip(r, area)
    Rect *r;		/* Rectangle to be clipped. */
    Rect *area;		/* Area against which to be clipped. */

{
    if (r->r_xbot < area->r_xbot) r->r_xbot = area->r_xbot;
    if (r->r_ybot < area->r_ybot) r->r_ybot = area->r_ybot;
    if (r->r_xtop > area->r_xtop) r->r_xtop = area->r_xtop;
    if (r->r_ytop > area->r_ytop) r->r_ytop = area->r_ytop;
}

/*-------------------------------------------------------------------
 *	GeoClipPoint --
 *	Clips one point against a rectangle, moving the point into
 *	the rectangle if needed.
 *
 *	Results:	None.
 *
 *	Side Effects:
 *	Point p is clipped so that it lies within or on the rectangle.
 *-------------------------------------------------------------------
 */

void
GeoClipPoint(p, area)
    Point *p;		/* Point to be clipped. */
    Rect *area;		/* Area against which to be clipped. */
{
    if (p->p_x < area->r_xbot) p->p_x = area->r_xbot;
    if (p->p_y < area->r_ybot) p->p_y = area->r_ybot;
    if (p->p_x > area->r_xtop) p->p_x = area->r_xtop;
    if (p->p_y > area->r_ytop) p->p_y = area->r_ytop;
}

/*
 * ----------------------------------------------------------------------------
 *	GeoDisjoint --
 *
 * 	Clip a rectanglular area against a clipping box, applying the
 *	supplied procedure to each rectangular region in "area" which
 *	falls outside "clipbox".  This works in tile space, where a
 *	rectangle is assumed to contain its lower x- and y-coordinates
 *	but not its upper coordinates.  It does NOT work in pixel space
 *	(think about this carefully before using it for pixels!).
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
GeoDisjoint(area, clipBox, func, cdarg)
    Rect	* area;
    Rect	* clipBox;
    bool 	(*func) ();
    ClientData	  cdarg;
{
    Rect 	  ok, rArea;
    bool	  result;

#define NULLBOX(R) ((R.r_xbot>R.r_xtop)||(R.r_ybot>R.r_ytop))

    ASSERT((area!=(Rect *) NULL), "GeoDisjoint");
    if((clipBox==(Rect *) NULL)||(!GEO_OVERLAP(area, clipBox)))
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
	rArea.r_ytop = ok.r_ybot = clipBox->r_ytop;
	if (!(*func)(&ok, cdarg)) result = FALSE;
    }

    /* Bottom edge of clipBox: */

    if (clipBox->r_ybot > rArea.r_ybot)
    {
	ok = rArea;
	rArea.r_ybot = ok.r_ytop = clipBox->r_ybot;
	if (!(*func)(&ok, cdarg)) result = FALSE;
    }

    /* Right edge of clipBox: */

    if (clipBox->r_xtop < rArea.r_xtop)
    {
	ok = rArea;
	rArea.r_xtop = ok.r_xbot = clipBox->r_xtop;
	if (!(*func)(&ok, cdarg)) result = FALSE;
    }

    /* Left edge of clipBox: */

    if (clipBox->r_xbot > rArea.r_xbot)
    {
	ok = rArea;
	rArea.r_xbot = ok.r_xtop = clipBox->r_xbot;
	if (!(*func)(&ok, cdarg)) result = FALSE;
    }

    /* Just throw away what's left of the area being clipped, since
     * it overlaps the clipBox.
     */

    return result;
} /*GeoDisjoint*/


bool
GeoDummyFunc(box, cdarg)
    Rect * box;
    ClientData cdarg;
{
    return TRUE;
}


/*-------------------------------------------------------------------
 *	GeoCanonicalRect --
 *	Turns a rectangle into a canonical form in which the
 *	lower left is really below and to the left of the upper right.
 *
 *	Results:	None.
 *
 *	Side Effects:
 *	Rectangle rnew is set to the canonical form of rectangle r.
 *-------------------------------------------------------------------
 */

void
GeoCanonicalRect(r, rnew)
    Rect *r;
    Rect *rnew;
{
    if (r->r_xbot > r->r_xtop)
    {
	rnew->r_xbot = r->r_xtop;
	rnew->r_xtop = r->r_xbot;
    }
    else
    {
	rnew->r_xbot = r->r_xbot;
	rnew->r_xtop = r->r_xtop;
    }

    if (r->r_ybot > r->r_ytop)
    {
	rnew->r_ybot = r->r_ytop;
	rnew->r_ytop = r->r_ybot;
    }
    else
    {
	rnew->r_ybot = r->r_ybot;
	rnew->r_ytop = r->r_ytop;
    }
}

/*-------------------------------------------------------------------
 *	GeoScale --
 *
 *	Returns the scale factor associated with a transform.
 *
 *	Results:
 *	Scale factor.
 *
 *	Side Effects:
 *	None.
 *-------------------------------------------------------------------
 */

int
GeoScale(t)
    Transform *t;
{
    int scale;

    scale = t->t_a;
    if (scale == 0)
	scale = t->t_b;

    if (scale < 0)
	scale = (-scale);

    return (scale);
}

/*-------------------------------------------------------------------
 *	GeoScaleTrans --
 *	Scale a transform by the indicated magnification.
 *
 *	Results:	None.
 *
 *	Side Effects:
 *	Trans2 is set to the result of scaling trans1 by (integer)
 *	magnification m.  Non-integer magnifications are not
 *	handled.
 *-------------------------------------------------------------------
 */

void
GeoScaleTrans(trans1, m, trans2)
    Transform *trans1;	/* Transform to be scaled */
    int m;		/* Amount by which to scale */
    Transform *trans2;	/* Result transform */
{
    trans2->t_a = trans1->t_a * m;
    trans2->t_b = trans1->t_b * m;
    trans2->t_c = trans1->t_c * m;
    trans2->t_d = trans1->t_d * m;
    trans2->t_e = trans1->t_e * m;
    trans2->t_f = trans1->t_f * m;
}


/*-------------------------------------------------------------------
 *	GeoRectPointSide --
 *
 *	Returns the side of the rect on which a point lies.
 *
 *	Results:
 *	    A direction, or GEO_CENTER if the point is off the boundary.
 *
 *	Side Effects:
 *	    None.
 *-------------------------------------------------------------------
 */

int
GeoRectPointSide(r, p)
    Rect  * r;
    Point * p;
{
    if(r->r_xbot == p->p_x) return GEO_WEST;
    else
    if(r->r_xtop == p->p_x) return GEO_EAST;
    else
    if(r->r_ybot == p->p_y) return GEO_SOUTH;
    else
    if(r->r_ytop == p->p_y) return GEO_NORTH;
    else
	return(GEO_CENTER);
}

/*-------------------------------------------------------------------
 *	GeoRectRectSide --
 *
 *	Returns the side of the first rect on which the second one
 *	lies.
 *
 *	Results:
 *	    A direction, or GEO_CENTER if the rects don't share some
 *	    coordinate.  Note, this won't detect the case where the
 *	    rectangles don't touch but do share some coordinate.
 *
 *	Side Effects:
 *	    None.
 *-------------------------------------------------------------------
 */

int
GeoRectRectSide(r0, r1)
    Rect * r0;
    Rect * r1;
{
    if(r0->r_xbot == r1->r_xtop) return GEO_WEST;
    else
    if(r0->r_xtop == r1->r_xbot) return GEO_EAST;
    else
    if(r0->r_ybot == r1->r_ytop) return GEO_SOUTH;
    else
    if(r0->r_ytop == r1->r_ybot) return GEO_NORTH;
    else
	return(GEO_CENTER);
}

/* ----------------------------------------------------------------------------
 *
 * GeoDecomposeTransform --
 *
 *	Break a transform up into an optional mirror followed by an optional
 *	rotation.  Translation is ignored.  Maybe someone will add this at
 *	a later date.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Modifies 'angle' and 'upsidedown' parameters.
 *
 * ----------------------------------------------------------------------------
 */

void
GeoDecomposeTransform(t, upsidedown, angle)
    Transform *t;
    bool *upsidedown;		/* Set to TRUE iff we should flip upsidedown 
				 * before rotating.
				 */
    int *angle;			/* Amount to rotate.
				 * Will be 0, 90, 180, or 270.
				 */
{
    Transform notrans;		/* Transform without any translation -- includes
				 * both rotation and mirroring.
				 */
    Transform rotonly;		/* Version of above with only rotation. */

    notrans = *t;
    notrans.t_c = 0;
    notrans.t_f = 0;

    /* Compute rotations and flips. */
    *upsidedown = ((notrans.t_a == 0) ^
	(notrans.t_b == notrans.t_d) ^ (notrans.t_a == notrans.t_e));
    if (*upsidedown) 
	GeoTransTrans(&notrans, &GeoUpsideDownTransform, &rotonly);
    else
	rotonly = notrans;
    /* Verify no flipping. */
    ASSERT(rotonly.t_a == rotonly.t_e, "GeoDecomposeTransform");	  

    *angle = 0;
    if (rotonly.t_b != 0) 
    {
	*angle += 90;
	if (*upsidedown) *angle += 180;
    }
    if ((rotonly.t_a < 0) || (rotonly.t_b < 0)) *angle += 180;
    if (*angle > 270) *angle -= 360;
}
