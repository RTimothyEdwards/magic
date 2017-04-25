/* CIFreadpaint.c -
 *
 *	This file contains more routines to parse CIF files.  In
 *	particular, it contains the routines to handle paint,
 *	including rectangles, wires, flashes, and polygons.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/cif/CIFrdpt.c,v 1.2 2010/06/24 12:37:15 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <ctype.h>
#include <math.h>		/* for wire path-to-poly path conversion */

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "utils/malloc.h"
#include "database/database.h"
#include "windows/windows.h"
#include "utils/main.h"
#include "cif/CIFint.h"
#include "cif/CIFread.h"


/*
 * ----------------------------------------------------------------------------
 *
 * CIFParseBox --
 *
 * 	This procedure parses a CIF box command.
 *
 * Results:
 *	TRUE is returned if the parse completed successfully, and
 *	FALSE is returned otherwise.
 *
 * Side effects:
 *	A box is added to the CIF information for this cell.  The
 *	box better not have corners that fall on half-unit boundaries.
 *
 * Correction:
 *	A box may be centered on a half lambda grid but have width
 *	and height such that the resulting box is entirely on the lambda
 *	grid.  So:  don't divide by 2 until the last step!
 *	---Tim Edwards, 4/20/00
 *
 * ----------------------------------------------------------------------------
 */

bool
CIFParseBox()
{
    Point	center;
    Point	direction;
    Rect	rectangle, r2;
    int		savescale;
    
    /*	Take the 'B'. */

    TAKE();
    if (cifReadPlane == NULL)
    {
	CIFSkipToSemi();
	return FALSE;
    }

    /* Treat length and width as a point so we can make use of the code in */
    /* CIFParsePoint();  however, before moving on, check that both values */
    /* are strictly positive.						   */

    if (!CIFParsePoint(&rectangle.r_ur, 1))
    {
	CIFReadError("box, but no length and/or width; ignored.\n");
	CIFSkipToSemi();
	return FALSE;
    }
    if (rectangle.r_xtop <= 0)
    {
	CIFReadError("box length not strictly positive; ignored.\n");
	CIFSkipToSemi();
	return FALSE;
    }
    if (rectangle.r_ytop <= 0)
    {
	CIFReadError("box width not strictly positive; ignored.\n");
	CIFSkipToSemi();
	return FALSE;
    }

    savescale = cifReadScale1;

    if (!CIFParsePoint(&center, 2))
    {
	CIFReadError("box, but no center; ignored.\n");
	CIFSkipToSemi();
	return FALSE;
    }

    /* If reading the center causes a CIF input scale to be redefined,	*/
    /* then the length and width must also be changed.			*/

    if (savescale != cifReadScale1)
    {
	rectangle.r_xtop *= (cifReadScale1 / savescale);
	rectangle.r_ytop *= (cifReadScale1 / savescale);
    }

    rectangle.r_xbot = -rectangle.r_xtop;
    rectangle.r_ybot = -rectangle.r_ytop;
   
    /*	Optional direction vector:  have to build transform to do rotate. */

    if (CIFParseSInteger(&direction.p_x))
    {
	if (!CIFParseSInteger(&direction.p_y))
	{
	    CIFReadError("box, direction botched; box ignored.\n");
	    CIFSkipToSemi();
	    return FALSE;
	}
	GeoTransRect(CIFDirectionToTrans(&direction), &rectangle , &r2);
    }
    else r2 = rectangle;

    /* Offset by center only now that rotation is complete, and divide by two. */

    r2.r_xbot = (r2.r_xbot + center.p_x) / 2;
    r2.r_ybot = (r2.r_ybot + center.p_y) / 2;
    r2.r_xtop = (r2.r_xtop + center.p_x) / 2;
    r2.r_ytop = (r2.r_ytop + center.p_y) / 2;

    DBPaintPlane(cifReadPlane, &r2, CIFPaintTable, (PaintUndoInfo *) NULL);
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFParseFlash --
 *
 * 	This routine parses and processes a roundflash command.  The syntax is:
 *	roundflash ::= R diameter center
 *
 *	We approximate a roundflash by a box.
 *
 * Results:
 *	TRUE is returned if the parse completed successfully, and
 *	FALSE is returned otherwise.
 *
 * Side effects:
 *	Paint is added to the current CIF plane.
 *
 * Corrections:  Incorrectly implemented.  Now CIFParsePoint returns the
 *	center coordinate doubled;  in this way, the center can be on the
 *	half-lambda grid but the resulting block on-grid, if the diameter
 *	is an odd number.
 *
 * ----------------------------------------------------------------------------
 */

bool
CIFParseFlash()
{
    int		diameter;
    int		savescale;
    Point	center;
    Rect	rectangle;
    
    /* Take the 'R'. */

    TAKE();
    if (cifReadPlane == NULL)
    {
	CIFSkipToSemi();
	return FALSE;
    }
    if (!CIFParseInteger(&diameter))
    {
	CIFReadError("roundflash, but no diameter; ignored.\n");
	CIFSkipToSemi();
	return FALSE;
    }
    diameter *= cifReadScale1;
    if (diameter % cifReadScale2 != 0)
	CIFReadWarning("Roundflash diameter snapped to nearest integer boundary.\n");

    diameter /= cifReadScale2;
    savescale = cifReadScale1;
    if (!CIFParsePoint(&center, 2))
    {
	CIFReadError("roundflash, but no center; ignored.\n");
	CIFSkipToSemi();
	return FALSE;
    }
    if (savescale != cifReadScale1)
	diameter *= (cifReadScale1 / savescale);

    rectangle.r_xbot = (center.p_x - diameter) / 2;
    rectangle.r_ybot = (center.p_y - diameter) / 2;
    rectangle.r_xtop = (center.p_x + diameter) / 2;
    rectangle.r_ytop = (center.p_y + diameter) / 2;
    DBPaintPlane(cifReadPlane, &rectangle, CIFPaintTable,
	    (PaintUndoInfo *) NULL);
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFPaintWirePath --
 *
 * Draw a "wire path" described by the endpoints of a centerline through
 * a series of segments, and a wire width.  We pass the plane and paint
 * table information so this routine can be used by the database for
 * painting paths from the command-line.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Paints layout into magic.  The original wire path is destroyed
 *	(memory free'd).
 *
 * Notes:
 *	Path coordinates for wires are always assumed to be twice the
 *	actual value to avoid roundoff errors, since the centerline of
 *	a path can be halfway between two coordinates of the layout grid
 *	and still describe a polygon whose endpoints are all on the grid.
 *
 * Warning:
 *	It is still possible to get roundoff problems with different
 *	values of segment width at different angles caused by snapping
 *	to grid points.  While this is "as it should be", it causes
 *	problems when process design rules demand geometry at 45 degrees
 *	only, and the algorithm produces points that are 1 unit off.
 *	A possible solution is to adjust "cwidth" to match the average
 *	value of "width" after snapping at entering and exiting angles.
 *
 * ----------------------------------------------------------------------------
 */

void
CIFPaintWirePath(pathheadp, width, endcap, plane, ptable, ui)
    CIFPath *pathheadp;
    int width;
    bool endcap;
    Plane *plane;
    PaintResultType *ptable;
    PaintUndoInfo *ui;
{
    CIFPath *pathp, *previousp, *nextp, *polypath;
    CIFPath *returnpath, *newpath, *savepath;
    LinkedRect	*rectp;
    double theta, phi, alpha, delta, cwidth, adjwidth, testmitre, savetheta;
    double xmaxoff, ymaxoff, xminoff, yminoff;
    double xmin, ymin, xmax, ymax, xnext, ynext;
    bool firstpoint;

    /* Get rid of any repeat points, which just screw up the algorithm */

    previousp = pathheadp;
    pathp = pathheadp->cifp_next;
    if (pathp != NULL)
    {
	while (pathp->cifp_next != NULL)
	{
	    if (pathp->cifp_next->cifp_x == pathp->cifp_x &&
			pathp->cifp_next->cifp_y == pathp->cifp_y)
	    {
		previousp->cifp_next = pathp->cifp_next;
		freeMagic(pathp);
	    }
	    else
		previousp = pathp;
	    pathp = pathp->cifp_next;
	}
    }

    previousp = pathheadp;
    polypath = NULL;

    /* Single-point paths are okay; just set the endpoints equal */
    if (pathheadp->cifp_next == NULL)
	pathp = pathheadp;
    else
	pathp = pathheadp->cifp_next;

    firstpoint = TRUE;
    theta = 0;
    while (pathp != NULL)
    {
	/* Advance to the next point */
	xmin = (double)previousp->cifp_x;
	xmax = (double)pathp->cifp_x;
	ymin = (double)previousp->cifp_y;
	ymax = (double)pathp->cifp_y;

	/* Angle of this segment */
	savetheta = theta;
	theta = atan2(ymax - ymin, xmax - xmin);

	/* Look ahead to the next point */
	if (firstpoint)
	{
	    /* Back first point up by endcap amount (width,	*/
	    /* which is half the width of the route segment.)	*/

	    if (endcap)
	    {
		xmin -= (double)width * cos(theta);
		ymin -= (double)width * sin(theta);
	    }
	    xminoff = (double)width * cos(theta - 1.5708); /* 90 degrees */
	    yminoff = (double)width * sin(theta - 1.5708);
	    firstpoint = FALSE;

	    newpath = (CIFPath *)mallocMagic(sizeof(CIFPath));
	    newpath->cifp_next = polypath;
	    polypath = newpath;
	    returnpath = polypath;	/* returnpath is always at the end */
	    newpath->cifp_x = round((xmin + xminoff) / 2);
	    newpath->cifp_y = round((ymin + yminoff) / 2);

	    newpath = (CIFPath *)mallocMagic(sizeof(CIFPath));
	    newpath->cifp_next = polypath;
	    polypath = newpath;
	    newpath->cifp_x = round((xmin - xminoff) / 2);
	    newpath->cifp_y = round((ymin - yminoff) / 2);
	}

	nextp = pathp->cifp_next;
	if (nextp != NULL)
	{
	    xnext = (double)nextp->cifp_x;
	    ynext = (double)nextp->cifp_y;
	    phi = atan2(ynext - ymax, xnext - xmax);
	}
	else
	{
	    /* Endpoint:  create 1/2 width endcap */
	    phi = theta;
	    if (endcap)
	    {
		xmax += (double)width * cos(theta);
		ymax += (double)width * sin(theta);
	    }
	}

	alpha = 0.5 * (phi - theta);
	testmitre = fabs(cos(alpha));

	/* This routine does not (yet) do mitre limits, so for	*/
	/* now, we do a sanity check.  In the case of an	*/
	/* extremely acute angle, we generate a warning and	*/
	/* truncate the route.  The mitre limit is arbitrarily	*/
	/* set at 4 times the route width.  Such extreme bends	*/
	/* are usually DRC violations, anyway.  Tighter bends	*/
	/* than this tend to cause difficulties for the		*/
	/* CIFMakeManhattanPath() routine.			*/

	if (testmitre < 0.25) {
	    if (testmitre < 1.0e-10) {
		/* Wire reverses direction.  Break wire here,	*/
		/* draw, and start new polygon.			*/

		TxError("Warning: direction reversal in path.\n");

		phi = theta;
		if (endcap)
		{
		    xmax += (double)width * cos(theta);
		    ymax += (double)width * sin(theta);
		}
		alpha = 0.5 * (phi - theta);
		firstpoint = TRUE;
	    }
	    else {
		TxError("Error: mitre limit exceeded at wire junction.\n");
		TxError("Route has been truncated.\n");
		break;
	   }
	}

	delta = (0.5 * (phi + theta)) - 1.5708;
	cwidth = (double)width / cos(alpha);
	xmaxoff = cwidth * cos(delta);
	ymaxoff = cwidth * sin(delta);

	newpath = (CIFPath *)mallocMagic(sizeof(CIFPath));
	newpath->cifp_next = polypath;
	polypath = newpath;
	newpath->cifp_x = round((xmax - xmaxoff) / 2);
	newpath->cifp_y = round((ymax - ymaxoff) / 2);

	newpath = (CIFPath *)mallocMagic(sizeof(CIFPath));
	newpath->cifp_next = NULL;
	savepath = returnpath;
	returnpath->cifp_next = newpath;
	returnpath = newpath;
	newpath->cifp_x = round((xmax + xmaxoff) / 2);
	newpath->cifp_y = round((ymax + ymaxoff) / 2);

	if (firstpoint == TRUE || nextp == NULL)
	{
	    /* Slow draw for non-Manhattan paths:		*/
	    /* Break the area up into triangles and rectangles	*/

	    rectp = CIFPolyToRects(polypath, plane, ptable, ui);
	    CIFFreePath(polypath);

	    for (; rectp != NULL ; rectp = rectp->r_next)
	    {
		DBPaintPlane(plane, &rectp->r_r, ptable, ui);
		freeMagic((char *) rectp);
	    }
	    polypath = NULL;
	}
	else
	{
	    Rect r;
	    double a1, a2, r2, d1;
	    Point newpt;

	    /* Check if either of the two new segments travels opposite	*/
	    /* to theta.  If so, then we need to find the intersection	*/
	    /* with the previous point, to avoid creating a cut-out	*/
	    /* wedge in the path.					*/

	    a1 = fabs(atan2(returnpath->cifp_y - savepath->cifp_y,
			returnpath->cifp_x - savepath->cifp_x) - theta);
	    a2 = fabs(atan2(polypath->cifp_y - polypath->cifp_next->cifp_y,
			polypath->cifp_x - polypath->cifp_next->cifp_x) - theta);
	    if (a1 > 0.1 && a1 < 6.1)
	    {
		/* Find new intersection point */
		d1 = cos(savetheta) * sin(phi) - sin(savetheta) * cos(phi);
		if (fabs(d1) > 1.0e-4)
		{
		    r2 = (sin(phi) * (returnpath->cifp_x - savepath->cifp_x)
		    	 - cos(phi) * (returnpath->cifp_y - savepath->cifp_y)) / d1;
		    savepath->cifp_x += round(r2 * cos(savetheta));
		    savepath->cifp_y += round(r2 * sin(savetheta));
		}
	    }
	    else if (a2 > 0.1 && a2 < 6.1)
	    {
		/* Find new intersection point */
		d1 = cos(savetheta) * sin(phi) - sin(savetheta) * cos(phi);
		if (fabs(d1) > 1.0e-4)
		{
		    r2 = (sin(phi) * (polypath->cifp_x - polypath->cifp_next->cifp_x)
		     	- cos(phi) * (polypath->cifp_y - polypath->cifp_next->cifp_y))
		     	/ d1;
		    polypath->cifp_next->cifp_x += round(r2 * cos(savetheta));
		    polypath->cifp_next->cifp_y += round(r2 * sin(savetheta));
		}
	    }
	}

	previousp = pathp;
	pathp = pathp->cifp_next;
    }
    CIFFreePath(pathheadp);
}

/*
 * ----------------------------------------------------------------------------
 *
 * PaintPolygon --
 *
 * Convert a list of points in the form of an array of type Point to a
 * CIFPath linked structure, and paint them into the database.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Paints tiles into the layout database.  Calling routine is
 *	responsible for free'ing memory of the pointlist, if necessary.
 *
 * Notes:
 *	This is a database routine, not a CIF routine.  However, it makes
 *	use of the CIFPath structure, so it is included here.
 *
 * ----------------------------------------------------------------------------
 */

LinkedRect *
PaintPolygon(pointlist, number, plane, ptable, ui, keep)
    Point *pointlist;		/* Array of Point structures */
    int number;			/* total number of points */
    Plane *plane;		/* Plane structure to paint into */
    PaintResultType *ptable;	/* Paint result table */
    PaintUndoInfo *ui;		/* Undo record */
    bool keep;			/* Return list of rects if true */
{
    LinkedRect	*rectp, *rectlist;
    CIFPath *newpath, *cifpath = (CIFPath *)NULL;
    int i;
   
    for (i = 0; i < number; i++)
    {
	newpath = (CIFPath *) mallocMagic((unsigned) sizeof (CIFPath));
	newpath->cifp_x = pointlist[i].p_x;
	newpath->cifp_y = pointlist[i].p_y;
	newpath->cifp_next = cifpath;
	cifpath = newpath;
    }

    rectlist = CIFPolyToRects(cifpath, plane, ptable, ui);
    CIFFreePath(cifpath);

    for (rectp = rectlist; rectp != NULL ; rectp = rectp->r_next)
    {
	DBPaintPlane(plane, &rectp->r_r, ptable, ui);
	if (!keep) freeMagic((char *) rectp);
    }
    return (keep) ? rectlist : (LinkedRect *)NULL;
}

/*
 * ----------------------------------------------------------------------------
 *
 * PaintWireList --
 *
 * Convert a list of points in the form of an array of type Point to a
 * CIFPath linked structure, and paint them into the database.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Paints tiles into the layout database.  Calling routine is
 *	responsible for free'ing memory of the pointlist, if necessary.
 *
 * Notes:
 *	This is a database routine, not a CIF routine.  However, it makes
 *	use of the CIFPath structure, so it is included here.
 *
 * ----------------------------------------------------------------------------
 */

void
PaintWireList(pointlist, number, width, endcap, plane, ptable, ui)
    Point *pointlist;		/* Array of Point structures */
    int number;			/* total number of points */
    int width;			/* Route width of path */
    bool endcap;		/* Whether or not to add 1/2 width endcaps */
    Plane *plane;		/* Plane structure to paint into */
    PaintResultType *ptable;	/* Paint result table */
    PaintUndoInfo *ui;		/* Undo record */
{
    CIFPath *newpath, *cifpath = (CIFPath *)NULL;
    int i;
   
    for (i = 0; i < number; i++)
    {
	newpath = (CIFPath *) mallocMagic((unsigned) sizeof (CIFPath));
	newpath->cifp_x = pointlist[i].p_x;
	newpath->cifp_y = pointlist[i].p_y;
	newpath->cifp_next = cifpath;
	cifpath = newpath;
    }
    CIFPaintWirePath(cifpath, width, endcap, plane, ptable, ui);
}


/*
 * ----------------------------------------------------------------------------
 *
 * CIFParseWire --
 *
 * 	This procedure parses CIF wire commands, and adds paint
 *	to the current CIF cell.  A wire command consists of
 *	an integer width, then a path.
 *
 * Results:
 *	TRUE is returned if the parse completed successfully, and
 *	FALSE is returned otherwise.
 *
 * Side effects:
 *	The current CIF planes are modified.
 *
 * ----------------------------------------------------------------------------
 */

bool
CIFParseWire()
{
    int		width;
    CIFPath	*pathheadp, *polypath;
    int		savescale;

    /* Take the 'W'. */

    TAKE();
    if (cifReadPlane == NULL)
    {
	CIFSkipToSemi();
	return FALSE;
    }
    if (!CIFParseInteger(&width))
    {
	CIFReadError("wire, but no width; ignored.\n");
	CIFSkipToSemi();
	return FALSE;
    }
    width *= cifReadScale1;
    if (width % cifReadScale2 != 0)
	CIFReadWarning("Wire width snapped to nearest integer boundary.\n");

    width /= cifReadScale2;
    savescale = cifReadScale1;
    if (!CIFParsePath(&pathheadp, 2))
    {
	CIFReadError("wire, but improper path; ignored.\n");
	CIFSkipToSemi();
	return FALSE;
    }
    if (savescale != cifReadScale1)
	width *= (cifReadScale1 / savescale);

    CIFPaintWirePath(pathheadp, width, TRUE, cifReadPlane, CIFPaintTable,
		(PaintUndoInfo *)NULL);
    return TRUE;
}


/*
 * ----------------------------------------------------------------------------
 *
 * CIFParseLayer --
 *
 * 	This procedure parses layer changes.  The syntax is:
 *	layer ::= L { blank } processchar layerchars
 *
 * Results:
 *	TRUE is returned if the parse completed successfully, and
 *	FALSE is returned otherwise.
 *
 * Side effects:
 *	Switches the CIF plane where paint is being saved.
 *
 * ----------------------------------------------------------------------------
 */

bool
CIFParseLayer()
{
#define MAXCHARS 4
    char	name[MAXCHARS+1];
    char	c;
    int		i;
    TileType	type;

    /* Take the 'L'. */

    TAKE();
    CIFSkipBlanks();

    /* Get the layer name. */

    for (i=0; i<=MAXCHARS; i++)
    {
	c = PEEK();
	if (isdigit(c) || isupper(c))
	    name[i] = TAKE();
	else break;
    }
    name[i] = '\0';

    /* Set current plane for use by the routines that parse geometric
     * elements.
     */
    
    type = CIFReadNameToType(name, FALSE);
    if (type < 0)
    {
	cifReadPlane = NULL;
	cifCurLabelType = TT_SPACE;
	CIFReadError("layer %s isn't known in the current style.\n",
		name);
    } else {
	cifCurLabelType = cifCurReadStyle->crs_labelLayer[type];
	cifReadPlane = cifCurReadPlanes[type];
    }

    CIFSkipToSemi();
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFParsePoly --
 *
 * 	This procedure reads and processes a polygon command.  The syntax is:
 *	polygon ::= path
 *
 * Results:
 *	TRUE is returned if the parse completed successfully, and
 *	FALSE is returned otherwise.
 *
 * Side effects:
 *	Paint is added to the current CIF plane.
 *
 * ----------------------------------------------------------------------------
 */

bool
CIFParsePoly()
{
    CIFPath	*pathheadp;
    LinkedRect	*rectp;

    /* Take the 'P'. */

    TAKE();
    if (cifReadPlane == NULL)
    {
	CIFSkipToSemi();
	return FALSE;
    }
    if (!CIFParsePath(&pathheadp, 1)) 
    {
	CIFReadError("polygon, but improper path; ignored.\n");
	CIFSkipToSemi();
	return FALSE;
    }

    /* Convert the polygon to rectangles. */

    rectp = CIFPolyToRects(pathheadp, cifReadPlane, CIFPaintTable,
		(PaintUndoInfo *)NULL);
    CIFFreePath(pathheadp);
    if (rectp == NULL)
    {
        /* The non-Manhattan geometry polygon parsing algorithm */
        /* typically leaves behind degenerate paths, so they    */
        /* should not be considered erroneous.                  */
	CIFSkipToSemi();
	return FALSE;
    }
    for (; rectp != NULL ; rectp = rectp->r_next)
    {
	DBPaintPlane(cifReadPlane, &rectp->r_r, CIFPaintTable,
		(PaintUndoInfo *) NULL);
	freeMagic((char *) rectp);
    }
    return TRUE;
}
