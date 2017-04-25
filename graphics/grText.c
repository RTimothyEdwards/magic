/* grText.c -
 *
 * Contains functions for manipulating text that are not dependent
 * upon the display type.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/graphics/grText.c,v 1.3 2008/12/11 04:20:06 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <math.h>

#include "utils/magic.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "database/database.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "graphics/graphicsInt.h"
#include "utils/utils.h"

/*---------------------------------------------------------
 *
 * GrFontText ---
 *
 *	This routine is a glorified version of GrPutText
 *	that draws vector outline fonts according to
 *	information loaded and stored into the font lists
 *	(see DBLoadFont() in DBlabel.c).  In addition to
 *	the properties used by GrPutText(), GrFontText()
 *	uses label scale, rotation, offset, and font.
 *
 *---------------------------------------------------------
 */

bool
GrFontText(str, style, p, font, size, rotate, clip)
    char *str;		/* The text to be drawn. */
    int style;		/* Display style to use for the text */
    Point *p;		/* Point of origin */
    int font;		/* Font to use */
    int size;		/* Scale */
    int rotate;		/* Rotation (in degrees) */
    Rect *clip;		/* Clipping area */
{
    Rect nClip;
    Point pstart;

    /* Fall back on X11 text if vectored text unavailable */
    if (!grFontTextPtr)
    {
	GrPutText(str, style, p, GEO_CENTER, GR_TEXT_MEDIUM, FALSE, clip, NULL);
	return TRUE;
    }

    nClip = *clip;
    GeoClip(&nClip, &grCurClip);

    GR_CHECK_LOCK();
    if (!grDriverInformed) grInformDriver();

    if (style >= 0)
	(*grSetWMandCPtr)(GrStyleTable[style].mask, GrStyleTable[style].color);

    (*grFontTextPtr)(str, font, size, rotate, p, &nClip, grCurObscure);

    return TRUE;
}

/* The following definition defines how much text is offset from
 * its positioning point when it isn't centered.
 */

#define TEXTOFFSET 5


/*---------------------------------------------------------
 * GrPutText:
 *	This routine puts a chunk of text on the screen in the given
 *	color, size, and position.  It is clipped to a rectangle.
 *
 * Results:	
 *	True if the text was able to be displayed.
 *
 * Side Effects:
 *	The text is drawn on the screen at pos relative to p, using
 *	style (text can also be erased by passing a suitable style).
 *	The rectangle 'actual' is filled in with the actual location of
 *	the text on the screen (if actual is a non-null pointer).  The
 *	text will be shrunk to a smaller font, if that will help it to
 *	fit into the clipping rectangle.
 *---------------------------------------------------------
 */

bool
GrPutText(str, style, p, pos, size, adjust, clip, actual)
    char *str;		/* The text to be drawn. */
    int style;		/* The style for drawing text; if -1 then
			 * the caller has already set the style
			 * and we don't have to.
			 */

    Point *p;		/* The point to align with */
    int pos;		/* The alignment desired (GR_NORTH, 
			 * GR_NORTHEAST, etc.)
			 */
    int size;		/* The desired size of the text 
			 * (such as GR_TEXT_MEDIUM).  
			 */
    bool adjust;	/* TRUE means adjust the text (either by
			 * sliding it around or using a smaller font)
			 * if that is necessary to make it fit into
			 * the clipping rectangle.  FALSE means
			 * display the text exactly as instructed,
			 * clipping it if it doesn't fit.
			 */
    Rect *clip;		/* A clipping rectangle for the text */
    Rect *actual;	/* To be filled in with the location of the
			 * text.
			 */
{
    Rect posR;
    Point drawPoint;
    int xpos, ypos, hangBelow;
    Rect nClip;

    nClip = *clip;
    GeoClip(&nClip, &grCurClip);

    GR_CHECK_LOCK();
    if (!grDriverInformed) grInformDriver();

    if (actual != (Rect *) NULL)
    {
	actual->r_xbot = actual->r_ybot = 0;
	actual->r_xtop = actual->r_ytop = 0;
    }

    /* The following loop sees if the text will fit in the clipping
     * area.  If not, and shrinking is allowed, we try again and
     * again with smaller sizes.
     */

    while (TRUE)
    {
	/* what portion of the screen is taken up by the text? */
	GrTextSize(str, size, &posR);
	hangBelow = -posR.r_ybot;

	/* figure out where the text will go, including a border on 1 side  */

	switch (pos)	/* horizontal centering */
	{
	    case GEO_NORTHWEST:
	    case GEO_WEST:
	    case GEO_SOUTHWEST:
		xpos = p->p_x - TEXTOFFSET - posR.r_xtop;
		break;
	    case GEO_NORTH:
	    case GEO_CENTER:
	    case GEO_SOUTH:
		xpos = p->p_x - posR.r_xtop/2;
		break;
	    case GEO_NORTHEAST:
	    case GEO_EAST:
	    case GEO_SOUTHEAST:
		xpos = p->p_x + TEXTOFFSET;
		break;
	    default:
		TxError("Illegal position (%d) for text (internal error)\n", 
		    pos);
		return FALSE;
		break;
	}
	switch (pos)	/* vertical centering */
	{
	    case GEO_NORTH:
	    case GEO_NORTHEAST:
	    case GEO_NORTHWEST:
		ypos = p->p_y + TEXTOFFSET;
		break;
	    case GEO_CENTER:
	    case GEO_WEST:
	    case GEO_EAST:
		ypos = p->p_y - (posR.r_ytop / 2);
		break;
	    case GEO_SOUTH:
	    case GEO_SOUTHEAST:
	    case GEO_SOUTHWEST:
		ypos = p->p_y - posR.r_ytop - TEXTOFFSET;
		break;
	}

	/* move area to screen coordinates */
	posR.r_xbot += xpos;
	posR.r_xtop += xpos;
	posR.r_ybot += ypos;
	posR.r_ytop += ypos;

	/* will that area fit within the clipping rectangle? */
	if ( (posR.r_xtop <= nClip.r_xtop) && (posR.r_xbot >= nClip.r_xbot) &&
	    (posR.r_ytop <= nClip.r_ytop) && (posR.r_ybot >= nClip.r_ybot) )
	{
	    /* it fits! */
	    break;
	}

	/* it doesn't fit, will sliding it be enough? */
	if (adjust)
	{
	    if ( ((nClip.r_xtop-nClip.r_xbot) >= (posR.r_xtop - posR.r_xbot)) &&
		((nClip.r_ytop - nClip.r_ybot) >= (posR.r_ytop - posR.r_ybot)) )
	    {
		/* it will fit */
		break;
	    }

	}

	/* Won't fit even with sliding, so shrink if possible. */
	if (adjust && (size > 0) )
	{
	    /* maybe shrinking it will help */
	    size -= 1;
	}
	else break;

    } /* while */


    /* ASSERTION: We've shrunk things to the proper size at this point.  */

    /* Slide the text, if that is allowable and needed.  We'll only
     * slide the text if there's available space on one side and
     * insufficient space on the other.
     */

    if (adjust)
    {
	int top, bottom, left, right;	/* Space needed on each side. */
	int slide;

	right = posR.r_xtop - nClip.r_xtop;
	left = nClip.r_xbot - posR.r_xbot;
	top = posR.r_ytop - nClip.r_ytop;
	bottom = nClip.r_ybot - posR.r_ybot;

	slide = 0;
	if (right > 0)
	{
	    if (left < 0) slide = MAX(-right, left);
	}
	else if (left > 0) slide = MIN(left, -right);
	posR.r_xbot += slide;
	posR.r_xtop += slide;

	slide = 0;
	if (top > 0)
	{
	    if (bottom < 0) slide = MAX(-top, bottom);
	}
	else if (bottom > 0) slide = MIN(bottom, -top);
	posR.r_ybot += slide;
	posR.r_ytop += slide;
    }

    /* ASSERTION: By now the text is positioned properly. */
    
    /* do all clipping within the grPutTextPtr routine */

    (*grSetCharSizePtr)(size);
    if (style >= 0)
	(*grSetWMandCPtr)(GrStyleTable[style].mask, GrStyleTable[style].color);

    drawPoint.p_x = posR.r_xbot;
    drawPoint.p_y = posR.r_ybot + hangBelow;
    (*grPutTextPtr)(str, &drawPoint, &nClip, grCurObscure);

    if (actual != (Rect *) NULL)
	*actual = posR;
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * GrLabelSize --
 *
 * 	Determines the total size, in pixels, of a label.
 *	Used to compute how much to redisplay when a label changes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The parameter "area" is modified to contain the size, in pixels,
 *	of the given label, relative to its positioning point.  This
 *	size includes just the text, and not any marker (e.g. cross
 * 	displayed at the label position).
 *
 * ----------------------------------------------------------------------------
 */

void
GrLabelSize(text, pos, size, area)
    char *text;				/* Text of the label. */
    int pos;				/* Position of the label relative
					 * to its positioning point.
					 */
    int size;				/* Text size. */
    Rect *area;				/* To be filled in with label size. */
{
    int xoffset, yoffset;		/* Offsets due to label position. */

    GrTextSize(text, size, area);

    /* Now we now the text's size... but if it's positioned to one
     * side of the point, we have to offset it.
     */

    switch (pos)			/* Get x offset first. */
    {
	case GEO_NORTHWEST:
	case GEO_WEST:
	case GEO_SOUTHWEST:
	    xoffset = - TEXTOFFSET - area->r_xtop;
	    break;
	case GEO_NORTH:
	case GEO_CENTER:
	case GEO_SOUTH:
	    xoffset =  - area->r_xtop/2;
	    break;
	case GEO_NORTHEAST:
	case GEO_EAST:
	case GEO_SOUTHEAST:
	    xoffset = TEXTOFFSET;
	    break;
    }
    switch (pos)			/* Now get y offset. */
    {
	case GEO_NORTH:
	case GEO_NORTHEAST:
	case GEO_NORTHWEST:
	    yoffset = TEXTOFFSET;
	    break;
	case GEO_CENTER:
	case GEO_WEST:
	case GEO_EAST:
	    yoffset = - area->r_ytop/2;
	    break;
	case GEO_SOUTH:
	case GEO_SOUTHEAST:
	case GEO_SOUTHWEST:
	    yoffset = - area->r_ytop - TEXTOFFSET;
	    break;
    }

    area->r_xbot += xoffset;
    area->r_xtop += xoffset;
    area->r_ybot += yoffset;
    area->r_ytop += yoffset;
}
