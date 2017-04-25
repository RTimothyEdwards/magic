/*
 * EFvisit.c -
 *
 * Procedures to traverse and output flattened nodes, capacitors,
 * transistors, resistors, and Distances.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/extflat/EFvisit.c,v 1.5 2010/08/10 00:18:45 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "utils/hash.h"
#include "utils/malloc.h"
#include "utils/utils.h"
#include "extflat/extflat.h"
#include "extflat/EFint.h"
#include "extract/extract.h"

/* Root of the tree being flattened */
extern Def *efFlatRootDef;
extern Use efFlatRootUse;
extern HierContext efFlatContext;

extern void efDevFixLW();
extern void efHNOutPrefix();

bool efDevKilled();

/*
 * ----------------------------------------------------------------------------
 *
 * EFVisitSubcircuits --
 *
 * Visit all of the "defined" subcircuits in the circuit.
 * This is meant to provide a generic functionality similar to
 * the transistor/resistor/capacitor extraction.  It assumes that the
 * end-user has an existing description of the extracted subcircuit,
 * such as a characterized standard cell, and that magic is not to
 * attempt an extraction itself, but only to call the predefined
 * subcircuit, matching nodes to the subcircuit's port list.
 *
 * For each def encountered which has the DEF_SUBCIRCUIT flag set,
 * call the user-supplied procedure (*subProc)(), which should be of
 * the following form:
 *
 * 	(*subProc)(use, hierName, is_top)
 *	    Use *use;
 *	    HierName *hierName;
 *	    Boolean is_top;
 *	{
 *	}
 *
 * is_top will be TRUE for the top-level cell, and FALSE for all
 * other cells.  The procedure should return 0 normally, or 1 to abort
 * the search.
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
EFVisitSubcircuits(subProc, cdata)
    int (*subProc)();
    ClientData cdata;
{
    CallArg ca;
    HierContext *hc;
    int efVisitSubcircuits();   /* Forward declaration */

    /* If the top-level def is defined as a subcircuit, call topProc */

    hc = &efFlatContext;
    if (hc->hc_use->use_def->def_flags & DEF_SUBCIRCUIT)
	if ((*subProc)(hc->hc_use, hc->hc_hierName, TRUE))
	    return 1;

    /* For each subcell of the top-level def that is defined as */
    /* a subcircuit, call subProc.				*/

    ca.ca_proc = subProc;
    ca.ca_cdata = cdata;

    if (efHierSrUses(hc, efVisitSubcircuits, (ClientData) &ca))
	return 1;

    return 0;
}

/*
 * Procedure to visit recursively all subcircuits in the design.
 * Does all the work of EFVisitSubcircuits() above.
 *
 * Results:
 *	Returns 0 to keep efHierSrUses going.
 *
 * Side effects:
 *	Calls the client procedure (*ca->ca_proc)().
 */

int
efVisitSubcircuits(hc, ca)
    HierContext *hc;
    CallArg *ca;
{
    /* Look for children of this def which are defined	*/
    /* as subcircuits via the DEF_SUBCIRCUIT flag.	*/

    if (hc->hc_use->use_def->def_flags & DEF_SUBCIRCUIT)
    {
	if ((*ca->ca_proc)(hc->hc_use, hc->hc_hierName, NULL))
	    return 1;
	else
	    return 0;
    }

    /* Recursively visit subcircuits in our children last. */

    if (efHierSrUses(hc, efVisitSubcircuits, (ClientData) ca))
	return 1;

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * EFGetLengthAndWidth --
 *
 *	Estimate length and width for a device from area and perimeter values.
 *	Mostly this routine is meant to handle the older "fet" record.
 *	Newer "device" types should have length and width properly determined
 *	already, and we just return those values from the device structure.
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	Values substituted for length and width.
 *
 * ----------------------------------------------------------------------------
 */

void
EFGetLengthAndWidth(dev, lptr, wptr)
    Dev *dev;
    int *lptr;
    int *wptr;
{
    DevTerm *gate, *source, *drain;
    int area, perim, l, w;

    switch (dev->dev_class)
    {
	case DEV_MOSFET:
	case DEV_ASYMMETRIC:
	case DEV_BJT:
	case DEV_SUBCKT:
	case DEV_MSUBCKT:
	case DEV_RSUBCKT:
	case DEV_DIODE:
	case DEV_PDIODE:
	case DEV_NDIODE:
	case DEV_CAP:
	case DEV_CAPREV:
	case DEV_RES:
	    l = dev->dev_length;
	    w = dev->dev_width;
	    break;

	case DEV_FET:
	    area = dev->dev_area;
	    perim = dev->dev_perim;

	    gate = &dev->dev_terms[0];

	    /*
	     * L, W, and flat coordinates of a point inside the channel.
	     * Handle FETs with two terminals (capacitors) separately.
	     */

	    if (dev->dev_nterm == 2)
	    {
		/* Convert area to type double to avoid overflow in	*/
		/* extreme cases.					*/
		l = perim - (int)sqrt((double)(perim * perim) - 16 * (double)area);
		l >>= 2;
		w = area / l;
	    }
	    else
	    {
		source = drain = &dev->dev_terms[1];
		if (dev->dev_nterm >= 3)
		    drain = &dev->dev_terms[2];
		l = gate->dterm_length / 2;
		w = (source->dterm_length + drain->dterm_length) / 2;
	    }
	    if (gate->dterm_attrs) efDevFixLW(gate->dterm_attrs, &l, &w);
	    break;

	default:
	    l = w = 0;
	    break;
    }

    *lptr = l;
    *wptr = w;
}

/*
 * ----------------------------------------------------------------------------
 *
 * EFVisitDevs --
 *
 * Visit all the devs in the circuit.
 * Must be called after EFFlatBuild().
 * For each dev in the circuit, call the user-supplied procedure
 * (*devProc)(), which should be of the following form:
 *
 *	(*devProc)(dev, hierName, scale, cdata)
 *	    Dev *dev;
 *	    HierName *hierName;
 *	    float scale;
 *	    Transform *trans;
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
EFVisitDevs(devProc, cdata)
    int (*devProc)();
    ClientData cdata;
{
    CallArg ca;

    ca.ca_proc = devProc;
    ca.ca_cdata = cdata;
    return efVisitDevs(&efFlatContext, (ClientData) &ca);
}

/*
 * Procedure to visit recursively all devs in the design.
 * Does all the work of EFVisitDevs() above.
 *
 * Results:
 *	Returns 0 to keep efHierSrUses going.
 *
 * Side effects:
 *	Calls the client procedure (*ca->ca_proc)().
 */

int
efVisitDevs(hc, ca)
    HierContext *hc;
    CallArg *ca;
{
    Def *def = hc->hc_use->use_def;
    Dev *dev;
    float scale;
    Transform t;

    if (def->def_flags & DEF_SUBCIRCUIT) return 0;

    /* Recursively visit devs in our children first */
    if (efHierSrUses(hc, efVisitDevs, (ClientData) ca))
	return 1;

    scale = (efScaleChanged && def->def_scale != 1.0) ? def->def_scale : 1.0;
    t = hc->hc_trans;
  
    /* Visit our own devices */
    for (dev = def->def_devs; dev; dev = dev->dev_next)
    {
	if (efDevKilled(dev, hc->hc_hierName))
	    continue;

	if ((*ca->ca_proc)(dev, hc->hc_hierName, scale, &t, ca->ca_cdata))
	    return 1;
    }

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * efDevKilled --
 *
 * Check all of the nodes to which the dev 'dev' is connected (its
 * hierarchical prefix is hc->hc_hierName).  If any of these nodes
 * have been killed, then the dev is also killed.
 *
 * Results:
 *	TRUE if the dev is connected to a killed node, FALSE if it's ok.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
efDevKilled(dev, prefix)
    Dev *dev;
    HierName *prefix;
{
    HierName *suffix;
    HashEntry *he;
    EFNodeName *nn;
    int n;

    for (n = 0; n < dev->dev_nterm; n++)
    {
	suffix = dev->dev_terms[n].dterm_node->efnode_name->efnn_hier;
	he = EFHNConcatLook(prefix, suffix, "kill");
	if (he  && (nn = (EFNodeName *) HashGetValue(he))
		&& (nn->efnn_node->efnode_flags & EF_KILLED))
	    return TRUE;
    }

    return FALSE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * efDevFixLW --
 *
 * Called for any devs that have gate attributes; these attributes may
 * specify the L and W of the dev explicitly.  The attributes will be
 * of the form ext:l=value or ext:w=value, where value is either numerical
 * or symbolic; if symbolic the symbol must have been defined via efSymAdd().
 * If the value is symbolic but wasn't defined by efSymAdd(), it's ignored.
 * The variables *pL and *pW are changed to reflect the new L and W as
 * appropriate.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

void
efDevFixLW(attrs, pL, pW)
    char *attrs;
    int *pL, *pW;
{
    char *cp, *ep;
    char attrName, savec;
    int value;

    cp = attrs;
    while (cp && *cp)
    {
	if (*cp != 'e' || strncmp(cp, "ext:", 4) != 0)
	    goto skip;

	cp += 4;
	if (*cp && cp[1] == '=')
	{
	    switch (*cp)
	    {
		case 'w': case 'W':
		    attrName = 'w';
		    goto both;
		case 'l': case 'L':
		    attrName = 'l';
		both:
		    cp += 2;
		    for (ep = cp; *ep && *ep != ','; ep++)
			/* Nothing */;
		    savec = *ep;
		    *ep = '\0';
		    if (StrIsInt(cp)) value = atoi(cp);
		    else if (!efSymLook(cp, &value)) goto done;

		    if (attrName == 'w')
			*pW = value;
		    else if (attrName == 'l')
			*pL = value;

		done:
		    *ep = savec;
	    }
	}

skip:
	/* Skip to next attribute */
	while (*cp && *cp++ != ',')
	    /* Nothing */;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * EFVisitResists --
 *
 * Visit all the resistors in the circuit.
 * Must be called after EFFlatBuild().
 * For each resistor in the circuit, call the user-supplied procedure
 * (*resProc)(), which should be of the following form, where hn1 and
 * hn2 are the HierNames of the two nodes connected by the resistor.
 *
 *	(*resProc)(hn1, hn2, resistance, cdata)
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
EFVisitResists(resProc, cdata)
    int (*resProc)();
    ClientData cdata;
{
    CallArg ca;

    ca.ca_proc = resProc;
    ca.ca_cdata = cdata;
    return efVisitResists(&efFlatContext, (ClientData) &ca);
}

/*
 * Procedure to visit recursively all resistors in the design.
 * Does all the work of EFVisitResists() above.
 *
 * Results:
 *	Returns 0 to keep efHierSrUses going.
 *
 * Side effects:
 *	Calls the client procedure (*ca->ca_proc)().
 */

extern int efVisitSingleResist();

int
efVisitResists(hc, ca)
    HierContext *hc;
    CallArg *ca;
{
    Def *def = hc->hc_use->use_def;
    Connection *res;

    /* Ignore subcircuits */
    if (def->def_flags & DEF_SUBCIRCUIT) return 0;

    /* Recursively visit resistors in our children first */
    if (efHierSrUses(hc, efVisitResists, (ClientData) ca))
	return 1;

    /* Visit our own resistors */
    for (res = def->def_resistors; res; res = res->conn_next)
    {
	/* Special case for speed if no arraying info */
	if (res->conn_1.cn_nsubs == 0)
	{
	    if (efVisitSingleResist(hc, res->conn_name1, res->conn_name2,
			res, ca))
		return 1;
	}
	else if (efHierSrArray(hc, res, efVisitSingleResist, (ClientData) ca))
	    return 1;
    }

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * efVisitSingleResist --
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
efVisitSingleResist(hc, name1, name2, res, ca)
    HierContext *hc;		/* Contains hierarchical pathname to cell */
    char *name1, *name2;	/* Names of nodes connecting to resistor */
    Connection *res;		/* Contains resistance to add */
    CallArg *ca;
{
    EFNode *n1, *n2;
    HashEntry *he;

    if ((he = EFHNLook(hc->hc_hierName, name1, "resist(1)")) == NULL)
	return 0;
    n1 = ((EFNodeName *) HashGetValue(he))->efnn_node;
    if (n1->efnode_flags & EF_KILLED)
	return 0;

    if ((he = EFHNLook(hc->hc_hierName, name2, "resist(2)")) == NULL)
	return 0; 
    n2 = ((EFNodeName *) HashGetValue(he))->efnn_node;
    if (n2->efnode_flags & EF_KILLED)
	return 0;

    /* Do nothing if the nodes aren't different */
    if (n1 == n2)
	return 0;

    return (*ca->ca_proc)(n1->efnode_name->efnn_hier,
		n2->efnode_name->efnn_hier,
		res->conn_res, ca->ca_cdata);
}

/*
 * ----------------------------------------------------------------------------
 *
 * EFVisitCaps --
 *
 * Visit all the capacitors built up by efFlatCaps.
 * Calls the user-provided procedure (*capProc)()
 * which should be of the following format:
 *
 *	(*capProc)(hierName1, hierName2, cap, cdata)
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
EFVisitCaps(capProc, cdata)
    int (*capProc)();
    ClientData cdata;
{
    HashSearch hs;
    HashEntry *he;
    EFCoupleKey *ck;
    EFCapValue cap;

    HashStartSearch(&hs);
    while (he = HashNext(&efCapHashTable, &hs))
    {
	cap = CapHashGetValue(he);
	ck = (EFCoupleKey *) he->h_key.h_words;
	if ((*capProc)(ck->ck_1->efnode_name->efnn_hier,
		       ck->ck_2->efnode_name->efnn_hier,
		       (double) cap, cdata))
	    return 1;
    }

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * EFVisitNodes --
 *
 * Procedure to visit all flat nodes in the circuit.
 * For each node, calls the procedure (*nodeProc)(),
 * which should be of the following form:
 *
 *	(*nodeProc)(node, r, c, cdata)
 *	    EFNode *node;
 *          int r;
 *          EFCapValue c;
 *	    ClientData cdata;
 *	{
 *	}
 *
 * Where 'r' and 'c' are the lumped resistance estimate
 * and capacitance to ground, in milliohms and attofarads
 * respectively.  When either falls below the threshold
 * for output, they are passed as 0.
 *
 * Results:
 *	Returns 1 if (*nodeProc)() returned 1 to abort the
 *	search; otherwise, returns 0.
 *
 * Side effects:
 *	Calls (*nodeProc)().
 *
 * ----------------------------------------------------------------------------
 */

int
EFVisitNodes(nodeProc, cdata)
    int (*nodeProc)();
    ClientData cdata;
{
    EFNode *node;
    EFNodeName *nn;
    HierName *hierName;
    EFCapValue cap;
    int res;

    for (node = (EFNode *) efNodeList.efnode_next;
	    node != &efNodeList;
	    node = (EFNode *) node->efnode_next)
    {
	res = EFNodeResist(node);
	cap = node->efnode_cap;
	hierName = (HierName *) node->efnode_name->efnn_hier;
	if (EFCompat)
	{
	    if (EFHNIsGND(hierName))
		cap = 0;
	}
	else
	{
	    if (node->efnode_flags & EF_SUBS_NODE)
		cap = 0;
	}
	if (efWatchNodes)
	{
	    for (nn = node->efnode_name; nn; nn = nn->efnn_next)
		if (HashLookOnly(&efWatchTable, (char *) nn->efnn_hier))
		{
		    TxPrintf("Equivalent nodes:\n");
		    for (nn = node->efnode_name; nn; nn = nn->efnn_next)
			TxPrintf("\t%s\n", EFHNToStr(nn->efnn_hier));
		    break;
		}
	}

	if (node->efnode_flags & EF_KILLED)
	    continue;

	if ((*nodeProc)(node, res, (double) cap, cdata))
	    return 1;
    }

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * EFNodeResist --
 *
 * The input to this procedure is a pointer to a EFNode.
 * Its resistance is computed from the area and perimeter stored
 * in the array efnode_pa.
 *
 * We approximate the resistive region as a collection of rectangles
 * of width W and length L, one for each set of layers having a different
 * sheet resistivity.  We do so by noting that for a rectangle,
 *
 *		Area = L * W
 *		Perimeter = 2 * (L + W)
 *
 * Solving the two simultaneous equations for L yields the following
 * quadratic:
 *
 *		2 * (L**2) - Perimeter * L + 2 * Area = 0
 *
 * Solving this quadratic for L, the longer dimension, we get
 *
 *		L = (Perimeter + S) / 4
 *
 * where
 *
 *		S = sqrt( (Perimeter**2) - 16 * Area )
 *
 * The smaller dimension is W, ie,
 *
 *		W = (Perimeter - S) / 4
 *
 * The resistance is L / W squares:
 *
 *			Perimeter + S
 *		R =	-------------
 *			Perimeter - S
 *
 * Results:
 *	Returns the resistance.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
EFNodeResist(node)
    EFNode *node;
{
    int n, perim, area;
    float s, fperim;
    double v, dresist;
    int resist;

    resist = 0;
    for (n = 0; n < efNumResistClasses; n++)
    {
	area = node->efnode_pa[n].pa_area;
	perim = node->efnode_pa[n].pa_perim;
	if (area > 0 && perim > 0)
	{
	    v = (double) perim * (double) perim - 16.0 * area;

	    /* Approximate by one square if v < 0; shouldn't happen! */
	    if (v < 0.0) s = 0.0; else s = sqrt(v);

	    fperim = (float) perim;
	    dresist = (fperim + s)/(fperim - s) * efResists[n];
	    if (dresist + (double) resist > (double) INT_MAX)
		resist = INT_MAX;
	    else
		resist += dresist;
	}
    }
    return (resist);
}

/*
 * ----------------------------------------------------------------------------
 *
 * EFLookDist --
 *
 * Look for the Distance between two points given by their HierNames.
 *
 * Results:
 *	TRUE if a distance was found, FALSE if not.
 *
 * Side effects:
 *	Sets *pMinDist and *pMaxDist to the min and max distances
 *	if found.
 *
 * ----------------------------------------------------------------------------
 */

bool
EFLookDist(hn1, hn2, pMinDist, pMaxDist)
    HierName *hn1, *hn2;
    int *pMinDist, *pMaxDist;
{
    Distance distKey, *dist;
    HashEntry *he;

    if (EFHNBest(hn1, hn2))
    {
	distKey.dist_1 = hn1;
	distKey.dist_2 = hn2;
    }
    else
    {
	distKey.dist_1 = hn2;
	distKey.dist_2 = hn1;
    }
    he = HashLookOnly(&efDistHashTable, (char *) &distKey);
    if (he == NULL)
	return FALSE;

    dist = (Distance *) HashGetValue(he);
    *pMinDist = dist->dist_min;
    *pMaxDist = dist->dist_max;
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * EFHNOut --
 *
 * Output a hierarchical node name.
 * The flags in EFTrimFlags control whether global (!) or local (#)
 * suffixes are to be trimmed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes to the files 'outf'.
 *
 * ----------------------------------------------------------------------------
 */

void
EFHNOut(hierName, outf)
    HierName *hierName;
    FILE *outf;
{
    bool trimGlob, trimLocal, trimComma;
    char *cp, c;

    if (hierName->hn_parent) efHNOutPrefix(hierName->hn_parent, outf);
    if (EFTrimFlags)
    {
	cp = hierName->hn_name; 
	trimGlob = (EFTrimFlags & EF_TRIMGLOB);
	trimLocal = (EFTrimFlags & EF_TRIMLOCAL);
	trimComma = (EFTrimFlags & EF_CONVERTCOMMAS);
	while (c = *cp++)
	{
	    if (*cp) 
	    {
		if (trimComma && (c == ','))
		    putc(';', outf);
		else
		    putc(c, outf);
	    }
	    else switch (c)
	    {
		case '!':	if (!trimGlob) (void) putc(c, outf); break;
		case '#':	if (trimLocal) break;
		default:	(void) putc(c, outf); break;
	    }
	}
    }
    else (void) fputs(hierName->hn_name, outf);
}

void
efHNOutPrefix(hierName, outf)
    HierName *hierName;
    FILE *outf;
{
    char *cp, c;

    if (hierName->hn_parent)
	efHNOutPrefix(hierName->hn_parent, outf);

    cp = hierName->hn_name;
    while (c = *cp++)
	putc(c, outf);
    putc('/', outf);
}
