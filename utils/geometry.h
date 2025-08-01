/* geometry.h --
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
 * This module contains the basic definitions for geometrical
 * elements:  points, rectangles, and transforms.
 */

/* rcsid "$Header: /usr/cvsroot/magic-8.0/utils/geometry.h,v 1.2 2009/09/10 20:32:55 tim Exp $" */

#ifndef _MAGIC__UTILS__GEOMETRY_H
#define _MAGIC__UTILS__GEOMETRY_H 

/*-------------------------------------------------------------------
 * Structure definition for Point (an x,y pair).
 *-------------------------------------------------------------------
 */

typedef struct
{
    int p_x;
    int p_y;
} Point;

/*-------------------------------------------------------------------
 * Structure definition for rectangles.  A rectangle is defined
 * by the coordinates of its lower-left and upper-right corners.
 * Most routines that manipulate rectangles require the first
 * point to really be the lower-left one, so be careful about this.
 * A null rectangle is indicated by making both x-coordinates the
 * same.
 *-------------------------------------------------------------------
 */

typedef struct
{
    Point r_ll;			/* Lower-left corner of rectangle. */
    Point r_ur;			/* Upper-right corner of rectangle. */
} Rect;

#define r_xbot r_ll.p_x
#define r_ybot r_ll.p_y
#define r_xtop r_ur.p_x
#define r_ytop r_ur.p_y

typedef struct _linkedRect	/* A linked rectangle */
{
    Rect r_r;			/* A rectangle. */
    int  r_type;	   	/* Tile type of rectangle */
    struct _linkedRect *r_next; /* Pointer to another linked rectangle */
} LinkedRect;

/*-------------------------------------------------------------------
 * Structure definition for geometrical transformers.  They are
 * stored in the form described by Newman and Sproull on page 57.
 * Magic allows only 90 degree orientations, and normally there
 * is no scaling (scaling only occurs when transforming to pixel
 * coordinates).  Thus the elements a, b, d, and e always have
 * one of the following forms, where S is the scaling factor:
 *
 *  S  0    0 -S    -S  0    0  S    S  0    0  S    -S  0    0 -S
 *  0  S    S  0     0 -S   -S  0    0 -S    S  0     0  S   -S  0
 *
 * The first four forms correspond to clockwise rotations of 0, 90,
 * 180, and 270 degrees, and the second four correspond to the same
 * four orientations flipped upside down (mirror across the x-axis
 * after rotating).
 *-------------------------------------------------------------------
 */

typedef struct
{
    int t_a, t_b, t_c, t_d, t_e, t_f;
} Transform;

/*-------------------------------------------------------------------
 *	Definitions for positions.  Positions are small integers
 *	used to select where text gets placed, relative to a point.
 *-------------------------------------------------------------------
 */

#define GEO_CENTER	0
#define GEO_NORTH	1
#define GEO_NORTHEAST	2
#define GEO_EAST	3
#define GEO_SOUTHEAST	4
#define GEO_SOUTH	5
#define GEO_SOUTHWEST	6
#define GEO_WEST	7
#define GEO_NORTHWEST	8


/* See if two points are equal */
#define GEO_SAMEPOINT(p1, p2) ((p1).p_x == (p2).p_x && (p1).p_y == (p2).p_y)

/* See if two rects are equal */
#define	GEO_SAMERECT(r1, r2) \
    (GEO_SAMEPOINT((r1).r_ll, (r2).r_ll) && GEO_SAMEPOINT((r1).r_ur, (r2).r_ur))

/*-------------------------------------------------------------------
 *	The following macros are predicates to see if two
 *	rectangles overlap or touch.
 *-------------------------------------------------------------------
 */

/* see if the rectangles overlap (the overlap contains some area) */

#define GEO_OVERLAP(r1, r2) \
    (((r1)->r_xbot < (r2)->r_xtop) && ((r2)->r_xbot < (r1)->r_xtop) \
    && ((r1)->r_ybot < (r2)->r_ytop) && ((r2)->r_ybot < (r1)->r_ytop))

/* see if the rectangles touch (share part of a side) or overlap */

#define GEO_TOUCH(r1, r2) \
    (((r1)->r_xbot <= (r2)->r_xtop) && ((r2)->r_xbot <= (r1)->r_xtop) \
    && ((r1)->r_ybot <= (r2)->r_ytop) && ((r2)->r_ybot <= (r1)->r_ytop))

/* see if rectangle r1 completely surrounds rectangle r2.  Touching between
 * r2 and r1 IS allowed.
 */

#define GEO_SURROUND(r1,r2) \
    ( ((r2)->r_xbot >= (r1)->r_xbot) && ((r2)->r_xtop <= (r1)->r_xtop) \
    && ((r2)->r_ybot >= (r1)->r_ybot) && ((r2)->r_ytop <= (r1)->r_ytop) )

/* see if rectangle r1 completely surrounds rectangle r2 WITHOUT touching it.
 */

#define GEO_SURROUND_STRONG(r1,r2) \
    ( ((r2)->r_xbot > (r1)->r_xbot) && ((r2)->r_xtop < (r1)->r_xtop) \
    && ((r2)->r_ybot > (r1)->r_ybot) && ((r2)->r_ytop < (r1)->r_ytop) )

/* See if point p in inside of or on the border of r */
#define GEO_ENCLOSE(p, r)	\
    ( ((p)->p_x <= (r)->r_xtop) && ((p)->p_x >= (r)->r_xbot) &&  \
      ((p)->p_y <= (r)->r_ytop) && ((p)->p_y >= (r)->r_ybot) )

/* See if a label is in a given area */
#define GEO_LABEL_IN_AREA(lab,area) \
    (GEO_SURROUND(area, lab) || \
	(GEO_RECTNULL(area) && GEO_TOUCH(lab, area) && \
	!(GEO_SURROUND_STRONG(lab, area))))

/* See if a rectangle has no area. */

#define GEO_RECTNULL(r) \
    (((r)->r_xbot >= (r)->r_xtop) || ((r)->r_ybot >= (r)->r_ytop))

/* Expand a rectangular area by a given amount. */

#define GEO_EXPAND(src, amount, dst) \
{ \
    (dst)->r_xbot = (src)->r_xbot - amount; \
    (dst)->r_ybot = (src)->r_ybot - amount; \
    (dst)->r_xtop = (src)->r_xtop + amount; \
    (dst)->r_ytop = (src)->r_ytop + amount; \
}

/* Sizes of rectangles. */
#define GEO_WIDTH(r)    ((r)->r_xtop - (r)->r_xbot)
#define GEO_HEIGHT(r)   ((r)->r_ytop - (r)->r_ybot)


/*-------------------------------------------------------------------
 *	Declarations for exported procedures:
 *-------------------------------------------------------------------
 */

extern void GeoTransPointDelta(const Transform *t, const Point *p1, Point *p2);
extern int GeoTransAngle(const Transform *t, int a);

extern int GeoNameToPos(const char *name, bool manhattan, bool verbose);
extern const char *GeoPosToName(int pos);
extern void GeoTransRect(const Transform *t, const Rect *r1, Rect *r2);
extern void GeoTransTrans(const Transform *first, const Transform *second, Transform *net);
extern void GeoTransPoint(const Transform *t, const Point *p1, Point *p2);
extern void GeoInvertTrans(const Transform *t, Transform *inverse);
extern void GeoTranslateTrans(const Transform *trans1, int x, int y, Transform *trans2);
extern void GeoTransTranslate(int x, int y, const Transform *trans1, Transform *trans2);
extern int GeoTransPos(const Transform *t, int pos);
extern int GeoTransOrient(const Transform *t);
extern bool GeoInclude(const Rect *src, Rect *dst);
extern bool GeoIncludeAll(const Rect *src, Rect *dst);
extern void GeoClip(Rect *r, const Rect *area);
extern void GeoClipPoint(Point *p, const Rect *area);
extern int GeoRectPointSide(const Rect *r, const Point *p);
extern int GeoRectRectSide(const Rect *r0, const Rect *r1);
extern void GeoIncludePoint(const Point *src, Rect *dst);
extern void GeoDecomposeTransform(const Transform *t, bool *upsidedown, int *angle);
extern void GeoIncludeRectInBBox(const Rect *r, Rect *bbox);
extern void GeoCanonicalRect(const Rect *r, Rect *rnew);

/* C99 compat */
extern bool GeoDisjoint(Rect *area, const Rect *clipBox, bool (*func)(), ClientData cdarg);
extern int GeoScale(const Transform *t);
extern void GeoScaleTrans(const Transform *trans1, int m, Transform *trans2);

/*
 *-------------------------------------------------------------------
 *	Declarations of exported transforms and rectangles:
 *-------------------------------------------------------------------
 */

extern const Transform GeoIdentityTransform;
extern const Transform GeoUpsideDownTransform;
extern const Transform GeoSidewaysTransform;
extern const Transform Geo90Transform;
extern const Transform Geo180Transform;
extern const Transform Geo270Transform;
extern const Transform GeoRef45Transform;
extern const Transform GeoRef135Transform;

extern const Rect GeoNullRect;
extern const Rect GeoInvertedRect;
extern const Point GeoOrigin;

extern const int GeoOppositePos[];

#endif /* _MAGIC__UTILS__GEOMETRY_H */
