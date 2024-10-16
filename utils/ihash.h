// ************************************************************************
//
// Copyright (c) 1995-2002 Juniper Networks, Inc. All rights reserved.
//
// Permission is hereby granted, without written agreement and without
// license or royalty fees, to use, copy, modify, and distribute this
// software and its documentation for any purpose, provided that the
// above copyright notice and the following three paragraphs appear in
// all copies of this software.
//
// IN NO EVENT SHALL JUNIPER NETWORKS, INC. BE LIABLE TO ANY PARTY FOR
// DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
// ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF
// JUNIPER NETWORKS, INC. HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
// DAMAGE.
//
// JUNIPER NETWORKS, INC. SPECIFICALLY DISCLAIMS ANY WARRANTIES,
// INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND
// NON-INFRINGEMENT.
//
// THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND JUNIPER
// NETWORKS, INC. HAS NO OBLIGATION TO PROVIDE MAINTENANCE, SUPPORT,
// UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
//
// ************************************************************************



/* ihash.h --
 *
 * "Internal" hash routines.
 * Allows hashing of existing structs without creating parallel structs
 * to hold keys etc.
 *
 * The structs to be hashed must have "key" and "next" fields.  The offsets
 * of these fields are passed to HashInit.
 */

/* rcsid "$Header$" */

#ifndef _MAGIC__UTILS__IHASH_H
#define	_MAGIC__UTILS__IHASH_H

/* returns total memory required for malloc of given size, for	*/
/* routine IHashStat2() only.					*/

static __inline__ int IHashAlignedSize(int size)
{
    int result;
    /* Expand size to be double-word (64 bit) aligned */
    result = ((size + 7) / 8) * 8;
    return result;
}

/* The IHashTable struct should not be manipulated directly by clients */

typedef struct ihashtable
{
    void **iht_table;    	/* Pointer to array of pointers. */
    int iht_nBucketsInit;	/* Initial size of array. */
    int iht_nBuckets;		/* Size of array. */
    int iht_nEntries;		/* Number of hashed items */
    int iht_keyOffset;          /* offset of keys in client strucs */
    int iht_nextOffset;         /* offset of next fields in client strucs */
    int (*iht_hashFn)(void *key);	           /* Hash function */
    int (*iht_sameKeyFn)(void *key1, void *key2);  /* returns 1 if keys match */

} IHashTable;

/* create a new hash table */
extern IHashTable *IHashInit(
		      int nBuckets,
		      int keyOffset,
		      int nextOffset,
		      int (*hashFn)(void *key),
		      int (*sameKeyFn)(void *key1, void *key2)
		      );

/* lookup an entry in table  (returns first match) */
extern void *IHashLookUp(IHashTable *table, void *key);

/* lookup NEXT matching entry in table */
extern void *IHashLookUpNext(IHashTable *table, void *prevEntry);

/* add an entry to the table */
extern void IHashAdd(IHashTable *table, void *entry);

/* delete an entry from the table */
extern void IHashDelete(IHashTable *table, void *entry);

/* delete all entrys (and restore initial hash table size) */
extern void IHashClear(IHashTable *table);

/* callback supplied func for each entry in table */
extern void IHashEnum(IHashTable *table, void (*clientFunc)(void *entry));

/* return number of entries in table */
extern int IHashEntries(IHashTable *table);

/* print hash table statistics */
extern void IHashStats(IHashTable *table);

/* return hashtable memory usage and stats */
extern int IHashStats2(IHashTable *table, int *nBuckets, int *nEntries);

/* free hash table (does not free client strucs!) */
extern void IHashFree(IHashTable *table);

/* A hash function suitable for hash key fields that are pointers to strings */
extern int IHashStringPKeyHash(void *key);

/* key comparison function for key fields that are pointers to strings */
extern int IHashStringPKeyEq(void *key1, void *key2);

/* A hash function suitable for hash key fields that are strings */
extern int IHashStringKeyHash(void *key);

/* key comparison function for key fields that are strings */
extern int IHashStringKeyEq(void *key1, void *key2);

/* A hash function suitable for keys that are pointers */
extern int IHashWordKeyHash(void *key);

/* key comparison function for key fields that are pointers */
extern int IHashWordKeyEq(void *key1, void *key2);

/* A hash function suitable for 4 word keys */
extern int IHash4WordKeyHash(void *key);

/* key comparison function for four word keys */
extern int IHash4WordKeyEq(void *key1, void *key2);
#endif /* _MAGIC__UTILS__IHASH_H */
