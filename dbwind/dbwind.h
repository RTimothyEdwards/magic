/*
 * dbwind.h --
 *
 *	Interface definitions for the 'glue' between the window
 *	manager and the database.
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
 * rcsid $Header: /usr/cvsroot/magic-8.0/dbwind/dbwind.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 */

#ifndef _DBWIND_H
#define _DBWIND_H

#include "database/database.h"
#include "windows/windows.h"

/*--------------------------- Window Client Data ----------------------------
 * The dbwind package keeps special client data that it uses to
 * manage windows on the database.
 */


typedef struct DBW1 {
    int dbw_bitmask;		/* A single bit in a word, unique between all
				 * layout windows.  Any cell that is expanded
				 * in this window has this bit set in its
				 * expand mask.
				 */
    int dbw_flags;		/* Various flags, see below. */
    int dbw_watchPlane; 	/* The plane number of a plane to watch
				 * (show tile structure)
				 */
    CellDef *dbw_watchDef;	/* The name of a celldef to watch */
    Transform dbw_watchTrans;	/* A transform to root coordinates that
				 * uniquely identifies the -position- of
				 * the cell use being watched, in the root
				 * cell of the window.
				 */
    Rect dbw_expandAmounts;	/* The sides of this rectangle are expanded
				 * out from the origin by the same amount
				 * that a redisplayed area should be expanded
				 * in order to catch all labels.  This
				 * reflects the size of the largest label
				 * displayed anywhere in the window.
				 */
    TileTypeBitMask dbw_visibleLayers;
				/* This bit mask tells which mask layers
				 * should be displayed on the screen.
				 */
    Plane *dbw_hlErase;		/* ERROR_P tiles on this plane record highlight
				 * areas that must be erased in this window,
				 * in screen coordinates.
				 */
    Plane *dbw_hlRedraw;	/* ERROR_P tiles on this plane record highlight
				 * areas that must be redrawn in this window, in
				 * root database coordinates.
				 */
    Rect dbw_gridRect;		/* Defines grid in world coordinates:  grid
				 * lines run along sides of rect, rect size
				 * determines spacing.
				 */
    int dbw_labelSize;		/* What size to use for text when drawing
				 * labels in this window (e.g. GR_TEXT_SMALL).
				 * This is recomputed each time the window
				 * is completely redrawn.  -1 means don't
				 * draw labels at all.
				 */
    Rect dbw_surfaceArea;	/* This field and the next two that follow
				 * are just copies of the corresponding
				 * fields from window records.  They're used
				 * to detect when a window has resized or
				 * rescaled.
				 */
    Point dbw_origin;
    int dbw_scale;
} DBWclientRec;

/* Flag values for dbw_flags:
 *
 * DBW_GRID:		Means grid is to be displayed in window.
 * DBW_WATCHDEMO:	Use `demo' style of watching (arrows, not addresses)
 * DBW_ALLSAME:		Means don't use different display styles for
 *			edit and other cells.
 * DBW_SEELABELS:	0 means don't display labels ever.
 * DBW_SEECELLS		0 means don't display cell names and bounding boxes
 * DBW_SEETYPES		display tiletype instead of tile address
 */

#define DBW_GRID 1
#define DBW_WATCHDEMO 2
#define DBW_ALLSAME 4
#define DBW_SEELABELS 010
#define DBW_SEECELLS  020
#define DBW_SEETYPES  040

/*
 * exported variables
 *
 */

extern WindClient DBWclientID;
extern int DBWSnapToGrid;

extern int DBWMaxTechStyles;
extern int DBWMaxTileStyles;
extern int DBWNumStyles;

extern int RtrPolyWidth, RtrMetalWidth, RtrContactWidth;

/*
 * Exported procedure headers for redisplay
 */

extern int DBWWatchTiles();
extern void DBWAreaChanged();
extern void DBWLabelChanged();
extern void DBWDrawLabel();

/*
 * Exported procedures and variables related to the technology file
 */

extern void DBWTechInitStyles();
extern bool DBWTechAddStyle();
extern char *DBWStyleType;

/* 
 * exported button procedures and variables
 */

extern void (*DBWButtonCurrentProc)();
extern void DBWAddButtonHandler();
extern char *DBWChangeButtonHandler();
extern void DBWPrintButtonDoc();
extern void DBWBoxHandler();

/* The following defines are used to indicate corner positions
 * of the box:
 */

#define TOOL_BL 0
#define TOOL_BR 1
#define TOOL_TR 2
#define TOOL_TL 3
#define TOOL_ILG -1

/* The following defines are used to indicate which coordinate system
 * the cursor box snaps to when moved with mouse clicks (values for
 * DBWSnapToGrid).
 */

#define DBW_SNAP_INTERNAL	0	/* internal units (fine grid)	*/
#define DBW_SNAP_LAMBDA		1	/* lambda units (coarse grid)	*/
#define DBW_SNAP_USER		2	/* user grid units (user grid)	*/
#define DBW_SNAP_MICRONS	3	/* micron units			*/

/* The following window mask can be used to select all database windows
 * for things like the mask parameter to DBWAreaChanged.
 */

#define DBW_ALLWINDOWS -1

extern MagWindow *ToolGetPoint();
extern MagWindow *ToolGetBoxWindow();
extern bool ToolGetBox();
extern void ToolSnapToGrid();
extern bool ToolGetEditBox(Rect *);
extern void ToolMoveBox(), ToolMoveCorner();
extern int ToolGetCorner();
extern void DBWloadWindow(), DBWxloadWindow();
extern void DBWSetBox();
extern void DBWUndoOldEdit();
extern void DBWUndoNewEdit();

/* Exported procedures for managing highlights: */

extern void DBWHLAddClient();
extern void DBWHLRemoveClient();
extern void DBWHLRedraw();
extern int DBWHLRedrawWind();
extern void DBWDrawBox();
extern void DBWDrawCrosshair();

/* Exported procedures and variables relating to feedback: */

extern int DBWFeedbackCount;
extern void DBWFeedbackClear();
extern void DBWFeedbackAdd();
extern char *DBWFeedbackNth();

/* Exported procedures and variables relating to elements: */

/* flag fields for all elements (8 bits maximum) */
#define DBW_ELEMENT_PERSISTENT	0x01
#define DBW_ELEMENT_TEXT_SIZE   0x0e	/* 3 bits (5 text sizes) */
#define DBW_ELEMENT_TEXT_POS    0xf0	/* 4 bits (9 positions) */
#define DBW_ELEMENT_LINE_HALFX	0x02	/* Add 1/2 to X position */
#define DBW_ELEMENT_LINE_HALFY	0x04	/* Add 1/2 to Y position */
#define DBW_ELEMENT_LINE_ARROWL	0x08	/* Add arrowhead left/bottom */
#define DBW_ELEMENT_LINE_ARROWR	0x10	/* Add arrowhead top/right */

extern void DBWElementAddRect();
extern void DBWElementAddLine();
extern void DBWElementAddText();
extern void DBWElementDelete();
extern void DBWElementNames();
extern void DBWElementInbox();
extern void DBWElementParseFlags();
extern char *DBWPrintElements();

/* Random procedures used internally to this module.  None of these
 * should ever need to be called by the outside world.
 */

extern void DBWCheckBoxDisplay();
extern void DBWUndoBox();
extern void DBWHLUpdate();
extern void DBWFeedbackShow();

extern void dbwElementInit();
extern void dbwCrosshairInit();

#endif /* _DBWIND_H */
