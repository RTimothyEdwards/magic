/* CIFreadpoly.c -
 *
 *	This file contains procedures that turn polygons into
 *	rectangles, as part of CIF file reading.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/cif/CIFrdpoly.c,v 1.3 2010/06/24 12:37:15 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "cif/CIFint.h"
#include "cif/CIFread.h"
#include "utils/malloc.h"

#define HEDGE 0		/* Horizontal edge */
#define REDGE 1		/* Rising edge */
#define FEDGE -1	/* Falling edge */

/*
 * ----------------------------------------------------------------------------
 *
 * cifLowX --
 *
 * 	This is a comparison procedure called by qsort.
 *
 * Results:
 *	1 if a.x > b.x,
 *     -1 if a.x < b.x,
 *	0 otherwise.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int 
cifLowX(a, b)
    CIFPath **a, **b;
{
    Point *p, *q;

    p = &(*a)->cifp_point;
    q = &(*b)->cifp_point;
    if (p->p_x < q->p_x)
	return (-1);
    if (p->p_x > q->p_x)
	return (1);
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifLowY --
 *
 * 	This is another comparison procedure called by qsort.
 *
 * Results:
 *	1 if a.y > b.y
 *     -1 if a.y < b.y
 *	0 otherwise
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int 
cifLowY(a, b)
    Point **a, **b;
{
    if ((*a)->p_y < (*b)->p_y)
	return (-1);
    if ((*a)->p_y > (*b)->p_y)
	return (1);
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifOrient --
 *
 * 	This procedure assigns a direction to each of the edges in a
 *	polygon.
 *
 * Results:
 *	TRUE is returned if all of the edges are horizontal or vertical,
 *	FALSE is returned otherwise.  If FALSE is returned, not all of
 *	the directions will have been filled in.
 *
 * Side effects:
 *	The parameter dir is filled in with the directions, which are
 *	each one of HEDGE, REDGE, or FEDGE.
 *
 * ----------------------------------------------------------------------------
 */

bool
cifOrient(edges, nedges, dir)
    CIFPath *edges[];		/* Array of edges to be categorized. */
    int dir[];			/* Array to hold directions. */
    int nedges;			/* Size of arrays. */
{
    Point *p, *q;
    int n;

    for (n = 0; n < nedges; n++)
    {
	/* note - path list should close on itself */
	p = &edges[n]->cifp_point;
	q = &edges[n]->cifp_next->cifp_point;
	if (p->p_y == q->p_y)
	{
	    /* note - point may connect to itself here */
	    dir[n] = HEDGE;
	    continue;
	}
	if (p->p_x == q->p_x)
	{
	    if (p->p_y < q->p_y)
	    {
		dir[n] = REDGE;
		continue;
	    }
	    if (p->p_y > q->p_y)
	    {
		dir[n] = FEDGE;
		continue;
	    }
	    /* Point connects to itself */
	    dir[n] = HEDGE;
	    continue;
	}
	/* It's not Manhattan, folks. */
	return (FALSE);
    }
    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifCross --
 *
 * 	This procedure is used to see if an edge crosses a particular
 *	area.
 *
 * Results:
 *	TRUE is returned if edge is vertical and if it crosses the
 *	y-range defined by ybot and ytop.  FALSE is returned otherwise.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
cifCross(edge, dir, ybot, ytop)
    CIFPath *edge;	/* Pointer to first of 2 path points in edge */
    int dir;			/* Direction of edge */
    int ybot, ytop;		/* Range of interest */
{
    int ebot, etop;

    switch (dir)
    {
	case REDGE:
	    ebot = edge->cifp_point.p_y;
	    etop = edge->cifp_next->cifp_point.p_y;
	    return (ebot <= ybot && etop >= ytop);

	case FEDGE:
	    ebot = edge->cifp_next->cifp_point.p_y;
	    etop = edge->cifp_point.p_y;
	    return (ebot <= ybot && etop >= ytop);

    }

    return (FALSE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFPolyToRects --
 *
 * 	Converts a manhattan polygon (specified as a path) into a
 *	linked list of rectangles.
 *
 * Results:
 *	The return value is a linked list of rectangles, or NULL if
 *	something went wrong.
 *
 * Side effects:
 *	Memory is allocated to hold the list of rectangles.  It is
 *	the caller's responsibility to free up the memory.
 *
 * ----------------------------------------------------------------------------
 */

LinkedRect *
CIFPolyToRects(path, plane, resultTbl, ui)
    CIFPath *path;		/* Path describing a polygon. */
    Plane *plane;		/* Plane to draw on */
    PaintResultType *resultTbl;
    PaintUndoInfo *ui;
{
    int npts = 0, n, *dir, curr, wrapno;
    int xbot, xtop, ybot, ytop;
    Point **pts;
    CIFPath *p, **edges, *tail = 0;
    LinkedRect *rex = 0, *new;

    /* Close path list. */

    for (tail = path; tail->cifp_next; tail = tail->cifp_next);

    if ((tail->cifp_x != path->cifp_x) || (tail->cifp_y != path->cifp_y))
    {
	p = (CIFPath *) mallocMagic ((unsigned) sizeof (CIFPath));
	p->cifp_x = path->cifp_x;
	p->cifp_y = path->cifp_y;
	p->cifp_next = (CIFPath *) 0;
	tail->cifp_next = p;
    }

    CIFMakeManhattanPath(path, plane, resultTbl, ui);

    for (p = path; p->cifp_next; p = p->cifp_next, npts++);
    pts = (Point **)mallocMagic(npts * sizeof(Point *));
    dir = (int *)mallocMagic(npts * sizeof(int));
    edges = (CIFPath **)mallocMagic(npts * sizeof(CIFPath *));
    npts = 0;

    for (p = path; p->cifp_next; p = p->cifp_next, npts++)
    {
	pts[npts] = &(p->cifp_point);
	edges[npts] = p;
    }

    if (npts < 4)
    {
	if (npts > 0)
	    CIFReadError("polygon with fewer than 4 points.\n" );
	goto done;
    }

    /* Sort points by low y, edges by low x */

    qsort ((char *) pts, npts, (int) sizeof (Point *), cifLowY);
    qsort ((char *) edges, npts, (int) sizeof (CIFPath *), cifLowX);

    /* Find out which direction each edge points. */

    if (!cifOrient (edges, npts, dir))
    {
	CIFReadError("non-manhattan polygon.\n" );
	goto done;
    }

    /* Scan the polygon from bottom to top.  At each step, process
     * a minimum-sized y-range of the polygon (i.e. a range such that
     * there are no vertices inside the range).  Use wrap numbers
     * based on the edge orientations to determine how much of the
     * x-range for this y-range should contain material.
     */

    for (curr = 1; curr < npts; curr++)
    {
	/* Find the next minimum-sized y-range. */

	ybot = pts[curr-1]->p_y;
	while (ybot == pts[curr]->p_y)
	    if (++curr >= npts) goto done;
	ytop = pts[curr]->p_y;

	/* Process all the edges that cross the y-range, from left
	 * to right.
	 */

	for (wrapno=0, n=0; n < npts; n++)
	{
	    if (wrapno == 0) xbot = edges[n]->cifp_x;
	    if (!cifCross(edges[n], dir[n], ybot, ytop))
		    continue;
	    wrapno += dir[n] == REDGE ? 1 : -1;
	    if (wrapno == 0)
	    {
		xtop = edges[n]->cifp_point.p_x;
		if (xbot == xtop) continue;
		new = (LinkedRect *) mallocMagic(sizeof(LinkedRect));
		new->r_r.r_xbot = xbot;
		new->r_r.r_ybot = ybot;
		new->r_r.r_xtop = xtop;
		new->r_r.r_ytop = ytop;
		new->r_next = rex;
		rex = new;
	    }
	}
    }

    /* Normally, the loop exits directly to done, below.  It
     * only falls through here if the polygon has a degenerate
     * spike at its top (i.e. if there's only one point with
     * highest y-coordinate).
     */

done:
    freeMagic((char *)edges);
    freeMagic((char *)dir);
    freeMagic((char *)pts);
    return (rex);
}
