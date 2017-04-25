/*
 * strdup.c --
 *
 * Return a malloc'd copy of a string.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/strdup.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>		/* for strtod */
#include <string.h>
#include <ctype.h>

#include "utils/magic.h"
#include "utils/malloc.h"


/*
 * ----------------------------------------------------------------------------
 * StrDup --
 *
 * Return a malloc'd copy of a string.
 *
 * Results:
 *	Returns a pointer to a newly malloc'd character array just
 *	large enough to hold the supplied string and its trailing
 *	null byte, which contains a copy of the supplied string.
 *
 * Side effects:
 *	malloc's a character array large enough to hold str, and
 *	copies str into it, unless str is NULL.  If str is NULL, no
 *	malloc is done.  Also, if oldstr is non-NULL, then a) if
 *	*oldstr is not NULL, frees the storage allocated to oldstr,
 *	and b) sets *oldstr to the new string allocated, or to
 *	NULL if str is NULL.
 * ----------------------------------------------------------------------------
 */

char *
StrDup(oldstr, str)
    char **oldstr;
    char *str;
{
    char *newstr;

    if (str != NULL)
    {
	newstr = (char *) mallocMagic((unsigned) (strlen(str) + 1));
	(void) strcpy(newstr, str);
    }
    else newstr = NULL;
    if (oldstr != (char **) NULL)
    {
	if (*oldstr != NULL)
	    freeMagic(*oldstr);
	*oldstr = newstr;
    }

    return (newstr);
}


/*
 * ----------------------------------------------------------------------------
 * StrIsWhite:
 *
 *	Check to see if a string is all white space or is a comment.
 *
 * Results:
 *	True if it is all white, false otherwise.
 *
 * Side effects:
 *	none.
 * ----------------------------------------------------------------------------
 */

bool
StrIsWhite(line, commentok)
    char *line;
    bool commentok;	/* TRUE means # comments are considered all-white */
{
    if ( (*line == '#') && commentok)
	return TRUE;
    while(*line)
    {
	if ( !isspace(*line) && (*line != '\n') )
	    return FALSE;
	line++;
    }
    return TRUE;
}


/*
 * ----------------------------------------------------------------------------
 *
 * StrIsInt --
 *
 * 	Check a string for being an integer.
 *
 * Results:
 *	TRUE if the string is a well-formed integer, FALSE otherwise.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
StrIsInt(s)
    char *s;
{
    if (*s == '-' || *s == '+') s++;
    while (*s)
	if (!isdigit(*s++))
	    return (FALSE);

    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * StrIsNumeric --
 *
 *	Check a string for being any numeric value.
 *
 * Results:
 *	TRUE if the string can be interpreted as a numeric value,
 *	FALSE otherwise.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
StrIsNumeric(s)
    char *s;
{
    double result;
    char *endptr;
    
    result = strtod(s, &endptr);

    if (endptr == s) return FALSE;

    return TRUE;
}

