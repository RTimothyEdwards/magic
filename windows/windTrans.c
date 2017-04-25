
/* windTransform.c -
 *
 *	Routines to transform from screen coords to surface coords and the
 *	other way.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/windows/windTrans.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include "utils/magic.h"
#include "utils/geometry.h"
#include "windows/windows.h"
#include "graphics/glyphs.h"
#include "windows/windInt.h"


/*
 * ----------------------------------------------------------------------------
 * WindScreenToSurface --
 *
 *	Convert a rectangle in screen coordinates to surface coords.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Returns a rectangle that is big enough to fully contain everything
 *	that is displayed in the screen rectangle.
 * ----------------------------------------------------------------------------
 */

void
WindScreenToSurface(w, screen, surface)
    MagWindow *w;			/* Window in whose coordinates screen is
				 * is defined.
				 */
    Rect *screen;		/* A rectangle in screen coordinates */
    Rect *surface;		/* A pointer to a rectangle to be filled
				 * in with a rectangle in surface coords that
				 * is big enough to contain everything 
				 * displayed within the screen rectangle.
				 */
{
    Rect r;
    WindPointToSurface(w, &(screen->r_ll), (Point *) NULL, surface);
    WindPointToSurface(w, &(screen->r_ur), (Point *) NULL, &r);
    surface->r_ur = r.r_ur;
}


/*
 * ----------------------------------------------------------------------------
 * WindPointToSurface --
 *
 *	Take a point in screen coordinates and find the nearest point
 *	in surface coordinates.  Also return a box in surface coords
 *	that completely surrounds the pixel.
 *
 * Results:
 *	None.
 *
 * Side effects:
 * 	The parameters surfaceBox and surfacePoint are modified, if
 *	they are non-NULL.  The box is always at least 1x1, but it
 *	will be larger if the scale is so coarse that a single screen
 *	coordinate is more than one surface coordinate.  In this case
 *	the box is large enough to include every surface coordinate
 *	that can map to the given pixel (this feature is essential
 *	if WindScreenToSurface is to use this routine).
 *
 * ----------------------------------------------------------------------------
 */

void
WindPointToSurface(w, screenPoint, surfacePoint, surfaceBox)
    MagWindow *w;		/* Window in whose coordinate system the
			 * transformation is to be done.
			 */
    Point *screenPoint;	/* The point in screen coordinates. */
    Point *surfacePoint;/* Filled in with the nearest surface coordinate.
			 * Nothing is filled in if the pointer is NULL.
			 */
    Rect *surfaceBox;	/* Filled in with a box in surface coordinates that
			 * surrounds the point.  It is not filled in if this
			 * is a NULL pointer.
			 */
{
    int tmp, adjust, unitsPerPixel;

    /* Do the inverse transformation twice, once with rounding for
     * the point and once with truncation for the box.
     */
    
    unitsPerPixel = SUBPIXEL/w->w_scale;

    if (surfaceBox != NULL)
    {
	tmp = (SUBPIXEL*screenPoint->p_x) - w->w_origin.p_x;
	if (tmp < 0) tmp -= w->w_scale-1;
	surfaceBox->r_xbot = w->w_surfaceArea.r_xbot + tmp/w->w_scale;
	surfaceBox->r_xtop = surfaceBox->r_xbot + unitsPerPixel + 1;
    
	tmp = (SUBPIXEL*screenPoint->p_y) - w->w_origin.p_y;
	if (tmp < 0) tmp -= w->w_scale-1;
	surfaceBox->r_ybot = w->w_surfaceArea.r_ybot + tmp/w->w_scale;
	surfaceBox->r_ytop = surfaceBox->r_ybot + unitsPerPixel + 1;
    }

    if (surfacePoint != NULL)
    {
        adjust = w->w_scale/2;
	tmp = (SUBPIXEL*screenPoint->p_x) - w->w_origin.p_x;
	if (tmp >= 0) tmp += adjust;
	else tmp -= adjust;
	surfacePoint->p_x = w->w_surfaceArea.r_xbot + tmp/w->w_scale;

	tmp = (SUBPIXEL*screenPoint->p_y) - w->w_origin.p_y;
	if (tmp >= 0) tmp += adjust;
	else tmp -= adjust;
	surfacePoint->p_y = w->w_surfaceArea.r_ybot + tmp/w->w_scale;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * WindSurfaceToScreen --
 *
 * 	This procedure transforms a rectangle in surface coordinates
 *	to a rectangle in the screen coordinates of a particular window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The rectangle pointed to by screen is filled in with the
 *	coordinates (in pixels) of the screen area corresponding
 *	to surface.  A pixel is included in screen if it is
 *	touched in any way by surface.  If any of the edges of
 *	screen is outside w->w_surfaceArea, the corresponding
 *	edge of the result will also be outside w->w_screenArea.
 *
 * Design Note:
 *	This procedure is trickier than it looks, for a couple of
 *	reasons having to do with numerical accuracy.  The
 *	transformation is done in units of 1/SUBPIXEL pixel (fix-point
 *	arithmetic), in order to avoid accumulated round-off error
 *	when there are fractional pixels per surface unit.  Furthermore,
 *	it's important that the transformation to screen units NOT be
 *	done with a procedure like GeoTransRect.  The reason is that
 *	GeoTransRect does the multiplication before the addition or
 *	subtraction, which can result in arithmeic overflow.  We use
 *	a different form for the transform here, in terms of an origin
 *	and a scale, with the translation done FIRST and multiplication
 *	later.  This, combined with the clipping to w->w_surfaceArea,
 *	guarantees numerical accuracy as long as surface coordinates
 *	fit into an integer and SUBPIXEL*screenSize fits into an integer.
 *
 * ----------------------------------------------------------------------------
 */

void
WindSurfaceToScreen(w, surface, screen)
    MagWindow *w;			/* Window in whose coordinate system the
				 * transform is to be done.
				 */
    Rect *surface;		/* Rectangle in surface coordinates of w. */
    Rect *screen;		/* Rectangle filled in with screen coordinates
				 * (in w) of surface.
				 */
{
    int tmp;

    /* Do the four coordinates one at a time. */
    
    /* The apparently redundant clipping (also done later by the box	*/
    /* drawing routine) is necessary for two reasons:  1) it prevents	*/
    /* having to do a multiply and shift function for any box extending	*/
    /* outside the viewport, and more critically 2) it prevents integer	*/
    /* overflow when a tile extends very far out of the viewport.	*/

    tmp = surface->r_xbot;
    if (tmp > w->w_surfaceArea.r_xtop)
	screen->r_xbot = w->w_screenArea.r_xtop + 1;
    else
    {
	tmp -= w->w_surfaceArea.r_xbot;
	if (tmp < 0)
	    screen->r_xbot = w->w_screenArea.r_xbot - 1;
        else screen->r_xbot = (w->w_origin.p_x + (tmp * w->w_scale)) >> SUBPIXELBITS;
    }
    
    tmp = surface->r_ybot;
    if (tmp > w->w_surfaceArea.r_ytop)
	screen->r_ybot = w->w_screenArea.r_ytop + 1;
    else
    {
	tmp -= w->w_surfaceArea.r_ybot;
	if (tmp < 0)
	    screen->r_ybot = w->w_screenArea.r_ybot - 1;
        else screen->r_ybot = (w->w_origin.p_y + (tmp * w->w_scale)) >> SUBPIXELBITS;
    }
    
    tmp = surface->r_xtop;
    if (tmp > w->w_surfaceArea.r_xtop)
	screen->r_xtop = w->w_screenArea.r_xtop + 1;
    else
    {
	tmp -= w->w_surfaceArea.r_xbot;
	if (tmp < 0)
	    screen->r_xtop = w->w_screenArea.r_xbot - 1;
        else screen->r_xtop = (w->w_origin.p_x + (tmp * w->w_scale)) >> SUBPIXELBITS;
    }
    
    tmp = surface->r_ytop;
    if (tmp > w->w_surfaceArea.r_ytop)
	screen->r_ytop = w->w_screenArea.r_ytop + 1;
    else
    {
	tmp -= w->w_surfaceArea.r_ybot;
	if (tmp < 0)
	    screen->r_ytop = w->w_screenArea.r_ybot - 1;
        else screen->r_ytop = (w->w_origin.p_y + (tmp * w->w_scale)) >> SUBPIXELBITS;
    }
}

/*     
 * ----------------------------------------------------------------------------
 *
 * WindSurfaceToScreenNoClip --
 *
 *	This procedure is like WindSurfaceToScreen except that it
 *	does not clip to the window boundary.  This is necessary for
 *	non-Manhattan tiles to make sure they are not clipped.
 *	However, this routine is also useful for Manhattan geometry
 *	when, for example, determining the transformation of a unit
 *	coordinate.
 *
 *	We convert to type double long because at close-in scales, large
 *	values of w_scale can cause integer overflow on 32-bit integers.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Pointer argument "screen" is filled with the transformed
 *	coordinates.
 *
 * ----------------------------------------------------------------------------
 */
 
void
WindSurfaceToScreenNoClip(w, surface, screen)
    MagWindow *w;
    Rect *surface;
    Rect *screen;
{
    dlong tmp, dval;

    tmp = (dlong)(surface->r_xbot - w->w_surfaceArea.r_xbot);
    dval = (dlong)w->w_origin.p_x + (tmp * (dlong)w->w_scale);
    screen->r_xbot = (int)(dval >> SUBPIXELBITS);
    tmp = surface->r_ybot - w->w_surfaceArea.r_ybot;
    dval = (dlong)w->w_origin.p_y + (tmp * (dlong)w->w_scale);
    screen->r_ybot = (int)(dval >> SUBPIXELBITS);
    tmp = surface->r_xtop - w->w_surfaceArea.r_xbot;
    dval = (dlong)w->w_origin.p_x + (tmp * (dlong)w->w_scale);
    screen->r_xtop = (int)(dval >> SUBPIXELBITS);
    tmp = surface->r_ytop - w->w_surfaceArea.r_ybot;
    dval = (dlong)w->w_origin.p_y + (tmp * (dlong)w->w_scale);
    screen->r_ytop = (int)(dval >> SUBPIXELBITS);
}


/*
 * ----------------------------------------------------------------------------
 *
 * WindPointToScreen --
 *
 * 	This is just like WindSurfaceToScreen, except it transforms a
 *	point instead of a rectangle.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Screen is modified to contain screen coordinates.  They may be
 *	clipped just as in WindSurfaceToScreen.
 *
 * ----------------------------------------------------------------------------
 */

void
WindPointToScreen(w, surface, screen)
    MagWindow *w;			/* Window in whose coordinate system the
				 * transform is to be done.
				 */
    Point *surface;		/* Point in surface coordinates of w. */
    Point *screen;		/* Point filled in with screen coordinates
				 * (in w) of surface.
				 */
{
    int tmp;

    /* Do the coordinates one at a time. */
    
    tmp = surface->p_x;
    if (tmp > w->w_surfaceArea.r_xtop)
	tmp = w->w_surfaceArea.r_xtop;
    tmp -= w->w_surfaceArea.r_xbot;
    if (tmp < 0) tmp = 0;
    screen->p_x = (w->w_origin.p_x + (tmp*w->w_scale)) >> SUBPIXELBITS;
    
    tmp = surface->p_y;
    if (tmp > w->w_surfaceArea.r_ytop)
	tmp = w->w_surfaceArea.r_ytop;
    tmp -= w->w_surfaceArea.r_ybot;
    if (tmp < 0) tmp = 0;
    screen->p_y = (w->w_origin.p_y + (tmp*w->w_scale)) >> SUBPIXELBITS;
}

/*
 * ----------------------------------------------------------------------------
 *
 * windScreenToFrame --
 *
 * 	Transform a point from screen coordinates to frame coordinates.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frame is filled in with the frame coordinates corresponding to
 *	the screen coordinates.
 *
 * ----------------------------------------------------------------------------
 */

void
windScreenToFrame(w, screen, frame)
    MagWindow *w;			/* Window in whose coordinate system the
				 * transform is to be done.
				 */
    Point *screen;		/* Point in screen coordinates of w. */
    Point *frame;		/* Point filled in with frame coordinates.
				 */
{
    switch ( WindPackageType )
    {
	case WIND_X_WINDOWS:
	    frame->p_x = screen->p_x + w->w_frameArea.r_xbot;
	    frame->p_y = screen->p_y + w->w_frameArea.r_ybot;
	    break;
	
	default:
	    /* WIND_MAGIC_WINDOWS */
	    *frame = *screen;
    }
}
