/* hash.h --
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
 * This file contains definitions used by the hash module,
 * which maintains hash tables.
 */

/* rcsid "$Header: /usr/cvsroot/magic-8.0/utils/hash.h,v 1.2 2009/09/10 20:32:55 tim Exp $" */

#ifndef	_HASH_H
#define	_HASH_H

/*---------------------------------------------------------
 *	This first stuff should only be used internally
 *	in the hash routines.
 *---------------------------------------------------------
 */

/* The following defines one entry in the hash table. */

typedef struct h1
{
    char *h_pointer;		/* Pointer to anything. */
    struct h1 *h_next;		/* Next element, zero for end. */
    union
    {
	char *h_ptr;		/* One-word key value to identify entry. */
	unsigned h_words[1];	/* N-word key value.  Note: the actual
				 * size may be longer if necessary to hold
				 * the entire key.
				 */
	char h_name[4];		/* Text name of this entry.  Note: the
				 * actual size may be longer if necessary
				 * to hold the whole string. This MUST be
				 * the last entry in the structure!!!
				 */
    } h_key;
} HashEntry;

/* A hash table consists of an array of pointers to hash
 * lists:
 */

typedef struct h3
{
    HashEntry **ht_table;	/* Pointer to array of pointers. */
    int ht_size;		/* Actual size of array. */
    int ht_nEntries;		/* Number of entries in the table. */
    int ht_downShift;		/* Shift count, used in hashing function. */
    int ht_mask;		/* Used to select bits for hashing. */
    int ht_ptrKeys;		/* See below */

    /* Used if ht_ptrKeys == HT_CLIENTKEYS */
    char *(*ht_copyFn)();	/* Used for copying a key value */
    int (*ht_compareFn)();	/* Used for comparing two keys for equality */
    int (*ht_hashFn)();		/* Hash function */
    int (*ht_killFn)();		/* Used to kill an entry */
} HashTable;

/*
 * Values for ht_ptrKeys.
 *
 * HT_STRINGKEYS means that keys are NULL-terminated strings stored
 * in the variable-sized array h_key.h_name at the end of a HashEntry.
 *
 * HT_WORDKEYS and HT_CLIENTKEYS both mean that the value is stored
 * in h_key.h_ptr; with HT_WORDKEYS it is treated as a one-word value,
 * while with HT_CLIENTKEYS it is interpreted by user-supplied procedures
 * (and hence it can be a pointer).
 *
 * Finally, values of ht_ptrKeys of HT_STRUCTKEYS or greater mean
 * that the key consists of ht_ptrKeys 32-bit words of data, stored
 * in the variable-sized array h_key.h_words.
 */
#define	HT_CLIENTKEYS	-1
#define	HT_STRINGKEYS	0
#define	HT_WORDKEYS	1
#define	HT_STRUCTKEYS	2

/*
 * Default initial size (number of buckets) in a hash table.
 * May be passed to HashInit() or HashInitClient().
 */
#define	HT_DEFAULTSIZE	32

/* The following structure is used by the searching routines
 * to record where we are in the search.
 */

typedef struct h2
{
    int hs_nextIndex;		/* Next bucket to check (after current). */
    HashEntry * hs_h;		/* Next entry to check in current bucket. */
} HashSearch;

/*---------------------------------------------------------
 *	The following procedure declarations and macros
 *	are the only things that should be needed outside
 *	the implementation code.
 *---------------------------------------------------------
 */

extern void HashInit(HashTable *, int, int), HashInitClient(), HashStats(), HashKill(), 
	    HashFreeKill();
extern HashEntry *HashFind(HashTable *, char *);
extern HashEntry *HashLookOnly(HashTable *, char *);
extern void HashStartSearch(HashSearch *);
extern HashEntry *HashNext(HashTable *, HashSearch *);

/* char * HashGetValue(h); HashEntry *h; */

#define HashGetValue(h) ((h)->h_pointer)

/* HashSetValue(h, val); HashEntry *h; char *val; */

#define HashSetValue(h, val) ((h)->h_pointer = (char *) (val))

/* HashSize(n) returns the number of words in an object of n bytes */
#define	HashSize(n)	(((n) + sizeof (unsigned) - 1) / sizeof (unsigned))

/* HashGetNumEntries(ht); HashTable *ht ; returns number of entries in table */
#define	HashGetNumEntries(ht)	((ht)->ht_nEntries)

#endif	/* _HASH_H */
