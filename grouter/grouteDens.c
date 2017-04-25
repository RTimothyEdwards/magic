/*
 * grouteDens.c
 *
 * Procedures for manipulating DensMap structures.
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

#ifndef	lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/grouter/grouteDens.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif	/* lint */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/hash.h"
#include "utils/heap.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "utils/netlist.h"
#include "gcr/gcr.h"
#include "router/router.h"
#include "grouter/grouter.h"
#include "utils/signals.h"
#include "textio/textio.h"
#include "utils/malloc.h"
#include "utils/styles.h"
#include "debug/debug.h"

/*
 * ----------------------------------------------------------------------------
 *
 * glDensAdjust --
 *
 * Increment the density in the channel as a result of routing the segment
 * from p1 to p2 through the channel common to both.  The segment is
 * identified by the NetId 'netid'.  The density map 'dens' is incremented
 * for each column/row of the channel through which the segment must run,
 * where the segment doesn't already exist.
 *
 * Results:
 *	Returns TRUE if any more of srcPin->gcr_ch became filled
 *	up to maximum density.
 *
 * Side effects:
 *	Modifies the local and possibly the maximum densities in *dens.
 *
 * ----------------------------------------------------------------------------
 */

bool
glDensAdjust(dens, srcPin, dstPin, netid)
    DensMap dens[2];
    GCRPin *srcPin, *dstPin;
    NetId netid;
{
    int minprow, maxprow, minpcol, maxpcol, mincol, maxcol, minrow, maxrow;
    int maxvd, maxhd, col, row, nrow, ncol;
    GCRChannel *ch = srcPin->gcr_ch;
    GCRPin *p1, *p2;
    short *dvec;
    bool densChanged;

    /* Sanity checking */
    ASSERT(srcPin && dstPin, "glDensAdjust");
    ASSERT(srcPin->gcr_ch == dstPin->gcr_ch, "glDensAdjust");

    if (DebugIsSet(glDebugID, glDebGreedy))
	return FALSE;

    /*
     * Find the first and last column where this net (netId)
     * is previously present in the channel, and also the first
     * and last row.
     */
    nrow = dens[CZ_ROW].dm_size - 1;
    ncol = dens[CZ_COL].dm_size - 1;
    maxprow = 0, minprow = dens[CZ_ROW].dm_size;
    maxpcol = 0, minpcol = dens[CZ_COL].dm_size;

	/* Rows */
    p1 = &ch->gcr_lPins[1], p2 = &ch->gcr_rPins[1];
    for (row = 1; row < dens[CZ_ROW].dm_size; row++, p1++, p2++)
    {
	if (SAMENET(p1, netid.netid_net, netid.netid_seg))
	{
	    minpcol = 1;
	    minprow = MIN(row, minprow);
	    maxprow = MAX(row, maxprow);
	}
	if (SAMENET(p2, netid.netid_net, netid.netid_seg))
	{
	    maxpcol = ncol;
	    minprow = MIN(row, minprow);
	    maxprow = MAX(row, maxprow);
	}
    }

	/* Columns */
    p1 = &ch->gcr_bPins[1], p2 = &ch->gcr_tPins[1];
    for (col = 1; col < dens[CZ_COL].dm_size; col++, p1++, p2++)
    {
	if (SAMENET(p1, netid.netid_net, netid.netid_seg))
	{
	    minprow = 1;
	    minpcol = MIN(col, minpcol);
	    maxpcol = MAX(col, maxpcol);
	}
	if (SAMENET(p2, netid.netid_net, netid.netid_seg))
	{
	    maxprow = nrow;
	    minpcol = MIN(col, minpcol);
	    maxpcol = MAX(col, maxpcol);
	}
    }

    /*
     * Increment the density over any range where
     * this net was not already present but is now.
     */
    p1 = srcPin;
    p2 = dstPin;

#define	CLIPTORANGE(x, min, max) \
    ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))

    densChanged = FALSE;
    minrow = MIN(p1->gcr_y, p2->gcr_y);
    minrow = CLIPTORANGE(minrow, 1, nrow);
    maxrow = MAX(p1->gcr_y, p2->gcr_y);
    maxrow = CLIPTORANGE(maxrow, 1, nrow);
    maxvd = dens[CZ_ROW].dm_max;
    dvec = dens[CZ_ROW].dm_value;
    for (row = minrow; row <= maxrow; row++)
	if (row < minprow || row > maxprow)
	    if (++dvec[row] >= maxvd)
	    {
		densChanged = TRUE;
		maxvd = dvec[row];
	    }
    dens[CZ_ROW].dm_max = maxvd;

    mincol = MIN(p1->gcr_x, p2->gcr_x);
    mincol = CLIPTORANGE(mincol, 1, ncol);
    maxcol = MAX(p1->gcr_x, p2->gcr_x);
    maxcol = CLIPTORANGE(maxcol, 1, ncol);
    maxhd = dens[CZ_COL].dm_max;
    dvec = dens[CZ_COL].dm_value;
    for (col = mincol; col <= maxcol; col++)
	if (col < minpcol || col > maxpcol)
	    if (++dvec[col] >= maxhd)
	    {
		maxhd = dvec[col];
		densChanged = TRUE;
	    }
    dens[CZ_COL].dm_max = maxhd;
    return densChanged;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glDMAlloc --
 *
 * Allocate and zero the dm_value array for the DensMap structure 'dm'.
 * This array will have size 'top+1'.  The maximum capacity is 'cap'.
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
glDMAlloc(dm, top, cap)
    DensMap *dm;
    int top, cap;
{
    dm->dm_max = 0;
    dm->dm_size = top + 1;
    dm->dm_cap = cap;
    dm->dm_value = (short *) callocMagic((unsigned) (sizeof (short) * dm->dm_size));
}

/*
 * ----------------------------------------------------------------------------
 *
 * glDMCopy --
 *
 * Copy the DensMap structure *dm1 to *dm2, copying the dm_value array
 * as well.  (The two DensMap structures better have the same size;
 * also, dm2->dm_value must already be allocated).
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
glDMCopy(dm1, dm2)
    DensMap *dm1, *dm2;
{
    dm2->dm_max = dm1->dm_max;
    ASSERT(dm2->dm_size == dm1->dm_size, "glDMCopy");
    ASSERT(dm2->dm_cap == dm1->dm_cap, "glDMCopy");
    bcopy((char *) dm1->dm_value, (char *) dm2->dm_value,
	    sizeof (short) * dm1->dm_size);
}

/*
 * ----------------------------------------------------------------------------
 *
 * glDMFree --
 *
 * Free the memory allocated to dm->dm_value.
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
glDMFree(dm)
    DensMap *dm;
{
    freeMagic((char *) dm->dm_value);
}

/*
 * ----------------------------------------------------------------------------
 *
 * glDMMaxInRange --
 *
 * Return the maximum value of the DensMap in the inclusive range lo .. hi.
 *
 * Results:
 *	See above.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
glDMMaxInRange(dm, lo, hi)
    DensMap *dm;
    int lo, hi;
{
    short *dval = dm->dm_value;
    int n, max;

    /* Sanity checks */
    ASSERT(lo > 0, "glDMMaxInRange");
    ASSERT(hi < dm->dm_size, "glDMMaxInRange");

    max = 0;
    for (n = lo; n <= hi; n++)
	if (dval[n] > max)
	    max = dval[n];

    return (max);
}

/*
 * ----------------------------------------------------------------------------
 *
 * glDensInit --
 *
 * Initialize the DensMap pair dmap[] from the already-initialized
 * density map in the GCRChannel *ch.  (The use of two separate kinds
 * of density maps is a temporary measure that will go away when the
 * integration of the new routing code is complete).
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
glDensInit(dmap, ch)
    DensMap dmap[2];
    GCRChannel *ch;
{
    short *dSrc, *dDst, *dEnd;

    dmap[CZ_COL].dm_max = ch->gcr_dMaxByCol;
    dmap[CZ_ROW].dm_max = ch->gcr_dMaxByRow;
    dSrc = ch->gcr_dRowsByCol;
    dDst = dmap[CZ_COL].dm_value;
    dEnd = &dmap[CZ_COL].dm_value[dmap[CZ_COL].dm_size];
    while (dDst < dEnd)
	*dDst++ = *dSrc++;

    dSrc = ch->gcr_dColsByRow;
    dDst = dmap[CZ_ROW].dm_value;
    dEnd = &dmap[CZ_ROW].dm_value[dmap[CZ_ROW].dm_size];
    while (dDst < dEnd)
	*dDst++ = *dSrc++;
}
