
/* grGlyphs.c -
 *
 *	Handle glyphs -- reading and display.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/graphics/grGlyphs.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/utils.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "graphics/graphicsInt.h"
#include "graphics/glyphs.h"
#include "textio/textio.h"
#include "utils/malloc.h"

#define STRLEN	500

extern void (*grFreeCursorPtr)();


/*
 * ----------------------------------------------------------------------------
 * GrFreeGlyphs --
 *
 *	Free a malloc'ed glyph structure.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

void
GrFreeGlyphs(g)
    GrGlyphs *g;
{
    int i;
    ASSERT(g != NULL, "GrFreeGlyphs");

    if (grFreeCursorPtr != NULL)
	(*grFreeCursorPtr)(g);


    for (i = 0; i < g->gr_num; i++)
    {
	if ((g->gr_glyph[i]->gr_cache != (ClientData) NULL) &&
	    (g->gr_glyph[i]->gr_free != NULL))
	{
	    (*(g->gr_glyph[i]->gr_free))(g->gr_glyph[i]->gr_cache);
	}
	freeMagic( (char *) g->gr_glyph[i]);
    }
    freeMagic( (char *) g);
}


/*
 * ----------------------------------------------------------------------------
 * GrReadGlyphs --
 *
 *	Read a set of glyphs from the specified file.
 *
 * Results:
 *	TRUE if it worked.
 *
 * Side effects:
 *	A structure is malloc'ed and the last argument pointed to it.
 *	Error messages are printed if there is any problem reading
 *	the glyphs file.
 * ----------------------------------------------------------------------------
 */

bool
GrReadGlyphs(filename, path, libPath, gl)
    char *filename;
    char *path, *libPath;	/* paths to search in for the file */
    GrGlyphs **gl;		/* To be filled in with a malloc'ed structure 
				 * This structure must be freed by the caller
				 * if it is not to live forever.
				 */
{
    FILE *file;
    bool result = TRUE;
    GrGlyphs *ourgl = NULL;
    int x, y, glyphnum;
    int xmax, ymax, glyphnummax;
    char line[STRLEN], *fullname;
    bool sizeline = FALSE;

    file = PaOpen(filename, "r", ".glyphs", path, libPath, &fullname);
    if (file == NULL)
    {
	TxError("Couldn't read glyphs file \"%s.glyphs\"\n", filename);
	result = FALSE;
	goto endit;
    }
    y = glyphnum = glyphnummax = xmax = ymax = -1;

    while (TRUE)
    {
	char *sres;
	sres = fgets(line, STRLEN, file);
	if (sres == NULL)  /* end of file */
	{
	    if ((y == 0) && (glyphnum == glyphnummax))
		result = TRUE;
	    else
	    {
		TxError("Unexpected end of file in file '%s'\n", fullname);
		result = FALSE;
	    }
	    goto endit;
	}
	else if (!StrIsWhite(line, TRUE))
	{
	    char *cp;
	    /* an interesting line */
	    if (sizeline)
	    {
		if (y > 0)
		    y--; 	/* scan from top down */
		else
		{
		    y = ymax;
		    glyphnum++;
		    if (glyphnum > glyphnummax)
		    {
			TxError("Extra lines at end of glyph file '%s'\n", 
			    fullname);
			result = TRUE;
			goto endit;
		    }
		}
		cp = line;

		for (x = 0; x <= xmax; x++)
		{
		    char trailingChar;

		    if (isspace(*cp))
		    {
			TxError("Error in glyph file '%s', %s\n ", fullname,
			    "white space is not a valid glyph character");
			TxError("Line in error is '%s'\n", line);
			result = FALSE;
			goto endit;
		    }
		    ourgl->gr_glyph[glyphnum]->gr_pixels[x + (xmax+1) * y] = 
			    GrStyleNames[ (*cp) & 127 ];

		    cp++;
		    trailingChar = *cp;
		    if (trailingChar == '*')
		    {
			ourgl->gr_glyph[glyphnum]->gr_origin.p_x = x;
			ourgl->gr_glyph[glyphnum]->gr_origin.p_y = y;
		    }

		    if (x != xmax)
		    {
			cp++;
			if ((trailingChar == '\0') || (*cp == '\0'))
			{
			    TxError("Error in glyph file '%s', %s\n ", 
				fullname, "line is too short.");
			    TxError("Line in error is '%s'\n", line);
			    result = FALSE;
			    goto endit;
			}
		    }
		}
	    }
	    else
	    {
		int i;
		if (sscanf(line, "size %d %d %d\n", 
			&glyphnummax, &xmax, &ymax) != 3)
		{
		    TxError("Format error in  file '%s'\n", fullname);
		    result = FALSE;
		    goto endit;
		}
		sizeline = TRUE;
		glyphnummax--;
		xmax--;
		ymax--;
		glyphnum = 0;
		x = 0;
		y = ymax + 1;
		ourgl = (GrGlyphs *) mallocMagic((unsigned) (sizeof(GrGlyphs) + 
			((glyphnummax + 1) * sizeof(GrGlyph *))) );
		ourgl->gr_num = glyphnummax + 1;
		for (i = 0; i <= glyphnummax; i++)
		{
		    size_t size = sizeof(GrGlyph) + sizeof(int) * (xmax + 1)
				* (ymax + 1);
		    ourgl->gr_glyph[i] = (GrGlyph *) mallocMagic(size);

		    /* Null all the fields initially. */
		    memset(ourgl->gr_glyph[i], 0, size);

		    ourgl->gr_glyph[i]->gr_origin.p_x = 0;
		    ourgl->gr_glyph[i]->gr_origin.p_y = 0;
		    ourgl->gr_glyph[i]->gr_xsize = xmax + 1;
		    ourgl->gr_glyph[i]->gr_ysize = ymax + 1;
		}
	    }
	}
    }

endit:
    if (file != NULL)
	(void) fclose(file);
    if (result)
	*gl = ourgl;
    else if (ourgl != NULL)
	GrFreeGlyphs(ourgl);
    return result;
}
