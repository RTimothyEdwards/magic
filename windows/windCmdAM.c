/* windCmdAM.c -
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/windows/windCmdAM.c,v 1.2 2008/12/11 04:20:15 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/times.h>
#include <sys/stat.h>
#include <errno.h>
#include <math.h>		/* for round() function */

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "windows/windows.h"
#include "utils/malloc.h"
#include "utils/runstats.h"
#include "utils/macros.h"
#include "utils/signals.h"
#include "graphics/graphics.h"
#include "utils/styles.h"
#include "textio/txcommands.h"
#include "graphics/glyphs.h"
#include "windows/windInt.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "dbwind/dbwind.h"
#include "utils/utils.h"
#include "cif/cif.h"

/* Forward declarations */
void windDoMacro();

/*
 * ----------------------------------------------------------------------------
 *
 * windBorderCmd --
 *
 *	Change the flag which says whether new windows will have a border.
 *
 * Usage:
 *	windborder [on|off]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A flag is changed.
 *	In Tcl, if no options are presented, the border status is returned
 *	in the command exit status.
 *
 * ----------------------------------------------------------------------------
 */

void
windBorderCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int place;
    bool value;
    static char *onoff[] = {"on", "off", 0};
    static bool truth[] = {TRUE, FALSE};

    if (cmd->tx_argc > 2) goto usage;
    else if (cmd->tx_argc == 1)
    {
	if (w == (MagWindow *)NULL)
	{
	    TxError("No window specified for caption command\n");
	    goto usage;
	}

	value = (w->w_flags & WIND_BORDER) ? 0 : 1;

#ifdef MAGIC_WRAPPER
	Tcl_SetResult(magicinterp, onoff[value], TCL_STATIC);
#else
	TxPrintf("Window border is %s\n", onoff[value]);
#endif
	return;
    }

    place = Lookup(cmd->tx_argv[1], onoff);
    if (place < 0)
    goto usage;

    if (truth[place])
    {
        WindDefaultFlags |= WIND_BORDER;
	TxPrintf("New windows will have a border.\n");
    }
    else {
        WindDefaultFlags &= ~WIND_BORDER;
	TxPrintf("New windows will not have a border.\n");
    }
    return;

    usage:
	TxError("Usage: %s [on|off]\n", cmd->tx_argv[0]);
	return;
}
/*
 * ----------------------------------------------------------------------------
 *
 * windCaptionCmd --
 *
 *	Change the flag which says whether new windows will have a title caption.
 *
 * Usage:
 *	windcaption [on|off]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A flag is changed.
 *	In Tcl, if no options are presented, the window title caption is
 *	returned in the command status.
 *
 * ----------------------------------------------------------------------------
 */

void
windCaptionCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int place;
    Rect ts;
    static char *onoff[] = {"on", "off", 0};
    static bool truth[] = {TRUE, FALSE};

    if (cmd->tx_argc > 2) goto usage;
    else if (cmd->tx_argc == 1)
    {
	if (w == (MagWindow *)NULL)
	{
	    TxError("No window specified for caption command\n");
	    goto usage;
	}

#ifdef MAGIC_WRAPPER
	Tcl_SetResult(magicinterp, w->w_caption, TCL_STATIC);
#else
	TxPrintf("Window caption is \"%s\"\n", w->w_caption);
#endif
	return;
    }

    place = Lookup(cmd->tx_argv[1], onoff);
    if (place < 0)
    goto usage;

    if (truth[place])
    {
        WindDefaultFlags |= WIND_CAPTION;
	TxPrintf("New windows will have a title caption.\n");
    }
    else {
        WindDefaultFlags &= ~WIND_CAPTION;
	TxPrintf("New windows will not have a title caption.\n");
    }
    return;

    usage:
	TxError("Usage: %s [on|off]\n", cmd->tx_argv[0]);
	return;
}

/*
 * ----------------------------------------------------------------------------
 *
 * windCenterCmd --
 *
 * Implement the "center" command.
 * Move a window's view to center the point underneath the cursor, or to
 * the specified coordinate (in surface units).
 *
 * Usage:
 *	center [x y]
 *	center horizontal|vertical f
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The view in the window underneath the cursor is changed
 *	to center the point underneath the cursor.  
 *
 * ----------------------------------------------------------------------------
 */

void
windCenterCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    Point rootPoint;
    Rect newArea, oldArea;

    if (w == NULL)
    {
	TxError("Point to a window first.\n");
	return;
    }

    if (cmd->tx_argc == 1)
    {
	if ((w->w_flags & WIND_SCROLLABLE) == 0)
	{
	    TxError("Sorry, can't scroll this window.\n");
	    return;
	}
	WindPointToSurface(w, &cmd->tx_p, &rootPoint, (Rect *) NULL);
    }
    else if (cmd->tx_argc == 3)
    {
	if ((w->w_flags & WIND_SCROLLABLE) == 0)
	{
	    TxError("Sorry, can't scroll this window.\n");
	    return;
	}
	if (cmd->tx_argv[1][0] == 'h' || cmd->tx_argv[1][0] == 'v')
	{
	    double frac;

	    if (!StrIsNumeric(cmd->tx_argv[2]))
	    {
		TxError("Must specify a fractional value.\n");
		return;
	    }
	    frac = atof(cmd->tx_argv[2]);
	    if (cmd->tx_argv[1][0] == 'h')
	    {
		rootPoint.p_y = 0;
		rootPoint.p_x = w->w_bbox->r_xbot + frac *
			(w->w_bbox->r_xtop - w->w_bbox->r_xbot) -
			(w->w_surfaceArea.r_xtop + w->w_surfaceArea.r_xbot)/2;
	    }
	    else
	    {
		rootPoint.p_x = 0;
		rootPoint.p_y = w->w_bbox->r_ybot + frac *
			(w->w_bbox->r_ytop - w->w_bbox->r_ybot) -
			(w->w_surfaceArea.r_ytop + w->w_surfaceArea.r_ybot)/2;
	    }
	    WindScroll(w, &rootPoint, (Point *)NULL);
	    return;
	}
        else
	{
	    if (!StrIsInt(cmd->tx_argv[1]) || !StrIsInt(cmd->tx_argv[2]))
	    {
		TxError("Coordinates must be integer values\n");
		return;
	    }
	    rootPoint.p_x = atoi(cmd->tx_argv[1]);
	    rootPoint.p_y = atoi(cmd->tx_argv[2]);
	}
    }
    else
    {
	TxError("Usage: center [x y]\n");
	TxError("       center horizontal|vertical f\n");
	return;
    }

    oldArea = w->w_surfaceArea;
    newArea.r_xbot = rootPoint.p_x - (oldArea.r_xtop - oldArea.r_xbot)/2;
    newArea.r_xtop = newArea.r_xbot - oldArea.r_xbot + oldArea.r_xtop;
    newArea.r_ybot = rootPoint.p_y - (oldArea.r_ytop - oldArea.r_ybot)/2;
    newArea.r_ytop = newArea.r_ybot - oldArea.r_ybot + oldArea.r_ytop;

    WindMove(w, &newArea);
}


/*
 * ----------------------------------------------------------------------------
 * windCloseCmd --
 *
 *	Close the window that is pointed at.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The window is closed, and the client is notified.  The client may
 *	refuse to have the window closed, in which case nothing happens.
 * ----------------------------------------------------------------------------
 */

void
windCloseCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    if ((cmd->tx_argc == 2) && GrWindowNamePtr)
    {
	char *mwname;

	for (w = windTopWindow; w != (MagWindow *)NULL; w = w->w_nextWindow)
	{
	    mwname = (*GrWindowNamePtr)(w);
	    if (!strcmp(mwname, cmd->tx_argv[1]))
		break;
	}
	if (w == NULL)
	{
	    TxError("Window named %s cannot be found\n", cmd->tx_argv[1]);
	    return;
	}
    }

    if (w == (MagWindow *) NULL)
    {
	TxError("Point to a window first\n");
	return;
    }
    if (!WindDelete(w))
    {
	TxError("Unable to close that window\n");
	return;
    }
}

#ifdef MAGIC_WRAPPER
/*
 * ----------------------------------------------------------------------------
 * windBypassCmd --
 *
 *	Run a magic command independently of the command line.  That is,
 *	if a command is being typed on the command line, the input
 *	redirection will not be reset by the execution of this command.
 *	To avoid having such commands interfere with the selection
 *	mechanism, save and restore the command count.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	
 *
 * ----------------------------------------------------------------------------
 */

void
windBypassCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int saveCount;

    if (cmd->tx_argc == 1)
    {
	TxError("Usage:  *bypass <command>\n");
	return;
    }

    /* Dispatch the referenced command */
    saveCount = TxCommandNumber;
    TxTclDispatch((ClientData)w, cmd->tx_argc - 1, cmd->tx_argv + 1, FALSE);
    TxCommandNumber = saveCount;
    if (TxInputRedirect == TX_INPUT_PENDING_RESET)
	TxInputRedirect = TX_INPUT_REDIRECTED;
}

#endif	/* MAGIC_WRAPPER */

/*
 * ----------------------------------------------------------------------------
 * windCrashCmd --
 *
 *	Generate a core dump.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Dumps core by calling niceabort().
 *
 * ----------------------------------------------------------------------------
 */

void
windCrashCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    if (cmd->tx_argc != 1)
    {
	TxError("Usage:  *crash\n");
	return;
    }

    TxPrintf("OK -- crashing...\n");
    TxFlush();
    niceabort();
}


/*
 * ----------------------------------------------------------------------------
 * windCursorCmd --
 *
 *	Report the cursor position in Magic (internal) coordinates
 *	If an argument of a number is given, then the cursor icon
 *	is changed to the glyph of that number.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Prints coordinates (non-Tcl version)
 *	Return value set to the cursor position as a list (Tcl version)
 * ----------------------------------------------------------------------------
 */

void
windCursorCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    Point p_in, p_out;
    int  resulttype = DBW_SNAP_INTERNAL;
    double cursx, cursy, oscale;
    DBWclientRec *crec;

#ifdef MAGIC_WRAPPER
    Tcl_Obj *listxy;
#endif

    if (cmd->tx_argc == 2)
    {
	if (StrIsInt(cmd->tx_argv[1]))
	{
	    if (GrSetCursorPtr != NULL)
		(*GrSetCursorPtr)(atoi(cmd->tx_argv[1]));
	    return;
	}
	else if (*cmd->tx_argv[1] ==  'l')
	{
	    resulttype = DBW_SNAP_LAMBDA;
	}
	else if (*cmd->tx_argv[1] ==  'u')
	{
	    resulttype = DBW_SNAP_USER;
	}
	else if (*cmd->tx_argv[1] ==  'm')
	{
	    resulttype = DBW_SNAP_MICRONS;
	}
	else if (*cmd->tx_argv[1] == 'w')
	{
	    resulttype = -1;	// Use this value for "window"
	}
	else if (*cmd->tx_argv[1] == 's')
	{
	    resulttype = -2;	// Use this value for "screen"
	}
	else if (*cmd->tx_argv[1] != 'i')
	{
	    TxError("Usage: cursor glyphnum\n");
	    TxError(" (or): cursor [internal | lambda | microns | user | window]\n");
	    return;
	}
    }

    if (GrGetCursorPosPtr == NULL)
	return;

    if (resulttype == -2)
	GrGetCursorRootPos(w, &p_in);
    else
	GrGetCursorPos(w, &p_in);

    if (resulttype >= 0)
    {
	WindPointToSurface(w, &p_in, &p_out, (Rect *)NULL);

	/* Snap the cursor position if snap is in effect */
	if (DBWSnapToGrid != DBW_SNAP_INTERNAL)
	    ToolSnapToGrid(w, &p_out, (Rect *)NULL);
    }

    /* Transform the result to declared units with option "lambda" or "grid" */ 
    switch (resulttype) {
	case -2:
	case -1:
	    cursx = (double)p_in.p_x;
	    cursy = (double)p_in.p_y;
	    break;
	case DBW_SNAP_INTERNAL:
	    cursx = (double)p_out.p_x;
	    cursy = (double)p_out.p_y;
	    break;
	case DBW_SNAP_LAMBDA:
	    cursx = (double)(p_out.p_x * DBLambda[0]) / (double)DBLambda[1];
	    cursy = (double)(p_out.p_y * DBLambda[0]) / (double)DBLambda[1];
	    break;
	case DBW_SNAP_MICRONS:
	    oscale = (double)CIFGetOutputScale(1000);
	    cursx = (double)(p_out.p_x * oscale);
	    cursy = (double)(p_out.p_y * oscale);
	    break;
	case DBW_SNAP_USER:
	    crec = (DBWclientRec *)w->w_clientData;
	    cursx = (double)((p_out.p_x - crec->dbw_gridRect.r_xbot)
			/ (crec->dbw_gridRect.r_xtop - crec->dbw_gridRect.r_xbot));
	    cursy = (double)((p_out.p_y - crec->dbw_gridRect.r_ybot)
			/ (crec->dbw_gridRect.r_ytop - crec->dbw_gridRect.r_ybot));
	    break;
    }

#ifdef MAGIC_WRAPPER
    listxy = Tcl_NewListObj(0, NULL);
    if ((cursx == round(cursx)) && (cursy == round(cursy))) 
    {
	Tcl_ListObjAppendElement(magicinterp, listxy, Tcl_NewIntObj((int)cursx));
	Tcl_ListObjAppendElement(magicinterp, listxy, Tcl_NewIntObj((int)cursy));
    }
    else
    {
	Tcl_ListObjAppendElement(magicinterp, listxy, Tcl_NewDoubleObj(cursx));
	Tcl_ListObjAppendElement(magicinterp, listxy, Tcl_NewDoubleObj(cursy));
    }
    Tcl_SetObjResult(magicinterp, listxy);
#else
    TxPrintf("%g %g\n", cursx, cursy);
#endif
}

/*
 * ----------------------------------------------------------------------------
 * windDebugCmd --
 *
 *	Change to a new debugging mode.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

void
windDebugCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    if (cmd->tx_argc != 1) goto usage;
    windPrintCommands = !windPrintCommands;
    TxError("Window command debugging set to %s\n", 
	(windPrintCommands ? "TRUE" : "FALSE"));
    return;

usage:
    TxError("Usage:  *winddebug\n");
}

/*
 * ----------------------------------------------------------------------------
 * windDumpCmd --
 *
 *	Dump out debugging info.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

void
windDumpCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    (void) windDump();
}

#ifndef MAGIC_WRAPPER

/*
 * ----------------------------------------------------------------------------
 *
 * windEchoCmd --
 *
 *	Echo the arguments
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Text may appear on the terminal
 *
 * ----------------------------------------------------------------------------
 */

void
windEchoCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int i;
    bool newline = TRUE;

    for (i = 1; i < cmd->tx_argc; i++)
    {
	if (i != 1)
	    TxPrintf(" ");
	if ( (i == 1) && (strcmp(cmd->tx_argv[i], "-n") == 0) )
	   newline = FALSE;
	else
	    TxPrintf("%s", cmd->tx_argv[i]);
    }
    
    if (newline)
	TxPrintf("\n");
    TxFlush();
}

#endif


/*
 * ----------------------------------------------------------------------------
 *
 * windFilesCmd --
 *
 *	Find out what files are currently open.
 *
 * Usage:
 *	*files
 *
 * Side Effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */
 /*ARGSUSED*/

void
windFilesCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
#define NUM_FD	20	/* max number of open files per process */
    int fd;
    struct stat buf;
    int unopen, open;

    open = unopen = 0;
    for (fd = 0; fd < NUM_FD; fd++) {
	if (fstat(fd, &buf) != 0) {
	    if (errno == EBADF)
		unopen++;
	    else
		TxError("file descriptor %d: %s\n", fd, strerror(errno));
	}
	else {
	    char *type;
	    switch (buf.st_mode & S_IFMT) {
		case S_IFDIR: {type = "directory"; break;}
		case S_IFCHR: {type = "character special"; break;}
		case S_IFBLK: {type = "block special"; break;}
		case S_IFREG: {type = "regular"; break;}
		case S_IFLNK: {type = "symbolic link"; break;}
		case S_IFSOCK: {type = "socket"; break;}
		default: {type = "unknown"; break;}
	    }
	    TxError("file descriptor %d: open  (type: '%s', inode number %ld)\n", 
		fd, type, buf.st_ino);
	    open++;
	}
    }
    TxError("%d open files, %d unopened file descriptors left\n", open, unopen);
}


/*
 * ----------------------------------------------------------------------------
 *
 * windGrowCmd --
 *
 *	Grow a window to full-screen size or back to previous size.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Text may appear on the terminal
 *
 * ----------------------------------------------------------------------------
 */

void
windGrowCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    if (w == NULL)
    {
	TxError("Point to a window first.\n");
	return;
    };

    WindFullScreen(w);
}


/*
 * ----------------------------------------------------------------------------
 * windGrstatsCmd --
 *
 *	Take statistics on the graphics code.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

void
windGrstatsCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    char *RunStats(), *rstatp;
    static struct tms tlast, tdelta;
    int i, style, count;
    int us;
    extern int GrNumClipBoxes;
    int usPerRect, rectsPerSec;

    if (cmd->tx_argc < 2 || cmd->tx_argc > 3)
    {
	TxError("Usage: grstats num [ style ]\n");
	return;
    }

    if (!StrIsInt(cmd->tx_argv[1]) || 
	(cmd->tx_argc == 3 && !StrIsInt(cmd->tx_argv[2])))
    {
	TxError("Count & style must be numeric\n");
	return;
    }
    if (w == (MagWindow *) NULL)
    {
	TxError("Point to a window first.\n");
	return;
    }

    count = atoi(cmd->tx_argv[1]);
    if (cmd->tx_argc == 3)
	style = atoi(cmd->tx_argv[2]);
    else
	style = -1;

    WindUpdate();

    if (style >= 0)
	GrLock(w, TRUE);

    (void) RunStats(RS_TINCR, &tlast, &tdelta);
    GrNumClipBoxes = 0;
    for (i = 0; i < count; i++)
    {
	if (SigInterruptPending)
	    break;
	if (style < 0)
	{
	    WindAreaChanged(w, (Rect *) NULL);
	    WindUpdate();
	}
	else
	{
	    Rect r;
#define GRSIZE	15
#define GRSPACE 20
	    r.r_xbot = w->w_screenArea.r_xbot -  GRSIZE/2;
	    r.r_ybot = w->w_screenArea.r_ybot -  GRSIZE/2;
	    r.r_xtop = r.r_xbot + GRSIZE - 1;
	    r.r_ytop = r.r_ybot + GRSIZE - 1;
	    GrClipBox(&w->w_screenArea, STYLE_ERASEALL);
	    GrSetStuff(style);
	    while (r.r_xbot <= w->w_screenArea.r_xtop)
	    {
		while (r.r_ybot <= w->w_screenArea.r_ytop)
		{
		    GrFastBox(&r);
		    r.r_ybot += GRSPACE;
		    r.r_ytop += GRSPACE;
		}
		r.r_xbot += GRSPACE;
		r.r_xtop += GRSPACE;
		r.r_ybot = w->w_screenArea.r_ybot -  GRSIZE/2;
		r.r_ytop = r.r_ybot + GRSIZE - 1;
	    }
	}
    }
    rstatp = RunStats(RS_TINCR, &tlast, &tdelta);

    us = tdelta.tms_utime * (1000000 / 60);
    usPerRect = us / MAX(1, GrNumClipBoxes);
    rectsPerSec = 1000000 / MAX(1, usPerRect);
    TxPrintf("[%s]\n%d rectangles, %d uS, %d uS/rectangle, %d rects/sec\n", 
	rstatp, GrNumClipBoxes, us, usPerRect, rectsPerSec);

    if (style >= 0)
	GrUnlock(w);
}


/*
 * ----------------------------------------------------------------------------
 * windHelpCmd --
 *
 *	Just a dummy proc.  (Only for this particular, global, client)
 *	This is just here so that there is an entry in our help table!
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

void
windHelpCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    ASSERT(FALSE, windHelpCmd);
}

static char *logKeywords[] =
    {
	"update",
	0
    };

/*
 * ----------------------------------------------------------------------------
 * windLogCommandsCmd --
 *
 *	Log the commands and button pushes in a file.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

void
windLogCommandsCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    char *fileName;
    bool update;

    if ((cmd->tx_argc < 1) || (cmd->tx_argc > 3)) goto usage;

    update = FALSE;

    if (cmd->tx_argc == 1) 
	fileName = NULL;
    else
	fileName = cmd->tx_argv[1];

    if (cmd->tx_argc == 3) {
	int i;
	i = Lookup(cmd->tx_argv[cmd->tx_argc - 1], logKeywords);
	if (i != 0) goto usage;
	update = TRUE;
    }

    TxLogCommands(fileName, update);
    return;

usage:
    TxError("Usage: %s [filename [update]]\n", cmd->tx_argv[0]);
}

/*
 * ----------------------------------------------------------------------------
 *
 * windIntMacroCmd --
 *
 *	Define a new interactive macro.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Calls windDoMacro.
 *
 * ----------------------------------------------------------------------------
 */

void
windIntMacroCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    windDoMacro(w, cmd, TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * windMacroCmd --
 *
 *	Define a new macro.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Calls windDoMacro
 *
 * ----------------------------------------------------------------------------
 */

void
windMacroCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    windDoMacro(w, cmd, FALSE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * windDoMacro --
 *
 *	Working function for CmdIntMacro and CmdMacro
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Causes the macro package to define a new macro.
 *
 * ----------------------------------------------------------------------------
 */

void
windDoMacro(w, cmd, interactive)
    MagWindow *w;
    TxCommand *cmd;
    bool interactive;
{
    char *cp, *cn;
    char nulltext[] = "";
    char ch;
    int ct, argstart, verbose;
    bool any, iReturn;
    bool do_list = FALSE;
    bool do_help = FALSE;
    bool do_reverse = FALSE;
    char *searchterm = NULL;
    macrodef *cMacro;
    HashTable *clienttable;
    HashEntry *h;
    HashSearch hs;
    WindClient wc;

    /* If the first argument is a window name, we attempt to	*/
    /* retrieve a client ID from it.  This overrides the actual	*/
    /* window the command was called from, so technically we	*/
    /* can define macros for clients from inside other clients.	*/
    /* The main use, though, is to define macros for a client	*/
    /* from a script, rc file, or command-line.			*/
    /* Default to the layout window if the command has no	*/
    /* associated window.					*/

    argstart = 1;
    if (cmd->tx_argc == 1)
	wc = DBWclientID;  /* Added by NP 11/15/04 */
    else if (cmd->tx_argc > 1)
	wc = WindGetClient(cmd->tx_argv[1], TRUE);

    while (cmd->tx_argc > argstart)
    {
	if (!strcmp(cmd->tx_argv[argstart], "list"))
	{
	    do_list = TRUE;
	    argstart++;
	}
	else if (!strcmp(cmd->tx_argv[argstart], "help"))
	{
	    do_help = TRUE;
	    argstart++;
	}
	else if (!strcmp(cmd->tx_argv[argstart], "search"))
	{
	    if (cmd->tx_argc > (argstart + 1))
	    {
		argstart++;
		searchterm = cmd->tx_argv[argstart];
	    }
	    argstart++;
	}
	else if (!strcmp(cmd->tx_argv[argstart], "-reverse"))
	{
	    do_reverse = TRUE;
	    argstart++;
	}
	else break;
    }

    /* If client wasn't specified, use window default, else use	*/
    /* DBW client.						*/

    if (wc == (WindClient)NULL)
    {
	if (w != NULL)
	    wc = w->w_client;
	else
	    wc = DBWclientID;

	if (cmd->tx_argc > (argstart + 1))
	{
	    /* The first argument, if there is one after resolving	*/
	    /* all of the optional arugments, should be a key.		*/
	    /* If it doesn't look like one, then check if the		*/
	    /* next argument looks like a key, which would indicate	*/
	    /* an unregistered client as the first argument.  A		*/
	    /* macro retrieved from an unregistered client returns	*/
	    /* nothing but does not generate an error.			*/

	    if (MacroKey(cmd->tx_argv[argstart], &verbose) == 0)
		if (MacroKey(cmd->tx_argv[argstart + 1], &verbose) != 0)
		{
		    wc = 0;
		    argstart++;
		}
	}
    }
    else
	argstart++;

    if (cmd->tx_argc == argstart)
    {
	h = HashLookOnly(&MacroClients, wc);
	if (h == NULL)
	    return;
	else
	{
	    clienttable = (HashTable *)HashGetValue(h);
	    if (clienttable == (HashTable *)NULL)
	    {
		TxError("No such client.\n");
		return;
	    }
	}

	any = FALSE;
	ch = 0;
	HashStartSearch(&hs);
	while (TRUE)
	{
	    h = HashNext(clienttable, &hs);
	    if (h == NULL) break;
	    cMacro = (macrodef *) HashGetValue(h);
	    if (cMacro == (macrodef *)NULL) break;
	    cn = MacroName((spointertype)h->h_key.h_ptr);

	    /* "imacro list" returns only interactive macros. */
	    if (interactive && !cMacro->interactive) continue;

	    if (do_help)
		cp = cMacro->helptext;
	    else
		cp = cMacro->macrotext;

	    if (cp == (char *)NULL) cp = (char *)(&nulltext[0]);

	    if (searchterm != NULL)
	    {
		/* Refine results by keyword search */
		if (!strstr(cp, searchterm)) continue;
	    }

	    if (do_list)
	    {
#ifdef MAGIC_WRAPPER
		// NOTE:  Putting cp before cn makes it easier to
		// generate a reverse lookup hash table for matching
		// against menu items, to automatically generate
		// the "accelerator" text.
		if (do_reverse)
		    Tcl_AppendElement(magicinterp, cp);
		Tcl_AppendElement(magicinterp, cn);
		if (!do_reverse)
		    Tcl_AppendElement(magicinterp, cp);
#else
		TxPrintf("%s = \"%s\"\n", cn, cp);
#endif
	    }
	    else
	    {
	        if (cMacro->interactive)
		    TxPrintf("Interactive macro '%s' contains \"%s\"\n",
			     cn, cp);
		else
		    TxPrintf("Macro '%s' contains \"%s\"\n", cn, cp);
	    }
	    freeMagic(cn);
	    any = TRUE;
	}
	if (!any)
	{
	    if (!do_list) TxPrintf("No macros are defined for this client.\n");
	}
	return;
    }
    else if (cmd->tx_argc == (argstart + 1))
    {
	ct = MacroKey(cmd->tx_argv[argstart], &verbose);
	if (ct == 0)
	{
	    if (verbose)
		TxError("Unrecognized macro name %s\n", cmd->tx_argv[argstart]);
	    return;
	}
	if (do_help)
	    cp = MacroRetrieveHelp(wc, ct);
	else
	    cp = MacroRetrieve(wc, ct, &iReturn);
	if (cp != NULL)
	{
	    cn = MacroName(ct);
	    if (do_list)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetResult(magicinterp, cp, TCL_VOLATILE);
#else
		TxPrintf("%s\n", cp);
#endif
	    }
	    else
	    {
		if (iReturn)
		{
		    TxPrintf("Interactive macro '%s' contains \"%s\"\n",
			 	cn, cp);
		}
		else
		{
		    TxPrintf("Macro '%s' contains \"%s\"\n",
				cn, cp);
		}
	    }
	    freeMagic(cp);
	    freeMagic(cn);
	}
	return;
    }
    else if (cmd->tx_argc == (argstart + 2))
    {
	int verbose;
	ct = MacroKey(cmd->tx_argv[argstart], &verbose);
	if (ct == 0)
	{
	    if (verbose)
		TxError("Unrecognized macro name %s\n", cmd->tx_argv[argstart]);
	    return;
	}
	argstart++;
	if (cmd->tx_argv[argstart][0] == '\0') MacroDelete(wc, ct);
	else if (do_help)
	    MacroDefineHelp(wc, ct, cmd->tx_argv[argstart]);
	else if (interactive)
	    MacroDefine(wc, ct, cmd->tx_argv[argstart], NULL, TRUE);
	else 
	    MacroDefine(wc, ct, cmd->tx_argv[argstart], NULL, FALSE);
	return;
    }
    else if (cmd->tx_argc == (argstart + 3))
    {
	int verbose;
	ct = MacroKey(cmd->tx_argv[argstart], &verbose);
	if (ct == 0)
	{
	    if (verbose)
		TxError("Unrecognized macro name %s\n", cmd->tx_argv[argstart]);
	    return;
	}
	argstart++;
	if (cmd->tx_argv[argstart][0] == '\0') MacroDelete(wc, ct);
	else if (interactive) MacroDefine(wc, ct, cmd->tx_argv[argstart],
		cmd->tx_argv[argstart + 1], TRUE);
	else MacroDefine(wc, ct, cmd->tx_argv[argstart],
		cmd->tx_argv[argstart + 1], FALSE);
	return;
    }

    TxError("Usage: %s [macro_name [string] [help_text]]\n", cmd->tx_argv[0]);
}
