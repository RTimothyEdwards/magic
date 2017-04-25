/*
 * lookupany.c --
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
 * Full rights reserved.
 *
 * This file contains a single routine used to find a string in a
 * table which contains the supplied character.
 */

#include <string.h>

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/lookupany.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */


/*
 * ----------------------------------------------------------------------------
 * LookupAny --
 *
 * Look up a single character in a table of pointers to strings.  The last
 * entry in the string table must be a NULL pointer.
 * The index of the first string in the table containing the indicated
 * character is returned.
 *
 * Results:
 *	Index of the name supplied in the table, or -1 if the name
 *	is not found.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
LookupAny(c, table)
    char c;
    char **table;
{
    char **tp;

    for (tp = table; *tp; tp++)
	if (strchr(*tp, c) != (char *) 0)
	    return (tp - table);

    return (-1);
}
