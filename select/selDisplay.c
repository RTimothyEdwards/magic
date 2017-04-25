/* selDisplay.c -
 *
 *	This file provides routines for displaying the current
 *	selection on the screen.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/select/selDisplay.c,v 1.5 2010/06/24 12:37:56 tim Exp $";
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
#include "utils/signals.h"

/* The current selection is displayed by displaying the outline of
 * shapes in one cell as an overlay (using the higlight facilities)
 * on top of another cell.  The variables below are used to remember
 * these two cells.
 */

static CellUse *selDisUse = NULL;	/* Name of cell whose contents are
					 * highlighted.
					 */
static CellDef *selDisRoot = NULL;	/* Name of a root cell in a window,
					 * on top of whose contents selDisUse
					 * is highlighted.  (i.e. determines
					 * windows in which selection is
					 * displayed).  NULL means no current
					 * selection.
					 */

/* The following variable is shared between SelRedisplay and the search
 * functions that it invokes.  It points to the plane indicating which
 * highlight areas must be redrawn.
 */

static Plane *selRedisplayPlane;

/*
 * ----------------------------------------------------------------------------
 *
 * SelRedisplay --
 *
 * 	This procedure is called by the highlight code to redraw
 *	the selection highlights.  The caller must have locked
 *	the window already.  Only the highlight code should invoke
 *	this procedure.  Other clients should always call the highlight
 *	procedures.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Highlights are redrawn, if there is a selection to display
 *	and if it overlaps any non-space tiles in plane.
 *
 * ----------------------------------------------------------------------------
 */

void
SelRedisplay(window, plane)
    MagWindow *window;		/* Window in which to redisplay. */
    Plane *plane;		/* Non-space tiles on this plane indicate
				 * which areas must have their highlights
				 * redrawn.
				 */
{
    int i, labelSize;
    CellDef *displayDef;
    Label *label;
    Transform tinv;
    Rect planeArea, screenArea, selArea;
    SearchContext scx;
    DBWclientRec *crec = (DBWclientRec *) window->w_clientData;

    extern int selRedisplayFunc();	/* Forward declaration. */
    extern int selRedisplayCellFunc();	/* Forward declaration. */
    extern int selAlways1();		/* Forward declaration. */

    /* Make sure that we've got something to show in the area
     * being redisplayed.
     */
    
    if (((CellUse *) (window->w_surfaceID))->cu_def != selDisRoot) return;
    displayDef = selDisUse->cu_def;
    if (!DBBoundPlane(plane, &planeArea)) return;
    GeoInvertTrans(&selDisUse->cu_transform, &tinv);
    GeoTransRect(&tinv, &planeArea, &selArea);
    if (!GEO_OVERLAP(&displayDef->cd_bbox, &selArea))
    {
	/* Check if any labels in the selection overlap */
	for (label = displayDef->cd_labels; label != NULL; label = label->lab_next)
	    if (label->lab_font >= 0)
		if (GEO_OVERLAP(&label->lab_bbox, &selArea))
		    break;

	if (label == NULL) return;
    }

    /* Redisplay the information on the paint planes. */

    GrSetStuff(STYLE_OUTLINEHIGHLIGHTS);
    selRedisplayPlane = plane;
    for (i = PL_SELECTBASE; i < DBNumPlanes; i += 1)
    {
	(void) DBSrPaintArea((Tile *) NULL, displayDef->cd_planes[i],
		&selArea, &DBAllTypeBits, selRedisplayFunc,
		(ClientData) window);
    }

    /* Redisplay all of the labels in the selection. */

    labelSize = crec->dbw_labelSize;
    if (labelSize < GR_TEXT_SMALL) labelSize = GR_TEXT_SMALL;

    for (label = displayDef->cd_labels; label != NULL; label = label->lab_next)
    {
	Rect larger, tmpr;

	/* See if the label needs to be redisplayed (make sure we do the
	 * search with a non-null area, or it will never return "yes").
	 */

	if (label->lab_font < 0)
	{
	    larger = label->lab_rect;
	    if (larger.r_xbot == larger.r_xtop)
		larger.r_xtop += 1;
	    if (larger.r_ybot == larger.r_ytop)
		larger.r_ytop += 1;
	    if (!DBSrPaintArea((Tile *) NULL, plane, &larger, &DBAllButSpaceBits,
			selAlways1, (ClientData) NULL))
		continue;

	    GeoTransRect(&selDisUse->cu_transform, &label->lab_rect, &tmpr);
	    WindSurfaceToScreen(window, &tmpr, &screenArea);

	    DBWDrawLabel(label, &screenArea, label->lab_just,
			STYLE_OUTLINEHIGHLIGHTS, labelSize,
			&crec->dbw_expandAmounts);
	}
	else
	{
	    DBWDrawFontLabel(label, window, &selDisUse->cu_transform,
			STYLE_OUTLINEHIGHLIGHTS);

	    /* Disabled for now because there is no routine to compute bbox  */
	    // GeoTransRect(&selDisUse->cu_transform, &label->lab_bbox, &tmpr);
	    // WindSurfaceToScreen(window, &tmpr, &screenArea);
	    // GeoInclude(&screenArea, &crec->dbw_expandAmounts);
	}
	if (SigInterruptPending) break;
    }

    /* Redisplay all of the subcells in the selection.  Change the
     * clipping rectangle to full-screen or else the cell names won't
     * get displayed properly.  The search function will clip itself.
     * This is the same hack that's in the display module.
     */

    GrClipTo(&GrScreenRect);
    scx.scx_use = selDisUse;
    scx.scx_area = selArea;
    scx.scx_trans = selDisUse->cu_transform;
    (void) DBCellSrArea(&scx, selRedisplayCellFunc, (ClientData) window);
}

/* Function used to see if an area in the selection touches an area
 * that's to be redisplayed:  it just returns 1 always.
 */

int
selAlways1()
{
    return 1;
}

/* Redisplay function for selected paint:  draw lines to outline
 * material.  Only draw lines on boundaries between different
 * kinds of material.
 */

int
selRedisplayFunc(tile, window)
    Tile *tile;			/* Tile to be drawn on highlight layer. */
    MagWindow *window;		/* Window in which to redisplay. */
{
    Rect area, edge, screenEdge, tmpr;
    Tile *neighbor;
    TileType loctype, ntype;
    Transform *t = &selDisUse->cu_transform;

    TiToRect(tile, &area);
    GeoTransRect(t, &area, &tmpr);

    /* Watch for infinities.  Because the select use can't be rotated,	*/
    /* we can just replace infinity markers in tmpr with those in area.	*/

    if (area.r_xbot <= MINFINITY + 2) tmpr.r_xbot = area.r_xbot;
    if (area.r_xtop >= INFINITY - 2) tmpr.r_xtop = area.r_xtop;
    if (area.r_ybot <= MINFINITY + 2) tmpr.r_ybot = area.r_ybot;
    if (area.r_ytop >= INFINITY - 2) tmpr.r_ytop = area.r_ytop;

    if (!DBSrPaintArea((Tile *) NULL, selRedisplayPlane, &tmpr,
	    &DBAllButSpaceBits, selAlways1, (ClientData) NULL))
	return 0;

    /* Go along the tile's bottom border, searching for tiles
     * of a different type along that border.  If the bottom of
     * the tile is at -infinity, then don't do anything.
     */
    
    if (IsSplit(tile))  
    {
	/* By definition, split tiles have a different type on the other */
	/* side of the split.  So always draw a line on the diagonal.    */
	/* Use GrDrawTriangleEdge() so that clipping for area select     */
	/* draws the correct line.					 */

	WindSurfaceToScreenNoClip(window, &tmpr, &screenEdge);
	if (screenEdge.r_ll.p_x != screenEdge.r_ur.p_x &&
		screenEdge.r_ll.p_y != screenEdge.r_ur.p_y)
	    GrDrawTriangleEdge(&screenEdge, TiGetTypeExact(tile));
	loctype = (SplitSide(tile)) ? SplitRightType(tile) : SplitLeftType(tile);
    }
    else
	loctype = TiGetTypeExact(tile);

    if (IsSplit(tile) && (!(SplitSide(tile) ^ SplitDirection(tile))))
	goto searchleft;        /* nothing on bottom of split */

    if (area.r_ybot > TiPlaneRect.r_ybot)
    {
	edge.r_ybot = edge.r_ytop = area.r_ybot;
	for (neighbor = LB(tile); LEFT(neighbor) < area.r_xtop;
		neighbor = TR(neighbor))
	{
	    ntype = TiGetTopType(neighbor);
	    if (ntype == loctype) continue;
	    edge.r_xbot = LEFT(neighbor);
	    edge.r_xtop = RIGHT(neighbor);
	    if (edge.r_xbot < area.r_xbot) edge.r_xbot = area.r_xbot;
	    if (edge.r_xtop > area.r_xtop) edge.r_xtop = area.r_xtop;
	    GeoTransRect(t, &edge, &tmpr);
	    WindSurfaceToScreen(window, &tmpr, &screenEdge);
	    GrClipLine(screenEdge.r_xbot, screenEdge.r_ybot,
		    screenEdge.r_xtop, screenEdge.r_ytop);
	}
    }

    /* Now go along the tile's left border, doing the same thing.   Ignore
     * edges that are at infinity.
     */

searchleft:
    if (IsSplit(tile) && SplitSide(tile)) return 0;

    if (area.r_xbot > TiPlaneRect.r_xbot)
    {
	edge.r_xbot = edge.r_xtop = area.r_xbot;
	for (neighbor = BL(tile); BOTTOM(neighbor) < area.r_ytop;
		neighbor = RT(neighbor))
	{
	    ntype = TiGetRightType(neighbor);
	    if (ntype == loctype) continue;
	    edge.r_ybot = BOTTOM(neighbor);
	    edge.r_ytop = TOP(neighbor);
	    if (edge.r_ybot < area.r_ybot) edge.r_ybot = area.r_ybot;
	    if (edge.r_ytop > area.r_ytop) edge.r_ytop = area.r_ytop;
	    GeoTransRect(t, &edge, &tmpr);
	    WindSurfaceToScreen(window, &tmpr, &screenEdge);
	    GrClipLine(screenEdge.r_xbot, screenEdge.r_ybot,
		    screenEdge.r_xtop, screenEdge.r_ytop);
	}
    }
    return 0;			/* To keep the search from aborting. */
}

/* Redisplay function for cells:  do what the normal redisplay code does
 * in DBWdisplay.c, except draw in the highlight color.
 */

int
selRedisplayCellFunc(scx, window)
    SearchContext *scx;		/* Describes cell found. */
    MagWindow *window;		/* Window in which to redisplay. */
{
    Rect tmp, screen;
    Point p;
    char idName[100];

    GeoTransRect(&scx->scx_trans, &scx->scx_use->cu_def->cd_bbox, &tmp);
    if (!DBSrPaintArea((Tile *) NULL, selRedisplayPlane, &tmp,
	    &DBAllButSpaceBits, selAlways1, (ClientData) NULL))
	return 0;
    WindSurfaceToScreen(window, &tmp, &screen);
    GrFastBox(&screen);

    /* Don't futz around with text if the bbox is tiny. */

    GrLabelSize("BBB", GEO_CENTER, GR_TEXT_SMALL, &tmp);
    if (((screen.r_xtop-screen.r_xbot) < tmp.r_xtop)
	|| ((screen.r_ytop-screen.r_ybot) < tmp.r_ytop)) return 0;

    p.p_x = (screen.r_xbot + screen.r_xtop)/2;
    p.p_y = (screen.r_ybot + 2*screen.r_ytop)/3;
    GeoClip(&screen, &window->w_screenArea);
    GrPutText(scx->scx_use->cu_def->cd_name, STYLE_SOLIDHIGHLIGHTS, &p,
	GEO_CENTER, GR_TEXT_LARGE, TRUE, &screen, (Rect *) NULL);
    (void) DBPrintUseId(scx, idName, 100, TRUE);
    p.p_y = (2*screen.r_ybot + screen.r_ytop)/3;
    GrPutText(idName, STYLE_SOLIDHIGHLIGHTS, &p, GEO_CENTER,
        GR_TEXT_LARGE, TRUE, &screen, (Rect *) NULL);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * SelSetDisplay --
 *
 * 	This procedure is called to set up displaying of the selection.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	This procedure sets things up so that future calls to the
 *	highlight code will cause information in selectUse to be
 *	outlined on top of windows containing displayRoot.  This
 *	procedure should be called whenever either of the two
 *	cells changes.  Note:  this procedure does NOT actually
 *	redisplay anything.  The highlight procedures should be
 *	invoked to do the redisplay.
 *
 * ----------------------------------------------------------------------------
 */

void
SelSetDisplay(selectUse, displayRoot)
    CellUse *selectUse;		/* Cell whose contents are to be
				 * highlighted.
				 */
    CellDef *displayRoot;	/* Cell definition on top of whose contents
				 * the highlights are to be displayed.  Must
				 * be the root cell of a window.  May be NULL
				 * to turn off selection displaying.
				 */
{
    static bool firstTime = TRUE;

    if (firstTime)
    {
	DBWHLAddClient(SelRedisplay);
	firstTime = FALSE;
    }
    selDisUse = selectUse;
    selDisRoot = displayRoot;
}

/*----------------------------------------------------------------------*/
/* Functions for converting selections to highlight areas		*/
/*----------------------------------------------------------------------*/

typedef struct {
    char *text;
    int  style;
} FeedLayerData;

void
SelCopyToFeedback(celldef, seluse, style, text)
    CellDef *celldef;		/* Cell def to hold feedback */
    CellUse *seluse;		/* Cell use holding selection */
    int style;			/* Style to use for feedback */
    char *text;			/* Text to attach to feedback */
{
    int selFeedbackFunc();	/* Forward reference */
    int i;
    CellDef *saveDef;
    FeedLayerData fld;

    if (celldef == NULL) return;

    saveDef = selDisRoot;
    selDisRoot = celldef;

    fld.text = text;
    fld.style = style;

    UndoDisable();
    for (i = PL_SELECTBASE; i < DBNumPlanes; i += 1)
    {
	(void) DBSrPaintArea((Tile *) NULL, seluse->cu_def->cd_planes[i],
		&TiPlaneRect, &DBAllButSpaceBits, selFeedbackFunc,
		(ClientData)&fld);
    }
    UndoEnable();

    selDisRoot = saveDef;
}

/*----------------------------------------------------------------------*/
/* Callback function per tile of the selection
/*----------------------------------------------------------------------*/

int
selFeedbackFunc(tile, fld)
    Tile *tile;
    FeedLayerData *fld;
{
    Rect area;

    TiToRect(tile, &area);

    DBWFeedbackAdd(&area, fld->text, selDisRoot, 1, fld->style |
                (TiGetTypeExact(tile) & (TT_DIAGONAL | TT_DIRECTION | TT_SIDE)));
        /* (preserve information about the geometry of a diagonal tile) */
    return 0;
}

/*----------------------------------------------------------------------*/
