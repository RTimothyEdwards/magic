/* grTCairo4.c -
 *
 * Copyright 2017 Open Circuit Design
 *
 * This file contains functions to manage the graphics tablet associated
 * with the X display.
 *
 * Written by Chuan Chen
 */

#include <signal.h>
#include <stdio.h>
#include <X11/Xlib.h>
#include <cairo/cairo-xlib.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "graphics/graphicsInt.h"
#include "graphics/grTkCommon.h"
#include "textio/txcommands.h"
#include "grTCairoInt.h"

extern Display *grXdpy;


/*---------------------------------------------------------
 * GrTCairoDisableTablet:
 *	Turns off the cursor.
 *
 * Results:	None.
 *
 * Side Effects:    None.
 *---------------------------------------------------------
 */

void
GrTCairoDisableTablet ()
{
    /* (Unimplemented) */
}


/*---------------------------------------------------------
 * GrTCairoEnableTablet:
 *	This routine enables the graphics tablet.
 *
 * Results:
 *   	None.
 *
 * Side Effects:
 *	Simply turn on the crosshair.
 *---------------------------------------------------------
 */

void
GrTCairoEnableTablet ()
{
    /* (Unimplemented) */
}


/*
 * ----------------------------------------------------------------------------
 * grtcairoGetCursorPos:
 * 	Read the cursor position in magic coordinates.
 *
 * Results:
 *	TRUE is returned if the coordinates were succesfully read, FALSE
 *	otherwise.
 *
 * Side effects:
 *	The parameter is filled in with the cursor position, in the form of
 *	a point in screen coordinates.
 * ----------------------------------------------------------------------------
 */

bool
grtcairoGetCursorPos (mw, p)
    MagWindow *mw;	/* window for which result is given */
    Point *p;		/* point to be filled in with screen coordinates */
{
    int x, y, x1, y1;
    unsigned int buttons;
    Window win1, win2;

    if (mw == (MagWindow *)NULL) mw = tcairoCurrent.mw;

    XQueryPointer(grXdpy, Tk_WindowId((Tk_Window)(mw->w_grdata)),
		  &win1, &win2, &x1, &y1,
		  &x, &y, &buttons);

    p->p_x = x;
    p->p_y = grXtransY(mw, y);

    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 * grtcairoGetCursorRootPos:
 * 	Read the cursor position in sreen root coordinates.
 *
 * Results:
 *	TRUE is returned if the coordinates were succesfully read, FALSE
 *	otherwise.
 *
 * Side effects:
 *	The parameter is filled in with the cursor position, in the form of
 *	a point in screen coordinates.
 * ----------------------------------------------------------------------------
 */

bool
grtcairoGetCursorRootPos (mw, p)
    MagWindow *mw;	/* window for which result is given */
    Point *p;		/* point to be filled in with screen coordinates */
{
    int x, y, x1, y1;
    unsigned int buttons;
    Window win1, win2;

    if (mw == (MagWindow *)NULL) mw = tcairoCurrent.mw;

    XQueryPointer(grXdpy, Tk_WindowId((Tk_Window)(mw->w_grdata)),
		  &win1, &win2, &x1, &y1,
		  &x, &y, &buttons);

    p->p_x = x1;
    p->p_y = y1;

    return TRUE;
}
