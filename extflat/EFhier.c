/*
 * EFhier.c -
 *
 * Procedures for traversing the hierarchical representation
 * of a circuit.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/extflat/EFhier.c,v 1.5 2010/12/16 18:59:03 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <math.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "utils/hash.h"
#include "utils/malloc.h"
#include "utils/utils.h"
#include "extflat/extflat.h"
#include "extflat/EFint.h"

/*
 * ----------------------------------------------------------------------------
 *
 * efHierSrUses --
 *
 * Visit all the children of hc->hc_use->use_def, keeping the transform
 * to flat coordinates and the hierarchical path from the root up to date.
 * For each child, calls the function 'func', which should be of the
 * following form:
 *
 *	int
 *	(*func)(hc, cdata)
 *	    HierContext *hc;
 *	    ClientData cdata;
 *	{
 *	}
 *
 * This procedure should return 0 normally, or 1 to abort the search.
 *
 * Hierarchical names:
 *	The current hierarchical prefix down to this point is given by the
 *	the HierName pointed to by hc->hc_hierName.  To construct a full
 *	hierarchical name from a name local to this def, we prepend a
 *	newly allocated HierName component to hc->hc_hierName.
 *
 * Results:
 *	Returns 0 if completed successfully, 1 if aborted.
 *
 * Side effects:
 *	Whatever (*func)() does.
 *
 * ----------------------------------------------------------------------------
 */

int
efHierSrUses(hc, func, cdata)
    HierContext *hc;
    int (*func)();
    ClientData cdata;
{
    int xlo, xhi, ylo, yhi, xbase, ybase, xsep, ysep;
    HierContext newhc;
    Transform t;
    Use *u;

    for (u = hc->hc_use->use_def->def_uses; u; u = u->use_next)
    {
	newhc.hc_use = u;
	if (!IsArray(u))
	{
	    newhc.hc_hierName = efHNFromUse(&newhc, hc->hc_hierName);
	    GeoTransTrans(&u->use_trans, &hc->hc_trans, &newhc.hc_trans);
	    if ((*func)(&newhc, cdata))
		return (1);
	    continue;
	}

	/* Set up for iterating over all array elements */
	if (u->use_xlo <= u->use_xhi)
	    xlo = u->use_xlo, xhi = u->use_xhi, xsep = u->use_xsep;
	else
	    xlo = u->use_xhi, xhi = u->use_xlo, xsep = -u->use_xsep;
	if (u->use_ylo <= u->use_yhi)
	    ylo = u->use_ylo, yhi = u->use_yhi, ysep = u->use_ysep;
	else
	    ylo = u->use_yhi, yhi = u->use_ylo, ysep = -u->use_ysep;

	GeoTransTrans(&u->use_trans, &hc->hc_trans, &t);
	for (newhc.hc_x = xlo; newhc.hc_x <= xhi; newhc.hc_x++)
	    for (newhc.hc_y = ylo; newhc.hc_y <= yhi; newhc.hc_y++)
	    {
		xbase = xsep * (newhc.hc_x - u->use_xlo);
		ybase = ysep * (newhc.hc_y - u->use_ylo);
		GeoTransTranslate(xbase, ybase, &t, &newhc.hc_trans);
		newhc.hc_hierName = efHNFromUse(&newhc, hc->hc_hierName);
		if ((*func)(&newhc, cdata))
		    return (1);
	    }
    }

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * efHierSrArray --
 *
 * Iterate over the subscripts in the Connection 'conn', deriving the
 * names conn_name1 and conn_name2 for each such subscript, calling
 * the supplied procedure for each.
 *
 * This procedure should be of the following form:
 *
 *	(*proc)(hc, name1, name2, conn, cdata)
 *	    HierContext *hc;
 *	    char *name1;	/# Fully-expanded first name #/
 *	    char *name2;	/# Fully-expanded 2nd name, or NULL #/
 *	    Connection *conn;
 *	    ClientData cdata;
 *	{
 *	}
 *
 * The procedure should return 0 normally, or 1 if it wants us to abort.
 *
 * Results:
 *	0 normally, or 1 if we were aborted.
 *
 * Side effects:
 *	Whatever those of 'proc' are.
 *
 * ----------------------------------------------------------------------------
 */

int
efHierSrArray(hc, conn, proc, cdata)
    HierContext *hc;
    Connection *conn;
    int (*proc)();
    ClientData cdata;
{
    char name1[1024], name2[1024];
    int i, j, i1lo, i2lo, j1lo, j2lo;
    ConnName *c1, *c2;

    /*
     * Only handle three cases:
     *  0 subscripts
     *	1 subscript
     *	2 subscripts
     */
    c1 = &conn->conn_1;
    c2 = &conn->conn_2;
    switch (c1->cn_nsubs)
    {
	case 0:
	    return (*proc)(hc, c1->cn_name, c2->cn_name, conn, cdata);
	    break;
	case 1:
	    i1lo = c1->cn_subs[0].r_lo, i2lo = c2->cn_subs[0].r_lo;
	    for (i = i1lo; i <= c1->cn_subs[0].r_hi; i++)
	    {
		(void) sprintf(name1, c1->cn_name, i);
		if (c2->cn_name)
		    (void) sprintf(name2, c2->cn_name, i - i1lo + i2lo);
		if ((*proc)(hc, name1, c2->cn_name ? name2 : (char *) NULL,
				conn, cdata))
		    return 1;
	    }
	    break;
	case 2:
	    i1lo = c1->cn_subs[0].r_lo, i2lo = c2->cn_subs[0].r_lo;
	    j1lo = c1->cn_subs[1].r_lo, j2lo = c2->cn_subs[1].r_lo;
#ifdef	notdef
	    (void) printf("[%d:%d,%d:%d] [%d:%d,%d:%d]\n",
		i1lo, c1->cn_subs[0].r_hi,
		j1lo, c1->cn_subs[1].r_hi,
		i2lo, c2->cn_subs[0].r_hi,
		j2lo, c2->cn_subs[1].r_hi);
#endif	/* notdef */
	    for (i = i1lo; i <= c1->cn_subs[0].r_hi; i++)
	    {
		for (j = j1lo; j <= c1->cn_subs[1].r_hi; j++)
		{
		    (void) sprintf(name1, c1->cn_name, i, j);
		    if (c2->cn_name)
			(void) sprintf(name2, c2->cn_name,
				    i - i1lo + i2lo, j - j1lo + j2lo);
		    if ((*proc)(hc,name1,c2->cn_name ? name2 : (char *) NULL,
				conn, cdata))
			return 1;
		}
	    }
	    break;
	default:
	    printf("Can't handle > 2 array subscripts\n");
	    break;
    }

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * EFHierSrDefs ---
 *
 *	Traverse the cell definition hierarchy, processing each cell once.
 *	For each definition, call func() with client data argument cdata.
 *	If func() is NULL, then traverse the hierarchy and mark all cell
 *	definitions as unprocessed.
 *
 * ----------------------------------------------------------------------------
 */

int
EFHierSrDefs(hc, func, cdata)
    HierContext *hc;
    int (*func)();
    ClientData cdata;
{
    HierContext newhc;
    Use *u;
    int retval;

    if (func == NULL)
    {
	if (!(hc->hc_use->use_def->def_flags & DEF_PROCESSED))
	    return 0;
	hc->hc_use->use_def->def_flags &= ~DEF_PROCESSED;
    }
    else
    {
	if (hc->hc_use->use_def->def_flags & DEF_PROCESSED)
	    return 0;
	hc->hc_use->use_def->def_flags |= DEF_PROCESSED;
    }

    for (u = hc->hc_use->use_def->def_uses; u; u = u->use_next)
    {
	newhc.hc_use = u;
	newhc.hc_hierName = NULL;
	GeoTransTrans(&u->use_trans, &hc->hc_trans, &newhc.hc_trans);
	if (EFHierSrDefs(&newhc, func, cdata))
	    return 1;
    }
    if (func == NULL)
	return 0;
    else
    {
	/* Clear DEF_PROCESSED for the duration of running the function */

	hc->hc_use->use_def->def_flags &= ~DEF_PROCESSED;
	retval = (*func)(hc, cdata);
	hc->hc_use->use_def->def_flags |= DEF_PROCESSED;
	return retval;
    }
}

/*----------------------------------------------------------------------*/
/* All the routines below have been copied and modified from EFvisit.c.	*/
/* They are used for hierarchical output, such as a netlist for LVS.	*/
/*----------------------------------------------------------------------*/

/*
 * ----------------------------------------------------------------------------
 *
 * EFHierVisitSubcircuits --
 *
 * Visit all of the "defined" subcircuits in the circuit.
 * This is meant to provide a generic functionality similar to
 * the transistor/resistor/capacitor extraction.  It assumes that the
 * end-user has an existing description of the extracted subcircuit,
 * such as a characterized standard cell, and that magic is not to
 * attempt an extraction itself, but only to call the predefined
 * subcircuit, matching nodes to the subcircuit's port list.
 *
 * For each def encountered, call the user-supplied procedure
 * (*subProc)(), which should be of the following form:
 *
 * 	(*subProc)(hc, use, hierName)
 *	    HierContext *hc;
 *	    bool is_top;
 *	{
 *	}
 *
 * The procedure should return 0 normally, or 1 to abort the search.
 *
 * Results:
 *	Returns 0 if terminated normally, or 1 if the search
 *	was aborted.
 *
 * Side effects:
 *	Whatever (*subProc)() does.
 *
 * ----------------------------------------------------------------------------
 */

int
EFHierVisitSubcircuits(hc, subProc, cdata)
    HierContext *hc;
    int (*subProc)();
    ClientData cdata;	/* unused */
{
    CallArg ca;
    int efHierVisitSubcircuits();   /* Forward declaration */

    /* For each subcell of the top-level def that is defined as */
    /* a subcircuit, call subProc.				*/

    ca.ca_proc = subProc;
    ca.ca_cdata = (ClientData)hc->hc_use->use_def;	/* Save top-level def */

    if (efHierSrUses(hc, efHierVisitSubcircuits, (ClientData) &ca))
	return 1;

    return 0;
}

/*
 * Procedure to visit recursively all subcircuits in the design.
 * Does all the work of EFHierVisitSubcircuits() above.
 *
 * Results:
 *	Returns 0 to keep efHierSrUses going.
 *
 * Side effects:
 *	Calls the client procedure (*ca->ca_proc)().
 */

int
efHierVisitSubcircuits(hc, ca)
    HierContext *hc;
    CallArg *ca;
{
    /* Visit all children of this def */
    Def *def = (Def *)ca->ca_cdata;
    bool is_top = (def == hc->hc_use->use_def) ? TRUE : FALSE;

    if ((*ca->ca_proc)(hc->hc_use, hc->hc_hierName, is_top))
	return 1;
    else
	return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * efHierDevKilled --
 *
 * Check all of the nodes to which the dev 'dev' is connected.
 * If any of these nodes have been killed, then the dev is also killed.
 *
 * Results:
 *      TRUE if the dev is connected to a killed node, FALSE if it's ok.
 *
 * Side effects:
 *      None.
 *
 * ----------------------------------------------------------------------------
 */

bool
efHierDevKilled(hc, dev, prefix)
    HierContext *hc;
    Dev *dev;
    HierName *prefix;
{
    HierName *suffix;
    HashEntry *he;
    EFNodeName *nn;
    int n;
    Def *def = hc->hc_use->use_def;

    for (n = 0; n < dev->dev_nterm; n++)
    {
	suffix = dev->dev_terms[n].dterm_node->efnode_name->efnn_hier;
	he = HashLookOnly(&efNodeHashTable, (char *)suffix);
	if (he  && (nn = (EFNodeName *) HashGetValue(he))
		&& (nn->efnn_node->efnode_flags & EF_KILLED))
	    return TRUE;
    }

    return FALSE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * EFHierVisitDevs --
 *
 * Visit all the devs in the circuit.
 * For each dev in the circuit, call the user-supplied procedure
 * (*devProc)(), which should be of the following form:
 *
 *	(*devProc)(hc, dev, scale, cdata)
 *	    HierContext *hc;
 *	    Dev *dev;
 *	    float scale;
 *	    ClientData cdata;
 *	{
 *	}
 *
 * The procedure should return 0 normally, or 1 to abort the
 * search.
 *
 * We ensure that no devs connected to killed nodes are passed
 * to this procedure.
 *
 * Results:
 *	Returns 0 if terminated normally, or 1 if the search
 *	was aborted.
 *
 * Side effects:
 *	Whatever (*devProc)() does.
 *
 * ----------------------------------------------------------------------------
 */

int
EFHierVisitDevs(hc, devProc, cdata)
    HierContext *hc;
    int (*devProc)();
    ClientData cdata;
{
    CallArg ca;

    ca.ca_proc = devProc;
    ca.ca_cdata = cdata;
    return efHierVisitDevs(hc, (ClientData) &ca);
}

/*
 * Procedure to visit recursively all devs in the design.
 * Does all the work of EFHierVisitDevs() above.
 *
 * Results:
 *	Returns 0 to keep efHierSrUses going.
 *
 * Side effects:
 *	Calls the client procedure (*ca->ca_proc)().
 */

int
efHierVisitDevs(hc, ca)
    HierContext *hc;
    CallArg *ca;
{
    Def *def = hc->hc_use->use_def;
    Dev *dev;
    float scale;

    /*
     * Note that the transform passed does not transform
     * the scale;  where def->def_scale != 1.0, the visited
     * procedure will have to multiply values by def->def_scale.
     */

    scale = (efScaleChanged && def->def_scale != 1.0) ? def->def_scale : 1.0;

    /* Visit all devices */
    for (dev = def->def_devs; dev; dev = dev->dev_next)
    {
	if (efHierDevKilled(hc, dev, hc->hc_hierName))
	    continue;

	if ((*ca->ca_proc)(hc, dev, scale, ca->ca_cdata))
	    return 1;
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * efHierVisitSingleResist --
 *
 * Visit a resistor of res->conn_res milliohms between the nodes
 * 'name1' and 'name2' (text names, not hierarchical names).  Don't
 * process the resistor if either terminal is a killed node.
 *
 * Results:
 *	Whatever the user-supplied procedure (*ca->ca_proc)() returns
 *	(type int).
 *
 * Side effects:
 *	Calls the user-supplied procedure.
 *
 * ----------------------------------------------------------------------------
 */

int
efHierVisitSingleResist(hc, name1, name2, res, ca)
    HierContext *hc;		/* Contains hierarchical pathname to cell */
    char *name1, *name2;	/* Names of nodes connecting to resistor */
    Connection *res;		/* Contains resistance to add */
    CallArg *ca;
{
    EFNode *n1, *n2;
    HashEntry *he;
    Def *def = hc->hc_use->use_def;

    if ((he = HashLookOnly(&def->def_nodes, name1)) == NULL)
	return 0;
    n1 = ((EFNodeName *) HashGetValue(he))->efnn_node;
    if (n1->efnode_flags & EF_KILLED)
	return 0;

    if ((he = HashLookOnly(&def->def_nodes, name2)) == NULL)
	return 0;
    n2 = ((EFNodeName *) HashGetValue(he))->efnn_node;
    if (n2->efnode_flags & EF_KILLED)
	return 0;

    /* Do nothing if the nodes aren't different */
    if (n1 == n2)
	return 0;

    return (*ca->ca_proc)(hc, n1->efnode_name->efnn_hier,
		n2->efnode_name->efnn_hier,
		res->conn_res, ca->ca_cdata);
}

/*
 * ----------------------------------------------------------------------------
 *
 * EFHierVisitResists --
 *
 * Visit all the resistors in the circuit.
 * For each resistor in the circuit, call the user-supplied procedure
 * (*resProc)(), which should be of the following form, where hn1 and
 * hn2 are the HierNames of the two nodes connected by the resistor.
 *
 *	(*resProc)(hc, hn1, hn2, resistance, cdata)
 *	    HierContext *hc;
 *	    HierName *hn1, *hn2;
 *	    int resistance;
 *	    ClientData cdata;
 *	{
 *	}
 *
 * The procedure should return 0 normally, or 1 to abort the
 * search.
 *
 * We ensure that no resistors connected to killed nodes are passed
 * to this procedure.
 *
 * Results:
 *	Returns 0 if terminated normally, or 1 if the search
 *	was aborted.
 *
 * Side effects:
 *	Whatever (*resProc)() does.
 *
 * ----------------------------------------------------------------------------
 */

int
EFHierVisitResists(hc, resProc, cdata)
    HierContext *hc;
    int (*resProc)();
    ClientData cdata;
{
    CallArg ca;
    int efHierVisitResists();	/* Forward reference */

    ca.ca_proc = resProc;
    ca.ca_cdata = cdata;
    return efHierVisitResists(hc, (ClientData) &ca);
}

/*
 * Procedure to visit recursively all resistors in the design.
 * Does all the work of EFHierVisitResists() above.
 *
 * Results:
 *	Returns 0 to keep efHierSrUses going.
 *
 * Side effects:
 *	Calls the client procedure (*ca->ca_proc)().
 */

int
efHierVisitResists(hc, ca)
    HierContext *hc;
    CallArg *ca;
{
    Def *def = hc->hc_use->use_def;
    Connection *res;
    Transform t;
    int scale;

    /* Visit all resistors */
    for (res = def->def_resistors; res; res = res->conn_next)
    {
	/* Special case for speed if no arraying info */
	if (res->conn_1.cn_nsubs == 0)
	{
	    if (efHierVisitSingleResist(hc, res->conn_name1, res->conn_name2,
			res, ca))
		return 1;
	}
	else if (efHierSrArray(hc, res, efHierVisitSingleResist, (ClientData) ca))
	    return 1;
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * EFHierVisitCaps --
 *
 * Visit all the local capacitance records
 * Calls the user-provided procedure (*capProc)()
 * which should be of the following format:
 *
 *	(*capProc)(hc, hierName1, hierName2, cap, cdata)
 *	    HierContext *hc;
 *	    HierName *hierName1, *hierName2;
 *	    EFCapValue cap;
 *	    ClientData cdata;
 *	{
 *	}
 *
 * Here cap is the capacitance in attofarads.
 *
 * Results:
 *	Returns 1 if the client procedure returned 1;
 *	otherwise returns 0.
 *
 * Side effects:
 *	Calls the user-provided procedure (*capProc)().
 *
 * ----------------------------------------------------------------------------
 */

int
EFHierVisitCaps(hc, capProc, cdata)
    HierContext *hc;
    int (*capProc)();
    ClientData cdata;
{
    HashSearch hs;
    HashEntry *he;
    EFCoupleKey *ck;
    EFCapValue ccap;

    /* Visit capacitors flattened from a lower level, as well	*/
    /* as our own.  These have been created and saved in	*/
    /* efCapHashTable using efFlatCaps().			*/

    HashStartSearch(&hs);
    while (he = HashNext(&efCapHashTable, &hs))
    {
	ccap = CapHashGetValue(he);
	ck = (EFCoupleKey *) he->h_key.h_words;
	if ((*capProc)(hc, ck->ck_1->efnode_name->efnn_hier,
			ck->ck_2->efnode_name->efnn_hier,
			(double) ccap, cdata))
	    return 1;
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * EFHierVisitNodes --
 *
 * Visit all the flat nodes in the circuit.
 * Calls the user-provided procedure (*nodeProc)()
 * which should be of the following format:
 *
 *	(*nodeProc)(hc, hierName1, hierName2, res, cap, cdata)
 *	    HierContext *hc;
 *	    HierName *hierName1, *hierName2;
 *	    int res;
 *	    EFCapValue cap;
 *	    ClientData cdata;
 *	{
 *	}
 *
 * Here cap is the lumped capacitance to substrate in attofarads,
 * and res is the lumped resistance to substrate in milliohms.
 *
 * Results:
 *	Returns 1 if the client procedure returned 1;
 *	otherwise returns 0.
 *
 * Side effects:
 *	Calls the user-provided procedure (*nodeProc)().
 *
 * ----------------------------------------------------------------------------
 */

int
EFHierVisitNodes(hc, nodeProc, cdata)
    HierContext *hc;
    int (*nodeProc)();
    ClientData cdata;
{
    Def *def = hc->hc_use->use_def;
    EFCapValue cap;
    int res;
    EFNode *snode;
    HierName *hierName;

    for (snode = (EFNode *) efNodeList.efnode_next;
            snode != &efNodeList;
            snode = (EFNode *) snode->efnode_next)
    {
	res = EFNodeResist(snode);
	cap = snode->efnode_cap;
	hierName = (HierName *) snode->efnode_name->efnn_hier;
	if (snode->efnode_flags & EF_SUBS_NODE)
	    cap = 0;

	if (snode->efnode_flags & EF_KILLED) continue;

	if ((*nodeProc)(hc, snode, res, (double)cap, cdata))
	    return 1;
    }
    return 0;
}

