
#ifndef	lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/drc/DRCextend.c,v 1.6 2010/09/20 21:13:22 tim Exp $";
#endif	

#include <sys/types.h>
#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/malloc.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "dbwind/dbwtech.h"
#include "drc/drc.h"
#include "utils/signals.h"
#include "utils/stack.h"
#include "utils/maxrect.h"

Stack *DRCstack = (Stack *)NULL;

#define PUSHTILE(tp) \
    if ((tp)->ti_client == (ClientData) DRC_UNPROCESSED) { \
        (tp)->ti_client = (ClientData)  DRC_PENDING; \
        STACKPUSH((ClientData) (tp), DRCstack); \
    }

/*
 *-------------------------------------------------------------------------
 *
 * drcCheckAngles --- checks whether a tile conforms to orthogonal-only
 *	geometry (90 degree angles only) or 45-degree geometry (x must
 *	be equal to y on all non-Manhattan tiles).
 *
 * Results: none
 *
 * Side Effects: may cause errors to be painted.
 *
 *-------------------------------------------------------------------------
 */


void
drcCheckAngles(tile, arg, cptr)
    Tile	*tile;
    struct drcClientData *arg;
    DRCCookie	*cptr;
{
    Rect rect;
    int ortho = (cptr->drcc_flags & 0x01);  /* 1 = orthogonal, 0 = 45s */

    if (IsSplit(tile))
    {
	if (ortho || (RIGHT(tile) - LEFT(tile)) != (TOP(tile) - BOTTOM(tile)))
	{
	    TiToRect(tile, &rect);
	    GeoClip(&rect, arg->dCD_clip);
	    if (!GEO_RECTNULL(&rect))
	    {
		arg->dCD_cptr = cptr;
		(*(arg->dCD_function)) (arg->dCD_celldef, &rect,
			arg->dCD_cptr, arg->dCD_clientData);
		(*(arg->dCD_errors))++;
	    }
	}
    }
}

/*
 *-------------------------------------------------------------------------
 *
 * drcCheckArea- checks to see that a collection of tiles of a given 
 *	type have more than a minimum area.
 *
 * Results: none
 *
 * Side Effects: may cause errors to be painted.
 *
 *-------------------------------------------------------------------------
 */

void
drcCheckArea(starttile,arg,cptr)
	Tile	*starttile;
	struct drcClientData	*arg;
	DRCCookie	*cptr;

{
    int			arealimit;
    long		area = 0L;
    TileTypeBitMask	*oktypes = &cptr->drcc_mask;
    Tile		*tile,*tp;
    Rect		*cliprect = arg->dCD_rect;

    arealimit = cptr->drcc_cdist;
     
    arg->dCD_cptr = cptr;
    if (DRCstack == (Stack *) NULL)
	DRCstack = StackNew(64);

    /* Mark this tile as pending and push it */
    PUSHTILE(starttile);

    while (!StackEmpty(DRCstack))
    {
	tile = (Tile *) STACKPOP(DRCstack);
	if (tile->ti_client != (ClientData)DRC_PENDING) continue;
	area += (long)(RIGHT(tile)-LEFT(tile))*(TOP(tile)-BOTTOM(tile));
	tile->ti_client = (ClientData)DRC_PROCESSED;
	/* are we at the clip boundary? If so, skip to the end */
	if (RIGHT(tile) == cliprect->r_xtop ||
	    LEFT(tile) == cliprect->r_xbot ||
	    BOTTOM(tile) == cliprect->r_ybot ||
	    TOP(tile) == cliprect->r_ytop) goto forgetit;

        if (area >= (long)arealimit) goto forgetit;

	/* Top */
	for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp = BL(tp))
	    if (TTMaskHasType(oktypes, TiGetBottomType(tp)))	PUSHTILE(tp);

	/* Left */
	for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
	    if (TTMaskHasType(oktypes, TiGetRightType(tp))) PUSHTILE(tp);

	/* Bottom */
	for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp = TR(tp))
	    if (TTMaskHasType(oktypes, TiGetTopType(tp))) PUSHTILE(tp);

	/* Right */
	for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp = LB(tp))
	    if (TTMaskHasType(oktypes, TiGetLeftType(tp))) PUSHTILE(tp);
     }

     if (area < (long)arealimit)
     {
	 Rect	rect;
	 TiToRect(starttile,&rect);
	 GeoClip(&rect, arg->dCD_clip);
	 if (!GEO_RECTNULL(&rect)) {
	     (*(arg->dCD_function)) (arg->dCD_celldef, &rect,
		 arg->dCD_cptr, arg->dCD_clientData);
	     /***
	     DBWAreaChanged(arg->dCD_celldef,&rect, DBW_ALLWINDOWS, 
						    &DBAllButSpaceBits);
	     ***/
	     (*(arg->dCD_errors))++;
	 }
     }

forgetit:
     while (!StackEmpty(DRCstack)) tile = (Tile *) STACKPOP(DRCstack);

     /* reset the tiles */
     starttile->ti_client = (ClientData)DRC_UNPROCESSED;
     STACKPUSH(starttile, DRCstack);
     while (!StackEmpty(DRCstack))
     {
	tile = (Tile *) STACKPOP(DRCstack);

	/* Top */
	for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp = BL(tp))
	    if (tp->ti_client != (ClientData)DRC_UNPROCESSED)
	    {
	    	 tp->ti_client = (ClientData)DRC_UNPROCESSED;
		 STACKPUSH(tp,DRCstack);
	    }

	/* Left */
	for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
	    if (tp->ti_client != (ClientData)DRC_UNPROCESSED)
	    {
	    	 tp->ti_client = (ClientData)DRC_UNPROCESSED;
		 STACKPUSH(tp,DRCstack);
	    }

	/* Bottom */
	for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp = TR(tp))
	    if (tp->ti_client != (ClientData)DRC_UNPROCESSED)
	    {
	    	 tp->ti_client = (ClientData)DRC_UNPROCESSED;
		 STACKPUSH(tp,DRCstack);
	    }

	/* Right */
	for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp = LB(tp))
	    if (tp->ti_client != (ClientData)DRC_UNPROCESSED)
	    {
	    	 tp->ti_client = (ClientData)DRC_UNPROCESSED;
		 STACKPUSH(tp,DRCstack);
	    }

     }
}


/*
 *-------------------------------------------------------------------------
 *
 * drcCheckMaxwidth - checks to see that at least one dimension of a region
 *	does not exceed some amount (original version---for "bends_illegal"
 *	option only).
 *
 *  This should really be folded together with drcCheckArea, since the routines
 *	are nearly identical, but I'm feeling lazy, so I'm just duplicating
 *	the code for now.
 *
 * Results: 1 if within max bounds, 0 otherwise.
 *
 * Side Effects: may cause errors to be painted.
 *
 *-------------------------------------------------------------------------
 */

int
drcCheckMaxwidth(starttile,arg,cptr)
    Tile	*starttile;
    struct drcClientData	*arg;
    DRCCookie	*cptr;
{
    int			edgelimit;
    int			retval = 0;
    Rect		boundrect;
    TileTypeBitMask	*oktypes;
    Tile		*tile,*tp;

    oktypes = &cptr->drcc_mask;
    edgelimit = cptr->drcc_dist;
    arg->dCD_cptr = cptr;
    if (DRCstack == (Stack *) NULL)
	DRCstack = StackNew(64);

    /* Mark this tile as pending and push it */

    PUSHTILE(starttile);
    TiToRect(starttile,&boundrect);

    while (!StackEmpty(DRCstack))
    {
	tile = (Tile *) STACKPOP(DRCstack);
	if (tile->ti_client != (ClientData)DRC_PENDING) continue;
	tile->ti_client = (ClientData)DRC_PROCESSED;
	
	if (boundrect.r_xbot > LEFT(tile)) boundrect.r_xbot = LEFT(tile);
	if (boundrect.r_xtop < RIGHT(tile)) boundrect.r_xtop = RIGHT(tile);
	if (boundrect.r_ybot > BOTTOM(tile)) boundrect.r_ybot = BOTTOM(tile);
	if (boundrect.r_ytop < TOP(tile)) boundrect.r_ytop = TOP(tile);

        if (boundrect.r_xtop - boundrect.r_xbot > edgelimit &&
             boundrect.r_ytop - boundrect.r_ybot > edgelimit)
	{
	    while (!StackEmpty(DRCstack)) tile = (Tile *) STACKPOP(DRCstack);
	    break;
	}

	/* Top */
	for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp = BL(tp))
	    if (TTMaskHasType(oktypes, TiGetBottomType(tp)))	PUSHTILE(tp);

	/* Left */
	for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
	    if (TTMaskHasType(oktypes, TiGetRightType(tp))) PUSHTILE(tp);

	/* Bottom */
	for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp = TR(tp))
	    if (TTMaskHasType(oktypes, TiGetTopType(tp))) PUSHTILE(tp);

	/* Right */
	for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp = LB(tp))
	    if (TTMaskHasType(oktypes, TiGetLeftType(tp))) PUSHTILE(tp);
    }

    if (boundrect.r_xtop - boundrect.r_xbot > edgelimit &&
             boundrect.r_ytop - boundrect.r_ybot > edgelimit) 
    {
	Rect	rect;
	TiToRect(starttile,&rect);
	GeoClip(&rect, arg->dCD_clip);
	if (!GEO_RECTNULL(&rect)) {
	    (*(arg->dCD_function)) (arg->dCD_celldef, &rect,
			arg->dCD_cptr, arg->dCD_clientData);
	    (*(arg->dCD_errors))++;
	    retval = 1;
	}
	 
    }

    /* reset the tiles */
    starttile->ti_client = (ClientData)DRC_UNPROCESSED;
    STACKPUSH(starttile, DRCstack);
    while (!StackEmpty(DRCstack))
    {
	tile = (Tile *) STACKPOP(DRCstack);

	/* Top */
	for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp = BL(tp))
	    if (tp->ti_client != (ClientData)DRC_UNPROCESSED)
	    {
	    	 tp->ti_client = (ClientData)DRC_UNPROCESSED;
		 STACKPUSH(tp,DRCstack);
	    }

	/* Left */
	for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
	    if (tp->ti_client != (ClientData)DRC_UNPROCESSED)
	    {
	    	 tp->ti_client = (ClientData)DRC_UNPROCESSED;
		 STACKPUSH(tp,DRCstack);
	    }

	/* Bottom */
	for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp = TR(tp))
	    if (tp->ti_client != (ClientData)DRC_UNPROCESSED)
	    {
	    	 tp->ti_client = (ClientData)DRC_UNPROCESSED;
		 STACKPUSH(tp,DRCstack);
	    }

	/* Right */
	for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp = LB(tp))
	    if (tp->ti_client != (ClientData)DRC_UNPROCESSED)
	    {
	    	 tp->ti_client = (ClientData)DRC_UNPROCESSED;
		 STACKPUSH(tp,DRCstack);
	    }

    }
    return retval;
}


/*
 *-------------------------------------------------------------------------
 *
 * drcCheckRectSize- 
 *
 *	Checks to see that a collection of tiles of given 
 *	types have the proper size (max size and also even or odd size).
 *
 * Results: none
 *
 * Side Effects: may cause errors to be painted.
 *
 *-------------------------------------------------------------------------
 */

void
drcCheckRectSize(starttile, arg, cptr)
    Tile *starttile;
    struct drcClientData *arg;
    DRCCookie *cptr;
{
    int maxsize, even;
    TileTypeBitMask *oktypes = &cptr->drcc_mask;
    int width;
    int height;
    int errwidth;
    int errheight;
    Tile *t;
    bool error = FALSE;

    maxsize = cptr->drcc_dist;
    even = cptr->drcc_cdist;

    /* This code only has to work for rectangular regions, since we always
     * check for rectangular-ness using normal edge rules produced when
     * we read in the tech file.
     */
    arg->dCD_cptr = cptr;
    ASSERT(TTMaskHasType(oktypes, TiGetType(starttile)), "drcCheckRectSize");
    for (t = starttile; TTMaskHasType(oktypes, TiGetType(t)); t = TR(t)) 
	/* loop has empty body */ ;
    errwidth = width = LEFT(t) - LEFT(starttile);
    for (t = starttile; TTMaskHasType(oktypes, TiGetType(t)); t = RT(t)) 
	/* loop has empty body */ ;
    errheight = height = BOTTOM(t) - BOTTOM(starttile);
    ASSERT(width > 0 && height > 0, "drcCheckRectSize");

    if (width > maxsize) {error = TRUE; errwidth = (width - maxsize);}
    else if (height > maxsize) {error = TRUE; errheight = (height - maxsize);}
    else if (even >= 0) {
	/* meaning of "even" variable:  -1, any; 0, even; 1, odd */
	if (ABS(width - ((width/2)*2)) != even) {error = TRUE; errwidth = 1;}
	else if (ABS(height - ((height/2)*2)) != even) {error = TRUE; errheight = 1;}
    }

    if (error) {
	Rect rect;
	TiToRect(starttile, &rect);
	rect.r_xtop = rect.r_xbot + errwidth;
	rect.r_ytop = rect.r_ybot + errheight;
	GeoClip(&rect, arg->dCD_clip);
	if (!GEO_RECTNULL(&rect)) {
	    (*(arg->dCD_function)) (arg->dCD_celldef, &rect,
		arg->dCD_cptr, arg->dCD_clientData);
	    (*(arg->dCD_errors))++;
	}
	
    }
}

/*
 *-------------------------------------------------------------------------
 *
 * drcCanonicalMaxwidth - checks to see that at least one dimension of a
 *	rectangular region does not exceed some amount.
 *
 *	This differs from "CheckMaxwidth" in being more rigorous about
 *	determining where a region of max width might be found.  There
 *	is no "bend" rule here.  We check from the edge being observed
 *	and back, and adjust the bounds on the sides, forking as
 *	necessary to consider alternative arrangements of the interior
 *	rectangle.  A distance "dist" is passed to the routine.  We
 *	may push the interior rectangle back by up to this amount from
 *	the observed edge.  For "widespacing" rules, we check all
 *	interior regions that satisfy maxwidth and whose edge is
 *	within "dist" of the original edge.  For slotting requirement
 *	rules, "dist" is zero (inability to find a rectangle touching
 *	the original edge ensures that no such rectangle exists that
 *	can't be found touching a different edge).  Also, we only
 *	need to check one of the four possible edge combinations
 *	(this part of it is handled in the drcBasic code).
 *
 * Results:
 *	LinkedRect list of areas satisfying maxwidth.  There may be
 *	more than one rectangle, and rectangles may overlap.  It
 *	may make more sense to return only one rectangle, the union
 *	of all rectangles in the list.
 *
 * Side Effects:
 *	None.
 *
 *-------------------------------------------------------------------------
 */

MaxRectsData *
drcCanonicalMaxwidth(starttile, dir, arg, cptr)
    Tile	*starttile;
    int		dir;			/* direction of rule */
    struct	drcClientData	*arg;
    DRCCookie	*cptr;
{
    int		    s, edgelimit;
    Tile	    *tile,*tp;
    TileTypeBitMask wrongtypes;
    static MaxRectsData *mrd = (MaxRectsData *)NULL;
    Rect	    *boundrect, boundorig;

    /* Generate an initial array size of 8 for rlist and swap. */
    if (mrd == (MaxRectsData *)NULL)
    {
	mrd = (MaxRectsData *)mallocMagic(sizeof(MaxRectsData));
	mrd->rlist = (Rect *)mallocMagic(8 * sizeof(Rect));
	mrd->swap = (Rect *)mallocMagic(8 * sizeof(Rect));
	mrd->listdepth = 8;
    }
    if (starttile == NULL) return mrd;

    boundrect = &(mrd->rlist[0]);
    mrd->match = CLIENTDEFAULT;
     
    edgelimit = cptr->drcc_dist;
    arg->dCD_cptr = cptr;

    TiToRect(starttile, boundrect);

    /* Determine area to be searched */

    switch (dir)
    {
	case GEO_NORTH:
	    boundrect->r_ytop = boundrect->r_ybot;
	    boundrect->r_xbot -= (edgelimit - 1);
	    boundrect->r_xtop += (edgelimit - 1);
	    boundrect->r_ytop += edgelimit;
	    break;

	case GEO_SOUTH:
	    boundrect->r_ybot = boundrect->r_ytop;
	    boundrect->r_xbot -= (edgelimit - 1);
	    boundrect->r_xtop += (edgelimit - 1);
	    boundrect->r_ybot -= edgelimit;
	    break;

	case GEO_EAST:
	    boundrect->r_xtop = boundrect->r_xbot;
	    boundrect->r_ybot -= (edgelimit - 1);
	    boundrect->r_ytop += (edgelimit - 1);
	    boundrect->r_xtop += edgelimit;
	    break;

	case GEO_WEST:
	    boundrect->r_xbot = boundrect->r_xtop;
	    boundrect->r_ybot -= (edgelimit - 1);
	    boundrect->r_ytop += (edgelimit - 1);
	    boundrect->r_xbot -= edgelimit;
	    break;

	case GEO_CENTER:
	    boundrect->r_xbot -= edgelimit;
	    boundrect->r_xtop += edgelimit;
	    boundrect->r_ybot -= edgelimit;
	    boundrect->r_ytop += edgelimit;
	    break;
    }

    /* Do an area search on boundrect to find all materials not	*/
    /* in oktypes.  Each such tile clips or subdivides		*/
    /* boundrect.  Any rectangles remaining after the search	*/
    /* satisfy the maxwidth rule.				*/

    mrd->entries = 1;
    mrd->maxdist = edgelimit;
    TTMaskCom2(&wrongtypes, &cptr->drcc_mask);
    boundorig = *boundrect;
    DBSrPaintArea(starttile, arg->dCD_celldef->cd_planes[cptr->drcc_plane],
		&boundorig, &wrongtypes, FindMaxRects, mrd);
    if (mrd->entries == 0)
	return NULL;
    else
	return (MaxRectsData *)mrd;
}

