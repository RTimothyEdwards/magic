/* gcrUnsplit.c -
 *
 *	The greed router:  Procedures for jogging unsplit nets.
 *		* Raising rising nets and lowering falling nets.
 *		* Vacating tracks needed at the end of the channel.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/gcr/gcrUnsplit.c,v 1.2 2009/12/30 13:42:34 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "gcr/gcr.h"
#include "utils/malloc.h"

/* Forward declarations */

void gcrMakeRuns();


/*
 * ----------------------------------------------------------------------------
 *	gcrVacate --
 *
 * 	Try to vacate tracks that are needed to make right-edge connections.
 *	Only consider moving nets that are unsplit.  Vacate the nets in
 *	asccending order of jog length.
 *
 *	If the jogged net is rising or falling, make the jog in the direction
 *	of motion, except that when near the end of the channel jog any way
 *	necessary to vacate a needed track.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes the wiring in the current column.
 * ----------------------------------------------------------------------------
 */

void
gcrVacate(ch, column)
    GCRChannel * ch;
    int		 column;
{
    int        i;
    int		        to, count, gcrIsGreater();
    GCRColEl *	col;
    GCRNet *		net, ** list;
    bool		nearEnd;

    list=(GCRNet **) mallocMagic((unsigned) (ch->gcr_width+1) * sizeof(GCRNet *));
    ASSERT(list!=(GCRNet **) NULL,"gcrVacate:  malloc failed.");
    col=ch->gcr_lCol;
    count=0;
    nearEnd=(ch->gcr_length-column<=GCREndDist);

    for(i=1; i<=ch->gcr_width; i++)
    {
	net=col[i].gcr_h;

    /* Don't need to vacate an empty track. */
	if(net==(GCRNet *) NULL)
	    continue;

    /* Skip nets if they are done:  unsplit with no more pins */
	if( (col[i].gcr_hi== EMPTY) && (col[i].gcr_lo== EMPTY) &&
	    (GCRPin1st(net)==(GCRPin *) NULL) )
	    continue;

    /* Select nets that must vacate their tracks. */
	if( ((col[i].gcr_wanted!=net) && (col[i].gcr_wanted!=(GCRNet *) NULL))||
	    ((col[i].gcr_flags & GCRVL) && !nearEnd) )
	{
	/* Skip split nets */
	    if((col[i].gcr_hi!= EMPTY) || (col[i].gcr_lo!= EMPTY))
		continue;

	/* If the track ends here, then take an empty track, even if it is
	 * covered;  otherwise only vacate to a track that is completely
	 * free of obstacles.
	 */
	    if( (col[i].gcr_flags & GCRTE) || (i==1) || (i==ch->gcr_width) )
		to=gcrLook(ch, i, TRUE);
	    else
		to=gcrLook(ch, i, FALSE);

	    if(to!= EMPTY)	/*found a vacating jog*/
	    {
	    /* Skip it if it is to a track needed for an end connection*/
		if(ch->gcr_rPins[to].gcr_pId!=(GCRNet *) 0)
		    continue;
		list[count++]=net;
		net->gcr_track=i;
		net->gcr_dist=to-i;
		net->gcr_sortKey=abs(to-i);
	    }
	}
    }
    if(count>0)
    {
	gcrShellSort(list, count, TRUE);
	gcrMakeRuns(ch, column, list, count, FALSE);
    }
}

/*
 * ----------------------------------------------------------------------------
 *	gcrLook --
 *
 * 	Look for an empty track.
 *
 * Results:
 *	Return the track index of the nearest empty track which is accessible
 *	and is not blocked or wanted by some other net.  If the currently
 *	occupying net is rising or falling, the new track should go that way
 *	if possible.
 *
 *	Want to choose the empty track to minimize the wire length from
 *	"track" to "empty" to "target".
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

int
gcrLook(ch, track, canCover)
    GCRChannel * ch;
    int		 track;
    bool	 canCover;
{
    int 	up, dn, dir, bestUp= EMPTY, bestDn= EMPTY, uplim, dnlim;
    int		target, upLength, dnLength;
    short	flag;
    GCRNet   * 	net;
    GCRColEl *	col;
    bool	uBlock, dBlock;

    col=ch->gcr_lCol;
    net=col[track].gcr_h;

    /* Conflicting vertical wiring blocks any run.
     */
    if((col[track].gcr_v!=(GCRNet *) NULL)&&(col[track].gcr_v!=net))
	return(EMPTY);

    /* Find the upper and lower limits on searching:  either the edge of
     * the channel or else the next track occupied by the net.
     */
    if(col[track].gcr_hi!= EMPTY)
	 uplim=col[track].gcr_hi;
    else uplim=ch->gcr_width;
    if(col[track].gcr_lo!= EMPTY)
	 dnlim=col[track].gcr_lo;
    else dnlim=1;

    /* Look both upwards and downwards for an available track.
     */
    dir=gcrClass(net, track);
    target=track+dir;
    uBlock=dBlock=FALSE;
    for(up=track+1, dn=track-1; ((up<=uplim) || (dn>=dnlim)); up++, dn--)
    {
	if((up<=uplim) && (!uBlock) && (bestUp== EMPTY))
	{
	    flag = col[up].gcr_flags;
	    if(BLOCK(flag))
		uBlock=TRUE;
	    else
	    if((col[up].gcr_v!=net) && (col[up].gcr_v!=(GCRNet *) NULL))
		uBlock=TRUE;
	    else
	    if((col[up].gcr_h != (GCRNet *) NULL) && (col[up].gcr_h != net) &&
	       (flag & (GCRBLKM | GCRBLKP)))
		uBlock=TRUE;
	    else
	    if(((col[up].gcr_wanted==(GCRNet *) NULL) ||
		 (col[up].gcr_wanted==net)) && !(flag & GCRVL) &&

	/* A track with a single layer of obstacle is disallowed unless
	 * canCover is set TRUE.
	 */
		 (((!(flag & GCRBLKM)) && (!(flag & GCRBLKP)) ) || canCover))
	    {
		if(dir>=0)
		    return(up);
		else
		{
		/* Figure out the wiring length to this track.  Set this
		 * as the length to beat in the other direction.
		 */
		    bestUp=up;
		    upLength=(bestUp-track)+bestUp-target;
		    if((track-upLength+1)>dnlim)
			dnlim=track-upLength+1;
		}
	    }
	}

	if((dn>=dnlim) && (!dBlock) && (bestDn== EMPTY))
	{
	    flag = col[dn].gcr_flags;
	    if(BLOCK(flag))
		dBlock=TRUE;
	    else
	    if((col[dn].gcr_v!=net) && (col[dn].gcr_v!=(GCRNet *) NULL))
		dBlock=TRUE;
	    else
	    if((col[dn].gcr_h != (GCRNet *) NULL) && (col[dn].gcr_h != net) &&
	       (flag & (GCRBLKM | GCRBLKP)))
		dBlock=TRUE;
	    else
	    if( (col[dn].gcr_h==(GCRNet *) NULL) &&
		((col[dn].gcr_wanted==(GCRNet *) NULL) ||
		 (col[dn].gcr_wanted==net)) && !(flag & GCRVL) &&

	/* A track with a single layer of obstacle is disallowed unless
	 * canCover is set TRUE.
	 */
		 ((!(flag & GCRBLKM) && !(flag & GCRBLKP) ) || canCover))
	    {
		if(dir<=0)
		    return(dn);
		else
		{
		    bestDn=dn;
		    dnLength=(track-bestDn)+(target-bestDn);
		    if((track+dnLength-1)<uplim)
			uplim=track+dnLength-1;
		}
	    }
	}
    }
    if(dir>0)
	return(bestDn);
    else
	return(bestUp);
}

/*
 * ----------------------------------------------------------------------------
 *	gcrClassify --
 *
 * 	Determine whether each net is rising or falling.
 *	GRclassify is called at startup, when the channel is split,
 *	and whenever the routing direction changes.
 *
 * Results:
 *	Return a pointer to an array of pointers to rising or falling nets,
 *	sorted in order of decreasing distance to target edge.
 *
 * Side effects:
 *	Sets the rising/falling status for all nets.
 * ----------------------------------------------------------------------------
 */

GCRNet **
gcrClassify(ch, count)
    GCRChannel * ch;
    int	       * count;
{
    GCRColEl *	col;
    GCRPin   	      *	pin, * next;
    int			i, dist;
    GCRNet  **	result;

    col=ch->gcr_lCol;
    result=(GCRNet **) mallocMagic((unsigned) (ch->gcr_width+1) * sizeof(GCRNet *));
    ASSERT(result!=(GCRNet **) NULL, "gcrClassify:  malloc failed.");
    *count=0;

/* Scan up the column, classifying nets.  Since each net need only be
 * classified once, skip over those nets with some lower track.
 */
    for(i=1; i<=ch->gcr_width; i++)
    {
    /* Process non-empty tracks with no lower track for the net */

	if( (col[i].gcr_h!=(GCRNet *) NULL) && (col[i].gcr_lo== EMPTY) )
	{
	    if(col[i].gcr_hi!= EMPTY)
		continue;

	    pin=GCRPin1st(col[i].gcr_h);
	    if(pin==NULL)
		continue;

	    dist=pin->gcr_y-i;
	    if(dist==0)
		continue;

	    for(next=GCRPinNext(pin); next!=(GCRPin *) NULL;
		    next=GCRPinNext(next))
	    {
		if(next->gcr_x>pin->gcr_x+GCRSteadyNet)
		    break;	/*Too far => doesn't count*/

		if( ((next->gcr_y-i)>0)!=(dist>0) )
		{
		    dist=0;		/*Pin in the opposite direction*/
		    break;
		}
	    }
	    if(dist!=0)
	    {
		col[i].gcr_h->gcr_dist=dist;
		col[i].gcr_h->gcr_sortKey=gcrRealDist(col, i, dist);
		col[i].gcr_h->gcr_track=i;
		result[(*count)++]=col[i].gcr_h;
	    }
	}
    }
    result[(*count)]=(GCRNet *) NULL;
    if(*count >0)
    gcrShellSort(result, *count, FALSE);
    return(result);
}

/*
 * ----------------------------------------------------------------------------
 *
 * gcrRealDist --
 *
 * 	Convert a distance from a track to a pin into a sort key for ranking
 *	rising and falling nets.  Ranking numbers for sorting are non-negative
 *	integers reflecting the distance a net must move to reach a given track.
 *	The ranking can be less than the actual distance if the net has vertical
 *	wiring occupying a range of tracks.
 *
 * Results:
 *	Distance value (integer).
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
gcrRealDist(col, i, dist)
    GCRColEl * col;
    int i, dist;
{
    int j, last;
    GCRNet * net=col[i].gcr_h;
    last=0;
    for(j=i; j!=(i+dist); j=j+ ((dist>0) ? 1 : -1))
    {
	if(col[j].gcr_v!=net)
	    break;
	if((col[j].gcr_h==net) || (col[j].gcr_h==(GCRNet *) NULL))
	    last=j-i;
    }
    return(abs(dist-last));
}

/*
 * ----------------------------------------------------------------------------
 *	gcrClass --
 *
 * 	Classify a single net as rising, falling, or steady.  Assume that it
 *	is not split.
 *
 * Results:
 *	The distance, or 0 if steady.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

int
gcrClass(net, track)
    GCRNet    * net;
    int		track;
{
    GCRPin    *	pin, * next;
    int	 	dist;

    pin=GCRPin1st(net);
    if(pin==NULL)
	return(0);

    dist=pin->gcr_y-track;
    if(dist==0)
	return(0);

    for(next=GCRPinNext(pin); next!=(GCRPin *) NULL;
	    next=GCRPinNext(next))
    {
	if(next->gcr_x>pin->gcr_x+GCRSteadyNet)
	    return(dist);	/*Too far => doesn't count*/

	if( ((next->gcr_y-track)>0)!=(dist>0) )
	    return(0);		/*Pin in the opposite direction*/
    }
    return(dist);
}

/*
 * ----------------------------------------------------------------------------
 *	gcrMakeRuns --
 *
 * 	Add jogs to try to move nets from track to track.
 *
 *	Make no jogs that are shorter than minimum jog length, except when
 *		-within GCREndDist of the end of the channel,
 *		-the jog brings an unsplit net to its destination track,
 *		-doing a vacating jog.
 *
 *	Don't jog a net to a free track if it is reserved by another net.
 *
 *	If riseFall (rising and falling jogs) make full or partial runs,
 *	but only if they do not cause transitions from blocked to clear
 *	or clear to blocked (blocked to blocked and clear to clear are okay).
 *	Furthermore, don't overshoot the target track unless it gets
 *	the track closer to its destination.
 *
 *	If not "riseFall" (vacating jogs) then only make the run if can
 *	only wire the entire jog.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes the wiring in the current column.
 * ----------------------------------------------------------------------------
 */

void
gcrMakeRuns(ch, column, list, count, riseFall)
    GCRChannel * ch;
    int		 column;
    GCRNet **	 list;
    int		 count;
    bool	 riseFall;
{
    int		 j, from, to, runTo;
    int		 distToTarget;
    GCRNet   *	net, * gcrEmptySpan(), *save;
    GCRColEl *	col;

    col=ch->gcr_lCol;

    for(j=0; j<count; j++)
    {
	net = list[j];
	from= net->gcr_track;
	to  = from+net->gcr_dist;
	distToTarget=abs(from-to);
	if(to<=0)
	    to=1;
	else
	if(to==ch->gcr_width+1)
	    to=ch->gcr_width;

	runTo=gcrTryRun(ch, net, from, to, column);

    /* Make no jogs shorter than minimum jog length, except when close
     * to the end of the channel, or if the jog would put the net into
     * its final track.
     */
	if(runTo== EMPTY)
	    continue;

	if((!riseFall)&&(runTo!=to))
	    continue;

	if( (!riseFall) ||
	    (abs(from-runTo)>=GCRMinJog) ||
	    (GCRNearEnd(ch, column) &&
		   (ch->gcr_rPins[runTo].gcr_pId==net)) )
	{
	    if(!riseFall)
	    {
		save=col[from].gcr_wanted;
		col[from].gcr_wanted=(GCRNet *) NULL;
		gcrMoveTrack(col, net, from, runTo);
		col[from].gcr_wanted=save;
	    }
	    else
	    /* Don't make a rising or falling jog unless it moves the
	     * net closer to its target track.
	     */
	    if(distToTarget>abs(runTo-to))
		gcrMoveTrack(col, net, from, runTo);
	}
	gcrCheckCol(ch, column, "gcrMakeRuns");
    }
    freeMagic((char *) list);
}

/*
 * ----------------------------------------------------------------------------
 *
 * gcrTryRun --
 *
 * 	Try to make a vertical run from one track to another
 *
 * Results:
 *	Return the index of the track as close as possible to the desired
 *	track.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
gcrTryRun(ch, net, from, to, column)
    GCRChannel	* ch;
    GCRNet	* net;
    int		  from, to, column;
{
    GCRColEl * col;
    GCRNet * vnet, * hnet;
    int firstFlag, flag, runTo, i;
    bool sourceEnds, covered, up;

    if(from==to) return(EMPTY);
    up		= (from<to);
    col		= ch->gcr_lCol;
    runTo	= EMPTY;
    firstFlag	= col[from].gcr_flags;
    covered	= (firstFlag & (GCRBLKM | GCRBLKP)) ? TRUE : FALSE;
    sourceEnds  = (firstFlag & GCRTE) ? TRUE : FALSE; 
    for(i=from; (up && (i<=to)) || (!up && (i>=to)); i=i + ((from>to) ? -1 : 1))
    {
	flag=col[i].gcr_flags;
	vnet=col[i].gcr_v;
	hnet=col[i].gcr_h;

    /* If the column ends here, might as well give up */
	if(flag & GCRCE) break;

    /* Give up if there is previously placed vertical wiring here */
	if( (vnet!=(GCRNet *) NULL) && (vnet!=net) )
	    break;

    /* Give up if blocked on both layers by obstacles		*/
	if(BLOCK(flag))
	    break;

    /* Check to see if blocked due to single layer only allowed here */
	if( (flag & (GCRBLKM | GCRBLKP | GCRCC)) &&
	    (hnet!=(GCRNet *) NULL) && (hnet!=net))
	    break;

    /* Don't make a run into a track that is blocked beyond this column */
	if(flag & GCRTE)
	    continue;

    /* Don't run to a track needed for a column contact, unless near the
     * end and an end connection is needed, or source ends and no other track
     * was found.
     */
	if( (flag & GCRCC) &&
	    (!(GCRNearEnd(ch, column) && (net==col[i].gcr_wanted)) ||
	      (sourceEnds && (runTo== EMPTY))  ))
	    continue;

    /* Don't use a VACATE track unless source track ends or is also VACATE */
	if((flag & GCRVL) && !(firstFlag & GCRVL)
	      && !(sourceEnds && (runTo== EMPTY))
	      && !((net==col[i].gcr_wanted) && GCRNearEnd(ch, column)) )
	    continue;

    /* If current track is occupied, then skip it */
	if( (hnet!=(GCRNet *) NULL) && (hnet!=net) )
	    continue;

    /* Take a location with conflicting end connection only if the source is
     * similar and no other place has yet been found.
     */
	if((col[i].gcr_wanted!=(GCRNet *) NULL) && (col[i].gcr_wanted!=net)
	    && ((runTo!= EMPTY) || ((col[from].gcr_wanted==net) ||
	    (col[from].gcr_wanted==(GCRNet *) NULL))))
	    continue;

    /* Don't move from an uncovered track to a covered track, unless
     * near the end and a connection is needed.
     */
	if( !covered && (flag & (GCRBLKM | GCRBLKP)) &&
	    !((net==col[i].gcr_wanted) && (GCRNearEnd(ch, column))) )
	    continue;

	if(i!=from)
	    runTo=i;
    }
    return(runTo);
}
