/*
 * CMWmain.c --
 *
 * Procedures to interface the colormap editor with the window package
 * for the purposes of window creation, deletion, and modification.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/cmwind/CMWmain.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "windows/windows.h"
#include "database/database.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "utils/main.h"
#include "commands/commands.h"
#include "cmwind/cmwind.h"
#include "graphics/graphicsInt.h"
#include "graphics/graphics.h"
#include "textio/textio.h"
#include "utils/utils.h"
#include "utils/styles.h"
#include "graphics/glyphs.h"
#include "utils/malloc.h"

global WindClient CMWclientID;

/* Forward and external declarations */

extern void cmwColor();
extern void cmwSave();
extern void cmwLoad();
extern void cmwPushbutton();
extern void RGBxHSV();
extern void CMWundoInit();

/* -------------------------------------------------------------------- */

/*
 * The following is the layout of a color map window:
 *
 * +========================================================================+
 * |                         +--------------------+			    |
 * |			     |                    |			    |
 * |                         +--------------------+			    |
 * |									    |
 * |               Red   		                 Hue                |
 * |  +---+ +----------------+ +---+        +---+ +----------------+ +---+  |
 * |  | - | |                | | + |        | - | |                | | + |  |
 * |  +---+ +----------------+ +---+        +---+ +----------------+ +---+  |
 * |									    |
 * |              Green                               Saturation            |
 * |  +---+ +----------------+ +---+        +---+ +----------------+ +---+  |
 * |  | - | |                | | + |        | - | |                | | + |  |
 * |  +---+ +----------------+ +---+        +---+ +----------------+ +---+  |
 * |									    |
 * |               Blue                                 Value  
 * |  +---+ +----------------+ +---+        +---+ +----------------+ +---+  |
 * |  | - | |                | | + |        | - | |                | | + |  |
 * |  +---+ +----------------+ +---+        +---+ +----------------+ +---+  |
 * |									    |
 * +========================================================================+
 *
 * The top box displays the current color, and can be buttoned
 * to select a different color for editing, or to copy an existing
 * color into the current color.
 *
 * Each of the boxes Red, Green, Blue, etc, is referred to as a
 * "color bar".  The following table defines the location of each
 * color bar.  Each of the + and minus boxes as a "color pump",
 * which increments or decrements the particular value depending on
 * which pump is hit and which mouse button is used to hit it.
 */

ColorBar colorBars[] =
{
    "Red",	CB_RED,		STYLE_RED,	{2000,	8000,	10000,	9000},
						{2000,	9500,	10000,	10500},
    "Green",	CB_GREEN,	STYLE_GREEN,	{2000,	5000,	10000,	6000},
						{2000,	6500,	10000,	7500},
    "Blue",	CB_BLUE,	STYLE_BLUE,	{2000,	2000,	10000,	3000},
						{2000,	3500,	10000,	4500},
    "Hue",	CB_HUE,		STYLE_YELLOW,	{14000,	8000,	22000,	9000},
						{14000,	9500,	22000,	10500},
    "Saturation",  CB_SAT,	STYLE_GRAY,	{14000,	5000,	22000,	6000},
						{14000,	6500,	22000,	7500},
    "Value",	CB_VALUE,	STYLE_BROWN1,	{14000,	2000,	22000,	3000},
						{14000,	3500,	22000,	4500},
    0
};

ColorPump colorPumps[] =
{
    CB_RED,	-.0078,	  {500,	8000,	 1500,	9000},
    CB_RED,	 .0078,	{10500, 8000,	11500,	9000},
    CB_GREEN,	-.0078,	  {500,	5000,	 1500,	6000},
    CB_GREEN,	 .0078,	{10500,	5000,	11500,	6000},
    CB_BLUE,	-.0078,	  {500,	2000,	 1500,	3000},
    CB_BLUE,	 .0078,	{10500,	2000,	11500,	3000},
    CB_HUE,	-.01,	{12500,	8000,	13500,	9000},
    CB_HUE,	 .01,	{22500,	8000,	23500,	9000},
    CB_SAT,	-.01,	{12500,	5000,	13500,	6000},
    CB_SAT,	 .01,	{22500,	5000,	23500,	6000},
    CB_VALUE,	-.01,	{12500,	2000,	13500,	3000},
    CB_VALUE,	 .01,	{22500,	2000,	23500,	3000},
    -1
};

Rect cmwCurrentColorArea = {{6000, 12000}, {18000, 15000}};
Rect cmwCurrentColorTextBox = {{6000, 15500}, {18000, 16500}};
char *cmwCurrentColorText = "Color Being Edited";

/* Bounding rectangle for entire window */

Rect colorWindowRect = {{0, 1500}, {24000, 17000}};

/*
 * ----------------------------------------------------------------------------
 *
 * CMWcreate --
 *
 * A new window has been created.  Create and initialize the needed 
 * structures.
 *
 * Results:
 *	FALSE if we have too many windows, TRUE otherwise.
 *
 * Side effects:
 *	Initialize the window to be editing the background color.
 *
 * ----------------------------------------------------------------------------
 */

bool
CMWcreate(window, argc, argv)
    MagWindow *window;
    int argc;
    char *argv[];
{
    CMWclientRec *crec;
    int color;

    crec = (CMWclientRec *) mallocMagic(sizeof(CMWclientRec));
    window->w_clientData = (ClientData) crec;
    if (argc > 0) sscanf(argv[0], "%o", &color);
    else color = 0;
    color &= 0377;
    window->w_flags &= ~(WIND_SCROLLABLE | WIND_SCROLLBARS | WIND_CAPTION);
    window->w_frameArea.r_xtop = GrScreenRect.r_xtop;
    window->w_frameArea.r_xbot = GrScreenRect.r_xtop - 250;
    window->w_frameArea.r_ybot = 0;
    window->w_frameArea.r_ytop = 200;
    WindSetWindowAreas(window);
    CMWloadWindow(window, color);
    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CMWdelete --
 *
 * Clean up the data structures before deleting a window.
 *
 * Results:
 *	TRUE if we really want to delete the window, FALSE otherwise.
 *
 * Side effects:
 *	A CMWclientRec is freed.
 *
 * ----------------------------------------------------------------------------
 */

bool
CMWdelete(window)
    MagWindow *window;
{
    CMWclientRec *cr;

    cr = (CMWclientRec *) window->w_clientData;
    if (cr->cmw_cname)
	freeMagic(cr->cmw_cname);
    freeMagic((char *) cr);
    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CMWreposition --
 *
 * A window has moved -- center it.
 *
 * Results:
 *	none.
 *
 * Side effects:
 *	May change the window's view.
 *
 * ----------------------------------------------------------------------------
 */

void
CMWreposition(window, newScreenArea, final)
    MagWindow *window;
    Rect *newScreenArea;
    bool final;
{
    if (final)
	WindMove(window, &colorWindowRect);
}


/*
 * ----------------------------------------------------------------------------
 *
 * CMWredisplay --
 *
 * Redisplay a portion of a window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Redisplay is done.
 * ----------------------------------------------------------------------------
 */

void
CMWredisplay(w, rootArea, clipArea)
    MagWindow *w;		/* The window containing the area. */
    Rect *rootArea;	/* Redisplay area in surface coordinates. */
    Rect *clipArea;	/* An area on the screen to clip to. */
{
    CMWclientRec *cr;
    ColorBar *cb;
    ColorPump *cp;
    Rect rect, screenR;
    Point screenP;
    double values[6], x;
    int r, g, b;
    char *string;

    GrLock(w, TRUE);

    cr = (CMWclientRec *) w->w_clientData;

    /* Erase previous information */

    WindSurfaceToScreen(w, rootArea, &screenR);
    GrClipBox(clipArea, STYLE_ERASEALL);

    /* Get current color map values */
    (void) GrGetColor(cr->cmw_color, &r, &g, &b);
    values[CB_RED] = ( ((double) r) + 0.5 ) / 255.0;
    values[CB_GREEN] = ( ((double) g) + 0.5 ) / 255.0;
    values[CB_BLUE] = ( ((double) b) + 0.5 ) / 255.0;
    RGBxHSV(values[CB_RED], values[CB_GREEN], values[CB_BLUE],
	   &values[CB_HUE], &values[CB_SAT], &values[CB_VALUE]);

    /* Redisplay each bar */
    for (cb = colorBars; cb->cb_name; cb++)
    {
	x = values[cb->cb_code];

	/* Display the color bar and its bounding box. */

	if (GEO_TOUCH(&cb->cb_rect, rootArea))
	{
	    rect.r_xbot = cb->cb_rect.r_xbot;
	    rect.r_ybot = cb->cb_rect.r_ybot;
	    rect.r_ytop = cb->cb_rect.r_ytop;
	    rect.r_xtop = cb->cb_rect.r_xbot +
		(int) (x * (double) (cb->cb_rect.r_xtop - cb->cb_rect.r_xbot));
	    WindSurfaceToScreen(w, &rect, &screenR);
	    GrClipBox(&screenR, cb->cb_style);

	    WindSurfaceToScreen(w, &cb->cb_rect, &screenR);
	    GrClipBox(&screenR, STYLE_BBOX);
	}

	/* Display bar name */
	if (GEO_TOUCH(&cb->cb_textRect, rootArea))
	{
	    WindSurfaceToScreen(w, &cb->cb_textRect, &screenR);
	    screenP.p_x = (screenR.r_xbot + screenR.r_xtop)/2;
	    screenP.p_y = (screenR.r_ybot + screenR.r_ytop)/2;
	    GeoClip(&screenR, &GrScreenRect);
	    GrPutText(cb->cb_name, STYLE_BBOX, &screenP,
		GEO_CENTER, GR_TEXT_LARGE,
		TRUE, &screenR, (Rect *) NULL);
	}
    }

    /* Redisplay each of the color pumps. */

    for (cp = colorPumps; cp->cp_code >= 0; cp++)
    {
	if (GEO_TOUCH(&cp->cp_rect, rootArea))
	{
	    WindSurfaceToScreen(w, &cp->cp_rect, &screenR);
	    GrClipBox(&screenR, STYLE_BBOX);
	    screenP.p_x = (screenR.r_xbot + screenR.r_xtop)/2;
	    screenP.p_y = (screenR.r_ybot + screenR.r_ytop)/2;
	    if (cp->cp_amount < 0) string = "-";
	    else string = "+";
	    GeoClip(&screenR, &GrScreenRect);
	    GrPutText(string, STYLE_BBOX, &screenP, GEO_CENTER, GR_TEXT_LARGE,
		    TRUE, &screenR, (Rect *) NULL);
	}
    }

    /* Redisplay the box at the top that shows the color being edited.
     * This is a bit tricky, since we don't know the style corresponding
     * to that color.  Instead, change the color in a particular style
     * reserved for our own use.
     */
    
    if (GEO_TOUCH(&cmwCurrentColorArea, rootArea))
    {
	GrStyleTable[STYLE_CMEDIT].color = cr->cmw_color;
	WindSurfaceToScreen(w, &cmwCurrentColorArea, &screenR);
	GrClipBox(&screenR, STYLE_CMEDIT);
	GrClipBox(&screenR, STYLE_BBOX);
    }
    if (GEO_TOUCH(&cmwCurrentColorTextBox, rootArea))
    {
	WindSurfaceToScreen(w, &cmwCurrentColorTextBox, &screenR);
	screenP.p_x = (screenR.r_xbot + screenR.r_xtop)/2;
	screenP.p_y = (screenR.r_ybot + screenR.r_ytop)/2;
	GeoClip(&screenR, &GrScreenRect);
	GrPutText(cmwCurrentColorText, STYLE_BBOX, &screenP,
	    GEO_CENTER, GR_TEXT_LARGE,
	    TRUE, &screenR, (Rect *) NULL);
    }

    GrUnlock(w);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CMWloadWindow --
 *
 * Load the window with a new color.
 * A color name of NULL causes us to edit the background color.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Loads a color into the window.
 *
 * ----------------------------------------------------------------------------
 */

void
CMWloadWindow(w, color)
    MagWindow *w;	/* Identifies window to which color is to be bound */
    int color;		/* New color to be bound to this window. */
{
    CMWclientRec *cr = (CMWclientRec *) w->w_clientData;
    char caption[40];

    cr->cmw_color = color;
    cr->cmw_cname = (char *) NULL;

    sprintf(caption, "COLOR = 0%o", cr->cmw_color);
    WindCaption(w, caption);
    WindAreaChanged(w, (Rect *) NULL);

    /* move the contents of the window so the color bars show */
    WindMove(w, &colorWindowRect);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CMWinit --
 *
 *	Add the color-map window client to the window module.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Add ourselves as a client to the window package.
 *
 * ----------------------------------------------------------------------------
 */

void
CMWinit()
{
    CMWclientID = WindAddClient("color", CMWcreate, CMWdelete,
			CMWredisplay, CMWcommand,
			(void(*)()) NULL,
			CMWCheckWritten,
			CMWreposition,
			(GrGlyph *) NULL);
    CMWundoInit();

    /* Register commands with the client */

    WindAddCommand(CMWclientID,
	"pushbutton button	invoke a button press in the color window",
	 cmwPushbutton, FALSE);
    WindAddCommand(CMWclientID,
       "color [color-#]	        specify color to edit, or print current intensities",
	cmwColor, FALSE);
    WindAddCommand(CMWclientID,
       "load [techStyle displayStyle monitorType]\n\
                        load new color map techStyle.displayStyle.monitorType",
	cmwLoad, FALSE);
    WindAddCommand(CMWclientID,
       "save [techStyle displayStyle monitorType]\n\
                        save color map to techStyle.displayStyle.monitorType",
	cmwSave, FALSE);
}
