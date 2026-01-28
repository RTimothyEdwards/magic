
#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/resis/ResReadExt.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

/*
 *-------------------------------------------------------------------------
 *
 * ResReadExt.c -- Routines to parse .ext files for information needed
 *	by extresist.
 *
 *-------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "utils/magic.h"
#include "utils/main.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/malloc.h"
#include "textio/textio.h"
#include "extract/extract.h"
#include "extract/extractInt.h"
#include "extflat/extflat.h"
#include "extflat/extparse.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/utils.h"
#include "utils/tech.h"
#include "textio/txcommands.h"
#include "resis/resis.h"

/* constants defining where various fields can be found in .ext files.	*/
/* The value corresponds to the argument number on the list after	*/
/* parsing by efReadLine().						*/

#define		FET_NAME		1
#define		FET_X			2
#define		FET_Y			3
#define		FET_AREA		4
#define		FET_PERIM		5
#define		FET_SUBS		6
#define		FET_GATE		7
#define		FET_GATE_ATTR		9
#define		FET_SOURCE		10
#define		FET_SOURCE_ATTR		12
#define		FET_DRAIN		13
#define		FET_DRAIN_ATTR		15

#define		DEV_NAME		2
#define		DEV_X			3
#define		DEV_Y			4

#define		NODES_NODENAME		1
#define		NODES_NODEX		4
#define		NODES_NODEY		5
#define		NODES_NODETYPE		6

#define		COUPLETERMINAL1		1
#define		COUPLETERMINAL2		2
#define		COUPLEVALUE		3

#define		RES_EXT_ATTR_NAME	1
#define		RES_EXT_ATTR_X		2
#define		RES_EXT_ATTR_Y		3
#define		RES_EXT_ATTR_TYPE	6
#define		RES_EXT_ATTR_TEXT	7

#define		PORT_NAME		1
#define		PORT_LLX		3
#define		PORT_LLY		4
#define		PORT_URX		5
#define		PORT_URY		6
#define		PORT_TYPE		7

#define MAXDIGIT		20

ResExtNode	*ResInitializeNode();

ResExtNode	*ResOriginalNodes;	/*Linked List of Nodes 	*/
char		RDEV_NOATTR[1] = {'0'};
ResFixPoint	*ResFixList;

/*
 *-------------------------------------------------------------------------
 *
 * ResReadExt--
 *
 * Results: returns 0 if ext file is correct, 1 if not.
 *
 * Side Effects:Reads in ExtTable and makes a hash table of nodes.
 *
 *-------------------------------------------------------------------------
 */

int
ResReadExt(char *extfile)
{
    char *line = NULL, *argv[128];
    int	result, locresult;
    int argc, n, size = 0;
    FILE *fp;
    CellDef *dbdef;
    ResExtNode *curnode;

    /* Search for the .ext fie in the same way that efReadDef() does. */

    fp = PaOpen(extfile, "r", ".ext", EFSearchPath, EFLibPath, (char **)NULL);
    if ((fp == NULL) && (dbdef = DBCellLookDef(extfile)) != NULL)
    {
	char *filepath, *sptr;

	filepath = StrDup((char **)NULL, dbdef->cd_file);
	sptr = strrchr(filepath, '/');
	if (sptr)
	{
	    *sptr = '\0';
	    fp = PaOpen(extfile, "r", ".ext", filepath, EFLibPath, (char **)NULL);
	}
	freeMagic(filepath);
    }

    /* Try with the standard search path */
    if ((fp == NULL) && (EFSearchPath == NULL))
	fp = PaOpen(extfile, "r", ".ext", Path, EFLibPath, (char **)NULL);

    if (fp == NULL)
    {
    	TxError("Cannot open file %s%s\n", extfile, ".ext");
	return 1;
    }

    /* Read in the file.  Makes use of various functions
     * from extflat, mostly in EFread.c.
     */

    EFSaveLocs = FALSE;
    efReadLineNum = 0;
    result = 0;

    while ((argc = efReadLine(&line, &size, fp, argv)) >= 0)
    {
	n = LookupStruct(argv[0], (const LookupTable *)keyTable, sizeof keyTable[0]);
	if (n < 0)
	{
	    efReadError("Unrecognized token \"%s\" (ignored)\n", argv[0]);
	    continue;
	}
	if (argc < keyTable[n].k_mintokens)
	{
	    efReadError("Not enough tokens for %s line\n", argv[0]);
	    continue;
	}

	/* We don't care about most tokens, only DEVICE, NODE, PORT,
	 * and SUBSTRATE; and MERGE is used to locate drive points.
	 */
	switch (keyTable[n].k_key)
	{
	    case DEVICE:
		locresult = ResReadDevice(argc, argv);
		break;
	    case FET:
		locresult = ResReadFET(argc, argv);
		break;
	    case MERGE:
		/* To be completed */
		/* ResReadDrivePoint(argc, argv); */
		break;
	    case NODE:
	    case SUBSTRATE:
		curnode = ResReadNode(argc, argv);
		break;
	    case PORT:
		locresult = ResReadPort(argc, argv);
		break;
	    case ATTR:
		locresult = ResReadAttribute(curnode, argc, argv);
		break;
	    case CAP:
		locresult = ResReadCapacitor(argc, argv);
		break;
	    default:
		break;
	}
	if (locresult == 1) result = 1;
    }
    fclose(fp);
    return(result);
}


/*
 *-------------------------------------------------------------------------
 *
 * ResReadNode-- Reads in a node statement, puts location and type of
 *	node into a node structure.
 *
 * Results:  Pointer to the node record if the node was read correctly,
 *	NULL otherwise.
 *
 * Side Effects: see above
 *
 *-------------------------------------------------------------------------
 */

ResExtNode *
ResReadNode(int argc, char *argv[])
{
    HashEntry	*entry;
    ResExtNode	*node;

    entry = HashFind(&ResNodeTable, argv[NODES_NODENAME]);
    node = ResInitializeNode(entry);

    node->location.p_x = atoi(argv[NODES_NODEX]);
    node->location.p_y = atoi(argv[NODES_NODEY]);
    node->type = DBTechNameType(argv[NODES_NODETYPE]);

    if (node->type == -1)
    {
	TxError("Bad tile type name in .ext file for node %s\n", node->name);
	return NULL;
    }
    return node;
}

/*
 *-------------------------------------------------------------------------
 *
 * ResReadPort-- Reads in a port statement from the .ext file and sets
 *	node records accordingly to mark the node as a drivepoint.
 *
 * Results:  0 if successful and 1 otherwise.
 *
 * Side Effects: see above
 *
 *-------------------------------------------------------------------------
 */

int
ResReadPort(int argc,
    char *argv[])
{
    HashEntry	*entry;
    ResExtNode	*node;

    entry = HashFind(&ResNodeTable, argv[PORT_NAME]);
    node = ResInitializeNode(entry);

    node->drivepoint.p_x = atoi(argv[PORT_LLX]);
    node->drivepoint.p_y = atoi(argv[PORT_LLY]);
    node->status |= FORCE;
    /* To do:  Check for multiple ports on a net;  each port needs its
     * own drivepoint.
     */
    node->status |= DRIVELOC | PORTNODE;
    node->rs_bbox.r_ll = node->drivepoint;
    node->rs_bbox.r_ur.p_x = atoi(argv[PORT_URX]);
    node->rs_bbox.r_ur.p_y = atoi(argv[PORT_URY]);
    node->rs_ttype = DBTechNoisyNameType(argv[PORT_TYPE]);
    node->type = node->rs_ttype;

    if (node->type == -1)
    {
	TxError("Bad tile type name in .ext file for node %s\n", node->name);
	return 1;
    }
    return 0;
}
/*
 *-------------------------------------------------------------------------
 *
 * ResNodeAddDevice --
 *
 *	Given a device and a node which connects to one of its terminals,
 *	add the device to the node's device list.  Device type is one
 *	of the indexes defined by GATE, SOURCE, or DRAIN (to do: generalize
 *	this).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates memory for a devPtr, adds to the node's firstDev linked
 *	list.
 * 
 *-------------------------------------------------------------------------
 */

void
ResNodeAddDevice(ResExtNode *node,
    RDev *device,
    int termtype)
{
    devPtr *tptr;

    tptr = (devPtr *)mallocMagic((unsigned)(sizeof(devPtr)));
    tptr->thisDev = device;
    tptr->nextDev = node->firstDev;
    node->firstDev = tptr;
    tptr->terminal = termtype;
}

/*
 *-------------------------------------------------------------------------
 *
 *  ResReadDevice--
 *
 *	Process a "device" line from a ext file.
 *
 * Results: returns 0 if line was added correctly.
 *
 * Side Effects: Allocates devicesl
 *
 *-------------------------------------------------------------------------
 */

int
ResReadDevice(int argc,
    char *argv[])
{
    RDev	*device;
    int		rvalue, i, j, k;
    ExtDevice	*devptr;
    TileType	ttype;
    HashEntry	*entry;
    ResExtNode	*node;

    device = (RDev *)mallocMagic((unsigned)(sizeof(RDev)));

    device->resistance = 0;	/* Linear resistance from FET line, unused */

    device->status = FALSE;
    device->nextDev = ResRDevList;

    /* Find the device definition record corresponding to the device name */
    devptr = (ExtDevice *)NULL;
    for (ttype = TT_TECHDEPBASE; ttype < DBNumTypes; ttype++)
    {
	for (devptr = ExtCurStyle->exts_device[ttype]; devptr;
			devptr = devptr->exts_next)
	    if (!strcmp(devptr->exts_deviceName, argv[DEV_NAME])) break;
	if (devptr != NULL) break;
    }

    device->location.p_x = atoi(argv[DEV_X]);
    device->location.p_y = atoi(argv[DEV_Y]);

    device->rs_gattr = RDEV_NOATTR;
    device->rs_sattr = RDEV_NOATTR;
    device->rs_dattr = RDEV_NOATTR;
    device->rs_devptr = devptr;

    device->source = (ResExtNode *)NULL;
    device->drain = (ResExtNode *)NULL;
    device->subs = (ResExtNode *)NULL;

    /* Pass over parameters and find the next argument */
    for (i = DEV_Y; i < argc; i++)
	if (!StrIsInt(argv[i]) && !(strchr(argv[i], '=')))
	    break;

    if (i == argc)
    {
	TxError("Bad device %s:  Too few arguments in .ext file\n",
		argv[DEV_NAME]);
	return 1;
    }

    /* Find and record the device terminal nodes */
    /* Note that this only records up to two terminals matching FET
     * source and drain;  it needs to be expanded to include an
     * arbitrary number of terminals.
     */

    if (strcmp(argv[i], "None"))
    {
	entry = HashFind(&ResNodeTable, argv[i]);
	device->subs = (ResExtNode *)HashGetValue(entry);
	ResNodeAddDevice(device->subs, device, SUBS);
    }
    i++;
    entry = HashFind(&ResNodeTable, argv[i]);
    device->gate = (ResExtNode *)HashGetValue(entry);
    device->rs_gattr = StrDup((char **)NULL, argv[i + 2]);
    ResNodeAddDevice(device->gate, device, GATE);
    i += 3;
    
    if (i < argc - 2)
    {
	entry = HashFind(&ResNodeTable, argv[i]);
	device->source = (ResExtNode *)HashGetValue(entry);
	device->rs_sattr = StrDup((char **)NULL, argv[i + 2]);
	ResNodeAddDevice(device->source, device, SOURCE);
	i += 3;
    }

    if (i < argc - 2)
    {
	entry = HashFind(&ResNodeTable, argv[i]);
	device->drain = (ResExtNode *)HashGetValue(entry);
	device->rs_dattr = StrDup((char **)NULL, argv[i + 2]);
	ResNodeAddDevice(device->drain, device, DRAIN);
	i += 3;
    }
    if (i < argc - 2)
    {
	TxError("Warning:  Device %s has more than 4 ports (not handled).\n", 
		argv[DEV_NAME]);
    }

    device->rs_ttype = extGetDevType(devptr->exts_deviceName);

    ResRDevList = device;
    device->layout = NULL;
    return 0;
}

/*
 *-------------------------------------------------------------------------
 *
 *  ResReadFET-- Processes a "fet" line from a ext file.
 *
 * Results: returns 0 if line was added correctly.
 *
 * Side Effects: Allocates devices.
 *
 *-------------------------------------------------------------------------
 */

int
ResReadFET(int argc,
    char *argv[])
{
    RDev	*device;
    int		rvalue, i, j, k;
    ExtDevice	*devptr;
    TileType	ttype;
    HashEntry	*entry;
    ResExtNode	*node;

    device = (RDev *)mallocMagic((unsigned)(sizeof(RDev)));

    device->resistance = 0;	/* Linear resistance from FET line, unused */

    device->status = FALSE;
    device->nextDev = ResRDevList;

    /* Find the device definition record corresponding to the device name */
    devptr = (ExtDevice *)NULL;
    for (ttype = TT_TECHDEPBASE; ttype < DBNumTypes; ttype++)
    {
	for (devptr = ExtCurStyle->exts_device[ttype]; devptr;
			devptr = devptr->exts_next)
	    if (!strcmp(devptr->exts_deviceName, argv[FET_NAME])) break;
	if (devptr != NULL) break;
    }

    device->location.p_x = atoi(argv[FET_X]);
    device->location.p_y = atoi(argv[FET_Y]);

    device->rs_gattr=RDEV_NOATTR;
    device->rs_sattr=RDEV_NOATTR;
    device->rs_dattr=RDEV_NOATTR;
    device->rs_devptr = devptr;

    /* Find and record the FET terminal nodes */

    entry = HashFind(&ResNodeTable, argv[FET_GATE]);
    device->gate = (ResExtNode *)HashGetValue(entry);
    
    entry = HashFind(&ResNodeTable, argv[FET_SOURCE]);
    device->source = (ResExtNode *)HashGetValue(entry);

    entry = HashFind(&ResNodeTable, argv[FET_DRAIN]);
    device->drain = (ResExtNode *)HashGetValue(entry);

    entry = HashFind(&ResNodeTable, argv[FET_SUBS]);
    device->subs = (ResExtNode *)HashGetValue(entry);

    device->rs_ttype = extGetDevType(devptr->exts_deviceName);

    /* Copy attributes verbatim */
    device->rs_gattr = StrDup((char **)NULL, argv[FET_GATE_ATTR]);
    device->rs_sattr = StrDup((char **)NULL, argv[FET_SOURCE_ATTR]);
    device->rs_dattr = StrDup((char **)NULL, argv[FET_DRAIN_ATTR]);

    ResRDevList = device;
    device->layout = NULL;
    return 0;
}

/*
 *-------------------------------------------------------------------------
 *
 *  ResReadCapacitor-- Adds the capacitance  from a C line to the appropriate
 *	node. Coupling capacitors are added twice, moving the capacitance
 *	to the substrate.
 *
 *  Results:
 *	Always return 0
 *
 *  Side Effects: modifies capacitance field  of ResExtNode.
 *
 *-------------------------------------------------------------------------
 */

int
ResReadCapacitor(int argc,
    char *argv[])
{
    HashEntry	*entry1, *entry2;
    ResExtNode	*node1, *node2;

    entry1 = HashFind(&ResNodeTable, argv[COUPLETERMINAL1]);
    node1 = ResInitializeNode(entry1);

    if (ResOptionsFlags & ResOpt_Signal)
    {
        node1->capacitance += MagAtof(argv[COUPLEVALUE]);
        entry2 = HashFind(&ResNodeTable, argv[COUPLETERMINAL2]);
        node2 = ResInitializeNode(entry2);
        node2->capacitance += MagAtof(argv[COUPLEVALUE]);
        return 0;
    }

    entry2 = HashFind(&ResNodeTable, argv[COUPLETERMINAL2]);
    node2 = ResInitializeNode(entry2);

    node1->cap_couple += MagAtof(argv[COUPLEVALUE]);
    node2->cap_couple += MagAtof(argv[COUPLEVALUE]);
    return 0;
}

/*
 *-------------------------------------------------------------------------
 *
 *  ResReadAttribute--checks to see if a node attribute is a resistance
 *	attribute. If it is, add it to the correct node's status flag.
 *	Only works with 5.0 1/line attributes
 *
 *  Results:
 *	Return 0 to keep search going, 1 to abort
 *
 *  Side Effects: modifies resistance field of ResExtNode
 *
 *-------------------------------------------------------------------------
 */

int
ResReadAttribute(ResExtNode *node,
    int argc,
    char *argv[])
{
    char	*aname, *avalue;
    char	digit[MAXDIGIT];
    int		i;
    static int	notwarned = TRUE;

    aname = argv[RES_EXT_ATTR_NAME];
    avalue = argv[RES_EXT_ATTR_TEXT];

    if (strncmp(avalue, "res:skip", 8) == 0)
    {
     	if (node->status & FORCE)
	{
	    TxError("Warning: Node %s is both forced and skipped\n", aname);
	}
	else
	{
	    node->status |= SKIP;
	}
    }
    else if (strncmp(avalue, "res:force", 9) == 0)
    {
     	if (node->status & SKIP)
	    TxError("Warning: Node %s is both skipped and forced \n", aname);
	else
	    node->status |= FORCE;
    }
    else if (strncmp(avalue, "res:min=", 8) == 0)
    {
	node->status |= MINSIZE;
	for (i = 0, avalue += 8; *avalue != '\0'; avalue++)
	{
	    digit[i++] = *avalue;
	}
	digit[i++] = '\0';
	node->minsizeres = MagAtof(digit);
    }
    else if (strncmp(avalue, "res:drive", 9) == 0 &&
     	      (ResOptionsFlags & ResOpt_Signal))
    {
	node->drivepoint.p_x = atoi(argv[RES_EXT_ATTR_X]);
	node->drivepoint.p_y = atoi(argv[RES_EXT_ATTR_Y]);
	node->rs_ttype = DBTechNoisyNameType(argv[RES_EXT_ATTR_TYPE]);
	node->status |= DRIVELOC;
    }
    return 0;
}

/*
 *-------------------------------------------------------------------------
 *
 * ResInitializeNode --
 *	Gets the node corresponding to a given hash table entry.  If no
 *	such node exists, one is created.
 *
 * Results: Returns ResExtNode corresponding to entry.
 *
 * Side Effects: May allocate a new ResExtNode.
 *
 *-------------------------------------------------------------------------
 */

ResExtNode *
ResInitializeNode(entry)
    HashEntry	*entry;
{
    ResExtNode	*node;

    if ((node = (ResExtNode *) HashGetValue(entry)) == NULL)
    {
	node = (ResExtNode *)mallocMagic((unsigned)(sizeof(ResExtNode)));
	HashSetValue(entry, (char *) node);
	node->nextnode = ResOriginalNodes;
	ResOriginalNodes = node;
	node->status = FALSE;
	node->forward = (ResExtNode *) NULL;
	node->capacitance = 0;
	node->cap_vdd = 0;
	node->cap_couple = 0;
	node->resistance = 0;
	node->type = 0;
	node->firstDev = NULL;
	node->name = entry->h_key.h_name;
	node->oldname = NULL;
	node->drivepoint.p_x = INFINITY;
	node->drivepoint.p_y = INFINITY;
	node->location.p_x = INFINITY;
	node->location.p_y = INFINITY;
	node->rs_sublist[0] = NULL;
	node->rs_sublist[1] = NULL;
    }
    while (node->status & FORWARD)
    {
     	node = node->forward;
    }
    return node;
}
