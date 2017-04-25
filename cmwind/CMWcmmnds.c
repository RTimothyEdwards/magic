/*
 * CMWcommands.c --
 *
 * Commands for the color map editor.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/cmwind/CMWcmmnds.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "windows/windows.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "dbwind/dbwind.h"
#include "utils/main.h"
#include "commands/commands.h"
#include "cmwind/cmwind.h"
#include "graphics/graphics.h"
#include "textio/textio.h"
#include "utils/utils.h"
#include "utils/styles.h"
#include "textio/txcommands.h"
#include "utils/undo.h"

/* Forward declarations: */

extern void cmwButtonUp(), cmwButtonDown();
extern void cbUpdate();
extern void RGBxHSV();
extern void HSVxRGB();

/* If a button is pressed over the top box in the window, which
 * displays the current color, we must save the window in which
 * it was pressed for use when the button is released.  We also
 * set a flag so we remember to process the button release.
 */

MagWindow *cmwWindow;
bool cmwWatchButtonUp;

/* The following variable keeps track of whether or not the color
 * map has been modified, so that we can warn the user if he attempts
 * to exit or load a new color map without saving the changes.
 */

bool cmwModified = FALSE;


/*
 * ----------------------------------------------------------------------------
 *
 * CMWcommand --
 *
 *	This procedure is invoked by the window package whenever a
 *	button is pushed or a command is typed while the cursor is
 *	over a colormap window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A color may be changed, depending on the command.
 *
 * ----------------------------------------------------------------------------
 */

void
CMWcommand(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    switch (cmd->tx_button)
    {
        case TX_NO_BUTTON:
	    WindExecute(w, CMWclientID, cmd);
	    break;
        case TX_LEFT_BUTTON:
        case TX_RIGHT_BUTTON:
        case TX_MIDDLE_BUTTON:
	    switch (cmd->tx_buttonAction)
	    {
		case TX_BUTTON_UP:
		    cmwButtonUp(w, &(cmd->tx_p), cmd->tx_button);
		    break;
		case TX_BUTTON_DOWN:
		    cmwButtonDown(w, &(cmd->tx_p), cmd->tx_button);
		    break;
	    }
	    break;
	default:
	    ASSERT(FALSE, "CMWcommand");
    }

    UndoNext();
}

/*
 * ----------------------------------------------------------------------------
 *
 * cmwButtonDown --
 *
 * Process the button command to change a color value.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May update a color value.  Depends on where the button is
 *	pushed.  If a button is pushed over a color bar, then that
 *	bar is updated to fall where the cursor is.  If a button
 *	is pushed over a color pump, the value is incremented or
 *	decremented, depending on which pump is hit.  If the left
 *	button is pushed then a small increment is made (the value
 *	in the associated pump record).  If one of the other buttons
 *	is pushed, an increment 5 times as great is made.
 *
 * ----------------------------------------------------------------------------
 */

void
cmwButtonDown(w, p, button)
    MagWindow *w;		/* Window where the button was pressed. */
    Point *p;
    int button;
{
    ColorBar *cb;
    ColorPump *cp;
    Point surfacePoint;
    int x;
    double dx;

    if (w == NULL) return;

    WindPointToSurface(w, p, &surfacePoint, (Rect *) NULL);

    /* See if the cursor is over a color bar. */

    for (cb = colorBars; cb->cb_name; cb++)
    {
	if (GEO_ENCLOSE(&surfacePoint, &cb->cb_rect))
	{
	    x = surfacePoint.p_x;
	    x = MAX(x, cb->cb_rect.r_xbot);
	    x = MIN(x, cb->cb_rect.r_xtop);
	    dx = (double) (x - cb->cb_rect.r_xbot)
		/ (double) (cb->cb_rect.r_xtop - cb->cb_rect.r_xbot);
	    cbUpdate(w, cb->cb_code, dx, TRUE);
	    return;
	}
    }

    /* See if the cursor is over a color pump. */

    for (cp = colorPumps; cp->cp_code >= 0; cp++)
    {
	if (GEO_ENCLOSE(&surfacePoint, &cp->cp_rect))
	{
	    if (button == TX_LEFT_BUTTON)
	        cbUpdate(w, cp->cp_code, cp->cp_amount, FALSE);
	    else
		cbUpdate(w, cp->cp_code, (double) 5*cp->cp_amount, FALSE);
	    return;
	}
    }

    /* Note that the "color picker" function can't work as implemented
     * because this function is registered only with the color window,
     * so it cannot be called with a different window "w".  So, we can
     * only disable the button functions when pushed over the "current
     * color area" rectangle.
     */

#ifndef MAGIC_WRAPPER

    /* See if the cursor is over the current color area.  If so, remember
     * the fact and wait for the button to be released.
     */
    
    if (GEO_ENCLOSE(&surfacePoint, &cmwCurrentColorArea))
    {
	cmwWindow = w;
	cmwWatchButtonUp = TRUE;
	if (button == TX_LEFT_BUTTON)
	    TxPrintf("Release button over new color to edit.\n");
	else TxPrintf("Release button over color to copy.\n");
    }
#endif

}

/*
 * ----------------------------------------------------------------------------
 *
 * cmwButtonUp --
 *
 * 	This procedure handles button releases.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The current color, or its value, may be changed.
 *
 * ----------------------------------------------------------------------------
 */

void
cmwButtonUp(w, p, button)
    MagWindow *w;	/* Window where the button was released */
    Point *p;		/* Point where button was released, in window coords.*/
    int button;		/* Button that was released. */
{
    CMWclientRec *crec;
    int r, g, b, color, oldR, oldG, oldB;
    extern int cmwRedisplayFunc();

    /* If the button wasn't depressed over the top box in the window
     * (the one displaying the current color), then we ignore the
     * button release.
     */
    
    if (!cmwWatchButtonUp) return;
    cmwWatchButtonUp = FALSE;

    /* If it's the left button, change the color being edited to the
     * one at the pixel underneath the cursor.  Otherwise, copy the
     * color values from the pixel underneath the cursor to the current
     * color.
     */
    
    /* Read the pixel from the window that was underneath the cursor when
     * the button was released.
     */
    color = GrReadPixel(w, p->p_x, p->p_y);
    if (color < 0)
    {
	TxError("Couldn't read that pixel!\n");
	color = 0;
    }

    if (button == TX_LEFT_BUTTON)
	CMWloadWindow(cmwWindow, color);
    else
    {
	crec = (CMWclientRec *) cmwWindow->w_clientData;
	(void) GrGetColor(color, &r, &g, &b);
	(void) GrGetColor(crec->cmw_color, &oldR, &oldG, &oldB);
	(void) GrPutColor(crec->cmw_color, r, g, b);
	cmwModified = TRUE;

	/* Record old values for undo. */
	cmwUndoColor(crec->cmw_color, oldR, oldG, oldB, r, g, b);

	/* Mark all windows containing this color as modified */
	(void) WindSearch(CMWclientID, (ClientData) NULL, (Rect *) NULL,
		cmwRedisplayFunc, (ClientData)(pointertype) crec->cmw_color);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * cmwPushbutton --
 *
 * Usage:
 *	pushbutton <button>
 *
 * where button is "left", "middle", or "right".
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Performs the default action associated with the button event.
 *
 * ----------------------------------------------------------------------------
 */

void
cmwPushbutton(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int button;
    static char *cmwButton[] = {"left", "middle", "right", NULL};

    if (cmd->tx_argc != 2)
    {
	TxError("Usage: pushbutton <button>\n");
	return;
    }
    button = Lookup(cmd->tx_argv[1], cmwButton);
    if (button < 0)
    {
	TxError("Argument \"button\" must be one of "
		"\"left\", \"middle\", or \"right\".\n");
	return;
    }
    else switch(button)
    {
	case 0:
	    cmd->tx_button = TX_LEFT_BUTTON;
	    break;
	case 1:
	    cmd->tx_button = TX_MIDDLE_BUTTON;
	    break;
	case 2:
	    cmd->tx_button = TX_RIGHT_BUTTON;
	    break;
    }

    cmd->tx_buttonAction = TX_BUTTON_DOWN;
    CMWcommand(w, cmd);
    cmwWatchButtonUp = FALSE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * cmwColor --
 *
 * Load a color map window with a particular color.
 *
 * Usage:
 *	color color-#|next|last|get|rgb
 *
 * where color-# is the octal number of the color to be edited.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Loads a new color into this window.
 *
 * ----------------------------------------------------------------------------
 */

void
cmwColor(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int color, r, g, b;
    CMWclientRec *crec;

    if (cmd->tx_argc == 1)
    {
	crec = (CMWclientRec *) w->w_clientData;
	GrGetColor(crec->cmw_color, &r, &g, &b);
	TxPrintf("Current color is %o octal (%d decimal) "
			"(red = %d, green = %d, blue = %d)\n",
	    crec->cmw_color, crec->cmw_color, r, g, b);
    }
    else if (cmd->tx_argc == 2)
    {
	if (sscanf(cmd->tx_argv[1], "%i", &color) == 0)
	{
	    /* Invalid color---allow keywords "next" and "last" */
	    crec = (CMWclientRec *) w->w_clientData;
	    if (!strncmp(cmd->tx_argv[1], "next", 4))
	    {
		color = crec->cmw_color + 1;
		if (color >= GrNumColors)
		    color = 0;
	    }
	    else if (!strncmp(cmd->tx_argv[1], "last", 4))
	    {
		color = crec->cmw_color - 1;
		if (color < 0)
		    color = GrNumColors - 1;
	    }
	    else if (!strncmp(cmd->tx_argv[1], "get", 3))
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewIntObj(crec->cmw_color));
#else
		TxPrintf("Color index %d\n", crec->cmw_color);
#endif
		return;
	    }
	    else if (!strncmp(cmd->tx_argv[1], "rgb", 3))
	    {
#ifdef MAGIC_WRAPPER
		Tcl_Obj *cobj = Tcl_NewListObj(0, NULL);
		GrGetColor(crec->cmw_color, &r, &g, &b);
		Tcl_ListObjAppendElement(magicinterp, cobj, Tcl_NewIntObj(r));
		Tcl_ListObjAppendElement(magicinterp, cobj, Tcl_NewIntObj(g));
		Tcl_ListObjAppendElement(magicinterp, cobj, Tcl_NewIntObj(b));
		Tcl_SetObjResult(magicinterp, cobj);
#else
		GrGetColor(crec->cmw_color, &r, &g, &b);
		TxPrintf("Color RGB=%d %d %d\n", r, g, b);
#endif
		return;
	    }
	    else
	    {
		TxError("Usage: color [#|next|last|get|rgb]\n");
		return;
	    }
	}
	if (color < 0 || color >= GrNumColors)
	{
	    TxError("The colormap only has values from 0 to %d (decimal).\n",
			(GrNumColors - 1));
	    return;
	}
	CMWloadWindow(w, color);
    }
    else 
	TxError("Usage: color [#|next|last|get|rgb]\n");
}

/*
 * ----------------------------------------------------------------------------
 *
 * cmwSave --
 *
 * Save the color map to a disk file.
 *
 * Usage:
 *	save [techStyle displayStyle monitorType]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes the color map out to disk.
 *
 * ----------------------------------------------------------------------------
 */

void
cmwSave(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    bool ok;
    if ((cmd->tx_argc != 1) && (cmd->tx_argc != 4))
    {
	TxError("Usage: %s [techStyle displayStyle monitorType]\n",
		cmd->tx_argv[0]);
	return;
    }
    if (cmd->tx_argc > 1)
    {
	ok = GrSaveCMap(cmd->tx_argv[1], cmd->tx_argv[2],
		cmd->tx_argv[3], ".", SysLibPath);
    }
    else
    {
	ok = GrSaveCMap(DBWStyleType, (char *) NULL, MainMonType,
		".", SysLibPath);
    }
    if (ok) cmwModified = FALSE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * cmwLoad --
 *
 * Load a new color map from a disk file.
 *
 * Usage:
 *	load [file]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A new color map is read from disk.  If the file name isn't
 *	given, the current technology, display type, etc. are used.
 *
 * ----------------------------------------------------------------------------
 */

void
cmwLoad(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    if ((cmd->tx_argc != 1) && (cmd->tx_argc != 4))
    {
	TxError("Usage: %s [techStyle displayStyle monitorType]\n",
		cmd->tx_argv[0]);
	return;
    }
    if (!CMWCheckWritten()) return;
    if (cmd->tx_argc == 4)
    {
	(void) GrReadCMap(cmd->tx_argv[1], cmd->tx_argv[2],
		cmd->tx_argv[3], ".", SysLibPath);
    }
    else (void) GrReadCMap(DBWStyleType, (char *) NULL,
	   MainMonType, ".", SysLibPath);
}

/*
 * ----------------------------------------------------------------------------
 *
 * cbUpdate --
 *
 * Update the value stored with a particular color bar in
 * this window.  This is a low-level routine that does all
 * the dirty work of updating colors.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The color in window w is changed by either replacing one
 *	of its components (replace == TRUE), or by incrementing
 *	one of its components.  All windows containing this color
 *	are forced to be redisplayed.
 *
 * ----------------------------------------------------------------------------
 */

void
cbUpdate(w, code, x, replace)
    MagWindow *w;		/* Window whose color is to be changed. */
    int code;		/* Indicates which color component to change. */
    double x;		/* Gives increment or new value for color. */
    int replace;	/* TRUE means replace component with x, FALSE
			 * means increment component by x.
			 */
{
    CMWclientRec *cr = (CMWclientRec *) w->w_clientData;
    double values[6];
    int r, g, b, nr, ng, nb;
    extern int cmwRedisplayFunc();

    /* Get current color map values */
    (void) GrGetColor(cr->cmw_color, &r, &g, &b);
    values[CB_RED] =  ((double) r) / 255.0;
    values[CB_GREEN] = ((double) g) / 255.0;
    values[CB_BLUE] = ((double) b) / 255.0;
    RGBxHSV(values[CB_RED], values[CB_GREEN], values[CB_BLUE],
	   &values[CB_HUE], &values[CB_SAT], &values[CB_VALUE]);

    /* Update the color that changed */
    if (replace) values[code] = x;
    else values[code] = values[code] + x;
    if (values[code] > 1.0) values[code] = 1.0;
    if (values[code] < 0.0) values[code] = 0.0;
    switch (code)
    {
	case CB_HUE:
	case CB_SAT:
	case CB_VALUE:
	    HSVxRGB(values[CB_HUE], values[CB_SAT], values[CB_VALUE],
		   &values[CB_RED], &values[CB_GREEN], &values[CB_BLUE]);
	    break;
    }

    /* Store this entry back in the color map */
    nr = (int) (values[CB_RED] * 255.0 + 0.5);
    ng = (int) (values[CB_GREEN] * 255.0 + 0.5);
    nb = (int) (values[CB_BLUE] * 255.0 + 0.5);
    (void) GrPutColor(cr->cmw_color, nr, ng, nb);
    cmwModified = TRUE;

    /* Record an undo entry */
    cmwUndoColor(cr->cmw_color, r, g, b, nr, ng, nb);

    /* Mark all windows containing this color as modified */
    (void) WindSearch(CMWclientID, (ClientData) NULL, (Rect *) NULL,
		cmwRedisplayFunc, (ClientData)(pointertype) cr->cmw_color);

    /* Here. . . ought to mark the layout windows as modified? */
}

int
cmwRedisplayFunc(w, color)
    MagWindow *w;		/* Window that may have to be redisplayed. */
    int color;			/* If this color is in window, redisplay the
				 * color bars in the window.
				 */
{
    ColorBar *cb;
    ColorPump *cp;
    Rect screenR;
    CMWclientRec *cr = (CMWclientRec *) w->w_clientData;

    if (cr->cmw_color == color)
    {
	for (cb = colorBars; cb->cb_name; cb++)
	{
	    WindSurfaceToScreen(w, &cb->cb_rect, &screenR);
            WindAreaChanged(w, &screenR);
	}
	for (cp = colorPumps; cp->cp_code >= 0; cp++)
	{
	    WindSurfaceToScreen(w, &cp->cp_rect, &screenR);
            WindAreaChanged(w, &screenR);
	}
    }

    /* Also update the "color being edited" rectangle */
    WindSurfaceToScreen(w, &cmwCurrentColorArea, &screenR);
    WindAreaChanged(w, &screenR);

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CMWCheckWritten --
 *
 * 	Checks to see if the current color map has been modified
 *	but not written back to disk.  If so, asks user whether he
 *	cares about the changes.
 *
 * Results:
 *	Returns TRUE if the color map hasn't been modified, or if
 *	the user says he doesn't care about the changes.  Returns FALSE
 *	if the user says he cares.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
CMWCheckWritten()
{
    bool indx;
    char *prompt;
    static char *(yesno[]) = {"no", "yes", NULL};

    if (!cmwModified) return TRUE;
    prompt = TxPrintString("The color map has been modified.\n"
		"  Do you want to lose the changes? ");
    indx = TxDialog(prompt, yesno, 0);
    return indx;
}
