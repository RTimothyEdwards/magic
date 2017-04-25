/* grCmap.c -
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
 * This file provides routines that manipulate the color map on behalf
 * of the magic design system.
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/graphics/grCMap.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/malloc.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "graphics/graphicsInt.h"
#include "utils/utils.h"
#include "textio/textio.h"

static colorEntry *colorMap = NULL;	/* Storage for the color map. */
int GrNumColors = 0;			/* Number of colors */


/*-----------------------------------------------------------------------------
 *
 * GrResetCMap --
 *
 *	Free memory associated with the colormap in preparation for
 *	rereading the colormap file or reading a different colormap file.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Currently does nothing;  size of colormap structures are fixed.
 *
 *-----------------------------------------------------------------------------
 */

void
GrResetCMap()
{
    int i;
    colorEntry *ce;

    if (colorMap == NULL || GrNumColors == 0) return;

    for (i = 0; i < GrNumColors; i++)
    {
	ce = colorMap + i;
	if (ce->name != NULL)
	    freeMagic(ce->name);
    }
    freeMagic(colorMap);
    colorMap = NULL;
    GrNumColors = 0;
}


/*-----------------------------------------------------------------------------
 * GrReadCmap:
 *
 *	This routine initializes the color map values from the data
 *	given in a file.
 *
 * Results:
 *	The return value is TRUE if the color map was successfully
 *	loaded, and FALSE otherwise.
 *
 * Side Effects:
 *	The color map is read from the file and loaded into the graphics
 *	display.  The name of the color map file is x.y.z.cmap, where
 *	x is techStyle, y is dispType, and z is monType.
 *
 * Design:
 *	The format of the file is one or more lines of the form
 *	<red> <green> <blue> <max location>.  When the first line
 *	is read in, the given red, green, and blue values are used
 *	to fill locations 0 - <max location> in the color map.  Then
 *	the next line is used to fill from there to the next max location,
 *	which must be larger than the first, and so on.  The last
 *	<max location> is expected to be 255.
 *-----------------------------------------------------------------------------
 */

bool
GrReadCMap(techStyle, dispType, monType, path, libPath)
char *techStyle;		/* The type of dstyle file requested by
				 * the current technology.
				 */
char *dispType;			/* A class of color map files for one or
				 * more display types.  Usually this
				 * is defaulted to NULL, in which case the
				 * type required by the current driver is
				 * used.  If the current driver lists a
				 * NULL type, it means no color map is
				 * needed at all (it's a black-and-white
				 * display), so nothing is loaded.
				 */
char *monType;			/* The class of monitors being used.  Usually
				 * given as "std".
				 */
char *path;			/* a search path */
char *libPath;			/* a library search path */

{
    FILE *f;
    int max, red, green, blue, newmax, argc, i;
    colorEntry *ce;
    char fullName[256], inputLine[128], colorName[100];

    if (dispType == NULL)
    {
	if (grCMapType == NULL) return TRUE;
	dispType = grCMapType;
    }
    (void) sprintf(fullName, "%.80s.%.80s.%.80s", techStyle,
	    dispType, monType);

    f = PaOpen(fullName, "r", ".cmap", path, libPath, (char **) NULL);
    if (f == NULL)
    { 
	/* Check for original ".cmap1" file (prior to magic v. 7.2.27) */
	f = PaOpen(fullName, "r", ".cmap1", path, libPath, (char **) NULL);
	if (f == NULL)
	{
	    TxError("Couldn't open color map file \"%s.cmap\"\n", fullName);
	    return FALSE;
	}
    }

    /* Reset original colormap if necessary */
    GrResetCMap();

    /* Get maximum color entry from file (1st pass) */

    max = 0;
    while (fgets(inputLine, 128, f) != NULL)
    {
	argc = sscanf(inputLine, "%*d %*d %*d %d", &newmax);
	if (argc == 0)
	{
	    /* Allow comment lines */
	    if (*inputLine == '#') continue;

	    TxError("Syntax error in color map file \"%s.cmap\"\n", fullName);
	    TxError("Last color read was index %d\n", max);
	    return FALSE;
	}
	else
	{
	    if (newmax > max)
		max = newmax;
	}
    }
    rewind(f);
    colorMap = (colorEntry *)mallocMagic((max + 1) * sizeof(colorEntry));
    GrNumColors = max + 1;

    /* Read data into colorMap (2nd pass) */

    for (i = 0; i < GrNumColors; )
    {
	if (fgets(inputLine, 128, f) == NULL)
	{
	    TxError("Premature end-of-file in color map file \"%s.cmap\"\n",
			fullName);
	    break;
	}
	if ((argc = sscanf(inputLine, "%d %d %d %d %99[^\n]",
		&red, &green, &blue, &newmax, colorName)) < 4)
	{
	    /* Allow comment lines */
	    if (*inputLine == '#') continue;

	    TxError("Syntax error in color map file \"%s.cmap\"\n", fullName);
	    TxError("Expecting to read color index %d\n", i);
	    break;
	}
	else if (newmax < i)
	{
	    TxError("Colors in map are out of order!\n");
	    break;
	}

	for (; i <= newmax; i++)
	{
	    ce = colorMap + i;
	    ce->red = (unsigned char)(red & 0xff);
	    ce->green = (unsigned char)(green & 0xff);
	    ce->blue = (unsigned char)(blue & 0xff);
	    if (argc == 5)
		ce->name = StrDup(NULL, colorName);
	    else
		ce->name = NULL;
	}
    }

    fclose(f);
    if (i < GrNumColors) return FALSE;

    GrSetCMap();
    return TRUE;
}


/*-----------------------------------------------------------------------------
 * GrSaveCMap
 *
 *	CMSave will save the current contents of the color map in a file
 *	so that it can be read back in later with GrLoadCMap.
 *
 * Results:
 *	TRUE is returned if the color map was successfully saved.  Otherwise
 *	FALSE is returned.  The file that's actually modified is x.y.z.cmap,
 *	where x is techStyle, y is dispType, and z is monType.
 *
 * Side Effects:
 *	The file is overwritten with the color map values in the form
 *	described above under GrLoadCMap.
 *-----------------------------------------------------------------------------
 */

bool
GrSaveCMap(techStyle, dispType, monType, path, libPath)
char *techStyle;		/* The type of dstyle file requested by
				 * the current technology.
				 */
char *dispType;			/* A class of color map files for one or
				 * more display types.  Usually this
				 * is defaulted to NULL, in which case the
				 * type required by the current driver is
				 * used.
				 */
char *monType;			/* The class of monitors being used.  Usually
				 * given as "std".
				 */
char *path;			/* a search path */
char *libPath;			/* a library search path */

{
    FILE *f;
    colorEntry *ce, *ce2;
    int red, green, blue;
    int i;
    char fullName[256];

    if (dispType == NULL) dispType = grCMapType;
    (void) sprintf(fullName, "%.80s.%.80s.%.80s", techStyle,
	    dispType, monType);
    f = PaOpen(fullName, "w", ".cmap", path, libPath, (char **) NULL);
    if (f == NULL)
    {
	TxError("Couldn't write color map file \"%s.cmap\"\n", fullName);
	return FALSE;
    }

    for (i = 0; i < GrNumColors; i++)
    {
	ce = colorMap + i;
	red = (int)ce->red;
	green = (int)ce->green;
	blue = (int)ce->blue;

	while (i < (GrNumColors - 1))
	{
	    ce2 = colorMap + i + 1;
	    if ((red != (int)ce2->red) || (green != (int)ce2->green)
			|| (blue != (int)ce2->blue))
		break;
	    i++;
	}

	fprintf(f, "%d %d %d %d", red, green, blue, i);
	if (ce->name != NULL)
	    fprintf(f, " %s", ce->name);
	fprintf(f, "\n");
    }
    (void) fclose(f);
    return TRUE;
}

/*
 *-----------------------------------------------------------------------------
 * GrNameToColor --
 *
 *	Get the index of a specific named color, as named in the
 *	colormap file.
 *
 * Results:
 *	The index of the named color, or -1 if there is no match.
 *
 * Side Effects:
 *	None.
 *
 *-----------------------------------------------------------------------------
 */

int
GrNameToColor(colorname)
    char *colorname;
{
    int i;
    colorEntry *ce;

    for (i = 0; i < GrNumColors; i++)
    {
	ce = colorMap + i;
	if (ce->name == NULL) continue;
	else if (!strcmp(colorname, ce->name))
	    return i;
    }
    return -1;
}

/*
 *-----------------------------------------------------------------------------
 * GrGetColor --
 *	reads a color value out of the map.
 *
 * Results:
 *	TRUE on success, FALSE on passing an illegal color index.
 *
 * Side Effects:
 *	The values of red, green, and blue are overwritten with the
 *	red, green, and blue intensities for the color indicated by
 *	layer.
 *-----------------------------------------------------------------------------
 */

bool
GrGetColor(color, red, green, blue)
    int color;			/* Color to be read. */
    int *red, *green, *blue;	/* Pointers to values of color elements. */
{
    colorEntry *ce;

    if (color >= GrNumColors) return FALSE;

    ce = colorMap + color;
    *red = (int)ce->red;
    *green = (int)ce->green;
    *blue = (int)ce->blue;
    return TRUE;
}


/*-----------------------------------------------------------------------------
 *  GrPutColor --
 *	 modifies the color map values for a single layer spec.
 *
 *  Results:
 *	TRUE on success, FALSE on passing an illegal color index.
 *
 *  Side Effects:
 *	The indicated color is modified to have the given red, green, and
 *	blue intensities.
 *-----------------------------------------------------------------------------
 */

bool
GrPutColor(color, red, green, blue)
    int color;			/* Color to be changed. */
    int red, green, blue;	/* New intensities for color. */
{
    colorEntry *ce;

    if (color >= GrNumColors) return FALSE;

    ce = colorMap + color;
    ce->red = (unsigned char)(red & 0xff);
    ce->green = (unsigned char)(green & 0xff);
    ce->blue = (unsigned char)(blue & 0xff);
    if (ce->name != NULL)
    {
	freeMagic(ce->name);
	ce->name = NULL;
    }
    GrSetCMap();
    return TRUE;
}


/*-----------------------------------------------------------------------------
 * GrPutManyColors --
 *
 *	Stores a new set of intensities in all portions of the map
 *	which contain a given set of color bits.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	The color map entries for all layer combinations containing at
 *	least the bits in layers are reset to have the values given by
 *	red, green, and blue.  For example, if layers is 1 then all odd
 *	map entries are changed.  If layers is 3, every fourth map entry
 *	is changed, and if layers is 0 then all entries are changed.  This
 *	routine is a little tricky because if an opaque layer is present,
 *	then the six mask color bits must match exactly, since the opaque
 *	layer obscures ones beneath it.
 *-----------------------------------------------------------------------------
 */

void
GrPutManyColors(color, red, green, blue, opaqueBit)
   int color;			/* A specification of colors to be modified. */
   int red, green, blue;	/* New intensity values. */
   int opaqueBit;		/* The opaque/transparent bit.  It is assumed
				 * that the opaque layer colors or 
				 * transparent layer bits lie to the right
				 * of the opaque/transparent bit.
				 */

{
    int i;
    int mask = color;

    /* if a transparent layer */
    if (color & (opaqueBit + opaqueBit - 1)) mask |= opaqueBit;

    /* if a opaque layer */
    if (color & opaqueBit) mask |= (opaqueBit - 1);

    for (i = 0; i < GrNumColors; i++)
	if ((i & mask) == color)
	    (void) GrPutColor(i, red, green, blue);

    GrSetCMap();
}
