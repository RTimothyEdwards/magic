/*
 * heap.c -
 *
 *	Routines to create and manipulate heaps.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/heap.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/heap.h"
#include "utils/utils.h"
#include "utils/malloc.h"

#define heapLeft(used,i)  ((used) < ( (i)<<1)    ? 0 : (i)+(i)  )
#define heapRight(used,i) ((used) < (((i)<<1)+1) ? 0 : (i)+(i)+1)

#define	KEY_LESS_COND(t, l, i1, i2, stmt) \
	switch (t) { \
	    case HE_INT: if (l[i1].he_int < l[i2].he_int) stmt; \
		break; \
	    case HE_DLONG: if ((l[i1].he_dlong < l[i2].he_dlong)) stmt; \
		break; \
	    case HE_DOUBLE: if (l[i1].he_double < l[i2].he_double) stmt; \
		break; \
	    case HE_FLOAT: if (l[i1].he_float < l[i2].he_float) stmt; \
		break; \
	}

#define	KEY_LE_COND(t, l, i1, i2, stmt) \
	switch (t) { \
	    case HE_INT: if (l[i1].he_int <= l[i2].he_int) stmt; \
		break; \
	    case HE_DLONG: if ((l[i1].he_dlong <=l[i2].he_dlong)) stmt; \
		break; \
	    case HE_DOUBLE: if (l[i1].he_double <= l[i2].he_double) stmt; \
		break; \
	    case HE_FLOAT: if (l[i1].he_float <= l[i2].he_float) stmt; \
		break; \
	}

#define	KEY_GREATER_COND(t, l, i1, i2, stmt) \
	switch (t) { \
	    case HE_INT: if (l[i1].he_int > l[i2].he_int) stmt; \
		break; \
	    case HE_DLONG:if(l[i1].he_dlong > l[i2].he_dlong) stmt; \
		break; \
	    case HE_DOUBLE: if (l[i1].he_double > l[i2].he_double) stmt; \
		break; \
	    case HE_FLOAT: if (l[i1].he_float > l[i2].he_float) stmt; \
		break; \
	}

#define	KEY_GE_COND(t, l, i1, i2, stmt) \
	switch (t) { \
	    case HE_INT: if (l[i1].he_int >= l[i2].he_int) stmt; \
		break; \
	    case HE_DLONG: if ((l[i1].he_dlong >= l[i2].he_dlong)) stmt; \
		break; \
	    case HE_DOUBLE: if (l[i1].he_double >= l[i2].he_double) stmt; \
		break; \
	    case HE_FLOAT: if (l[i1].he_float >= l[i2].he_float) stmt; \
		break; \
	}

/* Forward declarations */

extern void heapify();


/*
 * ----------------------------------------------------------------------------
 *
 * HeapInit --
 * HeapInitType --
 *
 * Initialize a heap.  The first form is a heap with integer keys; the
 * second allows specification of the type of key to use.
 *
 * Note that with this addressing scheme it is necessary to allocate
 * 2**n + 1 locations for the heap block, location 0 being unused.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates storage for the heap.  Initializes the fields in the heap
 *	struct.  Memory is allocated.
 *
 * ----------------------------------------------------------------------------
 */

void
HeapInit(heap, size, descending, stringIds)
    Heap *heap;		/* Pointer to a heap struct */
    int size;		/* Initial size of heap */
    int	descending;	/* TRUE if largest on top */
    int	stringIds;	/* TRUE if entry id's are strings */
{
    HeapInitType(heap, size, descending, stringIds, HE_INT);
}

void
HeapInitType(heap, size, descending, stringIds, keyType)
    Heap *heap;		/* Pointer to a heap struct */
    int size;		/* Initial size of heap */
    int	descending;	/* TRUE if largest on top */
    int	stringIds;	/* TRUE if entry id's are strings */
    int keyType;	/* Type of keys to use */
{
    if (size < 0) size = -size;
    heap->he_size = 2;
    while (size > heap->he_size)
	heap->he_size <<= 1;
    heap->he_used = 0;
    heap->he_built = 0;
    heap->he_big = descending;
    heap->he_stringId = stringIds;

    /*
     * Add 2 to the size because location 0 is used as a temporary,
     * and location (2**n) + 1 is the last leaf
     */
    switch (heap->he_keyType = keyType)
    {
	case HE_INT:
	case HE_DLONG:
	case HE_FLOAT:
	case HE_DOUBLE:
	    break;
	default:
	    TxError("Unsupported key type: %d\n", keyType);
	    break;
    }

    heap->he_list = (HeapEntry *) mallocMagic((unsigned) ((heap->he_size+2)
		* sizeof (HeapEntry)));
    ASSERT(heap->he_list != NULL, "Malloc failed in HeapInit");
}

/*
 * ----------------------------------------------------------------------------
 *
 * HeapKill --
 *
 * Deallocate all storage associated with the heap.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Storage is freed.  Fields in the heap record are reset.  If func is
 *	not NULL then call it with each heap element before freeing the heap
 *	entry.
 *
 * ----------------------------------------------------------------------------
 */

void
HeapKill(heap, func)
    Heap *heap;		/* The heap being killed */
    void (*func)();	/* Some function to free each id element */
{
    int i;

    /*
     * Free used locations except 0, since that location is reserved to pass
     * back values.
     */
    if (func)
        for (i = 1; i <= heap->he_used; i++)
	   (*func) (heap, i);
    freeMagic((char *) heap->he_list);
    heap->he_list = NULL;
}

/*
 * ----------------------------------------------------------------------------
 *
 * HeapFreeIdFunc --
 *
 * 	Supplied function to HeapKill.  Frees the referenced entry id if it
 *	is a string, otherwise do nothing.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory is freed.
 *
 * ----------------------------------------------------------------------------
 */

void
HeapFreeIdFunc(heap, i)
    Heap *heap;	/* The heap in which the free is performed */
    int i;			/* The index of the entry whose id gets freed */
{
    if (heap->he_stringId)
	freeMagic(heap->he_list[i].he_id);
}

/*
 * ----------------------------------------------------------------------------
 *
 * HeapRemoveTop --
 *	Delete the top element from the heap.
 *
 * Results:
 *	Pointer to the removed heap element, which is the same as the entry
 *	pointer passed to the function from outside,  or NULL if no entries
 *	remain.
 *
 * Side effects:
 *	If the heap has been touched, restore the heap property before removing
 *	the top element.
 *
 * ----------------------------------------------------------------------------
 */

HeapEntry *
HeapRemoveTop(heap, entry)
    Heap *heap;	/* The heap from which the top is removed */
    HeapEntry *entry;		/* Return the value in this struct */
{
    int i;

    if (heap->he_used == 0)
	return (HeapEntry *) NULL;

    if (!heap->he_built)
	for (i = heap->he_used; i > 0; i--)
	    heapify(heap, i);

    heap->he_built = heap->he_used;
    *entry = heap->he_list[1];
    heap->he_list[1] = heap->he_list[heap->he_used];
    heap->he_used--;
    heapify(heap, 1);
    return entry;
}

/*
 * ----------------------------------------------------------------------------
 *
 * HeapLookAtTop --
 *
 * Return pointer to top element, but don't remove it.
 *
 * Results:
 *	Pointer to the top heap element, or NULL if heap is empty.
 *
 * Side effects:
 *	If the heap has been touched, restore the heap property before 
 *      returning the top element.
 *
 * ----------------------------------------------------------------------------
 */

HeapEntry *
HeapLookAtTop(heap)
    Heap *heap;
{
    int i;

    if (heap->he_used == 0)
	return (HeapEntry *) NULL;

    if (!heap->he_built)
	for (i = heap->he_used; i > 0; i--)
	    heapify(heap, i);

    heap->he_built = heap->he_used;
    return &heap->he_list[1];
}

/*
 * ----------------------------------------------------------------------------
 *
 * heapify --
 *
 * Restore the heap property to the heap, where the root has changed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Things get shuffled around in the heap.  Location 0 is trashed.
 *
 * ----------------------------------------------------------------------------
 */

void
heapify(heap, root)
    Heap *heap;
    int root;
{
    HeapEntry *list = heap->he_list;
    int x, r, used = heap->he_used;
    int keyType = heap->he_keyType;

    if (heap->he_big)
    {
	/* Biggest one on the top of the heap */
	while (1)
	{
	    if ((x = heapLeft(used, root)) == 0) break;
	    if (r = heapRight(used, root))
		KEY_LESS_COND(keyType, list, x, r, x = r);

	    KEY_LE_COND(keyType, list, x, root, return);
	    *list = list[root];
	    list[root] = list[x];
	    list[x] = *list;
	    root = x;
	}
    }
    else
    {
	/* Smallest one on the top of the heap */
	while (1)
	{
	    if ((x = heapLeft(used, root)) == 0) break;
	    if (r = heapRight(used, root))
				KEY_GREATER_COND(keyType, list, x, r, x = r);
	    KEY_GE_COND(keyType, list, x, root, return);
	    *list = list[root];
	    list[root] = list[x];
	    list[x] = *list;
	    root = x;
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * HeapAdd --
 *
 * Add an item to the bottom of the heap.  Restore the heap structure
 * by propagating the item upwards.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Things get shuffled around in the heap.
 *	Free the old block and allocate a larger block if necessary.
 *
 * Warning:
 *	Something awful may happen if the id value was declared a string in
 *	HeapInit and you provide something else.
 *
 * Note:
 *	Because throwing pointers to indeterminate types (int, float, etc.)
 *	into the second argument produces results with unititialized fields,
 *	use of HeapAdd() is now deprecated, in favor of the determinate-type
 *	versions HeapAddInt, etc. (see below).  These routines also protect
 *	against writing any data type into a heap initialized as a different
 *	data type.
 *
 * ----------------------------------------------------------------------------
 */

void
HeapAdd(heap, pKey, id)
    Heap *heap;
    union heUnion *pKey;
    char *id;
{
    HeapEntry *list = heap->he_list;
    int i, cmp, keyType;

    keyType = heap->he_keyType;
    if (heap->he_used == heap->he_size)
    {
	HeapEntry *new;

	/* Need to recopy to a larger area */
	new = (HeapEntry *) mallocMagic((unsigned) ((2 * heap->he_size + 2)
			* sizeof (HeapEntry)));
	bcopy((char *) list, (char *) new,
		(heap->he_size + 2) * sizeof (HeapEntry));
	freeMagic((char *) heap->he_list);
	heap->he_list = list = new;
	heap->he_size *= 2;
    }

    i = ++heap->he_used;
    list[i].he_union = *pKey;
    if (heap->he_stringId) list[i].he_id = StrDup((char **) NULL, id);
    else list[i].he_id = id;

    if (heap->he_built)
    {
	if (heap->he_big)
	{
	    /* Biggest on the top */
	    while (1)
	    {
		/* If odd then new entry is the right half of a pair */
		cmp = i;
		if (i & 1)
		    KEY_LE_COND(keyType, list, i, i-1, cmp = i-1);

		/* Find parent.  If 0 then at the root so quit */
		if ((i >>= 1) == 0) return;
		KEY_LE_COND(keyType, list, cmp, i, return);
		list[0] = list[cmp]; list[cmp] = list[i]; list[i] = list[0];
		heapify(heap, cmp);
	    }
	}
	else
	{
	    /* Smallest on top */
	    while (1)
	    {
		/* If odd then new entry is the right half of a pair */
		cmp = i;
		if (i & 1)
		    KEY_GE_COND(keyType, list, i, i-1, cmp = i-1);

		/* Find parent.  If 0 then at the root so quit */
		if ((i >>= 1) == 0) return;
		KEY_GE_COND(keyType, list, cmp, i, return);
		list[0] = list[cmp]; list[cmp] = list[i]; list[i] = list[0];
		heapify(heap, cmp);
	    }
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 * HeapAddInt, HeapAddDLong, HeapAddFloat, HeapAddDouble --
 *
 *	Type-determinate versions of HeapAdd().  Note that unlike HeapAdd(),
 *	the second argument is the value, not a pointer to the value.
 *
 * ----------------------------------------------------------------------------
 */
void
HeapAddInt(heap, data, id)
    Heap *heap;
    int data;
    char *id;
{
    union heUnion pKey;

    ASSERT(heap->he_keyType == HE_INT,
		"Attempt to add type int to non-int heap.");
    pKey.hu_int = data;
    HeapAdd(heap, &pKey, id);
}

void
HeapAddDLong(heap, data, id)
    Heap *heap;
    dlong data;
    char *id;
{
    union heUnion pKey;

    ASSERT(heap->he_keyType == HE_DLONG,
		"Attempt to add type double long to non-double long heap.");
    pKey.hu_dlong = data;
    HeapAdd(heap, &pKey, id);
}

void
HeapAddFloat(heap, data, id)
    Heap *heap;
    float data; 
    char *id;
{
    union heUnion pKey;

    ASSERT(heap->he_keyType == HE_FLOAT,
		"Attempt to add type float to non-float heap.");
    pKey.hu_float = data;
    HeapAdd(heap, &pKey, id);
}

void
HeapAddDouble(heap, data, id)
    Heap *heap;
    double data;
    char *id;
{
    union heUnion pKey;

    ASSERT(heap->he_keyType == HE_DOUBLE,
		"Attempt to add type double to non-double heap.");
    pKey.hu_double = data;
    HeapAdd(heap, &pKey, id);
}


/*
 * ----------------------------------------------------------------------------
 *
 * HeapDump --
 * 	Dump the contents of the heap for debugging purposes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
HeapDump(heap)
    Heap *heap;
{
    int i;

    if (heap->he_big) printf("Heap with biggest on the top\n");
    else printf("Heap with smallest on the top\n");

    for (i = 1; i <= heap->he_used; i++)
    {
	printf("[%d]: Key ", i);
	switch (heap->he_keyType)
	{
	    case HE_INT:	printf("%d", heap->he_list[i].he_int); break;
	    case HE_FLOAT:	printf("%f", heap->he_list[i].he_float); break;
	    case HE_DOUBLE:	printf("%f", heap->he_list[i].he_double); break;
	    case HE_DLONG:	printf("%"DLONG_PREFIX"d", heap->he_list[i].he_dlong); break;
	}
	if (heap->he_stringId == TRUE)
	    printf("//id %s; ", heap->he_list[i].he_id);
	else
	    printf("//id %p; ", heap->he_list[i].he_id);
    }
    printf("\n");
}
