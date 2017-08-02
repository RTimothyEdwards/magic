/* windCmdSZ.c -
 *
 *	This file contains Magic command routines for those commands
 *	that are valid in all windows.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/windows/windCmdSZ.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>		/* for sleep() */
#include <string.h>
#include <math.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "windows/windows.h"
#include "graphics/glyphs.h"
#include "windows/windInt.h"
#include "utils/undo.h"
#include "utils/utils.h"
#include "utils/signals.h"
#include "textio/txcommands.h"
#include "utils/hash.h"
#include "database/database.h"
#include "dbwind/dbwind.h"
#include "graphics/graphics.h"


/*
 * ----------------------------------------------------------------------------
 *
 * windScrollCmd --
 *
 *	Scroll the view around
 *
 * Usage:
 *	scroll dir [amount [units]]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The window underneath the cursor is changed.
 *	Note: behavior has been changed from original.  To have "amount"
 *	parsed as a fractional scroll amount, "units" *must* be declared
 *	as "w".  Otherwise, no units implies that "amount" is an absolute
 *	value.
 *	
 *
 * ----------------------------------------------------------------------------
 */

void
windScrollCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    Rect r;
    int xsize, ysize;
    Point p;
    int pos, locargc = cmd->tx_argc;
    float amount;
    bool doFractional = FALSE;

    if ( (cmd->tx_argc < 2) || (cmd->tx_argc > 4) )
    {
	TxError("Usage: %s direction [amount [units]]\n", cmd->tx_argv[0]);
	return;
    }

    if (w == NULL)
    {
	TxError("Point to a window first.\n");
	return;
    }

    if ((w->w_flags & WIND_SCROLLABLE) == 0) {
	TxError("Sorry, can't scroll this window.\n");
	return;
    };

    pos = GeoNameToPos(cmd->tx_argv[1], FALSE, TRUE);
    if (pos < 0 || pos == GEO_CENTER)
	return;

    if (cmd->tx_argc == 2)	/* default = half-screen pan */
    {
	r = w->w_screenArea;
	amount = 0.5;
	doFractional = TRUE;
    }
    else if (cmd->tx_argc == 4)
    {
	char unitc = cmd->tx_argv[3][0];

	if (unitc == 'w')
	    r = w->w_screenArea;
	else if (unitc == 'l')
	    r = *(w->w_bbox);
	else
	{
	    TxError("Usage: %s direction [amount [units]]\n", cmd->tx_argv[0]);
	    TxError("  'units' must be one of 'w' (window) or 'l' (layout);\n");
	    return;
	}

	if (sscanf(cmd->tx_argv[2], "%f", &amount) != 1)
	{
	    TxError("Usage: %s direction [amount [units]]\n", cmd->tx_argv[0]);
	    TxError("  'amount' is a fractional value.\n");
	    return;
	}
	doFractional = TRUE;
    }

    if (doFractional)
    {
	xsize = (r.r_xtop - r.r_xbot) * amount;
	ysize = (r.r_ytop - r.r_ybot) * amount;
    }
    else
    {
	/* Alternate syntax: parse for integer coordinate amount to scroll */
	xsize = cmdParseCoord(w, cmd->tx_argv[2], TRUE, TRUE);
	ysize = cmdParseCoord(w, cmd->tx_argv[2], TRUE, FALSE);
    }

    p.p_x = 0;
    p.p_y = 0;

    switch (pos)
    {
	case GEO_NORTH:
	    p.p_y = -ysize;
	    break;
	case GEO_SOUTH:
	    p.p_y = ysize;
	    break;
	case GEO_EAST:
	    p.p_x = -xsize;
	    break;
	case GEO_WEST:
	    p.p_x = xsize;
	    break;
	case GEO_NORTHEAST:
	    p.p_x = -xsize;
	    p.p_y = -ysize;
	    break;
	case GEO_NORTHWEST:
	    p.p_x = xsize;
	    p.p_y = -ysize;
	    break;
	case GEO_SOUTHEAST:
	    p.p_x = -xsize;
	    p.p_y = ysize;
	    break;
	case GEO_SOUTHWEST:
	    p.p_x = xsize;
	    p.p_y = ysize;
	    break;
    }

    if (doFractional)
	WindScroll(w, (Point *) NULL, &p);
    else
    {
	/* Direction is reversed w/respect to above call to WindScroll() */
	p.p_x = -p.p_x;
	p.p_y = -p.p_y;
	WindScroll(w, &p, (Point *) NULL);
    }

    return;
}

/*
 * ----------------------------------------------------------------------------
 * windSetpointCmd --
 *
 *	Use the x, y specified as the location of the point tool for the
 *	next command.
 *
 * Results:
 *	None.  Under the Tcl interpreter, returns the screen and layout
 *	coordinates as a list of four integers: sx sy lx ly.
 *
 * Side effects:
 *	global variables.
 * ----------------------------------------------------------------------------
 */

void
windSetpointCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int wid;
    Point rootPoint;
#ifdef MAGIC_WRAPPER
    char *ptstr;
#endif

    if ((cmd->tx_argc != 4) && (cmd->tx_argc != 3) && (cmd->tx_argc != 1)) 
	goto usage;
    if ((cmd->tx_argc != 1) && !
	(StrIsInt(cmd->tx_argv[1]) && StrIsInt(cmd->tx_argv[2])) )
	goto usage;

    if (cmd->tx_argc == 4) {
	if (StrIsInt(cmd->tx_argv[3]))
	   wid = atoi(cmd->tx_argv[3]);
	else if (GrWindowIdPtr)
	   wid = (*GrWindowIdPtr)(cmd->tx_argv[3]);
	else
	   wid = WIND_UNKNOWN_WINDOW;
    }
    else if (w != NULL)
	wid = w->w_wid;
    else {
	windCheckOnlyWindow(&w, DBWclientID);
	if (w != NULL)
	   wid = w->w_wid;
	else
	   wid = WIND_UNKNOWN_WINDOW;
    }

    /* Ensure a valid window, if possible */
    if (w == NULL) w = WindSearchWid(wid);

    if (cmd->tx_argc == 1)
    {
	if (w != (MagWindow *) NULL) 
	{
	    WindPointToSurface(w, &cmd->tx_p, &rootPoint, (Rect *) NULL);

#ifdef MAGIC_WRAPPER
	    ptstr = (char *)Tcl_Alloc(50);
	    sprintf(ptstr, "%d %d %d %d", cmd->tx_p.p_x, cmd->tx_p.p_y,
			rootPoint.p_x, rootPoint.p_y);
	    Tcl_SetResult(magicinterp, ptstr, TCL_DYNAMIC);
#else
	    TxPrintf("Point is at screen coordinates (%d, %d) in window %d.\n",
			cmd->tx_p.p_x, cmd->tx_p.p_y, w->w_wid);
	    TxPrintf("Point is at layout coordinates (%d, %d)\n",
			rootPoint.p_x, rootPoint.p_y);
#endif
	} else {
	    TxPrintf("Point is at screen coordinates (%d, %d).\n",
			cmd->tx_p.p_x, cmd->tx_p.p_y);
	}
    }
    else {
	int yval;

	yval = atoi(cmd->tx_argv[2]);

	/* Reinterpret coordinates according to the graphics package */ 
	switch (WindPackageType)
	{
	    case WIND_X_WINDOWS:
		/* Windows have origin at lower-left corner */
		yval = w->w_allArea.r_ytop - yval;
		break;
	}
	TxSetPoint(atoi(cmd->tx_argv[1]), yval, wid);
    }
    return;

usage:
    TxError("Usage: %s [x y [window ID|name]]\n", cmd->tx_argv[0]);
}

int
windSetPrintProc(name, val)
    char *name;
    char *val;
{
    TxPrintf("%s = \"%s\"\n", name, val);
    return 0;
}


/*
 * ----------------------------------------------------------------------------
 * windSleepCmd --
 *
 *	Take a nap.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

void
windSleepCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int time;

    if (cmd->tx_argc != 2) 
    {
	TxError("Usage: %s seconds\n", cmd->tx_argv[0]);
	return;
    }

    time = atoi(cmd->tx_argv[1]);
    for ( ; time > 1; time--)
    {
	sleep(1);
	if (SigInterruptPending) return;
    }
}

#ifndef MAGIC_WRAPPER


/*
 * ----------------------------------------------------------------------------
 *
 * windSourceCmd --
 *
 * Implement the "source" command.
 * Process a file as a list of commands.
 *
 * Usage:
 *	source filename
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Whatever the commands request.
 *
 * ----------------------------------------------------------------------------
 */

void
windSourceCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    FILE *f;

    if (cmd->tx_argc != 2)
    {
	TxError("Usage: %s filename\n", cmd->tx_argv[0]);
	return;
    }

    f = PaOpen(cmd->tx_argv[1], "r", (char *) NULL, ".", 
	    SysLibPath, (char **) NULL);
    if (f == NULL)
	TxError("Couldn't read from %s.\n", cmd->tx_argv[1]);
    else {
	TxDispatch(f);
	(void) fclose(f);
    };
}

#endif

/*
 * ----------------------------------------------------------------------------
 * windSpecialOpenCmd --
 *
 *	Open a new window at the cursor position.  Give it a default size,
 *	and take the client's name from the command line.  Pass the rest of
 *	the command arguments off to the client.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

void
windSpecialOpenCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    WindClient wc;
    Rect area;
    bool haveCoords;
    char *client;

    haveCoords = FALSE;

    if (cmd->tx_argc < 2) goto usage;
    haveCoords = StrIsInt(cmd->tx_argv[1]);
    if (haveCoords && (
	(cmd->tx_argc < 6) ||
	!StrIsInt(cmd->tx_argv[2]) ||
	!StrIsInt(cmd->tx_argv[3]) ||
	!StrIsInt(cmd->tx_argv[4]) 
	)) goto usage;
    if (haveCoords)
	client = cmd->tx_argv[5];
    else
	client = cmd->tx_argv[1];

    wc = WindGetClient(client, FALSE);
    /* clients whose names begin with '*' are hidden */
    if ((wc == (WindClient) NULL) || (client[0] == '*')) goto usage;

    if (haveCoords) {
	windCheckOnlyWindow(&w, wc);

	area.r_xbot = atoi(cmd->tx_argv[1]);
	area.r_ybot = atoi(cmd->tx_argv[2]);
	area.r_xtop = MAX(atoi(cmd->tx_argv[3]), area.r_xbot + WIND_MIN_WIDTH);
	area.r_ytop = MAX(atoi(cmd->tx_argv[4]), area.r_ybot + WIND_MIN_HEIGHT);
	/* Assume that the client will print an error message if it fails */
	(void) WindCreate(wc, &area, FALSE, cmd->tx_argc - 6, cmd->tx_argv + 6);
    }
    else {
	area.r_xbot = cmd->tx_p.p_x - CREATE_WIDTH/2;
	area.r_xtop = cmd->tx_p.p_x + CREATE_WIDTH/2;
	area.r_ybot = cmd->tx_p.p_y - CREATE_HEIGHT/2;
	area.r_ytop = cmd->tx_p.p_y + CREATE_HEIGHT/2;
	/* Assume that the client will print an error message if it fails */
	(void) WindCreate(wc, &area, TRUE, cmd->tx_argc - 2, cmd->tx_argv + 2);
    }

    return;

usage:
    TxPrintf("Usage: specialopen [leftx bottomy rightx topy] type [args]\n");
    TxPrintf("Valid window types are:\n");
    WindPrintClientList(FALSE);
    return;
}

/*
 * ----------------------------------------------------------------------------
 *  windNamesCmd --
 *
 *	Register or retrieve the name associated with the layout window
 *
 * Results:
 *	Returns the name as a Tcl string result.  If "all" is selected,
 *	or if w is NULL and cannot be determined, returns a list of all
 *	window names.  The first argument may also be a window client
 *	type, in which case only windows of that type are returned.
 *
 * Note:
 *	This routine can easily be made "generic", not Tcl-specific.
 *	However, of the graphics interfaces available, only Tcl/Tk keeps
 *	track of windows by name.
 *
 * ----------------------------------------------------------------------------
 */

void
windNamesCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    bool doforall = FALSE;
    WindClient wc = (WindClient)NULL;
    MagWindow *sw;

    if (cmd->tx_argc > 2)
    {
	TxError("Usage:  windownames [all | client_type]\n");
	return;
    }
    if (cmd->tx_argc == 2)
    {
	if (!strncmp(cmd->tx_argv[1], "all", 3))
	    doforall = TRUE;
#ifndef THREE_D
	else if (!strncmp(cmd->tx_argv[1], "wind3d", 6))
	{
	    return;	// do nothing
	}
#endif 	/* THREE_D */
	else
	{
	    wc = WindGetClient(cmd->tx_argv[1], FALSE);
	    if (wc == (WindClient) NULL)
	    {
		TxError("Usage:  windownames [all | client_type]\n");
		TxPrintf("Valid window types are:\n");
		WindPrintClientList(FALSE);
		return;
	    }
	    doforall = TRUE;
	}
    }

    if (cmd->tx_argc == 1)
    {
	wc = DBWclientID;
	windCheckOnlyWindow(&w, wc);
	if (w == (MagWindow *)NULL)
	    doforall = TRUE;
    }

#ifdef MAGIC_WRAPPER
    if (doforall == TRUE)
    {
	Tcl_Obj *tlist;

	tlist = Tcl_NewListObj(0, NULL);
	for (sw = windTopWindow; sw != NULL; sw = sw->w_nextWindow)
	    if ((wc == NULL) || (sw->w_client == wc))
	    {
		if (GrWindowNamePtr)
		    Tcl_ListObjAppendElement(magicinterp, tlist,
				Tcl_NewStringObj((*GrWindowNamePtr)(sw), -1));
		else
		    Tcl_ListObjAppendElement(magicinterp, tlist,
				Tcl_NewIntObj(sw->w_wid));
	    }
	Tcl_SetObjResult(magicinterp, tlist);
    }
    else
    {
	if (GrWindowNamePtr)
	    Tcl_SetResult(magicinterp, (*GrWindowNamePtr)(w), NULL);
	else
	    Tcl_SetObjResult(magicinterp, Tcl_NewIntObj(w->w_wid));
    }
#else
    if (doforall == TRUE)
    {
	if (GrWindowNamePtr)
	    TxPrintf("Window ID	Window Name\n");
	else
	    TxPrintf("Window ID\n");
	for (sw = windTopWindow; sw != NULL; sw = sw->w_nextWindow)
	    if ((wc == (WindClient)NULL) || (sw->w_client == wc))
	    {
		if (GrWindowNamePtr)
		    TxPrintf("%d    	%s\n", sw->w_wid, (*GrWindowNamePtr)(sw));
		else
		    TxPrintf("%d\n", sw->w_wid);
	    }
    }
    else
    {
	if (GrWindowNamePtr)
	    TxPrintf("Window ID = %d, Name = %s\n", w->w_wid,
			(*GrWindowNamePtr)(w));
	else
	    TxPrintf("Window ID = %d\n", w->w_wid);
    }
#endif /* !MAGIC_WRAPPER */
}


/*
 * ----------------------------------------------------------------------------
 * windUnderCmd --
 *
 *	Move a window underneath the other windows.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Screen updates.
 * ----------------------------------------------------------------------------
 */

void
windUnderCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    if (cmd->tx_argc != 1)
    {
	TxError("Usage: %s\n", cmd->tx_argv[0]);
    }
    if (w == NULL)
    {
	TxError("Point to a window first.\n");
	return;
    }
    WindUnder(w);
}


/*
 * ----------------------------------------------------------------------------
 *
 * windUndoCmd
 *
 * Implement the "undo" command.
 *
 * Usage:
 *	undo [count]
 *
 * If a count is supplied, the last count events are undone.  The default
 * count if none is given is 1.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Calls the undo module.
 *
 * ----------------------------------------------------------------------------
 */

void
windUndoCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int count;

    if (cmd->tx_argc > 3)
    {
	TxError("Usage: undo [count]\n");
	TxError("       undo print [count]\n");
	TxError("       undo enable|disable\n");
	return;
    }
    else if (cmd->tx_argc == 3)
    {
	if (strncmp(cmd->tx_argv[1], "print", 5))
	{
	    TxError("Usage: undo print count\n");
	    return;
	}
	else if (!StrIsInt(cmd->tx_argv[2]))
	{
	    TxError("Usage: undo print count\n");
	    return;
	}
	else
	{
	    /* Implement undo stack trace */
	    UndoStackTrace((-1) - atoi(cmd->tx_argv[2]));
	    return;
	}
    }
    else if (cmd->tx_argc == 2)
    {
	if (!StrIsInt(cmd->tx_argv[1]))
	{
	    if (!strcmp(cmd->tx_argv[1], "enable"))
		UndoEnable();
	    else if (!strcmp(cmd->tx_argv[1], "disable"))
		UndoDisable();
	    else
		TxError("Option must be a count (integer)\n");
	    return;
	}
	count = atoi(cmd->tx_argv[1]);
	if (count < 0)
	{
	    TxError("Count must be a positive integer\n");
	    return;
	}
    }
    else
	count = 1;

    if (count == 0)
    {
	UndoEnable();
    }
    else
    {
	if (UndoBackward(count) == 0)
	    TxPrintf("Nothing more to undo\n");
    }
}


/*
 * ----------------------------------------------------------------------------
 * windUpdateCmd --
 *
 *	Force an update of the graphics screen.  This is usually only called
 *	from command scripts.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Display updates.
 * ----------------------------------------------------------------------------
 */

void
windUpdateCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    if (cmd->tx_argc == 1)
	WindUpdate();
#ifdef MAGIC_WRAPPER
    else if (cmd->tx_argc > 2)
	goto badusage;
    else if (!strcmp(cmd->tx_argv[1], "suspend"))
	GrDisplayStatus = DISPLAY_SUSPEND;
    else if (!strcmp(cmd->tx_argv[1], "resume"))
	GrDisplayStatus = DISPLAY_IDLE;
    else
	goto badusage;
#else
    else if (cmd->tx_argc >= 2)
	goto badusage;
#endif
    return;

badusage:
    TxError("Usage: %s [suspend | resume]\n", cmd->tx_argv[0]);
    return;
}

/*
 * ----------------------------------------------------------------------------
 *
 * windVersionCmd --
 *
 *	Print version information.
 *
 * Usage:
 *	version
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Prints on stdout.
 *
 * ----------------------------------------------------------------------------
 */

void
windVersionCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    if (cmd->tx_argc != 1) {
	TxError("Usage: %s\n", cmd->tx_argv[0]);
	return;
    }

    TxPrintf("Version %s revision %s.  Compiled on %s.\n",
		MagicVersion, MagicRevision, MagicCompileTime);
}

/*
 * ----------------------------------------------------------------------------
 *
 * windViewCmd --
 *
 * Implement the "View" command.
 * Change the view in the selected window so everything is visible.
 *
 * Usage:
 *	view
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The window underneath the cursor is changed.
 *	In Tcl version, if supplied with the argument "get", the
 *	interpreter return value is set to the current view position.
 *
 * ----------------------------------------------------------------------------
 */

void
windViewCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    if (w == NULL)
	return;

    if (cmd->tx_argc == 1)
    {
	if ((w->w_flags & WIND_SCROLLABLE) == 0) {
	    TxError("Sorry, can't zoom out this window.\n");
	    return;
	}
	WindView(w);
    }
    else if (cmd->tx_argc == 2)
    {
#ifdef MAGIC_WRAPPER
	Tcl_Obj *listxy, *fval;
	
	listxy = Tcl_NewListObj(0, NULL);
#endif

	if (!strncmp(cmd->tx_argv[1], "get", 3))
	{
#ifndef MAGIC_WRAPPER
	    TxPrintf("(%d, %d) to (%d, %d)\n", 
			w->w_surfaceArea.r_xbot, w->w_surfaceArea.r_ybot,
			w->w_surfaceArea.r_xtop, w->w_surfaceArea.r_ytop);
#else
	    Tcl_ListObjAppendElement(magicinterp, listxy,
			Tcl_NewIntObj((int)w->w_surfaceArea.r_xbot));
	    Tcl_ListObjAppendElement(magicinterp, listxy,
			Tcl_NewIntObj((int)w->w_surfaceArea.r_ybot));
	    Tcl_ListObjAppendElement(magicinterp, listxy,
			Tcl_NewIntObj((int)w->w_surfaceArea.r_xtop));
	    Tcl_ListObjAppendElement(magicinterp, listxy,
			Tcl_NewIntObj((int)w->w_surfaceArea.r_ytop));
	    Tcl_SetObjResult(magicinterp, listxy);
#endif
	}
	else if (!strncmp(cmd->tx_argv[1], "bbox", 4))
	{
#ifndef MAGIC_WRAPPER
	    TxPrintf("(%d, %d) to (%d, %d)\n", 
			w->w_bbox->r_xbot, w->w_bbox->r_ybot,
			w->w_bbox->r_xtop, w->w_bbox->r_ytop);
#else
	    Tcl_ListObjAppendElement(magicinterp, listxy,
			Tcl_NewIntObj((int)w->w_bbox->r_xbot));
	    Tcl_ListObjAppendElement(magicinterp, listxy,
			Tcl_NewIntObj((int)w->w_bbox->r_ybot));
	    Tcl_ListObjAppendElement(magicinterp, listxy,
			Tcl_NewIntObj((int)w->w_bbox->r_xtop));
	    Tcl_ListObjAppendElement(magicinterp, listxy,
			Tcl_NewIntObj((int)w->w_bbox->r_ytop));
	    Tcl_SetObjResult(magicinterp, listxy);
#endif
	}
	else
	{
	    char *sptr, *pptr;
	    Rect r;

	    // Parse out coordinates where all coordinates have been
	    // put into a single string argument, as happens when the
	    // coordinates are a Tcl list, e.g., from "[box values]"

	    sptr = cmd->tx_argv[1];
	    if ((pptr = strchr(sptr, ' ')) == NULL) return;
	    *pptr++ = '\0';
	    r.r_xbot = cmdParseCoord(w, sptr, FALSE, TRUE);

	    sptr = pptr;
	    if ((pptr = strchr(sptr, ' ')) == NULL) return;
	    *pptr++ = '\0';
	    r.r_ybot = cmdParseCoord(w, sptr, FALSE, TRUE);

	    sptr = pptr;
	    if ((pptr = strchr(sptr, ' ')) == NULL) return;
	    *pptr++ = '\0';
	    r.r_xtop = cmdParseCoord(w, sptr, FALSE, TRUE);
	    r.r_ytop = cmdParseCoord(w, pptr, FALSE, TRUE);

	    /* Redisplay */
	    WindMove(w, &r);
	}
    }
    else if (cmd->tx_argc == 5)
    {
	Rect r;
	r.r_xbot = cmdParseCoord(w, cmd->tx_argv[1], FALSE, TRUE);
        r.r_ybot = cmdParseCoord(w, cmd->tx_argv[2], FALSE, FALSE);
        r.r_xtop = cmdParseCoord(w, cmd->tx_argv[3], FALSE, TRUE);
        r.r_ytop = cmdParseCoord(w, cmd->tx_argv[4], FALSE, FALSE);

	/* Redisplay */
	WindMove(w, &r);
    }
    else
    {
	TxError("Usage: view [get|bbox|llx lly urx ury]\n");
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * windXviewCmd --
 *
 * Implement the "Xview" command.
 * Change the view in the selected window so everything is visible, but
 * not expanded.
 *
 * Usage:
 *	xview
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The window underneath the cursor is changed. The root cell of the
 *	window is unexpanded.
 *
 * ----------------------------------------------------------------------------
 */

void
windXviewCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    CellUse *celluse;
    int ViewUnexpandFunc();

    if (w == NULL)
	return;

    if ((w->w_flags & WIND_SCROLLABLE) == 0) {
	TxError("Sorry, can't zoom out this window.\n");
	return;
    };

    celluse = (CellUse *) (w->w_surfaceID);
    DBExpandAll(celluse, &(celluse->cu_bbox),
	((DBWclientRec *)w->w_clientData)->dbw_bitmask, FALSE,
	ViewUnexpandFunc,
	(ClientData)(pointertype) (((DBWclientRec *)w->w_clientData)->dbw_bitmask));

    WindView(w);
}

/* This function is called for each cell whose expansion status changed.
 * It forces the cells area to be redisplayed, then returns 0 to keep
 * looking for more cells to unexpand.
 */

int
ViewUnexpandFunc(use, windowMask)
    CellUse *use;		/* Use that was just unexpanded. */
    int windowMask;		/* Window where it was unexpanded. */
{
    if (use->cu_parent == NULL) return 0;
    DBWAreaChanged(use->cu_parent, &use->cu_bbox, windowMask,
	    (TileTypeBitMask *) NULL);
    return 0;
}


/*
 * ----------------------------------------------------------------------------
 *
 * windScrollBarsCmd --
 *
 *	Change the flag which says whether new windows will have scroll bars.
 *
 * Usage:
 *	windscrollbars [on|off]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A flag is changed.
 *
 * ----------------------------------------------------------------------------
 */

void
windScrollBarsCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int place;
    static char *onoff[] = {"on", "off", 0};
    static bool truth[] = {TRUE, FALSE};

    if (cmd->tx_argc != 2) goto usage;

    place = Lookup(cmd->tx_argv[1], onoff);
    if (place < 0) goto usage;

    if (truth[place])
    {
	WindDefaultFlags |= WIND_SCROLLBARS;
	TxPrintf("New windows will have scroll bars.\n");
    }
    else
    {
	WindDefaultFlags &= ~WIND_SCROLLBARS;
	TxPrintf("New windows will not have scroll bars.\n");
    }
    return;

    usage:
	TxError("Usage: %s [on|off]\n", cmd->tx_argv[0]);
	return;
}

#ifndef MAGIC_WRAPPER


/*
 * ----------------------------------------------------------------------------
 *
 * windSendCmd --
 *
 *	Send a command to a certain window type.  If possible we will pass a
 *	arbitrarily chosen window of that type down to the client.
 *
 * Usage:
 *	send type command
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Whatever the client does
 *
 * ----------------------------------------------------------------------------
 */

void
windSendCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    MagWindow *toWindow;
    WindClient client;
    TxCommand newcmd;
    extern int windSendCmdFunc();

    if (cmd->tx_argc < 3) goto usage;
    if (cmd->tx_argv[1][0] == '*') goto usage; /* hidden window client */

    client = WindGetClient(cmd->tx_argv[1], FALSE);
    if (client == (WindClient) NULL) goto usage;
    toWindow = (MagWindow *) NULL;
    (void) WindSearch(client, (ClientData) NULL, (Rect *) NULL, 
	windSendCmdFunc, (ClientData) &toWindow);
    {
	int i;
	newcmd = *cmd;
	newcmd.tx_argc -= 2;
	for (i = 0; i < newcmd.tx_argc; i++) {
	    newcmd.tx_argv[i] = newcmd.tx_argv[i + 2];
	};
    }
    newcmd.tx_wid = WIND_UNKNOWN_WINDOW;
    if (toWindow != NULL) newcmd.tx_wid = toWindow->w_wid;
    (void) WindSendCommand(toWindow, &newcmd, FALSE);
    return;

    usage:
	TxError("Usage: send type command\n");
	TxPrintf("Valid window types are:\n");
	WindPrintClientList(FALSE);
	return;
}

int
windSendCmdFunc(w, cd)
    MagWindow *w;
    ClientData cd;
{
    *((MagWindow **) cd) = w;
    return 1;
}

#endif

/* Structure used by "position" command to pass to WindSearch	*/
/* as client data.						*/

typedef struct _cdwpos {
    FILE *file;
    bool doFrame;
} cdwpos;
   

/*
 * ----------------------------------------------------------------------------
 *
 * windPositionsCmd --
 *
 * Print out the positions of the windows.
 *
 * Usage:
 *	windowpositions [file]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A file is written.
 *
 * ----------------------------------------------------------------------------
 */

void
windPositionsCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    extern int windPositionsFunc();
    char *filename = NULL;
    cdwpos windpos;

    windpos.doFrame = FALSE;
    windpos.file = stdout;

    if (cmd->tx_argc > 3) goto usage;
    if (cmd->tx_argc > 1)
    {
	if (!strncmp(cmd->tx_argv[1], "frame", 5))
	{
	   windpos.doFrame = TRUE;
	   if (cmd->tx_argc == 3)
		filename = cmd->tx_argv[2];
	}
	else if (cmd->tx_argc == 2)
	   filename = cmd->tx_argv[1];
	else
	    goto usage;
    }

    if (filename) {
	windpos.file = fopen(filename, "w");
	if (windpos.file == (FILE *) NULL)
	{
	    TxError("Could not open file %s for writing.\n", filename);
	    return;
	};
    }
    (void) WindSearch((WindClient) NULL, (ClientData) NULL, (Rect *) NULL, 
	windPositionsFunc, (ClientData) &windpos);
    if (filename) (void) fclose(windpos.file);
    return;
    
usage:
    TxError("Usage:  windowpositions [file]\n");
    return;
}

int
windPositionsFunc(w, cdata)
    MagWindow *w;
    ClientData cdata;
{
    cdwpos *windpos = (cdwpos *)cdata;
    Rect r;

    if (windpos->doFrame)
	r = w->w_frameArea;
    else
	r = w->w_screenArea;

    if (windpos->file == stdout)
#ifdef MAGIC_WRAPPER
    {
	Tcl_Obj *lobj;

	lobj = Tcl_NewListObj(0, NULL);

	Tcl_ListObjAppendElement(magicinterp, lobj, Tcl_NewIntObj((int)r.r_xbot));
	Tcl_ListObjAppendElement(magicinterp, lobj, Tcl_NewIntObj((int)r.r_ybot));
	Tcl_ListObjAppendElement(magicinterp, lobj, Tcl_NewIntObj((int)r.r_xtop));
	Tcl_ListObjAppendElement(magicinterp, lobj, Tcl_NewIntObj((int)r.r_ytop));
	Tcl_ListObjAppendElement(magicinterp, lobj,
		Tcl_NewStringObj(((clientRec *)w->w_client)->w_clientName,
		strlen(((clientRec *)w->w_client)->w_clientName)));
	Tcl_SetObjResult(magicinterp, lobj);
    }
#else
	TxPrintf("specialopen %d %d %d %d %s\n", 
		r.r_xbot, r.r_ybot, r.r_xtop, r.r_ytop,
		((clientRec *) w->w_client)->w_clientName);
#endif
    else
	fprintf((FILE *)cdata, "specialopen %d %d %d %d %s\n", 
		r.r_xbot, r.r_ybot, r.r_xtop, r.r_ytop,
		((clientRec *) w->w_client)->w_clientName);
    return 0;
}


/*
 * ----------------------------------------------------------------------------
 *
 * windZoomCmd --
 *
 * Implement the "zoom" command.
 * Change the view in the selected window by the given scale factor.
 *
 * Usage:
 *	zoom amount
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The window underneath the cursor is changed.
 *
 * ----------------------------------------------------------------------------
 */

void
windZoomCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    float factor;

    if (w == NULL)
	return;

    if ((w->w_flags & WIND_SCROLLABLE) == 0) {
	TxError("Sorry, can't zoom this window.\n");
	return;
    };

    if (cmd->tx_argc != 2)
    {
	TxError("Usage: %s factor\n", cmd->tx_argv[0]);
	return;
    }

    factor = MagAtof(cmd->tx_argv[1]);
    if ((factor <= 0) || (factor >= 20))
    {
	TxError("zoom factor must be between 0 and 20.\n");
	return;
    }

    WindZoom(w, factor);
}
