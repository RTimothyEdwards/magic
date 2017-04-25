/*
 * DBWtools.c --
 *
 * Implements tools for Magic.  Two tools are
 * provided:  a box and a point.  This module is a sort of
 * broker between the low-level display routines and the
 * high-level command routines.  It provides two kinds of
 * routines:  those invoked by the screen handler to change
 * the tool location, and those invoked by the command interpreter
 * to get information about the current tool location and relocate
 * the box.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/dbwind/DBWtools.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/styles.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "dbwind/dbwind.h"
#include "textio/textio.h"
#include "utils/main.h"
#include "textio/txcommands.h"

#define WINDOW_DEF(w)	(((CellUse *)(w->w_surfaceID))->cu_def)

/* The following rectangle defines the current box location.
 * The box is defined in terms of a particular root cell def
 * and an area within that def.  Note:  there is a notion
 * of "compatibility" between windows, as far as tools are
 * concerned.  Two windows are compatible if they have the same
 * root cell def.  If the box is placed in one window, it will
 * be displayed in all compatible windows.  The box may not have
 * one corner placed in one window and other corners in
 * incompatible windows.  
 */

static CellDef *boxRootDef = NULL;	/* CellDef for the box */
static Rect boxRootArea;		/* Root def coords */
static Point crosshairPos;		/* Crosshair position */

/*
 * If the following is DBW_SNAP_USER, the box gets snapped to the user's
 * grid always, instead of snapping to the usual 1x1 grid.  If the value
 * is DBW_SNAP_INTERNAL, the box gets snapped to the internal grid.
 */
int DBWSnapToGrid = DBW_SNAP_LAMBDA;

/* Forward reference: */

extern int DBWToolDraw();


/*
 * ----------------------------------------------------------------------------
 *
 *	toolFindPoint --
 *
 * 	Returns the point in root coordinates.
 *	If DBWSnapToGrid is DBW_SNAP_USER, pick the nearest point that is
 *	aligned with the window's grid.  If DBWSnapToGrid is DBW_SNAP_LAMBDA,
 *	pick the nearest point that is an integer lambda value.
 *
 * Results:
 *	The return value is a pointer to the window containing the
 *	point, or NULL if no window contains the point.
 *
 * Side effects:
 *	The parameters rootPoint and rootArea are modified
 *	to contain information about the point's location.  RootPoint
 *	is the nearest lambda grid point to the point's actual
 *	location, while rootArea is a one-lambda-square box surrounding
 *	the point.  If rootPoint or rootArea is NULL, then that structure
 *	isn't filled in.
 * ----------------------------------------------------------------------------
 */

MagWindow *
toolFindPoint(p, rootPoint, rootArea)
    Point *p;			/* The point to find, in the current window. */
    Point *rootPoint;		/* Modified to contain coordinates of point
				 * in root cell coordinates.  Is unchanged
				 * if NULL is returned.
				 */
    Rect *rootArea;		/* Modified to contain box around point.  Is
				 * unchanged when NULL is returned.
				 */
{
    extern MagWindow *WindCurrentWindow;

    if (WindCurrentWindow == NULL) return NULL;

    if (WindCurrentWindow->w_client != DBWclientID) return NULL;

    if (!GEO_ENCLOSE(p, &WindCurrentWindow->w_screenArea)) return NULL;

    WindPointToSurface(WindCurrentWindow, p, rootPoint, rootArea);
    if (DBWSnapToGrid != DBW_SNAP_INTERNAL)
	ToolSnapToGrid(WindCurrentWindow, rootPoint, rootArea);
    return WindCurrentWindow;
    
}


/*
 * ----------------------------------------------------------------------------
 *	ToolGetPoint --
 *
 * 	Returns information about the point.  Used by command processors.
 *
 * Results:
 *	The return value is a pointer to the window containing the
 *	point, or NULL if no window contains the point.
 *
 * Side effects:
 *	The parameters rootPoint and rootArea are modified
 *	to contain information about the point's location.  RootPoint
 *	is the nearest lambda grid point to the point's actual
 *	location, while rootArea is a one-lambda-square box surrounding
 *	the point.  If rootPoint or rootArea is NULL, then that structure
 *	isn't filled in.
 * ----------------------------------------------------------------------------
 */

MagWindow *
ToolGetPoint(rootPoint, rootArea)
    Point *rootPoint;		/* Modified to contain coordinates of point
				 * in root cell coordinates.  Is unchanged
				 * if NULL is returned.
				 */
    Rect *rootArea;		/* Modified to contain box around point.  Is
				 * unchanged when NULL is returned.
				 */
{
    extern TxCommand *WindCurrentCmd;

    if (WindCurrentCmd == NULL) 
	return NULL;
    else
	return toolFindPoint(&WindCurrentCmd->tx_p, rootPoint, rootArea);
}


/*
 * ----------------------------------------------------------------------------
 * ToolGetBox --
 *
 *	Returns the box CellDef and location in CellDef coords.
 *
 * Results:
 *	TRUE if the box exists.
 *
 * Side effects:
 *	The rootArea parameter is modified to contain the area
 *	of the box.  If rootArea is NULL, it is ignored.
 *	Same with rootDef.
 * ----------------------------------------------------------------------------
 */

bool
ToolGetBox(rootDef, rootArea)
    CellDef **rootDef;		/* Filled in with the root def of the box */
    Rect *rootArea;		/* Filled in with area of box.  Will be
				 * unchanged when NULL is returned.
				 */
{
    if (boxRootDef == NULL) return FALSE;
    if (rootDef != NULL)
	*rootDef = boxRootDef;
    if (rootArea != NULL)
	*rootArea = boxRootArea;
    return TRUE;
}


/*
 * ----------------------------------------------------------------------------
 * ToolGetBoxWindow --
 *
 * 	Returns information about the current box location.  Used by
 *	command processing routines.
 *
 * Results:
 *	The return value is a pointer to a window containing the
 *	box, or NULL if the box doesn't exist in any window.  Note:
 *	the box may actually be in more than one window, so this
 *	isn't necessarily the only window containing the box.
 *
 * Side effects:
 *	The rootArea parameter is modified to contain the area
 *	of the box.  If rootArea is NULL, it is ignored.  The
 *	integer pointed to by pMask is modified to contain a
 *	mask of all windows containing the box (there may be more
 *	than one).  If pMask is NULL, it is ignored.
 * ----------------------------------------------------------------------------
 */

static int toolMask;		/* Shared between these two routines. */

MagWindow *
ToolGetBoxWindow(rootArea, pMask)
    Rect *rootArea;		/* Filled in with area of box.  Will be
				 * unchanged when NULL is returned.
				 */
    int *pMask;			/* Filled in with mask of all windows
				 * containing box.
				 */
{
    MagWindow *window;
    extern int toolWindowSave();

    /* Search through the windows and remember a window that has
     * the right root cell.  It's important to NOT search on the
     * area of the box (i.e. take any window with the box's root
     * definition, even if the box isn't visible in the window).
     * Otherwise, some commands won't work when the box goes
     * off-screen.  Also accumulate the mask bits.
     */

    toolMask = 0;
    window = NULL;
    if (boxRootDef != NULL)
	(void) WindSearch(DBWclientID, (ClientData) NULL, (Rect *) NULL, 
	    toolWindowSave, (ClientData) &window);
    if ((window != NULL) && (rootArea != NULL)) *rootArea = boxRootArea;
    if (pMask != NULL) *pMask = toolMask;
    return window;
}

int
toolWindowSave(window, clientData)
    MagWindow *window;		/* Window that matched in some search. */
    ClientData clientData;	/* Contains the address of a location
				 * to be filled in with the window address.
				 */
{
    if (WINDOW_DEF(window) == boxRootDef)
    {
	*((MagWindow **) clientData) = window;
        toolMask |= ((DBWclientRec *) window->w_clientData)->dbw_bitmask;
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * ToolGetEditBox --
 *
 *	Fill in the location of the box in edit cell coordinates.
 *
 * Results:
 *	TRUE if the box can indeed be put into edit cell coordinates.
 *	FALSE and an error message otherwise.
 *
 * Side effects:
 *	Sets *rect to be the coordinates of the box tool in edit cell
 *	coordinates, if TRUE was returned.
 *
 *	Prints an error message if the box is not found or the box
 *	is not in the edit cell coordinate system.
 *
 * ----------------------------------------------------------------------------
 */

bool
ToolGetEditBox(rect)
    Rect *rect;
{
    if (boxRootDef == NULL) 
    {
	TxError("Box must be present\n");
	return FALSE;
    }
    if (EditRootDef != boxRootDef) 
    {
	TxError("The box isn't in a window on the edit cell.\n");
	return FALSE;
    }
    if (rect != NULL)
	GeoTransRect(&RootToEditTransform, &boxRootArea, rect);
    return TRUE;
}


/*
 * ----------------------------------------------------------------------------
 *	ToolGetCorner --
 *
 * 	Returns the corner of the box closest to a given screen location.
 *
 * Results:
 *	An integer value is returned, indicating the corner closest to
 *	the given screen location.  The point must be in a window
 *	containing a root cell use, and the box must currently be in
 *	that window, or in another window with the same root cell use.
 *	If these conditions aren't satisfied, then the lower-left corner
 *	is returned.  Note:  "closeness" is determined not by screen
 *	closeness, but by closeness in root cell coordinates.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

int
ToolGetCorner(screenPoint)
    Point *screenPoint;

{
    Point p;
    MagWindow *w;
    Rect r;

    /* Make sure that the cursor is in a valid window. */

    w = toolFindPoint(screenPoint, &p, (Rect *) NULL);
    if (w == NULL) return TOOL_BL;
    if (WINDOW_DEF(w) != boxRootDef) return TOOL_BL;

    /* Find out which corner is closest.  Consider only the
     * intersection of the box with the window (otherwise it
     * may not be possible to select off-screen corners).
     */

    WindSurfaceToScreen(w, &boxRootArea, &r);
    GeoClip(&r, &w->w_screenArea);
    if (screenPoint->p_x < ((r.r_xbot + r.r_xtop)/2))
    {
	if (screenPoint->p_y < ((r.r_ybot + r.r_ytop)/2))
	    return TOOL_BL;
	else return TOOL_TL;
    }
    else
    {
	if (screenPoint->p_y < ((r.r_ybot + r.r_ytop)/2))
	    return TOOL_BR;
	else return TOOL_TR;
    }
}


/*
 * ----------------------------------------------------------------------------
 * dbwCrosshairInit --
 *
 *	Set the initial crosshair position to MINFINITY, where it will
 *	not display until explicitly set by a command.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initializes static memory.
 *	
 * ----------------------------------------------------------------------------
 */

void
dbwCrosshairInit()
{
    crosshairPos.p_x = MINFINITY;
    crosshairPos.p_y = MINFINITY;
    DBWHLAddClient(DBWDrawCrosshair);
}

/*
 * ----------------------------------------------------------------------------
 * dbwRecordCrosshair[X,Y]Pos --
 *
 * 	This procedure tells the highlight manager that the crosshair's
 *	current position needs to be erased or redisplayed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Highlight redisplay information is logged.
 * ----------------------------------------------------------------------------
 */

void
dbwRecordCrosshairYPos(window, erase)
    MagWindow *window;
    bool erase;			/* TRUE means crossair is being erased from its
				 * current position.  FALSE means the crosshair
				 * is being added at a new position.
				 */
{
    Rect xwire;

    xwire.r_xbot = MINFINITY;
    xwire.r_xtop = INFINITY;
    xwire.r_ybot = xwire.r_ytop = crosshairPos.p_y;
    DBWHLRedraw(WINDOW_DEF(window), &xwire, erase);
}

void
dbwRecordCrosshairXPos(window, erase)
    MagWindow *window;
    bool erase;			/* TRUE means crossair is being erased from its
				 * current position.  FALSE means the crosshair
				 * is being added at a new position.
				 */
{
    Rect xwire;

    xwire.r_ybot = MINFINITY;
    xwire.r_ytop = INFINITY;
    xwire.r_xbot = xwire.r_xtop = crosshairPos.p_x;
    DBWHLRedraw(WINDOW_DEF(window), &xwire, erase);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBWDrawCrosshair --
 *
 * 	This procedure will redraw the crosshair in a given window, if
 *	the crosshair is to appear in that window.  It is called only by
 *	the highlight code.  The caller must lock the window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The crosshair is redrawn, unless it isn't supposed to appear.
 *
 * ----------------------------------------------------------------------------
 */

void
DBWDrawCrosshair(window, plane)
    MagWindow *window;		/* Window in which to redraw box. */
    Plane *plane;		/* Non-space tiles on this plane indicate
				 * which highlight areas must be redrawn.
				 */
{
    Point p;

    WindPointToScreen(window, &crosshairPos, &p);

    GrSetStuff(STYLE_YELLOW1);
    if (p.p_x > window->w_screenArea.r_xbot &&
		p.p_x < window->w_screenArea.r_xtop)
	GrClipLine(p.p_x, window->w_screenArea.r_ybot,
		   p.p_x, window->w_screenArea.r_ytop);

    if (p.p_y > window->w_screenArea.r_ybot &&
		p.p_y < window->w_screenArea.r_ytop)
	GrClipLine(window->w_screenArea.r_xbot, p.p_y,
		   window->w_screenArea.r_xtop, p.p_y);
}

/*
 * ----------------------------------------------------------------------------
 *	DBWSetCrosshair --
 *
 * 	Change the location of the crosshair.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information is recorded so that the crosshair will be redrawn.
 * ----------------------------------------------------------------------------
 */

void
DBWSetCrosshair(window, pos)
    MagWindow *window;
    Point *pos;			/* New crosshair location in coords of rootDef. */
{
    if (crosshairPos.p_x != pos->p_x)
    {
	/* Record the old and area of the vertical line for redisplay. */
	dbwRecordCrosshairXPos(window, TRUE);

	/* Update the crosshair location. */
	crosshairPos.p_x = pos->p_x;

	/* Record the new area for redisplay. */
	dbwRecordCrosshairXPos(window, FALSE);
    }

    /* Do the same thing for the horizontal crosshair line */
    if (crosshairPos.p_y != pos->p_y)
    {
	dbwRecordCrosshairYPos(window, TRUE);
	crosshairPos.p_y = pos->p_y;
	dbwRecordCrosshairYPos(window, FALSE);
    }
}

/*
 * ----------------------------------------------------------------------------
 * dbwRecordBoxArea --
 *
 * 	This procedure tells the highlight manager that the box's current
 *	area needs to be redisplayed.  It contains a special optimization
 *	for when the box is big:  record four separate areas, one along
 *	each side, instead of one huge area.  This means stuff inside
 *	the box doesn't have to be redisplayed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Highlight redisplay information is logged.
 * ----------------------------------------------------------------------------
 */

void
dbwRecordBoxArea(erase)
    bool erase;			/* TRUE means box is being erased from its
				 * current area.  FALSE means box is being
				 * added to a new area.
				 */
{
    Rect side;

    if (((boxRootArea.r_xtop - boxRootArea.r_xbot) < 20)
	    || ((boxRootArea.r_ytop - boxRootArea.r_ybot) < 20))
    {
	DBWHLRedraw(boxRootDef, &boxRootArea, erase);
    }
    else
    {
	side = boxRootArea;
	side.r_xtop = side.r_xbot + 1;
	DBWHLRedraw(boxRootDef, &side, erase);
	side = boxRootArea;
	side.r_ytop = side.r_ybot + 1;
	DBWHLRedraw(boxRootDef, &side, erase);
	side = boxRootArea;
	side.r_xbot = side.r_xtop - 1;
	DBWHLRedraw(boxRootDef, &side, erase);
	side = boxRootArea;
	side.r_ybot = side.r_ytop - 1;
	DBWHLRedraw(boxRootDef, &side, erase);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBWDrawBox --
 *
 * 	This procedure will redraw the box in a given window, if the
 *	box is to appear in that window.  It is called only by the
 *	highlight code.  The caller must lock the window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The box is redrawn, unless it isn't supposed to appear.
 *
 * ----------------------------------------------------------------------------
 */

void
DBWDrawBox(window, plane)
    MagWindow *window;		/* Window in which to redraw box. */
    Plane *plane;		/* Non-space tiles on this plane indicate
				 * which highlight areas must be redrawn.
				 */
{
    Rect screenArea;
    Rect side;
    int boxStyle = STYLE_SOLIDHIGHLIGHTS;
    extern int dbwBoxAlways1();	/* Forward reference. */

    /* Return if the window is not compatible with the box location
     * or if the box's area doesn't need to be redrawn.
     */

    if (boxRootDef != WINDOW_DEF(window)) return;
    if (!DBSrPaintArea((Tile *) NULL, plane, &boxRootArea,
	    &DBAllButSpaceBits, dbwBoxAlways1, (ClientData) NULL))
	return;

    /* Get the box coordinates in the coordinate system of the	*/
    /* edit cell.  If the box is outside of the edit cell, then	*/
    /* we draw it with style "medium_highlights" instead of	*/
    /* "solid_highlights".					*/

    if (EditRootDef == boxRootDef)
    {
	Rect editbox;
        GeoTransRect(&RootToEditTransform, &boxRootArea, &editbox);
	if (!GEO_OVERLAP(&editbox, &EditCellUse->cu_def->cd_bbox))
	    boxStyle = STYLE_MEDIUMHIGHLIGHTS;
    }

    /* Transform the box into screen coordinates, then draw it.  If the
     * box is a point, draw it as a cross (GrFastBox does this automatically)
     * and add a slightly solid center.  Otherwise, draw the box several
     * pixels wide so it will stand out from other highlights.
     */

    WindSurfaceToScreen(window, &boxRootArea, &screenArea);

    if ((screenArea.r_xbot == screenArea.r_xtop) &&
	    (screenArea.r_ybot == screenArea.r_ytop))
    {
	GrSetStuff(STYLE_OUTLINEHIGHLIGHTS);
	GrFastBox(&screenArea);
	GEO_EXPAND(&screenArea, 1, &screenArea);
	GrFastBox(&screenArea);
	return;
    }

    /* One more optimization here:  if the box is not flattened to
     * a line, but is so skinny that widening it will make it a solid
     * blob, then don't do the widening.  This is to make the box more
     * useable when features are very small.
     */
    
    if (((screenArea.r_xtop != screenArea.r_xbot) &&
	    (screenArea.r_xtop < screenArea.r_xbot + 4))
	    || ((screenArea.r_ytop != screenArea.r_ybot) &&
	    (screenArea.r_ytop < screenArea.r_ybot + 4)))
    {
	GrClipBox(&screenArea, STYLE_OUTLINEHIGHLIGHTS);
	return;
    }

    GrSetStuff(boxStyle);
    if ((screenArea.r_xbot >= window->w_screenArea.r_xbot) &&
	(screenArea.r_xbot <= window->w_screenArea.r_xtop))
    {
	side = screenArea;
	side.r_xtop = side.r_xbot + 2 - GrPixelCorrect;
	if (side.r_ytop != side.r_ybot) {
	    GrFastBox(&side);
	}
    }
    if ((screenArea.r_ybot >= window->w_screenArea.r_ybot) &&
	(screenArea.r_ybot <= window->w_screenArea.r_ytop))
    {
	side = screenArea;
	side.r_ytop = side.r_ybot + 1;
	if (!GrPixelCorrect) side.r_ybot--;
	if (side.r_xtop != side.r_xbot) {
	    GrFastBox(&side);
	}
    }
    if ((screenArea.r_xtop >= window->w_screenArea.r_xbot) &&
	(screenArea.r_xtop <= window->w_screenArea.r_xtop))
    {
	side = screenArea;
	side.r_xbot = side.r_xtop - 1;
	if (!GrPixelCorrect) side.r_xtop++;
	if (side.r_ytop != side.r_ybot) {
	    GrFastBox(&side);
	}
    }
    if ((screenArea.r_ytop >= window->w_screenArea.r_ybot) &&
	(screenArea.r_ytop <= window->w_screenArea.r_ytop))
    {
	side = screenArea;
	side.r_ybot = side.r_ytop - 2 + GrPixelCorrect;
	if (side.r_xtop != side.r_xbot) {
	    GrFastBox(&side);
	}
    }
}

int
dbwBoxAlways1()
{
    return 1;
}

/*
 * ----------------------------------------------------------------------------
 *	DBWSetBox --
 *
 * 	Change the location and/or size of the box.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information is recorded so that the box will be redrawn.
 * ----------------------------------------------------------------------------
 */

void
DBWSetBox(rootDef, rect)
    CellDef *rootDef;		/* Root definition in whose coordinate system
				 * the box is defined.  It will appear in all
				 * windows with this as root cell.
				 */
    Rect *rect;			/* New box location in coords of rootDef. */
{
    /* Record the old and area of the box for redisplay. */

    dbwRecordBoxArea(TRUE);

    /* Save information for undo-ing. */

    DBWUndoBox(boxRootDef, &boxRootArea, rootDef, rect);

    /* Update the box location. */

    boxRootDef = rootDef;
    boxRootArea = *rect;

    /* Record the new area for redisplay. */

    dbwRecordBoxArea(FALSE);
}

/*
 * ----------------------------------------------------------------------------
 *	ToolMoveBox --
 *
 * 	Repositions the box by one of its corners.
 *	If the point given to reposition the box is in screen coordinates,
 *	the box corner is snapped to the user's grid (set with the :grid
 *	command) if DBWSnapToGrid is DBW_SNAP_USER.  If DBWSnapToGrid is
 *	DBW_SNAP_LAMBDA, the box corner is snapped to the nearest integer
 *	lambda value. 
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The box is repositioned so that the given corner is at the
 *	given screen or root cell position.  If the point is given
 *	in screen coordinates, it must fall within the active area
 *	of a window with a non-NULL root cell use.  An error message
 *	is output if this can't be done.  The size of the box isn't
 *	changed.  The box is marked for redisplay, but isn't actually
 *	redisplayed on the screen.
 * ----------------------------------------------------------------------------
 */

void
ToolMoveBox(corner, point, screenCoords, rootDef)
    int corner;			/* Specifies a corner in the format
				 * returned by ToolGetCorner.
				 */
    Point *point;		/* New position of corner, in screen
				 * coordinates.
				 */
    int screenCoords;		/* TRUE means point is in screen coordinates,
				 * FALSE means root cell coordinates.
				 */
    CellDef *rootDef;		/* Used only when screenCoords = FALSE, to
				 * give root cell def.
				 */
{
    Point p;
    MagWindow *w;
    int x, y;
    CellDef *newDef;
    Rect newArea;

    /* Find the current point location. */

    if (screenCoords)
    {
	w = toolFindPoint(point, &p, (Rect *) NULL);
	if ((w == NULL) || (w->w_client != DBWclientID))
	{
	    TxError("Can't put box there.\n");
	    return;
	}
	newDef = WINDOW_DEF(w);
    }
    else
    {
	p = *point;
	newDef = rootDef;
    }

    /* Move the box.  If an illegal corner is specified, then
     * move by the bottom-left corner.
     */

    switch (corner)
    {
	case TOOL_BL:
	    x = p.p_x - boxRootArea.r_xbot;
	    y = p.p_y - boxRootArea.r_ybot;
	    break;
	case TOOL_BR:
	    x = p.p_x - boxRootArea.r_xtop;
	    y = p.p_y - boxRootArea.r_ybot;
	    break;
	case TOOL_TR:
	    x = p.p_x - boxRootArea.r_xtop;
	    y = p.p_y - boxRootArea.r_ytop;
	    break;
	case TOOL_TL:
	    x = p.p_x - boxRootArea.r_xbot;
	    y = p.p_y - boxRootArea.r_ytop;
	    break;
	default:
	    x = p.p_x - boxRootArea.r_xbot;
	    y = p.p_y - boxRootArea.r_ybot;
	    break;
    }
    newArea = boxRootArea;
    newArea.r_xbot += x;
    newArea.r_ybot += y;
    newArea.r_xtop += x;
    newArea.r_ytop += y;

    DBWSetBox(newDef, &newArea);
}


/*
 * ----------------------------------------------------------------------------
 *	ToolMoveCorner --
 *
 * 	This procedure moves one corner of the box, leaving the other
 *	corners as fixed as possible.  Invoked by low-level screen
 *	handler.
 *
 *	If the point given to reposition the box is in screen coordinates,
 *	the box corner is snapped to the user's grid (set with the :grid
 *	command) if DBWSnapToGrid is DBW_SNAP_USER.  If DBWSnapToGrid is
 *	DBW_SNAP_LAMBDA, the box corner is snapped to the nearest integer
 *	lambda value.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The box is modified so that the given corner is at the given
 *	point.  As with ToolMoveBox, the point may either be given as
 *	a screen location, or as a location in the coordinates of a
 *	root cell.  If the new corner location is indicated in a window
 *	that isn't compatible with the current box window, then the
 *	whole box is moved.  Otherwise, the corner diagonally opposite
 *	the given corner isn't moved at all.
 * ----------------------------------------------------------------------------
 */

void
ToolMoveCorner(corner, point, screenCoords, rootDef)
    int corner;			/* The corner to be moved, in format
				 * returned by ToolGetCorner.
				 */
    Point *point;		/* Destination of corner. */
    int screenCoords;		/* TRUE means point is in screen coordinates,
				 * we look up window and translate to root
				 * cell coordinates.  FALSE means point is in
				 * coordinates of rootDef.
				 */
    CellDef *rootDef;		/* Root cell Def if screenCoords = FALSE,
				 * unused otherwise.
				 */
{
    Point p;
    MagWindow *w;
    CellDef *oldDef, *newDef;
    int tmp;
    Rect newArea;

    /* Find the current point location. */

    oldDef = boxRootDef;
    if (screenCoords)
    {
	w = toolFindPoint(point, &p, (Rect *) NULL);
	if ((w == NULL) || (w->w_client != DBWclientID))
	{
	    TxError("Can't put box there.\n");
	    return;
	}
	newDef = WINDOW_DEF(w);
    }
    else
    {
	p = *point;
	newDef = rootDef;
    }

    /* If the root def for the moved corner isn't the same as the
     * current box root def, then just move the whole durned box.
     * Also move the whole box if a weird corner is specified.
     */
    
    if ((newDef != oldDef) || (corner < 0) || (corner > TOOL_TL))
    {
	ToolMoveBox(corner, &p, FALSE, newDef);
	return;
    }

    /* Move the requested corner. */

    newArea = boxRootArea;
    switch (corner)
    {
	case TOOL_BL:
	    newArea.r_xbot = p.p_x;
	    newArea.r_ybot = p.p_y;
	    break;
	case TOOL_BR:
	    newArea.r_xtop = p.p_x;
	    newArea.r_ybot = p.p_y;
	    break;
	case TOOL_TR:
	    newArea.r_xtop = p.p_x;
	    newArea.r_ytop = p.p_y;
	    break;
	case TOOL_TL:
	    newArea.r_xbot = p.p_x;
	    newArea.r_ytop = p.p_y;
	    break;
    }

    /* If the movement turned the box inside out, turn it right
     * side out again.
     */
    
    if (newArea.r_xbot > newArea.r_xtop)
    {
	tmp = newArea.r_xtop;
	newArea.r_xtop = newArea.r_xbot;
	newArea.r_xbot = tmp;
    }
    if (newArea.r_ybot > newArea.r_ytop)
    {
	tmp = newArea.r_ytop;
	newArea.r_ytop = newArea.r_ybot;
	newArea.r_ybot = tmp;
    }

    DBWSetBox(newDef, &newArea);
}

/*
 * ----------------------------------------------------------------------------
 *
 * ToolSnapToGrid --
 *
 * Snap the point *p (in root cell coordinates) to the nearest
 * point in the user-defined grid or the nearest integer lambda value,
 * according to the setting of DBWSnapToGrid.  Also translates the rectangle
 * *rEnclose by the same amount by which the point *p was snapped.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies *p.
 * ----------------------------------------------------------------------------
 */

void
ToolSnapToGrid(w, p, rEnclose)
    MagWindow *w;
    Point *p;
    Rect *rEnclose;
{
    DBWclientRec *crec = (DBWclientRec *) w->w_clientData;
    Rect *r;
    Rect lr;
    int xd, yd, xlo, xhi, ylo, yhi, xtmp, ytmp;

    if (crec == NULL || p == NULL)
	return;

    if (DBWSnapToGrid == DBW_SNAP_LAMBDA)
    {
	lr.r_xbot = lr.r_ybot = 0;
	lr.r_xtop = DBLambda[1] / DBLambda[0];
	if (lr.r_xtop < 1) lr.r_xtop = 1;
	lr.r_ytop = lr.r_xtop;
	r = &lr;
    }
    else
        r = &crec->dbw_gridRect;

    xd = r->r_xtop - r->r_xbot;
    yd = r->r_ytop - r->r_ybot;

    /*
     * The following is tricky because we want to ensure we bracket
     * the point p.
     */
    xtmp = p->p_x - r->r_xbot;
    if (xtmp < 0)
    {
	xhi = xd * (xtmp / xd) + r->r_xbot;
	xlo = xhi - xd;
    }
    else
    {
	xlo = xd * (xtmp / xd) + r->r_xbot;
	xhi = xlo + xd;
    }

    ytmp = p->p_y - r->r_ybot;
    if (ytmp < 0)
    {
	yhi = yd * (ytmp / yd) + r->r_ybot;
	ylo = yhi - yd;
    }
    else
    {
	ylo = yd * (ytmp / yd) + r->r_ybot;
	yhi = ylo + yd;
    }

    xtmp = (ABSDIFF(p->p_x, xlo) < ABSDIFF(p->p_x, xhi)) ? xlo : xhi;
    ytmp = (ABSDIFF(p->p_y, ylo) < ABSDIFF(p->p_y, yhi)) ? ylo : yhi;

    if (rEnclose)
    {
	rEnclose->r_xbot += xtmp - p->p_x;
	rEnclose->r_ybot += ytmp - p->p_y;
	rEnclose->r_xtop += xtmp - p->p_x;
	rEnclose->r_ytop += ytmp - p->p_y;
    }
    p->p_x = xtmp;
    p->p_y = ytmp;
}
