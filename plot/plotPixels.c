/*
 * plotPixels.c --
 *
 * This file contains the procedures that generate pix files.  each file is
 * a sequence of unsigned chars, each triplet of chars representing a red,
 * green and blue value between 0 and 255 for a pixel.  Pixels go across left
 * to right, and down the image.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/plot/plotPixels.c,v 1.2 2008/06/01 18:37:45 tim Exp $";
#endif  /* not lint */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "dbwind/dbwtech.h"
#include "utils/malloc.h"
#include "plot/plotInt.h"
#include "windows/windows.h"
#include "commands/commands.h"
#include "textio/textio.h"
#include "utils/utils.h"
#include "utils/tech.h"
#include "utils/signals.h"
#include "utils/styles.h"
#include "graphics/graphics.h"	
#include "dbwind/dbwind.h"
    
#ifdef LLNL

/* forward declarations */

void PlotPixFatLine();

/*
 * ----------------------------------------------------------------------------
 * The parameters below control various aspects of the plotting
 * process.  The initial values are defaults for the pixels
 * printer.  However, many of them can be modified by users
 * with the "plot option" command.
 * ----------------------------------------------------------------------------
 */

int PlotPixWidth = 512;		/* Number of dots across image.
				 */
int PlotPixHeight = 512;	/* Height of swath to generate at one time,
				 * in dots.
				 */


/* Directory in which to create temporary file to hold raster: */

static char *defaultDirectory = "/usr/tmp";
extern char *PlotTempDirectory;

/*
 * ----------------------------------------------------------------------------
 * The variables below are shared between the top-level pixels
 * plotting procedure and the lower-level search functions.  They
 * contain specific information about how to generate the current
 * plot.  There area three coordinate systems of interest here:
 *
 * 1. Magic root coordinates.
 * 2. Raster coordinates:  based on printer pixels, one unit per pixel,
 *    where (0,0) corresponds to the lower-leftmost dot that will be
 *    printed.
 * 3. Swath coordinates:  the raster will be much too large to keep in
 *    memory all at once, so it's generated in a series of horizontal
 *    swaths.  The stippling routines all work in swath-relative
 *    coordinates, which are the same as raster coordinates except for
 *    a y-displacement (swathY below).
 * ----------------------------------------------------------------------------
 */

static PixRaster *pixRasterSwath = NULL; /* Raster used to hold current swath. */

extern Point plotLL;		/* Point in root Magic coords that corresponds
				 * to (0,0) in raster coordinates.
				 */
extern int swathY;		/* The y-coordinate in raster coordinates that
				 * corresponds to (0,0) in swath coords.  It's
				 * always >= 0.
				 */
int scale;		/* How many (2**scaleShift)-ths of a pixel
				 * correspond to one Magic unit.
				 */
extern int scaleShift;		/* The idea is that one Magic unit is equal
				 * to scale/(2**scaleShift) pixels.
				 */
static Rect rootClip;		/* Total root area of the plot.  Used for
				 * clipping.
				 */
static Rect swathClip;		/* Rectangle used for clipping to the area of
				 * the current swath.  This is in swath
				 * coordinates.
				 */
static int curStyle;		/* Current style being processed. */
static TileTypeBitMask *curMask; /* Mask of layers currently being stippled.
				 * This is the AND of the mask from curStyle
				 * and the layers that the user wants plotted.
				 */
static int crossSize;		/* Length of each arm of the crosses used
				 * to draw labels, in pixel units.
				 */
static RasterFont *labelPixFont;    /* Font to use when rendering labels. */
static RasterFont *cellNamePixFont; /* Font to use when rendering cell names. */
static RasterFont *cellIdPixFont;   /* Font to use when rendering cell ids. */

#endif /* LLNL */


/*
 * ----------------------------------------------------------------------------
 *	PlotPixTechInit --
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
PlotPixTechInit()
{
    /* most intersting stuff is done by PlotVersTechInit -- setting default
     * plot directory, default font names, etc.
     */
}

/*
 * ----------------------------------------------------------------------------
 *	PlotPixTechLine --
 *
 * 	This procedure is invoked by the technology module once for
 *	each line in the "pixels" subsection of the "plot" section
 *	of the technology file.
 *
 * Results:
 *	Always returns TRUE (otherwise the technology module would
 *	abort Magic with a fatal error).
 *
 * Side effects:
 *	Builds up the table of pixels styles.
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
bool
PlotPixTechLine(sectionName, argc, argv)
    char *sectionName;		/* Name of this section (unused). */
    int argc;			/* Number of arguments on line. */
    char *argv[];		/* Pointers to fields of line. */
{
    return TRUE;
}

#ifdef LLNL


/*
 * ----------------------------------------------------------------------------
 *
 * PlotPixPoint --
 *
 * 	Sets a particular pixel of a PixRaster.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If x and y lie inside the raster then the pixel that they select
 *	is set to 1.  If x or y is outside the raster area then nothing
 *	happens.
 *
 * ----------------------------------------------------------------------------
 */

void
PlotPixPoint(raster, x, y, style)
    PixRaster *raster;	/* Raster containing pixel to be
				 * filled.
				 */
    int x, y;			/* Coordinates of pixel. */
    int style;			/* style to render the pixel with. */
{
    char *rp;			/* ptr to pixel to change. */
    if ((x < 0) || (x >= raster->pix_width)) return;
    y = (raster->pix_height - 1) - y;
    if ((y < 0) || (y >= raster->pix_height)) return;

    rp = raster->pix_pixels + x + y*raster->pix_width;
    *rp = (*rp & ~(GrStyleTable[style].mask)) | 
	   (GrStyleTable[style].color & GrStyleTable[style].mask);

}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotPixArbLine --
 *
 * 	Draws a one-pixel-wide line between two points.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A line is drawn between pixels src and dst.  Only the portion
 *	of the line that lies inside the raster is drawn;  the endpoints
 *	may lie outside the raster (this feature is necessary to draw
 *	straight lines that cross multiple swaths).
 *
 * ----------------------------------------------------------------------------
 */

void
PlotPixArbLine(raster, src, dst, style)
    Raster *raster;		/* Where to render the line. */
    Point *src;			/* One endpoint of line, in raster coords. */
    Point *dst;			/* The other endpoint, in raster coords. */
    int style;			/* Display style to render this line in */
{
    int x, y, dx, dy, xinc, incr1, incr2, d, done;

    /* Compute the total x- and y-motions, and arrange for the line to be
     * drawn in increasing order of y-coordinate.
     */

    dx = dst->p_x - src->p_x;
    dy = dst->p_y - src->p_y;
    if (dy < 0)
    {
	dy = -dy;
	dx = -dx;
	x = dst->p_x;
	y = dst->p_y;
	dst = src;
    }
    else
    {
	x = src->p_x;
	y = src->p_y;
    }

    /* The code below is just the Bresenham algorithm from Foley and
     * Van Dam (pp. 435), modified slightly so that it can work in
     * all directions.
     */

    if (dx < 0)
    {
	xinc = -1;
	dx = -dx;
    }
    else xinc = 1;
    if (dx >= dy)
    {
	d = 2*dy - dx;
	incr1 = 2*dy;
	incr2 = 2*(dy - dx);
	done = dst->p_x;
	for ( ; x != done ; x += xinc)
	{
	    PlotPixPoint(raster, x, y, style);
	    if (d < 0)
		d += incr1;
	    else
	    {
		d += incr2;
		y += 1;
	    }
	}
    }
    else
    {
	d = 2*dx - dy;
	incr1 = 2*dx;
	incr2 = 2*(dx - dy);
	done = dst->p_y;
	for ( ; y != done ; y += 1)
	{
	    PlotPixPoint(raster, x, y, style);
	    if (d < 0)
		d += incr1;
	    else
	    {
		d += incr2;
		x += xinc;
	    }
	}
    }
    PlotPixPoint(raster, x, y, style);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plotPixLine  --
 *
 * 	This procedure plots a line of a given thickness.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The current raster is modified.
 *
 * ----------------------------------------------------------------------------
 */

void
plotPixLine(area, widen, style)
    Rect *area;			/* The "corner" points of this rectangle
				 * give the endpoints of the line, in
				 * Magic root coordinates.
				 */
    int widen;			/* Amount by which to widen line.  0 means
				 * line is drawn one pixel wide, 1 means 3
				 * pixels wide, etc.
				 */
    int style;			/* style in which to render the line */
{
    Rect swathArea;

    plotTransToSwath(area, &swathArea);

    /* Handle Manhattan lines using rectangle-drawing, since it's faster. */

    if ((swathArea.r_xbot == swathArea.r_xtop) ||
	    (swathArea.r_ybot == swathArea.r_ytop))
    {
	GEO_EXPAND(&swathArea, widen, &swathArea);
	GEOCLIP(&swathArea, &swathClip);
	if ((swathArea.r_xbot <= swathArea.r_xtop) &&
		(swathArea.r_ybot <= swathArea.r_ytop))
	    plotFillPixRaster(pixRasterSwath, &swathArea,style, GR_STSOLID);
    }
    else
      PlotPixFatLine(pixRasterSwath, 
		     &swathArea.r_ll, &swathArea.r_ur, 
		     widen, curStyle);
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotPixFatLine --
 *
 * 	Draws a line many pixels wide between two points.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A line is drawn between pixels src and dst.  Only the portion
 *	of the line that lies inside the raster is drawn;  the endpoints
 *	may lie outside the raster (this feature is necessary to draw
 *	straight lines that cross multiple swaths).  The line is drawn
 *	several pixels wide, as determined by the "widen" parameter.
 *	The ends of the line are square, not rounded, which may cause
 *	upleasant effects for some uses.  If the line is Manhattan,
 *	this procedure is very inefficient:  it's better to use the
 *	PlotFillPixRaster procedure.
 *
 * ----------------------------------------------------------------------------
 */

void
PlotPixFatLine(raster, src, dst, widen, style)
    PixRaster *raster;		/* Where to render the line. */
    Point *src;			/* One endpoint of line, in raster coords. */
    Point *dst;			/* The other endpoint, in raster coords. */
    int widen;			/* How much to widen the line.  0 means the
				 * line is one pixel wide, 1 means it's 3
				 * pixels wide, and so on.
				 */
    int style;
{
    double dx, dy, x, y;
    int nLines;

    /* Just draw (2*widen) + 1 lines spaced about one pixel apart.
     * The first lines here compute how far apart to space the lines.
     */

    nLines = (2*widen) + 1;
    x = dst->p_x - src->p_x;
    y = dst->p_y - src->p_y;
    dy = sqrt(x*x + y*y);
    dx = y/dy;
    dy = -x/dy;
    x = -dy*(widen);
    y = dx*(widen);

    for (x = -dx*widen,  y = -dy*widen;
	 nLines > 0;
	 nLines -= 1, x += dx, y += dy)
    {
	Point newSrc, newDst;

	if (x > 0)
	    newSrc.p_x = (x + .5);
	else newSrc.p_x = (x - .5);
	if (y > 0)
	    newSrc.p_y = (y + .5);
	else newSrc.p_y = (y - .5);
	newDst.p_x = dst->p_x + newSrc.p_x;
	newDst.p_y = dst->p_y + newSrc.p_y;
	newSrc.p_x += src->p_x;
	newSrc.p_y += src->p_y;

	PlotPixArbLine(raster, &newSrc, &newDst, style);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * plotPixRect --
 *
 * 	This procedure takes a rectangular area, given in Magic root
 *	coordinates, and draws it as an outline of a given thickness.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies raster.
 *
 * ----------------------------------------------------------------------------
 */

void
plotPixRect(area, widen, style)
    Rect *area;	/* Rectangular area to draw, in root
				 * coordinates.
				 */
    int widen;			/* If zero, rectangular outline is drawn
				 * one pixel wide.  If non-zero, the outline
				 * is widened by this many units on each
				 * side.
				 */
    int style;			/* style in which to render the rect */
{
    Rect side;

    /* First, the bottom side. */

    if (area->r_xbot != area->r_xtop)
    {
	side = *area;
	side.r_ytop = side.r_ybot;
	plotPixLine(&side, widen,style);

	/* Now the top side, if it doesn't coincide with the bottom. */

	if (area->r_ybot != area->r_ytop)
	{
	    side = *area;
	    side.r_ybot = side.r_ytop;
	    plotPixLine(&side, widen, style);
	}
    }

    /* Now do the left side. */

    if (area->r_ybot != area->r_ytop)
    {
	side = *area;
	side.r_xtop = side.r_xbot;
	plotPixLine(&side, widen, style);

	/* Now the right side, if it doesn't coincide with the left. */

	if (area->r_xbot != area->r_xtop)
	{
	    side = *area;
	    side.r_xbot = side.r_xtop;
	    plotPixLine(&side, widen, style);
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * plotPixTile --
 *
 *	This procedure is called for paint tiles.  It renders each tile
 *	in the current style, in the current swath.
 *
 * Results:
 *	Always returns 0 to keep the search alive.
 *
 * Side effects:
 *	Modifies the raster area.
 *
 * ----------------------------------------------------------------------------
 */

int
plotPixTile(tile, cxp)
    Tile *tile;	/* Tile that's of type to be output. */
    TreeContext *cxp;		/* Describes search in progress. */
{
    Rect tileArea, rootArea, swathArea;
    Transform *trans = &cxp->tc_scx->scx_trans;

    /* Transform tile coords to root coords and then to swath coords. */

    TITORECT(tile, &tileArea);
#ifdef DEBUG
    TxPrintf("PlotPixTile: (%d,%d)(%d,%d) ",
	   tileArea.r_xbot,
	   tileArea.r_ybot,
	   tileArea.r_xtop,
	   tileArea.r_ytop);
#endif
    GEOTRANSRECT(trans, &tileArea, &rootArea);
    plotTransToSwath(&rootArea, &swathArea);

    /* Clip and then fill with the color. */

    GEOCLIP(&swathArea, &swathClip);
    if (swathArea.r_xbot > swathArea.r_xtop) return 0;
    if (swathArea.r_ybot > swathArea.r_ytop) return 0;
#ifdef DEBUG
    TxPrintf(">> (%d,%d)(%d,%d)\n",
	   swathArea.r_xbot,
	   swathArea.r_ybot,
	   swathArea.r_xtop,
	   swathArea.r_ytop);
#endif

    /* if the current style's fill-mode is GR_STOUTLINE or GR_STCROSS
     * we cannot let plotFillPixRaster render the tile, since we get
     * extra horizontal lines at swath boundaries.  Too bad, it was a 
     * neat idea, but we cannot force the user to render the entire 
     * image at once (*sigh*)
     */

    if((GrStyleTable[curStyle].fill == GR_STOUTLINE) ||
       (GrStyleTable[curStyle].fill == GR_STCROSS))    
    {				/* draw the outline */
	plotPixRect(&tileArea, 1, curStyle);
    }
    if (GrStyleTable[curStyle].fill == GR_STCROSS)
    {				/* draw the cross */
	Point src, dst;

	src = swathArea.r_ll;
	dst = swathArea.r_ur;
	PlotPixFatLine(pixRasterSwath, &src, &dst, 1, curStyle);
	src.p_y = swathArea.r_ytop;
	dst.p_y = swathArea.r_ybot;
	PlotPixFatLine(pixRasterSwath, &src, &dst, 1, curStyle);
    }
    plotFillPixRaster(pixRasterSwath, &swathArea, curStyle, -1);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * plotPixLabel --
 *
 * 	This procedure is invoked for labels.  It generates bits to
 *	display the label in the current raster swath.
 *
 * Results:
 *	Always returns 0 to keep the search from aborting.
 *
 * Side effects:
 *	Modifies the raster.
 *
 * ----------------------------------------------------------------------------
 */

int
plotPixLabel(scx, label)
    SearchContext *scx;		/* Describes state of search when label
				 * was found.
				 */
    Label *label;		/* Label that was found. */
{
    Rect rootArea, swathArea, labelSize;
    Point point;
    int pos;

    /* Translate the label's area and relative position to root
     * coordinates and then to swath coordinates.  Figure out
     * the point relative to which the label is to be positioned.
     */

    GeoTransRect(&scx->scx_trans, &label->lab_rect, &rootArea);
    plotTransToSwath(&rootArea, &swathArea);
    pos = GeoTransPos(&scx->scx_trans, label->lab_just);
    PlotTextSize(labelPixFont, label->lab_text, &labelSize);

    switch (pos)
    {
	case GEO_NORTH:
	case GEO_NORTHEAST:
	case GEO_NORTHWEST:
	    point.p_y = swathArea.r_ytop + crossSize + 2 - labelSize.r_ybot;
	    break;

	case GEO_CENTER:
	case GEO_WEST:
	case GEO_EAST:
	    point.p_y = (swathArea.r_ytop + swathArea.r_ybot)/2;
	    point.p_y -= (labelSize.r_ytop + labelSize.r_ybot)/2;
	    break;

	case GEO_SOUTH:
	case GEO_SOUTHEAST:
	case GEO_SOUTHWEST:
	    point.p_y = swathArea.r_ybot - crossSize - 2 - labelSize.r_ytop;
	    break;
    }
    switch (pos)
    {
	case GEO_WEST:
	case GEO_NORTHWEST:
	case GEO_SOUTHWEST:
	    point.p_x = swathArea.r_xbot - crossSize - 2 - labelSize.r_xtop;
	    break;

	case GEO_CENTER:
	case GEO_NORTH:
	case GEO_SOUTH:
	    point.p_x = (swathArea.r_xtop + swathArea.r_xbot)/2;
	    point.p_x -= (labelSize.r_xtop + labelSize.r_xbot)/2;
	    break;

	case GEO_EAST:
	case GEO_NORTHEAST:
	case GEO_SOUTHEAST:
	    point.p_x = swathArea.r_xtop + crossSize + 2 - labelSize.r_xbot;
	    break;
    }

    /* Output lines marking the label's area.  Different things are
     * done depending on whether the label is a point, a line, or an
     * area.
     */

    if ((rootArea.r_xbot == rootArea.r_xtop) &&
	    (rootArea.r_ybot == rootArea.r_ytop))
    {
	Rect tmp;

	/* Point label.  Output a cross. */

	tmp = swathArea;
	tmp.r_ytop += crossSize;
	tmp.r_ybot -= crossSize;
	GEO_EXPAND(&tmp, 1, &tmp);
	GEOCLIP(&tmp, &swathClip);
	if ((tmp.r_xbot <= tmp.r_xtop) &&
		(tmp.r_ybot <= tmp.r_ytop))
	    plotFillPixRaster(pixRasterSwath, &tmp, STYLE_LABEL, GR_STSOLID);
	tmp = swathArea;
	tmp.r_xtop += crossSize;
	tmp.r_xbot -= crossSize;
	GEO_EXPAND(&tmp, 1, &tmp);
	GEOCLIP(&tmp, &swathClip);
	if ((tmp.r_xbot <= tmp.r_xtop) &&
		(tmp.r_ybot <= tmp.r_ytop))
	    plotFillPixRaster(pixRasterSwath, &tmp,STYLE_LABEL, GR_STSOLID);
    }
    else
    {
	/* Line or rectangle.  Draw outline.
	 * plotFillPixRaster will outline the area if STYLE_LABEL has
	 * fill mode GR_STOUTLINE.
	 *  Cute, berry, but it loses when the rect crosses a swath boundary.
	 */
	GEOCLIP(&swathArea, &swathClip);
	plotPixRect(&rootArea, 1, STYLE_LABEL);
/* 	plotFillPixRaster(pixRasterSwath, &swathArea, STYLE_LABEL, -1); */
    }

    /* Output the text for the label. */

    labelSize.r_xbot += point.p_x - 1;
    labelSize.r_xtop += point.p_x + 1;
    labelSize.r_ybot += point.p_y - 1;
    labelSize.r_ytop += point.p_y + 1;
    PlotPixRasterText(pixRasterSwath, &swathClip, 
		      labelPixFont, label->lab_text, &point, STYLE_LABEL);

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * plotPixCell --
 *
 * 	This procedure is invoked for unexpanded cells.
 *
 * Results:
 *	Always returns 0 to keep the search from aborting.
 *
 * Side effects:
 *	The raster is modified to depict the cell's boundary,
 *	name, and instance id.
 *
 * ----------------------------------------------------------------------------
 */

int
plotPixCell(scx)
    SearchContext *scx;		/* Describes cell whose bbox is to
				 * be plotted.
				 */
{
    char idName[100];
    Rect rootArea, swathArea, textSize;
    Point point;
    CellDef *def;

    /* Convert the cell's bounding box to root coordinates and then
     * draw the outline.
     */

    def = scx->scx_use->cu_def;
    GeoTransRect(&scx->scx_trans, &def->cd_bbox, &rootArea);
    plotPixRect(&rootArea, 1, STYLE_BLACK);

    if (!PlotShowCellNames)
	return (0);

    /* Output the cell's name and id text. */
    plotTransToSwath(&rootArea, &swathArea);
    PlotTextSize(cellNamePixFont, def->cd_name, &textSize);
    point.p_x = (swathArea.r_xtop + swathArea.r_xbot)/2;
    point.p_x -= (textSize.r_xtop + textSize.r_xbot)/2;
    point.p_y = (2*swathArea.r_ytop + swathArea.r_ybot)/3;
    point.p_y -= (textSize.r_ytop + textSize.r_ybot)/2;
    PlotPixRasterText(pixRasterSwath, &swathClip, 
		 cellNamePixFont, def->cd_name, &point, STYLE_BBOX);

    (void) DBPrintUseId(scx, idName, 100, TRUE);
    PlotTextSize(cellIdPixFont, idName, &textSize);
    point.p_x = (swathArea.r_xtop + swathArea.r_xbot)/2;
    point.p_x -= (textSize.r_xtop + textSize.r_xbot)/2;
    point.p_y = (swathArea.r_ytop + 2*swathArea.r_ybot)/3;
    point.p_y -= (textSize.r_ytop + textSize.r_ybot)/2;
    PlotPixRasterText(pixRasterSwath, &swathClip, 
		      cellIdPixFont, idName, &point, STYLE_BBOX);

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * Plotpixels --
 *
 * 	This procedure generates a pix file usable by various programs
 * 	that produce images on Dicomed cameras and so on.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Lots and lots of disk space is chewed up by the file.
 *
 * ----------------------------------------------------------------------------
 */

void
PlotPixels(scx, layers, xMask, width)
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
    int width;				/* How many pixels across the plot
					 * should be.
					 */
{
    static char *yesNo[] = {"no", "yes", NULL};
    int dotsAcross, dotsDown, swathsDown, scaleDown;
    char fileName[200], answer[32];
    float mBytes;
    Transform tinv;
    int action, result;
    FILE *file;

    /*
     * check to make sure that the swath height is a multiple of 8 so that stipples
     * will look correct across swath boundaries.
     */

    if (PlotPixHeight & 007) {
	TxPrintf("Warning: plot parameter \"pixheight\" is not a multiple of 8.  It\n");
	TxPrintf("will be rounded from %d to %d.\n", PlotPixHeight, (PlotPixHeight&(~007))+8);
	PlotPixHeight = (PlotPixHeight&(~007))+8;
    }
    /* did the user specify an explicit width? */
    if (width != 0)
      PlotPixWidth = width;

    GeoTransRect(&scx->scx_trans, &scx->scx_area, &rootClip);
    GEO_EXPAND(&rootClip, 1, &rootClip);
    dotsAcross = width;
    if (dotsAcross <= 0)
      dotsAcross = PlotPixWidth;

    /*
     * Compute the number of pixels per magic unit.
     * This number will be the fraction: scale / (1 << scaleShift).
     * In order to be reasonably sure that we have enough precision
     * in the numerator of the fraction, require that scale have at
     * least three bits (i.e., be greater than or equal to 4).
     */
    for (scale = 0, scaleShift = 4; ; scaleShift++)
    {
	scaleDown = 1 << scaleShift;
	scale = (scaleDown * dotsAcross) / (rootClip.r_xtop - rootClip.r_xbot);
	if (scaleShift >= 8 * sizeof (int))
	{
	    TxError("The area selected is too many lambda wide to plot.\n");
	    TxError("(There are numerical problems with rasterizing it).\n");
	    TxError("Try selecting a smaller area, or else asking for ");
	    TxError("a wider plot.\n");
	    return;
	}
	if (scale >= 4)
	  break;
    }

    /*
     * Compute scaling information, and tell the user how big the
     * plot will be.
     */
    dotsDown = ((rootClip.r_ytop - rootClip.r_ybot)*scale) >> scaleShift;
    swathsDown = (dotsDown + PlotPixHeight - 1)/PlotPixHeight;
    dotsDown = swathsDown * PlotPixHeight;
    mBytes = (double)(PlotPixWidth*dotsDown)*3.0/1000000.0;
    TxPrintf("Plot will take %.2f Megabytes in \"%s\".\n",
	     mBytes, PlotTempDirectory);
    do
    {
	TxPrintf("Do you still want the plot? [yes] ");
	if (TxGetLine(answer, sizeof answer) == NULL || answer[0] == '\0')
	{
	    action = 1;
	    break;
	}
    } while ((action = Lookup(answer, yesNo)) < 0);
    if (action == 0) return;

    /* The plot has been "approved".  Now obtain swath rasters if
     * we don't already have them.  If the swath size has changed,
     * recycle the rasters for new ones.
     */

    if (pixRasterSwath == NULL)
      pixRasterSwath = PlotNewPixRaster(PlotPixHeight, PlotPixWidth);
    else
    {
	if ((pixRasterSwath->pix_width != PlotPixWidth)
	    || (pixRasterSwath->pix_height != PlotPixHeight))
	{
	    PlotFreePixRaster(pixRasterSwath);
	    pixRasterSwath = PlotNewPixRaster(PlotPixHeight, PlotPixWidth);
	}
    }

    /* Load font information for the plot, if it isn't already
     * loaded.  We use the "versatec" fonts for convenience.
     */
    labelPixFont = PlotLoadFont(PlotVersLabelFont);
    cellNamePixFont = PlotLoadFont(PlotVersNameFont);
    cellIdPixFont = PlotLoadFont(PlotVersIdFont);
    if ((labelPixFont == NULL) ||
	(cellNamePixFont == NULL) ||
	(cellIdPixFont == NULL))
    {
	TxPrintf("PlotPixels: can't load fonts\n");
	return;
    }

    /* Compute the name of the file to use for output, and open it. */

    sprintf(fileName, "%s/magicPlot-%d-%d-XXXXXX", PlotTempDirectory,
	    PlotPixWidth, PlotPixHeight*swathsDown);
    result = mkstemp(fileName);
    if (result == -1)
    {
	TxError("Couldn't create temporary filename for %s\n", fileName);
	return;
    }
    file = PaOpen(fileName, "w", (char *) NULL, (char *) NULL, (char *) NULL,
		  (char **) NULL);
    if (file == NULL)
    {
	TxError("Couldn't open file \"%s\" to write plot.\n", fileName);
	return;
    }

    /* Set up the rest of the transformation variables.
     * Arrange for the plot to be centered on the page.
     */

    plotLL.p_y = rootClip.r_ybot;
    plotLL.p_x = (rootClip.r_xtop+rootClip.r_xbot)/2 - (PlotPixWidth*8)/scale;
    if (plotLL.p_x > rootClip.r_xbot)
      plotLL.p_x = rootClip.r_xbot;

    /* Compute a distance equal to 1/8th the size of a typical wire
     * (max of thicknesses of routing layers).  This is used to offset
     * text from labels and to compute cross size for point labels.
     */

    if (RtrMetalWidth > RtrPolyWidth)
      crossSize = (RtrMetalWidth*scale)/(scaleDown*8);
    else crossSize = (RtrPolyWidth*scale)/(scaleDown*8);
    if (crossSize < 2) crossSize = 2;

    /* Step down the page one swath at a time, rasterizing everything
     * that overlaps the current swath, then outputting the swath.
     */

    GeoInvertTrans(&scx->scx_trans, &tinv);
    for (swathsDown -= 1; swathsDown >= 0; swathsDown -= 1)
    {
	SearchContext scx2;
	Rect root, labelArea;
	int labelHeight;

	swathY = swathsDown * PlotPixHeight;
	PlotClearPixRaster(pixRasterSwath, (Rect *) NULL);
	
	/* Compute the area of the swath that overlaps the portion of
	 * the layout we're plotting.
	 */

	plotTransToSwath(&rootClip, &swathClip);
	if (swathClip.r_xbot < 0) swathClip.r_xbot = 0;
	if (swathClip.r_ybot < 0) swathClip.r_ybot = 0;
	if (swathClip.r_xtop >= PlotPixWidth)
	  swathClip.r_xtop = PlotPixWidth - 1;
	if (swathClip.r_ytop >= PlotPixHeight)
	  swathClip.r_ytop = PlotPixHeight - 1;

	/* Compute the area of layout that overlaps this swath.  This is
	 * done twice, once for mask material and once for labels.  The
	 * separate computation for labels is because labels stick out
	 * from their positioning points.  We may have to search a larger
	 * area than just the swath in order to find all the labels that
	 * must be drawn in this swath.  Only the y-direction needs to
	 * be expanded this way, since we're only swathing in y.  Even
	 * non-label stuff has to be expanded slightly, because lines
	 * are drawn more than 1 pixel thick.
	 */

	scx2 = *scx;
	root.r_xbot = (scaleDown*swathClip.r_xbot)/scale + plotLL.p_x;
	root.r_xtop = (scaleDown*swathClip.r_xtop)/scale + plotLL.p_x;
	root.r_ybot = (scaleDown*(swathY-4))/scale + plotLL.p_y;
	root.r_ytop = (scaleDown*(swathY+swathClip.r_ytop+4))/scale+plotLL.p_y;
	GEO_EXPAND(&root, 1, &root);
	GeoTransRect(&tinv, &root, &scx2.scx_area);

	labelArea.r_xbot = root.r_xbot;
	labelArea.r_xtop = root.r_xtop;
	labelHeight =
	  (labelPixFont->fo_bbox.r_ytop - labelPixFont->fo_bbox.r_ybot) + 2;
	labelArea.r_ybot =
	  (scaleDown*(swathY-crossSize-labelHeight))/scale
	    + plotLL.p_y;
	labelArea.r_ytop =
	  (scaleDown*(swathY+swathClip.r_ytop+crossSize+labelHeight))/scale
	    + plotLL.p_y;
	GEO_EXPAND(&labelArea, 1, &labelArea);

	/* For each style, output areas for all the tiles
	 * requested by the style.
	 */

	for (curStyle = 0; curStyle < DBWNumStyles; curStyle++)
	{
	    /*  visit all the tiles in this swath */
	    curMask = DBWStyleToTypes(curStyle);
	    (void) DBTreeSrTiles(&scx2, curMask, xMask, plotPixTile,
				 (ClientData) NULL);
	}

	/* Output labels, if they are wanted. */

	if (TTMaskHasType(layers, L_LABEL))
	{
	    curMask = layers;
	    TTMaskSetType(curMask, TT_SPACE);
	    GeoTransRect(&tinv, &labelArea, &scx2.scx_area);
	    (void)DBTreeSrLabels(&scx2, curMask, xMask, (TerminalPath *) NULL,
				 TF_LABEL_ATTACH, plotPixLabel, (ClientData) NULL);
	}

	/* Output subcell bounding boxes, if they are wanted. */

	if (TTMaskHasType(layers, L_CELL))
	{
	    (void) DBTreeSrCells(&scx2, xMask, plotPixCell, (ClientData) NULL);
	}
	TxPrintf("#");
	TxFlush();

	if (PlotDumpPixRaster(pixRasterSwath,file)!= 0)
	  goto error;
	if (SigInterruptPending)
	  goto error;

    }

    /* Close the file and tell the user where it is */

    fclose(file);
    TxPrintf("\nPlot complete.  pix file is \"%s\"\n", fileName);
    return;

  error:
    TxError("\npixel plot aborted.\n");
    fclose(file);
    unlink(fileName);
}



/*
 * ----------------------------------------------------------------------------
 *
 * PlotNewPixRaster --
 *
 * 	Allocate and initialize a new raster structure.
 *
 * Results:
 *	The return value is a pointer to the new PixRaster object.
 *
 * Side effects:
 *	Memory is allocated.
 *
 * ----------------------------------------------------------------------------
 */

PixRaster *
PlotNewPixRaster(height, width)
    int height;			/* PixRaster's height in pixels.*/
    int width;			/* PixRaster's width in pixels.*/
{
    PixRaster *new;

    new = (PixRaster *) mallocMagic(sizeof(PixRaster));
    new->pix_width = width;
    new->pix_height = height;
    new->pix_pixels = (char *) mallocMagic((unsigned) (height * width));
    return new;
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotFreePixRaster --
 *
 * 	Frees up the memory associated with an existing PixRaster structure.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The storage associated with PixRaster is returned to the allocator.
 *
 * ----------------------------------------------------------------------------
 */

void
PlotFreePixRaster(pr)
PixRaster *pr;		/* PixRaster whose memory is to be freed.
				 * Should have been created with
				 * PlotNewPixRaster.
				 */
{
    freeMagic((char *) pr->pix_pixels);
    freeMagic((char *) pr);
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotClearPixRaster --
 *
 * 	This procedure clears out an area of the PixRaster.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The area of the PixRaster indicated by "area" is set to all zeroes.
 *
 * ----------------------------------------------------------------------------
 */

void
PlotClearPixRaster(pr, area)
PixRaster *pr;			/* PixRaster that's to be cleared. */
Rect *area;				/* Area to be cleared, in PixRaster
					 * coords.  NULL means clear the
					 * whole PixRaster.
					 */
{
    char *left, *right, *cur;
    int line;

    if (area == NULL)
    {
	bzero((char *) pr->pix_pixels,
		pr->pix_height*pr->pix_width);
	return;
    }

    /* Compute the address of the leftmost word in the topmost line
     * to be cleared, and the rightmost word in the topmost line to
     * be cleared.
     */

    left = pr->pix_pixels +
	((pr->pix_height-1) - area->r_ytop)* pr->pix_width;
    right = left + area->r_xtop;
    left += area->r_xbot;

    /* Clear the area one PixRaster line at a time, top to bottom. */

    for (line = area->r_ytop; line >= area->r_ybot; line -= 1)
    {
	/* Clear the line. */

	for (cur = left; cur <= right; cur += 1)
	  *cur = 0;
	left += pr->pix_width;
	right += pr->pix_width;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * plotFillPixRaster --
 *
 * 	Given a PixRaster and an area, this procedure renders the given area
 *	of the PixRaster with a particular stipple pattern or other 
 * 	appropriate style.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The current PixRaster is modified..
 *
 * ----------------------------------------------------------------------------
 */

void
plotFillPixRaster(pr, area, style, fill)
PixRaster *pr;		/* Pointer to PixRaster whose bits are
				 * to be filled in.
				 */
    Rect *area;	/* Area to be filled in pixel coords.
				 * This is an inclusive area:  it
				 * includes the boundary pixels.  The
				 * caller must ensure that it is
				 * clipped to the PixRaster area.
				 */
    int style;			/* index of style to be used */
    int fill;			/* if >0, override GrStyleTable fill mode */
{
    char *left, *cur;
    int line;
    char *right;
    int curStipple, curColor, curMask; /* local copies so we don't have to
					* continually indirect through 
					* GrStyleTable
					*/
    Rect r;			/* for passing to plotPixLine */

#ifdef DEBUG
    TxPrintf("plotFillPixRaster: raster buffer@0x%x : ", pr->pix_pixels);
    TxPrintf(" (%d,%d)(%d,%d)\n",
	   area->r_xbot,
	   area->r_ybot,
	   area->r_xtop,
	   area->r_ytop);
#endif

    /* Compute the address of the leftmost word in the topmost line
     * to be filled, and the rightmost word in the topmost line to
     * be filled.
     */

    /* find beginning of first swath line that is affected */

    left = pr->pix_pixels +
      ((pr->pix_height-1) - area->r_ytop)*pr->pix_width;
    right = left + area->r_xtop; /* right edge */
    left += area->r_xbot;	/* left edge */

    /* Process the area one PixRaster line at a time, top to bottom. */
    curStipple = GrStyleTable[style].stipple;
    curColor   = GrStyleTable[style].color;
    curMask    = GrStyleTable[style].mask;

    /*
     * now select the appropriate rendering style
     * and render the area.  If we were passed a non-negative fill argument,
     * let it override the fill mode from GrStyleTable.  Otherwise, use the 
     * mode from the table.  This is so we can make solid areas in STYLE_LABEL,
     * for example, instead of getting the arms of the crosses hollow.
     */

    if (fill < 0) 
      fill = GrStyleTable[style].fill;

    switch (fill)
    {
      case GR_STSTIPPLE:
#ifdef DEBUG
	TxPrintf ("Stipple: %d %d %d %d %d %d %d %d \n",
		  GrStippleTable[curStipple][0],
		  GrStippleTable[curStipple][1],
		  GrStippleTable[curStipple][2],
		  GrStippleTable[curStipple][3],
		  GrStippleTable[curStipple][4],
		  GrStippleTable[curStipple][5],
		  GrStippleTable[curStipple][6],
		  GrStippleTable[curStipple][7]);
#endif
	for (line = area->r_ytop; line >= area->r_ybot; line -= 1)
	{
	    for (cur = left; cur <= right; cur += 1)

	      /* select "line" of stipple pattern and AND it with the bitmask
	       * for the bit in the line
	       * x&07 == x % 8 and is faster 
	       */

	      if(GrStippleTable[curStipple][line&07] &
		 1<<(int)((7-(cur-pr->pix_pixels)%
			   pr->pix_width)&07))
		*cur = (*cur & ~curMask) | (curColor & curMask);

	    left  += pr->pix_width;
	    right += pr->pix_width;
	}
	break;
      case GR_STGRID:
	/* not implemented */
	break;
	/* cross and outline are handled at a higher level */
      case GR_STCROSS:
	/* can't do it here due to swath boundary problems. */
	break;
      case GR_STOUTLINE:
	break;

      case GR_STSOLID:
      default:
	for (line = area->r_ytop; line >= area->r_ybot; line -= 1)
	{
	    for (cur = left; cur <= right; cur += 1)
	      *cur = (*cur & ~curMask) | (curColor & curMask);
	    left  += pr->pix_width;
	    right += pr->pix_width;
	}
	break;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotDumpPixRaster  --
 *
 * 	Writes out the contents of the given PixRaster to the given file,
 *	in binary format.  Goes trhough color map table to get the values for
 * 	each pixel.
 *
 * Results:
 *	Returns 0 if all was well.  Returns non-zero if there was
 *	an I/O error.  In this event, this procedure prints an
 *	error message before returning.
 *
 * Side effects:
 *	Information is added to file.
 *
 * ----------------------------------------------------------------------------
 */

int
PlotDumpPixRaster(pr, file)
    PixRaster *pr;		/* PixRaster to be dumped. */
    FILE *file;			/* File stream on which to dump it. */
{
    int i;
    int r, g, b;

    for (i = 0; i < (pr->pix_width) * (pr->pix_height); i++)
    {
	GrGetColor(pr->pix_pixels[i], &r, &g, &b);
	if (putc(r, file) == EOF)	/* red */
	{
	    TxError("I/O error in writing PixRaster file:  %s.\n",
		    strerror(errno));
	    return 1;
	}
	if (putc(g, file) == EOF) /* green */
	{
	    TxError("I/O error in writing PixRaster file:  %s.\n",
		    strerror(errno));
	    return 1;
	}
	if (putc(b, file) == EOF) /* blue */
	{
	    TxError("I/O error in writing PixRaster file:  %s.\n",
		    strerror(errno));
	    return 1;
	}
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotPixRasterText --
 *
 * 	Given a text string and a font, this procedure scan-converts
 *	the string and writespixels in the current raster that correspond
 *	to on-bits in the text.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Bits are modified in the raster.
 *
 * ----------------------------------------------------------------------------
 */

void
PlotPixRasterText(raster, clip, font, string, point, style)
    PixRaster *raster;	/* Raster whose bits are to be filled in. */
    Rect *clip;			/* Area to which to clip the text.  Must be
				 * entirely within the area of the raster.
				 */
    RasterFont *font;		/* Font to use for rasterizing string.  Must
				 * have been obtained by calling PlotLoadFont.
				 */
    char *string;		/* String of text to rasterize. */
    Point *point;		/* X-Y coordinates of origin of text.  The
				 * origin need not be inside the area of
				 * the raster, but only raster points inside
				 * the area will be modified.
				 */
    int style;			/* style for the text to be rendered in -- 
				 * used only for the correct color.
				 */
{
    int xOrig;			/* X-origin for current character. */
    int color;
    
    color = GrStyleTable[style].color;
    /* Outer loop:  process each character. */

    xOrig = point->p_x;
    for (; *string != 0; string++)
    {
	int cBytesPerLine, i;
	struct dispatch *d;	/* Descriptor for current character. */

	/* Handle spaces and tabs specially by just spacing over. */

	if ((*string == ' ') || (*string == '\t'))
	{
	    xOrig += font->fo_chars['t'].width;
	    continue;
	}

	/* Middle loop:  render each character one raster line at a
	 * time, from top to bottom.  Skip rows that are outside the
	 * area of the raster.
	 */
	
	d = &font->fo_chars[*string];
	cBytesPerLine = (d->left + d->right + 7) >> 3;
	for (i = 0; i < d->up + d->down; i++)
	{
	    int y, j;
	    char *charBitPtr;

	    y = point->p_y + d->up - 1 - i;
	    if (y < clip->r_ybot) break;
	    if (y > clip->r_ytop) continue;

	    /* Inner loop: process a series of bytes in a row to
	     * render one raster line of one character.  Be sure
	     * to skip areas that fall outside the raster to the
	     * left or right.
	     */
	    
	    for (j = -d->left,
		     charBitPtr = font->fo_bits + d->addr + i*cBytesPerLine;
	         j < d->right;
		 j += 8, charBitPtr++)
	    {
		char *rPtr;
		int charBits, x, k;

		x = xOrig + j;
		if (x > clip->r_xtop) break;
		if (x < clip->r_xbot - 7) continue;

		rPtr = (char *) raster->pix_pixels;
		rPtr += (raster->pix_height - 1 - y)*raster->pix_width + x;
		charBits = *charBitPtr & 0xff;

		/* Inner inner loop: process the bytes worth of bits, setting
		 * pixels in the raster to the current color
		 */
		for(k=7;k>=0;k--)
		{
		    if(charBits&(1<<k))
		      *rPtr |= color;
		    rPtr++;
		}
	    }
	}

	xOrig += d->width;
    }
}

#endif /* LLNL */
