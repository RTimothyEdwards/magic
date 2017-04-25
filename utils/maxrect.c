
#ifndef	lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/maxrect.c,v 1.4 2010/09/24 19:53:20 tim Exp $";
#endif	

#include <sys/types.h>
#include <stdio.h>
#include <string.h>	/* for memcpy() */

#include "utils/maxrect.h"
#include "utils/malloc.h"

/*
 *-------------------------------------------------------------------------
 *
 * genCanonicalMaxwidth - checks to see that at least one dimension of a
 *	rectangular region does not exceed some amount.  Searches on all
 *	tiles of types in "mask", unless mask is NULL, in which case it
 *	searches over all tiles whose client record matches that of the
 *	start tile.
 *
 * Side Effects:
 *	None.
 *
 *-------------------------------------------------------------------------
 */

MaxRectsData *
genCanonicalMaxwidth(bbox, starttile, plane, mask)
    Rect	*bbox;		/* Bounding box of geometry to search */
    Tile	*starttile;	/* Any point in the geometry to search */
    Plane	*plane;		/* Plane being searched */
    TileTypeBitMask *mask;	/* Mask of types to check */
{
    int		    s;
    Tile	    *tile, *tp;
    TileTypeBitMask wrongtypes;
    static MaxRectsData *mrd = (MaxRectsData *)NULL;
    Rect	    boundorig;

    /* Generate an initial array size of 8 for rlist and swap. */
    if (mrd == (MaxRectsData *)NULL)
    {
	mrd = (MaxRectsData *)mallocMagic(sizeof(MaxRectsData));
	mrd->rlist = (Rect *)mallocMagic(8 * sizeof(Rect));
	mrd->swap = (Rect *)mallocMagic(8 * sizeof(Rect));
	mrd->listdepth = 8;
    }
    if (mask == NULL)
	mrd->match = starttile->ti_client;
    else
	mrd->match = CLIENTDEFAULT;

    mrd->rlist[0] = *bbox;
    boundorig = *bbox;

    /* Do an area search on boundrect to find all materials not	*/
    /* in oktypes.  Each such tile clips or subdivides		*/
    /* boundrect.  Due to the way FindMaxRects is written for	*/
    /* the DRC maxwidth rule, maxdist = 1 is required to avoid	*/
    /* creating degenerate (zero size) areas.			*/

    mrd->entries = 1;
    mrd->maxdist = 1;
    if (mask != NULL)
	TTMaskCom2(&wrongtypes, mask);
    else
	TTMaskSetMask(&wrongtypes, &DBAllTypeBits);

    DBSrPaintArea(starttile, plane, &boundorig, &wrongtypes, FindMaxRects, mrd);
    if (mrd->entries == 0)
	return NULL;
    else
	return (MaxRectsData *)mrd;
}

/*
 *-------------------------------------------------------------------------
 *
 * FindMaxRects ---
 *
 *	Procedure used to split an area up into multiple sets of possible
 *	convex rectangles.
 *
 * Results:
 *	Return 0 to keep the search going.
 *
 * Side effects:
 *	Adds data to structure mrd;  possibly expanding its memory allocation.
 *
 * Notes:
 *	This routine does *not* handle non-Manhattan geometry.
 *
 *-------------------------------------------------------------------------
 */

int
FindMaxRects(tile, mrd)
    Tile *tile;
    MaxRectsData *mrd;
{
    Rect area;
    Rect *rlist, *sr, *tmp;
    int s, entries;

    if (mrd->match != CLIENTDEFAULT)
	if (tile->ti_client == mrd->match)
	    return 0;

    entries = 0;
    TiToRect(tile, &area);

    rlist = mrd->swap;
    for (s = 0; s < mrd->entries; s++)
    {
	/* Watch out for (M)INFINITY values! */

	sr = &(mrd->rlist[s]);
	if (GEO_OVERLAP(sr, &area))
	{
	    /* Top */
	    if (area.r_ytop < INFINITY - 2)
		if (sr->r_ytop >= area.r_ytop + mrd->maxdist)
		{
		    rlist[entries] = *sr;
		    rlist[entries].r_ybot = area.r_ytop;
		    entries++;
		}
	    /* Bottom */
	    if (area.r_ybot > MINFINITY + 2)
		if (sr->r_ybot <= area.r_ybot - mrd->maxdist)
		{
		    rlist[entries] = *sr;
		    rlist[entries].r_ytop = area.r_ybot;
		    entries++;
		}
	    /* Left */
	    if (area.r_xbot > MINFINITY + 2)
		if (sr->r_xbot <= area.r_xbot - mrd->maxdist)
		{
		    rlist[entries] = *sr;
		    rlist[entries].r_xtop = area.r_xbot;
		    entries++;
		}
	    /* Right */
	    if (area.r_xtop < INFINITY - 2)
		if (sr->r_xtop >= area.r_xtop + mrd->maxdist)
		{
		    rlist[entries] = *sr;
		    rlist[entries].r_xbot = area.r_xtop;
		    entries++;
		}

	}
	else
	{
	    /* Copy sr to the new list */
	    rlist[entries] = *sr;
	    entries++;
	}

	/* If we have more rectangles than we allocated space for,	*/
	/* double the list size and continue.				*/

	if (entries > (mrd->listdepth - 4))  // was entries == mrd->listdepth
	{
	    Rect *newrlist;		

	    mrd->listdepth <<= 1;	/* double the list size */

	    newrlist = (Rect *)mallocMagic(mrd->listdepth * sizeof(Rect));
	    memcpy((void *)newrlist, (void *)mrd->rlist,
				(size_t)mrd->entries * sizeof(Rect));
	    // for (s = 0; s < entries; s++)
	    //	   newrlist[s] = mrd->rlist[s];
	    freeMagic(mrd->rlist);
	    mrd->rlist = newrlist;

	    newrlist = (Rect *)mallocMagic(mrd->listdepth * sizeof(Rect));
	    memcpy((void *)newrlist, (void *)mrd->swap,
				(size_t)entries * sizeof(Rect));
	    // for (s = 0; s < entries; s++)
	    //     newrlist[s] = mrd->swap[s];
	    freeMagic(mrd->swap);
	    mrd->swap = newrlist;

	    rlist = mrd->swap;
	}
    }

    /* Sort areas and remove areas enclosed by other areas	*/
    /* Is this routine too time-consuming (short answer: yes)?	*/

/*
    for (s = 0; s < entries - 1; s++)
    {
 	int r, t;
        for (t = s + 1, r = 0; t < entries; t++)
	{
	    if (GEO_SURROUND(&rlist[t], &rlist[s]))
	    {
		rlist[s] = rlist[t];
		r++;
	    }
	    else if (GEO_SURROUND(&rlist[s], &rlist[t]))
		r++;
	    else if (r > 0)
		rlist[t - r] = rlist[t];
	}
	entries -= r;
    }
*/

    /* copy rlist back to mrd by swapping "rlist" and "swap" */
    mrd->entries = entries;
    tmp = mrd->rlist;
    mrd->rlist = rlist;
    mrd->swap = tmp;

    if (entries > 0)
	return 0; 	/* keep the search going */
    else
	return 1;	/* no more max size areas; stop the search */
}

/*
 *-------------------------------------------------------------------------
 *
 * FindMaxRectangle ---
 *
 *	This is a general-purpose routine to be called from anywhere
 *	that expands an area to the largest area rectangle containing
 *	tiles connected to the given types list.  It can be called, for
 *	example, from the router code to expand a point label into the
 *	maximum size rectangular terminal.
 *
 * Results:
 *	Pointer to a rectangle containing the maximum size area found.
 *	This pointer should not be deallocated!
 *	
 *-------------------------------------------------------------------------
 */

Rect *
FindMaxRectangle(bbox, startpoint, plane, expandtypes)
    Rect *bbox;				/* bounding box of area searched */
    Point *startpoint;
    Plane *plane;			/* plane of types to expand */
    TileTypeBitMask *expandtypes;	/* types to expand in */
{
    MaxRectsData *mrd;
    Tile *starttile;
    TileType tt;
    int rectArea;
    int maxarea = 0;
    int s, sidx = -1;

    /* Find tile in def that surrounds or touches startpoint */
    starttile = plane->pl_hint;
    GOTOPOINT(starttile, startpoint);
    mrd = genCanonicalMaxwidth(bbox, starttile, plane, NULL);

    /* Return only the largest rectangle found */

    for (s = 0; s < mrd->entries; s++)
    {
	rectArea = (mrd->rlist[s].r_xtop - mrd->rlist[s].r_xbot) *
			(mrd->rlist[s].r_ytop - mrd->rlist[s].r_ybot);
	if (rectArea > maxarea)
	{
	    maxarea = rectArea;
	    sidx = s;
	}
    }

    if (sidx < 0)
    {
	Rect origrect;

	TiToRect(starttile, &origrect);
	sidx = 0;
	mrd->rlist[0] = origrect;
    }
    return &(mrd->rlist[sidx]);
}

/*
 *-------------------------------------------------------------------------
 *
 * FindMaxRectangle2 ---
 *
 *	This routine differs from FindMaxRectangle in passing a starting
 *	tile to the routine
 *
 * Results:
 *	Pointer to a rectangle containing the maximum size area found.
 *	This pointer should not be deallocated!
 *	
 *-------------------------------------------------------------------------
 */

Rect *
FindMaxRectangle2(bbox, starttile, plane)
    Rect *bbox;				/* bounding box of area searched */
    Tile *starttile;			/* use this tile to start */
    Plane *plane;			/* plane of types to expand */
{
    MaxRectsData *mrd;
    TileType tt;
    int rectArea;
    int maxarea = 0;
    int s, sidx = -1;

    /* Find tile in def that surrounds or touches startpoint */
    mrd = genCanonicalMaxwidth(bbox, starttile, plane, NULL);

    /* Return only the largest rectangle found */

    for (s = 0; s < mrd->entries; s++)
    {
	rectArea = (mrd->rlist[s].r_xtop - mrd->rlist[s].r_xbot) *
			(mrd->rlist[s].r_ytop - mrd->rlist[s].r_ybot);
	if (rectArea > maxarea)
	{
	    maxarea = rectArea;
	    sidx = s;
	}
    }

    if (sidx < 0)
    {
	Rect origrect;

	TiToRect(starttile, &origrect);
	sidx = 0;
	mrd->rlist[0] = origrect;
    }
    return &(mrd->rlist[sidx]);
}

