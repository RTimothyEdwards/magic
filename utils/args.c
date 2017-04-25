/*
 * args.c --
 *
 * Procedures to assist in command-line argument processing.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/args.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/utils.h"

/*
 * ----------------------------------------------------------------------------
 *
 * ArgStr --
 *
 * Process a single argument that is supposed to have a string value.
 * A string argument can appear in two ways:
 *
 *	-avalue		(a single element of argv)
 *	-a value	(two elements of argv)
 *
 * Both are recognized.
 *
 * Results:
 *	Returns a pointer to the value, or NULL if there wasn't one.
 *
 * Side effects:
 *	Complains if there was no value.
 *	Leaves *pargc and *pargv updated if the string was in the
 *	next element of argv.
 *
 * ----------------------------------------------------------------------------
 */

char *
ArgStr(pargc, pargv, argType)
    int *pargc;
    char ***pargv;
    char *argType;	/* For error messages: what the following string is
			 * supposed to be interpreted as.
			 */
{
    char **argv = *pargv;
    char *result;

    if (argv[0][2])
	return (&argv[0][2]);

    if ((*pargc)-- > 0)
    {
	result = *++argv;
	*pargv = argv;
	return (result);
    }

    TxError("-%c requires a following %s\n", argv[0][1], argType);
    return (NULL);
}
