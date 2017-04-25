
/*
 * grGlyphs.h --
 *
 * 	Data structures to hold glyphs.
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
 *
 * rcsid "$Header: /usr/cvsroot/magic-8.0/graphics/glyphs.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $"
 */

#ifndef _GLYPHS_H
#define _GLYPHS_H

#include "utils/geometry.h"

/* data structures */

typedef struct GR_GLY2 {
    Point gr_origin;	/* The location of the origin of the glyph. */
    int gr_xsize;	/* The width of the glyph. */
    int gr_ysize;	/* The height of the glyph. */
    ClientData gr_cache;/* Glyph cached source data.  Used by Tk/Tcl; 	*/
			/* without it, Tk will re-use the cursor memory	*/
			/* and cursors will get rearranged.		*/
    void (*gr_free)();	/* Routine to call to free the cached data.	*/
    int gr_pixels[1];	/* Will actually be as large an array as needed. */
} GrGlyph;

typedef struct GR_GLY3
{
    int gr_num;	   	   /* The number of glyphs in this record. */
    GrGlyph *gr_glyph[1];  /* Will be big enough to hold as many glyphs as 
			    * we have.
			    */
} GrGlyphs;

/* procedures */

extern void GrFreeGlyphs();

/* global pointer to the set of window glyphs */

extern GrGlyphs *windGlyphs;

#endif /* _GLYPHS_H */
