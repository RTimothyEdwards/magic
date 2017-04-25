/*
 * plotPNM.c --
 *
 * This file contains procedures that generate PNM format files
 * to describe a section of layout.
 *
 *     ********************************************************************* 
 *     * Copyright (C) 2000 Cornell University                             *
 *     * Permission to use, copy, modify, and distribute this              * 
 *     * software and its documentation for any purpose and without        * 
 *     * fee is hereby granted, provided that the above copyright          * 
 *     * notice appear in all copies.  Cornell University                  * 
 *     * makes no representations about the suitability of this            * 
 *     * software for any purpose.  It is provided "as is" without         * 
 *     * express or implied warranty.  Export of this software outside     * 
 *     * of the United States of America may require an export license.    * 
 *     *********************************************************************
 *
 * R. Timothy Edwards
 * Copyright (C) 2004
 * MultiGiG, Inc.
 * Scotts Valley, CA
 *
 * Cleaned up the code, including optimization for speed
 * Added:  Non-Manhattan geometry handling, 24-bit color,
 * plot styles automatically generated from display styles,
 * downsampling for large plots, extended syntax for the
 * technology file description.
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/plot/plotPNM.c,v 1.3 2010/06/24 12:37:25 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/tech.h"
#include "utils/malloc.h"
#include "utils/utils.h"
#include "utils/styles.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "dbwind/dbwtech.h"
#include "dbwind/dbwind.h"
#include "utils/main.h"
#include "commands/commands.h"
#include "textio/textio.h"
#include "utils/signals.h"
#include "plot/plotInt.h"

#define LANCZOS_KERNEL_SIZE 1024
#define PI 3.14159265

/* Structure for saving R, G, B components of colors	*/
/* from a non-default colormap.				*/

typedef struct _pnmcolor {
    unsigned char r, g, b;
} pnmcolor;   

pnmcolor *PNMcolors = NULL;
static int ncolors = 0;

#define PIXELSZ sizeof(pnmcolor)

int PlotPNMmaxmem = 64 * 1024;	/* 64MB */
int PlotPNMdownsample = 0;	/* No downsampling by default */
unsigned char PlotPNMBG = 0xff;	/* White background by default */

#ifdef VERSATEC
bool PlotPNMRTL = FALSE;	/* If true, filter output through HP driver */
#endif

/*
 * Local variables, modified/shared by callbacks.
 */

int 		Init_Error;
float 		lk[2 * LANCZOS_KERNEL_SIZE + 1];
int		*lkstep; /* lanczos kernel steps */
pnmcolor 	*rtile;
int		tile_xsize, tile_ysize;
int		ds_xsize, ds_ysize;
Rect		bb;
unsigned long	BBinit;
int		tile_yshift, tile_xshift;
int 		im_x, im_y;
int		im_yoffset;
int 		y_pixels;

/* Structure for saving styles that are different from	*/
/* the styles loaded for this technology.		*/

typedef struct _dstyle {
    char *name;
    int init;
    unsigned int wmask;
    pnmcolor color;
} dstyle;

dstyle *Dstyles = NULL;
static int ndstyles = 0;

/* Structure which records how to paint a tile type.	*/

typedef struct _pstyle {
    unsigned int wmask;
    pnmcolor color;
} pstyle;

pstyle *PaintStyles = NULL;

/* Forward declarations */

extern void PlotLoadStyles();
extern void PlotLoadColormap();
extern pnmcolor PNMColorBlend();
extern pnmcolor PNMColorIndexAndBlend();

/*
 * ----------------------------------------------------------------------------
 *
 * Function for output of PNM line data to HPRTL format
 *
 * ----------------------------------------------------------------------------
 */

#ifdef VERSATEC

struct plotRTLdata {
   FILE *outfile;
   unsigned char *outbytes;
};

int
pnmRTLLineFunc(linebuffer, arg)
    unsigned char *linebuffer;
    struct plotRTLdata *arg;
{
    int size;

    size = PlotRTLCompress(linebuffer, arg->outbytes, im_x * 3);
    fprintf(arg->outfile, "\033*b%dW", size);
    fwrite(arg->outbytes, size, 1, arg->outfile);
    return 0;
}

#endif

/*
 * ----------------------------------------------------------------------------
 *
 * Function for output of PNM line data to file fp
 *
 * ----------------------------------------------------------------------------
 */

int
pnmLineFunc(linebuffer, fp)
    unsigned char *linebuffer;
    FILE *fp;
{
    fwrite(linebuffer, im_x * 3, 1, fp);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * pnmRenderRegion --
 *
 *      Antialiased rendering.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes output to file.
 *
 * ----------------------------------------------------------------------------
 */

void
pnmRenderRegion(scale, scale_over_2, normal, temp, func, arg)
     float scale;
     int scale_over_2;
     float normal;		/* normalizing factor */
     float *temp;		/* passed so we don't have to allocate it
				 * on every call.
				 */
     int (*func)();		/* Function to call per line of output */
     ClientData arg;		/* Arguments to function */
{
    int i, j;
    int jmax;
    int x, y;
    int dx, dy;
    int ds_over_2;
    pnmcolor *color;
    float r, g, b;
    unsigned char *linebuffer, *lineptr;
  
    jmax = MIN(y_pixels, im_yoffset + 1);
    ds_over_2 = scale_over_2 >> PlotPNMdownsample;

    linebuffer = mallocMagic(im_x * 3);

    /* x, y : pixel coords */

    if (ds_over_2 == 0)
    {
	for (j = 0; j < jmax; j++)
	{
	    lineptr = linebuffer;
	    y = scale * (y_pixels - 1 - j);
	    y >>= PlotPNMdownsample;
	    for (i = 0; i < im_x; i++)
	    {
		x = scale * i;
	        x >>= PlotPNMdownsample;
		color = rtile + x + y * ds_xsize;
		*lineptr++ = color->r;
		*lineptr++ = color->g;
		*lineptr++ = color->b;
	    }
	    (*func)(linebuffer, arg);
	}
    }
    else
    {
	/* When the scale is small enough, we have to resort to antialiasing */

	float lkval;
	int tidx;

	for (j = 0; j < jmax; j++) {
	    y = scale_over_2 + scale * (y_pixels - 1 - j);
	    y >>= PlotPNMdownsample;
	    lineptr = linebuffer;
	    for (i = 0; i < im_x; i++) {
		x = scale_over_2 + scale * i;
	        x >>= PlotPNMdownsample;
		for (dx = -ds_over_2; dx < ds_over_2; dx++)
		{
		    r = 0.0; g = 0.0; b = 0.0;
		    for (dy = -ds_over_2; dy < ds_over_2; dy++)
		    {
			if (dy + y >= ds_ysize) continue;
			/* grab rgb for (x + dx, y + dy) */
			color = rtile + (x + dx) + (y + dy) * ds_xsize;

			lkval = lk[lkstep[dy + ds_over_2]];
			r += (float)color->r * lkval;
			g += (float)color->g * lkval;
			b += (float)color->b * lkval;
		    }
		    tidx = 3 * (dx + ds_over_2);
		    temp[tidx++] = r;
		    temp[tidx++] = g;
		    temp[tidx] = b;
		}
		r = 0.0; g = 0.0; b = 0.0;
		for (dx = 0; dx < 2 * ds_over_2; dx++)
		{
		    tidx = 3 * dx;
		    lkval = lk[lkstep[dx]];
		    r += temp[tidx++] * lkval;
		    g += temp[tidx++] * lkval;
		    b += temp[tidx] * lkval;
		}
		r /= normal;
		g /= normal;
		b /= normal;
		*lineptr++ = (unsigned char)r;
		*lineptr++ = (unsigned char)g;
		*lineptr++ = (unsigned char)b;
	    }
	    (*func)(linebuffer, arg);
	}
    }
    freeMagic(linebuffer);
}



/*
 * ----------------------------------------------------------------------------
 *
 * pnmBBOX --
 *
 *      Callback for DBTreeSrTiles; compute bounding box of plot
 *
 * Results:
 *	Always return 0 to keep search going.
 *
 * Side effects:
 *	Modifies BBinit, updates bounding box "bb"
 *
 * ----------------------------------------------------------------------------
 */

int
pnmBBOX (tile,cxp)
     Tile *tile;
     TreeContext *cxp;
{
    Rect targetRect, sourceRect;
    SearchContext *scx = cxp->tc_scx;
    Rect *arg;
    TileType type;

    if (!IsSplit(tile))
	if ((type = TiGetType(tile)) == TT_SPACE)
	    return 0;
  
    /* grab rectangle from tile */
    TITORECT(tile, &targetRect);
  
    /* coordinate transform */
    GEOTRANSRECT(&scx->scx_trans, &targetRect, &sourceRect);

    /* Clip */
    arg = (Rect *)cxp->tc_filter->tf_arg;
    GEOCLIP(&sourceRect, arg);

    /* compute bbox */
    if (!BBinit)
	bb = sourceRect;
    else
    {
	bb.r_xbot = MIN(bb.r_xbot, sourceRect.r_xbot);
	bb.r_ybot = MIN(bb.r_ybot, sourceRect.r_ybot);
	bb.r_xtop = MAX(bb.r_xtop, sourceRect.r_xtop);
	bb.r_ytop = MAX(bb.r_ytop, sourceRect.r_ytop);
    }

    BBinit = 1;
    return 0;
}


/*
 * ----------------------------------------------------------------------------
 *
 * pnmTile --
 *
 *      Callback for DBTreeSrTiles; paints tiles in the current rtile buffer.
 *
 * Results:
 *	Return 0 to keep search going unless an error condition was
 *	encountered.
 *
 * Side effects:
 *	Modifies rtile array.
 *
 * ----------------------------------------------------------------------------
 */

int
pnmTile (tile, cxp)
     Tile *tile;
     TreeContext *cxp;
{
    SearchContext *scx = cxp->tc_scx;
    Rect targetRect, sourceRect, *clipRect;
    int type, j, x, y, dx, dy;
    pnmcolor *t;
    pnmcolor col;

    if ((type = (int)TiGetTypeExact(tile)) == TT_SPACE)
	return 0;

    /* undefined type; paint nothing */
    if (!IsSplit(tile))
	if (PaintStyles[type].wmask == 0) return 0;
  
    /* grab rectangle from tile */
    TITORECT(tile, &targetRect);
  
    /* coordinate transform */
    GEOTRANSRECT(&scx->scx_trans, &targetRect, &sourceRect);

    /* Clip */
    clipRect = (Rect *)cxp->tc_filter->tf_arg;

    /* Handle non-Manhattan geometry */
    if (IsSplit(tile))
    {
	TileType dinfo;
	int w, h, llx, lly, urx, ury;
	Rect scaledClip;

        type = (SplitSide(tile)) ? SplitRightType(tile) : SplitLeftType(tile);
	if (type == TT_SPACE) return 0;
	else if (PaintStyles[type].wmask == 0) return 0;

	llx = sourceRect.r_xbot - tile_xshift;
	lly = sourceRect.r_ybot - tile_yshift;
	llx >>= PlotPNMdownsample;
	lly >>= PlotPNMdownsample;
	dx = sourceRect.r_xtop - sourceRect.r_xbot;
	dy = sourceRect.r_ytop - sourceRect.r_ybot;
	dx >>= PlotPNMdownsample;
	dy >>= PlotPNMdownsample;
	urx = llx + dx;
	ury = lly + dy;

	col = PaintStyles[type].color;
	scaledClip = *clipRect;
	scaledClip.r_xbot -= tile_xshift;
	scaledClip.r_xtop -= tile_xshift;
	scaledClip.r_ybot -= tile_yshift;
	scaledClip.r_ytop -= tile_yshift;
	scaledClip.r_xbot >>= PlotPNMdownsample;
	scaledClip.r_xtop >>= PlotPNMdownsample;
	scaledClip.r_ybot >>= PlotPNMdownsample;
	scaledClip.r_ytop >>= PlotPNMdownsample;

	/* The following structures could be much better	*/
	/* written for considerable speedup. . .		*/

	dinfo = DBTransformDiagonal(TiGetTypeExact(tile), &scx->scx_trans);
	if (((dinfo & TT_SIDE) >> 1) != (dinfo & TT_DIRECTION))
	{
	    /* work top to bottom */
	    for (y = ury - 1; y >= lly; y--)
	    {
		if (y >= scaledClip.r_ytop) continue;
		else if (y < scaledClip.r_ybot) break;
		if (dinfo & TT_SIDE)	/* work right to left */
		{
		    for (x = urx - 1; x >= llx; x--)
		    {
			if (x >= scaledClip.r_xtop) continue;
			else if (x < scaledClip.r_xbot) break;
			if (((urx - x) * dy) > ((ury - y) * dx)) break;
			t = rtile + x + ds_xsize * y;
			*t = PNMColorBlend(t, &col);
		    }
		}
		else	/* work left to right */
		{
		    for (x = llx; x < urx; x++)
		    {
			if (x < scaledClip.r_xbot) continue;
			else if (x >= scaledClip.r_xtop) break;
			if (((x - llx) * dy)  > ((ury - y) * dx)) break;
			t = rtile + x + ds_xsize * y;
			*t = PNMColorBlend(t, &col);
		    }
		}
	    }
	}
	else	/* work bottom to top */
	{
	    for (y = lly; y < ury; y++)
	    {
		if (y < scaledClip.r_ybot) continue;
		else if (y >= scaledClip.r_ytop) break;
		if (dinfo & TT_SIDE)	/* work right to left */
		{
		    for (x = urx; x >= llx; x--)
		    {
			if (x >= scaledClip.r_xtop) continue;
			else if (x < scaledClip.r_xbot) break;
			if (((urx - x) * dy) > ((y - lly) * dx)) break;
			t = rtile + x + ds_xsize * y;
			*t = PNMColorBlend(t, &col);
		    }
		}
		else	/* work left to right */
		{
		    for (x = llx; x < urx; x++)
		    {
			if (x < scaledClip.r_xbot) continue;
			else if (x >= scaledClip.r_xtop) break;
			if (((x - llx) * dy) > ((y - lly) * dx)) break;
			t = rtile + x + ds_xsize * y;
			*t = PNMColorBlend(t, &col);
		    }
		}
	    }
	}
	return 0;
    }

    GEOCLIP(&sourceRect, clipRect);

    /* paint rectangle */

    x = sourceRect.r_xbot - tile_xshift;
    y = sourceRect.r_ybot - tile_yshift;

    /* stop the search on an error condition */
    /* (this should not happen---is guaranteed by GEOCLIP */
    if ((x < 0) || (y < 0) || (x >= tile_xsize) || (y >= tile_ysize))
	return 1;

    x >>= PlotPNMdownsample;
    y >>= PlotPNMdownsample;

    dx = sourceRect.r_xtop - sourceRect.r_xbot;
    dy = sourceRect.r_ytop - sourceRect.r_ybot;
    dx >>= PlotPNMdownsample;
    dy >>= PlotPNMdownsample;

    col = PaintStyles[type].color;
    t = rtile + x + ds_xsize * y;

    for (dy; dy > 0; dy--)
    {
	for (j = 0; j < dx; j++)
	{
	    *t = PNMColorBlend(t, &col);
	    t++;
	}
	t = t - dx + ds_xsize;
    }

    /* Continue search function */
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotPNM --
 *
 * 	This procedure generates a PNM file to describe an area of
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
PlotPNM(fileName, scx, layers, xMask, width)
    char *fileName;			/* Name of PNM file to write. */
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
    int  width;		        	/* Indicates the width of the
					 * plot, in pixels.
					 */
{
    FILE *fp;
    Rect bbox;
    int bb_ysize, bb_xsize;
    int i, x, y, tile_ydelta;
    int save_ds, iter;
    int scale_over_2, ds_over_2;
    float *strip;
    float scale, invscale, scaledown, normal;

#ifdef VERSATEC
    struct plotRTLdata rtl_args;
    char command[200], tempFile[200];
#endif

    // Sanity check on PaintStyles---may be NULL if plot section
    // was missing from techfile.  If so, run default init/final
    // procedures, flag a warning, and continue.

    if (PaintStyles == NULL)
    {
	TxError ("Warning:  No plot section in techfile, using defaults.\n");
	PlotPNMTechInit();
	PlotPNMTechFinal();
    }

    if (width <= 0)
    {
	TxError ("PNM module given negative pixel width; cannot plot\n");
	return;
    }

    if (Init_Error)
    {
	TxError ("PNM module initialization had failed; cannot plot\n");
	return;
    }

    /* image:
     *    -----     
     *   | xxx |
     *   | xxx |
     *   | xxx |
     *    -----
     *
     * Use -scale/2 to scale/2 magic coordinates for each output pixel.
     *
     */

    /* Rendering Tile:
     *
     *    0.. bbox size + 2 * scale_over_2.
     *   
     * To sample, pixel (i,j) will be at:
     *     (scale_over_2 + scale*i, scale_over_2 + scale*j)
     * 
     * Given an initial pixel position at (i,j), we sample from
     *     -scale_over_2 to scale_over_2
     */

    /* Compute bounding box size in lambda */
    BBinit = 0;
    DBTreeSrTiles(scx, layers, xMask, pnmBBOX, (ClientData)&scx->scx_area);

    /* Initial bounding box size */
    bb_ysize = bb.r_ytop - bb.r_ybot;
    bb_xsize = bb.r_xtop - bb.r_xbot;

    /* Determine value of "scale" from the total pixel width. */
    scale = (float)bb_xsize / (float)width;
    invscale = 1.0 / scale;

    scale = 1.0 / invscale;
    if ((scale > 2) || (invscale != ceil(invscale)))
	scale_over_2 = (int) ceil(scale / 2.0);
    else
	scale_over_2 = 0;

    /* bump search context by scale_over_2 pixels on each side */
    scx->scx_area.r_xbot = bb.r_xbot - scale_over_2;
    scx->scx_area.r_ybot = bb.r_ybot - scale_over_2;
    scx->scx_area.r_xtop = bb.r_xtop + scale_over_2;
    scx->scx_area.r_ytop = bb.r_ytop + scale_over_2;

    /* Recalculate bounding box with extended boundary */
    bb_ysize = bb.r_ytop - bb.r_ybot;
    bb_xsize = bb.r_xtop - bb.r_xbot;

    tile_xsize = bb_xsize + 2 * scale_over_2;

    /* check for empty region */
    if (BBinit == 0 || tile_xsize <= 0 || bb_ysize <= 0)
    {
	TxPrintf ("Empty region, no plot\n");
	return;
    }

    /* 
     * Compute memory requirements; a single pixel line needs a tile
     * that has size "xsize" by "scale." To keep inter-tile overlap low,
     * we insist that a single tile must have at least 3*scale pixels in
     * it.
     */

    save_ds = PlotPNMdownsample;
    while ((PlotPNMmaxmem * 1024) <
		((3 * scale + 2 * scale_over_2) * PIXELSZ * tile_xsize)
		/ (1 << (PlotPNMdownsample * 2)))
	PlotPNMdownsample++;

    if (PlotPNMdownsample != save_ds)
    {
	TxPrintf ("%dX downsampling forced by memory size requirements.\n",
		PlotPNMdownsample);
	TxPrintf ("Current: %d KB; Required for non-downsampled image: %d KB\n", 
		PlotPNMmaxmem, (int) (1023 + ((3 * scale + 2 * scale_over_2)
		* PIXELSZ * tile_xsize) / 1024) / (1 << (save_ds * 2)));
	TxPrintf ("Use \"plot parameter pnmmaxmem\" to increase allocation.\n");
    }

    /* 
     * Compute the maximum y size for a tile.
     */

    tile_ysize = PlotPNMmaxmem * 1024 / (PIXELSZ * tile_xsize);
    tile_ydelta = (tile_ysize - scale_over_2 * 2);

    /* Determine the amount shifted in Y for each consecutively */
    /* computed region.						*/
    /* tile_ydelta is the amount of shift in magic units.	*/
    /* y_pixels is the amount of shift in PNM pixels.		*/
    /* tile_ydelta MUST EQUAL y_pixels * scale.  If not, then	*/
    /* we need to back-compute a better tile_ysize value.  	*/

    y_pixels = tile_ydelta / scale;
    if (y_pixels == 0) y_pixels = 1;
    if (y_pixels * scale != tile_ydelta)
    {
	tile_ydelta = scale * y_pixels;
	tile_ysize = tile_ydelta + (scale_over_2 * 2);
    }

    /* If there's enough memory allocation, tile_ysize bounds the whole plot */
    if (tile_ysize > (bb_ysize + 2 * scale_over_2))
    {
	tile_ysize = bb_ysize + 2 * scale_over_2;
	tile_ydelta = bb_ysize;
	y_pixels = tile_ydelta / scale;
    }

    ds_xsize = tile_xsize >> PlotPNMdownsample;
    ds_ysize = tile_ysize >> PlotPNMdownsample;
    ds_over_2 = scale_over_2 >> PlotPNMdownsample;

    rtile = (pnmcolor *) mallocMagic((ds_xsize * ds_ysize) * PIXELSZ);

    /* bump search context by scale_over_2 pixels on each side */
    scx->scx_area.r_ybot = scx->scx_area.r_ytop - tile_ysize;
    tile_yshift = scx->scx_area.r_ybot;
    tile_xshift = scx->scx_area.r_xbot;

    im_x = (int)(bb_xsize / scale);
    im_y = (int)(bb_ysize / scale);

#ifdef VERSATEC
    if (PlotPNMRTL)
    {
	if (fileName == NULL)
	{
	    int result;

	    sprintf(tempFile, "%s/magicPlotXXXXXX", PlotTempDirectory);
	    result = mkstemp(tempFile);
	    if (result == -1)
	    {
		TxError("Failed to create temporary filename for %s\n", tempFile);
		return;
	    }
	    fileName = tempFile;
	}
	rtl_args.outfile = PaOpen(fileName, "w", (char *)NULL, ".",
		(char *)NULL, (char **)NULL);
	if (rtl_args.outfile == NULL)
	{
	    TxError("Couldn't open file \"%s\" to write plot.\n", fileName);
	    return;
	}

	switch (PlotVersPlotType)
	{
	    case HPGL2:		/* Write HPGL2 header */
		/* Universal Command Language. */
		fprintf(rtl_args.outfile, "\033%%-12345X");
		/* Reset printer; set HPGL2 mode. */
		fprintf(rtl_args.outfile, "@PJL ENTER LANGUAGE=HPGL2\r\n");
		fprintf(rtl_args.outfile, "\033E\033%%0B");
		/* Declare name; disable auto-rotate */
		fprintf(rtl_args.outfile, "BP1,\"MAGIC\",5,1;");
		/* Enter RTL mode. */
		fprintf(rtl_args.outfile, "\033%%0A");
		/* Source mode opaque */
		fprintf(rtl_args.outfile, "\033*v1N");
		/* Drop through */

	    case HPRTL:		/* Write HPRTL header */
		/* Direct pixel mode, 8 bits/component */
		fwrite("\033*v6W\000\003\010\010\010\010", 11, 1, rtl_args.outfile);
		/* Image width in pixels. */
		fprintf(rtl_args.outfile, "\033*r%dS", im_x);
		/* Image height in pixels.*/
		fprintf(rtl_args.outfile, "\033*r%dT", im_y);
		/* No negative motion. */
		fprintf(rtl_args.outfile, "\033&a1N");
		/* Mode 2 row compression */
		/* But, we REALLY ought to have delta row compression here. . . */
		fprintf(rtl_args.outfile, "\033*b2M");
		/* Printer resolution in DPI */
		fprintf(rtl_args.outfile, "\033*t%dR", PlotVersDotsPerInch);
		/* Start raster data */
		fprintf(rtl_args.outfile, "\033*r%cA",
			(PlotVersPlotType == HPGL2) ? '1' : '0');
		break;
	}

	/* Reserve enough space for run-length encoding compression */
	rtl_args.outbytes = mallocMagic((im_x * 3) + ((im_x * 3) / 127) + 1);
    }
    else
#endif
    {
	/* open PNM file */

	fp = PaOpen (fileName, "w", ".pnm", ".", NULL, NULL);
	if (fp == NULL)
	{
	    TxError ("Could not open file `%s' for writing\n", fileName);
	    goto done;
	}
  
	fprintf (fp, "P6\n");
	fprintf (fp, "%d %d\n", im_x, im_y);
	fprintf (fp, "255\n");
    }

    im_yoffset = im_y - 1;

    TxPrintf ("PNM image dimensions: %d x %d\n", im_x, im_y);
#if 0
    TxPrintf ("Region size: %d x %d\n", tile_xsize, tile_ysize);
    TxPrintf ("Pixels per region: %d\n", y_pixels);
    TxPrintf ("Scale: %g\n", scale);
    TxPrintf ("Antialiasing overlap: %d\n", scale_over_2);
    if (PlotPNMdownsample > 0)
    {
	TxPrintf ("Downsampling: %d\n", PlotPNMdownsample);
	TxPrintf ("Downsampled region size: %d x %d\n", ds_xsize, ds_ysize);
    }
#endif

    strip = (float *) mallocMagic((unsigned) (ds_over_2 * 2 * 3 * sizeof(float)));
    lkstep = (int *) mallocMagic((unsigned) (ds_over_2 * 2 * sizeof(int)));

    scaledown = scale / (2 * (1 << PlotPNMdownsample));
    for (x = -ds_over_2; x < ds_over_2; x++)
    {
	lkstep[ds_over_2 + x] = ((float)ABS(x)) / scaledown * LANCZOS_KERNEL_SIZE;
	if (lkstep[ds_over_2 + x] >= LANCZOS_KERNEL_SIZE)
	    lkstep[ds_over_2 + x] = LANCZOS_KERNEL_SIZE - 1;
    }

    /* Compute the normalization factor (what to divide by after adding up the	*/
    /* weighted values of all pixels in the kernel area).			*/

    normal = 0.0;
    for (x = 0; x < 2 * ds_over_2; x++)
	for (y = 0; y < 2 * ds_over_2; y++)
	    normal += lk[lkstep[x]] * lk[lkstep[y]];

    iter = 0;
    while (im_yoffset >= 0)
    {
	/* If this is a slow rendering, then we'll announce	*/
	/* the progress every 20 steps.				*/

	if ((++iter) % 10 == 0)
	{
	    TxPrintf("%g%% done\n",
			100 * (float)(im_y - im_yoffset + 1) / (float)im_y);
	    TxFlushOut();
	}

	/* Clear tile memory with the background gray level */

	memset((void *)rtile, PlotPNMBG,
		(size_t)(ds_xsize * ds_ysize * PIXELSZ));

	if (SigInterruptPending)
	{
	    TxPrintf (" *** interrupted ***\n");
	    goto done;
	}
	/* Use the "UniqueTiles" function to avoid painting contacts twice */
	DBTreeSrUniqueTiles(scx, layers, xMask, pnmTile, (ClientData)&scx->scx_area);

	/* anti-aliased rendering */

#ifdef VERSATEC
	if (PlotPNMRTL)
	    pnmRenderRegion(scale, scale_over_2, normal, strip, pnmRTLLineFunc,
			(ClientData)(&rtl_args));
	else
#endif
	    pnmRenderRegion(scale, scale_over_2, normal, strip, pnmLineFunc,
			(ClientData)fp);

	/* advance to the next strip */
	im_yoffset -= y_pixels;			/* in output coords */
	tile_yshift -= tile_ydelta;		/* in magic coords */
	scx->scx_area.r_ybot -= tile_ydelta;
	scx->scx_area.r_ytop -= tile_ydelta;
    }

    /* TxPrintf ("Save to file `%s', scale = %f\n", fileName, scale);*/
#ifdef VERSATEC
    if (PlotPNMRTL)
    {
	switch (PlotVersPlotType)
	{
	    case HPRTL:
		PlotHPRTLTrailer(rtl_args.outfile);
		break;
	    case HPGL2:
		PlotHPGL2Trailer(rtl_args.outfile);
		break;
	}
	fflush(rtl_args.outfile);
	fclose(rtl_args.outfile);
	freeMagic(rtl_args.outbytes);

	/* Run spooler */
	sprintf(command, PlotVersCommand, PlotVersPrinter, fileName);
	if (system(command) != 0)
	{
	    TxError("Couldn't execute spooler command to print \"%s\"\n",
			fileName);
	}
    }
    else
#endif
	fclose (fp);

done:
    PlotPNMdownsample = save_ds;
    freeMagic(rtile);
    freeMagic(strip);
    freeMagic(lkstep);
    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * lanczos_kernel --
 *
 * 	Compute the value of the lanczos kernel at the given position.
 *	
 *
 * Results:
 *	Returns kernel value at arg x.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

float lanczos_kernel(i, n)
    int i, n;
{
    double x;		/* position at which to evaluate the lanczos kernel */

    if (i == 0)
	return (float)1.0;
    else
	x = (double)i / (double)n;

    return (float)(sin(PI * x) / (PI * x)) * (sin(PI * 0.5 * x) / (PI * 0.5 * x));
}

/*
 * ----------------------------------------------------------------------------
 *	Color blending functions.
 *
 *	PNMColorBlend blends two colors denoted by R, G, B components (0-255).
 *	"c_have" is the color that is already present, and "c_put" is the
 *	color being overlaid.
 *
 *	PNMColorIndexAndBlend blends an R, G, B component color with a color
 *	indexed into a colormap table.
 *
 *	Both functions return an R, G, B component color.
 * ----------------------------------------------------------------------------
 */

pnmcolor
PNMColorBlend(c_have, c_put)
    pnmcolor *c_have, *c_put;
{
    pnmcolor loccolor;
    short r, g, b;

    /* "127" is half the background color (which should be derived) */ 

    r = (short)c_put->r - 127 + (short)c_have->r / 2;
    g = (short)c_put->g - 127 + (short)c_have->g / 2;
    b = (short)c_put->b - 127 + (short)c_have->b / 2; 

    loccolor.r = (r < 0) ? 0 : (unsigned char)r;
    loccolor.g = (g < 0) ? 0 : (unsigned char)g;
    loccolor.b = (b < 0) ? 0 : (unsigned char)b;

    return loccolor;
}

pnmcolor
PNMColorIndexAndBlend(c_have, cidx)
    pnmcolor *c_have;
    int cidx;
{
    pnmcolor loccolor, *c_put;
    int ir, ig, ib;
    short r, g, b;

    if ((ncolors > 0) && (cidx < ncolors))
    {
	c_put = &PNMcolors[cidx];
	r = (short)c_put->r;
	g = (short)c_put->g;
	b = (short)c_put->b;
    }
    else
    {
	GrGetColor(cidx, &ir, &ig, &ib);
	r = (short)ir;
	g = (short)ig;
	b = (short)ib;
    }

    /* "127" is half the background color (which should be derived) */ 

    r += (short)c_have->r / 2 - 127;
    g += (short)c_have->g / 2 - 127;
    b += (short)c_have->b / 2 - 127;

    loccolor.r = (r < 0) ? 0 : (unsigned char)r;
    loccolor.g = (g < 0) ? 0 : (unsigned char)g;
    loccolor.b = (b < 0) ? 0 : (unsigned char)b;

    return loccolor;
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotPNMTechInit --
 *
 * 	Called when magic starts up.
 *	
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initializes lk[...] array with the lanczos kernel.
 *
 * ----------------------------------------------------------------------------
 */

void
PlotPNMTechInit()
{
    int i;

    /* Clear out any old information */

    if (PaintStyles != NULL)
	freeMagic(PaintStyles);

    PaintStyles = (pstyle *)mallocMagic(DBNumUserLayers * sizeof(pstyle));
    for (i = 0; i < DBNumUserLayers; i++)
    {
	PaintStyles[i].wmask = 0;
	PaintStyles[i].color.r = 0xff;
	PaintStyles[i].color.g = 0xff;
	PaintStyles[i].color.b = 0xff;
    }
   
    Init_Error = 0;

     /* Initialize Lanczos kernel */
    for (i = 0; i <= 2 * LANCZOS_KERNEL_SIZE; i++)
	lk[i] = lanczos_kernel(i, LANCZOS_KERNEL_SIZE);
}



/*
 * ----------------------------------------------------------------------------
 *
 * PlotPNMTechLine --
 *
 * 	Parse a magic technology file line for the pnm plot style
 *
 * Results:
 *	Return TRUE always (no errors flagged).
 *
 * Side effects:
 *	Modifies paintstyles[] array.
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
bool
PlotPNMTechLine(sectionName, argc, argv)
    char *sectionName;		/* Name of this section (unused). */
    int argc;			/* Number of arguments on line. */
    char *argv[];		/* Pointers to fields of line. */
{
    int i, j, k, style;
    void PlotPNMSetDefaults();	/* Forward declaration */

    if (!strncmp(argv[0], "color", 5))
	PlotLoadColormap((argc == 1) ? NULL : argv[1]);
    else if (!strncmp(argv[0], "dstyle", 6))
	PlotLoadStyles((argc == 1) ? NULL : argv[1]);
    else if (!strncmp(argv[0], "default", 7))
	PlotPNMSetDefaults();
    else if (!strncmp(argv[0], "draw", 4))
    {
	if (argc == 2)
	{
	    /* Use the default drawing style(s) for this type.	*/

	    i = (int)DBTechNameType(argv[1]);
	    if (i >= 0 && i < DBNumUserLayers)
	    {
		for (j = 0; j < DBWNumStyles; j++)
		{
		    style = j + TECHBEGINSTYLES;
		    if (TTMaskHasType(DBWStyleToTypes(j), i))
		    {
			PaintStyles[i].wmask |= GrStyleTable[style].mask;
			PaintStyles[i].color =
				PNMColorIndexAndBlend(&PaintStyles[i].color,
				GrStyleTable[style].color);
		    }
		}
	    }
	}
	else if (argc == 3)
	{
	    pstyle savestyle;
	    bool newcolor = FALSE;

	    /* Use the specified drawing style(s) instead of the */
	    /* display drawing styles (used to override crosses	 */
	    /* on contacts and such).				 */

	    k = (int)DBTechNameType(argv[1]);
	    if (k >= 0 && k < DBNumUserLayers)
	    {
	    	savestyle = PaintStyles[k];
		PaintStyles[k].wmask = 0;
		PaintStyles[k].color.r = 255;
		PaintStyles[k].color.g = 255;
		PaintStyles[k].color.b = 255;

		for (j = 2; j < argc; j++)
		{
		    /* Use the specified display style, or the internal one */
		    if (ndstyles > 0)
		    {
			for (i = 0; i < ndstyles; i++)
			{
			    if (!strcmp(Dstyles[i].name, argv[j]))
			    {
				PaintStyles[k].wmask |= Dstyles[i].wmask;
				PaintStyles[k].color =
					PNMColorBlend(&PaintStyles[k].color,
						&Dstyles[i].color);
				newcolor = TRUE;
			    }
			}
		    }
		    else
		    {
			i = (int)GrGetStyleFromName(argv[j]);
			if (i >= 0)
			{
			    PaintStyles[k].wmask |= GrStyleTable[i].mask;
			    PaintStyles[k].color =
					PNMColorIndexAndBlend(&PaintStyles[k].color,
					GrStyleTable[i].color);
			    newcolor = TRUE;
			}
			else
			    TxError("Unknown drawing style \"%s\" for PNM plot.\n",
					argv[j]);
		    }

		    /* In case of error, revert to the default style */

		    if (newcolor == FALSE)
			PaintStyles[k] = savestyle;
		}
	    }
	    else
		TxError("Unknown magic layer \"%s\" for PNM plot.\n", argv[1]);
	}
    }
    else if (!strncmp(argv[0], "map", 3))
    {
	k = (int)DBTechNameType(argv[1]);
	if (k >= 0 && k < DBNumUserLayers)
	{
	    for (j = 2; j < argc; j++)
	    {
		i = (int)DBTechNameType(argv[j]);
		if (i >= 0)
		{
		    PaintStyles[k].wmask |= PaintStyles[i].wmask;
		    PaintStyles[k].color = PNMColorBlend(&PaintStyles[k].color,
				&PaintStyles[i].color);
		}
	    }
	}
    }
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotPNMSetDefaults --
 *
 *	Generate default colors for the PNM plot style from existing
 *	graphics colors for the window.
 *
 * ----------------------------------------------------------------------------
 */

void
PlotPNMSetDefaults()
{
    int i, j, style;

    for (i = TT_SPACE + 1; i < DBNumUserLayers; i++)
    {
	for (j = 0; j < DBWNumStyles; j++)
	{
	    style = j + TECHBEGINSTYLES;
	    if (TTMaskHasType(DBWStyleToTypes(j), i))
	    {
		PaintStyles[i].wmask |= GrStyleTable[style].mask;
		PaintStyles[i].color =
			PNMColorIndexAndBlend(&PaintStyles[i].color,
			GrStyleTable[style].color);
	    }
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotPNMTechFinal --
 *
 *	Routine to be run at the end of reading the  "plot pnm" techfile
 *	section.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The "Dstyles" array is no longer needed and is free'd.
 *	The "PNMTypeTable" is malloc'd and entries filled.
 *
 * ----------------------------------------------------------------------------
 */

void
PlotPNMTechFinal()
{
    int i;

    for (i = 0; i < ndstyles; i++)
	freeMagic(Dstyles[i].name);

    if (Dstyles != NULL)
    {
	freeMagic(Dstyles);
	Dstyles = NULL;
	ndstyles = 0;
    }

    if (PNMcolors != NULL)
    {
	freeMagic(PNMcolors);
	PNMcolors = NULL;
	ncolors = 0;
    }

    /* If no "draw" or "map" lines were declared in the technology	*/
    /* file, then we put together a default style where we use the	*/
    /* display dstyles for each layer.  We detect the condition as	*/
    /* having all wmask values 0 in the PaintStyles array.		*/

    for (i = TT_SPACE + 1; i < DBNumUserLayers; i++)
	if (PaintStyles[i].wmask != 0)
	    break;

    if (i < DBNumUserLayers)
	return;

    PlotPNMSetDefaults();
}


/*
 * ----------------------------------------------------------------------------
 *
 * PlotLoadStyles --
 *
 * 	Read in the plotting styles for rendering.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initializes arrays for drawing/plotting.
 *
 * ----------------------------------------------------------------------------
 */

void
PlotLoadStyles(filename)
    char *filename;
{
    FILE *inp;
    char fullName[256];
    char *buf;
    int newsec;
    int ord, mask, color, outline, nfill, stipple;
    int ir, ig, ib;
    char shortname;
    char longname[128];
    char fill[42];

    if (filename == NULL)
    {
	(void) sprintf(fullName, "%.100s.7bit.mraster_dstyle", DBWStyleType);
	buf = fullName;
    }
    else
    {
	buf = filename;
    }
    inp = PaOpen(buf, "r", (char *)NULL, ".", SysLibPath, (char **) NULL);
    if (inp == NULL)
    {
	TxError ("PNM plot: Could not open display style file\n");
	Init_Error = 1;
	return;
    }

    buf = fullName;	/* reuse this space for input */

    ndstyles = 0;
    Dstyles = (dstyle *)mallocMagic(DBWNumStyles * sizeof(dstyle));

    /* Read in the dstyle file */
    newsec = FALSE;
    while (fgets (buf, 256, inp))
    {
	if (buf[0] == '#') continue;
	if (StrIsWhite (buf, FALSE))
	{
	    newsec = TRUE;
	    continue;
	}
	else if (newsec)
	{
	    if (strncmp (buf, "display_styles", 14) != 0)
		goto dstyle_err;
	    newsec = FALSE;
 	}
	else
	{
	    if (sscanf (buf, "%d %d %d %d %40s %d %c %126s",
		    &ord, &mask, &color, &outline, fill, &stipple,
		    &shortname, longname) !=  8)
		goto dstyle_err;
	    if (ndstyles == DBWNumStyles)
		goto dstyle_err;
	    Dstyles[ndstyles].wmask = mask;
	    if ((ncolors > 0) && (color >=0) && (color < ncolors))
	    {
		Dstyles[ndstyles].color = PNMcolors[color];
	    }
	    else
	    {
		GrGetColor(color, &ir, &ig, &ib);
		Dstyles[ndstyles].color.r = (unsigned char)ir;
		Dstyles[ndstyles].color.g = (unsigned char)ig;
		Dstyles[ndstyles].color.b = (unsigned char)ib;
	    }
	    Dstyles[ndstyles].name = StrDup(NULL, longname);
	    ndstyles++;
	    if (ndstyles == DBWNumStyles) break;
	}
    }
    fclose (inp);
    return;

dstyle_err:
    Init_Error = 1;
    TxError ("Format error in display style file\n");
    fclose (inp);
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotLoadColormap --
 *
 * 	Read in the colormap for rendering.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initializes arrays for drawing/plotting.
 *
 * ----------------------------------------------------------------------------
 */

void
PlotLoadColormap(filename)
    char *filename;
{
    FILE *inp;
    char fullName[256];
    char *buf;
    int red, blue, green;

    /* read in color map */
    if (filename == NULL)
    {
	(void) sprintf(fullName, "%.100s.7bit.mraster.cmap", DBWStyleType);
	buf = fullName;
    }
    else
	buf = filename;

    inp = PaOpen(buf, "r", (char *) NULL, ".", SysLibPath, (char **) NULL);
    if (inp == NULL)
    {
	TxError("Couldn't open colormap file \"%s\"\n", buf);
	Init_Error = 1;
	return;
    }
    buf = fullName;	/* reuse this space for input */

    ncolors = 0;
    PNMcolors = (pnmcolor *)mallocMagic(128 * PIXELSZ);

    while (fgets (buf, 256, inp)) {
      if (buf[0] == '#') continue;
      if (StrIsWhite (buf, FALSE))
	continue;
      if (ncolors == 128) {
	goto color_err;
      }
      if (sscanf (buf, "%d %d %d", &red, &green, &blue) != 3) {
	goto color_err;
      }
      PNMcolors[ncolors].r = (unsigned char)red;
      PNMcolors[ncolors].g = (unsigned char)green;
      PNMcolors[ncolors].b = (unsigned char)blue;
      ncolors++;
    }
    fclose(inp);
    return;

color_err:
    Init_Error = 1;
    TxError ("Format error in colormap file\n");
    fclose (inp);
}
