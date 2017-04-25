/* grDStyle.c -
 *
 *	Parse and read in the display style file.
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
 * Portions of this code are Copyright (C) 2003 Open Circuit Design, Inc.,
 * for MultiGiG Ltd.
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/graphics/grDStyle.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "utils/magic.h"
#include "utils/malloc.h"
#include "utils/styles.h"
#include "utils/utils.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "graphics/glyphs.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "graphics/graphicsInt.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "dbwind/dbwind.h"

/* imports from other graphics files */
extern void (*grSetSPatternPtr)();
extern void (*grDefineCursorPtr)();

/* data structures local to this file only */

/* Define a linked-list record to keep track of styles prior    */
/* to allocating the fixed array GrStyleTable.                  */

typedef struct _dstylelink {  
    GR_STYLE_LINE       style;
    char		shortname;
    struct _dstylelink  *next;
} dstylelink;

/* data structures local to the graphics module */

extern int grBitPlaneMask;	/* Mask of the valid bit-plane bits. */

static GrGlyphs *grCursorGlyphs = NULL;
static dstylelink *dstylehead = NULL;

global int **GrStippleTable = NULL;
global int grNumStipples = 0;

/* MUST be the same indices as the constants in graphicsInt.h */
char *fillStyles[] = {
	"solid",
	"cross",
	"outline",
	"stipple",
	"grid",
	NULL };



/* Internal constants for each section of the style file. */
/* These are bitmask-mapped so the display style reader	  */
/* can check that the section are read in proper order.	  */

#define IGNORE		-1
#define	DISP_STYLES	1
#define	LAYOUT_STYLES	2
#define	PALE_STYLES	4
#define	STIPPLES	8
#define DISP_VERSION	16

#define	STRLEN	200

/* Global variables to export to other modules */

int GrStyleNames[128];		/* short names for styles */
GR_STYLE_LINE *GrStyleTable;



bool
GrDrawGlyphNum(num, xoff, yoff)
    int num;
    int xoff;
    int yoff;
{
    Point p;

    p.p_x = xoff;
    p.p_y = yoff;
    if (num >= grCursorGlyphs->gr_num) return FALSE;
    GrDrawGlyph(grCursorGlyphs->gr_glyph[num], &p);
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * GrGetStyleFromName --
 *
 * Return an integer style number given a "long name".
 *
 * Results:
 *	Integer style number, or -1 if no style matches the name.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
GrGetStyleFromName(stylename)
    char *stylename;
{
    int style;
    int maxstyles = TECHBEGINSTYLES + (DBWNumStyles * 2);

    for (style = 0; style < maxstyles; style++)
        if (GrStyleTable[style].longname != NULL)
            if (!strcmp(stylename, GrStyleTable[style].longname))
                break;                          

    return (style == maxstyles) ? -1 : style;
}

/*
 * ----------------------------------------------------------------------------
 *
 * GrResetStyles --
 *
 * Free memory associated with the display styles in preparation for
 * re-reading the styles file.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocated memory free'd.
 *
 * ----------------------------------------------------------------------------
 */

void
GrResetStyles()
{
    int i;

    if (DBWNumStyles == 0) return;	/* Nothing has been allocated yet */

    for (i = 0; i < TECHBEGINSTYLES + (DBWNumStyles * 2); i++)
	if (GrStyleTable[i].longname != NULL)
	    freeMagic(GrStyleTable[i].longname);

    freeMagic((char *)GrStyleTable);
    GrStyleTable = NULL;
    DBWNumStyles = 0;
}


/*
 * ----------------------------------------------------------------------------
 * styleBuildDisplayStyle:
 *
 *	Take one line of the display_styles section and process it.
 *
 * Results:
 *	True if things worked, false otherwise.
 *
 * Side effects:
 *	none
 * ----------------------------------------------------------------------------
 */

bool
styleBuildDisplayStyle(line, version)
    char *line;
    int version;
{
    bool res;
    int argsread;
    int ord = 1, mask, color, outline, nfill, stipple;
    char shortName, longName[52];
    char fill[42], ordstr[12], colorName[30];
    dstylelink *newstyle, *sstyle;

    char v6scanline[] = "%10s %o %29s %o %40s %d %c %50s";
    char v7scanline[] = "%10s %i %29s %i %40s %d %c %50s";

    char *scanline = (version > 6) ? v7scanline : v6scanline;

    res = TRUE;

    if ((argsread = sscanf(line, scanline,
		ordstr, &mask, colorName, &outline, fill, &stipple,
		&shortName, &longName)) < 7)
    {
	res = FALSE;
    }
    else
    {
	newstyle = (dstylelink *)mallocMagic(sizeof(dstylelink));
	newstyle->next = dstylehead;
	dstylehead = newstyle;		/* note that dstylehead has styles
					 * in reverse order.
					 */

	/* Allow colors to be specified by index or by name */
	if (sscanf(colorName, ((version > 6) ? "%i" : "%o"), &color) == 0)
	    color = GrNameToColor(colorName);

	newstyle->style.mask = (mask & grBitPlaneMask);
	newstyle->style.color = (color & grBitPlaneMask);
	newstyle->style.outline = outline;
	if (StrIsInt(ordstr))
	{
	    newstyle->style.idx = atoi(ordstr);
	    if (newstyle->style.idx > ord)
		ord = newstyle->style.idx + 1;
	}
	else
	    newstyle->style.idx = ord++;
	nfill = LookupFull(fill, fillStyles);
	if (nfill < 0)
	    res = FALSE;
	newstyle->style.fill = nfill;
	newstyle->style.stipple = stipple;

	/* Add short style name reverse lookup table entry */
	newstyle->shortname = shortName & 0x7f;

	if (argsread == 8)
	    newstyle->style.longname = StrDup(NULL, longName);
	else
	    newstyle->style.longname = NULL;
    }
    return(res);
}


/*
 * ----------------------------------------------------------------------------
 * styleBuildStippleStyle:
 *
 *	Take one line of the stipples section and process it.
 *
 * Results:
 *	True if things worked, false otherwise.
 *
 * Side effects:
 *	none
 * ----------------------------------------------------------------------------
 */

bool
styleBuildStipplesStyle(line, version)
    char *line;
    int version;
{
    bool res;
    int ord;
    int row[8];

    char v6scanline[] = "%d %o %o %o %o %o %o %o %o";
    char v7scanline[] = "%d %x %x %x %x %x %x %x %x";

    char *scanline = (version > 6) ? v7scanline : v6scanline;

    res = TRUE;

    if (sscanf(line, scanline,
	    &ord, &(row[0]), &(row[1]), &(row[2]), &(row[3]), 
	    &(row[4]), &(row[5]), &(row[6]), &(row[7]) ) != 9) 
    {
	res = FALSE;
    }
    else
    {
	if (ord < 0)
	    res = FALSE;
	else
	{
	    int i, j, ns, **sttable;

	    ns = MAX(grNumStipples, ord + 1);
	    if (ns > grNumStipples)
	    {
		/* Grab in blocks of 8 to avoid over-use of malloc. . . */
		if (ns < (grNumStipples + 8)) ns = grNumStipples + 8;

		sttable = (int **)mallocMagic(ns * sizeof(int *));
	  	for (i = 0; i < grNumStipples; i++)
		    sttable[i] = GrStippleTable[i];
		for (; i < ns; i++)
		{
		    sttable[i] = (int *)mallocMagic(8 * sizeof(int));
		    for (j = 0; j < 8; j++)
			sttable[i][j] = 0;
		}
		if (GrStippleTable) freeMagic((char *)GrStippleTable);
		GrStippleTable = sttable;
		grNumStipples = ns;
	    }
	    for (i = 0; i < 8; i++)
		GrStippleTable[ord][i] = row[i];
	}
    }
    return(res);
}


/*
 * ----------------------------------------------------------------------------
 * GrLoadCursors --
 *
 *	Loads the graphics cursors from a given file.  There must not be
 *	any window locks set, as this routine may need to write to the
 *	display.
 *
 * Results:
 *	True if things worked, false otherwise.
 *
 * Side effects:
 *	Cursor patterns are loaded, which may involve writing to the
 *	display.  The file from which the cursors are read is determined
 *	by combining grCursorType (a driver-dependent string) with a
 *	".glyphs" extension.
 * ----------------------------------------------------------------------------
 */

bool
GrLoadCursors(path, libPath)
char *path;
char *libPath;
{
    if (grCursorGlyphs != (GrGlyphs *) NULL)
    {
	GrFreeGlyphs(grCursorGlyphs);
	grCursorGlyphs = (GrGlyphs *) NULL;
    }

    if (!GrReadGlyphs(grCursorType, path, libPath, &grCursorGlyphs))
    {
	return FALSE;
    }

    if (grDefineCursorPtr == NULL)
	TxError("Display does not have a programmable cursor.\n");
    else 
	(*grDefineCursorPtr)(grCursorGlyphs);

    return TRUE;  
}


/*
 * ----------------------------------------------------------------------------
 * GrLoadStyles:
 *
 *	Reads in a display style file.  This has the effect of setting the
 *	box styles and stipple patterns.
 *
 * Results:
 *	-1 if the file contained a format error.
 *	-2 if the file was not found.
 *	0 if everything went OK.
 *
 * Side effects:
 *	global variables are changed.
 * ----------------------------------------------------------------------------
 */

int
GrLoadStyles(techType, path, libPath)
char *techType;			/* Type of styles wanted by the technology
				 * file (usually "std").  We tack two things
				 * onto this name:  the type of styles
				 * wanted by the display, and a version
				 * suffix.  This three-part name is used
				 * to look up the actual display styles
				 * file.
				 */
char *path;
char *libPath;
{
    FILE *inp;
    int res = 0;
    int i, scount, processed = DISP_VERSION;
    char fullName[256];
    dstylelink *sstyle;
    int MaxTechStyles = 0, MaxTileStyles = 0;

    /* The dstyle file format version number was buried in the filename */
    /* prior to version 6.  Therefore versions which do not have a	*/
    /* "version" keyword in the file should default to version 5.	*/
    int version = 5;

    /* Reset number of styles to zero */
    GrResetStyles();

    for (i = 0; i < 128; i++) GrStyleNames[i] = 0;

    (void) sprintf(fullName, "%.100s.%.100s.dstyle", techType, grDStyleType);

    inp = PaOpen(fullName, "r", (char *) NULL, path, libPath, (char **) NULL);
    if (inp == NULL)
    {
	/* Try old format ".dstyle5"? */
        (void) sprintf(fullName, "%.100s.%.100s.dstyle5", techType, grDStyleType);
	inp = PaOpen(fullName, "r", (char *) NULL, path, libPath, (char **) NULL);
	if (inp == NULL)
	{
	    TxError("Couldn't open display styles file \"%s\"\n", fullName);
            return(-2);
	}
    }
    else
    {
	char line[STRLEN], sectionName[STRLEN];
	char *sres;
	bool newSection = FALSE;
        int section;

	while (TRUE)
	{
	    sres = fgets(line, STRLEN, inp);
	    if (sres == NULL) break;
	    if (StrIsWhite(line, FALSE)) 
		newSection = TRUE;
	    else if (line[0] == '#')
	    {
		/* comment line */
	    }
	    else if (newSection)
	    {
		if (sscanf(line, "%s", sectionName) != 1)
		{
		    TxError("File contained format error: " 
			    "unable to read section name.\n");
		    res = -1;
		}
		if (strcmp(sectionName, "version") == 0)
		{
		    if (sscanf(line, "%*s %d", &version) != 1)
		    {
		        TxError("DStyle format version could not be "
				"read: assuming version 6\n");
			version = 6;
		    }
		    section = DISP_VERSION;
		}
		else if (strcmp(sectionName, "display_styles") == 0)
		{
		    int locbitplanes;

		    if ((processed & (LAYOUT_STYLES | PALE_STYLES)) != 0)
		    {
			TxError("DStyle sections out of order: display_styles must "
				"come before layout_styles and pale_styles\n");
			res = -1;
		    }
		    section = DISP_STYLES;
		    scount = 0;
		}
		else if (strcmp(sectionName, "layout_styles") == 0)
		{
		    if ((processed & PALE_STYLES) != 0)
		    {
			TxError("DStyle sections out of order: layout_styles must "
				"come before pale_styles\n");
			MainExit(1);
		    }
		    section = LAYOUT_STYLES;
		    if (scount < TECHBEGINSTYLES)
		    {
			TxError("Error: Display style file defines only %d of "
				"%d required internal styles.\n", scount,
				TECHBEGINSTYLES);
		    }
		    else if (scount > TECHBEGINSTYLES)
		    {
			TxError("Error: Display style file defines too many (%d) "
				"internal styles; should be %d.\n", scount,
				TECHBEGINSTYLES);
		    }
		    scount = 0;
		}
		else if (strcmp(sectionName, "pale_styles") == 0)
		{
		    section = PALE_STYLES;
		    MaxTechStyles = scount + TECHBEGINSTYLES;
		    scount = 0;
		}
		else if (strcmp(sectionName, "stipples") == 0)
		{
		    section = STIPPLES;
		    if (grNumStipples > 0)
		    {
			while (grNumStipples > 0)
			    freeMagic((char *)GrStippleTable[--grNumStipples]);
			freeMagic((char *)GrStippleTable);
			GrStippleTable = NULL;
		    }
		}
		else
		{
		    if (StrIsInt(sectionName))
		    {
			TxError("Unexpected empty line in .dstyle file.\n");
			newSection = FALSE;
			goto recovery;
		    }
		    TxError("Bad section name \"%s\" in .dstyle file.\n",
			sectionName);
		    section = IGNORE;
		}
		newSection = FALSE;
	        processed |= section;
	    }
	    else
	    {
		int newres = TRUE;

recovery:
		switch (section)
		{
		    case LAYOUT_STYLES:
		    case PALE_STYLES:
		    case DISP_STYLES:
			newres = styleBuildDisplayStyle(line, version);
			scount++;
			break;
		    case STIPPLES:
			newres = styleBuildStipplesStyle(line, version);
			break;
		    case DISP_VERSION:
		    case IGNORE:
			break;
		    default:
			TxError("Internal error in GrStyle\n");
			break;
		}
		if (!newres)
		{
		    TxError("Style line contained format error: %s", line);
		    res = -1;
		}
	    }
	}
    }
    if (fclose(inp) == EOF)
	TxError("Could not close styles file.\n");

    if ((processed | STIPPLES) != (LAYOUT_STYLES | DISP_STYLES
		| PALE_STYLES | STIPPLES | DISP_VERSION))
    {
	TxError("Not all required style sections were read.  Missing"
		" sections are:");
	if (!(processed & DISP_STYLES))
	    TxError(" display_styles");
	if (!(processed & LAYOUT_STYLES))
	    TxError(" layout_styles");
	if (!(processed & PALE_STYLES))
	    TxError(" pale_styles");
	if (!(processed & DISP_VERSION))
	    TxError(" version");
	TxError("\n");
	res = -1;
    }
    else
    {
	if (grSetSPatternPtr)
	    (*grSetSPatternPtr)(GrStippleTable, grNumStipples);

	if ((MaxTechStyles - TECHBEGINSTYLES) != scount)
	{
	    TxError("Error:  Number of pale styles (%d) is different from "
			"the number of layout styles (%d)\n",
			scount, MaxTechStyles - TECHBEGINSTYLES);
	    res = -1;
	}
	else
	{
	    DBWNumStyles = scount;
	    MaxTileStyles = MaxTechStyles + scount;

	    GrStyleTable = (GR_STYLE_LINE *)mallocMagic(MaxTileStyles *
		sizeof(GR_STYLE_LINE));

	    /* Fill in table backwards, since linked list is a stack */
	    sstyle = dstylehead;
	    for (i = MaxTileStyles - 1; i >= 0; i--)
	    {
		if (sstyle == NULL)
		{
		    GrStyleTable[i].longname = NULL;
		    break;
		}
		else
		{
		    GrStyleTable[i] = sstyle->style;
		    /* Add short style name reverse lookup table entry */
		    GrStyleNames[(int)(sstyle->shortname)] = i;
		    freeMagic(sstyle);
		    sstyle = sstyle->next;
		}
	    }
	    dstylehead = NULL;
	}
    }
    if (res != 0) GrResetStyles();
    return(res);
}
