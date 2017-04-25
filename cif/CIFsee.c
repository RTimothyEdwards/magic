/* CIFsee.c -
 *
 *	This file provides procedures for displaying CIF layers on
 *	the screen using the highlight facilities.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/cif/CIFsee.c,v 1.5 2010/06/24 12:37:15 tim Exp $";
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
#include "cif/CIFint.h"
#include "textio/textio.h"
#include "utils/undo.h"

/* The following variable holds the CellDef into which feedback
 * is to be placed for displaying CIF.
 */

static CellDef *cifSeeDef;

/* Verbosity of warning messages */
global int CIFWarningLevel = CIF_WARN_DEFAULT;

typedef struct {
   CellDef *paintDef;
   int	   layer;
} PaintLayerData;

typedef struct {
   char *text;
   int  layer;
   int  style;
} SeeLayerData;

/*
 * ----------------------------------------------------------------------------
 *
 * cifPaintDBFunc --
 *
 *	This routine paints CIF back into the database at the inverse
 *	scale at which it was generated.  Otherwise, it is very much like
 *	cifPaintCurrentFunc() in CIFrdcl.c.
 *
 * Results:
 *	Always returns 0 to keep the search alive.
 *
 * Side effects:
 *	Paint is generated in the cell.
 * ----------------------------------------------------------------------------
 */

int
cifPaintDBFunc(tile, pld)
    Tile *tile;			/* Tile of CIF information. */
    PaintLayerData *pld;
{
    Rect area;
    int pNum;
    TileType type = pld->layer;		/* Magic type to be painted.	*/
    CellDef *paintDef = pld->paintDef;	/* Cell to draw into.		*/
    int cifScale = CIFCurStyle->cs_scaleFactor;
    PaintUndoInfo ui;

    /* Compute the area of the CIF tile, then scale it into
     * Magic coordinates.
     */

    TiToRect(tile, &area);
    area.r_xtop /= cifScale;
    area.r_xbot /= cifScale;
    area.r_ytop /= cifScale;
    area.r_ybot /= cifScale;

    /* Check for degenerate areas (from rescale limiting) before painting */
    if ((area.r_xbot == area.r_xtop) || (area.r_ybot == area.r_ytop))
	return 0;

    ui.pu_def = paintDef;
    for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
	if (DBPaintOnPlane(type, pNum))
	{
	    ui.pu_pNum = pNum;
	    DBNMPaintPlane(paintDef->cd_planes[pNum], TiGetTypeExact(tile),
		    &area, DBStdPaintTbl(type, pNum), (PaintUndoInfo *) &ui);
	}

    return  0;		/* To keep the search alive. */
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFPaintLayer --
 *
 * 	Generates CIF over a given area of a given cell, then
 *	paints the CIF layer back into the cell (or optionally,
 *	into a separate cell) as a magic layer.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Highlight information is drawn on the screen.
 *
 * ----------------------------------------------------------------------------
 */

void
CIFPaintLayer(rootDef, area, cifLayer, magicLayer, paintDef)
    CellDef *rootDef;		/* Cell for which to generate CIF.  Must be
				 * the rootDef of a window.
				 */
    Rect *area;			/* Area in which to generate CIF. */
    char *cifLayer;		/* CIF layer to highlight on the screen. */
    int	  magicLayer;		/* Magic layer to paint with the result */
    CellDef *paintDef;		/* CellDef to paint into (may be NULL)	*/
{
    int oldCount, i;
    char msg[100];
    SearchContext scx;
    PaintLayerData pld;
    TileTypeBitMask mask, depend;

    /* Make sure the desired layer exists. */

    if (!CIFNameToMask(cifLayer, &mask, &depend)) return;

    /* Paint directly into the root cellDef if passed a NULL paintDef*/
    pld.paintDef = (paintDef == NULL) ? rootDef : paintDef;
    pld.layer = magicLayer;

    /* Flatten the area and generate CIF for it. */

    CIFErrorDef = rootDef;
    CIFInitCells();
    UndoDisable();
    CIFDummyUse->cu_def = rootDef;
    GEO_EXPAND(area, CIFCurStyle->cs_radius, &scx.scx_area);
    scx.scx_use = CIFDummyUse;
    scx.scx_trans = GeoIdentityTransform;
    (void) DBTreeSrTiles(&scx, &DBAllButSpaceAndDRCBits, 0,
	cifHierCopyFunc, (ClientData) CIFComponentDef);
    oldCount = DBWFeedbackCount;
    CIFGen(CIFComponentDef, area, CIFPlanes, &depend, TRUE, TRUE);
    DBCellClearDef(CIFComponentDef);

    /* Report any errors that occurred. */

    if (DBWFeedbackCount != oldCount)
    {
	TxPrintf("%d problems occurred.  See feedback entries.\n",
	    DBWFeedbackCount-oldCount);
    }

    /* Paint back the chosen layer. */

    UndoEnable();
    for (i = 0; i < CIFCurStyle->cs_nLayers; i++)
	if (TTMaskHasType(&mask, i))
	    DBSrPaintArea((Tile *)NULL, CIFPlanes[i], &TiPlaneRect,
			&CIFSolidBits, cifPaintDBFunc, (ClientData)&pld);

    DBWAreaChanged(rootDef, area, DBW_ALLWINDOWS, &mask);
    DBReComputeBbox(rootDef);
    DRCCheckThis(rootDef, TT_CHECKPAINT, area);
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifSeeFunc --
 *
 * 	Called once for each tile that is to be displayed as feedback.
 *	This procedure enters the tile as feedback.  Note: the caller
 *	must arrange for cifSeeDef to contain a pointer to the cell
 *	def where feedback is to be displayed.
 *
 * Results:
 *	Always returns 0 to keep the search alive.
 *
 * Side effects:
 *	A new feedback area is created over the tile.  The parameter
 *	"text" is associated with the feedback.
 * ----------------------------------------------------------------------------
 */

int
cifSeeFunc(tile, sld)
    Tile *tile;			/* Tile to be entered as feedback. */
    SeeLayerData *sld;		/* Layer and explanation for the feedback. */
{
    Rect area;

    TiToRect(tile, &area);

    if (((area.r_xbot & 0x1) || (area.r_ybot & 0x1))
                && (CIFWarningLevel == CIF_WARN_ALIGN))
    {
	TxError("Warning: Corner (%.1f, %.1f) has half-lambda placement.\n",
		(float)area.r_xbot / (float)CIFCurStyle->cs_scaleFactor,
		(float)area.r_ybot / (float)CIFCurStyle->cs_scaleFactor);
    }

    DBWFeedbackAdd(&area, sld->text, cifSeeDef, CIFCurStyle->cs_scaleFactor,
	sld->style |
                (TiGetTypeExact(tile) & (TT_DIAGONAL | TT_DIRECTION | TT_SIDE)));
        /* (preserve information about the geometry of a diagonal tile) */
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFSeeLayer --
 *
 * 	Generates CIF over a given area of a given cell, then
 *	highlights a particular CIF layer on the screen.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Highlight information is drawn on the screen.
 *
 * ----------------------------------------------------------------------------
 */

void
CIFSeeLayer(rootDef, area, layer)
    CellDef *rootDef;		/* Cell for which to generate CIF.  Must be
				 * the rootDef of a window.
				 */
    Rect *area;			/* Area in which to generate CIF. */
    char *layer;		/* CIF layer to highlight on the screen. */
{
    int oldCount, i;
    char msg[100];
    SearchContext scx;
    SeeLayerData sld;
    TileTypeBitMask mask, depend;

    /* Make sure the desired layer exists. */

    if (!CIFNameToMask(layer, &mask, &depend)) return;

    /* Flatten the area and generate CIF for it. */

    CIFErrorDef = rootDef;
    CIFInitCells();
    UndoDisable();
    CIFDummyUse->cu_def = rootDef;
    GEO_EXPAND(area, CIFCurStyle->cs_radius, &scx.scx_area);
    scx.scx_use = CIFDummyUse;
    scx.scx_trans = GeoIdentityTransform;
    (void) DBTreeSrTiles(&scx, &DBAllButSpaceAndDRCBits, 0,
	cifHierCopyFunc, (ClientData) CIFComponentDef);
    oldCount = DBWFeedbackCount;
    CIFGen(CIFComponentDef, area, CIFPlanes, &depend, TRUE, TRUE);
    DBCellClearDef(CIFComponentDef);

    /* Report any errors that occurred. */

    if (DBWFeedbackCount != oldCount)
    {
	TxPrintf("%d problems occurred.  See feedback entries.\n",
	    DBWFeedbackCount-oldCount);
    }

    /* Display the chosen layer. */

    (void) sprintf(msg, "CIF layer \"%s\"", layer);
    cifSeeDef = rootDef;
    sld.text = msg;

    for (i = 0; i < CIFCurStyle->cs_nLayers; i++)
    {
	if (TTMaskHasType(&mask, i))
	{
	    sld.layer = i;
#ifdef THREE_D
	    sld.style = CIFCurStyle->cs_layers[i]->cl_renderStyle
			+ TECHBEGINSTYLES;
#else
	    sld.style = STYLE_PALEHIGHLIGHTS;
#endif
	    DBSrPaintArea((Tile *)NULL, CIFPlanes[i], &TiPlaneRect,
			&CIFSolidBits, cifSeeFunc, (ClientData)&sld);
	}
    }
    UndoEnable();
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFSeeHierLayer --
 *
 * 	This procedure is similar to CIFSeeLayer except that it only
 *	generates hierarchical interaction information.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	CIF information is highlighed on the screen.  If arrays is
 *	TRUE, then CIF that stems from array interactions is displayed.
 *	if subcells is TRUE, then CIF stemming from subcell interactions
 *	is displayed.  If both are TRUE, then both are displayed.
 *
 * ----------------------------------------------------------------------------
 */

void
CIFSeeHierLayer(rootDef, area, layer, arrays, subcells)
    CellDef *rootDef;		/* Def in which to compute CIF.  Must be
				 * the root definition of a window.
				 */
    Rect *area;			/* Area in which to generate CIF. */
    char *layer;		/* CIF layer to be highlighted. */
    bool arrays;		/* TRUE means show array interactions. */
    bool subcells;		/* TRUE means show subcell interactions. */
{
    int i, oldCount;
    SeeLayerData sld;
    char msg[100];
    TileTypeBitMask mask;

    /* Check out the CIF layer name. */

    if (!CIFNameToMask(layer, &mask, NULL)) return;

    CIFErrorDef = rootDef;
    oldCount = DBWFeedbackCount;
    CIFClearPlanes(CIFPlanes);
    if (subcells)
	CIFGenSubcells(rootDef, area, CIFPlanes);
    if (arrays)
	CIFGenArrays(rootDef, area, CIFPlanes);
    
    /* Report any errors that occurred. */

    if (DBWFeedbackCount != oldCount)
    {
	TxPrintf("%d problems occurred.  See feedback entries.\n",
	    DBWFeedbackCount - oldCount);
    }
    
    (void) sprintf(msg, "CIF layer \"%s\"", layer);
    cifSeeDef = rootDef;
    sld.text = msg;

    for (i = 0; i < CIFCurStyle->cs_nLayers; i++)
    {
	if (TTMaskHasType(&mask, i))
	{
	    sld.layer = i;
#ifdef THREE_D
	    sld.style = CIFCurStyle->cs_layers[i]->cl_renderStyle
			+ TECHBEGINSTYLES;
#else
	    sld.style = STYLE_PALEHIGHLIGHTS;
#endif
	    DBSrPaintArea((Tile *)NULL, CIFPlanes[i], &TiPlaneRect,
			&CIFSolidBits, cifSeeFunc, (ClientData)&sld);
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFCoverageLayer --
 *
 *	This procedure reports the total area coverage of a CIF layer
 *	within the bounding box of the root CellDef.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Prints results.
 *
 * ----------------------------------------------------------------------------
 */

typedef struct {
   long long coverage;
   Rect bounds;
} coverstats;

void
CIFCoverageLayer(rootDef, area, layer)
    CellDef *rootDef;		/* Def in which to compute CIF coverage */
    Rect *area;			/* Area in which to compute coverage */
    char *layer;		/* CIF layer for coverage computation. */
{
    coverstats cstats;
    int i, scale;
    long long atotal, btotal;
    SearchContext scx;
    TileTypeBitMask mask, depend;
    float fcover;
    int cifCoverageFunc();
    bool doBox = (area != &rootDef->cd_bbox) ? TRUE : FALSE;

    /* Check out the CIF layer name. */

    if (!CIFNameToMask(layer, &mask, &depend)) return;

    CIFErrorDef = rootDef;
    CIFInitCells();
    UndoDisable();
    CIFDummyUse->cu_def = rootDef;

    GEO_EXPAND(area, CIFCurStyle->cs_radius, &scx.scx_area);

    scx.scx_use = CIFDummyUse;
    scx.scx_trans = GeoIdentityTransform;
    (void) DBTreeSrTiles(&scx, &DBAllButSpaceAndDRCBits, 0,
	cifHierCopyFunc, (ClientData) CIFComponentDef);
    CIFGen(CIFComponentDef, area, CIFPlanes, &depend, TRUE, TRUE);
    DBCellClearDef(CIFComponentDef);
    
    cstats.coverage = 0;
    cstats.bounds.r_xbot = cstats.bounds.r_xtop = 0;
    cstats.bounds.r_ybot = cstats.bounds.r_ytop = 0;

    for (i = 0; i < CIFCurStyle->cs_nLayers; i++)
	if (TTMaskHasType(&mask, i))
	    DBSrPaintArea((Tile *)NULL, CIFPlanes[i], &TiPlaneRect,
			&CIFSolidBits, cifCoverageFunc,
			(ClientData) &cstats);

    /* Print results */

    scale = CIFCurStyle->cs_scaleFactor;

    btotal = (long long)(area->r_xtop - area->r_xbot);
    btotal *= (long long)(area->r_ytop - area->r_ybot);
    btotal *= (long long)(scale * scale);
    fcover = 0.0;
    if (btotal > 0.0) fcover = (float)cstats.coverage / (float)btotal;

    atotal = (long long)(cstats.bounds.r_xtop - cstats.bounds.r_xbot);
    atotal *= (long long)(cstats.bounds.r_ytop - cstats.bounds.r_ybot);

    TxPrintf("%s Area = %lld CIF units^2\n", doBox ?  "Cursor Box" :
		"Cell", btotal);
    TxPrintf("Layer Bounding Area = %lld CIF units^2\n", atotal);
    TxPrintf("Layer Total Area = %lld CIF units^2\n", cstats.coverage);
    TxPrintf("Coverage in %s = %1.1f%%\n", doBox ? "box" :
		"cell", 100.0 * fcover);
}

int
cifCoverageFunc(tile, arg)
    Tile *tile;
    ClientData *arg;
{
    coverstats *cstats = (coverstats *)arg;
    Rect r;

    TiToRect(tile, &r);
    cstats->coverage += (long long)((r.r_xtop - r.r_xbot) * (r.r_ytop - r.r_ybot));
    GeoInclude(&r, &cstats->bounds);

    return(0);
}
