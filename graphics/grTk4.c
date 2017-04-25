/* grTk4.c -
 *
 * Copyright 2003 Open Circuit Design, Inc., for MultiGiG Ltd.
 *
 * This file contains functions to manage the graphics tablet associated
 * with the X display.
 *
 */

#include <signal.h>
#include <stdio.h>
#include <X11/Xlib.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/magsgtty.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "graphics/graphicsInt.h"
#include "textio/txcommands.h"
#include "grTkInt.h"
#include "grTkCommon.h"


/*---------------------------------------------------------
 * GrTkDisableTablet:
 *	Turns off the cursor.
 *
 * Results:	None.
 *
 * Side Effects:    None.		
 *---------------------------------------------------------
 */

void
GrTkDisableTablet ()
{
}


/*---------------------------------------------------------
 * GrTkEnableTablet:
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
GrTkEnableTablet ()
{
}


/*
 * ----------------------------------------------------------------------------
 * grtkGetCursorPos:
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
grtkGetCursorPos (mw, p)
    MagWindow *mw;
    Point *p;		/* point to be filled in with screen coordinates */
{
    int x, y, x1, y1;
    unsigned int buttons;
    Window win1, win2;

    if (mw == NULL) mw = grCurrent.mw;
    
    XQueryPointer(grXdpy, Tk_WindowId((Tk_Window)mw->w_grdata),
		  &win1, &win2, &x1, &y1,
		  &x, &y, &buttons);
    p->p_x = x;
    p->p_y = grXtransY(mw, y);

    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 * grtkGetCursorRootPos:
 * 	Read the cursor position in screen root coordinates.
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
grtkGetCursorRootPos (mw, p)
    MagWindow *mw;
    Point *p;		/* point to be filled in with root coordinates */
{
    int x, y, x1, y1;
    unsigned int buttons;
    Window win1, win2;

    if (mw == NULL) mw = grCurrent.mw;
    
    XQueryPointer(grXdpy, Tk_WindowId((Tk_Window)mw->w_grdata),
		  &win1, &win2, &x1, &y1,
		  &x, &y, &buttons);
    p->p_x = x1;
    p->p_y = y1;

    return TRUE;
}
