
#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/resis/ResRex.c,v 1.3 2010/03/08 13:33:33 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#include "database/database.h"
#include "utils/malloc.h"
#include "textio/textio.h"
#include "extract/extract.h"
#include "extract/extractInt.h"
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


/* time constants are produced by multiplying attofarads by milliohms,  */
/* giving zeptoseconds (yes, really.  Look it up).  This constant 	*/
/* converts zeptoseconts to nanoseconds.				*/
#define Z_TO_N		1e12

/* ResSimNode is a node read in from a sim file */


HashTable 	ResNodeTable;   /* Hash table of sim file nodes   */
RTran		*ResTranList;	/* Linked list of Sim transistors */
ResGlobalParams	gparams;	/* Junk passed between 		  */
				/* ResCheckSimNodes and 	  */
				/* ResExtractNet.		  */
int		Maxtnumber;     /*maximum transistor number 	  */
extern ResSimNode	*ResOriginalNodes;	/*Linked List of Nodes		  */
int		resNodeNum;

#ifdef LAPLACE
int	ResOptionsFlags = ResOpt_Simplify|ResOpt_Tdi|ResOpt_DoExtFile|ResOpt_CacheLaplace;
#else
int	ResOptionsFlags = ResOpt_Simplify|ResOpt_Tdi|ResOpt_DoExtFile;
#endif
char	*ResCurrentNode;

FILE	*ResExtFile;
FILE	*ResLumpFile;
FILE	*ResFHFile;

int	ResPortIndex;	/* Port ordering to backannotate into magic */

/* external declarations */
extern ResSimNode *ResInitializeNode();
extern CellUse	  *CmdGetSelectedCell();

/* Structure stores information required to be sent to ExtResisForDef() */
typedef struct
{
    float	tolerance;
    float	tdiTolerance;
    float	frequency;
    CellDef	*mainDef;
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
    RTran	*oldTran;
    HashSearch	hs;
    HashEntry	*entry;
    tranPtr	*tptr,*oldtptr;
    ResSimNode  *node;
    int		result;

    ResTranList = NULL;
    ResOriginalNodes = NULL;

    Maxtnumber = 0;
    HashInit(&ResNodeTable, INITFLATSIZE, HT_STRINGKEYS);
    /* read in .sim file */
    result = ResReadSim(celldef->cd_name,
	      	ResSimTransistor,ResSimCapacitor,ResSimResistor,
		ResSimAttribute,ResSimMerge) == 0;

    if (result == 0)
	/* read in .nodes file   */
	result = ResReadNode(celldef->cd_name);

    /* Check for subcircuit ports */
    if (ResOptionsFlags & ResOpt_Blackbox)
	result &= ResCheckBlackbox(celldef);
    else
	result &= ResCheckPorts(celldef);

    if (result == 0) 
    {
	/* Extract networks for nets that require it. */
	if (!(ResOptionsFlags & ResOpt_FastHenry) || 
			DBIsSubcircuit(celldef))
	    ResCheckSimNodes(celldef, resisdata);

	if (ResOptionsFlags & ResOpt_Stat)
	    ResPrintStats((ResGlobalParams *)NULL,"");
    }

    HashStartSearch(&hs);
    while((entry = HashNext(&ResNodeTable,&hs)) != NULL)
    {
	node=(ResSimNode *) HashGetValue(entry);
	tptr = node->firstTran;
	if (node == NULL)
	{
	    TxError("Error:  NULL Hash entry!\n");
	    TxFlushErr();
	}
	while (tptr != NULL)
	{
	    oldtptr = tptr;
	    tptr = tptr->nextTran;
	    freeMagic((char *)oldtptr);
	}
	freeMagic((char *) node);
    }
    HashKill(&ResNodeTable);
    while (ResTranList != NULL)
    {
    	oldTran = ResTranList;
	ResTranList = ResTranList->nextTran;
	if (oldTran->layout != NULL)
	{
	    freeMagic((char *)oldTran->layout);
	    oldTran->layout = NULL;
	}
	freeMagic((char *)oldTran);
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
    int		i, j, k, option, value, saveFlags;
    static int	init=1;
    static float tolerance, tdiTolerance, fhFrequency;
    CellDef	*mainDef;
    CellUse	*selectedUse;
    ResisData	resisdata;
    char	*endptr;	/* for use with strtod() */

    extern int resSubcircuitFunc();	/* Forward declaration */

    static char *onOff[] =
    {
	"off",
	"on",
	NULL
    };

    static char *cmdExtresisCmd[] = 
    {
	"tolerance [value]    set ratio between resistor and transistor tol.",
	"all 		       extract all the nets",
	"simplify [on/off]    turn on/off simplification of resistor nets",
	"extout   [on/off]    turn on/off writing of .res.ext file",
	"lumped   [on/off]    turn on/off writing of updated lumped resistances",
	"silent   [on/off]    turn on/off printing of net statistics",
	"skip     mask        don't extract these types",
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
	RES_BAD=-2, RES_AMBIG, RES_TOL,
	RES_ALL, RES_SIMP, RES_EXTOUT, RES_LUMPED,
	RES_SILENT, RES_SKIP, RES_BOX, RES_CELL, RES_BLACKBOX,
	RES_FASTHENRY, RES_GEOMETRY, RES_HELP,
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
	tolerance = 1;
	tdiTolerance = 1;
	fhFrequency = 10e6;	/* 10 MHz default */
	init = 0;
    }

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
		tolerance = MagAtof(cmd->tx_argv[2]);
		if (tolerance <= 0)
		{
		    TxError("Usage:  %s tolerance [value]\n", cmd->tx_argv[0]);
			return;
		}
		tdiTolerance = tolerance;
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
		ResFixPoint	fp;

		if (ToolGetBoxWindow((Rect *) NULL, (int *) NULL) == NULL)
		{
		    TxError("Sorry, the box must appear in one of the windows.\n");
		    return;
		}

	     	if (cmd->tx_argc != 3) return;
		tt = DBTechNoisyNameType(cmd->tx_argv[2]);
		if (tt <= 0 || ToolGetBox(&def, &rect)== FALSE) return;
		gparams.rg_tranloc = &rect.r_ll;
		gparams.rg_ttype = tt;
		gparams.rg_status = DRIVEONLY;
		oldoptions = ResOptionsFlags;
		ResOptionsFlags = ResOpt_DoSubstrate|ResOpt_Signal|ResOpt_Box;
#ifdef LAPLACE
		ResOptionsFlags |= (oldoptions & (ResOpt_CacheLaplace|ResOpt_DoLaplace));
		LaplaceMatchCount = 0;
		LaplaceMissCount = 0;
#endif
		fp.fp_ttype = tt;
		fp.fp_loc = rect.r_ll;
		fp.fp_next = NULL;
		if (ResExtractNet(&fp, &gparams, NULL) != 0) return;
		ResPrintResistorList(stdout,ResResList);
		ResPrintTransistorList(stdout,ResTransList);
#ifdef LAPLACE
		if (ResOptionsFlags & ResOpt_DoLaplace)
		{
		    TxPrintf("Laplace   solved: %d matched %d\n",
				LaplaceMissCount,LaplaceMatchCount);
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
  	    TxPrintf("Ambiguous option: %s\n",cmd->tx_argv[1]);
	    TxFlushOut();
	    return;
	case RES_BAD:
  	    TxPrintf("Unknown option: %s\n",cmd->tx_argv[1]);
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
    ResOptionsFlags &= 	~ResOpt_Power;
#endif

    resisdata.tolerance = tolerance;
    resisdata.tdiTolerance = tdiTolerance;
    resisdata.frequency = fhFrequency;
    resisdata.mainDef = mainDef;

    /* Do subcircuits (if any) first */
    if (!(ResOptionsFlags & ResOpt_Blackbox))
	(void) DBCellSrDefs(0, resSubcircuitFunc, (ClientData) &resisdata);

    ExtResisForDef(mainDef, &resisdata);

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
 *	For each encountered cell, call the resistance extractor.
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
resSubcircuitFunc(cellDef, rdata)
    CellDef *cellDef;
    ResisData *rdata;
{
    if (cellDef != rdata->mainDef)
	if (DBIsSubcircuit(cellDef))
	    ExtResisForDef(cellDef, rdata);
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

    GeoTransRect(&scx->scx_trans, &lab->lab_rect, &r);

    // To be expanded.  Currently this handles digital signal inputs
    // and outputs, for standard cells.

    if (lab->lab_flags & PORT_DIR_MASK) {
	pclass = lab->lab_flags & PORT_CLASS_MASK;
	puse = lab->lab_flags & PORT_USE_MASK;

	// Ad hoc rule:  If port use is not declared, but port
	// direction is either INPUT or OUTPUT, then use SIGNAL is implied.

	if ((puse == 0) && ((pclass == PORT_CLASS_INPUT)
		|| (pclass == PORT_CLASS_OUTPUT)))
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

	    if ((pclass == PORT_CLASS_INPUT) || (pclass == PORT_CLASS_OUTPUT)) {
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
 *	no transistors in the subcircuit cell, we must find the ports
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
		TxError("Port: name = %s exists, forcing drivepoint\n",
			lab->lab_text);
		TxError("Location is (%d, %d); drivepoint (%d, %d)\n",
			node->location.p_x, node->location.p_y,
			portloc.p_x, portloc.p_y);
		TxFlushErr();
		node->drivepoint = portloc;
		node->status |= FORCE;
	    }
	    else
	    {
		/* This is a port, but it's merged with another node.	*/
		/* We have to make sure it's listed as a separate node	*/
		/* and a drivepoint.					*/

		node = ResInitializeNode(entry);
		TxError("Port: name = %s is new node 0x%x\n",
			lab->lab_text, node);
		TxError("Location is (%d, %d); drivepoint (%d, %d)\n",
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
 *		      transistor resistance; if it is, Extract the net 
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
    tranPtr	*ptr;
    float	ftolerance, rctolerance, minRes, cumRes;
    int		failed1=0;
    int 	failed3=0;
    int		total =0;
    char	*outfile = celldef->cd_name;
    float	tol = resisdata->tolerance;
    float	rctol = resisdata->tdiTolerance;
    int		nidx = 1, eidx = 1;	/* node & segment counters for geom. */

    if (ResOptionsFlags & ResOpt_DoExtFile)
    {
	ResExtFile = PaOpen(outfile,"w",".res.ext",".",(char *) NULL, (char **) NULL);
    }
    else
    {
	ResExtFile = NULL;
    }
    if (ResOptionsFlags & ResOpt_DoLumpFile)
    {
        ResLumpFile = PaOpen(outfile,"w",".res.lump",".",(char *) NULL, (char **) NULL);
    }
    else
    {
     	ResLumpFile = NULL;
    }
    if (ResOptionsFlags & ResOpt_FastHenry)
    {
	char *geofilename;
        ResFHFile = PaOpen(outfile,"w",".fh",".",(char *) NULL, &geofilename);
	TxPrintf("Writing FastHenry-format geometry file \"%s\"\n", geofilename);
	ResPortIndex = 0;
    }
    else
    {
     	ResFHFile = NULL;
    }
  
    if (ResExtFile == NULL && (ResOptionsFlags & ResOpt_DoExtFile)
         || (ResOptionsFlags & ResOpt_DoLumpFile) && ResLumpFile == NULL
         || (ResOptionsFlags & ResOpt_FastHenry) && ResFHFile == NULL)
    {
     	TxError("Couldn't open output file\n");
	return;
    }

     /*
      *	Write reference plane (substrate) definition and end statement
      * to the FastHenry geometry file.	
      */
    if (ResOptionsFlags & ResOpt_FastHenry)
    {
	ResPrintReference(ResFHFile, ResTranList, celldef);
    }

    for (node = ResOriginalNodes; node != NULL; node=node->nextnode)
    {
	ResCurrentNode = node->name;
	if (!(ResOptionsFlags & ResOpt_FastHenry))
	{
	    /* Hack!!  Don't extract Vdd or GND lines */

	    char *last4, *last3;
	    last4 = node->name+strlen(node->name)-4;
	    last3 = node->name+strlen(node->name)-3;

	    if ((strncmp(last4,"Vdd!",4) == 0 || 
	          strncmp(last4,"VDD!",4) == 0 ||
	          strncmp(last4,"vdd!",4) == 0 ||
	          strncmp(last4,"Gnd!",4) == 0 ||
	          strncmp(last4,"gnd!",4) == 0 ||
	          strncmp(last4,"GND!",4) == 0 ||
	          strncmp(last3,"Vdd",3) == 0 || 
	          strncmp(last3,"VDD",3) == 0 ||
	          strncmp(last3,"vdd",3) == 0 ||
	          strncmp(last3,"Gnd",3) == 0 ||
	          strncmp(last3,"gnd",3) == 0 ||
	          strncmp(last3,"GND",3) == 0) &&
	          (node->status & FORCE) != FORCE) continue;
	}

	/* Has this node been merged away or is it marked as skipped? */
	/* If so, skip it */
	if ((node->status & (FORWARD | REDUNDANT)) ||
		((node->status & SKIP) && 
	  	(ResOptionsFlags & ResOpt_ExtractAll) == 0))
	    continue;
	total++;
	  
     	ResSortByGate(&node->firstTran);
	/* Find largest SD transistor connected to node.	*/
	  
	minRes = FLT_MAX;
	gparams.rg_tranloc = (Point *) NULL;
	gparams.rg_status = FALSE;
	gparams.rg_nodecap = node->capacitance;

	/* the following is only used if there is a drivepoint */
	/* to identify which tile the drivepoint is on.	 */
	gparams.rg_ttype = node->rs_ttype;

	for (ptr = node->firstTran; ptr != NULL; ptr=ptr->nextTran)
	{
	    RTran	*t1;
	    RTran	*t2;

	    if (ptr->terminal == GATE)
	    {
	       	break;
	    }
	    else
	    {
	       	/* get cumulative resistance of all transistors */
		/* with same connections.			    */
		cumRes = ptr->thisTran->resistance;
	        t1 = ptr->thisTran;
		for (; ptr->nextTran != NULL; ptr = ptr->nextTran)
		{
	            t1 = ptr->thisTran;
		    t2 = ptr->nextTran->thisTran;
		    if (t1->gate != t2->gate) break;
		    if ((t1->source != t2->source ||
			     t1->drain  != t2->drain) &&
			    (t1->source != t2->drain ||
			     t1->drain  != t2->source)) break;

		    /* do parallel combination  */
		    if (cumRes != 0.0 && t2->resistance != 0.0)
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
		    gparams.rg_tranloc = &t1->location;
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
	       	gparams.rg_tranloc = &node->drivepoint;
		gparams.rg_status |= DRIVEONLY;
	    }
	}
	if (gparams.rg_tranloc == NULL && node->status & FORCE)
	{
    	    TxError("Node %s has force label but no drive point or "
			"driving transistor\n",node->name);
	}
	if (minRes == FLT_MAX || gparams.rg_tranloc == NULL)
	{
	    continue;
	}
	gparams.rg_bigtranres = (int)minRes*OHMSTOMILLIOHMS;
	if (rctol == 0.0 || tol == 0.0)
	{
	    ftolerance = 0.0;
	    rctolerance = 0.0;
	}
	else
	{
	    ftolerance =  minRes/tol;
	    rctolerance = minRes/rctol;
	}

	/* 
	 *   Is the transistor resistance greater than the lumped node 
	 *   resistance? If so, extract net.
	 */

	if (node->resistance > ftolerance || node->status & FORCE ||
		(ResOpt_ExtractAll & ResOptionsFlags))
	{
	    ResFixPoint	fp;

	    failed1++;
	    fp.fp_loc = node->location;
	    fp.fp_ttype = node->type;
	    fp.fp_next = NULL;
	    if (ResExtractNet(&fp, &gparams, outfile) != 0)
	    {
	       	TxError("Error in extracting node %s\n",node->name);
		// break;	// Don't stop for one error. . .
	    }
	    else
	    {
		ResDoSimplify(ftolerance,rctol,&gparams);
		if (ResOptionsFlags & ResOpt_DoLumpFile)
		{
		    ResWriteLumpFile(node);
		}
		if (gparams.rg_maxres >= ftolerance  || 
		        gparams.rg_maxres >= rctolerance || 
			(ResOptionsFlags & ResOpt_ExtractAll))
		{
		    resNodeNum = 0;
		    failed3 += ResWriteExtFile(celldef, node, tol, rctol,
				&nidx, &eidx);
		}
	    }
#ifdef PARANOID
	    ResSanityChecks(node->name,ResResList,ResNodeList,ResTransList);
#endif
	    ResCleanUpEverything();
	}
    }
     
    /* 
     * Print out all transistors which have had at least one terminal changed
     * by resistance extraction.
     */

    if (ResOptionsFlags & ResOpt_DoExtFile)
    {
	ResPrintExtTran(ResExtFile,ResTranList);
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
		fprintf(ResFHFile, "* %d %s\n", lab->lab_flags & PORT_NUM_MASK,
			lab->lab_text);

	fprintf(ResFHFile, "\n.end\n");
    }

    /* Output statistics about extraction */

    if (total)
    {
        TxError("Total Nets: %d\nNets extracted: "
		"%d (%f)\nNets output: %d (%f)\n", total, failed1,
		(float)failed1 / (float)total, failed3,
		(float)failed3 / (float)total);
    }
    else
    {
        TxError("Total Nodes: %d\n",total);
    }

    /* close output files */

    if (ResExtFile != NULL) 
    {
     	(void) fclose(ResExtFile);
    }
    if (ResLumpFile != NULL)
    {
     	(void) fclose(ResLumpFile);
    }
    if (ResFHFile != NULL)
    {
	(void) fclose(ResFHFile);
    }
}


/*
 *-------------------------------------------------------------------------
 *
 * ResFixUpConnections-- Changes the connection to  a terminal of the sim 
 *	transistor.  The new name is formed by appending .t# to the old name.
 *	The new name is added to the hash table of node names.
 *
 * Results:none
 *
 * Side Effects: Allocates new ResSimNodes. Modifies the terminal connections
 *	of sim Transistors.
 *
 *-------------------------------------------------------------------------
 */

void
ResFixUpConnections(simTran, layoutTran, simNode, nodename)
    RTran		*simTran;
    resTransistor 	*layoutTran;
    ResSimNode		*simNode;
    char		*nodename;

{
    static char	newname[MAXNAME], oldnodename[MAXNAME];
    int		notdecremented;
    resNode	*gate, *source, *drain;
     
    /* If we aren't doing output (i.e. this is just a statistical run) */
    /* don't patch up networks.  This cuts down on memory use.		*/

    if ((ResOptionsFlags & (ResOpt_DoRsmFile | ResOpt_DoExtFile)) == 0)
    {
	return;
    }
    if (simTran->layout == NULL)
    {
	layoutTran->rt_status |= RES_TRAN_SAVE;
	simTran->layout = layoutTran;
    }
    simTran->status |= TRUE;
    if (strcmp(nodename,oldnodename) != 0)
    {
	strcpy(oldnodename,nodename);
    }
    (void)sprintf(newname,"%s%s%d",nodename,".t",resNodeNum++);
    notdecremented = TRUE;
     
    if (simTran->gate == simNode)
    {
	if ((gate=layoutTran->rt_gate) != NULL)
	{
	    /* cosmetic addition: If the layout tran already has a      */
	    /* name, the new one won't be used, so we decrement resNodeNum */
	    if (gate->rn_name != NULL)
	    {
	       	resNodeNum--;
		notdecremented = FALSE;
	    }  

	    ResFixTranName(newname,GATE,simTran,gate);
	    gate->rn_name = simTran->gate->name;
     	    (void)sprintf(newname,"%s%s%d",nodename,".t",resNodeNum++);
	}
	else
	{
	    TxError("Missing gate connection\n");
	}
    }
    if (simTran->source == simNode)
    {
     	if (simTran->drain == simNode)
	{
	    if ((source=layoutTran->rt_source) && 
	       	   (drain=layoutTran->rt_drain))
	    {
	        if (source->rn_name != NULL && notdecremented)
		{
		    resNodeNum--;
		    notdecremented = FALSE;
		}  
	        ResFixTranName(newname,SOURCE,simTran,source);
	        source->rn_name = simTran->source->name;
		(void)sprintf(newname,"%s%s%d",nodename,".t",resNodeNum++);
	        if (drain->rn_name != NULL)  resNodeNum--;
	        ResFixTranName(newname,DRAIN,simTran,drain);
	        drain->rn_name = simTran->drain->name;
	       	/* one to each */
	    }
	    else
	    {
	        TxError("Missing SD connection\n");
	    }
	}
	else
	{
	    if (source=layoutTran->rt_source)
	    {
		if (drain=layoutTran->rt_drain)
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
		    layoutTran->rt_drain = (resNode *)NULL;
	            if (source->rn_name != NULL)  resNodeNum--;
	            ResFixTranName(newname,SOURCE,simTran,source);
	            source->rn_name = simTran->source->name;
		}
		else
		{
             	    if (source->rn_name != NULL && notdecremented)
		    {
			resNodeNum--;
			notdecremented = FALSE;
		    }  
	            ResFixTranName(newname,SOURCE,simTran,source);
	            source->rn_name = simTran->source->name;
		}
		    
	    }
	    else
	    {
	       	TxError("missing SD connection\n");
	    }
	}
    }
    else if (simTran->drain == simNode)
    {
	if (source=layoutTran->rt_source)
	{
	    if (drain=layoutTran->rt_drain)
	    {
		if (drain != source)
		{
		    if (drain->rn_why & ORIGIN)
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
		layoutTran->rt_source = (resNode *) NULL;
             	if (drain->rn_name != NULL)
		{
		    resNodeNum--;
		    notdecremented = FALSE;
		}  
	        ResFixTranName(newname, DRAIN, simTran, drain);
	        drain->rn_name = simTran->drain->name;
	    }
	    else
	    {
		if (source->rn_name != NULL  && notdecremented)
		{
		    resNodeNum--;
		    notdecremented = FALSE;
		}  
		ResFixTranName(newname,DRAIN,simTran,source);
		source->rn_name = simTran->drain->name;
	    }
	}
	else
	{
	    TxError("missing SD connection\n");
	}
    }
    else
    {
	resNodeNum--;
    }
}


/*
 *-------------------------------------------------------------------------
 *
 *  ResFixTranName-- Moves transistor connection to new node.
 *
 * Results:
 *	None.
 *
 * Side Effects: May create a new node. Creates a new transistor pointer.
 *
 *-------------------------------------------------------------------------
 */

void
ResFixTranName(line,type,transistor,layoutnode)
    char 	line[];
    int		type;
    RTran	*transistor;
    resNode	*layoutnode;

{
    HashEntry		*entry;
    ResSimNode		*node;
    tranPtr		*tptr;
     
    if (layoutnode->rn_name != NULL)
    {
        entry = HashFind(&ResNodeTable,layoutnode->rn_name);
        node = ResInitializeNode(entry);
     	  
    }
    else
    {
        entry = HashFind(&ResNodeTable,line);
        node = ResInitializeNode(entry);
    }
    tptr = (tranPtr *) mallocMagic((unsigned) (sizeof(tranPtr)));
    tptr->thisTran = transistor;
    tptr->nextTran = node->firstTran;
    node->firstTran = tptr;
    tptr->terminal = type;
    switch(type)
    {
     	case GATE:
	    node->oldname = transistor->gate->name;
	    transistor->gate = node;
	    break;
     	case SOURCE:
	    node->oldname = transistor->source->name;
	    transistor->source = node;
	    break;
     	case DRAIN:
	    node->oldname = transistor->drain->name;
	    transistor->drain = node;
	    break;
	default:
	    TxError("Bad Terminal Specifier\n");
	    break;
    }
}


/*
 *-------------------------------------------------------------------------
 *
 *  ResSortByGate--sorts transistor pointers whose terminal field is either
 *	drain or source by gate node number, then by drain (source) number.
 *	This places transistors with identical connections next to one 
 *	another.
 *
 * Results: none
 *
 * Side Effects: modifies order of transistors
 *
 *-------------------------------------------------------------------------
 */

void
ResSortByGate(TranpointerList)
    tranPtr	**TranpointerList;
{
    int		changed=TRUE;
    int		localchange=TRUE;
    tranPtr	*working, *last=NULL, *current, *gatelist=NULL;
     
    working = *TranpointerList;
    while (working != NULL)
    {
	if (working->terminal == GATE)
	{
	    current = working;
	    working = working->nextTran;
       	    if (last == NULL)
	    {
		*TranpointerList = working;
	    }
	    else
	    {
	      	last->nextTran = working;
	    }
	    current->nextTran = gatelist;
	    gatelist = current;
	}
	else
	{
	    last = working;
	    working = working->nextTran;
	}
    }
    while (changed == TRUE)
    {
	changed = localchange = FALSE;
	working = *TranpointerList;
	last = NULL;
	while (working != NULL && (current = working->nextTran) != NULL)
	{
	    RTran	*w = working->thisTran;
	    RTran	*c = current->thisTran;

	    if (w->gate > c->gate)
	    {
	    	changed = TRUE;
		localchange = TRUE;
	    }
	    else if (w->gate == c->gate &&
			(working->terminal == SOURCE && 
			current->terminal == SOURCE && 
			w->drain > c->drain    ||
			working->terminal == SOURCE && 
			current->terminal == DRAIN && 
			w->drain > c->source    ||
			working->terminal == DRAIN && 
			current->terminal == SOURCE && 
			w->source > c->drain    ||
			working->terminal == DRAIN && 
			current->terminal == DRAIN && 
			w->source >  c->source))
	    {
		changed = TRUE;
		localchange = TRUE;
	    }
	    else
	    {
		last = working;
		working = working->nextTran;
		continue;
	    }
	    if (localchange)
	    {
		localchange = FALSE;
		if (last == NULL)
		{
		     *TranpointerList = current;
		}
		else
		{
		     last->nextTran = current;
		}
		working->nextTran = current->nextTran;
		current->nextTran = working;
		last = current;
	    }
	}
    }
    if (working == NULL)
    {
	*TranpointerList = gatelist;
    }
    else
    {
     	if (working->nextTran != NULL)
	{
	    TxError("Bad Transistor pointer in sort\n");
	}
	else
	{
	    working->nextTran = gatelist;
	}
    }
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
	    lumpedres = (int)((gparams.rg_Tdi/gparams.rg_nodecap
			-(float)(gparams.rg_bigtranres))/OHMSTOMILLIOHMS);
	}
	else
	{
	    lumpedres = 0;
	}
    }
    else
    {
	lumpedres = gparams.rg_maxres;
    }
    fprintf(ResLumpFile,"R %s %d\n", node->name, lumpedres);
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
ResWriteExtFile(celldef, node, tol, rctol, nidx, eidx)
    CellDef	*celldef;
    ResSimNode	*node;
    float	tol, rctol;
    int		*nidx, *eidx;
{
    float	RCtran;
    char	*cp, newname[MAXNAME];
    tranPtr	*ptr;
    resTransistor	*layoutFet, *ResGetTransistor();
     
    RCtran = gparams.rg_bigtranres * gparams.rg_nodecap;

    if (tol == 0.0 ||(node->status & FORCE) ||
		(ResOptionsFlags & ResOpt_ExtractAll)||
		(ResOptionsFlags & ResOpt_Simplify)==0||
		(rctol+1)*RCtran < rctol*gparams.rg_Tdi)
    {
	ASSERT(gparams.rg_Tdi != -1,"ResWriteExtFile");
	(void)sprintf(newname,"%s",node->name);
        cp = newname+strlen(newname)-1;
        if (*cp == '!' || *cp == '#') *cp = '\0';
	if ((rctol+1)*RCtran < rctol*gparams.rg_Tdi || 
	  			(ResOptionsFlags & ResOpt_Tdi) == 0)
	{
	    if ((ResOptionsFlags & (ResOpt_RunSilent|ResOpt_Tdi)) == ResOpt_Tdi)
	    {
		TxError("Adding  %s; Tnew = %.2fns,Told = %.2fns\n",
		     	    node->name,gparams.rg_Tdi/Z_TO_N, RCtran/Z_TO_N);
	    }
        }
        for (ptr = node->firstTran; ptr != NULL; ptr=ptr->nextTran)
        {
	    if (layoutFet = ResGetTransistor(&ptr->thisTran->location))
	    {
		ResFixUpConnections(ptr->thisTran,layoutFet,node,newname);
	    }
	}
        if (ResOptionsFlags & ResOpt_DoExtFile)
        {
	    ResPrintExtNode(ResExtFile,ResNodeList,node->name);
      	    ResPrintExtRes(ResExtFile,ResResList,newname);
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

