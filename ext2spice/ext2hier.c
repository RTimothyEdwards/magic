/*
 * ext2hier.c --
 *
 * Program to convert hierarchical .ext files into a single
 * hierarchical .spice file, suitable for use as input to a
 * hierarchy-capable LVS (layout vs. schematic) tool such as
 * netgen.
 *
 * Generates the tree rooted at file.ext, reading in additional .ext
 * files as specified by "use" lines in file.ext.  The output is left
 * in file.spice, unless '-o esSpiceFile' is specified, in which case the
 * output is left in 'esSpiceFile'.
 *
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/ext2spice/ext2hier.c,v 1.5 2010/12/16 18:59:03 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>		/* for atof() */
#include <string.h>
#include <ctype.h>
#include <math.h>		/* for fabs() */

#ifdef MAGIC_WRAPPER
#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/malloc.h"
#include "utils/geometry.h"
#include "utils/hash.h"
#include "utils/dqueue.h"
#include "utils/utils.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "windows/windows.h"
#include "textio/textio.h"
#include "dbwind/dbwind.h"	/* for DBWclientID */
#include "commands/commands.h"  /* for module auto-load */
#include "textio/txcommands.h"
#include "extflat/extflat.h"
#include "extflat/EFint.h"
#include "extract/extract.h"	/* for extDevTable */
#include "utils/runstats.h"
#include "ext2spice/ext2spice.h"

// Structure passed to esHierVisit

typedef struct _defflagsdata {
   Def *def;
   int flags;
} DefFlagsData;

/*
 * ----------------------------------------------------------------------------
 *
 * ESGenerateHierarchy ---
 *
 *	Generate hierarchical SPICE output
 *
 * ----------------------------------------------------------------------------
 */

void
ESGenerateHierarchy(inName, flags)
    char *inName;
    int flags;
{
    int esHierVisit(), esMakePorts();	/* Forward declaration */
    Use u;
    Def *def;
    HierContext hc;
    DefFlagsData dfd;

    u.use_def = efDefLook(inName);
    hc.hc_use = &u;
    hc.hc_hierName = NULL;
    hc.hc_trans = GeoIdentityTransform;
    hc.hc_x = hc.hc_y = 0;
    EFHierSrDefs(&hc, esMakePorts, NULL);
    EFHierSrDefs(&hc, NULL, NULL);	/* Clear processed */

    dfd.def = u.use_def;
    dfd.flags = flags; 
    EFHierSrDefs(&hc, esHierVisit, (ClientData)(&dfd));
    EFHierSrDefs(&hc, NULL, NULL);	/* Clear processed */

    return;
}

/*
 * ----------------------------------------------------------------------------
 *
 * GetHierNode --
 *
 * function to find a node structure given its name
 *
 * Results:
 *  a pointer to the node struct or NULL
 *
 * ----------------------------------------------------------------------------
 */

EFNode *
GetHierNode(hc, name)
    HierContext *hc;
    HierName *name;
{
    HashEntry *he;
    EFNodeName *nn;
    Def *def = hc->hc_use->use_def;

    he = EFHNConcatLook(hc->hc_hierName, name, "node");
    if (he == NULL) return NULL;
    nn = (EFNodeName *) HashGetValue(he);
    if (nn == NULL) return NULL;
    return(nn->efnn_node);
}


/*
 * ----------------------------------------------------------------------------
 *
 * spcHierWriteParams ---
 *
 * Write parameters to a device line in SPICE output.  This is normally
 * restricted to subcircuit devices but may include other devices to
 * accomodate various extensions to the basic SPICE format.
 * ----------------------------------------------------------------------------
 */

void
spcHierWriteParams(hc, dev, scale, l, w, sdM)
    HierContext *hc;
    Dev *dev;           /* Dev being output */
    float scale;        /* Scale transform for output */
    int l;              /* Device length, in internal units */
    int w;              /* Device width, in internal units */
    float sdM;          /* Device multiplier */
{
    bool hierD;
    DevParam *plist;
    int parmval;
    EFNode *dnode, *subnodeFlat = NULL;

    /* Write all requested parameters to the subcircuit call.	*/

    plist = efGetDeviceParams(EFDevTypes[dev->dev_type]);
    while (plist != NULL)
    {
	switch (plist->parm_type[0])
	{
	    case 'a':
		// Check for area of terminal node vs. device area
		if (plist->parm_type[1] == '\0' || plist->parm_type[1] == '0')
		{
		    fprintf(esSpiceF, " %s=", plist->parm_name);
		    parmval = dev->dev_area;
		    if (esScale < 0)
			fprintf(esSpiceF, "%g", parmval * scale * scale);
		    else if (plist->parm_scale != 1.0)
			fprintf(esSpiceF, "%g", parmval * scale * scale
				* esScale * esScale * plist->parm_scale
				* 1E-12);
		    else
			fprintf(esSpiceF, "%gp", parmval * scale * scale
				* esScale * esScale);
		}
		else
		{
		    int pn;
		    pn = plist->parm_type[1] - '0';
		    if (pn >= dev->dev_nterm) pn = dev->dev_nterm - 1;

		    dnode = GetHierNode(hc,
			dev->dev_terms[pn].dterm_node->efnode_name->efnn_hier);

		    // For parameter an followed by parameter pn,
		    // process both at the same time

		    if (plist->parm_next && plist->parm_next->parm_type[0] ==
				'p' && plist->parm_next->parm_type[1] ==
				plist->parm_type[1])
		    {
			spcnAP(dnode, esFetInfo[dev->dev_type].resClassSD,
				scale, plist->parm_name, 
				plist->parm_next->parm_name, sdM,
				esSpiceF, w);
			plist = plist->parm_next;
		    }
		    else
		    {
			spcnAP(dnode, esFetInfo[dev->dev_type].resClassSD,
				scale, plist->parm_name, NULL, sdM,
				esSpiceF, w);
		    }
		}

		break;
	    case 'p':
		// Check for perimeter of terminal node vs. device perimeter
		if (plist->parm_type[1] == '\0' || plist->parm_type[1] == '0')
		{
		    fprintf(esSpiceF, " %s=", plist->parm_name);
		    parmval = dev->dev_perim;

		    if (esScale < 0)
			fprintf(esSpiceF, "%g", parmval * scale);
		    else if (plist->parm_scale != 1.0)
			fprintf(esSpiceF, "%g", parmval * scale
				* esScale * plist->parm_scale * 1E-6);
		    else
			fprintf(esSpiceF, "%gu", parmval * scale * esScale);
		}
		else
		{
		    int pn;
		    pn = plist->parm_type[1] - '0';
		    if (pn >= dev->dev_nterm) pn = dev->dev_nterm - 1;

		    dnode = GetHierNode(hc,
			dev->dev_terms[pn].dterm_node->efnode_name->efnn_hier);

		    // For parameter pn followed by parameter an,
		    // process both at the same time

		    if (plist->parm_next && plist->parm_next->parm_type[0] ==
				'a' && plist->parm_next->parm_type[1] ==
				plist->parm_type[1])
		    {
			spcnAP(dnode, esFetInfo[dev->dev_type].resClassSD,
				scale, plist->parm_next->parm_name, 
				plist->parm_name, sdM, esSpiceF, w);
			plist = plist->parm_next;
		    }
		    else
		    {
			spcnAP(dnode, esFetInfo[dev->dev_type].resClassSD,
				scale, NULL, plist->parm_name, sdM,
				esSpiceF, w);
		    }
		}

		break;
	    case 'l':
		fprintf(esSpiceF, " %s=", plist->parm_name);
		if (esScale < 0)
		    fprintf(esSpiceF, "%g", l * scale);
		else if (plist->parm_scale != 1.0)
		    fprintf(esSpiceF, "%g", l * scale * esScale
				* plist->parm_scale * 1E-6);
		else
		    fprintf(esSpiceF, "%gu", l * scale * esScale);
		break;
	    case 'w':
		fprintf(esSpiceF, " %s=", plist->parm_name);
		if (esScale < 0)
		    fprintf(esSpiceF, "%g", w * scale);
		else if (plist->parm_scale != 1.0)
		    fprintf(esSpiceF, "%g", w * scale * esScale
				* plist->parm_scale * 1E-6);
		else
		    fprintf(esSpiceF, "%gu", w * scale * esScale);
		break;
	    case 's':
		fprintf(esSpiceF, " %s=", plist->parm_name);
		subnodeFlat = spcdevSubstrate(hc->hc_hierName,
			dev->dev_subsnode->efnode_name->efnn_hier,
			dev->dev_type, esSpiceF);
		break;
	    case 'x':
		fprintf(esSpiceF, " %s=", plist->parm_name);
		if (esScale < 0)
		    fprintf(esSpiceF, "%g", dev->dev_rect.r_xbot * scale);
		else if (plist->parm_scale != 1.0)
		    fprintf(esSpiceF, "%g", dev->dev_rect.r_xbot * scale
				* esScale * plist->parm_scale * 1E-6);
		else
		    fprintf(esSpiceF, "%gu", dev->dev_rect.r_xbot * scale
				* esScale);
		break;
	    case 'y':
		fprintf(esSpiceF, " %s=", plist->parm_name);
		if (esScale < 0)
		    fprintf(esSpiceF, "%g", dev->dev_rect.r_ybot * scale);
		else if (plist->parm_scale != 1.0)
		    fprintf(esSpiceF, "%g", dev->dev_rect.r_ybot * scale
				* esScale * plist->parm_scale * 1E-6);
		else
		    fprintf(esSpiceF, "%gu", dev->dev_rect.r_ybot * scale
				* esScale);
		break;
	    case 'r':
		fprintf(esSpiceF, " %s=", plist->parm_name);
		fprintf(esSpiceF, "%f", (double)(dev->dev_res));
		break;
	    case 'c':
		fprintf(esSpiceF, " %s=", plist->parm_name);
		fprintf(esSpiceF, "%ff", (double)(dev->dev_cap));
		break;
	}
	plist = plist->parm_next;
    }

    /* Add parameters that are to be copied verbatim */
    for (plist = dev->dev_params; plist; plist = plist->parm_next)
	fprintf(esSpiceF, " %s", plist->parm_name);
}

/*
 * ----------------------------------------------------------------------------
 *
 * esOutputHierResistor ---
 *
 * Routine used by spcdevHierVisit to print a resistor device.  This
 * is broken out into a separate routine so that each resistor
 * device may be represented (if the option is selected) by a
 * "tee" network of two resistors on either side of the central
 * node, which then has a capacitance to ground.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Output to the SPICE deck.
 *
 * ----------------------------------------------------------------------------
 */

void
esOutputHierResistor(hc, dev, scale, term1, term2, has_model, l, w, dscale)
    HierContext *hc;
    Dev *dev;			/* Dev being output */
    float scale;		/* Scale transform for output */
    DevTerm *term1, *term2;	/* Terminals of the device */
    bool has_model;		/* Is this a modeled resistor? */
    int l, w;			/* Device length and width */
    int dscale;			/* Device scaling (for split resistors) */
{
    Rect r;
    float sdM ; 
    char name[12], devchar;

    /* Resistor is "Rnnn term1 term2 value" 		 */
    /* extraction sets two terminals, which are assigned */
    /* term1=gate term2=source by the above code.	 */
    /* extracted units are Ohms; output is in Ohms 	 */

    spcdevOutNode(hc->hc_hierName, term1->dterm_node->efnode_name->efnn_hier,
		"res_top", esSpiceF);
    spcdevOutNode(hc->hc_hierName, term2->dterm_node->efnode_name->efnn_hier,
		"res_bot", esSpiceF);

    sdM = getCurDevMult();

    /* SPICE has two resistor types.  If the "name" (EFDevTypes) is */
    /* "None", the simple resistor type is used, and a value given. */
    /* If not, the "semiconductor resistor" is used, and L and W    */
    /* and the device name are output.				    */

    if (!has_model)
    {
	fprintf(esSpiceF, " %f", ((double)(dev->dev_res)
			/ (double)(dscale)) / (double)sdM);
	spcHierWriteParams(hc, dev, scale, l, w, sdM);
    }
    else
    {
	fprintf(esSpiceF, " %s", EFDevTypes[dev->dev_type]);

	if (esScale < 0) 
	{
	    fprintf(esSpiceF, " w=%d l=%d", (int)((float)w * scale),
			(int)(((float)l * scale) / (float)dscale));
	}
	else
	{
	    fprintf(esSpiceF, " w=%gu l=%gu",
		(float)w * scale * esScale,
		(float)((l * scale * esScale) / dscale));
	}
	spcHierWriteParams(hc, dev, scale, l, w, sdM);
	if (sdM != 1.0)
	    fprintf(esSpiceF, " M=%g", sdM);
    }
}

/*
 * ----------------------------------------------------------------------------
 * ----------------------------------------------------------------------------
 *
 */

int
subcktHierVisit(use, hierName, is_top)
    Use *use;
    HierName *hierName;
    bool is_top;                /* TRUE if this is the top-level cell */
{
    Def *def = use->use_def;
    EFNode *snode;
    EFNodeName *nodeName;
    bool hasports = FALSE;

    /* Avoid generating records for circuits that have no ports.	*/
    /* These are already absorbed into the parent.  All other		*/
    /* subcircuits have at least one port marked by the EF_PORT flag.	*/
    /* Do not count the substrate port, as it exists even on cells	*/
    /* with no other ports.						*/
 
    for (snode = (EFNode *) def->def_firstn.efnode_next;
		snode != &def->def_firstn;
		snode = (EFNode *) snode->efnode_next)
	if (snode->efnode_flags & EF_PORT)
	{
	    for (nodeName = snode->efnode_name; nodeName != NULL;
			nodeName = nodeName->efnn_next)
		if (nodeName->efnn_port >= 0)
		{
		    hasports = TRUE;
		    break;
		}
	}
	else if (snode->efnode_flags & EF_SUBS_PORT)
	{
	    hasports = TRUE;
	    break;
	}

    if (hasports || is_top)
	return subcktVisit(use, hierName, is_top);
    else if (def->def_flags & DEF_NODEVICES)
	return 0;
    else
	return subcktVisit(use, hierName, is_top);
}

/*
 * ----------------------------------------------------------------------------
 *
 * spcdevHierVisit --
 *
 * Procedure to output a single dev to the .spice file.
 * Called by EFHierVisitDevs().
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Writes to the file esSpiceF.
 *
 * Format of a .spice dev line:
 *
 *	M%d drain gate source substrate type w=w l=l * x y
 *      + ad= pd= as= ps=  * asub= psub=  
 *      **devattr g= s= d= 
 *
 * where
 *	type is a name identifying this type of transistor
 *      other types of transistors are extracted with
 *      an M card but it should be easy to turn them to whatever 
 *      you want.
 *	gate, source, and drain are the nodes to which these three
 *		terminals connect
 *	l, w are the length and width of the channel
 *	x, y are the x, y coordinates of a point within the channel.
 *	g=, s=, d= are the (optional) attributes; if present, each
 *		is followed by a comma-separated list of attributes.
 *
 * ----------------------------------------------------------------------------
 */

int
spcdevHierVisit(hc, dev, scale)
    HierContext *hc;
    Dev *dev;		/* Dev being output */
    float scale;	/* Scale transform for output */
{
    DevParam *plist, *pptr;
    DevTerm *gate, *source, *drain;
    EFNode  *subnode, *snode, *dnode, *subnodeFlat = NULL;
    int l, w, i, parmval;
    Rect r;
    bool subAP= FALSE, hierS, hierD, extHierSDAttr() ;
    float sdM; 
    char devchar;
    bool has_model = TRUE;

    /* If no terminals, or only a gate, can't do much of anything */
    if (dev->dev_nterm <= 1 ) 
	return 0;

    if ( (esMergeDevsA || esMergeDevsC) && devIsKilled(esFMIndex++) )
	    return 0;

    /* Get L and W of device */
    EFGetLengthAndWidth(dev, &l, &w);

    /* If only two terminals, connect the source to the drain */
    gate = &dev->dev_terms[0];
    source = drain = (DevTerm *)NULL;
    if (dev->dev_nterm >= 2)
	source = drain = &dev->dev_terms[1];
    if (dev->dev_nterm >= 3)
    {
        /* If any terminal is marked with attribute "D" or "S"  */
        /* (label "D$" or "S$" at poly-diffusion interface),    */
        /* then force order of source and drain accordingly.    */

        if ((dev->dev_terms[1].dterm_attrs &&
                !strcmp(dev->dev_terms[1].dterm_attrs, "D")) ||
                (dev->dev_terms[2].dterm_attrs &&
                !strcmp(dev->dev_terms[2].dterm_attrs, "S")))
        {
            drain = &dev->dev_terms[1];
            source = &dev->dev_terms[2];
        }
        else
            drain = &dev->dev_terms[2];
    }
    else if (dev->dev_nterm == 1)	// Is a device with one terminal an error?
	source = drain = &dev->dev_terms[0];
    subnode = dev->dev_subsnode;

    /* Original hack for BiCMOS, Tim 10/4/97, is deprecated.	*/
    /* Use of "device bjt" preferred to "fet" with model="npn".	*/

    if (!strcmp(EFDevTypes[dev->dev_type], "npn")) dev->dev_class = DEV_BJT;

    /* For resistor and capacitor classes, set a boolean to	*/
    /* denote whether the device has a model or not, so we	*/
    /* don't have to keep doing a string compare on EFDevTypes.	*/

    switch(dev->dev_class)
    {
	case DEV_RES:
	case DEV_CAP:
	case DEV_CAPREV:
	    if (dev->dev_type == esNoModelType)
		has_model = FALSE;
	    break;
    }

    /* Flag shorted devices---this should probably be an option */
    switch(dev->dev_class)
    {
	case DEV_MOSFET:
	case DEV_ASYMMETRIC:
	case DEV_FET:
	    if (source == drain)
	    {
		if (esFormat == NGSPICE) fprintf(esSpiceF, "; ");
		fprintf(esSpiceF, "** SOURCE/DRAIN TIED\n");
	    }
	    break;

	default:
	    if (gate == source)
	    {
		if (esFormat == NGSPICE) fprintf(esSpiceF, "; ");
		fprintf(esSpiceF, "** SHORTED DEVICE\n");
	    }
	    break;
    }

    /* Generate SPICE device name */
    switch(dev->dev_class)
    {
	case DEV_MOSFET:
	case DEV_ASYMMETRIC:
	case DEV_FET:
	    devchar = 'M';
	    break;
	case DEV_BJT:
	    devchar = 'Q';
	    break;
	case DEV_DIODE:
	case DEV_NDIODE:
	case DEV_PDIODE:
	    devchar = 'D';
	    break;
	case DEV_RES:
	    devchar = 'R';
	    break;
	case DEV_CAP:
	case DEV_CAPREV:
	    devchar = 'C';
	    break;
	case DEV_SUBCKT:
	case DEV_RSUBCKT:
	case DEV_MSUBCKT:
	    devchar = 'X';
	    break;
    }
    fprintf(esSpiceF, "%c", devchar);

    /* Device index is taken from gate attributes if attached;	*/
    /* otherwise, the device is numbered in sequence.		*/

    if (gate->dterm_attrs)
	fprintf(esSpiceF, "%s", gate->dterm_attrs);
    else
    {
	switch (dev->dev_class)
	{
	    case DEV_RES:
		fprintf(esSpiceF, "%d", esResNum++);
		/* For resistor tee networks, use, e.g.,	*/
		/* "R1A" and "R1B", for clarity			*/
		if (esDoResistorTee) fprintf(esSpiceF, "A");
		break;
	    case DEV_DIODE:
	    case DEV_NDIODE:
	    case DEV_PDIODE:
		fprintf(esSpiceF, "%d", esDiodeNum++);
		break;
	    case DEV_CAP:
	    case DEV_CAPREV:
		fprintf(esSpiceF, "%d", esCapNum++);
		break;
	    case DEV_SUBCKT:
	    case DEV_RSUBCKT:
	    case DEV_MSUBCKT:
		fprintf(esSpiceF, "%d", esSbckNum++);
		break;
	    default:
		fprintf(esSpiceF, "%d", esDevNum++);
		break;
	}
    }
    /* Order and number of nodes in the output depends on the device class */

    switch (dev->dev_class)
    {
	case DEV_BJT:
	    if (source == NULL) break;

	    /* BJT is "Qnnn collector emitter base model" 			*/
	    /* extraction sets collector=subnode, emitter=gate, base=drain	*/

	    spcdevOutNode(hc->hc_hierName, subnode->efnode_name->efnn_hier,
			"collector", esSpiceF); 
	    spcdevOutNode(hc->hc_hierName, gate->dterm_node->efnode_name->efnn_hier,
			"emitter", esSpiceF);

	    /* fix mixed up drain/source for bjts hace 2/2/99 */
	    if (gate->dterm_node->efnode_name->efnn_hier ==
			source->dterm_node->efnode_name->efnn_hier)
		spcdevOutNode(hc->hc_hierName,
			drain->dterm_node->efnode_name->efnn_hier,
			"base", esSpiceF);
	    else
		spcdevOutNode(hc->hc_hierName,
			source->dterm_node->efnode_name->efnn_hier,
			"base", esSpiceF);

	    fprintf(esSpiceF, " %s", EFDevTypes[dev->dev_type]);
	    spcHierWriteParams(hc, dev, scale, l, w, sdM);
	    break;

	case DEV_MSUBCKT:
	    /* msubcircuit is "Xnnn source gate [drain [sub]]]"		*/
	    /* to more conveniently handle situations where MOSFETs	*/
	    /* are modeled by subcircuits with the same pin ordering.	*/

	    spcdevOutNode(hc->hc_hierName,
			source->dterm_node->efnode_name->efnn_hier,
			"subckt", esSpiceF);

	    /* Drop through to below (no break statement) */

	case DEV_SUBCKT:

	    /* Subcircuit is "Xnnn gate [source [drain [sub]]]"		*/
	    /* Subcircuit .subckt record must be ordered to match!	*/

	    spcdevOutNode(hc->hc_hierName,
			gate->dterm_node->efnode_name->efnn_hier,
			"subckt", esSpiceF);

	    /* Drop through to below (no break statement) */

	case DEV_RSUBCKT:
	    /* RC-like subcircuits are exactly like other subcircuits	*/
	    /* except that the "gate" node is treated as an identifier	*/
	    /* only and is not output.					*/

	    if ((dev->dev_nterm > 1) && (dev->dev_class != DEV_MSUBCKT))
		spcdevOutNode(hc->hc_hierName,
			source->dterm_node->efnode_name->efnn_hier,
			"subckt", esSpiceF);
	    if (dev->dev_nterm > 2)
		spcdevOutNode(hc->hc_hierName,
			drain->dterm_node->efnode_name->efnn_hier,
			"subckt", esSpiceF);

	    /* The following only applies to DEV_SUBCKT*, which may define as	*/
	    /* many terminal types as it wants.					*/

	    for (i = 4; i < dev->dev_nterm; i++)
	    {
		drain = &dev->dev_terms[i - 1];
		spcdevOutNode(hc->hc_hierName,
			drain->dterm_node->efnode_name->efnn_hier,
			"subckt", esSpiceF);
	    }

	    /* Get the device parameters now, and check if the substrate is	*/
	    /* passed as a parameter rather than as a node.			*/

	    plist = efGetDeviceParams(EFDevTypes[dev->dev_type]);
	    for (pptr = plist; pptr != NULL; pptr = pptr->parm_next)
		if (pptr->parm_type[0] == 's')
		    break;

	    if ((pptr == NULL) && subnode)
	    {
		fprintf(esSpiceF, " ");
		subnodeFlat = spcdevSubstrate(hc->hc_hierName,
			subnode->efnode_name->efnn_hier,
			dev->dev_type, esSpiceF);
	    }
	    fprintf(esSpiceF, " %s", EFDevTypes[dev->dev_type]);

	    /* Write all requested parameters to the subcircuit call.	*/
	    sdM = getCurDevMult();
	    spcHierWriteParams(hc, dev, scale, l, w, sdM);
	    if (sdM != 1.0)
		fprintf(esSpiceF, " M=%g", sdM);
	    break;

	case DEV_RES:
	    if (esDoResistorTee)
	    {
		/* There are three ways of handling capacitance	*/
		/* on resistor networks.  One is to ignore it	*/
		/* (the default; generates "floating" nodes in	*/
		/* the SPICE output) which is okay for LVS. 	*/
		/* Another way is the Pi network, in which the	*/
		/* capacitance is split evenly between the	*/
		/* terminals.  Again, the resistor node is left	*/
		/* floating.  The third is the Tee network, in	*/
		/* which the resistance is split in two parts,	*/
		/* connecting to a capacitor to ground in the	*/
		/* middle.  This is the best solution but plays	*/
		/* havoc with LVS.  So, the choice is a command	*/
		/* line option.					*/

		esOutputHierResistor(hc, dev, scale, gate, source, has_model,
			l, w, 2);
		fprintf(esSpiceF, "\n%c", devchar);
		if (gate->dterm_attrs)
		    fprintf(esSpiceF, "%sB", gate->dterm_attrs);
		else
		    fprintf(esSpiceF, "%dB", esResNum - 1);
		esOutputHierResistor(hc, dev, scale, gate, drain, has_model,
			l, w, 2);
	    }
	    else
	    {
		esOutputHierResistor(hc, dev, scale, source, drain, has_model,
			l, w, 1);
	    }
	    break;

	case DEV_DIODE:
	case DEV_PDIODE:
	    if (source == NULL) break;

	    /* Diode is "Dnnn top bottom model"	*/

	    spcdevOutNode(hc->hc_hierName,
			gate->dterm_node->efnode_name->efnn_hier,
			"diode_top", esSpiceF);
	    if (dev->dev_nterm > 1)
		spcdevOutNode(hc->hc_hierName,
			source->dterm_node->efnode_name->efnn_hier,
			"diode_bot", esSpiceF);
	    else if (subnode)
		spcdevOutNode(hc->hc_hierName,
			subnode->efnode_name->efnn_hier,
			"diode_bot", esSpiceF);
	    fprintf(esSpiceF, " %s", EFDevTypes[dev->dev_type]);
	    spcHierWriteParams(hc, dev, scale, l, w, sdM);
	    break;

	case DEV_NDIODE:
	    if (source == NULL) break;

	    /* Diode is "Dnnn bottom top model"	*/

	    if (dev->dev_nterm > 1)
		spcdevOutNode(hc->hc_hierName,
			source->dterm_node->efnode_name->efnn_hier,
			"diode_bot", esSpiceF);
	    else if (subnode)
		spcdevOutNode(hc->hc_hierName,
			subnode->efnode_name->efnn_hier,
			"diode_bot", esSpiceF);
	    spcdevOutNode(hc->hc_hierName,
			gate->dterm_node->efnode_name->efnn_hier,
			"diode_top", esSpiceF);
	    fprintf(esSpiceF, " %s", EFDevTypes[dev->dev_type]);
	    spcHierWriteParams(hc, dev, scale, l, w, sdM);
	    break;

	case DEV_CAP:
	    if (source == NULL) break;

	    /* Capacitor is "Cnnn top bottom value"	*/
	    /* extraction sets top=gate bottom=source	*/
	    /* extracted units are fF; output is in fF */

	    spcdevOutNode(hc->hc_hierName,
			gate->dterm_node->efnode_name->efnn_hier,
			"cap_top", esSpiceF);
	    spcdevOutNode(hc->hc_hierName,
			source->dterm_node->efnode_name->efnn_hier,
			"cap_bot", esSpiceF);

	    sdM = getCurDevMult();

	    /* SPICE has two capacitor types.  If the "name" (EFDevTypes) is */
	    /* "None", the simple capacitor type is used, and a value given. */
	    /* If not, the "semiconductor capacitor" is used, and L and W    */
	    /* and the device name are output.				     */

	    if (!has_model)
	    {
		fprintf(esSpiceF, " %ffF", (double)sdM *
				(double)(dev->dev_cap));
		spcHierWriteParams(hc, dev, scale, l, w, sdM);
	    }
	    else
	    {
		fprintf(esSpiceF, " %s", EFDevTypes[dev->dev_type]);

		if (esScale < 0)
		{
		    fprintf(esSpiceF, " w=%g l=%g", w*scale, l*scale);
		}
		else
		{
		    fprintf(esSpiceF, " w=%gu l=%gu",
			w * scale * esScale,
			l * scale * esScale);
		}
		spcHierWriteParams(hc, dev, scale, l, w, sdM);
		if (sdM != 1.0)
		    fprintf(esSpiceF, " M=%g", sdM);
	    }
	    break;

	case DEV_CAPREV:
	    if (source == NULL) break;

	    /* Capacitor is "Cnnn bottom top value"	*/
	    /* extraction sets top=source bottom=gate	*/
	    /* extracted units are fF; output is in fF */

	    spcdevOutNode(hc->hc_hierName,
			gate->dterm_node->efnode_name->efnn_hier,
			"cap_bot", esSpiceF);
	    spcdevOutNode(hc->hc_hierName,
			source->dterm_node->efnode_name->efnn_hier,
			"cap_top", esSpiceF);

	    sdM = getCurDevMult();

	    /* SPICE has two capacitor types.  If the "name" (EFDevTypes) is */
	    /* "None", the simple capacitor type is used, and a value given. */
	    /* If not, the "semiconductor capacitor" is used, and L and W    */
	    /* and the device name are output.				     */

	    if (!has_model)
	    {
		fprintf(esSpiceF, " %ffF", (double)sdM *
				(double)(dev->dev_cap));
		spcHierWriteParams(hc, dev, scale, l, w, sdM);
	    }
	    else
	    {
		fprintf(esSpiceF, " %s", EFDevTypes[dev->dev_type]);

		if (esScale < 0)
		{
		    fprintf(esSpiceF, " w=%g l=%g", w*scale, l*scale);
		}
		else
		{
		    fprintf(esSpiceF, " w=%gu l=%gu",
			w * scale * esScale,
			l * scale * esScale);
		}
		spcHierWriteParams(hc, dev, scale, l, w, sdM);
		if (sdM != 1.0)
		    fprintf(esSpiceF, " M=%g", sdM);
	    }
	    break;

	case DEV_FET:
	case DEV_MOSFET:
	case DEV_ASYMMETRIC:
	    if (source == NULL) break;

	    /* MOSFET is "Mnnn drain gate source [L=x W=x [attributes]]" */

	    spcdevOutNode(hc->hc_hierName,
			drain->dterm_node->efnode_name->efnn_hier,
			"drain", esSpiceF);
	    spcdevOutNode(hc->hc_hierName,
			gate->dterm_node->efnode_name->efnn_hier,
			"gate", esSpiceF);
	    spcdevOutNode(hc->hc_hierName,
			source->dterm_node->efnode_name->efnn_hier,
			"source", esSpiceF);
	    if (subnode)
	    {
		fprintf(esSpiceF, " ");
		subnodeFlat = spcdevSubstrate(hc->hc_hierName,
			subnode->efnode_name->efnn_hier,
			dev->dev_type, esSpiceF);
	    }
	    fprintf(esSpiceF, " %s", EFDevTypes[dev->dev_type]);

	    sdM = getCurDevMult();

	    if (esScale < 0)
	    {
		fprintf(esSpiceF, " w=%g l=%g", w*scale, l*scale);
	    }
	    else
	    {
		fprintf(esSpiceF, " w=%gu l=%gu",
			w * scale * esScale,
			l * scale * esScale);
	    }
	    spcHierWriteParams(hc, dev, scale, l, w, sdM);
	    if (sdM != 1.0)
		fprintf(esSpiceF, " M=%g", sdM);

	    /*
	     * Check controlling attributes and output area and perimeter. 
	     */
	    hierS = extHierSDAttr(source);
	    hierD = extHierSDAttr(drain);
	    if ( gate->dterm_attrs ) 
		subAP = Match(ATTR_SUBSAP, gate->dterm_attrs ) ;

	    fprintf(esSpiceF, "\n+ ");
	    dnode = GetHierNode(hc, drain->dterm_node->efnode_name->efnn_hier);
            spcnAP(dnode, esFetInfo[dev->dev_type].resClassSD, scale,
			"ad", "pd", sdM, esSpiceF, w);
	    snode= GetHierNode(hc, source->dterm_node->efnode_name->efnn_hier);
	    spcnAP(snode, esFetInfo[dev->dev_type].resClassSD, scale,
			"as", "ps", sdM, esSpiceF, w);
	    if (subAP)
	    {
		fprintf(esSpiceF, " * ");
		if (esFetInfo[dev->dev_type].resClassSub < 0)
		{
		    TxError("error: subap for devtype %d unspecified\n",
				dev->dev_type);
		    fprintf(esSpiceF, "asub=0 psub=0");
		}
		else if (subnodeFlat) 
		    spcnAP(subnodeFlat, esFetInfo[dev->dev_type].resClassSub, scale, 
	       			"asub", "psub", sdM, esSpiceF, -1);
		else
		    fprintf(esSpiceF, "asub=0 psub=0");
	    }

	    /* Now output attributes, if present */
	    if (!esNoAttrs)
	    {
		if (gate->dterm_attrs || source->dterm_attrs || drain->dterm_attrs)
		    fprintf(esSpiceF,"\n**devattr");
		if (gate->dterm_attrs)
		    fprintf(esSpiceF, " g=%s", gate->dterm_attrs);
		if (source->dterm_attrs) 
		    fprintf(esSpiceF, " s=%s", source->dterm_attrs);
		if (drain->dterm_attrs) 
		    fprintf(esSpiceF, " d=%s", drain->dterm_attrs);
	    }
	    break;
    }
    fprintf(esSpiceF, "\n");
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * spcdevHierMergeVisit --
 *
 * First pass visit to devices to determine if they can be merged with
 * any previously visited device.
 *
 * ----------------------------------------------------------------------------
 */

int
spcdevHierMergeVisit(hc, dev, scale)
    HierContext *hc;
    Dev *dev;		/* Dev being output */
    float scale;	/* Scale of transform (may be non-integer) */
{
    DevTerm *gate, *source, *drain;
    EFNode *subnode, *snode, *dnode, *gnode;
    int pmode, l, w;
    devMerge *fp, *cfp;
    float m;

    /* If no terminals, or only a gate, can't do much of anything */
    if (dev->dev_nterm < 2) return 0;

    gate = &dev->dev_terms[0];
    source = drain = &dev->dev_terms[1];
    if (dev->dev_nterm >= 3)
	drain = &dev->dev_terms[2];

    gnode = GetHierNode(hc, gate->dterm_node->efnode_name->efnn_hier);
    snode = GetHierNode(hc, source->dterm_node->efnode_name->efnn_hier);
    dnode = GetHierNode(hc, drain->dterm_node->efnode_name->efnn_hier);
    subnode = dev->dev_subsnode;

    EFGetLengthAndWidth(dev, &l, &w);

    fp = mkDevMerge((float)((float)l * scale), (float)((float)w * scale),
		gnode, snode, dnode, subnode, hc->hc_hierName, dev);
		
    for (cfp = devMergeList; cfp != NULL; cfp = cfp->next)
    {
	if ((pmode = parallelDevs(fp, cfp)) != NOT_PARALLEL)
	{
	    /* To-do:  add back source, drain attribute check */

	    switch(dev->dev_class)
	    {
		case DEV_MOSFET:
		case DEV_MSUBCKT:
		case DEV_ASYMMETRIC:
		case DEV_FET:
		    m = esFMult[cfp->esFMIndex] + (fp->w / cfp->w);
		    break;
		case DEV_RSUBCKT:
		case DEV_RES:
		    if (fp->dev->dev_type == esNoModelType)
			m = esFMult[cfp->esFMIndex] + (fp->dev->dev_res
				/ cfp->dev->dev_res);
		    else
			m = esFMult[cfp->esFMIndex] + (fp->l / cfp->l);
		    break;
		case DEV_CAP:
		case DEV_CAPREV:
		    if (fp->dev->dev_type == esNoModelType)
			m = esFMult[cfp->esFMIndex] + (fp->dev->dev_cap
				/ cfp->dev->dev_cap);
		    else
			m = esFMult[cfp->esFMIndex] +
				((fp->l * fp->w) / (cfp->l * cfp->w));
		    break;
	    }
	    setDevMult(fp->esFMIndex, DEV_KILLED);
	    setDevMult(cfp->esFMIndex, m);
	    esSpiceDevsMerged++;
	    freeMagic(fp);
	    return 0;
	}
    }

    /* No devices are parallel to this one (yet) */
    fp->next = devMergeList;
    devMergeList = fp;
    return 0;
}


/*
 * ----------------------------------------------------------------------------
 *
 * spccapHierVisit --
 *
 * Procedure to output a single capacitor to the .spice file.
 * Called by EFHierVisitCaps().
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Writes to the file esSpiceF. Increments esCapNum.
 *
 * Format of a .spice cap line:
 *
 *	C%d node1 node2 cap
 *
 * where
 *	node1, node2 are the terminals of the capacitor
 *	cap is the capacitance in femtofarads (NOT attofarads).
 *
 * ----------------------------------------------------------------------------
 */

int
spccapHierVisit(hc, hierName1, hierName2, cap)
    HierContext *hc;
    HierName *hierName1;
    HierName *hierName2;
    double cap;
{
    cap = cap / 1000;
    if (fabs(cap) <= EFCapThreshold)
	return 0;

    fprintf(esSpiceF, esSpiceCapFormat, esCapNum++,
		nodeSpiceHierName(hc, hierName1),
                nodeSpiceHierName(hc, hierName2), cap);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * spcresistHierVisit --
 *
 * Procedure to output a single resistor to the .spice file.
 * Called by EFHierVisitResists().
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Writes to the file esSpiceF. Increments esResNum.
 *
 * Format of a .spice resistor line:
 *
 *	R%d node1 node2 res
 *
 * where
 *	node1, node2 are the terminals of the resistor
 *	res is the resistance in ohms (NOT milliohms)
 *
 *
 * ----------------------------------------------------------------------------
 */
int
spcresistHierVisit(hc, hierName1, hierName2, res)
    HierContext *hc;
    HierName *hierName1;
    HierName *hierName2;
    int res;
{
    res = (res + 500) / 1000;
 
    fprintf(esSpiceF, "R%d %s %s %d\n", esResNum++,
		nodeSpiceHierName(hc, hierName1),
                nodeSpiceHierName(hc, hierName2), res);

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * spcsubHierVisit --
 *
 * Find the node that connects to the substrate.  Copy the string name
 * of this node into "resstr" to be returned to the caller.
 *
 * Results:
 *	Return 1 if the substrate node has been found, to stop the search.
 *	Otherwise return 0 to keep the search going.
 *
 * ----------------------------------------------------------------------------
 */

int
spcsubHierVisit(hc, node, res, cap, resstrptr)
    HierContext *hc;
    EFNode *node;
    int res; 		// Unused
    double cap;		// Unused
    char **resstrptr;
{
    HierName *hierName;
    char *nsn;

    if (node->efnode_flags & EF_SUBS_NODE)
    {
	hierName = (HierName *) node->efnode_name->efnn_hier;
	nsn = nodeSpiceHierName(hc, hierName);
	*resstrptr = StrDup((char **)NULL, nsn);
	return 1;
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * spcnodeHierVisit --
 *
 * Procedure to output a single node to the .spice file along with its 
 * attributes and its dictionary (if present). Called by EFHierVisitNodes().
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Writes to the files esSpiceF
 *
 * ----------------------------------------------------------------------------
 */

int
spcnodeHierVisit(hc, node, res, cap)
    HierContext *hc;
    EFNode *node;
    int res; 
    double cap;
{
    EFNodeName *nn;
    HierName *hierName;
    bool isConnected = FALSE;
    char *fmt, *nsn;
    EFAttr *ap;

    if (node->efnode_client)
    {
	isConnected = (esDistrJunct) ?
		(((nodeClient *)node->efnode_client)->m_w.widths != NULL) :
        	((((nodeClient *)node->efnode_client)->m_w.visitMask
		& DEV_CONNECT_MASK) != 0);
    }
    if (!isConnected && esDevNodesOnly) 
	return 0;

    /* Don't mark known ports as "FLOATING" nodes */
    if (!isConnected && node->efnode_flags & EF_PORT) isConnected = TRUE;

    hierName = (HierName *) node->efnode_name->efnn_hier;
    nsn = nodeSpiceHierName(hc, hierName);

    if (esFormat == SPICE2 || esFormat == HSPICE && !strncmp(nsn, "z@", 2)) {
	static char ntmp[MAX_STR_SIZE];

	EFHNSprintf(ntmp, hierName);
	if (esFormat == NGSPICE) fprintf(esSpiceF, " ; ");
	fprintf(esSpiceF, "** %s == %s\n", ntmp, nsn);
    }
    cap = cap  / 1000;
    if (fabs(cap) > EFCapThreshold)
    {
	fprintf(esSpiceF, esSpiceCapFormat, esCapNum++, nsn, cap,
			(isConnected) ?  "" :
			(esFormat == NGSPICE) ? " ; **FLOATING" :
			" **FLOATING");
    }
    if (node->efnode_attrs && !esNoAttrs)
    {
	if (esFormat == NGSPICE) fprintf(esSpiceF, " ; ");
	fprintf(esSpiceF, "**nodeattr %s :",nsn );
	for (fmt = " %s", ap = node->efnode_attrs; ap; ap = ap->efa_next)
	{
	    fprintf(esSpiceF, fmt, ap->efa_text);
	    fmt = ",%s";
	}
	putc('\n', esSpiceF);
    }

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * nodeSpiceHierName --
 * Find the real spice name for the node with hierarchical name hname.
 *   SPICE2 ==> numeric
 *   SPICE3, NGSPICE ==> full magic path
 *   HSPICE ==> less than 15 characters long
 *
 * Results:
 *	Returns the spice node name.
 *
 * Side effects:
 *      Allocates nodeClients for the node.
 *
 * ----------------------------------------------------------------------------
 */
static char esTempName[MAX_STR_SIZE];

char *nodeSpiceHierName(hc, hname)
    HierContext *hc;
    HierName *hname;
{
    EFNodeName *nn;
    HashEntry *he;
    EFNode *node;
    Def *def = hc->hc_use->use_def;

    // he = HashLookOnly(&def->def_nodes, EFHNToStr(hname));
    he = HashLookOnly(&efNodeHashTable, (char *)hname);
    if (he == NULL) return "error";

    nn = (EFNodeName *) HashGetValue(he);
    if (nn == NULL)
	return "<invalid node>";
    node = nn->efnn_node;

    if ((nodeClient *) (node->efnode_client) == NULL)
    {
    	initNodeClient(node);
	goto makeName;
    }
    else if (((nodeClient *) (node->efnode_client))->spiceNodeName == NULL)
	goto makeName;
    else goto retName;

makeName:
    if (esFormat == SPICE2) 
	sprintf(esTempName, "%d", esNodeNum++);
    else {
       EFHNSprintf(esTempName, node->efnode_name->efnn_hier);
       if (esFormat == HSPICE) /* more processing */
	  nodeHspiceName(esTempName);
    }
    ((nodeClient *) (node->efnode_client))->spiceNodeName = 
	    StrDup(NULL, esTempName);

retName:
    return ((nodeClient *) (node->efnode_client))->spiceNodeName;
}

/*
 * ----------------------------------------------------------------------------
 *
 * devMergeVisit --
 * Visits each dev throu EFHierVisitDevs and finds if it is in parallel with
 * any previously visited dev.
 *
 * Results:
 *  0 always to keep the caller going.
 *
 * Side effects:
 *  Numerous.
 *
 * ----------------------------------------------------------------------------
 */

int
devMergeHierVisit(hc, dev, scale)
    HierContext *hc;
    Dev *dev;			/* Dev to examine */
    float scale;		/* Scale transform of output */
{
    DevTerm *gate, *source, *drain;
    Dev     *cf;
    DevTerm *cg, *cs, *cd;
    EFNode *subnode, *snode, *dnode, *gnode;
    int      pmode, l, w;
    bool     hS, hD, chS, chD;
    devMerge *fp, *cfp;
    float m;

    if (esDistrJunct)
	devDistJunctHierVisit(hc, dev, scale);

    if (dev->dev_nterm < 2)
    {
	TxError("outPremature\n");
	return 0;
    }

    gate = &dev->dev_terms[0];
    source = drain = &dev->dev_terms[1];
    if (dev->dev_nterm >= 3)
	drain = &dev->dev_terms[2];


    gnode = GetHierNode(hc, gate->dterm_node->efnode_name->efnn_hier);
    snode = GetHierNode(hc, source->dterm_node->efnode_name->efnn_hier);
    dnode = GetHierNode(hc, drain->dterm_node->efnode_name->efnn_hier);
    if (dev->dev_subsnode)
	subnode = spcdevSubstrate(hc->hc_hierName,
			dev->dev_subsnode->efnode_name->efnn_hier, 
			dev->dev_type, NULL);
    else
	subnode = NULL;

    /* Get length and width of the device */
    EFGetLengthAndWidth(dev, &l, &w);

    fp = mkDevMerge((float)((float)l * scale), (float)((float)w * scale),
			gnode, snode, dnode, subnode, NULL, dev);
    hS = extHierSDAttr(source);
    hD = extHierSDAttr(drain);

    /*
     * run the list of devs. compare the current one with 
     * each one in the list. if they fullfill the matching requirements
     * merge them only if:
     * 1) they have both apf S, D attributes
     * or 
     * 2) one of them has aph S, D attributes and they have the same 
     *    hierarchical prefix
     * If one of them has apf and the other aph print a warning.
     */

    for (cfp = devMergeList; cfp != NULL; cfp = cfp->next)
    {
	if ((pmode = parallelDevs(fp, cfp)) != NOT_PARALLEL)
	{
	    cf = cfp->dev;
	    cg = &cfp->dev->dev_terms[0];
	    cs = cd = &cfp->dev->dev_terms[1];
	    if (cfp->dev->dev_nterm >= 3)
	    {
		if (pmode == PARALLEL) 
		    cd = &cfp->dev->dev_terms[2];
		    else if (pmode == ANTIPARALLEL) 
			cs = &cfp->dev->dev_terms[2];
	    }

	    chS = extHierSDAttr(cs); chD = extHierSDAttr(cd);
	    if (!(chS || chD || hS || hD)) /* all flat S, D */
		goto mergeThem;

	    if (hS && !chS)
	    {
		mergeAttr(&cs->dterm_attrs, &source->dterm_attrs);
	    }
	    if (hD && !chD)
	    {
		mergeAttr(&cd->dterm_attrs, &drain->dterm_attrs);
	    }
mergeThem:
	    switch(dev->dev_class)
	    {
		case DEV_MOSFET:
		case DEV_ASYMMETRIC:
		case DEV_MSUBCKT:
		case DEV_FET:
		    m = esFMult[cfp->esFMIndex] + ((float)fp->w / (float)cfp->w);
		    break;
		case DEV_RSUBCKT:
		case DEV_RES:
		    if (fp->dev->dev_type == esNoModelType)
		        m = esFMult[cfp->esFMIndex] + (fp->dev->dev_res
				/ cfp->dev->dev_res);
		    else
		        m = esFMult[cfp->esFMIndex] + (fp->l / cfp->l);
		    break;
		case DEV_CAP:
		case DEV_CAPREV:
		    if (fp->dev->dev_type == esNoModelType)
		        m = esFMult[cfp->esFMIndex] + (fp->dev->dev_cap
				/ cfp->dev->dev_cap);
		    else
		        m = esFMult[cfp->esFMIndex] +
				((fp->l  * fp->w) / (cfp->l * cfp->w));
		    break;
	    }
	    setDevMult(fp->esFMIndex, DEV_KILLED); 
	    setDevMult(cfp->esFMIndex, m);
	    esSpiceDevsMerged++;
	    /* Need to do attribute stuff here */
	    freeMagic(fp);
	    return 0;
	}
    }

    /* No parallel devs to it yet */
    fp->next = devMergeList;
    devMergeList = fp;
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * devDistJunctVisit --
 *  Called for every dev and updates the nodeclients of its terminals
 *
 * Results:
 *  0 to keep the calling procedure going
 *
 * Side effects:
 *  calls update_w which might allocate stuff
 *
 * ----------------------------------------------------------------------------
 */

int 
devDistJunctHierVisit(hc, dev, scale)
    HierContext *hc;
    Dev *dev;			/* Dev to examine */
    float scale;		/* Scale tranform of output */
{
    EFNode  *n;
    int i, l, w;

    if (dev->dev_nterm < 2)
    {
	TxError("outPremature\n");
	return 0;
    }

    w = (int)((float)w * scale);
    EFGetLengthAndWidth(dev, &l, &w);

    for (i = 1; i<dev->dev_nterm; i++)
    {
	n = GetHierNode(hc, dev->dev_terms[i].dterm_node->efnode_name->efnn_hier);
	update_w(esFetInfo[dev->dev_type].resClassSD, w, n);
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * esMakePorts ---
 *
 *	Routine called once for each cell definition in the extraction
 *	hierarchy.  Called from EFHierSrDefs().  Looks at all subcircuit
 *	connections in the cell, and adds a port record to the subcircuit
 *	for each connection to it.  Note that this generates an arbitrary
 *	port order for each cell.  To have a specific port order, it is
 *	necessary to generate ports for each cell.
 * ----------------------------------------------------------------------------
 */

int
esMakePorts(hc, cdata)
    HierContext *hc;
    ClientData cdata;
{
    Connection *conn;
    Def *def = hc->hc_use->use_def, *portdef, *updef;
    Use *use;
    HashEntry *he;
    EFNodeName *nn;
    char *name, *portname, *tptr, *aptr, *locname;
    int j;

    if (def->def_uses == NULL) return 0;	/* Bottom of hierarchy */

    for (conn = (Connection *)def->def_conns; conn; conn = conn->conn_next)
    {
	for (j = 0; j < 2; j++)
	{
	    name = (j == 0) ? conn->conn_1.cn_name : conn->conn_2.cn_name;
	    locname = (j == 0) ? conn->conn_2.cn_name : conn->conn_1.cn_name;
	    if ((tptr = strchr(name, '/')) == NULL)
		continue;

	    portname = name;
	    updef = def;

	    while (tptr != NULL)
	    {
		/* Ignore array information for the purpose of tracing	*/	
		/* the cell definition hierarchy.			*/

		aptr = strchr(portname, '[');
		if ((aptr == NULL) || (aptr > tptr))
		    *tptr = '\0';
		else
		    *aptr = '\0';

		// Find the cell for the instance
		portdef = NULL;
		for (use = updef->def_uses; use; use = use->use_next)
		{
		    if (!strcmp(use->use_id, portname))
		    {
			portdef = use->use_def;
			break;
		    }
		}
		if ((aptr == NULL) || (aptr > tptr))
		    *tptr = '/';
		else
		    *aptr = '[';
		portname = tptr + 1;

		// Find the net of portname in the subcell and
		// make it a port if it is not already.

		if (portdef)
		{
		    he = HashFind(&portdef->def_nodes, portname);
		    nn = (EFNodeName *) HashGetValue(he);
		    if (nn == NULL)
		    {
			efBuildNode(portdef, FALSE, portname, 0.0,
					0, 0, NULL, NULL, 0);
			nn = (EFNodeName *) HashGetValue(he);
		    }

		    if (!(nn->efnn_node->efnode_flags & EF_PORT))
		    {
			nn->efnn_node->efnode_flags |= EF_PORT;
			nn->efnn_port = -1;	// Will be sorted later
		    }
		}

		if ((tptr = strchr(portname, '/')) == NULL)
		    break;
		if (portdef == NULL) break;	// Error condition?

		updef = portdef;
	    }
	    // Diagnostic
	    // TxPrintf("Connection in %s to net %s (%s)\n", def->def_name,
	    //		name, portname);
	}
    }

    // Now do the same thing for parasitic connections into subcells
    // However, restrict the number of ports based on "cthresh".

    for (conn = (Connection *)def->def_caps; conn; conn = conn->conn_next)
    {
	for (j = 0; j < 2; j++)
	{
	    name = (j == 0) ? conn->conn_1.cn_name : conn->conn_2.cn_name;
	    locname = (j == 0) ? conn->conn_2.cn_name : conn->conn_1.cn_name;
	    if ((tptr = strchr(name, '/')) == NULL)
		continue;

	    // Ignore capacitances that are less than the threshold.
	    // In particular, this keeps parasitics out of the netlist for
	    // LVS purposes if "cthresh" is set to "infinite".

	    if (fabs((double)conn->conn_cap) < EFCapThreshold) continue;

	    portname = name;
	    updef = def;

	    while (tptr != NULL)
	    {
		/* Ignore array information for the purpose of tracing	*/	
		/* the cell definition hierarchy.			*/

		aptr = strchr(portname, '[');
		if ((aptr == NULL) || (aptr > tptr))
		    *tptr = '\0';
		else
		    *aptr = '\0';

		// Find the cell for the instance
		portdef = NULL;
		for (use = updef->def_uses; use; use = use->use_next)
		{
		    if (!strcmp(use->use_id, portname))
		    {
			portdef = use->use_def;
			break;
		    }
		}
		if ((aptr == NULL) || (aptr > tptr))
		    *tptr = '/';
		else
		    *aptr = '[';
		portname = tptr + 1;

		// Find the net of portname in the subcell and
		// make it a port if it is not already.

		if (portdef)
		{
		    he = HashFind(&portdef->def_nodes, portname);
		    nn = (EFNodeName *) HashGetValue(he);
		    if (nn == NULL)
		    {
			efBuildNode(portdef, FALSE, portname, 0.0,
					0, 0, NULL, NULL, 0);
			nn = (EFNodeName *) HashGetValue(he);
		    }

		    if (!(nn->efnn_node->efnode_flags & EF_PORT))
		    {
			nn->efnn_node->efnode_flags |= EF_PORT;
			nn->efnn_port = -1;	// Will be sorted later
		    }
		}

		if ((tptr = strchr(portname, '/')) == NULL)
		    break;
		if (portdef == NULL) break;	// Error condition?

		updef = portdef;
	    }
	    // Diagnostic
	    // TxPrintf("Connection in %s to net %s (%s)\n", def->def_name,
	    //		name, portname);
	}
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * esHierVisit ---
 *
 *	Routine called once for each cell definition in the extraction
 *	hierarchy.  Called from EFHierSrDefs().  Outputs a single
 *	subcircuit record for the cell definition.  Note that this format
 *	ignores all information pertaining to flattened cells, and is
 *	appropriate mainly for LVS purposes.
 * ----------------------------------------------------------------------------
 */

int
esHierVisit(hc, cdata)
    HierContext *hc;
    ClientData cdata;
{
    HierContext *hcf;
    Def *def = hc->hc_use->use_def;
    Def *topdef;
    EFNode *snode;
    char *resstr = NULL;
    DefFlagsData *dfd;
    int flags;
    int locDoSubckt = esDoSubckt;
    bool doStub;

    dfd = (DefFlagsData *)cdata;
    topdef = dfd->def;
    flags = dfd->flags;

    /* Cells without any contents (devices or subcircuits) will	*/
    /* be absorbed into their parents.  Use this opportunity to	*/
    /* remove all ports.					*/

    if (def != topdef)
    {
	if (def->def_devs == NULL && def->def_uses == NULL)
	{
	    if (locDoSubckt == AUTO)
	    {
		/* Determine if there are ports, and don't	*/
		/* kill the cell if it has any.			*/
		locDoSubckt = FALSE;
		for (snode = (EFNode *) def->def_firstn.efnode_next;
			snode != &def->def_firstn;
			snode = (EFNode *) snode->efnode_next)
		    if (snode->efnode_flags & (EF_PORT | EF_SUBS_PORT))
		    {
			locDoSubckt = TRUE;
			break;
		    }
	    }
	    if (locDoSubckt == FALSE)
	    {
		for (snode = (EFNode *) def->def_firstn.efnode_next;
			snode != &def->def_firstn;
			snode = (EFNode *) snode->efnode_next)
		    snode->efnode_flags &= ~(EF_PORT | EF_SUBS_PORT);
		if (def != topdef) return 0;
	    }
	}
    } 

    /* Flatten this definition only */
    hcf = EFFlatBuildOneLevel(hc->hc_use->use_def, flags);

    /* If definition has been marked as having no devices, then this	*/
    /* def is not to be output unless it is the top level.  However, if	*/
    /* this subcircuit is an abstract view, then create a subcircuit	*/
    /* stub entry for it (declares port names and order but no devices)	*/

    doStub = ((hc->hc_use->use_def->def_flags & DEF_ABSTRACT) && esDoBlackBox) ?
		TRUE : FALSE;

    if ((def != topdef) && (hc->hc_use->use_def->def_flags & DEF_NODEVICES) &&
		(!doStub))
    {
	EFFlatDone();
	return 0;
    }
    else if (doStub)
	fprintf(esSpiceF, "* Black-box entry subcircuit for %s abstract view\n",
		def->def_name);

    /* Generate subcircuit header */
    if ((def != topdef) || (def->def_flags & DEF_SUBCIRCUIT))
	topVisit(def, doStub);
    else
	fprintf(esSpiceF, "\n* Top level circuit %s\n\n", topdef->def_name);

    if (!doStub)	/* By definition, stubs have no internal components */
    {
	/* Output subcircuit calls */
	EFHierVisitSubcircuits(hcf, subcktHierVisit, (ClientData)NULL);

	/* Merge devices */
	if (esMergeDevsA || esMergeDevsC)
	{
	    devMerge *p;

	    EFHierVisitDevs(hcf, spcdevHierMergeVisit, (ClientData)NULL);
	    TxPrintf("Devs merged: %d\n", esSpiceDevsMerged);
	    esFMIndex = 0;
	    for (p = devMergeList; p != NULL; p = p->next)
		freeMagic(p);
	    devMergeList = NULL;
	}

	/* Output devices */
	EFHierVisitDevs(hcf, spcdevHierVisit, (ClientData)NULL);

	/* Output lumped parasitic resistors */
	EFHierVisitResists(hcf, spcresistHierVisit, (ClientData)NULL);

	/* Output coupling capacitances */
	sprintf( esSpiceCapFormat,  "C%%d %%s %%s %%.%dlffF\n", esCapAccuracy);
	EFHierVisitCaps(hcf, spccapHierVisit, (ClientData)NULL);

	if (EFCompat == FALSE)
	{
	    /* Find the substrate node */
	    EFHierVisitNodes(hcf, spcsubHierVisit, (ClientData)&resstr);
	    if (resstr == NULL) resstr = StrDup((char **)NULL, "0");

	    /* Output lumped capacitance and resistance to substrate */
	    sprintf( esSpiceCapFormat,  "C%%d %%s %s %%.%dlffF%%s\n",
			resstr, esCapAccuracy);
	    EFHierVisitNodes(hcf, spcnodeHierVisit, (ClientData) NULL);
	    freeMagic(resstr);
	}
    }

    if ((def != topdef) || (def->def_flags & DEF_SUBCIRCUIT))
	fprintf(esSpiceF, ".ends\n\n");
    else
	fprintf(esSpiceF, ".end\n\n");

    /* Reset device/node/subcircuit instance counts */

    esCapNum  = 0;
    esDevNum = 1000;
    esResNum = 0;
    esDiodeNum = 0;
    esSbckNum = 0;
    esNodeNum = 10;

    EFFlatDone();
    return 0;
}

#endif /* MAGIC_WRAPPER */
