/*
 * EFerr.c -
 *
 *	Contains only the routine efReadError().  This used to be in EFread.c,
 *	but including Tcl/Tk stuff caused definition conflicts.  So now it
 *	gets its own file.  Note that *printf routines have been changed to
 *	the Tx* print routines for compatibility with the Tcl-based version.
 *	Note also that the standalone executables ext2sim and ext2spice get
 *	the Tx* functions from utils/LIBtextio.c, whereas the built-in
 *	"extract" function gets them from textio/txOutput.c.  In the Tcl
 *	version, all these functions are built in, so utils/LIBtextio.c is
 *	not compiled or linked.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/extflat/EFerr.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "textio/textio.h"

extern char *efReadFileName;
extern int efReadLineNum;

#ifdef MAGIC_WRAPPER
extern int Tcl_printf();
#endif


/*
 * ----------------------------------------------------------------------------
 *
 * efReadError --
 *
 * Complain about an error encountered while reading an .ext file.
 * Called with a variable number of arguments.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Prints an error message to stderr, complete with offending
 *	filename and line number.
 *
 * ----------------------------------------------------------------------------
 */

void
efReadError(char *fmt, ...)
{
    va_list args;

    TxError("%s, line %d: ", efReadFileName, efReadLineNum);
    va_start(args, fmt);
#ifdef MAGIC_WRAPPER
    Tcl_printf(stderr, fmt, args);
#else
    vfprintf(stderr, fmt, args);
#endif
    va_end(args);
    TxFlushErr();
}
