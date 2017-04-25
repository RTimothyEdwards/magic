/* gcrColl.c -
 *
 *	The greedy router:  Collapsing split nets.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/gcr/gcrColl.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "gcr/gcr.h"
#include "utils/malloc.h"

/* Variables to save the best pattern of collapsing jogs.  They get their
 * initialized values from an initial call to gcrEvalPat.
 *
 *	gcrBestCol:	The best column of collapsing jogs seen so far.
 *	gcrBestFreed:  	The number of tracks freed by the pattern in gcrBestCol
 */
GCRColEl *	gcrBestCol=(GCRColEl *) NULL;
int		gcrBestFreed;
int		gcrSplitNets;
int      *	gcrNthSplit=(int *) NULL;

/* Forward declarations */
void gcrEvalPat();


/*
 * ----------------------------------------------------------------------------
 *	gcrCollapse  --
 *
 * 	Add vertical segments to this column to collapse split nets in a
 *	pattern that will create the most empty tracks.  Generate all legal
 *	combinations and pick the best.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If the evaluated column is the best seen, then gcrEvalPat saves it
 *	away.  In any event, gcrEvalPat sets its col argument to NULL.
 * ----------------------------------------------------------------------------
 */

void
gcrCollapse(col, width, bot, top, freed)
    GCRColEl **	col;
    int    	width;
    int 	bot;
    int   	top;
    int	        freed;
{
    int         i, to;
    GCRNet   *	net;
    GCRColEl *  newCol, *gcrCopyCol();

    ASSERT(freed<=width, "gcrCollapse.");
    for(i=bot; i<=top; i++)
    {
    /* If net at i is split with a higher track, and there is no
     * interfering vertical wiring placed in a previous step...
     */
	to= (*col)[i].gcr_hi;
	if((to==EMPTY) || (*col)[i].gcr_hOk)
	    continue;

    /* ...except that the collapse should be skipped if both tracks involved
     * are where they are supposed to be to make connections at the end of the
     * channel.
     */
	/* comment out for a test
	if(((*col)[i].gcr_h==(*col)[i].gcr_wanted) &&
	   ((*col)[to].gcr_h==(*col)[to].gcr_wanted))
	    continue;
	*/

	if( gcrVertClear( *col, i, to) )
	{
	/* There is one freed track for each collapsing jog, plus
	 * one additional track for every net finished.
	 */
	    net=(*col)[i].gcr_h;
	    newCol=gcrCopyCol( *col, width);
	    if( ((*col)[to].gcr_wanted!=net) &&
		((*col)[ i].gcr_wanted==net) )
	        gcrMoveTrack(newCol, net, to, i);
	    else
	        gcrMoveTrack(newCol, net, i, to);
	    if(newCol[to].gcr_h!=(GCRNet *) NULL)
	    {
		if((newCol[to].gcr_hi== EMPTY) && (newCol[to].gcr_lo== EMPTY) &&
			(GCRPin1st(newCol[to].gcr_h)==(GCRPin *) NULL))
		    gcrCollapse(&newCol, width, to, top, freed+2);
		else
		    gcrCollapse(&newCol, width, to, top, freed+1);
	    }
	    if(top>to)
		top=to-1;
	}
    }
    gcrEvalPat( col, freed, width);
    *col=(GCRColEl *) NULL;
}

/*
 * ----------------------------------------------------------------------------
 *
 * gcrInitCollapse --
 *
 * 	This procedure sets up the data structure used in determining
 *	optimal split-net collapses.  It must be called before the
 *	first call to gcrEvalPat to make sure that there's a large
 *	enough internal structure.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The gcrNthSplit array is allocated.
 *
 * ----------------------------------------------------------------------------
 */

void
gcrInitCollapse(size)
    int size;		/* Number of tracks in column. */
{
    gcrBestFreed = 0;
    gcrSplitNets = -1;
    if(gcrNthSplit)
	freeMagic((char *) gcrNthSplit);

    gcrNthSplit =(int *) mallocMagic((unsigned) size*sizeof(int));
    ASSERT(gcrNthSplit, "gcrEvalPat: mallocMagic failed");
    if (gcrBestCol)
    {
	freeMagic((char *) gcrBestCol);
	gcrBestCol = (GCRColEl *) NULL;
    }
}

/*
 * ----------------------------------------------------------------------------
 *	gcrEvalPat --
 *
 * 	Evaluate a pattern of jogs to determine whether it is better than
 *	a previous pattern.  Evaluation criteria in order of importance:
 *		1.  Highest number of tracks freed.
 *		2.  Outermost uncollapsed split nets furthest from channel edge.
 *		3.  Largest sum of jog lengths.
 * Results:
 *	None.
 *
 * Side effects:
 *	If col better than previously best, then free previous best, save col
 *	in gcrBestCol, save tracks freed in gcrBestFreed, and set *col to NULL.
 *
 * ----------------------------------------------------------------------------
 */

void
gcrEvalPat(col, freed, size)
    GCRColEl **	col;		/* Describes column in which to collapse. */
    int		freed;		/* We've already found a set of collapsing
				 * jogs that frees at least this many tracks.
				 */
    int		size;		/* Number of tracks in column. */
{
    int		i, n, newDist, oldWiring, newWiring;

    if(gcrBestCol==(GCRColEl *) NULL)
    {
	gcrBestCol= *col;
	gcrBestFreed=freed;
	return;
    }

    if(freed>gcrBestFreed) goto newBetter;
    if(freed<gcrBestFreed) goto oldBetter;

/* If tied, save col with outermost uncollapsed net as far as possible from
 * channel edge;  if necessary consider the second outermost such net, etc.
 */
    n=0;
    for(i=0; i<size/2; i++)
    {
	if(n>gcrSplitNets)	/* Don't recompute previously found values */
	    gcrNthSplit[++gcrSplitNets]=gcrNextSplit(gcrBestCol, size, i);

	if (gcrNthSplit[n]>size)	/* No more uncollapsed split nets */
	    goto oldBetter;

	newDist=gcrNextSplit(*col, size, i);
	if(newDist<gcrNthSplit[n]) goto oldBetter;

	if(newDist==gcrNthSplit[n])	/*There is a tie*/
	    n++;
	else
	{
	    gcrNthSplit[n]=newDist;	/*Save new best split information*/
	    gcrSplitNets=n;
	    goto newBetter;
	}
    }

/* If still tied, choose the pattern with the largest sum of jog lengths. */
    oldWiring=newWiring=0;
    for(i=1; i<size; i++)
    {
	if(gcrBestCol[i].gcr_v!=(GCRNet *) NULL)
	    oldWiring++;
	if((*col)[i].gcr_v!=(GCRNet *) NULL)
	    newWiring++;
    }
    if(oldWiring>newWiring) goto oldBetter;
    else goto newBetter;

    /* We get here when we find out that the new pattern is better than
     * the old pattern.
     */

    newBetter:
    if (gcrBestCol != NULL) freeMagic((char *) gcrBestCol);
    gcrBestCol = *col;
    gcrBestFreed = freed;
    return;

    /* We get here when we find out that the old pattern is still best. */

    oldBetter:
    freeMagic((char *) *col);
    return;
}

/*
 * ----------------------------------------------------------------------------
 *	gcrNextSplit --
 *
 * 	Given a distance i from the channel edge, search inward from the top
 *	and bottom of the channel for the next uncollapsed split net.
 *	If there aren't any, return size+1.
 *
 * Results:
 *	Return the distance from the channel edge to the next split net.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

int
gcrNextSplit(col, size, i)
    GCRColEl *	col;
    int		size;
    int		i;
{
    for(i++; i<size/2; i++)
	if( ((col[i].gcr_hi!= EMPTY)&&(col[i].gcr_lo== EMPTY)) ||
	    ((col[size-i+1].gcr_lo!= EMPTY)&&(col[size-i+1].gcr_hi== EMPTY)) )
	    return(i);
    return(size+1);
}

/*
 * ----------------------------------------------------------------------------
 *	gcrPickBest --
 *
 *	Replace the active column of the given channel with the best
 *	collapsed column in gcrBestCol.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memeory may be freed.  gcrBestCol gets set to NULL.
 * ----------------------------------------------------------------------------
 */

void
gcrPickBest(ch)
    GCRChannel * ch;
{
    ASSERT(gcrBestCol!=(GCRColEl *) NULL, "gcrPickBest");
    ASSERT(ch->gcr_lCol==(GCRColEl *) NULL, "gcrPickBest");
    ch->gcr_lCol=gcrBestCol;
    gcrBestCol=(GCRColEl *) NULL;
}

/*
 * ----------------------------------------------------------------------------
 *	gcrReduceRange --
 *
 *	Scan from the outside in, trying to move the outermost top track
 *	of each uncollapsed split net downward, and trying to move the
 *	outermost bottom track of each uncollapsed split net upwards.
 *	Make no jogs which are shorter than minimum jog length.
 * 
 * Results:
 *	None.
 *
 * Side effects:
 *	Adjust the list of occupied tracks for the net.
 * ----------------------------------------------------------------------------
 */

void
gcrReduceRange(col, width)
    GCRColEl *	col;
    int		width;
{
    int		i, j;
    int		farthest;
    GCRNet *	net;
    bool	clear;

    for(i=1; i<width; i++)
    {
    /* Find the lowest track assigned to an uncollapsed split net */
	if((col[i].gcr_hi!= EMPTY)&&(col[i].gcr_lo== EMPTY) &&
		(col[i].gcr_h!=col[i].gcr_wanted) &&
		!col[i].gcr_hOk)
	{
	    clear=!(col[i].gcr_flags & (GCRBLKM | GCRBLKP));
	    farthest=i;
	    net=col[farthest].gcr_h;
	    for(j=i+1; j<=width; j++)
	    {
		if((net==col[j].gcr_h) && col[j].gcr_hOk)
		    break;
		if(gcrBlocked(col, j, net, width))
		    break;

	    /* If the original track was clear, don't move into a blocked
	     * track.
	     */
		if(clear && (col[j].gcr_flags & (GCRBLKM | GCRBLKP)))
		    break;

		if( (col[j].gcr_h==(GCRNet *) NULL) &&
		    !(col[j].gcr_flags & GCRCC) )
		    farthest=j;
	    }
	    if((farthest-i)>=GCRMinJog)
		gcrMoveTrack(col, net, i, farthest);
	}

    /* Find the highest track assigned to an uncollapsed split net */
	farthest=width+1-i;
	if((col[farthest].gcr_hi== EMPTY)&&(col[farthest].gcr_lo!= EMPTY)
		&&(col[farthest].gcr_h!=col[farthest].gcr_wanted)
		&& !col[farthest].gcr_lOk)
	{
	    clear=!(col[i].gcr_flags & (GCRBLKM | GCRBLKP));
	    net=col[farthest].gcr_h;
	    for(j=farthest-1; j>0; j--)
	    {
		if((net==col[j].gcr_h) && col[j].gcr_lOk)
		    break;
		if(gcrBlocked(col, j, net, 0))
		    break;

	    /* If the original track was clear, don't move into a blocked
	     * track.
	     */
		if(clear && (col[j].gcr_flags & (GCRBLKM | GCRBLKP)))
		    break;

		if( (col[j].gcr_h==(GCRNet *) NULL) &&
		    !(col[j].gcr_flags & GCRCC) )
		{
		    farthest=j;
		    if(col[j].gcr_lo== EMPTY)
			break;
		}
	    }
	    if((width+1-i-farthest)>=GCRMinJog)
		gcrMoveTrack(col, net, width+1-i, farthest);
	}
    }
}
