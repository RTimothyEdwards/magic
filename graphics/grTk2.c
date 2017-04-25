/* grTk2.c -
 *
 * Copyright 2003 Open Circuit Design, Inc., for MultiGiG Ltd.
 *
 * This file contains additional functions to manipulate an X
 * color display.  Included here are rectangle drawing and color map
 * loading.
 */

#include <stdio.h>
#include <stdlib.h>
char *getenv();
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "graphics/glyphs.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "graphics/graphicsInt.h"
#include "grTkInt.h"

extern unsigned long grPixels[256];
extern unsigned long grPlanes[256];
extern XColor colors[256 * 3];
extern Colormap grXcmap;


/*---------------------------------------------------------------------
 * GrXSetCMap:
 *
 * Results:	None.
 *
 * Side Effects:
 *      The values in the color map are set from the default colormap
 *	table (see grCMap.c).  X color cells are allocated if this
 *	display has more than 1 plane.
 *
 *---------------------------------------------------------------------
 */

void
GrTkSetCMap()
{
    int i,j;
    int red, green, blue;
    int red_size, green_size, blue_size;
    int red_shift, green_shift, blue_shift;
    unsigned long grCompleteMask;

    /* grCompleteMask is a mask of all the planes not used by this 
     * technology.  It is OR'd in with the mask that magic supplies
     * to ensure that unused bits of the pixel are cleared.
     */

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
	}
        else {
	    red_size = 8;
	    green_size = 8;
	    blue_size = 8;
	}
	red_shift = green_size + blue_size;
	green_shift = blue_size;
	if ((grDisplay.planeCount == 24) && (grDisplay.red_mask == 0xff))
	{
	    /* this is SUN Solaris doing it backwards: BGR  */
	    red_shift = 0;
	    green_shift = red_size;
	    blue_shift = green_size + red_size;
	}

	/* DANGER! Modify the code below with care: gcc 2.7.2 generates bad */
	/* code if the masks are not used as below.			    */

	for (i = 0; i < grDisplay.colorCount; i++)
	{
	    if (!(GrGetColor(i, &red, &green, &blue))) break;

	    if ((grDisplay.planeCount == 16) || (grDisplay.planeCount ==15))
	    {
		grPixels[i] = (long)((red >> (8 - red_size))
			<< (green_size + blue_size)) & grDisplay.red_mask; /* red */
		grPixels[i] |= (long)((green >> (8 - green_size))
			<< blue_size) & grDisplay.green_mask;              /* green */
		grPixels[i] |= (long)(blue >> (8 - blue_size))
			& grDisplay.blue_mask;				   /* blue */
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
    else	/* grDisplay.planeCount <= 8 */
    { 
	grCompleteMask = 0;
	for (i=0; i < grDisplay.planeCount; i++)
	{
	    grCompleteMask |= grDisplay.planes[i];
	}
	grCompleteMask = AllPlanes & ~grCompleteMask;

	for (i = 0; i < grDisplay.colorCount; i++)
	{
	    grPixels[i] = grDisplay.basepixel;
	    grPlanes[i] = grCompleteMask;
	    for (j = 0; j != grDisplay.planeCount; j++)
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
	    if (!(GrGetColor(i, &red, &green, &blue))) break;
	    colors[i].pixel = grPixels[i];
	    colors[i].red = (unsigned short)(red << 8);
	    colors[i].green = (unsigned short)(green << 8);
	    colors[i].blue = (unsigned short)(blue << 8);
	    colors[i].flags = DoRed | DoGreen | DoBlue;
	}
	if (grDisplay.planeCount <= 8)
	{
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

XSegment grtkLines[TK_BATCH_SIZE];
int grtkNbLines=0;
XRectangle grtkRects[TK_BATCH_SIZE];
int grtkNbRects=0;


/*---------------------------------------------------------
 * grtkDrawLines:
 *	This routine draws a batch of lines.
 *
 * Results:	None.
 *
 * Side Effects:
 *	Draw a bunch of lines.
 *---------------------------------------------------------
 */

void
grtkDrawLines(lines, nb)
    XSegment lines[];
    int nb;
{
    XDrawSegments(grXdpy, grCurrent.windowid, grGCDraw,
	      lines, nb);
}


/*---------------------------------------------------------
 * grtkDrawLine:
 *	This routine draws a line.
 *
 * Results:	None.
 *
 * Side Effects:
 *	Draw a line for (x1, y1) to (x2, y2) inclusive.
 *---------------------------------------------------------
 */

void
grtkDrawLine (x1, y1, x2, y2)
    int x1, y1;			/* Screen coordinates of first point. */
    int x2, y2;			/* Screen coordinates of second point. */
{
    if (grtkNbLines == TK_BATCH_SIZE) GR_TK_FLUSH_LINES();
    grtkLines[grtkNbLines].x1 = x1;
    grtkLines[grtkNbLines].y1 = grMagicToX(y1);
    grtkLines[grtkNbLines].x2 = x2;
    grtkLines[grtkNbLines].y2 = grMagicToX(y2);

    grtkNbLines++;
}


/*---------------------------------------------------------
 * grtkFillRects:
 *	This routine draws a bunch of solid rectangles.
 *
 * Results:	None.
 *
 * Side Effects:
 *	Drawing.
 *---------------------------------------------------------
 */

void
grtkFillRects(rects, nb)
    XRectangle rects[];
    int nb;
{
    XFillRectangles(grXdpy, grCurrent.windowid, grGCFill, rects, nb);
}


/*---------------------------------------------------------
 * grtkFillRect:
 *	This routine draws a solid rectangle.
 *
 * Results:	None.
 *
 * Side Effects:
 *	Drawing.
 *---------------------------------------------------------
 */

void
grtkFillRect(r)
    Rect *r;	/* Address of a rectangle in screen
			 * coordinates.
			 */
{
    if (grtkNbRects == TK_BATCH_SIZE) GR_TK_FLUSH_RECTS();
    grtkRects[grtkNbRects].x = r->r_xbot;
    grtkRects[grtkNbRects].y = grMagicToX(r->r_ytop);
    grtkRects[grtkNbRects].width = r->r_xtop - r->r_xbot + 1;
    grtkRects[grtkNbRects].height = r->r_ytop - r->r_ybot + 1;

    grtkNbRects++;

}

/*
 *---------------------------------------------------------
 *
 * grtkFillPolygon:
 *	This routine draws a solid polygon
 *
 * Results:     None.
 *
 * Side Effects:
 *	Drawing.
 *
 *---------------------------------------------------------
 */

void
grtkFillPolygon(tp, np)
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
    XFillPolygon(grXdpy, grCurrent.windowid, grGCFill, xp, np,
		Convex, CoordModeOrigin);
}

