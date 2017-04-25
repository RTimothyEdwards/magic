/* port.c
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
 * This file contains routines that are needed when porting between machines.
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/port.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "utils/magic.h"
#include "utils/hash.h"
#include "utils/malloc.h"

/*-------------------------------------------------------------------
 * MagAtof --
 *	Magic's own atof function.
 *	Convert a string to a single-precision floating point number.
 *
 * Results:
 *	A floating point number.
 *
 * Special Features:
 *	No error is produced if the string isn't a valid number.
 *-------------------------------------------------------------------
 */

float
MagAtof(s)
    char *s;
{
#ifdef linux
    float flt;
    if (sscanf(s, "%f", &flt) == 1) return flt;
    else return (float)(-1.0);
#else
    return (float)atof(s);
#endif
}
