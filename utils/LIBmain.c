/*
 * LIBmain.c --
 *
 * File that only goes in libmagicutils.a to define procedures
 * referenced from main that might not be defined elsewhere.
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
static char rcsid[] = "$Header: /usr/cvsroot/magic-8.0/utils/LIBmain.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif	/* not lint */

#include <stdio.h>
#include <stdlib.h>


/*
 * ----------------------------------------------------------------------------
 *
 * MainExit --
 *
 * Exit.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Exits.
 *
 * ----------------------------------------------------------------------------
 */

void
MainExit(code)
    int code;
{
    exit (code);
}

char AbortMessage[500] = "";


/*
 * ----------------------------------------------------------------------------
 *
 * niceabort --
 *
 * Simple version of niceabort, which dumps core and terminates the program.
 * Magic uses the more complex version found in misc/niceabort.c.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Dumps core and exits.
 * ----------------------------------------------------------------------------
 */

void
niceabort()
{
    fprintf(stderr, "A major internal inconsistency has been detected:\n");
    fprintf(stderr, "        %s\n\n", AbortMessage);
    abort();                    /* core dump! */
}
