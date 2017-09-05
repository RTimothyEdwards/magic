/* grTOGL3.c -
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

/*
#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glu.h>
*/

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
//#include "graphics/grTOGLInt.h"
#include "graphics/grTCairoInt.h"
#include "graphics/grTkCommon.h"
#include "database/fonts.h"

extern Display *grXdpy;
extern cairo_t *grCairoContext;
extern cairo_surface_t *grCairoSurface;

static GC grXcopyGC = (GC)NULL;

/* locals */

//GLuint  grXBases[4];


/*---------------------------------------------------------
 * grtoglDrawGrid:
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

	xsize = prect->r_xtop - prect->r_xbot;
	ysize = prect->r_ytop - prect->r_ybot;
	if (!xsize || !ysize || GRID_TOO_SMALL(xsize, ysize))
		return FALSE;

	xstart = prect->r_xbot % xsize;
	while (xstart < clip->r_xbot << SUBPIXELBITS) xstart += xsize;
	ystart = prect->r_ybot % ysize;
	while (ystart < clip->r_ybot << SUBPIXELBITS) ystart += ysize;

	//grtoglSetLineStyle(outline);

	//glBegin(GL_LINES);

	snum = 0;
	low = clip->r_ybot;
	hi = clip->r_ytop;
	for (x = xstart; x < (clip->r_xtop + 1) << SUBPIXELBITS; x += xsize)
	{
		shifted = x >> SUBPIXELBITS;
		//glVertex2i(shifted, low);
		//glVertex2i(shifted, hi);
		cairo_move_to(grCairoContext, shifted, low);
		cairo_line_to(grCairoContext, shifted, hi);
		snum++;
	}

	snum = 0;
	low = clip->r_xbot;
	hi = clip->r_xtop;
	for (y = ystart; y < (clip->r_ytop + 1) << SUBPIXELBITS; y += ysize)
	{
		shifted = y >> SUBPIXELBITS;
		//glVertex2i(low, shifted);
		//glVertex2i(hi, shifted);
		cairo_move_to(grCairoContext, low, shifted);
		cairo_line_to(grCairoContext, hi, shifted);
		snum++;
	}
	//glEnd();
	cairo_stroke(grCairoContext);
	return TRUE;
}


/*---------------------------------------------------------
 * grtoglLoadFont
 *  This local routine transfers the X font bitmaps
 *  into OpenGL display lists for simple text
 *  rendering.
 *
 * Results: Success/Failure
 *
 * Side Effects:    None.
 *---------------------------------------------------------
 */

bool
grtcairoLoadFont()
{
	/*
	Font id;
	unsigned int i;

	for (i = 0; i < 4; i++) {
		id = Tk_FontId(grTkFonts[i]);

		grXBases[i] = glGenLists(256);
		if (grXBases[i] == 0) {
			TxError("Out of display lists!\n");
			return FALSE;
		}
		glXUseXFont(id, 0, 256, grXBases[i]);
	}
	*/
	cairo_select_font_face(grCairoContext, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	return TRUE;
}


/*---------------------------------------------------------
 * grtoglSetCharSize:
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
	tcairoCurrent.fontSize = size;
	cairo_set_font_size(grCairoContext, size * 4 + 10);
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
 * GrTOGLTextSize --
 *
 *  Determine the size of a text string.
 *
 * Results:
 *  None.
 *
 * Side effects:
 *  A rectangle is filled in that is the size of the text in pixels.
 *  The origin (0, 0) of this rectangle is located on the baseline
 *  at the far left side of the string.
 * ----------------------------------------------------------------------------
 */

void
GrTCairoTextSize(text, size, r)
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
		TxError("%s%d\n", "GrTCairoTextSize: Unknown character size ",
		        size );
		break;
	}
	if (font == NULL) return;
	Tk_GetFontMetrics(font, &overall);
	width = Tk_TextWidth(font, text, strlen(text));
	/* Hack alert!  Tk_TextWidth returns values too small! */
	width = width + (width >> 4);
	r->r_ytop = overall.ascent;
	r->r_ybot = -overall.descent;
	r->r_xtop = width;
	r->r_xbot = 0;
}

/* OpenGL backing store functions (now removed from the X11-based ones) */
/* Since we always paint into the front buffer, the back buffer is  */
/* always available for backing store.  We need not create or destroy   */
/* it.  We just use the w_backingStore location to store whether the    */
/* backing store contains valid data or not.                */

void
grtcairoFreeBackingStore(MagWindow *window)
{
	//window->w_backingStore = (ClientData)0;
	Pixmap pmap = (Pixmap)window->w_backingStore;
	if (pmap == (Pixmap)NULL) return;
	XFreePixmap(grXdpy, pmap);
	window->w_backingStore = (ClientData)NULL;
}

void
grtcairoCreateBackingStore(MagWindow *w)
{
	Pixmap pmap;
	//Window wind = (Window)w->w_grdata;
	Tk_Window tkwind = (Tk_Window)w->w_grdata;
	Window wind;
	unsigned int width, height;
	GC gc;
	XGCValues gcValues;
	int grDepth;



	/* ignore all windows other than layout */
	//if (w->w_client != DBWclientID) return;

	/* Deferred */
	if (tkwind == NULL) return;

	//w->w_backingStore = (ClientData)1;

	wind = (Window)Tk_WindowId(tkwind);

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

	//if (grClass == 3) grDepth = 8;  /* Needed since grDisplay.depth is reset
	//			     to 7 if Pseudocolor      */

	pmap = XCreatePixmap(grXdpy, wind, width, height, grDepth);
	w->w_backingStore = (ClientData)pmap;
}

bool
grtcairoGetBackingStore(MagWindow *w, Rect *area)
{
	unsigned int width, height;
	int xbot, ybot;
	Rect r;

	// GLboolean result;
	// GLint rasterpos[4];

	if (w->w_backingStore == (ClientData)0) return FALSE;

	GEO_EXPAND(area, 1, &r);
	GeoClip(&r, &(w->w_screenArea));

	width = r.r_xtop - r.r_xbot;
	height = r.r_ytop - r.r_ybot;

	xbot = r.r_xbot;
	ybot = r.r_ybot;

	/*
		glDrawBuffer(GL_FRONT);
		glReadBuffer(GL_BACK);
		glRasterPos2i((GLint)xbot, (GLint)ybot);
	*/
	/* Check for valid raster position */
	// glGetBooleanv(GL_CURRENT_RASTER_POSITION_VALID, (GLboolean *)(&result));
	// glGetIntegerv(GL_CURRENT_RASTER_POSITION, (GLint *)(&rasterpos[0]));

	// TxPrintf("Raster valid = %d, position = %d %d %d %d\n",
	//      (int)result, (int)rasterpos[0], (int)rasterpos[1],
	//      (int)rasterpos[2], (int)rasterpos[3]);
	// if (result == 0)
	//    TxPrintf("Intended position = %d %d\n", xbot, ybot);
	/*
		glDisable(GL_BLEND);
		glCopyPixels(xbot, ybot, width, height, GL_COLOR);
	*/

	Window root_return;
	int x_return, y_return;
	unsigned int width_return, height_return;
	unsigned int border_width_return;
	unsigned int depth_return;

	Pixmap pmap;
	pmap = (Pixmap)w->w_backingStore;
	if (pmap == (Pixmap)NULL)
		return FALSE;

	XGetGeometry(grXdpy, pmap, &root_return, &x_return, &y_return, &width_return, &height_return, &border_width_return, &depth_return);

	cairo_surface_t *backingStoreSurface;
	backingStoreSurface = cairo_xlib_surface_create(grXdpy, pmap, DefaultVisual(grXdpy, DefaultScreen(grXdpy)), width_return, height_return);
//	backingStoreSurface = cairo_xlib_surface_create(grXdpy, pmap, DefaultVisual(grXdpy, DefaultScreen(grXdpy)), width, height);
	cairo_set_source_surface(grCairoContext, backingStoreSurface, 0, 0);
	cairo_rectangle(grCairoContext, xbot, ybot, width, height);
	cairo_set_operator(grCairoContext, CAIRO_OPERATOR_SOURCE);
	cairo_fill(grCairoContext);

	return TRUE;
}


bool
grtcairoScrollBackingStore(MagWindow *w, Point *shift)
{
	/*
	unsigned int width, height;
	int xorigin, yorigin, xshift, yshift;

	if (w->w_backingStore == (ClientData)0)
	{
		TxPrintf("grtoglScrollBackingStore %d %d failure\n",
		         shift->p_x, shift->p_y);
		return FALSE;
	}

	width = w->w_screenArea.r_xtop - w->w_screenArea.r_xbot;
	height = w->w_screenArea.r_ytop - w->w_screenArea.r_ybot;
	xorigin = 0;
	yorigin = 0;
	xshift = shift->p_x;
	yshift = shift->p_y;

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

	glDrawBuffer(GL_BACK);
	glReadBuffer(GL_BACK);
	glRasterPos2i((GLint)xshift, (GLint)yshift);
	glDisable(GL_BLEND);
	glCopyPixels(xorigin, yorigin, width, height, GL_COLOR);

	glDrawBuffer(GL_FRONT);

	return TRUE;
	*/

	// copied from grX11su3.c
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

	/*
	XCopyArea(grXdpy, pmap, pmap, grXcopyGC, xorigin, yorigin, width, height,
	          xshift, yshift);
	*/

	/* TxPrintf("grx11ScrollBackingStore %d %d\n", shift->p_x, shift->p_y); */

	cairo_surface_t *backingStoreSurface;
	backingStoreSurface = cairo_xlib_surface_create(grXdpy, pmap, DefaultVisual(grXdpy, DefaultScreen(grXdpy)), width, height);
	cairo_set_source_surface(grCairoContext, backingStoreSurface, xshift, yshift);
	//cairo_rectangle(grCairoContext, xorigin, yorigin, width, height);
	cairo_rectangle(grCairoContext, xshift, yshift, width, height);
	cairo_set_operator(grCairoContext, CAIRO_OPERATOR_SOURCE);
	cairo_fill(grCairoContext);

	return TRUE;
}

void
grtcairoPutBackingStore(MagWindow *w, Rect *area)
{
	unsigned int width, height;
	int ybot, xbot;

	// GLboolean result;
	// GLint rasterpos[4];

	if (w->w_backingStore == (ClientData)0) return;

	width = area->r_xtop - area->r_xbot;
	height = area->r_ytop - area->r_ybot;

	ybot = area->r_ybot;
	xbot = area->r_xbot;

	if (xbot < 0) {
		width -= xbot;
		xbot = 0;
	}

	if (ybot < 0) {
		height -= ybot;
		ybot = 0;
	}
	/*
		glReadBuffer(GL_FRONT);
		glDrawBuffer(GL_BACK);
		glRasterPos2i((GLint)xbot, (GLint)ybot);
	*/
	// Check for valid raster position
	// glGetBooleanv(GL_CURRENT_RASTER_POSITION_VALID, (GLboolean *)(&result));
	// glGetIntegerv(GL_CURRENT_RASTER_POSITION, (GLint *)(&rasterpos[0]));

	// TxPrintf("Raster valid = %d, position = %d %d %d %d\n",
	//      (int)result, (int)rasterpos[0], (int)rasterpos[1],
	//      (int)rasterpos[2], (int)rasterpos[3]);
	// if (result == 0)
	//    TxPrintf("Intended position = %d %d\n", xbot, ybot);
	/*
		glDisable(GL_BLEND);
		glCopyPixels(xbot, ybot, width, height, GL_COLOR);

		glDrawBuffer(GL_FRONT); // Return to normal front rendering
	*/

	Window root_return;
	int x_return, y_return;
	unsigned int width_return, height_return;
	unsigned int border_width_return;
	unsigned int depth_return;
	Pixmap pmap;

	pmap = (Pixmap)w->w_backingStore;
	if (pmap == (Pixmap)NULL)
		return;
	XGetGeometry(grXdpy, pmap, &root_return, &x_return, &y_return, &width_return, &height_return, &border_width_return, &depth_return);

	cairo_surface_t *backingStoreSurface;
	//backingStoreSurface = cairo_xlib_surface_create(grXdpy, pmap, DefaultVisual(grXdpy, DefaultScreen(grXdpy)), width_return, height_return);
	backingStoreSurface = cairo_xlib_surface_create(grXdpy, pmap, DefaultVisual(grXdpy, DefaultScreen(grXdpy)), width, height);
	cairo_t *tempContext = cairo_create(backingStoreSurface);
	cairo_set_source_surface(tempContext, grCairoSurface, 0.0, 0.0);
	cairo_rectangle(tempContext, xbot, ybot, width, height);
	cairo_set_operator(tempContext, CAIRO_OPERATOR_SOURCE);
	cairo_fill(tempContext);
	
	cairo_surface_flush(backingStoreSurface);
	pmap = (Pixmap) cairo_image_surface_get_data(backingStoreSurface);
	cairo_surface_mark_dirty(backingStoreSurface);
}


/*
 * ----------------------------------------------------------------------------
 * GrTOGLReadPixel --
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
	return 0;       /* OpenGL has no such function, so return 0 */
}


/*
 * ----------------------------------------------------------------------------
 * GrTOGLBitBlt --
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
	/*
	glCopyPixels(r->r_xbot, r->r_ybot, r->r_xtop - r->r_xbot + 1,
	             r->r_ytop - r->r_ybot + 1, GL_COLOR);
	*/

	//cairo_set_source_surface(grCairoContext, grCairoSurface, p->p_x, p->p_y);
	// do some stuff

	; // unimplemented
}

#ifdef VECTOR_FONTS

/*
 *----------------------------------------------------------------------
 *
 * Technically, there should be no self-intersecting polygons in outline
 * fonts.  However, decomposition of bezier curves into line segments
 * may occasionally produce one, so it needs to be handled.
 *----------------------------------------------------------------------
 */

/*
// unused in cairo graphics
void
myCombine(GLdouble coords[3], GLdouble *vertex_data[4],
          GLfloat weight[4], GLdouble **outData, void *dataptr)
{
	// This needs to be free'd at the end of gluTessEndPolygon()!
	GLdouble *new = (GLdouble *)mallocMagic(2 * sizeof(GLdouble));
	new[0] = coords[0];
	new[1] = coords[1];
	*outData = new;
	// Diagnostic
	TxError("Intersecting polygon in char \"%c\" at %g %g!\n",
	        *((char *)dataptr), coords[0], coords[1]);
}
*/

/*
 *----------------------------------------------------------------------
 * Draw a text character
 * This routine differs from grtoglFillPolygon() in that it uses the
 * glu library to handle non-convex polygons as may appear in font
 * outlines.
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
	int i, j;
//	static GLUtesselator *tess = NULL;
//	static GLdouble *v = NULL;
	static int maxnp = 0;
	FontChar *ccur;

	if (pixsize < 5) return;    /* Label too small to be useful */

	//should we be using cairo_show_glyphs??
	for (ccur = clist; ccur != NULL; ccur = ccur->fc_next) {
		tp = ccur->fc_points;
		np = ccur->fc_numpoints;
		cairo_move_to(grCairoContext, tp[0].p_x, tp[0].p_y);
		for (i = 1; i < np; i++, j += 3) {
			cairo_line_to(grCairoContext, tp[0].p_x, tp[0].p_y);
		}
	}
	cairo_fill(grCairoContext);

	/*
		if (tess == NULL)
		{
			tess = gluNewTess();
			gluTessCallback(tess, GLU_TESS_BEGIN, (_GLUfuncptr)glBegin);
			gluTessCallback(tess, GLU_TESS_VERTEX, (_GLUfuncptr)glVertex3dv);
			gluTessCallback(tess, GLU_TESS_END, (_GLUfuncptr)glEnd);
			gluTessCallback(tess, GLU_TESS_COMBINE_DATA, (_GLUfuncptr)myCombine);
		}
		// Boundary-only does not look particularly good. . .
		gluTessProperty(tess, GLU_TESS_BOUNDARY_ONLY, GL_FALSE);

		nptotal = 0;
		for (ccur = clist; ccur != NULL; ccur = ccur->fc_next)
			nptotal += ccur->fc_numpoints;

		if (nptotal > maxnp)
		{
			if (v != NULL) freeMagic((char *)v);
			maxnp = nptotal;
			v = (GLdouble *)mallocMagic(nptotal * 3 * sizeof(GLdouble));
		}

		j = 0;
		for (ccur = clist; ccur != NULL; ccur = ccur->fc_next)
		{
			tp = ccur->fc_points;
			np = ccur->fc_numpoints;

			for (i = 0; i < np; i++, j += 3) {
				v[j] = tp[i].p_x;
				v[j + 1] = tp[i].p_y;
				v[j + 2] = 0;
			}
		}

		gluTessBeginPolygon(tess, (GLvoid *)(&tc));
		j = 0;
		for (ccur = clist; ccur != NULL; ccur = ccur->fc_next)
		{
			np = ccur->fc_numpoints;
			gluTessBeginContour(tess);
			for (i = 0; i < np; i++, j += 3) {
				gluTessVertex(tess, &v[j], &v[j]);
			}
			gluTessEndContour(tess);
		}
		gluTessEndPolygon(tess);
	*/
}

/*---------------------------------------------------------
 * grtoglFontText:
 *
 *  This routine draws text from font vectors using the
 *  font vector routines in DBlabel.c.  Text is clipped
 *  to the clipping rectangle.
 *
 *  For speed, we should be transferring the font
 *  vectors into OpenGL display lists!
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
	//GLfloat fsize, matvals[16];
	float fsize;
	FontChar *clist;
	int cheight, baseline;
	float tmp;

	/* Keep it simple for now---ignore clip and obscure */
	/*
		glDisable(GL_BLEND);
		glPushMatrix();
		glTranslated(pos->p_x, pos->p_y, 0);
		glRotated(rotate, 0, 0, 1);
	*/

	cairo_save(grCairoContext);
	cairo_translate(grCairoContext, pos->p_x, pos->p_y);
	cairo_rotate(grCairoContext, ((double)rotate) / 360 * 2 * M_PI);

	/* Get label size */
	cbbox = &DBFontList[font]->mf_extents;

	//fsize = (GLfloat)size / (GLfloat)cbbox->r_ytop;
	fsize = (uint8_t)size / (uint8_t)cbbox->r_ytop;
	//glScalef(fsize, fsize, 1.0);
	cairo_scale(grCairoContext, fsize, fsize);

	/* Adjust to baseline */
	baseline = 0;
	for (tptr = text; *tptr != '\0'; tptr++)
	{
		DBFontChar(font, *tptr, NULL, NULL, &cbbox);
		if (cbbox->r_ybot < baseline)
			baseline = cbbox->r_ybot;
	}
	//glTranslated(0, -baseline, 0);
	cairo_translate(grCairoContext, 0, -baseline);

	for (tptr = text; *tptr != '\0'; tptr++)
	{
		DBFontChar(font, *tptr, &clist, &coffset, NULL);
		grtcairoDrawCharacter(clist, *tptr, size);
		//glTranslated(coffset->p_x, coffset->p_y, 0);
		cairo_translate(grCairoContext, coffset->p_x, coffset->p_y);
	}
	//glPopMatrix();
	cairo_restore(grCairoContext);
}

#endif /* VECTOR_FONTS */

/*---------------------------------------------------------
 * grtoglPutText:
 *      (modified on SunPutText)
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

	GrTCairoTextSize(text, tcairoCurrent.fontSize, &textrect);

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

	/* copy the text to the color screen */
	if ((overlap.r_xbot < overlap.r_xtop) && (overlap.r_ybot <= overlap.r_ytop))
	{
		//glScissor(overlap.r_xbot, overlap.r_ybot, overlap.r_xtop - overlap.r_xbot,
		//          overlap.r_ytop - overlap.r_ybot);
		cairo_rectangle(grCairoContext, overlap.r_xbot, overlap.r_ybot, overlap.r_xtop - overlap.r_xbot, overlap.r_ytop - overlap.r_ybot);
		cairo_clip(grCairoContext);
		/*
		glEnable(GL_SCISSOR_TEST);
		glDisable(GL_BLEND);
		glRasterPos2i(pos->p_x, pos->p_y);
		glListBase(grXBases[(toglCurrent.fontSize == GR_TEXT_DEFAULT) ?
		                    GR_TEXT_SMALL : toglCurrent.fontSize]);
		glCallLists(strlen(text), GL_UNSIGNED_BYTE, (unsigned char *)text);
		glDisable(GL_SCISSOR_TEST);
		*/
		cairo_move_to(grCairoContext, location.r_xbot, location.r_ybot);
		cairo_show_text(grCairoContext, text);
		cairo_fill(grCairoContext);
	}
}


/* grTOGLGeoSub:
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
