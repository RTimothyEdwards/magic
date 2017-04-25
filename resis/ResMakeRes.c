
#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/resis/ResMakeRes.c,v 1.3 2010/06/24 12:37:56 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/malloc.h"
#include "textio/textio.h"
#include "extract/extract.h"
#include "extract/extractInt.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/tech.h"
#include "textio/txcommands.h"
#include "resis/resis.h"

/* Forward declarations */
bool ResCalcNearTransistor();
bool ResCalcNorthSouth();
bool ResCalcEastWest();


/*
 *--------------------------------------------------------------------------
 *
 * ResCalcTileResistance-- Given a set of partitions for a tile, the tile can
 *	be converted into resistors. To do this, nodes are sorted in the 
 *	direction of current flow. Resistors are created by counting squares
 *	between successive breakpoints. Breakpoints with the same coordinate
 *	are combined.
 *
 *   Results: returns TRUE if the startnode was involved in a merge.
 *
 *   Side Effects:  Resistor structures are produced.  Some nodes may be
 *		    eliminated.
 *--------------------------------------------------------------------------
 *
 */

bool 
ResCalcTileResistance(tile, junk, pendingList, doneList)
    Tile 	*tile;
    tileJunk 	*junk;
    resNode	**pendingList, **doneList;

{
    int 	MaxX = MINFINITY, MinX = INFINITY;
    int		MaxY = MINFINITY, MinY = INFINITY;
    int		transistor;
    bool	merged;
    Breakpoint 	*p1;
     
    merged = FALSE;
    transistor = FALSE;
     
    if ((p1 = junk->breakList) == NULL) return FALSE;
    for (; p1; p1 = p1->br_next)
    {
	int	x = p1->br_loc.p_x;
	int	y = p1->br_loc.p_y;
	if (x > MaxX) MaxX = x;
	if (x < MinX) MinX = x;
	if (y > MaxY) MaxY = y;
	if (y < MinY) MinY = y;
	if (p1->br_this->rn_why == RES_NODE_TRANSISTOR)
	{
	    transistor = TRUE;
	}
    }
     
    /* Finally, produce resistors for partition. Keep track of	*/
    /* whether or not the node was involved in a merge.		*/
     
    if (transistor)
    {
	merged |= ResCalcNearTransistor(tile, pendingList, doneList, &ResResList);
    }
    else if (MaxY-MinY > MaxX-MinX)
    {
	merged |= ResCalcNorthSouth(tile, pendingList, doneList, &ResResList);
    }
    else
    {
	merged |= ResCalcEastWest(tile, pendingList, doneList, &ResResList);
    }

    /* 
     * For all the new resistors, propagate the resistance from the origin
     * to the new nodes.
     */

    return(merged);
}


/*
 *-------------------------------------------------------------------------
 *
 * ResCalcEastWest-- Makes resistors from an EastWest partition.
 *
 * Results: Returns TRUE if the sacredNode was involved in a merge.
 *
 * Side Effects: Makes resistors. Frees breakpoints.
 *
 *-------------------------------------------------------------------------
 */

bool
ResCalcEastWest(tile, pendingList, doneList, resList)
    Tile	*tile;
    resNode	**pendingList, **doneList;
    resResistor	**resList;
{
    int 	height;
    bool	merged;
    Breakpoint	*p1, *p2, *p3;
    resResistor	*resistor;
    resElement	*element;
    resNode	*currNode;
    float	rArea;
    tileJunk	*junk = (tileJunk *)tile->ti_client;
     
    merged = FALSE;
    height = TOP(tile) - BOTTOM(tile);

    /* 
     * One Breakpoint? No resistors need to be made. Free up the first
     * breakpoint, then return.
     */

    p1 = junk->breakList;
    if   (p1->br_next == NULL) 
    {
	p1->br_this->rn_float.rn_area += height * (LEFT(tile) - RIGHT(tile));
	freeMagic((char *)p1);
	junk->breakList = NULL;
	return(merged);
    }

    /* re-sort nodes left to right. */

    ResSortBreaks(&junk->breakList, TRUE);
     
    /* 
     * Eliminate breakpoints with the same X coordinate and merge 
     * their nodes.
     */

    p2= junk->breakList;

    /* add extra left area to leftmost node */

    p2->br_this->rn_float.rn_area += 	height*(p2->br_loc.p_x-LEFT(tile));
    while (p2->br_next != NULL)
    {
	p1 = p2;
	p2 = p2->br_next;
	if (p2->br_loc.p_x == p1->br_loc.p_x)
	{
	    if (p2->br_this == p1->br_this)
	    {
		 currNode = NULL;
		 p1->br_next = p2->br_next;
		 freeMagic((char *)p2);
		 p2 = p1;
	    }
	    else if (p2->br_this == resCurrentNode)
	    {
		 currNode = p1->br_this;
	    	 ResMergeNodes(p2->br_this,p1->br_this,pendingList,doneList);
		 merged = TRUE;
		 freeMagic((char *)p1);
	    }
	    else if (p1->br_this == resCurrentNode)
	    {
		 currNode = p2->br_this;
		 p1->br_next = p2->br_next;
	    	 ResMergeNodes(p1->br_this,p2->br_this,pendingList,doneList);
		 merged = TRUE;
		 freeMagic((char *)p2);
		 p2 = p1;
	    }
	    else
	    {
		 currNode = p1->br_this;
	    	 ResMergeNodes(p2->br_this,p1->br_this,pendingList,doneList);
		 freeMagic((char *)p1);
	    }

	    /* 
	     * Was the node used in another junk or breakpoint?
	     * If so, replace the old node with the new one.
	     */
	    p3  = p2->br_next;
	    while (p3 != NULL)
	    {
		if (p3->br_this == currNode)
		{
		     p3->br_this = p2->br_this;
		}
		p3 = p3->br_next;
	    }
       }

       /* 
        * If the X coordinates don't match, make a resistor between
        * the breakpoints.
        */

       else
       {
            resistor = (resResistor *) mallocMagic((unsigned) (sizeof(resResistor)));
            resistor->rr_nextResistor = (*resList);
            resistor->rr_lastResistor = NULL;
            if ((*resList) != NULL) (*resList)->rr_lastResistor = resistor;
            (*resList) = resistor;
            resistor->rr_connection1 = p1->br_this;
            resistor->rr_connection2 = p2->br_this;
            element = (resElement *) mallocMagic((unsigned) (sizeof(resElement)));
            element->re_nextEl = p1->br_this->rn_re;
            element->re_thisEl = resistor;
            p1->br_this->rn_re = element;
            element = (resElement *) mallocMagic((unsigned) (sizeof(resElement)));
            element->re_nextEl = p2->br_this->rn_re;
            element->re_thisEl = resistor;
            p2->br_this->rn_re = element;
	    resistor->rr_cl = (TOP(tile) + BOTTOM(tile)) >> 1;
	    resistor->rr_width = height;

	    if (IsSplit(tile))
	    {
		resistor->rr_tt = (SplitSide(tile)) ? SplitRightType(tile)
			: SplitLeftType(tile);
		resistor->rr_status = RES_DIAGONAL;
		resistor->rr_status |= (SplitDirection(tile)) ? RES_NS
			: RES_EW;
			
	    }
	    else
	    {
		resistor->rr_status = RES_EW;
		resistor->rr_tt = TiGetTypeExact(tile);
	    }
#ifdef ARIEL
	    resistor->rr_csArea = height *
				ExtCurStyle->exts_thick[resistor->rr_tt];
#endif
	    resistor->rr_value =
		    	  (ExtCurStyle->exts_sheetResist[resistor->rr_tt]
		          * (p2->br_loc.p_x-p1->br_loc.p_x)) / height;
	    rArea = ((p2->br_loc.p_x-p1->br_loc.p_x) * height) / 2;
	    resistor->rr_connection1->rn_float.rn_area += rArea;
	    resistor->rr_connection2->rn_float.rn_area += rArea;
	    resistor->rr_float.rr_area = 0;
		    
	    freeMagic((char *)p1);
	}
    }
    p2->br_this->rn_float.rn_area += height * (RIGHT(tile) - p2->br_loc.p_x);
    freeMagic((char *)p2);
    junk->breakList = NULL;
    return(merged);
}


/*
 *-------------------------------------------------------------------------
 *
 * ResCalcNorthSouth-- Makes resistors from a NorthSouth partition
 *
 * Results: Returns TRUE if the resCurrentNode was involved in a merge.
 *
 * Side Effects: Makes resistors. Frees breakpoints
 *
 *-------------------------------------------------------------------------
 */

bool
ResCalcNorthSouth(tile, pendingList, doneList, resList)
    Tile	*tile;
    resNode	**pendingList, **doneList;
    resResistor	**resList;
{
    int 	width;
    bool	merged;
    Breakpoint	*p1, *p2, *p3;
    resResistor	*resistor;
    resElement	*element;
    resNode	*currNode;
    float	rArea;
    tileJunk	*junk = (tileJunk *)tile->ti_client;
     
    merged = FALSE;
    width = RIGHT(tile)-LEFT(tile);

    /* 
     * One Breakpoint? No resistors need to be made. Free up the first
     * breakpoint, then return.
     */

    p1 = junk->breakList;
    if   (p1->br_next == NULL) 
    {
	p1->br_this->rn_float.rn_area += width * (TOP(tile) - BOTTOM(tile));
     	freeMagic((char *)p1);
	junk->breakList = NULL;
	return(merged);
    }

    /* re-sort nodes south to north. */
    ResSortBreaks(&junk->breakList, FALSE);

    /* 
     * Eliminate breakpoints with the same Y coordinate and merge 
     * their nodes.
     */

    p2 = junk->breakList;

    /* add extra left area to leftmost node */

    p2->br_this->rn_float.rn_area += width * (p2->br_loc.p_y - BOTTOM(tile));
    while (p2->br_next != NULL)
    {
	p1 = p2;
	p2 = p2->br_next;
	if (p1->br_loc.p_y == p2->br_loc.p_y)
	{
	    if (p2->br_this == p1->br_this)
	    {
		 currNode = NULL;
		 p1->br_next = p2->br_next;
		 freeMagic((char *)p2);
		 p2 = p1;
	    }
	    else if (p2->br_this == resCurrentNode)
	    {
		 currNode = p1->br_this;
	    	 ResMergeNodes(p2->br_this,p1->br_this,pendingList,doneList);
		 freeMagic((char *)p1);
		 merged = TRUE;
	    }
	    else if (p1->br_this == resCurrentNode)
	    {
		 currNode = p2->br_this;
		 p1->br_next = p2->br_next;
	    	 ResMergeNodes(p1->br_this,p2->br_this,pendingList,doneList);
		 merged = TRUE;
		 freeMagic((char *)p2);
		 p2 = p1;
	    }
	    else
	    {
		 currNode = p1->br_this;
	    	 ResMergeNodes(p2->br_this,p1->br_this,pendingList,doneList);
		 freeMagic((char *)p1);
	    }
		    
	    /* 
	     * Was the node used in another junk or breakpoint?
	     * If so, replace the old node with the new one.
	     */
	    p3  = p2->br_next;
	    while (p3 != NULL)
	    {
		if (p3->br_this == currNode)
		{
		     p3->br_this = p2->br_this;
		}
		p3 = p3->br_next;
	    }
	}

	/* 
	 * If the Y coordinates don't match, make a resistor between
	 * the breakpoints.
	 */

	else
	{
	    resistor = (resResistor *) mallocMagic((unsigned) (sizeof(resResistor)));
	    resistor->rr_nextResistor = (*resList);
	    resistor->rr_lastResistor = NULL;
	    if ((*resList) != NULL) (*resList)->rr_lastResistor = resistor;
	    (*resList) = resistor;
	    resistor->rr_connection1 = p1->br_this;
	    resistor->rr_connection2 = p2->br_this;
	    element = (resElement *) mallocMagic((unsigned) (sizeof(resElement)));
	    element->re_nextEl = p1->br_this->rn_re;
	    element->re_thisEl = resistor;
	    p1->br_this->rn_re = element;
	    element = (resElement *) mallocMagic((unsigned) (sizeof(resElement)));
	    element->re_nextEl = p2->br_this->rn_re;
	    element->re_thisEl = resistor;
	    p2->br_this->rn_re = element;
	    resistor->rr_cl = (LEFT(tile) + RIGHT(tile)) >> 1;
	    resistor->rr_width = width;
	    if (IsSplit(tile))
	    {
		resistor->rr_tt = (SplitSide(tile)) ? SplitRightType(tile)
			: SplitLeftType(tile);
		resistor->rr_status = RES_DIAGONAL;
		resistor->rr_status |= (SplitDirection(tile)) ? RES_NS
			: RES_EW;
	    }
	    else
	    {
		resistor->rr_status = RES_NS;
		resistor->rr_tt = TiGetTypeExact(tile);
	    }
#ifdef ARIEL
	    resistor->rr_csArea = width
			* ExtCurStyle->exts_thick[resistor->rr_tt];
#endif
	    resistor->rr_value =
		    	  (ExtCurStyle->exts_sheetResist[resistor->rr_tt]
		          * (p2->br_loc.p_y-p1->br_loc.p_y)) / width;
	    rArea = ((p2->br_loc.p_y-p1->br_loc.p_y) * width) / 2;
	    resistor->rr_connection1->rn_float.rn_area += rArea;
	    resistor->rr_connection2->rn_float.rn_area += rArea;
	    resistor->rr_float.rr_area = 0;
	    freeMagic((char *)p1);
	}
    }
    p2->br_this->rn_float.rn_area += width * (TOP(tile) - p2->br_loc.p_y);
    freeMagic((char *)p2);
    junk->breakList = NULL;
    return(merged);
}


/*
 *-------------------------------------------------------------------------
 *
 * ResCalcNearTransistor-- Calculating the direction of current flow near
 *	transistors is tricky because there are two adjoining regions with
 *	vastly different sheet resistances.  ResCalcNearTransistor is called
 *	whenever a diffusion tile adjoining a real tile is found.  It makes
 *	a guess at the correct direction of current flow, removes extra 
 *	breakpoints, and call either ResCalcEastWest or ResCalcNorthSouth
 *
 * Results:
 *	TRUE if merging occurred, FALSE if not.
 *
 * Side Effects: Makes resistors. Frees breakpoints
 *
 *-------------------------------------------------------------------------
 */

bool
ResCalcNearTransistor(tile, pendingList, doneList, resList)
    Tile	*tile;
    resNode	**pendingList, **doneList;
    resResistor	**resList;

{
     bool 		merged;
     int		trancount,tranedge,deltax,deltay;
     Breakpoint		*p1,*p2,*p3;
     tileJunk		*junk = (tileJunk *)tile->ti_client;
     
     
     merged = FALSE;

     /* 
        One Breakpoint? No resistors need to be made. Free up the first
     	breakpoint, then return.
     */
     
     if   (junk->breakList->br_next == NULL) 
     {
     	  freeMagic((char *)junk->breakList);
	  junk->breakList = NULL;
	  return(merged);
     }
     /* count the number of transistor breakpoints  */
     /* mark which edge they connect to		    */
     trancount = 0;
     tranedge = 0;
     for (p1=junk->breakList; p1 != NULL;p1 = p1->br_next)
     {
	  if (p1->br_this->rn_why == RES_NODE_TRANSISTOR)
	  {
	       trancount++;
	       if (p1->br_loc.p_x == LEFT(tile)) tranedge |= LEFTEDGE;
	       else if (p1->br_loc.p_x == RIGHT(tile)) tranedge |= RIGHTEDGE;
	       else if (p1->br_loc.p_y == TOP(tile)) tranedge |= TOPEDGE;
	       else if (p1->br_loc.p_y == BOTTOM(tile)) tranedge |= BOTTOMEDGE;
	  }
     }
     /* use distance from transistor to next breakpoint as determinant     */
     /* if there is only one transistor or if all the transitors are along */
     /* the same edge.							   */
     if (trancount == 1 		|| 
        (tranedge & LEFTEDGE) == tranedge	||
        (tranedge & RIGHTEDGE) == tranedge 	||
        (tranedge & TOPEDGE) == tranedge 	||
        (tranedge & BOTTOMEDGE) == tranedge)
     {
	  ResSortBreaks(&junk->breakList,TRUE);
          p2 = NULL;
          for (p1=junk->breakList; p1 != NULL;p1 = p1->br_next)
          {
	       if (p1->br_this->rn_why == RES_NODE_TRANSISTOR)
	       {
	            break;
	       }
	       if (p1->br_next != NULL && 
	       	     (p1->br_loc.p_x != p1->br_next->br_loc.p_x ||
	       	      p1->br_loc.p_y != p1->br_next->br_loc.p_y))
	       
	       {
	       	    p2 = p1;
	       }
          }
	  deltax=INFINITY;
	  for (p3 = p1->br_next; 
	       p3 != NULL && 
	       p3->br_loc.p_x == p1->br_loc.p_x && 
	       p3->br_loc.p_y == p1->br_loc.p_y; p3 = p3->br_next);
	  if (p3 != NULL)
	  {
	       if (p3->br_crect)
	       {
	       	    if (p3->br_crect->r_ll.p_x > p1->br_loc.p_x)
		    {
		         deltax = p3->br_crect->r_ll.p_x-p1->br_loc.p_x;
		    }
	       	    else if (p3->br_crect->r_ur.p_x < p1->br_loc.p_x)
		    {
		         deltax = p1->br_loc.p_x-p3->br_crect->r_ur.p_x;
		    }
		    else
		    {
		    	 deltax=0;
		    }
	       }
	       else
	       {
	       deltax = abs(p1->br_loc.p_x-p3->br_loc.p_x);
	       }
	  }
	  if (p2 != NULL)
	  {
	       if (p2->br_crect)
	       {
	       	    if (p2->br_crect->r_ll.p_x > p1->br_loc.p_x)
		    {
		         deltax = MIN(deltax,p2->br_crect->r_ll.p_x-p1->br_loc.p_x);
		    }
	       	    else if (p2->br_crect->r_ur.p_x < p1->br_loc.p_x)
		    {
		         deltax = MIN(deltax,p1->br_loc.p_x-p2->br_crect->r_ur.p_x);
		    }
		    else
		    {
		    	 deltax=0;
		    }
	       }
	       else
	       {
	            deltax = MIN(deltax,abs(p1->br_loc.p_x-p2->br_loc.p_x));
	       }
	  }

          /* re-sort nodes south to north. */
	  ResSortBreaks(&junk->breakList,FALSE);
          p2 = NULL;
          for (p1=junk->breakList; p1 != NULL;p1 = p1->br_next)
          {
	       if (p1->br_this->rn_why == RES_NODE_TRANSISTOR)
	       {
	            break;
	       }
	       if (p1->br_next != NULL && 
	       	     (p1->br_loc.p_x != p1->br_next->br_loc.p_x ||
	       	      p1->br_loc.p_y != p1->br_next->br_loc.p_y))
	       
	       {
	       	    p2 = p1;
	       }
          }
	  deltay=INFINITY;
	  for (p3 = p1->br_next; 
	       p3 != NULL && 
	       p3->br_loc.p_x == p1->br_loc.p_x && 
	       p3->br_loc.p_y == p1->br_loc.p_y; p3 = p3->br_next);
	  if (p3 != NULL)
	  {
	       if (p3->br_crect)
	       {
	       	    if (p3->br_crect->r_ll.p_y > p1->br_loc.p_y)
		    {
		         deltay = p3->br_crect->r_ll.p_y-p1->br_loc.p_y;
		    }
	       	    else if (p3->br_crect->r_ur.p_y < p1->br_loc.p_y)
		    {
		         deltay = p1->br_loc.p_y-p3->br_crect->r_ur.p_y;
		    }
		    else
		    {
		    	 deltay=0;
		    }
	       }
	       else
	       {
	       deltay = abs(p1->br_loc.p_y-p3->br_loc.p_y);
	       }
	  }
	  if (p2!= NULL)
	  {
	       if (p2->br_crect)
	       {
	       	    if (p2->br_crect->r_ll.p_y > p1->br_loc.p_y)
		    {
		         deltay = MIN(deltay,p2->br_crect->r_ll.p_y-p1->br_loc.p_y);
		    }
	       	    else if (p2->br_crect->r_ur.p_y < p1->br_loc.p_y)
		    {
		         deltay = MIN(deltay,p1->br_loc.p_y-p2->br_crect->r_ur.p_y);
		    }
		    else
		    {
		    	 deltay=0;
		    }
	       }
	       else
	       {
	            deltay = MIN(deltay,abs(p1->br_loc.p_y-p2->br_loc.p_y));
	       }
	  }
	  if (deltay > deltax)
	  {
	       return(ResCalcNorthSouth(tile,pendingList,doneList,resList));
	  }
	  else
	  {
	       return(ResCalcEastWest(tile,pendingList,doneList,resList));
	  }

     }
     /* multiple transistors connected to the partition */
     else  
     {
     	  if (tranedge == 0)
	  {
	       TxError("Error in transistor current direction routine\n");
	       return(merged);
	  }
	  /* check to see if the current flow is north-south		*/
	  /* possible north-south conditions:				*/
	  /* 1. there are transistors along the top and bottom edges    */
	  /*    but not along the left or right			        */
	  /* 2. there are transistors along two sides at right angles,  */
	  /*    and the tile is wider than it is tall.			*/
	  
	  if ((tranedge & TOPEDGE)     && 
	      (tranedge & BOTTOMEDGE)  &&
	      !(tranedge & LEFTEDGE)   &&
	      !(tranedge & RIGHTEDGE)  			||
	      (tranedge & TOPEDGE || tranedge & BOTTOMEDGE) &&
	      (tranedge & LEFTEDGE || tranedge & RIGHTEDGE) &&
	      RIGHT(tile)-LEFT(tile) > TOP(tile)-BOTTOM(tile))
	 {
               /* re-sort nodes south to north. */
	       ResSortBreaks(&junk->breakList,FALSE);

	      /* eliminate duplicate S/D pointers */
	      for (p1 = junk->breakList; p1 != NULL; p1 = p1->br_next)
	      {
	      	   if  (p1->br_this->rn_why == RES_NODE_TRANSISTOR &&
		       (p1->br_loc.p_y == BOTTOM(tile) ||
		        p1->br_loc.p_y == TOP(tile)))
		   {
	      		p3 = NULL;
			p2 = junk->breakList;
			while ( p2 != NULL)
			{
			     if (p2->br_this == p1->br_this	&& 
			         p2 != p1			&&
				 p2->br_loc.p_y != BOTTOM(tile) &&
				 p2->br_loc.p_y != TOP(tile))
			     {
			     	  if (p3 == NULL)
				  {
				       junk->breakList = p2->br_next;
				       freeMagic((char *) p2);
				       p2 = junk->breakList;
				  }
				  else
				  {
				       p3->br_next = p2->br_next;
				       freeMagic((char *) p2);
				       p2 = p3->br_next;
				  }
			     }
			     else
			     {
			          p3 = p2;
				  p2 = p2->br_next;
			     }
			}
		   }
	      }
	      return(ResCalcNorthSouth(tile,pendingList,doneList,resList));
	 }
	 else
	 {
	      /* eliminate duplicate S/D pointers */
	      for (p1 = junk->breakList; p1 != NULL; p1 = p1->br_next)
	      {
	      	   if (p1->br_this->rn_why == RES_NODE_TRANSISTOR &&
		       (p1->br_loc.p_x == LEFT(tile) ||
		        p1->br_loc.p_x == RIGHT(tile)))
		   {
	      		p3 = NULL;
			p2 = junk->breakList;
			while ( p2 != NULL)
			{
			     if (p2->br_this == p1->br_this	&& 
			         p2 != p1			&&
				 p2->br_loc.p_x != LEFT(tile) &&
				 p2->br_loc.p_x != RIGHT(tile))
			     {
			     	  if (p3 == NULL)
				  {
				       junk->breakList = p2->br_next;
				       freeMagic((char *) p2);
				       p2 = junk->breakList;
				  }
				  else
				  {
				       p3->br_next = p2->br_next;
				       freeMagic((char *) p2);
				       p2 = p3->br_next;
				  }
			     }
			     else
			     {
			          p3 = p2;
				  p2 = p2->br_next;
			     }
			}
		   }
	      }
	      return(ResCalcEastWest(tile,pendingList,doneList,resList));
	 }
     }
}


/*
 *-------------------------------------------------------------------------
 *
 * ResDoContacts-- Add node (or nodes) for a contact.  If there are contact
 *	resistances, also add a resistor.
 *
 * Results: 
 *	None.
 *
 * Side Effects: Creates nodes and resistors
 *
 *-------------------------------------------------------------------------
 */

void
ResDoContacts(contact, nodes, resList)
    ResContactPoint	*contact;
    resNode		**nodes;
    resResistor	**resList;
{
    resNode	 *resptr;
    cElement	 *ccell;
    int		 tilenum, squaresx, squaresy, viawidth;
    int 	 minside, spacing, border;
    float	 squaresf;
    resResistor	 *resistor;
    resElement	 *element;
    static int	 too_small = 1;
     
    minside = CIFGetContactSize(contact->cp_type, &viawidth, &spacing, &border);

    if ((ExtCurStyle->exts_viaResist[contact->cp_type] == 0) || (viawidth == 0))
    {
	int x = contact->cp_center.p_x;
	int y = contact->cp_center.p_y;

	resptr = (resNode *) mallocMagic((unsigned) (sizeof(resNode)));
	InitializeNode(resptr,x,y,RES_NODE_CONTACT);
	ResAddToQueue(resptr,nodes);

 	ccell = (cElement *) mallocMagic((unsigned) (sizeof(cElement)));
	ccell->ce_nextc = resptr->rn_ce;
	resptr->rn_ce = ccell;
	ccell->ce_thisc = contact;

	/* add 1 celement for each layer of contact  */

	for (tilenum=0; tilenum < contact->cp_currentcontact; tilenum++)
	{
	    Tile	*tile = contact->cp_tile[tilenum];

	    contact->cp_cnode[tilenum] = resptr;
	    NEWBREAK(resptr, tile, contact->cp_center.p_x,
			contact->cp_center.p_y, &contact->cp_rect);
        }
    }
    else
    {
	if ((contact->cp_width < minside) || (contact->cp_height < minside))
	{
	    if (too_small)
	    {
		TxError("Warning: %s at %d %d smaller than extract section allows\n",
	            DBTypeLongNameTbl[contact->cp_type],
	            contact->cp_center.p_x, contact->cp_center.p_y);
	        too_small = 0;
	    }
	    squaresx = squaresy = 1;
	}
	else
	{
	    viawidth += spacing;
	    squaresf = (float)(contact->cp_width - minside) / (float)viawidth;
	    squaresf *= ExtCurStyle->exts_unitsPerLambda;
	    squaresf /= (float)viawidth;
	    squaresx = (int)squaresf;
	    squaresx++;

	    squaresf = (float)(contact->cp_height - minside) / (float)viawidth;
	    squaresf *= ExtCurStyle->exts_unitsPerLambda;
	    squaresf /= (float)viawidth;
	    squaresy = (int)squaresf;
	    squaresy++;
	}
        for (tilenum=0; tilenum < contact->cp_currentcontact; tilenum++)
	{
      	    int  x = contact->cp_center.p_x;
      	    int  y = contact->cp_center.p_y;
	    Tile *tile = contact->cp_tile[tilenum];

	    resptr = (resNode *) mallocMagic((unsigned) (sizeof(resNode)));
	    InitializeNode(resptr,x,y,RES_NODE_CONTACT);
	    ResAddToQueue(resptr,nodes);
	  
 	    /* add contact pointer to node  */

	    ccell = (cElement *) mallocMagic((unsigned) (sizeof(cElement)));
	    ccell->ce_nextc = resptr->rn_ce;
	    resptr->rn_ce = ccell;
	    ccell->ce_thisc = contact;
	       
	    contact->cp_cnode[tilenum] = resptr;
	    NEWBREAK(resptr, tile, contact->cp_center.p_x,
			contact->cp_center.p_y, &contact->cp_rect);

	    /* add resistors here */

	    if (tilenum > 0)
	    {
	        resistor = (resResistor *) mallocMagic((unsigned) (sizeof(resResistor)));
	        resistor->rr_nextResistor = (*resList);
	        resistor->rr_lastResistor = NULL;
	        if ((*resList) != NULL) (*resList)->rr_lastResistor = resistor;
	        (*resList) = resistor;
	        resistor->rr_connection1 = contact->cp_cnode[tilenum-1];
	        resistor->rr_connection2 = contact->cp_cnode[tilenum];
		    
	        element = (resElement *) mallocMagic((unsigned) (sizeof(resElement)));
	        element->re_nextEl = contact->cp_cnode[tilenum-1]->rn_re;
	        element->re_thisEl = resistor;
	        contact->cp_cnode[tilenum-1]->rn_re = element;
	        element = (resElement *) mallocMagic((unsigned)(sizeof(resElement)));
	        element->re_nextEl = contact->cp_cnode[tilenum]->rn_re;
	        element->re_thisEl = resistor;
	        contact->cp_cnode[tilenum]->rn_re = element;

		/* Need to figure out how to handle the multiple nodes	*/	
		/* and multiple resistors necessary to determine the 	*/
		/* correct geometry for the geometry extractor.  For	*/
		/* now, extract as one big glob.			*/
	
		/* rr_cl doesn't need to represent centerline; use for	*/
		/* # squares in y direction instead; use rr_width for	*/
		/* # squares in x direction.				*/

		resistor->rr_cl = squaresy;
		resistor->rr_width = squaresx;
		resistor->rr_value =
		    	ExtCurStyle->exts_viaResist[contact->cp_type] /
			(squaresx * squaresy);
#ifdef ARIEL
		resistor->rr_csArea =
		    	ExtCurStyle->exts_thick[contact->cp_type] /
			(squaresx * squaresy);
#endif
		resistor->rr_tt = contact->cp_type;
		resistor->rr_float.rr_area= 0;
		resistor->rr_status = 0;
	    }
	}
    }
}

/*
 *-------------------------------------------------------------------------
 *
 *-------------------------------------------------------------------------
 */

void
ResSortBreaks(masterlist, xsort)
    Breakpoint	**masterlist;
    int		xsort;
{
    Breakpoint	*p1, *p2, *p3, *p4;
    bool		changed;

    changed = TRUE;
    while (changed == TRUE)
    {
	changed = FALSE;
	p1 = NULL;
	p2 = *masterlist;
	p3 = p2->br_next;
	while (p3 != NULL)
	{
	    if (xsort == TRUE  && p2->br_loc.p_x > p3->br_loc.p_x ||
	           xsort == FALSE && p2->br_loc.p_y > p3->br_loc.p_y)
	    {
		changed = TRUE;
		if (p1 == NULL)
		{
		    *masterlist = p3;
		}
		else
		{
		    p1->br_next = p3;
		}
		p2->br_next = p3->br_next;
		p3->br_next = p2;
		p4 = p2;
		p2 = p3;
		p3 = p4;
	    }
	    else
	    {
	        p1 = p2;
	        p2 = p3;
	       	p3 = p3->br_next;
	    }
	}
    }
}
	
