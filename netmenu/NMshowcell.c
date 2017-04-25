/* NMshowcell.c -
 *
 *	This file provides procedures that highlight all the paint
 *	in a given cell by drawing it on the highlight layer.  It
 *	is used for things like displaying all the wiring in a net,
 *	or for displaying splotches around labels with a given name.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/netmenu/NMshowcell.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "dbwind/dbwind.h"
#include "utils/styles.h"
#include "textio/textio.h"
#include "utils/main.h"
#include "netmenu/nmInt.h"

/* The following owns describe the cell currently being highlighted,
 * and the root cell that identifies windows in which the highlighting
 * is to occur.  The cell's coordinate system is assumed to be the
 * same as the coordinate system of the root cell.
 */

static CellUse *nmscUse = NULL;
static CellDef *nmscRootDef = NULL;	/* NULL means no cell currently
					 * being highlighted.
					 */

/* The use and def below are used for highlighting nets and
 * labels.
 */

static CellUse *nmscShowUse = NULL;
static CellDef *nmscShowDef = NULL;

/*
 * ----------------------------------------------------------------------------
 *
 * NMRedrawCell --
 *
 * 	This procedure is invoked by the highlight code to redraw
 *	the highlights managed by this file.  The window has been
 *	already locked by the highlight code.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Highlights are redrawn, if there is a cell to be highlighted
 *	and if it contains information in the area to be redrawn.
 *
 * ----------------------------------------------------------------------------
 */

static Plane *nmscPlane;	/* Shared between procs below. */
extern int nmscRedrawFunc();	/* Forward declaration. */

int
NMRedrawCell(window, plane)
    MagWindow *window;		/* Window in which to redisplay. */
    Plane *plane;		/* Non-space tiles on this plane indicate,
				 * in root cell coordinates, the areas where
				 * highlight information must be redrawn.
				 */
{
    int i;
    Rect area;

    /* Make sure that cell highlights are supposed to appear in
     * this window.
     */

    if (((CellUse *)(window->w_surfaceID))->cu_def != nmscRootDef) return 0;

    /* If this window is zoomed way out (less than 1 pixel per lambda)
     * use solid highlighting to maximize visibility.  It the window
     * is at a reasonably high magnification, then use a paler stipple
     * so that the material type is easy to see through the highlighting.
     */
    
    if (window->w_scale > SUBPIXEL)
	GrSetStuff(STYLE_PALEHIGHLIGHTS);
    else 
	GrSetStuff(STYLE_SOLIDHIGHLIGHTS);

    /* Find all paint on all layers in the area where we may have to
     * redraw.
     */

    if (!DBBoundPlane(plane, &area)) return 0;
    nmscPlane = plane;
    for (i = PL_TECHDEPBASE; i < DBNumPlanes; i += 1)
    {
	(void) DBSrPaintArea((Tile *) NULL, nmscUse->cu_def->cd_planes[i],
	    &area, &DBAllButSpaceAndDRCBits,
	    nmscRedrawFunc, (ClientData) window);
    }
    return 0;
}

int
nmscRedrawFunc(tile, window)
    Tile *tile;			/* Tile to be redisplayed on highlight layer.*/
    MagWindow *window;		/* Window in which to redisplay. */
{
    Rect area, screenArea;
    extern int nmscAlways1();	/* Forward reference. */

    TiToRect(tile, &area);
    if (!DBSrPaintArea((Tile *) NULL, nmscPlane, &area,
	    &DBAllButSpaceBits, nmscAlways1, (ClientData) NULL))
	return 0;
    WindSurfaceToScreen(window, &area, &screenArea);
    GrFastBox(&screenArea);
    return 0;
}

int
nmscAlways1()
{
    return 1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMUnsetCell --
 *
 * 	This procedure causes us to forget about the cell currently
 *	being highlighted, and erase the highlights from the screen.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	From now on, no cell will be highlighted.  The screen is
 *	redrawn to remove existing cell highlights.
 *
 * ----------------------------------------------------------------------------
 */

void
NMUnsetCell()
{
    CellDef *oldDef;

    if (nmscRootDef == NULL) return;
    oldDef = nmscRootDef;
    nmscRootDef = NULL;
    DBWHLRedraw(oldDef, &nmscUse->cu_def->cd_bbox, TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMShowCell --
 *
 * 	This procedure sets up a particular cell to be highlighted,
 *	and draws its paint on the highlight layer.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If there was already a cell being highlighted, it is unset
 *	and erased.  Then the new cell is remembered, and the screen
 *	is redisplayed.
 *
 * Notes:
 *	When using this, make sure the highlight use's bounds are
 *	properly recomputed.
 *
 * ----------------------------------------------------------------------------
 */

void
NMShowCell(use, rootDef)
    CellUse *use;		/* Cell whose contents are to be drawn
				 * on the highlight plane.
				 */
    CellDef *rootDef;		/* Highlights will appear in all windows
				 * with this root definition.
				 */
{
    if (nmscRootDef != NULL) NMUnsetCell();
    nmscRootDef = rootDef;
    nmscUse = use;
    DBWHLRedraw(nmscRootDef, &nmscUse->cu_def->cd_bbox, FALSE);
}

/*
 * ----------------------------------------------------------------------------
 *
 *  nmGetShowCell --
 *
 * 	This procedure makes sure that nmscShowUse and nmscShowDef
 *	have been properlay initialized to refer to a cell definition
 *	named "__SHOW__".
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A new cell use and/or def are created if necessary.
 *
 * ----------------------------------------------------------------------------
 */

void
nmGetShowCell()
{
    if (nmscShowUse != NULL) return;
    nmscShowDef = DBCellLookDef("__SHOW__");
    if (nmscShowDef == NULL)
    {
	nmscShowDef = DBCellNewDef("__SHOW__", (char *) NULL);
	ASSERT (nmscShowDef != (CellDef *) NULL, "nmGetShowCell");
	DBCellSetAvail(nmscShowDef);
	nmscShowDef->cd_flags |= CDINTERNAL;
    }
    nmscShowUse = DBCellNewUse(nmscShowDef, (char *) NULL);
    DBSetTrans(nmscShowUse, &GeoIdentityTransform);
    nmscShowUse->cu_expandMask = CU_DESCEND_SPECIAL;
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMShowUnderBox --
 *
 * 	This procedure copies into the show cell all paint connected to
 *	anything underneath the box.  The show cell is then highlighted.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Any previous highlighted cell is unset, and any previous contents
 *	of the show cell are destroyed.
 *
 * ----------------------------------------------------------------------------
 */

void
NMShowUnderBox()
{
    CellDef *rootDef;
    MagWindow *w;
    SearchContext scx;

    NMUnsetCell();
    nmGetShowCell();

    w = ToolGetBoxWindow(&scx.scx_area, (int *) NULL); if (w == NULL)
    {
	TxError("There's no box!  Please use the box to select one\n");
	TxError("or more nets to be highlighted.\n");
	return;
    }

    scx.scx_use = (CellUse *) w->w_surfaceID;
    scx.scx_trans = GeoIdentityTransform;

    /* Expand the box area by one so we'll get everything that even
     * touches it.
     */
    
    GEO_EXPAND(&scx.scx_area, 1, &scx.scx_area);
    rootDef = scx.scx_use->cu_def;

    DBWAreaChanged(nmscShowDef, &nmscShowDef->cd_bbox, DBW_ALLWINDOWS,
	&DBAllButSpaceBits);
    DBCellClearDef(nmscShowUse->cu_def);
    DBTreeCopyConnect(&scx, &DBAllButSpaceAndDRCBits, 0,
	    DBConnectTbl, &TiPlaneRect, nmscShowUse);
    DBWAreaChanged(nmscShowDef, &nmscShowDef->cd_bbox, DBW_ALLWINDOWS,
	&DBAllButSpaceBits);
    NMShowCell(nmscShowUse, rootDef);
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMShowRoutedNet --
 *
 * 	This procedure copies into the show cell all paint connected to
 *	any terminal of the currently selected net.  The show cell is then
 *	highlighted.  If an argument is given, that net is selected and then
 *	highlighted, instead.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Any previous highlighted cell is unset, and any previous contents
 *	of the show cell are destroyed.
 *
 * ----------------------------------------------------------------------------
 */

int
NMShowRoutedNet(netName)
    char * netName;
{
    int nmShowRoutedNetFunc();

    if (netName == NULL)
    {
	if (NMCurNetName == NULL)
	{
	    TxError("You must select a net before you can trace it.\n");
	    return 0;
	}
	else netName = NMCurNetName;
    }
    NMUnsetCell();
    nmGetShowCell();
    DBWAreaChanged(nmscShowDef, &nmscShowDef->cd_bbox, DBW_ALLWINDOWS,
	 &DBAllButSpaceBits);
    DBCellClearDef(nmscShowUse->cu_def);
    NMSelectNet(netName);
    if(NMCurNetName==NULL)
    {
	TxError("The net list has no net containing the terminal \"%s\"\n",
	    netName);
	return 0;
    }
    (void) NMEnumTerms(NMCurNetName, nmShowRoutedNetFunc,
	    (ClientData) EditCellUse);
    DBWAreaChanged(nmscShowDef, &nmscShowDef->cd_bbox, DBW_ALLWINDOWS,
	&DBAllButSpaceBits);
    NMShowCell(nmscShowUse, EditCellUse->cu_def);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * nmShowRoutedNetFunc --
 *
 * 	This procedure simply calls nmSRNFunc with the name of a terminal.
 *
 * Results:
 *	Returns 0.
 *
 * Side effects:
 *	Paints into the highlight cell.
 *
 * ----------------------------------------------------------------------------
 */
int
nmShowRoutedNetFunc(name, clientData)
    char *name;
    ClientData clientData;
{
    int nmSRNFunc();

    (void) DBSrLabelLoc((CellUse *) clientData, name, nmSRNFunc, clientData);
    return(0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * nmSRNFunc --
 *
 *	This procedure copies into the show cell all paint connected to
 *	anything underneath the terminal.
 *
 * Results:
 *	Returns 0.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

    /*ARGSUSED*/
int
nmSRNFunc(rect, name, label, cdarg)
    Rect *rect;
    char *name;		/* Unused */
    Label *label;
    ClientData cdarg;
{
    SearchContext scx;
    
    /* Expand the box area by one so we'll get everything that even
     * touches it.  Search on layers connected to the layer of the label.
     */
    
    scx.scx_area = *rect;
    GEO_EXPAND(&scx.scx_area, 1, &scx.scx_area);
    scx.scx_use = (CellUse *) cdarg;
    scx.scx_trans = GeoIdentityTransform;

    DBTreeCopyConnect(&scx, &DBConnectTbl[label->lab_type], 0,
	    DBConnectTbl, &TiPlaneRect, nmscShowUse);
    return(0);
}
