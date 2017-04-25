/*
 * dqueue.c --
 *
 *	Routines for double ended queues.  See 'dqueue.h'.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/dqueue.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/dqueue.h"
#include "utils/malloc.h"

/*
 * ----------------------------------------------------------------------------
 *
 * DQInit --
 *
 *	Initialize a new queue to have a certain capacity.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
DQInit(q, capacity)
    DQueue *q;
    int capacity;
{
    if (capacity < 1) capacity = 1;
    q->dq_data = (ClientData *) mallocMagic((unsigned)((capacity+1) * sizeof (ClientData)));
    q->dq_size = 0;
    q->dq_maxSize = capacity;
    q->dq_front = 0;	/* next slot in front is loc 0 */
    q->dq_rear = 1;	/* next slot in rear is loc 1 */
}


/*
 * ----------------------------------------------------------------------------
 *
 * DQFree --
 *
 *	Free up a queue.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
DQFree(q)
    DQueue *q;
{
    freeMagic((char *) q->dq_data); 
}

/*
 * ----------------------------------------------------------------------------
 *
 * DQPushFront & DQPushRear --
 *
 *	Push a new element onto one end of the DQueue.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Puts an element in the queue.
 *
 * ----------------------------------------------------------------------------
 */

void
DQPushFront(q, elem)
    DQueue *q;
    ClientData elem;
{
    if (q->dq_size == q->dq_maxSize)  DQChangeSize(q, 2 * q->dq_maxSize);
    q->dq_data[q->dq_front] = elem; 
    q->dq_front--;
    if (q->dq_front < 0) q->dq_front = q->dq_maxSize;
    q->dq_size++;
}

void
DQPushRear(q, elem)
    DQueue *q;
    ClientData elem;
{
    if (q->dq_size == q->dq_maxSize)  DQChangeSize(q, 2 * q->dq_maxSize);
    q->dq_data[q->dq_rear] = elem; 
    q->dq_rear++;
    if (q->dq_rear > q->dq_maxSize) q->dq_rear = 0;
    q->dq_size++;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DQPopFront & DQPopRear --
 *
 *	Pop an element from one end of the queue.
 *
 * Results:
 *	The element, or NULL if there is none.
 *
 * Side Effects:
 *	Removes the element from the queue.
 *
 * ----------------------------------------------------------------------------
 */

ClientData
DQPopFront(q)
    DQueue *q;
{
    if (q->dq_size == 0) return (ClientData) NULL;
    q->dq_size--;
    q->dq_front++;
    if (q->dq_front > q->dq_maxSize) q->dq_front = 0;
    return q->dq_data[q->dq_front];
}

ClientData
DQPopRear(q)
    DQueue *q;
{
    if (q->dq_size == 0) return (ClientData) NULL;
    q->dq_size--;
    q->dq_rear--;
    if (q->dq_rear < 0) q->dq_rear = q->dq_maxSize;
    return q->dq_data[q->dq_rear];
}


/*
 * ----------------------------------------------------------------------------
 *
 * DQChangeSize --
 *
 *	Change the size of a DQueue -- either increase or decrease.
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	The DQueue changes size.
 *
 * ----------------------------------------------------------------------------
 */

void
DQChangeSize(q, newSize)
    DQueue *q;
    int newSize;
{
    DQueue newq;

    if (newSize < q->dq_size) newSize = q->dq_size;
    DQInit(&newq, newSize);
    DQCopy(&newq, q);
    freeMagic((char *) q->dq_data); 
    q->dq_data = newq.dq_data;
    q->dq_maxSize = newq.dq_maxSize;
    q->dq_front = newq.dq_front;
    q->dq_rear = newq.dq_rear;
}


/*
 * ----------------------------------------------------------------------------
 *
 * DQCopy --
 *
 *	Copy one DQueue into another.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Elements (ClientData pointers) are copied.
 *
 * ----------------------------------------------------------------------------
 */

void
DQCopy(dst, src)
    DQueue *dst;	/* The destination queue */
    DQueue *src;	/* The source queue */
{
    int i;
    dst->dq_size = 0;
    i = src->dq_front;
    while (dst->dq_size != src->dq_size) {
	i = i + 1;
	if (i > src->dq_maxSize) i = 0;
	DQPushRear(dst, src->dq_data[i]);
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * Main --
 *
 *	Test out this module.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Stuff on the screen.
 *
 * ----------------------------------------------------------------------------
 */

/****
void
main()
{
    int i;
    DQueue q;
    DQInit(&q, 0);
    for (i = 0; i < 10; i++) DQPushFront(&q, (ClientData) i);
    while (!DQIsEmpty(&q)) printf("got %d\n", (int) DQPopRear(&q));
    DQFree(&q);
    printf("-------\n");
    DQInit(&q, 0);
    for (i = 0; i < 10; i++) DQPushFront(&q, (ClientData) i);
    for (i = 0; i < 10000; i++) DQPushRear(&q, DQPopFront(&q));
    for (i = 0; i < 10000; i++) DQPushFront(&q, DQPopRear(&q));
    while (!DQIsEmpty(&q)) printf("got %d\n", (int) DQPopFront(&q));
}
****/
