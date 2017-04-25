/* windDisplay.c -
 *
 *	Display the borders of the window, including the caption area.
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
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/windows/windDisp.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/utils.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "graphics/glyphs.h"
#include "windows/windows.h"
#include "windows/windInt.h"
#include "graphics/graphics.h"
#include "utils/styles.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/undo.h"
#include "utils/signals.h"
#include "utils/malloc.h"

/* The following Plane is used to keep track of areas of the
 * screen that have changed.  ERROR_P tiles are used to record
 * areas of the screen (in pixels) that must be redisplayed.
 *
 * If windows have speparate coordinate spaces (as on a SUN), this plane
 * is ignored and the one in w->w_redrawAreas is used instead.
 */

global Plane *windRedisplayArea = NULL;
static Plane *windCurRedrawPlane = NULL;  /* used for sharing between procs */
static LinkedRect *windCoveredAreas = NULL;
global GrGlyphs *windGlyphs = NULL;

/* windCaptionPixels is the height of the caption strip.  It is internal to
 * this module.
 */

int windCaptionPixels = 0;

/* Are the windows in their own coordinate system?
 */
bool windSomeSeparateRedisplay = FALSE;

/*
 * ----------------------------------------------------------------------------
 *
 * windCheckOnlyWindow --
 *
 * Check if the TopWindow is the only window---if so, we shouldn't 
 * generate bothersome messages about the cursor not being in a window,
 * because there is no confusion.
 *
 * We want to make sure that we don't count windows which are not of
 * the same client type.
 *
 * ----------------------------------------------------------------------------
 */

int
windCheckOnlyWindow(MagWindow **w, WindClient client)
{
    MagWindow *sw, *tw;
    int wct = 0;

    if (*w != NULL) return 0;

    if (windTopWindow != NULL)
    {
	for (sw = windTopWindow; sw != NULL; sw = sw->w_nextWindow)
	{
	    if (sw->w_client == client)
	    {
		wct++;
		tw = sw;
	    }
	}
	if (wct == 1) *w = tw;
    }
    return 0;
}


/*
 * ----------------------------------------------------------------------------
 * windFreeList --
 *
 *	Free up a list of linked rectangles.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Storage is reclaimed.  The original pointer is NULLed out.
 * ----------------------------------------------------------------------------
 */

void
windFreeList(llr)
    LinkedRect **llr;	/* A pointer to a list of linked rectangles */
{
    LinkedRect *lr, *freelr;

    lr = *llr; 
    while (lr != (LinkedRect *) NULL)
    {
	freelr = lr;
	lr = lr->r_next;
	freeMagic( (char *) freelr);
    }

    *llr = (LinkedRect *) NULL;
}



/*
 * ----------------------------------------------------------------------------
 * windReClip --
 *
 *	Traverse the linked list of windows, updating w_clipAgainst
 *	for each window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes w_clipAgainst for some windows.
 * ----------------------------------------------------------------------------
 */

void
windReClip()
{
    MagWindow *w1, *w2;

    /* an O(n**2) operation!! */
    windFreeList(&windCoveredAreas);

    for (w1 = windBottomWindow; w1 != (MagWindow *) NULL; w1 = w1->w_prevWindow)
    {
	LinkedRect *tmp;

	/* add window onto windCoveredAreas */
	tmp = (LinkedRect *) mallocMagic((unsigned) (sizeof (LinkedRect)));
	tmp->r_next = windCoveredAreas;
	tmp->r_r = w1->w_frameArea;
	windCoveredAreas = tmp;

	/* free up the old clipping areas and make new ones */
	windFreeList( &(w1->w_clipAgainst) );
	w1->w_clipAgainst = (LinkedRect *) NULL;

	/* Leave w_clipAgainst to be NULL if we are using some other
	 * window package, because that package will handle overlapping
	 * windows.
	 */
	if (WindPackageType == WIND_MAGIC_WINDOWS) {
	    for (w2 = w1->w_prevWindow; w2 != (MagWindow *) NULL; 
		    w2 = w2->w_prevWindow)
	    {
		if ( GEO_TOUCH( &(w1->w_frameArea), &(w2->w_frameArea) ))
		{
		    tmp = (LinkedRect *) mallocMagic((unsigned) (sizeof (LinkedRect)));
		    tmp->r_next = w1->w_clipAgainst;
		    tmp->r_r = w2->w_frameArea;
		    w1->w_clipAgainst = tmp;
		}
	    }
	}
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * WindSeparateRedisplay --
 *
 *	Tells the window manager to record redisplay areas for this window
 *	separately -- probably because the window has its own separate 
 *	coordinate system.  (Used on the Sun with Suntools or X.)
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
WindSeparateRedisplay(w)
    MagWindow *w;
{
    windSomeSeparateRedisplay = TRUE;
    if (w->w_redrawAreas != (ClientData)NULL) return;
    w->w_redrawAreas = (ClientData) DBNewPlane((ClientData) TT_SPACE);
}

 
/*
 * ----------------------------------------------------------------------------
 * WindIconChanged --
 *
 *	Mark the icon for this window as needing to be redisplayed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

void
WindIconChanged(w)
    MagWindow *w;
{
    ASSERT(w != NULL, "WindIconChanged");
    w->w_flags |= WIND_REDRAWICON;
}


/*
 * ----------------------------------------------------------------------------
 * WindAreaChanged --
 *
 *	Notify the window package that a certain area of the screen has
 *	changed, and now needs to be redrawn.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The area is noted as having changed in window w.  When
 *	WindUpdate is called, this area will be redisplayed, but
 *	only in w.  If w is NULL, then this area will be redisplayed
 *	in all windows.  If area is NULL, then the whole area of
 *	the window will be redisplayed.  If both are NULL, then
 *	the whole screen will be redisplayed.
 * ----------------------------------------------------------------------------
 */

void
WindAreaChanged(w, area)
    MagWindow *w;		/* The window that changed. */
    Rect *area;		/* The area in screen coordinates.
			 * NULL means the whole screen.  Caller
			 * should clip this rectangle to the area
			 * of the window.
			 */
{
    Rect biggerArea;
    int windChangedFunc();	/* Forward declaration. */

    if (w == NULL) {
        if (windSomeSeparateRedisplay) {
	    MagWindow *nw;
	    for (nw = windTopWindow; nw != NULL; nw = nw->w_nextWindow)
		WindAreaChanged(nw, area);
	    return;
	} else
	    windCurRedrawPlane = windRedisplayArea;
    } else {
	if (w->w_redrawAreas != (ClientData)NULL) 
	    windCurRedrawPlane = (Plane *) w->w_redrawAreas;
	else
	    windCurRedrawPlane = windRedisplayArea;
    }

    if (area == (Rect *) NULL)
    {
	/* Everything changed -- all the window's area as well as the icon. */
	if (w != (MagWindow *) NULL) {
	    area = &w->w_allArea;
	    WindIconChanged(w);
	} else {
	    MagWindow *nw;
	    area = &GrScreenRect;
	    for (nw = windTopWindow; nw != NULL; nw = nw->w_nextWindow)
		WindIconChanged(nw);
	}
    }

    /* We have to increase the upper x- and y-coordinates of
     * the redisplay area by one.  This is because a pixel is
     * considered to include its entire area (up to the beginning
     * of the next pixel). Without this code and corresponding
     * hacks in WindUpdate and windBackgroundFunc, little slivers
     * get left lying around.
     */
    
    biggerArea = *area;
    biggerArea.r_xtop += 1;
    biggerArea.r_ytop += 1;

    /* If no window is given, or if the window is unobscured,
     * then just paint an error tile over the area to be redisplayed.
     * Otherwise, clip the area against all the obscuring areas
     * for the window.  Be careful to keep undo away from this.
     */

    UndoDisable();
    if ((w == NULL) || (w->w_clipAgainst == NULL))
	DBPaintPlane(windCurRedrawPlane, &biggerArea,
	    DBStdPaintTbl(TT_ERROR_P, PL_DRC_ERROR), (PaintUndoInfo *) NULL);
    else
    {
	(void) GeoDisjoint(&biggerArea, &w->w_clipAgainst->r_r, windChangedFunc,
	    (ClientData) w->w_clipAgainst->r_next);
    }
    UndoEnable();

    /* If the area is NULL or encompasses the whole screen area, and	*/
    /* there is no backing store, then we should create it so that it	*/
    /* will be copied into on the next display redraw.			*/

    if ((w != NULL) && (w->w_backingStore == (ClientData)NULL) &&
		(!(w->w_flags & WIND_OBSCURED)) && (GrCreateBackingStorePtr != NULL))
	if ((area == (Rect *)NULL) || GEO_SURROUND(&biggerArea, &w->w_screenArea))
	    (*GrCreateBackingStorePtr)(w);
}

int
windChangedFunc(area, next)
    Rect *area;			/* Area that is still unobscured. */
    LinkedRect *next;		/* Next obscuring area. */
{
    /* If we're at the end of obscuring areas, paint an error
     * tile to mark what's to be redisplayed.  Otherwise,
     * clip against the next obscuring area.
     */
    
    if (next == NULL)
	DBPaintPlane(windCurRedrawPlane, area,
	    DBStdPaintTbl(TT_ERROR_P, PL_DRC_ERROR), (PaintUndoInfo *) NULL);
    else (void) GeoDisjoint(area, &next->r_r, windChangedFunc,
	(ClientData) next->r_next);
    return 0;
}




/*
 * ----------------------------------------------------------------------------
 * windBarLocations --
 *
 *	Find the scroll bars and icons in the window.
 *	Each argument must point to a different piece of memory.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Returns a bunch of rectangles describing the locations of things.
 *	See comments next to the argument list for details.
 * ----------------------------------------------------------------------------
 */

void
windBarLocations(w, leftBar, botBar, up, down, right, left, zoom)
    MagWindow *w;		/* The window under consideration. */
			/* The following are rectangles that will be filled
			 * in by this procedure.  The values will be in the
			 * same coordinate sytem as w->w_allArea.
			 */
    Rect *leftBar;	/* The location of the left scrollbar area (not the
			 * bar itself).
			 */
    Rect *botBar;	/* The location of the bottom scrollbar area. */
    Rect *up;		/* The location of the 'up arrow' icon above the 
			 * left scroll bar.
			 */
    Rect *down;		/* The location of the 'down arrow' icon below the
			 * left scroll bar.
			 */
    Rect *right;	/* The location of the 'right arrow' icon to the right
			 * of the bottom scroll bar.
			 */
    Rect *left;		/* The location of the 'left arrow' icon to the left of
			 * the bottom scroll bar.
			 */
    Rect *zoom;		/* The location of the 'zoom' icon in the lower-left
			 * corner of the window.
			 */
{
    /* left scroll bar area */
    leftBar->r_xbot = w->w_allArea.r_xbot + THIN_LINE;
    leftBar->r_ybot = w->w_allArea.r_ybot + THIN_LINE + WindScrollBarWidth + 
	BOT_BORDER(w);
    leftBar->r_xtop = leftBar->r_xbot + WindScrollBarWidth - GrPixelCorrect;
    leftBar->r_ytop = w->w_allArea.r_ytop - THIN_LINE - WindScrollBarWidth -
	TOP_BORDER(w);

    /* bottom scroll bar area */
    botBar->r_ybot = w->w_allArea.r_ybot + THIN_LINE;
    botBar->r_xbot = w->w_allArea.r_xbot + THIN_LINE + WindScrollBarWidth + 
	LEFT_BORDER(w);
    botBar->r_ytop = botBar->r_ybot + WindScrollBarWidth - GrPixelCorrect;
    botBar->r_xtop = w->w_allArea.r_xtop - THIN_LINE - WindScrollBarWidth -
	RIGHT_BORDER(w);

    /* border icons */
    down->r_xbot = up->r_xbot = leftBar->r_xbot;
    down->r_xtop = up->r_xtop = leftBar->r_xtop;
    up->r_ybot = leftBar->r_ytop + THIN_LINE + 1;
    up->r_ytop = up->r_ybot + WindScrollBarWidth - 1;
    down->r_ytop = leftBar->r_ybot - THIN_LINE - 1;
    down->r_ybot = down->r_ytop - WindScrollBarWidth + 1;

    left->r_ybot = right->r_ybot = botBar->r_ybot;
    left->r_ytop = right->r_ytop = botBar->r_ytop;
    right->r_xbot = botBar->r_xtop + THIN_LINE + 1;
    right->r_xtop = right->r_xbot + WindScrollBarWidth - 1;
    left->r_xtop = botBar->r_xbot - THIN_LINE - 1;
    left->r_xbot = left->r_xtop - WindScrollBarWidth + 1;

    zoom->r_xbot = w->w_allArea.r_xbot + THIN_LINE;
    zoom->r_ybot = w->w_allArea.r_ybot + THIN_LINE;
    zoom->r_xtop = zoom->r_xbot + WindScrollBarWidth - 1;
    zoom->r_ytop = zoom->r_ybot + WindScrollBarWidth - 1;
}


/*
 * ----------------------------------------------------------------------------
 * WindDrawBorder --
 *
 *	Draw the border of windows.  A window lock is created & then destroyed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Redisplays the scroll bar & caption areas of a window's border.
 * ----------------------------------------------------------------------------
 */

void
WindDrawBorder(w, clip)
    MagWindow *w;
    Rect *clip;
{
    Rect r;
    Rect leftBar, botBar, up, down, left, right, zoom;
    Rect leftElev, botElev;
    int bar, bbox, viewl, viewu;
    Point capp;
    Rect capr;

    GrLock(w, FALSE);
    GrClipTo(clip);


    /* Redisplay the caption if it overlaps the area. */

    capr = w->w_allArea;
    capr.r_ybot = capr.r_ytop - TOP_BORDER(w) + GrPixelCorrect;
    capp.p_x = (capr.r_xbot + capr.r_xtop) / 2;
    capp.p_y = (capr.r_ybot + capr.r_ytop + 1) / 2;
    if (GEO_TOUCH(&capr, clip)) {
	if (w->w_flags & WIND_BORDER)
	    GrClipBox(&capr, STYLE_BORDER);
	if ((w->w_flags & WIND_CAPTION) && (w->w_caption != NULL)) {
	    (void) GrPutText(w->w_caption, STYLE_CAPTION, &capp, 
			GEO_CENTER, GR_TEXT_DEFAULT, FALSE, &capr,
			(Rect *) NULL);
	}
    }

    if ((w->w_flags & WIND_BORDER) != 0)
    {
	/* right border */
	r = w->w_allArea;
	r.r_xbot = w->w_allArea.r_xtop - RIGHT_BORDER(w) + GrPixelCorrect;
	r.r_ytop = w->w_allArea.r_ytop - TOP_BORDER(w);
	if (GEO_TOUCH(&r, clip)) GrClipBox(&r, STYLE_BORDER);

	if ((w->w_flags & WIND_SCROLLBARS) == 0)
	{
	    /* windows without scroll bars */

	    /* left border */
	    r = w->w_allArea;
	    r.r_xtop = w->w_allArea.r_xbot + LEFT_BORDER(w) - GrPixelCorrect;
	    r.r_ytop = w->w_allArea.r_ytop - TOP_BORDER(w);
	    if (GEO_TOUCH(&r, clip)) GrClipBox(&r, STYLE_BORDER);

	    /* bottom border */
	    r = w->w_allArea;
	    r.r_ytop = w->w_allArea.r_ybot + BOT_BORDER(w) - GrPixelCorrect;
	    if (GEO_TOUCH(&r, clip)) GrClipBox(&r, STYLE_BORDER);
	}
    }

    if ((w->w_flags & WIND_SCROLLBARS) != 0)
    {
	/* windows with scroll bars */

	/* left vertical lines */
	r = w->w_allArea;
	r.r_ytop = w->w_allArea.r_ytop - TOP_BORDER(w);
	r.r_xtop = r.r_xbot + THIN_LINE - GrPixelCorrect;
	if (GEO_TOUCH(&r, clip)) GrClipBox(&r, STYLE_BORDER);
	r.r_xbot += WindScrollBarWidth + THIN_LINE;
	r.r_xtop = r.r_xbot + THIN_LINE - GrPixelCorrect;
	if (GEO_TOUCH(&r, clip)) GrClipBox(&r, STYLE_BORDER);

	/* bottom horizontal lines */
	r = w->w_allArea;
	r.r_ytop = r.r_ybot + THIN_LINE - GrPixelCorrect;
	if (GEO_TOUCH(&r, clip)) GrClipBox(&r, STYLE_BORDER);
	r.r_ybot += WindScrollBarWidth + THIN_LINE;
	r.r_ytop = r.r_ybot + THIN_LINE - GrPixelCorrect;
	if (GEO_TOUCH(&r, clip)) GrClipBox(&r, STYLE_BORDER);

	/* scroll bars */
	windBarLocations(w, &leftBar, &botBar, &up, &down, &right, &left, &zoom);
	GrClipBox(&leftBar, STYLE_CAPTION);
	GrClipBox(&botBar, STYLE_CAPTION);

	if (w->w_bbox == NULL) {
	    TxError("Warning:  scroll bars but no w->w_bbox!\n");
	    TxError("Report this to a magic implementer.\n");
	    goto leave;
	};

	/* left scroll bar */
	bar = MAX(1, leftBar.r_ytop - leftBar.r_ybot + 1);
	bbox = MAX(1, w->w_bbox->r_ytop - w->w_bbox->r_ybot + 1);
	viewl = w->w_surfaceArea.r_ybot - w->w_bbox->r_ybot + 1;
	viewu = w->w_surfaceArea.r_ytop - w->w_bbox->r_ybot + 1;
	leftElev.r_xbot = leftBar.r_xbot + 2;
	leftElev.r_xtop = leftBar.r_xtop - 3 + GrPixelCorrect;
	leftElev.r_ybot = (bar * viewl) / bbox + leftBar.r_ybot;
	leftElev.r_ytop = (bar * viewu) / bbox + leftBar.r_ybot;
	leftElev.r_ytop = MIN(leftElev.r_ytop, leftBar.r_ytop - 2);
	leftElev.r_ybot = MIN(leftElev.r_ybot, leftElev.r_ytop - 3);
	leftElev.r_ybot = MAX(leftElev.r_ybot, leftBar.r_ybot + 2);
	leftElev.r_ytop = MAX(leftElev.r_ytop, leftElev.r_ybot + 1
		+ GrPixelCorrect + GrPixelCorrect);
	GrClipBox(&leftElev, STYLE_ELEVATOR);
	r.r_xbot = leftBar.r_xbot;
	r.r_xtop = leftBar.r_xtop;
	r.r_ybot = leftBar.r_ybot - THIN_LINE;
	r.r_ytop = leftBar.r_ybot - GrPixelCorrect;
	if (GEO_TOUCH(&r, clip)) GrClipBox(&r, STYLE_BORDER);
	r.r_ybot = leftBar.r_ytop + GrPixelCorrect;
	r.r_ytop = leftBar.r_ytop + THIN_LINE; 
	if (GEO_TOUCH(&r, clip)) GrClipBox(&r, STYLE_BORDER);


	/* bottom scroll bar */
	bar = MAX(1, botBar.r_xtop - botBar.r_xbot + 1);
	bbox = MAX(1, w->w_bbox->r_xtop - w->w_bbox->r_xbot + 1);
	viewl = w->w_surfaceArea.r_xbot - w->w_bbox->r_xbot + 1;
	viewu = w->w_surfaceArea.r_xtop - w->w_bbox->r_xbot + 1;
	botElev.r_ybot = botBar.r_ybot + 2;
	botElev.r_ytop = botBar.r_ytop - 3 + GrPixelCorrect;
	botElev.r_xbot = (bar * viewl) / bbox + botBar.r_xbot;
	botElev.r_xtop = (bar * viewu) / bbox + botBar.r_xbot;
	botElev.r_xtop = MIN(botElev.r_xtop, botBar.r_xtop - 2);
	botElev.r_xbot = MIN(botElev.r_xbot, botElev.r_xtop - 3);
	botElev.r_xbot = MAX(botElev.r_xbot, botBar.r_xbot + 2);
	botElev.r_xtop = MAX(botElev.r_xtop, botElev.r_xbot + 1
		+ GrPixelCorrect + GrPixelCorrect);
	GrClipBox(&botElev, STYLE_ELEVATOR);
	r.r_ybot = botBar.r_ybot;
	r.r_ytop = botBar.r_ytop;
	r.r_xbot = botBar.r_xbot - THIN_LINE;
	r.r_xtop = botBar.r_xbot - GrPixelCorrect;
	if (GEO_TOUCH(&r, clip)) GrClipBox(&r, STYLE_BORDER);
	r.r_xbot = botBar.r_xtop + GrPixelCorrect;
	r.r_xtop = botBar.r_xtop + THIN_LINE;
	if (GEO_TOUCH(&r, clip)) GrClipBox(&r, STYLE_BORDER);

	/* icons */
	GrDrawGlyph(windGlyphs->gr_glyph[0], &(up.r_ll));
	GrDrawGlyph(windGlyphs->gr_glyph[1], &(down.r_ll));
	GrDrawGlyph(windGlyphs->gr_glyph[2], &(left.r_ll));
	GrDrawGlyph(windGlyphs->gr_glyph[3], &(right.r_ll));
	GrDrawGlyph(windGlyphs->gr_glyph[4], &(zoom.r_ll));
    }

leave:
    GrUnlock(w);
}



/*
 * ----------------------------------------------------------------------------
 * WindCaption --
 *
 *	Set the caption on the window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The caption is changed in the data structure and is redisplayed on the
 *	screen (if any of it is visible).  If anything overlaps the caption and
 *	the caption was redisplayed then the overlap material will be
 *	redisplayed.
 *
 *	If the new caption is identical to the old then only redisplay is done.
 * ----------------------------------------------------------------------------
 */

void
WindCaption(w, caption)
    MagWindow *w;
    char *caption;	/* The string that is to be copied into the caption.
			 * (The string is copied, not just pointed at.)
			 */
{
    Rect r;

    if (w->w_caption != caption)
	(void) StrDup( &(w->w_caption), caption);
    r = w->w_allArea;
    r.r_ybot = r.r_ytop - TOP_BORDER(w) + 1;
    WindAreaChanged(w, &r);
    if (GrUpdateIconPtr)(*GrUpdateIconPtr)(w,w->w_caption);
}



/*
 * ----------------------------------------------------------------------------
 * windNewView --
 *
 *	The window's view has moved -- update the scroll bars.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Records areas for redisplay.
 * ----------------------------------------------------------------------------
 */

void
windNewView(w)
    MagWindow *w;
{
    Rect leftBar, botBar, up, down, right, left, zoom;

    if ((w->w_flags & WIND_SCROLLBARS) != 0)
    {
	windBarLocations(w, &leftBar, &botBar, &up, &down, &right, &left, &zoom);
	WindAreaChanged(w, &leftBar);
	WindAreaChanged(w, &botBar);
    }
}


/*
 * ----------------------------------------------------------------------------
 * WindRedisplay --
 *
 *	Redisplay the entire window, including the border areas.
 *	Called without any window locks.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Areas of the screen will be redrawn.
 * ----------------------------------------------------------------------------
 */

void
WindRedisplay(w)
    MagWindow *w;
{
    WindAreaChanged(w, &(w->w_allArea));
}
 
/*
 * ----------------------------------------------------------------------------
 * windRedrawIcon --
 *
 *	Redraw a windows icon.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

void
windRedrawIcon(w)
    MagWindow *w;
{
    Point p;
    clientRec *cl;
    char *name;

    /* Prepare for graphics. */
    cl = (clientRec *) w->w_client;
    GrLock(w, FALSE);

    GrClipBox(&w->w_allArea, STYLE_ERASEALL);
    if (cl->w_icon != NULL) {
	/* Draw the glyph */
	GrDrawGlyph(cl->w_icon, &(w->w_allArea.r_ll));
    }

    /* Now label the icon */
    if (w->w_iconname != NULL)
	name = w->w_iconname;
    else
	name = cl->w_clientName;
    p.p_y = w->w_allArea.r_ybot;
    p.p_x = (w->w_allArea.r_xbot + w->w_allArea.r_xtop) / 2;
    GrPutText(name, STYLE_BORDER, &p, GEO_NORTH, GR_TEXT_SMALL, TRUE,
	&w->w_allArea, (Rect *) NULL);

    /* We are done */
    w->w_flags &= ~WIND_REDRAWICON;
    GrUnlock(w);
}


/*
 * ----------------------------------------------------------------------------
 * WindUpdate --
 *
 *	Update the screen areas that were previously passed to WindAreaChanged.
 *	Calls clients without any window locks.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Clients will be called to update portions of the screen.
 * ----------------------------------------------------------------------------
 */

bool WindAnotherUpdatePlease;

void
WindUpdate()
{
    clientRec *cr;
    MagWindow *w;
    TileTypeBitMask windTileMask;
    extern int windUpdateFunc();		/* Forward declaration. */
    extern int windBackgroundFunc();		/* Forward declaration. */
    Rect r;

    WindAnotherUpdatePlease = FALSE;

    /* First, if there was a SigWinch (as on a Sun160), then call the
     * graphics module so that it can record additional areas to be
     * redisplayed.
     */
    if (SigGotSigWinch) {
	SigGotSigWinch = FALSE;
	if (GrDamagedPtr != NULL) (*GrDamagedPtr)();
    }

#ifdef MAGIC_WRAPPER
    /* Honor the display redraw suspension state */
    if (GrDisplayStatus == DISPLAY_SUSPEND) return;
    GrDisplayStatus = DISPLAY_IN_PROGRESS;
    SigSetTimer(0);
#endif

    TTMaskSetOnlyType(&windTileMask, TT_ERROR_P);

    /* Make a scan through each of the windows, in order from top
     * down.  For each window, redisplay the areas of that window
     * that have changed, then erase the area of that window from
     * the redisplay plane.  Since our window areas INCLUDE their
     * border pixels on both sides, expand the area on the top and
     * right sides before erasing.  Without this expansion, and
     * corresponding hacks in WindAreaChanged and windBackgroundFunc,
     * slivers will accidentally be left undisplayed.
     */

    UndoDisable();
    for (w = windTopWindow; w != NULL; w = w->w_nextWindow)
    {
	if (w->w_flags & WIND_ISICONIC) {
	    if (w->w_flags & WIND_REDRAWICON) windRedrawIcon(w);
	} else {
	    if (w->w_redrawAreas == (ClientData)NULL)
		windCurRedrawPlane = windRedisplayArea;
	    else
		windCurRedrawPlane = (Plane *) w->w_redrawAreas;

	    (void) DBSrPaintArea((Tile *) NULL,
		windCurRedrawPlane, &w->w_allArea, &windTileMask,
		windUpdateFunc, (ClientData) w);

	    if (windCurRedrawPlane == windRedisplayArea) {
		/* Erase this window from our list, since we have redrawn it.
		 */
		r = w->w_allArea;
		r.r_xtop += 1;
		r.r_ytop += 1;
		DBPaintPlane(windRedisplayArea, &r,
		    DBStdEraseTbl(TT_ERROR_P, PL_DRC_ERROR), 
		    (PaintUndoInfo *) NULL);
	    } else {
		/* We are finished with this window's redisplay plane.  Clear 
		 * any remaining redisplay tiles, as we may have interrupted
		 * the redislay and don't want this stuff any more.
		 */
		DBClearPaintPlane(windCurRedrawPlane);
	    }
	}
    }

    if (WindPackageType == WIND_MAGIC_WINDOWS)
    {
	/* For any tiles left that intersect the screen, draw the background
	 * color (there are no windows over these tiles).
	 */
	(void) DBSrPaintArea((Tile *) NULL,
	    windRedisplayArea, &GrScreenRect, &windTileMask,
	    windBackgroundFunc, (ClientData) NULL);

	/* Clear any remaining redisplay tiles, as we may have interrupted
	 * the redislay and don't want this stuff any more.
	 */
	DBClearPaintPlane(windRedisplayArea);
    };
    
    UndoEnable();

    /* Now give the clients a chance to update anything that they wish */
    for (cr = windFirstClientRec; cr != (clientRec *) NULL; 
	    cr = cr->w_nextClient)
    {
	if (cr->w_update != NULL)
	    (*(cr->w_update)) ();
    }
    GrFlush();

#ifdef MAGIC_WRAPPER
    SigRemoveTimer();
    GrDisplayStatus = DISPLAY_IDLE;
#endif

    /* See comment in windows.h */
    if (WindAnotherUpdatePlease) WindUpdate();
}

/*
 * ----------------------------------------------------------------------------
 *
 * windUpdateFunc --
 *
 * 	This procedure is invoked during WindUpdate for each tile that
 *	intersects a particular window.  If the tile type is TT_ERROR_P
 *	then the intersection between the tile and the window is
 *	redisplayed.
 *
 * Results:
 *	Always returns 0 to keep the search from aborting.
 *
 * Side effects:
 *	Information is redisplayed on the screen.
 *
 * ----------------------------------------------------------------------------
 */

int
windUpdateFunc(tile, w)
    Tile *tile;			/* Tile in the redisplay plane. */
    MagWindow *w;			/* Window we're currently interested in. */
{
    Rect area;

    /* If this is a space tile, there's nothing to do. */

    if (TiGetType(tile) == TT_SPACE) return 0;

    TiToRect(tile, &area);
    GeoClip(&area, &w->w_allArea);
    GeoClip(&area, &GrScreenRect);
    if ((area.r_xbot > area.r_xtop) || (area.r_ybot > area.r_ytop))
	/* nothing to display */
	return 0;

    /* Skip the border stuff if it isn't going to change.  This
     * test has to be especially tricky because of the decision
     * that pixel at (0,0) extends from (0,0) up to but not including
     * (1,1).
     */

    if (!((w->w_screenArea.r_xbot <= area.r_xbot)
	&& (w->w_screenArea.r_xtop+1 >= area.r_xtop)
	&& (w->w_screenArea.r_ybot <= area.r_ybot)
	&& (w->w_screenArea.r_ytop+1 >= area.r_ytop))) {
	/* Redisplay the border areas. */
	WindDrawBorder(w, &area);
    };

    /* Now call the client to redisplay the interior of the window. */
    if (GEO_TOUCH(&(w->w_screenArea), &area)) 
    {
	Rect clientArea;

	WindScreenToSurface(w, &area, &clientArea);
	GeoClip(&area, &w->w_screenArea);
	if ( ((clientRec *) (w->w_client))->w_redisplay != NULL)
	{
	    (*(( (clientRec *) (w->w_client))->w_redisplay))
		    (w, &clientArea, &area);
	}
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * windBackgroundFunc --
 *
 * 	Called for each tile left after redisplaying all of the windows.
 *	This procedure just draws the background colors in any remaining
 *	areas that have changed.
 *
 * Results:
 *	Always returns 0 to keep the search from aborting.
 *
 * Side effects:
 *	The background color gets drawn on the screen.
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
int
windBackgroundFunc(tile, notUsed)
    Tile *tile;
    ClientData notUsed;
{
    Rect area;

    if (TiGetType(tile) == (TileType) TT_SPACE) return 0;
    TiToRect(tile, &area);

    /* Since windows include their border pixels, we have to be
     * a bit careful.  If the upper or right edge of the area isn't
     * the edge of the screen, it is a window.  In that case, decrement
     * the coordinate so we don't overwrite the edge of the window.
     */

    if (area.r_xtop < GrScreenRect.r_xtop) area.r_xtop -= 1;
    if (area.r_ytop < GrScreenRect.r_ytop) area.r_ytop -= 1;
    GrLock(GR_LOCK_SCREEN, FALSE);
    GrClipBox(&area, STYLE_BACKGROUND);
    GrUnlock(GR_LOCK_SCREEN);
    return 0;
}
