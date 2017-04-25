/* hist.c -
 *
 *	Module to collect and print histograms.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/debug/hist.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "database/database.h"
#include "tiles/tile.h"
#include "utils/utils.h"
#include "textio/textio.h"
#include "debug/debug.h"
#include "utils/malloc.h"

Histogram * hist_list = (Histogram *) NULL;

/*
 * ----------------------------------------------------------------------------
 *
 * histFind --
 *
 * 	Traverse the linked list of histograms to find the one with the
 *	given name.
 *
 * Results:
 *	NULL if not found;  otherwise a pointer to the histogram.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */
Histogram *
histFind(name, ptrKeys)
    char * name;
    bool ptrKeys;
{
    Histogram * h;
    for(h=hist_list; h!=(Histogram *) NULL; h=h->hi_next)
	if(ptrKeys && (strcmp(name, h->hi_title)==0))
	    return(h);
	else
	if(!ptrKeys && (name == h->hi_title))
	    return(h);
    return((Histogram *) NULL);
}

/*
 * ----------------------------------------------------------------------------
 *
 * HistCreate --
 *
 * 	Create a histogram.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates buckets for a histogram of the given size, plus coverage of
 *	lower and upper ranges.  Links the bucket onto a list.
 *
 * ----------------------------------------------------------------------------
 */
void
HistCreate(name, ptrKeys, low, step, bins)
    char * name;	/* Name for histogram add and print   */
    bool   ptrKeys;	/* TRUE if name is a character pointer*/
    int    low;		/* The lowest value for the histogram */
    int    step;	/* The increment for each bin         */
    int    bins;	/* The numbe of bins in the histogram */
{
    Histogram * new;
    int i;

    ASSERT(histFind(name, ptrKeys) == (Histogram *) NULL, "HistCreate");
    new = (Histogram *) mallocMagic ((unsigned) sizeof (Histogram));
    ASSERT(new!=(Histogram *) NULL, "HistCreate");
    ASSERT(step>0, "HistCreate");
    ASSERT(bins>0, "HistCreate");

    new->hi_ptrKeys=ptrKeys;
    new->hi_lo=low;
    new->hi_step=step;
    new->hi_bins=bins;
    new->hi_cum =0;
    new->hi_max=MINFINITY;
    new->hi_min=INFINITY;
    if(ptrKeys)
	new->hi_title=StrDup((char **) NULL, name);
    else
	new->hi_title=name;
    new->hi_data = (int *) mallocMagic((unsigned) (bins+2) * sizeof (int));
    for(i=0; i<bins+2; i++)
	new->hi_data[i]=0;

    ASSERT(new->hi_data!=(int *) NULL, "HistCreate");
    new->hi_next=hist_list;
    hist_list=new;
}

/*
 * ----------------------------------------------------------------------------
 *
 * HistAdd --
 *
 * 	Add an entry into the named histogram.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Searches the histogram list for the named histogram.  Adds one to the
 *	appropriate range in the histogram.
 *
 * ----------------------------------------------------------------------------
 */
void
HistAdd(name, ptrKeys, value)
    char * name;	/* Identifier for the histogram */
    bool   ptrKeys;	/* TRUE if the name is a pointer*/
    int    value;	/* Value to index the column	*/
{
    Histogram * h;

    if((h=histFind(name, ptrKeys))==(Histogram *) NULL)
	HistCreate(name, ptrKeys, 0, 20, 10);

    h->hi_cum+=value;
    if(value < h->hi_lo)
	h->hi_data[0]++;
    else
    if(value > h->hi_lo - 1 + h->hi_bins * h->hi_step)
	h->hi_data[h->hi_bins+1]++;
    else
	h->hi_data[(value - h->hi_lo + h->hi_step)/h->hi_step]++;
    if(value < h->hi_min)
	h->hi_min=value;
    if(value > h->hi_max)
	h->hi_max=value;
}

/*
 * ----------------------------------------------------------------------------
 *
 * HistPrint --
 *
 * 	Print all histograms to the named file.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Creates a file.
 *
 * ----------------------------------------------------------------------------
 */
void
HistPrint(name)
    char * name;
{
    FILE * fp, * fopen();
    Histogram * h;
    int i;
    float total, running;

    fp=fopen(name, "w");
    if(fp==(FILE *) NULL)
    {
	TxError("Can't open histogram file %s\n", name);
	return;
    }

    for(h=hist_list; h!=(Histogram *) NULL; h=h->hi_next)
    {
	if(h->hi_ptrKeys)
	    fprintf(fp, "Histogram %s", h->hi_title);
	else
	    fprintf(fp, "Histogram %"DLONG_PREFIX"d", (dlong) h->hi_title);
	fprintf(fp, "; Low=%d; Bins=%d\n", h->hi_lo, h->hi_bins);
	total=0.0;
	running=0.0;
	for(i=0; i<h->hi_bins+2; i++)
	    total+=h->hi_data[i];
	if(total==0.0)
	{
	    fprintf(fp, "   No items.\n");
	    continue;
	}
	fprintf(fp, "   %10.0f total items, %d total values, %10.2f average.\n",
		total, h->hi_cum, h->hi_cum/total);
	for(i=0; i<h->hi_bins + 2; i++)
	{
	    if(running==total)
	    {
		fprintf(fp, "No more data.\n");
		break;
	    }
	    running+=h->hi_data[i];
	    if(i==0)
	    {
		fprintf(fp, "< %5d:  %10d (%5.2f%%)", h->hi_lo, h->hi_data[i],
			h->hi_data[i]/total);
		fprintf(fp, ";  smallest value was %d\n", h->hi_min);
	    }
	    else
	    if(i==(h->hi_bins+1))
	    {
		fprintf(fp, "> %5d:  %10d (%5.2f%%)\n",
			(h->hi_bins * h->hi_step) + h->hi_lo - 1,
			h->hi_data[i], h->hi_data[i]/total);
	    }
	    else
	    fprintf(fp, "  %3d..%3d:  %10d (%5.2f%%) (%5.2f%%)\n",
		    h->hi_lo  + (i-1) * h->hi_step,
		    h->hi_lo  + i * h->hi_step - 1,
		    h->hi_data[i], h->hi_data[i]/total,
		    running/total);
	}
	fprintf(fp, "; largest value was %d\n", h->hi_max);
	fprintf(fp, "\n\n\n");
    }
    fclose(fp);
}
