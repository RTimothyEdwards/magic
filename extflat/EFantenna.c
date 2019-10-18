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

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/hash.h"
#include "utils/utils.h"
#include "tiles/tile.h"
#ifdef MAGIC_WRAPPER
#include "database/database.h"
#include "windows/windows.h"
#include "textio/textio.h"
#include "dbwind/dbwind.h"	/* for DBWclientID */
#include "textio/txcommands.h"
#endif
#include "extflat/extflat.h"
#include "extract/extract.h"
#include "extract/extractInt.h"
#include "utils/malloc.h"

/* Forward declarations */
int antennacheckArgs();
int antennacheckVisit();

typedef struct {
	long    visitMask:MAXDEVTYPES; 
} nodeClient;

typedef struct {
	HierName *lastPrefix;
	long    visitMask:MAXDEVTYPES; 
} nodeClientHier;

#define NO_RESCLASS	-1

#define markVisited(client, rclass) \
  { (client)->visitMask |= (1<<rclass); }

#define clearVisited(client) \
   { (client)->visitMask = (long)0; }

#define beenVisited(client, rclass)  \
   ( (client)->visitMask & (1<<rclass))

#define initNodeClient(node) \
{ \
	(node)->efnode_client = (ClientData) mallocMagic((unsigned) (sizeof(nodeClient))); \
	(( nodeClient *)(node)->efnode_client)->visitMask = (long) 0; \
}


#define initNodeClientHier(node) \
{ \
	(node)->efnode_client = (ClientData) mallocMagic((unsigned) (sizeof(nodeClientHier))); \
	((nodeClientHier *) (node)->efnode_client)->visitMask = (long) 0; \
}


/*
 * ----------------------------------------------------------------------------
 *
 * Main Tcl callback for command "magic::antennacheck"
 *
 * ----------------------------------------------------------------------------
 */

#define ANTENNACHECK_RUN     0
#define ANTENNACHECK_HELP    1

void
CmdAntennaCheck(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int i,flatFlags;
    char *inName;
    FILE *f;

    int option = ANTENNACHECK_RUN;
    int value;
    int argc = cmd->tx_argc;
    char **argv = cmd->tx_argv; 
    char **msg;
    bool err_result;

    short sd_rclass;
    short sub_rclass;
    char *devname;
    char *subname;
    int idx;

    CellUse *editUse;

    static char *cmdAntennaCheckOption[] = {
	"[run] [options]	run antennacheck on current cell\n"
	"			use \"run -help\" to get standard options",
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
	EFDone();
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
	    EFDone();
	    return /* TCL_ERROR */;
	}
	inName = ((CellUse *) w->w_surfaceID)->cu_def->cd_name;
    }
    editUse = (CellUse *)w->w_surfaceID;

    /*
     * Initializations specific to this program.
     */

    /* Read the hierarchical description of the input circuit */
    if (EFReadFile(inName, FALSE, FALSE, FALSE) == FALSE)
    {
	EFDone();
	return /* TCL_ERROR */;
    }

    /* Convert the hierarchical description to a flat one */
    flatFlags = EF_FLATNODES;
    EFFlatBuild(inName, flatFlags);

    EFVisitDevs(antennacheckVisit, (ClientData)editUse);
    EFFlatDone();
    EFDone();

    TxPrintf("antennacheck finished.\n");
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
antennacheckVisit(dev, hierName, scale, trans, editUse)
    Dev *dev;		/* Device being output */
    HierName *hierName;	/* Hierarchical path down to this device */
    float scale;	/* Scale transform for output */
    Transform *trans;	/* Coordinate transform */
    CellUse *editUse;	/* ClientData is edit cell use */
{
    DevTerm *gate;
    int pos, pNum, pNum2, pmax, p, i, j, gatearea, diffarea, total;
    double difftotal;
    int *antennaarea;
    Rect r;
    EFNode *gnode;
    SearchContext scx;
    TileTypeBitMask gatemask;

    extern CellDef *extPathDef;	    /* see extract/ExtLength.c */
    extern CellUse *extPathUse;	    /* see extract/ExtLength.c */

    extern int  areaAccumFunc(), antennaAccumFunc();

    antennaarea = (int *)mallocMagic(DBNumTypes * sizeof(int));
    
    for (i = 0; i < DBNumTypes; i++) antennaarea[i] = 0;

    switch(dev->dev_class)
    {
	case DEV_FET:
	case DEV_MOSFET:
	    GeoTransRect(trans, &dev->dev_rect, &r);

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

	    gate = &dev->dev_terms[0];

	    gnode = AntennaGetNode(hierName, gate->dterm_node->efnode_name->efnn_hier);
	    if (beenVisited((nodeClient *)gnode->efnode_client, 0))
		return 0;
	    else
		markVisited((nodeClient *)gnode->efnode_client, 0);

	    /* Find the plane of the gate type */
	    pNum = DBPlane(dev->dev_type);
	    pos = ExtCurStyle->exts_planeOrder[pNum];
	    pmax = ++pos;

	    /* Find the highest plane in the technology */
	    for (p = PL_TECHDEPBASE; p < DBNumPlanes; p++)
		if (ExtCurStyle->exts_planeOrder[p] > pmax)
		    pmax = ExtCurStyle->exts_planeOrder[p];

	    /* Use the cellDef reserved for extraction */
	    DBCellClearDef(extPathDef);
	    scx.scx_use = editUse;
	    scx.scx_trans = GeoIdentityTransform;

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
		/* Modify DBConnectTbl to limit connectivity to the plane   */
		/* of the antenna check and below			    */

		/* To be completed */

		DBTreeCopyConnect(&scx, &DBConnectTbl[dev->dev_type], 0,
			DBConnectTbl, &TiPlaneRect, extPathUse);

		/* Search plane of gate type and accumulate all (new) gate area */
		DBSrPaintArea((Tile *)NULL, extPathUse->cu_def->cd_planes[pNum],
			&TiPlaneRect, &gatemask, areaAccumFunc, (ClientData)&gatearea);

		/* Search planes of tie type and accumulate all (new) tiedown areas */
		for (p = 0;  p < DBNumPlanes; p++)
		    DBSrPaintArea((Tile *)NULL, extPathUse->cu_def->cd_planes[p],
			    &TiPlaneRect, &ExtCurStyle->exts_antennaTieTypes,
			    areaAccumFunc, (ClientData)&diffarea);

		/* Search metal planes and accumulate all (new) antenna areas */
		for (p = 0;  p < DBNumPlanes; p++)
		{
		    if (ExtCurStyle->exts_planeOrder[p] == pos)
		    {
			pNum2 = p;
		    }
		    if (ExtCurStyle->exts_planeOrder[p] <= pos)
			DBSrPaintArea((Tile *)NULL, extPathUse->cu_def->cd_planes[p],
				&TiPlaneRect, &DBAllButSpaceAndDRCBits,
				antennaAccumFunc, (ClientData)&antennaarea);
		}

		/* To be elaborated. . . this encodes only one of several   */
		/* methods of calculating antenna violations.		    */

		if (diffarea == 0)
		{
		    difftotal = 0.0;
		    for (i = 0; i < DBNumTypes; i++)
			difftotal += antennaarea[i] / ExtCurStyle->exts_antennaRatio[i];

		    if (difftotal > gatearea)
			TxError("Antenna violation detected at plane %s "
				"(violation to be elaborated)",
				DBPlaneLongNameTbl[pNum2]);
		}
	    }
    }
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
areaAccumFunc(tile, totalarea)
    Tile *tile;
    int *totalarea;
{
    Rect rect;
    int area;

    TiToRect(tile, &rect);

    area += (rect.r_xtop - rect.r_xbot) * (rect.r_ytop - rect.r_ybot);

    *totalarea += area;

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * antennaAccumFunc --
 *
 *	Accumulate the total tile area searched, keeping an individual
 *	count for each tile type.
 *
 * ----------------------------------------------------------------------------
 */

int
antennaAccumFunc(tile, typeareas)
    Tile *tile;
    int **typeareas;
{
    Rect rect;
    int area;
    int type;

    type = TiGetType(tile);

    TiToRect(tile, &rect);

    area += (rect.r_xtop - rect.r_xbot) * (rect.r_ytop - rect.r_ybot);

    *typeareas[type] += area;

    return 0;
}
