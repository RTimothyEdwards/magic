/*
 * gcrChannel.c -
 *
 * Channel manipulation: allocation, freeing, and transforming
 * (e.g, flipping them left-to-right or rotating).  Transforming
 * is done so that the channel can be routed in the easiest
 * direction.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/gcr/gcrChannel.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "gcr/gcr.h"
#include "utils/malloc.h"


/*
 * ----------------------------------------------------------------------------
 *
 * GCRNewChannel --
 *
 * This procedure allocates storage for a new channel structure,
 * and initializes most of the structure.
 *
 * Results:
 *	The return value is a pointer to the new channel.
 *
 * Side effects:
 *	Storage is allocated.  The initial state of the channel is
 *	completely empty, although all pin and result arrays are
 *	allocated.  This procedure does not initialize the fields
 *	gcr_area, gcr_origin, or gcr_lCol, although it allocates
 *	storage for gcr_lCol.  Also, it doesn't initialize the
 *	value of gcr_point for each pin.
 *
 * ----------------------------------------------------------------------------
 */

GCRChannel *
GCRNewChannel(length, width)
    int length;		/* Length of the channel (# of columns) */
    int width;		/* Width of the channel (# rows) */
{
    unsigned lenWds, widWds, nBytes;
    GCRChannel *ch;
    int i;

    lenWds = length + 2;
    widWds = width + 2;

    ch = (GCRChannel *) mallocMagic((unsigned) (sizeof (GCRChannel)));
    ch->gcr_type = CHAN_NORMAL;
    ch->gcr_length = length;
    ch->gcr_width = width;
    ch->gcr_transform = GeoIdentityTransform;
    ch->gcr_nets  = (GCRNet *) NULL;

    /* Malloc storage for pin arrays and zero each */
    nBytes = lenWds * sizeof (GCRPin);
    ch->gcr_tPins = (GCRPin *) mallocMagic((unsigned) nBytes);
    ch->gcr_bPins = (GCRPin *) mallocMagic((unsigned) nBytes);
    bzero((char *) ch->gcr_tPins, (int) nBytes);
    bzero((char *) ch->gcr_bPins, (int) nBytes);

    nBytes = widWds * sizeof (GCRPin);
    ch->gcr_lPins = (GCRPin *) mallocMagic((unsigned) nBytes);
    ch->gcr_rPins = (GCRPin *) mallocMagic((unsigned) nBytes);
    bzero((char *) ch->gcr_lPins, (int) nBytes);
    bzero((char *) ch->gcr_rPins, (int) nBytes);

    ch->gcr_lCol = (GCRColEl *) mallocMagic((unsigned) (widWds * sizeof (GCRColEl)));
    ch->gcr_density = (int *) mallocMagic((unsigned) (lenWds * sizeof (int)));

    /* Global router-specific initialization */
    ch->gcr_dRowsByCol = (short *) mallocMagic((unsigned) (lenWds * sizeof (short)));
    bzero((char *) ch->gcr_dRowsByCol, (int) lenWds * sizeof (short));
    ch->gcr_dColsByRow = (short *) mallocMagic ((unsigned) (widWds * sizeof (short)));
    bzero((char *) ch->gcr_dColsByRow, (int) widWds * sizeof (short));
    ch->gcr_dMaxByRow = ch->gcr_dMaxByCol = 0;

#ifdef	IDENSITY
	/* For debugging */
    ch->gcr_iRowsByCol = (short *) mallocMagic((unsigned) (lenWds * sizeof (short)));
    bzero((char *) ch->gcr_iRowsByCol, (int) lenWds * sizeof (short));
    ch->gcr_iColsByRow = (short *) mallocMagic((unsigned) (widWds * sizeof (short)));
    bzero((char *) ch->gcr_iColsByRow, (int) widWds * sizeof (short));
#endif	/* IDENSITY */

    ch->gcr_client = (ClientData) NULL;

    /* Allocate the result array */
    ch->gcr_result = (short **) mallocMagic ((unsigned) (lenWds * sizeof (short *)));

    /*
     * Fill in fields of pins that aren't zero; also allocate
     * and clear each row of the result array.
     */
    nBytes = widWds * sizeof (short);
    for (i = 0; i < lenWds; i++)
    {
	ch->gcr_result[i] = (short *) mallocMagic((unsigned) nBytes);
	bzero((char *) ch->gcr_result[i], (int) nBytes);

	/* BOTTOM */
	ch->gcr_bPins[i].gcr_pDist = -1;
	ch->gcr_bPins[i].gcr_x = i;
	ch->gcr_bPins[i].gcr_y = 0;

	/* TOP */
	ch->gcr_tPins[i].gcr_pDist = -1;
	ch->gcr_tPins[i].gcr_x = i;
	ch->gcr_tPins[i].gcr_y = widWds - 1;
    }

    for (i = 0; i < widWds; i++)
    {
	/* LEFT */
	ch->gcr_lPins[i].gcr_pDist = -1;
	ch->gcr_lPins[i].gcr_x = 0;
	ch->gcr_lPins[i].gcr_y = i;

	/* RIGHT */
	ch->gcr_rPins[i].gcr_pDist = -1;
	ch->gcr_rPins[i].gcr_x = lenWds - 1;
	ch->gcr_rPins[i].gcr_y = i;
    }

    return (ch);
}

/*
 * ----------------------------------------------------------------------------
 *
 * GCRFreeChannel --
 *
 * 	This procedure frees up all the storage associated with
 *	a channel
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The storage of ch is completely freed.  The caller should
 *	refrain from any use of the pointer after this procedure
 *	returns.
 *
 * ----------------------------------------------------------------------------
 */

void
GCRFreeChannel(ch)
    GCRChannel *ch;		/* Pointer to channel structure to be freed. */
{
    GCRNet *net;
    int i;

    freeMagic((char *) ch->gcr_tPins);
    freeMagic((char *) ch->gcr_bPins);
    freeMagic((char *) ch->gcr_lPins);
    freeMagic((char *) ch->gcr_rPins);
    for (net = ch->gcr_nets; net; net = net->gcr_next)
	freeMagic((char *) net);
    freeMagic((char *) ch->gcr_lCol);

    freeMagic((char *) ch->gcr_dRowsByCol);
    freeMagic((char *) ch->gcr_dColsByRow);

#ifdef	IDENSITY
	/* For debugging */
    freeMagic((char *) ch->gcr_iRowsByCol);
    freeMagic((char *) ch->gcr_iColsByRow);
#endif	/* IDENSITY */

    freeMagic((char *) ch->gcr_density);
    for (i = 0; i <= ch->gcr_length + 1; i++)
	freeMagic((char *) ch->gcr_result[i]);
    freeMagic((char *) ch->gcr_result);
    freeMagic((char *) ch);
}

/*
 * ----------------------------------------------------------------------------
 *
 * GCRFlipLeftRight --
 *
 * 	This procedure will flip the contents of a channel left-to-right.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The contents of channel dst are modified so that they are
 *	a left-to-right flip of src.  Only the four pin arrays, the
 *	transform, the origin, the area, and the result array are
 *	modified. The other fields of dst are untouched.  This means,
 *	in particular, that this procedure should be called BEFORE
 *	the linked lists for nets get set up, or else AFTER all the
 *	channel routing has been done and the nets have been freed up.
 *	Also, this procedure does not transform the flag bits GCRTE or
 *	GCRCE, so this procedure shouldn't be called when those flags
 *	are significant.
 *
 * ----------------------------------------------------------------------------
 */

void
GCRFlipLeftRight(src, dst)
    GCRChannel *src;	/* Original channel. */
    GCRChannel *dst;	/* Channel to be modified to contain
				 * transformed info from src.  The two
				 * channels must have the same dimensions,
				 * but must not be the same channel.
				 */
{
    int y, lenWds, widWds;
    short old, new;
    int i, j;
    Transform t;

    ASSERT(src->gcr_length == dst->gcr_length, "GCRFlipLeftRight: mismatch");
    ASSERT(src->gcr_width == dst->gcr_width, "GCRFlipLeftRight: mismatch");

    lenWds = src->gcr_length + 1;
    widWds = src->gcr_width + 1;

    for (i = 0; i <= lenWds; i++)
    {
	j = lenWds - i;

	    /* Exchange pairs of pins in the top and bottom arrays */
	dst->gcr_tPins[j] = src->gcr_tPins[i];
	dst->gcr_tPins[j].gcr_x = j;
	dst->gcr_bPins[j] = src->gcr_bPins[i];
	dst->gcr_bPins[j].gcr_x = j;

	    /*
	     * Go through the result array, exchanging flag values.  Also,
	     * be careful to switch the left-right hazard bits now that the
	     * sense of the channel is reversed.  Also switch the GCRR bit
	     * in case there's been routing done.
	     */
	for (y = 0; y <= widWds; y++)
	{
	    old = src->gcr_result[i][y];
	    new = old & ~(GCRVR|GCRVL|GCRR);
	    if (old & GCRVR) new |= GCRVL;
	    if (old & GCRVL) new |= GCRVR;
	    if (i != 0 && (src->gcr_result[i-1][y] & GCRR))
		new |= GCRR;
	    dst->gcr_result[j][y] = new;
	}
    }

	/* Switch the left and right end pins */
    for (i = 0; i <= widWds; i++)
    {
	dst->gcr_lPins[i] = src->gcr_rPins[i];
	dst->gcr_lPins[i].gcr_x = 0;
	dst->gcr_rPins[i] = src->gcr_lPins[i];
	dst->gcr_rPins[i].gcr_x = widWds;
    }

	/* Copy the horizontal and vertical density information */
    dst->gcr_dMaxByCol = src->gcr_dMaxByCol;
    dst->gcr_dMaxByRow = src->gcr_dMaxByRow;
    bcopy((char *) src->gcr_dColsByRow, (char *) dst->gcr_dColsByRow,
		sizeof (short) * widWds);
#ifdef	IDENSITY
    bcopy((char *) src->gcr_iColsByRow, (char *) dst->gcr_iColsByRow,
		sizeof (short) * widWds);
#endif	/* IDENSITY */
    for (i = 0; i <= lenWds; i++)
    {
	/* Flip left-to-right */
	j = lenWds - i;
	dst->gcr_dRowsByCol[j] = src->gcr_dRowsByCol[i];
#ifdef	IDENSITY
	dst->gcr_iRowsByCol[j] = src->gcr_iRowsByCol[i];
#endif	/* IDENSITY */
    }

	/* Now fix up the transform of the new channel */
    GeoTranslateTrans(&GeoSidewaysTransform, src->gcr_length+1, 0, &t);
    GeoTransTrans(&t, &src->gcr_transform, &dst->gcr_transform);
    dst->gcr_origin = src->gcr_origin;
    dst->gcr_area = src->gcr_area;
    dst->gcr_type = src->gcr_type;
}

/*
 * ----------------------------------------------------------------------------
 *
 * GCRFlipXY --
 *
 * 	This procedure rotates and flips a channel to interchange
 *	x and y coordinates.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The contents of channel dst are modified so that they are
 *	a x-y flip of src.  See comments for GCRFlipLeftRight for
 *	warnings about when it is and isn't safe to call this
 *	procedure.
 *
 * ----------------------------------------------------------------------------
 */

void
GCRFlipXY(src, dst)
    GCRChannel *src;	/* Original channel. */
    GCRChannel *dst;	/* Channel to be modified to contain
				 * transformed info from src.
				 */
{
    static Transform flipxy = {0, 1, 0, 1, 0, 0};
    int tmp, lenWds, widWds;
    short old, new;
    int i, j;

    ASSERT(src->gcr_width == dst->gcr_length, "gcrFlipXY: channel mismatch");
    ASSERT(src->gcr_length == dst->gcr_width, "gcrFlipXY: channel mismatch");

    lenWds = src->gcr_length + 1;
    widWds = src->gcr_width + 1;

	/* First, flip the side pins to top and bottom */
    for (i = 0; i <= widWds; i++)
    {
	dst->gcr_tPins[i] = src->gcr_rPins[i];
	tmp = dst->gcr_tPins[i].gcr_x;
	dst->gcr_tPins[i].gcr_x = dst->gcr_tPins[i].gcr_y;
	dst->gcr_tPins[i].gcr_y = tmp;
	dst->gcr_bPins[i] = src->gcr_lPins[i];
	tmp = dst->gcr_bPins[i].gcr_x;
	dst->gcr_bPins[i].gcr_x = dst->gcr_bPins[i].gcr_y;
	dst->gcr_bPins[i].gcr_y = tmp;
    }

	/* Same thing except flip top and bottom pins to sides */
    for (i = 0; i <= lenWds; i++)
    {
	dst->gcr_rPins[i] = src->gcr_tPins[i];
	tmp = dst->gcr_rPins[i].gcr_x;
	dst->gcr_rPins[i].gcr_x = dst->gcr_rPins[i].gcr_y;
	dst->gcr_rPins[i].gcr_y = tmp;
	dst->gcr_lPins[i] = src->gcr_bPins[i];
	tmp = dst->gcr_lPins[i].gcr_x;
	dst->gcr_lPins[i].gcr_x = dst->gcr_lPins[i].gcr_y;
	dst->gcr_lPins[i].gcr_y = tmp;
    }

    /*
     * Now flip the result array.  EXTRA SPECIAL TRICKINESS: the
     * GCRBLKM and GCRBLKP flags must get switched, because what
     * blocked a column in the old channel blocks a row in the
     * new one.
     */
    for (i = 0; i <= lenWds; i++)
	for (j = 0; j <= widWds; j++)
	{
	    old = src->gcr_result[i][j];
	    new = old & ~(GCRVR|GCRVL|GCRVU|GCRVD|GCRR|GCRU|GCRBLKM|GCRBLKP);
	    if (old & GCRVR) new |= GCRVU;
	    if (old & GCRVU) new |= GCRVR;
	    if (old & GCRVL) new |= GCRVD;
	    if (old & GCRVD) new |= GCRVL;
	    if (old & GCRR)  new |= GCRU;
	    if (old & GCRU)  new |= GCRR;
	    if (old & GCRBLKM) new |= GCRBLKP;
	    if (old & GCRBLKP) new |= GCRBLKM;
	    dst->gcr_result[j][i] = new;
	}

	/* Copy the horizontal and vertical density information */
    dst->gcr_dMaxByRow = src->gcr_dMaxByCol;
    dst->gcr_dMaxByCol = src->gcr_dMaxByRow;
    bcopy((char *) src->gcr_dRowsByCol, (char *) dst->gcr_dColsByRow,
		sizeof (short) * lenWds);
    bcopy((char *) src->gcr_dColsByRow, (char *) dst->gcr_dRowsByCol,
		sizeof (short) * widWds);
#ifdef	IDENSITY
    bcopy((char *) src->gcr_iRowsByCol, (char *) dst->gcr_iColsByRow,
		sizeof (short) * lenWds);
    bcopy((char *) src->gcr_iColsByRow, (char *) dst->gcr_iRowsByCol,
		sizeof (short) * widWds);
#endif	/* IDENSITY */
    
	/* Lastly, make a new transform */
    GeoTransTrans(&flipxy, &src->gcr_transform, &dst->gcr_transform);
    dst->gcr_origin = src->gcr_origin;
    dst->gcr_area = src->gcr_area;
    switch (src->gcr_type)
    {
	case CHAN_HRIVER:	dst->gcr_type = CHAN_VRIVER; break;
	case CHAN_VRIVER:	dst->gcr_type = CHAN_HRIVER; break;
	default:		dst->gcr_type = CHAN_NORMAL; break;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * GCRNoFlip --
 *
 * 	This procedure performs the identity transform.  It makes a copy.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The contents of channel dst are modified so that they are
 *	a copy of src.  Only the four pin arrays, the transform, the
 *	the area, and the result array are modified. The other fields
 *	of dst are untouched.  This means,
 *	in particular, that this procedure should be called BEFORE
 *	the linked lists for nets get set up, or else AFTER all the
 *	channel routing has been done and the nets have been freed up.
 *
 * ----------------------------------------------------------------------------
 */

void
GCRNoFlip(src, dst)
    GCRChannel *src;	/* Original channel. */
    GCRChannel *dst;	/* Channel to be modified to contain
				 * transformed info from src.  The two
				 * channels must have the same dimensions,
				 * but must not be the same channel.
				 */
{
    int lenWds, widWds, pinBytes, resBytes;
    int i;

    ASSERT(src->gcr_length == dst->gcr_length, "GCRFlipLeftRight: mismatch");
    ASSERT(src->gcr_width == dst->gcr_width, "GCRFlipLeftRight: mismatch");

    lenWds = src->gcr_length + 1;
    widWds = src->gcr_width + 1;

	/* Copy pairs of pins in the top and bottom arrays */
    pinBytes = lenWds * sizeof (GCRPin);
    bcopy((char *) src->gcr_tPins, (char *) dst->gcr_tPins, pinBytes);
    bcopy((char *) src->gcr_bPins, (char *) dst->gcr_bPins, pinBytes);

	/* Copy flag values from the result array */
    resBytes = widWds * sizeof (short);
    for (i = 0; i <= lenWds; i++)
	bcopy((char *)src->gcr_result[i], (char *)dst->gcr_result[i], resBytes);

	/* Copy the left and right end pins */
    pinBytes = widWds * sizeof (GCRPin);
    bcopy((char *) src->gcr_lPins, (char *) dst->gcr_lPins, pinBytes);
    bcopy((char *) src->gcr_rPins, (char *) dst->gcr_rPins, pinBytes);

	/* Copy the horizontal and vertical density information */
    dst->gcr_dMaxByCol = src->gcr_dMaxByCol;
    dst->gcr_dMaxByRow = src->gcr_dMaxByRow;
    bcopy((char *) src->gcr_dRowsByCol, (char *) dst->gcr_dRowsByCol,
		sizeof (short) * lenWds);
    bcopy((char *) src->gcr_dColsByRow, (char *) dst->gcr_dColsByRow,
		sizeof (short) * widWds);
#ifdef	IDENSITY
    bcopy((char *) src->gcr_iRowsByCol, (char *) dst->gcr_iRowsByCol,
		sizeof (short) * lenWds);
    bcopy((char *) src->gcr_iColsByRow, (char *) dst->gcr_iColsByRow,
		sizeof (short) * widWds);
#endif	/* IDENSITY */

	/* Now fix up the transform of the new channel */
    dst->gcr_origin = src->gcr_origin;
    dst->gcr_transform = src->gcr_transform;
    dst->gcr_area = src->gcr_area;
    dst->gcr_type = src->gcr_type;
}
