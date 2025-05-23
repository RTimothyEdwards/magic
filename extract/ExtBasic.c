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
#include <ctype.h>

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
const char * const extDevTable[] = {"fet", "mosfet", "asymmetric", "bjt", "devres",
	"devcap", "devcaprev", "vsource", "diode", "pdiode", "ndiode",
	"subckt", "rsubckt", "msubckt", "csubckt", NULL};
#endif

/* --------------------- Data local to this file ---------------------- */

    /*
     * The following are used to accumulate perimeter and area
     * on each layer when building up the node list.  They are
     * used to compute the resistance of each node.  Each is
     * indexed by sheet resistivity class.
     */
int extResistPerim[NT];
dlong extResistArea[NT];

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

/* Field definitions for tr_devmatch */
#define MATCH_ID    0x01		/* Device matches identifier in devrec */
#define MATCH_PARAM 0x02		/* Device is compatible with parameter range */
#define MATCH_SUB   0x04		/* Device matches substrate type in devrec */
#define MATCH_TERM  0x08		/* Device matches terminal in devrec */
/* (additional fields: bit shifts up by 1 for each defined device terminal) */

struct transRec
{
    ExtDevice   *tr_devrec;		/* Device record in ExtCurStyle */
    int		 tr_devmatch;		/* Fields of tr_devrec that match device */
    int		 tr_nterm;		/* Number of terminals */
    int		 tr_gatelen;		/* Perimeter of connection to gate */
    NodeRegion	*tr_gatenode;		/* Node region for gate terminal */
    NodeRegion	*tr_termnode[MAXSD];	/* Node region for each diff terminal */
    NodeRegion  *tr_subsnode;		/* Substrate node */
    int		 tr_termlen[MAXSD];	/* Length of each diff terminal edge,
					 * used for computing L/W for the fet.
					 */
    int		 tr_termarea[MAXSD];	/* Total area of the terminal */
    int		 tr_termperim[MAXSD];	/* Total perimeter of the terminal */
    int		 tr_termshared[MAXSD];	/* Number of devices sharing this terminal */
    Point	 tr_termvector[MAXSD];	/* Perimeter traversal vector, used to
					 * find and calculate correct parameters
					 * for annular (ring) devices and other
					 * non-rectangular geometries.
					 */
    int		 tr_perim;		/* Total perimeter */
    int		 tr_plane;		/* Plane of device */
    TermTilePos  tr_termpos[MAXSD];	/* lowest tile connecting to term */
} extTransRec;

typedef struct LB1
{
    Rect r;	/* Boundary segment */
    int dir;	/* Direction of travel */
    struct LB1 *b_next;
} LinkedBoundary;

LinkedBoundary **extSpecialBounds;	/* Linked Boundary List */

typedef struct LT1
{
    Tile *t;
    struct LT1 *t_next;
} LinkedTile;

LinkedTile *extSpecialDevice;		/* Linked tile list */

NodeRegion *glob_subsnode = NULL;	/* Global substrate node */
NodeRegion *temp_subsnode = NULL;	/* Last subsnode found */

#define	EDGENULL(r)	((r)->r_xbot > (r)->r_xtop || (r)->r_ybot > (r)->r_ytop)

/* Forward declarations */
void extOutputNodes();
int extTransTileFunc();
int extTransPerimFunc();
int extTransFindSubs();
int extTransFindId();

int extAnnularTileFunc();
int extResistorTileFunc();
int extSpecialPerimFunc();

void extFindDuplicateLabels();
void extOutputDevices();
void extOutputParameters();
void extTransOutTerminal();
void extTransBad();

ExtDevice *extDevFindMatch();

bool extLabType();

/* Function returns 1 if a tile is found by DBTreeSrTiles()	*/
/* that is not in the topmost def of the search.		*/

int
extFoundFunc(tile, cxp)
    Tile *tile;
    TreeContext *cxp;
{
    CellDef *def = (CellDef *)cxp->tc_filter->tf_arg;
    return (def == cxp->tc_scx->scx_use->cu_def) ? 0 : 1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extBasic --
 *
 * Extract a single CellDef, and output the result to the
 * file 'outFile'.
 *
 * Results:
 *	Returns a list of ExtRegion structs that comprise all
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
 * Interruptible in a limited sense.  We will still return an
 * ExtRegion list, but labels may not have been assigned, and
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
    TransRegion *transList, *reg;
    HashTable extCoupleHash;
    char *propptr;
    bool propfound = FALSE;
    bool isabstract = FALSE;

    glob_subsnode = (NodeRegion *)NULL;

    /*
     * Build up a list of the device regions for extOutputDevices()
     * below.  We're only interested in pointers from each region to
     * a tile in that region, not the back pointers from the tiles to
     * the regions.
     */
    transList = (TransRegion *) ExtFindRegions(def, &TiPlaneRect,
				    &ExtCurStyle->exts_deviceMask,
				    ExtCurStyle->exts_deviceConn,
				    extUnInit, extTransFirst, extTransEach);
    ExtResetTiles(def, extUnInit);

    for (reg = transList; reg && !SigInterruptPending; reg = reg->treg_next)
    {
	/* For each transistor region, check if there is an equivalent	*/
	/* region at the same location in a subcell.  The device in the	*/
	/* subcell is given priority.  This avoids duplicating devices	*/
	/* when, for example, a device contact is placed in another	*/
	/* cell, which can happen for devices like capacitors and	*/
	/* diodes, where the device identifier layer may include	*/
	/* a contact type.  NOTE:  This routine needs to limit the	*/
	/* search to devices in the same plane as the transistor under	*/
	/* consideration.						*/

	SearchContext scontext;
	CellUse	      dummy;
	int	      extFoundFunc();
	TileTypeBitMask transPlaneMask;

	scontext.scx_use = &dummy;
	dummy.cu_def = def;
	dummy.cu_id = NULL;
	scontext.scx_trans = GeoIdentityTransform;
	scontext.scx_area.r_ll = scontext.scx_area.r_ur = reg->treg_tile->ti_ll;
	scontext.scx_area.r_ur.p_x++;
	scontext.scx_area.r_ur.p_y++;

	TTMaskAndMask3(&transPlaneMask, &ExtCurStyle->exts_deviceMask,
		    &DBPlaneTypes[reg->treg_pnum]);

	if (DBTreeSrTiles(&scontext, &transPlaneMask, 0, extFoundFunc,
		    (ClientData)def) != 0)
	    reg->treg_type = TT_SPACE;	/* Disables the trans record */
    }

    /*
     * Build up a list of the electrical nodes (equipotentials)
     * for extOutputNodes() below.  For this, we definitely want
     * to leave each tile pointing to its associated ExtRegion struct.
     * Compute resistance and capacitance on the fly.
     * Use a special-purpose version of ExtFindRegions for speed.
     */
    if (!SigInterruptPending)
	nodeList = extFindNodes(def, (Rect *) NULL, FALSE);

    /* Check for "LEFview", for which special output handling	*/
    /* can be specified in ext2spice.				*/

    DBPropGet(def, "LEFview", &isabstract);

    /* Keep a record of the def's substrate (unless this is an abstract view) */
    glob_subsnode = (isabstract) ? NULL : temp_subsnode;

    /* Assign the labels to their associated regions */
    if (!SigInterruptPending)
	ExtLabelRegions(def, ExtCurStyle->exts_nodeConn, &nodeList, &TiPlaneRect);

    /*
     * Make sure all geometry with the same label is part of the
     * same electrical node.  However:  Unconnected labels are allowed
     * on abstract views.
     */
    if (!SigInterruptPending && (ExtDoWarn & EXTWARN_DUP) && !isabstract)
	extFindDuplicateLabels(def, nodeList);

    /*
     * Build up table of coupling capacitances (overlap, sidewall).
     * This comes before extOutputNodes because we may have to adjust
     * node capacitances in this step.
     */
    if (!SigInterruptPending && (ExtOptions & EXT_DOCOUPLING))
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

	/* If the device name is "primitive", then parameters	*/
	/* are output here.					*/

	if (!strncmp(propptr, "primitive ", 10))
	    fprintf(outFile, "parameters :%s %s\n", def->cd_name, propptr + 10);
    }

    /* Output device parameters for any subcircuit devices.		*/
    /* This includes devices specified with the "device" parameter.	*/

    if (!SigInterruptPending)
	extOutputParameters(def, transList, outFile);

    if (isabstract) fprintf(outFile, "abstract\n");

    /* Output each node, along with its resistance and capacitance to substrate */
    if (!SigInterruptPending)
	extOutputNodes(nodeList, outFile);

    /* Output coupling capacitances */
    if (!SigInterruptPending && coupleInitialized && (ExtOptions & EXT_DOCOUPLING)
			&& (!propfound))
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

	/* Special case of device "primitive":  The subcircuit has the	*/
	/* name of a primitive device model and should be extracted as	*/
	/* the same.  The property string may contain parameters to	*/
	/* pass to the subcircuit.					*/

	if (propfound && (!strncmp(propptr, "primitive", 9)))
	    fprintf(outFile, "primitive\n");

	else if (propfound)
	{
	    /* Sanity checking on syntax of property line, plus	*/
	    /* conversion of values to internal units.		*/
	    propvalue = StrDup((char **)NULL, propptr);
	    token = strtok(propvalue, " ");

	    devidx = Lookup(token, extDevTable);
	    if (devidx < 0)
	    {
		TxError("Extract error:  \"device\" property has unknown "
				"device type: %s\n", token);
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
		if ((token == NULL) || (sscanf(token, "%d", &llx) != 1))
		    propfound = FALSE;
		else
		    llx *= ExtCurStyle->exts_unitsPerLambda;
	    }
	    if (propfound)
	    {
		token = strtok(NULL, " ");
		if ((token == NULL) || (sscanf(token, "%d", &lly) != 1))
		    propfound = FALSE;
		else
		    lly *= ExtCurStyle->exts_unitsPerLambda;
	    }
	    if (propfound)
	    {
		token = strtok(NULL, " ");
		if ((token == NULL) || (sscanf(token, "%d", &urx) != 1))
		    propfound = FALSE;
		else
		{
		    urx *= ExtCurStyle->exts_unitsPerLambda;
		    if (urx <= llx) urx++;
		}
	    }
	    if (propfound)
	    {
		token = strtok(NULL, " ");
		if ((token == NULL) || (sscanf(token, "%d", &ury) != 1))
		    propfound = FALSE;
		else
		{
		    ury *= ExtCurStyle->exts_unitsPerLambda;
		    if (ury <= lly) ury++;
		}
	    }
	    if (propfound)
	    {
		switch (devidx)
		{
		    case DEV_FET:
			/* Read area */
			token = strtok(NULL, " ");
			if ((token == NULL) || (sscanf(token, "%d", &w) != 1))
			    propfound = FALSE;
			else
			    w *= ExtCurStyle->exts_unitsPerLambda *
			    	   ExtCurStyle->exts_unitsPerLambda;
			/* Read perimeter */
			token = strtok(NULL, " ");
			if ((token == NULL) || (sscanf(token, "%d", &l) != 1))
			    propfound = FALSE;
			else
			    l *= ExtCurStyle->exts_unitsPerLambda;
			break;
		    case DEV_MOSFET:
		    case DEV_ASYMMETRIC:
		    case DEV_BJT:
			/* Read width */
			token = strtok(NULL, " ");
			if ((token == NULL) || (sscanf(token, "%d", &w) != 1))
			    propfound = FALSE;
			else
			    w *= ExtCurStyle->exts_unitsPerLambda;
			/* Read length */
			token = strtok(NULL, " ");
			if ((token == NULL) || (sscanf(token, "%d", &l) != 1))
			    propfound = FALSE;
			else
			    l *= ExtCurStyle->exts_unitsPerLambda;
			break;
		    case DEV_RES:
			if (strcmp(modelname, "None"))
			{
			    /* Read width */
			    token = strtok(NULL, " ");
			    if ((token == NULL) || (sscanf(token, "%d", &w) != 1))
				propfound = FALSE;
			    else
				w *= ExtCurStyle->exts_unitsPerLambda;
			    /* Read length */
			    token = strtok(NULL, " ");
			    if ((token == NULL) || (sscanf(token, "%d", &l) != 1))
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
			    if ((token == NULL) || (sscanf(token, "%d", &w) != 1))
				propfound = FALSE;
			    else
				w *= ExtCurStyle->exts_unitsPerLambda *
				     ExtCurStyle->exts_unitsPerLambda;
			    /* Read perimeter */
			    token = strtok(NULL, " ");
			    if ((token == NULL) || (sscanf(token, "%d", &l) != 1))
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
    int n, perim;
    dlong area;
    float s, fperim, v;

    for (n = 0; n < ExtCurStyle->exts_numResistClasses; n++)
    {
	reg->nreg_pa[n].pa_area = area = extResistArea[n];
	reg->nreg_pa[n].pa_perim = perim = extResistPerim[n];
	if (area > 0 && perim > 0)
	{
	    v = (double) ((dlong)perim * perim - 16 * area);

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
			ll->ll_label->lab_port,
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
	    /* Avoid negative capacitance caused by round-off near zero */
	    if (finC < 0.0) finC = 0.0;
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
	    fprintf(outFile, " %"DLONG_PREFIX"d %d", reg->nreg_pa[n].pa_area,
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

	/* Output the alternate names for the node.  Avoid generating	*/
	/* unnecessary "equiv A A" entries for labels on disconnected	*/
	/* nets.  Also avoid multiple "equiv" statements with the same	*/
	/* nets (happens when ports with the same name have different	*/
	/* port numbers, which should probably just be prohibited), and	*/
	/* raise an error if two ports with different names are being	*/
	/* marked as equivalent.					*/

	for (ll = reg->nreg_labels; ll; ll = ll->ll_next)
	{
	    bool isPort;

	    /* Do not export aliases that are not ports unless the  */
	    /* "extract do aliases" options was selected.	    */

	    if (ll->ll_label->lab_text == text)
	    {
		char *portname = NULL;
		char *lastname = NULL;

		isPort = (ll->ll_attr == LL_PORTATTR) ? TRUE : FALSE;
		if (isPort) portname = text;

		for (ll = ll->ll_next; ll; ll = ll->ll_next)
		     if (extLabType(ll->ll_label->lab_text, LABTYPE_NAME))
			if (strcmp(text, ll->ll_label->lab_text))
			{
			    if ((ll->ll_attr == LL_PORTATTR) ||
					(ExtOptions & EXT_DOALIASES))
			    {
				if ((portname == NULL) ||
					    (strcmp(ll->ll_label->lab_text, portname)))
				{
				    if ((lastname == NULL) ||
					    (strcmp(ll->ll_label->lab_text, lastname)))
					fprintf(outFile, "equiv \"%s\" \"%s\"\n",
						    text, ll->ll_label->lab_text);
				    lastname = ll->ll_label->lab_text;
				}
				/* Don't print a warning unless both labels are
				 * really ports.
				 */
				if ((portname != NULL) &&
			    		    (ll->ll_attr == LL_PORTATTR) &&
					    (strcmp(ll->ll_label->lab_text, portname)))
				    TxError("Warning:  Ports \"%s\" and \"%s\" are"
					    " electrically shorted.\n",
					    text, ll->ll_label->lab_text);
				if (!isPort && (ll->ll_attr == LL_PORTATTR))
				    portname = ll->ll_label->lab_text;
			    }
			    else
			    {
				/* Label is not recorded an an alias, so    */
				/* mark the label so that it will not be    */
				/* used for extracting merges or caps.	    */
				ll->ll_label->lab_port = INFINITY;
			    }
			}
		break;
	    }
	}
    }
}

/*
 * ---------------------------------------------------------------------
 *
 * extSubsName --
 *
 *	Return the name of the substrate node, if the node belongs to
 *	the substrate region and a global substrate node name has been
 *	specified by the tech file.  If the substrate node name is a
 *	Tcl variable name, then perform the variable substitution.
 *
 * Results:
 *	Pointer to a character string.
 *
 * Side Effects:
 *	None.
 *
 * ---------------------------------------------------------------------
 */
 
char *
extSubsName(node)
    LabRegion *node;
{
    char *subsName;

    /* If the techfile specifies a global name for the substrate, use	*/
    /* that in preference to the default "p_x_y#" name.	 Use this name	*/
    /* only to substitute for nodes with tiles at -(infinity).		*/

    if (ExtCurStyle->exts_globSubstrateName != NULL)
    {
	if (node->lreg_ll.p_x <= (MINFINITY + 3))
	{
#ifdef MAGIC_WRAPPER
	    if (ExtCurStyle->exts_globSubstrateName[0] == '$' &&
		 ExtCurStyle->exts_globSubstrateName[1] != '$')
	    {
		// If subsName is a Tcl variable (begins with "$"), make the
		// variable substitution, if one exists.  Ignore double-$.
		// If the variable is undefined in the interpreter, then
		// strip the "$" from the front as this is not legal in most
		// netlist formats.

		char *varsub = (char *)Tcl_GetVar(magicinterp,
			&ExtCurStyle->exts_globSubstrateName[1],
			TCL_GLOBAL_ONLY);
		return (varsub != NULL) ? varsub : ExtCurStyle->exts_globSubstrateName
			+ 1;
	    }
	    else
#endif
		return ExtCurStyle->exts_globSubstrateName;
	}
	else return NULL;
    }
    return NULL;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extMakeNodeNumPrint --
 *
 *	Construct a node name from the plane number "plane" and lower left Point
 *	"coord", and place it in the string "buf" (which must be large enough).
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Fills in string "buf".
 *
 * ----------------------------------------------------------------------------
 */

void
extMakeNodeNumPrint(buf, lreg)
    char *buf;
    LabRegion *lreg;
{
    int plane = lreg->lreg_pnum;
    Point *p = &lreg->lreg_ll;
    char *subsName;

    subsName = extSubsName(lreg);
    if (subsName != NULL)
	strcpy(buf, subsName);
    else
    	sprintf(buf, "%s_%s%d_%s%d#",
		DBPlaneShortName(plane),
		(p->p_x < 0) ? "n": "", abs(p->p_x),
		(p->p_y < 0) ? "n": "", abs(p->p_y));
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

    extMakeNodeNumPrint(namebuf, node);
    return (namebuf);
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
				extMakeNodeNumPrint(name, (LabRegion *)np2);
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
    int		tmp_len, tmp_area, tmp_perim, tmp_shared;
    LabelList   *lp;

    do
    {
	changed = 0;
	for( nsd = 0; nsd < tran->tr_nterm-1; nsd++ )
	{
	    p1 = &(tran->tr_termpos[nsd]);
	    p2 = &(tran->tr_termpos[nsd+1]);
	    if (p2->pnum > p1->pnum)
		continue;
	    else if (p2->pnum == p1->pnum)
	    {
		if (p2->pt.p_x > p1->pt.p_x)
		    continue;
		else if (p2->pt.p_x == p1->pt.p_x && p2->pt.p_y > p1->pt.p_y)
		    continue;
		else if (p2->pt.p_x == p1->pt.p_x && p2->pt.p_y == p1->pt.p_y)
		{
		    TxPrintf("Extract error:  Duplicate tile position, ignoring\n");
		    continue;
		}
	    }
	    changed = 1;
	    tmp_node = tran->tr_termnode[nsd];
	    tmp_pos = tran->tr_termpos[nsd];
	    tmp_len = tran->tr_termlen[nsd];
	    tmp_area = tran->tr_termarea[nsd];
	    tmp_perim = tran->tr_termperim[nsd];
	    tmp_shared = tran->tr_termshared[nsd];

	    tran->tr_termnode[nsd] = tran->tr_termnode[nsd+1];
	    tran->tr_termpos[nsd] = tran->tr_termpos[nsd+1];
	    tran->tr_termlen[nsd] = tran->tr_termlen[nsd+1];
	    tran->tr_termperim[nsd] = tran->tr_termperim[nsd+1];
	    tran->tr_termarea[nsd] = tran->tr_termarea[nsd+1];
	    tran->tr_termshared[nsd] = tran->tr_termshared[nsd+1];

	    tran->tr_termnode[nsd+1] = tmp_node;
	    tran->tr_termpos[nsd+1] = tmp_pos;
	    tran->tr_termlen[nsd+1] = tmp_len;
	    tran->tr_termarea[nsd+1] = tmp_area;
	    tran->tr_termperim[nsd+1] = tmp_perim;
	    tran->tr_termshared[nsd+1] = tmp_shared;

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
	    default:        ASSERT(FALSE, "oppdir"); /* should never happen */
	                    oppdir = 0;         break;
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
    ExtDevice *devptr;
    bool propfound = FALSE;
    char *propptr;

    TTMaskZero(&tmask);

    for (reg = transList; reg && !SigInterruptPending; reg = reg->treg_next)
    {
	TileType loctype = reg->treg_type;

	if (loctype == TT_SPACE) continue;	/* This has been disabled */

	/* Watch for rare split reg->treg_type */
	if (loctype & TT_DIAGONAL)
	    loctype = (reg->treg_type & TT_SIDE) ? ((reg->treg_type &
			TT_RIGHTMASK) >> 14) : (reg->treg_type & TT_LEFTMASK);

	TTMaskSetType(&tmask, loctype);
    }

    /* Check for the presence of property "device" followed by a device type
     * and device name, and if detected, add the type corresponding to the
     * device name to the mask so it gets handled, too.
     */
    propptr = DBPropGet(def, "device", &propfound);
    if (propfound)
    {
	char *devname;
	devname = propptr;
	while (!isspace(*devname)) devname++;
	if (*devname != '\0')
	    while (isspace(*devname)) devname++;

	if (*devname != '\0')
	{
	    char replace = *(devname + strlen(devname));
	    *(devname + strlen(devname)) = '\0';

	    /* This is dreadfully inefficient but happens only once */
	    for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
	    {
		for (devptr = ExtCurStyle->exts_device[t]; devptr;
				devptr = devptr->exts_next)
		{
		    if (!strcmp(devptr->exts_deviceName, devname))
		    {
			TTMaskSetType(&tmask, t);
			break;
		    }
		}
	    }

	    *(devname + strlen(devname)) = replace;
	}
    }

    for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
    {
	if (TTMaskHasType(&tmask, t))
	{
	    /* Note:  If there are multiple variants of a device type, they	*/
	    /* will all be listed even if they are not all present in the	*/
	    /* design.								*/

	    for (devptr = ExtCurStyle->exts_device[t]; devptr; devptr = devptr->exts_next)
	    {
		bool has_output = FALSE;

		/* Do not output parameters for ignored devices */
		if (!strcmp(devptr->exts_deviceName, "Ignore")) continue;

		/* Do a quick first pass to determine if there is anything
		 * to output (only entries with non-NULL pl_name get output).
		 */
		plist = devptr->exts_deviceParams;
		for (; plist != NULL; plist = plist->pl_next)
		    if (plist->pl_name != NULL)
		    {
			has_output = TRUE;
			break;
		    }

		if (has_output)
		{
		    fprintf(outFile, "parameters %s", devptr->exts_deviceName);
		    plist = devptr->exts_deviceParams;
		    for (; plist != NULL; plist = plist->pl_next)
		    {
			if (plist->pl_name == NULL) continue;
			else if (plist->pl_param[1] != '\0')
			{
			    if (plist->pl_scale != 1.0)
				fprintf(outFile, " %c%c=%s*%g",
					plist->pl_param[0], plist->pl_param[1],
					plist->pl_name, plist->pl_scale);
			    else if (plist->pl_offset != 0.0)
				fprintf(outFile, " %c%c=%s%+d",
					plist->pl_param[0], plist->pl_param[1],
					plist->pl_name, plist->pl_offset);
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
			    else if (plist->pl_offset != 0.0)
				fprintf(outFile, " %c=%s%+d",
					plist->pl_param[0],
					plist->pl_name, plist->pl_offset);
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
extOutputDevParams(reg, devptr, outFile, length, width, areavec, perimvec)
    TransRegion *reg;
    ExtDevice *devptr;
    FILE *outFile;
    int length;
    int width;
    int *areavec;
    int *perimvec;
{
    ParamList *chkParam;

    for (chkParam = devptr->exts_deviceParams; chkParam
		!= NULL; chkParam = chkParam->pl_next)
    {
	if (chkParam->pl_name == NULL) continue;
	switch(tolower(chkParam->pl_param[0]))
	{
	    case 'a':
		if (chkParam->pl_param[1] == '\0' ||
			chkParam->pl_param[1] == '0')
		    fprintf(outFile, " %c=%d", chkParam->pl_param[0],
				reg->treg_area);
		/* Note: a1, a2, etc., are standard output */
		break;
	    case 'p':
		if (chkParam->pl_param[1] == '\0' ||
			chkParam->pl_param[1] == '0')
		    fprintf(outFile, " %c=%d", chkParam->pl_param[0],
				extTransRec.tr_perim);
		/* Note: p1, p2, etc., are standard output */
		break;
	    case 'l':
		if (chkParam->pl_param[1] == '\0' ||
			chkParam->pl_param[1] == '0')
		    fprintf(outFile, " %c=%d", chkParam->pl_param[0],
				length);
		else if (chkParam->pl_param[1] > '0' && chkParam->pl_param[1] <= '9')
		{
		    int tidx = chkParam->pl_param[1] - '1';
		    /* output length of terminal, assuming a rectangular
		     * shape, as simplified terminal area / width
		     */
		    fprintf(outFile, " %c%c=%d", chkParam->pl_param[0],
				chkParam->pl_param[1],
				((width == 0) ? 0 : areavec[tidx] / width));
		}
		break;
	    case 'w':
		if (chkParam->pl_param[1] == '\0' ||
			chkParam->pl_param[1] == '0')
		    fprintf(outFile, " %c=%d", chkParam->pl_param[0],
				width);
		else if (chkParam->pl_param[1] > '0' && chkParam->pl_param[1] <= '9')
		{
		    int tidx = chkParam->pl_param[1] - '1';
		    /* NOTE: For a MOSFET, the gate width is the terminal
		     * width, and only "w" should be used as a parameter.
		     * For other devices, "w" with an index indicates that
		     * the device width is *not* the gate width.  Since only
		     * the device area is maintained, then in this case the
		     * terminal must be a single rectangle, from which the
		     * length and width are extracted as the length of the
		     * short and long sides, respectively.  This changes the
		     * value "width";  therefore, "w" with a suffix should
		     * come before "l" with a suffix in the device line in
		     * the tech file, since the "l" value will be derived
		     * from the area and width.
		     */
		    double newwidth = (double)(perimvec[tidx] * perimvec[tidx]);
		    newwidth -= (double)(16 * areavec[tidx]);
		    newwidth = sqrt(newwidth);
		    newwidth += perimvec[tidx];
		    width = (int)(0.25 * newwidth);
		    fprintf(outFile, " %c%c=%d", chkParam->pl_param[0],
				chkParam->pl_param[1], width);
		}
		break;
	    case 'c':
		fprintf(outFile, " %c=%g", chkParam->pl_param[0],
			(extTransRec.tr_devrec->exts_deviceGateCap
			* reg->treg_area) +
			(extTransRec.tr_devrec->exts_deviceSDCap
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

/* Structures used by extTermAPFunc() for storing area and perimeter data */

typedef struct _nodelist {
    struct _nodelist *nl_next;
    NodeRegion *nl_node;
} ExtNodeList;

typedef struct _extareaperimdata {
    int eapd_area;
    int eapd_perim;
    TileTypeBitMask eapd_mask;
    TileTypeBitMask *eapd_gatemask;
    NodeRegion *eapd_gatenode;
    ExtNodeList *eapd_shared;
} ExtAreaPerimData;

/*
 * ----------------------------------------------------------------------------
 *
 * extDevFindParamMatch --
 *
 * Routine which checks parameter values of a device against parameter
 * ranges specified for the device model.  If the parameters of the
 * current device do not match the ranges, then another device record
 * with matching parameters will be sought, and returned if found.
 * If no device with matching parameters is found, then the original
 * device record is returned, and a warning about parameter mismatch
 * is printed.
 *
 * When looking for alternate parameter ranges, all other parameter
 * record values must be the same as the current one.  A record with
 * no range (min > max) always matches.
 *
 * Return value:
 *	Pointer to a device record.
 *  
 * ----------------------------------------------------------------------------
 */

ExtDevice *
extDevFindParamMatch(devptr, length, width)
    ExtDevice *devptr;
    int length;		/* Computed effective length of device */
    int width;		/* Computed effective width of device */
{
    ExtDevice *newdevptr, *nextdev;
    int i;

    while (TRUE)
    {
	ParamList *chkParam;
	bool out_of_bounds = FALSE;

	newdevptr = devptr;
	nextdev = devptr->exts_next;

	for (chkParam = devptr->exts_deviceParams; chkParam != NULL;
			chkParam = chkParam->pl_next)
	{
	    if (chkParam->pl_minimum > chkParam->pl_maximum) continue;

	    switch (tolower(chkParam->pl_param[0]))
	    {
		case 'a':
		    if (chkParam->pl_param[1] == '\0' ||
				chkParam->pl_param[1] == '0')
		    {
			int area = length * width;
			if (area < chkParam->pl_minimum) out_of_bounds = TRUE;
			if (area > chkParam->pl_maximum) out_of_bounds = TRUE;
		    }
		    else
		    {
			int tidx = chkParam->pl_param[1] - '1';
			int area = extTransRec.tr_termarea[tidx];
			if (area < chkParam->pl_minimum) out_of_bounds = TRUE;
			if (area > chkParam->pl_maximum) out_of_bounds = TRUE;
		    }
		    break;
		case 'p':
		    if (chkParam->pl_param[1] == '\0' ||
				chkParam->pl_param[1] == '0')
		    {
			int perim = 2 * (length + width);
			if (perim < chkParam->pl_minimum) out_of_bounds = TRUE;
			if (perim > chkParam->pl_maximum) out_of_bounds = TRUE;
		    }
		    else
		    {
			int tidx = chkParam->pl_param[1] - '1';
			int perim = extTransRec.tr_termperim[tidx];
			if (perim < chkParam->pl_minimum) out_of_bounds = TRUE;
			if (perim > chkParam->pl_maximum) out_of_bounds = TRUE;
		    }
		    break;
		case 'l':
		    if (chkParam->pl_param[1] == '\0' ||
				chkParam->pl_param[1] == '0')
		    {
			if (length < chkParam->pl_minimum) out_of_bounds = TRUE;
			if (length > chkParam->pl_maximum) out_of_bounds = TRUE;
		    }
		    else if (chkParam->pl_param[1] > '0' && chkParam->pl_param[1] <= '9')
		    {
			int tidx = chkParam->pl_param[1] - '1';
			int len = extTransRec.tr_termlen[tidx];
			if (len < chkParam->pl_minimum) out_of_bounds = TRUE;
			if (len > chkParam->pl_maximum) out_of_bounds = TRUE;
		    }
		    break;
		case 'w':
		    if (width < chkParam->pl_minimum) out_of_bounds = TRUE;
		    if (width > chkParam->pl_maximum) out_of_bounds = TRUE;
		    break;
		default:
		    /* Do nothing;  these parameters cannot be used for
		     * differentiating device models.
		     */
		    break;
	    }
	    if (out_of_bounds) break;
	}
	if (chkParam == NULL) break;

	/* Check that the next device record is compatible in all values
	 * except for parameters and name.
	 */
	if (nextdev != NULL)
	{
	    if (nextdev->exts_deviceClass != devptr->exts_deviceClass)
		nextdev = NULL;
	    else if (nextdev->exts_deviceSDCount != devptr->exts_deviceSDCount)
		nextdev = NULL;
	}

	if (nextdev != NULL)
	{
	    for (i = 0; i < nextdev->exts_deviceSDCount; i++)
	    {
		if (!TTMaskEqual(&nextdev->exts_deviceSDTypes[i],
				&devptr->exts_deviceSDTypes[i]))
		{
		    nextdev = NULL;
		    break;
		}
	    }
	}

	if (nextdev != NULL)
	    if (!TTMaskEqual(&nextdev->exts_deviceSubstrateTypes,
				&devptr->exts_deviceSubstrateTypes))
		nextdev = NULL;

	if (nextdev != NULL)
	    if (!TTMaskEqual(&nextdev->exts_deviceIdentifierTypes,
				&devptr->exts_deviceIdentifierTypes))
		nextdev = NULL;

	if (nextdev == NULL)
	{
	    newdevptr = devptr;		/* Return to original entry */
	    TxError("Device parameters do not match any extraction model.\n");
	    break;
	}
	else
	    devptr = nextdev;
    }

    return newdevptr;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extSDTileFunc --
 *
 * Callback function to gather tiles belonging to a multiple-tile device
 * region.
 *
 * Results:
 *	Always return 0 to keep the search going.
 *
 * Side effects:
 *	Allocates memory in the extSpecialDevice linked list.
 *
 * ----------------------------------------------------------------------------
 */
int
extSDTileFunc(tile, pNum)
    Tile *tile;
    int pNum;
{
    LinkedTile *newdevtile;

    newdevtile = (LinkedTile *)mallocMagic(sizeof(LinkedTile));
    newdevtile->t = tile;
    newdevtile->t_next = extSpecialDevice;
    extSpecialDevice = newdevtile;
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extTransFindTermArea --
 *
 * Callback function to find the area and perimeter of a terminal area in
 * a plane other than the plane of the device identifier type.  This routine
 * searches around the first tile of the terminal for all connected terminal
 * types and calculates area and perimeter.
 *
 * Return value:
 *	Always return 1 to stop the search, because we need only one tile
 *	under the identifier tile to start the search.
 *
 * ----------------------------------------------------------------------------
 */
int
extTransFindTermArea(tile, eapd)
    Tile *tile;
    ExtAreaPerimData *eapd;
{
    int extTermAPFunc();	/* Forward declaration */

    DBSrConnectOnePlane(tile, DBConnectTbl, extTermAPFunc, (ClientData)eapd);
    return 1;
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
 *	Writes a number of 'device' records to the file 'outFile'.
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
    ExtDevice *devptr, *deventry;
    char *subsName;
    FindRegion arg;
    LabelList *ll;
    TileType t;
    int nsd, length, width, n, i, ntiles, corners, tn, rc, termcount;
    double dres, dcap;
    char mesg[256];
    bool isAnnular, hasModel, sd_is_tied;

    for (reg = transList; reg && !SigInterruptPending; reg = reg->treg_next)
    {
	if (reg->treg_type == TT_SPACE) continue;	/* This has been disabled */

	/*
	 * Visit all of the tiles in the transistor region, updating
	 * extTransRec.tr_termnode[] and extTransRec.tr_termlen[],
	 * and the attribute lists for this transistor.
	 *
	 * Algorithm: first visit all tiles in the transistor, marking
	 * them with 'reg', then visit them again re-marking them with
	 * the gate node (extGetRegion(reg->treg_tile)).
	 */
	extTransRec.tr_devrec = (ExtDevice *)NULL;
	extTransRec.tr_devmatch = 0;
	extTransRec.tr_nterm = 0;
	extTransRec.tr_gatelen = 0;
	extTransRec.tr_perim = 0;
	extTransRec.tr_plane = reg->treg_pnum;		/* Save this value! */
	extTransRec.tr_subsnode = (NodeRegion *)NULL;

	for (i = 0; i < MAXSD; i++)
	{
	    extTransRec.tr_termnode[i] = NULL;
	    extTransRec.tr_termlen[i] = 0;
	    extTransRec.tr_termarea[i] = 0;
	    extTransRec.tr_termperim[i] = 0;
	    extTransRec.tr_termshared[i] = 0;
	    extTransRec.tr_termvector[i].p_x = 0;
	    extTransRec.tr_termvector[i].p_y = 0;
	    extTransRec.tr_termpos[i].pnum = 0;
	    extTransRec.tr_termpos[i].pt.p_x = 0;
	    extTransRec.tr_termpos[i].pt.p_y = 0;
	}

	arg.fra_def = def;
	arg.fra_connectsTo = ExtCurStyle->exts_deviceConn;

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

	for (i = 0; i < MAXSD; i++) extTransRec.tr_termnode[i] = NULL;

	/* Mark with reg and process each perimeter segment */
	arg.fra_uninit = (ClientData) extTransRec.tr_gatenode;
	arg.fra_region = (ExtRegion *) reg;
	arg.fra_each = extTransTileFunc;
	ntiles = ExtFindNeighbors(reg->treg_tile, arg.fra_pNum, &arg);

	/* Re-mark with extTransRec.tr_gatenode */
	arg.fra_uninit = (ClientData) reg;
	arg.fra_region = (ExtRegion *) extTransRec.tr_gatenode;
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

	devptr = extTransRec.tr_devrec;
	if (devptr == NULL) continue;	/* Bad device; do not output */
	deventry = devptr;

	/* Use devmatch flags to determine if the specific S/D	*/
	/* terminal was handled by extTransPerimFunc already.	*/
	/* Only look at required S/D terminals that have not	*/
	/* yet been found.					*/

	while (TRUE)
	{
	    if (devptr == NULL) break;	    /* Bad device */
	    nsd = devptr->exts_deviceSDCount;
	    sd_is_tied = FALSE;
	    for (termcount = 0; termcount < nsd; termcount++)
	    {
		TileTypeBitMask *tmask;

		if ((extTransRec.tr_devmatch & (MATCH_TERM << termcount)) != 0)
		    continue;   /* This terminal already found by perimeter search */

		tmask = &devptr->exts_deviceSDTypes[termcount];
		if (TTMaskIsZero(tmask)) {
		    if (termcount < nsd) {
			/* Not finding another device record just means that	*/
			/* terminals are tied together on the same net, such as	*/
			/* with a MOS cap.  Accept this fact and move on.	*/
			sd_is_tied = TRUE;
		    } 
		    break;	/* End of SD terminals */
		}
		else if (!TTMaskIntersect(tmask, &DBPlaneTypes[reg->treg_pnum])
			|| (TTMaskHasType(tmask, TT_SPACE)))
		{
		    ExtAreaPerimData eapd;
		    TileType tt = TT_SPACE;
		    Rect r;

		    node = NULL;

		    /* First try to find a region under the device */
		    extTransFindSubs(reg->treg_tile, t, tmask, def, &node, &tt);

		    /* If the device has multiple tiles, then check all of them.
		     * This is inefficient, so this routine first assumes that
		     * the device is a single tile, and if nothing is found
		     * underneath, it then checks if the accumulated gate area
		     * is larger than the tile area.  If so, it does a full
		     * search on all tiles.
		     */
		    if (node == NULL)
		    {
			int tarea;
			tarea = (RIGHT(reg->treg_tile) - LEFT(reg->treg_tile)) *
				(TOP(reg->treg_tile) - BOTTOM(reg->treg_tile));
			if (tarea < reg->treg_area)
			{
			    LinkedTile *lt;

			    extSpecialDevice = (LinkedTile *)NULL;

			    arg.fra_uninit = (ClientData)extTransRec.tr_gatenode;
			    arg.fra_region = (ExtRegion *)reg;
			    arg.fra_each = extSDTileFunc;
			    ExtFindNeighbors(reg->treg_tile, arg.fra_pNum, &arg);

			    arg.fra_uninit = (ClientData) reg;
			    arg.fra_region = (ExtRegion *) extTransRec.tr_gatenode;
			    arg.fra_each = (int (*)()) NULL;
			    ExtFindNeighbors(reg->treg_tile, arg.fra_pNum, &arg);

			    for (lt = extSpecialDevice; lt; lt = lt->t_next)
			    {
				extTransFindSubs(lt->t, t, tmask, def, &node, &tt);
				if (node != NULL)
				{
				    TiToRect(lt->t, &r);
				    break;
				}
			    }
			    for (lt = extSpecialDevice; lt; lt = lt->t_next)
				freeMagic((char *)lt);
			}
		    }
		    else
			TiToRect(reg->treg_tile, &r);

		    if ((node == NULL) && (TTMaskHasType(tmask, TT_SPACE)))
		    {
			/* Device node is the global substrate. */
			node = glob_subsnode;
		    }
		    else if (node == NULL) {
			/* See if there is another matching device record	*/
			/* with a different terminal type, and try again.	*/
			devptr = extDevFindMatch(devptr, t);
			break;
		    }
		    extTransRec.tr_devmatch |= (MATCH_TERM << termcount);
		    extTransRec.tr_termnode[termcount] = node;

		    /* Terminals on other planes will not have area and perimeter
		     * computed, so do that here.
		     */
		    eapd.eapd_area = 0;
		    eapd.eapd_perim = 0;
		    eapd.eapd_shared = NULL;
		    /* NOTE: Currently there is no way to determine if a
		     * terminal on another plane belongs to multiple devices,
		     * so device sharing is not checked.  Could be done by
		     * checking the terminal area for the gate mask (on its
		     * own plane) in extTermAPFunc().
		     */
		    eapd.eapd_gatemask = &DBZeroTypeBits;
		    TTMaskCom2(&eapd.eapd_mask, tmask);
		    if (tt == TT_SPACE)
		    {
			/* Terminal may be the substrate, in which case	*/
			/* the device should not be recording area or	*/
			/* perimeter, so leave them as zero.		*/
			extTransRec.tr_termarea[termcount] = 0;
			extTransRec.tr_termperim[termcount] = 0;
		    }
		    else
		    {
			DBSrPaintArea((Tile *)NULL, def->cd_planes[DBPlane(tt)],
				&r, tmask, extTransFindTermArea, (ClientData)&eapd);
			extTransRec.tr_termarea[termcount] = eapd.eapd_area;
			extTransRec.tr_termperim[termcount] = eapd.eapd_perim;
		    }
		    extTransRec.tr_termshared[termcount] = 1;
		}
		else {
		    /* Determine if there is another matching device record */
		    /* that has fewer required terminals.		    */
		    devptr = extDevFindMatch(devptr, t);
		    break;
		}
		if (termcount == nsd) break;    /* All terminals accounted for */
	    }
	    if (termcount == nsd) break;    /* All terminals accounted for */
	    if (devptr == deventry) break;  /* No other device records available */
	    if (sd_is_tied) break;	    /* Legal case of tied source and drain */
	    /* Try again with a different device record */
	}
	extTransRec.tr_nterm = termcount;

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
		(void) strncat(mesg, extNodeName((LabRegion *) node),
				255 - strlen(mesg));
		while (extTransRec.tr_nterm < nsd)
		{
		    extTransRec.tr_termlen[extTransRec.tr_nterm] = 0;
		    extTransRec.tr_termarea[extTransRec.tr_nterm] = 0;
		    extTransRec.tr_termperim[extTransRec.tr_nterm] = 0;
		    extTransRec.tr_termshared[extTransRec.tr_nterm] = 0;
		    extTransRec.tr_termnode[extTransRec.tr_nterm++] = node;
		}
	    }
	    if (ExtDoWarn & EXTWARN_FETS)
		extTransBad(def, reg->treg_tile, mesg);

	    /* Devices with no terminals or a null node are badly	*/
	    /* formed and should not be output.	  This can happen when	*/
	    /* parts of devices are split into different cells.		*/

	    if ((extTransRec.tr_nterm == 0) || (node == NULL))
		continue;
	}
	else if (extTransRec.tr_nterm > nsd)
	{
	    /* It is not an error condition to have more terminals */
	    /* than the minimum.				   */
	}
	if (devptr == NULL) {
	    TxError("Warning:  No matching extraction type for device at (%d %d)\n",
			reg->treg_tile->ti_ll.p_x, reg->treg_tile->ti_ll.p_y);
	    continue;
	}

	/*
	 * Output the transistor record.
	 * The type is devptr->exts_deviceName, which should have
	 * some meaning to the simulator we are producing this file for.
	 * Use the default substrate node unless the transistor overlaps
	 * material whose type is in exts_deviceSubstrateTypes, in which
	 * case we use the node of the overlapped material.
	 *
	 * Technology files using the "substrate" keyword (magic-8.1 or
	 * newer) should have the text "error" in the substrate node
	 * name.
	 */
	subsName = devptr->exts_deviceSubstrateName;
	if (!TTMaskIsZero(&devptr->exts_deviceSubstrateTypes)
		&& (subsNode = extTransRec.tr_subsnode))
	{
	    subsName = extNodeName((LabRegion *)subsNode);
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
	extTransRec.tr_devrec = devptr;

	/* Model type "Ignore" in the techfile indicates a device   */
	/* to be ignored (i.e., a specific combination of layers    */
	/* does not form an extractable device, or overlaps another */
	/* device type that should take precedence).		    */

	if (!strcmp(devptr->exts_deviceName, "Ignore"))
	    continue;

	/* Model type "Short" in the techfile indicates a device    */
	/* to short across the first two nodes (the gate and the    */
 	/* source).  This solves the specific issue of a transistor */
	/* extended drain where the drain is a resistor but the	    */
	/* resistor is part of the model and should not be output.  */

	if (!strcmp(devptr->exts_deviceName, "Short"))
	{
	    fprintf(outFile, "equiv ");

	    /* To do:  Use parameters to specify which terminals   */
	    /* are shorted.					   */

	    /* gate */
	    node = (NodeRegion *)extGetRegion(reg->treg_tile);
	    fprintf(outFile, "\"%s\" ", extNodeName((LabRegion *)node));

	    /* First non-gate terminal */
	    node = (NodeRegion *)extTransRec.tr_termnode[0];
	    fprintf(outFile, "\"%s\"\n", extNodeName((LabRegion *)node));

	    continue;
	}

	/* Original-style FET record backward compatibility */
	if (devptr->exts_deviceClass != DEV_FET)
	    fprintf(outFile, "device ");

	/* NOTE:  The code for the old FET device makes unreasonable	*/
	/* simplifying assumptions about how to calculate device length	*/
	/* and width.  The newer MOSFET and MSUBCKT and other devices	*/
	/* compute proper length and width, including but not limited	*/
	/* to dealing with bends and annular shapes.			*/

	switch (devptr->exts_deviceClass)
	{
	    case DEV_FET:	/* old style, perimeter & area */
		fprintf(outFile, "%s %s",
			extDevTable[(unsigned char)devptr->exts_deviceClass],
			devptr->exts_deviceName);

		fprintf(outFile, " %d %d %d %d",
			reg->treg_ll.p_x, reg->treg_ll.p_y,
			reg->treg_ll.p_x + 1, reg->treg_ll.p_y + 1);

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
		length = 0;
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

		/* For devices missing a terminal, reduce n accordingly. */
		/* This avoids errors in length and width calculations.  */
		while ((n > 0) && (extTransRec.tr_termnode[n - 1]) &&
			(extTransRec.tr_termlen[n - 1] == 0) &&
			(extTransRec.tr_termarea[n - 1] == 0) &&
			(extTransRec.tr_termperim[n - 1] == 0)) n--;

		if (n)
		{
		    width /= n;
		    if (n > 1)
			length = extTransRec.tr_gatelen / n;
		    else if (extTransRec.tr_gatelen < width)
			length = extTransRec.tr_gatelen;
		    else
		    {
			/* Assumption:  A device with a single terminal	*/
			/* must be rectangular;  an example is a MOSCAP	*/
			/* with poly over three sides of diffusion.  	*/
			/* Length in this case is not properly defined,	*/
			/* but it is only necessary for the model to	*/
			/* get the correct area per W * L.  If the	*/
			/* device is annular, then this assumption will	*/
			/* get corrected by extComputeEffectiveLW().	*/
			length = (extTransRec.tr_gatelen - width) / 2;
		    }
		}

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
		    /* because the default substrate node is the other	*/
		    /* terminal.					*/

		    if (ExtDoWarn && (devptr->exts_deviceSDCount > 0))
			extTransBad(def, reg->treg_tile,
				"Could not determine device boundary");
		    length = width = 0;
		}
		else
		{
		    LinkedBoundary *lb;

		    extSpecialBounds = (LinkedBoundary **)mallocMagic(
				extTransRec.tr_nterm *
				sizeof(LinkedBoundary *));

		    for (i = 0; i < extTransRec.tr_nterm; i++)
			extSpecialBounds[i] = NULL;

		    /* Mark with reg and process each perimeter segment */

		    arg.fra_uninit = (ClientData) extTransRec.tr_gatenode;
		    arg.fra_region = (ExtRegion *) reg;
		    arg.fra_each = extAnnularTileFunc;

		    (void) ExtFindNeighbors(reg->treg_tile, arg.fra_pNum, &arg);

		    extSeparateBounds(n - 1);	/* Handle MOScaps (if necessary) */
		    extComputeEffectiveLW(&length, &width, n,
				ExtCurStyle->exts_cornerChop[t]);

		    /* Free the lists */

		    for (i = 0; i < extTransRec.tr_nterm; i++)
			for (lb = extSpecialBounds[i]; lb != NULL; lb = lb->b_next)
			    freeMagic((char *)lb);
		    freeMagic((char *)extSpecialBounds);

		    /* Put the region list back the way we found it: */
		    /* Re-mark with extTransRec.tr_gatenode */

		    arg.fra_uninit = (ClientData) reg;
		    arg.fra_region = (ExtRegion *) extTransRec.tr_gatenode;
		    arg.fra_each = (int (*)()) NULL;
		    (void) ExtFindNeighbors(reg->treg_tile, arg.fra_pNum, &arg);

		}

		devptr = extDevFindParamMatch(devptr, length, width);
		fprintf(outFile, "%s %s",
			extDevTable[(unsigned char)devptr->exts_deviceClass],
			devptr->exts_deviceName);

		fprintf(outFile, " %d %d %d %d",
			reg->treg_ll.p_x, reg->treg_ll.p_y,
			reg->treg_ll.p_x + 1, reg->treg_ll.p_y + 1);

		if (devptr->exts_deviceClass == DEV_MOSFET ||
			devptr->exts_deviceClass == DEV_ASYMMETRIC ||
			devptr->exts_deviceClass == DEV_BJT)
		{
		    fprintf(outFile, " %d %d", length, width);
		}

		extOutputDevParams(reg, devptr, outFile, length, width,
				extTransRec.tr_termarea, extTransRec.tr_termperim);

		fprintf(outFile, " \"%s\"", (subsName == NULL) ?
					"None" : subsName);
		break;

	    case DEV_DIODE:	/* Only handle the optional substrate node */
	    case DEV_NDIODE:
	    case DEV_PDIODE:
		devptr = extDevFindParamMatch(devptr, length, width);
		fprintf(outFile, "%s %s",
			extDevTable[(unsigned char)devptr->exts_deviceClass],
			devptr->exts_deviceName);

		fprintf(outFile, " %d %d %d %d",
			reg->treg_ll.p_x, reg->treg_ll.p_y,
			reg->treg_ll.p_x + 1, reg->treg_ll.p_y + 1);

		extOutputDevParams(reg, devptr, outFile, length, width,
				extTransRec.tr_termarea, extTransRec.tr_termperim);
		if (subsName != NULL)
		    fprintf(outFile, " \"%s\"", subsName);
		break;

	    case DEV_RES:
	    case DEV_RSUBCKT:
		hasModel = strcmp(devptr->exts_deviceName, "None");
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
		    {
			/* Update: 11/14/2024:  A snake resistor with terminals
			 * tied together also has a zero termvector and should
			 * not be treated as annular!
			 */
			if ((n == 0) || (extTransRec.tr_termnode[n] !=
				extTransRec.tr_termnode[n - 1]))
			    isAnnular = TRUE;
		    }
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
		    arg.fra_region = (ExtRegion *) reg;
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
		    arg.fra_region = (ExtRegion *) extTransRec.tr_gatenode;
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

		devptr = extDevFindParamMatch(devptr, length, width);

		fprintf(outFile, "%s %s",
			extDevTable[(unsigned char)devptr->exts_deviceClass],
			devptr->exts_deviceName);

		fprintf(outFile, " %d %d %d %d",
			reg->treg_ll.p_x, reg->treg_ll.p_y,
			reg->treg_ll.p_x + 1, reg->treg_ll.p_y + 1);

		if (devptr->exts_deviceClass == DEV_RSUBCKT)
		{
		    /* (Nothing) */
		}
		else if (hasModel)	/* SPICE semiconductor resistor */
		{
		    fprintf(outFile, " %d %d", length, width);
		    if (subsName != NULL)
			fprintf(outFile, " \"%s\"", subsName);
		}
		else		/* regular resistor */
		    fprintf(outFile, " %g", dres / 1000.0); /* mOhms -> Ohms */

		extOutputDevParams(reg, devptr, outFile, length, width,
				extTransRec.tr_termarea, extTransRec.tr_termperim);

		if (devptr->exts_deviceClass == DEV_RSUBCKT)
		{
		    fprintf(outFile, " \"%s\"", (subsName == NULL) ?
				"None" : subsName);
		}
		break;

	    case DEV_CAP:
	    case DEV_CAPREV:
	    case DEV_CSUBCKT:
		fprintf(outFile, "%s %s",
			extDevTable[(unsigned char)devptr->exts_deviceClass],
			devptr->exts_deviceName);

		fprintf(outFile, " %d %d %d %d",
			reg->treg_ll.p_x, reg->treg_ll.p_y,
			reg->treg_ll.p_x + 1, reg->treg_ll.p_y + 1);

		hasModel = strcmp(devptr->exts_deviceName, "None");
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
			arg.fra_region = (ExtRegion *) reg;
			arg.fra_each = extAnnularTileFunc;
			(void) ExtFindNeighbors(reg->treg_tile, arg.fra_pNum, &arg);

			extComputeCapLW(&length, &width);

			/* Free the lists */

			for (i = 0; i < n; i++)
			    for (lb = extSpecialBounds[i]; lb != NULL; lb = lb->b_next)
				freeMagic((char *)lb);
			freeMagic((char *)extSpecialBounds);

			/* Put the region list back the way we found it: */
			/* Re-mark with extTransRec.tr_gatenode */

			arg.fra_uninit = (ClientData) reg;
			arg.fra_region = (ExtRegion *) extTransRec.tr_gatenode;
			arg.fra_each = (int (*)()) NULL;
			(void) ExtFindNeighbors(reg->treg_tile, arg.fra_pNum, &arg);
		    }

		    if (devptr->exts_deviceClass == DEV_CSUBCKT)
		    {
			/* (Nothing) */
		    }
		    else	/* SPICE semiconductor resistor */
		    {
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
			if (subsName != NULL)
			    fprintf(outFile, " \"%s\"", subsName);
		    }

		    extOutputDevParams(reg, devptr, outFile, length, width,
				extTransRec.tr_termarea, extTransRec.tr_termperim);

		    if (devptr->exts_deviceClass == DEV_CSUBCKT)
		    {
			fprintf(outFile, " \"%s\"", (subsName == NULL) ?
				"None" : subsName);
		    }
		}
		else
		{
		    dcap = (devptr->exts_deviceGateCap * reg->treg_area) +
			(devptr->exts_deviceSDCap * extTransRec.tr_perim);

		    fprintf(outFile, " %g", dcap / 1000.0);  /* aF -> fF */
		}
		break;
	}

	/* gate */
	node = (NodeRegion *) extGetRegion(reg->treg_tile);
	ll = node->nreg_labels;
	extTransOutTerminal((LabRegion *) node, ll, LL_GATEATTR,
			extTransRec.tr_gatelen, 0, 0, 0, outFile);

	/* Sort source and drain terminals by position, unless the	*/
	/* device is asymmetric, in which case source and drain do not	*/
	/* permute, and the terminal order is fixed.			*/

	switch (devptr->exts_deviceClass)
	{
	    case DEV_FET:
	    case DEV_MOSFET:
	    case DEV_MSUBCKT:
		if (TTMaskIsZero(&devptr->exts_deviceSDTypes[1]))
		    ExtSortTerminals(&extTransRec, ll);
		break;
	}

	/* each non-gate terminal */
	for (nsd = 0; nsd < extTransRec.tr_nterm; nsd++)
	    extTransOutTerminal((LabRegion *) extTransRec.tr_termnode[nsd], ll,
			nsd, extTransRec.tr_termlen[nsd],
			extTransRec.tr_termarea[nsd],
			extTransRec.tr_termperim[nsd],
			extTransRec.tr_termshared[nsd], outFile);

	(void) fputs("\n", outFile);
    }
}

/* Structure to hold a node region and a tile type */

typedef struct _node_type {
    NodeRegion *region;
    TileType layer;
} NodeAndType;

int
extTransFindSubs(tile, t, mask, def, sn, layerptr)
    Tile *tile;
    TileType t;
    TileTypeBitMask *mask;
    CellDef *def;
    NodeRegion **sn;
    TileType *layerptr;
{
    Rect tileArea, tileAreaPlus;
    int pNum;
    int extTransFindSubsFunc1();	/* Forward declaration */
    NodeAndType noderec;
    TileTypeBitMask lmask;

    noderec.region = (NodeRegion *)NULL;
    noderec.layer = TT_SPACE;

    TiToRect(tile, &tileArea);

    /* Expand tile area by 1 in all directions.  This catches terminals */
    /* on certain extended drain MOSFET devices.			*/
    GEO_EXPAND(&tileArea, 1, &tileAreaPlus);

    /* If mask includes TT_SPACE, make sure that is removed before	*/
    /* determining a plane intersection (because space intersects all	*/
    /* planes).								*/

    lmask = *mask;
    TTMaskClearType(&lmask, TT_SPACE);

    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
    {
	if (TTMaskIntersect(&DBPlaneTypes[pNum], &lmask))
	{
	    if (DBSrPaintArea((Tile *) NULL, def->cd_planes[pNum], &tileAreaPlus,
		    mask, extTransFindSubsFunc1, (ClientData)&noderec))
	    {
		*sn = noderec.region;
		if (layerptr) *layerptr = noderec.layer;
		return 1;
	    }
	}
    }
    return 0;
}

int
extTransFindSubsFunc1(tile, noderecptr)
    Tile *tile;
    NodeAndType *noderecptr;
{
    TileType type;

    /* Report split substrate region errors (two different substrate
     * regions under the same device)
     */

    ClientData ticlient = TiGetClient(tile);
    if (ticlient != extUnInit)
    {
	NodeRegion *reg = (NodeRegion *) CD2PTR(ticlient);
	if ((noderecptr->region != (NodeRegion *)NULL) &&
		    (noderecptr->region != reg))
	    TxError("Warning:  Split substrate under device at (%d %d)\n",
			tile->ti_ll.p_x, tile->ti_ll.p_y);
	if (IsSplit(tile))
	{
	    type = (SplitSide(tile)) ? SplitRightType(tile): SplitLeftType(tile);
	    if (type == TT_SPACE) return 0;	/* Ignore space in split tiles */
	}
	else
	    type = TiGetTypeExact(tile);

	noderecptr->region = (NodeRegion *) reg;
	noderecptr->layer = type;
	return 1;
    }
    return 0;
}

int
extTransFindId(tile, mask, def, idtypeptr)
    Tile *tile;
    TileTypeBitMask *mask;
    CellDef *def;
    TileType *idtypeptr;
{
    TileType type;
    Rect tileArea;
    int pNum;
    int extTransFindIdFunc1();	/* Forward declaration */

    TiToRect(tile, &tileArea);
    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
    {
	if (TTMaskIntersect(&DBPlaneTypes[pNum], mask))
	{
	    if (DBSrPaintArea((Tile *) NULL, def->cd_planes[pNum], &tileArea,
		    mask, extTransFindIdFunc1, (ClientData)idtypeptr))
		return 1;
	}
    }
    return 0;
}

int
extTransFindIdFunc1(tile, idtypeptr)
    Tile *tile;
    TileType *idtypeptr;
{
    /*
     * ID Layer found overlapping device area, so return 1 to halt search.
     */
    if (IsSplit(tile))
        *idtypeptr = (SplitSide(tile)) ? SplitRightType(tile): SplitLeftType(tile);
    else
	*idtypeptr = TiGetTypeExact(tile);

    return 1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extDevFindMatch --
 *
 * Find and return the next matching device record.  extTransRec is queried
 * to find how many fields have been matched to the device already.  Then
 * it finds the next record with the same matching fields.  Because records
 * are in no specific order, it will loop to the beginning of the records
 * after reaching the end.  It is the responsibility of the calling routine to
 * determine if all matching devices have been tested.
 *
 * deventry is the record position to start the search.
 * t is the tile type of the device.
 * ----------------------------------------------------------------------------
 */

ExtDevice *
extDevFindMatch(deventry, t)
    ExtDevice *deventry;
    TileType t;
{
    ExtDevice *devptr;
    int i, j, k, matchflags;
    bool match;

    matchflags = extTransRec.tr_devmatch;

    if (deventry->exts_next == NULL)
	devptr = ExtCurStyle->exts_device[t];
    else
	devptr = deventry->exts_next;

    for (; devptr != deventry;
		devptr = (devptr->exts_next) ? devptr->exts_next :
		ExtCurStyle->exts_device[t])
    {
	if (matchflags == 0) break;	/* Always return next entry */

	if (matchflags & MATCH_ID)	/* Must have the same identifier */
	    if (!TTMaskEqual(&devptr->exts_deviceIdentifierTypes,
			    &deventry->exts_deviceIdentifierTypes)) continue;

	if (matchflags & MATCH_SUB)	/* Must have the same substrate type */
	    if (!TTMaskEqual(&devptr->exts_deviceSubstrateTypes,
			    &deventry->exts_deviceSubstrateTypes)) continue;

	if (matchflags & MATCH_PARAM)	/* Must have compatible parameter range */
	    /* To be completed */
	    ;

	j = MATCH_TERM;
	i = 0;
	match = TRUE;
	for (k = 0; k < devptr->exts_deviceSDCount; k++)
	{
	    if (extTransRec.tr_termnode[k] == NULL) break;
	    if (matchflags & j)	/* Must have the same terminal type */
	    {
		if (TTMaskIsZero(&devptr->exts_deviceSDTypes[i]))
		{
		    match = FALSE;
		    break;
		}
		if (!TTMaskEqual(&devptr->exts_deviceSDTypes[i],
			    &deventry->exts_deviceSDTypes[i]))
		{
		    match = FALSE;
		    break;
		}
	    }
	    j <<= 1;

	    /* NOTE:  There are fewer exts_deviceSDTypes records than	*/
	    /* terminals if all S/D terminals are the same type.  In	*/
	    /* that case k increments and j bit shifts but i remains	*/
	    /* the same.						*/
	    if (!TTMaskIsZero(&devptr->exts_deviceSDTypes[i + 1])) i++;
	}
	if (match) break;
    }
    return (devptr == deventry) ? NULL : devptr;
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
    TileTypeBitMask mask, cmask, *smask;
    TileType loctype, idlayer, sublayer;
    int perim, result, i;
    bool allow_globsubsnode;
    ExtDevice *devptr, *deventry, *devtest;
    NodeRegion *region;

    LabelList *ll;
    Label *lab;
    Rect r;

    TITORECT(tile, &r);
    for (ll = extTransRec.tr_gatenode->nreg_labels; ll; ll = ll->ll_next)
    {
	/* Skip if already marked */
	if (ll->ll_attr != LL_NOATTR) continue;
	lab = ll->ll_label;
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

    mask = ExtCurStyle->exts_deviceConn[loctype];
    TTMaskCom(&mask);

    /* NOTE:  DO NOT USE extTransRec.tr_perim += extEnumTilePerim(...)	*/
    /* The AMD target gcc compile works and the Intel target gcc	*/
    /* compile doesn't!  The following code works the same on both.	*/

    perim = extEnumTilePerim(tile, &mask, pNum,
		extTransPerimFunc, (ClientData)NULL);
    extTransRec.tr_perim += perim;

    devptr = extTransRec.tr_devrec;
    if (devptr == NULL) return 0;   /* No matching devices, so forget it. */

    allow_globsubsnode = FALSE;

    /* Create a mask of all substrate types of all device records, and	*/
    /* search for substrate types on this combined mask.		*/

    TTMaskZero(&cmask);
    for (devtest = ExtCurStyle->exts_device[loctype]; devtest;
		devtest = devtest->exts_next)
	TTMaskSetMask(&cmask, &devtest->exts_deviceSubstrateTypes);

    if (!TTMaskIsZero(&cmask))
    {
	if (TTMaskHasType(&cmask, TT_SPACE))
	{
	    allow_globsubsnode = TRUE;
	    TTMaskClearType(&cmask, TT_SPACE);
	}

	if (extTransRec.tr_subsnode == (NodeRegion *)NULL)
	{
	    sublayer = TT_SPACE;
	    region = NULL;
	    extTransFindSubs(tile, loctype, &cmask, arg->fra_def, &region, &sublayer);

	    /* If the device does not connect to a defined node, and
	     * the substrate types include "space", then it is assumed to
	     * connect to the global substrate.
	     */

	    if (region == (NodeRegion *)NULL)
		if (allow_globsubsnode)
		    region = glob_subsnode;

	    extTransRec.tr_subsnode = region;

	    if ((region != (NodeRegion *)NULL) &&
		    !(TTMaskHasType(&devptr->exts_deviceSubstrateTypes, sublayer)))
	    {
		/* A substrate layer was found but is not compatible with the   */
		/* current device.  Find a device record with the substrate	*/
		/* layer that was found, and set the substrate match flag.	*/

		deventry = devptr;
		while (devptr != NULL)
		{
		    devptr = extDevFindMatch(devptr, loctype);
		    if ((devptr == NULL) || (devptr == deventry))
		    {
			TxError("No matching device for %s with substrate layer %s\n",
				DBTypeLongNameTbl[loctype], DBTypeLongNameTbl[sublayer]);
			devptr = NULL;
			break;
		    }
		    if (TTMaskHasType(&devptr->exts_deviceSubstrateTypes, sublayer))
		    {
			extTransRec.tr_devmatch |= MATCH_SUB;
			break;
		    }
		}
	    }
	    else if (region == (NodeRegion *)NULL)
	    {
		/* If ExtCurStyle->exts_globSubstrateTypes contains no types	*/
		/* then this is an older style techfile without a "substrate"	*/
		/* definition in the extract section.  In that case, it is	*/
		/* expected that the substrate name in the device line will be	*/
		/* used.							*/

		if (!TTMaskIsZero(&ExtCurStyle->exts_globSubstrateTypes) ||
			(devptr->exts_deviceSubstrateName == NULL))
		{
		    TxError("Device %s does not have a compatible substrate node!\n",
				DBTypeLongNameTbl[loctype]);
		    devptr = NULL;
		}
	    }
	}
	extTransRec.tr_devrec = devptr;
	if (devptr == NULL) return 0;	/* No matching devices, so forget it. */
    }

    /* If at least one device type declares an ID layer, then make a	*/
    /* mask of all device ID types, and search on the area of the	*/
    /* device to see if any device identifier layers are found.		*/

    TTMaskZero(&cmask);
    for (devtest = ExtCurStyle->exts_device[loctype]; devtest;
		devtest = devtest->exts_next)
	TTMaskSetMask(&cmask, &devtest->exts_deviceIdentifierTypes);

    if (!TTMaskIsZero(&cmask))
    {
	idlayer = TT_SPACE;
	extTransFindId(tile, &cmask, arg->fra_def, &idlayer);

	if ((idlayer == TT_SPACE) && !TTMaskIsZero(&devptr->exts_deviceIdentifierTypes))
	{
	    /* Device expected an ID layer but none was present.  Find a device	*/
	    /* record with no ID layer, and set the ID match flag.		*/

	    deventry = devptr;
	    while (devptr != NULL)
	    {
		devptr = extDevFindMatch(devptr, loctype);
		if ((devptr == NULL) || (devptr == deventry))
		{
		    TxError("No matching device for %s with no ID layer\n",
				DBTypeLongNameTbl[loctype]);
		    devptr = NULL;
		    break;
		}
		if (TTMaskIsZero(&devptr->exts_deviceIdentifierTypes))
		{
		    extTransRec.tr_devmatch |= MATCH_ID;
		    break;
		}
	    }
	}
	else if ((idlayer != TT_SPACE) &&
		    !TTMaskHasType(&devptr->exts_deviceIdentifierTypes, idlayer))
	{
	    /* Device expected no ID layer but one was present.	 Find a device	*/
	    /* record with the ID layer and set the ID match flag.  If there is	*/
	    /* a valid device without the ID layer, then ignore the ID layer	*/
	    /* and flag a warning.						*/

	    deventry = devptr;
	    while (devptr != NULL)
	    {
		devptr = extDevFindMatch(devptr, loctype);
		if ((devptr == NULL) || (devptr == deventry))
		{
		    TxError("ID layer %s on non-matching device %s was ignored.\n",
			    DBTypeLongNameTbl[idlayer], DBTypeLongNameTbl[loctype]);
		    devptr = deventry;
		    break;
		}
		if (TTMaskHasType(&devptr->exts_deviceIdentifierTypes, idlayer))
		{
		    extTransRec.tr_devmatch |= MATCH_ID;
		    break;
		}
	    }
	}
	else
	    extTransRec.tr_devmatch |= MATCH_ID;
    }
    extTransRec.tr_devrec = devptr;
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extAddSharedDevice --
 *
 *	Add a node region representing a device to the list of nodes
 *	kept in the structure passed to extTermAPFunc(), to keep track
 *	of how many devices share the same terminal area.
 *
 * ----------------------------------------------------------------------------
 */

void
extAddSharedDevice(eapd, node)
    ExtAreaPerimData *eapd;
    NodeRegion *node;
{
    ExtNodeList *nl, *newnl;

    for (nl = eapd->eapd_shared; nl; nl = nl->nl_next)
	if (nl->nl_node == node) break;

    if (nl == NULL)
    {
	newnl = (ExtNodeList *)mallocMagic(sizeof(ExtNodeList));
	newnl->nl_node = node;
	newnl->nl_next = eapd->eapd_shared;
	eapd->eapd_shared = newnl;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * extTermAPFunc --
 *
 *	Callback function used by extTransPerimFunc() to find the largest
 *	area encompassing a device terminal.  This is the bounding box of
 *	the area containing terminal types connected to a device tile.
 *
 *	This routine is redundant with the area and perimeter calculations
 *	in extFindNodes(), but that routine traverses an entire net.  This
 *	routine finds the area and perimeter belonging to material on a
 *	single plane extending from a device (e.g., diffusion and contacts
 *	on a FET source or drain).
 *
 *	Note that this definition is not necessarily accurate for defining
 *	terminal area and perimeter, as the area of terminal types may not
 *	be rectangular, making an approximation using length and width
 *	inappropriate.
 *
 * ----------------------------------------------------------------------------
 */

int
extTermAPFunc(tile, pNum, eapd)
    Tile *tile;		/* Tile extending a device terminal */
    int   pNum;		/* Plane of tile (unused, set to -1) */
    ExtAreaPerimData *eapd;	/* Area and perimeter totals for terminal */
{
    TileType type;
    Tile *tp;
    Rect r;

    TiToRect(tile, &r);
    eapd->eapd_area += (r.r_xtop - r.r_xbot) * (r.r_ytop - r.r_ybot);

    /* Diagonal */
    if (IsSplit(tile))
    {
	int w, h, l;
	type = (SplitSide(tile)) ? SplitLeftType(tile): SplitRightType(tile);
	w = RIGHT(tile) - LEFT(tile);
	h = TOP(tile) - BOTTOM(tile);
	l = w * w + h * h;
	eapd->eapd_perim += (int)sqrt((double)l);
    }

    /* Top */
    for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp = BL(tp))
    {
	type = TiGetBottomType(tp);
	if (TTMaskHasType(&eapd->eapd_mask, type))
	{
	    eapd->eapd_perim += MIN(RIGHT(tile), RIGHT(tp)) -
			MAX(LEFT(tile), LEFT(tp));
	    if (TTMaskHasType(eapd->eapd_gatemask, type))
		if (TiGetClientPTR(tp) != eapd->eapd_gatenode)
		    extAddSharedDevice(eapd, (NodeRegion *)TiGetClientPTR(tp));
	}
    }

    /* Bottom */
    for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp = TR(tp))
    {
	type = TiGetTopType(tp);
	if (TTMaskHasType(&eapd->eapd_mask, type))
	{
	    eapd->eapd_perim += MIN(RIGHT(tile), RIGHT(tp)) -
			MAX(LEFT(tile), LEFT(tp));
	    if (TTMaskHasType(eapd->eapd_gatemask, type))
		if (TiGetClientPTR(tp) != eapd->eapd_gatenode)
		    extAddSharedDevice(eapd, (NodeRegion *)TiGetClientPTR(tp));
	}
    }

    /* Left */
    for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
    {
	type = TiGetRightType(tp);
	if (TTMaskHasType(&eapd->eapd_mask, type))
	{
	    eapd->eapd_perim += MIN(TOP(tile), TOP(tp)) -
			MAX(BOTTOM(tile), BOTTOM(tp));
	    if (TTMaskHasType(eapd->eapd_gatemask, type))
		if (TiGetClientPTR(tp) != eapd->eapd_gatenode)
		    extAddSharedDevice(eapd, (NodeRegion *)TiGetClientPTR(tp));
	}
    }

    /* Right */
    for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp = LB(tp))
    {
	type = TiGetLeftType(tp);
	if (TTMaskHasType(&eapd->eapd_mask, type)) 
	{
	    eapd->eapd_perim += MIN(TOP(tile), TOP(tp)) -
			MAX(BOTTOM(tile), BOTTOM(tp));
	    if (TTMaskHasType(eapd->eapd_gatemask, type))
		if (TiGetClientPTR(tp) != eapd->eapd_gatenode)
		    extAddSharedDevice(eapd, (NodeRegion *)TiGetClientPTR(tp));
	}
    }

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extTransPerimFunc --
 *
 *	Callback function for exploring the perimeter of a device to find
 *	areas connected to the device (e.g., gate) and areas adjacent but
 *	not connected (e.g., source and drain).
 *
 * ----------------------------------------------------------------------------
 */

int
extTransPerimFunc(bp)
    Boundary *bp;
{
    TileType tinside, toutside;
    Tile *tile;
    NodeRegion *diffNode = (NodeRegion *) extGetRegion(bp->b_outside);
    ExtDevice *devptr, *deventry;
    int i, area, perim, len = BoundaryLength(bp);
    int thisterm;
    LabelList *ll;
    Label *lab;
    bool SDterm = FALSE;

    /* NOTE:  The tile side bit is not guaranteed to be correct
     * when entering this routine.  For split tiles, determine
     * the correct type (type on left, right, bottom, or top)
     * based on the recorded boundary direction.
     */

    tile = bp->b_inside;
    if (IsSplit(tile))
    {
	switch (bp->b_direction)
	{
	    case BD_LEFT:
		tinside = TiGetLeftType(tile);
		break;
	    case BD_TOP:
		tinside = TiGetTopType(tile);
		break;
	    case BD_RIGHT:
		tinside = TiGetRightType(tile);
		break;
	    case BD_BOTTOM:
		tinside = TiGetBottomType(tile);
		break;
	}
    }
    else
        tinside = TiGetTypeExact(bp->b_inside);
    tile = bp->b_outside;
    if (IsSplit(tile))
    {
	switch (bp->b_direction)
	{
	    case BD_LEFT:
		toutside = TiGetRightType(tile);
		break;
	    case BD_TOP:
		toutside = TiGetBottomType(tile);
		break;
	    case BD_RIGHT:
		toutside = TiGetLeftType(tile);
		break;
	    case BD_BOTTOM:
		toutside = TiGetTopType(tile);
		break;
	}
    }
    else
        toutside = TiGetTypeExact(bp->b_outside);

    if (extTransRec.tr_devrec != NULL)
	devptr = extTransRec.tr_devrec;
    else
	devptr = ExtCurStyle->exts_device[tinside];

    deventry = devptr;
    while(devptr)
    {
	extTransRec.tr_devrec = devptr;
	for (i = 0; !TTMaskIsZero(&devptr->exts_deviceSDTypes[i]); i++)
	{
	    /* TT_SPACE is allowed, for declaring that a device terminal is	*/
	    /* the substrate.  However, it should not be in the plane of	*/
	    /* the device identifier layer, so space tiles should never be	*/
	    /* flagged during a device perimeter search.			*/

	    if (toutside == TT_SPACE) break;

	    if (TTMaskHasType(&devptr->exts_deviceSDTypes[i], toutside))
	    {
		/*
		 * It's a diffusion terminal (source or drain).  See if the node is
		 * already in our table; add it if it wasn't already there.
		 * Asymmetric devices must have terminals in order.
		 */
		if (TTMaskIsZero(&devptr->exts_deviceSDTypes[1]))
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
		    extTransRec.tr_termarea[thisterm] = 0;
		    extTransRec.tr_termperim[thisterm] = 0;
		    extTransRec.tr_termshared[thisterm] = 0;
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

		    if (DBPlane(TiGetType(otile)) < pos->pnum)
		    {
			pos->pnum = DBPlane(TiGetType(otile));
			pos->pt = otile->ti_ll;
		    }
		    else if (DBPlane(TiGetType(otile)) == pos->pnum)
		    {
			if (LEFT(otile) < pos->pt.p_x)
			    pos->pt = otile->ti_ll;
			else if (LEFT(otile) == pos->pt.p_x &&
				BOTTOM(otile) < pos->pt.p_y)
			    pos->pt.p_y = BOTTOM(otile);
		    }
		}
		else
		    /* Do not print error messages here, as errors may
		     * be resolved when checking the next device entry.
		     */
		    break;

		/* Add the length to this terminal's perimeter */
		extTransRec.tr_termlen[thisterm] += len;

		if (extTransRec.tr_termarea[thisterm] == 0)
		{
		    /* Find the area and perimeter of the terminal area (connected
		     * area outside the boundary on a single plane).  Note that
		     * this does not consider terminal area outside of the cell
		     * or how area or perimeter may be shared or overlap between
		     * devices.
		     */

		    ExtAreaPerimData eapd;
		    int shared;

		    eapd.eapd_area = eapd.eapd_perim = 0;
		    TTMaskCom2(&eapd.eapd_mask, &DBConnectTbl[toutside]);
		    eapd.eapd_gatemask = &ExtCurStyle->exts_deviceMask;
		    eapd.eapd_gatenode = (NodeRegion *)extGetRegion(bp->b_inside);
		    eapd.eapd_shared = NULL;

		    DBSrConnectOnePlane(bp->b_outside, DBConnectTbl,
					extTermAPFunc, (ClientData)&eapd);

		    shared = 1;
		    while (eapd.eapd_shared)
		    {
			shared++;
			freeMagic(eapd.eapd_shared);
			eapd.eapd_shared = eapd.eapd_shared->nl_next;
		    }

		    extTransRec.tr_termarea[thisterm] = eapd.eapd_area;
		    extTransRec.tr_termperim[thisterm] = eapd.eapd_perim;
		    extTransRec.tr_termshared[thisterm] = shared;
		}

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

		/* Check if number of terminals exceeds the number allowed in	*/
		/* this device record.  If so, check if there is another device	*/
		/* record with a different number of terminals.			*/

		extTransRec.tr_devmatch |= (MATCH_TERM << thisterm);
		if (thisterm >= devptr->exts_deviceSDCount)
		{
		    devptr = extDevFindMatch(devptr, tinside);

		    /* Should this be an error instead of a warning?		*/
		    /* Traditionally more terminals than defined was allowed	*/
		    /* but not necessarily handled correctly by ext2spice.	*/

		    if (devptr == deventry)
			TxError("Warning:  Device has more terminals than defined "
				"for type!\n");
		    else
			extTransRec.tr_devrec = devptr;
		}
		SDterm = TRUE;
		break;
	    }
	}
	if (toutside == TT_SPACE) break;
	if (SDterm) break;
	if (extConnectsTo(tinside, toutside, ExtCurStyle->exts_nodeConn))
	{
	    /* Not in a terminal, but are in something that connects to gate */
	    extTransRec.tr_gatelen += len;
	    break;
	}

	/* Did not find a matching terminal, so see if a different extraction	*/
	/* record matches the terminal type.					*/

	devptr = extDevFindMatch(devptr, tinside);
	if (devptr == deventry) devptr = NULL;
    }

    if (devptr == NULL)
    {
	/* Outside type is not a terminal, so return to the original	*/
	/* device record.  NOTE:  Should probably check if this device	*/
	/* type is a FET, as being here would indicate an error.	*/
	/* However, failure to find all terminals will be flagged as an	*/
	/* error elsewhere.						*/

	extTransRec.tr_devrec = deventry;
    }

    /*
     * Total perimeter (separate from terminals, for dcaps
     * that might not be surrounded by terminals on all sides).
     */

    /* Don't double-count contact perimeters (added by Tim 1/9/07)	*/
    /* 8/30/2022:  The code at line 681 can reassign a transistor	*/
    /* gate node off of the device plane, so the original plane of	*/
    /* the gate node is saved in extTransRec.tr_plane and used here.	*/
    /* Do *not* user extTransRec.tr_gatenode->nreg_pnum!		*/

    if ((!DBIsContact(toutside) && !DBIsContact(tinside)) ||
		(bp->b_plane == extTransRec.tr_plane))
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

    mask = ExtCurStyle->exts_deviceConn[loctype];
    TTMaskCom(&mask);
    extEnumTilePerim(tile, &mask, pNum, extSpecialPerimFunc, (ClientData) TRUE);
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
    ExtDevice *devptr;

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

    mask = ExtCurStyle->exts_deviceConn[loctype];

    devptr = extTransRec.tr_devrec;
    if (devptr == NULL) devptr = ExtCurStyle->exts_device[loctype];

    while (devptr)
    {
	TTMaskSetMask(&mask, &devptr->exts_deviceSDTypes[0]);
	TTMaskCom(&mask);

	extEnumTilePerim(tile, &mask, pNum, extSpecialPerimFunc, (ClientData)FALSE);

	if (extSpecialBounds[0] != NULL) break;
	devptr = devptr->exts_next;
    }
    if (devptr != NULL) extTransRec.tr_devrec = devptr;

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
    ExtDevice *devptr;
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

    /* Required to use the same device record that was used to find */
    /* the terminals.						    */
    if ((devptr = extTransRec.tr_devrec) == NULL) return 0;

    /* Check all terminal classes for a matching type */
    needSurvey = FALSE;
    for (i = 0; !TTMaskIsZero(&devptr->exts_deviceSDTypes[i]); i++)
    {
	if (TTMaskHasType(&devptr->exts_deviceSDTypes[i], toutside))
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
    }

    /* Check for terminal on different plane than the device */
    if (!needSurvey)
    {
	for (i = 0; !TTMaskIsZero(&devptr->exts_deviceSDTypes[i]); i++)
	{
	    TileTypeBitMask mmask;
	    PlaneMask pmask;

	    mmask = devptr->exts_deviceSDTypes[i];
	    if (toutside != TT_SPACE) TTMaskClearType(&mmask, TT_SPACE);
	    pmask = DBTechTypesToPlanes(&mmask);

	    if (!PlaneMaskHasPlane(pmask, DBPlane(tinside)))
	    {
		diffNode = extTransRec.tr_termnode[i];
		needSurvey = TRUE;
		break;
	    }
	}
    }

    if (!sense || needSurvey)
    {
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
		/* This is not necessarily an error;  e.g., happens for	*/
		/* a device like a diode with TT_SPACE in the source/	*/
		/* drain list.						*/
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
extTransOutTerminal(lreg, ll, whichTerm, len, area, perim, shared, outFile)
    LabRegion *lreg;		/* Node connected to terminal */
    LabelList *ll;	/* Gate's label list */
    int whichTerm;		/* Which terminal we are processing.  The gate
				 * is indicated by LL_GATEATTR.
				 */
    int len;			/* Length of perimeter along terminal */
    int area;			/* Total area of terminal */
    int perim;			/* Total perimeter of terminal (includes len) */
    int shared;			/* Number of devices sharing the terminal */
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

    /* NOTE:  The area and perimeter of a terminal are divided equally
     * among devices that share the same terminal area.  This may not
     * necessarily be the best way to handle shared terminals;  in
     * particular, it is preferable to detect and output fingered
     * devices separately.
     */

    if ((whichTerm != LL_GATEATTR) && (area != 0) && (perim != 0))
	fprintf(outFile, "%c%d,%d", fmt, (area / shared), (perim / shared));
    else if (fmt == ' ')
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

    tp = PlaneGetHint(myplane);
    GOTOPOINT(tp, &np->nreg_ll);
    PlaneSetHint(myplane, tp);

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
	else
	{
	    type = SplitSide(tile) ? SplitRightType(tile) : SplitLeftType(tile);
	    /* (Are these type checks necessary?) */
	    if ((type == TT_SPACE) || !TTMaskHasType(&DBPlaneTypes[plane], type))
	    	type = SplitSide(tile) ? SplitLeftType(tile) : SplitRightType(tile);
	    if ((type == TT_SPACE) || !TTMaskHasType(&DBPlaneTypes[plane], type))
		return;
	}
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

ExtRegion *
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
    arg->fra_region = (ExtRegion *) reg;
    return ((ExtRegion *) reg);
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
    int extSubsFunc2();
    FindRegion arg;
    int pNum, n;
    TileTypeBitMask subsTypesNonSpace;
    bool space_is_substrate;
    bool isabstract;

    /* Reset perimeter and area prior to node extraction */
    for (n = 0; n < ExtCurStyle->exts_numResistClasses; n++)
	extResistArea[n] = extResistPerim[n] = 0;

    extNodeClipArea = clipArea;
    if (extNodeStack == (Stack *) NULL)
	extNodeStack = StackNew(64);

    arg.fra_def = def;
    arg.fra_region = (ExtRegion *) NULL;

    SigDisableInterrupts();

    temp_subsnode = (NodeRegion *)NULL;		// Reset for new search
    isabstract = FALSE;

    // Do not treat abstract cells differently per the substrate.
    /* DBPropGet(def, "LEFview", &isabstract); */

    if (!isabstract)
    {
	/* First pass:  Find substrate.  Collect all tiles belonging	*/
	/* to the substrate and push them onto the stack.  Then	 	*/
	/* call extNodeAreaFunc() on the first of these to generate	*/
	/* a single substrate node.					*/

	/* Refinement:  Split search into two parts, one on the		*/
	/* globSubstratePlane and one on all other planes.  ONLY	*/
	/* search other planes if TT_SPACE is in the list of		*/
	/* substrate types, and then only consider those types to	*/
	/* be part of the substrate node if they have only space	*/
	/* below them on the globSubstratePlane.  This method lets	*/
	/* a single type like "psd" operate on, for example, both	*/
	/* the substrate and an isolated pwell, without implicitly	*/
	/* connecting the isolated pwell to the substrate.		*/


	if (TTMaskHasType(&ExtCurStyle->exts_globSubstrateTypes, TT_SPACE))
	    space_is_substrate = TRUE;
	else
	    space_is_substrate = FALSE;

	TTMaskZero(&subsTypesNonSpace);
	TTMaskSetMask(&subsTypesNonSpace, &ExtCurStyle->exts_globSubstrateTypes);
	TTMaskClearType(&subsTypesNonSpace, TT_SPACE);

	/* If the default substrate type is set, it is used *only* for	*/
	/* isolated substrate regions and does not mark the default	*/
	/* substrate, so remove it from the list of substrate types.	*/

	if (ExtCurStyle->exts_globSubstrateDefaultType != -1)
	    TTMaskClearType(&subsTypesNonSpace,
			ExtCurStyle->exts_globSubstrateDefaultType);

	pNum = ExtCurStyle->exts_globSubstratePlane;
	/* Does the type set of this plane intersect the substrate types? */
	if (TTMaskIntersect(&DBPlaneTypes[pNum], &subsTypesNonSpace))
    	{
	    arg.fra_pNum = pNum;
	    DBSrPaintClient((Tile *) NULL, def->cd_planes[pNum],
			&TiPlaneRect, &subsTypesNonSpace, extUnInit,
			extSubsFunc, (ClientData) &arg);
        }

	for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	{
	    if (pNum == ExtCurStyle->exts_globSubstratePlane) continue;

	    /* Does the type set of this plane intersect the substrate types? */

	    if (TTMaskIntersect(&DBPlaneTypes[pNum], &subsTypesNonSpace))
	    {
	        arg.fra_pNum = pNum;
	        if (space_is_substrate)
		    DBSrPaintClient((Tile *) NULL, def->cd_planes[pNum],
				&TiPlaneRect, &subsTypesNonSpace, extUnInit,
				extSubsFunc2, (ClientData) &arg);
	    	else
		    DBSrPaintClient((Tile *) NULL, def->cd_planes[pNum],
				&TiPlaneRect, &subsTypesNonSpace, extUnInit,
				extSubsFunc, (ClientData) &arg);
	    }
    	}

	/* If there was a substrate connection, process it and		*/
	/* everything that was connected to it.  If not, then create a	*/
	/* new node to represent the substrate.				*/

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
    int pNum;
    Rect tileArea;
    TileType type;
    TileTypeBitMask *smask;
    int extSubsFunc3();

    if (IsSplit(tile))
    {
	type = (SplitSide(tile)) ? SplitRightType(tile) : SplitLeftType(tile);
	if (type == TT_SPACE) return 0;		/* Should not happen */
    }

    /* Run second search in the area of the tile on the substrate plane	*/
    /* to make sure that no shield types are covering this tile.	*/

    TiToRect(tile, &tileArea);
    smask = &ExtCurStyle->exts_globSubstrateShieldTypes;
    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	if (TTMaskIntersect(&DBPlaneTypes[pNum], smask))
	    if (DBSrPaintArea((Tile *) NULL, arg->fra_def->cd_planes[pNum],
			&tileArea, smask, extSubsFunc3, (ClientData)NULL) != 0)
		return (0);

    /* Mark this tile as pending and push it */
    PUSHTILE(tile, arg->fra_pNum);

    /* That's all we do */
    return (0);
}

int
extSubsFunc2(tile, arg)
    Tile *tile;
    FindRegion *arg;
{
    int pNum;
    Rect tileArea;
    TileTypeBitMask *smask;
    int extSubsFunc3();

    TiToRect(tile, &tileArea);

    /* Run second search in the area of the tile on the substrate plane	*/
    /* to make sure that no shield types are covering this tile.	*/

    smask = &ExtCurStyle->exts_globSubstrateShieldTypes;
    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	if (TTMaskIntersect(&DBPlaneTypes[pNum], smask))
	    if (DBSrPaintArea((Tile *) NULL, arg->fra_def->cd_planes[pNum],
			&tileArea, smask, extSubsFunc3, (ClientData)NULL) != 0)
		/* Keep the search going, as there may be other tiles to check */
		return (0);

    /* Run third search in the area of the tile on the substrate plane	*/
    /* to make sure that nothing but space is under these tiles.	*/

    pNum = ExtCurStyle->exts_globSubstratePlane;

    if (DBSrPaintArea((Tile *) NULL, arg->fra_def->cd_planes[pNum],
		&tileArea, &DBAllButSpaceBits,
		extSubsFunc3, (ClientData)NULL) == 0)
    {
	/* Mark this tile as pending and push it */
	PUSHTILE(tile, arg->fra_pNum);
    }
    return (0);
}

int
extSubsFunc3(tile)
    Tile *tile;
{
    /* Stops the search because something that was not space was found */
    return 1;
}

int
extNodeAreaFunc(tile, arg)
    Tile *tile;
    FindRegion *arg;
{
    int tilePlaneNum, pNum, len, resistClass, n, nclasses;
    dlong area;
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
    if ((old = (NodeRegion *) arg->fra_region))
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
    arg->fra_region = (ExtRegion *) reg;

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
	if (TiGetClientPTR(tile) == reg)
	    continue;
	TiSetClientPTR(tile, reg);
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

	    /* Check if the tile is outside the clip area */
	    if (GEO_OVERLAP(&r, extNodeClipArea))
	    {
	    	GEOCLIP(&r, extNodeClipArea);
		area = (dlong)(r.r_xtop - r.r_xbot) * (dlong)(r.r_ytop - r.r_ybot);
	    }
	    else
		area = (dlong)0;
	}
	else area = (dlong)(TOP(tile) - BOTTOM(tile)) * (dlong)(RIGHT(tile) - LEFT(tile));

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
	    ClientData ticlient = TiGetClient(tp);
            if (IsSplit(tp))
	    {
        	t = SplitBottomType(tp);
		if (ticlient == extUnInit && TTMaskHasType(mask, t))
		{
		    PUSHTILEBOTTOM(tp, tilePlaneNum);
		}
		else if ((NodeRegion *)CD2PTR(ticlient) != reg && TTMaskHasType(mask, t))
		{
		    /* Count split tile twice, once for each node it belongs to. */
		    TiSetClient(tp, extUnInit);
		    PUSHTILEBOTTOM(tp, tilePlaneNum);
		}
	    }
            else
	    {
		t = TiGetTypeExact(tp);
		if (ticlient == extUnInit && TTMaskHasType(mask, t))
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
	    ClientData ticlient = TiGetClient(tp);
            if (IsSplit(tp))
	    {
                t = SplitRightType(tp);
		if (ticlient == extUnInit && TTMaskHasType(mask, t))
		{
		    PUSHTILERIGHT(tp, tilePlaneNum);
		}
		else if ((NodeRegion *)CD2PTR(ticlient) != reg && TTMaskHasType(mask, t))
		{
		    /* Count split tile twice, once for each node it belongs to. */
		    TiSetClient(tp, extUnInit);
		    PUSHTILERIGHT(tp, tilePlaneNum);
		}
	    }
            else
	    {
		t = TiGetTypeExact(tp);
		if (ticlient == extUnInit && TTMaskHasType(mask, t))
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
	    ClientData ticlient = TiGetClient(tp);
            if (IsSplit(tp))
	    {
                t = SplitTopType(tp);
		if (ticlient == extUnInit && TTMaskHasType(mask, t))
		{
		    PUSHTILETOP(tp, tilePlaneNum);
		}
		else if ((NodeRegion *)CD2PTR(ticlient) != reg && TTMaskHasType(mask, t))
		{
		    /* Count split tile twice, once for each node it belongs to. */
		    TiSetClient(tp, extUnInit);
		    PUSHTILETOP(tp, tilePlaneNum);
		}
	    }
            else
	    {
		t = TiGetTypeExact(tp);
		if (ticlient == extUnInit && TTMaskHasType(mask, t))
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
	    ClientData ticlient = TiGetClient(tp);
            if (IsSplit(tp))
	    {
                t = SplitLeftType(tp);
		if (ticlient == extUnInit && TTMaskHasType(mask, t))
		{
		    PUSHTILELEFT(tp, tilePlaneNum);
		}
		else if ((NodeRegion *)CD2PTR(ticlient) != reg && TTMaskHasType(mask, t))
		{
		    /* Count split tile twice, once for each node it belongs to	*/
		    TiSetClient(tp, extUnInit);
		    PUSHTILELEFT(tp, tilePlaneNum);
		}
	    }
            else
	    {
		t = TiGetTypeExact(tp);
		if (ticlient == extUnInit && TTMaskHasType(mask, t))
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

		    tp = PlaneGetHint(plane);
		    GOTOPOINT(tp, &tile->ti_ll);
		    PlaneSetHint(plane, tp);

		    if (TiGetClient(tp) != extUnInit) continue;

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
	if ((pMask = DBAllConnPlanes[type]))
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
    while ((he = HashNext(ht, &hs)))
    {
	if (HashGetValue(he) != NULL)
	{
	    freeMagic(HashGetValue(he));  /* Free a malloc'ed CapValue */
	    HashSetValue(he, (ClientData) NULL);
	}
    }
    HashKill(ht);
}
