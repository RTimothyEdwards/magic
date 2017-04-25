/* windClient.c -
 *
 *	Send button pushes and commands to the window's command
 *	interpreters.
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
static char rcsid[] __attribute__ ((unused)) ="$Header: /usr/cvsroot/magic-8.0/windows/windClient.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "windows/windows.h"
#include "graphics/glyphs.h"
#include "windows/windInt.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/main.h"
#include "utils/macros.h"
#include "utils/utils.h"
#include "utils/malloc.h"
#include "graphics/graphics.h"
#include "utils/styles.h"
#include "textio/txcommands.h"
#include "utils/undo.h"

/* The following defines are used to indicate corner positions
 * of the box:
 */

#define WIND_BL 0
#define WIND_BR 1
#define WIND_TR 2
#define WIND_TL 3
#define WIND_ILG -1

/* our window client ID */
global WindClient windClientID = (WindClient) NULL;

extern int windBorderCmd();
extern int windCaptionCmd(), windCrashCmd(), windCursorCmd();
extern int windFilesCmd(), windCloseCmd(), windOpenCmd();
extern int windQuitCmd(), windRedrawCmd();
extern int windResetCmd(), windSpecialOpenCmd();
extern int windOverCmd(), windUnderCmd(), windDebugCmd();
extern int windDumpCmd(), windHelpCmd();
extern int windMacroCmd(), windIntMacroCmd();
extern int windLogCommandsCmd(), windUpdateCmd(), windSleepCmd();
extern int windSetpointCmd();
extern int windPushbuttonCmd();
extern int windPauseCmd(), windGrstatsCmd();
extern int windGrowCmd();
extern int windUndoCmd(), windRedoCmd();
extern int windCenterCmd(), windScrollCmd();
extern int windVersionCmd(), windViewCmd(), windXviewCmd(), windZoomCmd();
extern int windScrollBarsCmd(), windPositionsCmd();
extern int windNamesCmd();


#ifdef MAGIC_WRAPPER
extern int windBypassCmd();
#else
extern int windEchoCmd(), windSourceCmd(), windSendCmd();
#endif

static Rect windFrameRect;
static MagWindow *windFrameWindow;
static int windButton = TX_LEFT_BUTTON;
static int windCorner = WIND_ILG;	/* Nearest corner when button went
					 * down.
					 */


/*
 * ----------------------------------------------------------------------------
 *	windButtonSetCursor --
 *
 * 	Used to set the programmable cursor for a particular
 *	button state.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Selects and sets a programmable cursor based on the given
 *	button (for sizing or moving) and corner.
 * ----------------------------------------------------------------------------
 */

void
windButtonSetCursor(button, corner)
    int button;			/* Button that is down. */
    int corner;			/* Corner to be displayed in cursor. */
{
    switch (corner)
    {
	case WIND_BL:
	    if (button == TX_LEFT_BUTTON)
		GrSetCursor(STYLE_CURS_LLWIND);
	    else
		GrSetCursor(STYLE_CURS_LLWINDCORN);
	    break;
	case WIND_BR:
	    if (button == TX_LEFT_BUTTON)
		GrSetCursor(STYLE_CURS_LRWIND);
	    else
		GrSetCursor(STYLE_CURS_LRWINDCORN);
	    break;
	case WIND_TL:
	    if (button == TX_LEFT_BUTTON)
		GrSetCursor(STYLE_CURS_ULWIND);
	    else
		GrSetCursor(STYLE_CURS_ULWINDCORN);
	    break;
	case WIND_TR:
	    if (button == TX_LEFT_BUTTON)
		GrSetCursor(STYLE_CURS_URWIND);
	    else
		GrSetCursor(STYLE_CURS_URWINDCORN);
	    break;
    }
}


/*
 * ----------------------------------------------------------------------------
 * windGetCorner --
 *
 * 	Returns the corner of the window closest to a given screen location.
 *
 * Results:
 *	An integer value is returned, indicating the corner closest to
 *	the given screen location.  
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

int
windGetCorner(screenPoint, screenRect)
    Point *screenPoint;
    Rect *screenRect;

{
    Rect r;

    /* Find out which corner is closest.  Consider only the
     * intersection of the box with the window (otherwise it
     * may not be possible to select off-screen corners.
     */

    r = *screenRect;
    GeoClip(&r, &GrScreenRect);
    if (screenPoint->p_x < ((r.r_xbot + r.r_xtop)/2))
    {
	if (screenPoint->p_y < ((r.r_ybot + r.r_ytop)/2))
	    return WIND_BL;
	else 
	    return WIND_TL;
    }
    else
    {
	if (screenPoint->p_y < ((r.r_ybot + r.r_ytop)/2))
	    return WIND_BR;
	else 
	    return WIND_TR;
    }
}

/*
 * ----------------------------------------------------------------------------
 * windMoveRect --
 *
 * 	Repositions a rectangle by one of its corners.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The rectangle is changed so that the given corner is at the
 *	given position.  
 * ----------------------------------------------------------------------------
 */

void
windMoveRect(wholeRect, corner, p, rect)
    bool wholeRect;		/* move the whole thing?  or just a corner? */
    int corner;			/* Specifies a corner in the format
				 * returned by ToolGetCorner.
				 */
    Point *p;			/* New position of corner, in screen
				 * coordinates.
				 */
    Rect *rect;
{
    int x, y, tmp;

    /* Move the rect.  If an illegal corner is specified, then
     * move by the bottom-left corner.
     */

    if (wholeRect)
    {
	switch (corner)
	{
	    case WIND_BL:
		x = p->p_x - rect->r_xbot;
		y = p->p_y - rect->r_ybot;
		break;
	    case WIND_BR:
		x = p->p_x - rect->r_xtop;
		y = p->p_y - rect->r_ybot;
		break;
	    case WIND_TR:
		x = p->p_x - rect->r_xtop;
		y = p->p_y - rect->r_ytop;
		break;
	    case WIND_TL:
		x = p->p_x - rect->r_xbot;
		y = p->p_y - rect->r_ytop;
		break;
	    default:
		x = p->p_x - rect->r_xbot;
		y = p->p_y - rect->r_ybot;
		break;
	}
	rect->r_xbot += x;
	rect->r_ybot += y;
	rect->r_xtop += x;
	rect->r_ytop += y;
    }
    else
    {
	switch (corner)
	{
	    case WIND_BL:
		rect->r_xbot = p->p_x;
		rect->r_ybot = p->p_y;
		break;
	    case WIND_BR:
		rect->r_xtop = p->p_x;
		rect->r_ybot = p->p_y;
		break;
	    case WIND_TR:
		rect->r_xtop = p->p_x;
		rect->r_ytop = p->p_y;
		break;
	    case WIND_TL:
		rect->r_xbot = p->p_x;
		rect->r_ytop = p->p_y;
		break;
	}

	/* If the movement turned the box inside out, turn it right
	 * side out again.
	 */
	
	if (rect->r_xbot > rect->r_xtop)
	{
	    tmp = rect->r_xtop;
	    rect->r_xtop = rect->r_xbot;
	    rect->r_xbot = tmp;
	}
	if (rect->r_ybot > rect->r_ytop)
	{
	    tmp = rect->r_ytop;
	    rect->r_ytop = rect->r_ybot;
	    rect->r_ybot = tmp;
	}

    }
}


/*
 * ----------------------------------------------------------------------------
 * Button Routines --
 *
 * 	This page contains a set of routines to handle the puck
 *	buttons within the window border.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Left button:  used to move the whole window by the lower-left corner.
 *	Right button: used to re-size the window by its upper-right corner.
 *		If one of the left or right buttons is pushed, then the
 *		other is pushed, the corner is switched to the nearest
 *		one to the cursor.  This corner is remembered for use
 *		in box positioning/sizing when both buttons have gone up.
 *	Bottom button: Center the view on the point where the crosshair is
 *		at when the button is released.
 * ----------------------------------------------------------------------------
 */

void
windFrameDown(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    if (WindOldButtons == 0)
    {
	windFrameRect = w->w_frameArea;
	windFrameWindow = w;
	windButton = cmd->tx_button;
    }
#define BOTHBUTTONS (TX_LEFT_BUTTON | TX_RIGHT_BUTTON)
    if ((WindNewButtons & BOTHBUTTONS) == BOTHBUTTONS)
    {
	windCorner = windGetCorner(&(cmd->tx_p), &(windFrameWindow->w_frameArea));
    }
    else if (cmd->tx_button == TX_LEFT_BUTTON) 
    {
	windCorner = WIND_BL;
	windButtonSetCursor(windButton, windCorner);
    }
    else if (cmd->tx_button == TX_RIGHT_BUTTON) 
    {
	windCorner = WIND_TR;
	windButtonSetCursor(windButton, windCorner);
    }
}

    /*ARGSUSED*/
void
windFrameUp(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    if (WindNewButtons == 0)
    {
	GrSetCursor(STYLE_CURS_NORMAL);
	switch (cmd->tx_button)
	{
	    case TX_LEFT_BUTTON:
	    case TX_RIGHT_BUTTON:
		windMoveRect( (windButton == TX_LEFT_BUTTON),
			windCorner, &(cmd->tx_p), &windFrameRect);
		WindReframe(windFrameWindow, &windFrameRect, FALSE,
			(windButton == TX_LEFT_BUTTON) );
		break;
	}
    }
    else
    {
	/* If both buttons are down and one is released, we just change
	 * the cursor to reflect the current corner and the remaining
	 * button (i.e. move or size window).
	 */

	windCorner = windGetCorner(&(cmd->tx_p), &(windFrameWindow->w_frameArea));
	windButtonSetCursor(windButton, windCorner);
    }
}

/*
 * ----------------------------------------------------------------------------
 * windFrameButtons --
 *
 *	Handle button pushes to the window frame area (zoom and scroll)
 *	Always handle scroll bars ourselves, even if there is an external
 *	window package.  BUT---in the Tcl/Tk version of Magic, the
 *	window does not set WIND_SCROLLBARS, causing this routine to be
 *	bypassed.
 *
 * Results:
 *	TRUE if the button was pushed in the frame and handled by one
 *	of the button handler routines, FALSE if not.
 *
 * Side effects:
 *	Depends upon where the button was pushed.
 * ----------------------------------------------------------------------------
 */

bool
windFrameButtons(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    extern void windBarLocations();

    Rect leftBar, botBar, up, down, right, left, zoom;
    Point p;

    if (w == NULL) return FALSE;
    p.p_x = w->w_screenArea.r_xtop - w->w_screenArea.r_xbot;
    p.p_y = w->w_screenArea.r_ytop - w->w_screenArea.r_ybot;

    if ((w->w_flags & WIND_SCROLLBARS) != 0)
    {
	windBarLocations(w, &leftBar, &botBar, &up, &down, &right, &left, &zoom);
	if (cmd->tx_button == TX_MIDDLE_BUTTON) {
	    if (GEO_ENCLOSE(&cmd->tx_p, &leftBar))
	    {
		/* move elevator */
		p.p_x = 0;
		p.p_y = w->w_bbox->r_ybot + 
			((w->w_bbox->r_ytop - w->w_bbox->r_ybot) * 
			(cmd->tx_p.p_y - leftBar.r_ybot))
			/ (leftBar.r_ytop - leftBar.r_ybot) -
			(w->w_surfaceArea.r_ytop + w->w_surfaceArea.r_ybot)/2;
		WindScroll(w, &p, (Point *) NULL);
		return TRUE;
	    }
	    else if (GEO_ENCLOSE(&cmd->tx_p, &botBar))
	    {
		/* move elevator */
		p.p_y = 0;
		p.p_x = w->w_bbox->r_xbot + 
			((w->w_bbox->r_xtop - w->w_bbox->r_xbot) *
			(cmd->tx_p.p_x - botBar.r_xbot))
			/ (botBar.r_xtop - botBar.r_xbot) -
			(w->w_surfaceArea.r_xtop + w->w_surfaceArea.r_xbot)/2;
		WindScroll(w, &p, (Point *) NULL);
		return TRUE;
	    }
	    else if (GEO_ENCLOSE(&cmd->tx_p, &up))
	    {
		/* scroll up */
		p.p_y = -p.p_y;
		p.p_x = 0;
		WindScroll(w, (Point *) NULL, &p);
		return TRUE;
	    }
	    else if (GEO_ENCLOSE(&cmd->tx_p, &down))
	    {
		/* scroll down */
		p.p_x = 0;
		WindScroll(w, (Point *) NULL, &p);
		return TRUE;
	    }
	    else if (GEO_ENCLOSE(&cmd->tx_p, &right))
	    {
		/* scroll right */
		p.p_x = -p.p_x;
		p.p_y = 0;
		WindScroll(w, (Point *) NULL, &p);
		return TRUE;
	    }
	    else if (GEO_ENCLOSE(&cmd->tx_p, &left))
	    {
		/* scroll left */
		p.p_y = 0;
		WindScroll(w, (Point *) NULL, &p);
		return TRUE;
	    }
	}
	if (GEO_ENCLOSE(&cmd->tx_p, &zoom)) {
	    /* zoom in, out, or view */
	    switch (cmd->tx_button)
	    {
		case TX_LEFT_BUTTON:
		    WindZoom(w, 2.0);
		    break;
		case TX_MIDDLE_BUTTON:
		    WindView(w);
		    break;
		case TX_RIGHT_BUTTON:
		    WindZoom(w, 0.5);
		    break;
	    }
	    return TRUE;
	}
    }
    return FALSE;
}

/*
 * ----------------------------------------------------------------------------
 * windClientButtons --
 *
 *	Handle button pushes to the window border.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	depends upon where the button was pushed.
 * ----------------------------------------------------------------------------
 */

void
windClientButtons(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    /*
     * Is this an initial 'down push' in a non-iconic window?  If so, we
     * will initiate some user-interaction sequence, such as moving the corner
     * of the window or growing it to full-screen size.
     *
     * (An 'iconic' window is one that is closed down to an icon -- this
     * currently only happens when we are using the Sun window package, but
     * in the future it might happen in other cases too.)
     *
     */

    if ((WindOldButtons == 0) && ((w->w_flags & WIND_ISICONIC) == 0))
    {
	/* single button down */
	Point p;
	Rect caption;

	windFrameWindow = NULL;
	if (w == NULL) return;
	p.p_x = w->w_screenArea.r_xtop - w->w_screenArea.r_xbot;
	p.p_y = w->w_screenArea.r_ytop - w->w_screenArea.r_ybot;
	caption = w->w_allArea;
	if ((w->w_flags & WIND_CAPTION) != 0)
	    caption.r_ybot = caption.r_ytop - TOP_BORDER(w) + 1;
	else
	    caption.r_ybot = caption.r_ytop;

	/* Handle 'grow' for our window package. */
	if (WindPackageType == WIND_MAGIC_WINDOWS)
	{
	    if ((cmd->tx_button == TX_MIDDLE_BUTTON) && 
				GEO_ENCLOSE(&cmd->tx_p, &caption))
	    {
		WindFullScreen(w);
		return;
	    }
	}
	if (windFrameButtons(w, cmd)) return;
	    
	/* Otherwise, continue onward */
    }

    /*
     * At this point, we have decided that the button was not an initial
     * button push for Magic's window package.  Maybe an external window
     * package wants it, or maybe it is a continuation of a previous Magic
     * sequence (such as moving a corner of a window).
     */
    switch ( WindPackageType )
    {
	case WIND_X_WINDOWS:
	    break;
	
	default:
	    /* Magic Windows */
	    if (cmd->tx_button == TX_MIDDLE_BUTTON) 
		return;
	    if ((cmd->tx_buttonAction == TX_BUTTON_UP) && 
		(windFrameWindow == NULL)) 
		return;

	    /* no special area or else an up push -- reframe window */
	    switch (cmd->tx_buttonAction)
	    {
		case TX_BUTTON_DOWN:
		    windFrameDown(w, cmd);
		    break;
		case TX_BUTTON_UP:
		    windFrameUp(w, cmd);
		    break;
		default:
		    TxError("windClientButtons() failed!\n");
		    break;
	    }
    }
}

/*
 * ----------------------------------------------------------------------------
 * WindButtonInFrame --
 *
 *	To be called from the graphics packages. Handle button pushes to
 *	the window frame area (zoom and scroll) by calling windFrameButtons()
 *	if it appears to be appropriate for this MagWindow structure.
 *	This bypasses the key macro handler, thus hard-coding the button
 *	actions for the frame.  
 *
 * Results:
 *	TRUE if the button was pushed in the frame and handled by one
 *	of the button handler routines, FALSE if not.
 *
 * Side effects:
 *	Depends upon where the button was pushed.
 * ----------------------------------------------------------------------------
 */

bool
WindButtonInFrame(w, x, y, b)
    MagWindow *w;
    int x;
    int y;
    int b;
{
    TxCommand cmd;
    cmd.tx_p.p_x = x;
    cmd.tx_p.p_y = y;
    cmd.tx_button = b;
    if (windFrameButtons(w, &cmd))
    {
	WindUpdate();
	return TRUE;
    }
    return FALSE;
}


/*
 * ----------------------------------------------------------------------------
 *
 * windClientInterp
 *
 * Window's command interpreter.
 * Dispatches to long commands, providing them with the window and command
 * we are passed.
 *
 * Results:
 *	none.
 *
 * Side effects:
 *	Whatever occur as a result of executing the long
 *	command supplied.
 *
 * ----------------------------------------------------------------------------
 */

void
windCmdInterp(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int cmdNum;

    switch (cmd->tx_button)
    {
        case TX_LEFT_BUTTON:
        case TX_RIGHT_BUTTON:
        case TX_MIDDLE_BUTTON:
	    windClientButtons(w, cmd);
	    break;
	case TX_NO_BUTTON:
	    if (WindExecute(w, windClientID, cmd) >= 0)
		UndoNext();
	    break;
	default:
	    ASSERT(FALSE, "windCmdInterp");
	    break;
    }
}


/*
 * ----------------------------------------------------------------------------
 *  windCmdInit --
 *
 *	Initialize the window client.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

void
windClientInit()
{
    windClientID = WindAddClient(WINDOW_CLIENT, ( bool (*)() ) NULL,
	( bool (*)() ) NULL, ( void (*)() ) NULL, windCmdInterp, 
	( void (*)() ) NULL, ( bool (*)() ) NULL, ( void (*)() ) NULL,
	(GrGlyph *) NULL);

    /* Set up all of the window commands */

#ifdef MAGIC_WRAPPER
    WindAddCommand(windClientID,
	"*bypass command	run command independently of the command line",
	windBypassCmd, FALSE);
#endif
    WindAddCommand(windClientID,
	"*crash			cause a core dump",
	windCrashCmd, FALSE);
    WindAddCommand(windClientID,
	"*files			print out currently open files",
	windFilesCmd, FALSE);
    WindAddCommand(windClientID,
	"*grstats		print out stats on graphics",
	windGrstatsCmd, FALSE);
    WindAddCommand(windClientID,
	"*pause	[args]		print args and wait for <cr>",
	windPauseCmd, FALSE);
    WindAddCommand(windClientID,
	"*winddebug		set debugging mode",
	windDebugCmd, FALSE);
    WindAddCommand(windClientID,
	"*winddump		print out debugging info",
	windDumpCmd, FALSE);

    WindAddCommand(windClientID,
	"center [x y]		center window on the cursor or indicated coordinate",
	windCenterCmd, FALSE);
    WindAddCommand(windClientID,
	"closewindow [name]	close a window",
	windCloseCmd, FALSE);
    WindAddCommand(windClientID,
	"cursor			return magic coordinates of the cursor",
	windCursorCmd, FALSE);
    WindAddCommand(windClientID,
	"grow			blow a window up to full-screen size or back again",
	windGrowCmd, FALSE);
    WindAddCommand(windClientID,
	"help [pattern]		print out synopses for all commands valid\n\
			in the current window (or just those\n\
			containing pattern)",
	windHelpCmd, FALSE);
    WindAddCommand(windClientID,
        "imacro [char [string]] define or print an interactive macro called char",
        windIntMacroCmd, FALSE);
    WindAddCommand(windClientID,
	"logcommands [file [update]]\n\
			log all commands into a file",
	windLogCommandsCmd, FALSE);
    WindAddCommand(windClientID,
        "macro [char [string]]  define or print a macro called char",
        windMacroCmd, FALSE);
    WindAddCommand(windClientID,
	"openwindow [cell][name]\n\
			open a new window with indicated name, bound to indicated cell",
	windOpenCmd, FALSE);
    WindAddCommand(windClientID,
	"over			move a window over (on top of) the rest",
	windOverCmd, FALSE);
    WindAddCommand(windClientID,
	"pushbutton button act	push a mouse button",
	windPushbuttonCmd, FALSE);
    WindAddCommand(windClientID,
	"redo [count]		redo commands",
	windRedoCmd, FALSE);
    WindAddCommand(windClientID,
	"redraw			redraw the display",
	windRedrawCmd, FALSE);
    WindAddCommand(windClientID,
	"reset			reset the display",
	windResetCmd, FALSE);
    WindAddCommand(windClientID,
	"scroll dir [amount]	scroll the window",
	windScrollCmd, FALSE);
    WindAddCommand(windClientID,
	"setpoint [x y [WID]]	force to cursor (point) to x,y in window WID",
	windSetpointCmd, FALSE);
    WindAddCommand(windClientID,
	"sleep seconds		sleep for a number of seconds",
	windSleepCmd, FALSE);
    WindAddCommand(windClientID,
	"specialopen [coords] type [args]\n\
			open a special window",
	windSpecialOpenCmd, FALSE);
    WindAddCommand(windClientID,
	"quit			exit magic",
	windQuitCmd, FALSE);
    WindAddCommand(windClientID,
	"underneath		move a window underneath the rest",
	windUnderCmd, FALSE);
    WindAddCommand(windClientID,
	"undo [count]		undo commands",
	windUndoCmd, FALSE);
    WindAddCommand(windClientID,
#ifdef MAGIC_WRAPPER
	"updatedisplay [suspend|resume]\n\
			force display update, or suspend/resume updates",
#else
	"updatedisplay		force the display to be updated",
#endif
	windUpdateCmd, FALSE);
    WindAddCommand(windClientID,
	"version			print out version info",
	windVersionCmd, FALSE);
    WindAddCommand(windClientID,
	"view [get]              zoom window out so everything is visible",
	windViewCmd, FALSE);
    WindAddCommand(windClientID,
	"windowborder [on|off]	toggle border drawing for new windows",	
	windBorderCmd, FALSE);
    WindAddCommand(windClientID,
	"windowcaption [on|off]	toggle title caption for new windows",	
	windCaptionCmd, FALSE);
    WindAddCommand(windClientID,
	"windowscrollbars [on|off]\n\
			toggle scroll bars for new windows",
	windScrollBarsCmd, FALSE);
    WindAddCommand(windClientID,
	"windowpositions [file]	print out window positions",
	windPositionsCmd, FALSE);
    WindAddCommand(windClientID,
	"xview               	zoom window out so everything is unexpanded",
	windXviewCmd, FALSE);
    WindAddCommand(windClientID,
	"zoom amount		zoom window by amount",
	windZoomCmd, FALSE);
    WindAddCommand(windClientID,
	"windownames [all|type]	get name of current or all windows",
	windNamesCmd, FALSE);
#ifndef MAGIC_WRAPPER
    WindAddCommand(windClientID,
	"echo [-n] [strings]	print text on the terminal",
	windEchoCmd, FALSE);
    WindAddCommand(windClientID,
	"send type command	send a command to a certain window type",
	windSendCmd, FALSE);
    WindAddCommand(windClientID,
	"source filename		read in commands from file",
	windSourceCmd, FALSE);
#endif
}
