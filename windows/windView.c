/* windView.c -
 *
 *	Routines in this file control what is viewed in the contents
 *	of the windows.  This includes things like pan, zoom, and loading
 *	of windows.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/windows/windView.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "windows/windows.h"
#include "graphics/glyphs.h"
#include "graphics/graphics.h"
#include "windows/windInt.h"
#include "textio/textio.h"

extern void windNewView();


/*
 * ----------------------------------------------------------------------------
 *
 * windFixSurfaceArea --
 *
 * 	When a window's surface or screen area has been changed,
 *	this procedure is usually called to fix up w->w_surfaceArea and
 *	w_origin.  Before calling this procedure, w->w_origin gives the
 *	screen location of w_surfaceArea.r_ll in SUBPIXEL of a pixel and
 *	w->w_scale is correct, but w->w_surfaceArea may no longer be a
 *	slight overlap of w->w_screenArea as it should be.  This procedure
 *	regenerates w->w_surfaceArea to correspond to w->w_screenArea and
 *	changes w->w_origin to correspond to w->w_surfaceArea.r_ll again.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	w->w_origin and w->w_surfaceArea are modified.
 *
 * ----------------------------------------------------------------------------
 */

void
windFixSurfaceArea(w)
    MagWindow *w;			/* Window to fix up. */
{
    Rect newArea, tmp;

    GEO_EXPAND(&w->w_screenArea, 1, &tmp);
    WindScreenToSurface(w, &tmp, &newArea);
    w->w_origin.p_x += (newArea.r_xbot - w->w_surfaceArea.r_xbot) * w->w_scale;
    w->w_origin.p_y += (newArea.r_ybot - w->w_surfaceArea.r_ybot) * w->w_scale;
    w->w_surfaceArea = newArea;
}


/*
 * ----------------------------------------------------------------------------
 *
 * WindUnload --
 *
 *	Remove a client with the indicated surfaceID and reset the window
 *	to default cell (UNKNOWN).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

void
WindUnload(surfaceID)
    ClientData surfaceID;	/* A unique ID for this surface */
{
    MagWindow *mw;

    for (mw = windTopWindow; mw != NULL; mw = mw->w_nextWindow)
	if (mw->w_surfaceID == surfaceID)
	    DBWloadWindow(mw, (char *)NULL, TRUE, FALSE);
}

/*
 * ----------------------------------------------------------------------------
 * WindLoad --
 *
 *	Load a new client surface into a window.  An initial surface area
 *	must be specified -- this is the area that will be visible in
 *	the window initially.
 *
 * Results:
 *	True if everything went well, false if the client does not
 *	own the window.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

bool
WindLoad(w, client, surfaceID, surfaceArea)
    MagWindow *w;
    WindClient client;		/* The unique identifier of the client */
    ClientData surfaceID;	/* A unique ID for this surface */
    Rect *surfaceArea;		/* The area that should appear in the window */
{
   if (client != w->w_client) return FALSE;

   w->w_surfaceID = surfaceID;
   WindMove(w, surfaceArea);
   return TRUE;
}


/*
 * ----------------------------------------------------------------------------
 * WindMove --
 *
 *	Move the surface under the window so that the given area is visible
 *	and as large as possible.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The window will be now view a different portion of the clients area.  
 *	The client may be called to redisplay the areas that moved.
 * ----------------------------------------------------------------------------
 */

void
WindMove(w, surfaceArea)
    MagWindow *w;			/* the window to be panned */
    Rect *surfaceArea;		/* The area to be viewed */
{
    int size, xscale, yscale;
    int  halfSizePixels, halfSizeUnits;

    /* Compute the scale factor from world coordinates to 1/SUBPIXEL
     * of a pixel.  To be sure that surfaceArea will all fit in the
     * window, compute the scale twice, once using the y-dimension
     * alone, and once using x alone.  Then use the smaller scale factor.
     */
    
    size = (surfaceArea->r_xtop - surfaceArea->r_xbot + 1);
    xscale = ((dlong)(w->w_screenArea.r_xtop - 
	    w->w_screenArea.r_xbot + 1) * SUBPIXEL) / size;

    size = (surfaceArea->r_ytop - surfaceArea->r_ybot + 1);
    yscale = ((w->w_screenArea.r_ytop - 
	    w->w_screenArea.r_ybot + 1) * SUBPIXEL) / size;

    w->w_scale = MIN(xscale, yscale);
    if (w->w_scale < 1)
    {
	/* If this message appears, then it is likely that the	*/
	/* definition for SUBPIXELBITS should be increased.	*/

	TxError("Warning:  At minimum scale!\n");
	w->w_scale = 1;
    }

    /* Recompute w->surfaceArea and w->w_origin, which determine the
     * screen-surface mapping along with w->w_scale.  In order to
     * center surfaceArea in the window, compute the windows's half-size
     * in units of SUBPIXEL of a pixel and in units.  Be sure to round
     * things up so that w->w_surfaceArea actually overlaps the window
     * slightly.
     */

    halfSizePixels = (w->w_screenArea.r_xtop - w->w_screenArea.r_xbot) * HSUBPIXEL;
    halfSizeUnits = (halfSizePixels / w->w_scale) + 1;
    w->w_surfaceArea.r_xbot = (surfaceArea->r_xbot + surfaceArea->r_xtop) / 2
	- halfSizeUnits;
    w->w_surfaceArea.r_xtop = w->w_surfaceArea.r_xbot + 2 * halfSizeUnits + 1;
    w->w_origin.p_x =
	((w->w_screenArea.r_xtop + w->w_screenArea.r_xbot) * HSUBPIXEL) -
	(halfSizeUnits * w->w_scale);

    halfSizePixels = (w->w_screenArea.r_ytop - w->w_screenArea.r_ybot) * HSUBPIXEL;
    halfSizeUnits = (halfSizePixels/w->w_scale) + 1;
    w->w_surfaceArea.r_ybot = (surfaceArea->r_ybot + surfaceArea->r_ytop) / 2
	- halfSizeUnits;
    w->w_surfaceArea.r_ytop = w->w_surfaceArea.r_ybot + 2 * halfSizeUnits + 1;
    w->w_origin.p_y =
	((w->w_screenArea.r_ytop + w->w_screenArea.r_ybot) * HSUBPIXEL) -
	(halfSizeUnits * w->w_scale);

    WindAreaChanged(w, &(w->w_screenArea));
    windNewView(w);
}


/*
 * ----------------------------------------------------------------------------
 * WindZoom --
 *
 *	Zoom a window.  The window will stay centered about the same point
 *	as it is currently.  A factor greater than 1 increases the scale
 *	of the window (higher magnification), while a factor smaller than 1
 *	results in a lower scale (lower magnification).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The window will be now view a different portion of the client's area.  
 *	The client may be called to redisplay part of the screen.
 * ----------------------------------------------------------------------------
 */

void
WindZoom(w, factor)
    MagWindow *w;			/* the window to be zoomed */
    float factor;		/* The amount to zoom by (1 is no change),
				 * greater than 1 is a larger magnification
				 * (zoom in), and less than 1 is less mag.
				 * (zoom out) )
				 */
{
    int centerx, centery;
    Rect newArea;

    centerx = (w->w_surfaceArea.r_xbot + w->w_surfaceArea.r_xtop) / 2;
    centery = (w->w_surfaceArea.r_ybot + w->w_surfaceArea.r_ytop) / 2;

    newArea.r_xbot = centerx - (centerx - w->w_surfaceArea.r_xbot) * factor;
    newArea.r_xtop = centerx + (w->w_surfaceArea.r_xtop - centerx) * factor;
    newArea.r_ybot = centery - (centery - w->w_surfaceArea.r_ybot) * factor;
    newArea.r_ytop = centery + (w->w_surfaceArea.r_ytop - centery) * factor;

    WindMove(w, &newArea);
}

/*
 * ----------------------------------------------------------------------------
 *
 * WindScale --
 *
 *	Zoom all viewing windows by the given factor.  Because this is done
 *	in conjunction with rescaling the geometry, we don't preserve the
 *	center position like WindZoom() does.  The net effect is that the
 *	image in the window doesn't appear to change.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All windows will be now view a different portion of the client's area.  
 *
 * ----------------------------------------------------------------------------
 */

void
WindScale(scalen, scaled)
    int scalen, scaled;
{
    extern void DBScalePoint();
    MagWindow *w2;
    Rect newArea;

    for (w2 = windTopWindow; w2 != NULL; w2 = w2->w_nextWindow)
    {
	newArea.r_xbot = w2->w_surfaceArea.r_xbot;
	newArea.r_xtop = w2->w_surfaceArea.r_xtop;
	newArea.r_ybot = w2->w_surfaceArea.r_ybot;
	newArea.r_ytop = w2->w_surfaceArea.r_ytop;
	DBScalePoint(&newArea.r_ll, scalen, scaled);
	DBScalePoint(&newArea.r_ur, scalen, scaled);
	WindMove(w2, &newArea);
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * WindView --
 *
 * Change the view in the selected window to just contain the
 * bounding box for that window.
 *
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The window underneath the cursor is changed.
 *
 * ----------------------------------------------------------------------------
 */
    
    /* ARGSUSED */

void
WindView(w)
    MagWindow *w;
{
    Rect bbox;
#define SLOP	10	/* Amount of border (in fraction of a screenfull) 
			 * to add.
			 */
    if (w == NULL)
	return;

    if (w->w_bbox == NULL) {
	TxError("Can't do 'view' because w_bbox is NULL.\n");
	TxError("Report this to a magic implementer.\n");
	return;
    };

    bbox = *(w->w_bbox);
    bbox.r_xbot -= (bbox.r_xtop - bbox.r_xbot + 1) / SLOP;
    bbox.r_xtop += (bbox.r_xtop - bbox.r_xbot + 1) / SLOP;
    bbox.r_ybot -= (bbox.r_ytop - bbox.r_ybot + 1) / SLOP;
    bbox.r_ytop += (bbox.r_ytop - bbox.r_ybot + 1) / SLOP;

    WindMove(w, &bbox);
}

/*
 * ----------------------------------------------------------------------------
 *
 * WindScroll --
 *
 *	Scroll the view around.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The window underneath the cursor is changed.  The offset can
 *	be specified either in screen coordinates or surface coordinates
 *	or both.
 *
 * ----------------------------------------------------------------------------
 */

void
WindScroll(w, surfaceOffset, screenOffset)
    MagWindow *w;
    Point *surfaceOffset;	/* An offset in surface coordinates.  The
				 * screen point that used to display surface
				 * point (0,0) will now display surface point
				 * surfaceOffset.  Can be NULL to indicate
				 * no offset.
				 */
    Point *screenOffset;	/* An additional offset in screen coordinates.
				 * Can be NULL to indicate no offset.  If
				 * non-NULL, then after scrolling according
				 * to surfaceOffset, the view is adjusted again
				 * so that the surface unit that used to be
				 * displayed at (0,0) will now be displayed
				 * at screenOffset.  Be careful to make sure
				 * the coordinates in here are relatively
				 * small (e.g. the same order as the screen
				 * size), or else there may be arithmetic
				 * overflow and unexpected results.  Use only
				 * surfaceOffset if you're going to be
				 * scrolling a long distance.
				 */
{
    Rect screenorigin;
    bool useBackingStore = FALSE;
    Point moveorigin;
    Rect refresh, norefresh;

    WindSurfaceToScreenNoClip(w, &GeoNullRect, &screenorigin);

    if (surfaceOffset != NULL)
    {
	w->w_surfaceArea.r_xbot += surfaceOffset->p_x;
	w->w_surfaceArea.r_ybot += surfaceOffset->p_y;
	w->w_surfaceArea.r_xtop += surfaceOffset->p_x;
	w->w_surfaceArea.r_ytop += surfaceOffset->p_y;
    }

    /* Screen offsets are trickier.  Divide up into a whole-unit part
     * (which is applied to w->w_surfaceArea) and a fractional-unit
     * part (which is applied to w->w_origin.  Then readjust both to
     * make sure that w->w_surfaceArea still overlaps the window area
     * on all sides.
     */
    
    if (screenOffset != NULL)
    {
	int units, pixels;

	pixels = screenOffset->p_x * SUBPIXEL;
	units = pixels/w->w_scale;
	w->w_surfaceArea.r_xbot -= units;
	w->w_surfaceArea.r_xtop -= units;
	w->w_origin.p_x += pixels - (w->w_scale*units);

	pixels = screenOffset->p_y * SUBPIXEL;
	units = pixels/w->w_scale;
	w->w_surfaceArea.r_ybot -= units;
	w->w_surfaceArea.r_ytop -= units;
	w->w_origin.p_y += pixels - (w->w_scale*units);
    }

    /* For now, we forget about using backing store in the case	*/
    /* of diagonal scrolls.  Ideally, we would want to register	*/
    /* two rectangles for the window redraw in this case.	*/

    if (w->w_backingStore != (ClientData)NULL)
    {
	if (surfaceOffset)
	    if (surfaceOffset->p_x == 0 || surfaceOffset->p_y == 0)
		useBackingStore = TRUE;
	if (screenOffset)
	    if (screenOffset->p_x == 0 || screenOffset->p_y == 0)
		if (useBackingStore == FALSE)
		    useBackingStore = TRUE;
    }
    windFixSurfaceArea(w);

    /* Finally, if we are going to use backing store, we ought	*/
    /* to adjust the screen movement to the nearest multiple of	*/
    /* 8 so that stipples will remain aligned.  This must be	*/
    /* done after windFixSurfaceArea(), but cannot screw up the	*/
    /* coordinate system, so we do windFixSurfaceArea() again.	*/

    if (useBackingStore)
    {
	int units, pixels;

	WindSurfaceToScreenNoClip(w, &GeoNullRect, &refresh);
	moveorigin.p_x = refresh.r_xbot - screenorigin.r_xbot;
	moveorigin.p_y = refresh.r_ybot - screenorigin.r_ybot;

	pixels = (moveorigin.p_x % 8) * SUBPIXEL;
	units = pixels/w->w_scale;
	w->w_surfaceArea.r_xbot += units;
	w->w_surfaceArea.r_xtop += units;
	w->w_origin.p_x -= (pixels - (w->w_scale*units));

	pixels = (moveorigin.p_y % 8) * SUBPIXEL;
	units = pixels/w->w_scale;
	w->w_surfaceArea.r_ybot += units;
	w->w_surfaceArea.r_ytop += units;
	w->w_origin.p_y -= (pixels - (w->w_scale*units));

	moveorigin.p_x -= (moveorigin.p_x % 8);
	moveorigin.p_y -= (moveorigin.p_y % 8);

        windFixSurfaceArea(w);
    }

    /* If we have backing store available, shift the contents.	*/

    if (useBackingStore)
    {
	refresh = w->w_screenArea;
	norefresh = w->w_screenArea;
	if (moveorigin.p_x > 0)
	{
	    refresh.r_xtop = moveorigin.p_x + w->w_screenArea.r_xbot;
	    norefresh.r_xbot = refresh.r_xtop;
	}
	else if (moveorigin.p_x < 0)
	{
	    refresh.r_xbot = refresh.r_xtop + moveorigin.p_x;
	    norefresh.r_xtop += moveorigin.p_x;
	}
	if (moveorigin.p_y > 0)
	{
	    refresh.r_ytop = moveorigin.p_y + w->w_screenArea.r_ybot;
	    norefresh.r_ybot = refresh.r_ytop;
	}
	else if (moveorigin.p_y < 0)
	{
	    refresh.r_ybot = refresh.r_ytop + moveorigin.p_y;
	    norefresh.r_ytop += moveorigin.p_y;
	}

	(*GrScrollBackingStorePtr)(w, &moveorigin);
	(*GrGetBackingStorePtr)(w, &norefresh);
	WindAreaChanged(w, &refresh);
	/* Update highlights over entire screen area */
	DBWHLRedrawPrepWindow(w, &(w->w_surfaceArea));
    }
    else
	WindAreaChanged(w, &(w->w_screenArea));

    windNewView(w);
}
