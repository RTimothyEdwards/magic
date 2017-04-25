/*
 * ResFract.c 
 *
 * routines to convert a maximum horizontal rectangles database
 * into one fractured in the manner of Horowitz's '83 Transactions
 * on CAD paper.
 *
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/resis/ResFract.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "textio/txcommands.h"
#include "tiles/tile.h"
#include "utils/signals.h"
#include "utils/hash.h"
#include "database/database.h"
#include "database/databaseInt.h"
#include "utils/malloc.h"
#include "windows/windows.h"
#include "utils/main.h"

extern Tile *ResSplitX();


Tile	*resSrTile;
Tile	*resTopTile;
Plane	*resFracPlane;

/* Forward declarations */

extern void ResCheckConcavity();


/*
 * --------------------------------------------------------------------
 *
 * ResFracture --  Convert a maxiumum horizontal strips cell def into
 *	one where the split at each concave corner is in the direction
 *	with the least material of the same tiletype.  This is done
 *	using TiSplitX and TiJoinY.  Joins are only done on tiles with
 *	the same time; this implies that contacts should first be erased 
 *	using ResDissolve contacts. 
 *
 *	We can't use DBSrPaintArea because  the fracturing
 *	routines modify the database.  This is essentially the same routine
 *	except that has to be careful that it doesn't merge away the
 *	current tile in the search.
 *
 *
 * --------------------------------------------------------------------
 */

int
ResFracture(plane, rect)
    Plane *plane;
    Rect *rect;
{
    Point start;
    Tile  *tpnew;
    TileType	tt;

    resFracPlane = plane;

    start.p_x = rect->r_xbot;
    start.p_y = rect->r_ytop - 1;
    resSrTile = plane->pl_hint;
    GOTOPOINT(resSrTile, &start);

    /* Each iteration visits another tile on the LHS of the search area */
    while (TOP(resSrTile) > rect->r_ybot)
    {
	/* Each iteration enumerates another tile */
enumerate:
	plane->pl_hint = resSrTile;
	if (SigInterruptPending)
	    return (1);

	if ((tt=TiGetType(resSrTile)) != TT_SPACE)
	{
	     resTopTile = RT(resSrTile); 
	     while (RIGHT(resTopTile) > LEFT(resSrTile))
	     {
	     	  TileType ntt = TiGetType(resTopTile);
		  
		  if (ntt != tt)
		  {
		       resTopTile=BL(resTopTile);
		       continue;
		  } 
		  /* ok, we may have found a concave corner */
		  ResCheckConcavity(resSrTile,resTopTile,tt);
		  if (resTopTile  == NULL) break;
		  if (BOTTOM(resTopTile) != TOP(resSrTile))
		  {
		       resTopTile = RT(resSrTile);
		  }
		  else
		  {
		       resTopTile=BL(resTopTile);
		  }
	     }
	     
	}

	tpnew = TR(resSrTile);
	if (LEFT(tpnew) < rect->r_xtop)
	{
	    while (BOTTOM(tpnew) >= rect->r_ytop) tpnew = LB(tpnew);
	    if (BOTTOM(tpnew) >= BOTTOM(resSrTile) || BOTTOM(resSrTile) <= rect->r_ybot)
	    {
		resSrTile = tpnew;
		goto enumerate;
	    }
	}

	/* Each iteration returns one tile further to the left */
	while (LEFT(resSrTile) > rect->r_xbot)
	{
	    if (BOTTOM(resSrTile) <= rect->r_ybot)
		return (0);
	    tpnew = LB(resSrTile);
	    resSrTile = BL(resSrTile);
	    if (BOTTOM(tpnew) >= BOTTOM(resSrTile) || BOTTOM(resSrTile) <= rect->r_ybot)
	    {
		resSrTile = tpnew;
		goto enumerate;
	    }
	}

	/* At left edge -- walk down to next tile along the left edge */
	for (resSrTile = LB(resSrTile); RIGHT(resSrTile) <= rect->r_xbot; resSrTile = TR(resSrTile))
	    /* Nothing */;
    }
    return (0);
}

/*
 *-------------------------------------------------------------------------
 *
 * ResCheckConcavity -- Called when two tiles of the same type are found.
 *	These tiles can form concave edges 4 different ways; check for
 *	each such case.  When one is found, call the resWalk routines to
 *	decide whether any tiles need to be split.
 *
 * Results: none.
 *
 * Side Effects: may change the plane on which it acts.  Can also modify
 *	the global variable resTopTile.
 *
 *-------------------------------------------------------------------------
 */

void
ResCheckConcavity(bot,top,tt)
	Tile	*bot,*top;
	TileType	tt;

{
     Tile	*tp;
     int	xlen,ylen;
     /* corner #1:
	  XXXXXXX
       YYYYYYY
              ^--here
    */
     if (RIGHT(top) > RIGHT(bot) && TiGetType(TR(bot)) != tt)
     {
	    	 int	xpos = RIGHT(bot);
		 int	ypos = BOTTOM(top);
		 xlen = xpos - resWalkleft(top,tt,xpos,ypos,NULL);
		 ylen = resWalkup(top,tt,xpos,ypos,NULL)   - ypos;
		 if (xlen > ylen)
		 {
		      (void) resWalkup(top,tt,xpos,ypos,ResSplitX);
		 }
     }
     if (resTopTile == NULL) return;
     /* corner #2:
                 v--here
	  XXXXXXX
       	      YYYYYYY
    */
     if (RIGHT(top) < RIGHT(bot))
     {
	    for (tp = TR(top);BOTTOM(tp) > BOTTOM(top);tp=LB(tp));
	    if (TiGetType(tp) != tt)
	    {
	    	 int	xpos = RIGHT(top);
		 int	ypos = BOTTOM(top);
		 xlen = xpos-resWalkleft(top,tt,xpos,ypos,NULL);
		 ylen = ypos-resWalkdown(bot,tt,xpos,ypos,NULL);
		 if (xlen > ylen)
		 {
		      (void) resWalkdown(bot,tt,xpos,ypos,ResSplitX);
		 }
	    }
     }
     if (resTopTile == NULL) return;
     /* corner #3:
	  XXXXXXX
       	       YYYYYYY
              ^--here
    */
     if (LEFT(top) < LEFT(bot))
     {
	    for (tp = BL(bot);TOP(tp) < TOP(bot);tp=RT(tp));
	    if (TiGetType(tp) != tt)
	    {
	    	 int	xpos = LEFT(bot);
		 int	ypos = BOTTOM(top);
		 xlen = resWalkright(top,tt,xpos,ypos,NULL)- xpos;
		 ylen = resWalkup(top,tt,xpos,ypos,NULL)   - ypos;
		 if (xlen > ylen)
		 {
		      (void) resWalkup(top,tt,xpos,ypos,ResSplitX);
		 }
	    }
     }
     if (resTopTile == NULL) return;
     /* corner #4:
          v--here
	   XXXXXXX
       	YYYYYYY
     */
     if (LEFT(top) > LEFT(bot) && TiGetType(BL(top)) != tt)
     {
	    	 int	xpos = LEFT(top);
		 int	ypos = BOTTOM(top);
		 xlen = resWalkright(top,tt,xpos,ypos,NULL)- xpos;
		 ylen = ypos-resWalkdown(bot,tt,xpos,ypos,NULL);
		 if (xlen > ylen)
		 {
		      (void) resWalkdown(bot,tt,xpos,ypos,ResSplitX);
		 }
     }
}

/*
 *-------------------------------------------------------------------------
 *
 * resWalk{up,down,left,right} -- move in the specified direction looking
 *	for tiles of a given type.
 *
 * Results: returns the coordinate that is the farthest point in the specified
 *	direction that one can walk and still be surrounded by material of 
 *	a given type. 
 *
 * Side Effects: if func is non-NULL, it is called on each tile intersected
 *	by the path.  (Note that if the path moves along the edge of a tile,
 *	func is not called.)
 *
 *-------------------------------------------------------------------------
 */

int
resWalkup(tile,tt,xpos,ypos,func)
	Tile		*tile;
	TileType	tt;
	int		xpos,ypos;
	Tile *		(*func)();

{
     Point	pt;
     Tile	*tp;
     
     pt.p_x = xpos;
     while (TiGetType(tile) == tt)
     {
	  if (xpos == LEFT(tile))
	  {
	       /* walk up left edge */
	       for (tp = BL(tile);TOP(tp) <= ypos;tp=RT(tp));
	       for (;BOTTOM(tp) < TOP(tile);tp=RT(tp))
	       {
	       	    if (TiGetType(tp) != tt) return(BOTTOM(tp));
	       }
	  }
	  else 
	  {
	       if (func) tile = (*func)(tile,xpos);
	  }
          pt.p_y = TOP(tile);
          GOTOPOINT(tile,&pt);
     }
     return(BOTTOM(tile));
}

int
resWalkdown(tile,tt,xpos,ypos,func)
	Tile		*tile;
	TileType	tt;
	int		xpos,ypos;
	Tile *		(*func)();

{
     Point	pt;
     Tile	*tp;
     Tile	*endt;
     
     pt.p_x = xpos;
     while (TiGetType(tile) == tt)
     {
	  if (xpos == LEFT(tile))
	  {
	       /* walk up left edge */
	       endt = NULL;
	       for (tp = BL(tile);BOTTOM(tp) < TOP(tile);tp=RT(tp))
	       {
	       	    if (TiGetType(tp) != tt)
		    {
		    	 if (BOTTOM(tp) < ypos) endt = tp;
		    } 
	       }
	       if (endt)
	       {
	       	    return TOP(endt);
	       }
	  }
	  else 
	  {
	       if (func) tile = (*func)(tile,xpos);
	  }
          pt.p_y = BOTTOM(tile)-1;
          GOTOPOINT(tile,&pt);
     }
     return(TOP(tile));
}

int
resWalkright(tile,tt,xpos,ypos,func)
	Tile		*tile;
	TileType	tt;
	int		xpos,ypos;
	Tile *		(*func)();

{
     Point	pt;
     Tile	*tp;
     
     pt.p_y = ypos;
     while (TiGetType(tile) == tt)
     {
	  if (ypos == BOTTOM(tile))
	  {
	       /* walk along bottom edge */
	       for (tp = LB(tile);LEFT(tp) < xpos;tp=TR(tp));
	       for (;LEFT(tp) < RIGHT(tile);tp=TR(tp))
	       {
	       	    if (TiGetType(tp) != tt) return(LEFT(tp));
	       }
	  }
	  else 
	  {
	       if (func) tile = (*func)(tile,ypos);
	  }
          pt.p_x = RIGHT(tile);
          GOTOPOINT(tile,&pt);
     }
     return(LEFT(tile));
}

int
resWalkleft(tile,tt,xpos,ypos,func)
	Tile		*tile;
	TileType	tt;
	int		xpos,ypos;
	Tile *		(*func)();

{
     Point	pt;
     Tile	*tp;
     Tile	*endt;
     
     pt.p_y = ypos;
     while (TiGetType(tile) == tt)
     {
	  if (ypos == BOTTOM(tile))
	  {
	       /* walk along bottom edge */
	       endt = NULL;
	       for (tp = LB(tile);LEFT(tp) < RIGHT(tile);tp=TR(tp))
	       {
	       	    if (TiGetType(tp) != tt)
		    {
		    	 if (LEFT(tp) < xpos) endt = tp;
		    } 
	       }
	       if (endt)
	       {
	       	    return RIGHT(endt);
	       }
	  }
	  else 
	  {
	       if (func) tile = (*func)(tile,ypos);
	  }
          pt.p_x = LEFT(tile)-1;
          GOTOPOINT(tile,&pt);
     }
     return(RIGHT(tile));
}

/*
 *-------------------------------------------------------------------------
 *
 * ResSplitX -- calls TiSplitX, sets the tiletype,
 *		then tries to join tiles that share a common long edge.
 *
 * Results: returns new tile if tile was destroyed by a Join function.
 *
 * Side Effects: modifies the tile plane and the global variables
 *		 resSrTile and resTopTile.
 *
 *-------------------------------------------------------------------------
 */
Tile *
ResSplitX(tile,x)
	Tile	*tile;
	int	x;

{
     TileType	tt = TiGetType(tile);
     Tile	*tp = TiSplitX(tile,x);
     Tile	*tp2;
     
     TiSetBody(tp,tt);
     /* check to see if we can combine with the tiles above or below us */
     tp2 = RT(tile);
     if (TiGetType(tp2) == tt && LEFT(tp2) == LEFT(tile) && RIGHT(tp2) == RIGHT(tile))
     {
     	  if (tp2 == resSrTile)
	  {
	       if (resTopTile == tile) resTopTile = NULL;
	       TiJoinY(tp2,tile,resFracPlane); 
	       tile = tp2;
	  }
	  else
	  {
	       if (resTopTile == tp2) resTopTile = NULL;
	       TiJoinY(tile,tp2,resFracPlane); 
	  }
     }
     tp2 = LB(tile);
     if (TiGetType(tp2) == tt && LEFT(tp2) == LEFT(tile) && RIGHT(tp2) == RIGHT(tile))
     {
     	  if (tp2 == resSrTile)
	  {
	       if (resTopTile == tile) resTopTile = NULL;
	       TiJoinY(tp2,tile,resFracPlane); 
	       tile = tp2;
	  }
	  else
	  {
	       if (resTopTile == tp2) resTopTile = NULL;
	       TiJoinY(tile,tp2,resFracPlane); 
	  }
     }
     /* do the same checks with the newly created tile */
     tp2 = RT(tp);
     if (TiGetType(tp2) == tt && LEFT(tp2) == LEFT(tp) && RIGHT(tp2) == RIGHT(tp))
     {
     	  TiJoinY(tp2,tp,resFracPlane);
	  tp = tp2;
     }
     tp2 = LB(tp);
     if (TiGetType(tp2) == tt && LEFT(tp2) == LEFT(tp) && RIGHT(tp2) == RIGHT(tp))
     {
     	  TiJoinY(tp2,tp,resFracPlane); 
     }
     return tile;
}
