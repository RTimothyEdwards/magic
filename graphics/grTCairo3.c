/* grTCairo3.c -
 *
 * Copyright 2017 Open Circuit Design
 *
 * This file contains additional functions to manipulate an X window system
 * color display.  Included here are device-dependent routines to draw and
 * erase text and draw a grid.
 *
 * Written by Chuan Chen
 */

#include <stdio.h>
#include <string.h>

#include <cairo/cairo-xlib.h>

#include <math.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/malloc.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "graphics/graphicsInt.h"
#include "dbwind/dbwind.h"
#include "textio/textio.h"
#include "utils/signals.h"
#include "utils/utils.h"
#include "utils/hash.h"
#include "graphics/grTCairoInt.h"
#include "graphics/grTkCommon.h"
#include "database/fonts.h"

extern Display *grXdpy;

static GC grXcopyGC = (GC)NULL;

/* locals */


/*---------------------------------------------------------
 * grtcairoDrawGrid:
 *  grxDrawGrid adds a grid to the grid layer, using the current
 *  write mask and color.
 *
 * Results:
 *  TRUE is returned normally.  However, if the grid gets too small
 *  to be useful, then nothing is drawn and FALSE is returned.
 *
 * Side Effects:    None.
 *---------------------------------------------------------
 */

bool
grtcairoDrawGrid (prect, outline, clip)
Rect *prect;            /* A rectangle that forms the template
                     * for the grid.  Note:  in order to maintain
                     * precision for the grid, the rectangle
                     * coordinates are specified in units of
                     * screen coordinates multiplied by SUBPIXEL.
                     */
int outline;        /* the outline style */
Rect *clip;         /* a clipping rectangle */
{
	int xsize, ysize;
	int x, y;
	int xstart, ystart;
	int snum, low, hi, shifted;
	TCairoData *tcairodata = (TCairoData *)tcairoCurrent.mw->w_grdata2;

	xsize = prect->r_xtop - prect->r_xbot;
	ysize = prect->r_ytop - prect->r_ybot;
	if (!xsize || !ysize || GRID_TOO_SMALL(xsize, ysize))
		return FALSE;

	xstart = prect->r_xbot % xsize;
	while (xstart < clip->r_xbot << SUBPIXELBITS) xstart += xsize;
	ystart = prect->r_ybot % ysize;
	while (ystart < clip->r_ybot << SUBPIXELBITS) ystart += ysize;

	snum = 0;
	low = clip->r_ybot;
	hi = clip->r_ytop;
	for (x = xstart; x < (clip->r_xtop + 1) << SUBPIXELBITS; x += xsize)
	{
		shifted = x >> SUBPIXELBITS;
		cairo_move_to(tcairodata->context, (double)shifted, (double)low);
		cairo_line_to(tcairodata->context, (double)shifted, (double)hi);
		snum++;
	}

	snum = 0;
	low = clip->r_xbot;
	hi = clip->r_xtop;
	for (y = ystart; y < (clip->r_ytop + 1) << SUBPIXELBITS; y += ysize)
	{
		shifted = y >> SUBPIXELBITS;
		cairo_move_to(tcairodata->context, (double)low, (double)shifted);
		cairo_line_to(tcairodata->context, (double)hi, (double)shifted);
		snum++;
	}
	cairo_stroke(tcairodata->context);
	return TRUE;
}


/*---------------------------------------------------------
 * grtcairoLoadFont
 *  This local routine loads the default ("toy API")
 *  font for Cairo.
 *
 * Results: Success/Failure
 *
 * Side Effects:    None.
 *---------------------------------------------------------
 */

bool
grtcairoLoadFont()
{
	TCairoData *tcairodata = (TCairoData *)tcairoCurrent.mw->w_grdata2;
	cairo_select_font_face(tcairodata->context, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	return TRUE;
}


/*---------------------------------------------------------
 * grtcairoSetCharSize:
 *  This local routine sets the character size in the display,
 *  if necessary.
 *
 * Results: None.
 *
 * Side Effects:    None.
 *---------------------------------------------------------
 */

void
grtcairoSetCharSize (size)
int size;       /* Width of characters, in pixels (6 or 8). */
{
	TCairoData *tcairodata = (TCairoData *)tcairoCurrent.mw->w_grdata2;
	tcairoCurrent.fontSize = size;
	cairo_set_font_size(tcairodata->context, size * 4 + 10);
	switch (size)
	{
	case GR_TEXT_DEFAULT:
	case GR_TEXT_SMALL:
		tcairoCurrent.font = grSmallFont;
		break;
	case GR_TEXT_MEDIUM:
		tcairoCurrent.font = grMediumFont;
		break;
	case GR_TEXT_LARGE:
		tcairoCurrent.font = grLargeFont;
		break;
	case GR_TEXT_XLARGE:
		tcairoCurrent.font = grXLargeFont;
		break;
	default:
		TxError("%s%d\n", "grtcairoSetCharSize: Unknown character size ",
		        size );
		break;
	}
}


/*
 * ----------------------------------------------------------------------------
 * GrTCairoTextSize --
 *
 *  Determine the size of a text string.
 *
 * Results:
 *  Return 0 when 'r' updated, otherwise -1 on error (no side-effects).
 *
 * Side effects:
 *  A rectangle is filled in that is the size of the text in pixels.
 *  The origin (0, 0) of this rectangle is located on the baseline
 *  at the far left side of the string.
 * ----------------------------------------------------------------------------
 */

int
GrTCairoTextSize(text, size, r)
char *text;
int size;
Rect *r;
{
	TCairoData *tcairodata;
	cairo_text_extents_t extents;
	int width;

	/* Note:  size is ignored, as it is passed the current value;	*/
	/* but the font size in cairo has already been set.		*/

	if (tcairoCurrent.mw == 0) return -1;

	tcairodata = (TCairoData *)tcairoCurrent.mw->w_grdata2;
	cairo_text_extents(tcairodata->context, text, &extents);

	r->r_ytop = -extents.y_bearing;
	r->r_ybot = -(extents.height + extents.y_bearing);
	r->r_xtop = extents.width + extents.x_bearing;
	r->r_xbot = extents.x_bearing;
	return 0;
}

/* Cairo backing store functions (now removed from the X11-based ones) */

void
grtcairoFreeBackingStore(MagWindow *window)
{
	TCairoData *tcairodata;
	Pixmap pmap = (Pixmap)window->w_backingStore;
	if (pmap == (Pixmap)NULL) return;
	XFreePixmap(grXdpy, pmap);
	window->w_backingStore = (ClientData)NULL;

	tcairodata = (TCairoData *)window->w_grdata2;
	cairo_surface_destroy(tcairodata->backing_surface);
	cairo_destroy(tcairodata->backing_context);
	tcairodata->backing_surface = NULL;
	tcairodata->backing_context = NULL;
}

void
grtcairoCreateBackingStore(MagWindow *w)
{
	Pixmap pmap;
	TCairoData *tcairodata;
	Tk_Window tkwind = (Tk_Window)w->w_grdata;
	Window wind;
	unsigned int width, height;
	GC gc;
	XGCValues gcValues;
	int grDepth;

	/* Deferred */
	if (tkwind == NULL) return;

	wind = (Window)Tk_WindowId(tkwind);

	/* ignore all windows other than layout */
	if (w->w_client != DBWclientID) return;

	/* deferred */
	if (wind == (Window)NULL) return;

	width = w->w_screenArea.r_xtop - w->w_screenArea.r_xbot;
	height = w->w_screenArea.r_ytop - w->w_screenArea.r_ybot;

	if (w->w_backingStore != (ClientData)NULL) grtcairoFreeBackingStore(w);

	if (grXcopyGC == (GC)NULL)
	{
		gcValues.graphics_exposures = FALSE;
		grXcopyGC = XCreateGC(grXdpy, wind, GCGraphicsExposures, &gcValues);
	}

	grDepth = Tk_Depth((Tk_Window)w->w_grdata);

	pmap = XCreatePixmap(grXdpy, wind, width, height, grDepth);
	w->w_backingStore = (ClientData)pmap;

	tcairodata = (TCairoData *)w->w_grdata2;

	if (tcairodata->backing_surface != NULL)
	{
	    cairo_surface_destroy(tcairodata->backing_surface);
	    cairo_destroy(tcairodata->backing_context);
	}
	tcairodata->backing_surface = cairo_xlib_surface_create(grXdpy, pmap,
		DefaultVisual(grXdpy, DefaultScreen(grXdpy)), width, height);
	tcairodata->backing_context = cairo_create(tcairodata->backing_surface);

	cairo_identity_matrix(tcairodata->backing_context);
}

bool
grtcairoGetBackingStore(MagWindow *w, Rect *area)
{
	unsigned int width, height, sheight;
	int xbot, ybot;
	Rect r;
	TCairoData *tcairodata = (TCairoData *)w->w_grdata2;

	if (w->w_backingStore == (ClientData)0) return FALSE;

	GEO_EXPAND(area, 1, &r);
	GeoClip(&r, &(w->w_screenArea));

	xbot = r.r_xbot;
	ybot = r.r_ybot;
	width = r.r_xtop - xbot;
	height = r.r_ytop - ybot;
	sheight = w->w_screenArea.r_ytop - w->w_screenArea.r_ybot;

	// Fix Y orientation
	ybot = sheight - height - ybot;

	cairo_save(tcairodata->context);
	cairo_identity_matrix(tcairodata->context);
	cairo_set_source_surface(tcairodata->context, tcairodata->backing_surface,
		0.0, 0.0);
	cairo_rectangle(tcairodata->context, (double)xbot, (double)ybot,
			(double)width, (double)height);
	cairo_set_operator(tcairodata->context, CAIRO_OPERATOR_SOURCE);
	cairo_fill(tcairodata->context);
	cairo_restore(tcairodata->context);

	return TRUE;
}


bool
grtcairoScrollBackingStore(MagWindow *w, Point *shift)
{
	TCairoData *tcairodata = (TCairoData *)w->w_grdata2;
	Pixmap pmap;
	unsigned int width, height;
	int xorigin, yorigin, xshift, yshift;

	pmap = (Pixmap)w->w_backingStore;
	if (pmap == (Pixmap)NULL)
	{
		TxPrintf("grtcairoScrollBackingStore %d %d failure\n",
		         shift->p_x, shift->p_y);
		return FALSE;
	}

	width = w->w_screenArea.r_xtop - w->w_screenArea.r_xbot;
	height = w->w_screenArea.r_ytop - w->w_screenArea.r_ybot;
	xorigin = 0;
	yorigin = 0;
	xshift = shift->p_x;
	yshift = -shift->p_y;

	/* Important:  Cairo does not watch where memory overlaps exist	*/
	/* when copying and will erase memory when yshift is positive.	*/

	if (yshift > 0)
	{
	    /* Noting that the highlights will be redrawn anyway, use	*/
	    /* the main window surface as an intermediary to copy.	*/

	    Rect area;

	    cairo_save(tcairodata->context);
	    cairo_identity_matrix(tcairodata->context);
	    cairo_set_source_surface(tcairodata->context,
			tcairodata->backing_surface, (double)xshift, (double)yshift);
	    cairo_rectangle(tcairodata->context, (double)xorigin,
			(double)yorigin, (double)width, (double)height);
	    cairo_set_operator(tcairodata->context, CAIRO_OPERATOR_SOURCE);
	    cairo_fill(tcairodata->context);
	    cairo_restore(tcairodata->context);

	    area.r_xbot = 0;
	    area.r_xtop = width;
	    area.r_ybot = 0;
	    area.r_ytop = height;
	    grtcairoPutBackingStore(w, &area);
	}
	else
	{
	    cairo_save(tcairodata->backing_context);
	    cairo_set_source_surface(tcairodata->backing_context,
			tcairodata->backing_surface, (double)xshift, (double)yshift);
	    cairo_rectangle(tcairodata->backing_context, (double)xorigin,
			(double)yorigin, (double)width, (double)height);
	    cairo_set_operator(tcairodata->backing_context, CAIRO_OPERATOR_SOURCE);
	    cairo_fill(tcairodata->backing_context);
	    cairo_restore(tcairodata->backing_context);
	}
	return TRUE;
}

void
grtcairoPutBackingStore(MagWindow *w, Rect *area)
{
	unsigned int width, height, sheight;
	int ybot, xbot;
	TCairoData *tcairodata = (TCairoData *)w->w_grdata2;

	if (w->w_backingStore == (ClientData)0) return;

	if (w->w_flags & WIND_OBSCURED)
	{
	    grtcairoFreeBackingStore(w);
	    return;
	}

	xbot = area->r_xbot;
	ybot = area->r_ybot;

	width = area->r_xtop - xbot;
	height = area->r_ytop - ybot;
	sheight = w->w_screenArea.r_ytop - w->w_screenArea.r_ybot;

	// Fix Y orientation
	ybot = sheight - height - ybot;

	cairo_save(tcairodata->backing_context);
	cairo_set_source_surface(tcairodata->backing_context, tcairodata->surface,
		0.0, 0.0);
	cairo_rectangle(tcairodata->backing_context, (double)xbot, (double)ybot,
		(double)width, (double)height);
	cairo_set_operator(tcairodata->backing_context, CAIRO_OPERATOR_SOURCE);
	cairo_fill(tcairodata->backing_context);
	cairo_restore(tcairodata->backing_context);
}


/*
 * ----------------------------------------------------------------------------
 * GrTCairoReadPixel --
 *
 *  Read one pixel from the screen.
 *
 * Results:
 *  An integer containing the pixel's color.
 *
 * Side effects:
 *  none.
 *
 * ----------------------------------------------------------------------------
 */

int
GrTCairoReadPixel (w, x, y)
MagWindow *w;
int x, y;       /* the location of a pixel in screen coords */
{
	return 0;       /* (unimplemented) */
}


/*
 * ----------------------------------------------------------------------------
 * GrTCairoBitBlt --
 *
 *  Copy information in bit block transfers.
 *
 * Results:
 *  None.
 *
 * Side effects:
 *  changes the screen.
 * ----------------------------------------------------------------------------
 */

void
GrTCairoBitBlt(r, p)
Rect *r;
Point *p;
{
	// (unimplemented)
}

#ifdef VECTOR_FONTS

/*
 *----------------------------------------------------------------------
 * Draw a text character
 * This routine is similar to grtcairoFillPolygon()
 *----------------------------------------------------------------------
 */

void
grtcairoDrawCharacter(clist, tc, pixsize)
FontChar *clist;
unsigned char tc;
int pixsize;
{
	Point *tp;
	int np, nptotal;
	int i;
	static int maxnp = 0;
	FontChar *ccur;
	TCairoData *tcairodata = (TCairoData *)tcairoCurrent.mw->w_grdata2;

	if (pixsize < 5) return;    /* Label too small to be useful */

	//should we be using cairo_show_glyphs??
	for (ccur = clist; ccur != NULL; ccur = ccur->fc_next) {
		tp = ccur->fc_points;
		np = ccur->fc_numpoints;
		cairo_move_to(tcairodata->context, (double)tp[0].p_x, (double)tp[0].p_y);
		for (i = 1; i < np; i++) {
			cairo_line_to(tcairodata->context, (double)tp[i].p_x,
				(double)tp[i].p_y);
		}
		cairo_close_path(tcairodata->context);
	}
	cairo_fill(tcairodata->context);
}

/*---------------------------------------------------------
 * grtcairoFontText:
 *
 *  This routine draws text from font vectors using the
 *  font vector routines in DBlabel.c.  Text is clipped
 *  to the clipping rectangle.
 *
 *  For speed, should we be transferring the font
 *  vectors into cairo glyphs?
 *
 *---------------------------------------------------------
 */

void
grtcairoFontText(text, font, size, rotate, pos, clip, obscure)
char *text;         /* The text to be drawn */
int   font;         /* Font to use from fontList */
int   size;         /* Pixel size of the font */
int   rotate;       /* Text rotation */
Point *pos;         /* Text base position */
Rect  *clip;        /* Clipping area */
LinkedRect *obscure;    /* List of obscuring areas */
{
	char *tptr;
	Point *coffset;     /* vector to next character */
	Rect *cbbox;
	float fsize;
	FontChar *clist;
	int cheight, baseline;
	float tmp;
	TCairoData *tcairodata = (TCairoData *)tcairoCurrent.mw->w_grdata2;

	cairo_save(tcairodata->context);
	cairo_set_operator(tcairodata->context, CAIRO_OPERATOR_SOURCE);
	cairo_translate(tcairodata->context, (double)pos->p_x, (double)pos->p_y);
	// cairo_scale(tcairodata->context, 1.0, -1.0);
	cairo_rotate(tcairodata->context, ((double)rotate) / 360 * 2 * M_PI);

	/* Get label size */
	cbbox = &DBFontList[font]->mf_extents;

	fsize = (float)size / (float)cbbox->r_ytop;
	cairo_scale(tcairodata->context, (double)fsize, (double)fsize);

	/* Adjust to baseline */
	baseline = 0;
	for (tptr = text; *tptr != '\0'; tptr++)
	{
		DBFontChar(font, *tptr, NULL, NULL, &cbbox);
		if (cbbox->r_ybot < baseline)
			baseline = cbbox->r_ybot;
	}
	cairo_translate(tcairodata->context, 0.0, (double)(-baseline));

	for (tptr = text; *tptr != '\0'; tptr++)
	{
		DBFontChar(font, *tptr, &clist, &coffset, NULL);
		grtcairoDrawCharacter(clist, *tptr, size);
		cairo_translate(tcairodata->context, (double)coffset->p_x,
				(double)coffset->p_y);
	}
	cairo_restore(tcairodata->context);
}

#endif /* VECTOR_FONTS */

/*---------------------------------------------------------
 * grtcairoPutText:
 *
 *  This routine puts a chunk of text on the screen in the current
 *  color, size, etc.  The caller must ensure that it fits on
 *  the screen -- no clipping is done except to the obscuring rectangle
 *  list and the clip rectangle.
 *
 * Results:
 *  none.
 *
 * Side Effects:
 *  The text is drawn on the screen.
 *
 *---------------------------------------------------------
 */

void
grtcairoPutText (text, pos, clip, obscure)
char *text;         /* The text to be drawn. */
Point *pos;         /* A point located at the leftmost point of
                 * the baseline for this string.
                 */
Rect *clip;         /* A rectangle to clip against */
LinkedRect *obscure;    /* A list of obscuring rectangles */

{
	Rect location;
	Rect overlap;
	Rect textrect;
	LinkedRect *ob;
	void grTCairoGeoSub();
	int i;
	float tscale;
	TCairoData *tcairodata = (TCairoData *)tcairoCurrent.mw->w_grdata2;

	if (GrTCairoTextSize(text, tcairoCurrent.fontSize, &textrect) < 0) return;

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
			grTCairoGeoSub(&location, &overlap);
		}
	}

	overlap = location;
	GeoClip(&overlap, clip);

	if ((overlap.r_xbot < overlap.r_xtop) && (overlap.r_ybot <= overlap.r_ytop))
	{
		cairo_save(tcairodata->context);
		/* Clip text to the clip rectangle */
		cairo_rectangle(tcairodata->context,
			(double)clip->r_xbot, (double)clip->r_ybot,
			(double)(clip->r_xtop - clip->r_xbot),
			(double)(clip->r_ytop - clip->r_ybot));
		cairo_clip(tcairodata->context);
		cairo_move_to(tcairodata->context, (double)location.r_xbot,
			(double)location.r_ybot);
		/* The cairo coordinate system is upside-down, so invert */
		cairo_scale(tcairodata->context, 1.0, -1.0);
		cairo_set_operator(tcairodata->context, CAIRO_OPERATOR_SOURCE);
		cairo_show_text(tcairodata->context, text);
		cairo_fill(tcairodata->context);
		cairo_restore(tcairodata->context);
	}
}


/* grTCairoGeoSub:
 *  return the tallest sub-rectangle of r not obscured by area
 *  area must be within r.
 */

void
grTCairoGeoSub(r, area)
Rect *r;        /* Rectangle to be subtracted from. */
Rect *area;     /* Area to be subtracted. */

{
	if (r->r_xbot == area->r_xbot) r->r_xbot = area->r_xtop;
	else if (r->r_xtop == area->r_xtop) r->r_xtop = area->r_xbot;
	else if (r->r_ybot <= area->r_ybot) r->r_ybot = area->r_ytop;
	else if (r->r_ytop == area->r_ytop) r->r_ytop = area->r_ybot;
	else
		r->r_xtop = area->r_xbot;
}
