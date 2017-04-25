/*
 *----------------------------------------------------------------------
 * plotHP.c --
 *
 * This file contains the procedures that generate plots in
 * HP Raster Transfer Language (HPRTL) and HPGL2.
 *
 *----------------------------------------------------------------------
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "utils/malloc.h"
#include "plot/plotInt.h"

#ifdef VERSATEC

extern int PlotRTLCompress();
extern void PlotHPRTLTrailer();

extern int rasFileByteCount;

/*
 * ----------------------------------------------------------------------------
 *
 * PlotHPRTLHeader --
 *
 *	Header for HP RTL format (standard for, e.g., HP color laser printers)
 *
 * ----------------------------------------------------------------------------
 */

void
PlotHPRTLHeader(width, height, density, hpfile)
    int width, height, density;
    FILE *hpfile;
{
    fprintf(hpfile, "\033*r-3U\n");	   /* Simple CMY color space  */
    fprintf(hpfile, "\033*r%dS", width);   /* Image width in pixels. */
    fprintf(hpfile, "\033*r%dT", height);  /* Image height in pixels.*/
    fprintf(hpfile, "\033&a1N");	   /* No negative motion. */
    fprintf(hpfile, "\033*b2M");	   /* Mode 2 row compression */
    fprintf(hpfile, "\033*t%dR", density); /* Plotting density in DPI. */
    fprintf(hpfile, "\033*r0A");	   /* Start raster data. */
} 

/*
 * ----------------------------------------------------------------------------
 *
 * PlotHPGL2Header --
 *
 *	Header for HPGL2 (plotter) format
 *
 * ----------------------------------------------------------------------------
 */

#define	LABEL_SPACER 200	/* Height of the label area, in pixels	  */ 
#define IN_TO_HPGL  1016	/* HPGL2 coordinates are in 0.025mm units */
#define THIN_MARGIN   40	/* thin spacer (1mm) in HPGL2 coordinates */

void
PlotHPGL2Header(width, height, density, scale, hpfile)
    int width, height, density, scale;
    FILE *hpfile;
{
    fprintf(hpfile, "\033%%-12345X");	   /* Universal Command Language. */
    fprintf(hpfile, "@PJL ENTER LANGUAGE=HPGL2\r\n");
    fprintf(hpfile, "\033E\033%%0B");	   /* Reset printer; set HPGL2 mode. */
    fprintf(hpfile, "BP1,\"MAGIC\",5,1;"); /* Declare name; disable auto-rotate */

    fprintf(hpfile, "PS%d,%d;", ((height + LABEL_SPACER) * IN_TO_HPGL / density)
		+ THIN_MARGIN, (width * IN_TO_HPGL / density) + THIN_MARGIN);
    /* Move the pen to the right edge of the paper. */
    fprintf(hpfile, "SP1PA%d,0", (width * IN_TO_HPGL / density));
    /* Plot upside down with a 10-point font. */
    fprintf(hpfile, "DI-1,0SD3,10;");
    /* Plot a label. */
    fprintf(hpfile, "LB\r\nMagic Plot (%dX)\r\n\003SP0;", scale);

    fprintf(hpfile, "\033%%0A");			/* Enter RTL mode. */
    fprintf(hpfile, "\033*v1N");			/* Source mode opaque */

    /* Make room for the label string by moving vertically. */
    fprintf(hpfile, "\033*b%dY", LABEL_SPACER);

    /* HPRTL Color reference guide:						*/
    /* h20000.www2.hp.com/bc/docs/support/SupportManual/bpl13212/bpl13212.pdf	*/

    /* Configure Image Data (CID) command		*/
    /* Color space (1) = CMY				*/
    /* Pixel encoding mode (0) = Index by plane		*/
    /* Bits per index (3) = 8-index (2^3) colormap	*/
    /* Color planes (1, 1, 1) = 1 bit each		*/
    /* Bit planes are cyan / magenta / yellow and must	*/
    /* be output in that order.				*/

    /* fwrite("\033*v6W\1\0\3\1\1\1", 11, 1, hpfile); */

    /* Apparently some plotters don't support plane-indexed CMY mode.	*/
    /* Due to this oversight, it is necessary to build the CMY color	*/
    /* table by hand by reversing the RGB color table.			*/

    fwrite("\033*v6W\0\0\3\1\1\1", 11, 1, hpfile);
    fprintf(hpfile, "\033*v255a255b255c0I\n");
    fprintf(hpfile, "\033*v0a255b255c1I\n");
    fprintf(hpfile, "\033*v255a0b255c2I\n");
    fprintf(hpfile, "\033*v0a0b255c3I\n");
    fprintf(hpfile, "\033*v255a255b0c4I\n");
    fprintf(hpfile, "\033*v0a255b0c5I\n");
    fprintf(hpfile, "\033*v255a0b0c6I\n");
    fprintf(hpfile, "\033*v0a0b0c7I\n");

    fprintf(hpfile, "\033*r%dS", width);   /* Set the image width in pixels. */
    fprintf(hpfile, "\033*r%dT", height);  /* Set the image height in pixels.*/
    fprintf(hpfile, "\033&a1N");	   /* No negative motion. */
    fprintf(hpfile, "\033*b2M");	   /* Mode 2 row compression */
    fprintf(hpfile, "\033*t%dR", density); /* Plot density, in DPI. */
    fprintf(hpfile, "\033*r1A");	   /* Start sending raster data. */
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotHPRTLTrailer --
 *
 * ----------------------------------------------------------------------------
 */

void
PlotHPRTLTrailer(hpfile)
    FILE *hpfile;
{
    fprintf(hpfile, "\033*r0B\014\n");		/* End raster graphics. */
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotHPGL2Trailer --
 *
 * ----------------------------------------------------------------------------
 */

void
PlotHPGL2Trailer(hpfile)
    FILE *hpfile;
{
    fprintf(hpfile, "\033*rC");		/* End raster graphics. */
    fprintf(hpfile, "\033%%0B");	/* HPGL2 mode. */
    fprintf(hpfile, "PG;");		/* Terminate plot. */
    fprintf(hpfile, "\033%%-12345X");	/* Universal command language reset. */
    fprintf(hpfile, "@PJL\r\n");
}

/*
 * ----------------------------------------------------------------------------
 * HP Run-length compression algorithm #2
 * This code has been taken from the "p3" PNM-to-HPRTL conversion tool.
 * Subroutine written by Noel Gorelick (ngorelic@speclab.cr.usgs.gov)
 * Author's comments below:
 *
 * The HP Paintjet 300-XL and DesignJet 650C both have the option of receiving
 * data using TIFF packbits encoding.  This seemed like a good idea to me, so I
 * ran to the libtiff package to see if there was anything useful there to do
 * packbits for me.  I was sorely disappointed to find a large (225 lines)
 * routine that will do this, but replys heavily on the internal data
 * structures used by the rest of the libtiff library.
 * 
 * So, I wrote my own.  Here is a much simpler pair of routines to do the same
 * thing.  These were tested on both the PaintJet-300XL and DesignJet 650C,
 * having been compiled using an HP9000s800.  If anyone is actually interested
 * in the driver program that converts a PPM image for use on these printers, I
 * could probably post that as well.
 * 
 * Noel (ngorelic@speclab.cr.usgs.gov)
 *-----------------------------------------------------------------------
 */

/* 
 *-----------------------------------------------------------------------
 *
 * PlotRTLCompress --
 *  
 * This routine encodes a string using the TIFF packbits encoding
 * scheme.  This encoding method is used by several HP peripherals to
 * reduce the size of incoming raster images.  Both routines convert s1
 * into s2.  The len parameter indicates the size of the incoming string.
 * The return value is the size of the output string (in s2).
 *
 * Results:
 *	Returns the length of the compressed data.  Note that compression
 *	is not guaranteed, and the output may be larger than the input.
 *	However, it is bounded by N + (N / 127) + 1.
 *
 * Side effects:
 *	Output data placed in s2, which must be large enough to hold it.
 * 
 *-----------------------------------------------------------------------
 */

int
PlotRTLCompress(s1, s2, len)
    unsigned char *s1, *s2;
    int len;
{
    /*
     * Pack s1 using TIFF packbits encoding into s2
     */
    int count = 0;
    int i;
    int base, newbase, size, outp;

    base = newbase = outp = 0;
    for (i = 1; i < len; i++)
    {
	if (s1[newbase] == s1[i])
	    count++;
	else
	{
	    if (count < 2)
	    {
		newbase = i;
		count = 0;
	    }
	    else
	    {
		/*
		 * Put any backed up literals first.
		 */
		while ((newbase - base) > 0)
		{
		    size = MIN(127, newbase - base - 1);
		    s2[outp++] = size;
		    memcpy(s2 + outp, s1 + base, size + 1);
		    outp += size + 1;
		    base += size + 1;
		}
		/*
		 * Now put -count and repeated string.
		 */
		count++;
		while (count > 0)
		{
		    size = MIN(128, count);
		    s2[outp++] = -(size - 1);
		    s2[outp++] = s1[newbase];
		    count -= size;
		}
		base = newbase = i;
	    }
	}
    }

    /*
     * Output any trailing literals.
     */
    newbase = i;
    while ((newbase - base) > 0)
    {
	size = MIN(127, newbase - base - 1);
	s2[outp++] = size;
	memcpy(s2 + outp, s1 + base, size + 1);
	outp += size + 1;
	base += size + 1;
    }
    return (outp);
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotDumpHPRTL  --
 *
 *	Combines the four (CMYK) swath buffers by ORing the black image
 *	into the primary color swaths.  The swath is compressed using
 *	HP run-length row compression mode 2 (TIFF compression).
 *
 * Results:
 *	Returns 0 if all was well.  Returns non-zero if there was
 *	an I/O error.  In this event, this procedure prints an
 *	error message before returning.
 *
 * Side effects:
 *	Information is added to the file.
 *
 * ----------------------------------------------------------------------------
 */

int
PlotDumpHPRTL(hpfile, kRaster, cRaster, mRaster, yRaster)
    FILE *hpfile;		/* File in which to dump it. */
    Raster *kRaster;		/* Rasters to be dumped. */
    Raster *cRaster;
    Raster *mRaster;
    Raster *yRaster;
{
    int line, count, line_offset = 0;
    int ipl, bpl;
    register int *c, *m, *y, *k;
    unsigned char *obytes;	/* bytes to output (compressed) */
    int size;

    ipl = kRaster->ras_intsPerLine;
    bpl = kRaster->ras_bytesPerLine;
    
    c = cRaster->ras_bits;
    m = mRaster->ras_bits;
    y = yRaster->ras_bits;
    k = kRaster->ras_bits;

    /* Mode 2 row compression has a worst-case length of N + (N / 127) + 1 */
    obytes = (unsigned char *)mallocMagic(bpl + (bpl / 127) + 1);

    for (line = 0; line < kRaster->ras_height; line++)
    {
	/* Merge the black plane into C, M, and Y */
	for (count = 0; count < ipl; count++)
	{
	    *c++ = (*c | *k);
	    *m++ = (*m | *k);
	    *y++ = (*y | *k);
	    k++;
	}

	/* Compress each plane (C, M, and Y) and output */

	size = PlotRTLCompress(c - ipl, obytes, bpl);
	fprintf(hpfile, "\033*b%dV", size);
	fwrite(obytes, size, 1, hpfile);

	size = PlotRTLCompress(m - ipl, obytes, bpl);
	fprintf(hpfile, "\033*b%dV", size);
	fwrite(obytes, size, 1, hpfile);

	size = PlotRTLCompress(y - ipl, obytes, bpl);
	fprintf(hpfile, "\033*b%dW", size);
	fwrite(obytes, size, 1, hpfile);
    }
    freeMagic(obytes);

    if (count < 0)
    {
	TxError("I/O error in writing HPRTL file:  %s.\n", strerror(errno));
	return 1;
    }
    rasFileByteCount += count;
    return 0;
}

#endif /* VERSATEC */
