/*
 *-------------------------------------------------------------------------
 *
 * ResSimplify -- contains routines used to simplify signal nets.
 *
 *
 *
 *-------------------------------------------------------------------------
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/resis/ResSimple.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "utils/heap.h"
#include "database/database.h"
#include "utils/malloc.h"
#include "textio/textio.h"
#include "extract/extract.h"
#include "extract/extractInt.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/stack.h"
#include "utils/tech.h"
#include "textio/txcommands.h"
#include	"resis/resis.h"

#define MILLIOHMSPEROHM 1000

/* Forward declarations */
void ResSetPathRes();
void resPathNode();
void resPathRes();
Heap	ResistorHeap;
int	resRemoveLoops = FALSE;

/* Forward declarations */

extern void ResMoveTransistors();
extern void ResAddResistorToList();



/*
 *-------------------------------------------------------------------------
 *
 * ResSimplifyNet- Reduces complete (?) net produced by ResProcessTiles into
 *    something a little less chaotic.
 *
 * Results: none
 *
 * Side Effects:  Can eliminate nodes and resistors, and move transistors from
 *		one node to another.
 *
 *-------------------------------------------------------------------------
 */

void
ResSimplifyNet(nodelist,biglist,reslist,tolerance)
	resNode		**nodelist,**biglist;
	resResistor	**reslist;
	float		tolerance;

{
     resElement     *resisptr;
     resNode        *node,*otherNode,*node1,*node2;
     resResistor    *resistor1 = NULL,*resistor2 = NULL;
     int             numdrive = 0, numreceive = 0;
     int		MarkedReceivers,UnMarkedReceivers,NumberOfDrivers,PendingReceivers;

     if (*nodelist == NULL) return;
     node = *nodelist;
     node->rn_status |= MARKED | FINISHED;
     *nodelist = node->rn_more;
     if (node->rn_more != NULL)
     {
          node->rn_more->rn_less = (resNode *) NULL;
     }
     node->rn_more = *biglist;
     if (*biglist != (resNode *) NULL)
     {
     	  (*biglist)->rn_less = node;
     }
     *biglist = node;

     /* 
        Walk though resistors. Mark uninitialized ones and assign them
        A direction. Keep track of the number of resistors pointing in
	each direction.
     */
     for (resisptr = node->rn_re; resisptr != NULL; resisptr = resisptr->re_nextEl) 
     {
     	if (((resisptr->re_thisEl->rr_status & RES_MARKED) == RES_MARKED) &&
     	    (resisptr->re_thisEl->rr_connection2 == node)) 
	{
		     if (resistor1 == NULL)
		     {
		          resistor1 = resisptr->re_thisEl;
		     }
		     else
		     {
		     	  resistor2 = resisptr->re_thisEl;
		     }
		     numdrive++;
	} 
	else 
	{
	     /* 
	        Resistor direction is from node1 to node2. If the resistor
	        is not marked, mark it and make sure the direction is
		set properly.
	     */
	     
	     if ((resisptr->re_thisEl->rr_status & RES_MARKED) != RES_MARKED)	     
	     {
	     	  if (resisptr->re_thisEl->rr_connection2 == node)
		  {
		       resisptr->re_thisEl->rr_connection2 = resisptr->re_thisEl->rr_connection1;
		       resisptr->re_thisEl->rr_connection1 = node;
		  }
		  resisptr->re_thisEl->rr_status |= RES_MARKED;
	     }
	     if (resistor1 == NULL)
	     {
	          resistor1 = resisptr->re_thisEl;
	     }
	     else		     
	     {
	     	  resistor2 = resisptr->re_thisEl;
	     }
	     numreceive++;
	}
     }

     /* 
        Is the node reached by one resistor? If it is, check the resistor's
        other end.  Check the number of drivers at the other end. If it is
	more than 1, delete the current resistor to break the deadlock.
     */
     if (numreceive == 0 && numdrive == 1 && node->rn_why != RES_NODE_ORIGIN)
     {
	resistor1->rr_status |= RES_DEADEND;
	if (resistor1->rr_value < tolerance)
	{
	     otherNode = (resistor1->rr_connection1 == node) ? resistor1->rr_connection2 : resistor1->rr_connection1;
	     MarkedReceivers = 0;
	     UnMarkedReceivers = 0;
	     NumberOfDrivers = 0;
	     PendingReceivers = 0;
	     resistor2 = resistor1;
	     for (resisptr = otherNode->rn_re; resisptr != NULL; resisptr=resisptr->re_nextEl)
	     {
	     	  if (resisptr->re_thisEl->rr_connection1 == otherNode)
		  {
		       if ((resisptr->re_thisEl->rr_connection2->rn_status & MARKED) != MARKED)
		       {
		       	    PendingReceivers++;
		       }
		       if (resisptr->re_thisEl->rr_status & RES_DEADEND || 
		           resisptr->re_thisEl->rr_value > tolerance)
		       {
		       	    MarkedReceivers++;
			    resistor2 = (resisptr->re_thisEl->rr_value >= resistor2->rr_value) ? resisptr->re_thisEl : resistor2;
		       }
		       else
		       {
		       	    UnMarkedReceivers++;
		       }
		  }
		  else
		  {
		       NumberOfDrivers++;
		  }
	     }
	     /* other recievers at far end? If so, reschedule other node;
	        deadlock will be settled from that node.
	     */
	     if ((MarkedReceivers+UnMarkedReceivers+NumberOfDrivers == 2) ||
	        (UnMarkedReceivers == 0 && MarkedReceivers > 1 && resistor2 == resistor1 && PendingReceivers == 0))
	     {
	     	  if (otherNode->rn_status & MARKED)
		  {
		       otherNode->rn_status &= ~MARKED;
		       ResRemoveFromQueue(otherNode,biglist);
		       otherNode->rn_less= NULL;
		       otherNode->rn_more = *nodelist;
		       if (*nodelist != NULL)
		       {
		       	    (*nodelist)->rn_less = otherNode;
		       }
		       *nodelist = otherNode;
		  }
		  return;
	     }

	     /* 
	        Break loop here. More than one driver indicates a loop; 
	        remove deadend, allowing drivers to be merged
	     */
	     else if (UnMarkedReceivers == 0 && (MarkedReceivers == 1 && NumberOfDrivers > 1 || resistor2 != resistor1))
	     
	     {
		  otherNode->rn_float.rn_area += resistor1->rr_float.rr_area;
		  otherNode->rn_status &= ~RES_DONE_ONCE;
	          ResDeleteResPointer(resistor1->rr_connection1,resistor1);
	          ResDeleteResPointer(resistor1->rr_connection2,resistor1);
	          ResEliminateResistor(resistor1,reslist);
	          ResMergeNodes(otherNode,node,nodelist,biglist);
	          if (otherNode->rn_status & MARKED)
	          {
	               otherNode->rn_status &= ~MARKED;
	               ResRemoveFromQueue(otherNode,biglist);
	               otherNode->rn_less= NULL;
		       otherNode->rn_more = *nodelist;
		       if (*nodelist != NULL)
		       {
		            (*nodelist)->rn_less = otherNode;
		       }
		       *nodelist = otherNode;
	          }
	          ResDoneWithNode(otherNode); 
	     }
	}
        
     }
     /* 
         Two resistors in series? Combine them and move transistors to 
        appropriate end.
     */
     else if (numdrive+numreceive == 2 &&
             (resistor1->rr_value < tolerance && resistor2->rr_value < tolerance))
     {
	  if ((resistor1->rr_status & RES_MARKED) == 0 && (resistor1->rr_connection2 == node))
	  {
	       resistor1->rr_connection2 = resistor1->rr_connection1;
	       resistor1->rr_connection1 = node;
	  }
	  resistor1->rr_status |= RES_MARKED;
	  if ((resistor2->rr_status & RES_MARKED) == 0 && (resistor2->rr_connection2 == node))
	  {
	       resistor2->rr_connection2 = resistor2->rr_connection1;
	       resistor2->rr_connection1 = node;
	  }
	  resistor2->rr_status |= RES_MARKED;
	  node1 = (resistor1->rr_connection1 == node) ? resistor1->rr_connection2 : resistor1->rr_connection1;
	  node2 = (resistor2->rr_connection1 == node) ? resistor2->rr_connection2 : resistor2->rr_connection1;
	  otherNode = (resistor1->rr_status & RES_DEADEND && 
	  	       resistor1->rr_value  < tolerance / 2) ||
	  	      ((resistor2->rr_status & RES_DEADEND) == 0 && 
		      resistor1->rr_value < resistor2->rr_value) ? node1 : node2;

     	  /* 
	     make one big resistor out of two little ones, eliminating
	     the current node.  Transistors connected to this node are
	     moved to either end depending on their resistance.
	  */
	  ResMoveTransistors(node,otherNode);
          otherNode->rn_noderes = MIN(node->rn_noderes,otherNode->rn_noderes);
          node2->rn_float.rn_area += resistor1->rr_value*node->rn_float.rn_area/(resistor1->rr_value+resistor2->rr_value);
          node1->rn_float.rn_area += resistor2->rr_value*node->rn_float.rn_area/(resistor1->rr_value+resistor2->rr_value);
	  resistor1->rr_value += resistor2->rr_value;
	  resistor1->rr_float.rr_area +=resistor2->rr_float.rr_area;
	  if (resistor1 == *reslist)
	  {
	       *reslist = resistor1->rr_nextResistor;
	  }
	  else
	  {
	       resistor1->rr_lastResistor->rr_nextResistor = resistor1->rr_nextResistor;
	  }
	  if (resistor1->rr_nextResistor != NULL)
	  {
	       resistor1->rr_nextResistor->rr_lastResistor = resistor1->rr_lastResistor;
	  }
	  ResAddResistorToList(resistor1,reslist);
	  ResDeleteResPointer(node,resistor1);
	  ResDeleteResPointer(node,resistor2);
	  ResDeleteResPointer(node2,resistor2);
	  if (resistor1->rr_connection1 == node)
	  {
	       resistor1->rr_connection1 = node2;
	  }
	  else
	  {
	       resistor1->rr_connection2 = node2;
	  }
          resisptr = (resElement *) mallocMagic((unsigned)(sizeof(resElement)));
          resisptr->re_thisEl = resistor1;
          resisptr->re_nextEl = node2->rn_re;
          node2->rn_re = resisptr;
	  ResEliminateResistor(resistor2,reslist);
	  otherNode->rn_status |= (node->rn_status & RN_MAXTDI);
	  ResCleanNode(node,TRUE,biglist,nodelist);
	  node1->rn_status &= ~RES_DONE_ONCE;
	  if (node1->rn_status & MARKED) 
	  {
	     node1->rn_status &= ~MARKED;
	     ResRemoveFromQueue(node1,biglist);
	     node1->rn_less= NULL;
	     node1->rn_more = *nodelist;
	     if (*nodelist != NULL)
 	     {
	     	  (*nodelist)->rn_less = node1;
	     }
	     *nodelist = node1;
	  }
	  node2->rn_status &= ~RES_DONE_ONCE;
	  if (node2->rn_status & MARKED) 
	  {
	     node2->rn_status &= ~MARKED;
	     ResRemoveFromQueue(node2,biglist);
	     node2->rn_less= NULL;
	     node2->rn_more = *nodelist;
	     if (*nodelist != NULL)
	     {
	     	  (*nodelist)->rn_less = node2;
	     }
	     *nodelist = node2;
	  }
	  ResDoneWithNode(node1); 
     }

     /* 
        Last resort- keep propagating down the tree.  To avoid looping,
        mark each node when it is reached. Don't reschedule node if 
	none of the connections to it have changed since it was marked
     */
     else if (numreceive > 0 && (node->rn_status & RES_DONE_ONCE) == 0)
     {
          node->rn_status |= RES_DONE_ONCE;
	  for (resisptr = node->rn_re; resisptr != NULL; resisptr = resisptr->re_nextEl) 
	  {
		if (resisptr->re_thisEl->rr_connection1 == node)
		{
		     	  /* 
			     elements with a resistance greater than the
			     tolerance should only be propagated past once-
			     loops may occur otherwise.
			   */
			  if (resisptr->re_thisEl->rr_status & RES_DONE_ONCE)
			  {
			       continue;
			  }
			  if (resisptr->re_thisEl->rr_connection2->rn_status & MARKED)
			  {
			       /* 
			          mark big resistors so we only process them 
			          once.
			       */
			       if (resisptr->re_thisEl->rr_value > tolerance)
			       {
			       	    resisptr->re_thisEl->rr_status |= RES_DONE_ONCE;
			       }
			       resisptr->re_thisEl->rr_connection2->rn_status &= ~MARKED;
			       ResRemoveFromQueue(resisptr->re_thisEl->rr_connection2,biglist);
	     		       resisptr->re_thisEl->rr_connection2->rn_less= NULL;
			       resisptr->re_thisEl->rr_connection2->rn_more = *nodelist;
			       if (*nodelist != NULL)
			       {
			       	    (*nodelist)->rn_less = resisptr->re_thisEl->rr_connection2;
			       }
			       *nodelist = resisptr->re_thisEl->rr_connection2;
			  }
		}
	  }
     }
}



/*
 *-------------------------------------------------------------------------
 *
 * ResMoveTransistors-- move transistors from one node1 to node2
 *
 * Results: none
 *
 * Side Effects: Changes transistor connections and node tElements.
 *
 *-------------------------------------------------------------------------
 */

void
ResMoveTransistors(node1,node2)
	resNode		*node1,*node2;

{
     tElement		*tranptr,*oldptr;
     resTransistor	*transistor;
     
     tranptr = node1->rn_te;
     while (tranptr != NULL)
     {
	  transistor = tranptr->te_thist;
	  oldptr = tranptr;
	  tranptr = tranptr->te_nextt;
	  if (transistor->rt_status & RES_TRAN_PLUG)
	  {
	       if (((ResPlug *)(transistor))->rpl_node == node1)
	       {
	       	    ((ResPlug *)(transistor))->rpl_node = node2;
	       }
	       else
	       {
	       	    TxError("Bad node connection in plug\n");
	       }
	  }
	  else
	  {
	       if (transistor->rt_gate == node1)
	       {
   	            transistor->rt_gate = node2;
	       }
	       else	if (transistor->rt_source == node1)
	       {
	            transistor->rt_source = node2;
	       }
	       else if (transistor->rt_drain == node1)
	       {
  	            transistor->rt_drain = node2;
	       }
	       else
	       {
	            TxError("Missing Transistor connection in squish routines at %d, %d\n",node1->rn_loc.p_x,node1->rn_loc.p_y);
	       }
	  }
	  oldptr->te_nextt = node2->rn_te;
	  node2->rn_te = oldptr;
     }
     node1->rn_te = NULL;
}


/*
 *-------------------------------------------------------------------------
 *
 * ResScrunchNet-- Last ditch net simplification. Used to break deadlocks
 *	in ResSimplifyNet.  Resistors are sorted by value. The smallest 
 *	resistor is combined with its smallest neighbor, and ResSimplifyNet
 *	is called.  This continues until the smallest resistor is greater
 *	than the tolerance.
 *
 * Results:none
 *
 * Side Effects: Nodes and resistors are eliminated.
 *
 *-------------------------------------------------------------------------
 */

void
ResScrunchNet(reslist,pendingList,biglist,tolerance)
	resResistor 	**reslist;
	resNode		**pendingList,**biglist;
	float		tolerance;

{
     resResistor *locallist=NULL,*current,*working;
     resNode *node1,*node2;
     resElement  *rcell1;
     int	c1,c2;
     
     /* sort resistors by size */
     current = *reslist;
     while (current != NULL)
     {
	       working = current;
	       current = current->rr_nextResistor;
	       if (working == *reslist)
	       {
	       	    *reslist = current;
	       }
	       else
	       {
	       	    working->rr_lastResistor->rr_nextResistor = current;
	       }
	       if (current != NULL)
	       {
	       	    current->rr_lastResistor = working->rr_lastResistor;
	       }
	       ResAddResistorToList(working,&locallist);
     }
     
     
     *reslist = locallist;
     while (*reslist != NULL && (*reslist)->rr_value < tolerance)
     {
	  current = *reslist;
	  if (current->rr_nextResistor == NULL)
	  {
	       break;
	  }
	  working = NULL;
	  c1=0;
	  c2=0;
	  
	  /* search for next smallest adjoining resistor */
	  for (rcell1 = current->rr_connection1->rn_re; rcell1 != NULL; rcell1 = rcell1->re_nextEl)
	  {
	       if (rcell1->re_thisEl != current)
	       {
	       	    c1++;
		    if (working == NULL)
		    {
		    	 working = rcell1->re_thisEl;
			 node1 = current->rr_connection1;
		    }
		    else
		    {
		    	 if (working->rr_value > rcell1->re_thisEl->rr_value) 
			 {
			      node1 = current->rr_connection1;
			      working = rcell1->re_thisEl;
			 }
		    }
	       }
	  }
	  for (rcell1 = current->rr_connection2->rn_re; rcell1 != NULL; rcell1 = rcell1->re_nextEl)
	  {
	       if (rcell1->re_thisEl != current)
	       {
	       	    c2++;
		    if (working == NULL)
		    {
		    	 working = rcell1->re_thisEl;
			 node1 = current->rr_connection2;
		    }
		    else
		    {
		    	 if (working->rr_value > rcell1->re_thisEl->rr_value) 
			 {
			      node1 = current->rr_connection2;
			      working = rcell1->re_thisEl;
			 }
		    }
	       }
	  }
	  /* 
	     if the current resistor isn't a deadend, add its  value and
	     area to that of the next smallest one.  If it is a deadend,
	     simply add its area to its node.
	  */
	  if (c1 != 0 && c2 != 0)
	  {
	       working->rr_value += current->rr_value;
	       working->rr_float.rr_area += current->rr_float.rr_area;
	  }
	  else
	  {
	       node1->rn_float.rn_area += current->rr_float.rr_area;
	  }
	  /* 
	     Move everything from from one end of the ressistor to the
	     other and eliminate the resistor.
	  */
	  node2 = (current->rr_connection1 == node1) ? current->rr_connection2 : current->rr_connection1;
	  ResDeleteResPointer(current->rr_connection1,current);
	  ResDeleteResPointer(current->rr_connection2,current);
	  working->rr_lastResistor->rr_nextResistor = working->rr_nextResistor;
	  if (working->rr_nextResistor != NULL)
	  {
	       working->rr_nextResistor->rr_lastResistor = working->rr_lastResistor;
	  }
	  ResEliminateResistor(current,reslist);
	  ResAddResistorToList(working,reslist);
	  if (node2->rn_why & RES_NODE_ORIGIN)
	  {
	       ResMergeNodes(node2,node1,pendingList,biglist);
	       node1 = node2;
	  }
	  else
	  {
	       ResMergeNodes(node1,node2,pendingList,biglist);
	  }

	  /* 
	     Try further simplification on net using ResDoneWithNode and 
	     ResSimplifyNet.
	  */
	  ResRemoveFromQueue(node1,biglist);
	  ResAddToQueue(node1,pendingList);
	  node1->rn_status &= ~(RES_DONE_ONCE | FINISHED);
	  ResDoneWithNode(node1);
	  while (*pendingList != NULL)
	  {
	       ResSimplifyNet(pendingList,biglist,reslist,tolerance);
	  }
     }
}


/*
 *-------------------------------------------------------------------------
 *
 * ResAddResistorToList-- Adds resistor to list according to its value 
 *	(smallest first).
 *
 * Results:none
 *
 * Side Effects: modifies locallist.
 *
 *-------------------------------------------------------------------------
 */

void
ResAddResistorToList(resistor,locallist)
     resResistor	*resistor,**locallist;

{     
     resResistor *local,*last=NULL;
     
     for (local = *locallist; local != NULL; local = local->rr_nextResistor)
     {
	  if (local->rr_value >= resistor->rr_value)
	  {
	        break;
	  }
	  last = local;
     }
     if (local != NULL)
     {
	 resistor->rr_nextResistor = local;
	 resistor->rr_lastResistor = local->rr_lastResistor;
	 if (local->rr_lastResistor == NULL)
	 {
	      *locallist = resistor;
	 }
	 else
	 {
	      local->rr_lastResistor->rr_nextResistor = resistor;
	 }
	 local->rr_lastResistor = resistor;
    }
    else
    {
         if (last != NULL)
	 {
	       last->rr_nextResistor = resistor;
               resistor->rr_lastResistor = last;
               resistor->rr_nextResistor = NULL;
	 }
	 else
         {
	      resistor->rr_nextResistor = NULL;
   	      resistor->rr_lastResistor = NULL;
   	      *locallist = resistor;
	}
    }
}


/*
 *-------------------------------------------------------------------------
 *
 * ResdistributeSubstrateCapacitance--
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	takes total capacitance to VDD or GND in a node and distributes
 * 	it onto the new nodes.
 *
 *-------------------------------------------------------------------------
 */

void
ResDistributeCapacitance(nodelist,totalcap)
	resNode		*nodelist;
	float		totalcap;

{
     float totalarea=0,capperarea;
     resNode	*workingNode;
     resElement	*rptr;
     
     for (workingNode = nodelist; workingNode != NULL; workingNode=workingNode->rn_more)
     {
     	  for (rptr = workingNode->rn_re; rptr != NULL; rptr=rptr->re_nextEl)
	  {
	       if (rptr->re_thisEl->rr_float.rr_area != 0.0)
	       {
	       	    TxError("Nonnull resistor area\n");
	       }
/*	       workingNode->rn_float.rn_area += rptr->re_thisEl->rr_float.rr_area/2;  */
	  }
	  totalarea += workingNode->rn_float.rn_area;
     }
     if (totalarea == 0)
     {
     	  TxError("Error: Node with no area.\n");
	  return;
     }
     capperarea = FEMTOTOATTO*totalcap/totalarea;
     for (workingNode = nodelist; workingNode != NULL; workingNode=workingNode->rn_more)
     {
     	  workingNode->rn_float.rn_area  *= capperarea;
     }
}


/*
 *-------------------------------------------------------------------------
 *
 * ResCalculateChildCapacitance-- calculates capacitance of this node and 
 * all downstream nodes. 
 *
 * Results: Returns capacitance of this node and children nodes if connected
 * 	    to a tree- returns -1 If the subtree contains loops.
 *
 * Side Effects:  Adds RCDelayStuff fields to nodes.
 *
 *-------------------------------------------------------------------------
 */

float
ResCalculateChildCapacitance(me)
	resNode	*me;

{
     RCDelayStuff	*myC;
     resElement		*workingRes;
     resTransistor	*tran;
     float		childcap;
     tElement		*tptr;
     int		t;

     
     if (me->rn_client != (ClientData) NULL) /* we have a loop */
     {
	  return(-1);
     }
     myC = (RCDelayStuff *) mallocMagic((unsigned) (sizeof(RCDelayStuff)));
     me->rn_client = (ClientData) myC;

     /* This following assumes that ResDistributeCapacitance has been run */
     /* and the the resulting capacitance value is stored in the area field */
     myC->rc_Cdownstream = me->rn_float.rn_area;
     
     /* get capacitance for all connected gates */
     for (tptr = me->rn_te; tptr != NULL; tptr = tptr->te_nextt)
     {
     	  tran = tptr->te_thist;
	  t = TiGetType(tran->rt_tile);
	  if (tran->rt_gate == me)
	  {
	       myC->rc_Cdownstream += 
	       		tran->rt_length*
			tran->rt_width*
			ExtCurStyle->exts_transGateCap[t]+
	       		(tran->rt_width+tran->rt_width)*
			ExtCurStyle->exts_transSDCap[t];

	  }
     }

     /* Calculate child Capacitance  */
     for (workingRes = me->rn_re; workingRes != NULL; workingRes=workingRes->re_nextEl)
     {
	  if (workingRes->re_thisEl->rr_connection1 == me &&
	     (workingRes->re_thisEl->rr_status & RES_TDI_IGNORE) == 0)
	  {
	       childcap = ResCalculateChildCapacitance(workingRes->re_thisEl->rr_connection2);
	       if (childcap == -1)
	       {
	       	    return(-1);
	       }
	       myC->rc_Cdownstream +=childcap;
	  }
     }
     return(myC->rc_Cdownstream);
}


/*
 *-------------------------------------------------------------------------
 *
 * ResCalculateTDi- Calculates TDi numbers for all the nodes in the circuit.
 *
 * Results: none
 *
 * Side Effects: sets the rc_Tdi fields of the RCDelayStuff fields of the
 *	nodes.
 *
 *-------------------------------------------------------------------------
 */

void
ResCalculateTDi(node,resistor,resistorvalue)
	resNode	*node;
	resResistor *resistor;
	int	resistorvalue;

{
     resElement	*workingRes;
     RCDelayStuff	*rcd = (RCDelayStuff *)node->rn_client;
     RCDelayStuff	*rcd2;
     
     ASSERT(rcd != NULL,"ResCalculateTdi");
     if (resistor == NULL)
     {
          rcd->rc_Tdi = rcd->rc_Cdownstream*(float)resistorvalue;
     }
     else
     {
          rcd2 = (RCDelayStuff *)resistor->rr_connection1->rn_client;
          ASSERT(rcd2 != NULL,"ResCalculateTdi");
          rcd->rc_Tdi=rcd->rc_Cdownstream*(float)resistor->rr_value + 
	  							rcd2->rc_Tdi;
     
     }

     for (workingRes = node->rn_re; workingRes != NULL; workingRes=workingRes->re_nextEl)
     {
     	  if (workingRes->re_thisEl->rr_connection1 == node &&
	      (workingRes->re_thisEl->rr_status & RES_TDI_IGNORE) == 0)
	  {
	       ResCalculateTDi(workingRes->re_thisEl->rr_connection2,
	       		       workingRes->re_thisEl,
	       		       workingRes->re_thisEl->rr_value);
	  }
     }
}

/*
 *-------------------------------------------------------------------------
 *
 *  ResPruneTree-- Designed to be run just after ResCalculateTDi to prune all 
 *	branches off the tree whose end node value of Tdi is less than the
 *	tolerance.  This eliminates many resistors in nets with high fanout.
 *
 * Results: none
 *
 * Side Effects: May Eliminate Resistors and Merge Nodes
 *
 *-------------------------------------------------------------------------
 */

void
ResPruneTree(node,minTdi,nodelist1,nodelist2,resistorlist)
	resNode		*node,**nodelist1,**nodelist2;
	float		minTdi;
	resResistor	**resistorlist;

{
     resResistor	*currentRes;
     resElement		*current;
     
     current = node->rn_re;
     while(current != NULL)
     {
     	  currentRes = current->re_thisEl;
	  current = current->re_nextEl;
	  /* if branch points outward, call routine on subtrees */
	  if (currentRes->rr_connection1 == node) 
	  {
	       ResPruneTree(currentRes->rr_connection2, minTdi,nodelist1,nodelist2,resistorlist);
	  }
     }
     /* We eliminate this branch if 				*/
     /*   1.  It is a terminal node, i.e. it is the connected   */
     /*	      to only one resistor.				*/
     /*	  2.  The direction of this resistor is toward the node */
     /*	      (This prevents the root from being eliminated	*/
     /*   3.  The time constant TDI is less than the tolerance. */
     if (node->rn_re != NULL &&
	 node->rn_re->re_nextEl == NULL &&
	 node->rn_re->re_thisEl->rr_connection2 == node)
     {
     	  if (node->rn_client == (ClientData)NULL)
	  {
	       TxError("Internal Error in Tree Pruning: Missing TDi value.\n");
	  }
	  else if (((RCDelayStuff *)(node->rn_client))->rc_Tdi < minTdi)
	  {
	       currentRes = node->rn_re->re_thisEl;
	       ResDeleteResPointer(currentRes->rr_connection1,currentRes);
	       ResDeleteResPointer(currentRes->rr_connection2,currentRes);
	       ResMergeNodes(currentRes->rr_connection1,currentRes->rr_connection2,nodelist2,nodelist1); /* Patched 7/5/94 */
	       ResEliminateResistor(currentRes,resistorlist);
	  }
     }
}

int
ResDoSimplify(tolerance,rctol,goodies)
	float	tolerance;
	float	rctol;
	ResGlobalParams	*goodies;

{
    resNode 		*node,*slownode;
    float 		bigres = 0;
    float		millitolerance;
    float		totalcap;
    resResistor		*res;
    

    resRemoveLoops = FALSE;
    ResSetPathRes();
    for (node = ResNodeList; node != NULL; node = node->rn_more)
    {
    	 bigres = MAX(bigres,node->rn_noderes);
    }
    
    bigres /= OHMSTOMILLIOHMS; /* convert from milliohms to ohms */
    goodies->rg_maxres = bigres;
    
#ifdef PARANOID
    ResSanityChecks("ExtractSingleNet",ResResList,ResNodeList,ResTransList);
#endif

    /* Is extracted network still greater than the tolerance?	*/
    /* Even if it isn't, we still let the next section run if   */
    /* we're calculating lumped values so that the capacitance  */
    /* values get calculated correctly.				*/

     (void) ResDistributeCapacitance(ResNodeList,goodies->rg_nodecap);
     
    if ((tolerance > bigres || (ResOptionsFlags &ResOpt_Simplify)==0) &&
	(ResOptionsFlags &ResOpt_DoLumpFile)==0)
    {
    	 return(0);
    } 
    res = ResResList;
    while (res)
    {
    	 resResistor	*oldres = res;

	 res = res->rr_nextResistor;
    	 oldres->rr_status &= ~RES_HEAP;
	 if (oldres->rr_status & RES_TDI_IGNORE)
	 {
	       ResDeleteResPointer(oldres->rr_node[0],oldres);
	       ResDeleteResPointer(oldres->rr_node[1],oldres);
	       ResEliminateResistor(oldres,&ResResList);
	 }
    }

     if (ResOptionsFlags & ResOpt_Tdi)
     {
	 if (goodies->rg_nodecap != -1 && 
	 	(totalcap = ResCalculateChildCapacitance(ResOriginNode)) != -1)
	 {
	      RCDelayStuff	*rc = (RCDelayStuff *) ResNodeList->rn_client;

	      if (rc != (RCDelayStuff *)NULL)
	      {
	          goodies->rg_nodecap = totalcap;
	          ResCalculateTDi(ResOriginNode,(resResistor *)NULL,
	      					goodies->rg_bigtranres);
	          goodies->rg_Tdi = rc->rc_Tdi;
	          slownode = ResNodeList;
	          for (node = ResNodeList; node != NULL; node = node->rn_more)
	          {     
	      	       rc = (RCDelayStuff *)node->rn_client;
		       if (rc && (goodies->rg_Tdi < rc->rc_Tdi))
		       {
		   	    slownode = node;
			    goodies->rg_Tdi = rc->rc_Tdi;
		       }
	          }
	          slownode->rn_status |= RN_MAXTDI;
	      }
	      else
		  goodies->rg_Tdi = 0;
	 }
	 else
	 {
	      goodies->rg_Tdi = -1;
	 }
    }
    else
    {
    	 goodies->rg_Tdi = 0;
    }
    if ((rctol+1)*goodies->rg_bigtranres*goodies->rg_nodecap > 
    rctol*goodies->rg_Tdi && 
    (ResOptionsFlags & ResOpt_Tdi) &&
    goodies->rg_Tdi != -1)
    {
    	  return(0);
    }

    /* Simplify network; resistors are still in milliohms, so use 
       millitolerance.
    */

    if (ResOptionsFlags & ResOpt_Simplify)
    {
	 
	 millitolerance = tolerance*MILLIOHMSPEROHM;

         /* 
         Start simplification at driver (R=0). Remove it from the done list
         and add it to the pending list. Call ResSimplifyNet as long as nodes
         remain in the pending list.
         */
	 for (node = ResNodeList; node != NULL ;node = node->rn_more)
	 {
	      if (node->rn_noderes == 0)
	      {
	      	   ResOriginNode = node;
	      }
	      node->rn_status |= FINISHED;
	 }
         if (ResOriginNode  != NULL)
         {

              /* if Tdi is enabled, prune all branches whose end nodes have */
	      /* time constants less than the tolerance.	    */

	      if ((ResOptionsFlags & ResOpt_Tdi) &&
	           goodies->rg_Tdi != -1 &&
		   rctol != 0)
	      {
	           ResPruneTree(ResOriginNode,
		   (rctol+1)*goodies->rg_bigtranres*goodies->rg_nodecap/rctol,
		   	        &ResNodeList,&ResNodeQueue,&ResResList);
	      }
	      ResOriginNode->rn_status &= ~MARKED;
	      if (ResOriginNode->rn_less == NULL)
	      {
	           ResNodeList = ResOriginNode->rn_more;
	      }
	      else
	      {
	           ResOriginNode->rn_less->rn_more = ResOriginNode->rn_more;
	      }
	      if (ResOriginNode->rn_more != NULL)
	      {
	           ResOriginNode->rn_more->rn_less = ResOriginNode->rn_less;
	      }
	      ResOriginNode->rn_more = NULL;
	      ResOriginNode->rn_less = NULL;
	      ResNodeQueue = ResOriginNode;
	      while (ResNodeQueue != NULL)
	      {
	           ResSimplifyNet(&ResNodeQueue,&ResNodeList,&ResResList,millitolerance);
	      }
	 
	     /* 
	      Call ResScrunchNet to eliminate any remaining under tolerance
	      resistors.
	     */
	      ResScrunchNet(&ResResList,&ResNodeQueue,&ResNodeList,millitolerance);
         }
     }
    return(0);
     
}

void
ResSetPathRes()

{
     HeapEntry		he;
     resNode		*node;
     static int		init=1;

     if (init)
     {
     	  init = 0;
	  HeapInit(&ResistorHeap,128,FALSE,FALSE);
     }
     
     for (node = ResNodeList; node != NULL ;node = node->rn_more)
     {
	 if (node->rn_noderes == 0)
	 {
	      ResOriginNode = node;
	      node->rn_status |= FINISHED;
	 }
	 else 
	 {
	      node->rn_noderes = RES_INFINITY;
	      node->rn_status &= ~FINISHED;
	 }
     }
     if (ResOriginNode == NULL)
     {
     	  resTransistor	*res = ResGetTransistor(gparams.rg_tranloc);
	  ResOriginNode = res->rt_source;
	  ResOriginNode->rn_why = RES_NODE_ORIGIN;
	  ResOriginNode->rn_noderes = 0;
     }
     ASSERT(ResOriginNode != NULL,"ResDoSimplify");
     resPathNode(ResOriginNode);
     while (HeapRemoveTop(&ResistorHeap,&he))
     {
	  resPathRes((resResistor *) he.he_id);
     }
}

void
resPathNode(node)
	resNode	*node;

{
     resElement	*re;

     node->rn_status |= FINISHED;
     for (re = node->rn_re;re;re=re->re_nextEl)
     {
     	  resResistor	*res = re->re_thisEl;
	  resNode	*node2;
	  
	  if (res->rr_status & RES_HEAP) continue;
	  if ((node2=res->rr_node[0]) == node) node2 = res->rr_node[1];
	  if ((node2->rn_status & FINISHED) == 0)
	  {
	       HeapAddInt(&ResistorHeap,  node->rn_noderes + res->rr_value, 
			(char *)res);
	  }
     }
}

void
resPathRes(res)
	resResistor	*res;

{
     resNode	*node0,*node1;
     int	flag0,flag1;
     
     res->rr_status |= RES_HEAP;
     res->rr_status &= ~RES_MARKED;
     node0 = res->rr_node[0];
     node1 = res->rr_node[1];
     flag0 = node0->rn_status & FINISHED;
     flag1 = node1->rn_status & FINISHED;
     if (flag0 && flag1)
     {
          res->rr_status |= RES_TDI_IGNORE;
	  if (resRemoveLoops)
	  {
	       ResDeleteResPointer(node0,res);
	       ResDeleteResPointer(node1,res);
	       ResEliminateResistor(res,&ResResList);
	  }
     }
     else if (flag0)
     {
     	  node1->rn_noderes = node0->rn_noderes+res->rr_value;
	  resPathNode(node1);
     }
     else  
     {
     	  ASSERT(flag1,"ResPathRes");
          res->rr_node[0] = node1;
	  res->rr_node[1] = node0;
     	  node0->rn_noderes = node1->rn_noderes+res->rr_value;
	  resPathNode(node0);
     }
}
