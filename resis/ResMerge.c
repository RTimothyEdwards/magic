
#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/resis/ResMerge.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/malloc.h"
#include "textio/textio.h"
#include "resis/resis.h"

TileTypeBitMask	ResNoMergeMask[NT];

/* Forward declarations */

extern void ResMergeNodes();
extern void ResDeleteResPointer();
extern void ResEliminateResistor();
extern void ResCleanNode();
extern void ResFixBreakPoint();


/*
 *-------------------------------------------------------------------------
 *
 * ResDoneWithNode--After all connections to node are made, ResDoneWithNode
 *   is called. It checks for parallel, series, loop, triangle,
 *   and single conections, and simplifies the network where possible.
 *
 * Results: none
 *
 * Side Effects: deletes resistors and/or nodes.
 *
 *-------------------------------------------------------------------------
 */

void
ResDoneWithNode(resptr)
	resNode *resptr;

{
     int		status;
     resNode		*resptr2;
     resElement		*rcell1;
     resResistor	*rr1;
     
     resptr2 = NULL;
     resptr->rn_status |= TRUE;
     status = UNTOUCHED;

     /* are there any resistors? */
     
     if (resptr->rn_re == NULL)
     {
	  return;
     }

     /* Special handling for geometry option */

     if (ResOptionsFlags & ResOpt_Geometry) return;
     
     /* Eliminate resistors with connections to one terminal and */
     /* resistors with value 0.					 */
     
     for (rcell1 = resptr->rn_re; rcell1 != NULL; rcell1 = rcell1->re_nextEl)
     {
     	  rr1 = rcell1->re_thisEl;
	  if (rr1->rr_connection1 == rr1->rr_connection2)
	  {
	       ResDeleteResPointer(resptr,rr1);
	       ResDeleteResPointer(resptr,rr1);
	       resptr->rn_float.rn_area += rr1->rr_float.rr_area;
	       ResEliminateResistor(rr1,&ResResList);
	       status = LOOP;
	       ResDoneWithNode(resptr);
	       break;
  
	  }
	  else if (rr1->rr_value == 0)
	  {
	       ResDeleteResPointer(rr1->rr_connection1,rr1);
	       ResDeleteResPointer(rr1->rr_connection2,rr1);
		if (rr1->rr_connection1 == resptr)
		{
		     resptr2 = rr1->rr_connection2;
		}else
		{
		     resptr2 = rr1->rr_connection1;
		}
	        ResMergeNodes(resptr2,resptr,&ResNodeQueue,&ResNodeList);
		resptr2->rn_float.rn_area += rr1->rr_float.rr_area;
	       	ResEliminateResistor(rr1,&ResResList);
		if ((resptr2->rn_status & TRUE) == TRUE) 
		{
		     resptr2->rn_status &= ~TRUE;
		     ResDoneWithNode(resptr2);
		}
	       resptr2 = NULL;
	       status = SINGLE;
	       break;
	  }
     }

     /* Eliminations that can be only if there are no transistors connected */
     /* to node. Series and dangling connections fall in this group.	    */

     if ((resptr->rn_te == NULL) && (resptr->rn_why != RES_NODE_ORIGIN)
		&& (status == UNTOUCHED))
     {
     	  status = ResSeriesCheck(resptr);
     }
     if ((status == UNTOUCHED) && (resptr->rn_why != RES_NODE_ORIGIN))
     {
	  status = ResParallelCheck(resptr);
     }
     if ((status == UNTOUCHED) && (resptr->rn_why != RES_NODE_ORIGIN))
     {
	  status = ResTriangleCheck(resptr);   
     }
}


/*
 *------------------------------------------------------------------------
 *
 * ResFixRes--
 *
 * Results: none
 *
 * Side Effects: ResFixRes combines two resistors in series.  the second 
 * Resistor is eliminated.  Resptr is the node that is "cut out" of the
 * network.
 *
 *------------------------------------------------------------------------
 */

void
ResFixRes(resptr,resptr2,resptr3,elimResis,newResis)
	resNode *resptr,*resptr2,*resptr3;
	resResistor *elimResis, *newResis; 

{
     resElement  *thisREl;
     
     resptr3->rn_float.rn_area += newResis->rr_value*resptr->rn_float.rn_area/((float)(newResis->rr_value+elimResis->rr_value));
     resptr2->rn_float.rn_area += elimResis->rr_value*resptr->rn_float.rn_area/((float)(newResis->rr_value+elimResis->rr_value));
     newResis->rr_value += elimResis->rr_value;
     ASSERT(newResis->rr_value > 0,"series");
     newResis->rr_float.rr_area += elimResis->rr_float.rr_area;
#ifdef ARIEL
     if (elimResis->rr_csArea && elimResis->rr_csArea < newResis->rr_csArea || newResis->rr_csArea == 0)
     {
     	  newResis->rr_csArea = elimResis->rr_csArea;
	  newResis->rr_tt = elimResis->rr_tt;
     }
#endif
     for (thisREl = resptr3->rn_re; (thisREl != NULL); thisREl = thisREl->re_nextEl)
     {
     	  if (thisREl->re_thisEl == elimResis)
	  {
	       (thisREl->re_thisEl = newResis);
	       break;
	  }
	  	
     }
     if (thisREl == NULL) TxError("Resistor not found in duo\n");
     ResDeleteResPointer(resptr,elimResis);
     ResDeleteResPointer(resptr,newResis);
     ResEliminateResistor(elimResis, &ResResList);     
     ResCleanNode(resptr, TRUE,&ResNodeList,&ResNodeQueue);
}

/*
 *------------------------------------------------------------------------
 *
 * ResFixParallel--
 *
 * Results: none
 *
 * Side Effects: ResFixParallel combines two resistors in parallel. T
 *  	The second  Resistor is eliminated.  
 * 
 *
 *------------------------------------------------------------------------
 */

void
ResFixParallel(elimResis,newResis)
	resResistor *elimResis,*newResis;

{
     if ((newResis->rr_value+elimResis->rr_value) != 0)
     {
          newResis->rr_value = (((float) newResis->rr_value)*
	  			((float)elimResis->rr_value))/
     		            	((float)(newResis->rr_value+
					 elimResis->rr_value));
	  ASSERT(newResis->rr_value >= 0,"parallel");
     }
     else
     {
     	  newResis->rr_value =0;
     }
     newResis->rr_float.rr_area += elimResis->rr_float.rr_area;
#ifdef ARIEL
     newResis->rr_csArea += elimResis->rr_csArea;
#endif
     ResDeleteResPointer(elimResis->rr_connection1,elimResis);
     ResDeleteResPointer(elimResis->rr_connection2,elimResis);
     ResEliminateResistor(elimResis,&ResResList);
}

/*
 *-------------------------------------------------------------------------
 *
 * ResSeriesCheck -- for nodes with no transistors, sees if a series
 	or loop combination is possible.
 *
 * Results: returns SINGLE,LOOP,or SERIES if succesful.
 *
 * Side Effects: may delete some nodes and resistors.
 *
 *-------------------------------------------------------------------------
 */

int
ResSeriesCheck(resptr)
	resNode		*resptr;

{
     resResistor	*rr1,*rr2;
     resNode		*resptr2,*resptr3;
     int		status=UNTOUCHED;
     resElement		*res_next;

     rr1 = resptr->rn_re->re_thisEl;
     res_next = resptr->rn_re->re_nextEl;

     if (res_next == NULL)
     /* node with only one connection */
     {
	  resptr2 = (rr1->rr_connection1 == resptr)?rr1->rr_connection2:
	  					    rr1->rr_connection1;

	  ResDeleteResPointer(rr1->rr_connection1,rr1);
	  ResDeleteResPointer(rr1->rr_connection2,rr1);
	  resptr2->rn_float.rn_area += rr1->rr_float.rr_area+
	  				resptr->rn_float.rn_area;
	  ResEliminateResistor(rr1,&ResResList);
	  ResCleanNode(resptr,TRUE,&ResNodeList,&ResNodeQueue);
	  status = SINGLE;
	  if (resptr2->rn_status & TRUE)
	  {
		resptr2->rn_status &= ~TRUE;
	        ResDoneWithNode(resptr2);
	  }
	  resptr2 = NULL;
     }
     else if (res_next->re_nextEl == NULL)
     {
	   rr2 = res_next->re_thisEl;
	   if (!TTMaskHasType(ResNoMergeMask+rr1->rr_tt,rr2->rr_tt)) 
	   {
		if (rr1->rr_connection1 == resptr)
		{

		     if (rr2->rr_connection1 == resptr)
		     {
			  resptr2 = rr1->rr_connection2;
		     	  if (rr1->rr_connection2 ==
			      rr2->rr_connection2)
			  {
			       status = LOOP;
			       ResDeleteResPointer(rr1->rr_connection1,rr1);
			       ResDeleteResPointer(rr1->rr_connection2,rr1);
			       ResDeleteResPointer(rr2->rr_connection1,rr2);
			       ResDeleteResPointer(rr2->rr_connection2,rr2);
			       resptr2->rn_float.rn_area += rr1->rr_float.rr_area
					+ rr2->rr_float.rr_area
					+ resptr->rn_float.rn_area;
			       ResEliminateResistor(rr1,&ResResList);
			       ResEliminateResistor(rr2,&ResResList);
			       ResCleanNode(resptr,TRUE,&ResNodeList,&ResNodeQueue);
			  }else
			  {
			       status = SERIES;
			       resptr3 = rr2->rr_connection2;
			       rr1->rr_connection1 = rr2->rr_connection2;
			       ResFixRes(resptr,resptr2,resptr3,rr2,rr1);
			  }
			  if ((resptr2->rn_status & TRUE) == TRUE) 
			  {
			       resptr2->rn_status &= ~TRUE;
			       ResDoneWithNode(resptr2);
			  }
			  resptr2 = NULL;
		     }else
		     {
  		          resptr2 = rr1->rr_connection2;
		     	  if (rr1->rr_connection2 == rr2->rr_connection1)
			  {
			       status = LOOP;
			       ResDeleteResPointer(rr1->rr_connection1,rr1);
			       ResDeleteResPointer(rr1->rr_connection2,rr1);
			       ResDeleteResPointer(rr2->rr_connection1,rr2);
			       ResDeleteResPointer(rr2->rr_connection2,rr2);
	        	       resptr2->rn_float.rn_area += rr1->rr_float.rr_area
						+ rr2->rr_float.rr_area
						+ resptr->rn_float.rn_area;
			       ResEliminateResistor(rr1,&ResResList);
			       ResEliminateResistor(rr2,&ResResList);
			       ResCleanNode(resptr,TRUE,&ResNodeList,&ResNodeQueue);
			  }else
			  {
			       status = SERIES;
			       resptr3 = rr2->rr_connection1;
			       rr1->rr_connection1 = rr2->rr_connection1;
			       ResFixRes(resptr,resptr2,resptr3,rr2,rr1);
			  }
			  if ((resptr2->rn_status & TRUE) == TRUE) 
			  {
			      resptr2->rn_status &= ~TRUE;
			      ResDoneWithNode(resptr2);
			  }
			  resptr2 = NULL;
		     }
		}else
		{
		     if (rr2->rr_connection1 == resptr)
		     {
			  resptr2 = rr1->rr_connection1;
		     	  if (rr1->rr_connection1 == rr2->rr_connection2)
			  {
			       status = LOOP;
			       ResDeleteResPointer(rr1->rr_connection1,rr1);
			       ResDeleteResPointer(rr1->rr_connection2,rr1);
			       ResDeleteResPointer(rr2->rr_connection1,rr2);
			       ResDeleteResPointer(rr2->rr_connection2,rr2);
	        	       resptr2->rn_float.rn_area += rr1->rr_float.rr_area
						+ rr2->rr_float.rr_area
						+ resptr->rn_float.rn_area;
			       ResEliminateResistor(rr1,&ResResList);
			       ResEliminateResistor(rr2,&ResResList);
			       ResCleanNode(resptr,TRUE,&ResNodeList,&ResNodeQueue);
			  }else
			  {
			       status = SERIES;
			       resptr3 = rr2->rr_connection2;
			       rr1->rr_connection2 = 
			       rr2->rr_connection2;
			       ResFixRes(resptr,resptr2,resptr3,rr2,rr1);
			  }
			  if ((resptr2->rn_status & TRUE) == TRUE) 
			  {
			       resptr2->rn_status &= ~TRUE;
			       ResDoneWithNode(resptr2);
			  }
			  resptr2 = NULL;
		     }else
		     {
			  resptr2 = rr1->rr_connection1;
		     	  if (rr1->rr_connection1 == rr2->rr_connection1)
			  {
			       status = LOOP;
			       ResDeleteResPointer(rr1->rr_connection1,rr1);
			       ResDeleteResPointer(rr1->rr_connection2,rr1);
			       ResDeleteResPointer(rr2->rr_connection1,rr2);
			       ResDeleteResPointer(rr2->rr_connection2,rr2);
	        	       resptr2->rn_float.rn_area += rr1->rr_float.rr_area
						+ rr2->rr_float.rr_area
						+ resptr->rn_float.rn_area;
			       ResEliminateResistor(rr1,&ResResList);
			       ResEliminateResistor(rr2,&ResResList);
			       ResCleanNode(resptr,TRUE,&ResNodeList,&ResNodeQueue);
			  }else
			  {
			       status = SERIES;
			       resptr3 = rr2->rr_connection1;
			       rr1->rr_connection2 = 
			       rr2->rr_connection1;
			       ResFixRes(resptr,resptr2,resptr3,rr2,rr1);
			  }
			  if ((resptr2->rn_status & TRUE) == TRUE) 
			  {
			       resptr2->rn_status &= ~TRUE;
			       ResDoneWithNode(resptr2);
			  }
			  resptr2 = NULL;
		     }
		}
	  }
     }
     return status;
}


/*
 *-------------------------------------------------------------------------
 *
 * ResParallelCheck -- tries to do parallel combinations of transistors.
 *
 * Results: returns PARALLEL if successful
 *
 * Side Effects: may delete resistors and nodes.
 *
 *-------------------------------------------------------------------------
 */

int
ResParallelCheck(resptr)
	resNode	*resptr;

{
     resResistor	*r1,*r2;
     resNode		*resptr2,*resptr3;
     int		status=UNTOUCHED;
     resElement		*rcell1,*rcell2;

	
     for (rcell1 = resptr->rn_re; 
     		rcell1->re_nextEl != NULL; rcell1 = rcell1->re_nextEl)
     {
	   r1 = rcell1->re_thisEl;

	   for (rcell2 = rcell1->re_nextEl; 
	   	rcell2 != NULL; rcell2 = rcell2->re_nextEl)
	       
	   {
	        r2 = rcell2->re_thisEl;
		if (TTMaskHasType(ResNoMergeMask+r1->rr_tt,r2->rr_tt)) continue;
	        if (((r1->rr_connection1 == r2->rr_connection1) &&
	             (r1->rr_connection2 == r2->rr_connection2))||
	            ((r1->rr_connection1 == r2->rr_connection2) &&
	             (r1->rr_connection2 == r2->rr_connection1)))
	        {
	             resptr3 = (r1->rr_connection1 == resptr) ? 
		     		r1->rr_connection2 :  r1->rr_connection1;
		     ResFixParallel(r1,r2);
	             status = PARALLEL;
		     resptr2 = NULL;
		     if (resptr3->rn_status & TRUE)
		     {
		           resptr2 = resptr3;
		           resptr2->rn_status &= ~TRUE;
		     }
		     ResDoneWithNode(resptr);
		     if (resptr2 != NULL) ResDoneWithNode(resptr2);
	             break;
	       }
	  }
	  if (status == PARALLEL) break;
     }
     return status;
}


/*
 *-------------------------------------------------------------------------
 *
 * ResTriangleCheck -- looks for places to do the traingle-to-Y conversion.
 *
 * Results: returns TRIANGLE if successful.
 *
 * Side Effects: may allocate a new node.
 *
 *-------------------------------------------------------------------------
 */

int
ResTriangleCheck(resptr)
	resNode		*resptr;

{
     resResistor	*rr1,*rr2,*rr3;
     int		status=UNTOUCHED;
     float		r1,r2,r3,denom;
     resNode		*n1,*n2,*n3;
     resElement		*rcell1,*rcell2,*rcell3,*element;

     for (rcell1 = resptr->rn_re; 
     		rcell1->re_nextEl != NULL; rcell1 = rcell1->re_nextEl)
     {
          rr1 = rcell1->re_thisEl;
	  n1 = (rr1->rr_connection1 == resptr)?rr1->rr_connection2:
	  				       rr1->rr_connection1;

          for (rcell2 = rcell1->re_nextEl; 
	  	rcell2 != NULL; rcell2 = rcell2->re_nextEl)
	  {
	       rr2 = rcell2->re_thisEl;
	       if (TTMaskHasType(ResNoMergeMask + rr1->rr_tt, rr2->rr_tt))
		   continue;
	       n2 = (rr2->rr_connection1 == resptr) ? rr2->rr_connection2 :
	       					    rr2->rr_connection1;
	       for (rcell3 = n1->rn_re; 
	       		rcell3 != NULL; rcell3 = rcell3->re_nextEl)
	       {
		    rr3 = rcell3->re_thisEl;
		    if (TTMaskHasType(ResNoMergeMask+rr1->rr_tt,rr3->rr_tt)) 
		    		continue;
		    if (TTMaskHasType(ResNoMergeMask+rr2->rr_tt,rr3->rr_tt)) 
		    		continue;

		    if (((rr3->rr_connection1 != n1) ||
		         (rr3->rr_connection2 != n2)) &&
		        ((rr3->rr_connection2 != n1) ||
		         (rr3->rr_connection1 != n2))) continue;

		    status = TRIANGLE;
		    if ((denom=rr1->rr_value+rr2->rr_value+rr3->rr_value) != 0.0)
		    {
			 denom = 1.0/denom;
			 /*calculate new values for resistors */
			 r1 = (((float) rr1->rr_value)*
			            ((float) rr2->rr_value))*denom;

			 r2 = (((float) rr2->rr_value)*
			            ((float) rr3->rr_value))*denom;

			 r3 = (((float) rr1->rr_value)*
			       ((float) rr3->rr_value))*denom;

			 rr1->rr_value = r1+0.5;
			 rr2->rr_value = r2+0.5;
			 rr3->rr_value = r3+0.5;
			 ASSERT(rr1->rr_value >= 0,"Triangle");
			 ASSERT(rr2->rr_value >= 0,"Triangle");
			 ASSERT(rr3->rr_value >= 0,"Triangle");
		    }
	            else
		    {
			  rr1->rr_value = 0;
			  rr2->rr_value = 0;
			  rr3->rr_value = 0;
	            }
		    n3 = (resNode  *) mallocMagic((unsigned) (sizeof(resNode)));
	      	    /*  Where should the new node be `put'? It */
	            /* is arbitrarily assigned to the location */
		    /* occupied by the first node.		 */

		    InitializeNode(n3,resptr->rn_loc.p_x,resptr->rn_loc.p_y,TRIANGLE);
		    n3->rn_status = FINISHED | TRUE | MARKED;

		    n3->rn_less = NULL;
		    n3->rn_more = ResNodeList;
		    ResNodeList->rn_less = n3;
		    ResNodeList = n3;
		    if (resptr == rr1->rr_connection1)
		    {
		    	   ResDeleteResPointer(rr1->rr_connection2,rr1);
			   rr1->rr_connection2 = n3;
		    }
		    else
		    {
		      	   ResDeleteResPointer(rr1->rr_connection1,rr1);
			   rr1->rr_connection1 = n3;
		    }
		    if (n2 == rr2->rr_connection1)
		    {
		     	   ResDeleteResPointer(rr2->rr_connection2,rr2);
			   rr2->rr_connection2 = n3;
		    }
		    else
		    {
		    	   ResDeleteResPointer(rr2->rr_connection1,rr2);
			   rr2->rr_connection1 = n3;
		    }
		    if (n1 == rr3->rr_connection1)
		    {
		      	   ResDeleteResPointer(rr3->rr_connection2,rr3);
			   rr3->rr_connection2 = n3;
		    }
		    else
		    {
		     	   ResDeleteResPointer(rr3->rr_connection1,rr3);
			   rr3->rr_connection1 = n3;
		    }
		    element = (resElement *) mallocMagic((unsigned)(sizeof(resElement)));
		    element->re_nextEl = NULL;
		    element->re_thisEl = rr1;
		    n3->rn_re = element;
		    element = (resElement *) mallocMagic((unsigned)(sizeof(resElement)));
		    element->re_nextEl = n3->rn_re;
		    element->re_thisEl = rr2;
		    n3->rn_re = element;
		    element = (resElement *) mallocMagic((unsigned)(sizeof(resElement)));
		    element->re_nextEl = n3->rn_re;
		    element->re_thisEl = rr3;
		    n3->rn_re = element;
	      	    if ((n1->rn_status & TRUE) == TRUE)
	            {
		      	   n1->rn_status &= ~TRUE;
		    }
		    else
		    {
		      	   n1 = NULL;
		    }
		    if ((n2->rn_status & TRUE) == TRUE)
		    {
		    	   n2->rn_status &= ~TRUE;
		    }
		    else
		    {
		     	   n2 = NULL;
		    }
		    ResDoneWithNode(resptr);
		    if (n1 != NULL) ResDoneWithNode(n1);
		    if (n2 != NULL) ResDoneWithNode(n2);
		    break;
	       }
	       if (status == TRIANGLE) break;
	  }
	  if (status == TRIANGLE) break;
     }
     return status;
}

/* 
 *--------------------------------------------------------------------------
 *
 * ResMergeNodes--
 *
 * results: none
 *
 * side effects: appends all the cElement, jElement, tElement and
 *       resElement structures from node 2 onto node 1.  Node 2 is
 *	 then eliminated.
 *
 *---------------------------------------------------------------------------
 */

void
ResMergeNodes(node1,node2,pendingList,doneList)
 	resNode *node1,*node2,**pendingList,**doneList;
	
 {
      resElement	*workingRes,*tRes;
      tElement		*workingFet,*tFet;
      jElement		*workingJunc,*tJunc;
      cElement		*workingCon,*tCon;
      Tile		*tile;
      int		i;
      
      /* sanity check  */
      if (node1 == node2) return;
      if (node1 == NULL || node2 == NULL)
      {
      	   TxError("Attempt to merge NULL node\n");
	   return;
      }
      
      /* don't want to merge away startpoint */
      if (node2->rn_why & RES_NODE_ORIGIN)
      {
      	   node1->rn_why = RES_NODE_ORIGIN;
      }

      /* set node resistance */
      if (node1->rn_noderes >node2->rn_noderes)
      {
	   node1->rn_noderes = node2->rn_noderes;
	   if ((node1->rn_status & FINISHED) != FINISHED)
	   {
	   	ResRemoveFromQueue(node1,pendingList);
		ResAddToQueue(node1,pendingList);
	   }
      }
      node1->rn_float.rn_area += node2->rn_float.rn_area;
      
      
      /* combine relevant flags */
      node1->rn_status |= (node2->rn_status & RN_MAXTDI);

      /*merge transistor lists */
      workingFet = node2->rn_te;
      while (workingFet != NULL)
      {
      	   if (workingFet->te_thist->rt_status & RES_TRAN_PLUG)
	   {
	       ResPlug	*plug = (ResPlug *) workingFet->te_thist;
	       if (plug->rpl_node == node2)
	       {
	       	    plug->rpl_node = node1;
	       }
	       else
	       {
	       	    TxError("Bad plug node: is (%d %d), should be (%d %d)\n",
		    	    plug->rpl_node->rn_loc,node2->rn_loc);
	       	    plug->rpl_node = NULL;
	       }
	   }
	   else
	   {
	        
		int j;

		for (j=0;j!= RT_TERMCOUNT;j++)
		if (workingFet->te_thist->rt_terminals[j] == node2)
	        {
	   	     workingFet->te_thist->rt_terminals[j] = node1;
	        }
	   }
	   tFet = workingFet;
	   workingFet = workingFet->te_nextt;
	   tFet->te_nextt = node1->rn_te;
	   node1->rn_te = tFet;
      }

      /* append junction lists */
      workingJunc = node2->rn_je;
      while (workingJunc != NULL)
      {
	   tJunc = workingJunc;
	   for (i=0; i<TILES_PER_JUNCTION; i++)
	   {
	        tileJunk *junk;

		tile =tJunc->je_thisj->rj_Tile[i];
		junk = (tileJunk *) tile->ti_client;

	   	if ((junk->tj_status & RES_TILE_DONE) == FALSE)
		{
		     ResFixBreakPoint(&junk->breakList,node2,node1);
		}
	   }
           tJunc->je_thisj->rj_jnode = node1;
	   workingJunc = workingJunc->je_nextj;
	   tJunc->je_nextj = node1->rn_je;
	   node1->rn_je = tJunc;
      }
      
      /* Append connection lists */
      workingCon = node2->rn_ce;
      while (workingCon != NULL)
      {
	   tCon = workingCon;
	   for (i=0; i <workingCon->ce_thisc->cp_currentcontact;i++)
	   {
	   	if (workingCon->ce_thisc->cp_cnode[i] == node2)
		{
	             tileJunk *junk;

		     workingCon->ce_thisc->cp_cnode[i] = node1;
	             tile =tCon->ce_thisc->cp_tile[i];
		     junk = (tileJunk *) tile->ti_client;
	   	     if ((junk->tj_status & RES_TILE_DONE) == FALSE)
		     {
		          ResFixBreakPoint(&junk->breakList,node2,node1);
		     }
		}
	   }
	   workingCon = workingCon->ce_nextc;
	   tCon->ce_nextc = node1->rn_ce;
	   node1->rn_ce = tCon;
      }
      
      /* Moves resistors to new node  */
      workingRes = node2->rn_re;
      while (workingRes != NULL)
      {
	   if (workingRes->re_thisEl->rr_connection1 == node2)
	   {
		     workingRes->re_thisEl->rr_connection1 = node1;
	   }
      	   else if (workingRes->re_thisEl->rr_connection2 == node2)
	   {
		     workingRes->re_thisEl->rr_connection2 = node1;
	   }
	   else
	   {
	   	TxError("Resistor not found.\n");
	   }
	   tRes = workingRes;
	   workingRes = workingRes->re_nextEl;
	   tRes->re_nextEl = node1->rn_re;
	   node1->rn_re = tRes;
      }
      if ((node2->rn_status & FINISHED) == FINISHED)
      {
      	   ResRemoveFromQueue(node2,doneList);
      }
      else
      {
      	   ResRemoveFromQueue(node2,pendingList);
      }
      if (node2->rn_client != (ClientData)NULL)
      {
	    freeMagic((char *)node2->rn_client);
	    node2->rn_client = (ClientData)NULL;
      }
      {
	    node2->rn_re = (resElement *) CLIENTDEFAULT;
	    node2->rn_ce = (cElement   *) CLIENTDEFAULT;
	    node2->rn_je = (jElement   *) CLIENTDEFAULT;
	    node2->rn_te = (tElement   *) CLIENTDEFAULT;
	    node2->rn_more = (resNode   *) CLIENTDEFAULT;
	    node2->rn_less = (resNode   *) CLIENTDEFAULT;
      }
      freeMagic((char *)node2);
 }


/*
 *-------------------------------------------------------------------------
 *
 * ResDeleteResPointer-- Deletes the pointer from a node to a resistor.
 *	Used when a resistor is deleted.
 *
 * Results:none
 *
 * Side Effects: Modifies a node's resistor list.
 *
 *-------------------------------------------------------------------------
 */

void
ResDeleteResPointer(node,resistor)
	resNode		*node;
	resResistor	*resistor;

{
     resElement *rcell1,*rcell2;
     int	notfound=TRUE;
     
     rcell1 = NULL;
     rcell2 = node->rn_re;
     while (rcell2 != NULL)
     {
     	  if (rcell2->re_thisEl == resistor)
	  {
	       notfound=FALSE;
	       if (rcell1 != NULL)
	       {
	       	    rcell1->re_nextEl = rcell2->re_nextEl;
	       }else
	       {
	       	    node->rn_re = rcell2->re_nextEl;
	       }
	       /* Set fields to null just in case there are any stray */
	       /* pointers to structure.			      */
	       rcell2->re_thisEl = NULL;
	       rcell2->re_nextEl = NULL;
	       freeMagic((char *)rcell2);
	       break;
	  }
	  rcell1 = rcell2;
	  rcell2 = rcell2->re_nextEl;
     }
     if (notfound)
     {
     	  TxError("Missing rptr at (%d %d).\n",
	  		node->rn_loc.p_x,node->rn_loc.p_y);
     }
}


/*
 *-------------------------------------------------------------------------
 *
 * ResEliminateResistor--
 *
 * Results:none
 *
 * Side Effects: Deletes a resistor. Does not delete pointers from nodes to
 *	resistor.
 *
 *-------------------------------------------------------------------------
 */

void
ResEliminateResistor(resistor,homelist)
	resResistor *resistor,**homelist;

{
     if (resistor->rr_lastResistor == NULL)
     {
     	  *homelist = resistor->rr_nextResistor;
     }else
     {
     	  resistor->rr_lastResistor->rr_nextResistor = resistor->rr_nextResistor;
     }
     if (resistor->rr_nextResistor != NULL)
     {
     	  resistor->rr_nextResistor->rr_lastResistor = resistor->rr_lastResistor;
     }

     /* set everything to null so that any stray pointers will cause */
     /* immediate death instead of a slow lingering one.	     */
     resistor->rr_nextResistor = NULL;
     resistor->rr_lastResistor = NULL;
     resistor->rr_connection1 = NULL;
     resistor->rr_connection2 = NULL;
     freeMagic((char *)resistor);
}


/*
 *-------------------------------------------------------------------------
 *
 * ResCleanNode--removes the linked lists of junctions and contacts after
 *		they are no longer needed. If the 'junk' option is used,
 *		the node is eradicated.
 *
 * Results:
 *	None.
 *
 * Side Effects: frees memory
 *
 *-------------------------------------------------------------------------
 */

void
ResCleanNode(resptr,junk,homelist1,homelist2)
	resNode *resptr;
	int 	junk;
        resNode **homelist1;
        resNode **homelist2;
{
     resElement *rcell;
     cElement *ccell;
     jElement *jcell;
     tElement *tcell;
     
     /* free up contact and junction lists */
     while (resptr->rn_ce != NULL)
     {
	     ccell = resptr->rn_ce;
	     resptr->rn_ce = resptr->rn_ce->ce_nextc;
	     freeMagic((char *)ccell);
     }
     while (resptr->rn_je != NULL)
     {
	     jcell = resptr->rn_je;
	     resptr->rn_je = resptr->rn_je->je_nextj;
	     freeMagic((char *)jcell->je_thisj);
	     freeMagic((char *)jcell);
     }
     if (junk == TRUE)
     {
	  if (resptr->rn_client != (ClientData)NULL)
	  {
	       freeMagic((char *)resptr->rn_client);
	       resptr->rn_client = (ClientData)NULL;
	  }
	  while (resptr->rn_te != NULL)
          {
	       tcell = resptr->rn_te;
	       resptr->rn_te = resptr->rn_te->te_nextt;
	       freeMagic((char *)tcell);
          }
          while (resptr->rn_re != NULL)
          {
	       rcell = resptr->rn_re;
	       resptr->rn_re = resptr->rn_re->re_nextEl;
	       freeMagic((char *)rcell);
          }
	  if (resptr->rn_less != NULL)
	  {
	       resptr->rn_less->rn_more = resptr->rn_more;
	  }else
	  {
	       if (*homelist1 == resptr)
	       {
	            *homelist1 = resptr->rn_more;
	       }
	       else if (*homelist2 == resptr)
	       {
	            *homelist2 = resptr->rn_more;
	       }
	       else
	       {
	       	    TxError("Error: Attempted to eliminate node from wrong list.\n");
	       }
	  }
	  if (resptr->rn_more != NULL) 
	  {
	       resptr->rn_more->rn_less = resptr->rn_less;
	  }
	  
	  {
	       resptr->rn_re = (resElement *) CLIENTDEFAULT;
	       resptr->rn_ce = (cElement   *) CLIENTDEFAULT;
	       resptr->rn_je = (jElement   *) CLIENTDEFAULT;
	       resptr->rn_te = (tElement   *) CLIENTDEFAULT;
	       resptr->rn_more = (resNode   *) CLIENTDEFAULT;
	       resptr->rn_less = (resNode   *) CLIENTDEFAULT;
	  }
	  freeMagic((char *)resptr);
     }
}


/*
 *-------------------------------------------------------------------------
 *
 * ResFixBreakPoint--moves breakpoints from one node to another, checking
 *     first to see whether the target node already has the breakpoint.
 *     Used when nodes are merged.
 *
 * Results: none
 *
 * Side Effects: may free up memory if breakpoint is already present.
 *
 *-------------------------------------------------------------------------
 */

void
ResFixBreakPoint(sourcelist,origNode,newNode)
	Breakpoint	**sourcelist;
	resNode		*origNode,*newNode;

{
     Breakpoint		*bp,*bp2,*bp3,*bp4;
     int		alreadypresent;
     
     alreadypresent = FALSE;
     for (bp4 = *sourcelist; bp4 != NULL; bp4 = bp4->br_next)
     {
     	  if (bp4->br_this == newNode) 
	  {
	       alreadypresent = TRUE;
	       break;
	  }
     }
     bp2 = NULL;
     bp = *sourcelist;
     while (bp != NULL)
     {
     	  if (bp->br_this == origNode) 
	  {
	       if (alreadypresent)
	       {
	       	    if (bp2 == NULL)
		    {
		    	 *sourcelist = bp->br_next;
		    }else
		    {
		    	 bp2->br_next = bp->br_next;
		    }
		    bp3 = bp;
		    bp = bp->br_next;

		    if (bp3->br_crect != NULL &&  bp4->br_crect == NULL)
		    {
			 bp4->br_crect = bp3->br_crect;
		    }
		    freeMagic((char *)bp3);
		    continue;
	       }else
	       {
	       	    (bp->br_this = newNode);
	       }
	  }
	  bp2 = bp;
	  bp = bp->br_next;
     }
     
}
