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

#include <GL/gl.h>
#include <GL/glx.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "graphics/glyphs.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "graphics/graphicsInt.h"
#include "grTOGLInt.h"

extern   char	     *DBWStyleType;
extern   GLXContext  grXcontext;
extern   Display     *grXdpy;


/*---------------------------------------------------------
 * GrOGLSetCMap --
 *
 *	OpenGL uses RGB values as read from the colormap file,
 *	directly, so there is no need to install colors into a
 *	colormap.  Therefore, this is a null routine.
 *
 * Results:	None.
 *
 * Side Effects: None.
 *
 *---------------------------------------------------------
 */

void
GrTOGLSetCMap ()
{
}

Rect grtoglLines[TOGL_BATCH_SIZE];
int grtoglNbLines = 0;
TOGLRect grtoglRects[TOGL_BATCH_SIZE];
int grtoglNbRects = 0;
Rect grtoglDiagonal[TOGL_BATCH_SIZE];
int grtoglNbDiagonal = 0;

/*---------------------------------------------------------
 * grtoglDrawLines:
 *	This routine draws a batch of lines.
 *
 * Results:	None.
 *
 * Side Effects:
 *	Draw a bunch of lines.
 *---------------------------------------------------------
 */

void
grtoglDrawLines(lines, nb)
    Rect lines[];
    int nb;
{

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
}

/*---------------------------------------------------------
 * grtoglDrawLine:
 *	This routine draws a line.
 *
 * Results:	None.
 *
 * Side Effects:
 *	Draw a line for (x1, y1) to (x2, y2) inclusive.
 *---------------------------------------------------------
 */

void
grtoglDrawLine (x1, y1, x2, y2)
    int x1, y1;			/* Screen coordinates of first point. */
    int x2, y2;			/* Screen coordinates of second point. */
{
    /* Treat straight and diagonal lines separately.  Some		*/
    /* implementations of OpenGL make straight lines twice as thick	*/
    /* when smoothing is enabled.					*/

    if ((x1 == x2) || (y1 == y2))
    {
	if (grtoglNbLines == TOGL_BATCH_SIZE) GR_TOGL_FLUSH_LINES();
	grtoglLines[grtoglNbLines].r_ll.p_x = x1;
	grtoglLines[grtoglNbLines].r_ll.p_y = y1;
	grtoglLines[grtoglNbLines].r_ur.p_x = x2;
	grtoglLines[grtoglNbLines].r_ur.p_y = y2;
	grtoglNbLines++;
    }
    else
    {
	if (grtoglNbDiagonal == TOGL_BATCH_SIZE) GR_TOGL_FLUSH_DIAGONAL();
	grtoglDiagonal[grtoglNbDiagonal].r_ll.p_x = x1;
	grtoglDiagonal[grtoglNbDiagonal].r_ll.p_y = y1;
	grtoglDiagonal[grtoglNbDiagonal].r_ur.p_x = x2;
	grtoglDiagonal[grtoglNbDiagonal].r_ur.p_y = y2;
	grtoglNbDiagonal++;
    }
}

/*---------------------------------------------------------
 * grtoglFillRects:
 *	This routine draws a bunch of solid rectangles.
 *
 * Results:	None.
 *
 * Side Effects:
 *	Drawing.
 *---------------------------------------------------------
 */

void
grtoglFillRects(rects, nb)
    TOGLRect rects[];
    int nb;
{

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
}

/*---------------------------------------------------------
 * grtoglFillRect:
 *	This routine draws a solid rectangle.
 *
 * Results:	None.
 *
 * Side Effects:
 *	Drawing.
 *---------------------------------------------------------
 */

void
grtoglFillRect(r)
    Rect *r;	/* Address of a rectangle in screen
			 * coordinates.
			 */
{
    if (grtoglNbRects == TOGL_BATCH_SIZE) GR_TOGL_FLUSH_RECTS();
    grtoglRects[grtoglNbRects].r_ll.p_x = r->r_ll.p_x;
    grtoglRects[grtoglNbRects].r_ll.p_y = r->r_ll.p_y;

    grtoglRects[grtoglNbRects].r_ur.p_x = r->r_ur.p_x;
    grtoglRects[grtoglNbRects].r_ur.p_y = r->r_ur.p_y;

#ifndef OGL_SERVER_SIDE_ONLY
    grtoglRects[grtoglNbRects].r_ul.p_x = r->r_ll.p_x;
    grtoglRects[grtoglNbRects].r_ul.p_y = r->r_ur.p_y;

    grtoglRects[grtoglNbRects].r_lr.p_x = r->r_ur.p_x;
    grtoglRects[grtoglNbRects].r_lr.p_y = r->r_ll.p_y;
#endif

    grtoglNbRects++;
}

/*---------------------------------------------------------
 * grtoglFillPolygon:
 *	This routine draws a solid (convex) polygon
 *
 * Results:     None.
 *
 * Side Effects:
 *	Drawing.
 *---------------------------------------------------------
 */

void
grtoglFillPolygon(tp, np)
    Point *tp;
    int np;
{
    int i;

    glEnable(GL_POLYGON_SMOOTH);
    glBegin(GL_POLYGON);
    for (i = 0; i < np; i++)
	glVertex2i(tp[i].p_x, tp[i].p_y);
    glEnd();
    glDisable(GL_POLYGON_SMOOTH);
}

