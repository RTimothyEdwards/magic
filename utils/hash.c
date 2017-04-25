/* hash.c --
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
 * This module contains routines to manipulate a hash table.
 * See hash.h for a definition of the structure of the hash
 * table.  Hash tables grow automatically as the amount of
 * information increases.
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/hash.c,v 1.2 2009/05/13 15:03:18 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/hash.h"
#include "utils/malloc.h"

/* Used before it's defined: */
void rebuild();

/*
 * The following defines the ratio of # entries to # buckets
 * at which we rebuild the table to make it larger.
 */
static int rebuildLimit = 3;

/*
 * An invalid pointer, guaranteed to cause a coredump if 
 * we try to indirect through it.  This should help catch
 * attempts to indirect through stale pointers.
 */
#define NIL ((HashEntry *) (1<<29))


/*---------------------------------------------------------
 *
 * HashInit --
 * HashInitClient --
 *
 * These procedures simply set up the hash table.  The standard
 * way of initializing the hash table is to use HashInit(), but
 * if it's desired to provide the hash module with procedures to
 * use for comparing and copying hash table keys, use HashInitClient().
 *
 * The number of buckets in the table at the start is 'nBuckets',
 * which is automatically rounded up to a power of two.  This isn't
 * a limit on the number of buckets the table will eventually contain,
 * though, since more buckets are automatically created if the table
 * gets too full (the number of buckets increases by 4x).
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Memory is allocated for the initial bucket area.
 *
 * Table Organization:
 *	Tables can be organized in either of four ways, depending
 *	on the type of comparison keys as specified by ptrKeys.
 *
 *	HT_STRINGKEYS:
 *	    Keys are NULL-terminated; their address is passed to
 *	    HashFind as a (char *).
 *
 *	HT_WORDKEYS:
 *	    These are any 32-bit word, passed to HashFind as a (char *).
 *
 *	HT_STRUCTKEYS:
 *	    Actually, any value of ptrKeys >= HT_STRUCTKEYS means
 *	    that keys are ptrKeys-word values whose ADDRESS is
 *	    passed to HashFind as a (char *).
 *
 *	HT_CLIENTKEYS:
 *	    Like HT_WORDKEYS, these are also 32-bit values, passed
 *	    to HashFind as a (char *).  However, they are compared
 *	    and copied using user-supplied procedures passed to
 *	    HashInitClient() when the hash table was created.
 *	    (Note that hash tables with keys of type HT_CLIENTKEYS
 *	    can ONLY be created using HashInitClient()).
 *
 *	Single-word values, a la HT_WORDKEYS, are fastest but most
 *	restrictive.
 *
 * Client procedures:
 *	Four client procedures are provided to HashInitClient()
 *	for use in dealing with HT_CLIENTKEYS data.  They should
 *	be of the following form:
 *
 *	Compare two hash keys; return 0 if equal, 1 if not.  If this
 *	procedure is NULL, comparison is just 32-bit comparison of
 *	k1 and k2.
 *
 *	int
 *	(*compareFn)(k1, k2)
 *	    char *k1, *k2;
 *	{
 *	}
 *
 *	Create a copy of a hash key for storing in a newly created
 *	hash entry.  If this procedure is NULL, the key is stored
 *	without being copied.
 *
 *	char *
 *	(*copyFn)(key)
 *	    char *key;
 *	{
 *	}
 *
 *	Produce a single 32-bit integer for a key value that will
 *	then be randomized by the hashing function.  If NULL, then
 *	the key itself is used as the 32-bit integer.
 *
 *	int
 *	(*hashFn)(key)
 *	    char *key;
 *	{
 *	}
 *
 *	Free a key that had been allocated with (*copyFn)().
 *	If NULL, then nothing is done.
 *
 *      int	
 *	(*killFn)(key)
 *	    char *key;
 *	{
 *	}
 *
 *---------------------------------------------------------
 */

void
HashInit(table, nBuckets, ptrKeys)
    HashTable *table;		/* Table to be initialized */
    int nBuckets;		/* How many buckets to create for starters */
    int ptrKeys;		/* See comments above */
{
    ASSERT(ptrKeys != HT_CLIENTKEYS, "HashInit: should use HashInitClient");
    HashInitClient(table, nBuckets, ptrKeys,
		(int (*)()) NULL, (char *(*)()) NULL,
		(int (*)()) NULL, (int (*)()) NULL);
}

void
HashInitClient(table, nBuckets, ptrKeys, compareFn, copyFn, hashFn, killFn)
    HashTable *table;		/* Table to be initialized */
    int nBuckets;		/* How many buckets to create for starters */
    int ptrKeys;		/* See comments above */
    int (*compareFn)();		/* Function to compare two keys */
    char *(*copyFn)();		/* Function to copy a key */
    int (*hashFn)();		/* For hashing */
    int (*killFn)();		/* For hashing */
{
    HashEntry ** ptr;
    int i;

    table->ht_nEntries = 0;
    table->ht_ptrKeys = ptrKeys;
    table->ht_compareFn = compareFn;
    table->ht_copyFn = copyFn;
    table->ht_hashFn = hashFn;
    table->ht_killFn = killFn;

    /* Round up the size to a power of two */
    if (nBuckets < 0) nBuckets = -nBuckets;
    table->ht_size = 2;
    table->ht_mask = 1;
    table->ht_downShift = 29;
    while (table->ht_size < nBuckets)
    {
	table->ht_size <<= 1;
	table->ht_mask = (table->ht_mask<<1) + 1;
	table->ht_downShift--;
    }

    /* Allocate and initialize the buckets */
    table->ht_table = (HashEntry **) mallocMagic(
		(unsigned) (sizeof (HashEntry *)  * table->ht_size));
    ptr = table->ht_table;
    for (i = 0; i < table->ht_size; i++)
	*ptr++ = NIL;
}

/*---------------------------------------------------------
 *
 * hash --
 *
 * This is a local procedure to compute a hash table
 * bucket address based on a key value.
 *
 * Results:
 *	The return value is an integer between 0 and size-1.
 *
 * Side Effects:
 *	None.
 *
 * Design:
 *	The randomizing code is stolen straight from the rand()
 *	library routine.
 *
 *---------------------------------------------------------
 */

int
hash(table, key)
    HashTable *table;
    char *key;
{
    unsigned *up;
    int i, j;

    i = 0;
    switch (table->ht_ptrKeys)
    {
	/* Add up the characters as though this were a number */
	case HT_STRINGKEYS:
	    while (*key != 0) i = (i*10) + (*key++ - '0');
	    break;

	/* Map the key into another 32-bit value if necessary */
	case HT_CLIENTKEYS:
	    if (table->ht_hashFn)
	    {
		i = (*(table->ht_hashFn))(key);
		break;
	    }
	    /* Fall through to ... */

	/* Just use the 32-bit key value */
	case HT_WORDKEYS:
	    i = (spointertype) key;
	    break;

	/* Special case for two-word structs */
	case HT_STRUCTKEYS:
	    i = ((unsigned *) key)[0] + ((unsigned *) key)[1];
	    break;

	/* General case of multi-word structs */
	default:
	    j = table->ht_ptrKeys;
	    up = (unsigned *) key;
	    do { i += *up++; } while (--j);
	    break;
    }

    /* Randomize! */
    return ((i*1103515245 + 12345) >> table->ht_downShift) & table->ht_mask;
}

/*---------------------------------------------------------
 *
 * HashLookOnly --
 *
 * Searches a hash table for an entry corresponding to key.
 *
 * Results:
 *	The return value is a pointer to the entry for key,
 *	if key was present in the table.  If key was not
 *	present, NULL is returned.
 *
 * Side Effects:
 *	None.
 *
 *---------------------------------------------------------
 */

HashEntry *
HashLookOnly(table, key)
    HashTable *table;	/* Hash table to search. */
    char *key;			/* Interpreted according to table->ht_ptrKeys
				 * as described in HashInit()'s comments.
				 */
{
    HashEntry *h;
    unsigned *up, *kp;
    int n;
    int bucket;

    bucket = hash(table, key);
    h = *(table->ht_table + bucket);
    while (h != NIL)
    {
	switch (table->ht_ptrKeys)
	{
	    case HT_STRINGKEYS:
		if (strcmp(h->h_key.h_name, key) == 0) return h;
		break;
	    case HT_CLIENTKEYS:
		if (table->ht_compareFn)
		{
		    if ((*table->ht_compareFn)(h->h_key.h_ptr, key) == 0)
			return h;
		    break;
		}
		/* Fall through to ... */
	    case HT_WORDKEYS:
		if (h->h_key.h_ptr == key) return h;
		break;
	    case HT_STRUCTKEYS:
		up = h->h_key.h_words;
		kp = (unsigned *) key;
		if (*up++ == *kp++ && *up == *kp) return h;
		break;
	    default:
		n = table->ht_ptrKeys;
		up = h->h_key.h_words;
		kp = (unsigned *) key;
		do { if (*up++ != *kp++) goto next; } while (--n);
		return h;
	}
next:
	h = h->h_next;
    }

    /* The desired entry isn't there */
    return ((HashEntry *) NULL);
}

/*---------------------------------------------------------
 *
 * HashFind --
 *
 * Searches a hash table for an entry corresponding to
 * key.  If no entry is found, then one is created.
 *
 * Results:
 *	The return value is a pointer to the entry for key.
 *	If the entry is a new one, then the h_pointer field
 *	of the entry we return is zero.
 *
 * Side Effects:
 *	Memory is allocated, and the hash buckets may be modified.
 *
 *---------------------------------------------------------
 */

HashEntry *
HashFind(table, key)
    HashTable *table;	/* Hash table to search. */
    char *key;			/* Interpreted according to table->ht_ptrKeys
				 * as described in HashInit()'s comments.
				 */
{
    unsigned *up, *kp;
    HashEntry *h;
    int n;
    int bucket;

    bucket = hash(table, key);
    h = *(table->ht_table + bucket);
    while (h != NIL)
    {
	switch (table->ht_ptrKeys)
	{
	    case HT_STRINGKEYS:
		if (strcmp(h->h_key.h_name, key) == 0) return h;
		break;
	    case HT_CLIENTKEYS:
		if (table->ht_compareFn)
		{
		    if ((*table->ht_compareFn)(h->h_key.h_ptr, key) == 0)
			return h;
		    break;
		}
		/* Fall through to ... */
	    case HT_WORDKEYS:
		if (h->h_key.h_ptr == key) return h;
		break;
	    case HT_STRUCTKEYS:
		up = h->h_key.h_words;
		kp = (unsigned *) key;
		if (*up++ == *kp++ && *up == *kp) return h;
		break;
	    default:
		n = table->ht_ptrKeys;
		up = h->h_key.h_words;
		kp = (unsigned *) key;
		do { if (*up++ != *kp++) goto next; } while (--n);
		return h;
	}
next:
	h = h->h_next;
    }

    /*
     * The desired entry isn't there.  Before allocating a new entry,
     * see if we're overloading the buckets.  If so, then make a
     * bigger table (4x as big).
     */
    if (table->ht_nEntries >= rebuildLimit*table->ht_size)
    {
	rebuild(table);
	bucket = hash(table, key);
    }
    table->ht_nEntries += 1;

    /*
     * Now allocate a new entry.  The size of the HashEntry allocated
     * depends on the size of the key: for multi-word keys or string
     * keys longer than 3 bytes, there will be extra space at the end
     * of the HashEntry to hold the key.
     */
    switch (table->ht_ptrKeys)
    {
	case HT_STRINGKEYS:
	    h = (HashEntry *) mallocMagic((unsigned) (sizeof(HashEntry)+strlen(key)-3));
	    (void) strcpy(h->h_key.h_name, key);
	    break;
	case HT_CLIENTKEYS:
	    if (table->ht_copyFn)
	    {
		h = (HashEntry *) mallocMagic((unsigned) (sizeof (HashEntry)));
		h->h_key.h_ptr = (*table->ht_copyFn)(key);
		break;
	    }
	    /* Fall through to ... */
	case HT_WORDKEYS:
	    h = (HashEntry *) mallocMagic((unsigned) (sizeof (HashEntry)));
	    h->h_key.h_ptr = key;
	    break;
	case HT_STRUCTKEYS:
	    h = (HashEntry *) mallocMagic(
		    (unsigned) (sizeof (HashEntry) + sizeof (unsigned)));
	    up = h->h_key.h_words;
	    kp = (unsigned *) key;
	    *up++ = *kp++;
	    *up = *kp;
	    break;
	default:
	    n = table->ht_ptrKeys;
	    h = (HashEntry *) mallocMagic(
		    (unsigned) (sizeof(HashEntry) + (n-1) * sizeof (unsigned)));
	    up = h->h_key.h_words;
	    kp = (unsigned *) key;
	    do { *up++ = *kp++; } while (--n);
	    break;
    }

    h->h_pointer = 0;
    h->h_next = *(table->ht_table + bucket);
    *(table->ht_table + bucket) = h;
    return h;
}

/*---------------------------------------------------------
 *
 * rebuild --
 *
 * This local routine makes a new hash table that
 * is 4x larger than the old one.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	The entire hash table is moved, so any bucket numbers
 *	from the old table are invalid.
 *
 *---------------------------------------------------------
 */

void
rebuild(table)
    HashTable *table;		/* Table to be enlarged. */
{
    HashEntry **oldTable, **old2, *h, *next;
    int oldSize, bucket;

    oldTable = table->ht_table;
    old2 = oldTable;
    oldSize = table->ht_size;

    /* Build a new table 4 times as large as the old one. */
    HashInitClient(table, table->ht_size*4, table->ht_ptrKeys,
		table->ht_compareFn, table->ht_copyFn,
		table->ht_hashFn, table->ht_killFn);
    for ( ; oldSize > 0; oldSize--)
    {
	h = *old2++;
	while (h != NIL)
	{
	    next = h->h_next;
	    switch (table->ht_ptrKeys)
	    {
		case HT_STRINGKEYS:
		    bucket = hash(table, h->h_key.h_name);
		    break;
		case HT_WORDKEYS:
		case HT_CLIENTKEYS:
		    bucket = hash(table, h->h_key.h_ptr);
		    break;
		default:
		    bucket = hash(table, (char *) h->h_key.h_words);
		    break;
	    }
	    h->h_next = *(table->ht_table + bucket);
	    *(table->ht_table + bucket) = h;
	    table->ht_nEntries += 1;
	    h = next;
	}
    }

    freeMagic((char *) oldTable);
}

/*---------------------------------------------------------
 *
 * HashStats --
 *
 * This routine merely prints statistics about the
 * current bucket situation.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Junk gets printed.
 *
 *---------------------------------------------------------
 */

#define	MAXCOUNT	15

void
HashStats(table)
    HashTable *table;
{
    int count[MAXCOUNT], overflow, i, j;
    HashEntry *h;

    overflow = 0;
    for (i = 0; i < MAXCOUNT; i++) count[i] = 0;
    for (i = 0; i < table->ht_size; i++)
    {
	j = 0;
	for (h = *(table->ht_table+i); h != NIL; h = h->h_next)
	    j++;
	if (j < MAXCOUNT) count[j]++;
	else overflow++;
    }

    for (i = 0;  i < MAXCOUNT; i++)
	printf("# of buckets with %d entries: %d.\n", i, count[i]);
    printf("# of buckets with >%d entries: %d.\n", MAXCOUNT-1, overflow);
}

/*---------------------------------------------------------
 *
 * HashStartSearch --
 *
 * This procedure sets things up for a complete search
 * of all entries recorded in the hash table.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	The information in hs is initialized so that successive
 *	calls to HashNext will return successive HashEntry's
 *	from the table.
 *---------------------------------------------------------
 */

void
HashStartSearch(hs)
    HashSearch *hs;	/* Area in which to keep state about search.*/
{
    hs->hs_nextIndex = 0;
    hs->hs_h = NIL;
}

/*---------------------------------------------------------
 *
 * HashNext --
 *
 * This procedure returns successive entries in the
 * hash table.
 *
 * Results:
 *	The return value is a pointer to the next HashEntry
 *	in the table, or NULL when the end of the table is
 *	reached.
 *
 * Side Effects:
 *	The information in hs is modified to advance to the
 *	next entry.
 *
 *---------------------------------------------------------
 */

HashEntry *
HashNext(table, hs)
    HashTable *table;	/* Table to be searched. */
    HashSearch *hs;	/* Area used to keep state about search. */
{
    HashEntry *h;

    while (hs->hs_h == NIL)
    {
	if (hs->hs_nextIndex >= table->ht_size) return NULL;
	hs->hs_h = *(table->ht_table + hs->hs_nextIndex);
	hs->hs_nextIndex += 1;
    }
    h = hs->hs_h;
    hs->hs_h = h->h_next;
    return h;
}

/*---------------------------------------------------------
 *
 * HashKill --
 *
 * This routine removes everything from a hash table
 * and frees up the memory space it occupied.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Lots of memory is freed up.
 *---------------------------------------------------------
 */

void
HashKill(table)
    HashTable *table;	/* Hash table whose space is to be freed */
{
    HashEntry *h, **hp, **hend;
    int (*killFn)() = (int (*)()) NULL;

    if (table->ht_ptrKeys == HT_CLIENTKEYS) killFn = table->ht_killFn;
    for (hp = table->ht_table, hend = &hp[table->ht_size]; hp < hend; hp++)
	for (h = *hp; h != NIL; h = h->h_next)
	{
	    freeMagic((char *) h);
	    if (killFn)
		(*killFn)(h->h_key.h_ptr);
	}
    freeMagic((char *) table->ht_table);

    /*
     * Set up the hash table to cause memory faults on any future
     * access attempts until re-initialization.
     */
    table->ht_table = (HashEntry **) (1<<29);
}

/*---------------------------------------------------------
 *
 * HashFreeKill ---
 * 
 * This routine removes everything from a hash table
 * and frees up the memory space it occupied along with
 * the stuff pointed by h_pointer
 * 
 * Results:
 *      None.
 *
 * Side Effects:
 *      Lots of memory is freed up.
 *---------------------------------------------------------
 */
void
HashFreeKill(table)
HashTable *table;
{
	HashSearch hs;
	HashEntry *he;
	void *p;

	HashStartSearch(&hs);
	while (he = HashNext(table, &hs)) {
		p = HashGetValue(he);
		freeMagic(p);
	}
	HashKill(table);
}
