/* grTk3.c -
 *
 * Copyright 2003 Open Circuit Design, Inc., for MultiGiG Ltd.
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

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "graphics/graphicsInt.h"
#include "textio/textio.h"
#include "utils/signals.h"
#include "utils/utils.h"
#include "utils/hash.h"
#include "utils/malloc.h"
#include "graphics/grTkInt.h"
#include "graphics/grTkCommon.h"
#include "database/fonts.h"


/*---------------------------------------------------------
 * grtkDrawGrid:
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
grtkDrawGrid (prect, outline, clip)
    Rect *prect;		/* A rectangle that forms the template
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
    
    grtkSetLineStyle(outline);

    snum = 0;
    low = grMagicToX(clip->r_ybot);
    hi = grMagicToX(clip->r_ytop);
    for (x = xstart; x < (clip->r_xtop + 1) << SUBPIXELBITS; x += xsize)
    {
        if (snum == GR_NUM_GRIDS)
	{
	    XDrawSegments(grXdpy, grCurrent.windowid, grGCDraw, seg, snum);
	    snum = 0;
	}
	shifted = x >> SUBPIXELBITS;
	seg[snum].x1 = shifted;
	seg[snum].y1 = low;
	seg[snum].x2 = shifted;
	seg[snum].y2 = hi;
	snum++;
    }
    XDrawSegments(grXdpy, grCurrent.windowid, grGCDraw, seg, snum);

    snum = 0;
    low = clip->r_xbot;
    hi = clip->r_xtop;
    for (y = ystart; y < (clip->r_ytop + 1) << SUBPIXELBITS; y += ysize)
    {
        if (snum == GR_NUM_GRIDS)
	{
	    XDrawSegments(grXdpy, grCurrent.windowid, grGCDraw, seg, snum);
	    snum = 0;
	}
	shifted = grMagicToX(y >> SUBPIXELBITS);
	seg[snum].x1 = low;
	seg[snum].y1 = shifted;
	seg[snum].x2 = hi;
	seg[snum].y2 = shifted;
	snum++;
    }
    XDrawSegments(grXdpy, grCurrent.windowid, grGCDraw, seg, snum);
    return TRUE;
}


/*---------------------------------------------------------
 * grtkSetCharSize:
 *	This local routine sets the character size in the display,
 *	if necessary.
 *
 * Results:	None.
 *
 * Side Effects:    None.
 *---------------------------------------------------------
 */

void
grtkSetCharSize (size)
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
	    TxError("%s%d\n", "grtkSetCharSize: Unknown character size ",
		size );
	    break;
    }
}


/*
 * ----------------------------------------------------------------------------
 * GrTkTextSize --
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
GrTkTextSize(text, size, r)
    char *text;
    int size;
    Rect *r;
{
    Tk_FontMetrics overall;
    Tk_Font font;
    int width;
    
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
	TxError("%s%d\n", "GrTkTextSize: Unknown character size ",
		size );
	break;
    }
    if (font == NULL) return;
    Tk_GetFontMetrics(font, &overall);
    width = Tk_TextWidth(font, text, strlen(text));
    r->r_ytop = overall.ascent;
    r->r_ybot = -overall.descent;
    r->r_xtop = width;
    r->r_xbot = 0;
}


/*
 * ----------------------------------------------------------------------------
 * GrTkReadPixel --
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
GrTkReadPixel (w, x, y)
    MagWindow *w;
    int x,y;		/* the location of a pixel in screen coords */
{
    XImage *image;
    unsigned long value;
    XWindowAttributes	att;

    XGetWindowAttributes(grXdpy, grCurrent.windowid, &att);
    if ( x < 0 || x >= att.width || grMagicToX(y) < 0 || grMagicToX(y) >= att.height) return(0);
    image = XGetImage(grXdpy, grCurrent.windowid, x, grMagicToX(y), 1, 1,
		      ~0, ZPixmap);
    value = XGetPixel(image, 0, 0);
    return (value & (1 << grDisplay.depth) - 1);
}


/*
 * ----------------------------------------------------------------------------
 * GrTkBitBlt --
 *
 *	Copy information from one part of the screen to the other.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	changes the screen.
 * ----------------------------------------------------------------------------
 */

void
GrTkBitBlt(r, p)
    Rect *r;
    Point *p;
{
    Window wind = grCurrent.windowid;

    XCopyArea(grXdpy, wind, wind, grGCCopy,
	      r->r_xbot, grMagicToX(r->r_ytop),
	      r->r_xtop - r->r_xbot + 1, r->r_ytop - r->r_ybot + 1,
	      p->p_x, grMagicToX(p->p_y));
}



/*
 * ----------------------------------------------------------------------------
 * grtkRectConvert --
 *	Convert a magic rectangle into an X11 rectangle
 *	(Both passed as pointers).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Coordinate-converted rectangle values placed in XRectangle pointer
 *	xr.
 * ----------------------------------------------------------------------------
 */

void
grtkRectConvert(mr, xr)
    Rect *mr;			/* source rectangle pointer */
    XRectangle *xr;		/* destination rectangle pointer */
{
	xr->x = mr->r_xbot;
	xr->y = grMagicToX(mr->r_ytop);
	xr->width = mr->r_xtop - mr->r_xbot + 1;
	xr->height = mr->r_ytop - mr->r_ybot + 1;
}

/*
 *---------------------------------------------------------
 * grtkFontText:
 *
 *	This routine is a fancier version of grtkPutText used for
 *	drawing vector outline fonts from the fontList records.
 *
 *---------------------------------------------------------
 */

void
grtkFontText(text, font, size, rotate, pos, clip, obscure)
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

	pxm = XCreatePixmap(grXdpy, grCurrent.windowid, w, h, 1);

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
	XFillRectangle(grXdpy, grCurrent.windowid, grGCText, locoffset.p_x,
		locoffset.p_y, w, h);

	lpos.p_x += coffset->p_x;
	lpos.p_y += coffset->p_y;

        XFreePixmap(grXdpy, pxm);
    }
}

/*
 *---------------------------------------------------------
 * grtkPutText:
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
grtkPutText (text, pos, clip, obscure)
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
    void grtkGeoSub();

    if (grCurrent.font == NULL) return;

    GrTkTextSize(text, grCurrent.fontSize, &textrect);

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
	    grtkGeoSub(&location, &overlap);
	}
    }
 
    overlap = location;
    GeoClip(&overlap, clip);

    /* copy the text to the color screen */
    if ((overlap.r_xbot < overlap.r_xtop)&&(overlap.r_ybot <= overlap.r_ytop))
    {
	XRectangle xr;

	grtkRectConvert(&overlap, &xr);
	XSetClipRectangles(grXdpy, grGCText, 0, 0, &xr, 1, Unsorted);

	XSetFont(grXdpy, grGCText, Tk_FontId(grCurrent.font));
	Tk_DrawChars(grXdpy, grCurrent.windowid, grGCText,
		    grCurrent.font, text, strlen(text),
		    pos->p_x, grMagicToX(pos->p_y));
    }
}


/*
 *---------------------------------------------------------
 * grtkGeoSub:
 *	Return the tallest sub-rectangle of r not obscured
 *	by area "area" must be within r.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Source rectangle "r" is modified.
 *		
 *---------------------------------------------------------
 */

void
grtkGeoSub(r, area)
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
