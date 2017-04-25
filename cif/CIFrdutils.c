/* CIFreadutils.c -
 *
 *	This file contains routines that parse a file in CIF
 *	format.  This file contains the top-level routine for
 *	reading CIF files, plus a bunch of utility routines
 *	for skipping white space, parsing numbers and points, etc.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/cif/CIFrdutils.c,v 1.4 2010/06/24 12:37:15 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "cif/CIFint.h"
#include "cif/CIFread.h"
#include "cif/cif.h"
#include "textio/textio.h"
#include "utils/signals.h"
#include "utils/undo.h"
#include "utils/malloc.h"

/* The following variables are used to provide one character of
 * lookahead.  cifParseLaAvail is TRUE if cifParseLaChar contains
 * a valid character, FALSE otherwise.  The PEEK and TAKE macros
 * are used to manipulate this stuff.
 */

bool cifParseLaAvail = FALSE;
int cifParseLaChar = EOF;

/* Below is a variable pointing to the CIF input file.  It's used
 * by the PEEK and TAKE macros.  The other stuff is used to keep
 * track of our location in the CIF file for error reporting
 * purposes.
 */

FILE *cifInputFile;
FILE *cifErrorFile;
int cifLineNumber;		/* Number of current line. */
int cifTotalWarnings;		/* Number of warnings detected */
int cifTotalErrors;		/* Number of errors detected */

/* The variables used below hold general information about what
 * we're currently working on.
 */

int cifReadScale1;			/* Scale factor:  multiply by Scale1 */
int cifReadScale2;			/* then divide by Scale2. */
int CIFRescaleLimit = CIFMAXRESCALE;	/* Don't increase cifReadScale1 by more
					 * than this amount;  internal units
					 * finer than this will be rounded.
					 */
bool CIFRescaleAllow = TRUE;		/* Don't subdivide the magic internal
					 * grid if this is FALSE.
					 */
bool CIFNoDRCCheck = FALSE;		/* If TRUE, then cell is marked DRC clean
					 * and not DRC checked.
					 */
char *CIFErrorFilename;			/* Name of file for error redirection */

int  CifPolygonCount;			/* Count of generated subcells
					 * containing polygons.  This number
					 * is used to create a unique cell name.
					 */
bool CIFSubcellPolygons = FALSE;	/* If TRUE, each non-Manhattan polygon
					 * will be put in a separate subcell
					 * to avoid too much tile splitting
					 */

Plane *cifReadPlane;			/* Plane into which to paint material
					 * NULL means no layer command has
					 * been seen for the current cell.
					 */

/*
 * ----------------------------------------------------------------------------
 *
 * CIFReadError --
 *
 * 	This procedure is called to print out error messages during
 *	CIF file reading.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	An error message is printed.
 *
 * ----------------------------------------------------------------------------
 */

    /* VARARGS1 */
void
CIFReadError(char *format, ...)
{
    va_list args;

    cifTotalErrors++;
    if (CIFWarningLevel == CIF_WARN_NONE) return;
    if ((cifTotalErrors < 100) || (CIFWarningLevel != CIF_WARN_LIMIT))
    {
	TxError("Error at line %d of CIF file: ", cifLineNumber);
	va_start(args, format);
	Vfprintf(stderr, format, args);
	va_end(args);
    }
    else if ((cifTotalErrors == 100) && (CIFWarningLevel == CIF_WARN_LIMIT))
    {
	TxError("Error limit set:  Remaining errors will not be reported.\n");
    }
}


void
CIFReadWarning(char *format, ...)
{
    va_list args;

    cifTotalWarnings++;
    if (CIFWarningLevel == CIF_WARN_NONE) return;
    if ((cifTotalWarnings < 100) || (CIFWarningLevel != CIF_WARN_LIMIT))
    {
	TxError("Warning at line %d of CIF file: ", cifLineNumber);
	va_start(args, format);
	Vfprintf(stderr, format, args);
	va_end(args);
    }
    else if ((cifTotalWarnings == 100) && (CIFWarningLevel == CIF_WARN_LIMIT))
    {
	TxError("Warning limit set:  Remaining warnings will not be reported.\n");
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 *	CIFScaleCoord
 *
 * 	This procedure does rounding and division to convert from
 *	CIF units back into Magic units.
 *
 *	"snap_type" may be one of:
 *	    COORD_EXACT:  result must be an exact integer.  If not, the
 *		magic grid spacing is changed such that the result will
 *		be an integer.
 *	    COORD_HALF_U:  twice the result must be an exact integer.  If
 *		not, the magic grid spacing is changed as above.  If the
 *		result is 1/2, it is rounded up to the nearest integer.
 *	    COORD_HALF_L:  same as above, but result is rounded down.
 *	    COORD_ANY:  result may be fractional, and will be snapped to
 *		the nearest magic grid.  Generally, this is used for
 *		labels whose position need not be exact.
 *
 * Results:
 *	The result is the Magic unit equivalent to cifCoord.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
CIFScaleCoord(cifCoord, snap_type)
    int cifCoord;			/* A coordinate in CIF units. */
    int snap_type;			/* How to deal with fractional results */
{
    int result, scale, remain, denom;
    int mult, mfactor;

    /* If internal grid subdivision is disallowed, always round to the	*/
    /* nearest grid unit.						*/

    if (!CIFRescaleAllow)
	snap_type = COORD_ANY;

    scale = cifCurReadStyle->crs_scaleFactor;
    mult = cifCurReadStyle->crs_multiplier;

    /* Check for non-integer result and warn of fractional-lambda violation */

    if ((remain = (cifCoord % scale)) != 0)
    {
	int lgcf = FindGCF(abs(cifCoord), scale);

	remain = abs(remain) / lgcf;
	denom = scale / lgcf;

	if (CIFReadTechLimitScale(1, denom)) snap_type = COORD_ANY;

	switch (snap_type)
	{
	    case COORD_EXACT:
		CIFReadWarning("Input off lambda grid by %d/%d; grid redefined.\n",
			remain, denom);

		CIFTechInputScale(1, denom, FALSE);
		CIFTechOutputScale(1, denom);
		DRCTechScale(1, denom);
		PlowAfterTech();
		ExtTechScale(1, denom);
		WireTechScale(1, denom);
#ifdef LEF_MODULE
		LefTechScale(1, denom);
#endif
#ifdef ROUTE_MODULE
		RtrTechScale(1, denom);
		MZAfterTech();
		IRAfterTech();
#endif
		DBScaleEverything(denom, 1);
		DBLambda[1] *= denom;
		ReduceFraction(&DBLambda[0], &DBLambda[1]);
		scale = cifCurReadStyle->crs_scaleFactor;
		result = cifCoord / scale;
		break;
	    case COORD_HALF_U: case COORD_HALF_L:
		if (denom > 2)
		{
		    CIFReadWarning("Input off lambda grid by %d/%d; grid redefined.\n",
				remain, denom);

		    /* scale to nearest half-lambda */
		    if (!(denom & 0x1)) denom >>= 1;

		    CIFTechInputScale(1, denom, FALSE);
		    CIFTechOutputScale(1, denom);
		    DRCTechScale(1, denom);
		    PlowAfterTech();
		    ExtTechScale(1, denom);
		    WireTechScale(1, denom);
		    MZAfterTech();
		    IRAfterTech();
#ifdef LEF_MODULE
		    LefTechScale(1, denom);
#endif
#ifdef ROUTE_MODULE
		    RtrTechScale(1, denom);
#endif
		    DBScaleEverything(denom, 1);
		    DBLambda[1] *= denom;
		    ReduceFraction(&DBLambda[0], &DBLambda[1]);
		    scale = cifCurReadStyle->crs_scaleFactor;
		}

		if (snap_type == COORD_HALF_U)
		    result = cifCoord + (scale >> 1);
		else
		    result = cifCoord - (scale >> 1);
		result /= scale;

		break;
	    case COORD_ANY:
		CIFReadWarning("Input off lambda grid by %d/%d; snapped to grid.\n",
			abs(remain), abs(denom));

		/* Careful:  must round down a bit more for negative numbers, in
		 * order to ensure that a point exactly halfway between Magic units
		 * always gets rounded down, rather than towards zero (this would
		 * result in different treatment of the same paint, depending on
		 * where it is in the coordinate system.
		 */

		if (cifCoord < 0)
		    result = cifCoord - ((scale)>>1);
		else
		    result = cifCoord + ((scale-1)>>1);
		result /= scale;
		break;
	}
    }
    else
	result = cifCoord / scale;

    return result;
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifIsBlank --
 *
 * 	Figures out whether a character qualifies as a blank in CIF.
 *	A blank is anything except a digit, an upper-case character,
 *	or the symbols "-", "(", "(", and ";".
 *
 * Results:
 *	Returns TRUE if ch is a CIF blank, FALSE otherwise.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
cifIsBlank(ch)
    int	ch;
{

    if (  isdigit(ch) || isupper(ch)
	|| (ch == '-') || (ch == ';')
	|| (ch == '(') || (ch == ')')
	|| (ch == EOF))
    {
	return FALSE;
    }
    else return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFSkipBlanks --
 *
 * 	This procedure skips over whitespace in the CIF file,
 *	keeping track of the line number and other information
 *	for error reporting.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Advances through the CIF file.
 *
 * ----------------------------------------------------------------------------
 */

void
CIFSkipBlanks()
{
    
    while (cifIsBlank(PEEK())) {
	if (TAKE() == '\n')
	{
	    cifLineNumber++;
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFSkipSep --
 *
 * 	Skip over separators in the CIF file.  Blanks and upper-case
 *	characters are separators.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Advances through the CIF file.
 *
 * ----------------------------------------------------------------------------
 */

void
CIFSkipSep()
{
    int	ch;

    for (ch = PEEK() ; isupper(ch) || cifIsBlank(ch) ; ch = PEEK()) {
	if (TAKE() == '\n')
	{
	    cifLineNumber++;
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFSkipToSemi --
 *
 * 	This procedure is called after errors.  It skips everything
 *	in the CIF file up to the next semi-colon.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Advances through the CIF file.
 *
 * ----------------------------------------------------------------------------
 */

void
CIFSkipToSemi()
{
    int	ch;
    
    for (ch = PEEK() ; ((ch != ';') && (ch != EOF)) ; ch = PEEK()) {
	if (TAKE() == '\n')
	{
	    cifLineNumber++;
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFSkipSemi --
 *
 * 	Skips a semi-colon, including blanks around the semi-colon.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Advances through the CIF file.
 *
 * ----------------------------------------------------------------------------
 */

void
CIFSkipSemi()
{
    
    CIFSkipBlanks();
    if (PEEK() != ';') {
	CIFReadError("`;\' expected.\n");
	return;
    }
    TAKE();
    CIFSkipBlanks();
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFParseSInteger --
 *
 * 	This procedure parses a signed integer from the CIF file.
 *
 * Results:
 *	TRUE is returned if the parse completed without error,
 *	FALSE otherwise.
 *
 * Side effects:
 *	The integer pointed to by valuep is modified with the
 *	value of the signed integer.
 *
 * ----------------------------------------------------------------------------
 */

bool
CIFParseSInteger(valuep)
    int		*valuep;
{
    bool	is_signed;
    char	buffer[ BUFSIZ ];
    char	*bufferp;

    *valuep = 0;
    CIFSkipSep();
    if (PEEK() == '-')
    {
	TAKE();
	is_signed = TRUE;
    }
    else is_signed = FALSE;
    bufferp = &buffer[0];
    while (isdigit(PEEK()))
	*bufferp++ = TAKE();
    if (bufferp == &buffer[0])
	return FALSE;
    *bufferp = '\0';
    *valuep = atoi(&buffer[0]);
    if (is_signed)
	*valuep = -(*valuep);
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFParseInteger --
 *
 * 	Parses a positive integer from the CIF file.
 *
 * Results:
 *	TRUE is returned if the parse was completed successfully,
 *	FALSE otherwise.
 *
 * Side effects:
 *	The value pointed to by valuep is modified to hold the integer.
 *
 * ----------------------------------------------------------------------------
 */

bool
CIFParseInteger(valuep)
    int *valuep;
{

    if (!CIFParseSInteger(valuep))
	return FALSE;
    if (*valuep < 0)
	CIFReadError("negative integer not permitted.\n");
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFParsePoint --
 *
 * 	Parse a point from a CIF file.  A point is two integers
 *	separated by CIF separators.
 *	parameter "iscale" (internal scale factor) is usually 1, but
 *	can be 2 to deal with half-lambda entries in the CIF by
 *	returning double the result.
 *
 * Results:
 *	TRUE is returned if the point was parsed correctly, otherwise
 *	FALSE is returned.
 *
 * Side effects:
 *	The parameter pointp is filled in with the coordinates of
 *	the point.
 *
 *	If the CIF scalefactors are such that the result would be a
 *	fractional value, the definition of the CIF scale is altered
 *	such that the result is integer, and all geometry read so far
 *	is altered to match.  This does not immediately affect the geometry
 *	in the magic database;  if that also appears to have fractional
 *	units, it will be discovered by CIFScaleCoord and corrected.
 *
 * ----------------------------------------------------------------------------
 */

bool
CIFParsePoint(pointp, iscale)
    Point *pointp;
    int iscale;
{
    int rescale;

    pointp->p_x = 0;
    pointp->p_y = 0;
    if (!CIFParseSInteger(&pointp->p_x))
	return FALSE;
    pointp->p_x *= (cifReadScale1 * iscale);
    if (pointp->p_x % cifReadScale2 != 0)
    {
	rescale = cifReadScale2 / FindGCF(cifReadScale2, abs(pointp->p_x));
	if ((cifReadScale1 * rescale) > CIFRescaleLimit)
	{
	    CIFReadWarning("CIF units at maximum scale; value is rounded\n");
	    /* prepare for nearest-integer rounding */
	    if (pointp->p_x < 0)
		pointp->p_x -= ((cifReadScale2 - 1) >> 1);
	    else
		pointp->p_x += (cifReadScale2  >> 1);
	}
	else
	{
	    cifReadScale1 *= rescale;
	    CIFInputRescale(rescale, 1);
	    pointp->p_x *= rescale;
	}
    }
    pointp->p_x /= cifReadScale2;
    if (!CIFParseSInteger(&pointp->p_y))
	return FALSE;
    pointp->p_y *= (cifReadScale1 * iscale);
    if (pointp->p_y % cifReadScale2 != 0)
    {
	rescale = cifReadScale2 / FindGCF(cifReadScale2, abs(pointp->p_y));
	if ((cifReadScale1 * rescale) > CIFRescaleLimit)
	{
	    CIFReadWarning("CIF units at maximum scale; value is rounded\n");
	    /* prepare for nearest-integer rounding */
	    if (pointp->p_y < 0)
		pointp->p_y -= ((cifReadScale2 - 1) >> 1);
	    else
		pointp->p_y += (cifReadScale2  >> 1);
	}
	else
	{
	    cifReadScale1 *= rescale;
	    CIFInputRescale(rescale, 1);
	    pointp->p_x *= rescale;
	    pointp->p_y *= rescale;
	}
    }
    pointp->p_y /= cifReadScale2;
    return TRUE;
}


/*
 * ----------------------------------------------------------------------------
 *
 * CIFParsePath --
 *
 * 	This procedure parses a CIF path, which is sequence of
 *	one or more points.
 *
 * Results:
 *	TRUE is returned if the path was parsed successfully,
 *	FALSE otherwise.
 *
 * Side effects:
 *	Modifies the parameter pathheadpp to point to the path
 *	that is constructed.
 *
 * Corrections:
 *	CIF coordinates are multiplied by 2 to cover the case where
 *	the path centerline lies on the half lambda grid but the line
 * 	itself is on-grid.  This can't be done for polygons, so a
 *	parameter "iscale" (internal scale) is added, and set to 1 for
 *	polygons, 2 for wires when calling CIFParsePath().
 *
 * ----------------------------------------------------------------------------
 */

bool
CIFParsePath(pathheadpp, iscale)
    CIFPath **pathheadpp;
    int iscale;
{
    CIFPath *pathtailp, *newpathp;
    bool nonManhattan = FALSE;		/* diagnostic only */
    CIFPath path;
    int savescale;

    *pathheadpp = NULL;
    pathtailp = NULL;
    path.cifp_next = NULL;
    while (TRUE)
    {
	CIFSkipSep();
	if (PEEK() == ';')
	    break;

	savescale = cifReadScale1;
	if (!CIFParsePoint(&path.cifp_point, iscale))
	{
	    CIFFreePath(*pathheadpp);
	    return FALSE;
	}
	if (savescale != cifReadScale1)
	{
	    CIFPath *phead = *pathheadpp;
	    int newscale = cifReadScale1 / savescale;
	    while (phead != NULL)
	    {
		phead->cifp_x *= newscale;
		phead->cifp_y *= newscale;
		phead = phead->cifp_next;
	    }
	}
	newpathp = (CIFPath *) mallocMagic((unsigned) (sizeof (CIFPath)));
	*newpathp = path;
	if (*pathheadpp)
	{
	    /*
	     * Check that this segment is Manhattan.  If not, remember the
	     * fact and later introduce extra stair-steps to make the path
	     * Manhattan.  We don't do the stair-step introduction here for
	     * two reasons: first, the same code is also used by the Calma
	     * module, and second, it is important to know which side of
	     * the polygon is the outside when generating the stair steps.
	     */
	    if (pathtailp->cifp_x != newpathp->cifp_x
		    && pathtailp->cifp_y != (newpathp->cifp_y))
	    {
		nonManhattan = TRUE;
	    }
	    pathtailp->cifp_next = newpathp;
	}
	else *pathheadpp = newpathp;
	pathtailp = newpathp;
    }
    return (*pathheadpp != NULL);
}

/*
 * ----------------------------------------------------------------------------
 *
 * test_insideness --
 *
 *	Determine if a point is inside a rectangle defined by the
 *	first three points in the given CIF path.
 *
 * Results:
 *	TRUE if point is inside, FALSE if outside or on the border 	
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

bool
test_insideness(start, tpoint)
    CIFPath *start;
    Point *tpoint;
{
    Rect tmprect, irect;

    tmprect.r_xbot = start->cifp_x;
    tmprect.r_ybot = start->cifp_y;
    tmprect.r_xtop = start->cifp_next->cifp_next->cifp_x;
    tmprect.r_ytop = start->cifp_next->cifp_next->cifp_y;

    GeoCanonicalRect(&tmprect, &irect);

    return ((tpoint->p_x > irect.r_xbot)
		&& (tpoint->p_x < irect.r_xtop)
		&& (tpoint->p_y > irect.r_ybot)
		&& (tpoint->p_y < irect.r_ytop)) ?  TRUE : FALSE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * seg_intersect --
 *
 *	Determine if two line segments intersect or touch
 *	Expects first line to be manhattan.
 *
 * Results:
 *	returns TRUE if segments intersect, FALSE otherwise
 *
 * Side effects:
 *	value of respt contains point to which segment will be
 *	truncated.
 *	
 * ----------------------------------------------------------------------------
 */

bool
seg_intersect(tstart, bf, bs, respt)
    CIFPath *tstart;
    Point *bf, *bs;
    Point *respt;
{
    int afx = tstart->cifp_x;
    int afy = tstart->cifp_y;
    int asx = tstart->cifp_next->cifp_x;
    int asy = tstart->cifp_next->cifp_y;
    int adx, ady;

    if (afx == asx)	/* "a" is a vertical line */
    {
	adx = afx + ((tstart->cifp_next->cifp_next->cifp_x > afx) ? 1 : -1);

	/* Ignore if b does not cross the x boundary of ad */
	if ((bf->p_x > adx && bs->p_x > adx) ||
	    	(bf->p_x < adx && bs->p_x < adx))
	    return FALSE;

	if (bs->p_x == bf->p_x)		/* nonintersecting vertical lines */
	    return FALSE;

	respt->p_x = afx;
        respt->p_y = bf->p_y + (int)
			(((dlong)(bs->p_y - bf->p_y) * (dlong)(afx - bf->p_x)) /
			(dlong)(bs->p_x - bf->p_x));
	if (((respt->p_y > afy) && (respt->p_y < asy)) ||
		((respt->p_y < afy) && (respt->p_y > asy)))
	    return TRUE;
    }
    else 	/* (afy == asy), "a" is a horizontal line */
    {
	ady = afy + ((tstart->cifp_next->cifp_next->cifp_y > afy) ? 1 : -1);

	/* Ignore if b does not cross the y boundary of ad */
	if ((bf->p_y > ady && bs->p_y > ady) ||
	    	(bf->p_y < ady && bs->p_y < ady))
	    return FALSE;

	if (bs->p_y == bf->p_y)		/* nonintersecting horizontal lines */
	    return FALSE;

	respt->p_y = afy;
        respt->p_x = bf->p_x + (int)
			(((dlong)(bs->p_x - bf->p_x) * (dlong)(afy - bf->p_y)) /
			(dlong)(bs->p_y - bf->p_y));
	if (((respt->p_x > afx) && (respt->p_x < asx)) ||
		((respt->p_x < afx) && (respt->p_x > asx)))
	    return TRUE;
    }
    return FALSE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * path_intersect --
 *
 *   Determine if a path intersects the given line segment.
 *   A path sharing a portion of the segment is not an intersection.
 *
 * ----------------------------------------------------------------------------
 */

bool
path_intersect(pathHead, start, respt)
    CIFPath *pathHead, *start;
    Point *respt;
{
    CIFPath *path, *segcrossed, *new;
    Point tmppt;
    bool does_cross = FALSE, diagonal = FALSE;
    int tdist, newdist;

    tdist = newdist = INFINITY;
    for (path = pathHead; path->cifp_next; path = path->cifp_next)
    {
	/* don't compare with self */
	if (path == start || path == start->cifp_next) continue;

	/* Does the path intersect the first line of the	*/
	/* right triangle, continuing in the direction of	*/
	/* the last point on the triangle?			*/

	if (seg_intersect(start, &path->cifp_point,
		&path->cifp_next->cifp_point, &tmppt))
	{
	    newdist = (start->cifp_x - tmppt.p_x) + (start->cifp_y - tmppt.p_y);
	    diagonal = TRUE;
	}

	/* Is the point inside the triangle, and the path is Manhattan?	*/
	/* (Note that *both* tests can be true, in which case the one	*/
	/* with the smaller absolute distance takes precedence.)	*/

	if (test_insideness(start, &path->cifp_point)) {
	    int tmpdist = abs(newdist);		/* save this value */	
	    if (path->cifp_x == path->cifp_next->cifp_x ||
			path->cifp_y == path->cifp_next->cifp_y)
	    {
		if (start->cifp_x == start->cifp_next->cifp_x)
		{
		    newdist = path->cifp_y - start->cifp_y;
		    if (abs(newdist) < tmpdist)
		    {
			tmppt.p_x = start->cifp_x;
			tmppt.p_y = path->cifp_y;
			diagonal = FALSE;
		    }
		}
		else
		{
		    newdist = path->cifp_x - start->cifp_x;
		    if (abs(newdist) < tmpdist)
		    {
			tmppt.p_y = start->cifp_y;
			tmppt.p_x = path->cifp_x;
			diagonal = FALSE;
		    }
		}
	    }
	}
	else if (diagonal == FALSE)
	    continue;

	newdist = abs(newdist);
	if ((!does_cross) || (newdist < tdist))
	{
	    does_cross = TRUE;
	    respt->p_x = tmppt.p_x;
	    respt->p_y = tmppt.p_y;
	    tdist = newdist;
	    segcrossed = (diagonal) ? path : NULL;
	}
    }

    /* If we're limited by another side of the polygon, then we're */
    /* guaranteed that we'll have to add another point there.  By  */
    /* doing it here, we avoid problems due to roundoff errors.	   */

    if (does_cross && segcrossed)
    {
	new = (CIFPath *) mallocMagic((unsigned) (sizeof (CIFPath)));
	new->cifp_next = segcrossed->cifp_next;
	segcrossed->cifp_next = new;
 	new->cifp_x = respt->p_x;
 	new->cifp_y = respt->p_y;
    }
    return does_cross;
}

/*
 * ----------------------------------------------------------------------------
 *
 * is_clockwise --
 *
 *	Determine if a path is clockwise or counterclockwise.
 *
 * Results:
 *	TRUE if the path is clockwise, FALSE otherwise.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
is_clockwise(pathHead)
    CIFPath *pathHead;
{
    CIFPath *path, *midx = NULL, *last;
    Point *p1, *p2, *p3;
    dlong sdir;
    int minx = INFINITY;

    /* Find out if this is a clockwise or counterclockwise path by	*/
    /* finding the (a) leftmost point and assuming the polygon to fill	*/
    /* is to the right.							*/

    for (path = pathHead; path->cifp_next; path = path->cifp_next)
    {
	if (path->cifp_next->cifp_x < minx)
	{
	    minx = path->cifp_next->cifp_x;
	    midx = path->cifp_next;
	    last = path;
	}
    }

    if (!midx) return TRUE;	/* one-point polygon? */

    /* Rare case of colinear points (implies degenerate polygon) requires */
    /* moving along pointlist until points are not colinear and repeating */
    /* the search for the minimum.					  */

    if (last->cifp_x == midx->cifp_x)
    {
	for (path = pathHead; path && path->cifp_x == minx;
			path = path->cifp_next);
	if (!path) return TRUE; /* completely degenerate; direc. irrelevant */
	minx = INFINITY;
	for (; path->cifp_next; path = path->cifp_next)
	{
	    if (path->cifp_next->cifp_x < minx)
	    {
		minx = path->cifp_next->cifp_x;
		midx = path->cifp_next;
		last = path;
	    }
	}
    }

    if (!(midx->cifp_next)) midx = pathHead;

    /* p2 is the (a) leftmost point; p1 and p3 are the points before	*/
    /* and after in the CIF path, respectively.				*/

    p1 = &(last->cifp_point);
    p2 = &(midx->cifp_point);
    p3 = &(midx->cifp_next->cifp_point);

    /* Find which side p3 falls on relative to the line p1-p2.  This	*/
    /* determines whether the path is clockwise or counterclockwise.	*/
    /* Use type dlong to avoid integer overflow.			*/

    sdir = ((dlong)(p2->p_x - p1->p_x) * (dlong)(p3->p_y - p1->p_y) -
		(dlong)(p2->p_y - p1->p_y) * (dlong)(p3->p_x - p1->p_x));

    return (sdir < 0) ? TRUE : FALSE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFMakeManhattanPath --
 *
 *	Convert a non-Manhattan path into a Manhattan one by
 *	breaking out triangles and leaving all Manhattan edges.
 *	Additional points are added which reroute the CIF path
 *	around the triangle.  In the simplest case, each non-Manhattan
 *	edge becomes a split tile bounding the edge endpoints.
 *	However, if that split tile would extend beyond the boundary
 *	of the CIF path, the edge is subdivided into as many
 *	triangles as are necessary to complete the path while remaining
 *	within the polygon boundary.  Unfortunately, for non-45-degree
 *	edges, the edge subdivision might not fall on an integer lambda
 *	value, so the resulting edge could be off by as much as 1/2
 *	lambda.  In this case, flag a warning.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May insert additional points in the path.
 *	May alter the intended geometry of a non-manhattan edge by as
 *		much as 1/2 lambda.
 *
 * ----------------------------------------------------------------------------
 */

void
CIFMakeManhattanPath(pathHead, plane, resultTbl, ui)
    CIFPath *pathHead;
    Plane *plane;
    PaintResultType *resultTbl;
    PaintUndoInfo *ui;
{
    CIFPath *new, *new2, *next, *path;
    int xinit, xdiff, xincr, xlast, x;
    int yinit, ydiff, yincr, ylast, y;

    bool clockwise;
    CIFPath *first, *last;
    Rect tt, tr;
    TileType type;

    clockwise = is_clockwise(pathHead);

    for (path = pathHead; path->cifp_next; path = path->cifp_next)
    {
	Point clipbase;
	int edir;
	next = path->cifp_next;

	/* No work if this segment is Manhattan */
	if (path->cifp_x == next->cifp_x || path->cifp_y == next->cifp_y)
	    continue;

	/* Otherwise, break out the triangle, then adjust as necessary */

	new = (CIFPath *) mallocMagic((unsigned) (sizeof (CIFPath)));
	path->cifp_next = new;
	new->cifp_next = next;

	/* Generate split tiles as necessary to reach next->cifp_y */

	if (clockwise)
	{
	    first = next;
	    last = path;
	}
	else
	{
	    first = path;
	    last = next;
	}
	edir = CIFEdgeDirection(first, last);
	if (edir == CIF_DIAG_DL || edir == CIF_DIAG_UR)
	{
	    new->cifp_x = first->cifp_x; 
	    new->cifp_y = last->cifp_y;
	}
	else  /* edir == CIF_DIAG_DR || edir == CIF_DIAG_UL */
	{
	    new->cifp_x = last->cifp_x;
	    new->cifp_y = first->cifp_y;
	}

	/* Check if the segment from first to base intersects	*/
	/* the polygon edge 				  	*/

	if (path_intersect(pathHead, path, &clipbase))
	{
	    new->cifp_x = clipbase.p_x;
	    new->cifp_y = clipbase.p_y;

	    new2 = (CIFPath *) mallocMagic((unsigned) (sizeof (CIFPath)));
	    new->cifp_next = new2;
	    new2->cifp_next = next;

	    /* Use double long for the multiplication and	*/
	    /* division, or else integer overflow can occur.	*/

	    if (path->cifp_x == new->cifp_x)	/* vertical line */
	    {
		new2->cifp_y = new->cifp_y;
		new2->cifp_x = path->cifp_x + (int)
			 ((dlong)(new2->cifp_y - path->cifp_y)
			* (dlong)(next->cifp_x - path->cifp_x)
			/ (dlong)(next->cifp_y - path->cifp_y));
	    }
	    else
	    {
		new2->cifp_x = new->cifp_x;
		new2->cifp_y = path->cifp_y + (int)
			 ((dlong)(new2->cifp_x - path->cifp_x)
			* (dlong)(next->cifp_y - path->cifp_y)
			/ (dlong)(next->cifp_x - path->cifp_x));
	    }
	}

	/* Break out the diagonal tile from the polygon and paint it. */

        type = (edir == CIF_DIAG_UR || edir == CIF_DIAG_UL) ? 0 : TT_SIDE;
        type |= (edir == CIF_DIAG_UR || edir == CIF_DIAG_DL) ? 0 : TT_DIRECTION;
	type |= TT_DIAGONAL;

	tt.r_ll = path->cifp_point;
	tt.r_ur = path->cifp_next->cifp_next->cifp_point;
	GeoCanonicalRect(&tt, &tr);

//	TxPrintf("CIF read: Triangle %s %c at (%d, %d) plane %x\n",
//		(type & TT_SIDE) ? "right" : "left", (type & TT_DIRECTION)
//		? '\\' : '/', tt.r_xbot, tt.r_ybot, plane);

	/* Final check---ensure that rectangle is not degenerate */

        if (plane && (tr.r_xtop - tr.r_xbot > 0) && (tr.r_ytop - tr.r_ybot > 0))
            DBNMPaintPlane(plane, type, &tr, resultTbl, ui);
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * CIFEdgeDirection --
 *
 * 	This procedure assigns a direction to the given edge.
 *
 * Results:
 *	CIF_ZERO	if the two points are the same
 *	CIF_LEFT	if the edge goes left
 *	CIF_UP		if the edge goes up
 *	CIF_RIGHT	if the edge goes right
 *	CIF_DOWN	if the edge goes down
 *	CIF_DIAG	if the edge is non-manhattan
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
CIFEdgeDirection(first, last)
    CIFPath *first, *last;		/* Edge to be categorized. */
{

    if (first->cifp_x < last->cifp_x)
    {
	if (first->cifp_y < last->cifp_y)
	    return CIF_DIAG_UR;
	if (first->cifp_y > last->cifp_y)
	    return CIF_DIAG_DR;
	return CIF_RIGHT;
    }
    if (first->cifp_x > last->cifp_x)
    {
	if (first->cifp_y < last->cifp_y)
	    return CIF_DIAG_UL;
	if (first->cifp_y > last->cifp_y)
	    return CIF_DIAG_DL;
	return CIF_LEFT;
    }
    if (first->cifp_y < last->cifp_y)
	return CIF_UP;
    if (first->cifp_y > last->cifp_y)
	return CIF_DOWN;
    return CIF_ZERO;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFCleanPath --
 *
 *	Removes a edge in a path if it has zero length (repeated points).
 *	Combines two consecutive edges if their direction is the same,
 *	and their direction is manhattan.
 *	CIFCleanPath assumes that the path is closed, and will eliminate
 *	the last edge if its direction is the same as the first.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May delete points in the path.
 *
 * ----------------------------------------------------------------------------
 */

void
CIFCleanPath(pathHead)
    CIFPath *pathHead;
{
    CIFPath *next, *path, *prev, *last;
    int dir1, dir2;

    if (!pathHead) return;
    prev = pathHead;
    path = prev->cifp_next;
    if (!path) return;
    while((dir1 = CIFEdgeDirection(prev, path)) == CIF_ZERO)
    {
	/* This is a repeated point. */
	next = path->cifp_next;
	prev->cifp_next = next;
	freeMagic((char *) path);
	path = next;
	if (!path) return;
    }
	 
    while (next = path->cifp_next)
    {
	if ((dir2 = CIFEdgeDirection(path, next)) == CIF_ZERO)
	{
	    /* This is a repeated point. */
	    path->cifp_next = next->cifp_next;
	    freeMagic((char *) next);
	    continue;
	}

	/* Skip any non-manhattan (diagonal) edges. */
	if (dir2 >= CIF_DIAG)
	    goto path_inc;

	if (dir1 == dir2)
	{
	    /* The middle point must go. */
	    prev->cifp_next = next;
	    freeMagic((char *) path);
	    path = next;
	    dir1 = CIFEdgeDirection(prev, path);
	    continue;
	}
path_inc:
	dir1 = dir2;
	prev = path;
	path = next;
    }

    /* Ensure that the path has more than one point. */
    if (!pathHead->cifp_next)
    {
	/* Ensure that the resulting path is closed. */
	if ((pathHead->cifp_x != path->cifp_x) ||	
	    (pathHead->cifp_y != path->cifp_y))
	{
	    next = (CIFPath *) mallocMagic((unsigned) (sizeof (CIFPath)));
	    next->cifp_x = pathHead->cifp_x;
	    next->cifp_y = pathHead->cifp_y;
	    next->cifp_next = (CIFPath *) 0;
	    path->cifp_next = next;
	    prev = path;
	    path = next;
	    dir1 = CIFEdgeDirection(prev, path);
	}
	if ((dir2 = CIFEdgeDirection(pathHead, pathHead->cifp_next)) <
	    CIF_DIAG)
	{
	    /* We have at least two edges in the path. We have to */
	    /* fix the first edge and eliminate the last edge if */
	    /* the first and last edge have the same direction. */
	    if (dir1 == dir2)
	    {
		pathHead->cifp_x = prev->cifp_x;
		pathHead->cifp_y = prev->cifp_y;
		prev->cifp_next = (CIFPath *) 0;
		freeMagic((char *) path);
	    }
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFFreePath --
 *
 * 	This procedure frees up a path once it has been used.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All the elements of path are returned to the storage allocator.
 *
 * ----------------------------------------------------------------------------
 */

void
CIFFreePath(path)
    CIFPath *path;		/* Path to be freed. */
{
    while (path != NULL)
    {
	freeMagic((char *) path);
	path = path->cifp_next;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifCommandError --
 *
 * 	This procedure is called when unknown CIF commands are found
 *	in CIF files.  It skips the command and advances to the next
 *	command.
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
cifCommandError()
{
    CIFReadError("unknown command `%c'; ignored.\n" , PEEK());
    CIFSkipToSemi();
}


/*
 * ----------------------------------------------------------------------------
 *
 * cifParseEnd --
 *
 * 	This procedure processes the "end" statement in a CIF file
 *	(it ignores it).
 *
 * Results:
 *	Always returns TRUE.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
cifParseEnd()
{
    TAKE();
    CIFSkipBlanks();
    if (PEEK() != EOF)
    {
	CIFReadError("End command isn't at end of file.\n");
	return FALSE;
    }
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifParseComment --
 *
 * 	This command skips over user comments in CIF files.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
cifParseComment()
{
    int		opens;
    int		ch;
    
	/*
	 *	take the '('
	 */
    TAKE();
    opens = 1;
    do
    {
	ch = TAKE();
	if (ch == '(')
	    opens++;
	else if (ch == ')')
	    opens--;
	else if (ch == '\n')
	{
	    cifLineNumber++;
	}
	else if (ch == EOF)
	{
	    CIFReadError("(comment) extends to end of file.\n");
	    return FALSE;
	}
    } while (opens > 0);
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFDirectionToTrans --
 *
 * 	This procedure is used to convert from a direction vector
 *	to a Magic transformation.  The direction vector is a point
 *	giving a direction from the origin.  It better be along
 *	one of the axes.
 *
 * Results:
 *	The return value is the transformation corresponding to
 *	the direction, or the identity transform if the direction
 *	isn't along one of the axes.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

Transform *
CIFDirectionToTrans(point)
    Point *point;		/* Direction vector from origin. */
{
    if ((point->p_x != 0) && (point->p_y == 0))
    {
	if (point->p_x > 0)
	    return &GeoIdentityTransform;
	else return &Geo180Transform;
    }
    else if ((point->p_y != 0) && (point->p_x == 0))
    {
	if (point->p_y > 0)
	    return &Geo270Transform;
	else return &Geo90Transform;
    }
    CIFReadError("non-manhattan direction vector (%d, %d); ignored.\n",
	point->p_x, point->p_y);
    return &GeoIdentityTransform;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFParseTransform --
 *
 * 	This procedure is called to read in a transform from a
 *	CIF file.
 *
 * Results:
 *	TRUE is returned if the parse completed successfully, and
 *	FALSE is returned otherwise.
 *
 * Side effects:
 *	The parameter pointed to by transformp is modified to
 *	contain the transform indicated by the CIF file.
 *
 * ----------------------------------------------------------------------------
 */

bool
CIFParseTransform(transformp)
    Transform	*transformp;
{
    char	ch;
    Point	point;
    Transform	tmp;
    int		savescale;

    *transformp = GeoIdentityTransform;
    CIFSkipBlanks();
    for (ch = PEEK() ; ch != ';' ; ch = PEEK())
    {
	switch (ch)
	{
	    case 'T':
		    TAKE();
		    if (!CIFParsePoint(&point, 1))
		    {
			CIFReadError("translation, but no point.\n");
			CIFSkipToSemi();
			return FALSE;
		    }
		    GeoTranslateTrans(transformp, point.p_x, point.p_y, &tmp);
		    *transformp = tmp;
		    break;
	    case 'M':
		    TAKE();
		    CIFSkipBlanks();
		    ch = PEEK();
		    if (ch == 'X')
		        GeoTransTrans(transformp, &GeoSidewaysTransform, &tmp);
		    else if (ch == 'Y')
		        GeoTransTrans(transformp, &GeoUpsideDownTransform,
				&tmp);
		    else
		    {
			CIFReadError("mirror, but not in X or Y.\n");
			CIFSkipToSemi();
			return FALSE;
		    }
		    TAKE();
		    *transformp = tmp;
		    break;
	    case 'R':
		    TAKE();
		    if (!CIFParseSInteger(&point.p_x) ||
			    !CIFParseSInteger(&point.p_y))
		    {
			CIFReadError("rotation, but no direction.\n");
			CIFSkipToSemi();
			return FALSE;
		    }
		    GeoTransTrans(transformp, CIFDirectionToTrans(&point),
			    &tmp);
		    *transformp = tmp;
		    break;
	    default:
		    CIFReadError("transformation expected.\n");
		    CIFSkipToSemi();
		    return FALSE;
	}
	CIFSkipBlanks();
    }

    /* Before returning, we must scale the transform into Magic units. */

    transformp->t_c = CIFScaleCoord(transformp->t_c, COORD_EXACT);
    savescale = cifCurReadStyle->crs_scaleFactor;
    transformp->t_f = CIFScaleCoord(transformp->t_f, COORD_EXACT);
    if (savescale != cifCurReadStyle->crs_scaleFactor)
	transformp->t_c *= (savescale / cifCurReadStyle->crs_scaleFactor);

    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFParseCommand --
 *
 * 	Parse one CIF command and farm it out to a routine to handle
 *	that command.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May modify the contents of cifReadCellDef by painting or adding
 *	new uses or labels.  May also create new CellDefs.
 *
 * ----------------------------------------------------------------------------
 */

void
CIFReadFile(file)
    FILE *file;			/* File from which to read CIF. */
{
    /* We will use 1-word CIF numbers as keys in this hash table */
    CIFReadCellInit(1);

    if (cifCurReadStyle == NULL)
    {
	TxError("Don't know how to read CIF:  nothing in tech file.\n");
	return;
    }
    TxPrintf("Warning: CIF reading is not undoable!  I hope that's OK.\n");
    UndoDisable();

    cifTotalWarnings = 0;
    cifTotalErrors = 0;
    CifPolygonCount = 0;

    cifInputFile = file;
    cifReadScale1 = 1;
    cifReadScale2 = 1;
    cifParseLaAvail = FALSE;
    cifLineNumber = 1;
    cifReadPlane = (Plane *) NULL;
    cifCurLabelType = TT_SPACE;
    while (PEEK() != EOF)
    {
	if (SigInterruptPending) goto done;
	CIFSkipBlanks();
	switch (PEEK())
	{
	    case EOF:
		    break;
	    case ';':
		    break;
	    case 'B':
		    (void) CIFParseBox();
		    break;
	    case 'C':
		    (void) CIFParseCall();
		    break;
	    case 'D':
		    TAKE();
		    CIFSkipBlanks();
		    switch (PEEK())
		    {
			case 'D':
				    (void) CIFParseDelete();
				    break;
			case 'F':
				    (void) CIFParseFinish();
				    break;
			case 'S':
				    (void) CIFParseStart();
				    break;
			default:
				    cifCommandError();
				    break;
		    }
		    break;
	    case 'E':
		    (void) cifParseEnd();
		    goto done;
	    case 'L':
		    (void) CIFParseLayer();
		    break;
	    case 'P':
		    (void) CIFParsePoly();
		    break;
	    case 'R':
		    (void) CIFParseFlash();
		    break;
	    case 'W':
		    (void) CIFParseWire();
		    break;
	    case '(':
		    (void) cifParseComment();
		    break;
	    case '0': case '1': case '2': case '3': case '4':
	    case '5': case '6': case '7': case '8': case '9':
		    (void) CIFParseUser();
		    break;
	    default:
		    cifCommandError();
		    break;
	}
	CIFSkipSemi();
    }

    CIFReadError("no \"End\" statement.\n");

    done:
    CIFReadCellCleanup(0);
    UndoEnable();
}
