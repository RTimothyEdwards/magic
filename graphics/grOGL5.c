/* grOGL5.c -
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
 *	Manipulate the programable cursor on the graphics display.
 *
 */

#include <stdio.h>

#include <GL/gl.h>
#include <GL/glx.h>

#include "utils/magic.h"
#include "utils/styles.h"
#include "utils/hash.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "graphics/graphics.h"
#include "graphics/glyphs.h"
#include "windows/windows.h"
#include "graphics/graphicsInt.h"
#include "grOGLInt.h"

extern HashTable	grOGLWindowTable;
extern Display *grXdpy;
extern int	grXscrn;

Cursor grCursors[MAX_CURSORS];


/*
 * ----------------------------------------------------------------------------
 * GrOGLDrawGlyph --
 *
 *	Draw one glyph on the display.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws pixels.
 * ----------------------------------------------------------------------------
 */

void
GrOGLDrawGlyph (gl, p)
    GrGlyph *gl;		/* A single glyph */
    Point *p;			/* screen pos of lower left corner */
{
    Rect bBox;
    bool anyObscure;
    LinkedRect *ob;
    
    GR_CHECK_LOCK();

    /* We're going to change the graphics state without affecting */
    /* the standard color and mask saved values, so we had better */
    /* flush all rects & lines first.                             */
    GR_X_FLUSH_BATCH();

    bBox.r_ll = *p;
    bBox.r_xtop = p->p_x + gl->gr_xsize - 1;
    bBox.r_ytop = p->p_y + gl->gr_ysize - 1;

    anyObscure = FALSE;
    for (ob = grCurObscure; ob != NULL; ob = ob->r_next) {
	if (GEO_TOUCH( &(ob->r_r), &bBox)) {
	    anyObscure = TRUE;
	    break;
	}
    }

    if ((!anyObscure) && (GEO_SURROUND(&grCurClip, &bBox)) ) {
	int *pixelp, x, y;

	/* no clipping, try to go quickly */
	pixelp = gl->gr_pixels;
	for (y = 0; y < gl->gr_ysize; y++) {
	    int x1, y1;

	    y1 = bBox.r_ybot + y;
	    for (x = 0; x < gl->gr_xsize; x++) {
		int color, red, green, blue, mask;
		if (*pixelp != 0) {
		    mask = GrStyleTable[*pixelp].color << 1;
	            color = GrStyleTable[*pixelp].color;
		    x1 = bBox.r_xbot + x;
		    GrGetColor(color, &red, &green, &blue);
		    glColor4ub((GLubyte)red, (GLubyte)green, (GLubyte)blue,
					(GLubyte)mask);
		    glBegin(GL_POINTS);
		    glVertex2i((GLint)x1, (GLint)y1);
		    glEnd();
		}
		pixelp++;
	    }
	}
    } else {
	/* do pixel by pixel clipping */
	int y, yloc;

	yloc = bBox.r_ybot;
	for (y = 0; y < gl->gr_ysize; y++) {
	    int startx, endx;
	    if ( (yloc <= grCurClip.r_ytop) && (yloc >= grCurClip.r_ybot) ) {
		int laststartx;
		laststartx = bBox.r_xbot - 1;
		for (startx = bBox.r_xbot; startx <= bBox.r_xtop; 
			startx = endx + 1) {
		    int *pixelp;

		    startx = MAX(startx, grCurClip.r_xbot);
		    endx = MIN(bBox.r_xtop, grCurClip.r_xtop);

		    if (anyObscure) {
			for (ob = grCurObscure; ob != NULL; ob = ob->r_next) {
			    if ( (ob->r_r.r_ybot <= yloc) && 
				 (ob->r_r.r_ytop >= yloc) ) {
				if (ob->r_r.r_xbot <= startx)
				    startx = MAX(startx, ob->r_r.r_xtop + 1);
				else if (ob->r_r.r_xbot <= endx)
				    endx = MIN(endx, ob->r_r.r_xbot - 1);
			    }
			}
		    }

		    /* stop if we aren't advancing */
		    if (startx == laststartx) break;  
		    laststartx = startx;
		    if (startx > endx) continue;

		    /* draw a section of this scan line */
		    pixelp = &( gl->gr_pixels[y*gl->gr_xsize +
			    (startx - bBox.r_xbot)]);
		    for ( ; startx <= endx; startx++) {
			int color, red, green, blue, mask;
			if (*pixelp != 0) {
			    mask = GrStyleTable[*pixelp].mask << 1;
			    color = GrStyleTable[*pixelp].color;
			    GrGetColor(color, &red, &green, &blue);
			    glColor4ub((GLubyte)red, (GLubyte)green,
					(GLubyte)blue, (GLubyte)mask);
			    glBegin(GL_POINTS);
			    glVertex2i((GLint)startx, (GLint)yloc);
			    glEnd();
			}
			pixelp++;
		    }
		    startx = endx + 1;
		}
	    }
	    yloc++;
	}
    }
}


/*
 * ----------------------------------------------------------------------------
 * groglDefineCursor:
 *
 * Use X cursors on top of the OpenGL window
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The given matrix is stored in the graphics display, and it can be
 *	used as the cursor by calling GrSetCursor.
 * ----------------------------------------------------------------------------
 */

void
groglDefineCursor(glyphs)
    GrGlyphs *glyphs;
{
    int glyphnum;
    Rect oldClip;
    int red, green, blue;
    Pixmap source,mask;
    XColor curcolor;
    

    if (glyphs->gr_num <= 0) return;

    if (glyphs->gr_num > MAX_CURSORS)
    {
	TxError("X only has room for %d cursors\n", MAX_CURSORS);
	return;
    }

    /* expand clipping amount for off-screen access on the X */
    GrLock(GR_LOCK_SCREEN, FALSE);
    oldClip = grCurClip;
    grCurClip = GrScreenRect;
    grCurClip.r_ytop += 16;
    
    /* what color should the cursor be?  The following makes it the
       opposite of what the background "mostly" is.
    */
    GrGetColor(GrStyleTable[STYLE_TRANSPARENT].color, &red, &green, &blue);

    if (red + green + blue > 0x180)  /* (0x180 = 128 * 3) */
    {
    	 curcolor.pixel = BlackPixel(grXdpy,grXscrn);
	 curcolor.red = 0;
	 curcolor.green = 0;
	 curcolor.blue = 0;
	 curcolor.flags |= DoRed|DoGreen|DoBlue;
    }
    else
    {
    	 curcolor.pixel = WhitePixel(grXdpy,grXscrn);
	 curcolor.red = 65535;
	 curcolor.green = 65535;
	 curcolor.blue = 65535;
	 curcolor.flags |= DoRed|DoGreen|DoBlue;
    }

    /* enter the glyphs */
    for (glyphnum = 0; glyphnum < glyphs->gr_num; glyphnum++) {
	int *p;
	GrGlyph *g;
	int x, y;
	unsigned char curs[32];
	
	g = glyphs->gr_glyph[glyphnum];
	if ((g->gr_xsize != 16) || (g->gr_ysize != 16)) {
	    TxError("Cursors for the X must be 16 X 16 pixels.\n");
	    return;
	}
	
	/* Perform transposition on the glyph matrix since X displays
	 * the least significant bit on the left hand side.
	 */
	p = &(g->gr_pixels[0]);
	for (y = 0; y < 32; y += 2) {
	    int i;

	    curs[i = 31 - y - 1] = 0;
	    for (x = 0; x < 8; x++) 
	    {
		if (GrStyleTable[*p].color != 0)
		{
		     curs[i] |= 1 << x; 
		}
		p++;
	    }
	    curs[i += 1] = 0;
	    for (x = 0; x < 8; x++) 
	    {
		if (GrStyleTable[*p].color != 0)
		{
		     curs[i] |= 1 << x; 
		}
		p++;
	    }
	}
	source = XCreateBitmapFromData(grXdpy, XDefaultRootWindow(grXdpy),
				       (char *)curs, 16, 16);
	mask = XCreateBitmapFromData(grXdpy, XDefaultRootWindow(grXdpy),
				     (char *)curs, 16, 16);
	grCursors[glyphnum] = XCreatePixmapCursor(grXdpy, source, mask,
						  &curcolor, &curcolor,
						  g->gr_origin.p_x,
						  (15 - g->gr_origin.p_y));
    }

    /* Restore clipping */
    grCurClip = oldClip;
    GrUnlock(GR_LOCK_SCREEN);
}


/*
 * ----------------------------------------------------------------------------
 * GrOGLSetCursor:
 *
 *	Make the cursor be a new pattern, as defined in the display styles file.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	When the cursor is turned back on it will take on the new pattern.
 * ----------------------------------------------------------------------------
 */

void
GrOGLSetCursor(cursorNum)
int cursorNum;		/* The cursor number as defined in the display
		         * styles file.
		         */
{
    HashEntry	*entry;
    HashSearch	hs;

    if (cursorNum >= MAX_CURSORS)
    {
	TxError("No such cursor!\n");
	return;
    }

    oglCurrent.cursor = grCursors[cursorNum];
    
    HashStartSearch(&hs);
    while (entry = HashNext(&grOGLWindowTable, &hs))
	if (HashGetValue(entry))
	    XDefineCursor(grXdpy, (Window)entry->h_key.h_ptr, oglCurrent.cursor);

    /* The following is necessary to make sure the cursor is changed NOW */
    glXWaitX();
}
