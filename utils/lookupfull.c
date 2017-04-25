/*
 * lookupfull.c --
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
 * This file contains a single routine used to look up a string in
 * a table with no abbreviations allowed.
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/lookupfull.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"


/*
 * ----------------------------------------------------------------------------
 * LookupFull --
 *
 * Look up a string in a table of pointers to strings.  The last
 * entry in the string table must be a NULL pointer.
 * This is much simpler than Lookup() in that it does not
 * allow abbreviations.  It does, however, ignore case.
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
LookupFull(name, table)
    char *name;
    char **table;
{
    char **tp;

    for (tp = table; *tp; tp++)
    {
	if (strcmp(name, *tp) == 0)
	    return (tp - table);
	else
	{
	    char *sptr, *tptr;
	    for (sptr = name, tptr = *tp; ((*sptr != '\0') && (*tptr != '\0'));
			sptr++, tptr++)
		if (toupper(*sptr) != toupper(*tptr))
		    break;
	    if ((*sptr == '\0') && (*tptr == '\0'))
		return (tp - table);
	}
    }

    return (-1);
}

/*---------------------------------------------------------
 *
 * LookupStructFull --
 *
 * Searches a table of structures, each of which contains a string
 * pointer as its first element, in a manner similar to that of Lookup()
 * above.  Each structure in the table has the following form:
 *
 *	struct
 *	{
 *		char *string;
 *		... rest of structure
 *	};
 *
 * The 'string' field of each structure is matched against the
 * argument 'str'.  The size of a single structure is given by
 * the argument 'size'.
 *
 * This is much simpler than LookupStruct() in that it does not
 * allow abbreviations.
 *
 * Results:
 *	Index of the name supplied in the table, or -1 if the name
 *	is not found.
 *
 * Side Effects:
 *	None.
 *
 *---------------------------------------------------------
 */

int
LookupStructFull(str, table, size)
    char str[];		/* Pointer to a string to be looked up */
    char **table;	/* Pointer to an array of structs containing string
			 * pointers to valid commands.  
			 * The last table entry should have a NULL
			 * string pointer.
			 */
    int	size;		/* The size, in bytes, of each table entry */
{
  char **entry;
  int pos;

  for(entry=table, pos=0; *entry!=NULL; pos++) {
    if( strcmp(str, *entry) == 0 ) {
      return pos;
    }
    entry = (char **)((long)entry + (long)size); 
  }

  return -1;
}
