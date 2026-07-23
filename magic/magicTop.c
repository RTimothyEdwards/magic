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

/* C99 compat */
#include "utils/main.h"

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
main(int argc, char *argv[])
{
    magicMain(argc, argv);
    exit(0);
}

/* MagicVersion / MagicRevision / MagicCompileTime are defined once in
 * utils/buildinfo.c -- the single unit compiled with the version defines --
 * and declared in utils/magic_buildinfo.h.  They used to be defined here (and,
 * for the Tcl build, in tclmagic.c under a MAGIC_WRAPPER guard); consolidating
 * them removes the duplicate-symbol hazard and keeps the volatile build-date
 * define off every compile command line.
 */
