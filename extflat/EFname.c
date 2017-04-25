/*
 * EFhier.c -
 *
 * Procedures for manipulating HierNames.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/extflat/EFname.c,v 1.2 2010/08/10 00:18:45 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "utils/hash.h"
#include "utils/malloc.h"
#include "utils/utils.h"
#include "extflat/extflat.h"
#include "extflat/EFint.h"

#ifdef MAGIC_WRAPPER 
#define PrintErr TxError
#else
#define PrintErr printf
#endif

/*
 * Hash table containing all flattened node names.
 * The keys in this table are HierNames, processed by the
 * procedures efHNCompare(), efHNHash(), efHierCopy(),
 * and efHierKill().
 */
HashTable efNodeHashTable;

/*
 * Hash table used by efHNFromUse to ensure that it always returns
 * a pointer to the SAME HierName structure each time it is called
 * with the same fields.
 */
HashTable efHNUseHashTable;

extern void EFHNFree();
extern void efHNInit();
extern void efHNRecord();


/*
 * ----------------------------------------------------------------------------
 *
 * EFHNIsGlob --
 *
 * Determine whether a HierName is of the format of a global name,
 * i.e, it ends in a '!'.
 * 
 * The Tcl version of magic further refines this to include names
 * which are defined in the global Tcl variable space.  (7.3.94):
 * also check if the array variable "globals" contains the name as
 * a key entry.
 *
 * Results:
 *	TRUE if the name is a global.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
EFHNIsGlob(hierName)
    HierName *hierName;
{
#ifdef MAGIC_WRAPPER
    char *retstr;
    retstr = (char *)Tcl_GetVar2(magicinterp, "globals", hierName->hn_name,
		TCL_GLOBAL_ONLY);
    if (retstr != NULL) return TRUE;

    retstr = (char *)Tcl_GetVar(magicinterp, hierName->hn_name, TCL_GLOBAL_ONLY);
    if (retstr != NULL) return TRUE;
#endif
    return hierName->hn_name[strlen(hierName->hn_name) - 1] == '!';
}

/*
 * ----------------------------------------------------------------------------
 *
 * EFHNIsGND --
 *
 * Determine whether a HierName is the same as the global signal GND.
 * 
 * The Tcl version of magic expands this to include names which are
 * equal to the global Tcl variable $GND, if it is set.
 *
 * This is only used in substrate backwards-compatibility mode, when the
 * substrate is not specified in the technology file.
 *
 * Results:
 *	TRUE if the name is GND, false if not.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
EFHNIsGND(hierName)
    HierName *hierName;
{
#ifdef MAGIC_WRAPPER
    char *retstr;
#endif

    if (hierName->hn_parent != (HierName *)NULL) return FALSE;

#ifdef MAGIC_WRAPPER
    retstr = (char *)Tcl_GetVar(magicinterp, "GND", TCL_GLOBAL_ONLY);
    if (retstr != NULL)
	if (!strcmp(hierName->hn_name, retstr)) return TRUE;
#endif

    return (strcmp(hierName->hn_name, "GND!") == 0);
}


/*
 * ----------------------------------------------------------------------------
 *
 * EFHNConcat --
 *
 * Given a HierName prefix and a HierName suffix, make a newly allocated
 * copy of the suffix that points to the prefix.
 *
 * Results:
 *	Pointer to the new HierName as described above.
 *
 * Side effects:
 *	May allocate memory.
 *
 * ----------------------------------------------------------------------------
 */

HierName *
EFHNConcat(prefix, suffix)
    HierName *prefix;		/* Components of name on root side */
    HierName *suffix;	/* Components of name on leaf side */
{
    HierName *new, *prev;
    HierName *firstNew;
    unsigned size;

    for (firstNew = prev = (HierName *) NULL;
	    suffix;
	    prev = new, suffix = suffix->hn_parent)
    {
	size = HIERNAMESIZE(strlen(suffix->hn_name));
	new = (HierName *) mallocMagic((unsigned)(size));
	if (efHNStats) efHNRecord(size, HN_CONCAT);
	new->hn_hash = suffix->hn_hash;
	(void) strcpy(new->hn_name, suffix->hn_name);
	if (prev)
	    prev->hn_parent = new;
	else
	    firstNew = new;
    }
    prev->hn_parent = prefix;

    return firstNew;
}

/*
 * ----------------------------------------------------------------------------
 *
 * EFStrToHN --
 *
 * Given a hierarchical prefix (the HierName pointed to by prefix)
 * and a name relative to that prefix (the string 'suffixStr'), return a
 * pointer to the HierName we should use.  Normally, this is just a newly
 * built HierName containing the path components of 'suffixStr' appended to
 * prefix.
 *
 * Results:
 *	Pointer to a name determined as described above.
 *
 * Side effects:
 *	May allocate new HierNames.
 *
 * ----------------------------------------------------------------------------
 */

HierName *
EFStrToHN(prefix, suffixStr)
    HierName *prefix;	/* Components of name on side of root */
    char *suffixStr;	/* Leaf part of name (may have /'s) */
{
    char *cp;
    HashEntry *he;
    char *slashPtr;
    HierName *hierName;
    unsigned size;
    int len;

    /* Skip to the end of the relative name */
    slashPtr = NULL;
    for (cp = suffixStr; *cp; cp++)
	if (*cp == '/')
	    slashPtr = cp;

    /*
     * Convert the relative name into a HierName path, with one HierName
     * created for each slash-separated segment of suffixStr.
     */
    cp = slashPtr = suffixStr;
    for (;;)
    {
	if (*cp == '/' || *cp == '\0')
	{
	    size = HIERNAMESIZE(cp - slashPtr);
	    hierName = (HierName *) mallocMagic((unsigned)(size));
	    if (efHNStats) efHNRecord(size, HN_ALLOC);
	    efHNInit(hierName, slashPtr, cp);
	    hierName->hn_parent = prefix;
	    if (*cp++ == '\0')
		break;
	    slashPtr = cp;
	    prefix = hierName;
	}
	else cp++;
    }

    return hierName;
}

/*
 * ----------------------------------------------------------------------------
 *
 * EFHNToStr --
 *
 * Convert a HierName chain into a printable name.
 * Stores the result in a static buffer.
 *
 * Results:
 *	Returns a pointer to the static buffer containing the
 *	printable name.
 *
 * Side effects:
 *	Overwrites the previous contents of the static buffer.
 *
 * ----------------------------------------------------------------------------
 */

char *
EFHNToStr(hierName)
    HierName *hierName;		/* Name to be converted */
{
    static char namebuf[2048];

    (void) efHNToStrFunc(hierName, namebuf);
    return namebuf;
}

/*
 * efHNToStrFunc --
 *
 * Recursive part of name conversion.
 * Calls itself recursively on hierName->hn_parent and dstp,
 * adding the prefix of the pathname to the string pointed to
 * by dstp.  Then stores hierName->hn_name at the end of the
 * just-stored prefix, and returns a pointer to the end.
 *
 * Results:
 *	Returns a pointer to the null byte at the end of
 *	all the pathname components stored so far in dstp.
 *
 * Side effects:
 *	Stores characters in dstp.
 */

char *
efHNToStrFunc(hierName, dstp)
    HierName *hierName;		/* Name to be converted */
    char *dstp;	/* Store name here */
{
    char *srcp;

    if (hierName == NULL)
    {
	*dstp = '\0';
	return dstp;
    }

    if (hierName->hn_parent)
    {
	dstp = efHNToStrFunc(hierName->hn_parent, dstp);
	*dstp++ = '/';
    }

    srcp = hierName->hn_name;
    while (*dstp++ = *srcp++)
	/* Nothing */;

    return --dstp;
}

/*
 * ----------------------------------------------------------------------------
 *
 * EFHNLook --
 *
 * Look for the entry in the efNodeHashTable whose name is formed
 * by concatenating suffixStr to prefix.  If there's not an
 * entry in efNodeHashTable, or the entry has a NULL value, complain
 * and return NULL; otherwise return the HashEntry.
 *
 * The string errorStr should say what we were processing, e.g,
 * "fet", "connect(1)", "connect(2)", etc., for use in printing
 * error messages.  If errorStr is NULL, we don't print any error
 * messages.
 *
 * Results:
 *	See above.
 *
 * Side effects:
 *	Allocates memory temporarily to build the key for HashLookOnly(),
 *	but then frees it before returning.
 *
 * ----------------------------------------------------------------------------
 */

HashEntry *
EFHNLook(prefix, suffixStr, errorStr)
    HierName *prefix;	/* Components of name on root side */
    char *suffixStr;	/* Part of name on leaf side */
    char *errorStr;	/* Explanatory string for errors */
{
    HierName *hierName, *hn;
    bool dontFree = FALSE;
    HashEntry *he;

    if (suffixStr == NULL)
    {
	hierName = prefix;
	dontFree = TRUE;
    }
    else hierName = EFStrToHN(prefix, suffixStr);

    he = HashLookOnly(&efNodeHashTable, (char *) hierName);
    if (he == NULL || HashGetValue(he) == NULL)
    {
	if (errorStr)
	    PrintErr("%s: no such node %s\n", errorStr, EFHNToStr(hierName));
	he = NULL;
    }

    /*
     * Free the portion of the HierName we just allocated for
     * looking in the table, if we allocated one.
     */
    if (!dontFree)
	EFHNFree(hierName, prefix, HN_ALLOC);

    return he;
}

/*
 * ----------------------------------------------------------------------------
 *
 * EFHNConcatLook --
 *
 * Like EFHNLook above, but the argument suffix is itself a HierName.
 * We construct the full name by concatenating the hierarchical prefix
 * and the node name 'suffix', then looking it up in the flat node
 * table for its real name.
 *
 * Results:
 *	See EFHNLook()'s comments.
 *
 * Side effects:
 *	See EFHNLook()'s comments.
 *
 * ----------------------------------------------------------------------------
 */

HashEntry *
EFHNConcatLook(prefix, suffix, errorStr)
    HierName *prefix;	/* Components of name on root side */
    HierName *suffix;	/* Part of name on leaf side */
    char *errorStr;	/* Explanatory string for errors */
{
    HashEntry *he;
    HierName *hn;

    /*
     * Find the last component of the suffix, then temporarily
     * link the HierNames for use as a hash key.  This is safe
     * because HashLookOnly() doesn't ever store anything in the
     * hash table, so we don't have to worry about this temporarily
     * built key somehow being saved without our knowledge.
     */
    hn = suffix;
    while (hn->hn_parent)
	hn = hn->hn_parent;
    hn->hn_parent = prefix;

    he = HashLookOnly(&efNodeHashTable, (char *) suffix);
    if (he == NULL || HashGetValue(he) == NULL)
    {
	PrintErr("%s: no such node %s\n", errorStr, EFHNToStr(suffix));
	he = (HashEntry *) NULL;
    }

    /* Undo the temp link */
    hn->hn_parent = (HierName *) NULL;
    return he;
}

/*
 * ----------------------------------------------------------------------------
 *
 * EFHNFree --
 *
 * Free a list of HierNames, up to but not including the element pointed
 * to by 'prefix' (or the NULL indicating the end of the HierName list).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees memory.
 *
 * ----------------------------------------------------------------------------
 */

void
EFHNFree(hierName, prefix, type)
    HierName *hierName, *prefix;
    int type;	/* HN_ALLOC, HN_CONCAT, etc */
{
    HierName *hn;

    for (hn = hierName; hn; hn = hn->hn_parent)
    {
	if (hn == prefix)
	    break;

	freeMagic((char *) hn);
	if (efHNStats)
	{
	    int len = strlen(hn->hn_name);
	    efHNRecord(-HIERNAMESIZE(len), type);
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * EFHNBest --
 *
 * Determine which of two names is more preferred.  The most preferred
 * name is a global name.  Given two non-global names, the one with the
 * fewest pathname components is the most preferred.  If the two names
 * have equally many pathname components, we choose the shortest.
 * If they both are of the same length, we choose the one that comes
 * later in the alphabet.
 *
 * Results:
 *	TRUE if the first name is preferable to the second, FALSE if not.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
EFHNBest(hierName1, hierName2)
    HierName *hierName1, *hierName2;
{
    int ncomponents1, ncomponents2, len1, len2;
    HierName *np1, *np2;
    char last1, last2;

    for (ncomponents1 = 0, np1 = hierName1; np1; np1 = np1->hn_parent)
	ncomponents1++;
    for (ncomponents2 = 0, np2 = hierName2; np2; np2 = np2->hn_parent)
	ncomponents2++;

    last1 = hierName1->hn_name[strlen(hierName1->hn_name) - 1];
    last2 = hierName2->hn_name[strlen(hierName2->hn_name) - 1];
    if (last1 != '!' || last2 != '!')
    {
	/* Prefer global over local names */
	if (last1 == '!') return TRUE;
	if (last2 == '!') return FALSE;

	/* Neither name is global, so chose label over generated name */
	if (last1 != '#' && last2 == '#') return TRUE;
	if (last1 == '#' && last2 != '#') return FALSE;
    }

    /*
     * Compare two names the hard way.  Both are of the same class,
     * either both global or both non-global, so compare in order:
     * number of pathname components, length, and lexicographic
     * ordering.
     */
    if (ncomponents1 < ncomponents2) return TRUE;
    if (ncomponents1 > ncomponents2) return FALSE;

    /* Non-default substrate node name is preferred over "0" */
    if (ncomponents1 == 1 && !strcmp(hierName1->hn_name, "0")) return FALSE;
    if (ncomponents2 == 1 && !strcmp(hierName2->hn_name, "0")) return TRUE;

    /* Same # of pathname components; check length */
    for (len1 = 0, np1 = hierName1; np1; np1 = np1->hn_parent)
	len1 += strlen(np1->hn_name);
    for (len2 = 0, np2 = hierName2; np2; np2 = np2->hn_parent)
	len2 += strlen(np2->hn_name);
    if (len1 < len2) return TRUE;
    if (len1 > len2) return FALSE;

    return (efHNLexOrder(hierName1, hierName2) > 0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * efHNLexOrder --
 *
 * Procedure to ensure that the canonical ordering used in determining
 * preferred names is the same as would have been used if we were comparing
 * the string version of two HierNames, instead of comparing them as pathnames
 * with the last component first.
 *
 * Results:
 *	Same as strcmp(), i.e,
 *		-1 if hierName1 should precede hierName2 lexicographically,
 *		+1 if hierName1 should follow hierName2, and
 *		 0 is they are identical.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
efHNLexOrder(hierName1, hierName2)
    HierName *hierName1, *hierName2;
{
    int i;

    if (hierName1 == hierName2)
	return 0;

    if (hierName1->hn_parent)
	if (i = efHNLexOrder(hierName1->hn_parent, hierName2->hn_parent))
	    return i;

    return strcmp(hierName1->hn_name, hierName2->hn_name);
}

/*
 * ----------------------------------------------------------------------------
 *
 * efHNFromUse --
 *
 * Construct a HierName for a cell use (for the array element identified
 * by (hc_x, hc_y) if the use is an array).  The parent pointer of this
 * newly allocated HierName will be set to prefix.
 *
 * Results:
 *	Returns a pointer to a newly allocated HierName.
 *
 * Side effects:
 *	See above.
 *	Note: we use a hash table to ensure that we always return
 *	the SAME HierName whenever prefix, the (x, y) use
 *	coordinates, and the use id are the same.
 *
 * ----------------------------------------------------------------------------
 */

HierName *
efHNFromUse(hc, prefix)
    HierContext *hc;	/* Contains use and array information */
    HierName *prefix;	/* Root part of name */
{
    char *srcp, *dstp;
    char name[2048], *namePtr;
    Use *u = hc->hc_use;
    HierName *hierName;
    bool hasX, hasY;
    HashEntry *he;
    unsigned size;

    hasX = u->use_xlo != u->use_xhi;
    hasY = u->use_ylo != u->use_yhi;
    namePtr = u->use_id;
    if (hasX || hasY)
    {
	namePtr = name;
	srcp = u->use_id;
	dstp = name;
	while (*dstp++ = *srcp++)
	    /* Nothing */;

	/* Array subscript */
	dstp[-1] = '[';

	/* Y comes before X */
	if (hasY)
	{
	    (void) sprintf(dstp, "%d", hc->hc_y);
	    while (*dstp++)
		/* Nothing */;
	    dstp--;		/* Leave pointing to NULL byte */
	}

	if (hasX)
	{
	    if (hasY) *dstp++ = ',';
	    (void) sprintf(dstp, "%d", hc->hc_x);
	    while (*dstp++)
		/* Nothing */;
	    dstp--;		/* Leave pointing to NULL byte */
	}

	*dstp++ = ']';
	*dstp = '\0';
    }

    size = HIERNAMESIZE(strlen(namePtr));
    hierName = (HierName *) mallocMagic ((unsigned)(size));
    if (efHNStats) efHNRecord(size, HN_FROMUSE);
    efHNInit(hierName, namePtr, (char *) NULL);
    hierName->hn_parent = prefix;

    /* See if we already have an entry for this one */
    he = HashFind(&efHNUseHashTable, (char *) hierName);
    if (HashGetValue(he))
    {
	freeMagic((char *) hierName);
	return (HierName *) HashGetValue(he);
    }
    HashSetValue(he, (ClientData) hierName);

    (void) HashFind(&efFreeHashTable, (char *) hierName);

    return hierName;
}

/*
 * ----------------------------------------------------------------------------
 *
 * efHNUseCompare --
 *
 *	Compare two HierNames for equality, but using a different sense
 *	of comparison than efHNCompare: two names are considered equal
 *	only if their hn_parent fields are equal and their hn_name strings
 *	are identical.
 *
 * Results: Returns 0 if they are equal, 1 if not.
 *
 * efHNUseHash --
 *
 *	Convert a HierName to a single 32-bit value suitable for being
 *	turned into a hash bucket by the hash module.  Hashes based on
 *	hierName->hn_hash and hierName->hn_parent, rather than summing
 *	the hn_hash values.
 *
 * Results: Returns the 32-bit hash value.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
efHNUseCompare(hierName1, hierName2)
    HierName *hierName1, *hierName2;
{
    return ((bool)(hierName1->hn_parent != hierName2->hn_parent
	           || strcmp(hierName1->hn_name, hierName2->hn_name)
		  ));
}

int
efHNUseHash(hierName)
    HierName *hierName;
{
    return hierName->hn_hash + (spointertype) hierName->hn_parent;
}

/*
 * ----------------------------------------------------------------------------
 *
 * efHNInit --
 *
 * Copy the string 'cp' into hierName->hn_name, also initializing
 * the hn_hash fields of hierName.  If 'endp' is NULL, copy all
 * characters in 'cp' up to a trailing NULL byte; otherwise, copy
 * up to 'endp'.
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
efHNInit(hierName, cp, endp)
    HierName *hierName;		/* Fill in fields of this HierName */
    char *cp;		/* Start of name to be stored in hn_name */
    char *endp;	/* End of name if non-NULL; else, see above */
{
    unsigned hashsum;
    char *dstp;

    hashsum = 0;
    dstp = hierName->hn_name;
    if (endp)
    {
	while (cp < endp)
	{
	    hashsum = HASHADDVAL(hashsum, *cp);
	    *dstp++ = *cp++;
	}
	*dstp = '\0';
    }
    else
    {
	while (*dstp++ = *cp)
	    hashsum = HASHADDVAL(hashsum, *cp++);
    }

    hierName->hn_hash = hashsum;
}

/*
 * ----------------------------------------------------------------------------
 *
 * efHNCompare --
 *
 *	Compare two HierNames for equality.  Passed as a client procedure
 *	to the hash module.  The most likely place for a difference in the
 *	two names is in the lowest-level component, which fortunately is
 *	the first in a HierName list.
 *
 * Results:
 *	Returns 0 if they are equal, 1 if not.
 *
 * efHNHash --
 *
 *	Convert a HierName to a single 32-bit value suitable for being
 *	turned into a hash bucket by the hash module.  Passed as a client
 *	procedure to the hash module.
 *
 * Results:
 *	Returns the 32-bit hash value.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
efHNCompare(hierName1, hierName2)
    HierName *hierName1, *hierName2;
{
    while (hierName1)
    {
	if (hierName1 == hierName2)
	    return 0;

	if (hierName2 == NULL
		|| hierName1->hn_hash != hierName2->hn_hash
		|| strcmp(hierName1->hn_name, hierName2->hn_name) != 0)
	    return 1;
	hierName1 = hierName1->hn_parent;
	hierName2 = hierName2->hn_parent;
    }

    return (hierName2 ? 1 : 0);
}

int
efHNHash(hierName)
    HierName *hierName;
{
    int n;

    for (n = 0; hierName; hierName = hierName->hn_parent)
	n += hierName->hn_hash;

    return n;
}

/*
 * ----------------------------------------------------------------------------
 *
 * efHNDistCompare --
 * efHNDistCopy --
 * efHNDistHash --
 * efHNDistKill --
 *
 * Procedures for managing a HashTable whose keys are pointers
 * to malloc'd Distance structures.  Distances point to a pair of
 * HierNames; the comparison and hashing functions rely directly
 * on those for processing HierNames (efHNCompare() and efHNHash()).
 *
 * Results:
 *	efHNDistCompare returns 0 if the two keys are equal, 1 if not.
 *	efHNDistCopy returns a pointer to a malloc'd copy of its Distance
 *	    argument.
 *	efHNDistHash returns a single 32-bit hash value based on a Distance's
 *	    two HierNames.
 *	efHNDistKill has no return value.
 *
 * Side effects:
 *	efHNDistKill frees its Distance argument, and adds the HierNames
 *	pointed to by it to the table of HierNames to free.
 *
 * ----------------------------------------------------------------------------
 */

bool
efHNDistCompare(dist1, dist2)
    Distance *dist1, *dist2;
{
    return ((bool)(efHNCompare(dist1->dist_1, dist2->dist_1)
	           || efHNCompare(dist1->dist_2, dist2->dist_2)
		  ));
}

char *
efHNDistCopy(dist)
    Distance *dist;
{
    Distance *distNew;

    distNew = (Distance *) mallocMagic ((unsigned)(sizeof (Distance)));
    *distNew = *dist;
    return (char *) distNew;
}

int
efHNDistHash(dist)
    Distance *dist;
{
    return efHNHash(dist->dist_1) + efHNHash(dist->dist_2);
}


void
efHNDistKill(dist)
    Distance *dist;
{
    HierName *hn;

    for (hn = dist->dist_1; hn; hn = hn->hn_parent)
	(void) HashFind(&efFreeHashTable, (char *) hn);
    for (hn = dist->dist_2; hn; hn = hn->hn_parent)
	(void) HashFind(&efFreeHashTable, (char *) hn);

    freeMagic((char *) dist);
}

/*
 * ----------------------------------------------------------------------------
 *
 * efHNBuildDistKey --
 *
 * Build the key for looking in the Distance hash table for efFlatDists()
 * above.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets up *distKey.
 *
 * ----------------------------------------------------------------------------
 */

void
efHNBuildDistKey(prefix, dist, distKey)
    HierName *prefix;
    Distance *dist;
    Distance *distKey;
{
    HierName *hn1, *hn2;

    hn1 = EFHNConcat(prefix, dist->dist_1);
    hn2 = EFHNConcat(prefix, dist->dist_2);
    if (EFHNBest(hn1, hn2))
    {
	distKey->dist_1 = hn1;
	distKey->dist_2 = hn2;
    }
    else
    {
	distKey->dist_1 = hn2;
	distKey->dist_2 = hn1;
    }

    distKey->dist_min = dist->dist_min;
    distKey->dist_max = dist->dist_max;
}

/*
 * ----------------------------------------------------------------------------
 *
 * efHNDump --
 *
 * Print all the names in the node hash table efNodeHashTable.
 * Used for debugging.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Creates a file "hash.dump" and writes the node names to
 *	it, one per line.
 *
 * ----------------------------------------------------------------------------
 */

void
efHNDump()
{
    HashSearch hs;
    HashEntry *he;
    FILE *f;

    f = fopen("hash.dump", "w");
    if (f == NULL)
    {
	perror("hash.dump");
	return;
    }

    HashStartSearch(&hs);
    while (he = HashNext(&efNodeHashTable, &hs))
	fprintf(f, "%s\n", EFHNToStr((HierName *) he->h_key.h_ptr));

    (void) fclose(f);
}

int efHNSizes[4];

void
efHNRecord(size, type)
    int size;
    int type;
{
    efHNSizes[type] += size;
}

void
efHNPrintSizes(when)
    char *when;
{
    int total, i;

    total = 0;
    for (i = 0; i < 4; i++)
	total += efHNSizes[i];

    printf("Memory used in HierNames %s:\n", when ? when : "");
    printf("%8d bytes for global names\n", efHNSizes[HN_GLOBAL]);
    printf("%8d bytes for concatenated HierNames\n", efHNSizes[HN_CONCAT]);
    printf("%8d bytes for names from cell uses\n", efHNSizes[HN_FROMUSE]);
    printf("%8d bytes for names from strings\n", efHNSizes[HN_ALLOC]);
    printf("--------\n");
    printf("%8d bytes total\n", total);
}
