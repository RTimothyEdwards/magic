/*
 * windInt.h --
 *
 *	Internal definitions for the window package.
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
 * rcsid $Header: /usr/cvsroot/magic-8.0/windows/windInt.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 *
 */

#ifndef _WINDINT_H
#define _WINDINT_H

#include "windows/windows.h"
#include "graphics/glyphs.h"

/* ----------------- data structures ----------------- */
typedef struct WIND_S3 {
    char *w_clientName;
    bool (*w_create)();
    bool (*w_delete)();
    void (*w_redisplay)();
    void (*w_command)();
    void (*w_update)();
    bool (*w_exit)();
    void (*w_reposition)();	/* called when a window moves or changes size */
    GrGlyph *w_icon;
    char **w_commandTable;
    void (**w_functionTable)();
    struct WIND_S3 *w_nextClient;
} clientRec;

/* ----------------- variables ----------------- */
extern MagWindow *windTopWindow;
extern MagWindow *windBottomWindow;
extern clientRec *windFirstClientRec;
extern char *butTable[];
extern char *actTable[];
extern bool windPrintCommands;

/* ----------------- procedures ----------------- */
extern void windDisplayArea();
extern void windPrintCommand();
extern void windSetPoint();
extern void windDump();
extern void windClientInit();
extern MagWindow *windSearchPoint();

/* ----------------- constants ----------------- */

/* the width of window borders */
extern int windCaptionPixels;
#define THIN_LINE	((((w == NULL) ? WindDefaultFlags \
	: (w)->w_flags) & WIND_BORDER) ? 2 : 0)	
#define TOP_BORDER(w)	((((w == NULL) ? WindDefaultFlags \
	: (w)->w_flags) & WIND_CAPTION) \
	? windCaptionPixels : 2*THIN_LINE)
#define BOT_BORDER(w)	((((w == NULL) ? WindDefaultFlags \
	: (w)->w_flags) & WIND_SCROLLBARS) \
	? 2*THIN_LINE + WindScrollBarWidth : 2*THIN_LINE)
#define LEFT_BORDER(w)	((((w == NULL) ? WindDefaultFlags \
	: (w)->w_flags) & WIND_SCROLLBARS) \
	? 2*THIN_LINE + WindScrollBarWidth : 2*THIN_LINE)
#define RIGHT_BORDER(w)	2*THIN_LINE

/* Always leave room for the borders plus 25 pixels */
#define WIND_MIN_WIDTH	(6*THIN_LINE + 3*WindScrollBarWidth + 25)
#define WIND_MIN_HEIGHT	(windCaptionPixels + 4*THIN_LINE + \
	3*WindScrollBarWidth + 25)

#define DEFAULT_CLIENT	"layout"
#define WINDOW_CLIENT	"*window"

/* Default size for new windows. */

#define CREATE_HEIGHT	300
#define CREATE_WIDTH	300

#endif /* _WINDINT_H */
