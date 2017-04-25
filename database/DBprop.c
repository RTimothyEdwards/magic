/*
 * DBprop.c --
 *
 * Implement properties on database cells.  Properties are name-value pairs
 * and provide a flexible way of extending the data that is stored in a
 * CellDef.  Maybe in the future properties will be added to other database
 * objects.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/database/DBprop.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/malloc.h"


/* ----------------------------------------------------------------------------
 *
 *DBPropPut --
 *
 * Put a property onto a celldef.
 *
 * Results:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
DBPropPut(cellDef, name, value)
    CellDef *cellDef;	/* Pointer to definition of cell. */
    char *name;		/* The name of the property desired. */
    ClientData value;	/* MUST point to a malloc'ed structure, or NULL. 
			 * This will be freed when the CellDef is freed. 
			 */

{
    HashTable *htab;
    HashEntry *entry;

    /* Honor the NOEDIT flag */
    if (cellDef->cd_flags & CDNOEDIT) return;

    if (cellDef->cd_props == (ClientData) NULL) 
    {
	cellDef->cd_props = (ClientData) mallocMagic(sizeof(HashTable));
	HashInit( (HashTable *) cellDef->cd_props, 8, 0);
    }
    htab = (HashTable *) cellDef->cd_props;
    
    entry = HashFind(htab, name);
    HashSetValue(entry, value);
}

/* ----------------------------------------------------------------------------
 *
 * DBPropGet --
 *
 * Get a property from a celldef.
 *
 * Results:
 *	NULL if the property didn't exist, or if the property value was NULL.
 *	Otherwise, ClientData that represents the property.
 *
 * ----------------------------------------------------------------------------
 */

ClientData
DBPropGet(cellDef, name, found)
    CellDef *cellDef;	/* Pointer to definition of cell. */
    char *name;		/* The name of the property desired. */
    bool *found;	/* If not NULL, filled in with TRUE iff the property
			 * exists.
			 */
{
    ClientData result;
    bool haveit;
    HashTable *htab;
    HashEntry *entry;

    result = (ClientData) NULL;
    haveit = FALSE;
    htab = (HashTable *) cellDef->cd_props;
    if (htab == (HashTable *) NULL) goto done;

    entry = HashLookOnly(htab, name);
    if (entry != NULL)
    {
	haveit = TRUE;
	result = (ClientData) HashGetValue(entry);
    }

done:
    if (found != (bool *) NULL) *found = haveit;
    return result;
}

/* ----------------------------------------------------------------------------
 *
 * DBPropEnum --
 *
 * Enumerate all the properties on a cell.
 *
 * Results:
 *	0 if the search completed, else whatever value was returned by the
 *	called proc.
 *
 * Side effects:
 *	Depends on the called proc.
 * ----------------------------------------------------------------------------
 */

int
DBPropEnum(cellDef, func, cdata)
    CellDef *cellDef;	/* Pointer to definition of cell. */
    int (*func)();	/* Function of the form:
			 *
			 *	int foo(name, value, cdata)
			 *	    char *name;
			 *	    ClientData value;
			 *	    ClientData cdata;
			 *	{
			 *	    -- return 0 to continue, 
			 *	    -- nonzero to abort.
			 *	    return result;
			 *	}
			 */
    ClientData	cdata;
{
    HashTable *htab;
    HashSearch hs;
    HashEntry *entry;
    int res;

    if (cellDef->cd_props == (ClientData) NULL) return 0;
    htab = (HashTable *) cellDef->cd_props;

    HashStartSearch(&hs);
    while ((entry = HashNext(htab, &hs)) != NULL)
    {
	res = (*func)(entry->h_key.h_name, (ClientData) entry->h_pointer, cdata);
	if (res != 0) return res;
    }

    return 0;
}


/* ----------------------------------------------------------------------------
 *
 * DBPropClearAll --
 *
 * Free up all properties and associated storage for a CellDef.
 *
 * Results:
 *	none.
 *
 * Side effects:
 *	Frees up storage, even for the property table itself.
 * ----------------------------------------------------------------------------
 */

void
DBPropClearAll(cellDef)
    CellDef *cellDef;	/* Pointer to definition of cell. */
{
    HashTable *htab;
    HashSearch hs;
    HashEntry *entry;

    if (cellDef->cd_props == (ClientData) NULL) return;
    htab = (HashTable *) cellDef->cd_props;

    HashStartSearch(&hs);
    while ((entry = HashNext(htab, &hs)) != NULL)
    {
	if (entry->h_pointer != NULL) freeMagic((char *) entry->h_pointer);
	HashSetValue(entry, NULL);
    }

    HashKill(htab);
    freeMagic((char *) htab);
    cellDef->cd_props = (ClientData) NULL;
}
