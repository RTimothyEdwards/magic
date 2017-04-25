/*
 * parser.c --
 *
 * Handles textual parsing.
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

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/parser.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <ctype.h>

#include "utils/magic.h"
#include "textio/textio.h"

#define ISEND(ch)	((ch == '\0') || (ch == ';'))


/*
 * ----------------------------------------------------------------------------
 * ParsSplit:
 *
 *	Take a string and split it into argc, argv format.
 *	The result of this split is stored back into the original string,
 *	and an array of pointers is set up to index into it.  Note that
 *	the contents of the original string are lost.
 *
 *      rajit@cs.caltech.edu: changed it so that quotes typed in by the
 *      user remain there.
 *
 * Results:
 *	TRUE if everything went OK.
 *	FALSE otherwise.
 *
 * Side effects:
 *	argc is filled in, str is changed, argv[] is changed to point into str
 * ----------------------------------------------------------------------------
 */

bool
ParsSplit(str, maxArgc, argc, argv, remainder)
    char *str;
    int maxArgc;
    int *argc;
    char **argv;
    char **remainder;
{
    char **largv;
    char *newstrp;
    char *strp;
    char terminator;

    *argc = 0;
    largv = argv;
    newstrp = str;
    strp = str;

    while (isspace(*strp) && (!ISEND(*strp)) ) strp++;

    terminator = *strp;
    *largv = strp;
    while (!ISEND(*strp))
    {
	/*
	 * hackhackhackhackhack
	 * rajit@cs.caltech.edu
	 * okay. so we're changing what ' ' can be used for.
	 * One can only have a _SINGLE_ character between the
	 * two quotes. i.e. ' ' are used to quote characters.
	 */
#ifdef SCHEME_INTERPRETER
	if (*strp == '\'' && *(strp+1) && *(strp+2) == '\'')
        {
	  strp++;
	  *newstrp++ = *strp++;
	  strp++;
	}
	/* 
	 * rajit@cs.caltech.edu
	 * keep " " around strings to distinguish symbols from strings.
	 * the lisp evaluator strips these quotes after parsing the
	 * string.
	 */
	else if (*strp == '"')
#else
	if (*strp == '"' || *strp == '\'')
#endif
	{
	    char compare;

#ifdef SCHEME_INTERPRETER
	    *newstrp++ = *strp;
#endif
	    compare = *strp++;
	    while ( (*strp != compare) && (*strp != '\0'))
	    {
		if (*strp == '\\')
#ifndef SCHEME_INTERPRETER
		    strp++;
#else
		    *newstrp++ = *strp++;
#endif
		*newstrp++ = *strp++;
	    }
	    if (*strp == compare)
#ifndef SCHEME_INTERPRETER
	      strp++;
#else
	      *newstrp++ = *strp++;
#endif
	    else
#ifndef SCHEME_INTERPRETER
		TxError("Unmatched %c in string, %s.\n", compare,
			"I'll pretend that there is one at the end");
#else
	    {
	        TxError ("Unmatched %c in string.\n", compare);
		return FALSE;
	    }
#endif
	}
	else
	    *newstrp++ = *strp++;
	if (isspace(*strp) || (ISEND(*strp)))
	{
	    while (isspace(*strp) && (!ISEND(*strp))) strp++;
	    terminator = *strp;
	    *newstrp++ = '\0';
	    (*argc)++;
	    if (*argc < maxArgc)
	    {
		*++largv = newstrp;
	    }
	    else 
	    {
		TxError("Too many arguments.\n");
		*remainder = NULL;
		return FALSE;
	    }
	}
    }
    
    ASSERT(remainder != (char **) NULL, "ParsSplit");

    if (terminator != '\0')
    {
	/* save other commands (those after the ';') for later parsing */
	strp++;
	while (isspace(*strp) && (!ISEND(*strp))) strp++;
	*remainder = strp;
    }
    else
	*remainder = NULL;

    return TRUE;
}
