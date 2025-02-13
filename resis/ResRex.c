
#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/resis/ResRex.c,v 1.3 2010/03/08 13:33:33 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>
#include <float.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "utils/undo.h"
#include "utils/signals.h"
#include "database/database.h"
#include "utils/malloc.h"
#include "textio/textio.h"
#include "extract/extract.h"
#include "extract/extractInt.h"
#include "extflat/extflat.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/utils.h"
#include "utils/tech.h"
#include "textio/txcommands.h"
#include	"resis/resis.h"
#ifdef LAPLACE
#include	"laplace.h"
#endif

#define INITFLATSIZE		1024
#define MAXNAME			1000

/* Time constants are produced by multiplying attofarads by milliohms,  */
/* giving zeptoseconds (yes, really.  Look it up).  This constant 	*/
/* converts zeptoseconds to picoseconds.				*/

#define Z_TO_P		1e9

/* Table of nodes to ignore (manually specified) */

HashTable 	ResIgnoreTable;	    /* Hash table of nodes to ignore  */

/* Table of nodes to include (manually specified) */

HashTable 	ResIncludeTable;    /* Hash table of nodes to include  */

/* Table of cells that have been processed */

HashTable	ResProcessedTable;

/* ResSimNode is a node read in from a sim file */

HashTable 	ResNodeTable;   /* Hash table of sim file nodes   */
RDev		*ResRDevList;	/* Linked list of Sim devices	  */
ResGlobalParams	gparams;	/* Junk passed between 		  */
				/* ResCheckSimNodes and 	  */
				/* ResExtractNet.		  */
extern ResSimNode	*ResOriginalNodes;	/*Linked List of Nodes		  */
int		resNodeNum;

#ifdef LAPLACE
int	ResOptionsFlags = ResOpt_Simplify | ResOpt_Tdi | ResOpt_DoExtFile
		| ResOpt_CacheLaplace;
#else
int	ResOptionsFlags = ResOpt_Simplify | ResOpt_Tdi | ResOpt_DoExtFile;
#endif
char	*ResCurrentNode;

FILE	*ResExtFile;
FILE	*ResLumpFile;
FILE	*ResFHFile;

int	ResPortIndex;	/* Port ordering to backannotate into magic */

/* external declarations */
extern ResSimNode *ResInitializeNode();
extern CellUse	  *CmdGetSelectedCell();

/* Linked list structure to use to store the substrate plane from each  */
/* extracted CellDef so that they can be returned to the original after */
/* extraction.								*/

struct saveList {
    Plane *sl_plane;
    CellDef *sl_def;
    struct saveList *sl_next;
};

/* Structure stores information required to be sent to ExtResisForDef() */
typedef struct
{
    float	    tdiTolerance;
    float	    frequency;
    float	    rthresh;
    struct saveList *savePlanes;
    CellDef	    *mainDef;
} ResisData;

/*
 *-------------------------------------------------------------------------
 *
 *  ExtResisForDef --
 *
 *	Do resistance network extraction for the indicated CellDef.
 *
 *-------------------------------------------------------------------------
 */

void
ExtResisForDef(celldef, resisdata)
    CellDef *celldef;
    ResisData *resisdata;
{
    RDev       *oldRDev;
    HashSearch hs;
    HashEntry  *entry;
    devPtr     *tptr, *oldtptr;
    ResSimNode  *node;
    int                result, idx;
    char       *devname;
    Plane      *savePlane;

    ResRDevList = NULL;
    ResOriginalNodes = NULL;

    /* Check if this cell has been processed */
    if (HashLookOnly(&ResProcessedTable, celldef->cd_name)) return;
    HashFind(&ResProcessedTable, celldef->cd_name);

    /* Prepare the substrate for resistance extraction */
    savePlane = extResPrepSubstrate(celldef);
    if (savePlane != NULL)
    {
	struct saveList *newsl;
	newsl = (struct saveList *)mallocMagic(sizeof(struct saveList));
	newsl->sl_plane = savePlane;
	newsl->sl_def = celldef;
	newsl->sl_next = resisdata->savePlanes;
	resisdata->savePlanes = newsl;
    }

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

    HashInit(&ResNodeTable, INITFLATSIZE, HT_STRINGKEYS);
    /* read in .sim file */
    result = (ResReadSim(celldef->cd_name,
		ResSimDevice, ResSimCapacitor, ResSimResistor,
		ResSimAttribute, ResSimMerge, ResSimSubckt) == 0);

    /* Clean up the EFDevTypes table */
    for (idx = 0; idx < EFDevNumTypes; idx++) freeMagic(EFDevTypes[idx]);
    EFDevNumTypes = 0;

    if (result)
	/* read in .nodes file   */
	result = (ResReadNode(celldef->cd_name) == 0);

    if (result)
    {
	/* Check for subcircuit ports */
	if (ResOptionsFlags & ResOpt_Blackbox)
	    ResCheckBlackbox(celldef);
	else
	    ResCheckPorts(celldef);

	/* Extract networks for nets that require it. */
	if (!(ResOptionsFlags & ResOpt_FastHenry) ||
			DBIsSubcircuit(celldef))
	    ResCheckSimNodes(celldef, resisdata);

	if (ResOptionsFlags & ResOpt_Stat)
	    ResPrintStats((ResGlobalParams *)NULL, "");
    }

    /* Clean up */

    HashStartSearch(&hs);
    while((entry = HashNext(&ResNodeTable, &hs)) != NULL)
    {
	node=(ResSimNode *) HashGetValue(entry);
	tptr = node->firstDev;
	if (node == NULL)
	{
	    TxError("Error:  NULL Hash entry!\n");
	    TxFlushErr();
	}
	while (tptr != NULL)
	{
	    oldtptr = tptr;
	    tptr = tptr->nextDev;
	    freeMagic((char *)oldtptr);
	}
	freeMagic((char *) node);
    }
    HashKill(&ResNodeTable);
    while (ResRDevList != NULL)
    {
	oldRDev = ResRDevList;
	ResRDevList = ResRDevList->nextDev;
	if (oldRDev->layout != NULL)
	{
	    freeMagic((char *)oldRDev->layout);
	    oldRDev->layout = NULL;
	}
	freeMagic((char *)oldRDev);
    }
}

/*
 *-------------------------------------------------------------------------
 *
 * CmdExtResis--  reads in sim file and layout, and produces patches to the
 *	.ext files and .sim files that include resistors.
 *
 * Results:
 *	None.
 *
 * Side Effects: Produces .res.sim file and .res.ext file for all nets that
 *	require resistors.
 *
 *-------------------------------------------------------------------------
 */

void
CmdExtResis(win, cmd)
    MagWindow *win;
    TxCommand *cmd;
{
    int i, j, k, option, value, saveFlags;
    static int init = 1;
    static float rthresh, tdiTolerance, fhFrequency;

    CellDef	*mainDef;
    CellUse	*selectedUse;
    ResisData	resisdata;
    char	*endptr;	/* for use with strtod() */
    struct saveList *sl;

    extern int resSubcircuitFunc();	/* Forward declaration */

    static const char * const onOff[] =
    {
	"off",
	"on",
	NULL
    };

    static const char * const cmdExtresisCmd[] =
    {
	"all 		       extract all the nets",
	"threshold [value]    set minimum resistor extraction threshold",
	"tolerance [value]    set ratio between resistor and device tol.",
	"simplify [on/off]    turn on/off simplification of resistor nets",
	"extout   [on/off]    turn on/off writing of .res.ext file",
	"lumped   [on/off]    turn on/off writing of updated lumped resistances",
	"silent   [on/off]    turn on/off printing of net statistics",
	"skip     mask        don't extract these types",
	"ignore	  names	      don't extract these nets",
	"include  names	      extract only these nets",
	"box      type        extract the signal under the box on layer type",
	"cell	   cellname    extract the network for the cell named cellname",
	"blackbox [on/off]    treat subcircuits with ports as black boxes",
	"fasthenry [freq]      extract subcircuit network geometry into .fh file",
	"geometry	      extract network centerline geometry (experimental)",
	"help                 print this message",
#ifdef LAPLACE
	"laplace  [on/off]    solve Laplace's equation using FEM",
#endif
	NULL
    };

typedef enum {
	RES_BAD=-2, RES_AMBIG, RES_ALL,
	RES_THRESH, RES_TOL,
	RES_SIMP, RES_EXTOUT, RES_LUMPED, RES_SILENT,
	RES_SKIP, RES_IGNORE, RES_INCLUDE, RES_BOX, RES_CELL,
	RES_BLACKBOX, RES_FASTHENRY, RES_GEOMETRY, RES_HELP,
#ifdef LAPLACE
	RES_LAPLACE,
#endif
	RES_RUN
} ResOptions;

    if (init)
    {
	for (i = 0; i < NT; i++)
	{
            TTMaskZero(&(ResCopyMask[i]));
	    TTMaskSetMask(&ResCopyMask[i], &DBConnectTbl[i]);
     	}
	rthresh = 0;
	tdiTolerance = 1;
	fhFrequency = 10e6;	/* 10 MHz default */
	HashInit(&ResIgnoreTable, INITFLATSIZE, HT_STRINGKEYS);
	HashInit(&ResIncludeTable, INITFLATSIZE, HT_STRINGKEYS);
	init = 0;
    }

    /* Initialize ResGlobalParams */
    gparams.rg_ttype = TT_SPACE;
    gparams.rg_Tdi = 0.0;
    gparams.rg_nodecap = 0.0;
    gparams.rg_maxres = 0.0;
    gparams.rg_bigdevres = 0;
    gparams.rg_tilecount = 0;
    gparams.rg_status = 0;
    gparams.rg_devloc = NULL;
    gparams.rg_name = NULL;

    option = (cmd->tx_argc > 1) ? Lookup(cmd->tx_argv[1], cmdExtresisCmd)
		: RES_RUN;

    switch (option)
    {
	case RES_SIMP:
	case RES_EXTOUT:
	case RES_LUMPED:
	case RES_SILENT:
	case RES_BLACKBOX:
	    if (cmd->tx_argc > 2)
	    {
		value = Lookup(cmd->tx_argv[2], onOff);
		if (value < 0)
		{
		    TxError("Value must be either \"on\" or \"off\".\n");
		    return;
		}
	    }
	    break;
    }

    switch (option)
    {
	 case RES_TOL:
	    ResOptionsFlags |=  ResOpt_ExplicitRtol;
	    if (cmd->tx_argc > 2)
	    {
		tdiTolerance = MagAtof(cmd->tx_argv[2]);
		if (tdiTolerance <= 0)
		{
		    TxError("Usage:  %s tolerance [value]\n", cmd->tx_argv[0]);
			return;
		}
	    }
	    else
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewDoubleObj((double)tdiTolerance));
#else
		TxPrintf("Tolerance ratio is %g.\n", tdiTolerance);
#endif
	    }
	    return;

	 case RES_THRESH:
	    if (cmd->tx_argc > 2)
	    {
		rthresh = MagAtof(cmd->tx_argv[2]);
		if (rthresh < 0)
		{
		    TxError("Usage:  %s threshold [value]\n", cmd->tx_argv[0]);
			return;
		}
	    }
	    else
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewDoubleObj((double)rthresh));
#else
		TxPrintf("Resistance threshold is %g.\n", rthresh);
#endif
	    }
	    return;

	 case RES_ALL:
	    ResOptionsFlags |= ResOpt_ExtractAll;
	    break;

	 case RES_GEOMETRY:
	    saveFlags = ResOptionsFlags;
	    ResOptionsFlags |= ResOpt_Geometry | ResOpt_ExtractAll;
	    ResOptionsFlags &= ~(ResOpt_DoExtFile | ResOpt_DoLumpFile
			| ResOpt_Simplify | ResOpt_Tdi);
	    break;

	 case RES_FASTHENRY:
	    if (cmd->tx_argc == 3)
	    {
		  double tmpf = strtod(cmd->tx_argv[2], &endptr);
		  if (endptr == cmd->tx_argv[2])
		  {
		      TxError("Cannot parse frequency value.  Assuming default\n");
		      TxError("Frequency = %2.1f Hz\n", fhFrequency);
		  }
		  else
		      fhFrequency = (float)tmpf;
	    }
	    saveFlags = ResOptionsFlags;
	    ResOptionsFlags |= ResOpt_FastHenry | ResOpt_ExtractAll;
	    ResOptionsFlags &= ~(ResOpt_DoExtFile | ResOpt_DoLumpFile
			| ResOpt_Simplify | ResOpt_Tdi);
	    break;

	case RES_BLACKBOX:
	    if (cmd->tx_argc == 2)
	    {
		value = (ResOptionsFlags & ResOpt_Blackbox) ?
			TRUE : FALSE;
		TxPrintf("%s\n", onOff[value]);
	    }
	    else
	    {
		value = Lookup(cmd->tx_argv[2], onOff);

		if (value)
	      	   ResOptionsFlags |= ResOpt_Blackbox;
		else
	      	   ResOptionsFlags &= ~ResOpt_Blackbox;
	    }
	    return;
	case RES_SIMP:
	    if (cmd->tx_argc == 2)
	    {
		value = (ResOptionsFlags & (ResOpt_Simplify | ResOpt_Tdi)) ?
			TRUE : FALSE;
		TxPrintf("%s\n", onOff[value]);
	    }
	    else
	    {
		value = Lookup(cmd->tx_argv[2], onOff);

		if (value)
	      	   ResOptionsFlags |= ResOpt_Simplify | ResOpt_Tdi;
		else
	      	   ResOptionsFlags &= ~(ResOpt_Simplify | ResOpt_Tdi);
	    }
	    return;
	case RES_EXTOUT:
	    if (cmd->tx_argc == 2)
	    {
		value = (ResOptionsFlags & ResOpt_DoExtFile) ?
			TRUE : FALSE;
		TxPrintf("%s\n", onOff[value]);
	    }
	    else
	    {
		value = Lookup(cmd->tx_argv[2], onOff);
		if (value)
	      	   ResOptionsFlags |= ResOpt_DoExtFile;
		else
	      	   ResOptionsFlags &= ~ResOpt_DoExtFile;
	    }
	    return;
	case RES_LUMPED:
	    if (cmd->tx_argc == 2)
	    {
		value = (ResOptionsFlags & ResOpt_DoLumpFile) ?
			TRUE : FALSE;
		TxPrintf("%s\n", onOff[value]);
	    }
	    else
	    {
		value = Lookup(cmd->tx_argv[2], onOff);
		if (value)
	      	   ResOptionsFlags |= ResOpt_DoLumpFile;
		else
	      	   ResOptionsFlags &= ~ResOpt_DoLumpFile;
	    }
	    return;
	case RES_SILENT:
	    if (cmd->tx_argc == 2)
	    {
		value = (ResOptionsFlags & ResOpt_RunSilent) ?
			TRUE : FALSE;
		TxPrintf("%s\n", onOff[value]);
	    }
	    else
	    {
		value = Lookup(cmd->tx_argv[2], onOff);
		if (value)
	      	   ResOptionsFlags |= ResOpt_RunSilent;
		else
	      	   ResOptionsFlags &= ~ResOpt_RunSilent;
	    }
	    return;

	case RES_SKIP:
	    if (cmd->tx_argc > 2)
	    {
		j = DBTechNoisyNameType(cmd->tx_argv[2]);
		if (j >= 0)
		    for (k = TT_TECHDEPBASE; k < TT_MAXTYPES; k++)
			TTMaskClearType(&ResCopyMask[k], j);
		TTMaskZero(&(ResCopyMask[j]));
	    }
	    else
	    {
    		for (i = 0; i != NT; i++)
    		{
		    TTMaskZero(&(ResCopyMask[i]));
		    TTMaskSetMask(&ResCopyMask[i], &DBConnectTbl[i]);
     		}
	    }
	    return;

	case RES_IGNORE:
	    if (cmd->tx_argc > 2)
	    {
		if (!strcasecmp(cmd->tx_argv[2], "none"))
		{
		    /* Kill and reinitialize the table of ignored nets */
		    HashKill(&ResIgnoreTable);
		    HashInit(&ResIgnoreTable, INITFLATSIZE, HT_STRINGKEYS);
		}
		else
		    HashFind(&ResIgnoreTable, cmd->tx_argv[2]);
	    }
	    else
	    {
		HashSearch hs;
		HashEntry *entry;

		/* List all net names that are being ignored */
		HashStartSearch(&hs);
		while((entry = HashNext(&ResIgnoreTable, &hs)) != NULL)
		    TxPrintf("%s ", (char *)entry->h_key.h_name);
		TxPrintf("\n");
	    }
	    return;

	case RES_INCLUDE:
	    if (cmd->tx_argc > 2)
	    {
		if (!strcasecmp(cmd->tx_argv[2], "all"))
		{
		    /* Kill and reinitialize the table of ignored nets */
		    HashKill(&ResIncludeTable);
		    HashInit(&ResIncludeTable, INITFLATSIZE, HT_STRINGKEYS);
		}
		else
		    HashFind(&ResIncludeTable, cmd->tx_argv[2]);
	    }
	    else
	    {
		HashSearch hs;
		HashEntry *entry;

		/* List all net names that are being included */
		HashStartSearch(&hs);
		while((entry = HashNext(&ResIncludeTable, &hs)) != NULL)
		    TxPrintf("%s ", (char *)entry->h_key.h_name);
		TxPrintf("\n");
	    }
	    return;

	case RES_HELP:
	    for (i = 0; cmdExtresisCmd[i] != NULL; i++)
		TxPrintf("%s\n", cmdExtresisCmd[i]);
	    return;

	case RES_BOX:
	    {
		TileType	tt;
		CellDef		*def;
		Rect		rect;
		int		oldoptions;
		ResSimNode	lnode;

		if (ToolGetBoxWindow((Rect *) NULL, (int *) NULL) == NULL)
		{
		    TxError("Sorry, the box must appear in one of the windows.\n");
		    return;
		}

	     	if (cmd->tx_argc != 3) return;
		tt = DBTechNoisyNameType(cmd->tx_argv[2]);
		if (tt <= 0 || ToolGetBox(&def, &rect)== FALSE) return;
		gparams.rg_devloc = &rect.r_ll;
		gparams.rg_ttype = tt;
		gparams.rg_status = DRIVEONLY;
		oldoptions = ResOptionsFlags;
		ResOptionsFlags = ResOpt_DoSubstrate | ResOpt_Signal | ResOpt_Box;
#ifdef LAPLACE
		ResOptionsFlags |= (oldoptions &
			    (ResOpt_CacheLaplace | ResOpt_DoLaplace));
		LaplaceMatchCount = 0;
		LaplaceMissCount = 0;
#endif
		lnode.location = rect.r_ll;
		lnode.type = tt;
		if (ResExtractNet(&lnode, &gparams, NULL) != 0) return;
		ResPrintResistorList(stdout, ResResList);
		ResPrintDeviceList(stdout, ResRDevList);
#ifdef LAPLACE
		if (ResOptionsFlags & ResOpt_DoLaplace)
		{
		    TxPrintf("Laplace   solved: %d matched %d\n",
				LaplaceMissCount, LaplaceMatchCount);
		}
#endif

		ResOptionsFlags = oldoptions;
		return;
	    }
	case RES_CELL:
	    selectedUse = CmdGetSelectedCell((Transform *) NULL);
	    if (selectedUse == NULL)
	    {
		TxError("No cell selected\n");
		return;
	    }
	    mainDef = selectedUse->cu_def;
	    ResOptionsFlags &= ~ResOpt_ExtractAll;
	    break;

	case RES_RUN:
	    ResOptionsFlags &= ~ResOpt_ExtractAll;
	    break;
#ifdef LAPLACE
	case RES_LAPLACE:
	    LaplaceParseString(cmd);
	    return;
#endif
	case RES_AMBIG:
  	    TxPrintf("Ambiguous option: %s\n", cmd->tx_argv[1]);
	    TxFlushOut();
	    return;
	case RES_BAD:
  	    TxPrintf("Unknown option: %s\n", cmd->tx_argv[1]);
	    TxFlushOut();
	    return;
	default:
	    return;
    }

#ifdef LAPLACE
    LaplaceMatchCount = 0;
    LaplaceMissCount = 0;
#endif
    /* turn off undo stuff */
    UndoDisable();

    if (!ToolGetBox(&mainDef,(Rect *) NULL))
    {
    	TxError("Couldn't find def corresponding to box\n");
	if ((option == RES_FASTHENRY) || (option == RES_GEOMETRY))
	    ResOptionsFlags = saveFlags;
	return;
    }
    ResOptionsFlags |= ResOpt_Signal;
#ifdef ARIEL
    ResOptionsFlags &= ~ResOpt_Power;
#endif

    resisdata.rthresh = rthresh;
    resisdata.tdiTolerance = tdiTolerance;
    resisdata.frequency = fhFrequency;
    resisdata.mainDef = mainDef;
    resisdata.savePlanes = (struct saveList *)NULL;

    /* Do subcircuits (if any) first */
    HashInit(&ResProcessedTable, INITFLATSIZE, HT_STRINGKEYS);
    if (!(ResOptionsFlags & ResOpt_Blackbox))
	DBCellEnum(mainDef, resSubcircuitFunc, (ClientData) &resisdata);

    ExtResisForDef(mainDef, &resisdata);
    HashKill(&ResProcessedTable);

    /* Revert substrate planes */
    free_magic1_t mm1 = freeMagic1_init();
    for (sl = resisdata.savePlanes; sl; sl = sl->sl_next)
    {
	ExtRevertSubstrate(sl->sl_def, sl->sl_plane);
	freeMagic1(&mm1, sl);
    }
    freeMagic1_end(&mm1);

    /* turn back on undo stuff */
    UndoEnable();
#ifdef LAPLACE
    if (ResOptionsFlags & ResOpt_DoLaplace)
    {
	TxPrintf("Laplace solved: %d matched %d\n",
				LaplaceMissCount, LaplaceMatchCount);
    }
#endif

    /* Revert to the original flags in the case of FastHenry or	*/
    /* geometry centerline extraction.				*/

    if ((option == RES_FASTHENRY) || (option == RES_GEOMETRY))
	ResOptionsFlags = saveFlags;

    return;
}

/*
 *-------------------------------------------------------------------------
 *
 * resSubcircuitFunc --
 *	For each encountered cell, call the resistance extractor,
 *	then recursively call resSubcircuitFunc on all children
 *	of the cell.
 *
 * Results:
 *	Always return 0 to keep search alive.
 *
 * Side Effects:
 *	Does resistance extraction for an entire cell.
 *
 *-------------------------------------------------------------------------
 */

int
resSubcircuitFunc(cellUse, rdata)
    CellUse *cellUse;
    ResisData *rdata;
{
    CellDef *cellDef = cellUse->cu_def;

    if (DBIsSubcircuit(cellDef))
    {
	ExtResisForDef(cellDef, rdata);
	DBCellEnum(cellDef, resSubcircuitFunc, (ClientData)rdata);
    }
    return 0;
}

/*
 *-------------------------------------------------------------------------
 *
 * Callback routine for ResCheckBlackBox.  For each label found in a
 * subcell, transform the label position back to the top level and
 * add to the list of nodes for extresis.
 *
 *-------------------------------------------------------------------------
 */

int
resPortFunc(scx, lab, tpath, result)
    SearchContext *scx;
    Label *lab;
    TerminalPath *tpath;
    int *result;
{
    Rect r;
    int pclass, puse;
    Point portloc;
    HashEntry *entry;
    ResSimNode *node;

    // Ignore the top level cell
    if (scx->scx_use->cu_id == NULL) return 0;

    GeoTransRect(&scx->scx_trans, &lab->lab_rect, &r);

    // To be expanded.  Currently this handles digital signal inputs
    // and outputs, for standard cells.

    if (lab->lab_flags & PORT_DIR_MASK) {
	pclass = lab->lab_flags & PORT_CLASS_MASK;
	puse = lab->lab_flags & PORT_USE_MASK;

	// Ad hoc rule:  If port use is not declared, but port
	// direction is either INPUT or OUTPUT, then use SIGNAL is implied.

	if ((puse == 0) && ((pclass == PORT_CLASS_INPUT)
		|| (pclass == PORT_CLASS_OUTPUT)
		|| (pclass == PORT_CLASS_DEFAULT)))
	    puse = PORT_USE_SIGNAL;

	if (puse == PORT_USE_SIGNAL || puse == PORT_USE_CLOCK) {

	    if (lab->lab_flags & (PORT_DIR_NORTH | PORT_DIR_SOUTH))
		portloc.p_x = (r.r_xbot + r.r_xtop) >> 1;
	    else if (lab->lab_flags & (PORT_DIR_EAST | PORT_DIR_WEST))
		portloc.p_y = (r.r_ybot + r.r_ytop) >> 1;

	    if (lab->lab_flags & PORT_DIR_NORTH)
		portloc.p_y = r.r_ytop;
	    if (lab->lab_flags & PORT_DIR_SOUTH)
		portloc.p_y = r.r_ybot;
	    if (lab->lab_flags & PORT_DIR_EAST)
		portloc.p_x = r.r_xtop;
	    if (lab->lab_flags & PORT_DIR_WEST)
		portloc.p_x = r.r_xbot;

	    if ((pclass == PORT_CLASS_INPUT) || (pclass == PORT_CLASS_OUTPUT)
			|| (pclass == PORT_CLASS_DEFAULT)) {
		int len;
		char *nodename;

		// Port name is the instance name / pin name
		// To do:  Make use of tpath
		len = strlen(scx->scx_use->cu_id) + strlen(lab->lab_text) + 2;
		nodename = (char *) mallocMagic((unsigned) len);
		sprintf(nodename, "%s/%s", scx->scx_use->cu_id, lab->lab_text);

		entry = HashFind(&ResNodeTable, nodename);
		node = ResInitializeNode(entry);

		/* Digital outputs are drivers */
		if (pclass == PORT_CLASS_OUTPUT) node->status |= FORCE;

		node->drivepoint = portloc;
		node->status |= DRIVELOC | PORTNODE;
		node->rs_bbox = r;
		node->location = portloc;
		node->rs_ttype = lab->lab_type;
		node->type = lab->lab_type;

		*result = 0;
		freeMagic(nodename);
	    }
	}
    }
    return 0;	/* Keep the search going */
}

/*
 *-------------------------------------------------------------------------
 *
 * ResCheckBlackbox--
 *
 *	For standard cell parasitic extraction, search all children
 *	of cellDef for ports, and add each port to the list of nodes
 *	for extresist to process.  If the port use is "ground" or
 *	"power", then don't process the node.  If the port class is
 *	"output", then make this node a (forced) driver.
 *
 * Results: 0 if one or more nodes was created, 1 otherwise
 *
 * Side Effects: Adds driving nodes to the extresis network database.
 *
 *-------------------------------------------------------------------------
 */

int
ResCheckBlackbox(cellDef)
    CellDef *cellDef;
{
    int result = 1;
    SearchContext scx;
    CellUse dummy;

    dummy.cu_expandMask = 0;
    dummy.cu_transform = GeoIdentityTransform;
    dummy.cu_def = cellDef;
    dummy.cu_id = NULL;

    scx.scx_area = cellDef->cd_bbox;
    scx.scx_trans = GeoIdentityTransform;
    scx.scx_use = (CellUse *)&dummy;

    /* Do a search on all children */

    DBTreeSrLabels(&scx, &DBAllButSpaceAndDRCBits, 0, NULL,
		TF_LABEL_ATTACH, resPortFunc, (ClientData)&result);

    return result;
}

/*
 *-------------------------------------------------------------------------
 *
 * ResCheckPorts--
 *
 *	Subcircuit boundaries mark an area which is to be checked
 *	explicitly for geometry information.  Because there may be
 *	no devices in the subcircuit cell, we must find the ports
 *	into the subcircuit and declare them to be "driving" nodes so
 *	the extresis algorithm will treat them as being part of valid
 *	networks.
 *
 * Results: 0 if one or more nodes was created, 1 otherwise
 *
 * Side Effects: Adds driving nodes to the extresis network database.
 *
 *-------------------------------------------------------------------------
 */

int
ResCheckPorts(cellDef)
    CellDef *cellDef;
{
    Point portloc;
    Label *lab;
    HashEntry *entry;
    ResSimNode *node;
    int result = 1;

    for (lab = cellDef->cd_labels; lab; lab = lab->lab_next)
    {
	if (lab->lab_flags & PORT_DIR_MASK)
	{
	    /* Get drivepoint from the port connection direction(s) */
	    /* NOTE:  This is not rigorous! */

	    if (lab->lab_flags & (PORT_DIR_NORTH | PORT_DIR_SOUTH))
		portloc.p_x = (lab->lab_rect.r_xbot + lab->lab_rect.r_xtop) >> 1;
	    else if (lab->lab_flags & (PORT_DIR_EAST | PORT_DIR_WEST))
		portloc.p_y = (lab->lab_rect.r_ybot + lab->lab_rect.r_ytop) >> 1;

	    if (lab->lab_flags & PORT_DIR_NORTH)
		portloc.p_y = lab->lab_rect.r_ytop;
	    if (lab->lab_flags & PORT_DIR_SOUTH)
		portloc.p_y = lab->lab_rect.r_ybot;
	    if (lab->lab_flags & PORT_DIR_EAST)
		portloc.p_x = lab->lab_rect.r_xtop;
	    if (lab->lab_flags & PORT_DIR_WEST)
		portloc.p_x = lab->lab_rect.r_xbot;

	    entry = HashFind(&ResNodeTable, lab->lab_text);
	    result = 0;
	    if ((node = (ResSimNode *) HashGetValue(entry)) != NULL)
	    {
		TxPrintf("Port: name = %s exists, forcing drivepoint\n",
			lab->lab_text);
		TxPrintf("Location is (%d, %d); drivepoint (%d, %d)\n",
			node->location.p_x, node->location.p_y,
			portloc.p_x, portloc.p_y);
		TxFlush();
		node->drivepoint = portloc;
		node->status |= FORCE;
	    }
	    else
	    {
		/* This is a port, but it's merged with another node.	*/
		/* We have to make sure it's listed as a separate node	*/
		/* and a drivepoint.					*/

		node = ResInitializeNode(entry);
		TxPrintf("Port: name = %s is new node %p\n",
			lab->lab_text, (void *)node);
		TxPrintf("Location is (%d, %d); drivepoint (%d, %d)\n",
			portloc.p_x, portloc.p_y,
			portloc.p_x, portloc.p_y);
		node->location = portloc;
		node->drivepoint = node->location;
	    }
	    node->status |= DRIVELOC | PORTNODE;
	    node->rs_bbox = lab->lab_rect;
	    node->rs_ttype = lab->lab_type;
	    node->type = lab->lab_type;
	}
    }
    return result;
}

/*
 *-------------------------------------------------------------------------
 *
 * ResCheckSimNodes-- check to see if lumped resistance is greater than the
 *		      device resistance; if it is, Extract the net
 *		      resistance. If the maximum point to point resistance
 *		      in the extracted net is still creater than the
 *		      tolerance, then output the extracted net.
 *
 * Results: none
 *
 * Side Effects: Writes networks to .res.ext and .res.sim files.
 *
 *-------------------------------------------------------------------------
 */

void
ResCheckSimNodes(celldef, resisdata)
    CellDef	*celldef;
    ResisData	*resisdata;
{
    ResSimNode	*node;
    devPtr	*ptr;
    float	ftolerance, minRes, cumRes;
    int		failed1=0;
    int 	failed3=0;
    int		total =0;
    char	*outfile = celldef->cd_name;
    float	rctol = resisdata->tdiTolerance;
    float	rthresh = resisdata->rthresh;
    int		nidx = 1, eidx = 1;	/* node & segment counters for geom. */

    if (ResOptionsFlags & ResOpt_DoExtFile)
    {
	if (ExtLocalPath != NULL)
	    if (strcmp(ExtLocalPath, "."))
	    {
		char *namebuf;
		namebuf = mallocMagic(strlen(ExtLocalPath) + strlen(celldef->cd_name)
				+ 2);
		sprintf(namebuf, "%s/%s", ExtLocalPath, celldef->cd_name);
		outfile = namebuf;
	    }
	ResExtFile = PaOpen(outfile, "w", ".res.ext", ".", (char *)NULL, (char **)NULL);
	if (outfile != celldef->cd_name) freeMagic(outfile);
	outfile = celldef->cd_name;
    }
    else
    {
	ResExtFile = NULL;
    }
    if (ResOptionsFlags & ResOpt_DoLumpFile)
    {
        ResLumpFile = PaOpen(outfile, "w", ".res.lump", ".", (char *)NULL, (char **)NULL);
    }
    else
    {
     	ResLumpFile = NULL;
    }
    if (ResOptionsFlags & ResOpt_FastHenry)
    {
	char *geofilename;
        ResFHFile = PaOpen(outfile, "w", ".fh", ".", (char *)NULL, &geofilename);
	TxPrintf("Writing FastHenry-format geometry file \"%s\"\n", geofilename);
	ResPortIndex = 0;
    }
    else
    {
     	ResFHFile = NULL;
    }

    if ((ResExtFile == NULL && (ResOptionsFlags & ResOpt_DoExtFile))
         || ((ResOptionsFlags & ResOpt_DoLumpFile) && ResLumpFile == NULL)
         || ((ResOptionsFlags & ResOpt_FastHenry) && ResFHFile == NULL))
    {
     	TxError("Couldn't open output file\n");
	return;
    }

    /*
     * Write a scale line at the top of the .res.ext file, as the
     * scale may be different from the original .ext file.
     */

    if (ResExtFile != NULL)
    {
	fprintf(ResExtFile, "scale %d %d %g\n",
                ExtCurStyle->exts_resistScale,
                ExtCurStyle->exts_capScale,
                ExtCurStyle->exts_unitsPerLambda);
    }

     /*
      *	Write reference plane (substrate) definition and end statement
      * to the FastHenry geometry file.
      */
    if (ResOptionsFlags & ResOpt_FastHenry)
    {
	ResPrintReference(ResFHFile, ResRDevList, celldef);
    }

    for (node = ResOriginalNodes; node != NULL; node=node->nextnode)
    {
	HashEntry *he;

	if (SigInterruptPending) break;

	/* Ignore or include specified nodes */

	if (ResIncludeTable.ht_nEntries > 0)
	{
	    he = HashLookOnly(&ResIncludeTable, node->name);
	    if (he == NULL) continue;
	}
	else
	{
	    he = HashLookOnly(&ResIgnoreTable, node->name);
	    if (he != NULL) continue;
	}

	/* Has this node been merged away or is it marked as skipped? */
	/* If so, skip it */
	if ((node->status & (FORWARD | REDUNDANT)) ||
		((node->status & SKIP) &&
	  	(ResOptionsFlags & ResOpt_ExtractAll) == 0))
	    continue;

	ResCurrentNode = node->name;
	total++;

     	ResSortByGate(&node->firstDev);

	/* Find largest SD device connected to node. */

	minRes = FLT_MAX;
	gparams.rg_devloc = (Point *) NULL;
	gparams.rg_status = FALSE;
	gparams.rg_nodecap = node->capacitance;

	/* The following is only used if there is a drivepoint */
	/* to identify which tile the drivepoint is on.	       */

	gparams.rg_ttype = node->rs_ttype;

	for (ptr = node->firstDev; ptr != NULL; ptr = ptr->nextDev)
	{
	    RDev	*t1;
	    RDev	*t2;

	    if (ptr->terminal == GATE)
	    {
	       	break;
	    }
	    else
	    {
	       	/* Get cumulative resistance of all devices */
		/* with same connections.		    */

		cumRes = ptr->thisDev->resistance;
	        t1 = ptr->thisDev;
		for (; ptr->nextDev != NULL; ptr = ptr->nextDev)
		{
	            t1 = ptr->thisDev;
		    t2 = ptr->nextDev->thisDev;
		    if (t1->gate != t2->gate) break;
		    if ((t1->source != t2->source ||
			     t1->drain  != t2->drain) &&
			    (t1->source != t2->drain ||
			     t1->drain  != t2->source)) break;

		    /* Do parallel combination  */
		    if ((cumRes != 0.0) && (t2->resistance != 0.0))
		    {
			cumRes = (cumRes * t2->resistance) /
			      	       (cumRes + t2->resistance);
		    }
		    else
		    {
			cumRes = 0;
		    }
		}
		if (minRes > cumRes)
		{
		    minRes = cumRes;
		    gparams.rg_devloc = &t1->location;
		    gparams.rg_ttype = t1->rs_ttype;
		}
	    }
	}

	/* special handling for FORCE and DRIVELOC labels:  */
	/* set minRes = node->minsizeres if it exists, 0 otherwise */

	if (node->status & (FORCE|DRIVELOC))
	{
	    if (node->status & MINSIZE)
	    {
		minRes = node->minsizeres;
	    }
	    else
	    {
	      	minRes = 0;
	    }
	    if (node->status  & DRIVELOC)
	    {
	       	gparams.rg_devloc = &node->drivepoint;
		gparams.rg_status |= DRIVEONLY;
	    }
	    if (node->status & PORTNODE)
	    {
		/* The node is a port, not a device, so make    */
		/* sure rg_ttype is set accordingly.		*/
		gparams.rg_ttype = node->rs_ttype;
	    }
	}
	if ((gparams.rg_devloc == NULL) && (node->status & FORCE))
	{
    	    TxError("Node %s has force label but no drive point or "
			"driving device\n", node->name);
	}
	if ((minRes == FLT_MAX) || (gparams.rg_devloc == NULL))
	{
	    continue;
	}
	gparams.rg_bigdevres = (int)minRes * OHMSTOMILLIOHMS;
	if (minRes > resisdata->rthresh)
	    ftolerance =  minRes;
	else
	    ftolerance = resisdata->rthresh; 

	/*
	 *   Is the device resistance greater than the lumped node
	 *   resistance? If so, extract net.
	 */

	if ((node->resistance > ftolerance) || (node->status & FORCE) ||
		(ResOpt_ExtractAll & ResOptionsFlags))
	{
	    ResFixPoint	fp;

	    failed1++;
	    if (ResExtractNet(node, &gparams, outfile) != 0)
	    {
		/* On error, don't output this net, but keep going */
		if (node->type == TT_SPACE)
		    TxPrintf("Note:  Substrate node %s not extracted as network.\n",
				node->name);
		else
		    TxError("Error in extracting node %s\n", node->name);
	    }
	    else
	    {
		ResDoSimplify(ftolerance, rctol, &gparams);
		if (ResOptionsFlags & ResOpt_DoLumpFile)
		{
		    ResWriteLumpFile(node);
		}
		if (gparams.rg_maxres >= ftolerance  ||
			(ResOptionsFlags & ResOpt_ExtractAll))
		{
		    resNodeNum = 0;
		    failed3 += ResWriteExtFile(celldef, node, rctol,
				&nidx, &eidx);
		}
	    }
#ifdef PARANOID
	    ResSanityChecks(node->name, ResResList, ResNodeList, ResDevList);
#endif
	    ResCleanUpEverything();
	}
    }

    /*
     * Print out all device which have had at least one terminal changed
     * by resistance extraction.
     */

    if (ResOptionsFlags & ResOpt_DoExtFile)
    {
	ResPrintExtDev(ResExtFile, ResRDevList);
    }

    /*
     *	Write end statement to the FastHenry geometry file.
     * (Frequency range should be user-specified. . .)
     */

    if (ResOptionsFlags & ResOpt_FastHenry)
    {
	Label *lab;

	fprintf(ResFHFile, "\n.freq fmin=%2.1g fmax=%2.1g\n",
			resisdata->frequency, resisdata->frequency);

	/*----------------------------------------------------------------------*/
	/* Write (in comment lines) the order in which arguments are written	*/
	/* to a SPICE subcircuit call when Magic runs ext2spice (exttospice).	*/
	/* At present, it is the responsibility of the program that generates	*/
	/* SPICE from FastHenry output to use this information to appropriately	*/
	/* order the arguments in the ".subckt" definition.			*/
	/*----------------------------------------------------------------------*/

	fprintf(ResFHFile, "\n* Order of arguments to SPICE subcircuit call:\n");
	for (lab = celldef->cd_labels; lab != NULL; lab = lab->lab_next)
	    if (lab->lab_flags & PORT_DIR_MASK)
		fprintf(ResFHFile, "* %d %s\n", lab->lab_port, lab->lab_text);

	fprintf(ResFHFile, "\n.end\n");
    }

    /* Output statistics about extraction */

    if (total)
    {
        TxPrintf("Total Nets: %d\nNets extracted: "
		"%d (%f)\nNets output: %d (%f)\n", total, failed1,
		(float)failed1 / (float)total, failed3,
		(float)failed3 / (float)total);
    }
    else
    {
        TxPrintf("Total Nodes: %d\n",total);
    }

    /* close output files */

    if (ResExtFile != NULL)
     	(void) fclose(ResExtFile);

    if (ResLumpFile != NULL)
     	(void) fclose(ResLumpFile);

    if (ResFHFile != NULL)
	(void) fclose(ResFHFile);
}


/*
 *-------------------------------------------------------------------------
 *
 * ResFixUpConnections-- Changes the connection to a terminal of the sim
 *	device.  The new name is formed by appending .t# to the old name.
 *	The new name is added to the hash table of node names.
 *
 * Results:none
 *
 * Side Effects: Allocates new ResSimNodes. Modifies the terminal connections
 *	of sim Devices.
 *
 *-------------------------------------------------------------------------
 */

void
ResFixUpConnections(simDev, layoutDev, simNode, nodename)
    RDev		*simDev;
    resDevice		*layoutDev;
    ResSimNode		*simNode;
    char		*nodename;

{
    static char	newname[MAXNAME], oldnodename[MAXNAME];
    int		notdecremented;
    resNode	*gate, *source, *drain, *subs;

    /* If we aren't doing output (i.e. this is just a statistical run) */
    /* don't patch up networks.  This cuts down on memory use.		*/

    if ((ResOptionsFlags & (ResOpt_DoRsmFile | ResOpt_DoExtFile)) == 0)
	return;

    if (simDev->layout == NULL)
    {
	layoutDev->rd_status |= RES_DEV_SAVE;
	simDev->layout = layoutDev;
    }
    simDev->status |= TRUE;
    if (strcmp(nodename, oldnodename) != 0)
    {
	strcpy(oldnodename, nodename);
    }
    sprintf(newname, "%s%s%d", nodename, ".t", resNodeNum++);
    notdecremented = TRUE;

    if (simDev->gate == simNode)
    {
	if ((gate = layoutDev->rd_fet_gate) != NULL)
	{
	    /* Cosmetic addition: If the layout device already has a      */
	    /* name, the new one won't be used, so we decrement resNodeNum */
	    if (gate->rn_name != NULL)
	    {
	       	resNodeNum--;
		notdecremented = FALSE;
	    }

	    ResFixDevName(newname, GATE, simDev, gate);
	    gate->rn_name = simDev->gate->name;
     	    sprintf(newname, "%s%s%d", nodename, ".t", resNodeNum++);
	}
	else
	{
	    TxError("Missing gate connection of device at (%d %d) on net %s\n",
			layoutDev->rd_inside.r_xbot, layoutDev->rd_inside.r_ybot,
			nodename);
	    simNode->status |= DONTKILL;
	}
    }
    if (simDev->subs == simNode)
    {
	if ((subs = layoutDev->rd_fet_subs) != NULL)
	{
	    if (subs->rn_name != NULL && notdecremented)
	    {
	       	resNodeNum--;
		notdecremented = FALSE;
	    }
	    ResFixDevName(newname, SUBS, simDev, subs);
	    subs->rn_name = simDev->subs->name;
     	    sprintf(newname, "%s%s%d", nodename, ".t", resNodeNum++);
	}
	else
	{
	    TxError("Missing substrate connection of device at (%d %d) on net %s\n",
			layoutDev->rd_inside.r_xbot, layoutDev->rd_inside.r_ybot,
			nodename);
	    simNode->status |= DONTKILL;
	}
    }

    if (simDev->source == simNode)
    {
	/* Check for devices with only one terminal.  If it was cast as drain,	*/
	/* then swap it with the source so that the code below handles it	*/
	/* correctly.								*/

	if (layoutDev->rd_fet_source == NULL && layoutDev->rd_fet_drain != NULL)
	{
	    layoutDev->rd_fet_source = layoutDev->rd_fet_drain;
	    layoutDev->rd_fet_drain = (struct resnode *)NULL;
	}

     	if (simDev->drain == simNode)
	{
	    if ((layoutDev->rd_fet_source != NULL) &&
			(layoutDev->rd_fet_drain == NULL))
	    {
		/* Handle source/drain-tied devices */
		if (simDev->drain == simDev->source)
		    layoutDev->rd_fet_drain = layoutDev->rd_fet_source;
	    }

	    if (((source = layoutDev->rd_fet_source) != NULL) &&
	       	   ((drain = layoutDev->rd_fet_drain) != NULL))
	    {
	        if (source->rn_name != NULL && notdecremented)
		{
		    resNodeNum--;
		    notdecremented = FALSE;
		}
	        ResFixDevName(newname, SOURCE, simDev, source);
	        source->rn_name = simDev->source->name;
		(void)sprintf(newname, "%s%s%d", nodename, ".t", resNodeNum++);
	        if (drain->rn_name != NULL)  resNodeNum--;
	        ResFixDevName(newname, DRAIN, simDev, drain);
	        drain->rn_name = simDev->drain->name;
	       	/* one to each */
	    }
	    else
	    {
		TxError("Missing terminal connection of device at (%d %d) on net %s\n",
			layoutDev->rd_inside.r_xbot, layoutDev->rd_inside.r_ybot,
			nodename);
		simNode->status |= DONTKILL;
	    }
	}
	else
	{
	    if ((source = layoutDev->rd_fet_source) != NULL)
	    {
		if ((drain = layoutDev->rd_fet_drain) != NULL)
		{
		    if (source != drain)
		    {
			if (drain->rn_why & RES_NODE_ORIGIN)
			{
			   ResMergeNodes(drain, source, &ResNodeQueue,
					&ResNodeList);
		  	   ResDoneWithNode(drain);
			   source = drain;
			}
			else
			{
			   ResMergeNodes(source, drain, &ResNodeQueue,
					&ResNodeList);
		  	   ResDoneWithNode(source);
			   drain = source;
			}
		    }
		    layoutDev->rd_fet_drain = (resNode *)NULL;
	            if (source->rn_name != NULL)  resNodeNum--;
		}
		else
		{
             	    if (source->rn_name != NULL && notdecremented)
		    {
			resNodeNum--;
			notdecremented = FALSE;
		    }
		}
	        ResFixDevName(newname, SOURCE, simDev, source);
	        source->rn_name = simDev->source->name;

	    }
	    else
	    {
	       	TxError("Missing terminal connection of device at (%d %d) on net %s\n",
			layoutDev->rd_inside.r_xbot, layoutDev->rd_inside.r_ybot,
			nodename);
		simNode->status |= DONTKILL;
	    }
	}
    }
    else if (simDev->drain == simNode)
    {
	/* Check for devices with only one terminal.  If it was cast as source,	*/
	/* then swap it with the drain so that the code below handles it	*/
	/* correctly.								*/

	if (layoutDev->rd_fet_drain == NULL && layoutDev->rd_fet_source != NULL)
	{
	    layoutDev->rd_fet_drain = layoutDev->rd_fet_source;
	    layoutDev->rd_fet_source = (struct resnode *)NULL;
	}

	if ((drain = layoutDev->rd_fet_drain) != NULL)
	{
	    if ((source = layoutDev->rd_fet_source) != NULL)
	    {
		if (drain != source)
		{
		    if (source->rn_why & ORIGIN)
		    {
			 ResMergeNodes(source, drain, &ResNodeQueue,
				&ResNodeList);
		         ResDoneWithNode(source);
			 drain = source;
		    }
  		    else
		    {
 		         ResMergeNodes(drain, source, &ResNodeQueue,
				&ResNodeList);
		         ResDoneWithNode(drain);
			 source = drain;
		    }
		}
		layoutDev->rd_fet_source = (resNode *) NULL;
             	if (drain->rn_name != NULL)
		{
		    resNodeNum--;
		    notdecremented = FALSE;
		}
	    }
	    else
	    {
		if (drain->rn_name != NULL  && notdecremented)
		{
		    resNodeNum--;
		    notdecremented = FALSE;
		}
	    }
	    ResFixDevName(newname, DRAIN, simDev, drain);
	    drain->rn_name = simDev->drain->name;
	}
	else
	{
	    TxError("Missing terminal connection of device at (%d %d) on net %s\n",
			layoutDev->rd_inside.r_xbot, layoutDev->rd_inside.r_ybot,
			nodename);
	    simNode->status |= DONTKILL;
	}
    }
    else
	resNodeNum--;
}


/*
 *-------------------------------------------------------------------------
 *
 *  ResFixDevName-- Moves device connection to new node.
 *
 * Results:
 *	None.
 *
 * Side Effects: May create a new node. Creates a new device pointer.
 *
 *-------------------------------------------------------------------------
 */

void
ResFixDevName(line, type, device, layoutnode)
    char 	line[];
    int		type;
    RDev	*device;
    resNode	*layoutnode;

{
    HashEntry		*entry;
    ResSimNode		*node;
    devPtr		*tptr;

    if (layoutnode->rn_name != NULL)
    {
        entry = HashFind(&ResNodeTable, layoutnode->rn_name);
        node = ResInitializeNode(entry);

    }
    else
    {
        entry = HashFind(&ResNodeTable, line);
        node = ResInitializeNode(entry);
    }
    tptr = (devPtr *) mallocMagic((unsigned) (sizeof(devPtr)));
    tptr->thisDev = device;
    tptr->nextDev = node->firstDev;
    node->firstDev = tptr;
    tptr->terminal = type;
    switch(type)
    {
     	case GATE:
	    node->oldname = device->gate->name;
	    device->gate = node;
	    break;
     	case SOURCE:
	    node->oldname = device->source->name;
	    device->source = node;
	    break;
     	case DRAIN:
	    node->oldname = device->drain->name;
	    device->drain = node;
	    break;
     	case SUBS:
	    node->oldname = device->subs->name;
	    device->subs = node;
	    break;
	default:
	    TxError("Bad Terminal Specifier\n");
	    break;
    }
}

/*
 *-------------------------------------------------------------------------
 *
 * devSortFunc ---
 *
 *	qsort() sorting function for gates.  See description in
 *	ResSortByGate() below.
 *
 * Returns:
 *	1 or -1 depending on comparison result.  The devices are sorted
 *	by gate first, then source or drain.
 *
 * Side effects:
 *	qsort() reorders the indexed list of which dev1 and dev2 are
 *	components.
 *
 *-------------------------------------------------------------------------
 */

int
devSortFunc(rec1, rec2)
    devPtr **rec1, **rec2;
{
    devPtr *dev1 = *rec1;
    devPtr *dev2 = *rec2;
    RDev *rd1 = dev1->thisDev;
    RDev *rd2 = dev2->thisDev;

    if (dev1->terminal == GATE)
	return 1;
    else if (dev2->terminal == GATE)
	return -1;
    else if (rd1->gate > rd2->gate)
    	return 1;
    else if (rd1->gate == rd2->gate)
    {
	if ((dev1->terminal == SOURCE &&
		dev2->terminal == SOURCE &&
		rd1->drain > rd2->drain)    ||
		(dev1->terminal == SOURCE &&
		dev2->terminal == DRAIN &&
		rd1->drain > rd2->source)    ||
		(dev1->terminal == DRAIN &&
		dev2->terminal == SOURCE &&
		rd1->source > rd2->drain)    ||
		(dev1->terminal == DRAIN &&
		dev2->terminal == DRAIN &&
		rd1->source >  rd2->source))
	{
	    return 1;
	}
    }
    return -1;
}

/*
 *-------------------------------------------------------------------------
 *
 *  ResSortByGate -- sorts device pointers whose terminal field is either
 *	drain or source by gate node number, then by drain (source) number.
 *	This places devices with identical connections next to one
 *	another.
 *
 * Results: none
 *
 * Side Effects: modifies order of devices
 *
 *-------------------------------------------------------------------------
 */

void
ResSortByGate(DevpointerList)
    devPtr	**DevpointerList;
{
    devPtr	*working, **Devindexed;
    int		listlen, listidx;

    /* Linked lists are very slow to sort.  Create an indexed list  */
    /* and run qsort() to sort, then regenerate the links.	    */

    listlen = 0;
    for (working = *DevpointerList; working; working = working->nextDev) listlen++;
    if (listlen == 0) return;

    Devindexed = (devPtr **)mallocMagic(listlen * sizeof(devPtr *));
    listidx = 0;
    for (working = *DevpointerList; working; working = working->nextDev)
	Devindexed[listidx++] = working;

    qsort(Devindexed, (size_t)listlen, (size_t)sizeof(devPtr *), devSortFunc);

    for (listidx = 0; listidx < listlen - 1; listidx++)
	Devindexed[listidx]->nextDev = Devindexed[listidx + 1];
    Devindexed[listidx]->nextDev = NULL;

    *DevpointerList = Devindexed[0];
    freeMagic(Devindexed);
}

/*
 *-------------------------------------------------------------------------
 *
 * ResWriteLumpFile
 *
 * Results: none
 *
 * Side Effects:
 *
 *-------------------------------------------------------------------------
 */

void
ResWriteLumpFile(node)
    ResSimNode	*node;
{
    int	lumpedres;

    if (ResOptionsFlags & ResOpt_Tdi)
    {
	if (gparams.rg_nodecap != 0)
	{
	    lumpedres = (int)((gparams.rg_Tdi / gparams.rg_nodecap
			- (float)(gparams.rg_bigdevres)) / OHMSTOMILLIOHMS);
	}
	else
	    lumpedres = 0;
    }
    else
    {
	lumpedres = gparams.rg_maxres;
    }
    fprintf(ResLumpFile, "R %s %d\n", node->name, lumpedres);
}


/*
 *-------------------------------------------------------------------------
 *
 * ResAlignNodes --
 *	Attempt to put nodes onto a Manhattan grid.
 *	At the same time, assign height values to nodes and thickness
 *	values to resistors.
 *
 *-------------------------------------------------------------------------
 */

void
ResAlignNodes(nodelist, reslist)
    resNode	*nodelist;
    resResistor *reslist;
{
    resResistor *resistor;
    resNode	*node1;
    short	i;

    for (resistor = reslist; resistor->rr_nextResistor != NULL;
		resistor = resistor->rr_nextResistor)
    {
	/* Don't try to align nodes which came from split */
	/* tiles;  we assume that the geometry there is	  */
	/* supposed to be non-Manhattan.		  */

	if (resistor->rr_status & RES_DIAGONAL) continue;

	for (i = 0; i < 2; i++)
	{
	    node1 = resistor->rr_node[i];
	    if (resistor->rr_status & RES_EW)
	    {
		if (node1->rn_loc.p_y != resistor->rr_cl)
		{
		    if (node1->rn_status & RES_NODE_YADJ)
			TxError("Warning: contention over node Y position\n");
		    node1->rn_loc.p_y = resistor->rr_cl;
		    node1->rn_status |= RES_NODE_YADJ;
		}
	    }
	    else if (resistor->rr_status & RES_NS)
	    {
		if (node1->rn_loc.p_x != resistor->rr_cl)
		{
		    if (node1->rn_status & RES_NODE_XADJ)
			TxError("Warning: contention over node X position\n");
		    node1->rn_loc.p_x = resistor->rr_cl;
		    node1->rn_status |= RES_NODE_XADJ;
		}
	    }
	}
    }
}

/*
 *-------------------------------------------------------------------------
 *
 * ResWriteExtFile --
 *
 * Results:
 *	1 if output was generated
 *	0 if no output was generated
 *
 * Side Effects:
 *
 *-------------------------------------------------------------------------
 */

int
ResWriteExtFile(celldef, node, rctol, nidx, eidx)
    CellDef	*celldef;
    ResSimNode	*node;
    float	rctol;
    int		*nidx, *eidx;
{
    float	RCdev;
    char	*cp, newname[MAXNAME];
    devPtr	*ptr;
    resDevice	*layoutDev, *ResGetDevice();

    RCdev = gparams.rg_bigdevres * gparams.rg_nodecap;

    if ((node->status & FORCE) ||
		(ResOptionsFlags & ResOpt_ExtractAll) ||
		(ResOptionsFlags & ResOpt_Simplify) == 0 ||
		(rctol + 1) * RCdev < rctol * gparams.rg_Tdi)
    {
	ASSERT(gparams.rg_Tdi != -1, "ResWriteExtFile");
	(void)sprintf(newname,"%s", node->name);
        cp = newname + strlen(newname)-1;
        if (*cp == '!' || *cp == '#') *cp = '\0';
	if ((rctol + 1) * RCdev < rctol * gparams.rg_Tdi ||
	  			(ResOptionsFlags & ResOpt_Tdi) == 0)
	{
	    if ((ResOptionsFlags & (ResOpt_RunSilent | ResOpt_Tdi)) == ResOpt_Tdi)
	    {
		TxPrintf("Adding  %s; Tnew = %.2fns, Told = %.2fns\n",
		     	    node->name, gparams.rg_Tdi / Z_TO_P, RCdev / Z_TO_P);
	    }
        }
        for (ptr = node->firstDev; ptr != NULL; ptr=ptr->nextDev)
        {
	    if ((layoutDev = ResGetDevice(&ptr->thisDev->location, ptr->thisDev->rs_ttype)))
	    {
		ResFixUpConnections(ptr->thisDev, layoutDev, node, newname);
	    }
	}
        if (ResOptionsFlags & ResOpt_DoExtFile)
        {
	    ResPrintExtNode(ResExtFile, ResNodeList, node);
      	    ResPrintExtRes(ResExtFile, ResResList, newname);
        }
	if (ResOptionsFlags & ResOpt_FastHenry)
	{
	    if (ResResList)
		ResAlignNodes(ResNodeList, ResResList);
	    ResPrintFHNodes(ResFHFile, ResNodeList, node->name, nidx, celldef);
	    ResPrintFHRects(ResFHFile, ResResList, newname, eidx);
	}
	if (ResOptionsFlags & ResOpt_Geometry)
	{
	    if (ResResList)
		ResAlignNodes(ResNodeList, ResResList);
	    if (ResCreateCenterlines(ResResList, nidx, celldef) < 0)
		return 0;
	}
	return 1;
    }
    else return 0;
}

