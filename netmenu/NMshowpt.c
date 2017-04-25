/* NMshowpoint.c -
 *
 *	This file provides procedures that use the highlight layer
 *	to display information about particular points.  Currently,
 *	this is used to highlight selected terminals in nets.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/netmenu/NMshowpt.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "dbwind/dbwind.h"
#include "utils/styles.h"
#include "utils/main.h"
#include "utils/malloc.h"

/* The net highlight information consists of an array of points.
 * Around each point, this file redisplays a box with a hollow
 * center.  Each box is displayed on the screen either MAXLAMBDAS
 * across, or MAXPIXELS across, whichever is less.  This means
 * that a) the boxes don't get too large at large view magnifications,
 * and b) when erasing a box, we have an upper limit on how much area
 * (in lambda coordinates) will have to be redisplayed.
 */

static Point * nmspPoints = NULL;	/* Pointer to array of points. */
static int nmspArraySize = 0;		/* Size of array. */
static int nmspArrayUsed = 0;		/* Number of entries actually used. */

#define MAXLAMBDAS 30
#define MAXPIXELS 14
#define THICKNESS 3


/*
 * ----------------------------------------------------------------------------
 *
 * NMRedrawPoints --
 *
 * 	This procedure is called by the highlight package to redisplay
 *	our highlights.  A lock has already been made on the window by the
 *	highlight code.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information gets redisplayed on the screen.
 *
 * ----------------------------------------------------------------------------
 */

int
NMRedrawPoints(window, plane)
    MagWindow *window;		/* Window in which to redisplay. */
    Plane *plane;		/* Non-space tiles on this plane indicate
				 * the areas where highlights must be redrawn.
				 */
{
    int i;
    Rect dbArea, screen;
    extern int nmspAlways1();	/* Forward reference. */

    /* Make sure that we have something to display, and that the
     * root definition for this window is the edit's root definition.
     */

    if (nmspArrayUsed == 0) return 0;
    if (((CellUse *)(window->w_surfaceID))->cu_def != EditRootDef) return 0;

    for (i = 0; i < nmspArrayUsed; i += 1)
    {
	/* Make a rectangle around the point that is either MAXLAMBAS
	 * across in database coordinates or MAXPIXELS across in screen
	 * coordinates, whichever is smaller.
	 */

	dbArea.r_xbot = nmspPoints[i].p_x - MAXLAMBDAS/2;
	dbArea.r_ybot = nmspPoints[i].p_y - MAXLAMBDAS/2;
	dbArea.r_xtop = dbArea.r_xbot + MAXLAMBDAS;
	dbArea.r_ytop = dbArea.r_ybot + MAXLAMBDAS;
	if (!DBSrPaintArea((Tile *) NULL, plane, &dbArea,
		&DBAllButSpaceBits, nmspAlways1, (ClientData) NULL))
	    continue;

	WindSurfaceToScreen(window, &dbArea, &screen);
	if (((screen.r_xtop - screen.r_xbot) > MAXPIXELS)
	    || ((screen.r_ytop - screen.r_ybot) > MAXPIXELS))
	{
	    dbArea.r_ur = dbArea.r_ll = nmspPoints[i];
	    WindSurfaceToScreen(window, &dbArea, &screen);
	    screen.r_xbot -= MAXPIXELS/2;
	    screen.r_xtop += MAXPIXELS/2;
	    screen.r_ybot -= MAXPIXELS/2;
	    screen.r_ytop += MAXPIXELS/2;
	}

	/* If the rectangle is less than 2*THICKNESS across, draw it
	 * solid.  Otherwise, draw it with a hollow center (i.e. as
	 * four boxes).
	 */
	
	if (((screen.r_xtop - screen.r_xbot) < 2*THICKNESS)
	    || ((screen.r_ytop - screen.r_ybot) < 2*THICKNESS))
	{
	    GrClipBox(&screen, STYLE_SOLIDHIGHLIGHTS);
	}
	else
	{
	    Rect screen2;
	    screen2 = screen;
	    screen2.r_ytop = screen2.r_ybot + THICKNESS - 1;
	    GrClipBox(&screen2, STYLE_SOLIDHIGHLIGHTS);
	    screen2.r_ytop = screen.r_ytop;
	    screen2.r_ybot = screen2.r_ytop - THICKNESS + 1;
	    GrClipBox(&screen2, STYLE_SOLIDHIGHLIGHTS);
	    screen2.r_ybot = screen.r_ybot + THICKNESS - 1;
	    screen2.r_xtop = screen2.r_xbot + THICKNESS - 1;
	    GrClipBox(&screen2, STYLE_SOLIDHIGHLIGHTS);
	    screen2.r_xtop = screen.r_xtop;
	    screen2.r_xbot = screen2.r_xtop - THICKNESS + 1;
	    GrClipBox(&screen2, STYLE_SOLIDHIGHLIGHTS);
	}
    }
    return 0;
}

int
nmspAlways1()
{
    return 1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMAddPoint --
 *
 * 	This procedure adds a point to the list of those being
 *	highlighted.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The point is added to our internal list of those to be
 *	highlighted.  It will also be displayed on the screen
 *	after the next call to WindUpdate.
 *
 * ----------------------------------------------------------------------------
 */

void
NMAddPoint(point)
    Point *point;		/* Point to be highlighted, in coordinates
				 * of EditRootDef.
				 */
{
    int i;
    Point *newArray;
    Rect area;

    /* Make sure this point isn't already on the list. */

    for (i = 0; i < nmspArrayUsed; i += 1)
    {
	if ((nmspPoints[i].p_x == point->p_x)
	    && (nmspPoints[i].p_y == point->p_y)) return;
    }

    /* Make sure there's enough space in the array.  Make it
     * bigger if necessary.
     */
    
    if (nmspArrayUsed == nmspArraySize)
    {
	nmspArraySize *= 2;
	if (nmspArraySize < 10) nmspArraySize = 10;
	newArray = (Point *) mallocMagic((unsigned)nmspArraySize*sizeof(Point));
	for (i = 0; i < nmspArrayUsed; i += 1)
	    newArray[i] = nmspPoints[i];
	if (nmspPoints != NULL) freeMagic((char *) nmspPoints);
	nmspPoints = newArray;
    }

    /* Add in the new point at the end of the array. */

    nmspPoints[nmspArrayUsed] = *point;
    nmspArrayUsed += 1;

    /* Remember the area to be redisplayed. */

    area.r_xbot = point->p_x - MAXLAMBDAS/2;
    area.r_xtop = point->p_x + MAXLAMBDAS/2;
    area.r_ybot = point->p_y - MAXLAMBDAS/2;
    area.r_ytop = point->p_y + MAXLAMBDAS/2;
    DBWHLRedraw(EditRootDef, &area, FALSE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMDeletePoint --
 *
 * 	This procedure removes a point from the list of those
 *	being highlighted.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The highlight information is removed from our internal list.
 *	The screen will not be updated until the next WindUpdate
 *	call.
 *
 * ----------------------------------------------------------------------------
 */

void
NMDeletePoint(point)
    Point *point;		/* Point to be un-highlighted, in coords
				 * of EditRootDef.
				 */
{
    int i;
    Rect area;

    /* Remove from list (smash list down to cover hole. */

    for (i = 0; i < nmspArrayUsed; i += 1)
    {
	if ((nmspPoints[i].p_x != point->p_x)
	    || (nmspPoints[i].p_y != point->p_y)) continue;
	for (i += 1; i < nmspArrayUsed; i += 1)
	    nmspPoints[i-1] = nmspPoints[i];
	nmspArrayUsed -= 1;
	break;
    }

    /* Redisplay on screen. */

    area.r_xbot = point->p_x - MAXLAMBDAS/2;
    area.r_xtop = area.r_xbot + MAXLAMBDAS;
    area.r_ybot = point->p_y - MAXLAMBDAS/2;
    area.r_ytop = area.r_ybot + MAXLAMBDAS;
    DBWHLRedraw(EditRootDef, &area, TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMClearPoints --
 *
 * 	This procedure clears the list of points being highlighted.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All points are removed from the list.  The screen won't be
 *	updated until the next call to WindUpdate.
 *
 * ----------------------------------------------------------------------------
 */

void
NMClearPoints()
{
    int i;
    Rect area;

    for (i = 0; i < nmspArrayUsed; i += 1)
    {
	area.r_xbot = nmspPoints[i].p_x - MAXLAMBDAS/2;
	area.r_xtop = area.r_xbot + MAXLAMBDAS;
	area.r_ybot = nmspPoints[i].p_y - MAXLAMBDAS/2;
	area.r_ytop = area.r_ybot + MAXLAMBDAS;
	DBWHLRedraw(EditRootDef, &area, TRUE);
    }
    nmspArrayUsed = 0;
}
