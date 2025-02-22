/*
 * wireOps.c --
 *
 * This file contains the basic procedures that provide a wiring-style
 * interface for Magic.  The procedures do things like select a wiring
 * material and thickness, add a leg to a wire, etc.
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
static char rcsid[] __attribute__ ((unused)) = "$Header$";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "select/select.h"
#include "textio/textio.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "drc/drc.h"
#include "utils/main.h"
#include "wiring/wiring.h"
#include "wiring/wireInt.h"
#include "textio/txcommands.h"
#include "utils/styles.h"

/* C99 compat */
#include "utils/undo.h"

/* The following variables define the state of the wiring interface. */

TileType WireType = TT_SELECTBASE-1; /* Type of material currently selected
				      * for wiring.
				      */
int WireWidth;			/* Thickness of material to use for wiring. */
int WireLastDir;		/* Last direction in which a wire was run. */

/* The following variable is used to communicate the desired root cellDef
 * between wireFindRootWindow and wireFindRootFunc.
 */

static CellDef *wireDesiredDef;

/*
 * ----------------------------------------------------------------------------
 *	wireFindRootWindow --
 *
 * 	This is a utility procedure to find a window containing a particular
 *	definition as its root.
 *
 * Results:
 *	The return value is a pointer to a window for which the
 *	window is an instance of rootDef.  If no window is an instance
 *	of rootDef, then NULL is returned.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

MagWindow *
wireFindRootWindow(rootDef)
    CellDef *rootDef;		/* Root definition for which a root use
				 * is desired.
				 */
{
    MagWindow *mw;
    extern int wireFindRootFunc();

    mw = NULL;
    wireDesiredDef = rootDef;
    (void) WindSearch(DBWclientID, (ClientData) NULL, (Rect *) NULL,
	    wireFindRootFunc, (ClientData) &mw);
    return mw;
}

/* The following search function is called for each window.  If the
 * window's root is wireDesiredDef, then cellUsePtr is filled in
 * with the window's root cellUse (and the search is aborted).
 */

int
wireFindRootFunc(window, mwPtr)
    MagWindow *window;		/* A layout window. */
    MagWindow **mwPtr;		/* Copy layout window pointer to this */
{
    CellUse *use;

    use = (CellUse *) window->w_surfaceID;
    if (use->cu_def != wireDesiredDef) return 0;
    *mwPtr = window;
    return 1;
}

/*
 * ----------------------------------------------------------------------------
 *	WirePickType --
 *
 * 	This procedure establishes a new material for future wiring, and
 *	terminates any wires in progress.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The current wire type and width are modified.  If type is
 *	less than zero, then a new type and width are picked based
 *	on what's under the cursor, and this piece is also selected
 *	as a chunk.  Otherwise, the parameters determine the new
 *	type and width.
 * ----------------------------------------------------------------------------
 */

void
WirePickType(type, ppoint, width)
    TileType type;		/* New type of material to use for wiring.
				 * If less than zero, then pick a new type
				 * based on what's underneath the cursor.
				 */
    Point *ppoint;		/* If non-NULL, contains a point position
				 * from which to get the wire type.
				 */
    int width;			/* Width to use for future wiring.  If type
				 * is less than zero then this parameter is
				 * ignored and the width of the material
				 * underneath the cursor is used.
				 */
{
    MagWindow *w;
    Point point;
    Rect chunk, box;
    SearchContext scx;
    DBWclientRec *crec;
    TileTypeBitMask mask;
    int height;

    if (type >= 0)
    {
	WireType = type;
	WireWidth = width;
	WireLastDir = -1;
	WireRememberForUndo();
	return;
    }

    /* Find what layers are visible underneath the point.  Pick one of
     * them as the material to select.  If there are several, cycle
     * through them one at a time, starting from the last selected type
     * so each type gets a chance.
     */

    if (ppoint)
    {
	point.p_x = ppoint->p_x;
	point.p_y = ppoint->p_y;
	w = wireFindRootWindow(EditRootDef);
	scx.scx_area.r_xbot = point.p_x;
	scx.scx_area.r_ybot = point.p_y;
	scx.scx_area.r_xtop = scx.scx_area.r_xbot + 1;
	scx.scx_area.r_ytop = scx.scx_area.r_ybot + 1;
    }
    else
    {
	w = ToolGetPoint(&point, &scx.scx_area);
	if (w == NULL)
	{
	    TxError("Can't use cursor to select wiring material unless\n");
	    TxError("    cursor is in a layout window.\n");
	    return;
	}
    }
    scx.scx_use = (CellUse *) w->w_surfaceID;
    scx.scx_trans = GeoIdentityTransform;
    crec = (DBWclientRec *) w->w_clientData;
    DBSeeTypesAll(scx.scx_use, &scx.scx_area, crec->dbw_bitmask, &mask);
    TTMaskAndMask(&mask, &crec->dbw_visibleLayers);
    TTMaskAndMask(&mask, &DBAllButSpaceAndDRCBits);
    if (TTMaskIsZero(&mask))
    {
	TxError("There's no material visible underneath the cursor.\n");
	return;
    }
    for (WireType += 1; ; WireType += 1)
    {
	if (WireType >= DBNumUserLayers)
	    WireType = TT_SELECTBASE;
	if (TTMaskHasType(&mask, WireType)) break;
    }

    /* Now select a chunk underneath the cursor of the particular type. */

    SelectClear();
    SelectChunk(&scx, WireType, crec->dbw_bitmask, &chunk, FALSE);
    WireWidth = chunk.r_xtop - chunk.r_xbot;
    height = chunk.r_ytop - chunk.r_ybot;
    if (height < WireWidth) WireWidth = height;

    /* Set the box and the selection to a square chunk that indicates the
     * wire width.
     */

    if (WireWidth & 1)
    {
	GEO_EXPAND(&scx.scx_area, WireWidth/2, &box);
    }
    else
    {
	box.r_xbot = point.p_x - WireWidth/2;
	box.r_ybot = point.p_y - WireWidth/2;
	box.r_xtop = box.r_xbot + WireWidth;
	box.r_ytop = box.r_ybot + WireWidth;
    }
    if (box.r_xbot < chunk.r_xbot)
    {
	box.r_xbot = chunk.r_xbot;
	box.r_xtop = box.r_xbot + WireWidth;
    }
    if (box.r_ybot < chunk.r_ybot)
    {
	box.r_ybot = chunk.r_ybot;
	box.r_ytop = box.r_ybot + WireWidth;
    }
    if (box.r_xtop > chunk.r_xtop)
    {
	box.r_xtop = chunk.r_xtop;
	box.r_xbot = box.r_xtop - WireWidth;
    }
    if (box.r_ytop > chunk.r_ytop)
    {
	box.r_ytop = chunk.r_ytop;
	box.r_ybot = box.r_ytop - WireWidth;
    }
    SelectClear();
    scx.scx_area = box;
    TTMaskSetOnlyType(&mask, WireType);
    SelectArea(&scx, &mask, crec->dbw_bitmask, NULL);
    DBWSetBox(scx.scx_use->cu_def, &box);
    TxPrintf("Using %s wires %d units wide.\n",
	    DBTypeLongName(WireType), WireWidth);

    WireLastDir = -1;
    WireRememberForUndo();
}

/*
 * ----------------------------------------------------------------------------
 *	WireGetWidth, WireGetType --
 *
 *	Two routines used to query the internal values of the wiring
 *	mechanism.
 *
 * ----------------------------------------------------------------------------
 */

int
WireGetWidth()
{
    return WireWidth;
}

TileType
WireGetType()
{
    return WireType;
}

/*
 * ----------------------------------------------------------------------------
 *	WireAddLeg --
 *
 * 	This procedure adds a new leg to the current wire in the current
 *	wiring material (set by WirePickType).  The new leg will abut rect
 *	and extend to either point's x-coordinate or its y-coordinate,
 *	whichever results in a longer wire.  Direction can be used to
 *	force the wire to run in a particular direction.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The edit cell is modified to contain more material.
 * ----------------------------------------------------------------------------
 */

void
WireAddLeg(rect, point, direction)
    Rect *rect;			/* Describes the current (last-painted) leg
				 * of the wire, in root coordinates.  If NULL,
				 * then the box is used for this.
				 */
    Point *point;		/* Describes where to paint the wire to, in
				 * root coordinates.  If NULL, then the cursor
				 * location is used as the point.
				 */
    int direction;		/* Which direction to run the wire, in root
				 * coords.  If WIRE_CHOOSE, then pick a
				 * direction based on point.  If WIRE_VERTICAL
				 * or WIRE_HORIZONTAL, then run the wire in
				 * the indicated direction.
				 */
{
    MagWindow *w;
    Rect current, new, leg, editArea;
    CellDef *boxRootDef;
    SearchContext scx;
    Point cursorPos;
    TileTypeBitMask mask;
    int hwidth = WireWidth / 2;

    if (WireType == 0)
    {
	TxError("Can't add a new wire leg:  no wiring material selected.\n");
	return;
    }

    /* If rect isn't supplied, use the box. */

    if (rect == NULL)
    {
	rect = &current;
	if (!ToolGetBox(&boxRootDef, rect))
	{
	    TxError("No box!  Before wiring a leg, you must set the box\n");
	    TxError("    to indicate where the leg starts.\n");
	    return;
	}
	if (boxRootDef != EditRootDef)
	{
	    TxError("The box must be on the edit cell so it can be used\n");
	    TxError("    as the starting point for a wire leg.\n");
	    return;
	}
    }

    /* If no point is supplied, read it from the cursor location. */

    if (point == NULL)
    {
	MagWindow *w;
	w = ToolGetPoint(&cursorPos, (Rect *) NULL);
	if ((w == NULL) ||
		(((CellUse *) w->w_surfaceID)->cu_def != EditRootDef))
	{
	    TxError("Before wiring, must place cursor over edit cell to\n");
	    TxError("    indicate endpoint of new wire leg.\n");
	    return;
	}
	point = &cursorPos;
    }

    /* If the caller didn't provide a direction, then pick the opposite
     * direction (if this isn't the first leg of the wire), or pick
     * the direction that results in the largest amount of wiring (if
     * this is the first leg).
     */

    if (direction == WIRE_CHOOSE)
    {
	int delx, dely;
	delx = point->p_x - rect->r_xtop;
	if (delx < 0)
	{
	    delx = rect->r_xbot - point->p_x;
	    if (delx < 0) delx = 0;
	}
	dely = point->p_y - rect->r_ytop;
	if (dely < 0)
	{
	    dely = rect->r_ybot - point->p_y;
	    if (dely < 0) dely = 0;
	}
	if (delx > dely) direction = WIRE_HORIZONTAL;
	else direction = WIRE_VERTICAL;
    }

    /* Now compute the area to paint. */

    if (direction == WIRE_HORIZONTAL)
    {
	/* If the rect height is not the same as WireWidth, then center	*/
	/* the new wire segment on the rect.				*/

	if (rect->r_ytop - rect->r_ybot != WireWidth)
	{
	    int rmid = (rect->r_ytop + rect->r_ybot) / 2;
	    rect->r_ybot = rmid - hwidth;
	    rect->r_ytop = rect->r_ybot + WireWidth;

	    rmid = (rect->r_xtop + rect->r_xbot) / 2;
	    rect->r_xbot = rmid - hwidth;
	    rect->r_xtop = rect->r_xbot + WireWidth;
	}

	/* The new leg will be horizontal.  First compute its span in
	 * x, then its span in y.
	 */

	if (point->p_x > rect->r_xtop)
	{
	    new.r_xbot = rect->r_xbot;
	    new.r_xtop = point->p_x + hwidth;
	    WireLastDir = GEO_EAST;
	}
	else if (point->p_x < rect->r_xbot)
	{
	    new.r_xtop = rect->r_xtop;
	    new.r_xbot = point->p_x - hwidth;
	    WireLastDir = GEO_WEST;
	}
	else return;			/* Nothing to paint! */

	/* Hook the segment up to the nearest point along the box
	 * to where we are.  Usually the box is exactly as wide as
	 * the wires so there's no real choice.
	 */

	new.r_ybot = point->p_y - hwidth;
	if (new.r_ybot < rect->r_ybot)
	    new.r_ybot = rect->r_ybot;
	else if (new.r_ybot > rect->r_ytop - WireWidth)
	    new.r_ybot = rect->r_ytop - WireWidth;
	new.r_ytop = new.r_ybot + WireWidth;
    }
    else
    {
	/* If the rect width is not the same as WireWidth, then center	*/
	/* the new wire segment on the rect.				*/

	if (rect->r_xtop - rect->r_xbot != WireWidth)
	{
	    int rmid = (rect->r_xtop + rect->r_xbot) / 2;
	    rect->r_xbot = rmid - hwidth;
	    rect->r_xtop = rect->r_xbot + WireWidth;

	    rmid = (rect->r_ytop + rect->r_ybot) / 2;
	    rect->r_ybot = rmid - hwidth;
	    rect->r_ytop = rect->r_ybot + WireWidth;
	}

	/* The new wire segment is vertical.  See comments above (this
	 * code is just like what's up there).
	 */

	if (point->p_y > rect->r_ytop)
	{
	    new.r_ybot = rect->r_ybot;
	    new.r_ytop = point->p_y + hwidth;
	    WireLastDir = GEO_NORTH;
	}
	else if (point->p_y < rect->r_ybot)
	{
	    new.r_ytop = rect->r_ytop;
	    new.r_ybot = point->p_y - hwidth;
	    WireLastDir = GEO_SOUTH;
	}
	else return;			/* Nothing to paint! */

	new.r_xbot = point->p_x - WireWidth/2;
	if (new.r_xbot < rect->r_xbot)
	    new.r_xbot = rect->r_xbot;
	if (new.r_xbot > rect->r_xtop - WireWidth)
	    new.r_xbot = rect->r_xtop - WireWidth;
	new.r_xtop = new.r_xbot + WireWidth;
    }

    /* Paint the new leg and select it. */

    GeoTransRect(&RootToEditTransform, &new, &editArea);
    TTMaskSetOnlyType(&mask, WireType);
    DBPaintValid(EditCellUse->cu_def, &editArea, &mask, 0);
    DBAdjustLabels(EditCellUse->cu_def, &editArea);
    DBWAreaChanged(EditCellUse->cu_def, &editArea, DBW_ALLWINDOWS, &mask);
    DRCCheckThis(EditCellUse->cu_def, TT_CHECKPAINT, &editArea);
    DBReComputeBbox(EditCellUse->cu_def);

    /* Select the new wire leg, if the edit cell is visible in any
     * windows.
     */

    w = wireFindRootWindow(EditRootDef);
    scx.scx_use = w->w_surfaceID;
    if (scx.scx_use != NULL)
    {
	SelectClear();
	scx.scx_area = new;
	scx.scx_trans = GeoIdentityTransform;
	SelectChunk(&scx, WireType, 0, &leg, FALSE);
    }

    /* Make the box a square at the tip of the new area just painted. */

    switch (WireLastDir)
    {
	case GEO_NORTH:
	    if (leg.r_ybot < new.r_ybot)
		new.r_ybot = leg.r_ybot;
	    if ((new.r_ytop - new.r_ybot) > WireWidth)
		new.r_ybot = new.r_ytop - WireWidth;
	    break;
	case GEO_SOUTH:
	    if (leg.r_ytop > new.r_ytop)
		new.r_ytop = leg.r_ytop;
	    if ((new.r_ytop - new.r_ybot) > WireWidth)
		new.r_ytop = new.r_ybot + WireWidth;
	    break;
	case GEO_EAST:
	    if (leg.r_xbot < new.r_xbot)
		new.r_xbot = leg.r_xbot;
	    if ((new.r_xtop - new.r_xbot) > WireWidth)
		new.r_xbot = new.r_xtop - WireWidth;
	    break;
	case GEO_WEST:
	    if (leg.r_xtop > new.r_xtop)
		new.r_xtop = leg.r_xtop;
	    if ((new.r_xtop - new.r_xbot) > WireWidth)
		new.r_xtop = new.r_xbot + WireWidth;
	    break;
    }
    DBWSetBox(EditRootDef, &new);
    WireRememberForUndo();
}

/*
 * ----------------------------------------------------------------------------
 *	WireShowLeg --
 *
 * 	This procedure adds a new leg to the current wire in the current
 *	wiring material (set by WirePickType).  Unlike WireAddLeg(), the
 *	wire is painted into the selection def, not the edit def.  This
 *	allows the selection to be updated with cursor movement, showing
 *	where magic is going to place the next wire leg.
 *	Also, we do not print error messages, as this function may be
 *	called several times per second.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The selection cell is cleared and painted.
 * ----------------------------------------------------------------------------
 */

void
WireShowLeg()
{
    Rect current, new, leg, editArea, *rect = &current;
    CellDef *boxRootDef;
    SearchContext scx;
    Point cursorPos, *point = &cursorPos;
    TileTypeBitMask mask;
    int direction = WIRE_CHOOSE;
    int delx, dely;
    MagWindow *w;
    int hwidth = WireWidth / 2;

    if (WireType == 0) return;

    /* get the cursor box position. */

    rect = &current;
    if (!ToolGetBox(&boxRootDef, rect)) return;
    if (boxRootDef != EditRootDef) return;

    /* get the cursor location. */

    w = ToolGetPoint(&cursorPos, (Rect *) NULL);
    if ((w == NULL) ||
		(((CellUse *) w->w_surfaceID)->cu_def != EditRootDef))
	return;

    /* Pick the opposite direction (if this isn't the first leg of the
     * wire), or pick the direction that results in the largest amount
     * of wiring (if this is the first leg).
     */

    delx = point->p_x - rect->r_xtop;
    if (delx < 0)
    {
	delx = rect->r_xbot - point->p_x;
	if (delx < 0) delx = 0;
    }
    dely = point->p_y - rect->r_ytop;
    if (dely < 0)
    {
	dely = rect->r_ybot - point->p_y;
	if (dely < 0) dely = 0;
    }
    if (delx > dely) direction = WIRE_HORIZONTAL;
    else direction = WIRE_VERTICAL;

    /* Now compute the area to paint. */

    if (direction == WIRE_HORIZONTAL)
    {
	/* Correct for different width between wire and rect. */
	if (rect->r_ytop - rect->r_ybot != WireWidth)
	{
	    int rmid = (rect->r_ytop + rect->r_ybot) / 2;
	    rect->r_ybot = rmid - hwidth;
	    rect->r_ytop = rect->r_ybot + WireWidth;

	    rmid = (rect->r_xtop + rect->r_xbot) / 2;
	    rect->r_xbot = rmid - hwidth;
	    rect->r_xtop = rect->r_xbot + WireWidth;
	}

	/* The new leg will be horizontal.  First compute its span in
	 * x, then its span in y.
	 */

	if (point->p_x > rect->r_xtop)
	{
	    new.r_xbot = rect->r_xbot;
	    new.r_xtop = point->p_x + hwidth;
	    WireLastDir = GEO_EAST;
	}
	else if (point->p_x < rect->r_xbot)
	{
	    new.r_xtop = rect->r_xtop;
	    new.r_xbot = point->p_x - hwidth;
	    WireLastDir = GEO_WEST;
	}
	else return;			/* Nothing to paint! */

	/* Hook the segment up to the nearest point along the box
	 * to where we are.  Usually the box is exactly as wide as
	 * the wires so there's no real choice.
	 */

	new.r_ybot = point->p_y - hwidth;
	if (new.r_ybot < rect->r_ybot)
	    new.r_ybot = rect->r_ybot;
	else if (new.r_ybot > rect->r_ytop - WireWidth)
	    new.r_ybot = rect->r_ytop - WireWidth;
	new.r_ytop = new.r_ybot + WireWidth;
    }
    else
    {
	/* Correct for different width between wire and rect. */
	if (rect->r_xtop - rect->r_xbot != WireWidth)
	{
	    int rmid = (rect->r_xtop + rect->r_xbot) / 2;
	    rect->r_xbot = rmid - hwidth;
	    rect->r_xtop = rect->r_xbot + WireWidth;

	    rmid = (rect->r_ytop + rect->r_ybot) / 2;
	    rect->r_ybot = rmid - hwidth;
	    rect->r_ytop = rect->r_ybot + WireWidth;
	}

	/* The new wire segment is vertical.  See comments above (this
	 * code is just like what's up there).
	 */

	if (point->p_y > rect->r_ytop)
	{
	    new.r_ybot = rect->r_ybot;
	    new.r_ytop = point->p_y + hwidth;
	    WireLastDir = GEO_NORTH;
	}
	else if (point->p_y < rect->r_ybot)
	{
	    new.r_ytop = rect->r_ytop;
	    new.r_ybot = point->p_y - hwidth;
	    WireLastDir = GEO_SOUTH;
	}
	else return;			/* Nothing to paint! */

	new.r_xbot = point->p_x - hwidth;
	if (new.r_xbot < rect->r_xbot)
	    new.r_xbot = rect->r_xbot;
	if (new.r_xbot > rect->r_xtop - WireWidth)
	    new.r_xbot = rect->r_xtop - WireWidth;
	new.r_xtop = new.r_xbot + WireWidth;
    }

    /* Clear any old selection.  Ignore the Undo mechanism so	*/
    /* we don't continuously record uncommitted routes.		*/

    UndoDisable();
    SelectClear();

    /* Paint the new leg into the selection box. */

    TTMaskSetOnlyType(&mask, WireType);
    DBPaintValid(SelectDef, &new, &mask, 0);
    DBAdjustLabels(SelectDef, &new);
    DBWAreaChanged(SelectDef, &new, DBW_ALLWINDOWS, &mask);
    DBReComputeBbox(SelectDef);
    DBWHLRedraw(SelectRootDef, &new, TRUE);
    DBWAreaChanged(SelectDef, &SelectDef->cd_bbox, DBW_ALLWINDOWS,
		&DBAllButSpaceBits);
    UndoEnable();
}

/*
 * ----------------------------------------------------------------------------
 *	WireAddContact --
 *
 * 	This procedure places a contact at the end of the current wire
 *	leg in order to switch layers, and records a new routing layer
 *	and width.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A contact is placed, and the current wire type and width are
 *	modified.  The contact type is chosen based on information
 *	from the technology file and the new and old routing layers.
 * ----------------------------------------------------------------------------
 */

#define WIRING_CONTACT_UP	1
#define WIRING_CONTACT_DOWN	0

void
WireAddContact(newType, newWidth)
    TileType newType;		/* New type of material to use for wiring.
				 * If less than zero, pick a new type based
				 * on what's underneath the cursor.
				 */
    int newWidth;		/* New width to use for future wiring.  If
				 * newType is less than zero, then this
				 * parameter is ignored and the width of
				 * the material underneath the cursor is
				 * used.
				 */
{
    MagWindow *w;
    Rect oldLeg, contactArea, tmp, tmp2, editArea;
    CellDef *boxRootDef;
    TileType oldType;
    TileTypeBitMask mask, allmask;
    int conSurround1, conSurround2, conExtend1, conExtend2, conSize;
    int oldOverlap, newOverlap;
    int i, totalSize, oldDir, updown;
    Contact *contact;
    SearchContext scx;

    /* First of all, find out the location of the last wire leg,
     * which is marked by the box.
     */

    if (!ToolGetBox(&boxRootDef, &oldLeg))
    {
	TxError("No box!  To place a contact, you must first use\n");
	TxError("    the box to mark the previous leg of the wire,\n");
	TxError("    at the end of which the contact will be placed.\n");
	return;
    }
    if (boxRootDef != EditRootDef)
    {
	TxError("The box must be on the edit cell; it marks the wire\n");
	TxError("    leg at the end of which a contact will be placed.\n");
	return;
    }

    oldType = WireType;
    oldDir = WireLastDir;

    /* Now find a new type and width.  If the type is the same, then
     * there's no need to add a contact.
     */

    WirePickType(newType, (Point *)NULL, newWidth);
    if (WireType == oldType)
    {
	TxError("The new wiring layer is the same as the old one, so\n");
	TxError("    there's no need for a contact.\n");
	return;
    }

    /* Choose a contact type. */

    for (contact = WireContacts; contact != NULL; contact = contact->con_next)
    {
	if ((contact->con_layer1 == oldType) &&
		(contact->con_layer2 == WireType))
	{
	    conSurround1 = contact->con_surround1 / WireUnits;
	    if ((contact->con_surround1 % WireUnits) != 0) conSurround1++;
	    conSurround2 = contact->con_surround2 / WireUnits;
	    if ((contact->con_surround2 % WireUnits) != 0) conSurround2++;
	    conExtend1 = contact->con_extend1 / WireUnits;
	    if ((contact->con_extend1 % WireUnits) != 0) conExtend1++;
	    conExtend2 = contact->con_extend2 / WireUnits;
	    if ((contact->con_extend2 % WireUnits) != 0) conExtend2++;
	    conSize = contact->con_size / WireUnits;
	    if ((contact->con_size % WireUnits) != 0) conSize++;

	    oldOverlap = conSurround1;
	    newOverlap = conSurround2;
	    updown = WIRING_CONTACT_UP;
	    goto gotContact;
	}
	if ((contact->con_layer2 == oldType) &&
		(contact->con_layer1 == WireType))
	{
	    conSurround1 = contact->con_surround1 / WireUnits;
	    if ((contact->con_surround1 % WireUnits) != 0) conSurround1++;
	    conSurround2 = contact->con_surround2 / WireUnits;
	    if ((contact->con_surround2 % WireUnits) != 0) conSurround2++;
	    conExtend1 = contact->con_extend1 / WireUnits;
	    if ((contact->con_extend1 % WireUnits) != 0) conExtend1++;
	    conExtend2 = contact->con_extend2 / WireUnits;
	    if ((contact->con_extend2 % WireUnits) != 0) conExtend2++;
	    conSize = contact->con_size / WireUnits;
	    if ((contact->con_size % WireUnits) != 0) conSize++;

	    oldOverlap = conSurround2;
	    newOverlap = conSurround1;
	    updown = WIRING_CONTACT_DOWN;
	    goto gotContact;
	}
    }
    TxError("The technology file doesn't define a contact\n");
    TxError("    between \"%s\" and \"%s\".\n",  DBTypeLongName(oldType),
	    DBTypeLongName(WireType));
    return;

    /* Compute the contact's bounding box, including surrounds. The idea here
     * is to center the contact at the end of the leg.  With the edge of
     * the surround corresponding to the old layer just lining up with
     * the end of the leg.  The contact may have to be wider than the leg.
     * However, if the wire is very wide, make the contact so it won't
     * extend past the edge of the wire.
     */

    gotContact:
    totalSize = conSize + 2 * oldOverlap;

    /* If the contact size + overlap is less than the wire width,
     * then make the contact size equal to the wire width - the
     * overlap so that the full contact area isn't larger than
     * the exiting wire width.
     */
    if (updown == WIRING_CONTACT_UP)
    {
	if (totalSize < (WireWidth - 2 * conSurround2))
	    totalSize = WireWidth - 2 * conSurround2;
    }
    else
    {
	if (totalSize < (WireWidth - 2 * conSurround1))
	    totalSize = WireWidth - 2 * conSurround1;
    }

    contactArea = oldLeg;
    if ((contactArea.r_xtop - contactArea.r_xbot) < totalSize)
    {
	contactArea.r_xbot -= (totalSize - (contactArea.r_xtop
		- contactArea.r_xbot)) / 2;
	contactArea.r_xtop = contactArea.r_xbot + totalSize;
    }
    if ((contactArea.r_ytop - contactArea.r_ybot) < totalSize)
    {
	contactArea.r_ybot -= (totalSize - (contactArea.r_ytop
		- contactArea.r_ybot)) / 2;
	contactArea.r_ytop = contactArea.r_ybot + totalSize;
    }

    switch (oldDir)
    {
	case GEO_NORTH:
	    i = contactArea.r_ytop - totalSize;
	    if (i > contactArea.r_ybot)
		contactArea.r_ybot = i;
	    break;
	case GEO_SOUTH:
	    i = contactArea.r_ybot + totalSize;
	    if (i < contactArea.r_ytop)
		contactArea.r_ytop = i;
	    break;
	case GEO_EAST:
	    i = contactArea.r_xtop - totalSize;
	    if (i > contactArea.r_xbot)
		contactArea.r_xbot = i;
	    break;
	case GEO_WEST:
	    i = contactArea.r_xbot + totalSize;
	    if (i < contactArea.r_xtop)
		contactArea.r_xtop = i;
	    break;
    }

    /* Paint the contact and its surrounding areas. */

    GeoTransRect(&RootToEditTransform, &contactArea, &editArea);
    GEO_EXPAND(&editArea, -oldOverlap, &tmp);
    TTMaskSetOnlyType(&mask, contact->con_type);
    TTMaskSetOnlyType(&allmask, contact->con_type);
    DBPaintValid(EditCellUse->cu_def, &tmp, &mask, 0);
    if (conSurround1 != 0)
    {
	TTMaskSetOnlyType(&mask, contact->con_layer1);
	TTMaskSetType(&allmask, contact->con_layer1);
	GEO_EXPAND(&tmp, conSurround1, &tmp2);
	(void) GeoInclude(&tmp2, &editArea);
	DBPaintValid(EditCellUse->cu_def, &tmp2, &mask, 0);
    }
    if (conSurround2 != 0)
    {
	TTMaskSetOnlyType(&mask, contact->con_layer2);
	TTMaskSetType(&allmask, contact->con_layer2);
	GEO_EXPAND(&tmp, conSurround2, &tmp2);
	(void) GeoInclude(&tmp2, &editArea);
	DBPaintValid(EditCellUse->cu_def, &tmp2, &mask, 0);
    }
    if (conExtend1 != 0)
    {
	TTMaskSetOnlyType(&mask, contact->con_layer1);
	TTMaskSetType(&allmask, contact->con_layer1);
	tmp2 = tmp;
	switch(oldDir)
	{
	    case GEO_NORTH:
	    case GEO_SOUTH:
		if (updown == WIRING_CONTACT_UP)
		{
		    tmp2.r_ybot -= conExtend1;
		    tmp2.r_ytop += conExtend1;
		    tmp2.r_xbot -= conSurround1;
		    tmp2.r_xtop += conSurround1;
		}
		else {
		    tmp2.r_xbot -= conExtend1;
		    tmp2.r_xtop += conExtend1;
		    tmp2.r_ybot -= conSurround1;
		    tmp2.r_ytop += conSurround1;
		}
		break;
	    case GEO_EAST:
	    case GEO_WEST:
		if (updown == WIRING_CONTACT_UP)
		{
		    tmp2.r_xbot -= conExtend1;
		    tmp2.r_xtop += conExtend1;
		    tmp2.r_ybot -= conSurround1;
		    tmp2.r_ytop += conSurround1;
		}
		else {
		    tmp2.r_ybot -= conExtend1;
		    tmp2.r_ytop += conExtend1;
		    tmp2.r_xbot -= conSurround1;
		    tmp2.r_xtop += conSurround1;
		}
		break;
	}
	(void) GeoInclude(&tmp2, &editArea);
	DBPaintValid(EditCellUse->cu_def, &tmp2, &mask, 0);
    }
    if (conExtend2 != 0)
    {
	TTMaskSetOnlyType(&mask, contact->con_layer2);
	TTMaskSetType(&allmask, contact->con_layer2);
	tmp2 = tmp;
	switch(oldDir)
	{
	    case GEO_NORTH:
	    case GEO_SOUTH:
		if (updown == WIRING_CONTACT_UP)
		{
		    tmp2.r_xbot -= conExtend2;
		    tmp2.r_xtop += conExtend2;
		    tmp2.r_ybot -= conSurround2;
		    tmp2.r_ytop += conSurround2;
		}
		else {
		    tmp2.r_ybot -= conExtend2;
		    tmp2.r_ytop += conExtend2;
		    tmp2.r_xbot -= conSurround2;
		    tmp2.r_xtop += conSurround2;
		}
		break;
	    case GEO_EAST:
	    case GEO_WEST:
		if (updown == WIRING_CONTACT_UP)
		{
		    tmp2.r_ybot -= conExtend2;
		    tmp2.r_ytop += conExtend2;
		    tmp2.r_xbot -= conSurround2;
		    tmp2.r_xtop += conSurround2;
		}
		else {
		    tmp2.r_xbot -= conExtend2;
		    tmp2.r_xtop += conExtend2;
		    tmp2.r_ybot -= conSurround2;
		    tmp2.r_ytop += conSurround2;
		}
		break;
	}
	(void) GeoInclude(&tmp2, &editArea);
	DBPaintValid(EditCellUse->cu_def, &tmp2, &mask, 0);
    }

    DBAdjustLabels(EditCellUse->cu_def, &editArea);
    DBWAreaChanged(EditCellUse->cu_def, &editArea, DBW_ALLWINDOWS, &allmask);
    DRCCheckThis(EditCellUse->cu_def, TT_CHECKPAINT, &editArea);
    DBReComputeBbox(EditCellUse->cu_def);

    /* Select the contact and its surrounds, if the edit cell is
     * in a window.
     */

    SelectClear();
    w = wireFindRootWindow(EditRootDef);
    scx.scx_use = w->w_surfaceID;
    if (scx.scx_use != NULL)
    {
	scx.scx_trans = GeoIdentityTransform;
	GEO_EXPAND(&contactArea, -oldOverlap, &tmp);
	scx.scx_area = tmp;
	TTMaskSetOnlyType(&mask, contact->con_type);
	SelectArea(&scx, &mask, 0, NULL);
	if (conSurround1 != 0)
	{
	    GEO_EXPAND(&tmp, conSurround1, &scx.scx_area);
	    TTMaskSetOnlyType(&mask, contact->con_layer1);
	    SelectArea(&scx, &mask, 0, NULL);
	}
	if (conSurround2 != 0)
	{
	    GEO_EXPAND(&tmp, conSurround2, &scx.scx_area);
	    TTMaskSetOnlyType(&mask, contact->con_layer2);
	    SelectArea(&scx, &mask, 0, NULL);
	}
    }

    /* Place the box over the overlap area of the new routing material. */

    GEO_EXPAND(&tmp, newOverlap, &tmp2);
    DBWSetBox(EditRootDef, &tmp2);
}

/*
 * ----------------------------------------------------------------------------
 *
 * WireButtonProc --
 *
 * 	This procedure implements a button-based wiring interface.  It
 *	is registered as a client of the dbwind button manager and is
 *	called automatically by dbwind when buttons are pushed and this
 *	handler is the active one.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Left button:	same as ":wire type" (pick wiring layer and width)
 *	Right button:	same as ":wire leg" (add a new leg to the wire)
 *	Middle button:  same as ":wire switch" (place contact to new layer)
 *
 * ----------------------------------------------------------------------------
 */

void
WireButtonProc(w, cmd)
    MagWindow *w;			/* Window in which button was pushed. */
    TxCommand *cmd;		/* Describes exactly what happened. */
{
    /* We do commands on the down-pushes and ignore the releases. */

    if (cmd->tx_buttonAction != TX_BUTTON_DOWN)
	return;

    switch (cmd->tx_button)
    {
	case TX_LEFT_BUTTON:
	    WirePickType(-1, (Point *)NULL, 0);
	    break;
	case TX_RIGHT_BUTTON:
	    WireAddLeg((Rect *) NULL, (Point *) NULL, WIRE_CHOOSE);
	    break;
	case TX_MIDDLE_BUTTON:
	    WireAddContact(-1, 0);
	    break;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * WireInit --
 *
 * 	This procedure is called when Magic starts up to initialize
 *	the wiring module.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Registers various things with various managers.
 *
 * ----------------------------------------------------------------------------
 */

void
WireInit()
{
    static char *doc =
"You are currently using the \"wiring\" tool.  The button actions are:\n\
   left    - pick a wiring layer and width (same as \":wire type\")\n\
   right   - add a leg to the wire (same as \":wire leg\")\n\
   middle  - place a contact to switch layers (same as \":wire switch\")\n";

    WireUndoInit();
    DBWAddButtonHandler("wiring", WireButtonProc, STYLE_CURS_ARROW, doc);
}
