
#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/resis/ResChecks.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
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
#include "database/database.h"
#include "utils/malloc.h"
#include "textio/textio.h"
#include "extract/extract.h"
#include "extract/extractInt.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/utils.h"
#include "utils/tech.h"
#include "textio/txcommands.h"
#include "utils/stack.h"
#include "resis/resis.h"

#ifdef PARANOID

/*
 *-------------------------------------------------------------------------
 *
 * ResSanityChecks -- Checks that resistor and node lists are consistant.
 *	Make sure that all resistors are connected, and that each node 
 *	to which a resistor is connected has the correct pointer in its list.
 *
 * Results: none
 *
 * Side Effects: prints out error messages if it finds something bogus.
 *
 *-------------------------------------------------------------------------
 */

void
ResSanityChecks(nodename,resistorList,nodeList,tranlist)
	char		*nodename;
	resResistor	*resistorList;
	resNode		*nodeList;
	resTransistor	*tranlist;

{
     resResistor	*resistor;
     resNode		*node;
     resTransistor	*tran;
     resElement		*rcell;
     static	Stack	*resSanityStack = NULL;
     int		reached,foundorigin;
     
     if (resSanityStack == NULL)
     {
     	  resSanityStack = StackNew(64);
     }
     for (node = nodeList; node != NULL; node=node->rn_more)
     {
     	  node->rn_status &= ~RES_REACHED_NODE;
	  if (node->rn_why == RES_NODE_ORIGIN)  
	  		STACKPUSH((ClientData) node, resSanityStack);
     }
     for (resistor = resistorList; resistor != NULL; resistor = resistor->rr_nextResistor)
     {
     	  resistor->rr_status &= ~RES_REACHED_RESISTOR;
     }
     
     /* Check 1- Are the resistors and nodes all connected?  */
     while (!StackEmpty(resSanityStack))
     {
     	  node = (resNode *)STACKPOP(resSanityStack);
	  if (node->rn_status & RES_REACHED_NODE) continue;
	  node->rn_status |= RES_REACHED_NODE;
	  for (rcell = node->rn_re; rcell != NULL; rcell=rcell->re_nextEl)
	  {
	       resistor = rcell->re_thisEl;
	       if (resistor->rr_status & RES_REACHED_RESISTOR) continue;
	       resistor->rr_status |= RES_REACHED_RESISTOR;
	       if (resistor->rr_connection1 != node && 
	           resistor->rr_connection2 != node)
		   {
		   	TxError("Stray resElement pointer- node %s, pointer %d\n",nodename,rcell);
			continue;
		   }
	       if ((resistor->rr_connection1->rn_status & RES_REACHED_NODE) == 0)
	       {
	       	    STACKPUSH((ClientData)resistor->rr_connection1,resSanityStack);
	       }
	       if ((resistor->rr_connection2->rn_status & RES_REACHED_NODE) == 0)
	       {
	       	    STACKPUSH((ClientData)resistor->rr_connection2,resSanityStack);
	       }
	  }
     }
     for (resistor = resistorList; resistor != NULL; resistor = resistor->rr_nextResistor)
     {
     	  if ((resistor->rr_status & RES_REACHED_RESISTOR) == 0)
	  {
	       TxError("Unreached resistor in %s\n",nodename);
	  }
	  resistor->rr_status &= ~RES_REACHED_RESISTOR;
     }
     for (tran = tranlist; tran != NULL; tran = tran->rt_nextTran)
     {
     	  int	i;

	  if (tran->rt_status & RES_TRAN_PLUG) continue;
	  reached = FALSE;
	  for (i=0;i != RT_TERMCOUNT;i++)
	  {
	       if (tran->rt_terminals[i] != NULL)
	       {
	            reached = TRUE;
	            if ((tran->rt_terminals[i]->rn_status & RES_REACHED_NODE) == 0)
	            {
	       	         TxError("Transistor node %d unreached in %s\n",i,nodename);
	            }
	       }
	  }
	  if (reached == 0)
	  {
	       TxError("Unreached transistor in %s at %d %d\n",
					nodename,
	       				tran->rt_inside.r_xbot,
	       				tran->rt_inside.r_ybot);
	  }
     }
     foundorigin = 0;
     for (node = nodeList; node != NULL; node=node->rn_more)
     {
     	  if ((node->rn_status & RES_REACHED_NODE) == 0)
	  {
	       TxError("Unreached node in %s at %d, %d\n",nodename,node->rn_loc.p_x,node->rn_loc.p_y);
	  }
	  node->rn_status &= ~RES_REACHED_NODE;
	  if (node->rn_why & RES_NODE_ORIGIN)
	  {
	       foundorigin = 1;
	  }
     }
     if (foundorigin == 0)
     {
	  TxError("Starting node not found in %s\n",nodename);
     }
}
#endif
