/* grX11su2.c -
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
 * This file contains additional functions to manipulate an X
 * color display.  Included here are rectangle drawing and color map
 * loading.
 */

/* #define HIRESDB */	/* debugging only */

#include <stdio.h>
#include <stdlib.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "utils/magic.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "graphics/graphics.h"
#include "windows/windows.h"
#include "graphics/graphicsInt.h"
#include "grX11Int.h"

extern char *DBWStyleType;
extern unsigned long grPlanes[256];
extern unsigned long grPixels[256];
extern XColor   colors[256*3];	/* Unique colors used by Magic */
extern Colormap grXcmap;


/*---------------------------------------------------------
 * GrXSetCMap:
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	The values in the color map are set from the system
 *	colormap (see grCMap.c).  X color cells are allocated
 *	if this display has more than 1 plane.
 *
 *---------------------------------------------------------
 */

void
GrX11SetCMap ()
{
    int i, j;
    int red, green, blue;
    int red_size, green_size, blue_size;
    int red_shift, green_shift, blue_shift;
    unsigned long grCompleteMask;

    /* grCompleteMask is a mask of all the planes not used by this 
     * technology.  It is OR'd in with the mask that magic supplies
     * to ensure that unused bits of the pixel are cleared.
     */

#ifdef HIRESDB
    TxPrintf("planeCount %d, realColors %d\n", grDisplay.planeCount,
		grDisplay.realColors);
#endif  /* HIRESDB */

    if (grDisplay.planeCount > 8) {
        grCompleteMask = 0;
	if (grDisplay.planeCount == 16)
	{
	    red_size = 5;
	    green_size = 6;
	    blue_size = 5;
	}
        else if (grDisplay.planeCount == 15)
	{
	    red_size = 5;
	    green_size = 5;
	    blue_size = 5;
	} else {
	    red_size = 8;
	    green_size = 8;
	    blue_size = 8;
	}
        red_shift = green_size + blue_size;
        green_shift = blue_size;
        if ((grDisplay.planeCount == 24) && (grDisplay.red_mask == 0xff))
	{
	    /* this is SUN Solaris doing it backwards: BGR  */
            red_shift = 0; green_shift = red_size;
            blue_shift = green_size + red_size;
	}

	/* DANGER! Modify the code below with care: gcc 2.7.2 generates bad */
	/* code if the masks are not used as below.			    */

        for (i = 0; i != grDisplay.colorCount; i++)
	{
	    if (!GrGetColor(i, &red, &green, &blue)) break;

	    if ((grDisplay.planeCount == 16) || (grDisplay.planeCount ==15))
	    {
		grPixels[i] = (long)((red >> (8 - red_size))
			<< (green_size + blue_size)) & grDisplay.red_mask; /* red */
		grPixels[i] |= (long)((green >> (8 - green_size))
			<< blue_size) & grDisplay.green_mask;		   /* green */
		grPixels[i] |= (long)(blue >> (8 - blue_size))
			& grDisplay.blue_mask; 				   /* blue */
	    }
	    else if ((grDisplay.planeCount == 24) && (grDisplay.red_mask == 0xff))
	    {
		/* this is SUN Solaris doing it backwards: BGR  */
		grPixels[i] = (long)(red & grDisplay.red_mask);
		/* here is where gcc really goes wrong (sign extends) */
		grPixels[i] |= (long)((green << green_shift) & grDisplay.green_mask);
		grPixels[i] |= (long)((blue << blue_shift) & grDisplay.blue_mask);
	    }
	    else {
		grPixels[i] =  (long)((red << red_shift) & grDisplay.red_mask);
		grPixels[i] |= (long)((green << green_shift) & grDisplay.green_mask);
		grPixels[i] |= (long)(blue & grDisplay.blue_mask);
	    }
	}

#ifdef HIRESDB
	TxPrintf("grPixels: %6x %6x %6x %6x\n", grPixels[0], grPixels[1],
		grPixels[2], grPixels[3]);
#endif  /* HIRESDB */

	for (i = 0; i < grDisplay.planeCount; i++)
	{
	    grDisplay.planes[i] = 1 << i;
	    grPlanes[i] = 0;
	    for (j = 0; j != grDisplay.planeCount; j++)
		if (i & (1 << j))
		{
		    grPlanes[i] |= grDisplay.planes[j];
		}
        }
    }
    else { 	/* grDisplay.planeCount <= 8 */

	if (grDisplay.planeCount < 0)
	{
	    TxError("number of bit planes must be 0 to 8 in this display.");
	    GrX11Close();
	    MainExit(1);
	}

	grCompleteMask = 0;
	for (i = 0; i < grDisplay.planeCount; i++)
	    grCompleteMask |= grDisplay.planes[i];

	grCompleteMask = AllPlanes & ~grCompleteMask;

	for (i = 0; i != grDisplay.colorCount; i++)
	{
	    grPixels[i] = grDisplay.basepixel;
	    grPlanes[i] = grCompleteMask;
	    for (j = 0; j < grDisplay.planeCount; j++)
		if (i & (1 << j))
		{
		    grPixels[i] |= grDisplay.planes[j];
		    grPlanes[i] |= grDisplay.planes[j];
		} 
	}
    }

    if (grDisplay.depth)
    {
	for (i = 0; i < grDisplay.realColors; i++) 
	{
	    if (!GrGetColor(i, &red, &green, &blue)) break;
	    colors[i].pixel = grPixels[i];
	    colors[i].red = (unsigned short)(red << 8);
	    colors[i].green = (unsigned short)(green << 8);
	    colors[i].blue = (unsigned short)(blue << 8);
	    colors[i].flags = DoRed | DoGreen | DoBlue;
	}
	if (grDisplay.planeCount <= 8)
	{
#ifdef HIRESDB
	    TxPrintf("XStoreColors: planeCount %d, realColors %d\n",
			grDisplay.planeCount, grDisplay.realColors);
#endif  /* HIRESDB */
	    XStoreColors(grXdpy, grXcmap, colors, grDisplay.realColors);
	}
    }
    else
    {
	grPixels[0] = WhitePixel(grXdpy,grXscrn);
	grPixels[1] = BlackPixel(grXdpy,grXscrn);
	grPlanes[0] = 0;
	grPlanes[1] = AllPlanes;
    }
}

XSegment grx11Lines[X11_BATCH_SIZE];
int grx11NbLines=0;
XRectangle grx11Rects[X11_BATCH_SIZE];
int grx11NbRects=0;


/*---------------------------------------------------------
 * grxDrawLines:
 *	This routine draws a batch of lines.
 *
 * Results:	None.
 *
 * Side Effects:
 *	Draw a bunch of lines.
 *---------------------------------------------------------
 */

void
grx11DrawLines(lines, nb)
    XSegment lines[];
    int nb;
{
    XDrawSegments(grXdpy, grCurrent.window, grGCDraw,
	      lines, nb);
}

/*---------------------------------------------------------
 * grxDrawLine:
 *	This routine draws a line.
 *
 * Results:	None.
 *
 * Side Effects:
 *	Draw a line for (x1, y1) to (x2, y2) inclusive.
 *---------------------------------------------------------
 */

void
grx11DrawLine (x1, y1, x2, y2)
    int x1, y1;			/* Screen coordinates of first point. */
    int x2, y2;			/* Screen coordinates of second point. */
{
    if (grx11NbLines == X11_BATCH_SIZE) GR_X_FLUSH_LINES();
    grx11Lines[grx11NbLines].x1 = x1;
    grx11Lines[grx11NbLines].y1 = grMagicToX(y1);
    grx11Lines[grx11NbLines].x2 = x2;
    grx11Lines[grx11NbLines].y2 = grMagicToX(y2);
    grx11NbLines++;
}


/*---------------------------------------------------------
 * grxFillRects:
 *	This routine draws a bunch of solid rectangles.
 *
 * Results:	None.
 *
 * Side Effects:
 *	Drawing.
 *---------------------------------------------------------
 */

void
grx11FillRects(rects, nb)
    XRectangle rects[];
    int nb;
{
    XFillRectangles(grXdpy, grCurrent.window, grGCFill, rects, nb);
}


/*---------------------------------------------------------
 * grxFillRect:
 *	This routine draws a solid rectangle.
 *
 * Results:	None.
 *
 * Side Effects:
 *	Drawing.
 *---------------------------------------------------------
 */

void
grx11FillRect(r)
    Rect *r;	/* Address of a rectangle in screen
			 * coordinates.
			 */
{
    if (grx11NbRects == X11_BATCH_SIZE) GR_X_FLUSH_RECTS();
    grx11Rects[grx11NbRects].x = r->r_xbot;
    grx11Rects[grx11NbRects].y = grMagicToX(r->r_ytop);
    grx11Rects[grx11NbRects].width = r->r_xtop - r->r_xbot + 1;
    grx11Rects[grx11NbRects].height = r->r_ytop - r->r_ybot + 1;
    grx11NbRects++;
}

/*---------------------------------------------------------
 * grx11FillPolygon:
 *	This routine draws a solid polygon
 *
 * Results:     None.
 *
 * Side Effects:
 *	Drawing.
 *---------------------------------------------------------
 */

void
grx11FillPolygon(tp, np)
    Point *tp;
    int np;
{
    XPoint xp[5];
    int i;

    for (i = 0; i < np; i++)
    {
	xp[i].x = tp[i].p_x;
	xp[i].y = grMagicToX(tp[i].p_y);
    }
    XFillPolygon(grXdpy, grCurrent.window, grGCFill, xp, np,
		Convex, CoordModeOrigin);
}

