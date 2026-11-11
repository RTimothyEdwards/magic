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
#include <stdlib.h>	/* for qsort() */
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
#include "resis/resis.h"

#define MILLIOHMSPEROHM 1000

/* Forward declarations */
void ResSetPathRes();
void resPathNode();
void resPathRes();
Heap ResistorHeap;

/* Forward declarations */

extern void ResMoveDevices();
extern void ResAddResistorToList();

/*
 *-------------------------------------------------------------------------
 *
 * ResSimplifyNet- Reduces complete (?) net produced by ResProcessTiles into
 *    something a little less chaotic.
 *
 * Results: none
 *
 * Side Effects:  Can eliminate nodes and resistors, and move devices from
 *		one node to another.
 *
 *-------------------------------------------------------------------------
 */

void
ResSimplifyNet(nodelist, biglist, reslist, tolerance)
    resNode	**nodelist, **biglist;
    resResistor	**reslist;
    float	tolerance;

{
    resElement  *resisptr;
    resNode     *node, *otherNode, *node1, *node2;
    resResistor *resistor1 = NULL, *resistor2 = NULL;
    int         numdrive = 0, numreceive = 0;
    int		MarkedReceivers, UnMarkedReceivers;
    int		NumberOfDrivers, PendingReceivers;

    if (*nodelist == NULL) return;
    node = *nodelist;
    node->rn_status |= RES_MARKED | RES_FINISHED;
    *nodelist = node->rn_more;
    if (node->rn_more != NULL)
        node->rn_more->rn_less = (resNode *) NULL;

    node->rn_more = *biglist;
    if (*biglist != (resNode *) NULL)
     	(*biglist)->rn_less = node;

    *biglist = node;

    /*
     * Walk though resistors. Mark uninitialized ones and assign them
     * a direction. Keep track of the number of resistors pointing in
     * each direction.
     */
    for (resisptr = node->rn_re; resisptr != NULL; resisptr = resisptr->re_nextEl)
    {
     	if (((resisptr->re_thisEl->rr_status & RES_MARKED) == RES_MARKED) &&
		(resisptr->re_thisEl->rr_connection2 == node))
	{
	    if (resistor1 == NULL)
	        resistor1 = resisptr->re_thisEl;
	    else
	  	resistor2 = resisptr->re_thisEl;

	    numdrive++;
	}
	else
	{
	    /*
	     * Resistor direction is from node1 to node2. If the resistor
	     * is not marked, mark it and make sure the direction is
	     * set properly.
	     */

	    if ((resisptr->re_thisEl->rr_status & RES_MARKED) != RES_MARKED)
	    {
	     	if (resisptr->re_thisEl->rr_connection2 == node)
		{
		     resisptr->re_thisEl->rr_connection2 =
				resisptr->re_thisEl->rr_connection1;
		     resisptr->re_thisEl->rr_connection1 = node;
		}
		resisptr->re_thisEl->rr_status |= RES_MARKED;
	    }
	    if (resistor1 == NULL)
	        resistor1 = resisptr->re_thisEl;
	    else
	     	resistor2 = resisptr->re_thisEl;

	    numreceive++;
	}
    }

    /*
     * Is the node reached by one resistor? If it is, check the resistor's
     * other end.  Check the number of drivers at the other end. If it is
     * more than 1, delete the current resistor to break the deadlock.
     */

    if (numreceive == 0 && numdrive == 1 &&
			!(node->rn_why & (RES_NODE_ORIGIN | RES_NODE_SINK)))
    {
	resistor1->rr_status |= RES_DEADEND;
	if (resistor1->rr_value < tolerance)
	{
	    otherNode = (resistor1->rr_connection1 == node) ?
			resistor1->rr_connection2 : resistor1->rr_connection1;
	    MarkedReceivers = 0;
	    UnMarkedReceivers = 0;
	    NumberOfDrivers = 0;
	    PendingReceivers = 0;
	    resistor2 = resistor1;
	    for (resisptr = otherNode->rn_re; resisptr != NULL;
			    resisptr = resisptr->re_nextEl)
	    {
	     	if (resisptr->re_thisEl->rr_connection1 == otherNode)
		{
		    if ((resisptr->re_thisEl->rr_connection2->rn_status & RES_MARKED)
				!= RES_MARKED)
		    {
		       	 PendingReceivers++;
		    }
		    if (resisptr->re_thisEl->rr_status & RES_DEADEND ||
			    resisptr->re_thisEl->rr_value > tolerance)
		    {
		       	 MarkedReceivers++;
			 resistor2 = (resisptr->re_thisEl->rr_value >=
				resistor2->rr_value) ? resisptr->re_thisEl : resistor2;
		    }
		    else
		       	 UnMarkedReceivers++;
		}
		else
		    NumberOfDrivers++;
	    }
	    /* other recievers at far end? If so, reschedule other node;
	     * deadlock will be settled from that node.
	     */
	    if ((MarkedReceivers + UnMarkedReceivers + NumberOfDrivers == 2) ||
			(UnMarkedReceivers == 0 && MarkedReceivers > 1 &&
			resistor2 == resistor1 && PendingReceivers == 0))
	    {
	     	if (otherNode->rn_status & RES_MARKED)
		{
		     otherNode->rn_status &= ~RES_MARKED;
		     ResRemoveFromQueue(otherNode, biglist);
		     otherNode->rn_less = NULL;
		     otherNode->rn_more = *nodelist;
		     if (*nodelist != NULL)
		       	 (*nodelist)->rn_less = otherNode;

		     *nodelist = otherNode;
		}
		return;
	    }

	    /*
	     * Break loop here. More than one driver indicates a loop;
	     * remove deadend, allowing drivers to be merged
	     */
	    else if (UnMarkedReceivers == 0 && ((MarkedReceivers == 1 &&
		    NumberOfDrivers > 1) || resistor2 != resistor1))

	    {
		otherNode->rn_float.rn_area += resistor1->rr_float.rr_area;
		otherNode->rn_status &= ~RES_DONE_ONCE;
	        ResDeleteResPointer(resistor1->rr_connection1, resistor1);
	        ResDeleteResPointer(resistor1->rr_connection2, resistor1);
	        ResEliminateResistor(resistor1, reslist);
	        ResMergeNodes(otherNode, node, nodelist, biglist);
	        if (otherNode->rn_status & RES_MARKED)
	        {
	            otherNode->rn_status &= ~RES_MARKED;
	            ResRemoveFromQueue(otherNode, biglist);
	            otherNode->rn_less= NULL;
		    otherNode->rn_more = *nodelist;
		    if (*nodelist != NULL)
		        (*nodelist)->rn_less = otherNode;

		    *nodelist = otherNode;
	        }
	        ResDoneWithNode(otherNode);
	    }
	}
    }
    /*
     * Two resistors in series? Combine them and move devices to
     * appropriate end.  Do not eliminate the origin or a port (both
     * marked RES_NODE_ORIGIN) or a sink, or else the port name would
     * be lost from the network.
     */
    else if (numdrive + numreceive == 2 && (resistor1->rr_value < tolerance &&
		resistor2->rr_value < tolerance) &&
		!(node->rn_why & (RES_NODE_ORIGIN | RES_NODE_SINK)))
    {
	if ((resistor1->rr_status & RES_MARKED) == 0 &&
		    (resistor1->rr_connection2 == node))
	{
	    resistor1->rr_connection2 = resistor1->rr_connection1;
	    resistor1->rr_connection1 = node;
	}
	resistor1->rr_status |= RES_MARKED;
	if ((resistor2->rr_status & RES_MARKED) == 0 &&
		    (resistor2->rr_connection2 == node))
	{
	    resistor2->rr_connection2 = resistor2->rr_connection1;
	    resistor2->rr_connection1 = node;
	}
	resistor2->rr_status |= RES_MARKED;
	node1 = (resistor1->rr_connection1 == node) ? resistor1->rr_connection2 :
		    resistor1->rr_connection1;
	node2 = (resistor2->rr_connection1 == node) ? resistor2->rr_connection2 :
		    resistor2->rr_connection1;
	otherNode = (resistor1->rr_status & RES_DEADEND &&
	  	       resistor1->rr_value  < tolerance / 2) ||
	  	      ((resistor2->rr_status & RES_DEADEND) == 0 &&
		      resistor1->rr_value < resistor2->rr_value) ? node1 : node2;
     	/*
	 * Make one big resistor out of two little ones, eliminating
	 * the current node.  Devices connected to this node are
	 * moved to either end depending on their resistance.
	 */
	ResMoveDevices(node,otherNode);
        otherNode->rn_noderes = MIN(node->rn_noderes, otherNode->rn_noderes);
        node2->rn_float.rn_area += resistor1->rr_value * node->rn_float.rn_area /
		    (resistor1->rr_value + resistor2->rr_value);
        node1->rn_float.rn_area += resistor2->rr_value * node->rn_float.rn_area /
		    (resistor1->rr_value + resistor2->rr_value);
	resistor1->rr_value += resistor2->rr_value;
	resistor1->rr_float.rr_area +=resistor2->rr_float.rr_area;
	if (resistor1 == *reslist)
	    *reslist = resistor1->rr_nextResistor;
	else
	    resistor1->rr_lastResistor->rr_nextResistor = resistor1->rr_nextResistor;

	if (resistor1->rr_nextResistor != NULL)
	    resistor1->rr_nextResistor->rr_lastResistor = resistor1->rr_lastResistor;

	ResAddResistorToList(resistor1, reslist);
	ResDeleteResPointer(node, resistor1);
	ResDeleteResPointer(node, resistor2);
	ResDeleteResPointer(node2, resistor2);
	if (resistor1->rr_connection1 == node)
	    resistor1->rr_connection1 = node2;
	else
	    resistor1->rr_connection2 = node2;

        resisptr = (resElement *)mallocMagic((unsigned)(sizeof(resElement)));
        resisptr->re_thisEl = resistor1;
        resisptr->re_nextEl = node2->rn_re;
        node2->rn_re = resisptr;
	ResEliminateResistor(resistor2, reslist);
	otherNode->rn_status |= (node->rn_status & RES_MAXTDI);
	ResCleanNode(node, TRUE, biglist, nodelist);
	node1->rn_status &= ~RES_DONE_ONCE;
	if (node1->rn_status & RES_MARKED)
	{
	    node1->rn_status &= ~RES_MARKED;
	    ResRemoveFromQueue(node1, biglist);
	    node1->rn_less = NULL;
	    node1->rn_more = *nodelist;
	    if (*nodelist != NULL)
	     	(*nodelist)->rn_less = node1;
	    *nodelist = node1;
	}
	node2->rn_status &= ~RES_DONE_ONCE;
	if (node2->rn_status & RES_MARKED)
	{
	    node2->rn_status &= ~RES_MARKED;
	    ResRemoveFromQueue(node2, biglist);
	    node2->rn_less = NULL;
	    node2->rn_more = *nodelist;
	    if (*nodelist != NULL)
		(*nodelist)->rn_less = node2;
	    *nodelist = node2;
	}
	ResDoneWithNode(node1);
    }

    /*
     * Last resort- keep propagating down the tree.  To avoid looping,
     * mark each node when it is reached. Don't reschedule node if
     * none of the connections to it have changed since it was marked
     */
    else if (numreceive > 0 && (node->rn_status & RES_DONE_ONCE) == 0)
    {
        node->rn_status |= RES_DONE_ONCE;
	for (resisptr = node->rn_re; resisptr != NULL; resisptr = resisptr->re_nextEl)
	{
	    if (resisptr->re_thisEl->rr_connection1 == node)
	    {
		/*
		 * Elements with a resistance greater than the
		 * tolerance should only be propagated past once-
		 * loops may occur otherwise.
		 */
		if (resisptr->re_thisEl->rr_status & RES_DONE_ONCE)
		    continue;

		if (resisptr->re_thisEl->rr_connection2->rn_status & RES_MARKED)
		{
		    /*
		     * Mark big resistors so we only process them
		     * once.
		     */
		    if (resisptr->re_thisEl->rr_value > tolerance)
			resisptr->re_thisEl->rr_status |= RES_DONE_ONCE;

		    resisptr->re_thisEl->rr_connection2->rn_status &= ~RES_MARKED;
		    ResRemoveFromQueue(resisptr->re_thisEl->rr_connection2, biglist);
	     	    resisptr->re_thisEl->rr_connection2->rn_less= NULL;
		    resisptr->re_thisEl->rr_connection2->rn_more = *nodelist;
		    if (*nodelist != NULL)
			(*nodelist)->rn_less = resisptr->re_thisEl->rr_connection2;

		    *nodelist = resisptr->re_thisEl->rr_connection2;
		}
	    }
	}
    }
}

/*
 *-------------------------------------------------------------------------
 *
 * ResMoveDevices-- move devices from one node (node1) to anther (node2)
 *
 * Results: none
 *
 * Side Effects: Changes device connections and node tElements.
 *
 *-------------------------------------------------------------------------
 */

void
ResMoveDevices(node1, node2)
    resNode *node1, *node2;

{
    tElement	*devptr, *oldptr;
    resDevice	*device;

    devptr = node1->rn_te;
    while (devptr != NULL)
    {
	device = devptr->te_thist;
	oldptr = devptr;
	devptr = devptr->te_nextt;
	if (device->rd_fet_gate == node1)
   	    device->rd_fet_gate = node2;
	else if (device->rd_fet_subs == node1)
   	    device->rd_fet_subs = node2;
	else if ((device->rd_nterms > 2) && (device->rd_fet_source == node1))
	    device->rd_fet_source = node2;
	else if ((device->rd_nterms > 3) && (device->rd_fet_drain == node1))
  	    device->rd_fet_drain = node2;
	else
	    TxError("Missing Device connection in squish routines"
			" at %d, %d\n", node1->rn_loc.p_x, node1->rn_loc.p_y);
	oldptr->te_nextt = node2->rn_te;
	node2->rn_te = oldptr;
    }
    node1->rn_te = NULL;
}

/*
 *-------------------------------------------------------------------------
 *
 * qrescompare ---
 *
 * Sort routine for qsort() to be used by ResScrunchNet().  Sorts in
 * order of the resistor value, smallest to largest.
 *-------------------------------------------------------------------------
 */

int
qrescompare(const void *one, const void *two)
{
    int cval;

    resResistor *r1 = *((resResistor **)one);
    resResistor *r2 = *((resResistor **)two);

    if (r1->rr_value < r2->rr_value) return -1;
    else if (r1->rr_value == r2->rr_value) return 0;
    else return 1;
}

/*
 *-------------------------------------------------------------------------
 *
 * resFindSameNameNodes --
 *
 *	Find two different nodes of the network that have the same name.
 *
 * Results:
 *	TRUE if such a pair was found, in which case *n1 and *n2 are set
 *	to the two nodes.  FALSE otherwise.
 *
 * Side Effects:
 *	None.
 *
 *-------------------------------------------------------------------------
 */

bool
resFindSameNameNodes(reslist, n1, n2)
    resResistor	*reslist;
    resNode	**n1, **n2;
{
    HashTable	nameTable;
    HashEntry	*he;
    resResistor *res;
    resNode	*node, *other;
    int		i;
    bool	found = FALSE;

    HashInit(&nameTable, 32, HT_STRINGKEYS);
    for (res = reslist; res != NULL && !found; res = res->rr_nextResistor)
    {
	for (i = 0; i < 2; i++)
	{
	    node = (i == 0) ? res->rr_connection1 : res->rr_connection2;
	    if (node->rn_name == NULL) continue;
	    he = HashFind(&nameTable, node->rn_name);
	    other = (resNode *)HashGetValue(he);
	    if (other == NULL)
		HashSetValue(he, (ClientData)node);
	    else if (other != node)
	    {
		*n1 = other;
		*n2 = node;
		found = TRUE;
		break;
	    }
	}
    }
    HashKill(&nameTable);
    return found;
}

/*
 *-------------------------------------------------------------------------
 *
 * ResMergeSameNameNodes --
 *
 *	Merge all nodes of the network that have the same name.  Such nodes
 *	are different connection points of the same port (e.g., the
 *	multiple labels of a standard cell pin, any of which is a valid
 *	connection point), and they are a single node in the netlist
 *	output.  Without merging, a resistor between two of them would
 *	connect the node to itself in the netlist, and resistors from two
 *	of them to the same node would be unsimplified parallel resistors.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Nodes are merged, resistors connecting a merged node to itself are
 *	eliminated, and the network is further simplified around each
 *	merged node.
 *
 *-------------------------------------------------------------------------
 */

void
ResMergeSameNameNodes(reslist, pendingList, biglist, tolerance)
    resResistor	**reslist;
    resNode	**pendingList, **biglist;
    float	tolerance;
{
    resResistor *res;
    resElement	*rcell;
    resNode	*node1, *node2, *tnode;

    /* Simplification may eliminate any node, so after each merge,	*/
    /* search again from the beginning.					*/

    while (resFindSameNameNodes(*reslist, &node1, &node2))
    {
	/* Keep the origin node, if it is one of the two */
	if (node2->rn_why & RES_NODE_ORIGIN)
	{
	    tnode = node1;
	    node1 = node2;
	    node2 = tnode;
	}
	ResMergeNodes(node1, node2, pendingList, biglist);

	/* Eliminate resistors that now connect node1 to itself */
	rcell = node1->rn_re;
	while (rcell != NULL)
	{
	    res = rcell->re_thisEl;
	    if (res->rr_connection1 == res->rr_connection2)
	    {
		ResDeleteResPointer(node1, res);
		ResDeleteResPointer(node1, res);
		ResEliminateResistor(res, reslist);
		rcell = node1->rn_re;
	    }
	    else
		rcell = rcell->re_nextEl;
	}

	/* Combine resistors in parallel between node1 and another node.	*/
	/* (ResDoneWithNode() does not do this for ports.)		*/
	rcell = node1->rn_re;
	while (rcell != NULL)
	{
	    resElement *rcell2;
	    resResistor *res2;
	    resNode *other, *other2;

	    res = rcell->re_thisEl;
	    other = (res->rr_connection1 == node1) ? res->rr_connection2 :
			res->rr_connection1;
	    for (rcell2 = rcell->re_nextEl; rcell2 != NULL;
			rcell2 = rcell2->re_nextEl)
	    {
		res2 = rcell2->re_thisEl;
		other2 = (res2->rr_connection1 == node1) ? res2->rr_connection2 :
			res2->rr_connection1;
		if (other2 == other) break;
	    }
	    if (rcell2 != NULL)
	    {
		ResFixParallel(res2, res);
		rcell = node1->rn_re;
	    }
	    else
		rcell = rcell->re_nextEl;
	}

	/* Try further simplification around the merged node */
	ResRemoveFromQueue(node1, biglist);
	ResAddToQueue(node1, pendingList);
	node1->rn_status &= ~(RES_DONE_ONCE | RES_FINISHED);
	ResDoneWithNode(node1);
	while (*pendingList != NULL)
	    ResSimplifyNet(pendingList, biglist, reslist, tolerance);
    }
}

/*
 *-------------------------------------------------------------------------
 *
 * resFindResistor --
 *
 *	Find a resistor connecting two nodes.
 *
 * Results:
 *	Pointer to the first resistor found between node1 and node2, or
 *	NULL if the nodes are not connected by a resistor.
 *
 * Side Effects:
 *	None.
 *
 *-------------------------------------------------------------------------
 */

resResistor *
resFindResistor(node1, node2)
    resNode *node1, *node2;
{
    resElement *rcell;
    resResistor *res;

    for (rcell = node1->rn_re; rcell != NULL; rcell = rcell->re_nextEl)
    {
	res = rcell->re_thisEl;
	if ((res->rr_connection1 == node2) || (res->rr_connection2 == node2))
	    return res;
    }
    return NULL;
}

/*
 *-------------------------------------------------------------------------
 *
 * resCombineParallel --
 *
 *	Combine all resistors in parallel between a node and each of its
 *	neighbors.
 *
 * Results:
 *	FALSE if the node has a zero-valued resistor or a resistor
 *	connecting it to itself, in which case nothing is done.  TRUE
 *	otherwise.
 *
 * Side Effects:
 *	Resistors are eliminated and changed.
 *
 *-------------------------------------------------------------------------
 */

bool
resCombineParallel(node)
    resNode	*node;
{
    resElement	*rcell, *rcell2;
    resResistor *res, *res2;
    resNode	*other;

    for (rcell = node->rn_re; rcell != NULL; rcell = rcell->re_nextEl)
    {
	res = rcell->re_thisEl;
	if ((res->rr_value <= 0) || (res->rr_connection1 == res->rr_connection2))
	    return FALSE;
    }

    rcell = node->rn_re;
    while (rcell != NULL)
    {
	res = rcell->re_thisEl;
	other = (res->rr_connection1 == node) ? res->rr_connection2 :
		res->rr_connection1;
	for (rcell2 = rcell->re_nextEl; rcell2 != NULL; rcell2 = rcell2->re_nextEl)
	{
	    res2 = rcell2->re_thisEl;
	    if ((res2->rr_connection1 == other) || (res2->rr_connection2 == other))
		break;
	}
	if (rcell2 != NULL)
	{
	    ResFixParallel(res2, res);
	    rcell = node->rn_re;
	}
	else
	    rcell = rcell->re_nextEl;
    }
    return TRUE;
}

/* Maximum number of neighbors of a node to be eliminated */
#define RES_ELIM_MAXDEG 8

/*
 *-------------------------------------------------------------------------
 *
 * resEliminateNode --
 *
 *	Try to eliminate one internal node of the network by the star-mesh
 *	transform (the generalization of the wye-delta transform):  The
 *	resistors from the node to its neighbors, with conductances g_i
 *	summing to G, are replaced by resistors between each pair of
 *	neighbors i and j, with conductance g_i * g_j / G, combined in
 *	parallel with any resistor already connecting i and j.  This is
 *	exact for the resistance between all remaining nodes.  The node's
 *	capacitance is distributed to its neighbors in proportion to g_i / G
 *	(as is done when combining resistors in series).
 *
 *	Only nodes with no device terminals, no name (port), and which are
 *	not the origin or a sink are eliminated, and only if the node has
 *	at most "maxdeg" neighbors and the transform does not create more
 *	new resistors than it removes.
 *
 * Results:
 *	TRUE if the node was eliminated, FALSE if not.
 *
 * Side Effects:
 *	Parallel resistors from the node to the same neighbor are combined
 *	whether or not the node is eliminated.  If the node is eliminated,
 *	it and its resistors are freed, and resistors are added or changed
 *	between its neighbors.
 *
 *-------------------------------------------------------------------------
 */

bool
resEliminateNode(node, maxdeg, reslist, pendingList, biglist)
    resNode	*node;
    int		maxdeg;
    resResistor	**reslist;
    resNode	**pendingList, **biglist;
{
    resElement	*rcell;
    resResistor *res, *rnew;
    resNode	*nb[RES_ELIM_MAXDEG];
    resResistor *nbres[RES_ELIM_MAXDEG];
    float	g[RES_ELIM_MAXDEG], gtot, gadd;
    int		d, i, j, imax, newcount;

    if (node->rn_te != NULL) return FALSE;
    if (node->rn_why & (RES_NODE_ORIGIN | RES_NODE_SINK)) return FALSE;
    if (node->rn_name != NULL) return FALSE;
    if (node->rn_re == NULL) return FALSE;

    /* Leave nodes with zero-valued resistors or loops to ResDoneWithNode() */
    if (!resCombineParallel(node)) return FALSE;

    /* Collect the neighbors */
    d = 0;
    for (rcell = node->rn_re; rcell != NULL; rcell = rcell->re_nextEl)
    {
	if (d == maxdeg) return FALSE;
	res = rcell->re_thisEl;
	nbres[d] = res;
	nb[d] = (res->rr_connection1 == node) ? res->rr_connection2 :
		res->rr_connection1;
	g[d] = 1.0 / res->rr_value;
	d++;
    }

    /* Do not eliminate the node if that would add resistors */
    newcount = 0;
    for (i = 0; i < d; i++)
	for (j = i + 1; j < d; j++)
	    if (resFindResistor(nb[i], nb[j]) == NULL)
		newcount++;
    if (newcount > d) return FALSE;

    gtot = 0.0;
    imax = 0;
    for (i = 0; i < d; i++)
    {
	gtot += g[i];
	if (g[i] > g[imax]) imax = i;
    }

    /* Connect each pair of neighbors */
    for (i = 0; i < d; i++)
	for (j = i + 1; j < d; j++)
	{
	    gadd = g[i] * g[j] / gtot;
	    if ((res = resFindResistor(nb[i], nb[j])) != NULL)
		res->rr_value = 1.0 / ((1.0 / res->rr_value) + gadd);
	    else
	    {
		rnew = (resResistor *)mallocMagic(sizeof(resResistor));
		rnew->rr_connection1 = nb[i];
		rnew->rr_connection2 = nb[j];
		rnew->rr_value = 1.0 / gadd;
		rnew->rr_status = 0;
		rnew->rr_float.rr_area = 0.0;
		rnew->rr_cl = 0;
		rnew->rr_width = 0;
		rnew->rr_tt = (g[i] > g[j]) ? nbres[i]->rr_tt : nbres[j]->rr_tt;

		rnew->rr_lastResistor = NULL;
		rnew->rr_nextResistor = *reslist;
		if (*reslist != NULL) (*reslist)->rr_lastResistor = rnew;
		*reslist = rnew;

		rcell = (resElement *)mallocMagic(sizeof(resElement));
		rcell->re_thisEl = rnew;
		rcell->re_nextEl = nb[i]->rn_re;
		nb[i]->rn_re = rcell;
		rcell = (resElement *)mallocMagic(sizeof(resElement));
		rcell->re_thisEl = rnew;
		rcell->re_nextEl = nb[j]->rn_re;
		nb[j]->rn_re = rcell;
	    }
	}

    /* Distribute the node capacitance and flags to the neighbors */
    for (i = 0; i < d; i++)
	nb[i]->rn_float.rn_area += node->rn_float.rn_area * g[i] / gtot;
    nb[imax]->rn_status |= (node->rn_status & RES_MAXTDI);

    /* Remove the node and its resistors */
    for (i = 0; i < d; i++)
    {
	ResDeleteResPointer(nb[i], nbres[i]);
	ResDeleteResPointer(node, nbres[i]);
	ResEliminateResistor(nbres[i], reslist);
    }
    ResCleanNode(node, TRUE, biglist, pendingList);
    return TRUE;
}

/*
 *-------------------------------------------------------------------------
 *
 * resPruneResistors --
 *
 *	Remove resistors that carry a negligible part of the current
 *	between their two nodes.  Removing a resistor R between nodes a
 *	and b increases the resistance between a and b, relative to its
 *	value, by at most R' / R, where R' is the resistance between a and
 *	b through the rest of the network.  Any single path from a to b is
 *	an upper bound on R'.  A resistor is removed if a path through one
 *	common neighbor of a and b (or a resistor in parallel) has less
 *	than "tolerance" times its resistance ("extresist prune").  A
 *	resistor which is the only connection between two parts of the
 *	network has no such path and is never removed.  Such resistors are
 *	typically produced when internal nodes are eliminated (see
 *	resEliminateNode()).
 *
 * Results:
 *	The number of resistors removed.
 *
 * Side Effects:
 *	Resistors are eliminated.
 *
 *-------------------------------------------------------------------------
 */

int
resPruneResistors(reslist, tolerance)
    resResistor	**reslist;
    float	tolerance;	/* Maximum relative change in resistance */
{
    resResistor *res, *next, *r1, *r2;
    resElement	*rcell;
    resNode	*a, *b, *c;
    float	path, best;
    int		count = 0;

    for (res = *reslist; res != NULL; res = next)
    {
	next = res->rr_nextResistor;
	a = res->rr_connection1;
	b = res->rr_connection2;
	if ((a == b) || (res->rr_value <= 0)) continue;

	/* Find the lowest resistance of a path from a to b of one or
	 * two resistors, not including res itself.
	 */
	best = -1.0;
	for (rcell = a->rn_re; rcell != NULL; rcell = rcell->re_nextEl)
	{
	    r1 = rcell->re_thisEl;
	    if (r1 == res) continue;
	    c = (r1->rr_connection1 == a) ? r1->rr_connection2 :
			r1->rr_connection1;
	    if (c == b)
		path = r1->rr_value;
	    else if ((r2 = resFindResistor(c, b)) != NULL)
		path = r1->rr_value + r2->rr_value;
	    else
		continue;
	    if ((best < 0) || (path < best)) best = path;
	}
	if ((best < 0) || (best >= tolerance * res->rr_value)) continue;

	ResDeleteResPointer(a, res);
	ResDeleteResPointer(b, res);
	ResEliminateResistor(res, reslist);
	count++;
    }
    return count;
}

/*
 *-------------------------------------------------------------------------
 *
 * ResEliminateNodes --
 *
 *	Combine all parallel resistors, then eliminate internal nodes of the
 *	network wherever this does not increase the number of resistors
 *	(see resEliminateNode()), taking nodes with fewer neighbors first.
 *	This removes, for example, the internal nodes of via arrays drawn as
 *	individual cuts, which form loops that cannot be reduced by series,
 *	parallel, and delta-wye combinations.  Node elimination is exact
 *	for the resistance between the remaining nodes.  Then remove
 *	resistors that carry a negligible part of the current (see
 *	resPruneResistors()), which changes the resistance between any two
 *	nodes by a small, bounded amount, and repeat.  If "prune" is zero,
 *	no resistors are removed.
 *
 * Results:
 *	The number of nodes eliminated.
 *
 * Side Effects:
 *	Nodes and resistors are eliminated or changed.
 *
 *-------------------------------------------------------------------------
 */

int
ResEliminateNodes(reslist, pendingList, biglist, prune)
    resResistor	**reslist;
    resNode	**pendingList, **biglist;
    float	prune;		/* Pruning tolerance, or 0 for none */
{
    resNode	*node, *next;
    int		maxdeg, count, total = 0;

    if (ResOptionsFlags & ResOpt_Geometry) return 0;

    /* Parallel resistors are not combined at ports and device terminals
     * during simplification;  combine them everywhere.
     */
    for (node = *biglist; node != NULL; node = node->rn_more)
	(void) resCombineParallel(node);

    /* Removing negligible resistors may allow more nodes to be
     * eliminated, so repeat until nothing more is removed.
     */
    do
    {
	maxdeg = 1;
	while (maxdeg <= RES_ELIM_MAXDEG)
	{
	    count = 0;
	    for (node = *biglist; node != NULL; node = next)
	    {
		next = node->rn_more;
		if (resEliminateNode(node, maxdeg, reslist, pendingList, biglist))
		    count++;
	    }
	    total += count;
	    if (count == 0) maxdeg++;
	}
    }
    while ((prune > 0) && (resPruneResistors(reslist, prune) > 0));

    return total;
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
ResScrunchNet(reslist, pendingList, biglist, tolerance)
    resResistor	**reslist;
    resNode	**pendingList, **biglist;
    float	tolerance;

{
    resResistor *current, *working;
    resNode	*node1, *node2;
    resElement  *rcell1;
    int	c1, c2, count = 0;

    /* Method used to sort resistors by size depends on list length */
    for (current = *reslist; current; current = current->rr_nextResistor)
    {
	count++;
	if (count >= 10) break;
    }
	
    /* Sort resistors by size */

    if (count >= 10)
    {
	int i;
	resResistor **resSortList;

	/* For long lists, sort using qsort() */
	/* NOTE:  It might be better to use the same merge sort used for
	 * MergeSortBreaks() in ResMakeRes.c, as it does not incur the
	 * overhead of allocating memory and populating the array.
	 */
	count = 0;
	for (current = *reslist; current; current = current->rr_nextResistor)
	    count++;

	resSortList = (resResistor **)mallocMagic(count * sizeof(resResistor *));

	count = 0;
	for (current = *reslist; current; current = current->rr_nextResistor)
	{
	    resSortList[count] = current;
	    count++;
	}

	/* Sort the list */

	qsort(resSortList, count, sizeof(resResistor *), qrescompare);

	/* Regenerate links on sorted list */
	for (i = 0; i < count; i++)
	{
	    current = resSortList[i];
	    current->rr_nextResistor = (i == count - 1) ? NULL : resSortList[i + 1];
	    current->rr_lastResistor = (i == 0) ? NULL : resSortList[i - 1];
	}
	*reslist = resSortList[0];
	
	freeMagic(resSortList);
    }
    else
    {
	/* Original method:  Walk the linked list and re-sort by size. */

	resResistor *locallist = NULL;

	current = *reslist;
	while (current != NULL)
	{
	    working = current;
	    current = current->rr_nextResistor;
	    if (working == *reslist)
		*reslist = current;
	    else
		working->rr_lastResistor->rr_nextResistor = current;

	    if (current != NULL)
		current->rr_lastResistor = working->rr_lastResistor;

	    ResAddResistorToList(working, &locallist);
	}
	*reslist = locallist;
    }

    current = *reslist;
    while (current != NULL && current->rr_value < tolerance)
    {
	if ((*reslist)->rr_nextResistor == NULL)
	   break;

	/* A resistor between two ports (or a port and the origin) cannot
	 * be eliminated without losing one of the port names, so leave it
	 * in the network.
	 */
	if ((current->rr_connection1->rn_why & (RES_NODE_ORIGIN | RES_NODE_SINK)) &&
		(current->rr_connection2->rn_why & (RES_NODE_ORIGIN | RES_NODE_SINK)))
	{
	    current = current->rr_nextResistor;
	    continue;
	}

	working = NULL;
	c1 = 0;
	c2 = 0;

	/* Search for next smallest adjoining resistor */
	for (rcell1 = current->rr_connection1->rn_re; rcell1 != NULL;
			rcell1 = rcell1->re_nextEl)
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
	for (rcell1 = current->rr_connection2->rn_re; rcell1 != NULL;
		    rcell1 = rcell1->re_nextEl)
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
	/* A resistor that shares neither node with another resistor cannot
	 * be merged into a neighbor (shouldn't happen).
	 */
	if (working == NULL)
	{
	    current = current->rr_nextResistor;
	    continue;
	}

	/*
	 * If the current resistor isn't a dead end, add its value and
	 * area to that of the next smallest one.  If it is a dead end,
	 * simply add its area to its node.
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
	 * Move everything from from one end of the ressistor to the
	 * other and eliminate the resistor.
	 */
	node2 = (current->rr_connection1 == node1) ? current->rr_connection2 :
			current->rr_connection1;
	ResDeleteResPointer(current->rr_connection1, current);
	ResDeleteResPointer(current->rr_connection2, current);

	/* Unlink "working" so that it can be reinserted in sorted order.
	 * Because resistors between ports are skipped above, "current" is
	 * not necessarily first in the list, and "working" may be.
	 */
	if (working->rr_lastResistor == NULL)
	    *reslist = working->rr_nextResistor;
	else
	    working->rr_lastResistor->rr_nextResistor = working->rr_nextResistor;
	if (working->rr_nextResistor != NULL)
	    working->rr_nextResistor->rr_lastResistor = working->rr_lastResistor;

	ResEliminateResistor(current, reslist);
	ResAddResistorToList(working, reslist);
	if (node2->rn_why & (RES_NODE_ORIGIN | RES_NODE_SINK))
	{
	    ResMergeNodes(node2, node1, pendingList, biglist);
	    node1 = node2;
	}
	else
	    ResMergeNodes(node1, node2, pendingList, biglist);

	/*
	 * Try further simplification on net using ResDoneWithNode and
	 * ResSimplifyNet.
	 */
	ResRemoveFromQueue(node1, biglist);
	ResAddToQueue(node1, pendingList);
	node1->rn_status &= ~(RES_DONE_ONCE | RES_FINISHED);
	ResDoneWithNode(node1);
	while (*pendingList != NULL)
	    ResSimplifyNet(pendingList, biglist, reslist, tolerance);

	/* The list has changed, so start again from the smallest */
	current = *reslist;
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
ResAddResistorToList(resistor, locallist)
    resResistor	*resistor, **locallist;

{
    resResistor *local, *last = NULL;

    for (local = *locallist; local != NULL; local = local->rr_nextResistor)
    {
	if (local->rr_value >= resistor->rr_value)
	    break;
	last = local;
    }
    if (local != NULL)
    {
	resistor->rr_nextResistor = local;
	resistor->rr_lastResistor = local->rr_lastResistor;
	if (local->rr_lastResistor == NULL)
	   *locallist = resistor;
	else
	   local->rr_lastResistor->rr_nextResistor = resistor;

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
ResDistributeCapacitance(nodelist, totalcap)
    resNode	*nodelist;
    float	totalcap;

{
    float totalarea = 0, capperarea;
    resNode	*workingNode;
    resElement	*rptr;

    for (workingNode = nodelist; workingNode != NULL; workingNode = workingNode->rn_more)
    {
     	for (rptr = workingNode->rn_re; rptr != NULL; rptr = rptr->re_nextEl)
	    if (rptr->re_thisEl->rr_float.rr_area != 0.0)
		TxError("Nonnull resistor area\n");

	totalarea += workingNode->rn_float.rn_area;
    }
    if (totalarea == 0)
    {
     	TxError("Error: Node with no area.\n");
	return;
    }
    capperarea = totalcap / totalarea;

    for (workingNode = nodelist; workingNode != NULL; workingNode = workingNode->rn_more)
     	workingNode->rn_float.rn_area *= capperarea;
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
    RCDelayStuff    *myC;
    resElement	    *workingRes;
    resDevice	    *dev;
    float	    childcap;
    tElement	    *tptr;
    int		    t;
    ExtDevice	    *devptr;

    if (me->rn_client != (ClientData) NULL) /* we have a loop */
	return(-1);

    myC = (RCDelayStuff *) mallocMagic((unsigned) (sizeof(RCDelayStuff)));
    me->rn_client = (ClientData) myC;

    /* This following assumes that ResDistributeCapacitance has been run */
    /* and the the resulting capacitance value is stored in the area field */
    myC->rc_Cdownstream = me->rn_float.rn_area;
    myC->rc_Tdi = 0.0;

    /* get capacitance for all connected gates */
    for (tptr = me->rn_te; tptr != NULL; tptr = tptr->te_nextt)
    {
     	dev = tptr->te_thist;
	/* Hack for non-Manhattan geometry.  Only one side of a split	*/
	/* tile should correspond to a device type.			*/
	if (IsSplit(dev->rd_tile))
	{
	    t = TiGetLeftType(dev->rd_tile);
	    if (ExtCurStyle->exts_device[t] == NULL)
		t = TiGetRightType(dev->rd_tile);
	}
	else
	    t = TiGetType(dev->rd_tile);
	if (dev->rd_fet_gate == me)
	{
	    devptr = ExtCurStyle->exts_device[t];
	    myC->rc_Cdownstream += dev->rd_length * dev->rd_width *
			devptr->exts_deviceGateCap +
	       		(dev->rd_width + dev->rd_width) *
			devptr->exts_deviceSDCap;

	}
    }

    /* Calculate child Capacitance  */
    for (workingRes = me->rn_re; workingRes != NULL; workingRes = workingRes->re_nextEl)
    {
	if (workingRes->re_thisEl->rr_connection1 == me &&
		(workingRes->re_thisEl->rr_status & RES_TDI_IGNORE) == 0)
	{
	    childcap = ResCalculateChildCapacitance(workingRes->re_thisEl->rr_connection2);
	    if (childcap == -1)
	        return(-1);

	    myC->rc_Cdownstream += childcap;
	}
    }
    return (myC->rc_Cdownstream);
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
ResCalculateTDi(node, resistor, resistorvalue)
    resNode	*node;
    resResistor *resistor;
    int		resistorvalue;

{
    resElement	    *workingRes;
    RCDelayStuff    *rcd = (RCDelayStuff *)node->rn_client;
    RCDelayStuff    *rcd2;

    ASSERT(rcd != NULL, "ResCalculateTdi");
    if (resistor == NULL)
        rcd->rc_Tdi = rcd->rc_Cdownstream * (float)resistorvalue;
    else
    {
        rcd2 = (RCDelayStuff *)resistor->rr_connection1->rn_client;
        ASSERT(rcd2 != NULL, "ResCalculateTdi");
        rcd->rc_Tdi = rcd->rc_Cdownstream * (float)resistor->rr_value +
	  		rcd2->rc_Tdi;
    }

    for (workingRes = node->rn_re; workingRes != NULL; workingRes = workingRes->re_nextEl)
    {
     	if (workingRes->re_thisEl->rr_connection1 == node &&
		    (workingRes->re_thisEl->rr_status & RES_TDI_IGNORE) == 0)
	     ResCalculateTDi(workingRes->re_thisEl->rr_connection2,
	       		  workingRes->re_thisEl,
	       		  workingRes->re_thisEl->rr_value);
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
ResPruneTree(node, minTdi, nodelist1, nodelist2, resistorlist)
    resNode	*node, **nodelist1, **nodelist2;
    float	minTdi;
    resResistor	**resistorlist;

{
    resResistor	*currentRes;
    resElement	*current;

    current = node->rn_re;
    while(current != NULL)
    {
     	currentRes = current->re_thisEl;
	current = current->re_nextEl;
	/* Ignore previously found loops */
	if (!(currentRes->rr_status & RES_TDI_IGNORE))
	    /* If branch points outward, call routine on subtrees */
	    if (currentRes->rr_connection1 == node)
		ResPruneTree(currentRes->rr_connection2, minTdi, nodelist1,
			nodelist2, resistorlist);
    }

    /* We eliminate this branch if:
     *   1.  It is a terminal node, i.e. it is the connected
     *	      to only one resistor.
     *	 2.  The direction of this resistor is toward the node
     *	      (This prevents the root from being eliminated
     *   3.  The time constant TDI is less than the tolerance.
     *   4.  It is not a port (which would lose the port name).
     */

    if (node->rn_re != NULL &&
	    node->rn_re->re_nextEl == NULL &&
	    node->rn_re->re_thisEl->rr_connection2 == node &&
	    !(node->rn_why & (RES_NODE_ORIGIN | RES_NODE_SINK)))
    {
     	if (node->rn_client == (ClientData)NULL)
	{
	    TxError("Internal Error in Tree Pruning: Missing TDi value.\n");
	}
	else if (((RCDelayStuff *)(node->rn_client))->rc_Tdi < minTdi)
	{
	    currentRes = node->rn_re->re_thisEl;
	    ResDeleteResPointer(currentRes->rr_connection1, currentRes);
	    ResDeleteResPointer(currentRes->rr_connection2, currentRes);
	    ResMergeNodes(currentRes->rr_connection1, currentRes->rr_connection2,
		    nodelist2, nodelist1);
	    ResEliminateResistor(currentRes, resistorlist);
	}
    }
}

/*
 *-------------------------------------------------------------------------
 *
 *-------------------------------------------------------------------------
 */

int
ResDoSimplify(resisdata)
    ResisData 	*resisdata;

{
    resNode 		*node, *slownode;
    float		bigres = 0.0;
    float		totalcap;
    resResistor		*res;

    ResSetPathRes(resisdata);

    for (node = ResNodeList; node != NULL; node = node->rn_more)
	bigres = MAX(bigres, node->rn_noderes);

    resisdata->rg_maxres = bigres;

#ifdef PARANOID
    ResSanityChecks("ExtractSingleNet", ResResList, ResNodeList, ResDevList);
#endif

    /* Is extracted network still greater than the tolerance?	*/
    /* Even if it isn't, we still let the next section run if   */
    /* we're calculating lumped values so that the capacitance  */
    /* values get calculated correctly.				*/

    (void) ResDistributeCapacitance(ResNodeList, resisdata->rg_nodecap);

    if (((ResOptionsFlags & ResOpt_Simplify) == 0) &&
		((ResOptionsFlags & ResOpt_DoLumpFile) == 0))
    	return 0;

    res = ResResList;
    while (res)
    {
    	resResistor	*oldres = res;

	res = res->rr_nextResistor;
    	oldres->rr_status &= ~RES_HEAP;
    }

    if (ResNodeAtOrigin == NULL)
    {
	TxError("Error:  Network simplification:  Failed to to get origin node.\n");
    	resisdata->rg_Tdi = 0;
    }
    else if (resisdata->mindelay > 0)
    {
	if ((resisdata->rg_nodecap != -1) &&
	 	(totalcap = ResCalculateChildCapacitance(ResNodeAtOrigin)) != -1)
	{
	    RCDelayStuff *rc = (RCDelayStuff *) ResNodeList->rn_client;

	    resisdata->rg_nodecap = totalcap;
	    ResCalculateTDi(ResNodeAtOrigin, (resResistor *)NULL, 0);
	    if (rc != (RCDelayStuff *)NULL)
		resisdata->rg_Tdi = rc->rc_Tdi;
	    else
		resisdata->rg_Tdi = 0;

	    slownode = ResNodeList;
	    for (node = ResNodeList; node != NULL; node = node->rn_more)
	    {
	      	rc = (RCDelayStuff *)node->rn_client;
		if ((rc != NULL) && (resisdata->rg_Tdi < rc->rc_Tdi))
		{
		    slownode = node;
		    resisdata->rg_Tdi = rc->rc_Tdi;
		}
	    }
	    slownode->rn_status |= RES_MAXTDI;
	}
	else
	    resisdata->rg_Tdi = -1;
    }
    else
    	resisdata->rg_Tdi = 0;

    /* Simplify network */

    if (ResOptionsFlags & ResOpt_Simplify)
    {
        /*
         * Start simplification at driver (R=0). Remove it from the done list
         * and add it to the pending list. Call ResSimplifyNet as long as
         * nodes remain in the pending list.
         */
	for (node = ResNodeList; node != NULL; node = node->rn_more)
	{
	    if (node->rn_noderes == 0)
	      	ResNodeAtOrigin = node;

	    node->rn_status |= RES_FINISHED;
	}
        if (ResNodeAtOrigin != NULL)
        {
            /* if Tdi is enabled, prune all branches whose end nodes	*/
	    /* have time constants less than the tolerance.		*/

	    if ((resisdata->rg_Tdi != -1) && (resisdata->mindelay > 0))
	        ResPruneTree(ResNodeAtOrigin, resisdata->mindelay,
		   	&ResNodeList, &ResNodeQueue, &ResResList);

	    ResNodeAtOrigin->rn_status &= ~RES_MARKED;
	    if (ResNodeAtOrigin->rn_less == CLIENTDEFAULT)
	    {
		TxError("ResSimplify:  Bad resptr at node %s origin.\n",
			ResNodeAtOrigin->rn_name);
		return 0;
	    }
	    else if (ResNodeAtOrigin->rn_less == NULL)
		ResNodeList = ResNodeAtOrigin->rn_more;
	    else
	        ResNodeAtOrigin->rn_less->rn_more = ResNodeAtOrigin->rn_more;

	    if (ResNodeAtOrigin->rn_more != NULL)
	        ResNodeAtOrigin->rn_more->rn_less = ResNodeAtOrigin->rn_less;

	    ResNodeAtOrigin->rn_more = NULL;
	    ResNodeAtOrigin->rn_less = NULL;
	    ResNodeQueue = ResNodeAtOrigin;
	    while (ResNodeQueue != NULL)
	        ResSimplifyNet(&ResNodeQueue, &ResNodeList, &ResResList,
				resisdata->minres);

	    /* Merge nodes that are connection points of the same port */
	    ResMergeSameNameNodes(&ResResList, &ResNodeQueue, &ResNodeList,
			resisdata->minres);

	    /*
	     * Call ResScrunchNet to eliminate any remaining under-tolerance
	     * resistors.
	     */
	    ResScrunchNet(&ResResList, &ResNodeQueue, &ResNodeList, resisdata->minres);

	    /* Eliminate internal nodes where this does not add resistors.
	     * This is exact, so do it last:  ResScrunchNet() approximates by
	     * folding small resistors into a neighbor, which is a poor
	     * approximation when a node is a hub of the reduced network.
	     */
	    ResEliminateNodes(&ResResList, &ResNodeQueue, &ResNodeList,
			resisdata->prune);
        }
    }
    return 0;
}

/*
 *-------------------------------------------------------------------------
 *
 *-------------------------------------------------------------------------
 */

void
ResSetPathRes(ResisData *resisdata)
{
    HeapEntry	he;
    resNode	*node;
    static int	init = 1;

    if (init)
    {
     	init = 0;
	HeapInit(&ResistorHeap, 128, FALSE, FALSE);
    }

    for (node = ResNodeList; node != NULL; node = node->rn_more)
    {
	if (node->rn_noderes == 0)
	{
	    ResNodeAtOrigin = node;
	    node->rn_status |= RES_FINISHED;
	}
	else
	{
	    node->rn_noderes = RES_INFINITY;
	    node->rn_status &= ~RES_FINISHED;
	}
    }
    if (ResNodeAtOrigin == NULL)
    {
	resDevice *res = ResGetDevice(resisdata->rg_devloc, resisdata->rg_ttype);
	if (res == (resDevice *)NULL)
	{
	    TxError("Error:  No device type %s found at location %s %s\n",
			DBTypeLongNameTbl[resisdata->rg_ttype],
			DBWPrintValue(resisdata->rg_devloc->p_x, (MagWindow *)NULL, TRUE),
			DBWPrintValue(resisdata->rg_devloc->p_y, (MagWindow *)NULL, FALSE));
	    return;
	}
	ResNodeAtOrigin = res->rd_fet_source;
	ResNodeAtOrigin->rn_why = RES_NODE_ORIGIN;
	ResNodeAtOrigin->rn_noderes = 0;
    }
    ASSERT(ResNodeAtOrigin != NULL, "ResDoSimplify");
    resPathNode(ResNodeAtOrigin);
    while (HeapRemoveTop(&ResistorHeap,&he))
	resPathRes((resResistor *)he.he_id);
}

/*
 *-------------------------------------------------------------------------
 * resPathNode ---
 *
 *	Given node "node", add every resistor connected to the node, and
 *	for which the node on the other side has not been processed, to
 *	the heap.  Node is marked with RES_FINISHED to prevent going 'round
 *	and 'round loops.
 *-------------------------------------------------------------------------
 */

void
resPathNode(node)
    resNode	*node;

{
    resElement	*re;

    node->rn_status |= RES_FINISHED;
    for (re = node->rn_re; re; re = re->re_nextEl)
    {
     	resResistor *res = re->re_thisEl;
	resNode	    *node2;

	if (res->rr_status & RES_HEAP) continue;
	if ((node2 = res->rr_node[0]) == node) node2 = res->rr_node[1];
	if ((node2->rn_status & RES_FINISHED) == 0)
	    HeapAddInt(&ResistorHeap,  node->rn_noderes + res->rr_value,
			(char *)res);
    }
}

/*
 *-------------------------------------------------------------------------
 *
 * resPathRes ---
 *
 *	Given resistor "res" pulled from the heap (in heap order, which
 *	is the highest valued resistor currently in the heap):
 *
 *	If both ends of the resistor have been processed, then we have
 *	a loop, and should return.
 *
 *	Otherwise, call resPathNode() on the node of whichever end of
 *	the resistor has not yet been processed, thus continuing through
 *	the path from the origin.
 *
 *-------------------------------------------------------------------------
 */

void
resPathRes(res)
    resResistor	*res;

{
    resNode *node0, *node1;
    int	    flag0, flag1;

    res->rr_status |= RES_HEAP;
    res->rr_status &= ~RES_MARKED;
    node0 = res->rr_node[0];
    node1 = res->rr_node[1];
    flag0 = node0->rn_status & RES_FINISHED;
    flag1 = node1->rn_status & RES_FINISHED;
    if (flag0 && flag1)
    {
        res->rr_status |= RES_TDI_IGNORE;
	/* Loop found---return without calling resPathNode() */
    }
    else if (flag0)
    {
     	node1->rn_noderes = node0->rn_noderes + res->rr_value;
	resPathNode(node1);
    }
    else
    {
     	ASSERT(flag1, "ResPathRes");
        res->rr_node[0] = node1;
	res->rr_node[1] = node0;
     	node0->rn_noderes = node1->rn_noderes + res->rr_value;
	resPathNode(node0);
    }
}
