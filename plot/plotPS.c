/*
 * plotPS.c --
 *
 * This file contains procedures that generate PS-format files
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/plot/plotPS.c,v 1.3 2010/06/24 12:37:25 tim Exp $";
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
 * PS output for a particular set of mask layers.  Each style
 * describes the PS figures to draw for a particular set of
 * layers.  A single layer may participate in several ps styles.
 */

typedef struct psstyle
{
    TileTypeBitMask grs_layers;		/* Layers to plot in this style. */
    int grs_stipple;			/* Index of fill to use. */
    int grs_color;			/* Index of color to use. */
    struct psstyle *grs_next;	/* Next style in chain. */
} PSStyle;

typedef struct pspattern
{
    int 		index;
    unsigned long	stipple[8];
    struct pspattern	*pat_next;
} PSPattern;

typedef struct pscolor
{
    int			index;
    unsigned char	color[4];
    struct pscolor	*col_next;
} PSColor;


static PSStyle *plotPSStyles = NULL;
static PSPattern *plotPSPatterns = NULL;
static PSColor	*plotPSColors = NULL;

int delta, xnmargin, ynmargin, xpmargin, ypmargin;
float fscale;

/* Most of the grs_stipple values are PS stipple numbers.  However,
 * if a grs_stipple value is less than zero, it means something special.
 * The definitions below give the possible alternatives:
 *
 * CROSS:		Draw a thick outline around the tile with
 *			a cross through it (used for contacts).
 * BORDER:		Same as CROSS, except draw the outline with
 *			no cross through it.
 * SOLID:		This is the same as a solid stipple but renders
 *			much faster.
 */

#define CROSS  -1
#define BORDER -2
#define SOLID  -3

/* The definitions below give the integers used for various PS
 * line drawing styles (brushes).
 */

#define PS_THIN		1
#define PS_MEDIUM	2
#define PS_THICK	3

/* The variables below are used to pass information from the top-level
 * procedure PlotPS down to the lower-level search functions
 * that are invoked for pieces of the layout.
 */

static FILE *file;		/* File to use for output. */
static PSStyle *curStyle;	/* Current style being output. */
static PSColor *curColor;	/* Current color being output. */
static PSPattern *curPattern;	/* Current pattern being output. */
static int curLineWidth;	/* Current line width */
static int curFont;		/* Current font */
static TileTypeBitMask curMask;	/* Layers currently being searched:  this
				 * is the AND of the mask from curStyle and
				 * the layers that the user specified.
				 */
static Rect bbox;		/* Bounding box, in root coordinates, of
				 * area being plotted.
				 */

/* Parameters passed to the plotting process */

static char *defaultBoldFont = "/HelveticaBold";
static char *defaultFont = "/Helvetica";
char *PlotPSIdFont = NULL;
char *PlotPSNameFont = NULL;
char *PlotPSLabelFont = NULL;
int PlotPSIdSize = 8;
int PlotPSNameSize = 12;
int PlotPSLabelSize = 12;
int PlotPSBoundary = 1;	 /* Print boundaries around all layers */
int PlotPSHeight = 792;  /* 11 inches * 72 PS units/inch */
int PlotPSWidth = 612;   /* 8.5 inches */
int PlotPSMargin = 72;   /* 1 inch */

int curx1, curx2, cury1, cury2;		    /* Last encountered line */
int curxbot, curybot, curwidth, curheight;  /* Last encountered rectangle */

/*
 * ----------------------------------------------------------------------------
 *
 * PSReset()
 *
 *	Plot optimization:   Reset buffered line and rectangle to default values
 *
 * ----------------------------------------------------------------------------
 */

void
PSReset()
{
    curxbot = curybot = curwidth = curheight = -2;
    curx1 = curx2 = cury1 = cury2 = -2;
}


/*
 * ----------------------------------------------------------------------------
 *	PlotPSTechInit --
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

void
PlotPSTechInit()
{
    int i, j;
    PSStyle *style;
    PSColor *color;
    PSPattern *pattern;

    /* Clear out any old information */

    for (style = plotPSStyles; style != NULL; style = style->grs_next)
    {
	freeMagic((char *) style);
    }
    plotPSStyles = NULL;

    for (pattern = plotPSPatterns; pattern != NULL; pattern = pattern->pat_next)
    {
	freeMagic((char *) pattern);
    }
    plotPSPatterns = NULL;

    for (color = plotPSColors; color != NULL; color = color->col_next)
    {
	freeMagic((char *) color);
    }
    plotPSColors = NULL;

    if (!PlotPSIdFont)
	StrDup(&PlotPSIdFont, defaultFont);
    if (!PlotPSNameFont)
	StrDup(&PlotPSNameFont, defaultBoldFont);
    if (!PlotPSLabelFont)
	StrDup(&PlotPSLabelFont, defaultFont);
}

/*
 * ----------------------------------------------------------------------------
 *	PlotPSTechLine --
 *
 * 	This procedure is invoked by the technology module once for
 *	each line in the "ps" subsection of the "plot" section
 *	of the technology file.
 *
 * Results:
 *	Always returns TRUE (otherwise the technology module would
 *	abort Magic with a fatal error).
 *
 * Side effects:
 *	Builds up the table of PS styles.
 * ----------------------------------------------------------------------------
 */

bool
PlotPSTechLine(sectionName, argc, argv)
    char *sectionName;		/* Name of this section (unused). */
    int argc;			/* Number of arguments on line. */
    char *argv[];		/* Pointers to fields of line. */
{
    PSStyle *newstyle;
    PSColor *newcolor;
    PSPattern *newpattern;
    int i, color, stipple;
    
    if (argc != 9 && argc != 5 && argc != 3)
    {
	TechError("\"ps\" lines must have either 9, 5, or 3 arguments.\n");
	return TRUE;
    }

    if (argc == 9)	/* pattern definition */
    {
	newpattern = (PSPattern *) mallocMagic(sizeof(PSPattern));
	sscanf(argv[0], "%d", &(newpattern->index));
	for(i = 0; i < 8; i++)
	{
	    sscanf(argv[1 + i], "%08lx", &(newpattern->stipple[i]));
	}
	newpattern->pat_next = plotPSPatterns;
	plotPSPatterns = newpattern;
    }
    else if (argc == 5)	/* color definition */
    {
	int tmpint;
	newcolor = (PSColor *) mallocMagic(sizeof(PSColor));
	sscanf(argv[0], "%d", &(newcolor->index));
	for(i = 0; i < 4; i++)
	{
	    sscanf(argv[1 + i], "%d", &tmpint);
	    newcolor->color[i] = (unsigned char)(tmpint & 0xff);
	}
	newcolor->col_next = plotPSColors;
	plotPSColors = newcolor;
    }
    else {   /* 3 args: layer definition */
	if (!StrIsInt(argv[1]))
	{
	    TechError("2nd field must be an integer\n");
	    return TRUE;
	}
	color = atoi(argv[1]);

	if (strcmp(argv[2], "X") == 0)
	    stipple = CROSS;
	else if (strcmp(argv[2], "B") == 0)
	    stipple = BORDER;
	else if (strcmp(argv[2], "S") == 0)
	    stipple = SOLID;
	else
	{
	    if (!StrIsInt(argv[2]))
	    {
		TechError("3rd field must be an integer or \"S\", \"X\", or \"B\".\n");
		return TRUE;
	    }
	    stipple = atoi(argv[2]);
        }

	newstyle = (PSStyle *) mallocMagic(sizeof(PSStyle));

	DBTechNoisyNameMask(argv[0], &newstyle->grs_layers);

	/* Replace non-primary contact images with primary images. */

	for (i = TT_TECHDEPBASE; i < DBNumTypes; i++)
	{
	    if TTMaskHasType(&newstyle->grs_layers, i)
		TTMaskSetMask(&newstyle->grs_layers, &DBLayerTypeMaskTbl[i]);
	}
	TTMaskAndMask(&newstyle->grs_layers, &DBUserLayerBits);
	newstyle->grs_stipple = stipple;
	newstyle->grs_color = color;
	newstyle->grs_next = plotPSStyles;
	plotPSStyles = newstyle;
    }

    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * plotPSFlushRect()
 *
 *	Plot optimization:   Draw last buffered rectangle.
 *
 * ----------------------------------------------------------------------------
 */

void
plotPSFlushRect(style)
    int style;
{
    if (curwidth > 0)
    {
	if (style == SOLID)
	    fprintf(file, "%d %d %d %d ms\n", curxbot, curybot,
			curwidth, curheight);
	else
	    fprintf(file, "%d %d %d %d fb\n", curxbot, curybot,
			curxbot + curwidth, curybot + curheight);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * plotPSFlushLine()
 *
 *	Plot optimization:   Draw last buffered line.
 *
 * ----------------------------------------------------------------------------
 */

void
plotPSFlushLine()
{
    if (cury1 == cury2)
    {
	if (curx1 != curx2)	/* true only if nothing is buffered */
	    fprintf(file, "%d %d %d hl\n", curx2 - curx1, curx1, cury1);
    }
    else if (curx1 == curx2)
	fprintf(file, "%d %d %d vl\n", cury2 - cury1, curx1, cury1);
    else
	fprintf(file, "%d %d %d %d ml\n", curx1, cury1, curx2, cury2);
}


/*
 * ----------------------------------------------------------------------------
 *
 * plotPSLine --
 *
 * 	Outputs a line into the current PS file.
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
plotPSLine(p1, p2)
    Point *p1, *p2;		/* Endpoints of line, given in root
				 * coordinates.
				 */
{
    int x1, x2, y1, y2, limit, diff;
    bool tmptf;

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

    /* Now clip against horizontal lines at the y-boundaries. */

    if (y2 < y1)
    {
	float tmp;
	tmp = y2; y2 = y1; y1 = tmp;
	tmp = x2; x2 = x1; x1 = tmp;
    }
    limit = bbox.r_ytop - bbox.r_ybot;
    if ((y1 > limit) || (y2 < 0)) return;

    /* compare against last output line and merge if possible */
    if (((x1 == x2) && (x1 == curx1) && (x2 == curx2)
		&& ((tmptf = (y1 == cury2)) || (y2 == cury1))))
    {
	if (tmptf) cury2 = y2; 
	else cury1 = y1;
    }
    else if (((y1 == y2) && (y1 == cury1) && (y2 == cury2)
		&& ((tmptf = (x1 == curx2)) || (x2 == curx1))))
    {
	if (tmptf) curx2 = x2;
	else curx1 = x1;
    }
    else
    {
	plotPSFlushLine();
	curx1 = x1;
	curx2 = x2;
	cury1 = y1;
	cury2 = y2;
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * plotPSRect --
 *
 * 	Outputs PS statements to draw a rectangular area as
 *	an outline with a given line style.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds information to the current PS file.
 *
 * ----------------------------------------------------------------------------
 */

void
plotPSRect(rect, style)
    Rect *rect;	/* Rectangle to be drawn, in root coords. */
    int style;
{
    int x, y, w, h;

    /* Output all boxes with any part visible.  Depend on PostScript to */
    /* do the clipping of any boxes crossing the plot boundary.		*/

    x = rect->r_xbot - bbox.r_xbot;
    if ((x < 0) || (rect->r_xbot > bbox.r_xtop)) return;
    w = rect->r_xtop - rect->r_xbot;
    y = rect->r_ybot - bbox.r_ybot;
    if ((y < 0) || (rect->r_ybot > bbox.r_ytop)) return;
    h = rect->r_ytop - rect->r_ybot;

    fprintf(file, "%d %d %d %d m%c\n", x, y, w, h, (style == CROSS) ? 'x' :
		(style == SOLID) ? 's' : 'r');
}


/*
 * ----------------------------------------------------------------------------
 *
 * plotPSPaint --
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
plotPSPaint(tile, cxp)
    Tile *tile;			/* Tile that's of type to be output. */
    TreeContext *cxp;		/* Describes search in progress. */
{
    Rect tileArea, edge, rootArea;
    int xbot, width, ybot, height;
    Tile *neighbor;
    SearchContext *scx = cxp->tc_scx;
    bool tmptf;
    TileType ntype;

    /* First transform tile coords to root coords */
    
    TiToRect(tile, &tileArea);
    GeoTransRect(&scx->scx_trans, &tileArea, &rootArea);

    /* See if this tile gets special handling. */

    if ((curStyle->grs_stipple == CROSS) || (curStyle->grs_stipple == BORDER))
    {
	/* Draw tile as a thick outline with a cross from corner
	 * to corner, and skip the rest of this procedure.
	 */

	Point ul, lr;

	if (curLineWidth != PS_MEDIUM) {
	    fprintf(file, "l2\n");
	    curLineWidth = PS_MEDIUM;
	}

	plotPSRect(&rootArea, curStyle->grs_stipple);
	return 0;
    }

    /* If this is a triangle, output the last rect and deal with this one */
    /* individually.							  */

    if (IsSplit(tile))
    {
	int np, i, j;
	TileType dinfo;
	Point polyp[5];

	plotPSFlushRect(curStyle->grs_stipple);
	plotPSFlushLine();
	PSReset();

	/* Side and direction are altered by geometric transformations */

	dinfo = DBTransformDiagonal(TiGetTypeExact(tile), &scx->scx_trans);

	/* Use GrClipTriangle() routine to get the n-sided polygon that */
	/* results from clipping a triangle to the clip region.		*/

	GrClipTriangle(&rootArea, &bbox, TRUE, dinfo, polyp, &np);
	for (i = 0; i < np; i++)
	{
	   polyp[i].p_x -= bbox.r_xbot;
	   polyp[i].p_y -= bbox.r_ybot;
	   fprintf(file, "%d %d ", polyp[i].p_x, polyp[i].p_y);
	}
	fprintf(file, "%d tb\n", np);

	if (PlotPSBoundary)
	{
	    if (curLineWidth != PS_THIN) {
		fprintf(file, "l1\n");
		curLineWidth = PS_THIN;
	    }

	    /* Diagonal is always drawn */

	    for (i = 0; i < np; i++)
	    {
		j = (i + 1) % np;
		if (polyp[i].p_x != polyp[j].p_x &&
			polyp[i].p_y != polyp[j].p_y)
		{
		    fprintf(file, "%d %d %d %d ml\n", polyp[i].p_x, polyp[i].p_y,
			polyp[j].p_x, polyp[j].p_y);
		    break;
		}
	    }
	}
    }
    else
    {

	/* This tile gets "normal" processing (i.e. stippling and outlining).
	 * Clip it to the plotting area and output.
	 */

	GeoClip(&rootArea, &bbox);
	xbot = rootArea.r_xbot - bbox.r_xbot;
	width = rootArea.r_xtop - rootArea.r_xbot;
	ybot = rootArea.r_ybot - bbox.r_ybot;
	height = rootArea.r_ytop - rootArea.r_ybot;

	/* compare against last output rectangle and merge if possible */
	if ((width == curwidth) && (xbot == curxbot) && ((tmptf = (ybot == curybot
			+ curheight)) || (ybot + height == curybot)))
	{
	    curheight += height;
	    if (!tmptf) curybot = ybot; 
	}
	else if ((height == curheight) && (ybot == curybot) && ((tmptf = (xbot	
		== curxbot + curwidth)) || (xbot + width == curxbot)))
	{
	    curwidth += width;
	    if (!tmptf) curxbot = xbot;
	}
	else
	{
	    plotPSFlushRect(curStyle->grs_stipple);
	    curheight = height;
	    curwidth = width;
	    curxbot = xbot;
	    curybot = ybot;
	}

	if (PlotPSBoundary && (curLineWidth != PS_THIN)) {
	    fprintf(file, "l1\n");
	    curLineWidth = PS_THIN;
	}
    }

    if (!PlotPSBoundary) return 0;	/* No borders */

    /* Now output lines for any edges between material of the type
     * currently being drawn and material of other types.  This is
     * done by searching along the tile's borders for neighbors that
     * have the wrong types.  First, search the tile's bottom border
     * (unless it is at infinity).
     *
     * (This code is essentially a duplicate of selRedisplayFunc())
     */
    
    if (IsSplit(tile) && (!(SplitSide(tile) ^ SplitDirection(tile))))
        goto searchleft;        /* nothing on bottom of split */

    if (tileArea.r_ybot > TiPlaneRect.r_ybot)
    {
	edge.r_ybot = edge.r_ytop = tileArea.r_ybot;
	for (neighbor = LB(tile); LEFT(neighbor) < tileArea.r_xtop;
		neighbor = TR(neighbor))
	{
	    ntype = TiGetTopType(neighbor);
	    if (TTMaskHasType(&curMask, ntype)) continue;
	    edge.r_xbot = LEFT(neighbor);
	    edge.r_xtop = RIGHT(neighbor);
	    if (edge.r_xbot < tileArea.r_xbot) edge.r_xbot = tileArea.r_xbot;
	    if (edge.r_xtop > tileArea.r_xtop) edge.r_xtop = tileArea.r_xtop;
	    GeoTransRect(&scx->scx_trans, &edge, &rootArea);
	    plotPSLine(&rootArea.r_ll, &rootArea.r_ur);
	}
    }

    /* Now go along the tile's left border, doing the same thing.   Ignore
     * edges that are at infinity.
     */

searchleft:
    if (IsSplit(tile) && SplitSide(tile))
	goto searchtop;		/* Nothing on left side of tile */

    if (tileArea.r_xbot > TiPlaneRect.r_xbot)
    {
	edge.r_xbot = edge.r_xtop = tileArea.r_xbot;
	for (neighbor = BL(tile); BOTTOM(neighbor) < tileArea.r_ytop;
		neighbor = RT(neighbor))
	{
 	    ntype = TiGetRightType(neighbor);
	    if (TTMaskHasType(&curMask, ntype)) continue;
	    edge.r_ybot = BOTTOM(neighbor);
	    edge.r_ytop = TOP(neighbor);
	    if (edge.r_ybot < tileArea.r_ybot) edge.r_ybot = tileArea.r_ybot;
	    if (edge.r_ytop > tileArea.r_ytop) edge.r_ytop = tileArea.r_ytop;
	    GeoTransRect(&scx->scx_trans, &edge, &rootArea);
	    plotPSLine(&rootArea.r_ll, &rootArea.r_ur);
	}
    }

    /* Same thing for the tile's top border. */

searchtop:
    if (IsSplit(tile) && (SplitSide(tile) ^ SplitDirection(tile)))
	goto searchright;		/* Nothing on top side of tile */

    if (tileArea.r_ytop < TiPlaneRect.r_ytop)
    {
	edge.r_ybot = edge.r_ytop = tileArea.r_ytop;
	for (neighbor = RT(tile); RIGHT(neighbor) > tileArea.r_xbot;
		neighbor = BL(neighbor))
	{
	    ntype = TiGetBottomType(neighbor);
	    if (TTMaskHasType(&curMask, ntype)) continue;
	    edge.r_xbot = LEFT(neighbor);
	    edge.r_xtop = RIGHT(neighbor);
	    if (edge.r_xbot < tileArea.r_xbot) edge.r_xbot = tileArea.r_xbot;
	    if (edge.r_xtop > tileArea.r_xtop) edge.r_xtop = tileArea.r_xtop;
	    GeoTransRect(&scx->scx_trans, &edge, &rootArea);
	    plotPSLine(&rootArea.r_ll, &rootArea.r_ur);
	}
    }

    /* Finally, the right border. */

searchright:
    if (IsSplit(tile) && !SplitSide(tile))
	return 0;		/* Nothing on right side of tile */

    if (tileArea.r_xtop < TiPlaneRect.r_xtop)
    {
	edge.r_xbot = edge.r_xtop = tileArea.r_xtop;
	for (neighbor = TR(tile); TOP(neighbor) > tileArea.r_ybot;
		neighbor = LB(neighbor))
	{
	    ntype = TiGetLeftType(neighbor);
	    if (TTMaskHasType(&curMask, ntype)) continue;
	    edge.r_ybot = BOTTOM(neighbor);
	    edge.r_ytop = TOP(neighbor);
	    if (edge.r_ybot < tileArea.r_ybot) edge.r_ybot = tileArea.r_ybot;
	    if (edge.r_ytop > tileArea.r_ytop) edge.r_ytop = tileArea.r_ytop;
	    GeoTransRect(&scx->scx_trans, &edge, &rootArea);
	    plotPSLine(&rootArea.r_ll, &rootArea.r_ur);
	}
    }

    return 0;
}


/*
 * ----------------------------------------------------------------------------
 *
 * plotPSLabelPosition --
 *
 *	Determine the label position, orientation, and approximate bounding box
 *
 * ----------------------------------------------------------------------------
 */

int
plotPSLabelPosition(scx, label, x, y, p)
    SearchContext *scx;		/* Describes state of search when label
				 * was found.
				 */
    Label *label;		/* Label that was found. */
    int *x;			/* returned x position */
    int *y;			/* returned y position */
    int *p;			/* returned orientation */
{
    Rect rootArea;
    int pos;

    /* Mapping from our GEO_xxx positions to PS object types: */

    static int psPosition[] =
    {
	 5, /* CENTCENT */
	 1, /* TOPCENT */
	 0, /* TOPRIGHT */
	 4, /* CENTRIGHT */
	12, /* BOTRIGHT */
	13, /* BOTCENT */
	15, /* BOTLEFT */
	 7, /* CENTLEFT */
	 3  /* TOPLEFT */
    };

    GeoTransRect(&scx->scx_trans, &label->lab_rect, &rootArea);
    pos = GeoTransPos(&scx->scx_trans, label->lab_just);
    switch (pos)
    {
	case GEO_NORTH:
	case GEO_NORTHEAST:
	case GEO_NORTHWEST:
	    *y = (rootArea.r_ytop - bbox.r_ybot);
	    *y += delta;
	    break;

	case GEO_CENTER:
	case GEO_WEST:
	case GEO_EAST:
	    *y = (rootArea.r_ytop + rootArea.r_ybot) / 2 - bbox.r_ybot;
	    break;
	
	case GEO_SOUTH:
	case GEO_SOUTHEAST:
	case GEO_SOUTHWEST:
	    *y = (rootArea.r_ybot - bbox.r_ybot);
	    *y -= delta;
	    break;
    }
    switch (pos)
    {
	case GEO_WEST:
	case GEO_NORTHWEST:
	case GEO_SOUTHWEST:
	    *x = (rootArea.r_xbot - bbox.r_xbot);
	    *x -= delta;
	    break;
	
	case GEO_CENTER:
	case GEO_NORTH:
	case GEO_SOUTH:
	    *x = (rootArea.r_xtop + rootArea.r_xbot) / 2 - bbox.r_xbot;
	    break;
	
	case GEO_EAST:
	case GEO_NORTHEAST:
	case GEO_SOUTHEAST:
	    *x = (rootArea.r_xtop - bbox.r_xbot);
	    *x += delta;
	    break;
    }
    *p = psPosition[pos];
    return 0;
}


/*
 * ----------------------------------------------------------------------------
 *
 * plotPSLabelBounds --
 *
 *	Estimate the bounding box extension based on label strings.
 *	In reality, we need to know label sizes to compute the scale,
 *	and we need to know the scale to compute label sizes.  However,
 *	in practice, we can only estimate the label size anyway, so we
 *	allow for some slop and just wing it.
 *
 * ----------------------------------------------------------------------------
 */

#define AVGCHARWIDTH 0.7
#define CHARHEIGHT 1.4

int
plotPSLabelBounds(scx, label)
    SearchContext *scx;		/* Describes state of search when label
				 * was found.
				 */
    Label *label;		/* Label that was found. */
{
    int pspos;
    int ls, psxsize, psysize;
    int llx, lly, urx, ury;
    int psdelta = (int)((float)delta / fscale);

    plotPSLabelPosition(scx, label, &llx, &lly, &pspos);
    urx = (int)((float)(llx - bbox.r_xtop) / fscale);
    ury = (int)((float)(lly - bbox.r_ytop) / fscale);
    llx = (int)((float)(bbox.r_xbot - llx) / fscale);
    lly = (int)((float)(bbox.r_ybot - lly) / fscale);
    ls = strlen(label->lab_text);
  
    psxsize = ls * (int)((float)PlotPSLabelSize * AVGCHARWIDTH);
    psysize = (int)((float)PlotPSLabelSize * CHARHEIGHT);

    switch (pspos) {
	case 0:
	    ury += psysize + psdelta;
	    urx += psxsize + psdelta;
	    break;
	case 4:
	    ury += psysize / 2;
	    lly += psysize / 2;
	    urx += psxsize + psdelta;
	    break;
	case 12:
	    lly += psysize + psdelta;
	    urx += psxsize + psdelta;
	    break;
	case 13:
	    lly += psysize + psdelta;
	    urx += psxsize / 2;
	    llx += psxsize / 2;
	    break;
	case 15:
	    lly += psysize + psdelta;
	    llx += psxsize + psdelta;
	    break;
	case 7:
	    ury += psysize / 2;
	    lly += psysize / 2;
	    llx += psxsize + psdelta;
	    break;
	case 3:
	    ury += psysize + psdelta;
	    llx += psxsize + psdelta;
	    break;
	case 1:
	    ury += psysize + psdelta;
	    urx += psxsize / 2;
	    llx += psxsize / 2;
	    break;
	case 5:
	    ury += psysize / 2;
	    lly += psysize / 2;
	    urx += psxsize / 2;
	    llx += psxsize / 2;
	    break;
    }
 
    if (xpmargin < urx) xpmargin = urx;
    if (ypmargin < ury) ypmargin = ury;
    if (xnmargin < llx) xnmargin = llx;
    if (ynmargin < lly) ynmargin = lly;

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * plotPSLabelBox --
 *
 *	Output the box connected to a label
 *
 * ----------------------------------------------------------------------------
 */

int
plotPSLabelBox(scx, label)
    SearchContext *scx;		/* Describes state of search when label
				 * was found.
				 */
    Label *label;		/* Label that was found. */
{
    Rect rootArea;
    int x, y;

    GeoTransRect(&scx->scx_trans, &label->lab_rect, &rootArea);

    /* Output lines marking the label's area.  Different things are
     * done depending on whether the label is a point, a line, or an
     * area.
     */
    
    if (curLineWidth != PS_MEDIUM) {
	fprintf(file, "l2\n");
	curLineWidth = PS_MEDIUM;
    }

    if ((rootArea.r_xbot == rootArea.r_xtop) &&
	    (rootArea.r_ybot == rootArea.r_ytop))
    {
	/* Point label.  Output a cross. */

	x = (rootArea.r_xbot - bbox.r_xbot);
	y = (rootArea.r_ybot - bbox.r_ybot);
	fprintf(file, "%d %d %d pl\n", delta, x, y);
    }
    else if ((rootArea.r_xbot == rootArea.r_xtop) ||
	     (rootArea.r_ybot == rootArea.r_ytop))
    {
	/* Line label.  Just draw a medium-thickness line. */

	plotPSLine(&rootArea.r_ll, &rootArea.r_ur);
    }
    else
    {
	/* Rectangular.  Draw lines around the boundary. */

	plotPSRect(&rootArea, 0);
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * plotPSLabel --
 *
 * 	This procedure is invoked once for each label overlapping the
 *	area being plotted.  It generates PS output to describe
 *	the label.
 *
 * Results:
 *	Always returns 0 to keep the search from aborting.
 *
 * Side effects:
 *	PS information is output.
 *
 * ----------------------------------------------------------------------------
 */

int
plotPSLabel(scx, label)
    SearchContext *scx;		/* Describes state of search when label
				 * was found.
				 */
    Label *label;		/* Label that was found. */
{
    int x, y;
    int pspos;

    plotPSLabelPosition(scx, label, &x, &y, &pspos);

    /* Output the text for the label, if the label is within delta
     * of the area we're plotting (a large label could overlap a
     * bit of the area but stick out way off-screen too).
     */
    
    if ((x >= -delta) && (y >= -delta) &&
	    (x <= (bbox.r_xtop - bbox.r_xbot) + delta) &&
	    (y <= (bbox.r_ytop - bbox.r_ybot) + delta))
    {
	fprintf(file, "(%s) %d %d %d lb\n", label->lab_text, pspos, x, y);
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * plotPSCell --
 *
 * 	This procedure is invoked once for each unexpanded cell that
 *	overlaps the area being plotted.
 *
 * Results:
 *	Always returns 0 to keep the search from aborting.
 *
 * Side effects:
 *	PS information is output to describe the cell.
 *
 * ----------------------------------------------------------------------------
 */

int
plotPSCell(scx)
    SearchContext *scx;		/* Describes cell whose bbox is to
				 * be plotted.
				 */	
{
    extern bool PlotShowCellNames;
    char idName[100];
    Rect rootArea;
    CellDef *def;
    int x, y;

    /* Convert the cell's bounding box to root coordinates and then
     * draw as a thick outline.
     */

    def = scx->scx_use->cu_def;
    GeoTransRect(&scx->scx_trans, &def->cd_bbox, &rootArea);
    if (curLineWidth != PS_THICK) {
	fprintf(file, "l3\n");
	curLineWidth = PS_THICK;
    }
    plotPSRect(&rootArea, 0);

    if (!PlotShowCellNames)
	return 0;

    /* Output the cell definition's name in the top of the bounding box.
     * Use a bold font (#3), in a medium size (#2).  Make sure that the
     * name's positioning point is within the area we're plotting.
     */

    x = (rootArea.r_xtop + rootArea.r_xbot - 2*bbox.r_xbot)/2;
    y = (2*rootArea.r_ytop + rootArea.r_ybot - 3*bbox.r_ybot)/3;
    if ((x >= 0) && (y >= 0) &&
	    (x <= (bbox.r_xtop - bbox.r_xbot)) &&
	    (y <= (bbox.r_ytop - bbox.r_ybot)))
    {
	fprintf(file, "f2 (%s) 5 %d %d lb\n", def->cd_name, x, y);
    }

    /* Output the cell id in the bottom of the bounding box.
     * Use an italic font (#2) in a medium size (#2).
     */

    x = (rootArea.r_xtop + rootArea.r_xbot - 2*bbox.r_xbot)/2;
    y = (rootArea.r_ytop + 2*rootArea.r_ybot - 3*bbox.r_ybot)/3;
    if ((x >= 0) && (y >= 0) &&
	    (x <= (bbox.r_xtop - bbox.r_xbot)) &&
	    (y <= (bbox.r_ytop - bbox.r_ybot)))
    {
	(void) DBPrintUseId(scx, idName, 100, TRUE);
	fprintf(file, "f3 (%s) 5 %d %d lb\n", idName, x, y);
    }

    return 0;
}


/*
 * ----------------------------------------------------------------------------
 *
 * PlotPS --
 *
 * 	This procedure generates a PS file to describe an area of
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
PlotPS(fileName, scx, layers, xMask)
    char *fileName;			/* Name of PS file to write. */
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
    int xsize, ysize;
    float yscale;
    FILE *infile;
    int i, j;
    int twidth, theight;
    char *fontptr, *fptr2, *fptr3;
    char line_in[100];

    PSReset();

    /* Compute a scale factor between our coordinates and PS
     * coordinates.
     */
    
    GeoTransRect(&scx->scx_trans, &scx->scx_area, &bbox);
    xsize = bbox.r_xtop - bbox.r_xbot;
    ysize = bbox.r_ytop - bbox.r_ybot;
    fscale = (float)(PlotPSWidth - 2 * PlotPSMargin) / (float)xsize;
    yscale = (float)(PlotPSHeight - 2 * PlotPSMargin) / (float)ysize;
    if (yscale < fscale) fscale = yscale;

    /* Compute a distance equal to 1/8th the size of a typical wire
     * (max of thicknesses of routing layers).  This is used to offset
     * text from labels and to compute cross size for point labels.
     */

    if (RtrMetalWidth > RtrPolyWidth)
	delta = RtrMetalWidth / 8;
    else delta = RtrPolyWidth / 8;
    if (delta == 0) delta = 1;

    /* Go through labels once to estimate the bounding box, including labels */

    xnmargin = ynmargin = xpmargin = ypmargin = 0;
    if (TTMaskHasType(layers, L_LABEL))
    {
	curMask = *layers;
	TTMaskSetType(&curMask, TT_SPACE);
	(void) DBTreeSrLabels(scx, &curMask, xMask, (TerminalPath *) NULL,
		TF_LABEL_ATTACH, plotPSLabelBounds, (ClientData) NULL);
	fscale = (float)(PlotPSWidth - 2 * PlotPSMargin - xnmargin - xpmargin)
		/ (float)(xsize);
	yscale = (float)(PlotPSHeight - 2 * PlotPSMargin - ynmargin - ypmargin)
		/ (float)(ysize);
        if (yscale < fscale) fscale = yscale;
    }
    twidth = (xsize * fscale) + xnmargin + xpmargin;
    theight = (ysize * fscale) + ynmargin + ypmargin;

    /* Open the PS file and output header information. */

    file = PaOpen(fileName, "w", (char *) NULL, ".", (char *) NULL,
	    (char **) NULL);
    if (file == NULL)
    {
	TxError("Couldn't write PS file \"%s\".\n", fileName);
	return;
    }
    fprintf(file, "%%!PS-Adobe-3.0 EPSF-3.0\n");
    fprintf(file, "%%%%BoundingBox: %d %d %d %d\n",
		PlotPSMargin, PlotPSMargin, twidth + PlotPSMargin,
		theight + PlotPSMargin);
    fontptr = PlotPSIdFont;
    fprintf(file, "%%%%DocumentNeededResources: font %s", fontptr);
    if (!Match(fptr2 = PlotPSNameFont, fontptr));
        fprintf(file, " font %s", fptr2);
    if (!Match(fptr3 = PlotPSLabelFont, fontptr))
	if (!Match(fptr3, fptr2))
            fprintf(file, " font %s", fptr3);
    fprintf(file, "\n");
    fprintf(file, "%%%%EndComments\n");

    /* Insert the prolog here */

    infile = PaOpen("magicps", "r", ".pro", ".", SysLibPath, NULL);
    if (infile != NULL)
	while(fgets(line_in, 99, infile) != NULL)
	    fputs(line_in, file);
    else
	fprintf(file, "\npostscript_prolog_is_missing\n\n");

    /* Insert the font definitions here. */

    fprintf(file, "/f1 { %.3f %s sf } def\n", (float)PlotPSLabelSize / fscale,
		PlotPSLabelFont);
    fprintf(file, "/f2 { %.3f %s sf } def\n", (float)PlotPSNameSize / fscale,
		PlotPSNameFont);
    fprintf(file, "/f3 { %.3f %s sf } def\n", (float)PlotPSIdSize / fscale,
		PlotPSIdFont);

    /* Insert the color and stipple definitions here. */

    for (curColor = plotPSColors; curColor != NULL;
	 curColor = curColor->col_next)
    {
	fprintf(file, "/col%d {%.3f %.3f %.3f %.3f sc} bind def\n",
		curColor->index,
		(float)curColor->color[0] / 255.0,
		(float)curColor->color[1] / 255.0,
		(float)curColor->color[2] / 255.0,
		(float)curColor->color[3] / 255.0);
    }

    for (curPattern = plotPSPatterns; curPattern != NULL;
	curPattern = curPattern->pat_next)
    {
	fprintf(file, "{<");
	for (j = 0; j < 8; j++)
	    fprintf(file, "%08lx%08lx", curPattern->stipple[j], curPattern->stipple[j]);
	fprintf(file, ">} %d dp\n", curPattern->index);
    }

    fprintf(file, "%%%%EndResource\n%%%%EndProlog\n\n");
    fprintf(file, "%%%%Page: 1 1\n");
    fprintf(file, "/pgsave save def bop\n");
    fprintf(file, "%% 0 0 offsets\nninit\n");
    fprintf(file, "%d %d translate\n", PlotPSMargin + xnmargin, PlotPSMargin
		+ ynmargin);
    fprintf(file, "%.3f %.3f scale\nminit\n", fscale, fscale);
    fprintf(file, "0 0 %d %d gsave rectclip\n", xsize, ysize);
    fprintf(file, "l2\nsp\n\n");

    curLineWidth = PS_MEDIUM;

    /* For each PS style, find all the paint layers that belong
     * to that style and put plot information into the file.
     */
    
    for (curStyle = plotPSStyles; curStyle != NULL;
	 curStyle = curStyle->grs_next)
    {
	fprintf(file, "col%d\n", curStyle->grs_color);
	if (curStyle->grs_stipple >= 0)
	    fprintf(file, "%d sl\n", curStyle->grs_stipple);
	TTMaskAndMask3(&curMask, layers, &curStyle->grs_layers);
	(void) DBTreeSrTiles(scx, &curMask, xMask, plotPSPaint,
		(ClientData) NULL);
	plotPSFlushRect(curStyle->grs_stipple);
	plotPSFlushLine();
	PSReset();
    }

    /* Output subcell bounding boxes, if they are wanted. */

    if (TTMaskHasType(layers, L_CELL))
    {
	(void) DBTreeSrCells(scx, xMask, plotPSCell, (ClientData) NULL);
	plotPSFlushRect(BORDER);
	plotPSFlushLine();
    }

    /* Output label boxes followed by labels */

    if (TTMaskHasType(layers, L_LABEL))
    {
	curMask = *layers;
	TTMaskSetType(&curMask, TT_SPACE);
	(void) DBTreeSrLabels(scx, &curMask, xMask, (TerminalPath *) NULL,
		TF_LABEL_ATTACH, plotPSLabelBox, (ClientData) NULL);
	plotPSFlushRect(BORDER);
	plotPSFlushLine();
	PSReset();
        fprintf(file, "grestore\n");	/* end clipping rectangle */
        fprintf(file, "f1 0 setgray\n");  /* set font, set color to black */

	curMask = *layers;
	TTMaskSetType(&curMask, TT_SPACE);
	(void) DBTreeSrLabels(scx, &curMask, xMask, (TerminalPath *) NULL,
		TF_LABEL_ATTACH, plotPSLabel, (ClientData) NULL);
    }
    else
    {
        fprintf(file, "grestore\n");	/* end clipping rectangle */
    }

    /* Output trailer information into the file, and close it. */

    fprintf(file, "pgsave restore showpage\n\n");
    fprintf(file, "%%%%Trailer\nMAGICsave restore\n%%%%EOF\n");
    fclose(file);
    return;
}
