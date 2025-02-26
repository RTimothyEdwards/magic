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
 * Overall cleanup.  For use of func(), see efFreeNodeList() in EFbuild.c.
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
EFDone(func)
    int (*func)();
{
    Connection *conn;
    HashSearch hs;
    HashEntry *he;
    Kill *kill;
    Def *def;
    Use *use;
    int n;

    HashStartSearch(&hs);
    while ((he = HashNext(&efDefHashTable, &hs)))
    {
	def = (Def *) HashGetValue(he);
	freeMagic(def->def_name);
	efFreeNodeTable(&def->def_nodes);
	efFreeNodeList(&def->def_firstn, func);
	efFreeUseTable(&def->def_uses);
	efFreeDevTable(&def->def_devs);
	HashKill(&def->def_nodes);
	HashKill(&def->def_dists);
	HashKill(&def->def_uses);
	HashKill(&def->def_devs);
	efConnectionFreeLinkedList(def->def_conns);
	efConnectionFreeLinkedList(def->def_caps);
	efConnectionFreeLinkedList(def->def_resistors);

	free_magic1_t mm1 = freeMagic1_init();
	for (kill = def->def_kills; kill; kill = kill->kill_next)
	{
	    freeMagic(kill->kill_name);
	    freeMagic1(&mm1, (char *) kill);
	}
	freeMagic1_end(&mm1);
	freeMagic((char *) def);
    }

    /* Misc cleanup */
    for (n = 0; n < EFDevNumTypes; n++) freeMagic(EFDevTypes[n]);
    EFDevNumTypes = 0;

    /* Changed from n = 0 to n = 1; First entry "space" is predefined,	*/
    /* not malloc'd.  ---Tim 9/3/02					*/
    for (n = 1; n < EFLayerNumNames; n++) freeMagic(EFLayerNames[n]);

    if (EFTech)
    {
	freeMagic(EFTech);
	EFTech = (char *)NULL;
    }

    /* Free up the parameter name tables for each device */

    HashStartSearch(&hs);
    while ((he = HashNext(&efDevParamTable, &hs)))
    {
	DevParam *plist = (DevParam *)HashGetValue(he);
	while (plist != NULL)
	{
	    freeMagic(plist->parm_name);
	    free_magic1_t mm1 = freeMagic1_init();
	    freeMagic1(&mm1, plist);
	    plist = plist->parm_next;
	    freeMagic1_end(&mm1);
	}
    }
    HashKill(&efDevParamTable);

    HashStartSearch(&hs);
    while ((he = HashNext(&efFreeHashTable, &hs)))
    {
	/* Keys of this table are entries to be free'd */
	freeMagic((void *)he->h_key.h_ptr);
    }
    HashKill(&efFreeHashTable);

    /* Final cleanup */
    HashKill(&efDefHashTable);

    /* EFSearchPath does not persist beyond the command that set it */
    if (EFSearchPath)
    {
	freeMagic(EFSearchPath);
	EFSearchPath = NULL;
    }
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
    newdef->def_kills = (Kill *) NULL;

    /* Initialize circular list of nodes */
    newdef->def_firstn.efnode_next = (EFNodeHdr *) &newdef->def_firstn;
    newdef->def_firstn.efnode_prev = (EFNodeHdr *) &newdef->def_firstn;

    /* Initialize hash table of uses */
    HashInit(&newdef->def_uses, INITNODESIZE, HT_STRINGKEYS);

    /* Initialize hash table of node names */
    HashInit(&newdef->def_nodes, INITNODESIZE, HT_STRINGKEYS);

    /* Initialize hash table of devices */
    HashInit(&newdef->def_devs, INITNODESIZE, HT_STRINGKEYS);

    /* Initialize hash table of distances */
    HashInitClient(&newdef->def_dists, INITNODESIZE, HT_CLIENTKEYS,
	    efHNDistCompare, efHNDistCopy, efHNDistHash, efHNDistKill);

    return (newdef);
}
