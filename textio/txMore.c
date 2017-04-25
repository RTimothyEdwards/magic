/* txMore.c -
 *
 *	Routine to pause until user hits `return'
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/textio/txMore.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"


/*
 * ----------------------------------------------------------------------------
 *
 * TxMore --
 *
 * Wait for the user to hit a carriage-return.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Prints a "<mesg> --more--" prompt.
 *
 * ----------------------------------------------------------------------------
 */

void
TxMore(mesg)
    char *mesg;
{
    char prompt[512];
    char line[512];

    (void) sprintf(prompt, "%s --more-- (Hit <RETURN> to continue)", mesg);
    (void) TxGetLinePrompt(line, sizeof line, prompt);
}
