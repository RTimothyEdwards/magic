/* grOGL3.c -
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

#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glu.h>

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
#include "grOGLInt.h"

extern Display *grXdpy;

static XFontStruct *grXFonts[4];
#define grSmallFont     grXFonts[0]
#define grMediumFont    grXFonts[1]
#define grLargeFont     grXFonts[2]
#define grXLargeFont    grXFonts[3]
GLuint	grXBases[4];



/*---------------------------------------------------------
 * groglDrawGrid:
 *	groglDrawGrid adds a grid to the grid layer, using the current
 *	write mask and color.
 *
 * Results:
 *	TRUE is returned normally.  However, if the grid gets too small
 *	to be useful, then nothing is drawn and FALSE is returned.
 *
 * Side Effects:    None.
 *---------------------------------------------------------
 */

bool
groglDrawGrid (prect, outline, clip)
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
    int snum, low, hi, shifted;

    xsize = prect->r_xtop - prect->r_xbot;
    ysize = prect->r_ytop - prect->r_ybot;
    if (!xsize || !ysize)
	return FALSE;
    if (GRID_TOO_SMALL(xsize, ysize))
	return FALSE;
    
    xstart = prect->r_xbot % xsize;
    while (xstart < clip->r_xbot << SUBPIXELBITS) xstart += xsize;
    ystart = prect->r_ybot % ysize;
    while (ystart < clip->r_ybot << SUBPIXELBITS) ystart += ysize;
    
    groglSetLineStyle(outline);

    glBegin(GL_LINES);

    snum = 0;
    low = clip->r_ybot;
    hi = clip->r_ytop;
    for (x = xstart; x < (clip->r_xtop + 1) << SUBPIXELBITS; x += xsize)
    {
	shifted = x >> SUBPIXELBITS;
	glVertex2i(shifted, low);
	glVertex2i(shifted, hi);
	snum++;
    }

    snum = 0;
    low = clip->r_xbot;
    hi = clip->r_xtop;
    for (y = ystart; y < (clip->r_ytop + 1) << SUBPIXELBITS; y += ysize)
    {
	shifted = y >> SUBPIXELBITS;
	glVertex2i(low, shifted);
	glVertex2i(hi, shifted);
	snum++;
    }

    glEnd();
    return TRUE;
}


/*---------------------------------------------------------
 * groglPreLoadFont
 *	This local routine loads the X fonts used by Magic.
 *	At this time, we need the font information to size
 *	the window (making room for the title at the top),
 *	but with no rendering context set (no window to
 *	draw to) we defer transferring the font bitmaps to
 *	OpenGL display lists until later.
 *
 * Results:	Success/Fail
 *
 * Side Effects:    None.
 *---------------------------------------------------------
 */

bool
groglPreLoadFont()
{
    XFontStruct *fontInfo;
    int i;
    char *s;
    char *unable = "Unable to load font";

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

    for (i = 0; i < 4; i++) {
    	 s = XGetDefault(grXdpy,"magic",optionnames[i]);
	 if (s) fontnames[i] = s;
         if ((fontInfo = XLoadQueryFont(grXdpy, fontnames[i])) == NULL) {
	      TxError("%s %s\n",unable,fontnames[i]);
              if ((grXFonts[i]= XLoadQueryFont(grXdpy,GR_DEFAULT_FONT))==NULL) {
	           TxError("%s %s\n",unable,GR_DEFAULT_FONT);
		   return FALSE;
	      }
         }
	 grXFonts[i] = fontInfo;
    }
    return TRUE;
}


/*---------------------------------------------------------
 * groglLoadFont
 *	This local routine transfers the X font bitmaps
 *	into OpenGL display lists for simple text
 *	rendering.
 *
 * Results:	Success/Fail.
 *
 * Side Effects:    None.
 *---------------------------------------------------------
 */

bool
groglLoadFont()
{
    XFontStruct *fontInfo;
    Font id;
    unsigned int first, last, i;

    for (i = 0; i < 4; i++) {
	fontInfo = grXFonts[i];
	id = fontInfo->fid;
	first = fontInfo->min_char_or_byte2;
	last = fontInfo->max_char_or_byte2;

	grXBases[i] = glGenLists(last+1);
	if (grXBases[i] == 0) {
	    TxError("Out of display lists!\n");
	    return FALSE;
	}
	glXUseXFont(id, first, last-first+1, grXBases[i]+first);
    }
    return TRUE;
}


/*---------------------------------------------------------
 * groglSetCharSize:
 *	This local routine sets the character size in the display,
 *	if necessary.
 *
 * Results:	None.
 *
 * Side Effects:    None.
 *---------------------------------------------------------
 */

void
groglSetCharSize (size)
    int size;		/* Width of characters */
{
    oglCurrent.fontSize = size;

    switch (size) {
    case GR_TEXT_DEFAULT:
    case GR_TEXT_SMALL:
	oglCurrent.font = grSmallFont;
	break;
    case GR_TEXT_MEDIUM:
	oglCurrent.font = grMediumFont;
	break;
    case GR_TEXT_LARGE:
	oglCurrent.font = grLargeFont;
	break;
    case GR_TEXT_XLARGE:
	oglCurrent.font = grXLargeFont;
	break;
    default:
	    TxError("%s%d\n", "groglSetCharSize: Unknown character size ",
		size );
	    break;
	break;
    }
}


/*
 * ----------------------------------------------------------------------------
 * GrOGLTextSize --
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
GrOGLTextSize(text, size, r)
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
	TxError("%s%d\n", "GrOGLTextSize: Unknown character size ",
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
 * GrOGLReadPixel --
 *
 *	Read one pixel from the screen.
 *
 * Results:
 *	An integer containing the pixel's color.
 *	(Except OpenGL has no such function, so we just return 0)
 *
 * Side effects:
 *	none.
 *
 * ----------------------------------------------------------------------------
 */

int
GrOGLReadPixel (w, x, y)
    MagWindow *w;
    int x,y;		/* the location of a pixel in screen coords */
{
   return 0;
}


/*
 * ----------------------------------------------------------------------------
 * GrOGLBitBlt --
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
GrOGLBitBlt(r, p)
    Rect *r;
    Point *p;
{
    glCopyPixels(r->r_xbot, r->r_ybot, r->r_xtop - r->r_xbot + 1,
		r->r_ytop - r->r_ybot + 1, GL_COLOR);
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

void
myCombine(GLdouble coords[3], GLdouble *vertex_data[4],
          GLfloat weight[4], GLdouble **outData, void *dataptr)
{
    /* This needs to be free'd at the end of gluTessEndPolygon()! */
    GLdouble *new = (GLdouble *)mallocMagic(2 * sizeof(GLdouble));
    new[0] = coords[0];
    new[1] = coords[1];
    *outData = new;
    /* Diagnostic */
    TxError("Intersecting polygon in char \"%c\" at %g %g!\n", 
	*((char *)dataptr), coords[0], coords[1]);
}

/*
 *----------------------------------------------------------------------
 * Draw a text character
 * This routine differs from grtoglFillPolygon() in that it uses the
 * glu library to handle non-convex polygons as may appear in font
 * outlines.
 *----------------------------------------------------------------------
 */

void
groglDrawCharacter(clist, tc, pixsize)
    FontChar *clist;
    unsigned char tc;
    int pixsize;
{
    Point *tp;
    int np, nptotal;
    int i, j;
    static GLUtesselator *tess = NULL;
    static GLdouble *v = NULL;
    static int maxnp = 0;
    FontChar *ccur;

    if (pixsize < 5) return;	/* Label too small to be useful */

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
}

/*---------------------------------------------------------
 * groglFontText:
 *
 *	This routine draws text from font vectors using the
 *	font vector routines in DBlabel.c.  Text is clipped
 *	to the clipping rectangle.
 *
 *	For speed, we should be transferring the font
 *	vectors into OpenGL display lists!
 *	
 *---------------------------------------------------------
 */

void
groglFontText(text, font, size, rotate, pos, clip, obscure)
    char *text;			/* The text to be drawn */
    int   font;			/* Font to use from fontList */
    int   size;			/* Pixel size of the font */
    int	  rotate;		/* Text rotation */
    Point *pos;			/* Text base position */
    Rect  *clip;		/* Clipping area */
    LinkedRect *obscure;	/* List of obscuring areas */ 
{
    char *tptr;
    Point *coffset;		/* vector to next character */
    Rect *cbbox;
    GLfloat fsize, matvals[16];
    FontChar *clist;
    int cheight, baseline;
    float tmp;

    /* Keep it simple for now---ignore clip and obscure */

    glDisable(GL_BLEND);
    glPushMatrix();
    glTranslated(pos->p_x, pos->p_y, 0);
    glRotated(rotate, 0, 0, 1);

    /* Get label size */
    cbbox = &DBFontList[font]->mf_extents;

    fsize = (GLfloat)size / (GLfloat)cbbox->r_ytop;
    glScalef(fsize, fsize, 1.0);

    /* Adjust to baseline */
    baseline = 0;
    for (tptr = text; *tptr != '\0'; tptr++)
    {
	DBFontChar(font, *tptr, NULL, NULL, &cbbox);
	if (cbbox->r_ybot < baseline)
	    baseline = cbbox->r_ybot;
    }
    glTranslated(0, -baseline, 0);

    for (tptr = text; *tptr != '\0'; tptr++)
    {
	DBFontChar(font, *tptr, &clist, &coffset, NULL);
	groglDrawCharacter(clist, *tptr, size);
	glTranslated(coffset->p_x, coffset->p_y, 0);
    }
    glPopMatrix();
}

#endif /* VECTOR_FONTS */

static GC grXcopyGC = (GC)NULL;

/*
 * ----------------------------------------------------------------------------
 * groglFreeBackingStore --
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
groglFreeBackingStore(MagWindow *window)
{
    Pixmap pmap = (Pixmap)window->w_backingStore;
    if (pmap == (Pixmap)NULL) return;
    XFreePixmap(grXdpy, pmap);
    window->w_backingStore = (ClientData)NULL;
    /* XFreeGC(grXdpy, grXcopyGC); */
    /* TxPrintf("groglFreeBackingStore called\n"); */
}

/*
 * ----------------------------------------------------------------------------
 * groglCreateBackingStore --
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
groglCreateBackingStore(MagWindow *w)
{
    Pixmap pmap;
    Window wind = (Window)w->w_grdata;
    unsigned int width, height;
    GC gc;
    XGCValues gcValues;

    /* ignore for all windows except layout */
    if (w->w_client != DBWclientID) return;

    /* deferred */
    if (w->w_grdata == (Window)NULL) return;

    width = w->w_screenArea.r_xtop - w->w_screenArea.r_xbot;
    height = w->w_screenArea.r_ytop - w->w_screenArea.r_ybot;

    if (w->w_backingStore != (ClientData)NULL) groglFreeBackingStore(w);

    if (grXcopyGC == (GC)NULL)
    {
	gcValues.graphics_exposures = FALSE;
	grXcopyGC = XCreateGC(grXdpy, wind, GCGraphicsExposures, &gcValues);
    }

    pmap = XCreatePixmap(grXdpy, wind, width, height, oglCurrent.depth);
    w->w_backingStore = (ClientData)pmap;

    /* TxPrintf("groglCreateBackingStore area %d %d %d %d\n",
	w->w_screenArea.r_xbot, w->w_screenArea.r_ybot,
	w->w_screenArea.r_xtop, w->w_screenArea.r_ytop); */
}

/*
 * ----------------------------------------------------------------------------
 * groglGetBackingStore --
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
groglGetBackingStore(MagWindow *w, Rect *area)
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
    ybot = glTransY(w, r.r_ytop);

    xoff = w->w_screenArea.r_xbot - w->w_allArea.r_xbot;
    yoff = w->w_allArea.r_ytop - w->w_screenArea.r_ytop;

    XCopyArea(grXdpy, pmap, wind, grXcopyGC, r.r_xbot - xoff, ybot - yoff,
		width, height, r.r_xbot, ybot);

    /* TxPrintf("groglGetBackingStore %d %d %d %d\n",
		r.r_xbot, r.r_ybot, r.r_xtop, r.r_ytop); */
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 * groglScrollBackingStore --
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
groglScrollBackingStore(MagWindow *w, Point *shift)
{
    Pixmap pmap;
    unsigned int width, height;
    int xorigin, yorigin, xshift, yshift;

    pmap = (Pixmap)w->w_backingStore;
    if (pmap == (Pixmap)NULL)
    {
	TxPrintf("groglScrollBackingStore %d %d failure\n",
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

    /* TxPrintf("groglScrollBackingStore %d %d\n", shift->p_x, shift->p_y); */
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 * groglPutBackingStore --
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
groglPutBackingStore(MagWindow *w, Rect *area)
{
    Pixmap pmap = (Pixmap)w->w_backingStore;
    Window wind = (Window)w->w_grdata;
    unsigned int width, height;
    int ybot, xoff, yoff;

    if (pmap == (Pixmap)NULL) return;

    /* Attempting to write backing store into an obscured	*/
    /* window (which we keep track of with Visibility events)	*/
    /* causes backing store to be invalid.			*/

    if (w->w_flags & WIND_OBSCURED)
    {
	groglFreeBackingStore(w);
	w->w_backingStore = (ClientData)NULL;
	return;
    }

    width = area->r_xtop - area->r_xbot;
    height = area->r_ytop - area->r_ybot;
    ybot = glTransY(w, area->r_ytop);

    xoff = w->w_screenArea.r_xbot - w->w_allArea.r_xbot;
    yoff = w->w_allArea.r_ytop - w->w_screenArea.r_ytop;

    XCopyArea(grXdpy, wind, pmap, grXcopyGC, area->r_xbot, ybot,
		width, height, area->r_xbot - xoff, ybot - yoff);

    /* TxPrintf("groglPutBackingStore %d %d %d %d\n",
		area->r_xbot, area->r_ybot, area->r_xtop, area->r_ytop); */
}


/*---------------------------------------------------------
 * groglPutText:
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
groglPutText (text, pos, clip, obscure)
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
    void grOGLGeoSub();
    int i;
    float tscale;

    GrOGLTextSize(text, oglCurrent.fontSize, &textrect);

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
	    grOGLGeoSub(&location, &overlap);
	}
    }
 
    overlap = location;
    GeoClip(&overlap, clip);

    /* copy the text to the color screen */
    if ((overlap.r_xbot < overlap.r_xtop)&&(overlap.r_ybot <= overlap.r_ytop))
    {
	glScissor(overlap.r_xbot, overlap.r_ybot, overlap.r_xtop - overlap.r_xbot,
		overlap.r_ytop - overlap.r_ybot);
	glEnable(GL_SCISSOR_TEST);
	glDisable(GL_BLEND);
	glRasterPos2i(pos->p_x, pos->p_y);
	glListBase(grXBases[(oglCurrent.fontSize == GR_TEXT_DEFAULT) ?
		GR_TEXT_SMALL : oglCurrent.fontSize]);
	glCallLists(strlen(text), GL_UNSIGNED_BYTE, (unsigned char *)text);
	glDisable(GL_SCISSOR_TEST);
    }
}


/* grOGLGeoSub:
 *	return the tallest sub-rectangle of r not obscured by area
 *	area must be within r.
 */

void
grOGLGeoSub(r, area)
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
