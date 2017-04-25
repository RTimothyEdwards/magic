/* magicTop.c -
 *
 *	The top level of the Magic VLSI Layout system.
 *
 *	This top level is purposely very short and is located directly in
 *	the directory used to remake Magic.  This is so that other
 *	programs may use all of Magic as a library but still provide their
 *	own 'main' procedure.  
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/magic/magicTop.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>

#include "utils/magic.h"
#include "utils/malloc.h"

/*---------------------------------------------------------------------------
 * main:
 *
 *	Top level of Magic.  Do NOT add code to this routine, as all code
 *	should go into `main.c' in the `main' directory.
 *
 * Results:	
 *	None.
 *
 * Side Effects:
 *	Invokes Magic to edit a VLSI design.
 *
 *----------------------------------------------------------------------------
 */

int
main(argc, argv)
    int argc;
    char *argv[];
{
    magicMain(argc, argv);
    exit(0);
}

/* String containing the version number of magic.  Don't change the string
 * here, nor its format.  It is updated by the Makefile in this directory. 
 *
 * The version string originates at the top of scripts/config.
 */

char *MagicVersion = MAGIC_VERSION;
char *MagicRevision = MAGIC_REVISION;
char *MagicCompileTime = MAGIC_DATE;
