/*
 * EFantenna.c --
 *
 * Program to flatten hierarchical .ext files and then execute an
 * antenna violation check for every MOSFET device in the flattened
 * design.
 *
 * Flattens the tree rooted at file.ext, reading in additional .ext
 * files as specified by "use" lines in file.ext.
 *
 */

#include <stdio.h>
#include <stdlib.h>	/* for atof() */
#include <string.h>
#include <ctype.h>
#include <math.h>	/* for INFINITY */

#ifdef MAGIC_WRAPPER
#include "tcltk/tclmagic.h"
#endif
#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/hash.h"
#include "utils/utils.h"
#include "utils/styles.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "windows/windows.h"
#include "textio/textio.h"
#include "dbwind/dbwind.h"
#include "textio/txcommands.h"
#include "extflat/extflat.h"
#include "extflat/EFint.h"
#include "extract/extract.h"
#include "extract/extractInt.h"
#include "select/select.h"
#include "utils/malloc.h"
#include "cif/cif.h"		/* For CIFGetContactSize() */
#include "cif/CIFint.h"		/* For CIFCurStyle */

/* C99 compat */
#include "extract/extract.h"

/* Forward declarations */
int antennacheckArgs();
int antennacheckVisit();

typedef struct {
	TileTypeBitMask visitMask;
} nodeClient;

typedef struct {
	HierName *lastPrefix;
	TileTypeBitMask visitMask;
} nodeClientHier;

#define NO_RESCLASS	-1

#define markVisited(client, rclass) \
  { TTMaskSetType(&((client)->visitMask), rclass); }

#define clearVisited(client) \
   { TTMaskZero(&((client)->visitMask); }

#define beenVisited(client, rclass)  \
  ( TTMaskHasType(&((client)->visitMask), rclass) )

#define initNodeClient(node) \
{ \
	(node)->efnode_client = (ClientData) mallocMagic((unsigned) (sizeof(nodeClient))); \
	TTMaskZero(&(( nodeClient *)(node)->efnode_client)->visitMask); \
}


#define initNodeClientHier(node) \
{ \
	(node)->efnode_client = (ClientData) mallocMagic((unsigned) (sizeof(nodeClientHier))); \
	TTMaskZero(&(( nodeClientHier *)(node)->efnode_client)->visitMask); \
}

/* Diagnostic */
int efGates;
static int efAntennaDebug = FALSE;

/* The extract file is designed to be independent of the magic database,    */
/* but that means that the device types do not match magic database types.  */
/* A lookup table is needed to cross-reference the device types.	    */

TileType *EFDeviceTypes;

typedef struct _aas {
    dlong *accum;	/* Pointer to array of accumulated areas per type */
    int pNum;		/* Plane of check */
    Rect r;		/* Holds any one visited rectangle */
    Rect via;		/* Holds any one visitid via rectangle */
    CellDef *def;	/* CellDef for adding feedback */
} AntennaAccumStruct;

typedef struct _gdas {
    dlong accum;	/* Accumulated area of all gates/diff */
    int pNum;		/* Plane of check */
    Rect r;		/* Holds any one visited rectangle */
    CellDef *def;	/* CellDef for adding feedback */
} GateDiffAccumStruct;

typedef struct _ams {
    int pNum;		/* Plane of check */
    CellDef *def;	/* CellDef for adding feedback */
} AntennaMarkStruct;


/*
 * ----------------------------------------------------------------------------
 *
 * Main Tcl callback for command "magic::antennacheck"
 *
 * ----------------------------------------------------------------------------
 */

#define ANTENNACHECK_RUN     0
#define ANTENNACHECK_DEBUG   1
#define ANTENNACHECK_HELP    2

void
CmdAntennaCheck(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int i, flatFlags;
    char *inName;
    FILE *f;
    TileType t;

    int option = ANTENNACHECK_RUN;
    int value;
    int argc = cmd->tx_argc;
    char **argv = cmd->tx_argv;
    char **msg;
    bool err_result;

    short sd_rclass;
    short sub_rclass;
    char *devname;
    int idx;

    CellUse *editUse;

    static char *cmdAntennaCheckOption[] = {
	"[run] [options]	run antennacheck on current cell\n"
	"			use \"run -help\" to get standard options",
	"debug			print detailed information about each error",
        "help			print help information",
	NULL
    };

    if (cmd->tx_argc > 1)
    {
	option = Lookup(cmd->tx_argv[1], cmdAntennaCheckOption);
	if (option < 0) option = ANTENNACHECK_RUN;
	else argv++;
    }

    switch (option)
    {
	case ANTENNACHECK_RUN:
	    goto runantennacheck;
	    break;
	case ANTENNACHECK_DEBUG:
	    efAntennaDebug = TRUE;
	    break;
	case ANTENNACHECK_HELP:
usage:
	    for (msg = &(cmdAntennaCheckOption[0]); *msg != NULL; msg++)
	    {
		TxPrintf("    %s\n", *msg);
	    }
	    break;
    }
    return;

runantennacheck:

    if (ExtCurStyle->exts_planeOrderStatus == noPlaneOrder)
    {
	TxError("No planeorder specified for this process:  "
		    "Cannot run antenna checks!\n");
	return;
    }

    EFInit();
    EFCapThreshold = INFINITY;
    EFResistThreshold = INFINITY;

    /* Process command line arguments */
    inName = EFArgs(argc, argv, &err_result, antennacheckArgs, (ClientData) NULL);

    if (err_result == TRUE)
    {
	EFDone(NULL);
        return /* TCL_ERROR */;
    }

    if (inName == NULL)
    {
	/* Assume that we want to do exttospice on the currently loaded cell */

	if (w == (MagWindow *) NULL)
	    windCheckOnlyWindow(&w, DBWclientID);

	if (w == (MagWindow *) NULL)
	{
	    TxError("Point to a window or specify a cell name.\n");
	    EFDone(NULL);
	    return /* TCL_ERROR */;
	}
	inName = ((CellUse *) w->w_surfaceID)->cu_def->cd_name;
    }
    editUse = (CellUse *)w->w_surfaceID;

    /*
     * Initializations specific to this program.
     */

    /* Read the hierarchical description of the input circuit */
    TxPrintf("Reading extract file.\n");
    if (EFReadFile(inName, FALSE, FALSE, FALSE, FALSE) == FALSE)
    {
	EFDone(NULL);
	return /* TCL_ERROR */;
    }

    /* Convert the hierarchical description to a flat one */
    flatFlags = EF_FLATNODES | EF_WARNABSTRACT;
    TxPrintf("Building flattened netlist.\n");
    EFFlatBuild(inName, flatFlags);

    /* Get device information from the current extraction style */
    idx = 0;
    while (ExtGetDevInfo(idx++, &devname, NULL, NULL, NULL, NULL, NULL))
    {
        if (idx == TT_MAXTYPES)
        {
            TxError("Error:  Ran out of space for device types!\n");
            break;
        }
        efBuildAddStr(EFDevTypes, &EFDevNumTypes, TT_MAXTYPES, devname);
    }

    /* Build device lookup table */
    EFDeviceTypes = (TileType *)mallocMagic(EFDevNumTypes * sizeof(TileType));
    for (i = 0; i < EFDevNumTypes; i++)
	if (EFDevTypes[i])
	    EFDeviceTypes[i] = extGetDevType(EFDevTypes[i]);

    efGates = 0;
    TxPrintf("Running antenna checks.\n");
    EFVisitDevs(antennacheckVisit, (ClientData)editUse);
    EFFlatDone(NULL);
    EFDone(NULL);

    TxPrintf("antennacheck finished.\n");
    freeMagic(EFDeviceTypes);
    efAntennaDebug = FALSE;
}


/*
 * ----------------------------------------------------------------------------
 *
 * antennacheckArgs --
 *
 * Process those arguments that are specific to antennacheck.
 * Assumes that *pargv[0][0] is '-', indicating a flag
 * argument.
 *
 * Results:
 *	None.  TCL version returns False if an error is encountered
 *	while parsing arguments, True otherwise.
 *
 * Side effects:
 *	After processing an argument, updates *pargc and *pargv
 *	to point to after the argument.
 *
 *	May initialize various global variables based on the
 *	arguments given to us.
 *
 *	Exits in the event of an improper argument.
 *
 * ----------------------------------------------------------------------------
 */

int
antennacheckArgs(pargc, pargv)
    int *pargc;
    char ***pargv;
{
    char **argv = *pargv, *cp;
    int argc = *pargc;

    switch (argv[0][1])
    {
	default:
	    TxError("Unrecognized flag: %s\n", argv[0]);
	    goto usage;
    }

    *pargv = argv;
    *pargc = argc;
    return 0;

usage:
    TxError("Usage: antennacheck\n");
    return 1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * AntennaGetNode --
 *
 * function to find a node given its hierarchical prefix and suffix
 *
 * Results:
 *  a pointer to the node struct or NULL
 *
 * ----------------------------------------------------------------------------
 */
EFNode *
AntennaGetNode(prefix, suffix)
HierName *prefix;
HierName *suffix;
{
        HashEntry *he;

        he = EFHNConcatLook(prefix, suffix, "output");
        return(((EFNodeName *) HashGetValue(he))->efnn_node);
}

/*
 * ----------------------------------------------------------------------------
 *
 * antennacheckVisit --
 *
 * Procedure to check for antenna violations from a single device.
 * Called by EFVisitDevs().
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	May tag other device records to avoid double-counting devices.
 *	Generates feedback entries if an antenna violation is found.
 *
 * ----------------------------------------------------------------------------
 */

int
antennacheckVisit(dev, hc, scale, trans, editUse)
    Dev *dev;		/* Device being output */
    HierContext *hc;	/* Hierarchical context down to this device */
    float scale;	/* Scale transform for output */
    Transform *trans;	/* Coordinate transform */
    CellUse *editUse;	/* ClientData is edit cell use */
{
    DevTerm *gate;
    TileType t, conType;
    int pos, pNum, pNum2, pmax, p, i, j, total;
    dlong gatearea, diffarea;
    double anttotal, conttotal;
    float saveRatio, ratioTotal;
    dlong *antennaarea;
    Rect r, gaterect;
    EFNode *gnode;
    SearchContext scx;
    TileTypeBitMask gatemask, saveConMask;
    bool antennaError;
    HierName *hierName = hc->hc_hierName;

    extern CellDef *extPathDef;	    /* see extract/ExtLength.c */
    extern CellUse *extPathUse;	    /* see extract/ExtLength.c */

    extern int  areaAccumFunc(), antennaAccumFunc(), areaMarkFunc();

    antennaarea = (dlong *)mallocMagic(DBNumTypes * sizeof(dlong));

    switch(dev->dev_class)
    {
	case DEV_FET:
	case DEV_MOSFET:
	case DEV_MSUBCKT:
	case DEV_ASYMMETRIC:

	    /* Procedure:
	     *
	     * 1.  If device gate node is marked visited, return.
	     * 2.  Mark device gate node visited
	     * 3.  For each plane from metal1 up (determined by planeorder):
	     *   a.  Run DBTreeCopyConnect()
	     *   b.  Accumulate gate area of connected devices
	     *   c.  Accumulate diffusion area of connected devices
	     *	 d.  Accumulate	metal area of connected devices
	     *	 e.  Check against antenna ratio(s)
	     *	 f.  Generate feedback if in violation of antenna rule
	     *
	     * NOTE: DBTreeCopyConnect() is used cumulatively, so that
	     * additional searching only needs to be done for the additional
	     * layer being searched.
	     */

	    GeoTransRect(trans, &dev->dev_rect, &r);
	    gate = &dev->dev_terms[0];
	    gnode = AntennaGetNode(hierName, gate->dterm_node->efnode_name->efnn_hier);
	    if (gnode->efnode_client == (ClientData) NULL)
                initNodeClient(gnode);
	    if (beenVisited((nodeClient *)gnode->efnode_client, 0))
		return 0;
	    else
		markVisited((nodeClient *)gnode->efnode_client, 0);

	    /* Diagnostic stuff */
	    efGates++;
	    if (efGates % 100 == 0) TxPrintf("   %d gates analyzed.\n", efGates);

	    /* Diagnostic for debugging */
	    /*
	    TxPrintf("Gate %d : (%d %d) to (%d %d) net %s\n", efGates, r.r_xbot,
		    r.r_ybot, r.r_xtop, r.r_ytop,
		    gnode->efnode_name->efnn_hier->hn_name);
	    */

	    /* Find the plane of the gate type */
	    t = EFDeviceTypes[dev->dev_type];
	    pNum = DBPlane(t);
	    pos = ExtCurStyle->exts_planeOrder[pNum];
	    pmax = ++pos;

	    /* Find the highest plane in the technology */
	    for (p = PL_TECHDEPBASE; p < DBNumPlanes; p++)
		if (ExtCurStyle->exts_planeOrder[p] > pmax)
		    pmax = ExtCurStyle->exts_planeOrder[p];

	    /* Create the yank cell if it doesn't already exist */
	    if (extPathDef == (CellDef *) NULL)
	        DBNewYank("__PATHYANK__", &extPathUse, &extPathDef);

	    /* Use the cellDef reserved for extraction */
	    /* DBCellClearDef(extPathDef); */	    /* See below */
	    scx.scx_use = editUse;
	    scx.scx_trans = GeoIdentityTransform;
	    scx.scx_area = r;

	    /* gatemask is a mask of all gate types for MOSFET devices	*/

	    TTMaskZero(&gatemask);
	    for (i = 0; i < DBNumTypes; i++)
	    {
		ExtDevice *ed;
		char devclass;

		if (ExtCurStyle->exts_device[i] != NULL)
		{
		    for (ed = ExtCurStyle->exts_device[i]; ed; ed = ed->exts_next)
		    {
			devclass = ed->exts_deviceClass;
			switch (devclass)
			{
			    case DEV_MOSFET:
			    case DEV_FET:
			    case DEV_ASYMMETRIC:
			    case DEV_MSUBCKT:
				TTMaskSetType(&gatemask, i);
				break;
			}
		    }
		}
	    }

	    for (; pos <= pmax; pos++)
	    {
		GateDiffAccumStruct gdas;
		AntennaAccumStruct aas;
		AntennaMarkStruct ams;

		/* Find the plane of pos */

		for (p = 0;  p < DBNumPlanes; p++)
		    if (ExtCurStyle->exts_planeOrder[p] == pos)
			pNum2 = p;

		/* Find the tiletype which is a contact and whose base is pNum2 */
		/* (NOTE:  Need to extend to all such contacts, as there may be	*/
		/* more than one.) (Also should find these types up top, not	*/
		/* within the loop.)						*/

		/* Modify DBConnectTbl to limit connectivity to the plane   */
		/* of the antenna check and below			    */

		conType = -1;
		for (i = 0; i < DBNumTypes; i++)
		    if (DBIsContact(i) && DBPlane(i) == pNum2)
		    {
			conType = i;
			TTMaskZero(&saveConMask);
			TTMaskSetMask(&saveConMask, &DBConnectTbl[i]);
			TTMaskZero(&DBConnectTbl[i]);
			for (j = 0; j < DBNumTypes; j++)
			    if (TTMaskHasType(&saveConMask, j) &&
					(DBPlane(j) <= pNum2))
				TTMaskSetType(&DBConnectTbl[i], j);
			break;
		    }

	        for (i = 0; i < DBNumTypes; i++) antennaarea[i] = (dlong)0;
	        gatearea = 0;
	        diffarea = 0;

		/* Note:  Ideally, the addition of material in the next	    */
		/* metal plane is additive.  But that requires enumerating  */
		/* all the vias and using those as starting points for the  */
		/* next connectivity search, which needs to be coded.	    */

		DBCellClearDef(extPathDef);

		/* To do:  Mark tiles so area count can be progressive */

		DBTreeCopyConnect(&scx, &DBConnectTbl[t], 0,
			DBConnectTbl, &TiPlaneRect, SEL_NO_LABELS, extPathUse);

		/* Search planes of tie types and accumulate all tiedown areas */
		gdas.accum = (dlong)0;
		for (p = 0;  p < DBNumPlanes; p++)
		{
		    gdas.pNum = p;
		    DBSrPaintArea((Tile *)NULL, extPathUse->cu_def->cd_planes[p],
			    &TiPlaneRect, &ExtCurStyle->exts_antennaTieTypes,
			    areaAccumFunc, (ClientData)&gdas);
		}
		diffarea = gdas.accum;

		/* Search plane of gate type and accumulate all gate area */
		gdas.accum = (dlong)0;
		gdas.pNum = pNum;
		DBSrPaintArea((Tile *)NULL, extPathUse->cu_def->cd_planes[pNum],
			&TiPlaneRect, &gatemask, areaAccumFunc, (ClientData)&gdas);
		gatearea = gdas.accum;

		/* Search metal planes and accumulate all antenna areas */
		for (p = 0;  p < DBNumPlanes; p++)
		{
		    if (ExtCurStyle->exts_antennaModel & ANTENNAMODEL_PARTIAL)
			if (p != pNum2) continue;

		    aas.pNum = p;
		    aas.accum = &antennaarea[0];
		    if (ExtCurStyle->exts_planeOrder[p] <= pos)
			DBSrPaintArea((Tile *)NULL, extPathUse->cu_def->cd_planes[p],
				&TiPlaneRect, &DBAllButSpaceAndDRCBits,
				antennaAccumFunc, (ClientData)&aas);
		}

		antennaError = FALSE;

		if (diffarea == 0)
		{
		    anttotal = 0.0;
		    conttotal = 0.0;
		    saveRatio = 0.0;
		    for (i = 0; i < DBNumUserLayers; i++)
		    {
			if (ExtCurStyle->exts_antennaRatio[i].ratioGate > 0)
			{
			    /* Partial model computes vias separately */
			    if ((ExtCurStyle->exts_antennaModel & ANTENNAMODEL_PARTIAL)
					&& !DBIsContact(i))
				anttotal += (double)antennaarea[i] /
					(double)ExtCurStyle->exts_antennaRatio[i].ratioGate;
			    else if ((ExtCurStyle->exts_antennaModel & ANTENNAMODEL_PARTIAL)
					&& (DBPlane(i) == pNum2))
				conttotal += (double)antennaarea[i] /
					(double)ExtCurStyle->exts_antennaRatio[i].ratioGate;
			}
			if (ExtCurStyle->exts_antennaRatio[i].ratioGate > saveRatio)
			    saveRatio = ExtCurStyle->exts_antennaRatio[i].ratioGate;
		    }

		    if (anttotal > (double)gatearea)
		    {
			antennaError = TRUE;
			if (efAntennaDebug == TRUE)
			{
			    if (hc->hc_use->use_id)
				TxError("Cell: %s\n", hc->hc_use->use_id);
			    else
				TxError("Cell: %s\n", hc->hc_use->use_def->def_name);
			    TxError("Antenna violation detected at plane %s\n",
				    DBPlaneLongNameTbl[pNum2]);
			    TxError("Effective antenna ratio %g > limit %g\n",
				    saveRatio * (float)anttotal / (float)gatearea,
				    saveRatio);
			    TxError("Gate rect (%d %d) to (%d %d)\n",
				    gdas.r.r_xbot, gdas.r.r_ybot,
				    gdas.r.r_xtop, gdas.r.r_ytop);
			    TxError("Antenna rect (%d %d) to (%d %d)\n",
				    aas.r.r_xbot, aas.r.r_ybot,
				    aas.r.r_xtop, aas.r.r_ytop);
			}
		    }
		    if (conttotal > (double)gatearea)
		    {
			antennaError = TRUE;
			if (efAntennaDebug == TRUE)
			{
			    if (hc->hc_use->use_id)
				TxError("Cell: %s\n", hc->hc_use->use_id);
			    else
				TxError("Cell: %s\n", hc->hc_use->use_def->def_name);
			    TxError("Antenna violation detected at plane %s contact\n",
				    DBPlaneLongNameTbl[pNum2]);
			    TxError("Effective antenna ratio %g > limit %g\n",
				    saveRatio * (float)conttotal / (float)gatearea,
				    saveRatio);
			    TxError("Gate rect (%d %d) to (%d %d)\n",
				    gdas.r.r_xbot, gdas.r.r_ybot,
				    gdas.r.r_xtop, gdas.r.r_ytop);
			    TxError("Antenna rect (%d %d) to (%d %d)\n",
				    aas.r.r_xbot, aas.r.r_ybot,
				    aas.r.r_xtop, aas.r.r_ytop);
			}
		    }
		}
		else
		{
		    anttotal = 0.0;
		    conttotal = 0.0;
		    saveRatio = 0.0;
		    for (i = 0; i < DBNumUserLayers; i++)
			if (ExtCurStyle->exts_antennaRatio[i].ratioDiffB != INFINITY)
			{
			    /* Compute effective gate ratio increased by diffusion area */
			    ratioTotal = ExtCurStyle->exts_antennaRatio[i].ratioGate +
			    		ExtCurStyle->exts_antennaRatio[i].ratioDiffB +
					ExtCurStyle->exts_antennaRatio[i].ratioDiffA *
					(double)diffarea;

			    if (ratioTotal > 0)
			    {
			    	/* Partial model computes vias separately */
			    	if ((ExtCurStyle->exts_antennaModel & ANTENNAMODEL_PARTIAL)
					&& !DBIsContact(i))
				    anttotal += (double)antennaarea[i] / ratioTotal;
			        else if ((ExtCurStyle->exts_antennaModel & ANTENNAMODEL_PARTIAL)
					&& (DBPlane(i) == pNum2))
				    conttotal += (double)antennaarea[i] / ratioTotal;
			    }
			    if (ratioTotal > saveRatio)
				saveRatio = ratioTotal;
			}

		    if (anttotal > (double)gatearea)
		    {
			antennaError = TRUE;
			if (efAntennaDebug == TRUE)
			{
			    if (hc->hc_use->use_id)
				TxError("Cell: %s\n", hc->hc_use->use_id);
			    else
				TxError("Cell: %s\n", hc->hc_use->use_def->def_name);
			    TxError("Antenna violation detected at plane %s\n",
		    			DBPlaneLongNameTbl[pNum2]);
			    TxError("Effective antenna ratio %g > limit %g\n",
				    saveRatio * (float)anttotal / (float)gatearea,
				    saveRatio);
			    TxError("Gate rect (%d %d) to (%d %d)\n",
				    gdas.r.r_xbot, gdas.r.r_ybot,
				    gdas.r.r_xtop, gdas.r.r_ytop);
			    TxError("Antenna rect (%d %d) to (%d %d)\n",
				    aas.r.r_xbot, aas.r.r_ybot,
				    aas.r.r_xtop, aas.r.r_ytop);
			}
		    }
		    if (conttotal > (double)gatearea)
		    {
			antennaError = TRUE;
			if (efAntennaDebug == TRUE)
			{
			    if (hc->hc_use->use_id)
				TxError("Cell: %s\n", hc->hc_use->use_id);
			    else
				TxError("Cell: %s\n", hc->hc_use->use_def->def_name);
			    TxError("Antenna violation detected at plane %s contact\n",
		    			DBPlaneLongNameTbl[pNum2]);
			    TxError("Effective antenna ratio %g > limit %g\n",
				    saveRatio * (float)conttotal / (float)gatearea,
				    saveRatio);
			    TxError("Gate rect (%d %d) to (%d %d)\n",
				    gdas.r.r_xbot, gdas.r.r_ybot,
				    gdas.r.r_xtop, gdas.r.r_ytop);
			    TxError("Antenna rect (%d %d) to (%d %d)\n",
				    aas.r.r_xbot, aas.r.r_ybot,
				    aas.r.r_xtop, aas.r.r_ytop);
			}
		    }
		}

		if (antennaError)
		{
		    /* Search plane of gate type and mark all gate areas */
		    ams.def = editUse->cu_def;
		    ams.pNum = pNum2;
		    DBSrPaintArea((Tile *)NULL, extPathUse->cu_def->cd_planes[pNum],
			    &TiPlaneRect, &gatemask, areaMarkFunc, (ClientData)&ams);

		    /* Search metal planes and accumulate all antenna areas */
		    for (p = 0;  p < DBNumPlanes; p++)
		    {
			if (ExtCurStyle->exts_antennaModel & ANTENNAMODEL_PARTIAL)
			    if (p != pNum2) continue;

			if (ExtCurStyle->exts_planeOrder[p] <= pos)
			    DBSrPaintArea((Tile *)NULL, extPathUse->cu_def->cd_planes[p],
				    &TiPlaneRect, &DBAllButSpaceAndDRCBits,
				    areaMarkFunc, (ClientData)&ams);
		    }
		}

		/* Put the connect table back the way it was */
		if (conType >= 0)
		    TTMaskSetMask(&DBConnectTbl[conType], &saveConMask);
	    }
    }
    freeMagic(antennaarea);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * areaMarkFunc --
 *
 *	Mark the tile areas searched with feedback entries
 *
 * ----------------------------------------------------------------------------
 */

int
areaMarkFunc(tile, ams)
    Tile *tile;
    AntennaMarkStruct *ams;
{
    Rect rect;
    char msg[200];

    TiToRect(tile, &rect);
    sprintf(msg, "Antenna error at plane %s\n", DBPlaneLongNameTbl[ams->pNum]);
    DBWFeedbackAdd(&rect, msg, ams->def, 1, STYLE_PALEHIGHLIGHTS);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * areaAccumFunc --
 *
 *	Accumulate the total tile area searched
 *
 * ----------------------------------------------------------------------------
 */

int
areaAccumFunc(tile, gdas)
    Tile *tile;
    GateDiffAccumStruct *gdas;
{
    Rect *rect = &(gdas->r);
    int type;
    dlong area;

    /* Avoid double-counting the area of contacts */
    if (IsSplit(tile))
	type = SplitSide(tile) ? SplitRightType(tile) : SplitLeftType(tile);
    else
	type = TiGetType(tile);

    if (DBIsContact(type))
	if (DBPlane(type) != gdas->pNum)
	    return 0;

    TiToRect(tile, rect);
    area = (dlong)(rect->r_xtop - rect->r_xbot) * (dlong)(rect->r_ytop - rect->r_ybot);
    gdas->accum += area;
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * antennaAccumFunc --
 *
 *	Accumulate the total tile area searched, keeping an individual
 *	count for each tile type.  If the antenna model is SIDEWALL, then
 *	calculate the area of the tile sidewall (tile perimeter * layer
 *	thickness), rather than the drawn tile area.
 *
 * ----------------------------------------------------------------------------
 */

int
antennaAccumFunc(tile, aaptr)
    Tile *tile;
    AntennaAccumStruct *aaptr;
{
    Rect *rect = &(aaptr->r);
    Rect *cont = &(aaptr->via);
    dlong area;
    int type;
    dlong *typeareas = aaptr->accum;
    int plane = aaptr->pNum;
    float thick, fcutsize;
    int cutsize, cutsep, cutborder, nx, ny, w, h;

    type = TiGetType(tile);

    if (ExtCurStyle->exts_antennaRatio[type].areaType & ANTENNAMODEL_SIDEWALL)
    {
	if (DBIsContact(type))
	{
	    int cperim;
	    TileType ttype;
	    TileTypeBitMask sMask;
	    float thick;

	    TiToRect(tile, cont);

	    /* Simple cut area calculation.  Note that this is not the same as	*/
	    /* the GDS output routine for contact cuts, and can be wrong for	*/
	    /* partially overlapping contacts where the tiles get subdivided.	*/

	    CIFGetContactSize(type, &cutsize, &cutsep, &cutborder);

	    w = cont->r_xtop - cont->r_xbot;
	    h = cont->r_ytop - cont->r_ybot;
	    nx = (w - 2 * cutborder) / (cutsize + cutsep);
	    ny = (h - 2 * cutborder) / (cutsize + cutsep);
	    if (nx == 0) nx = 1;
	    if (ny == 0) ny = 1;

	    cperim = nx * ny * (cutsize << 2) / CIFCurStyle->cs_scaleFactor;

	    /* For contacts, add the area of the perimeter to the   */
	    /* residue (metal) type on the plane being searched.    */
	    /* Then, if the plane is the same as the base type of   */
	    /* the contact, add the entire perimeter area of the    */
	    /* tile to the total for the contact type itself.	    */

	    DBFullResidueMask(type, &sMask);
	    for (ttype = TT_TECHDEPBASE; ttype < DBNumTypes; ttype++)
		if (TTMaskHasType(&sMask, ttype))
		    if (DBTypeOnPlane(ttype, plane))
		    {
			thick = ExtCurStyle->exts_thick[ttype];
			typeareas[ttype] += (dlong)((float)cperim * thick);
		    }

	    /* NOTE:  The "partial" model ignores the contribution of vias. */
	    if (!(ExtCurStyle->exts_antennaModel & ANTENNAMODEL_PARTIAL))
	    {
		if (type >= DBNumUserLayers)
		{
		    DBResidueMask(type, &sMask);
		    for (ttype = TT_TECHDEPBASE; ttype < DBNumTypes; ttype++)
			if (TTMaskHasType(&sMask, ttype))
			    if (DBTypeOnPlane(ttype, plane))
			    {
				thick = ExtCurStyle->exts_thick[ttype];
				typeareas[ttype] += (dlong)((float)cperim * thick);
				break;
			    }
		}
		else
		{
		    thick = ExtCurStyle->exts_thick[type];
		    typeareas[type] += (dlong)((float)cperim * thick);
		}
	    }
	}
	else
	{
	    Tile *tp;
	    int perimeter = 0, pmax, pmin;

	    TiToRect(tile, rect);

	    /* Accumulate perimeter of tile where tile abuts space */

	    /* Top */
	    for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp = BL(tp))
	    {
		if (TiGetBottomType(tp) == TT_SPACE)
		{
		    pmin = MAX(LEFT(tile), LEFT(tp));
		    pmax = MIN(RIGHT(tile), RIGHT(tp));
		    perimeter += (pmax - pmin);
		}
	    }
	    /* Bottom */
	    for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp = TR(tp))
	    {
		if (TiGetTopType(tp) == TT_SPACE)
		{
		    pmin = MAX(LEFT(tile), LEFT(tp));
		    pmax = MIN(RIGHT(tile), RIGHT(tp));
		    perimeter += (pmax - pmin);
		}
	    }
	    /* Left */
	    for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
	    {
		if (TiGetRightType(tp) == TT_SPACE)
		{
		    pmin = MAX(BOTTOM(tile), BOTTOM(tp));
		    pmax = MIN(TOP(tile), TOP(tp));
		    perimeter += (pmax - pmin);
		}
	    }
	    /* Right */
	    for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp = LB(tp))
	    {
		if (TiGetLeftType(tp) == TT_SPACE)
		{
		    pmin = MAX(BOTTOM(tile), BOTTOM(tp));
		    pmax = MIN(TOP(tile), TOP(tp));
		    perimeter += (pmax - pmin);
		}
	    }

	    /* Area is perimeter times layer thickness */
	    thick = ExtCurStyle->exts_thick[type];
	    typeareas[type] += (dlong)((float)perimeter * thick);
	}
    }
    else if (ExtCurStyle->exts_antennaRatio[type].areaType & ANTENNAMODEL_SURFACE)
    {
	/* If type is a contact, then add area to both residues as well	*/
	/* as the contact type.						*/

	/* NOTE:  Restrict area counts per plane so areas of contacts	*/
	/* are not double-counted.					*/

	if (DBIsContact(type))
	{
	    TileType ttype;
	    TileTypeBitMask sMask;

	    TiToRect(tile, cont);

	    /* Simple cut area calculation.  Note that this is not the same as	*/
	    /* the GDS output routine for contact cuts, and can be wrong for	*/
	    /* partially overlapping contacts where the tiles get subdivided.	*/

	    CIFGetContactSize(type, &cutsize, &cutsep, &cutborder);

	    w = cont->r_xtop - cont->r_xbot;
	    h = cont->r_ytop - cont->r_ybot;
	    nx = (w - 2 * cutborder) / (cutsize + cutsep);
	    ny = (h - 2 * cutborder) / (cutsize + cutsep);
	    if (nx == 0) nx = 1;
	    if (ny == 0) ny = 1;

	    fcutsize = (float)cutsize / (float)CIFCurStyle->cs_scaleFactor;
	    area = (dlong)(nx * ny) * (dlong)(fcutsize * fcutsize);

	    DBFullResidueMask(type, &sMask);
	    for (ttype = TT_TECHDEPBASE; ttype < DBNumTypes; ttype++)
		if (TTMaskHasType(&sMask, ttype))
		    if (DBTypeOnPlane(ttype, plane))
			typeareas[ttype] += area;

	    if (type >= DBNumUserLayers)
	    {
		DBResidueMask(type, &sMask);
		for (ttype = TT_TECHDEPBASE; ttype < DBNumTypes; ttype++)
		    if (TTMaskHasType(&sMask, ttype))
			if (DBTypeOnPlane(ttype, plane))
			{
			    typeareas[ttype] += area;
			    break;
			}
	    }
	    else
		typeareas[type] += area;
	}
	else
	{
	    TiToRect(tile, rect);
	    area = (dlong)(rect->r_xtop - rect->r_xbot)
			* (dlong)(rect->r_ytop - rect->r_ybot);

	    typeareas[type] += area;
	}
    }
    return 0;
}
