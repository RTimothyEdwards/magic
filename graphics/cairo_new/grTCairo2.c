/* grTCairo2.c -
 *
 * Copyright 2017 Open Circuit Design
 *
 * This file contains additional functions to manipulate an X
 * color display.  Included here are rectangle drawing and color map
 * loading.
 *
 * Written by Chuan Chen
 */

#include <stdio.h>
char *getenv();

#include <cairo/cairo-xlib.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "graphics/glyphs.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "graphics/graphicsInt.h"
#include "grTCairoInt.h"

#include "textio/txcommands.h"

extern   char        *DBWStyleType;
extern   Display     *grXdpy;

extern cairo_pattern_t *currentStipple;

/*---------------------------------------------------------
 * GrTCairoSetCMap --
 *
 *  Cairo uses RGB values as read from the colormap file,
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
 * grtcairoDrawLines:
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
	TCairoData *tcairodata = (TCairoData *)tcairoCurrent.mw->w_grdata2;
	int i;
	for (i = 0; i < nb; i++)
	{
		cairo_move_to(tcairodata->context, lines[i].r_ll.p_x, lines[i].r_ll.p_y);
		cairo_line_to(tcairodata->context, lines[i].r_ur.p_x, lines[i].r_ur.p_y);
	}
	// cairo_set_source_rgba(tcairodata->context, r, g, b, a);
	// cairo_set_line_width(tcairodata->context, width);
	cairo_stroke(tcairodata->context);
}

/*---------------------------------------------------------
 * grtcairoDrawLine:
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
	/* Treat straight and diagonal lines separately. */
	/* (Done for OpenGL;  possibly not necessary for Cairo) */

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
 * grtcairoFillRects:
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
	TCairoData *tcairodata = (TCairoData *)tcairoCurrent.mw->w_grdata2;
	int i;

	for (i = 0; i < nb; i++)
	{
		cairo_rectangle(tcairodata->context, 
						rects[i].r_ll.p_x, rects[i].r_ll.p_y,
		        		rects[i].r_ur.p_x-rects[i].r_ll.p_x, rects[i].r_ur.p_y-rects[i].r_ll.p_y);
		// TxPrintf("%d %d %d %d \n", rects[i].r_ll.p_x, rects[i].r_ll.p_y, rects[i].r_ur.p_x-rects[i].r_ll.p_x, rects[i].r_ur.p_y-rects[i].r_ll.p_y);
	}
	cairo_clip(tcairodata->context);
	cairo_mask(tcairodata->context, currentStipple);
}

/*---------------------------------------------------------
 * grtcairoFillRect:
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

	grtcairoRects[grtcairoNbRects].r_ul.p_x = r->r_ll.p_x;
	grtcairoRects[grtcairoNbRects].r_ul.p_y = r->r_ur.p_y;

	grtcairoRects[grtcairoNbRects].r_lr.p_x = r->r_ur.p_x;
	grtcairoRects[grtcairoNbRects].r_lr.p_y = r->r_ll.p_y;

	grtcairoNbRects++;
}

/*---------------------------------------------------------
 * grtcairoFillPolygon:
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
	TCairoData *tcairodata = (TCairoData *)tcairoCurrent.mw->w_grdata2;
	int i;
	cairo_move_to(tcairodata->context, tp[0].p_x, tp[0].p_y);
	for (i = 1; i < np; i++)
		cairo_line_to(tcairodata->context, tp[i].p_x, tp[i].p_y);
	cairo_close_path(tcairodata->context);
	cairo_fill(tcairodata->context);
}

