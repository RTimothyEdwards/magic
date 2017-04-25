/*
 * LIBdbio.c --
 *
 * File that only goes in libmagicutils.a to define procedures
 * referenced from dbio that might not be defined elsewhere.
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

#ifndef	lint
static char rcsid[] = "$Header: /usr/cvsroot/magic-8.0/utils/LIBdbio.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif	/* not lint */

#include <stdio.h>
#include <unistd.h>

#include "utils/magic.h"


/*
 * ----------------------------------------------------------------------------
 *
 * flock_open
 *
 * Like standard flock_open, except that it is always called read-only,
 * so it simply calls a normal fopen().
 *
 * Results:
 *	Returns a pointer to the opened file
 *
 * Side effects:
 *	None
 *
 * ----------------------------------------------------------------------------
 */

FILE *
flock_open(filename, mode, is_locked)
    char *filename;
    char *mode;
    bool *is_locked;
{
    FILE *f;

    if (is_locked) *is_locked = FALSE;
    f = fopen(filename, mode);
    return(f);
}
