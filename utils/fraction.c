/*
 * fraction.c -
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
 * This file contains procedures for handling fractions
 * Written by R. Timothy Edwards
 * Johns Hopkins University Applied Physics Laboratory
 * and MultiGiG Ltd.
 * January, 2002
 *
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/fraction.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "utils/magic.h"
#include "utils/geometry.h"

/*
 * ----------------------------------------------------------------------------
 *
 * FindGCF --
 *
 *	Your basic greatest-common-factor routine.  Something I invented
 *	one day as a teenager but is generally attributed to Euclid, or
 *	something like that.
 *
 * Results:
 *	The greatest (positive) common factor of the two integer arguments
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
FindGCF(a, b)
    int a, b;
{
    int a_mod_b, bp;

    bp = abs(b);
    if ((a_mod_b = (abs(a)) % bp) == 0) return (bp);
    else return (FindGCF(bp, a_mod_b));
}

/*
 * ----------------------------------------------------------------------------
 *
 * ReduceFraction --
 *
 *	Fraction reducer
 *
 *
 * Results:
 *	None
 *
 * Side effects:
 *	The fraction (*n / *d) as represented by integer pointer arguments
 *	n and d is reduced by the greatest common factor (GCF) of both.
 *
 * ----------------------------------------------------------------------------
 */

void
ReduceFraction(n, d) 
    int *n, *d;
{
    int c;

    c = FindGCF(*n, *d);

    if (c != 0)
    {
        *n /= c;
        *d /= c;
    }
    return;
}

