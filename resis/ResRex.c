
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
#include "commands/commands.h"
#include "resis/resis.h"

#define INITFLATSIZE		1024
#define MAXNAME			1000

/* Time constants are produced by multiplying attofarads by milliohms,  */
/* giving zeptoseconds (yes, really.  Look it up).  This constant 	*/
/* converts zeptoseconds to picoseconds.				*/

#define Z_TO_P		1e-9
#define P_TO_Z		1e9

/* Table of nodes to ignore (manually specified) */
HashTable 	ResIgnoreTable;	    /* Hash table of nodes to ignore  */

/* Table of nodes to include (manually specified) */
HashTable 	ResIncludeTable;    /* Hash table of nodes to include  */

/* Table of nodes to force extraction of (manually specified) */
HashTable 	ResForceTable;    /* Hash table of nodes to include  */

/* Table of cells that have been processed */
HashTable	ResProcessedTable;

/* ResExtNode is a node read in from a .ext file */

HashTable 	ResNodeTable;   /* Hash table of ext file nodes   */
RDev		*ResRDevList;	/* Linked list of Ext devices	  */
int		resNodeNum;

extern ResExtNode *ResOriginalNodes;	/*Linked List of Nodes */

int	ResOptionsFlags = ResOpt_Simplify | ResOpt_DoExtFile;
char	*ResCurrentNode;

FILE	*ResExtFile;
FILE	*ResLumpFile;
FILE	*ResFHFile;

int	ResPortIndex;	/* Port ordering to backannotate into magic */

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
    ResConnect *sptr, *snext;
    ResExtNode  *node;
    int                result, idx;
    char       *devname;

    ResRDevList = NULL;
    ResOriginalNodes = NULL;

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
    /* Read in the .ext file */
    result = (ResReadExt(celldef) == 0);

    /* Clean up the EFDevTypes table */
    for (idx = 0; idx < EFDevNumTypes; idx++) freeMagic(EFDevTypes[idx]);
    EFDevNumTypes = 0;

    if (result)
    {
	/* Check for subcircuit ports */
	if (ResOptionsFlags & ResOpt_Blackbox)
	    ResCheckBlackbox(celldef);

	/* Extract networks for nets that require it. */
	if (!(ResOptionsFlags & ResOpt_FastHenry) ||
			DBIsSubcircuit(celldef))
	    ResCheckExtNodes(celldef, resisdata);

	if (ResOptionsFlags & ResOpt_Stats)
	    ResPrintStats((ResisData *)NULL, "");
    }

    /* Clean up */

    HashStartSearch(&hs);
    while((entry = HashNext(&ResNodeTable, &hs)) != NULL)
    {
	node = (ResExtNode *) HashGetValue(entry);
	tptr = node->devices;
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
	for (sptr = node->drivepoints; sptr; )
	{
	    snext = sptr->rc_next;
	    freeMagic((char *)sptr);
	    sptr = snext;
	}
	for (sptr = node->sinkpoints; sptr; )
	{
	    snext = sptr->rc_next;
	    freeMagic((char *)sptr);
	    sptr = snext;
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
 *  ResInit --
 *
 *	Initialize data and hash tables for running "extresist".
 *
 * Results:
 *	Returns a pointer to an allocated ResisData structure.  This
 *	structure is static and may be modified by repeated execution
 *	of "extresist" commands.
 *
 * Side effects:
 *	Allocates memory for the ResisData structure.
 *
 *-------------------------------------------------------------------------
 */

ResisData *
ResInit()
{
    static ResisData	*resisdata;
    static int init = 1;	/* Ensure initialization is done only once */
    int i;

    if (init)
    {
	resisdata = (ResisData *)mallocMagic(sizeof(ResisData));

	for (i = 0; i < NT; i++)
	{
            TTMaskZero(&(ResCopyMask[i]));
	    TTMaskSetMask(&ResCopyMask[i], &DBConnectTbl[i]);
     	}

	/* Defaults:
	 * (1) rthresh:  Only extract networks with a lumped resistance > 10 ohms
	 * (2) minres:   Prune resistors < 1 ohm when possible
	 * (3) mindelay: Only extract networks with a calculated delay > 1 ps
	 */
	resisdata->rthresh = 10000.0;
	resisdata->minres = 1000.0;
	resisdata->mindelay = 1.0e9;	/* 1ps = 1.0e9zs
	resisdata->frequency = 10e6;	/* 10 MHz default */

	HashInit(&ResIgnoreTable, INITFLATSIZE, HT_STRINGKEYS);
	HashInit(&ResForceTable, INITFLATSIZE, HT_STRINGKEYS);
	HashInit(&ResIncludeTable, INITFLATSIZE, HT_STRINGKEYS);
	init = 0;
    }

    /* Reset global parameters */
    resisdata->rg_ttype = TT_SPACE;
    resisdata->rg_Tdi = 0.0;
    resisdata->rg_nodecap = 0.0;
    resisdata->rg_maxres = 0.0;
    resisdata->rg_tilecount = 0;
    resisdata->rg_status = 0;
    resisdata->rg_devloc = NULL;
    resisdata->rg_name = NULL;

    return resisdata;
}

/*
 *-------------------------------------------------------------------------
 *
 * CmdExtResis--
 *
 *	Reads in a .ext file and layout, and produces patches to the
 *	.ext files that include resistors and subnets.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Produces a .res.ext file with all nets that require subdivision
 *	into resistive segments.
 *
 *-------------------------------------------------------------------------
 */

void
CmdExtResis(win, cmd)
    MagWindow *win;
    TxCommand *cmd;
{
    int i, j, k, option, value, saveFlags;

    CellDef	*mainDef;
    CellUse	*selectedUse;
    ResisData	*resisdata;
    Plane	*savePlane;
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
	"threshold [value]    set minimum network resistance threshold (milliohms)",
	"minresist [value]    set minimum individual resistance threshold (milliohms)",
	"mindelay  [value]    set minimum network delay threshold (picoseconds)",
	"tolerance [value]    set ratio between resistor and device resistance (deprecated)",
	"simplify [on/off]    turn on/off simplification of resistor nets",
	"extout   [on/off]    turn on/off writing of .res.ext file",
	"lumped   [on/off]    turn on/off writing of updated lumped resistances",
	"silent   [on/off]    turn on/off printing of net statistics",
	"debug	  [on/off]    turn on/off printing of detailed information",
	"skip     mask        don't extract these types",
	"force	  names	      force these nets to be extracted",
	"ignore	  names	      don't extract these nets",
	"include  names	      extract only these nets",
	"box      type        extract the signal under the box on layer type",
	"cell	  cellname    extract the network for the cell named cellname",
	"blackbox [on/off]    treat subcircuits with ports as black boxes",
	"fasthenry [freq]     extract subcircuit network geometry into .fh file",
	"geometry	      extract network centerline geometry (experimental)",
	"stats		      print extresist statistics",
	"help                 print this message",
	NULL
    };

typedef enum {
	RES_BAD=-2, RES_AMBIG, RES_ALL,
	RES_THRESH, RES_MINRES, RES_MINDELAY, RES_TOL,
	RES_SIMP, RES_EXTOUT, RES_LUMPED, RES_SILENT, RES_DEBUG,
	RES_SKIP, RES_FORCE, RES_IGNORE, RES_INCLUDE, RES_BOX,
	RES_CELL, RES_BLACKBOX, RES_FASTHENRY, RES_GEOMETRY,
	RES_STATS, RES_HELP, RES_RUN
} ResOptions;

    resisdata = ResInit();

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
	case RES_MINRES:
	    if (cmd->tx_argc > 2)
	    {
		resisdata->minres = MagAtof(cmd->tx_argv[2]);
		if (resisdata->minres <= 0)
		{
		    TxError("Usage:  %s minres [value]\n", cmd->tx_argv[0]);
			return;
		}
	    }
	    else
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp,
			Tcl_NewDoubleObj((double)resisdata->minres));
#else
		TxPrintf("Minimum network resistance is %g milliohms.\n",
			resisdata->minres);
#endif
	    }
	    return;

	case RES_MINDELAY:
	    if (cmd->tx_argc > 2)
	    {
		resisdata->mindelay = MagAtof(cmd->tx_argv[2]) * P_TO_Z;
		if (resisdata->mindelay < 0)
		{
		    TxError("Usage:  %s mindelay [value]\n", cmd->tx_argv[0]);
			return;
		}
	    }
	    else
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp,
			Tcl_NewDoubleObj((double)resisdata->mindelay * Z_TO_P));
#else
		TxPrintf("Minimum network delay is %g picoseconds.\n",
			resisdata->mindelay);
#endif
	    }
	    return;

	case RES_TOL:
	    TxError("Note:  This option has been deprecated and is unused.\n");
	    return;

	case RES_THRESH:
	    if (cmd->tx_argc > 2)
	    {
		resisdata->rthresh = MagAtof(cmd->tx_argv[2]);
		if (resisdata->rthresh < 0)
		{
		    TxError("Usage:  %s threshold [value]\n", cmd->tx_argv[0]);
			return;
		}
	    }
	    else
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp,
			Tcl_NewDoubleObj((double)resisdata->rthresh));
#else
		TxPrintf("Minimum resistor threshold is %g mohms.\n",
				resisdata->rthresh);
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
			| ResOpt_Simplify);
	    break;

	case RES_FASTHENRY:
	    if (cmd->tx_argc == 3)
	    {
		  double tmpf = strtod(cmd->tx_argv[2], &endptr);
		  if (endptr == cmd->tx_argv[2])
		  {
		      TxError("Cannot parse frequency value.  Assuming default\n");
		      TxError("Frequency = %2.1f Hz\n", resisdata->frequency);
		  }
		  else
		      resisdata->frequency = (float)tmpf;
	    }
	    saveFlags = ResOptionsFlags;
	    ResOptionsFlags |= ResOpt_FastHenry | ResOpt_ExtractAll;
	    ResOptionsFlags &= ~(ResOpt_DoExtFile | ResOpt_DoLumpFile
			| ResOpt_Simplify);
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
	case RES_STATS:
	    if (cmd->tx_argc == 2)
	    {
		value = (ResOptionsFlags & ResOpt_Stats) ?
			TRUE : FALSE;
		TxPrintf("%s\n", onOff[value]);
	    }
	    else
	    {
		value = Lookup(cmd->tx_argv[2], onOff);

		if (value)
	      	   ResOptionsFlags |= ResOpt_Stats;
		else
	      	   ResOptionsFlags &= ~ResOpt_Stats;
	    }
	    return;

	case RES_SIMP:
	    /* Enable or disable resistor network simplification.  Usually
	     * enabled in conjunction with TDi calculations (see below).
	     */
	    if (cmd->tx_argc == 2)
	    {
		value = (ResOptionsFlags & ResOpt_Simplify) ?
			TRUE : FALSE;
		TxPrintf("%s\n", onOff[value]);
	    }
	    else
	    {
		value = Lookup(cmd->tx_argv[2], onOff);

		if (value)
	      	   ResOptionsFlags |= ResOpt_Simplify;
		else
	      	   ResOptionsFlags &= ~ResOpt_Simplify;
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

	case RES_DEBUG:
	    if (cmd->tx_argc == 2)
	    {
		value = (ResOptionsFlags & ResOpt_Debug) ?
			TRUE : FALSE;
		TxPrintf("%s\n", onOff[value]);
	    }
	    else
	    {
		value = Lookup(cmd->tx_argv[2], onOff);
		if (value)
	      	   ResOptionsFlags |= ResOpt_Debug;
		else
	      	   ResOptionsFlags &= ~ResOpt_Debug;
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

	case RES_FORCE:
	    if (cmd->tx_argc > 2)
	    {
		if (!strcasecmp(cmd->tx_argv[2], "none"))
		{
		    /* Kill and reinitialize the table of forced nets */
		    HashKill(&ResForceTable);
		    HashInit(&ResForceTable, INITFLATSIZE, HT_STRINGKEYS);
		}
		else
		    HashFind(&ResForceTable, cmd->tx_argv[2]);
	    }
	    else
	    {
		HashSearch hs;
		HashEntry *entry;

		/* List all net names that are being ignored */
		HashStartSearch(&hs);
		while((entry = HashNext(&ResForceTable, &hs)) != NULL)
		    TxPrintf("%s ", (char *)entry->h_key.h_name);
		TxPrintf("\n");
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
		ResExtNode	lnode;

		if (ToolGetBoxWindow((Rect *) NULL, (int *) NULL) == NULL)
		{
		    TxError("Sorry, the box must appear in one of the windows.\n");
		    return;
		}

	     	if (cmd->tx_argc != 3) return;
		tt = DBTechNoisyNameType(cmd->tx_argv[2]);
		if (tt <= 0 || ToolGetBox(&def, &rect)== FALSE) return;
		resisdata->rg_devloc = &rect.r_ll;
		resisdata->rg_ttype = tt;
		resisdata->rg_status = DRIVEONLY;
		oldoptions = ResOptionsFlags;
		ResOptionsFlags = ResOpt_DoSubstrate | ResOpt_Signal | ResOpt_Box;
		lnode.location = rect.r_ll;
		lnode.type = tt;
		if (ResExtractNet(&lnode, resisdata, NULL) != 0) return;
		ResPrintResistorList(stdout, ResResList);
		ResPrintDeviceList(stdout, ResRDevList);
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

    resisdata->mainDef = mainDef;
    resisdata->savePlanes = (struct saveList *)NULL;

    /* Do subcircuits (if any) first */
    HashInit(&ResProcessedTable, INITFLATSIZE, HT_STRINGKEYS);
    if (!(ResOptionsFlags & ResOpt_Blackbox))
	DBCellEnum(mainDef, resSubcircuitFunc, (ClientData)resisdata);

    HashFind(&ResProcessedTable, mainDef->cd_name);

    /* Prepare the substrate for resistance extraction */
    savePlane = extResPrepSubstrate(mainDef);
    if (savePlane != NULL)
    {
	struct saveList *newsl;
	newsl = (struct saveList *)mallocMagic(sizeof(struct saveList));
	newsl->sl_plane = savePlane;
	newsl->sl_def = mainDef;
	newsl->sl_next = resisdata->savePlanes;
	resisdata->savePlanes = newsl;
    }
    ExtResisForDef(mainDef, resisdata);
    HashKill(&ResProcessedTable);

    /* Revert substrate planes */
    free_magic1_t mm1 = freeMagic1_init();
    for (sl = resisdata->savePlanes; sl; sl = sl->sl_next)
    {
	ExtRevertSubstrate(sl->sl_def, sl->sl_plane);
	freeMagic1(&mm1, sl);
    }
    freeMagic1_end(&mm1);

    /* turn back on undo stuff */
    UndoEnable();

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
    CellDef	*cellDef = cellUse->cu_def;
    Plane	*savePlane;

    if (DBIsSubcircuit(cellDef))
    {
	/* Check if this cell has been processed */
	if (HashLookOnly(&ResProcessedTable, cellDef->cd_name)) return 0;
	HashFind(&ResProcessedTable, cellDef->cd_name);

	/* Prepare the substrate for resistance extraction */
	savePlane = extResPrepSubstrate(cellDef);
	if (savePlane != NULL)
	{
	    struct saveList *newsl;
	    newsl = (struct saveList *)mallocMagic(sizeof(struct saveList));
	    newsl->sl_plane = savePlane;
	    newsl->sl_def = cellDef;
	    newsl->sl_next = rdata->savePlanes;
	    rdata->savePlanes = newsl;
	}
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
    ResExtNode *node;
    ResConnect *newdriver;

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
		node = ResExtInitNode(entry);

		/* Digital outputs are drivers */
		if (pclass == PORT_CLASS_OUTPUT) node->status |= FORCE;

		/* Create new node drivepoint */
		newdriver = (ResConnect *)mallocMagic(sizeof(ResConnect));

		newdriver->rc_type = lab->lab_type;
		newdriver->rc_rect = r;
		newdriver->rc_next = node->drivepoints;
		node->drivepoints = newdriver;

		node->status |= DRIVELOC | PORTNODE;

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
    Label *lab;
    HashEntry *entry;
    ResExtNode *node;
    ResConnect *newdriver;
    int result = 1;

    for (lab = cellDef->cd_labels; lab; lab = lab->lab_next)
    {
	if (lab->lab_flags & PORT_DIR_MASK)
	{
	    entry = HashFind(&ResNodeTable, lab->lab_text);
	    result = 0;
	    if ((node = (ResExtNode *) HashGetValue(entry)) != NULL)
	    {
		TxPrintf("Port: name = %s exists, forcing drivepoint\n",
			lab->lab_text);
		TxPrintf("Location is (%d, %d); drivepoint (%d, %d)\n",
			node->location.p_x, node->location.p_y,
			lab->lab_rect.r_xbot, lab->lab_rect.r_ybot);
		TxFlush();

		node->status |= FORCE;
	    }
	    else
	    {
		/* This is a port, but it's merged with another node.	*/
		/* We have to make sure it's listed as a separate node	*/
		/* and a drivepoint.					*/

		node = ResExtInitNode(entry);
		TxPrintf("Port: name = %s is new node %p\n",
			lab->lab_text, (void *)node);
		TxPrintf("Location is (%d, %d); drivepoint (%d, %d)\n",
			lab->lab_rect.r_xbot, lab->lab_rect.r_ybot,
			lab->lab_rect.r_xtop, lab->lab_rect.r_ytop);
		TxFlush();

		node->status |= REDUNDANT;
	    }

	    newdriver = (ResConnect *)mallocMagic(sizeof(ResConnect));
	    newdriver->rc_rect = lab->lab_rect;
	    newdriver->rc_type = lab->lab_type;
	    newdriver->rc_next = node->drivepoints;
	    node->drivepoints = newdriver;

	    node->status |= DRIVELOC | PORTNODE;
	}
    }
    return result;
}

/*
 *-------------------------------------------------------------------------
 *
 * ResProcessNode ---
 *
 *	Do resistance extraction for a single network.
 *
 * Results:
 *	1 if node was processed, 0 otherwise.
 *
 * Side effects:
 *	Many.  Update the totals for number of nets extracted and
 *	number of nets output in the pointers in the argument list.
 *
 *-------------------------------------------------------------------------
 */

int
ResProcessNode(
    ResExtNode	*node,		/* Node record for network */
    CellDef	*celldef,	/* Cell def being processed */	
    ResisData	*resisdata,	/* Extraction parameters kept here */
    char	*outfile,	/* Name of output file */
    int		*num_extracted,	/* Number of nets extracted so far */
    int		*num_output)	/* Number of nets output so far */
{
    HashEntry	*he;
    devPtr	*ptr;
    int		totWL, maxWL = 0;
    int		nidx = 1, eidx = 1;	/* node & segment counters for geom. */
    bool	processThis;

    /* Ignore or include specified nodes */

    if (ResIncludeTable.ht_nEntries > 0)
    {
	he = HashLookOnly(&ResIncludeTable, node->name);
	if (he == NULL) return 0;
    }
    else
    {
	he = HashLookOnly(&ResIgnoreTable, node->name);
	if (he != NULL) return 0;
    }

    /* Has this node been merged away or is it marked as skipped? */
    /* If so, skip it */

    if ((node->status & (FORWARD | REDUNDANT)) ||
		((node->status & SKIP) &&
	  	(ResOptionsFlags & ResOpt_ExtractAll) == 0))
	return 0;

    ResCurrentNode = node->name;
    ResSortByGate(&node->devices);

    resisdata->rg_devloc = (Point *) NULL;
    resisdata->rg_status = FALSE;
    resisdata->rg_nodecap = node->capacitance;

    /* The following is only used if there is a drivepoint	*/
    /* to identify which tile the drivepoint is on.		*/

    resisdata->rg_ttype = node->type;

    /* Find the device with largest drive on the net;  this will be
     * assumed to be the primary net driver.  Make sure that devices
     * in parallel have been combined to determine which device is the
     * largest.  If there are no drivers on the net, then choose a
     * source or sink point as the driver.  The driver will be used as
     * the starting point to determine the longest driver-to-terminal
     * resistance or delay.
     */ 

    for (ptr = node->devices; ptr != NULL; ptr = ptr->nextDev)
    {
	RDev	*t1, *t2;

	/* Ignore devices unless they are FETs or BJTs */
	switch (ptr->thisDev->rs_devptr->exts_deviceClass)
	{
	    case DEV_FET:
	    case DEV_MOSFET:
	    case DEV_ASYMMETRIC:
	    case DEV_MSUBCKT:
	    case DEV_BJT:
		break;
	    default:
		continue;
	}

	/* Devices have been sorted with those being gate terminals
	 * at the end, so once the first gate terminal is reached,
	 * there are no more drivers to be found.
	 */
	if (ptr->terminal == GATE)
	    break;
	else if (ptr->terminal != SUBS)
	{
	    /* Sorting has put all parallel devices together, so
	     * combine their total W/L
	     */
	    totWL = ptr->thisDev->rs_wl;
	    t1 = ptr->thisDev;
	    for (; ptr->nextDev != NULL; ptr = ptr->nextDev)
	    {
		t1 = ptr->thisDev;
		t2 = ptr->nextDev->thisDev;
		if (t1->gate != t2->gate) break;
		if ((t1->source != t2->source || t1->drain != t2->drain) &&
			(t1->source != t2->drain || t2->drain != t2->source))
		    break;

		/* Sum the W/L value of devices in parallel */
		totWL += t2->rs_wl;
	    }
	    if (totWL > maxWL)
	    {
		maxWL = totWL;
		resisdata->rg_devloc = &t1->location;
		resisdata->rg_ttype = t1->rs_ttype;
	    }
	}
    }

    /* Special handling for DRIVELOC labels: */

    if (node->status & DRIVELOC)
    {
	if ((node->status & DRIVELOC) && (node->drivepoints != NULL))
	{
	    resisdata->rg_devloc = &node->drivepoints->rc_rect.r_ll;
	    resisdata->rg_ttype = node->drivepoints->rc_type;
	    resisdata->rg_status |= DRIVEONLY;
	}

	/* If there is no drivepoint but there is a sinkpoint, use that.
	 * The "drivers" and "sinks" are arbitrary, anyway, and any of
	 * them can be considered a node driver.
	 */
	else if ((node->status & DRIVELOC) && (node->sinkpoints != NULL))
	{
	    resisdata->rg_devloc = &node->sinkpoints->rc_rect.r_ll;
	    resisdata->rg_ttype = node->sinkpoints->rc_type;
	    resisdata->rg_status |= DRIVEONLY;
	}
    }

    /* If no driving device was found and node was not marked with a
     * DRIVELOC label, then use the first drivepoint as the driver.
     * If there are no drivepoints, then use a sinkpoint.  Using sinkpoints
     * (design is hierarchical) is problematic because there is no information
     * on whether the sinkpoint leads to a driver in the child cell, which
     * needs to be addressed to correctly handle hierarchical R-C extraction.
     */

    if (resisdata->rg_devloc == NULL)
    {
	if (node->drivepoints != NULL)
	{
	    resisdata->rg_devloc = &node->drivepoints->rc_rect.r_ll;
	    resisdata->rg_ttype = node->drivepoints->rc_type;
	    resisdata->rg_status |= DRIVEONLY;
	}
	else if (node->sinkpoints != NULL)
	{
	    resisdata->rg_devloc = &node->sinkpoints->rc_rect.r_ll;
	    resisdata->rg_ttype = node->sinkpoints->rc_type;
	    resisdata->rg_status |= DRIVEONLY;
	}
    }

    if ((resisdata->rg_devloc == NULL) && (node->status & FORCE))
    {
    	TxError("Node %s has force label but no drive point or "
			"driving device\n", node->name);
    }
    if (resisdata->rg_devloc == NULL)
	return 1;

    /*
     * Extract the net if:
     * 1. The lumped node resistance is greater than the minimum specified AND
     * 2. The maximum net delay is greater than the minimum delay specified OR
     * 3. "extresist all" has been invoked.
     *
     * The purpose of (1 AND 2) is to allow the cutoff to be specified either
     * by absolute resistance ("extresist threshold") or by effective signal
     * propagation delay ("extresist mindelay").  If either one is set to zero,
     * it will be ignored, although they can also be used in combination.
     *
     * Note that the substrate net has a capacitance to substrate of zero by
     * definition, and should be the only net with a zero capacitance.
     * Therefore, for any node with zero capacitance, use only the lumped
     * resistance to determine whether or not to process the node.
     */

    processThis = FALSE;
    if (ResOpt_ExtractAll & ResOptionsFlags)
	processThis = TRUE;
    else if ((node->capacitance > 0) && (node->resistance > resisdata->rthresh) &&
		(node->resistance * node->capacitance > resisdata->mindelay))
	processThis = TRUE;
    else if ((node->capacitance == 0) && (node->resistance > resisdata->rthresh))
	processThis = TRUE;
    else if (HashLookOnly(&ResForceTable, node->name) != NULL)
	processThis = TRUE;

    if (processThis)
    {
	ResFixPoint fp;

	/* Diagnostic */
	if (!(ResOptionsFlags & ResOpt_RunSilent))
	{
	    TxPrintf("Extracting %s", node->name);
	    if (ResOptionsFlags & ResOpt_Debug)
		TxPrintf(" (Rnode = %.2fohm ; Rthresh = %.2fohm)",
			node->resistance * MILLIOHMSTOOHMS,
			resisdata->rthresh * MILLIOHMSTOOHMS);
	    TxPrintf("\n");
	}

	(*num_extracted)++;
	if (ResExtractNet(node, resisdata, outfile) != 0)
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
	    ResDoSimplify(resisdata);
	    if (ResOptionsFlags & ResOpt_DoLumpFile)
		ResWriteLumpFile(node, resisdata);

	    resNodeNum = 0;
	    (*num_output) += ResWriteExtFile(celldef, node, resisdata,
				&nidx, &eidx);
	}
#ifdef PARANOID
	ResSanityChecks(node->name, ResResList, ResNodeList, ResDevList);
#endif
	ResCleanUpEverything();
    }
    return 1;
}

/*
 *-------------------------------------------------------------------------
 *
 * ResCheckExtNodes-- check to see if lumped resistance is greater than the
 *		      device resistance; if it is, Extract the net
 *		      resistance. If the maximum point to point resistance
 *		      in the extracted net is still creater than the
 *		      tolerance, then output the extracted net.
 *
 * Results: none
 *
 * Side Effects: Writes networks to .res.ext files.
 *
 *-------------------------------------------------------------------------
 */

void
ResCheckExtNodes(celldef, resisdata)
    CellDef	*celldef;
    ResisData	*resisdata;
{
    ResExtNode	*node;
    int		numext = 0;	/* Number of nets extracted */
    int 	numout = 0;	/* Number of nets output */
    int		total = 0;	/* Total number of nets processed */
    char	*outfile = celldef->cd_name;

    if (ResOptionsFlags & ResOpt_DoExtFile)
    {
	char *alloc = NULL;
	if (ExtLocalPath != NULL)
	    if (strcmp(ExtLocalPath, "."))
	    {
		char *namebuf;
		namebuf = alloc = mallocMagic(strlen(ExtLocalPath) +
				strlen(celldef->cd_name) + 2);
		sprintf(namebuf, "%s/%s", ExtLocalPath, celldef->cd_name);
		outfile = namebuf;
	    }
	ResExtFile = PaOpen(outfile, "w", ".res.ext", ".", (char *)NULL, (char **)NULL);
	if (alloc) freeMagic(alloc);
	outfile = celldef->cd_name;
    }
    else
	ResExtFile = NULL;

    if (ResOptionsFlags & ResOpt_DoLumpFile)
        ResLumpFile = PaOpen(outfile, "w", ".res.lump", ".", (char *)NULL, (char **)NULL);
    else
     	ResLumpFile = NULL;

    if (ResOptionsFlags & ResOpt_FastHenry)
    {
	char *geofilename;
        ResFHFile = PaOpen(outfile, "w", ".fh", ".", (char *)NULL, &geofilename);
	TxPrintf("Writing FastHenry-format geometry file \"%s\"\n", geofilename);
	ResPortIndex = 0;
    }
    else
     	ResFHFile = NULL;

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
	fprintf(ResExtFile, "scale %d %d %g\n",
                ExtCurStyle->exts_resistScale,
                ExtCurStyle->exts_capScale,
                ExtCurStyle->exts_unitsPerLambda);

     /*
      *	Write reference plane (substrate) definition and end statement
      * to the FastHenry geometry file.
      */
    if (ResOptionsFlags & ResOpt_FastHenry)
	ResPrintReference(ResFHFile, ResRDevList, celldef);

    for (node = ResOriginalNodes; node != NULL; node = node->nextnode)
    {
	if (SigInterruptPending) break;
	total += ResProcessNode(node, celldef, resisdata, outfile,
			&numext, &numout);
    }

    /*
     * Print out all device which have had at least one terminal changed
     * by resistance extraction.
     */

    if (ResOptionsFlags & ResOpt_DoExtFile)
	ResPrintExtDev(ResExtFile, ResRDevList);

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
        TxPrintf("Total Nets: %d\nNets extracted: "
		"%d (%f)\nNets output: %d (%f)\n", total, numext,
		(float)numext / (float)total, numout,
		(float)numout / (float)total);
    else
        TxPrintf("Total Nodes: %d\n",total);

    /* close output files */

    if (ResExtFile != NULL) (void) fclose(ResExtFile);
    if (ResLumpFile != NULL) (void) fclose(ResLumpFile);
    if (ResFHFile != NULL) (void) fclose(ResFHFile);
}

/*
 *-------------------------------------------------------------------------
 *
 * ResFixUpDrivepoints --
 *
 *	Change the name of a connection to a drivepoint (upward
 *	connection in the hierarchy).  If there is an existing
 *	drivepoint that has the name of the node, then keep it
 *	as-is.  If not, then assign the original name of the node
 *	to the drivepoint.  All other drivepoints get a ".uX"
 *	suffix added to the node name ("u" for "upward").
 *
 *-------------------------------------------------------------------------
 */

void
ResFixUpDrivepoints(ResConnect *driver,
    ResExtNode *node,
    char *nodename)
{
    /* To be completed */
}

/*
 *-------------------------------------------------------------------------
 *
 * ResFixUpSinkpoints --
 *
 *	Change the name of a connection to a sinkpoint (downward
 *	connection in the hierarchy).  All sinkpoints get a ".dX"
 *	suffix added to the node name ("d" for "downward").
 *
 *-------------------------------------------------------------------------
 */

void
ResFixUpSinkpoints(ResConnect *sink,
    ResExtNode *node,
    char *nodename)
{
    /* To be completed */
}

/*
 *-------------------------------------------------------------------------
 *
 * ResFixUpConnections --
 *
 *	Changes the connection to a terminal of a device.
 * 	The new name is formed by appending .t# to the old name.
 *	The new name is added to the hash table of node names.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Allocates new ResExtNodes. Modifies the terminal connections
 *	of devices.
 *
 *-------------------------------------------------------------------------
 */

void
ResFixUpConnections(extDev, layoutDev, extNode, nodename)
    RDev		*extDev;
    resDevice		*layoutDev;
    ResExtNode		*extNode;
    char		*nodename;
{
    static char	newname[MAXNAME], oldnodename[MAXNAME];
    int		notdecremented;
    ExtDevice  *devptr;
    resNode	*gate, *source, *drain, *subs;
    bool	doPermute = FALSE;

    /* If we aren't doing output (i.e. this is just a statistical run)	*/
    /* don't patch up networks.  This cuts down on memory use.		*/

    if ((ResOptionsFlags & ResOpt_DoExtFile) == 0)
	return;

    /* Check if device has symmetric source and drain, which will
     * force a check for permutations of source and drain.
     */
    devptr = extDev->rs_devptr;
    if (devptr->exts_deviceSDCount > 1)
	if (TTMaskIsZero(&devptr->exts_deviceSDTypes[1]) ||
		TTMaskEqual(&devptr->exts_deviceSDTypes[0],
		&devptr->exts_deviceSDTypes[1]))
	    doPermute = TRUE;

    if (extDev->layout == NULL)
    {
	layoutDev->rd_status |= RES_DEV_SAVE;
	extDev->layout = layoutDev;
    }
    extDev->status |= TRUE;
    if (strcmp(nodename, oldnodename) != 0)
    {
	strcpy(oldnodename, nodename);
    }
    sprintf(newname, "%s%s%d", nodename, ".t", resNodeNum++);
    notdecremented = TRUE;

    if (extDev->gate == extNode)
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

	    ResFixDevName(newname, GATE, extDev, gate);
	    gate->rn_name = extDev->gate->name;
     	    sprintf(newname, "%s%s%d", nodename, ".t", resNodeNum++);
	}
	else
	{
	    TxError("Missing gate connection of device at (%d %d) on net %s\n",
			layoutDev->rd_inside.r_xbot, layoutDev->rd_inside.r_ybot,
			nodename);
	    extNode->status |= DONTKILL;
	}
    }
    if (extDev->subs == extNode)
    {
	if ((subs = layoutDev->rd_fet_subs) != NULL)
	{
	    if (subs->rn_name != NULL && notdecremented)
	    {
	       	resNodeNum--;
		notdecremented = FALSE;
	    }
	    ResFixDevName(newname, SUBS, extDev, subs);
	    subs->rn_name = extDev->subs->name;
     	    sprintf(newname, "%s%s%d", nodename, ".t", resNodeNum++);
	}
	else
	{
	    TxError("Missing substrate connection of device at (%d %d) on net %s\n",
			layoutDev->rd_inside.r_xbot, layoutDev->rd_inside.r_ybot,
			nodename);
	    extNode->status |= DONTKILL;
	}
    }

    if ((extDev->source == extNode) && doPermute)
    {
	/* Check for devices with only one terminal.  If it was cast as drain,	*/
	/* then swap it with the source so that the code below handles it	*/
	/* correctly.								*/

	if ((layoutDev->rd_fet_source == NULL) && (layoutDev->rd_fet_drain != NULL))
	{
	    layoutDev->rd_fet_source = layoutDev->rd_fet_drain;
	    layoutDev->rd_fet_drain = (struct resnode *)NULL;
	}

     	if (extDev->drain == extNode)
	{
	    if ((layoutDev->rd_fet_source != NULL) &&
			(layoutDev->rd_fet_drain == NULL))
	    {
		/* Handle source/drain-tied devices */
		if (extDev->drain == extDev->source)
		    layoutDev->rd_fet_drain = layoutDev->rd_fet_source;
	    }

	    if (((source = layoutDev->rd_fet_source) != NULL) &&
	       	   ((drain = layoutDev->rd_fet_drain) != NULL))
	    {
	        if ((source->rn_name != NULL) && notdecremented)
		{
		    resNodeNum--;
		    notdecremented = FALSE;
		}
	        ResFixDevName(newname, SOURCE, extDev, source);
	        source->rn_name = extDev->source->name;
		(void)sprintf(newname, "%s%s%d", nodename, ".t", resNodeNum++);
	        if (drain->rn_name != NULL)  resNodeNum--;
	        ResFixDevName(newname, DRAIN, extDev, drain);
	        drain->rn_name = extDev->drain->name;
	       	/* one to each */
	    }
	    else
	    {
		TxError("Missing terminal connection of device at (%d %d) on net %s\n",
			layoutDev->rd_inside.r_xbot, layoutDev->rd_inside.r_ybot,
			nodename);
		extNode->status |= DONTKILL;
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
			if (drain->rn_why & (RES_NODE_ORIGIN | RES_NODE_SINK))
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
	        ResFixDevName(newname, SOURCE, extDev, source);
	        source->rn_name = extDev->source->name;

	    }
	    else
	    {
	       	TxError("Missing terminal connection of device at (%d %d) on net %s\n",
			layoutDev->rd_inside.r_xbot, layoutDev->rd_inside.r_ybot,
			nodename);
		extNode->status |= DONTKILL;
	    }
	}
    }
    else if (extDev->source == extNode)
    {
	/* Device only has 3 terminals, don't need to check for permutations */

	if ((source = layoutDev->rd_fet_source) != NULL)
	{
	    if (source->rn_name != NULL && notdecremented)
	    {
	       	resNodeNum--;
		notdecremented = FALSE;
	    }

	    ResFixDevName(newname, SOURCE, extDev, source);
	    source->rn_name = extDev->source->name;
     	    sprintf(newname, "%s%s%d", nodename, ".t", resNodeNum++);
	}
	else
	{
	    TxError("Missing source connection of device at (%d %d) on net %s\n",
			layoutDev->rd_inside.r_xbot, layoutDev->rd_inside.r_ybot,
			nodename);
	    extNode->status |= DONTKILL;
	}
    }
    else if ((extDev->drain == extNode) && doPermute)
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
		    if (source->rn_why & (RES_NODE_ORIGIN | RES_NODE_SINK))
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
	    ResFixDevName(newname, DRAIN, extDev, drain);
	    drain->rn_name = extDev->drain->name;
	}
	else
	{
	    TxError("Missing terminal connection of device at (%d %d) on net %s\n",
			layoutDev->rd_inside.r_xbot, layoutDev->rd_inside.r_ybot,
			nodename);
	    extNode->status |= DONTKILL;
	}
    }
    else if (extDev->drain == extNode)
    {
	if ((drain = layoutDev->rd_fet_drain) != NULL)
	{
	    if (drain->rn_name != NULL && notdecremented)
	    {
	       	resNodeNum--;
		notdecremented = FALSE;
	    }

	    ResFixDevName(newname, DRAIN, extDev, drain);
	    drain->rn_name = extDev->drain->name;
     	    sprintf(newname, "%s%s%d", nodename, ".t", resNodeNum++);
	}
	else
	{
	    TxError("Missing drain connection of device at (%d %d) on net %s\n",
			layoutDev->rd_inside.r_xbot, layoutDev->rd_inside.r_ybot,
			nodename);
	    extNode->status |= DONTKILL;
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
    ResExtNode		*node;
    devPtr		*tptr;

    if (layoutnode->rn_name != NULL)
    {
        entry = HashFind(&ResNodeTable, layoutnode->rn_name);
        node = ResExtInitNode(entry);

    }
    else
    {
        entry = HashFind(&ResNodeTable, line);
        node = ResExtInitNode(entry);
    }
    tptr = (devPtr *) mallocMagic((unsigned) (sizeof(devPtr)));
    tptr->thisDev = device;
    tptr->nextDev = node->devices;
    node->devices = tptr;
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
ResWriteLumpFile(node, resisdata)
    ResExtNode	*node;
    ResisData	*resisdata;
{
    int	lumpedres;

    if (resisdata->mindelay > 0)
    {
	if (resisdata->rg_nodecap != 0)
	{
	    lumpedres = (int)((resisdata->rg_Tdi / resisdata->rg_nodecap) /
			OHMSTOMILLIOHMS);
	}
	else
	    lumpedres = 0;
    }
    else
    {
	lumpedres = resisdata->rg_maxres * MILLIOHMSTOOHMS;
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
ResWriteExtFile(celldef, node, resisdata, nidx, eidx)
    CellDef	*celldef;
    ResExtNode	*node;
    ResisData	*resisdata;
    int		*nidx, *eidx;
{
    char	*cp, newname[MAXNAME];
    devPtr	*ptr;
    resDevice	*layoutDev, *ResGetDevice();
    ResConnect *driver, *sink;

    ASSERT(resisdata->rg_Tdi != -1, "ResWriteExtFile");

    sprintf(newname, "%s", node->name);
    cp = newname + strlen(newname) - 1;
    if (*cp == '!' || *cp == '#') *cp = '\0';

    /* Second cutoff (if mindelay is specified):  The original cutoff 
     * was based on the original lumped resistance estimate from basic
     * extraction.  Having done full network extraction, there is now
     * a refined delay estimate rg_Tdi which may be much lower than
     * the original estimate.  If the new estimate falls below the
     * threshold, then return without outputting the node's network.
     */
    if (resisdata->rg_Tdi == 0)
    {
	/* The substrate net has zero capacitance by definition and
	 * so it also has zero delay by definition.  In that case,
	 * check if the updated lumped resistance exceeds the
	 * threshold or not.
	 */
	if (resisdata->rg_maxres < resisdata->rthresh)
	    if (HashLookOnly(&ResForceTable, node->name) == NULL)
	    {
		if (ResOptionsFlags & ResOpt_Debug)
		    TxPrintf("Not adding %s (maxres = %.2fohm)\n", node->name,
				resisdata->rg_maxres * MILLIOHMSTOOHMS);
		return 0;
	    }
    }
    else if ((resisdata->rg_Tdi != -1) && (resisdata->rg_Tdi < resisdata->mindelay))
    {
	if (HashLookOnly(&ResForceTable, node->name) == NULL)
	{
	    if (ResOptionsFlags & ResOpt_Debug)
	    {
		/* Diagnostic */
		float ftdi, fdmin;

		ftdi = resisdata->rg_Tdi * Z_TO_P;
		fdmin = resisdata->mindelay * Z_TO_P;

		if ((ftdi < 0.01) || ((fdmin < 0.01) && (resisdata->mindelay > 0)))
		    TxPrintf("Not adding  %s; (Tnew = %.2ffs ; Tmin = %.2ffs)\n",
				node->name, ftdi * 1000, fdmin * 1000);
		else
		    TxPrintf("Not adding  %s; (Tnew = %.2fps ; Tmin = %.2fps)\n",
				node->name, ftdi, fdmin);
	    }
	    return 0;
	}
    }

    if (ResOptionsFlags & ResOpt_Debug)
    {
	if (resisdata->mindelay == 0)
	{
	    TxPrintf("Adding %s (maxres = %.2fohm)\n", node->name,
			resisdata->rg_maxres * MILLIOHMSTOOHMS);
	}
	else
	{
	    float ftdi, fdmin;
	    fdmin = resisdata->mindelay * Z_TO_P;

	    if (resisdata->rg_Tdi == -1)
		ftdi = resisdata->rg_maxres * resisdata->rg_nodecap * Z_TO_P;
	    else
		ftdi = resisdata->rg_Tdi * Z_TO_P;

	    if ((ftdi < 0.01) || ((fdmin < 0.01) && (resisdata->mindelay > 0)))
		TxPrintf("Adding %s; (Tnew = %.2ffs ; Tmin = %.2ffs)\n",
		     	node->name, ftdi * 1000, fdmin * 1000);
	    else
		TxPrintf("Adding %s; (Tnew = %.2fps ; Tmin = %.2fps)\n",
		     	node->name, ftdi, fdmin);
	}
    }

    for (ptr = node->devices; ptr != NULL; ptr = ptr->nextDev)
	if ((layoutDev = ResGetDevice(&ptr->thisDev->location,
			ptr->thisDev->rs_ttype)))
	    ResFixUpConnections(ptr->thisDev, layoutDev, node, newname);

    /* Copy the node name into a driver connection if no driver connection
     * has the original node name (e.g., was a port).  All other drivers
     * get ".uX" suffixes to distinguish them from internal network nodes
     * (".nX").
     */
    for (driver = node->drivepoints; driver != NULL; driver = driver->rc_next)
	ResFixUpDrivepoints(driver, node, newname);

    /* Replace downward connections (sinks) with new node names.  Node
     * names of sinks are given the suffix ".dX" to distinguish them
     * from terminals, drivers, and nodes.
     */
    for (sink = node->sinkpoints; sink != NULL; sink = sink->rc_next)
	ResFixUpSinkpoints(driver, node, newname);

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

/*
 *-------------------------------------------------------------------------
 *
 * InitializeResNode --
 *
 *	Initialize a ResNode structure.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Values are filled in the ResNode structure pointed to by "node".
 *
 * Notes:
 *	This routine was previously defined as a macro.
 *
 *-------------------------------------------------------------------------
 */

void InitializeResNode(resNode *node,
    int x,
    int y,
    int why)
{
    node->rn_te = NULL;
    node->rn_id = 0;
    node->rn_float.rn_area = 0.0;
    node->rn_name = NULL;
    node->rn_client = (ClientData)NULL;
    node->rn_noderes = RES_INFINITY;
    node->rn_je = NULL;
    node->rn_status = FALSE;
    node->rn_loc.p_x = x;
    node->rn_loc.p_y = y;
    node->rn_why = why;
    node->rn_ce = (cElement *)NULL;
    node->rn_re = (resElement *)NULL;
}

/*
 *-------------------------------------------------------------------------
 *
 * ResInfoInit--
 *
 *	Initialize a resInfo structure.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Values are filled in the resInfo structure pointed to by "Info".
 *
 * Notes:
 *	This routine was previously defined as a macro.
 *
 *-------------------------------------------------------------------------
 */

void ResInfoInit(resInfo *Info)
{
    Info->contactList = (cElement *)NULL;
    Info->deviceList = (resDevice *)NULL;
    Info->junctionList = (ResJunction *)NULL;
    Info->breakList = (Breakpoint *)NULL;
    Info->portList = (resPort *)NULL;
    Info->ri_status = FALSE;
    Info->sourceEdge = 0;
}

/*
 *-------------------------------------------------------------------------
 *
 * ResNewBreak --
 *
 *	Create and initialize a new breakpoint structure.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory is allocated for the breakpoint, values are filled in,
 *	and the breakpoint is added to the resNode structure pointed
 *	to by "node".
 *
 * Notes:
 *	This routine was previously defined as a macro.
 *
 *-------------------------------------------------------------------------
 */

void
ResNewBreak(resNode *node,
    Tile *tile,
    int px,
    int py,
    Rect *crect)
{
    Breakpoint *bp;
    resInfo *rX;

    rX = (resInfo *)((tile)->ti_client);
    bp = (Breakpoint *)mallocMagic((unsigned)(sizeof(Breakpoint)));
    bp->br_next= rX->breakList;
    bp->br_this = node;
    bp->br_loc.p_x = px;
    bp->br_loc.p_y = py;
    bp->br_crect = crect;
    rX->breakList = bp;
}

