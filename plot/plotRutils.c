/*
 * plotRastUtils.c --
 *
 * This file contains several procedures for manipulating rasters.
 * The procedures are used to create bitmaps to directly drive
 * printers such as the black-and-white Versatec.  For example,
 * the procedures draw stippled areas and raster-ize text strings
 * using fonts.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/plot/plotRutils.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/malloc.h"
#include "plot/plotInt.h"
#include "textio/textio.h"
#include "utils/utils.h"

extern double sqrt();

int rasFileByteCount = 0;
/* A solid black stipple: */

Stipple PlotBlackStipple =
{
    0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
    0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
    0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
    0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
};

/* The following two arrays are used to select a range of bits within
 * a word.  The first array selects all the bits at or to the left of
 * the given bit-position. "To the left" means in a lower-numbered
 * byte (according to byte order) or in a more significant bit of the
 * same byte.  The second array selects the bits at or to the right of
 * the given bit-position.  Bit position 0 is considered to be the
 * leftmost bit of the word;  it's the highest-order bit in the 0th
 * byte.
 */

static unsigned int leftBits[32] =
{
    0x00000080, 0x000000c0, 0x000000e0, 0x000000f0,
    0x000000f8, 0x000000fc, 0x000000fe, 0x000000ff,
    0x000080ff, 0x0000c0ff, 0x0000e0ff, 0x0000f0ff,
    0x0000f8ff, 0x0000fcff, 0x0000feff, 0x0000ffff,
    0x0080ffff, 0x00c0ffff, 0x00e0ffff, 0x00f0ffff,
    0x00f8ffff, 0x00fcffff, 0x00feffff, 0x00ffffff,
    0x80ffffff, 0xc0ffffff, 0xe0ffffff, 0xf0ffffff,
    0xf8ffffff, 0xfcffffff, 0xfeffffff, 0xffffffff
};
static unsigned int rightBits[32] =
{
    0xffffffff,	0xffffff7f, 0xffffff3f, 0xffffff1f,
    0xffffff0f, 0xffffff07, 0xffffff03, 0xffffff01,
    0xffffff00,	0xffff7f00, 0xffff3f00, 0xffff1f00,
    0xffff0f00, 0xffff0700, 0xffff0300, 0xffff0100,
    0xffff0000,	0xff7f0000, 0xff3f0000, 0xff1f0000,
    0xff0f0000, 0xff070000, 0xff030000, 0xff010000,
    0xff000000,	0x7f000000, 0x3f000000, 0x1f000000,
    0x0f000000, 0x07000000, 0x03000000, 0x01000000
};

/* The following arrray selects a single bit within a word.  The
 * zeroth entry selects the bit that will be leftmost in the plot.
 */
static unsigned int singleBit[32] =
{
    0x00000080, 0x00000040, 0x00000020, 0x00000010,
    0x00000008, 0x00000004, 0x00000002, 0x00000001,
    0x00008000, 0x00004000, 0x00002000, 0x00001000,
    0x00000800, 0x00000400, 0x00000200, 0x00000100,
    0x00800000, 0x00400000, 0x00200000, 0x00100000,
    0x00080000, 0x00040000, 0x00020000, 0x00010000,
    0x80000000, 0x40000000, 0x20000000, 0x10000000,
    0x08000000, 0x04000000, 0x02000000, 0x01000000
};

RasterFont *PlotFontList;	/* Linked list of all fonts that have
				 * been read in so far.
				 */

/*
 * ----------------------------------------------------------------------------
 *
 * PlotRastInit --
 *
 * 	Initialize data structures related to rasters.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All this procedure does is to swap bytes in the stipple
 *	masks if we're running on a non-VAX.
 *
 * ----------------------------------------------------------------------------
 */

void
PlotRastInit()
{
#ifdef WORDS_BIGENDIAN
    int i;

    for (i = 0; i < 32; i++)
    {
	leftBits[i] = PlotSwapBytes(leftBits[i]);
	rightBits[i] = PlotSwapBytes(rightBits[i]);
	singleBit[i] = PlotSwapBytes(singleBit[i]);
    }
#endif /* WORDS_BIGENDIAN */
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotNewRaster --
 *
 * 	Allocate and initialize a new raster structure.
 *
 * Results:
 *	The return value is a pointer to the new raster object.
 *
 * Side effects:
 *	Memory is allocated.
 *
 * ----------------------------------------------------------------------------
 */

Raster *
PlotNewRaster(height, width)
    int height;			/* Raster's height in pixels.  Should generally
				 * be a multiple of 16.
				 */
    int width;			/* Raster's width in pixels.  Should generally
				 * be a multiple of 32 bits.
				 */
{
    Raster *new;

    new = (Raster *) mallocMagic(sizeof(Raster));
    new->ras_width = width;
    new->ras_intsPerLine = (width+31)/32;
    new->ras_bytesPerLine = new->ras_intsPerLine*4;
    new->ras_height = height;
    new->ras_bits = (int *) mallocMagic(
	    (unsigned) (height * new->ras_intsPerLine * 4));

    return new;
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotFreeRaster --
 *
 * 	Frees up the memory associated with an existing raster structure.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The storage associated with raster is returned to the allocator.
 *
 * ----------------------------------------------------------------------------
 */

void
PlotFreeRaster(raster)
    Raster *raster;		/* Raster whose memory is to be freed.  Should
				 * have been created with PlotNewRaster.
				 */
{
    freeMagic((char *) raster->ras_bits);
    freeMagic((char *) raster);
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotClearRaster --
 *
 * 	This procedure clears out an area of the raster.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The area of the raster indicated by "area" is set to all zeroes.
 *
 * ----------------------------------------------------------------------------
 */

void
PlotClearRaster(raster, area)
    Raster *raster;			/* Raster that's to be cleared. */
    Rect *area;				/* Area to be cleared, in raster
					 * coords.  NULL means clear the
					 * whole raster.
					 */
{
    int *left, *right, *cur;
    int leftMask, rightMask, line;

    if (area == NULL)
    {
	bzero((char *) raster->ras_bits,
		raster->ras_bytesPerLine * raster->ras_height);
	return;
    }

    /* Compute the address of the leftmost word in the topmost line
     * to be cleared, and the rightmost word in the topmost line to
     * be cleared.
     */
    
    left = raster->ras_bits +
	((raster->ras_height-1) - area->r_ytop)*raster->ras_intsPerLine;
    right = left + area->r_xtop/32;
    left += area->r_xbot/32;

    /* Divide the x-span of the area into three parts:  the leftmost
     * word, of which only the rightmost bits are cleared, the
     * rightmost word, of which only the leftmost bits are cleared,
     * and the middle section, in which all bits of each word are
     * cleared.  Compute masks that determine which bits of the end
     * words will be cleared.  There's a special case when the left
     * and right ends are in the same word.
     */
    
    leftMask = rightBits[area->r_xbot&037];
    rightMask = leftBits[area->r_xtop&037];
    if (left == right)
	leftMask &= rightMask;
    
    /* Clear the area one raster line at a time, top to bottom. */

    for (line = area->r_ytop; line >= area->r_ybot; line -= 1)
    {
	/* Clear the leftmost word on this line. */

	*left &= ~leftMask;

	if (left != right)
	{
	    /* Clear the center of the line. */

	    for (cur = left+1; cur < right; cur += 1)
		*cur = 0;
	    
	    /* Clear the rightmost word on this line. */

	    *cur &= ~rightMask;
	}

	left += raster->ras_intsPerLine;
	right += raster->ras_intsPerLine;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 *  PlotPolyRaster --
 *
 *	Fill a polygonal raster area, given the area of a triangular
 *	tile (in pixel coordinates), information about what side and
 *	direction are filled, and the area to clip to (also in raster
 *	coordinates).  This is *not* a general-purpose polygon-filling
 *	routine.  It can only handle clipped right triangles.
 *
 *  Results:
 *  	None.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
PlotPolyRaster(raster, tileArea, clipArea, dinfo, stipple)
    Raster *raster;	/* Pointer to raster whose bits are
			 * to be filled in.
			 */
    Rect *tileArea;	/* Area of split tile, in pixel coordinates */
    Rect *clipArea;	/* Area to clip, in pixel coordinates */
    TileType dinfo;	/* Split tile side and direction information */
    Stipple stipple;	/* Stipple pattern to be used to fill
			 * in the raster area.
			 */
{
    int *cur, *rasleft, *tbase, *right, *left;
    int leftMask, rightMask, curStipple;
    int line, width, height, locleft, locright;
    Rect area;

    /* Compute the address of the leftmost word in the topmost line
     * to be filled, and the rightmost word in the topmost line to
     * be filled.
     */

    area = *tileArea;
    GEOCLIP(&area, clipArea);

    /* Ensure that we have not been clipped out of existence */
    if (area.r_xbot > area.r_xtop) return;
    if (area.r_ybot >= area.r_ytop) return;
    
    rasleft = raster->ras_bits +
	((raster->ras_height-1) - area.r_ytop)*raster->ras_intsPerLine;
    width = tileArea->r_xtop - tileArea->r_xbot;
    height = tileArea->r_ytop - tileArea->r_ybot;

    /* Process the stippled area one raster line at a time, top to bottom. */

    if (dinfo & TT_SIDE)
    {
	locright = area.r_xtop;
	tbase = rasleft + area.r_xtop / 32;	/* base is on right */
    }
    else
    {
	locleft = area.r_xbot;
	tbase = rasleft + area.r_xbot / 32;	/* base is on left */
    }

    for (line = area.r_ytop; line >= area.r_ybot; line -= 1)
    {
	if (dinfo & TT_SIDE)
	{
	    if (dinfo & TT_DIRECTION)
		locleft = tileArea->r_xbot + (((tileArea->r_ytop - line) * width)
			/ height);
	    else
		locleft = tileArea->r_xbot + (((line - tileArea->r_ybot) * width)
			/ height);
	    right = tbase;
	    left = rasleft + locleft / 32;
	}
	else
	{
	    if (dinfo & TT_DIRECTION)
		locright = tileArea->r_xbot + (((tileArea->r_ytop - line) * width)
			/ height);  
	    else
		locright = tileArea->r_xbot + (((line - tileArea->r_ybot) * width)
			/ height);
	    right = rasleft + locright / 32;
	    left = tbase;
	}
	if (left > right) continue;

	leftMask = rightBits[locleft & 037];
	rightMask = leftBits[locright & 037];
	if (left == right)
	    leftMask &= rightMask;
    
	curStipple = stipple[(-line)&017];

	/* Fill the leftmost word on this line. */

	*left |= curStipple & leftMask;

	if (left != right)
	{
	    /* Fill the center of the line. */

	    for (cur = left+1; cur < right; cur += 1)
		*cur |= curStipple;
	    
	    /* Fill the rightmost word on this line. */

	    *cur |= curStipple & rightMask;
	}

	rasleft += raster->ras_intsPerLine;
	tbase += raster->ras_intsPerLine;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotFillRaster --
 *
 * 	Given a raster and an area, this procedure fills the given area
 *	of the raster with a particular stipple pattern.
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
PlotFillRaster(raster, area, stipple)
    Raster *raster;		/* Pointer to raster whose bits are
					 * to be filled in.
					 */
    Rect *area;		/* Area to be filled in pixel coords.
					 * This is an inclusive area:  it
					 * includes the boundary pixels.  The
					 * caller must ensure that it is
					 * clipped to the raster area.
					 */
    Stipple stipple;			/* Stipple pattern to be used to fill
					 * in the raster area.
					 */
{
    int *left, *cur, line;
    int *right;
    int leftMask, rightMask, curStipple;

    /* Compute the address of the leftmost word in the topmost line
     * to be filled, and the rightmost word in the topmost line to
     * be filled.
     */
    
    left = raster->ras_bits +
	((raster->ras_height-1) - area->r_ytop)*raster->ras_intsPerLine;
    right = left + area->r_xtop/32;
    left += area->r_xbot/32;

    /* Divide the x-span of the area into three parts:  the leftmost
     * word, of which only the rightmost bits are modified, the
     * rightmost word, of which only the leftmost bits are modified,
     * and the middle section, in which all bits of each word are
     * modified.  Compute masks that determine which bits of the end
     * words will be modified.  There's a special case when the left
     * and right ends are in the same word.
     */
    
    leftMask = rightBits[area->r_xbot&037];
    rightMask = leftBits[area->r_xtop&037];
    if (left == right)
	leftMask &= rightMask;
    
    /* Process the stippled area one raster line at a time, top to bottom. */

    for (line = area->r_ytop; line >= area->r_ybot; line -= 1)
    {
	curStipple = stipple[(-line)&017];

	/* Fill the leftmost word on this line. */

	*left |= curStipple & leftMask;

	if (left != right)
	{
	    /* Fill the center of the line. */

	    for (cur = left+1; cur < right; cur += 1)
		*cur |= curStipple;
	    
	    /* Fill the rightmost word on this line. */

	    *cur |= curStipple & rightMask;
	}

	left += raster->ras_intsPerLine;
	right += raster->ras_intsPerLine;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotDumpRaster  --
 *
 * 	Writes out the contents of the given raster to the given file,
 *	in binary format.
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
PlotDumpRaster(raster, file)
    Raster *raster;		/* Raster to be dumped. */
    FILE *file;			/* File in which to dump it. */
{
    int count;

    count = write(fileno(file), (char *) raster->ras_bits,
	    raster->ras_bytesPerLine*raster->ras_height);
    if (count < 0)
    {
	TxError("I/O error in writing raster file:  %s.\n",
		strerror(errno));
	return 1;
    }
    rasFileByteCount += count;
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotLoadFont --
 *
 * 	Loads a font into memory, if it isn't already there.
 *
 ** Patched to accommodate both VAX format and Sun format vfont(5) files,
 ** regardless of the native byte order of the processor.  J. Gealow 3/30/94
 *
 * Results:
 *	The return value is a pointer to the font.  This must be used
 *	when calling procedures like PlotTextSize.  If the font file
 *	couldn't be found, an error message is output and NULL is
 *	returned.
 *
 * Side effects:
 *	New memory gets allocated.
 *
 * ----------------------------------------------------------------------------
 */

RasterFont *
PlotLoadFont(name)
    char *name;			/* Name of font file. */
{
    FILE *f;
    RasterFont *new;
    struct dispatch *d;

    /* See if we've already got the font. */

    for (new = PlotFontList; new != NULL; new = new->fo_next)
    {
	if (strcmp(new->fo_name, name) == 0)
	    return new;
    }

    f = PaOpen(name, "r", (char *) NULL, ".", SysLibPath, (char **) NULL);
    if (f == NULL)
    {
	TxError("Couldn't read font file \"%s\".\n", name);
	return NULL;
    }

    new = (RasterFont *) mallocMagic(sizeof(RasterFont));
    new->fo_name = NULL;
    StrDup(&new->fo_name, name);

    /* Read in the font's header and check the magic number. */

    if (read(fileno(f), (char *) &new->fo_hdr, sizeof(new->fo_hdr))
	    != sizeof(new->fo_hdr))
    {
	fontError:
	TxError("Error in reading font file \"%s\".\n", name);
	fclose(f);
	return NULL;
    }
    if (PlotSwapShort(new->fo_hdr.magic) == 0436)
    {
	new->fo_hdr.size = (PlotSwapShort(new->fo_hdr.size));
	new->fo_hdr.maxx = (PlotSwapShort(new->fo_hdr.maxx));
	new->fo_hdr.maxy = (PlotSwapShort(new->fo_hdr.maxy));
	new->fo_hdr.xtend = (PlotSwapShort(new->fo_hdr.xtend));
    } 
    else if (new->fo_hdr.magic != 0436)
    {
	TxError("Bad magic number in font file \"%s\".\n", name);
	fclose(f);
	return NULL;
    }

    /* Read the character descriptors and the bit map. */

    if (read(fileno(f), (char *) new->fo_chars, sizeof(new->fo_chars))
	    != sizeof(new->fo_chars))
	goto fontError;
    new->fo_bits = mallocMagic(new->fo_hdr.size);
    if (read(fileno(f), new->fo_bits, (unsigned) new->fo_hdr.size)
	    != new->fo_hdr.size)
	goto fontError;
    fclose(f);

    /* Compute the bounding box of all characters in the font. */

    new->fo_bbox.r_xbot = new->fo_bbox.r_xtop = 0;
    new->fo_bbox.r_ybot = new->fo_bbox.r_ytop = 0;
    for (d = &new->fo_chars[0]; d < &new->fo_chars[256]; d++)
    {
	if (PlotSwapShort(new->fo_hdr.magic) == 0436)
	{
	    d->addr = PlotSwapShort(d->addr);
	    d->nbytes = PlotSwapShort(d->nbytes);
	    d->width = PlotSwapShort(d->width);
	}
	if (d->nbytes == 0) continue;
	if (d->up > new->fo_bbox.r_ytop)
	    new->fo_bbox.r_ytop = d->up;
	if (d->down > new->fo_bbox.r_ybot)
	    new->fo_bbox.r_ybot = d->down;
	if (d->right > new->fo_bbox.r_xtop)
	    new->fo_bbox.r_xtop = d->right;
	if (d->left > new->fo_bbox.r_xbot)
	    new->fo_bbox.r_xbot = d->left;
#ifdef	DEBUG_FONT
	if (d == &new->fo_chars['d'])
	{
	    char *fontcp;
	    int count;

	    TxError("Character 'd' in font '%s' is at addr 0x%x, bytes %d, width %d\n",
		 new->fo_name, d->addr, d->nbytes, d->width);
	    count = 0;
	    for (fontcp = new->fo_bits + d->addr; 
	      fontcp < new->fo_bits + d->addr + d->nbytes; fontcp++)
	    {
		int i;
		char ch;
		ch = *fontcp;
		for (i = 0; i < 8; i++)
		{
		    TxError("%c", ((ch & 0x80) ? 'X' : '.'));
		    ch = (ch << 1);
		}
		count++;
		if (count >= (d->left + d->right + 7) >> 3)
		{
		    TxError("\n");
		    count = 0;
		}
	    }
	    TxError("\n");
	}
#endif	/* DEBUG_FONT */
    }
    new->fo_bbox.r_xbot = - new->fo_bbox.r_xbot;
    new->fo_bbox.r_ybot = - new->fo_bbox.r_ybot;

    new->fo_next = PlotFontList;
    PlotFontList = new;
    return new;
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotTextSize --
 *
 * 	Compute the area that a string will occupy.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The rectangle "area" is filled in with the bounding
 *	box of the bits in "string", assuming that the origin
 *	for the text is (0,0) and the text is rendered in font
 *	"font".
 *
 * ----------------------------------------------------------------------------
 */

void
PlotTextSize(font, string, area)
    RasterFont *font;		/* Font to use in computing text size. */
    char *string;		/* String to compute size of. */
    Rect *area;	/* Place to store bounding box. */
{
    int x;
    struct dispatch *d;

    area->r_xbot = area->r_xtop = 0;
    area->r_ybot = area->r_ytop = 0;
    x = 0;
    
    for ( ; *string != 0; string ++)
    {
	if ((*string == ' ') || (*string == '\t'))
	    d = &font->fo_chars['t'];
	else d = &font->fo_chars[*string];
	if (d->nbytes == 0) continue;
	if (d->up > area->r_ytop)
	    area->r_ytop = d->up;
	if (d->down > area->r_ybot)
	    area->r_ybot = d->down;
	if ((x+d->right) > area->r_xtop)
	    area->r_xtop = x + d->right;
	if ((x-d->left) < area->r_ybot)
	    area->r_ybot = x - d->left;
	x += d->width;
    }
    area->r_ybot = -area->r_ybot;
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotRasterText --
 *
 * 	Given a text string and a font, this procedure scan-converts
 *	the string and sets bits in the current raster that correspond
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
PlotRasterText(raster, clip, font, string, point)
    Raster *raster;	/* Raster whose bits are to be filled in. */
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
{
    int xOrig;			/* X-origin for current character. */

    
    /* Outer loop:  process each character. */

    xOrig = point->p_x;
    for ( ; *string != 0; string++)
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
		int charBits, x;

		x = xOrig + j;
		if (x > clip->r_xtop) break;
		if (x < clip->r_xbot - 7) continue;

		rPtr = (char *) raster->ras_bits;
		rPtr += (raster->ras_height - 1 - y)*raster->ras_bytesPerLine
			+ (x>>3);
		charBits = *charBitPtr & 0xff;

		/* One byte of the character's bit map may span two
		 * bytes of the raster, so process each of the two
		 * raster bytes separately.  Either of the two bytes
		 * may be off the edge of the raster, in which case
		 * it must be skipped.
		 */
		
		if (x >= 0)
		    *rPtr |= charBits >> (x & 0x7);
		rPtr += 1;
		if (x+8 <= clip->r_xtop)
		{
		    charBits = charBits << (8 - (x & 0x7));
		    *rPtr |= charBits;
		}
	    }
	}

	xOrig += d->width;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotRastPoint --
 *
 * 	Sets a particular pixel of a raster.
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
PlotRastPoint(raster, x, y)
    Raster *raster;		/* Raster containing pixel to be
					 * filled.
					 */
    int x, y;				/* Coordinates of pixel. */
{
    if ((x < 0) || (x >= raster->ras_width)) return;
    y = (raster->ras_height - 1) - y;
    if ((y < 0) || (y >= raster->ras_height)) return;

    raster->ras_bits[((y*raster->ras_intsPerLine) + (x>>5))]
	    |= singleBit[x&037];
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotRastLine --
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
PlotRastLine(raster, src, dst)
    Raster *raster;		/* Where to render the line. */
    Point *src;			/* One endpoint of line, in raster coords. */
    Point *dst;			/* The other endpoint, in raster coords. */
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
	    PlotRastPoint(raster, x, y);
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
	    PlotRastPoint(raster, x, y);
	    if (d < 0)
		d += incr1;
	    else
	    {
		d += incr2;
		x += xinc;
	    }
	}
    }
    PlotRastPoint(raster, x, y);
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotRastFatLine --
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
 *	PlotFillRaster procedure.
 *
 * ----------------------------------------------------------------------------
 */

void
PlotRastFatLine(raster, src, dst, widen)
    Raster *raster;		/* Where to render the line. */
    Point *src;			/* One endpoint of line, in raster coords. */
    Point *dst;			/* The other endpoint, in raster coords. */
    int widen;			/* How much to widen the line.  0 means the
				 * line is one pixel wide, 1 means it's 3
				 * pixels wide, and so on.
				 */
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

	PlotRastLine(raster, &newSrc, &newDst);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotSwapBytes --
 * PlotSwapShort --
 *
 * 	These two procedures do byte swapping in the way that's
 *	required when moving binary files between VAXes and Suns.
 *
 * Results:
 *	Each procedure returns a value in which the order of bytes
 *	has been reversed.  PlotSwapBytes takes an integer and returns
 *	an integer with the four bytes in reverse order;  PlotSwapShort
 *	takes a short and swaps the two bytes.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
PlotSwapBytes(value)
    int value;				/* 4-byte Value whose bytes are to
					 * be reversed.
					 */
{
    int result;

#define src ((char *) &value)
#define dst ((char *) &result)

    dst[0] = src[3];
    dst[1] = src[2];
    dst[2] = src[1];
    dst[3] = src[0];

    return result;
}

short
PlotSwapShort(value)
    short value;		/* Value whose bytes are to be swapped. */
{
    short result;

#define src ((char *) &value)
#define dst ((char *) &result)

    dst[0] = src[1];
    dst[1] = src[0];

    return result;
}



/*
 * ----------------------------------------------------------------------------
 *
 * PlotDumpColorPreamble --
 *
 * 	Dump a color preamble in vdmpc format for the color Versatec.  See
 *	the vdmpc(5) man page for details on the format.
 *
 *	Format:  
 *	    preamble is a 1K block
 *	    first word is 0xA5CF4DFB (a magic number)
 *	    second word gives the number of scan lines
 *	    third word gives width of the plot in pixels (must be multiple of 8)
 *	    rest of the words are zero
 *	
 * ----------------------------------------------------------------------------
 */

#define VERSATEC_BLOCK          1024
#define VERSATEC_MAGIC_WORD     0xA5CF4DFB
unsigned int VersHeader[VERSATEC_BLOCK/sizeof(unsigned int)] = {VERSATEC_MAGIC_WORD};

int
PlotDumpColorPreamble(color, file, lines, columns)
    VersatecColor color;	/* The color that the following raster will 
				 * be printed in.
				 */
    FILE *file;			/* file in which to place header */
    int lines;			/* number of scan lines */
    int columns;		/* Width in pixels. */
{

    int count;

    if (color == BLACK)
    {
	VersHeader[1] = lines;
	VersHeader[2] = columns;
	count = write(fileno(file), &VersHeader[0], sizeof(VersHeader));
	TxPrintf("Wrote %d bytes of control.\n", count);
    }
    return 0;
}
