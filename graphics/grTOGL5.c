/* grTOGL5.c -
 *
 * Copyright 2003 Open Circuit Design, Inc., for MultiGiG Ltd.
 *
 *	Manipulate the programable cursor on the graphics display.
 *
 */

#include <stdio.h>
#include <X11/Xlib.h>

#include <GL/gl.h>
#include <GL/glx.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/styles.h"
#include "utils/hash.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "graphics/glyphs.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "graphics/graphicsInt.h"
#include "grTkCommon.h"
#include "grTOGLInt.h"

extern Display	*grXdpy;
extern int	 grXscrn;
extern HashTable grTOGLWindowTable;


/*
 * ----------------------------------------------------------------------------
 * GrTOGLDrawGlyph --
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
GrTOGLDrawGlyph (gl, p)
    GrGlyph *gl;		/* A single glyph */
    Point *p;			/* screen pos of lower left corner */
{
    Rect bBox;
    bool anyObscure;
    LinkedRect *ob;
    
    GR_CHECK_LOCK();

    /* We're going to change the graphics state without affecting */
    /* the standard color and mask saved values, so we had better */
    /* flush all rects & lines first.				  */
    GR_TOGL_FLUSH_BATCH();

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
	int *pixelp, x, y, thisp, lastp;
	int color, red, green, blue, mask;

	/* no clipping, try to go quickly */
	pixelp = gl->gr_pixels;
	thisp = -1;
	for (y = 0; y < gl->gr_ysize; y++) {
	    int x1, y1;

	    y1 = bBox.r_ybot + y;
	    for (x = 0; x < gl->gr_xsize; x++) {
		lastp = thisp;
		thisp = *pixelp++;		
		if (thisp != 0)
		{
		    /* Note: mask has traditionally been 0-127 */
		    if (thisp != lastp) {
			if (lastp != -1) glEnd();
			
			mask = GrStyleTable[thisp].mask << 1;
			color = GrStyleTable[thisp].color;
			GrGetColor(color, &red, &green, &blue);
			glColor4ub((GLubyte)red, (GLubyte)green, (GLubyte)blue,
					(GLubyte)mask);
			glBegin(GL_POINTS);
		    }
		    x1 = bBox.r_xbot + x;
		    glVertex2i((GLint)x1, (GLint)y1);
		}
	    }
	}
	if (lastp != -1) glEnd();
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
			if (*pixelp != 0)
			{
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
 * GrTOGLSetCursor:
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
GrTOGLSetCursor(cursorNum)
    int cursorNum;	/* The cursor number as defined in the display
		         * styles file.
		         */
{
    HashEntry	*entry;
    HashSearch	hs;
    Tk_Window	tkwind;

    if (cursorNum >= MAX_CURSORS)
    {
	TxError("No such cursor!\n");
	return;
    }

    toglCurrent.cursor = grCursors[cursorNum];
    
    HashStartSearch(&hs);
    while (entry = HashNext(&grTOGLWindowTable,&hs))
    {
    	if (HashGetValue(entry))
	{
	    tkwind = (Tk_Window)entry->h_key.h_ptr;
            Tk_DefineCursor(tkwind, toglCurrent.cursor);
	}
    }
}
