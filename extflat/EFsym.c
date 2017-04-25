/*
 * EFsym.c -
 *
 * Procedures for managing symbolic names.
 * Such names are used to assign values to things like transistor dimensions.
 * When an attribute of the form "ext:what=value" is attached to a fet, the
 * corresponding quantity 'what' for that fet is set to 'value'.  The 'value'
 * can be symbolic, allowing us to change it during flattening without having
 * to re-extract.  The binding between symbolic names and numeric values is
 * set up by calls to efSymAdd().  Recognized values of 'what' are:
 *
 *	w, l
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/extflat/EFsym.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "utils/hash.h"
#include "utils/malloc.h"
#include "utils/utils.h"
#include "extflat/extflat.h"
#include "extflat/EFint.h"

/* Forward declarations */
bool efSymAdd();

HashTable efSymHash;

/*
 * ----------------------------------------------------------------------------
 *
 * efSymInit --
 *
 * Initialize the hash table 'efSymHash' used for symbolic name assignments.
 * Called by EFInit().
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

void
efSymInit()
{
    HashInit(&efSymHash, 16, HT_STRINGKEYS);
}

/*
 * ----------------------------------------------------------------------------
 *
 * efSymAddFile --
 *
 * Read the file 'name' for symbol assignments.  Each line of the file
 * should be of the form name=value.  We add each symbol 'name' to efSymHash
 * with value 'value'.
 *
 * Results:
 *	TRUE on success, FALSE on an error in opening 'name'.
 *
 * Side effects:
 *	Adds symbols to the hash table.
 *	Complains if we can't open the file or if errors are encountered
 *	while reading it.
 *
 * ----------------------------------------------------------------------------
 */

bool
efSymAddFile(name)
    char *name;
{
    char line[1024], *cp;
    int lineNum;
    FILE *f;

    f = fopen(name, "r");
    if (f == NULL)
    {
	perror(name);
	return FALSE;
    }

    for (lineNum = 1; fgets(line, sizeof line, f); lineNum++)
    {
	if (cp = strchr(line, '\n'))
	    *cp = '\0';
	if (!efSymAdd(line))
	    TxError("Error at line %d of %s\n", lineNum, name);
    }
    fclose(f);
    return TRUE;
}


/*
 * ----------------------------------------------------------------------------
 *
 * efSymAdd --
 *
 * Given a string of the form name=value, add the symbol 'name' to efSymHash
 * with value 'value'.
 *
 * Results:
 *	TRUE normally, FALSE if the input was malformed or resulted in
 *	assigning a new value to an existing name.
 *
 * Side effects:
 *	Adds a symbol to the hash table.
 *	Complains if we have to return FALSE.
 *
 * ----------------------------------------------------------------------------
 */

bool
efSymAdd(str)
    char *str;
{
    HashEntry *he;
    char *value;

    value = strchr(str, '=');
    if (value == NULL)
    {
	TxError("Missing '=' in symbol assignment\n");
	return FALSE;
    }

    value++;
    if (!StrIsInt(value))
    {
	TxError("Symbol value must be numeric; ignoring \"%s\"\n", str);
	return FALSE;
    }

    value[-1] = '\0';
    if (he = HashLookOnly(&efSymHash, str))
    {
	TxError("Symbol \"%s\" already defined\n", str);
	value[-1] = '=';
	return FALSE;
    }

    he = HashFind(&efSymHash, str);
    value[-1] = '=';
    HashSetValue(he, (spointertype)atoi(value));
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * efSymLook --
 *
 * Look up the value of a symbol and store the value in *pValue.
 *
 * Results:
 *	TRUE if the symbol was defined, FALSE if not.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

bool
efSymLook(name, pValue)
    char *name;
    int *pValue;
{
    HashEntry *he;

    he = HashLookOnly(&efSymHash, name);
    if (he == NULL)
	return FALSE;

    *pValue = (spointertype) HashGetValue(he);
    return TRUE;
}
