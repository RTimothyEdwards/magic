/*
 * plotGremlin.c --
 *
 * This file contains procedures that generate Gremlin-format files
 * to describe a section of layout.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/plot/plotGremln.c,v 1.2 2008/06/01 18:37:45 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/tech.h"
#include "utils/malloc.h"
#include "utils/utils.h"
#include "windows/windows.h"
#include "commands/commands.h"
#include "dbwind/dbwind.h"
#include "textio/textio.h"

/* Records of the following type are used to describe how to generate
 * Gremlin output for a particular set of mask layers.  Each style
 * describes the Gremlin figures to draw for a particular set of
 * layers.  A single layer may participate in several gremlin styles.
 */

#ifdef GREMLIN

typedef struct gremlinstyle
{
    TileTypeBitMask grs_layers;		/* Layers to plot in this style. */
    int grs_stipple;			/* Type of fill to use.  See below. */
    struct gremlinstyle *grs_next;	/* Next style in chain. */
} GremlinStyle;

static GremlinStyle *plotGremlinStyles;

/* Most of the grs_stipple values are Gremlin stipple numbers.  However,
 * if a grs_stipple value is less than zero, it means something special.
 * The definitions below give the possible alternatives:
 *
 * CROSS:		Draw a thick outline around the tile with
 *			a cross through it (used for contacts).
 * BORDER:		Same as CROSS, except draw the outline with
 *			no cross through it.
 */

#define CROSS -1
#define BORDER -2

/* The definitions below give the integers used for various Gremlin
 * line drawing styles (brushes).
 */

#define GREMLIN_DOTTED	1
#define GREMLIN_DASHED	4
#define GREMLIN_DOTDASH	2
#define GREMLIN_THIN	5
#define GREMLIN_MEDIUM	6
#define GREMLIN_THICK	3

/* The variables below are used to pass information from the top-level
 * procedure PlotGremlin down to the lower-level search functions
 * that are invoked for pieces of the layout.
 */

static FILE *file;		/* File to use for output. */
static float scale;		/* Multiply this by Magic units to get
				 * Gremlin units.
				 */
static GremlinStyle *curStyle;	/* Current style being output. */
static TileTypeBitMask curMask;	/* Layers currently being searched:  this
				 * is the AND of the mask from curStyle and
				 * the layers that the user specified.
				 */
static Rect bbox;		/* Bounding box, in root coordinates, of
				 * area being plotted.
				 */
#endif /* GREMLIN */


/*
 * ----------------------------------------------------------------------------
 *	PlotGremlinTechInit --
 *
 * 	Called once at beginning of technology file read-in to initialize
 *	data structures.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Clears out the list of things to plot.
 * ----------------------------------------------------------------------------
 */

#ifdef GREMLIN

void
PlotGremlinTechInit()
{
    GremlinStyle *style;

    for (style = plotGremlinStyles; style != NULL; style = style->grs_next)
    {
	freeMagic((char *) style);
    }
    plotGremlinStyles = NULL;
}

#else

void
PlotGremlinTechInit()
{}

#endif /* GREMLIN */

/*
 * ----------------------------------------------------------------------------
 *	PlotGremlinTechLine --
 *
 * 	This procedure is invoked by the technology module once for
 *	each line in the "gremlin" subsection of the "plot" section
 *	of the technology file.
 *
 * Results:
 *	Always returns TRUE (otherwise the technology module would
 *	abort Magic with a fatal error).
 *
 * Side effects:
 *	Builds up the table of Gremlin styles.
 * ----------------------------------------------------------------------------
 */

#ifdef GREMLIN

bool
PlotGremlinTechLine(sectionName, argc, argv)
    char *sectionName;		/* Name of this section (unused). */
    int argc;			/* Number of arguments on line. */
    char *argv[];		/* Pointers to fields of line. */
{
    GremlinStyle *new;
    int stipple, i;

    if (argc != 2)
    {
	TechError("\"gremlin\" lines must have exactly 2 arguments.\n");
	return TRUE;
    }

    if (strcmp(argv[1], "X") == 0)
	stipple = CROSS;
    else if (strcmp(argv[1], "B") == 0)
	stipple = BORDER;
    else
    {
	if (!StrIsInt(argv[1]))
	{
	    TechError("2nd field must be an integer or \"X\" or \"B\".\n");
	    return TRUE;
	}
	stipple = atoi(argv[1]);
    }

    new = (GremlinStyle *) mallocMagic(sizeof(GremlinStyle));

    DBTechNoisyNameMask(argv[0], &new->grs_layers);

    /* Replace non-primary contact images with primary images. */

    for (i = TT_TECHDEPBASE; i < DBNumTypes; i++)
    {
	if TTMaskHasType(&new->grs_layers, i)
	    TTMaskSetMask(&new->grs_layers, &DBLayerTypeMaskTbl[i]);
    }
    TTMaskAndMask(&new->grs_layers, &DBUserLayerBits);
    new->grs_stipple = stipple;
    new->grs_next = plotGremlinStyles;
    plotGremlinStyles = new;

    return TRUE;
}

#else

bool
PlotGremlinTechLine(sectionName, argc, argv)
    char *sectionName;		/* Name of this section (unused). */
    int argc;			/* Number of arguments on line (unused). */
    char *argv[];		/* Pointers to fields of line (unused). */
{
    return TRUE;
}

#endif /* GREMLIN */

#ifdef GREMLIN

/*
 * ----------------------------------------------------------------------------
 *
 * plotGremlinLine --
 *
 * 	Outputs a line into the current Gremlin file.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	I/O.
 *
 * ----------------------------------------------------------------------------
 */

void
plotGremlinLine(p1, p2, lineStyle)
    Point *p1, *p2;		/* Endpoints of line, given in root
				 * coordinates.
				 */
    int lineStyle;		/* Gremlin line style to use for line. */
{
    float x1, x2, y1, y2, limit;

    /* Clip the line to the rectangular area being output.  First,
     * arrange for the first x-coordinate to be the smaller, then
     * clip against vertical lines at the x-boundaries.
     */

    if (p1->p_x <= p2->p_x)
    {
	x1 = p1->p_x - bbox.r_xbot;
	x2 = p2->p_x - bbox.r_xbot;
	y1 = p1->p_y - bbox.r_ybot;
	y2 = p2->p_y - bbox.r_ybot;
    }
    else
    {
	x1 = p2->p_x - bbox.r_xbot;
	x2 = p1->p_x - bbox.r_xbot;
	y1 = p2->p_y - bbox.r_ybot;
	y2 = p1->p_y - bbox.r_ybot;
    }
    limit = bbox.r_xtop - bbox.r_xbot;
    if ((x1 > limit) || (x2 < 0)) return;
    if (x1 < 0)
    {
	y1 += (-x1)*(y2-y1)/(x2-x1);
	x1 = 0;
    }
    if (x2 > limit)
    {
	y2 -= (x2-limit)*(y2-y1)/(x2-x1);
	x2 = limit;
    }

    /* Now clip against horizontal lines at the y-boundaries. */

    if (y2 < y1)
    {
	float tmp;
	tmp = y2; y2 = y1; y1 = tmp;
	tmp = x2; x2 = x1; x1 = tmp;
    }
    limit = bbox.r_ytop - bbox.r_ybot;
    if ((y1 > limit) || (y2 < 0)) return;
    if (y1 < 0)
    {
	x1 += (-y1)*(x2-x1)/(y2-y1);
	y1 = 0;
    }
    if (y2 > limit)
    {
	x2 -= (y2-limit)*(x2-x1)/(y2-y1);
	y2 = limit;
    }

    /* Lastly, scale and generate Gremlin output. */

    x1 *= scale;
    x2 *= scale;
    y1 *= scale;
    y2 *= scale;

    fprintf(file, "VECTOR\n");
    fprintf(file, "%.3f %.3f\n%.3f %.3f\n", x1, y1, x2, y2);
    fprintf(file, "*\n%d 0\n0 \n", lineStyle);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plotGremlinRect --
 *
 * 	Outputs Gremlin statements to draw a rectangular area as
 *	an outline with a given line style.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds information to the current Gremlin file.
 *
 * ----------------------------------------------------------------------------
 */

void
plotGremlinRect(rect, lineStyle)
    Rect *rect;	/* Rectangle to be drawn, in root coords. */
    int lineStyle;		/* Gremlin line style to use for outline. */
{
    Point p;

    p.p_x = rect->r_xbot;
    p.p_y = rect->r_ytop;
    plotGremlinLine(&rect->r_ll, &p, lineStyle);
    plotGremlinLine(&p, &rect->r_ur, lineStyle);
    p.p_x = rect->r_xtop;
    p.p_y = rect->r_ybot;
    plotGremlinLine(&rect->r_ur, &p, lineStyle);
    plotGremlinLine(&p, &rect->r_ll, lineStyle);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plotGremlinPaint --
 *
 * 	This procedure is invoked once for each paint rectangle in
 *	the area being plotted.
 *
 * Results:
 *	Always returns 0 to keep the search alive.
 *
 * Side effects:
 *	Outputs information for the tile, including stipple for its
 *	interior, and a solid line for any portion of the boundary
 *	of the tile that is adjacent to a tile NOT in this style.
 *
 * ----------------------------------------------------------------------------
 */

int
plotGremlinPaint(tile, cxp)
    Tile *tile;			/* Tile that's of type to be output. */
    TreeContext *cxp;		/* Describes search in progress. */
{
    Rect tileArea, edge, rootArea;
    float xbot, xtop, ybot, ytop;
    Tile *neighbor;

    /* First transform tile coords to root coords */
    
    TiToRect(tile, &tileArea);
    GeoTransRect(&cxp->tc_scx->scx_trans, &tileArea, &rootArea);

    /* See if this tile gets special handling. */

    if ((curStyle->grs_stipple == CROSS) || (curStyle->grs_stipple == BORDER))
    {
	/* Draw tile as a thick outline with a cross from corner
	 * to corner, and skip the rest of this procedure.
	 */

	Point ul, lr;

	plotGremlinRect(&rootArea, GREMLIN_MEDIUM);
	if (curStyle->grs_stipple == CROSS)
	{
	    ul.p_x = rootArea.r_xbot;
	    ul.p_y = rootArea.r_ytop;
	    lr.p_x = rootArea.r_xtop;
	    lr.p_y = rootArea.r_ybot;
	    plotGremlinLine(&rootArea.r_ll, &rootArea.r_ur, GREMLIN_MEDIUM);
	    plotGremlinLine(&ul, &lr, GREMLIN_MEDIUM);
	}
	return 0;
    }

    /* This tile gets "normal" processing (i.e. stippling and outlining).
     * Clip it to the plotting area and translate to Gremlin coords.
     * Then output Gremlin information for stippled interior.
     */

    GeoClip(&rootArea, &bbox);
    xbot = (rootArea.r_xbot - bbox.r_xbot) * scale;
    xtop = (rootArea.r_xtop - bbox.r_xbot) * scale;
    ybot = (rootArea.r_ybot - bbox.r_ybot) * scale;
    ytop = (rootArea.r_ytop - bbox.r_ybot) * scale;
    fprintf(file, "POLYGON\n");
    fprintf(file, "%.3f %.3f\n%.3f %.3f\n%.3f %.3f\n%.3f %.3f\n%.3f %.3f\n",
	    xbot, ybot, xtop, ybot, xtop, ytop, xbot, ytop, xbot, ybot);
    fprintf(file, "*\n0 %d\n0 \n", curStyle->grs_stipple);

    /* Now output lines for any edges between material of the type
     * currently being drawn and material of other types.  This is
     * done by searching along the tile's borders for neighbors that
     * have the wrong types.  First, search the tile's bottom border
     * (unless it is at infinity).
     */
    
    if (tileArea.r_ybot > TiPlaneRect.r_ybot)
    {
	edge.r_ybot = edge.r_ytop = tileArea.r_ybot;
	for (neighbor = LB(tile); LEFT(neighbor) < tileArea.r_xtop;
		neighbor = TR(neighbor))
	{
	    if (TTMaskHasType(&curMask, TiGetType(neighbor))) continue;
	    edge.r_xbot = LEFT(neighbor);
	    edge.r_xtop = RIGHT(neighbor);
	    if (edge.r_xbot < tileArea.r_xbot) edge.r_xbot = tileArea.r_xbot;
	    if (edge.r_xtop > tileArea.r_xtop) edge.r_xtop = tileArea.r_xtop;
	    GeoTransRect(&cxp->tc_scx->scx_trans, &edge, &rootArea);
	    plotGremlinLine(&rootArea.r_ll, &rootArea.r_ur, GREMLIN_THIN);
	}
    }

    /* Now go along the tile's left border, doing the same thing.   Ignore
     * edges that are at infinity.
     */

    if (tileArea.r_xbot > TiPlaneRect.r_xbot)
    {
	edge.r_xbot = edge.r_xtop = tileArea.r_xbot;
	for (neighbor = BL(tile); BOTTOM(neighbor) < tileArea.r_ytop;
		neighbor = RT(neighbor))
	{
	    if (TTMaskHasType(&curMask, TiGetType(neighbor))) continue;
	    edge.r_ybot = BOTTOM(neighbor);
	    edge.r_ytop = TOP(neighbor);
	    if (edge.r_ybot < tileArea.r_ybot) edge.r_ybot = tileArea.r_ybot;
	    if (edge.r_ytop < tileArea.r_ytop) edge.r_ytop = tileArea.r_ytop;
	    GeoTransRect(&cxp->tc_scx->scx_trans, &edge, &rootArea);
	    plotGremlinLine(&rootArea.r_ll, &rootArea.r_ur, GREMLIN_THIN);
	}
    }

    /* Same thing for the tile's top border. */

    if (tileArea.r_ytop < TiPlaneRect.r_ytop)
    {
	edge.r_ybot = edge.r_ytop = tileArea.r_ytop;
	for (neighbor = RT(tile); RIGHT(neighbor) > tileArea.r_xbot;
		neighbor = BL(neighbor))
	{
	    if (TTMaskHasType(&curMask, TiGetType(neighbor))) continue;
	    edge.r_xbot = LEFT(neighbor);
	    edge.r_xtop = RIGHT(neighbor);
	    if (edge.r_xbot < tileArea.r_xbot) edge.r_xbot = tileArea.r_xbot;
	    if (edge.r_xtop > tileArea.r_xtop) edge.r_xtop = tileArea.r_xtop;
	    GeoTransRect(&cxp->tc_scx->scx_trans, &edge, &rootArea);
	    plotGremlinLine(&rootArea.r_ll, &rootArea.r_ur, GREMLIN_THIN);
	}
    }

    /* Finally, the right border. */

    if (tileArea.r_xtop < TiPlaneRect.r_xtop)
    {
	edge.r_xbot = edge.r_xtop = tileArea.r_xtop;
	for (neighbor = TR(tile); TOP(neighbor) > tileArea.r_ybot;
		neighbor = LB(neighbor))
	{
	    if (TTMaskHasType(&curMask, TiGetType(neighbor))) continue;
	    edge.r_ybot = BOTTOM(neighbor);
	    edge.r_ytop = TOP(neighbor);
	    if (edge.r_ybot < tileArea.r_ybot) edge.r_ybot = tileArea.r_ybot;
	    if (edge.r_ytop < tileArea.r_ytop) edge.r_ytop = tileArea.r_ytop;
	    GeoTransRect(&cxp->tc_scx->scx_trans, &edge, &rootArea);
	    plotGremlinLine(&rootArea.r_ll, &rootArea.r_ur, GREMLIN_THIN);
	}
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * plotGremlinLabel --
 *
 * 	This procedure is invoked once for each label overlapping the
 *	area being plotted.  It generates Gremlin output to describe
 *	the label.
 *
 * Results:
 *	Always returns 0 to keep the search from aborting.
 *
 * Side effects:
 *	Gremlin information is output.
 *
 * ----------------------------------------------------------------------------
 */

int
plotGremlinLabel(scx, label)
    SearchContext *scx;		/* Describes state of search when label
				 * was found.
				 */
    Label *label;		/* Label that was found. */
{
    Rect rootArea;
    float x, y;
    float delta;
    int pos;

    /* Mapping from our GEO_xxx positions to Gremlin object types: */

    static char *gremlinPosition[] =
    {
	"CENTCENT",
	"BOTCENT",
	"BOTLEFT",
	"CENTLEFT",
	"TOPLEFT",
	"TOPCENT",
	"TOPRIGHT",
	"CENTRIGHT",
	"BOTRIGHT"
    };

    /* Compute a distance equal to 1/8th the size of a typical wire
     * (max of thicknesses of routing layers).  This is used to offset
     * text from labels and to compute cross size for point labels.
     */

    if (RtrMetalWidth > RtrPolyWidth)
	delta = RtrMetalWidth*scale/8;
    else delta = RtrPolyWidth*scale/8;

    /* Translate the label's area and relative position to root
     * coordinates, and figure out the point relative to which
     * the label is to be positioned.
     */

    GeoTransRect(&scx->scx_trans, &label->lab_rect, &rootArea);
    pos = GeoTransPos(&scx->scx_trans, label->lab_just);
    switch (pos)
    {
	case GEO_NORTH:
	case GEO_NORTHEAST:
	case GEO_NORTHWEST:
	    y = (rootArea.r_ytop - bbox.r_ybot) * scale;
	    y += delta;
	    break;

	case GEO_CENTER:
	case GEO_WEST:
	case GEO_EAST:
	    y = (rootArea.r_ytop + rootArea.r_ybot - 2*bbox.r_ybot)*scale/2;
	    break;
	
	case GEO_SOUTH:
	case GEO_SOUTHEAST:
	case GEO_SOUTHWEST:
	    y = (rootArea.r_ybot - bbox.r_ybot) * scale;
	    y -= delta;
	    break;
    }
    switch (pos)
    {
	case GEO_WEST:
	case GEO_NORTHWEST:
	case GEO_SOUTHWEST:
	    x = (rootArea.r_xbot - bbox.r_xbot) * scale;
	    x -= delta;
	    break;
	
	case GEO_CENTER:
	case GEO_NORTH:
	case GEO_SOUTH:
	    x = (rootArea.r_xtop + rootArea.r_xbot - 2*bbox.r_xbot)*scale/2;
	    break;
	
	case GEO_EAST:
	case GEO_NORTHEAST:
	case GEO_SOUTHEAST:
	    x = (rootArea.r_xtop - bbox.r_xbot) * scale;
	    x += delta;
	    break;
    }

    /* Output the text for the label, if the label is within delta
     * of the area we're plotting (a large label could overlap a
     * bit of the area but stick out way off-screen too).
     */
    
    if ((x >= -delta) && (y >= -delta) &&
	    (x <= ((bbox.r_xtop - bbox.r_xbot) * scale) + delta) &&
	    (y <= ((bbox.r_ytop - bbox.r_ybot) * scale) + delta))
    {
	fprintf(file, "%s\n", gremlinPosition[pos]);
	fprintf(file, "%.3f %.3f\n*\n", x, y);
	fprintf(file, "1 1\n");		/* Roman font, small characters */
	fprintf(file, "%d %s\n", strlen(label->lab_text), label->lab_text);
    }

    /* Output lines marking the label's area.  Different things are
     * done depending on whether the label is a point, a line, or an
     * area.
     */
    
    if ((rootArea.r_xbot == rootArea.r_xtop) &&
	    (rootArea.r_ybot == rootArea.r_ytop))
    {
	/* Point label.  Output a cross. */

	float top, bot;

	x = (rootArea.r_xbot - bbox.r_xbot) * scale;
	y = (rootArea.r_ybot - bbox.r_ybot) * scale;
        top = y + delta;
	bot = y - delta;
	fprintf(file, "VECTOR\n%.3f %.3f\n%.3f %.3f\n*\n", x, bot, x, top);
	fprintf(file, "6 1\n0 \n");		/* Medium thickness */
	top = x + delta;
	bot = x - delta;
	fprintf(file, "VECTOR\n%.3f %.3f\n%.3f %.3f\n*\n", bot, y, top, y);
	fprintf(file, "6 1\n0 \n");		/* Medium thickness */
    }
    else if ((rootArea.r_xbot == rootArea.r_xtop) ||
	     (rootArea.r_ybot == rootArea.r_ytop))
    {
	/* Line label.  Just draw a medium-thickness line. */

	plotGremlinLine(&rootArea.r_ll, &rootArea.r_ur, GREMLIN_MEDIUM);
    }
    else
    {
	/* Rectangular.  Draw lines around the boundary. */

	plotGremlinRect(&rootArea, GREMLIN_MEDIUM);
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * plotGremlinCell --
 *
 * 	This procedure is invoked once for each unexpanded cell that
 *	overlaps the area being plotted.
 *
 * Results:
 *	Always returns 0 to keep the search from aborting.
 *
 * Side effects:
 *	Gremlin information is output to describe the cell.
 *
 * ----------------------------------------------------------------------------
 */

int
plotGremlinCell(scx)
    SearchContext *scx;		/* Describes cell whose bbox is to
				 * be plotted.
				 */	
{
    extern bool PlotShowCellNames;
    char idName[100];
    Rect rootArea;
    CellDef *def;
    float x, y;

    /* Convert the cell's bounding box to root coordinates and then
     * draw as a thick outline.
     */

    def = scx->scx_use->cu_def;
    GeoTransRect(&scx->scx_trans, &def->cd_bbox, &rootArea);
    plotGremlinRect(&rootArea, GREMLIN_THICK);

    if (!PlotShowCellNames)
	return 0;

    /* Output the cell definition's name in the top of the bounding box.
     * Use a bold font (#3), in a medium size (#2).  Make sure that the
     * name's positioning point is within the area we're plotting.
     */

    x = (rootArea.r_xtop + rootArea.r_xbot - 2*bbox.r_xbot)*scale/2;
    y = (2*rootArea.r_ytop + rootArea.r_ybot - 3*bbox.r_ybot)*scale/3;
    if ((x >= 0) && (y >= 0) &&
	    (x <= (bbox.r_xtop - bbox.r_xbot)*scale) &&
	    (y <= (bbox.r_ytop - bbox.r_ybot)*scale))
    {
	fprintf(file, "CENTCENT\n%.3f %.3f\n*\n", x, y);
	fprintf(file, "3 2\n%d %s\n", strlen(def->cd_name), def->cd_name);
    }

    /* Output the cell id in the bottom of the bounding box.
     * Use an italic font (#2) in a medium size (#2).
     */

    x = (rootArea.r_xtop + rootArea.r_xbot - 2*bbox.r_xbot)*scale/2;
    y = (rootArea.r_ytop + 2*rootArea.r_ybot - 3*bbox.r_ybot)*scale/3;
    if ((x >= 0) && (y >= 0) &&
	    (x <= (bbox.r_xtop - bbox.r_xbot)*scale) &&
	    (y <= (bbox.r_ytop - bbox.r_ybot)*scale))
    {
	(void) DBPrintUseId(scx, idName, 100, TRUE);
	fprintf(file, "CENTCENT\n%.3f %.3f\n*\n", x, y);
	fprintf(file, "2 2\n%d %s\n", strlen(idName), idName);
    }

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotGremlin --
 *
 * 	This procedure generates a Gremlin file to describe an area of
 *	a layout.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
PlotGremlin(fileName, scx, layers, xMask)
    char *fileName;			/* Name of Gremlin file to write. */
    SearchContext *scx;			/* The use and area and transformation
					 * in this describe what to plot.
					 */
    TileTypeBitMask *layers;		/* Tells what layers to plot.  Only
					 * paint layers in this mask, and also
					 * expanded according to xMask, are
					 * plotted.  If L_LABELS is set, then
					 * labels on the layers are also
					 * plotted, if expanded according to
					 * xMask.  If L_CELL is set, then
					 * subcells that are unexpanded
					 * according to xMask are plotted as
					 * bounding boxes.
					 */
    int xMask;				/* An expansion mask, used to indicate
					 * the window whose expansion status
					 * will be used to determine
					 * visibility.  Zero means treat
					 * everything as expanded.
					 */
{
    int size, tmp;

    /* Compute a scale factor between our coordinates and Gremlin
     * coordinates.  Pick an even power of two that will make all
     * the Gremlin units fall between 0 and 512.
     */
    
    GeoTransRect(&scx->scx_trans, &scx->scx_area, &bbox);
    size = bbox.r_xtop - bbox.r_xbot;
    tmp = bbox.r_ytop - bbox.r_ybot;
    if (size < tmp) size = tmp;
    scale = 64.0;
    while (scale*size > 512)
	scale /= 2.0;
    
    /* Open the Gremlin file and output header information. */

    file = PaOpen(fileName, "w", (char *) NULL, ".", (char *) NULL,
	    (char **) NULL);
    if (file == NULL)
    {
	TxError("Couldn't write Gremlin file \"%s\".\n", fileName);
	return;
    }
    fprintf(file, "sungremlinfile\n");
    fprintf(file, "1 0.00 0.00\n");

    /* For each Gremlin style, find all the paint layers that belong
     * to that style and put plot information into the file.
     */
    
    for (curStyle = plotGremlinStyles; curStyle != NULL;
	 curStyle = curStyle->grs_next)
    {
	TTMaskAndMask3(&curMask, layers, &curStyle->grs_layers);
	(void) DBTreeSrTiles(scx, &curMask, xMask, plotGremlinPaint,
		(ClientData) NULL);
    }

    /* Output labels, if they are wanted. */

    if (TTMaskHasType(layers, L_LABEL))
    {
	curMask = *layers;
	TTMaskSetType(&curMask, TT_SPACE);
	(void) DBTreeSrLabels(scx, &curMask, xMask, (TerminalPath *) NULL,
		TF_LABEL_ATTACH, plotGremlinLabel, (ClientData) NULL);
    }

    /* Output subcell bounding boxes, if they are wanted. */

    if (TTMaskHasType(layers, L_CELL))
    {
	(void) DBTreeSrCells(scx, xMask, plotGremlinCell, (ClientData) NULL);
    }

    /* Output trailer information into the file, and close it. */

    fprintf(file, "-1\n");
    fclose(file);
    return;
}

#endif /* GREMLIN */
