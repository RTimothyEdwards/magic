/*
 * getrect.c -
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
 *			Lawrence Livermore National Laboratory
 *
 * This file contains a procedure, GetRect(), for gobbling up
 * four integers from an input file.
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/getrect.c,v 1.3 2010/06/24 12:37:58 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <ctype.h>

#include "utils/magic.h"
#include "utils/geometry.h"

/*
 * ----------------------------------------------------------------------------
 *
 * GetRect --
 *
 * Parse a rectangle from a file and fill in the supplied rect struct.
 * We assume that the rectangle consists of four decimal numbers, each
 * separated from the next by a single space, and terminated by a newline.
 *
 * ESTHETIC WARNING:
 *	The algorithm used here is gross but extremely fast.
 *
 * Results:
 *	FALSE on end of file or error, TRUE otherwise.
 *	If nonmanhattan extensions are enabled, it returns a
 *	direction result for triangles.
 *
 * Side effects:
 *	Fills in the contents of *rect.
 *
 * ----------------------------------------------------------------------------
 */

#define	RECTBUFTHRESHOLD	100

int
GetRect(fin, skip, rect, scalen, scaled)
    FILE *fin;
    int skip;			/* Number of bytes to skip before rect */
    Rect *rect;	/* Pointer to rectangle to be filled in */
    int scalen;			/* Scale up by this amount */
    int scaled;			/* Scale down by this amount */
{
    int n, c;
    char *cp;
    bool isNegative;
    int dir = 0x1;

    while (skip-- > 0)
	(void) getc(fin);

    if (isNegative = ((c = getc(fin)) == '-')) c = getc(fin);
    for (n = 0; isdigit(c); n = n * 10 + c - '0', c = getc(fin))
	/* Nothing */;
    rect->r_xbot = isNegative ? -n : n;
    if (!isspace(c)) goto bad;
    while ((c = getc(fin)) != EOF && isspace(c)) /* Nothing */;

    if (isNegative = (c == '-')) c = getc(fin);
    for (n = 0; isdigit(c); n = n * 10 + c - '0', c = getc(fin))
	/* Nothing */;
    rect->r_ybot = isNegative ? -n : n;
    if (!isspace(c)) goto bad;
    while ((c = getc(fin)) != EOF && isspace(c)) /* Nothing */;

    if (isNegative = (c == '-')) c = getc(fin);
    for (n = 0; isdigit(c); n = n * 10 + c - '0', c = getc(fin))
	/* Nothing */;
    rect->r_xtop = isNegative ? -n : n;
    if (!isspace(c)) goto bad;
    while ((c = getc(fin)) != EOF && isspace(c)) /* Nothing */;

    if (isNegative = (c == '-')) c = getc(fin);
    for (n = 0; isdigit(c); n = n * 10 + c - '0', c = getc(fin))
	/* Nothing */;
    rect->r_ytop = isNegative ? -n : n;

    if (scalen > 1)
    {
	rect->r_xbot *= scalen;
	rect->r_ybot *= scalen;
	rect->r_xtop *= scalen;
	rect->r_ytop *= scalen;
    }
    if (scaled > 1)
    {
	rect->r_xbot /= scaled;
	rect->r_ybot /= scaled;
	rect->r_xtop /= scaled;
	rect->r_ytop /= scaled;
    }

    while (c != EOF && c != '\n')
    {
	c = getc(fin);
	switch ((char)c)
	{
	    case 's':
		dir |= 0x2;
		break;
	    case 'e':
		dir |= 0x4;
		break;
	}
    }
    return dir;

bad:
    while (c != EOF && c != '\n')
	c = getc(fin);
    return (FALSE);
}
