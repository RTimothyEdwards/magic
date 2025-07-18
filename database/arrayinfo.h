/*
 * arrayinfo.h --
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
 */

#ifndef _MAGIC__DATABASE__ARRAYINFO_H
#define _MAGIC__DATABASE__ARRAYINFO_H

/*
 * Description of an array.
 * The bounds xlo .. xhi and ylo .. yhi are transformed versions
 * of the bounds xlo' .. xhi' and ylo' .. yhi' supplied by the
 * user:
 *
 * User supplies:
 *	xlo'	index of leftmost array element in root coordinates
 *	xhi'	index of rightmost array element in root coordinates
 *	ylo'	index of bottommost array element in root coordinates
 *	yhi'	index of topmost array element in root coordinates
 *
 * There is no constraint on the order of any of these indices; xlo' may
 * be less than, equal to, or greater than xhi', and similarly for ylo'
 * and yhi'.
 *
 * In addition, the separations xsep and ysep are transformed versions
 * of the separations xsep' and ysep' supplied by the user:
 *
 * User supplies:
 *	xsep'	(positive) X spacing between array elements in root coords
 *	ysep'	(positive) Y spacing between array elements in root coords
 *
 * When the array is made via DBMakeArray, both the indices and the spacings
 * are transformed down to the coordinates of the CellDef that is the child
 * of the use containing the ArrayInfo.
 *
 * The significance of the various values is as follows:  the [xlo, ylo]
 * element of the array is gotten by transforming the celldef by the
 * transformation in the celluse.  the [x, y] element is gotten by
 * transforming the celldef by xsep*abs(x-xlo) in x, ysep*abs(y-ylo) in
 * y, and then transforming by the transformation in the celluse.
 */

typedef struct
{
    int		ar_xlo, ar_xhi;		/* Inclusive low/high X bounds */
    int		ar_ylo, ar_yhi;		/* Inclusive low/high Y bounds */
    int		ar_xsep, ar_ysep;	/* X,Y sep between array elements */
} ArrayInfo;

#endif /* _MAGIC__DATABASE__ARRAYINFO_H */
