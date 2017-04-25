/*
 * plotInt.h --
 *
 * Contains definitions for things that are used internally by the
 * plot module but not exported.
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
 * rcsid $Header: /usr/cvsroot/magic-8.0/plot/plotInt.h,v 1.2 2010/03/08 13:33:33 tim Exp $
 */

#ifndef _PLOTINT_H
#define _PLOTINT_H

#include "utils/magic.h"
#include "utils/geometry.h"

#define VERSATEC		/* Add this for HP plotter support */

/* system V machines lack vfont.h, so include the defs below. */
#if  !defined(SYSV) && !defined(__FreeBSD__) && !defined(__NetBSD__) && !defined(CYGWIN) && !defined(__APPLE__) && !defined(__DragonFly__) && !defined(__OpenBSD__)
#include <vfont.h>
#else
struct header {
        short magic;
        unsigned short size;
        short maxx;
        short maxy;
        short xtend;
};

struct dispatch {
        unsigned short addr;
        short nbytes;
        char up,down,left,right;
        short width;
};
#endif	/* SYSV */

/* The structure below is used for raster output, e.g. to a Versatec
 * printer.  It defines a rectangular raster area in which to fill
 * bits for printing.
 */

typedef struct
{
    int ras_width;		/* How many bits across the raster
				 * (x-dimension).
				 */
    int ras_bytesPerLine;	/* How much to add to the byte address
				 * of one pixel to find the byte containing
				 * the pixel just below it.
				 */
    int ras_intsPerLine;	/* How much to add to the address of an
				 * integer containing a pixel to find the
				 * address of the integer containing the 
				 * pixel just underneath it.
				 */
    int ras_height;		/* Raster height (y-dimension).  For proper
				 * stippling, this should usually be a
				 * multiple of the stipple height.
				 */
    int *ras_bits;		/* Storage for raster.  The raster is
				 * organized as BYTES:  the high-order bit
				 * of the first byte corresponds to the
				 * leftmost pixel of the highest raster line.
				 * Successive bits fill out the first line.
				 * The next raster line starts on the next
				 * 4-byte boundary.
				 */
} Raster;


/* This structure is also used for raster output, but to "deep"
 * devices such as frame buffers and Dicomend cameras.  It defines a
 * rectangular raster area in which to fill bits for printing. Each byte 
 * of the pix_pixels array holds one pixel; for an RGB image, three 
 * PixRasters will be needed.
 */

typedef struct
{
    int pix_width;		/* How many bytes across the raster
				 * (x-dimension).
				 */
    int pix_height;		/* Raster height (y-dimension).  For proper
				 * stippling, this should usually be a
				 * multiple of the stipple height.
				 */
    char *pix_pixels;		/* Storage for raster.  The raster is
				 * organized as BYTES: each byte is one pixel.
				 */
} PixRaster;

/* The structure below describes a stipple pattern.  16 32-bit words
 * are used to represent a 16-pixel high, 32-pixel wide texture pattern
 * for filling.  Most of the actual stipple patterns are 16-bits wide,
 * in which case the two halves of each word are duplicates.
 */

typedef unsigned int Stipple[16];

/*
 * The versatec color codes are chosen to correspond to the codes used by the
 * color plotter, so no translation will be necessary.
 */
typedef short VersatecColor;		/* the possible versatec colors */

#define    BLACK    0
#define    CYAN     1
#define    MAGENTA  2
#define    YELLOW   3


/* The structure below describes a font.  It consists primarily of the
 * stuff read in from disk in Vfont format.  See the vfont(5) man page
 * for details of what's in a font.  All the fonts that have been
 * read in so far are linked by their fo_next fields.
 */

typedef struct font
{
    char *fo_name;			/* Name of font. */
    struct header fo_hdr;		/* Header structure. */
    struct dispatch fo_chars[256];	/* Character descriptors. */
    char *fo_bits;			/* Character bitmaps. */
    Rect fo_bbox;			/* Bounding box of space occupied
					 * by all characters, assuming
					 * (0,0) origin.
					 */
    struct font *fo_next;		/* Next in list, or NULL for
					 * end of list.
					 */
} RasterFont;

/* Technology-file reading procedures: */

extern void PlotPSTechInit();
extern bool PlotPSTechLine();

extern void PlotGremlinTechInit();
extern bool PlotGremlinTechLine();

extern void PlotVersTechInit();
extern bool PlotVersTechLine();

extern void PlotColorVersTechInit();
extern bool PlotColorVersTechLine();

extern void PlotPNMTechInit();
extern bool PlotPNMTechLine();
extern void PlotPNMTechFinal();

extern void PlotPixTechInit();
extern bool PlotPixTechLine();

/* Raster utilities: */

extern void PlotRastInit();
extern Raster *PlotNewRaster();
extern void PlotFreeRaster();
extern void PlotClearRaster();
extern void PlotFillRaster();
extern void PlotPolyRaster();
extern int PlotDumpRaster();
extern Stipple PlotBlackStipple;
extern RasterFont *PlotLoadFont();
extern void PlotTextSize();
extern void PlotRasterText();
extern int PlotSwapBytes();
extern short PlotSwapShort();
extern int PlotDumpColorPreamble();

extern PixRaster *PlotNewPixRaster();
extern void PlotFreePixRaster();
extern void PlotClearPixRaster();
extern void plotFillPixRaster();
extern void PlotPixRasterText();
extern int PlotDumpPixRaster();

/* User-settable plotting parameters: */

/* for PostScript (plotPS.c): */
extern char *PlotPSIdFont;
extern char *PlotPSNameFont;
extern char *PlotPSLabelFont;
extern int PlotPSIdSize;
extern int PlotPSNameSize;
extern int PlotPSLabelSize;
extern int PlotPSBoundary;
extern int PlotPSWidth;
extern int PlotPSHeight;
extern int PlotPSMargin;

/* for Versatec (plotVers.c): */
#ifdef VERSATEC
extern int PlotVersWidth;
extern int PlotVersDotsPerInch;
extern int PlotVersSwathHeight;
extern char *PlotVersPrinter;
extern char *PlotVersCommand;
extern char *PlotTempDirectory;
extern char *PlotVersIdFont;
extern char *PlotVersNameFont;
extern char *PlotVersLabelFont;
extern unsigned char PlotVersPlotType;
#endif

enum { VERSATEC_COLOR = 0, VERSATEC_BW, HPRTL, HPGL2 };

/* for plotPixels.c: */
#ifdef LLNL
extern int PlotPixHeight;
extern int PlotPixWidth;
#endif

/* for all: */
extern bool PlotShowCellNames;

/* for plotPnm.c */
extern int PlotPNMmaxmem;
extern int PlotPNMdownsample;
extern unsigned char PlotPNMBG;
#ifdef VERSATEC
extern bool PlotPNMRTL;
#endif

#endif /* _PLOTINT_H */
