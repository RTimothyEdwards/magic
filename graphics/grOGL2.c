/* grOGL2.c -
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

#include <stdio.h>
char *getenv();

#include <GL/gl.h>
#include <GL/glx.h>

#include "utils/magic.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "graphics/glyphs.h"
#include "graphics/graphics.h"
#include "windows/windows.h"
#include "graphics/graphicsInt.h"
#include "grOGLInt.h"

extern char *DBWStyleType;


/* GROGLSetCMap (pmap)
 *	
 *	OpenGL uses R,G,B values explicitly, so there is no
 *	need to install or record colors in a colormap.
 *
 * Results:	None.
 *
 * Side Effects: None.
 *
 * Errors:		None.
 *
 *---------------------------------------------------------
 */

void
GrOGLSetCMap ()
{
}

Rect groglLines[OGL_BATCH_SIZE];
int groglNbLines=0;
OGLRect groglRects[OGL_BATCH_SIZE];
int groglNbRects=0;

/*---------------------------------------------------------
 * groglDrawLines:
 *	This routine draws a batch of lines.
 *
 * Results:	None.
 *
 * Side Effects:
 *	Draw a bunch of lines.
 *---------------------------------------------------------
 */

void
groglDrawLines(lines, nb)
    Rect lines[];
    int nb;
{
    int i;

#ifdef OGL_SERVER_SIDE_ONLY

    glBegin(GL_LINES);
    for (i = 0; i < nb; i++){
	glVertex2i(lines[i].r_ll.p_x, lines[i].r_ll.p_y);
	glVertex2i(lines[i].r_ur.p_x, lines[i].r_ur.p_y);
    }
    glEnd();

#else

    glVertexPointer(2, GL_INT, 0, (GLvoid *)lines);
    /* use (nb << 1) because there are 2 vertices per line */
    glDrawArrays(GL_LINES, 0, nb << 1);

#endif
}

/*---------------------------------------------------------
 * groglDrawLine:
 *	This routine queues a line for batch drawing.
 *	The batch drawing is much faster than repeated calls
 *	to glBegin() and glEnd().
 *
 * Results:	None.
 *
 * Side Effects:
 *	Draw a line for (x1, y1) to (x2, y2) inclusive.
 *---------------------------------------------------------
 */

void
groglDrawLine (x1, y1, x2, y2)
    int x1, y1;			/* Screen coordinates of first point. */
    int x2, y2;			/* Screen coordinates of second point. */
{
    if (groglNbLines == OGL_BATCH_SIZE) GR_X_FLUSH_LINES();
    groglLines[groglNbLines].r_ll.p_x = x1;
    groglLines[groglNbLines].r_ll.p_y = y1;
    groglLines[groglNbLines].r_ur.p_x = x2;
    groglLines[groglNbLines].r_ur.p_y = y2;
    groglNbLines++;
}


/*---------------------------------------------------------
 * groglFillRects:
 *	This routine draws a bunch of solid rectangles.
 *
 * Results:	None.
 *
 * Side Effects:
 *	Drawing.
 *---------------------------------------------------------
 */

void
groglFillRects(rects, nb)
    OGLRect rects[];
    int nb;
{
    int i;

#ifdef OGL_SERVER_SIDE_ONLY

    for (i = 0; i < nb; i++)
	glRecti(rects[i].r_ll.p_x, rects[i].r_ll.p_y,
    		rects[i].r_ur.p_x, rects[i].r_ur.p_y);

#else
    
    glVertexPointer(2, GL_INT, 0, (GLvoid *)rects);
    /* Use (nb << 2) because there are 4 vertices per rect */
    glDrawArrays(GL_QUADS, 0, nb << 2);

#endif
}

/*---------------------------------------------------------
 * groglFillRect:
 *	This routine queues a solid rectangle for batch drawing.
 *
 *
 * Results:	None.
 *
 * Side Effects:
 *	Drawing.
 *---------------------------------------------------------
 */

void
groglFillRect(r)
    Rect *r;	/* Address of a rectangle in screen
			 * coordinates.
			 */
{
    if (groglNbRects == OGL_BATCH_SIZE) GR_X_FLUSH_RECTS();
    groglRects[groglNbRects].r_ll.p_x = r->r_ll.p_x;
    groglRects[groglNbRects].r_ll.p_y = r->r_ll.p_y;

    groglRects[groglNbRects].r_ur.p_x = r->r_ur.p_x;
    groglRects[groglNbRects].r_ur.p_y = r->r_ur.p_y;

#ifndef OGL_SERVER_SIDE_ONLY
    groglRects[groglNbRects].r_ul.p_x = r->r_ll.p_x;
    groglRects[groglNbRects].r_ul.p_y = r->r_ur.p_y;
    
    groglRects[groglNbRects].r_lr.p_x = r->r_ur.p_x;
    groglRects[groglNbRects].r_lr.p_y = r->r_ll.p_y;
#endif
    
    groglNbRects++;  
}

/*---------------------------------------------------------
 * groglFillPolygon:
 *	This routine draws a solid polygon
 *
 * Results:     None.
 *
 * Side Effects:
 *      Drawing.
 *---------------------------------------------------------
 */

void
groglFillPolygon(tp, np)
    Point *tp;
    int np;
{
    int i;

    glBegin(GL_POLYGON);
    for (i = 0; i < np; i++)
	glVertex2i(tp[i].p_x, tp[i].p_y);
    glEnd();
}

