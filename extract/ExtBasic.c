/*
 * ExtBasic.c --
 *
 * Circuit extraction.
 * Flat extraction of a single CellDef.
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
static char sccsid[] = "@(#)ExtBasic.c	4.13 MAGIC (Berkeley) 12/5/85";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/malloc.h"
#include "textio/textio.h"
#include "debug/debug.h"
#include "extract/extract.h"
#include "extract/extractInt.h"
#include "utils/signals.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/styles.h"
#include "utils/stack.h"
#include "utils/utils.h"

/* These must be in the order of "known devices" in extract.h.		*/

/* Note: "fet" refers to the original fet type; "mosfet" refers to the	*/
/* new type.  The main difference is that "fet" records area/perimeter	*/
/* while "mosfet" records length/width.					*/
/* Also: Note that this table is repeated in extflat/EFread.c when	*/
/* ext2spice/ext2sim are compiled as separate programs (i.e., non-Tcl)	*/

#ifdef MAGIC_WRAPPER
char *extDevTable[] = {"fet", "mosfet", "asymmetric", "bjt", "devres",
	"devcap", "devcaprev", "diode", "pdiode", "ndiode", "subckt",
	"rsubckt", "msubckt", NULL};
#endif

/* --------------------- Data local to this file ---------------------- */

    /*
     * The following are used to accumulate perimeter and area
     * on each layer when building up the node list.  They are
     * used to compute the resistance of each node.  Each is
     * indexed by sheet resistivity class.
     */
int extResistPerim[NT], extResistArea[NT];

    /*
     * The following structure is used in extracting transistors.
     *
     * A "terminal" below refers to any port on the transistor that
     * is not the gate.  In most cases, these are the "diffusion"
     * ports of the transistor.
     */
#define	MAXSD	10	/* Maximum # of terminals per transistor */


typedef struct		/* Position of each terminal (below) tile position */
{
    int		pnum;
    Point	pt;
} TermTilePos;

struct transRec
{
    int		 tr_nterm;		/* Number of terminals */
    int		 tr_gatelen;		/* Perimeter of connection to gate */
    NodeRegion	*tr_gatenode;		/* Node region for gate terminal */
    NodeRegion	*tr_termnode[MAXSD];	/* Node region for each diff terminal */
    NodeRegion  *tr_subsnode;		/* Substrate node */
    int		 tr_termlen[MAXSD];	/* Length of each diff terminal edge,
					 * used for computing L/W for the fet.
					 */
    Point	 tr_termvector[MAXSD];	/* Perimeter traversal vector, used to
					 * find and calculate correct parameters
					 * for annular (ring) devices and other
					 * non-rectangular geometries.
					 */
    int		 tr_perim;		/* Total perimeter */
    TermTilePos  tr_termpos[MAXSD];	/* lowest tile connecting to term */
} extTransRec;

typedef struct LB1
{
    Rect r;	/* Boundary segment */
    int dir;	/* Direction of travel */
    struct LB1 *b_next;
} LinkedBoundary;

LinkedBoundary **extSpecialBounds;	/* Linked Boundary List */
NodeRegion *glob_subsnode = NULL;	/* Global substrate node */
NodeRegion *temp_subsnode = NULL;	/* Last subsnode found */

/* Structure used for finding substrate connections on implicitly-defined
 * substrates
 */

typedef struct TSD1
{
    bool found;		/* Set to 1 if a substrate connection was found */
    Rect rtrans;	/* Rectangle of device */
    Rect rhalo;		/* Search halo around device */
    NodeRegion *nreg;	/* Closest substrate region within halo */
} TransSubsData;

#define	EDGENULL(r)	((r)->r_xbot > (r)->r_xtop || (r)->r_ybot > (r)->r_ytop)

/* Forward declarations */
void extOutputNodes();
int extTransTileFunc();
int extTransPerimFunc();
int extTransFindSubs();

int extAnnularTileFunc();
int extResistorTileFunc();
int extSpecialPerimFunc();

void extFindDuplicateLabels();
void extOutputDevices();
void extOutputParameters();
void extTransOutTerminal();
void extTransBad();

bool extLabType();

/*
 * ----------------------------------------------------------------------------
 *
 * extBasic --
 *
 * Extract a single CellDef, and output the result to the
 * file 'outFile'.
 *
 * Results:
 *	Returns a list of Region structs that comprise all
 *	the nodes in 'def'.  It is the caller's responsibility
 *	to call ExtResetTile() and ExtFreeLabRegions() to restore
 *	the CellDef to its original state and to free the list
 *	of regions we build up.
 *
 * Side effects:
 *	Writes the result of extracting just the paint of
 *	the CellDef 'def' to the output file 'outFile'.
 *	The following kinds of records are output:
 *
 *		node
 *		substrate
 *		equiv
 *		fet
 *		device
 *
 * Interruptible in a limited sense.  We will still return a
 * Region list, but labels may not have been assigned, and
 * nodes and fets may not have been output.
 *
 * ----------------------------------------------------------------------------
 */

NodeRegion *
extBasic(def, outFile)
    CellDef *def;	/* Cell being extracted */
    FILE *outFile;	/* Output file */
{
    NodeRegion *nodeList, *extFindNodes();
    bool coupleInitialized = FALSE;
    TransRegion *transList;
    HashTable extCoupleHash;
    char *propptr;
    bool propfound = FALSE;

    glob_subsnode = (NodeRegion *)NULL;

    /*
     * Build up a list of the device regions for extOutputDevices()
     * below.  We're only interested in pointers from each region to
     * a tile in that region, not the back pointers from the tiles to
     * the regions.
     */
    transList = (TransRegion *) ExtFindRegions(def, &TiPlaneRect,
				    &ExtCurStyle->exts_transMask,
				    ExtCurStyle->exts_transConn,
				    extUnInit, extTransFirst, extTransEach);
    ExtResetTiles(def, extUnInit);

    /*
     * Build up a list of the electrical nodes (equipotentials)
     * for extOutputNodes() below.  For this, we definitely want
     * to leave each tile pointing to its associated Region struct.
     * Compute resistance and capacitance on the fly.
     * Use a special-purpose version of ExtFindRegions for speed.
     */
    if (!SigInterruptPending)
	nodeList = extFindNodes(def, (Rect *) NULL, FALSE);

    glob_subsnode = temp_subsnode;	// Keep a record of the def's substrate

    /* Assign the labels to their associated regions */
    if (!SigInterruptPending)
	ExtLabelRegions(def, ExtCurStyle->exts_nodeConn, &nodeList, &TiPlaneRect);

    /*
     * Make sure all geometry with the same label is part of the
     * same electrical node.
     */
    if (!SigInterruptPending && (ExtDoWarn & EXTWARN_DUP))
	extFindDuplicateLabels(def, nodeList);

    /*
     * Build up table of coupling capacitances (overlap, sidewall).
     * This comes before extOutputNodes because we may have to adjust
     * node capacitances in this step.
     */
    if (!SigInterruptPending && (ExtOptions&EXT_DOCOUPLING))
    {
	coupleInitialized = TRUE;
	HashInit(&extCoupleHash, 256, HashSize(sizeof (CoupleKey)));
	extFindCoupling(def, &extCoupleHash, (Rect *) NULL);

	/* Convert coupling capacitance to the substrate node to
	 * substrate capacitance on each node in nreg_cap
	 */

	if (ExtCurStyle->exts_globSubstratePlane != -1)
	    if (!SigInterruptPending && (ExtOptions&EXT_DOCOUPLING))
		extRelocateSubstrateCoupling(&extCoupleHash, glob_subsnode);
    }

    /* Output device parameters for any subcircuit devices */
    if (!SigInterruptPending)
	extOutputParameters(def, transList, outFile);

    /* Check for "device", as it modifies handling of parasitics */
    propptr = (char *)DBPropGet(def, "device", &propfound);
    if (propfound)
    {
	/* Remove parasitics from local nodes */
	NodeRegion *tnode;
	for (tnode = nodeList; tnode; tnode = tnode->nreg_next)
	{
	    tnode->nreg_cap = (CapValue)0.0;
	    tnode->nreg_resist = (ResValue)0;
	}
    }

    /* Output each node, along with its resistance and capacitance to substrate */
    if (!SigInterruptPending)
	extOutputNodes(nodeList, outFile);

    /* Output coupling capacitances */
    if (!SigInterruptPending && (ExtOptions&EXT_DOCOUPLING) && (!propfound))
	extOutputCoupling(&extCoupleHash, outFile);

    /* Output devices and connectivity between nodes */
    if (!SigInterruptPending)
    {
	int llx, lly, urx, ury, devidx, l, w;
	char *token, *modelname, *subsnode;
	char *propvalue;

	modelname = NULL;
	subsnode = NULL;
	propvalue = NULL;
	
	if (propfound)
	{
	    /* Sanity checking on syntax of property line, plus	*/
	    /* conversion of values to internal units.		*/
	    propvalue = StrDup((char **)NULL, propptr);
	    token = strtok(propvalue, " ");
	    devidx = Lookup(token, extDevTable);
	    if (devidx < 0)
	    {
		TxError("Extract error:  \"device\" property has unknown "
				"device type.\n", token);
		propfound = FALSE;
	    }
	    if (propfound)
	    {
		token = strtok(NULL, " ");
		if (token == NULL)
		    propfound = FALSE;
		else
		    modelname = StrDup((char **)NULL, token);
	    }
	    if (propfound)
	    {
		token = strtok(NULL, " ");
		if ((token == NULL) || !sscanf(token, "%d", &llx))
		    propfound = FALSE;
		else
		    llx *= ExtCurStyle->exts_unitsPerLambda;
	    }
	    if (propfound)
	    {
		token = strtok(NULL, " ");
		if ((token == NULL) || !sscanf(token, "%d", &lly))
		    propfound = FALSE;
		else
		    lly *= ExtCurStyle->exts_unitsPerLambda;
	    }
	    if (propfound)
	    {
		token = strtok(NULL, " ");
		if ((token == NULL) || !sscanf(token, "%d", &urx))
		    propfound = FALSE;
		else
		    urx *= ExtCurStyle->exts_unitsPerLambda;
		if (urx <= llx) urx++;
	    }
	    if (propfound)
	    {
		token = strtok(NULL, " ");
		if ((token == NULL) || !sscanf(token, "%d", &ury))
		    propfound = FALSE;
		else
		    ury *= ExtCurStyle->exts_unitsPerLambda;
		if (ury <= lly) ury++;
	    }
	    if (propfound)
	    {
		switch (devidx)
		{
		    case DEV_FET:
			/* Read area */
			token = strtok(NULL, " ");
			if ((token == NULL) || !sscanf(token, "%d", &w))
			    propfound = FALSE;
			else
			    w *= ExtCurStyle->exts_unitsPerLambda *
			    	   ExtCurStyle->exts_unitsPerLambda;
			/* Read perimeter */
			token = strtok(NULL, " ");
			if ((token == NULL) || !sscanf(token, "%d", &l))
			    propfound = FALSE;
			else
			    l *= ExtCurStyle->exts_unitsPerLambda;
			break;
		    case DEV_MOSFET:
		    case DEV_ASYMMETRIC:
		    case DEV_BJT:
			/* Read width */
			token = strtok(NULL, " ");
			if ((token == NULL) || !sscanf(token, "%d", &w))
			    propfound = FALSE;
			else
			    w *= ExtCurStyle->exts_unitsPerLambda;
			/* Read length */
			token = strtok(NULL, " ");
			if ((token == NULL) || !sscanf(token, "%d", &l))
			    propfound = FALSE;
			else
			    l *= ExtCurStyle->exts_unitsPerLambda;
			break;
		    case DEV_RES:
			if (strcmp(modelname, "None"))
			{
			    /* Read width */
			    token = strtok(NULL, " ");
			    if ((token == NULL) || !sscanf(token, "%d", &w))
				propfound = FALSE;
			    else
				w *= ExtCurStyle->exts_unitsPerLambda;
			    /* Read length */
			    token = strtok(NULL, " ");
			    if ((token == NULL) || !sscanf(token, "%d", &l))
				propfound = FALSE;
			    else
				l *= ExtCurStyle->exts_unitsPerLambda;
			    break;
			}
			break;
		    case DEV_CAP:
		    case DEV_CAPREV:
			if (strcmp(modelname, "None"))
			{
			    /* Read area */
			    token = strtok(NULL, " ");
			    if ((token == NULL) || !sscanf(token, "%d", &w))
				propfound = FALSE;
			    else
				w *= ExtCurStyle->exts_unitsPerLambda *
				     ExtCurStyle->exts_unitsPerLambda;
			    /* Read perimeter */
			    token = strtok(NULL, " ");
			    if ((token == NULL) || !sscanf(token, "%d", &l))
				propfound = FALSE;
			    else
				l *= ExtCurStyle->exts_unitsPerLambda;
			    break;
			}
			break;
		}
	    }

	    if (propfound)
	    {
		if (devidx == DEV_FET)
		    fprintf(outFile, "fet");
		else
		    fprintf(outFile, "device %s", extDevTable[devidx]);
		fprintf(outFile, " %s %d %d %d %d", modelname,
				llx, lly, urx, ury);
		switch (devidx) {
		    case DEV_FET:
		    case DEV_MOSFET:
		    case DEV_ASYMMETRIC:
		    case DEV_BJT:
			fprintf(outFile, " %d %d", w, l);
			break;
		    case DEV_RES:
		    case DEV_CAP:
		    case DEV_CAPREV:
			if (strcmp(modelname, "None"))
			    fprintf(outFile, " %d %d", w, l);
			break;
		}
		/* Print remainder of arguments verbatim. */
		/* Note:  There should be additional checks on 	*/
		/* node triplets including area and perim. conversions */
		while (1) {
		    token = strtok(NULL, " ");
		    if (token == NULL)
			break;
		    else
			fprintf(outFile, " %s", token);
		}
	    }
	    else if (devidx >= 0)
	    {
		TxError("Extract error:  \"device %s\" property syntax"
				" error\n", extDevTable[devidx]);
	    }
	    if (modelname) freeMagic(modelname);
	    if (propvalue) freeMagic(propvalue);
	}

	if (!propfound)
	    extOutputDevices(def, transList, outFile);
    }

    /* Clean up */
    if (coupleInitialized)
	extCapHashKill(&extCoupleHash);
    ExtFreeLabRegions((LabRegion *) transList);
    return (nodeList);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extSetResist --
 *
 * The input to this procedure is a pointer to a NodeRegion.
 * Its resistance is computed from the area and perimeter stored
 * in the arrays extResistPerim[] and extResistArea[].  These arrays
 * are then reset to zero.
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
 *	None.
 *
 * Side effects:
 *	See the comments above.
 *
 * ----------------------------------------------------------------------------
 */

void
extSetResist(reg)
    NodeRegion *reg;
{
    int n, perim, area;
    float s, fperim, v;

    for (n = 0; n < ExtCurStyle->exts_numResistClasses; n++)
    {
	reg->nreg_pa[n].pa_area = area = extResistArea[n];
	reg->nreg_pa[n].pa_perim = perim = extResistPerim[n];
	if (area > 0 && perim > 0)
	{
	    v = (double) (perim*perim - 16*area);

	    /* Approximate by one square if v < 0 */
	    if (v < 0) s = 0; else s = sqrt(v);

	    fperim = (float) perim;
	    reg->nreg_resist += (fperim + s) / (fperim - s)
				    * ExtCurStyle->exts_resistByResistClass[n];
	}

	/* Reset for the next pass */
	extResistArea[n] = extResistPerim[n] = 0;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * extOutputNodes --
 *
 * The resistance and capacitance of each node have already been
 * computed, so all we need do is output them.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes a number of 'node' and 'equiv' records to the file 'outFile'.
 *
 * Interruptible.  If SigInterruptPending is detected, we stop outputting
 * nodes and return.
 *
 * ----------------------------------------------------------------------------
 */

void
extOutputNodes(nodeList, outFile)
    NodeRegion *nodeList;	/* Nodes */
    FILE *outFile;		/* Output file */
{
    ResValue rround = ExtCurStyle->exts_resistScale / 2;
    CapValue finC;
    int intR;
    NodeRegion *reg;
    LabelList *ll;
    char *cp;
    int n;
    Label *lab;
    char *text;

    /* If this node is a subcircuit port, it gets special treatment.	*/
    /* There may be multiple ports per node.			 	*/

    for (reg = nodeList; reg && !SigInterruptPending; reg = reg->nreg_next)
	for (ll = reg->nreg_labels; ll; ll = ll->ll_next)
	    if (ll->ll_attr == LL_PORTATTR)
	    {
		fprintf(outFile, "port \"%s\" %d %d %d %d %d %s\n",
			ll->ll_label->lab_text,
			ll->ll_label->lab_flags & PORT_NUM_MASK,
			ll->ll_label->lab_rect.r_xbot,
			ll->ll_label->lab_rect.r_ybot,
			ll->ll_label->lab_rect.r_xtop,
			ll->ll_label->lab_rect.r_ytop,
			DBTypeShortName(ll->ll_label->lab_type));

		/* If the port name matches the node name to be written */
		/* to the node record, then reassign the node position	*/
		/* and type to be that of the port, so we don't have a	*/
		/* conflict.						*/

		if (!strcmp(extNodeName((LabRegion *) reg),
			ll->ll_label->lab_text))
		{
		    reg->nreg_ll.p_x = ll->ll_label->lab_rect.r_xbot;
		    reg->nreg_ll.p_y = ll->ll_label->lab_rect.r_ybot;
		    reg->nreg_type = ll->ll_label->lab_type;
		    reg->nreg_pnum = DBPlane(reg->nreg_type);
		}
	    }

    for (reg = nodeList; reg && !SigInterruptPending; reg = reg->nreg_next)
    {
	/* Output the node */
	text = extNodeName((LabRegion *) reg);

	/* Check if this node is the substrate */
	if (reg == glob_subsnode)
	{
	    fprintf(outFile, "substrate \"%s\" 0 0", text);
	}
	else
	{
	    intR = (reg->nreg_resist + rround) / ExtCurStyle->exts_resistScale;
	    finC = reg->nreg_cap/ExtCurStyle->exts_capScale;
	    fprintf(outFile, "node \"%s\" %d %lg", text, intR, finC);
	}

	/* Output its location (lower-leftmost point and type name) */

	if (reg->nreg_type & TT_DIAGONAL) {
	    /* Node may be recorded as a diagonal tile if no other	*/
	    /* non-diagonal tiles are adjoining it.			*/

	    TileType loctype = (reg->nreg_type & TT_SIDE) ? ((reg->nreg_type &
			TT_RIGHTMASK) >> 14) : (reg->nreg_type & TT_LEFTMASK);

	    fprintf(outFile, " %d %d %s",
		    reg->nreg_ll.p_x, reg->nreg_ll.p_y,
		    DBTypeShortName(loctype));
	}
	else
	{
	    fprintf(outFile, " %d %d %s",
		    reg->nreg_ll.p_x, reg->nreg_ll.p_y,
		    DBTypeShortName(reg->nreg_type));
	}

	/* Output its area and perimeter for each resistivity class */
	for (n = 0; n < ExtCurStyle->exts_numResistClasses; n++)
	    fprintf(outFile, " %d %d", reg->nreg_pa[n].pa_area,
				reg->nreg_pa[n].pa_perim);
	(void) putc('\n', outFile);

	/* Output its attribute list */
	for (ll = reg->nreg_labels; ll; ll = ll->ll_next)
	    if (extLabType(ll->ll_label->lab_text, LABTYPE_NODEATTR))
	    {
		/* Don't output the trailing character for node attributes */
		lab = ll->ll_label;
		fprintf(outFile, "attr %s %d %d %d %d %s \"",
			    text, lab->lab_rect.r_xbot, lab->lab_rect.r_ybot,
			          lab->lab_rect.r_xtop, lab->lab_rect.r_ytop,
			          DBTypeShortName(lab->lab_type));
		cp = lab->lab_text;
		n = strlen(cp) - 1;
		while (n-- > 0)
		    putc(*cp++, outFile);
		fprintf(outFile, "\"\n");
	    }

	/* Output the alternate names for the node */
	for (ll = reg->nreg_labels; ll; ll = ll->ll_next)
	    if (ll->ll_label->lab_text == text)
	    {
		for (ll = ll->ll_next; ll; ll = ll->ll_next)
		     if (extLabType(ll->ll_label->lab_text, LABTYPE_NAME))
			fprintf(outFile, "equiv \"%s\" \"%s\"\n",
					    text, ll->ll_label->lab_text);
		break;
	    }
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * extFindDuplicateLabels --
 *
 * Verify that no node in the list 'nreg' has a label that appears in
 * any other node in the list.  Leave a warning turd if one is.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Leaves feedback attached to each node that contains a label
 *	duplicated in another node.
 *
 * ----------------------------------------------------------------------------
 */

void
extFindDuplicateLabels(def, nreg)
    CellDef *def;
    NodeRegion *nreg;
{
    static char *badmesg =
	"Label \"%s\" attached to more than one unconnected node: %s";
    bool hashInitialized = FALSE;
    char message[512], name[512], *text;
    NodeRegion *np, *np2;
    LabelList *ll, *ll2;
    HashEntry *he;
    NodeRegion *lastreg;
    NodeRegion badLabel;
    HashTable labelHash;
    Rect r;

    for (np = nreg; np; np = np->nreg_next)
    {
	for (ll = np->nreg_labels; ll; ll = ll->ll_next)
	{
	    text = ll->ll_label->lab_text;
	    if (!extLabType(text, LABTYPE_NAME))
		continue;

	    if (!hashInitialized)
		HashInit(&labelHash, 32, 0), hashInitialized = TRUE;
	    he = HashFind(&labelHash, text);
	    lastreg = (NodeRegion *) HashGetValue(he);
	    if (lastreg == (NodeRegion *) NULL)
		HashSetValue(he, (ClientData) np);
	    else if (lastreg != np && lastreg != &badLabel)
	    {
		/*
		 * Make a pass through all labels for all nodes.
		 * Leave a feedback turd over each instance of the
		 * offending label.
		 */
		for (np2 = nreg; np2; np2 = np2->nreg_next)
		{
		    for (ll2 = np2->nreg_labels; ll2; ll2 = ll2->ll_next)
		    {
			if (strcmp(ll2->ll_label->lab_text, text) == 0)
			{
			    extNumWarnings++;
			    if (!DebugIsSet(extDebugID, extDebNoFeedback))
			    {
				r.r_ll = r.r_ur = ll2->ll_label->lab_rect.r_ll;
				r.r_xbot--, r.r_ybot--, r.r_xtop++, r.r_ytop++;
				extMakeNodeNumPrint(name,
					    np2->nreg_pnum, np2->nreg_ll);
				(void) sprintf(message, badmesg, text, name);
				DBWFeedbackAdd(&r, message, def,
					    1, STYLE_PALEHIGHLIGHTS);
			    }
			}
		    }
		}

		/* Mark this label as already having generated an error */
		HashSetValue(he, (ClientData) &badLabel);
	    }
	}
    }

    if (hashInitialized)
	HashKill(&labelHash);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extNodeName --
 *
 * Given a pointer to a LabRegion, return a pointer to a string
 * that can be printed as the name of the node.  If the LabRegion
 * has a list of attached labels, use one of the labels; otherwise,
 * use its node number.
 *
 * Results:
 *	Returns a pointer to a string.  If the node had a label, this
 *	is a pointer to the lab_text field of the first label on the
 *	label list for the node; otherwise, it is a pointer to a static
 *	buffer into which we have printed the node number.
 *
 * Side effects:
 *	May overwrite the static buffer used to hold the printable
 *	version of a node number.
 *
 * ----------------------------------------------------------------------------
 */

char *
extNodeName(node)
    LabRegion *node;
{
    static char namebuf[256];	/* Big enough to hold a generated nodename */
    LabelList *ll;

    if (node == (LabRegion *) NULL || SigInterruptPending)
	return ("(none)");

    for (ll = node->lreg_labels; ll; ll = ll->ll_next)
	if (extLabType(ll->ll_label->lab_text, LABTYPE_NAME))
	    return (ll->ll_label->lab_text);

    extMakeNodeNumPrint(namebuf, node->lreg_pnum, node->lreg_ll);
    return (namebuf);
}

/*
 * ---------------------------------------------------------------------
 *
 * ExtSortTerminals --
 *
 * Sort the terminals of a transistor so that the terminal with the
 * lowest leftmost coordinate on the plane with the lowest number is
 * output first.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	The tr_termnode, tr_termlen, and tr_termpos entries may change.
 *
 * ---------------------------------------------------------------------
 */

void
ExtSortTerminals(tran, ll)
    struct transRec  *tran;
    LabelList *ll;
{
    int		nsd, changed;
    TermTilePos	*p1, *p2;
    NodeRegion	*tmp_node;
    TermTilePos	tmp_pos;
    int		tmp_len;
    LabelList   *lp;

    do
    {
	changed = 0;
	for( nsd = 0; nsd < tran->tr_nterm-1; nsd++ )
	{
	    p1 = &(tran->tr_termpos[nsd]);
	    p2 = &(tran->tr_termpos[nsd+1]);
	    if( p2->pnum > p1->pnum )
		continue;
	    else if( p2->pnum == p1->pnum )
	    {
		if( p2->pt.p_x > p1->pt.p_x )
		    continue;
		else if( p2->pt.p_x == p1->pt.p_x && p2->pt.p_y > p1->pt.p_y )
		    continue;
		else if( p2->pt.p_x == p1->pt.p_x && p2->pt.p_y == p1->pt.p_y )
		{
		    TxPrintf("Extract error:  Duplicate tile position, ignoring\n");
		    continue;
		}
	    }
	    changed = 1;
	    tmp_node = tran->tr_termnode[nsd];
	    tmp_pos = tran->tr_termpos[nsd];
	    tmp_len = tran->tr_termlen[nsd];

	    tran->tr_termnode[nsd] = tran->tr_termnode[nsd+1];
	    tran->tr_termpos[nsd] = tran->tr_termpos[nsd+1];
	    tran->tr_termlen[nsd] = tran->tr_termlen[nsd+1];
	    
	    tran->tr_termnode[nsd+1] = tmp_node;
	    tran->tr_termpos[nsd+1] = tmp_pos;
	    tran->tr_termlen[nsd+1] = tmp_len;
	   /* Need to SWAP the indices in the labRegion too. 
            * These for loops within the  bubblesort in here are kinda slow 
            *  but S,D attributes are not that common so it should not matter 
            * that much -- Stefanos 5/96 */
            for ( lp = ll ; lp ; lp = lp->ll_next ) 
		if ( lp->ll_attr == nsd ) lp->ll_attr = LL_SORTATTR ;
		else if ( lp->ll_attr == nsd+1 ) lp->ll_attr = nsd ;
            for ( lp = ll ; lp ; lp = lp->ll_next ) 
		 if ( lp->ll_attr == LL_SORTATTR ) lp->ll_attr = nsd+1;
	}
     }
     while( changed );
}

/*
 *----------------------------------------------------------------------
 *
 * extComputeCapLW --
 *
 * Determine effective length and width of a rectangular capacitor,
 * based on the boundary vectors stored in extSpecialBounds.  This
 * routine should only be called for capacitors that have exactly
 * one terminal.
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	Puts effective length and width into the pointers
 *	passed as arguments.
 *----------------------------------------------------------------------
 */

void
extComputeCapLW(rlengthptr, rwidthptr)
   int *rlengthptr, *rwidthptr;
{
    LinkedBoundary *lb;
    Rect bbox;

    /* Quick algorithm---ignore tabs, compute max extents of	*/
    /* the special bounds vector.				*/

    lb = extSpecialBounds[0];
    if (lb == NULL)
    {
	TxError("extract:  Can't get capacitor L and W\n");
	return;	/* error condition */
    }
    bbox = lb->r;
    for (lb = extSpecialBounds[0]; lb != NULL; lb = lb->b_next)
	GeoIncludeAll(&lb->r, &bbox);

    *rwidthptr = bbox.r_xtop - bbox.r_xbot;
    *rlengthptr = bbox.r_ytop - bbox.r_ybot;
}

/*
 *----------------------------------------------------------------------
 *
 * extComputeEffectiveLW --
 *
 * Determine effective length and width of an annular (or otherwise
 * non-rectangular) transistor structure, based on the boundary vectors
 * stored in extSpecialBounds.
 *
 * Note that "L" and "W" are reversed when this routine is called
 * to compute L and W for a resistor.  The sense of "length" and
 * "width" as used in the routine are appropriate for a transistor.
 *
 * Also note that this algorithm will tend to over-estimate the width
 * of transistors with angled bends.  This problem would be eliminated
 * if non-Manhattan geometry were evaluated directly rather than being
 * first converted to Manhattan geometry.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Puts effective length and width into the pointers
 *	passed as arguments.
 *----------------------------------------------------------------------
 */

void
extComputeEffectiveLW(rlengthptr, rwidthptr, numregions, chop)
   int *rlengthptr, *rwidthptr;
   int numregions;
   float chop;
{
    int i, j, p, jmax;
    LinkedBoundary *lb, *lb2;
    int oppdir, length, loclength, testlen, width;
    int locwidth, testwid, cornerw;
    int segp, segn, segc, sege;
    bool isComplex = FALSE;

    /* First, check for MOScap-connected transistors.  In such
     * cases, one or more extSpecialBounds[] is NULL.  Try to
     * separate the existing extSpecialBounds[] vectors into 
     * independent (non-connecting) vectors.
     */

    /* For each segment in the primary list, find the closest
     * segment in the other list which lies on the opposite
     * side of the gate area.  Calculate the length, and check
     * for overlap, treating the length as a corner extension.
     *
     * The primary list is chosen as the one with the largest
     * number of elements.  This helps prevent the algorithm from
     * producing a different result for devices at different
     * orientations.
     */

    p = 0;
    jmax = 0;
    for (i = 0; i < numregions; i++)
    {
	j = 0;
	for (lb = extSpecialBounds[i]; lb != NULL; lb = lb->b_next) j++;
	if (j > jmax)
	{
	    jmax = j;
	    p = i;
	}
    }

    /* fprintf(stderr, "Annular transistor detailed L,W computation:\n"); */

    width = 0;
    length = 0;
    for (lb = extSpecialBounds[p]; lb != NULL; lb = lb->b_next)
    {
	loclength = INFINITY;
	switch (lb->dir)
	{
	    case BD_LEFT:   oppdir = BD_RIGHT;  break;
	    case BD_RIGHT:  oppdir = BD_LEFT;   break;
	    case BD_TOP:    oppdir = BD_BOTTOM; break;
	    case BD_BOTTOM: oppdir = BD_TOP;    break;
	}

	/* First pass: Find the distance of the closest segment within	*/
	/* the range of	its corner extension.  We do two passes because */
	/* there may be more than one segment at this distance.		*/

        for (i = 0; i < numregions; i++)
	{
	    if ((i == p) && (numregions > 1)) continue;
	    for (lb2 = extSpecialBounds[i]; lb2 != NULL; lb2 = lb2->b_next)
	    {
		if (lb2->dir == oppdir)
		{
		    switch (lb->dir)
		    {
			case BD_LEFT:
			    if (lb2->r.r_xbot > lb->r.r_xbot)
			    {
				testlen = lb2->r.r_xbot - lb->r.r_xbot;
				if (lb2->r.r_ybot < lb->r.r_ytop + testlen &&
				    	lb2->r.r_ytop > lb->r.r_ybot - testlen)
				{
				    /* Adjustments for offset segments */
				    if (lb2->r.r_ybot > lb->r.r_ytop)
					testlen += lb2->r.r_ybot - lb->r.r_ytop;
				    else if (lb2->r.r_ytop < lb->r.r_ybot)
					testlen += lb->r.r_ybot - lb2->r.r_ytop;

				    if (testlen < loclength) loclength = testlen;
				}
			    }
			    break;
			case BD_RIGHT:
			    if (lb2->r.r_xtop < lb->r.r_xtop)
			    {
				testlen = lb->r.r_xtop - lb2->r.r_xtop;
				if (lb2->r.r_ybot < lb->r.r_ytop + testlen &&
				    	lb2->r.r_ytop > lb->r.r_ybot - testlen)
				{
				    /* Adjustments for offset segments */
				    if (lb2->r.r_ybot > lb->r.r_ytop)
					testlen += lb2->r.r_ybot - lb->r.r_ytop;
				    else if (lb2->r.r_ytop < lb->r.r_ybot)
					testlen += lb->r.r_ybot - lb2->r.r_ytop;

				    if (testlen < loclength) loclength = testlen;
				}
			    }
			    break;
			case BD_TOP:
			    if (lb2->r.r_ytop < lb->r.r_ytop)
			    {
				testlen = lb->r.r_ytop - lb2->r.r_ytop;
				if (lb2->r.r_xbot < lb->r.r_xtop + testlen &&
				    	lb2->r.r_xtop > lb->r.r_xbot - testlen)
				{
				    /* Adjustments for offset segments */
				    if (lb2->r.r_xbot > lb->r.r_xtop)
					testlen += lb2->r.r_xbot - lb->r.r_xtop;
				    else if (lb2->r.r_xtop < lb->r.r_xbot)
					testlen += lb->r.r_xbot - lb2->r.r_xtop;

				    if (testlen < loclength) loclength = testlen;
				}
			    }
			    break;
			case BD_BOTTOM:
			    if (lb2->r.r_ybot > lb->r.r_ybot)
			    {
				testlen = lb2->r.r_ybot - lb->r.r_ybot;
				if (lb2->r.r_xbot < lb->r.r_xtop + testlen &&
				    	lb2->r.r_xtop > lb->r.r_xbot - testlen)
				{
				    /* Adjustments for offset segments */
				    if (lb2->r.r_xbot > lb->r.r_xtop)
					testlen += lb2->r.r_xbot - lb->r.r_xtop;
				    else if (lb2->r.r_xtop < lb->r.r_xbot)
					testlen += lb->r.r_xbot - lb2->r.r_xtop;

				    if (testlen < loclength) loclength = testlen;
				}
			    }
			    break;
		    }
		}
	    }
	}

	/* This segment should not be considered current-carrying; it	*/
	/* only adds to the gate capacitance.  Should we output the	*/
	/* extra capacitance somewhere?					*/

	if (loclength == INFINITY) continue;

	/* Note that the L/W calculation ignores the possibility that a	*/
	/* transistor may have multiple lengths.  Such cases should	*/
	/* either 1) scale the width to one of the lengths, or 2) out-	*/
	/* put a separate transistor record for each length.		*/

	if (length == 0)
	    length = loclength;		/* Default length */

	else if ((length != 0) && (length != loclength))
	{
	    /* If the newly computed length is less than the	*/
	    /* original, scale the original.  Otherwise, scale	*/
	    /* the new length.					*/

	    if (loclength < length)
	    {
		width *= loclength;
		width /= length;
		length = loclength;
	    } 
	    isComplex = TRUE;
	}

	/* fprintf(stderr, "   segment length = %d\n", loclength); */

	/* Second pass:  All segments at "length" distance add to the	*/
	/* length and width calculation.  Sides opposite and corner	*/
	/* extensions are treated separately.  Areas outside the corner	*/
	/* extension are ignored.					*/

	locwidth = 0;
	cornerw = 0;
        for (i = 0; i < numregions; i++)
	{
	    if ((i == p) && (numregions > 1)) continue;
	    for (lb2 = extSpecialBounds[i]; lb2 != NULL; lb2 = lb2->b_next)
	    {
		if (lb2->dir == oppdir)
		{
		    if (((lb->dir == BD_LEFT) &&
			(lb2->r.r_xbot - lb->r.r_xbot == loclength)) ||
			((lb->dir == BD_RIGHT) &&
			(lb->r.r_xtop - lb2->r.r_xtop == loclength)))
		    {
			/* opposite */
			segp = MIN(lb2->r.r_ytop, lb->r.r_ytop);
			segn = MAX(lb2->r.r_ybot, lb->r.r_ybot);
			testwid = segp - segn;
			if (testwid > 0) locwidth += testwid * 2;
			if (testwid <= -loclength) continue;

			/* corner extend top */
			segc = MAX(lb2->r.r_ytop, lb->r.r_ytop);
			sege = MAX(segp, segn);
			testwid = segc - sege;
			if (testwid > loclength) testwid = loclength;
			if (testwid > 0) cornerw += testwid;
			
			/* corner extend bottom */
			segc = MIN(lb2->r.r_ybot, lb->r.r_ybot);
			sege = MIN(segp, segn);
			testwid = sege - segc;
			if (testwid > loclength) testwid = loclength;
			if (testwid > 0) cornerw += testwid;
		    }
		    else if (((lb->dir == BD_TOP) &&
			(lb->r.r_ytop - lb2->r.r_ytop == loclength)) ||
			((lb->dir == BD_BOTTOM) &&
			(lb2->r.r_ybot - lb->r.r_ybot == loclength)))
		    {
			/* opposite */
			segp = MIN(lb2->r.r_xtop, lb->r.r_xtop);
			segn = MAX(lb2->r.r_xbot, lb->r.r_xbot);
			testwid = segp - segn;
			if (testwid > 0) locwidth += testwid * 2;
			if (testwid <= -loclength) continue;

			/* corner extend right */
			segc = MAX(lb2->r.r_xtop, lb->r.r_xtop);
			sege = MAX(segp, segn);
			testwid = segc - sege;
			if (testwid > loclength) testwid = loclength;
			if (testwid > 0) cornerw += testwid;
			
			/* corner extend left */
			segc = MIN(lb2->r.r_xbot, lb->r.r_xbot);
			sege = MIN(segp, segn);
			testwid = sege - segc;
			if (testwid > loclength) testwid = loclength;
			if (testwid > 0) cornerw += testwid;
		    }
		}
	    }
	}
	/* if (width > 0)
	    fprintf(stderr, "   segment width = %d\n", width); */

	/* Width scaling for transistor sections with different lengths */
	locwidth += (int)(0.5 + ((float)cornerw * chop));
	if (loclength != length)
	{
	    locwidth *= length;
	    locwidth /= loclength;
	}
	width += locwidth;
    }
    if ((length > 0) && (width > 0))
    {
	*rlengthptr = length;

	// If numregions == 1 then everything was put in one record,
	// and we have double-counted the width.

	if (numregions == 1)
	    *rwidthptr = (width >> 2);
	else
	    *rwidthptr = (width >> 1);

	/* fprintf(stderr, "total L = %d, W = %d\n", length, width); */
	/* fflush(stderr); */

	if (isComplex)
	    TxError("Device has multiple lengths:  scaling"
			" all widths to length %d\n", length);
    }
}
	
/*
 * ----------------------------------------------------------------------------
 *
 * extSeparateBounds --
 *
 * Because the non-source/drain perimeter is not a node, all the
 * boundary vectors end up in one record.  So we have to pry them
 * apart.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Messes with the extSpecialBounds[] linked lists.
 *
 * ----------------------------------------------------------------------------
 */

void
extSeparateBounds(nterm)
    int nterm;			/* last terminal (# terminals - 1) */
{
    Rect lbrect;
    LinkedBoundary *lb, *lbstart, *lbend, *lblast, *lbnext;
    bool found;

    /* Avoid crash condition on a badly-defined extract definition */
    if ((nterm < 0) || (extSpecialBounds[0] == NULL)) return;

    if (extSpecialBounds[nterm] == NULL)
    {
	/* Put first record into the unused terminal entry */
	extSpecialBounds[nterm] = extSpecialBounds[0];
	extSpecialBounds[0] = extSpecialBounds[nterm]->b_next;
	extSpecialBounds[nterm]->b_next = NULL;

	/* Add connected segments until no more are found */
	lbstart = lbend = extSpecialBounds[nterm];
	lbrect = lbstart->r;
	found = TRUE;
	while (found == TRUE)
	{
	    lblast = NULL;
	    found = FALSE;
	    for (lb = extSpecialBounds[0]; lb != NULL; lb = lbnext)
	    {
		/* perhaps we should cut down on these cases by */
		/* checking the direction of the segment. . . 	*/

		lbnext = lb->b_next;
		if (((lb->r.r_xbot == lbrect.r_xbot) &&
		    (lb->r.r_ybot == lbrect.r_ybot)))
		{
		    if (lblast == NULL)
			extSpecialBounds[0] = lb->b_next;
		    else
			lblast->b_next = lb->b_next;
		    // Insert lb after lbstart
		    lb->b_next = lbstart->b_next;
		    lbstart->b_next = lb;
		    lbstart = lb;
		    lbrect.r_xbot = lb->r.r_xtop;
		    lbrect.r_ybot = lb->r.r_ytop;
		    found = TRUE;
		}
		else if (((lb->r.r_xtop == lbrect.r_xbot) &&
		    (lb->r.r_ytop == lbrect.r_ybot)))
		{
		    if (lblast == NULL)
			extSpecialBounds[0] = lb->b_next;
		    else
			lblast->b_next = lb->b_next;
		    lb->b_next = lbstart->b_next;
		    lbstart->b_next = lb;
		    lbstart = lb;
		    lbrect.r_xbot = lb->r.r_xbot;
		    lbrect.r_ybot = lb->r.r_ybot;
		    found = TRUE;
		}
		else if (((lb->r.r_xtop == lbrect.r_xtop) &&
		    (lb->r.r_ytop == lbrect.r_ytop)))
		{
		    if (lblast == NULL)
			extSpecialBounds[0] = lb->b_next;
		    else
			lblast->b_next = lb->b_next;
		    lb->b_next = lbend->b_next;
		    lbend->b_next = lb;
		    lbend = lb;
		    lbrect.r_xtop = lb->r.r_xbot;
		    lbrect.r_ytop = lb->r.r_ybot;
		    found = TRUE;
		}
		else if (((lb->r.r_xbot == lbrect.r_xtop) &&
		    (lb->r.r_ybot == lbrect.r_ytop)))
		{
		    if (lblast == NULL)
			extSpecialBounds[0] = lb->b_next;
		    else
			lblast->b_next = lb->b_next;
		    lb->b_next = lbend->b_next;
		    lbend->b_next = lb;
		    lbend = lb;
		    lbrect.r_xtop = lb->r.r_xtop;
		    lbrect.r_ytop = lb->r.r_ytop;
		    found = TRUE;
		}
		else
		    lblast = lb;
	    }
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * extOutputParameters --
 *
 * Scan through the TransRegion in the supplied list, and collect a mask of
 * all transistor types used in the layout.  Then for each transistor type,
 * find if it belongs to a "subcircuit" (including "rsubcircuit" and
 * "msubcircuit") definition.  If it does, output a record containing the
 * list of parameter names used by that subcircuit.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Possibly writes to outFile.  The purpose of this scan is not to have
 *	to write out shared parameter information for every individual device.
 * ----------------------------------------------------------------------------
 */

void
extOutputParameters(def, transList, outFile)
    CellDef *def;		/* Cell being extracted */
    TransRegion *transList;	/* Transistor regions built up in first pass */
    FILE *outFile;		/* Output file */
{
    ParamList *plist;
    TransRegion *reg;
    TileType t;
    TileTypeBitMask tmask;

    TTMaskZero(&tmask);

    for (reg = transList; reg && !SigInterruptPending; reg = reg->treg_next)
    {
	TileType loctype = reg->treg_type;

	/* Watch for rare split reg->treg_type */
	if (loctype & TT_DIAGONAL)
	    loctype = (reg->treg_type & TT_SIDE) ? ((reg->treg_type &
			TT_RIGHTMASK) >> 14) : (reg->treg_type & TT_LEFTMASK);

	TTMaskSetType(&tmask, loctype);
    }

    for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
    {
	if (TTMaskHasType(&tmask, t))
	{
	    plist = ExtCurStyle->exts_deviceParams[t];
	    if (plist != (ParamList *)NULL)
	    {
		fprintf(outFile, "parameters %s", ExtCurStyle->exts_transName[t]);
		for (; plist != NULL; plist = plist->pl_next)
		{
		    if (plist->pl_param[1] != '\0')
		    {
			if (plist->pl_scale != 1.0)
			    fprintf(outFile, " %c%c=%s*%g", 
					plist->pl_param[0], plist->pl_param[1],
					plist->pl_name, plist->pl_scale);
			else
			    fprintf(outFile, " %c%c=%s", plist->pl_param[0],
					plist->pl_param[1], plist->pl_name);
		    }
		    else
		    {
			if (plist->pl_scale != 1.0)
			    fprintf(outFile, " %c=%s*%g", 
					plist->pl_param[0],
					plist->pl_name, plist->pl_scale);
			else
			    fprintf(outFile, " %c=%s", plist->pl_param[0],
					plist->pl_name);
		    }
		}
		fprintf(outFile, "\n");
	    }
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * extGetNativeResistClass() --
 *
 * For the purpose of generating a node area and perimeter value to output
 * to a subcircuit call as a passed parameter.  The value output is assumed
 * to refer only to the part of the whole eletrical node that is the
 * actual device node, not to include connected metal, contacts, etc.
 * Since area and perimeter information about a node is separated into
 * resist classes, we need to figure out which resist class belongs to
 * the device terminal type.
 *
 * "type" is the type identifier for the device (e.g., gate).  "term" is
 * the index of the terminal for the device.  Devices with symmetrical
 * terminals (e.g., MOSFETs), may have fewer type masks than terminals.
 *
 * ----------------------------------------------------------------------------
 */

int
extGetNativeResistClass(type, term)
    TileType type;
    int term;
{
    TileTypeBitMask *tmask, *rmask;
    int i, n;

    tmask = NULL;
    for (i = 0;; i++)
    {
	rmask = &ExtCurStyle->exts_transSDTypes[type][i];
	if (TTMaskIsZero(rmask)) break;
	tmask = rmask;
	if (i == term) break;
    }
    if (tmask == NULL) return -1;	/* Error */

    for (n = 0; n < ExtCurStyle->exts_numResistClasses; n++)
    {
	rmask = &ExtCurStyle->exts_typesByResistClass[n];
	if (TTMaskIntersect(rmask, tmask))
	    return n;
    }
    return -1; 		/* Error */
}

/*
 * ----------------------------------------------------------------------------
 *
 * extOutputDevParams ---
 *
 *	Write information to the output in the form of parameters
 *	representing pre-defined aspects of the device geometry
 *	that may be specified for any device.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes non-terminated output to the file 'outFile'.
 *
 * ----------------------------------------------------------------------------
 */

void
extOutputDevParams(reg, t, outFile, length, width)
    TransRegion *reg;
    TileType t;
    FILE *outFile;
    int length;
    int width;
{
    ParamList *chkParam;

    for (chkParam = ExtCurStyle->exts_deviceParams[t]; chkParam
		!= NULL; chkParam = chkParam->pl_next)
    {
	switch(tolower(chkParam->pl_param[0]))
	{
	    case 'a':
		if (chkParam->pl_param[1] == '\0' ||
			chkParam->pl_param[1] == '0')
		    fprintf(outFile, " %c=%d", chkParam->pl_param[0],
				reg->treg_area);
		break;
	    case 'p':
		if (chkParam->pl_param[1] == '\0' ||
			chkParam->pl_param[1] == '0')
		    fprintf(outFile, " %c=%d", chkParam->pl_param[0],
				extTransRec.tr_perim);
		break;
	    case 'l':
		fprintf(outFile, " %c=%d", chkParam->pl_param[0],
				length);
		break;
	    case 'w':
		fprintf(outFile, " %c=%d", chkParam->pl_param[0],
				width);
		break;
	    case 'c':
		fprintf(outFile, " %c=%g", chkParam->pl_param[0],
			(ExtCurStyle->exts_transGateCap[t]
			* reg->treg_area) +
			(ExtCurStyle->exts_transSDCap[t]
			* extTransRec.tr_perim));
		break;
	    case 's':
	    case 'x':
	    case 'y':
		/* Do nothing;  these values are standard output */
		break;
	    default:
		fprintf(outFile, " %c=", chkParam->pl_param[0]);
		break;
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * extOutputDevices --
 *
 * For each TransRegion in the supplied list, corresponding to a single
 * transistor in the layout, compute and output:
 *	- Its type
 *	- Its area and perimeter OR length and width OR capacitance OR resistance
 *	- Its substrate node
 *	- For each of the gate, and the various diff terminals (eg,
 *	  source, drain):
 *		Node to which the terminal connects
 *		Length of the terminal
 *		Attributes (comma-separated), or 0 if none.
 *
 * The tiles in 'def' don't point back to the TransRegions in this list,
 * but rather to the NodeRegions corresponding to their electrical nodes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes a number of 'fet' records to the file 'outFile'.
 *
 * Interruptible.  If SigInterruptPending is detected, we stop traversing
 * the transistor list and return.
 *
 * ----------------------------------------------------------------------------
 */

void
extOutputDevices(def, transList, outFile)
    CellDef *def;		/* Cell being extracted */
    TransRegion *transList;	/* Transistor regions built up in first pass */
    FILE *outFile;		/* Output file */
{
    NodeRegion *node, *subsNode;
    TransRegion *reg;
    char *subsName;
    FindRegion arg;
    LabelList *ll;
    TileType t;
    int nsd, length, width, n, i, ntiles, corners, tn, rc;
    double dres, dcap;
    char mesg[256];
    bool isAnnular, hasModel;

    for (reg = transList; reg && !SigInterruptPending; reg = reg->treg_next)
    {
	/*
	 * Visit all of the tiles in the transistor region, updating
	 * extTransRec.tr_termnode[] and extTransRec.tr_termlen[],
	 * and the attribute lists for this transistor.
	 *
	 * Algorithm: first visit all tiles in the transistor, marking
	 * them with 'reg', then visit them again re-marking them with
	 * the gate node (extGetRegion(reg->treg_tile)).
	 */
	extTransRec.tr_nterm = 0;
	extTransRec.tr_gatelen = 0;
	extTransRec.tr_perim = 0;
	extTransRec.tr_subsnode = (NodeRegion *)NULL;

	arg.fra_def = def;
	arg.fra_connectsTo = ExtCurStyle->exts_transConn;

	extTransRec.tr_gatenode = (NodeRegion *) extGetRegion(reg->treg_tile);
	t = reg->treg_type;

	/* Watch for rare split reg->treg_type */
	if (t & TT_DIAGONAL)
	    t = (reg->treg_type & TT_SIDE) ? ((reg->treg_type &
			TT_RIGHTMASK) >> 14) : (reg->treg_type & TT_LEFTMASK);

	arg.fra_pNum = DBPlane(t);

	/* Set all terminals to NULL to guard against	*/
	/* asymmetric devices missing a terminal.	*/
	/* 5/30/09---but, reinitialize the array out to MAXSD,	*/
	/* or devices declaring minterms < maxterms screw up!	*/

	nsd = ExtCurStyle->exts_transSDCount[t];
	for (i = 0; i < MAXSD; i++) extTransRec.tr_termnode[i] = NULL;

	/* Mark with reg and process each perimeter segment */
	arg.fra_uninit = (ClientData) extTransRec.tr_gatenode;
	arg.fra_region = (Region *) reg;
	arg.fra_each = extTransTileFunc;
	ntiles = ExtFindNeighbors(reg->treg_tile, arg.fra_pNum, &arg);

	/* Re-mark with extTransRec.tr_gatenode */
	arg.fra_uninit = (ClientData) reg;
	arg.fra_region = (Region *) extTransRec.tr_gatenode;
	arg.fra_each = (int (*)()) NULL;
	(void) ExtFindNeighbors(reg->treg_tile, arg.fra_pNum, &arg);

	/* Are the terminal types on a compeletely different	*/
	/* plane than the top type?  If so, do an area search	*/
	/* on that plane in the area under the device node.	*/

	/* Devices may define one terminal per plane, but this	*/
	/* method cannot handle several different layer types	*/
	/* on one plane under the device identifier layer	*/
	/* acting as separate device nodes.  If any terminal	*/
	/* search fails, give up and proceed with the reduced	*/
	/* number of terminals.					*/

	while (extTransRec.tr_nterm < nsd)
	{
	    TileTypeBitMask *tmask;

	    tmask = &ExtCurStyle->exts_transSDTypes[t][extTransRec.tr_nterm];
	    if (TTMaskIsZero(tmask)) break;
	    if (!TTMaskIntersect(tmask, &DBPlaneTypes[reg->treg_pnum]))
	    {
		node = NULL;
		extTransFindSubs(reg->treg_tile, t, tmask, def, &node);
		if (node == NULL) break;
		extTransRec.tr_termnode[extTransRec.tr_nterm++] = node;
	    }
	    else if (TTMaskHasType(tmask, TT_SPACE)) {
		/* Device node is specified as being the substrate */
		if (glob_subsnode == NULL) break;
		extTransRec.tr_termnode[extTransRec.tr_nterm++] = glob_subsnode;
	    }
	    else break;
	}

	/*
	 * For types that require a minimum number of terminals,
	 * check to make sure that they all exist.  If they don't,
	 * issue a warning message and make believe the missing
	 * terminals are the same as the last terminal we do have.
	 */
	if (extTransRec.tr_nterm < nsd)
	{
	    int missing = nsd - extTransRec.tr_nterm;

	    (void) sprintf(mesg, "device missing %d terminal%s", missing,
					missing == 1 ? "" : "s");
	    if (extTransRec.tr_nterm > 0)
	    {
		node = extTransRec.tr_termnode[extTransRec.tr_nterm - 1];
		(void) strcat(mesg, ";\n connecting remainder to node ");
		(void) strcat(mesg, extNodeName((LabRegion *) node));
		while (extTransRec.tr_nterm < nsd) 
		{
		    extTransRec.tr_termlen[extTransRec.tr_nterm] = 0;
		    extTransRec.tr_termnode[extTransRec.tr_nterm++] = node;
		}
	    }
	    if (ExtDoWarn & EXTWARN_FETS)
		extTransBad(def, reg->treg_tile, mesg);
	}
	else if (extTransRec.tr_nterm > nsd)
	{
	    /* It is not an error condition to have more terminals */
	    /* than the minimum.				   */
	}

	/*
	 * Output the transistor record.
	 * The type is ExtCurStyle->exts_transName[t], which should have
	 * some meaning to the simulator we are producing this file for.
	 * Use the default substrate node unless the transistor overlaps
	 * material whose type is in exts_transSubstrateTypes, in which
	 * case we use the node of the overlapped material.
	 *
	 * Technology files using the "substrate" keyword (magic-8.1 or
	 * newer) should have the text "error" in the substrate node
	 * name.
	 */
	subsName = ExtCurStyle->exts_transSubstrateName[t];
	if (!TTMaskIsZero(&ExtCurStyle->exts_transSubstrateTypes[t])
		&& (subsNode = extTransRec.tr_subsnode))
	{
	    subsName = extNodeName(subsNode);
	}

#ifdef MAGIC_WRAPPER

	// Substrate variable substitution when in backwards-compatibility
	// substrate mode.

	else if ((ExtCurStyle->exts_globSubstratePlane == -1) &&
		(subsName && subsName[0] == '$' && subsName[1] != '$'))
	{
	    // If subsName is a Tcl variable (begins with "$"), make the
	    // variable substitution, if one exists.  Ignore double-$.

	    char *varsub = (char *)Tcl_GetVar(magicinterp, &subsName[1],
			TCL_GLOBAL_ONLY);
	    if (varsub != NULL) subsName = varsub;
	}
#endif

	/* Original-style FET record backward compatibility */
	if (ExtCurStyle->exts_deviceClass[t] != DEV_FET)
	    fprintf(outFile, "device ");

	fprintf(outFile, "%s %s",
			extDevTable[ExtCurStyle->exts_deviceClass[t]],
			ExtCurStyle->exts_transName[t]);

	fprintf(outFile, " %d %d %d %d",
		reg->treg_ll.p_x, reg->treg_ll.p_y, 
		reg->treg_ll.p_x + 1, reg->treg_ll.p_y + 1);

	/* NOTE:  The following code makes unreasonable simplifying	*/
	/* assumptions about how to calculate device length and	width.	*/
	/* However, it is the same as was always used by ext2sim and	*/
	/* ext2spice.  By putting it here, where all the tile		*/
	/* information exists, it is at least theoretically possible to	*/
	/* write better routines that can deal with bends in resistors	*/
	/* and transistors, annular devices, multiple-drain devices,	*/
	/* etc., etc.							*/
	/*				Tim, 2/20/03			*/

	switch (ExtCurStyle->exts_deviceClass[t])
	{
	    case DEV_FET:	/* old style, perimeter & area */
		fprintf(outFile, " %d %d \"%s\"",
		    reg->treg_area, extTransRec.tr_perim, 
				(subsName == NULL) ? "None" : subsName);
		break;

	    /* "device <class>" types, calculation of length & width */

	    case DEV_MOSFET:
	    case DEV_BJT:
	    case DEV_SUBCKT:
	    case DEV_MSUBCKT:
	    case DEV_ASYMMETRIC:
		length = extTransRec.tr_gatelen / 2;	/* (default) */
		width = 0;
		isAnnular = FALSE;

		/* Note that width is accumulated on one tr_termlen	*/
		/* record when nodes are merged, so proper behavior	*/
		/* for transistors w/connected S-D is to count over	*/
		/* non-NULL termnodes, not non-zero termlens.		*/

		for (n = 0; n < extTransRec.tr_nterm; n++)
		{
		    if (extTransRec.tr_termnode[n] == NULL) continue;

		    width += extTransRec.tr_termlen[n];

		    /* Mark annular transistors as requiring extra processing */
		    if (extTransRec.tr_termvector[n].p_x == 0 &&
				extTransRec.tr_termvector[n].p_y == 0)
			isAnnular = TRUE;
		}
		if (n) width /= n;

		/*------------------------------------------------------*/
		/* Note that the tr_termvector says a lot about the	*/
		/* device geometry.  If the sum of x and y for any	*/
		/* vector is 0, then the terminal is enclosed (annular	*/
		/* device).  If the sum of x and y for all vectors is	*/
		/* zero, then we have a normal rectangular device.  But	*/
		/* if the sum of all x and y is nonzero, then the	*/
		/* device length changes along the device (including	*/
		/* bends).  This is a trigger to do a more extensive	*/
		/* boundary search to find the exact dimensions of the	*/
		/* device.						*/
		/*------------------------------------------------------*/

		if (n == 0)
		{
		    /* Don't issue a warning on devices such as a 	*/
		    /* vertical diode that may declare zero terminals	*/
		    /* because the substrate node (i.e., well) is the	*/
		    /* other terminal.					*/

		    if (ExtDoWarn && (ExtCurStyle->exts_transSDCount[t] > 0))
			extTransBad(def, reg->treg_tile,
				"Could not determine device boundary");
		    length = width = 0;
		}
		else
		{
		    LinkedBoundary *lb;

		    extSpecialBounds = (LinkedBoundary **)mallocMagic(n *
				sizeof(LinkedBoundary *));

		    for (i = 0; i < n; i++) extSpecialBounds[i] = NULL;

		    /* Mark with reg and process each perimeter segment */

		    arg.fra_uninit = (ClientData) extTransRec.tr_gatenode;
		    arg.fra_region = (Region *) reg;
		    arg.fra_each = extAnnularTileFunc;
		    (void) ExtFindNeighbors(reg->treg_tile, arg.fra_pNum, &arg);

		    extSeparateBounds(n - 1);	/* Handle MOScaps (if necessary) */
		    extComputeEffectiveLW(&length, &width, n,
				ExtCurStyle->exts_cornerChop[t]);

		    /* Free the lists */

		    for (i = 0; i < n; i++)
			for (lb = extSpecialBounds[i]; lb != NULL; lb = lb->b_next)
			    freeMagic((char *)lb);
		    freeMagic((char *)extSpecialBounds);

		    /* Put the region list back the way we found it: */
		    /* Re-mark with extTransRec.tr_gatenode */

		    arg.fra_uninit = (ClientData) reg;
		    arg.fra_region = (Region *) extTransRec.tr_gatenode;
		    arg.fra_each = (int (*)()) NULL;
		    (void) ExtFindNeighbors(reg->treg_tile, arg.fra_pNum, &arg);

		}

		if (ExtCurStyle->exts_deviceClass[t] == DEV_MOSFET ||
			ExtCurStyle->exts_deviceClass[t] == DEV_ASYMMETRIC ||
			ExtCurStyle->exts_deviceClass[t] == DEV_BJT)
		{
		    fprintf(outFile, " %d %d", length, width);
		}

		extOutputDevParams(reg, t, outFile, length, width);

		fprintf(outFile, " \"%s\"", (subsName == NULL) ?
					"None" : subsName);
		break;

	    case DEV_DIODE:	/* Only handle the optional substrate node */
	    case DEV_NDIODE:
	    case DEV_PDIODE:
		extOutputDevParams(reg, t, outFile, length, width);
		if (subsName != NULL)
		    fprintf(outFile, " \"%s\"", subsName);
		break;

	    case DEV_RES:
	    case DEV_RSUBCKT:
		hasModel = strcmp(ExtCurStyle->exts_transName[t], "None");
		length = extTransRec.tr_perim;
		isAnnular = FALSE;
	
		/* Boundary perimeter scan for resistors with more than */
		/* one tile.						*/

		for (n = 0; n < extTransRec.tr_nterm; n++)
		{
		    if (extTransRec.tr_termnode[n] == NULL) continue;

		    /* Mark annular resistors as requiring extra processing */
		    if (extTransRec.tr_termvector[n].p_x == 0 &&
				extTransRec.tr_termvector[n].p_y == 0)
			isAnnular = TRUE;
		}

		if (n == 0)
		    width = length = 0;
		else if (ntiles > 1)
		{
		    LinkedBoundary *lb;

		    extSpecialBounds = (LinkedBoundary **)mallocMagic(n *
				sizeof(LinkedBoundary *));

		    for (i = 0; i < n; i++) extSpecialBounds[i] = NULL;

		    /* Mark with reg and process each perimeter segment */

		    arg.fra_uninit = (ClientData) extTransRec.tr_gatenode;
		    arg.fra_region = (Region *) reg;
		    if (isAnnular)
		        arg.fra_each = extAnnularTileFunc;
		    else
		        arg.fra_each = extResistorTileFunc;
		    (void) ExtFindNeighbors(reg->treg_tile, arg.fra_pNum, &arg);

		    if (extSpecialBounds[0] != NULL)
		    {
			extSeparateBounds(n - 1);
			if (isAnnular)
			    extComputeEffectiveLW(&length, &width, n, 
					ExtCurStyle->exts_cornerChop[t]);
			else
			    extComputeEffectiveLW(&width, &length, n, 
					ExtCurStyle->exts_cornerChop[t]);
		    }
		    else
		    {
			if (ExtDoWarn)
			    extTransBad(def, reg->treg_tile,
					"Could not determine resistor boundary");
			length = width = 0;
		    }

		    /* Free the lists */

		    for (i = 0; i < n; i++)
			for (lb = extSpecialBounds[i]; lb != NULL; lb = lb->b_next)
			    freeMagic((char *)lb);
		    freeMagic((char *)extSpecialBounds);

		    /* Put the region list back the way we found it: */
		    /* Re-mark with extTransRec.tr_gatenode */

		    arg.fra_uninit = (ClientData) reg;
		    arg.fra_region = (Region *) extTransRec.tr_gatenode;
		    arg.fra_each = (int (*)()) NULL;
		    (void) ExtFindNeighbors(reg->treg_tile, arg.fra_pNum, &arg);
		}
		else
		{
		    /* Single tile resistor means a simple L,W	*/
		    /* calculation from perimeter & area.	*/

		    width = 0;
		    for (n = 0; extTransRec.tr_termlen[n] != 0; n++)
		    {
			width += extTransRec.tr_termlen[n];
			length -= extTransRec.tr_termlen[n];
		    }
		    width >>= 1;
		    length >>= 1;
		}
		if (width)
		{
		    dres = ExtCurStyle->exts_sheetResist[t] * (double)length /
				(double)width;
		    if (ExtDoWarn && (n > 2))
		    {
			if (hasModel)
			    sprintf(mesg, "Resistor has %d terminals: "
					"extracted L/W will be wrong", n);
			else
			    sprintf(mesg, "Resistor has %d terminals: "
					"extracted value will be wrong", n);
			extTransBad(def, reg->treg_tile, mesg);
		    }
		}
		else {
		    dres = 0.0;
		    if (ExtDoWarn)
			extTransBad(def, reg->treg_tile,
					"Resistor has zero width");
		}

		extOutputDevParams(reg, t, outFile, length, width);

		if (ExtCurStyle->exts_deviceClass[t] == DEV_RSUBCKT)
		{
		    fprintf(outFile, " \"%s\"", (subsName == NULL) ?
				"None" : subsName);
		}
		else if (hasModel)	/* SPICE semiconductor resistor */
		{
		    fprintf(outFile, " %d %d", length, width);
		    if (subsName != NULL)
			fprintf(outFile, " \"%s\"", subsName);
		}
		else		/* regular resistor */
		    fprintf(outFile, " %g", dres / 1000.0); /* mOhms -> Ohms */
		break;

	    case DEV_CAP:
	    case DEV_CAPREV:
		hasModel = strcmp(ExtCurStyle->exts_transName[t], "None");
		if (hasModel)
		{
		    for (n = 0; n < extTransRec.tr_nterm &&
				extTransRec.tr_termnode[n] != NULL; n++);

		    /* Don't know what to do (yet) with capacitors	*/
		    /* multiple terminals (see below); treat them in	*/
		    /* the original naive manner.			*/

		    if (n == 0)
		    {
			width = 0;
			extTransBad(def, reg->treg_tile,
						"Capacitor has zero size");
			fprintf(outFile, " 0 0");
		    }
		    else
		    {
			/* Special handling of multiple-tile areas.	*/
			/* This algorithm assumes that the capacitor	*/
			/* has one terminal and that the area is a	*/
			/* rectangle.  It should be extended to output	*/
			/* multiple capacitors for multiple rectangular	*/
			/* areas, combining to form any arbitrary shape	*/

			LinkedBoundary *lb;

			extSpecialBounds = (LinkedBoundary **)mallocMagic(n *
				sizeof(LinkedBoundary *));

			for (i = 0; i < n; i++) extSpecialBounds[i] = NULL;

			/* Mark with reg and process each perimeter segment */

			arg.fra_uninit = (ClientData) extTransRec.tr_gatenode;
			arg.fra_region = (Region *) reg;
			arg.fra_each = extAnnularTileFunc;
			(void) ExtFindNeighbors(reg->treg_tile, arg.fra_pNum, &arg);

			extComputeCapLW(&length, &width);
			if ((length * width) > reg->treg_area)
			{
			    if (ExtDoWarn)
				extTransBad(def, reg->treg_tile, "L,W estimated "
					"for non-rectangular capacitor.");
			    fprintf(outFile, " %d %d", width,
					reg->treg_area / width);
			}
			else
			    fprintf(outFile, " %d %d", length, width);

			/* Free the lists */

			for (i = 0; i < n; i++)
			    for (lb = extSpecialBounds[i]; lb != NULL; lb = lb->b_next)
				freeMagic((char *)lb);
			freeMagic((char *)extSpecialBounds);

			/* Put the region list back the way we found it: */
			/* Re-mark with extTransRec.tr_gatenode */

			arg.fra_uninit = (ClientData) reg;
			arg.fra_region = (Region *) extTransRec.tr_gatenode;
			arg.fra_each = (int (*)()) NULL;
			(void) ExtFindNeighbors(reg->treg_tile, arg.fra_pNum, &arg);
		    }
		    extOutputDevParams(reg, t, outFile, length, width);
		    if (subsName != NULL)
			fprintf(outFile, " \"%s\"", subsName);
		}
		else
		{
		    dcap = (ExtCurStyle->exts_transGateCap[t] * reg->treg_area) +
			(ExtCurStyle->exts_transSDCap[t] * extTransRec.tr_perim);

		    fprintf(outFile, " %g", dcap / 1000.0);  /* aF -> fF */
		}
		break;
	}

	/* gate */
	node = (NodeRegion *) extGetRegion(reg->treg_tile);
	ll = node->nreg_labels;
	extTransOutTerminal((LabRegion *) node, ll, LL_GATEATTR,
			extTransRec.tr_gatelen, outFile);

	/* Sort source and drain terminals by position, unless the	*/
	/* device is asymmetric, in which case source and drain do not	*/
	/* permute, and the terminal order is fixed.			*/

	if (TTMaskIsZero(&ExtCurStyle->exts_transSDTypes[t][1]))
	    ExtSortTerminals(&extTransRec, ll); 

	/* each non-gate terminal */
	for (nsd = 0; nsd < extTransRec.tr_nterm; nsd++)
	    extTransOutTerminal((LabRegion *) extTransRec.tr_termnode[nsd], ll,
			nsd, extTransRec.tr_termlen[nsd], outFile);

	(void) fputs("\n", outFile);
    }
}

int
extTransFindSubs(tile, t, mask, def, sn)
    Tile *tile;
    TileType t;
    TileTypeBitMask *mask;
    CellDef *def;
    NodeRegion **sn;
{
    Rect tileArea;
    int pNum;
    int extTransFindSubsFunc1();	/* Forward declaration */

    TiToRect(tile, &tileArea);
    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
    {
	if (TTMaskIntersect(&DBPlaneTypes[pNum], mask))
	{
	    if (DBSrPaintArea((Tile *) NULL, def->cd_planes[pNum], &tileArea,
		    mask, extTransFindSubsFunc1, (ClientData)sn))
		return 1;
	}
    }
    return 0;
}

int
extTransFindSubsFunc1(tile, sn)
    Tile *tile;
    NodeRegion **sn;
{
    /* Report split substrate region errors (two different substrate
     * regions under the same device)
     */

    if (tile->ti_client != (ClientData) extUnInit)
    {
	if ((*sn != (NodeRegion *)NULL) && (*sn != tile->ti_client))
	    TxError("Warning:  Split substrate under device at (%d %d)\n",
			tile->ti_ll.p_x, tile->ti_ll.p_y);

	*sn = (NodeRegion *) tile->ti_client;
	return 1;
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extTransTileFunc --
 *
 * Filter function called by ExtFindNeighbors for each tile in a
 * transistor.  Responsible for collecting the nodes, lengths,
 * and attributes of all the terminals on this transistor.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Fills in the transRec structure extTransRec.
 *
 * ----------------------------------------------------------------------------
 */

int
extTransTileFunc(tile, pNum, arg)
    Tile *tile;
    int pNum;
    FindRegion *arg;
{
    TileTypeBitMask mask;
    TileType loctype;
    int perim;
    bool allow_globsubsnode;

    LabelList *ll;
    Label *lab;
    Rect r;

    for (ll = extTransRec.tr_gatenode->nreg_labels; ll; ll = ll->ll_next)
    {
	/* Skip if already marked */
	if (ll->ll_attr != LL_NOATTR) continue;
	lab = ll->ll_label;
	TITORECT(tile, &r);
	if (GEO_TOUCH(&r, &lab->lab_rect) && 
		extLabType(lab->lab_text, LABTYPE_GATEATTR))  
	{
	     ll->ll_attr = LL_GATEATTR;
	}
    }
    /*
     * Visit each segment of the perimeter of this tile that
     * that borders on something of a different type.
     */
    if (IsSplit(tile))
    {
        loctype = (SplitSide(tile)) ? SplitRightType(tile): SplitLeftType(tile);
	// return (0); 	/* Hack alert!  We must properly handle diagonals! */
    }
    else
	loctype = TiGetTypeExact(tile);

    mask = ExtCurStyle->exts_transConn[loctype];
    TTMaskCom(&mask);

    /* NOTE:  DO NOT USE extTransRec.tr_perim += extEnumTilePerim(...)	*/
    /* The AMD target gcc compile works and the Intel target gcc	*/
    /* compile doesn't!  The following code works the same on both.	*/

    perim = extEnumTilePerim(tile, mask, pNum,
		extTransPerimFunc, (ClientData)NULL);
    extTransRec.tr_perim += perim;

    allow_globsubsnode = FALSE;
    if (extTransRec.tr_subsnode == (NodeRegion *)NULL)
    {
	TileTypeBitMask *smask;

	smask = &ExtCurStyle->exts_transSubstrateTypes[loctype];
	if (TTMaskHasType(smask, TT_SPACE))
	{
	    allow_globsubsnode = TRUE;
	    TTMaskClearType(smask, TT_SPACE);
	}
	extTransFindSubs(tile, loctype, smask, arg->fra_def, &extTransRec.tr_subsnode);
	if (allow_globsubsnode)
	    TTMaskSetType(smask, TT_SPACE);
    }

    /* If the transistor does not connect to a defined node, and
     * the substrate types include "space", then it is assumed to
     * connect to the global substrate.
     */

    if (extTransRec.tr_subsnode == (NodeRegion *)NULL)
	if (allow_globsubsnode)
	    extTransRec.tr_subsnode = glob_subsnode;

    return (0);
}

int
extTransPerimFunc(bp)
    Boundary *bp;
{
    TileType tinside, toutside;
    Tile *tile;
    NodeRegion *diffNode = (NodeRegion *) extGetRegion(bp->b_outside);
    int i, len = BoundaryLength(bp);
    int thisterm;
    LabelList *ll;
    Label *lab;
    Rect r;
    bool SDterm = FALSE;

    tile = bp->b_inside;
    if (IsSplit(tile))
        tinside = (SplitSide(tile)) ? SplitRightType(tile): SplitLeftType(tile);
    else
        tinside = TiGetTypeExact(bp->b_inside);
    tile = bp->b_outside;
    if (IsSplit(tile))
        toutside = (SplitSide(tile)) ? SplitRightType(tile): SplitLeftType(tile);
    else
        toutside = TiGetTypeExact(bp->b_outside);

    for (i = 0; !TTMaskIsZero(&ExtCurStyle->exts_transSDTypes[tinside][i]); i++)
    {
	/* TT_SPACE is allowed, for declaring that a device terminal is	*/
	/* the substrate.  However, it should not be in the plane of	*/
	/* the device identifier layer, so space tiles should never be	*/
	/* flagged during a device perimeter search.			*/

	if (toutside == TT_SPACE) break;

	if (TTMaskHasType(&ExtCurStyle->exts_transSDTypes[tinside][i], toutside))
	{
	    /*
	     * It's a diffusion terminal (source or drain).  See if the node is
	     * already in our table; add it if it wasn't already there.
	     * Asymmetric devices must have terminals in order.
	     */
	    if (TTMaskIsZero(&ExtCurStyle->exts_transSDTypes[tinside][1]))
	    {
		for (thisterm = 0; thisterm < extTransRec.tr_nterm; thisterm++)
		    if (extTransRec.tr_termnode[thisterm] == diffNode)
			break;
	    }
	    else
		thisterm = i;

	    if (extTransRec.tr_termnode[thisterm] == NULL)
	    {
		extTransRec.tr_nterm++;
		extTransRec.tr_termnode[thisterm] = diffNode;
		extTransRec.tr_termlen[thisterm] = 0;
		extTransRec.tr_termvector[thisterm].p_x = 0;
		extTransRec.tr_termvector[thisterm].p_y = 0;
		extTransRec.tr_termpos[thisterm].pnum = DBPlane(toutside);
		extTransRec.tr_termpos[thisterm].pt = bp->b_outside->ti_ll;

		/* Find the total area of this terminal */
	    }
	    else if (extTransRec.tr_termnode[thisterm] == diffNode)
	    {
		TermTilePos  *pos = &(extTransRec.tr_termpos[thisterm]);
		Tile         *otile = bp->b_outside;

		/* update the region tile position */

		if( DBPlane(TiGetType(otile)) < pos->pnum )
		{
		    pos->pnum = DBPlane(TiGetType(otile));
		    pos->pt = otile->ti_ll;
		}
		else if( DBPlane(TiGetType(otile)) == pos->pnum )
		{
		    if( LEFT(otile) < pos->pt.p_x )
			pos->pt = otile->ti_ll;
		    else if( LEFT(otile) == pos->pt.p_x && 
				BOTTOM(otile) < pos->pt.p_y )
			pos->pt.p_y = BOTTOM(otile);
		}
	    }
	    else
	    {
		TxError("Error:  Asymmetric device with multiple terminals!\n");
	    }

	    /* Add the length to this terminal's perimeter */
	    extTransRec.tr_termlen[thisterm] += len;

	    /* Update the boundary traversal vector */
	    switch(bp->b_direction) {
		case BD_LEFT:
		    extTransRec.tr_termvector[thisterm].p_y += len;
		    break;
		case BD_TOP:
		    extTransRec.tr_termvector[thisterm].p_x += len;
		    break;
		case BD_RIGHT:
		    extTransRec.tr_termvector[thisterm].p_y -= len;
		    break;
		case BD_BOTTOM:
		    extTransRec.tr_termvector[thisterm].p_x -= len;
		    break;
	    }

	    /*
	     * Mark this attribute as belonging to this transistor
	     * if it is either:
	     *	(1) a terminal attribute whose LL corner touches bp->b_segment,
	     *   or	(2) a gate attribute that lies inside bp->b_inside.
	     */
	    for (ll = extTransRec.tr_gatenode->nreg_labels; ll; ll = ll->ll_next)
	    {
		/* Skip if already marked */
		if (ll->ll_attr != LL_NOATTR)
		    continue;
		lab = ll->ll_label;
		if (GEO_ENCLOSE(&lab->lab_rect.r_ll, &bp->b_segment)
			&& extLabType(lab->lab_text, LABTYPE_TERMATTR))
		{
		    ll->ll_attr = thisterm;
		}
	    }
	    SDterm = TRUE;
	    break;
	}
    }

    if (!SDterm && extConnectsTo(tinside, toutside, ExtCurStyle->exts_nodeConn))
    {
	/* Not in a terminal, but are in something that connects to gate */
	extTransRec.tr_gatelen += len;
    }

    /*
     * Total perimeter (separate from terminals, for dcaps
     * that might not be surrounded by terminals on all sides).
     */

    /* Don't double-count contact perimeters (added by Tim 1/9/07) */
    if ((!DBIsContact(toutside) && !DBIsContact(tinside)) ||
		(bp->b_plane == extTransRec.tr_gatenode->nreg_pnum))
	extTransRec.tr_perim += len;

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extAnnularTileFunc --
 *
 * Filter function called by ExtFindNeighbors for each tile in a
 * transistor.  Responsible for doing an extensive boundary
 * survey to determine the length of the transistor.
 * This is basically a subset of the code in extTransTileFunc()
 * but passes a different function to extEnumTilePerim().
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *
 * ----------------------------------------------------------------------------
 */

int
extAnnularTileFunc(tile, pNum)
    Tile *tile;
    int pNum;
{
    TileTypeBitMask mask;
    TileType loctype;

    /*
     * Visit each segment of the perimeter of this tile that
     * that borders on something of a different type.
     */
    if (IsSplit(tile))
    {
        loctype = (SplitSide(tile)) ? SplitRightType(tile): SplitLeftType(tile);
    }
    else
	loctype = TiGetTypeExact(tile);

    mask = ExtCurStyle->exts_transConn[loctype];
    TTMaskCom(&mask);
    extEnumTilePerim(tile, mask, pNum, extSpecialPerimFunc, (ClientData) TRUE);
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extResistorTileFunc --
 *
 * Filter function called by ExtFindNeighbors for each tile in a
 * resistor.  This is very similar to the extAnnularTileFunc
 * above, but it looks a boundaries with non-source/drain types
 * rather than the source/drain boundaries themselves.  This is
 * correct for tracing the detailed perimeter of a device where
 * L > W.
 *
 * Ideally, one wants to call both of these functions to check
 * both the case of L > W and the case W > L, assuming that both
 * are legal resistor layouts.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *
 * ----------------------------------------------------------------------------
 */

int
extResistorTileFunc(tile, pNum)
    Tile *tile;
    int pNum;
{
    TileTypeBitMask mask;
    TileType loctype;

    /*
     * Visit each segment of the perimeter of this tile that
     * that borders on something of a different type.
     */
    if (IsSplit(tile))
    {
        loctype = (SplitSide(tile)) ? SplitRightType(tile): SplitLeftType(tile);
    }
    else
	loctype = TiGetTypeExact(tile);

    mask = ExtCurStyle->exts_transConn[loctype];
    TTMaskSetMask(&mask, &ExtCurStyle->exts_transSDTypes[loctype][0]);
    TTMaskCom(&mask);

    extEnumTilePerim(tile, mask, pNum, extSpecialPerimFunc, (ClientData)FALSE);

    return (0);
}

/*----------------------------------------------------------------------*/
/* Detailed boundary survey for unusual transistor geometries (esp.	*/
/* annular).  If "sense" is TRUE, look at boundaries with source/drain	*/
/* types.  If "sense" is FALSE, looks at non-source/drain boundaries.	*/
/*----------------------------------------------------------------------*/

int
extSpecialPerimFunc(bp, sense)
    Boundary *bp;
    bool sense;
{
    TileType tinside, toutside;
    NodeRegion *diffNode = (NodeRegion *) extGetRegion(bp->b_outside);
    int thisterm, extended, i;
    LinkedBoundary *newBound, *lb, *lastlb;
    bool needSurvey;

    /* Note that extEnumTilePerim() assumes for the non-Manhattan case	*/
    /* that non-Manhattan tiles should be incorporated into the device	*/
    /* gate for purposes of computing effective length and width.  In	*/
    /* most cases this will be only a slight deviation from the true	*/
    /* result.								*/

    switch (bp->b_direction)
    {
	case BD_TOP:
	    tinside = TiGetTopType(bp->b_inside);
	    toutside = TiGetBottomType(bp->b_outside);
	    break;
	case BD_BOTTOM:
	    tinside = TiGetBottomType(bp->b_inside);
	    toutside = TiGetTopType(bp->b_outside);
	    break;
	case BD_RIGHT:
	    tinside = TiGetRightType(bp->b_inside);
	    toutside = TiGetLeftType(bp->b_outside);
	    break;
	case BD_LEFT:
	    tinside = TiGetLeftType(bp->b_inside);
	    toutside = TiGetRightType(bp->b_outside);
	    break;
    }

    /* Check all terminal classes for a matching type */
    needSurvey = FALSE;
    for (i = 0; !TTMaskIsZero(&ExtCurStyle->exts_transSDTypes[tinside][i]); i++)
    {
	if (TTMaskHasType(&ExtCurStyle->exts_transSDTypes[tinside][i], toutside))
	{
	    needSurvey = TRUE;
	    break;
	}
    }

    if (!sense || needSurvey)
    {
	if (toutside == TT_SPACE)
	    if (glob_subsnode != NULL)
		diffNode = glob_subsnode;

	/*
	 * Since we're repeating the search, all terminals should be there.
	 */
	if (!sense)
	    thisterm = 0;
	else
	{
	    for (thisterm = 0; thisterm < extTransRec.tr_nterm; thisterm++)
		if (extTransRec.tr_termnode[thisterm] == diffNode)
		    break;
	    if (thisterm >= extTransRec.tr_nterm)
	    {
		if (toutside == TT_SPACE)
		TxError("Internal Error in Transistor Perimeter Boundary Search!\n");
		return 1;
	    }
	}

	/*
	 * Check the existing segment list to see if this segment
	 * extends an existing segment.
	 */

	extended = 0;
	for (lb = extSpecialBounds[thisterm]; lb != NULL; lb = lb->b_next)
	{
	    if (bp->b_direction == lb->dir)
	    {
		switch(lb->dir)
		{
		    case BD_LEFT:
		    case BD_RIGHT:
			if (bp->b_segment.r_xbot == lb->r.r_xbot)
			{
			    if (bp->b_segment.r_ybot == lb->r.r_ytop)
			    {
				if (extended)
				    lastlb->r.r_ybot = lb->r.r_ybot;
				else
				    lb->r.r_ytop = bp->b_segment.r_ytop;
				extended++;
				lastlb = lb;
			    }
			    else if (bp->b_segment.r_ytop == lb->r.r_ybot)
			    {
				if (extended)
				    lastlb->r.r_ytop = lb->r.r_ytop;
				else
				    lb->r.r_ybot = bp->b_segment.r_ybot;
				extended++;
				lastlb = lb;
			    }
			}
			break;
		    case BD_TOP:
		    case BD_BOTTOM:
			if (bp->b_segment.r_ybot == lb->r.r_ybot)
			{
			    if (bp->b_segment.r_xbot == lb->r.r_xtop)
			    {
				if (extended)
				    lastlb->r.r_xbot = lb->r.r_xbot;
				else
				    lb->r.r_xtop = bp->b_segment.r_xtop;
				extended++;
				lastlb = lb;
			    }
			    else if (bp->b_segment.r_xtop == lb->r.r_xbot)
			    {
				if (extended)
				    lastlb->r.r_xtop = lb->r.r_xtop;
				else
				    lb->r.r_xbot = bp->b_segment.r_xbot;
				extended++;
				lastlb = lb;
			    }
			}
			break;
		}
	    }
	    if (extended == 2)
	    {
		/* Connected two existing entries---need to remove lastlb, */
		/* which is now a redundant segment.			   */

		if (lastlb == extSpecialBounds[thisterm])
		    extSpecialBounds[thisterm] = lastlb->b_next;
		else
		{
		    for (lb = extSpecialBounds[thisterm]; lb != NULL;
				lb = lb->b_next)
		    {
			if (lastlb == lb->b_next)
			{
			    lb->b_next = lastlb->b_next;
			    break;
			}   
		    }
		}
		freeMagic((char *)lastlb);
	
		/* New segment cannot extend more than two existing segments */
		break;
	    }
	}

	if (!extended)
	{
	    newBound = (LinkedBoundary *)mallocMagic(sizeof(LinkedBoundary));
	    newBound->r = bp->b_segment;
	    newBound->dir = bp->b_direction;
	    newBound->b_next = extSpecialBounds[thisterm];
	    extSpecialBounds[thisterm] = newBound;
	}
    }
    return (0);
}


/*
 * ----------------------------------------------------------------------------
 *
 * extTransOutTerminal --
 *
 * Output the information associated with one terminal of a
 * transistor.  This consists of three things:
 *	- the name of the node to which the terminal is connected
 *	- the length of the terminal along the perimeter of the transistor
 *	- a list of attributes pertinent to this terminal.
 *
 * If 'whichTerm' is LL_GATEATTR, this is the gate; otherwise, it is one
 * of the diffusion terminals.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes the above information to 'outFile'.
 *	Resets ll_attr for each attribute we output to LL_NOATTR.
 *
 * ----------------------------------------------------------------------------
 */

void
extTransOutTerminal(lreg, ll, whichTerm, len, outFile)
    LabRegion *lreg;		/* Node connected to terminal */
    LabelList *ll;	/* Gate's label list */
    int whichTerm;		/* Which terminal we are processing.  The gate
				 * is indicated by LL_GATEATTR.
				 */
    int len;			/* Length of perimeter along terminal */
    FILE *outFile;		/* Output file */
{
    char *cp;
    int n;
    char fmt;


    fprintf(outFile, " \"%s\" %d", extNodeName(lreg), len);
    for (fmt = ' '; ll; ll = ll->ll_next) 
	if (ll->ll_attr == whichTerm)
	{
	    fprintf(outFile, "%c\"", fmt);
	    cp = ll->ll_label->lab_text;
	    n = strlen(cp) - 1;
	    while (n-- > 0)
		putc(*cp++, outFile);
	    ll->ll_attr = LL_NOATTR;
	    fprintf(outFile, "\"");
	    fmt = ',';
	}

    if (fmt == ' ')
	fprintf(outFile, " 0");
}

/*
 * ----------------------------------------------------------------------------
 *
 * extTransBad --
 *
 * For a transistor where an error was encountered, give feedback
 * as to the location of the error.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Complains to the user.
 *
 * ----------------------------------------------------------------------------
 */

void
extTransBad(def, tp, mesg)
    CellDef *def;
    Tile *tp;
    char *mesg;
{
    Rect r;

    if (!DebugIsSet(extDebugID, extDebNoFeedback))
    {
	TiToRect(tp, &r);
	DBWFeedbackAdd(&r, mesg, def, 1, STYLE_PALEHIGHLIGHTS);
    }
    extNumWarnings++;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extLabType --
 *
 * Check to see whether the text passed as an argument satisfies
 * any of the label types in 'typeMask'.
 *
 * Results:
 *	TRUE if the text is of one of the label types in 'typeMask',
 *	FALSE if not.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
extLabType(text, typeMask)
    char *text;
    int typeMask;
{
    if (*text == '\0')
	return (FALSE);

    while (*text) text++;
    switch (*--text)
    {
	case '@':	/* Node attribute */
	    return ((bool)(typeMask & LABTYPE_NODEATTR));
	case '$':	/* Terminal (source/drain) attribute */
	    return ((bool)(typeMask & LABTYPE_TERMATTR));
	case '^':	/* Gate attribute */
	    return ((bool)(typeMask & LABTYPE_GATEATTR));
	default:
	    return ((bool)(typeMask & LABTYPE_NAME));
    }
    /*NOTREACHED*/
}


/*
 * ----------------------------------------------------------------------------
 * extNodeToTile --
 *
 *	Sets tp to be the tile containing the lower-leftmost point of the
 *	NodeRegion *np, but in the tile planes of the ExtTree *et instead
 *	of the tile planes originally containing *np.  This routine used
 *	to be defined as the macro NODETOTILE().
 *
 * Results:
 *	Returns a pointer to the tile
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

Tile *extNodeToTile(np, et)
    NodeRegion *np;
    ExtTree *et;
{
    Tile *tp;
    Plane *myplane;

    myplane = et->et_use->cu_def->cd_planes[np->nreg_pnum];

    tp = myplane->pl_hint;
    GOTOPOINT(tp, &np->nreg_ll);
    myplane->pl_hint = tp;

    if (IsSplit(tp))
    {
	TileType tpt = TiGetTypeExact(tp);
	if ((tpt & TT_LEFTMASK) == (np->nreg_type & TT_LEFTMASK))
	    TiSetBody(tp, tpt & ~TT_SIDE);
	else
	    TiSetBody(tp, tpt | TT_SIDE);
    }

    return tp;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extSetNodeNum --
 *
 * Update reg->lreg_ll and reg->lreg_pnum so that they are always the
 * lowest leftmost coordinate in a cell, on the plane with the lowest
 * number (formerly a macro in extractInt.h).
 *
 * (10/1/05:  Changed from a macro to a subroutine and modified for
 * handling non-Manhattan geometry)
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
extSetNodeNum(reg, plane, tile)
    LabRegion *reg;
    int plane;
    Tile *tile;
{
    TileType type;

    if (IsSplit(tile))
    {
	/* Only consider split tiles if the lower-left-hand corner      */
	/* is only the type under consideration.                        */

	if (!SplitSide(tile) && SplitDirection(tile))
	    type = SplitSide(tile) ? SplitRightType(tile) : SplitLeftType(tile);
	else if (reg->lreg_pnum == DBNumPlanes)
	    type = TiGetTypeExact(tile);
	else
	    return;
    }
    else
	type = TiGetType(tile);

    if ((plane < reg->lreg_pnum) || (reg->lreg_type & TT_DIAGONAL))
    {
	reg->lreg_type = type;
	reg->lreg_pnum = plane;
	reg->lreg_ll = tile->ti_ll;
    }
    else if (plane == reg->lreg_pnum)
    {
	if (LEFT(tile) < reg->lreg_ll.p_x)
	{
	    reg->lreg_ll = tile->ti_ll;
	    reg->lreg_type = type;
	}
	else if (LEFT(tile) == reg->lreg_ll.p_x
			&& BOTTOM(tile) < reg->lreg_ll.p_y)
	{
	    reg->lreg_ll.p_y = BOTTOM(tile);
	    reg->lreg_type = type;
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * extTransFirst --
 * extTransEach --
 *
 * Filter functions passed to ExtFindRegions when tracing out transistor
 * regions as part of flat circuit extraction.
 *
 * Results:
 *	extTransFirst returns a pointer to a new TransRegion.
 *	extTransEach returns NULL.
 *
 * Side effects:
 *	Memory is allocated by extTransFirst.
 *	We cons the newly allocated region onto the front of the existing
 *	region list.
 *
 *	The area of each transistor is updated by extTransEach.
 *
 * ----------------------------------------------------------------------------
 */

Region *
extTransFirst(tile, arg)
    Tile *tile;
    FindRegion *arg;
{
    TransRegion *reg;

    reg = (TransRegion *) mallocMagic((unsigned) (sizeof (TransRegion)));
    reg->treg_next = (TransRegion *) NULL;
    reg->treg_labels = (LabelList *) NULL;
    reg->treg_area = 0;
    reg->treg_tile = tile;
    reg->treg_pnum = DBNumPlanes;

    if (IsSplit(tile))
	reg->treg_type = SplitSide(tile) ? SplitRightType(tile) : SplitLeftType(tile);
    else
	reg->treg_type = TiGetTypeExact(tile);

    /* Prepend it to the region list */
    reg->treg_next = (TransRegion *) arg->fra_region;
    arg->fra_region = (Region *) reg;
    return ((Region *) reg);
}

    /*ARGSUSED*/
int
extTransEach(tile, pNum, arg)
    Tile *tile;
    int pNum;
    FindRegion *arg;
{
    TransRegion *reg = (TransRegion *) arg->fra_region;
    int area = TILEAREA(tile);

    if (IsSplit(tile)) area /= 2;	/* Split tiles are 1/2 area! */
    else if (IsSplit(reg->treg_tile))
    {
	/* Avoid setting the region's tile pointer to a split tile */
	reg->treg_tile = tile;
	reg->treg_type = TiGetTypeExact(tile);
    }

    /* The following is non-ideal.  It assumes that the lowest plane of	*/
    /* types connected to a device is the plane of the device itself.	*/
    /* Otherwise, the area of the device will be miscalculated.		*/
    
    if (pNum < reg->treg_pnum) reg->treg_area = 0;

    extSetNodeNum((LabRegion *) reg, pNum, tile);

    if (pNum == reg->treg_pnum) reg->treg_area += area;

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extFindNodes --
 *
 * Build up, in the manner of ExtFindRegions, a list of all the
 * node regions in the CellDef 'def'.  This procedure is heavily
 * optimized for speed.
 *
 * Results:
 *	Returns a pointer to a NULL-terminated list of NodeRegions
 *	that correspond to the nodes in the circuit.  The label lists
 *	for each node region have not yet been filled in.
 *
 * Side effects:
 *	Memory is allocated.
 *
 * ----------------------------------------------------------------------------
 */

Stack *extNodeStack = NULL;
Rect *extNodeClipArea = NULL;

NodeRegion *
extFindNodes(def, clipArea, subonly)
    CellDef *def;	/* Def whose nodes are being found */
    Rect *clipArea;	/* If non-NULL, ignore perimeter and area that extend
			 * outside this rectangle.
			 */
    bool subonly;	/* If true, only find the substrate node, and return */
{
    int extNodeAreaFunc();
    int extSubsFunc();
    FindRegion arg;
    int pNum, n;
    TileTypeBitMask subsTypesNonSpace;

    /* Reset perimeter and area prior to node extraction */
    for (n = 0; n < ExtCurStyle->exts_numResistClasses; n++)
	extResistArea[n] = extResistPerim[n] = 0;

    extNodeClipArea = clipArea;
    if (extNodeStack == (Stack *) NULL)
	extNodeStack = StackNew(64);

    arg.fra_def = def;
    arg.fra_region = (Region *) NULL;

    SigDisableInterrupts();

    /* First pass:  Find substrate.  Collect all tiles belonging */
    /* to the substrate and push them onto the stack.  Then	 */
    /* call extNodeAreaFunc() on the first of these to generate	 */
    /* a single substrate node.					 */

    temp_subsnode = (NodeRegion *)NULL;		// Reset for new search

    TTMaskZero(&subsTypesNonSpace);
    TTMaskSetMask(&subsTypesNonSpace, &ExtCurStyle->exts_globSubstrateTypes);
    TTMaskClearType(&subsTypesNonSpace, TT_SPACE);

    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
    {
	/* Does the type set of this plane intersect the substrate types? */
	if (TTMaskIntersect(&DBPlaneTypes[pNum], &subsTypesNonSpace))
	{
	    arg.fra_pNum = pNum;
	    DBSrPaintClient((Tile *) NULL, def->cd_planes[pNum],
			&TiPlaneRect, &subsTypesNonSpace, extUnInit,
			extSubsFunc, (ClientData) &arg);
	}
    }

    /* If there was a substrate connection, process it and everything	*/
    /* that was connected to it.  If not, then create a new node	*/
    /* to represent the substrate.					*/

    if (!StackEmpty(extNodeStack))
    {
	Tile *tile;
	int tilePlaneNum;

	POPTILE(tile, tilePlaneNum);
	arg.fra_pNum = tilePlaneNum;
	extNodeAreaFunc(tile, &arg);
	temp_subsnode = (NodeRegion *)arg.fra_region;
    }
    else if (ExtCurStyle->exts_globSubstratePlane != -1)
    {
	NodeRegion *loc_subsnode;

	extNodeAreaFunc((Tile *)NULL, (FindRegion *)&arg);
	loc_subsnode = (NodeRegion *)arg.fra_region;
	loc_subsnode->nreg_pnum = ExtCurStyle->exts_globSubstratePlane;
	loc_subsnode->nreg_type = TT_SPACE;
	loc_subsnode->nreg_ll.p_x = MINFINITY + 3;
	loc_subsnode->nreg_ll.p_y = MINFINITY + 3;
	loc_subsnode->nreg_labels = NULL;
	temp_subsnode = loc_subsnode;
    }
    if (subonly == TRUE) return ((NodeRegion *) arg.fra_region);

    /* Second pass:  Find all other nodes */

    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
    {
	arg.fra_pNum = pNum;
	(void) DBSrPaintClient((Tile *) NULL, def->cd_planes[pNum],
		    &TiPlaneRect, &ExtCurStyle->exts_activeTypes,
		    extUnInit, extNodeAreaFunc, (ClientData) &arg);
    }
    SigEnableInterrupts();

    /* Compute resistance for last node */
    if (arg.fra_region && (ExtOptions & EXT_DORESISTANCE))
	extSetResist((NodeRegion *) arg.fra_region);

    return ((NodeRegion *) arg.fra_region);
}

int
extSubsFunc(tile, arg)
    Tile *tile;
    FindRegion *arg;
{
    TileType type;

    if (IsSplit(tile))
    {
	type = (SplitSide(tile)) ? SplitRightType(tile) : SplitLeftType(tile);
	if (type == TT_SPACE) return 0;		/* Should not happen */
    }

    /* Mark this tile as pending and push it */
    PUSHTILE(tile, arg->fra_pNum);

    /* That's all we do */
    return (0);
}


int
extNodeAreaFunc(tile, arg)
    Tile *tile;
    FindRegion *arg;
{
    int tilePlaneNum, pNum, len, area, resistClass, n, nclasses;
    PlaneMask pMask;
    CapValue capval;
    TileTypeBitMask *mask, *resMask;
    NodeRegion *reg;
    Tile *tp;
    TileType type, t, residue, tres;
    NodeRegion *old;
    Rect r;
    PlaneAndArea pla;

    if (tile && IsSplit(tile))
    {
	type = (SplitSide(tile)) ? SplitRightType(tile) : SplitLeftType(tile);
	if (type == TT_SPACE) return 0;		/* Should not happen */
    }

    /* Compute the resistance for the previous region */
    if (old = (NodeRegion *) arg->fra_region)
	if (ExtOptions & EXT_DORESISTANCE)
	    extSetResist(old);

    /* Allocate a new node */
    nclasses = ExtCurStyle->exts_numResistClasses;
    n = sizeof (NodeRegion) + (sizeof (PerimArea) * (nclasses - 1));
    reg = (NodeRegion *) mallocMagic((unsigned) n);
    reg->nreg_labels = (LabelList *) NULL;
    reg->nreg_cap = (CapValue) 0;
    reg->nreg_resist = 0;
    reg->nreg_pnum = DBNumPlanes;
    reg->nreg_next = (NodeRegion *) NULL;
    for (n = 0; n < nclasses; n++)
	reg->nreg_pa[n].pa_perim = reg->nreg_pa[n].pa_area = 0;

    /* Prepend the new node to the region list */
    reg->nreg_next = (NodeRegion *) arg->fra_region;
    arg->fra_region = (Region *) reg;

    /* Used by substrate generating routine */
    if (tile == NULL) return 1;

    /* Mark this tile as pending and push it */
    PUSHTILE(tile, arg->fra_pNum);

    /* Continue processing tiles until there are none left */
    while (!StackEmpty(extNodeStack))
    {
	POPTILE(tile, tilePlaneNum);

	/*
	 * Since tile was pushed on the stack, we know that it
	 * belongs to this region.  Check to see that it hasn't
	 * been visited in the meantime.  If it's still unvisited,
	 * visit it and process its neighbors.
	 */
	if (tile->ti_client == (ClientData) reg)
	    continue;
	tile->ti_client = (ClientData) reg;
	if (DebugIsSet(extDebugID, extDebNeighbor))
	    extShowTile(tile, "neighbor", 1);

        if (IsSplit(tile))
        {           
            type = (SplitSide(tile)) ? SplitRightType(tile):
                        SplitLeftType(tile);
        }           
        else        
            type = TiGetTypeExact(tile);

	/* Contacts are replaced by their residues when calculating */
	/* area/perimeter capacitance and resistance.		    */

	residue = (DBIsContact(type)) ?
		DBPlaneToResidue(type, tilePlaneNum) : type;

	mask = &ExtCurStyle->exts_nodeConn[type];
	resMask = &ExtCurStyle->exts_typesResistChanged[residue];
	resistClass = ExtCurStyle->exts_typeToResistClass[residue];

	/*
	 * Make sure the lower-leftmost point in the node is
	 * kept up to date, so we can generate an internal
	 * node name that does not depend on any other nodes
	 * in this cell.
	 */
	extSetNodeNum((LabRegion *) reg, tilePlaneNum, tile);

	/*
	 * Keep track of the total area of this node, and the
	 * contribution to parasitic ground capacitance resulting
	 * from area.
	 */
	if (extNodeClipArea)
	{
	    TITORECT(tile, &r);
	    GEOCLIP(&r, extNodeClipArea);
	    area = (r.r_xtop - r.r_xbot) * (r.r_ytop - r.r_ybot);
	}
	else area = TILEAREA(tile);

	if (IsSplit(tile)) area /= 2;	/* Split tiles are 1/2 area! */

	if (resistClass != -1)
	    extResistArea[resistClass] += area;
	reg->nreg_cap += area * ExtCurStyle->exts_areaCap[residue];

	/* Compute perimeter of nonManhattan edges */
	if (IsSplit(tile))
	{
	    len = ((RIGHT(tile) - LEFT(tile)) * (RIGHT(tile) - LEFT(tile))) +
		 	((TOP(tile) - BOTTOM(tile)) * (TOP(tile) - BOTTOM(tile)));
	    len = (int)sqrt((double)len);

	    if (extNodeClipArea)
	    {
		/* To-do:  Find perimeter length of clipped edge */
	    }

	    /* Find the type on the other side of the tile */
            t = (SplitSide(tile)) ? SplitLeftType(tile):
                        SplitRightType(tile);
	    tres = (DBIsContact(t)) ? DBPlaneToResidue(t, tilePlaneNum) : t;
	    if ((capval = ExtCurStyle->exts_perimCap[residue][tres]) != (CapValue) 0)
		reg->nreg_cap += capval * len;
	    if (TTMaskHasType(resMask, tres) && resistClass != -1)
		extResistPerim[resistClass] += len;
	}

	/*
	 * Walk along all four sides of tile.
	 * Sum perimeter capacitance as we go.
	 * Keep track of the contribution to the total perimeter
	 * of this node, for computing resistance.
	 */

	/* Top */
topside:

	if (IsSplit(tile) && (SplitSide(tile) ^ SplitDirection(tile))) goto leftside;

	for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp = BL(tp))
	{
	    if (extNodeClipArea)
	    {
		r.r_ybot = r.r_ytop = TOP(tile);
		r.r_xtop = MIN(RIGHT(tile), RIGHT(tp));
		r.r_xbot = MAX(LEFT(tile), LEFT(tp));
		GEOCLIP(&r, extNodeClipArea);
		len = EDGENULL(&r) ? 0 : r.r_xtop - r.r_xbot;
	    }
	    else len = MIN(RIGHT(tile), RIGHT(tp)) - MAX(LEFT(tile), LEFT(tp));
            if (IsSplit(tp))
	    {
        	t = SplitBottomType(tp);
		if (tp->ti_client == extUnInit && TTMaskHasType(mask, t))
		{
		    PUSHTILEBOTTOM(tp, tilePlaneNum);
		}
		else if (tp->ti_client != (ClientData)reg && TTMaskHasType(mask, t))
		{
		    /* Count split tile twice, once for each node it belongs to. */
		    tp->ti_client = extUnInit;
		    PUSHTILEBOTTOM(tp, tilePlaneNum);
		}
	    }
            else
	    {
		t = TiGetTypeExact(tp);
		if (tp->ti_client == extUnInit && TTMaskHasType(mask, t))
		{
		    PUSHTILE(tp, tilePlaneNum);
		}
	    }
	    tres = (DBIsContact(t)) ? DBPlaneToResidue(t, tilePlaneNum) : t;
	    if ((capval = ExtCurStyle->exts_perimCap[residue][tres]) != (CapValue) 0)
		reg->nreg_cap += capval * len;
	    if (TTMaskHasType(resMask, tres) && resistClass != -1)
		extResistPerim[resistClass] += len;
	}

	/* Left */
leftside:

	if (IsSplit(tile) && SplitSide(tile)) goto bottomside;

	for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
	{
	    if (extNodeClipArea)
	    {
		r.r_xbot = r.r_xtop = LEFT(tile);
		r.r_ytop = MIN(TOP(tile), TOP(tp));
		r.r_ybot = MAX(BOTTOM(tile), BOTTOM(tp));
		GEOCLIP(&r, extNodeClipArea);
		len = EDGENULL(&r) ? 0 : r.r_ytop - r.r_ybot;
	    }
	    else len = MIN(TOP(tile), TOP(tp)) - MAX(BOTTOM(tile), BOTTOM(tp));
            if (IsSplit(tp))
	    {
                t = SplitRightType(tp);
		if (tp->ti_client == extUnInit && TTMaskHasType(mask, t))
		{
		    PUSHTILERIGHT(tp, tilePlaneNum);
		}
		else if (tp->ti_client != (ClientData)reg && TTMaskHasType(mask, t))
		{
		    /* Count split tile twice, once for each node it belongs to. */
		    tp->ti_client = extUnInit;
		    PUSHTILERIGHT(tp, tilePlaneNum);
		}
	    }
            else
	    {
		t = TiGetTypeExact(tp);
		if (tp->ti_client == extUnInit && TTMaskHasType(mask, t))
		{
		    PUSHTILE(tp, tilePlaneNum);
		}
	    }
	    tres = (DBIsContact(t)) ? DBPlaneToResidue(t, tilePlaneNum) : t;
	    if ((capval = ExtCurStyle->exts_perimCap[residue][tres]) != (CapValue) 0)
		reg->nreg_cap += capval * len;
	    if (TTMaskHasType(resMask, tres) && resistClass != -1)
		extResistPerim[resistClass] += len;
	}

	/* Bottom */
bottomside:

	if (IsSplit(tile) && (!(SplitSide(tile) ^ SplitDirection(tile))))
	    goto rightside;

	for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp = TR(tp))
	{
	    if (extNodeClipArea)
	    {
		r.r_ybot = r.r_ytop = BOTTOM(tile);
		r.r_xtop = MIN(RIGHT(tile), RIGHT(tp));
		r.r_xbot = MAX(LEFT(tile), LEFT(tp));
		GEOCLIP(&r, extNodeClipArea);
		len = EDGENULL(&r) ? 0 : r.r_xtop - r.r_xbot;
	    }
	    else len = MIN(RIGHT(tile), RIGHT(tp)) - MAX(LEFT(tile), LEFT(tp));
            if (IsSplit(tp))
	    {
                t = SplitTopType(tp);
		if (tp->ti_client == extUnInit && TTMaskHasType(mask, t))
		{
		    PUSHTILETOP(tp, tilePlaneNum);
		}
		else if (tp->ti_client != (ClientData)reg && TTMaskHasType(mask, t))
		{
		    /* Count split tile twice, once for each node it belongs to. */
		    tp->ti_client = extUnInit;
		    PUSHTILETOP(tp, tilePlaneNum);
		}
	    }
            else
	    {
		t = TiGetTypeExact(tp);
		if (tp->ti_client == extUnInit && TTMaskHasType(mask, t))
		{
		    PUSHTILE(tp, tilePlaneNum);
		}
	    }
	    tres = (DBIsContact(t)) ? DBPlaneToResidue(t, tilePlaneNum) : t;
	    if ((capval = ExtCurStyle->exts_perimCap[residue][tres]) != (CapValue) 0)
		reg->nreg_cap += capval * len;
	    if (TTMaskHasType(resMask, tres) && resistClass != -1)
		extResistPerim[resistClass] += len;
	}

	/* Right */
rightside:

	if (IsSplit(tile) && !SplitSide(tile)) goto donesides;

	for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp = LB(tp))
	{
	    if (extNodeClipArea)
	    {
		r.r_xbot = r.r_xtop = RIGHT(tile);
		r.r_ytop = MIN(TOP(tile), TOP(tp));
		r.r_ybot = MAX(BOTTOM(tile), BOTTOM(tp));
		GEOCLIP(&r, extNodeClipArea);
		len = EDGENULL(&r) ? 0 : r.r_ytop - r.r_ybot;
	    }
	    else len = MIN(TOP(tile), TOP(tp)) - MAX(BOTTOM(tile), BOTTOM(tp));
            if (IsSplit(tp))
	    {
                t = SplitLeftType(tp);
		if (tp->ti_client == extUnInit && TTMaskHasType(mask, t))
		{
		    PUSHTILELEFT(tp, tilePlaneNum);
		}
		else if (tp->ti_client != (ClientData)reg && TTMaskHasType(mask, t))
		{
		    /* Count split tile twice, once for each node it belongs to	*/
		    tp->ti_client = extUnInit;
		    PUSHTILELEFT(tp, tilePlaneNum);
		}
	    }
            else
	    {
		t = TiGetTypeExact(tp);
		if (tp->ti_client == extUnInit && TTMaskHasType(mask, t))
		{
		    PUSHTILE(tp, tilePlaneNum);
		}
	    }
	    tres = (DBIsContact(t)) ? DBPlaneToResidue(t, tilePlaneNum) : t;
	    if ((capval = ExtCurStyle->exts_perimCap[residue][tres]) != (CapValue) 0)
		reg->nreg_cap += capval * len;
	    if (TTMaskHasType(resMask, tres) && resistClass != -1)
		extResistPerim[resistClass] += len;
	}

donesides:
	/* No capacitance */
	if ((ExtOptions & EXT_DOCAPACITANCE) == 0)
	    reg->nreg_cap = (CapValue) 0;

	/* If this is a contact, visit all the other planes */
	if (DBIsContact(type))
	{
	    pMask = DBConnPlanes[type];
	    pMask &= ~(PlaneNumToMaskBit(tilePlaneNum));
	    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
		if (PlaneMaskHasPlane(pMask, pNum))
		{
		    Plane *plane = arg->fra_def->cd_planes[pNum];

		    tp = plane->pl_hint;
		    GOTOPOINT(tp, &tile->ti_ll);
		    plane->pl_hint = tp;

		    if (tp->ti_client != extUnInit) continue;

		    /* tp and tile should have the same geometry for a contact */
		    if (IsSplit(tile) && IsSplit(tp))
		    {           
			if (SplitSide(tile))
			{
			    t = SplitRightType(tp);
			    if (TTMaskHasType(mask, t))
			    {
				PUSHTILERIGHT(tp, pNum);
			    }
			}
			else
			{
			    t = SplitLeftType(tp);
			    if (TTMaskHasType(mask, t))
			    {
				PUSHTILELEFT(tp, pNum);
			    }
			}
		    }           
		    else if (IsSplit(tp))
		    {
			/* Need to test both sides of the tile */
			t = SplitRightType(tp);
			if (TTMaskHasType(mask, t))
			{
			    PUSHTILERIGHT(tp, pNum);
			}
			t = SplitLeftType(tp);
			if (TTMaskHasType(mask, t))
			{
			    PUSHTILELEFT(tp, pNum);
			}
		    }
		    else
		    {
			t = TiGetTypeExact(tp);
			if (TTMaskHasType(mask, t))
			{
			    PUSHTILE(tp, pNum);
			}
		    }
		}
	}

	/*
	 * The hairiest case is when this type connects to stuff on
	 * other planes, but isn't itself connected as a contact.
	 * For example, a CMOS pwell connects to diffusion of the
	 * same doping (p substrate diff).  In a case like this,
	 * we need to search the entire AREA of the tile plus a
	 * 1-lambda halo to find everything it overlaps or touches
	 * on the other plane.
	 */
	if (pMask = DBAllConnPlanes[type])
	{
	    Rect biggerArea;
	    bool is_split = IsSplit(tile);

	    extNbrUn = extUnInit;
	    TITORECT(tile, &pla.area);
	    GEO_EXPAND(&pla.area, 1, &biggerArea);
	    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
		if ((pNum != tilePlaneNum) && PlaneMaskHasPlane(pMask, pNum))
		{
		    pla.plane = pNum;
		    if (is_split)
		        DBSrPaintNMArea((Tile *) NULL,
				arg->fra_def->cd_planes[pNum],
				TiGetTypeExact(tile) &
				(TT_DIAGONAL | TT_SIDE | TT_DIRECTION),
				&biggerArea, mask, extNbrPushFunc,
				(ClientData) &pla);
		    else
		        DBSrPaintArea((Tile *) NULL,
				arg->fra_def->cd_planes[pNum], &biggerArea,
				mask, extNbrPushFunc, (ClientData) &pla);
		}
	}
    }

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extGetCapValue --
 * extSetCapValue --
 *
 * Procedures to get/set a value from our capacitance tables.
 *
 * ----------------------------------------------------------------------------
 */

void 
extSetCapValue(he, value)
    HashEntry *he;
    CapValue value;
{
    if (HashGetValue(he) == NULL)
	HashSetValue(he, (CapValue *) mallocMagic(sizeof(CapValue)));
    *( (CapValue *) HashGetValue(he)) = value;
}

CapValue 
extGetCapValue(he)
    HashEntry *he;
{
    if (HashGetValue(he) == NULL)
	extSetCapValue(he, (CapValue) 0);
    return *( (CapValue *) HashGetValue(he));
}

/*
 * ----------------------------------------------------------------------------
 *
 * extCapHashKill --
 *
 * Kill off a coupling capacitance hash table.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees up storage in the table.
 * ----------------------------------------------------------------------------
 */

void
extCapHashKill(ht)
    HashTable *ht;
{
    HashSearch hs;
    HashEntry *he;

    HashStartSearch(&hs);
    while (he = HashNext(ht, &hs))
    {
	if (HashGetValue(he) != NULL) 
	{
	    freeMagic(HashGetValue(he));  /* Free a malloc'ed CapValue */
	    HashSetValue(he, (ClientData) NULL);
	}
    }
    HashKill(ht);
}
