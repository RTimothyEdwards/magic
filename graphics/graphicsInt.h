/*
 * graphicsInt.h --
 *
 * Internal definitions for the graphics module.
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
 *
 * rcsid "$Header: /usr/cvsroot/magic-8.0/graphics/graphicsInt.h,v 1.3 2010/06/24 12:37:18 tim Exp $"
 */

#ifndef _GRAPHICSINT_H
#define _GRAPHICSINT_H

#include "windows/windows.h"
#include "utils/magic.h"


extern int grNumBitPlanes;
extern int grBitPlaneMask;


/* procedures */
extern void (*grPutTextPtr)();
extern void (*grFontTextPtr)();		/* Vector-drawn text */
extern void (*grSetSPatternPtr)();
extern void (*grDefineCursorPtr)();
extern void (*grFreeCursorPtr)();
extern bool (*grDrawGridPtr)();
extern void (*grDrawLinePtr)();
extern void (*grSetWMandCPtr)();
extern void (*grFillRectPtr)();
extern void (*grSetStipplePtr)();
extern void (*grSetLineStylePtr)();
extern void (*grSetCharSizePtr)();
extern void (*grFillPolygonPtr)();

extern char *grFgets();
extern void grSimpleLock(), grSimpleUnlock();
extern void grNoLock();
#define	GR_CHECK_LOCK()	{if (grLockedWindow == NULL) grNoLock();} 

/* The size of the crosses drawn for degenerate box outlines: */
#define GR_CROSSSIZE 5

/* This becomes TRUE if we should quit drawing things and return */
extern bool SigInterruptPending;

/* clipping stuff from GrLock() */
extern MagWindow *grLockedWindow;
extern Rect grCurClip;
extern LinkedRect *grCurObscure;

/* Strings used to generate file names for glyphs, colormaps, and
 * display styles.
 */

extern char *grDStyleType;
extern char *grCMapType;
extern char *grCursorType;

/*
 * Used to pass display-style information to lower levels
 * of a graphics driver.
 */
extern int grCurDStyle;

/* Number of stipples in the style file */
extern int grNumStipples;

/* Used to setup current color, etc. */
extern bool grDriverInformed;
extern void grInformDriver();

/* Macro to check for a bogusly small grid. 
 * Turn off grid if gridlines are less than 4 pixels apart.
 */
#define GRID_TOO_SMALL(x,y) ( \
     (((x) >> SUBPIXELBITS) < 4) || (((y) >> SUBPIXELBITS) < 4) \
)

#endif /* _GRAPHICSINT_H */
