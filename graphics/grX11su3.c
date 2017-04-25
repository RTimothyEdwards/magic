/* grX11su3.c -
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
 * This file contains additional functions to manipulate an X window system
 * color display.  Included here are device-dependent routines to draw and 
 * erase text and draw a grid.
 *
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <X11/Xlib.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "graphics/graphics.h"
#include "windows/windows.h"
#include "graphics/graphicsInt.h"
#include "textio/textio.h"
#include "utils/signals.h"
#include "utils/utils.h"
#include "utils/hash.h"
#include "dbwind/dbwind.h"
#include "database/fonts.h"
#include "grX11Int.h"

/* locals */

static XFontStruct *grXFonts[4];
#define	grSmallFont	grXFonts[0]
#define	grMediumFont	grXFonts[1]
#define	grLargeFont	grXFonts[2]
#define	grXLargeFont	grXFonts[3]



/*---------------------------------------------------------
 * grxDrawGrid:
 *	grxDrawGrid adds a grid to the grid layer, using the current
 *	write mask and color.
 *
 * Results:
 *	TRUE is returned normally.  However, if the grid gets too small
 *	to be useful, then nothing is drawn and FALSE is returned.
 *
 * Side Effects:    None.
 *---------------------------------------------------------
 */

#define GR_NUM_GRIDS 64

bool
grx11DrawGrid (prect, outline, clip)
    Rect *prect;			/* A rectangle that forms the template
			         * for the grid.  Note:  in order to maintain
			         * precision for the grid, the rectangle
			         * coordinates are specified in units of
			         * screen coordinates multiplied by SUBPIXEL.
			         */
    int outline;		/* the outline style */
    Rect *clip;			/* a clipping rectangle */
{
    int xsize, ysize;
    int x, y;
    int xstart, ystart;
    XSegment seg[GR_NUM_GRIDS];
    int snum, low, hi, shifted;

    xsize = prect->r_xtop - prect->r_xbot;
    ysize = prect->r_ytop - prect->r_ybot;
    if (!xsize || !ysize || GRID_TOO_SMALL(xsize, ysize))
	return FALSE;

    xstart = prect->r_xbot % xsize;
    while (xstart < clip->r_xbot << SUBPIXELBITS) xstart += xsize;
    ystart = prect->r_ybot % ysize;
    while (ystart < clip->r_ybot << SUBPIXELBITS) ystart += ysize;
    
    grx11SetLineStyle(outline);

    snum = 0;
    low = grMagicToX(clip->r_ybot);
    hi = grMagicToX(clip->r_ytop);
    for (x = xstart; x < (clip->r_xtop + 1) << SUBPIXELBITS; x += xsize)
    {
        if (snum == GR_NUM_GRIDS)
	{
	    XDrawSegments(grXdpy, grCurrent.window, grGCDraw, seg, snum);
	    snum = 0;
	}
	shifted = x >> SUBPIXELBITS;
	seg[snum].x1 = shifted;
	seg[snum].y1 = low;
	seg[snum].x2 = shifted;
	seg[snum].y2 = hi;
	snum++;
    }
    XDrawSegments(grXdpy, grCurrent.window, grGCDraw, seg, snum);

    snum = 0;
    low = clip->r_xbot;
    hi = clip->r_xtop;
    for (y = ystart; y < (clip->r_ytop + 1) << SUBPIXELBITS; y += ysize)
    {
        if (snum == GR_NUM_GRIDS)
	{
	    XDrawSegments(grXdpy, grCurrent.window, grGCDraw, seg, snum);
	    snum = 0;
	}
	shifted = grMagicToX(y >> SUBPIXELBITS);
	seg[snum].x1 = low;
	seg[snum].y1 = shifted;
	seg[snum].x2 = hi;
	seg[snum].y2 = shifted;
	snum++;
    }
    XDrawSegments(grXdpy, grCurrent.window, grGCDraw, seg, snum);
    return TRUE;
}


/*---------------------------------------------------------
 * grxLoadFont
 *	This local routine loads the X fonts used by Magic.
 *
 * Results:	Success/failure
 *
 * Side Effects:    None.
 *---------------------------------------------------------
 */

bool
grx11LoadFont()
{
    static char *fontnames[4] = {
      X_FONT_SMALL,
      X_FONT_MEDIUM,
      X_FONT_LARGE,
      X_FONT_XLARGE };
    static char *optionnames[4] = {
      "small",
      "medium",
      "large",
      "xlarge"};

    int i;
    char *unable = "Unable to load font";

    for (i=0; i!= 4; i++)
    {
    	 char	*s = XGetDefault(grXdpy,"magic",optionnames[i]);
	 if (s) fontnames[i] = s;
         if ((grXFonts[i] = XLoadQueryFont(grXdpy, fontnames[i])) == NULL) 
         {
	      TxError("%s %s\n",unable,fontnames[i]);
              if ((grXFonts[i]= XLoadQueryFont(grXdpy,GR_DEFAULT_FONT))==NULL) 
	      {
	           TxError("%s %s\n",unable,GR_DEFAULT_FONT);
		   return FALSE;
	      }
         }
    }
    return TRUE;
}


/*---------------------------------------------------------
 * grxSetCharSize:
 *	This local routine sets the character size in the display,
 *	if necessary.
 *
 * Results:	None.
 *
 * Side Effects:    None.
 *---------------------------------------------------------
 */

void
grx11SetCharSize (size)
    int size;		/* Width of characters, in pixels (6 or 8). */
{
    grCurrent.fontSize = size;
    switch (size)
    {
	case GR_TEXT_DEFAULT:
	case GR_TEXT_SMALL:
	    grCurrent.font = grSmallFont;
	    break;
	case GR_TEXT_MEDIUM:
	    grCurrent.font = grMediumFont;
	    break;
	case GR_TEXT_LARGE:
	    grCurrent.font = grLargeFont;
	    break;
	case GR_TEXT_XLARGE:
	    grCurrent.font = grXLargeFont;
	    break;
	default:
	    TxError("%s%d\n", "grx11SetCharSize: Unknown character size ",
		size );
	    break;
    }
}


/*
 * ----------------------------------------------------------------------------
 * GrXTextSize --
 *
 *	Determine the size of a text string. 
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A rectangle is filled in that is the size of the text in pixels.
 *	The origin (0, 0) of this rectangle is located on the baseline
 *	at the far left side of the string.
 * ----------------------------------------------------------------------------
 */

void
GrX11TextSize(text, size, r)
    char *text;
    int size;
    Rect *r;
{
    XCharStruct overall;
    XFontStruct *font;
    int dir,fa,fd;
    
    switch (size) {
    case GR_TEXT_DEFAULT:
    case GR_TEXT_SMALL:
	font = grSmallFont;
	break;
    case GR_TEXT_MEDIUM:
	font = grMediumFont;
	break;
    case GR_TEXT_LARGE:
	font = grLargeFont;
	break;
    case GR_TEXT_XLARGE:
	font = grXLargeFont;
	break;
    default:
	TxError("%s%d\n", "GrX11TextSize: Unknown character size ",
		size );
	break;
    }
    if (font == NULL) return;
    XTextExtents(font, text, strlen(text), &dir, &fa, &fd, &overall);
    r->r_ytop = overall.ascent;
    r->r_ybot = -overall.descent;
    r->r_xtop = overall.width - overall.lbearing;
    r->r_xbot = -overall.lbearing - 1;
}


/*
 * ----------------------------------------------------------------------------
 * GrXReadPixel --
 *
 *	Read one pixel from the screen.
 *
 * Results:
 *	An integer containing the pixel's color.
 *
 * Side effects:
 *	none.
 *
 * ----------------------------------------------------------------------------
 */

int
GrX11ReadPixel (w, x, y)
    MagWindow *w;
    int x,y;		/* the location of a pixel in screen coords */
{
    XImage *image;
    unsigned long value;
    XWindowAttributes	att;

    XGetWindowAttributes(grXdpy,grCurrent.window, &att);
    if ( x < 0 || x >= att.width || grMagicToX(y) < 0
		|| grMagicToX(y) >= att.height)
	return(0);
    image = XGetImage(grXdpy, grCurrent.window, x, grMagicToX(y), 1, 1,
		      ~0, ZPixmap);
    value = XGetPixel(image, 0, 0);
    return (value & (1 << grDisplay.depth) - 1);
}


/*
 * ----------------------------------------------------------------------------
 * GrXBitBlt --
 *
 *	Copy information in bit block transfers.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	changes the screen.
 * ----------------------------------------------------------------------------
 */

void
GrX11BitBlt(r, p)
    Rect *r;
    Point *p;
{
    Drawable wind = (Drawable)grCurrent.window;

    XCopyArea(grXdpy, wind, wind, grGCCopy,
	      r->r_xbot, grMagicToX(r->r_ytop),
	      r->r_xtop - r->r_xbot + 1, r->r_ytop - r->r_ybot + 1,
	      p->p_x, grMagicToX(p->p_y));
}

static GC grXcopyGC = (GC)NULL;

/*
 * ----------------------------------------------------------------------------
 * grx11FreeBackingStore --
 *	Free up Pixmap memory for a backing store cell.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	memory Free'd
 * ----------------------------------------------------------------------------
 */

void
grx11FreeBackingStore(MagWindow *window)
{
    Pixmap pmap = (Pixmap)window->w_backingStore;
    if (pmap == (Pixmap)NULL) return;
    XFreePixmap(grXdpy, pmap);
    window->w_backingStore = (ClientData)NULL;
    /* XFreeGC(grXdpy, grXcopyGC); */
    /* TxPrintf("grx11FreeBackingStore called\n"); */
}

/*
 * ----------------------------------------------------------------------------
 * grx11CreateBackingStore --
 *	Create Pixmap memory for a backing store cell and copy data
 *	from the window into it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	memory Allocated.
 * 
 * ----------------------------------------------------------------------------
 */

void
grx11CreateBackingStore(MagWindow *w)
{
    Pixmap pmap;
    Window wind = (Window)w->w_grdata;
    unsigned int width, height;
    GC gc;
    XGCValues gcValues;
    int grDepth;

    /* ignore for all windows except layout */
    if (w->w_client != DBWclientID) return;

    /* deferred */
    if (w->w_grdata == (Window)NULL) return;
	
    width = w->w_screenArea.r_xtop - w->w_screenArea.r_xbot;
    height = w->w_screenArea.r_ytop - w->w_screenArea.r_ybot;

    if (w->w_backingStore != (ClientData)NULL) grx11FreeBackingStore(w);

    if (grXcopyGC == (GC)NULL)
    {
	gcValues.graphics_exposures = FALSE;
	grXcopyGC = XCreateGC(grXdpy, wind, GCGraphicsExposures, &gcValues);
    }

    grDepth = grDisplay.depth;
    if(grClass == 3) grDepth = 8;  /* Needed since grDisplay.depth is reset
				     to 7 if Pseudocolor      */

    pmap = XCreatePixmap(grXdpy, wind, width, height, grDepth);
    w->w_backingStore = (ClientData)pmap;

    /* TxPrintf("grx11CreateBackingStore area %d %d %d %d\n",
	w->w_screenArea.r_xbot, w->w_screenArea.r_ybot,
	w->w_screenArea.r_xtop, w->w_screenArea.r_ytop); */
}

/*
 * ----------------------------------------------------------------------------
 * grx11GetBackingStore --
 *	Copy data from a backing store Pixmap into the indicated window.
 *
 * Results:
 *	TRUE if backing store was copied successfully, FALSE if not.
 *
 * Side effects:
 *	Data copied into Pixmap memory.
 * 
 * ----------------------------------------------------------------------------
 */

bool
grx11GetBackingStore(MagWindow *w, Rect *area)
{
    Pixmap pmap;
    Window wind = (Window)w->w_grdata;
    unsigned int width, height;
    int ybot;
    int xoff, yoff;
    Rect r;

    pmap = (Pixmap)w->w_backingStore;
    if (pmap == (Pixmap)NULL)
	return FALSE;

    /* Make a local copy of area so we don't disturb the original */
    r = *area;
    GeoClip(&r, &(w->w_screenArea));

    width = r.r_xtop - r.r_xbot;
    height = r.r_ytop - r.r_ybot;
    ybot = grMagicToX(r.r_ytop);

    xoff = w->w_screenArea.r_xbot - w->w_allArea.r_xbot;
    yoff = w->w_allArea.r_ytop - w->w_screenArea.r_ytop;

    XCopyArea(grXdpy, pmap, wind, grXcopyGC, r.r_xbot - xoff, ybot - yoff,
		width, height, r.r_xbot, ybot);

    /* TxPrintf("grx11GetBackingStore %d %d %d %d\n",
		r.r_xbot, r.r_ybot, r.r_xtop, r.r_ytop); */
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 * grx11ScrollBackingStore --
 *	Enable fast scrolling by shifting part of the backing store
 *	from one position to another, with the amount of shift indicated
 *	by the X and/or Y value of the indicated point.
 *
 * Results:
 *	TRUE on success, FALSE on failure.
 *
 * Side effects:
 *	Data shifted in Pixmap memory.
 * 
 * ----------------------------------------------------------------------------
 */

bool
grx11ScrollBackingStore(MagWindow *w, Point *shift)
{
    Pixmap pmap;
    unsigned int width, height;
    int xorigin, yorigin, xshift, yshift;

    pmap = (Pixmap)w->w_backingStore;
    if (pmap == (Pixmap)NULL)
    {
	TxPrintf("grx11ScrollBackingStore %d %d failure\n",
		shift->p_x, shift->p_y);
	return FALSE;
    }

    width = w->w_screenArea.r_xtop - w->w_screenArea.r_xbot;
    height = w->w_screenArea.r_ytop - w->w_screenArea.r_ybot;
    xorigin = 0;
    yorigin = 0;
    xshift = shift->p_x;
    yshift = -shift->p_y;

    if (xshift > 0)
	width -= xshift;
    else if (xshift < 0)
    {
	width += xshift;
	xorigin = -xshift;
	xshift = 0;
    }
    if (yshift > 0)
	height -= yshift;
    else if (yshift < 0)
    {
	height += yshift;
	yorigin = -yshift;
	yshift = 0;
    }

    XCopyArea(grXdpy, pmap, pmap, grXcopyGC, xorigin, yorigin, width, height,
		xshift, yshift);

    /* TxPrintf("grx11ScrollBackingStore %d %d\n", shift->p_x, shift->p_y); */
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 * grx11PutBackingStore --
 *	Copy data from the window into backing store.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Graphics drawing into the window.
 * ----------------------------------------------------------------------------
 */

void
grx11PutBackingStore(MagWindow *w, Rect *area)
{
    Pixmap pmap = (Pixmap)w->w_backingStore;
    Window wind = (Window)w->w_grdata;
    unsigned int width, height;
    int ybot, xoff, yoff;

    if (pmap == (Pixmap)NULL) return;

    /* Attempting to write backing store into an obscured	*/
    /* window immediately invalidates everything in backing	*/
    /* store.  This is extreme, but is much simpler and under	*/
    /* normal conditions faster than tracking all obscured	*/
    /* areas separately.					*/

    if (w->w_flags & WIND_OBSCURED)
    {
	grx11FreeBackingStore(w);
	w->w_backingStore = (ClientData)NULL;
	return;
    }

    width = area->r_xtop - area->r_xbot;
    height = area->r_ytop - area->r_ybot;
    ybot = grMagicToX(area->r_ytop);

    xoff = w->w_screenArea.r_xbot - w->w_allArea.r_xbot;
    yoff = w->w_allArea.r_ytop - w->w_screenArea.r_ytop;

    XCopyArea(grXdpy, wind, pmap, grXcopyGC, area->r_xbot, ybot,
		width, height, area->r_xbot - xoff, ybot - yoff);

    /* TxPrintf("grx11PutBackingStore %d %d %d %d\n",
		area->r_xbot, area->r_ybot, area->r_xtop, area->r_ytop); */
}

/*
 * ----------------------------------------------------------------------------
 * GrX11RectConvert --
 *	Convert a magic rectangle into an X11 rectangle
 *	(Both passed as pointers)
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Converted value returned in xr.
 * ----------------------------------------------------------------------------
 */

void
grx11RectConvert(mr, xr)
    Rect *mr;
    XRectangle *xr;
{
	xr->x = mr->r_xbot;
	xr->y = grMagicToX(mr->r_ytop);
	xr->width = mr->r_xtop - mr->r_xbot + 1;
	xr->height = mr->r_ytop - mr->r_ybot + 1;
}

/*
 *---------------------------------------------------------
 * grx11FontText:
 *
 *	This routine is a fancier version of grx11PutText used for
 *	drawing vector outline fonts from the fontList records.
 *
 *---------------------------------------------------------
 */

void
grx11FontText(text, font, size, rotate, pos, clip, obscure)
    char *text;
    int font;
    int size;			/* pixel size of the text */
    int rotate;			/* text rotation */
    Point *pos;			/* text base position */
    Rect *clip;
    LinkedRect *obscure;
{
    char *tptr;
    FontChar *ccur, *clist;
    Point *coffset, *tp, loffset, locoffset, corners[4], lpos;
    Rect *cbbox, charbbox, *frect;
    int np, i, j, w, h, llx, lly, baseline;
    XPoint *xp;
    Pixmap pxm;
    double fscale, scx, scy, tmpx, tmpy, rrad, cr, sr;
    static GC fontgc = (GC)NULL;

    frect = &DBFontList[font]->mf_extents;
    fscale = (double)size / (double)frect->r_ytop;
    rrad = (double)rotate * 0.0174532925;
    cr = cos(rrad);
    sr = sin(rrad);
    lpos = GeoOrigin;

    /* 1st pass: find the descent of the string */

    baseline = 0;
    for (tptr = text; *tptr != '\0'; tptr++)
    {
        DBFontChar(font, *tptr, NULL, NULL, &cbbox);
	if (cbbox->r_ybot < -baseline)
	    baseline = -cbbox->r_ybot;
    }
    baseline = (int)((double)baseline * fscale);

    for (tptr = text; *tptr != '\0'; tptr++)
    {
	scx = (double)lpos.p_x * fscale;
	scy = (double)lpos.p_y * fscale;

	tmpx = scx * cr + scy * sr;
	tmpy = scy * cr - scx * sr;

	loffset.p_x = pos->p_x + (int)round(tmpx);
	loffset.p_y = grMagicToX(pos->p_y + baseline) + (int)round(tmpy);

        DBFontChar(font, *tptr, &clist, &coffset, &cbbox);
	np = 0;
	for (ccur = clist; ccur != NULL; ccur = ccur->fc_next)
	    np += ccur->fc_numpoints;

	xp = (XPoint *)mallocMagic(np * sizeof(XPoint));

	j = 0;
	for (ccur = clist; ccur != NULL; ccur = ccur->fc_next)
	{
	    tp = ccur->fc_points;
	    for (i = 0; i < ccur->fc_numpoints; i++, j++)
	    {
		scx = (double)tp[i].p_x * fscale;
		scy = (double)tp[i].p_y * fscale;

		tmpx = scx * cr - scy * sr;
		tmpy = scx * sr + scy * cr;

		xp[j].x = (int)round(tmpx);
		xp[j].y = (int)round(tmpy);

		/* Initialize bounding box */
		if (j == 0)
		{
		    charbbox.r_xbot = charbbox.r_xtop = xp[j].x;
		    charbbox.r_ybot = charbbox.r_ytop = xp[j].y;
		}
		else
		{
		    if (xp[j].x < charbbox.r_xbot)
			charbbox.r_xbot = xp[j].x;
		    else if (xp[j].x > charbbox.r_xtop)
			charbbox.r_xtop = xp[j].x;
		    if (xp[j].y < charbbox.r_ybot)
			charbbox.r_ybot = xp[j].y;
		    else if (xp[j].y > charbbox.r_ytop)
			charbbox.r_ytop = xp[j].y;
		}
	    }
	}

	/* Create a bitmap */

	w = charbbox.r_xtop - charbbox.r_xbot + 1;
	h = charbbox.r_ytop - charbbox.r_ybot + 1;

	/* Adjust all points to the bounding box origin, and invert Y */
	for (j = 0; j < np; j++)
	{
	    xp[j].x -= charbbox.r_xbot;
	    xp[j].y = charbbox.r_ytop - xp[j].y;
	}

	pxm = XCreatePixmap(grXdpy, grCurrent.window, w, h, 1);

	if (fontgc == (GC)NULL)
	{
	    XGCValues values;
	    values.foreground = 0;
	    values.background = 0;
	    fontgc = XCreateGC(grXdpy, pxm, GCForeground | GCBackground, &values);
	}

	locoffset.p_x = loffset.p_x + charbbox.r_xbot;
	locoffset.p_y = loffset.p_y - charbbox.r_ytop;

	XSetForeground(grXdpy, fontgc, 0);
	XSetFunction(grXdpy, fontgc, GXcopy);
	XFillRectangle(grXdpy, pxm, fontgc, 0, 0, w, h);
	XSetFunction(grXdpy, fontgc, GXxor);
	XSetForeground(grXdpy, fontgc, 1);

	j = 0;
	for (ccur = clist; ccur != NULL; ccur = ccur->fc_next)
	{
	    np = ccur->fc_numpoints;
	    XFillPolygon(grXdpy, pxm, fontgc, &xp[j], np, Complex, CoordModeOrigin);
	    j += np;
	}
	freeMagic((char *)xp);

	XSetClipMask(grXdpy, grGCText, pxm);
	XSetClipOrigin(grXdpy, grGCText, locoffset.p_x, locoffset.p_y);
	XFillRectangle(grXdpy, grCurrent.window, grGCText, locoffset.p_x,
		locoffset.p_y, w, h);

	lpos.p_x += coffset->p_x;
	lpos.p_y += coffset->p_y;

        XFreePixmap(grXdpy, pxm);
    }
}

/*---------------------------------------------------------
 * grxPutText:
 *      (modified on SunPutText)
 *
 *	This routine puts a chunk of text on the screen in the current
 *	color, size, etc.  The caller must ensure that it fits on
 *	the screen -- no clipping is done except to the obscuring rectangle
 *	list and the clip rectangle.
 *
 * Results:	
 *	none.
 *
 * Side Effects:
 *	The text is drawn on the screen.  
 *
 *---------------------------------------------------------
 */

void
grx11PutText (text, pos, clip, obscure)
    char *text;			/* The text to be drawn. */
    Point *pos;			/* A point located at the leftmost point of
				 * the baseline for this string.
				 */
    Rect *clip;			/* A rectangle to clip against */
    LinkedRect *obscure;	/* A list of obscuring rectangles */

{
    Rect location;
    Rect overlap;
    Rect textrect;
    LinkedRect *ob;
    void grX11suGeoSub();

    if (grCurrent.font == NULL) return;

    GrX11TextSize(text, grCurrent.fontSize, &textrect);

    location.r_xbot = pos->p_x + textrect.r_xbot;
    location.r_xtop = pos->p_x + textrect.r_xtop;
    location.r_ybot = pos->p_y + textrect.r_ybot;
    location.r_ytop = pos->p_y + textrect.r_ytop;

    /* erase parts of the bitmap that are obscured */
    for (ob = obscure; ob != NULL; ob = ob->r_next)
    {
	if (GEO_TOUCH(&ob->r_r, &location))
	{
	    overlap = location;
	    GeoClip(&overlap, &ob->r_r);
	    grX11suGeoSub(&location, &overlap);
	}
    }
 
    overlap = location;
    GeoClip(&overlap, clip);

    /* copy the text to the color screen */
    if ((overlap.r_xbot < overlap.r_xtop)&&(overlap.r_ybot <= overlap.r_ytop))
    {
	XRectangle xr;

	XSetFont(grXdpy, grGCText, grCurrent.font->fid);
	grx11RectConvert(&overlap, &xr);
	XSetClipRectangles(grXdpy, grGCText, 0, 0, &xr, 1, Unsorted);
	XDrawString(grXdpy, grCurrent.window, grGCText,
		    pos->p_x, grMagicToX(pos->p_y),
		    text, strlen(text));
    }
}


/*
 *---------------------------------------------------------
 * grX11suGeoSub --
 *	return the tallest sub-rectangle of r not obscured by area
 *	area must be within r.
 * 
 * Results:
 *	None.
 *
 * Side effects:
 *	Source rectangle "r" is modified.
 *---------------------------------------------------------
 */

void
grX11suGeoSub(r, area)
    Rect *r;		/* Rectangle to be subtracted from. */
    Rect *area;		/* Area to be subtracted. */
{
    if (r->r_xbot == area->r_xbot) r->r_xbot = area->r_xtop;
    else
    if (r->r_xtop == area->r_xtop) r->r_xtop = area->r_xbot;
    else
    if (r->r_ybot <= area->r_ybot) r->r_ybot = area->r_ytop;
    else
    if (r->r_ytop == area->r_ytop) r->r_ytop = area->r_ybot;
    else
    r->r_xtop = area->r_xbot;
}
