/* grTOGL2.c -
 *
 * Copyright 2003 Open Circuit Design, Inc., for MultiGiG Ltd.
 *
 * This file contains additional functions to manipulate an X
 * color display.  Included here are rectangle drawing and color map
 * loading.
 */

#include <stdio.h>
char *getenv();

/*
#include <GL/gl.h>
#include <GL/glx.h>
*/
#include <cairo/cairo-xlib.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "graphics/glyphs.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "graphics/graphicsInt.h"
//#include "grTOGLInt.h"
#include "grTCairoInt.h"

extern   char        *DBWStyleType;
//extern   GLXContext  grXcontext;
extern   Display     *grXdpy;

extern cairo_t *grCairoContext;


/*---------------------------------------------------------
 * GrOGLSetCMap --
 *
 *  OpenGL uses RGB values as read from the colormap file,
 *  directly, so there is no need to install colors into a
 *  colormap.  Therefore, this is a null routine.
 *
 * Results: None.
 *
 * Side Effects: None.
 *
 *---------------------------------------------------------
 */

void
GrTCairoSetCMap ()
{
}

Rect grtcairoLines[TCAIRO_BATCH_SIZE];
int grtcairoNbLines = 0;
TCairoRect grtcairoRects[TCAIRO_BATCH_SIZE];
int grtcairoNbRects = 0;
Rect grtcairoDiagonal[TCAIRO_BATCH_SIZE];
int grtcairoNbDiagonal = 0;

/*---------------------------------------------------------
 * grtoglDrawLines:
 *  This routine draws a batch of lines.
 *
 * Results: None.
 *
 * Side Effects:
 *  Draw a bunch of lines.
 *---------------------------------------------------------
 */

void
grtcairoDrawLines(lines, nb)
Rect lines[];
int nb;
{
	/*
	#ifdef OGL_SERVER_SIDE_ONLY
	    int i;

	    glBegin(GL_LINES);
	    for (i = 0; i < nb; i++)
	    {
	        glVertex2i(lines[i].r_ll.p_x, lines[i].r_ll.p_y);
	        glVertex2i(lines[i].r_ur.p_x, lines[i].r_ur.p_y);
	    }
	    glEnd();
	#else
	    glVertexPointer(2, GL_INT, 0, (GLvoid *)lines);
	    glDrawArrays(GL_LINES, 0, nb << 1);
	#endif
	*/

	int i;
	for (i = 0; i < nb; i++)
	{
		cairo_move_to(grCairoContext, lines[i].r_ll.p_x, lines[i].r_ll.p_y);
		cairo_line_to(grCairoContext, lines[i].r_ur.p_x, lines[i].r_ur.p_y);
	}
	// cairo_set_source_rgba(grCairoContext, r, g, b, a);
	// cairo_set_line_width(grCairoContext, width);
	cairo_stroke(grCairoContext);
}

/*---------------------------------------------------------
 * grtoglDrawLine:
 *  This routine draws a line.
 *
 * Results: None.
 *
 * Side Effects:
 *  Draw a line for (x1, y1) to (x2, y2) inclusive.
 *---------------------------------------------------------
 */

void
grtcairoDrawLine (x1, y1, x2, y2)
int x1, y1;         /* Screen coordinates of first point. */
int x2, y2;         /* Screen coordinates of second point. */
{
	/* Treat straight and diagonal lines separately.  Some      */
	/* implementations of OpenGL make straight lines twice as thick */
	/* when smoothing is enabled.                   */

	if ((x1 == x2) || (y1 == y2))
	{
		if (grtcairoNbLines == TCAIRO_BATCH_SIZE) GR_TCAIRO_FLUSH_LINES();
		grtcairoLines[grtcairoNbLines].r_ll.p_x = x1;
		grtcairoLines[grtcairoNbLines].r_ll.p_y = y1;
		grtcairoLines[grtcairoNbLines].r_ur.p_x = x2;
		grtcairoLines[grtcairoNbLines].r_ur.p_y = y2;
		grtcairoNbLines++;
	}
	else
	{
		if (grtcairoNbDiagonal == TCAIRO_BATCH_SIZE) GR_TCAIRO_FLUSH_DIAGONAL();
		grtcairoDiagonal[grtcairoNbDiagonal].r_ll.p_x = x1;
		grtcairoDiagonal[grtcairoNbDiagonal].r_ll.p_y = y1;
		grtcairoDiagonal[grtcairoNbDiagonal].r_ur.p_x = x2;
		grtcairoDiagonal[grtcairoNbDiagonal].r_ur.p_y = y2;
		grtcairoNbDiagonal++;
	}
}

/*---------------------------------------------------------
 * grtoglFillRects:
 *  This routine draws a bunch of solid rectangles.
 *
 * Results: None.
 *
 * Side Effects:
 *  Drawing.
 *---------------------------------------------------------
 */

void
grtcairoFillRects(rects, nb)
TCairoRect rects[];
int nb;
{
	/*
	#ifdef OGL_SERVER_SIDE_ONLY

	    int i;

	    for (i = 0; i < nb; i++)
	    {
	        glRecti(rects[i].r_ll.p_x, rects[i].r_ll.p_y,
	                rects[i].r_ur.p_x, rects[i].r_ur.p_y);
	    }

	#else

	    glVertexPointer(2, GL_INT, 0, (GLvoid *)rects);
	    glDrawArrays(GL_QUADS, 0, nb << 2);

	#endif
	*/
	int i;

	for (i = 0; i < nb; i++)
	{
		cairo_rectangle(grCairoContext, 
						rects[i].r_ll.p_x, rects[i].r_ll.p_y,
		        		rects[i].r_ur.p_x, rects[i].r_ur.p_y);
	}
	// cairo_set_source_rgba(grCairoContext, r, g, b, a);
	cairo_fill(grCairoContext);
}

/*---------------------------------------------------------
 * grtoglFillRect:
 *  This routine draws a solid rectangle.
 *
 * Results: None.
 *
 * Side Effects:
 *  Drawing.
 *---------------------------------------------------------
 */

void
grtcairoFillRect(r)
Rect *r;    /* Address of a rectangle in screen
             * coordinates.
             */
{
	if (grtcairoNbRects == TCAIRO_BATCH_SIZE) GR_TCAIRO_FLUSH_RECTS();
	grtcairoRects[grtcairoNbRects].r_ll.p_x = r->r_ll.p_x;
	grtcairoRects[grtcairoNbRects].r_ll.p_y = r->r_ll.p_y;

	grtcairoRects[grtcairoNbRects].r_ur.p_x = r->r_ur.p_x;
	grtcairoRects[grtcairoNbRects].r_ur.p_y = r->r_ur.p_y;

#ifndef OGL_SERVER_SIDE_ONLY
	grtcairoRects[grtcairoNbRects].r_ul.p_x = r->r_ll.p_x;
	grtcairoRects[grtcairoNbRects].r_ul.p_y = r->r_ur.p_y;

	grtcairoRects[grtcairoNbRects].r_lr.p_x = r->r_ur.p_x;
	grtcairoRects[grtcairoNbRects].r_lr.p_y = r->r_ll.p_y;
#endif

	grtcairoNbRects++;
}

/*---------------------------------------------------------
 * grtoglFillPolygon:
 *  This routine draws a solid (convex) polygon
 *
 * Results:     None.
 *
 * Side Effects:
 *  Drawing.
 *---------------------------------------------------------
 */

void
grtcairoFillPolygon(tp, np)
Point *tp;
int np;
{
	int i;
	/*
	glEnable(GL_POLYGON_SMOOTH);
	glBegin(GL_POLYGON);
	for (i = 0; i < np; i++)
		glVertex2i(tp[i].p_x, tp[i].p_y);
	glEnd();
	glDisable(GL_POLYGON_SMOOTH);
	*/
	cairo_move_to(grCairoContext, tp[0].p_x, tp[0].p_y);
	for (i = 1; i < np; i++)
		cairo_line_to(grCairoContext, tp[i].p_x, tp[i].p_y);
	cairo_close_path(grCairoContext);
	cairo_fill(grCairoContext);
}

