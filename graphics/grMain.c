/* grMain.c -
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
 *
 * This file contains a few core variables and routines for
 * manipulating color graphics displays.  Its main function is
 * to provide a central dispatch point to various routines for
 * different display types.
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/graphics/grMain.c,v 1.4 2010/06/24 12:37:18 tim Exp $";
#endif  /* not lint */

/*
 * The following display types are currently suported by Magic:
 *
 *	NULL		A null device for running Magic without using
 *			a graphics display.  This device does nothing
 *			when its routines are called.
 *
 *	X11		A port to the X11 window system, based on the Stanford
 *	XWIND		X10 driver, mods done at Brown University, an X11 port
 *			done at the University of Washington, and the X10a
 *			driver from Lawrence Livermore Labs.  This driver was
 *			developed by Don Stark (Stanford & decwrl).
 *	8BIT		X11 driver, force 8-bit graphics mode.
 *	16BIT		X11 driver, force 16-bit graphics mode.
 *	24BIT		X11 driver, force 24-bit graphics mode.
 *
 *	OpenGL		A port to OpenGL or Mesa.  Developed by Tim Edwards
 *			(Johns Hopkins University Applied Physics Lab)
 *
 * To port Magic to another type of display, you need to add its name to
 * the table 'grDisplayTypes' and then add a pointer to an initialization
 * routine to 'grInitProcs'.  The initialization routine will fill in all
 * of the graphics routine pointers so that they point to procedures that
 * can handle the new display type.  All calls to device-specific 
 * procedures are made by indirecting through these pointers.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <errno.h>
#include <ctype.h>

#include "utils/magic.h"
#include "utils/magsgtty.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "utils/hash.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "graphics/graphicsInt.h"

#define FAVORITE_DISPLAY	"NULL"	/* Default display type */

/* Correction between real-valued coordinate systems and pixel-based
 * coordinate systems, which can disagree by a pixel on the width of
 * polygons and position of lines.
 */
global int GrPixelCorrect = 1;

/* The following rectangle is describes the display area and is available
 * to the user of this module.
 */
global Rect GrScreenRect = {0, 0, 0, 0};

/*
 * Interrupt status for the timer.  In TCL, this is used for graphics
 * interrupts to periodically check the X11 queue for pending events.
 * In both TCL and non-TCL versions, it can be used for a general-
 * purpose interrupt timer.
 */
global unsigned char GrDisplayStatus = DISPLAY_IDLE;

/* The first of the following tables defines the legal
 * display types and the second table defines an
 * initialization routine for each type.
 *
 * These entries MUST be all upper case, since what the user types will
 * be converted to upper case before comparison.
 */

static char *grDisplayTypes[] = {
#ifdef	X11
    "XWIND",
    "X11", 	
    "8BIT",
    "16BIT",
    "24BIT",
#endif
#ifdef  OGL
    "OPEN_GL",
    "OGL",
    "OPENGL",
#endif
    "NULL",
    NULL};

extern bool x11SetDisplay();
extern bool oglSetDisplay();
extern bool nullSetDisplay();

static bool (*(grInitProcs[]))() = {
#ifdef	X11
    x11SetDisplay,  
    x11SetDisplay,  
    x11SetDisplay,  
    x11SetDisplay,  
    x11SetDisplay,  
#endif	/* X11 */
#ifdef  OGL
    oglSetDisplay,
    oglSetDisplay,
    oglSetDisplay,
#endif
    nullSetDisplay,
    NULL};

/* The following variables are pointers to the various graphics
 * procedures.  The macros in graphics.h cause these pointers
 * to be indirected through when calls occur to graphics procedures.
 * This indirection allows for several display types to be supported
 * by a single version of Magic.  The pointers are initially NULL,
 * but are rewritten by the various graphics initializers.
 */

void (*GrLockPtr)()		= NULL;
void (*GrUnlockPtr)()		= NULL;
bool (*GrInitPtr)()		= NULL;
void (*GrClosePtr)()		= NULL;
void (*GrSetCMapPtr)()		= NULL;

void (*GrSetCursorPtr)()	= NULL;
void (*GrTextSizePtr)()		= NULL;
void (*GrDrawGlyphPtr)()	= NULL;
void (*GrBitBltPtr)()		= NULL;
int  (*GrReadPixelPtr)()	= NULL;
void (*GrFlushPtr)()		= NULL;
bool (*GrCreateWindowPtr)()	= NULL;
void (*GrDeleteWindowPtr)()	= NULL;
void (*GrConfigureWindowPtr)()	= NULL;
void (*GrOverWindowPtr)()	= NULL;
void (*GrUnderWindowPtr)()	= NULL;
void (*GrDamagedPtr)()		= NULL;
void (*GrUpdateIconPtr)()   	= NULL;
bool (*GrEventPendingPtr)()   	= NULL;
int (*GrWindowIdPtr)()   	= NULL;
char *(*GrWindowNamePtr)()	= NULL;
bool (*GrGetCursorPosPtr)()   	= NULL;
bool (*GrGetCursorRootPosPtr)()	= NULL;

void (*GrEnableTabletPtr)()	= NULL;
void (*GrDisableTabletPtr)()	= NULL;

bool (*GrGetBackingStorePtr)() 	  = NULL;
bool (*GrScrollBackingStorePtr)() = NULL;
void (*GrPutBackingStorePtr)() 	  = NULL;
void (*GrFreeBackingStorePtr)()   = NULL;
void (*GrCreateBackingStorePtr)() = NULL;

/* variables similar to the above, except that they are only used
 * internal to the graphics package
 */
void (*grPutTextPtr)()		= NULL;
void (*grFontTextPtr)()		= NULL;
void (*grGetCharSizePtr)()	= NULL;
void (*grSetSPatternPtr)()	= NULL;
void (*grDefineCursorPtr)()	= NULL;
void (*grFreeCursorPtr)()	= NULL;
bool (*grDrawGridPtr)()		= NULL;
void (*grDrawLinePtr)()		= NULL;
void (*grSetWMandCPtr)()	= NULL;
void (*grFillRectPtr)()		= NULL;
void (*grSetStipplePtr)()	= NULL;
void (*grSetLineStylePtr)()	= NULL;
void (*grSetCharSizePtr)()	= NULL;
void (*grFillPolygonPtr)()      = NULL;

/* The following variables are set by initialization routines for the
 * various displays.  They are strings that indicate what kind of
 * dstyle, cmap and cursor files should be used for this display.  Almost
 * all of the displays are happy with the default values given below.
 * Note:  a NULL grCMapType means that this display doesn't need a
 * color map (it's black-and-white).
 */

char *grDStyleType = "7bit";
char *grCMapType = "7bit";
char *grCursorType = "bw";

int grNumBitPlanes = 0;      /* Number of bit-planes we are using. */
int grBitPlaneMask = 0;      /* Mask of the valid bit-plane bits. */

/* Procedures called just before and after Magic is suspended (via ^Z). */
extern void grNullProc();
void (*GrStopPtr)() = grNullProc;
void (*GrResumePtr)() = grNullProc;


/*---------------------------------------------------------
 * GrSetDisplay --
 *	This routine sets a display type, opens files,  and initializes the
 *	display.
 *
 * Results:
 *	TRUE is returned if the display was found and initialized
 *	successfully.  If the type didn't register, or the file is 
 *	NULL, then FALSE is returned.
 *
 * Side Effects:
 *	Tables are set up to control which display routines are
 *	used when communcating with the display.  The display
 *	is initialized and made ready for action.
 *---------------------------------------------------------
 */

bool
GrSetDisplay(type, outName, mouseName)
char *type;			/* Name of the display type. */
char *outName;			/* Filename used for communciation with 
				 * display. */
char *mouseName;		/* Filename used for communciation 
				 * with tablet. */

{
    char **ptr;
    char *cp;
    int i;
    bool res;

    if (outName == NULL) 
    {
	TxError("No graphics device specified.\n");
	return FALSE;
    }
    if (mouseName == NULL)
    {
	TxError("No mouse specified.\n");
	return FALSE;
    }

    /* Skip any white space */
    while (isspace(*type)) type++;

    /* Convert display type to upper case. */
    for (cp = type; *cp; cp++) { if (islower(*cp)) *cp = toupper(*cp); }

    /* See if the display type is in our table. */
    ptr = grDisplayTypes;
    for (i = 0; *ptr; i++)
    {
	if (strncmp(*ptr, type, strlen(*ptr)) == 0) break;
	ptr++;
    }

    /* Did we find it? */
    if (*ptr == NULL)
    {
	TxError("Unknown display type:  %s\n", type);
 	TxError("These display types are available in this version of Magic:\n");
	ptr = grDisplayTypes;
	for (i = 0; *ptr; i++)
	{
	    TxError("        %s\n", *ptr);
	    ptr++;
	}
	TxError("Use '-d NULL' if you don't need graphics.\n");
	return FALSE;
    }

    /* Call the initialization procedure. */
    res = (*(grInitProcs[i]))(type, outName, mouseName);
    if (!res) 
    {
	TxError("The graphics display couldn't be correctly initialized.\n");
	TxError("Use '-d NULL' if you don't need graphics.\n");
    }
    return res;
}

/*
 * ----------------------------------------------------------------------------
 *
 * GrIsDisplay --
 *
 *	Check if the first argument is the same type of display as the
 *	second argument.
 *
 * Results:
 *	TRUE if both strings represent the same display type, FALSE
 *	otherwise.  "same display type" is defined as both display
 *	strings in the grDisplayTypes list corresponding to the same
 *	initialization procedure in grInitProcs.
 *
 * Side Effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */
bool
GrIsDisplay(disp1, disp2)
    char *disp1, *disp2;
{
    char **ptr1, **ptr2;
    int i, j;

    /* See if the display type is in our table. */
    ptr1 = grDisplayTypes;
    for (i = 0; *ptr1; i++)
    {
	if (strncmp(*ptr1, disp1, strlen(*ptr1)) == 0) break;
	ptr1++;
    }
    if (*ptr1 == NULL)
    {
	TxError("Unknown display type:  %s\n", disp1);
	return FALSE;
    }

    ptr2 = grDisplayTypes;
    for (j = 0; *ptr2; j++)
    {
	if (strncmp(*ptr2, disp2, strlen(*ptr2)) == 0) break;
	ptr2++;
    }
    if (*ptr2 == NULL)
    {
	TxError("Unknown display type:  %s\n", disp2);
	return FALSE;
    }

    if (grInitProcs[i] == grInitProcs[j]) return TRUE;
    return FALSE;
}


/*
 * ----------------------------------------------------------------------------
 * GrGuessDisplayType --
 *
 *	Try to guess what sort of machine we are on, and set the display
 *	ports and type appropriately.  This info is overridden by
 *	$CAD_ROOT/magic/displays and by command line switches.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the strings passed in.
 * ----------------------------------------------------------------------------
 */

void
GrGuessDisplayType(graphics, mouse, display, monitor)
    char **graphics;		/* default device for sending out graphics */
    char **mouse;		/* default device for reading mouse (tablet) */
    char **display;		/* default type of device (OGL, etc...) */
    char **monitor;		/* default type of monitor (pale, std) */
{
    bool onSun;			/* Are we on a Sun? */
    bool haveX;			/* are we running under X? */
    char **ptr;

    *graphics = NULL;
    *mouse = NULL;
    *display = NULL;
    *monitor = "std";

    /* Check for signs of suntools. */
    onSun = (access("/dev/win0", 0) == 0);
    haveX = (getenv("DISPLAY") != NULL);

    if (haveX)
    {
	*mouse = *graphics = NULL;
	*display = "XWIND";
    }
    else if (onSun) {
	TxError("You are on a Sun but not running X.\n");
	*mouse = *graphics = NULL;
	*display = "NULL";
    }
    else {
	/* GUESS:  who knows, maybe a VAX? */
	*mouse = *graphics = NULL;
	*display = FAVORITE_DISPLAY;
    }

    /* If the guessed value is NOT in the known list of display types, then */
    /* choose the first display type in the list.	---Tim 3/13/00	    */

    ptr = grDisplayTypes;
    while ((*ptr != *display) && (*ptr != NULL)) ptr++;
    if ((*ptr == NULL) && (ptr != grDisplayTypes)) {
        ptr = grDisplayTypes;
	*display = *ptr;
    }
}


/*
 * ----------------------------------------------------------------------------
 * grFgets --
 *
 *	Just like fgets, except that it times out after 20 seconds, and prints
 *	a warning message.  After one second a warning message is also 
 *	printed.
 *
 * Results:
 *	Pointer to the string returned by fgets (equal to the 1st argument)
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

char *
grFgets(str, n, stream, name)
    char *str;
    int n;
    FILE *stream;
    char *name;		/* The user name of the stream, for the error msg */
{
    fd_set fn;
    char *newstr;
    struct timeval threeSec, twentySecs;

    threeSec.tv_sec = 3;	
    threeSec.tv_usec = 0;
    twentySecs.tv_sec = 20;	
    twentySecs.tv_usec = 0;

    FD_ZERO(&fn);
    FD_SET(fileno(stream), &fn);
    newstr = str;
    n--;
    if (n < 0) return (char *) NULL;

    while (n > 0)
    {
	fd_set f;
	char ch;
        int sel;

	f = fn;
	sel = select(TX_MAX_OPEN_FILES, &f, (fd_set *) NULL, (fd_set *) NULL, &threeSec);
	if  (sel == 0)
	{
	    TxError("The %s is responding slowly, or not at all.\n", name);
	    TxError("I'll wait for 20 seconds and then give up.\n");
	    f = fn;
	    sel = select(TX_MAX_OPEN_FILES, &f, (fd_set *) NULL,
			(fd_set *) NULL, &twentySecs);
	    if (sel == 0)
	    {
		TxError("The %s did not respond.\n", name);
		return (char *) NULL;
	    }
	    else if (sel < 0)
	    {
		if (errno == EINTR) {
		    TxError("Timeout aborted.\n");
		}
		else
		{
		    perror("magic");
		    TxError("Error in reading the %s\n", name);
		}
		return (char *) NULL;
	    }
	    else
		TxError("The %s finally responded.\n", name);
	}
	else if (sel < 0)
	{
	    if (errno != EINTR)
	    {
		perror("magic");
		TxError("Error in reading the %s\n", name);
		return (char *) NULL;
	    }
	    /* else try again, back to top of the loop */
	    continue;
	}

	ch = getc(stream);
	*newstr = ch;
	n--;
	newstr++;
	if (ch == '\n')
	    break;
    }

    *newstr = '\0';
    return str;
}


/*---------------------------------------------------------------------------
 * grNullProc --
 *
 *	A procedure of the type 'void' that does absolutely nothing.
 *	Used when we need to point a procedure pointer to something, but
 *	don't want it to do anything.
 *
 * Results:	
 *	None.
 *
 * Side Effects:
 *	None.
 *
 *----------------------------------------------------------------------------
 */

void
grNullProc()
{
}
