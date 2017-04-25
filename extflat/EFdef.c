/*
 * EFdef.c -
 *
 * Procedures for managing the database of Defs.
 * There is a single Def for each .ext file in a hierarchically
 * extracted circuit.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/extflat/EFdef.c,v 1.2 2008/12/03 14:12:09 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/hash.h"
#include "utils/utils.h"
#include "utils/malloc.h"
#include "extflat/extflat.h"
#include "extflat/EFint.h"

/* Initial size of def hash table */
#define	INITDEFSIZE	128

/* Initial size of node hash table in each def */
#define	INITNODESIZE	32

/* Def hash table itself; maps from def names into pointers to Defs */
HashTable efDefHashTable;

/* Hash table used for checking for malloc leaks */
HashTable efFreeHashTable;

/* Hash table used for keeping subcircuit parameter names for a device */
HashTable efDevParamTable;

/*
 * ----------------------------------------------------------------------------
 *
 * EFInit --
 *
 * Initialize the hash table of def names and global signal names.
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
EFInit()
{
    EFLayerNumNames = 1;
    EFDevNumTypes = 0;
    EFCompat = TRUE;

    HashInit(&efFreeHashTable, 32, HT_WORDKEYS);
    HashInit(&efDefHashTable, INITDEFSIZE, 0);
    HashInit(&efDevParamTable, 8, HT_STRINGKEYS);
    efSymInit();
}

/*
 * ----------------------------------------------------------------------------
 *
 * EFDone --
 *
 * Overall cleanup.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Eliminates the def and global name hash tables.
 *	If malloc tracing is enabled, also frees everything else we
 *	allocated with malloc.
 *
 * ----------------------------------------------------------------------------
 */

void
EFDone()
{
    Connection *conn;
    HashSearch hs;
    HashEntry *he;
    Kill *kill;
    Def *def;
    Use *use;
    Dev *dev;
    int n;

    HashStartSearch(&hs);
    while (he = HashNext(&efDefHashTable, &hs))
    {
	def = (Def *) HashGetValue(he);
	freeMagic(def->def_name);
	efFreeNodeTable(&def->def_nodes);
	efFreeNodeList(&def->def_firstn);
	HashKill(&def->def_nodes);
	HashKill(&def->def_dists);
	for (use = def->def_uses; use; use = use->use_next)
	{
	    freeMagic(use->use_id);
	    freeMagic((char *) use);
	}
	for (conn = def->def_conns; conn; conn = conn->conn_next)
	    efFreeConn(conn);
	for (conn = def->def_caps; conn; conn = conn->conn_next)
	    efFreeConn(conn);
	for (conn = def->def_resistors; conn; conn = conn->conn_next)
	    efFreeConn(conn);
	for (dev = def->def_devs; dev; dev = dev->dev_next)
	{
	    for (n = 0; n < (int)dev->dev_nterm; n++)
		if (dev->dev_terms[n].dterm_attrs)
		    freeMagic((char *) dev->dev_terms[n].dterm_attrs);
	    freeMagic((char *) dev);
	}
	for (kill = def->def_kills; kill; kill = kill->kill_next)
	{
	    freeMagic(kill->kill_name);
	    freeMagic((char *) kill);
	}
	freeMagic((char *) def);
    }

    /* Misc cleanup */
    for (n = 0; n < EFDevNumTypes; n++) freeMagic(EFDevTypes[n]);

    /* Changed from n = 0 to n = 1; First entry "space" is predefined,	*/
    /* not malloc'd.  ---Tim 9/3/02					*/
    for (n = 1; n < EFLayerNumNames; n++) freeMagic(EFLayerNames[n]);

    if (EFTech)
    {
	freeMagic(EFTech);
	EFTech = (char *)NULL;
    }

    /* Free up all HierNames that were stored in efFreeHashTable */
/*
    HashStartSearch(&hs);
    while (he = HashNext(&efFreeHashTable, &hs))
	freeMagic(he->h_key.h_ptr);
*/

    /* Free up the parameter name tables for each device */

    HashStartSearch(&hs);
    while (he = HashNext(&efDevParamTable, &hs))
    {
	DevParam *plist = (DevParam *)HashGetValue(he);
	while (plist != NULL)
	{
	    freeMagic(plist->parm_name);
	    freeMagic(plist);
	    plist = plist->parm_next;
	}
    }
    HashKill(&efDevParamTable);

    HashKill(&efFreeHashTable);

    /* Final cleanup */
    HashKill(&efDefHashTable);
}

/*
 * ----------------------------------------------------------------------------
 *
 * efDefLook --
 *
 * Look for a def by the given name in the hash table.
 * If the def doesn't exist, return NULL; otherwise, return
 * a pointer to the def.
 *
 * Results:
 *	Returns a pointer to a def, or NULL if none by that
 *	name exists.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

Def *
efDefLook(name)
    char *name;
{
    HashEntry *he;

    he = HashLookOnly(&efDefHashTable, name);
    if (he == (HashEntry*) NULL)
	return ((Def *) NULL);

    return ((Def *) HashGetValue(he));
}

/*
 * ----------------------------------------------------------------------------
 *
 * efDefNew --
 *
 * Allocate a new def by the given name.
 *
 * Results:
 *	Returns a pointer to a def.
 *
 * Side effects:
 *	Allocates a new def and initializes it.
 *
 * ----------------------------------------------------------------------------
 */

Def *
efDefNew(name)
    char *name;
{
    HashEntry *he;
    Def *newdef;

    he = HashFind(&efDefHashTable, name);
    newdef = (Def *) mallocMagic((unsigned) (sizeof (Def)));
    HashSetValue(he, (char *) newdef);

    newdef->def_name = StrDup((char **) NULL, name);
    newdef->def_flags = 0;
    newdef->def_scale = 1.0;
    newdef->def_conns = (Connection *) NULL;
    newdef->def_caps = (Connection *) NULL;
    newdef->def_resistors = (Connection *) NULL;
    newdef->def_devs = (Dev *) NULL;
    newdef->def_uses = (Use *) NULL;
    newdef->def_kills = (Kill *) NULL;

    /* Initialize circular list of nodes */
    newdef->def_firstn.efnode_next = (EFNodeHdr *) &newdef->def_firstn;
    newdef->def_firstn.efnode_prev = (EFNodeHdr *) &newdef->def_firstn;

    /* Initialize hash table of node names */
    HashInit(&newdef->def_nodes, INITNODESIZE, HT_STRINGKEYS);

    /* Initialize hash table of distances */
    HashInitClient(&newdef->def_dists, INITNODESIZE, HT_CLIENTKEYS,
	    efHNDistCompare, efHNDistCopy, efHNDistHash, efHNDistKill);

    return (newdef);
}
