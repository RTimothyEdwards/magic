
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
#define		DEV_PARAM_START		7

#define		NODES_NODENAME		1
#define		NODES_NODERES		2
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

#define		USE_DEF_NAME		1
#define		USE_ID_NAME		2
#define		USE_TRANSFORM_A		3
#define		USE_TRANSFORM_B		4
#define		USE_TRANSFORM_C		5
#define		USE_TRANSFORM_D		6
#define		USE_TRANSFORM_E		7
#define		USE_TRANSFORM_F		8

/* Note that "connect" lines may repeat these six entries up to argc */
#define		CONNECT_LLX		1
#define		CONNECT_LLY		2
#define		CONNECT_URX		3
#define		CONNECT_URY		4
#define		CONNECT_TYPE		5
#define		CONNECT_UP_NAME		6
#define		CONNECT_DOWN_NAME	7

#define MAXDIGIT		20

ResExtNode	*ResOriginalNodes;	/*Linked List of Nodes 	*/
char		RDEV_NOATTR[1] = {'0'};
ResFixPoint	*ResFixList;

/*
 *-------------------------------------------------------------------------
 *
 * ResReadExt --
 *
 * Read a .ext file for resistance extraction.  Extresist does not use
 * the .ext file reader in extflat/EFread.c because it takes only a
 * small amount of information from the .ext file, mainly to keep a
 * list of nets and net names, devices and their terminals and
 * connections, and subcell connections.  However, it does make use
 * of the line parser and tokenizer in extflat.
 *
 * Results: Returns 0 if ext file is correct, 1 if not.
 *
 * Side Effects:  Creates lists of nodes and devices for extresist.
 *
 *-------------------------------------------------------------------------
 */

int
ResReadExt(CellDef *def)
{
    char *line = NULL, *argv[128];
    int	result, locresult;
    int argc, n, size = 0;
    FILE *fp;
    CellDef *dbdef, *parent;
    CellUse *use;
    ResExtNode *curnode;
    HashTable parentHash;
    HashEntry *he;

    /* Search for the .ext file in the same way that efReadDef() does. */

    fp = ExtFileOpen(def, (char *)NULL, "r", (char **)NULL);
    if (fp == NULL)
    {
    	TxError("Cannot open file %s%s\n", def->cd_name, ".ext");
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
	 * and SUBSTRATE; and CONNECT is used to locate sink points.
	 * Note that MERGE is not useful here, as it may implicitly
 	 * merge nets in the cell, which is useful for netlisting but
	 * not for annotating the extraction file. 
	 */
	switch (keyTable[n].k_key)
	{
	    case SCALE:
		/* NOTE:  Currently the code assumes that the .ext
		 * file is read back immediately and has the same
		 * scale values currently in the extraction style.
		 * However, this should be style-independent and
		 * scale values should be read back and used.
		 * (to be completed).
		 */
		break;
	    case DEVICE:
		locresult = ResReadDevice(argc, argv);
		break;
	    case FET:
		locresult = ResReadFET(argc, argv);
		break;
	    case CONNECT:
		locresult = ResReadConnectPoint(def, argc, argv);
		break;
	    case PORT:
		locresult = ResReadPort(argc, argv);
		break;
	    case NODE:
	    case SUBSTRATE:
		curnode = ResReadNode(argc, argv);
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

    /* Find all the parent CellDefs of "def" and read the .ext file of
     * each one to find where connections are made to this cell from
     * parent cells.  Place drive points at each connection point.
     */
    HashInit(&parentHash, 32, HT_STRINGKEYS);

    for (use = def->cd_parents; use; use = use->cu_nextuse)
    {
	if ((parent = use->cu_parent) == NULL) continue;
	if (parent->cd_flags & CDINTERNAL) continue;
	he = HashFind(&parentHash, parent->cd_name);
	if ((CellDef *)HashGetValue(he) == NULL)
	{
	    /* Mark parent def as being visited */
	    HashSetValue(he, (char *)parent);
	    /* Read connection information from the parent's .ext file */
	    ResReadParentExt(parent, def);
	}
    }
    HashKill(&parentHash);

    return(result);
}

/*
 *-------------------------------------------------------------------------
 *
 * ResReadUse --
 *
 *	Read a "use" statement from the .ext file of a parent CellDef of
 *	the current def being extracted.  If the use is a use of the
 *	current def, then save the use name and its transform in the
 *	hash table so that later "connect" statements can be translated
 *	into the coordinate system of the current cell def.
 *
 * Results:
 *	1 if something went wrong with the parsing, 0 otherwise.
 *
 * Side effects:
 *	May add to the hash table.
 *
 *-------------------------------------------------------------------------
 */

int
ResReadUse(CellDef *def,
   int argc,
   char *argv[],
   HashTable *useHash)
{
    char *defname, *useid;
    Transform *tinv, t;
    HashEntry *he;

    defname = argv[USE_DEF_NAME];

    if (strcmp(defname, def->cd_name)) return 0;	/* Not my use */

    useid = argv[USE_ID_NAME];

    he = HashFind(useHash, useid);

    t.t_a = atoi(argv[USE_TRANSFORM_A]);
    t.t_b = atoi(argv[USE_TRANSFORM_B]);
    t.t_c = atoi(argv[USE_TRANSFORM_C]);
    t.t_d = atoi(argv[USE_TRANSFORM_D]);
    t.t_e = atoi(argv[USE_TRANSFORM_E]);
    t.t_f = atoi(argv[USE_TRANSFORM_F]);

    tinv = (Transform *)mallocMagic(sizeof(Transform));
    GeoInvertTrans(&t, tinv);

    HashSetValue(he, (char *)tinv);
    return 0;
}

/*
 *-------------------------------------------------------------------------
 *
 * ResReadDrivePoint --
 *
 *	Read a "connect" statement from the .ext file of a parent CellDef
 *	of the current def being extracted.  If the connection is made to
 *	a use of the current def, then translate the area of the connection
 *	into the current def, and mark the connection as a drive point of
 *	def.
 *
 * Results:
 *	1 if something went wrong with the parsing, 0 otherwise.
 *
 * Side effects:
 *	May add information to the node list of def.
 *
 *-------------------------------------------------------------------------
 */

int
ResReadDrivePoint(CellDef *def,
    int argc,
    char *argv[],
    HashTable *useHash)
{
    HashEntry	*entry;
    ResExtNode	*node;
    ResConnect  *newdriver;
    int pNum;
    TileType ttype;
    Transform *t;
    Rect r;
    char *hierptr, *useid, *qptr, *downname;

    /* Only handle entries that are in the use ID hash table */

    useid = argv[CONNECT_DOWN_NAME];
    if (*useid == '"') useid++;
    hierptr = strchr(useid, '/');
    if (hierptr != NULL) *hierptr = '\0';
    qptr = strrchr(useid, '"');
    if (qptr != NULL) *qptr = '\0';
    if (hierptr != NULL)
	downname = hierptr + 1;
    else
	downname = useid;	/* This is probably invalid */

    entry = HashFind(useHash, useid);
    if ((t = (Transform *)HashGetValue(entry)) == NULL) return 0;

    /* Check for the given tile type */
    ttype = DBTechNoisyNameType(argv[CONNECT_TYPE]);

    if (ttype == -1)
    {
	TxError("Bad tile type name \"%s\" in .ext file for node %s\n",
			argv[CONNECT_TYPE], argv[CONNECT_UP_NAME]);
	return 1;
    }

    /* Look up the node name */
    if (strcmp(downname, "None"))
    {
	entry = HashLookOnly(&ResNodeTable, downname);
	if (entry != NULL)
	    node = (ResExtNode *)HashGetValue(entry);
	else
	{
	    TxError("Unknown node name \"%s\" in .ext file connect entry\n",
			downname);
	    return 1;
	}

	/* Generate new drivepoint entry */

	newdriver = (ResConnect *)mallocMagic(sizeof(ResConnect));
    
	r.r_xbot = atoi(argv[CONNECT_LLX]);
	r.r_ybot = atoi(argv[CONNECT_LLY]);
	r.r_xtop = atoi(argv[CONNECT_URX]);
	r.r_ytop = atoi(argv[CONNECT_URY]);

	/* Translate the connection position from the parent to the
	 * current cell def.
	 */
	GeoTransRect(t, &r, &newdriver->rc_rect);

	newdriver->rc_type = ttype;
	newdriver->rc_node = (resNode *)NULL;

	newdriver->rc_next = node->drivepoints;
	node->drivepoints = newdriver;
	node->status |= FORCE | DRIVELOC;

	if (ResOptionsFlags & ResOpt_Debug)
	{
	    /* Diagnostic */
	    TxPrintf("Added driver at %d %d %d %d\n",
			newdriver->rc_rect.r_xbot, newdriver->rc_rect.r_ybot,
			newdriver->rc_rect.r_xtop, newdriver->rc_rect.r_ytop);
	}
    }

    return 0;
}

/*
 *-------------------------------------------------------------------------
 *
 * ResReadParentExt --
 *
 *	Read a .ext file for a parent cell of the cell being extracted.
 *	Each .ext file contains a list of connection points into its
 *	subcells.  However, no .ext file has information about how a
 *	parent cell connects to it;  the exact connection may depend on
 *	the layout, and may or may not coincide with marked ports.
 *	Except for the top level cell, for which only marked ports can
 *	be used to guess at intended points of connection, every subcell
 *	can query its parents to find exact points of connection.
 *
 * Results:  Returns 0 if ext file is correct, 1 if not.
 *
 * Side Effects:  Creates lists of connection points for extresist.
 *
 *-------------------------------------------------------------------------
 */

int
ResReadParentExt(CellDef *parent,
    CellDef *def)
{
    char *line = NULL, *argv[128];
    int	result, locresult;
    int argc, n, size = 0;
    FILE *fp;
    CellDef *dbdef;
    ResExtNode *curnode;
    HashTable useHash;
    HashEntry *he;
    HashSearch hs;

    /* Search for the .ext file in the same way that efReadDef() does. */

    fp = ExtFileOpen(parent, (char *)NULL, "r", (char **)NULL);
    if (fp == NULL)
    {
    	TxError("Cannot open file %s%s\n", parent->cd_name, ".ext");
	return 1;
    }

    HashInit(&useHash, 32, HT_STRINGKEYS);

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

	/* When reading a parent .ext file to find connections to
	 * the cell being extracted by "extresist", we only care
	 * about CONNECT lines, and USE lines so that we can
	 * translate the connection points into the current cell def.
	 *
	 * Note:  This method depends on the .ext file format having
	 * all "use" lines before "connect" lines.
	 */
	switch (keyTable[n].k_key)
	{
	    case USE:
		locresult = ResReadUse(def, argc, argv, &useHash);
		break;
	    case CONNECT:
		locresult = ResReadDrivePoint(def, argc, argv, &useHash);
		break;
	    default:
		break;
	}
	if (locresult == 1) result = 1;
    }
    fclose(fp);

    HashStartSearch(&hs);
    while ((he = HashNext(&useHash, &hs)))
    {
	if (HashGetValue(he) != NULL)
	{
	    freeMagic(HashGetValue(he));	/* Free the allocated tranform */
	    HashSetValue(he, (ClientData)NULL);
	}
    }
    HashKill(&useHash);
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
    int noderesist;

    entry = HashFind(&ResNodeTable, argv[NODES_NODENAME]);
    node = ResExtInitNode(entry);

    node->location.p_x = atoi(argv[NODES_NODEX]);
    node->location.p_y = atoi(argv[NODES_NODEY]);
    node->type = DBTechNameType(argv[NODES_NODETYPE]);
    noderesist = atoi(argv[NODES_NODERES]);
    if (noderesist < 0) noderesist = INFINITY;
    /* Make sure node resistance is in units of milliohms */
    node->resistance = (float)noderesist * (float)ExtCurStyle->exts_resistScale;

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
 * ResReadConnectPoint-- Reads in a "connect" statement from the .ext file
 *	and sets node records accordingly to mark the node as a connection
 *	point.  There is a use (instance) name associated with each connection,
 *	which is unused for finding connection points to subcells;  we
 *	don't care what the subcell is, only that there is a connection at
 *	a point on a net in this cell that should be recorded and never
 *	optimized out.
 *
 * Results:  0 if successful and 1 otherwise.
 *
 * Side Effects: see above
 *
 *-------------------------------------------------------------------------
 */

int
ResReadConnectPoint(CellDef *def,
    int argc,
    char *argv[])
{
    HashEntry	*entry;
    ResExtNode	*node;
    ResConnect  *newsink;
    int pNum;
    TileType ttype;

    /* Check for the given tile type */
    ttype = DBTechNoisyNameType(argv[CONNECT_TYPE]);

    if (ttype == -1)
    {
	TxError("Bad tile type name \"%s\" in .ext file for node %s\n",
			argv[CONNECT_TYPE], argv[CONNECT_UP_NAME]);
	return 1;
    }

    /* Look up the node name */
    if (strcmp(argv[CONNECT_UP_NAME], "None"))
    {
	entry = HashLookOnly(&ResNodeTable, argv[CONNECT_UP_NAME]);
	if (entry != NULL)
	    node = (ResExtNode *)HashGetValue(entry);
	else
	{
	    TxError("Unknown node name \"%s\" in .ext file connect entry\n",
			argv[CONNECT_UP_NAME]);
	    return 1;
	}

	/* Generate new sinkpoint entry */

	newsink = (ResConnect *)mallocMagic(sizeof(ResConnect));
    
	newsink->rc_rect.r_xbot = atoi(argv[CONNECT_LLX]);
	newsink->rc_rect.r_ybot = atoi(argv[CONNECT_LLY]);
	newsink->rc_rect.r_xtop = atoi(argv[CONNECT_URX]);
	newsink->rc_rect.r_ytop = atoi(argv[CONNECT_URY]);
	newsink->rc_type = ttype;
	newsink->rc_node = (resNode *)NULL;

	newsink->rc_next = node->sinkpoints;
	node->sinkpoints = newsink;
	node->status |= FORCE | DRIVELOC;

	if (ResOptionsFlags & ResOpt_Debug)
	{
	    /* Diagnostic */
	    TxPrintf("Added sink at %d %d %d %d\n", newsink->rc_rect.r_xbot,
			newsink->rc_rect.r_ybot, newsink->rc_rect.r_xtop,
			newsink->rc_rect.r_ytop);
	}
    }

    return 0;
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
 * NOTE: The use of "port" to mark drive points is restricted to top
 * level cells, because no other information is available about how the
 * cell connects to a parent cell.  For every cell other than the top
 * level, the "connect" statements are used to find the actual locations
 * where signals connect between cells through abutting or overlapping
 * material.
 *
 *-------------------------------------------------------------------------
 */

int
ResReadPort(int argc,
    char *argv[])
{
    HashEntry	*entry;
    ResExtNode	*node;
    ResConnect  *newdriver;

    entry = HashFind(&ResNodeTable, argv[PORT_NAME]);
    node = ResExtInitNode(entry);

    /* Generate new drivepoint entry */

    newdriver = (ResConnect *)mallocMagic(sizeof(ResConnect));

    newdriver->rc_rect.r_xbot = atoi(argv[PORT_LLX]);
    newdriver->rc_rect.r_ybot = atoi(argv[PORT_LLY]);
    newdriver->rc_rect.r_xtop = atoi(argv[PORT_URX]);
    newdriver->rc_rect.r_ytop = atoi(argv[PORT_URY]);
    newdriver->rc_type = DBTechNoisyNameType(argv[PORT_TYPE]);
    newdriver->rc_node = (resNode *)NULL;

    newdriver->rc_next = node->drivepoints;
    node->drivepoints = newdriver;
    node->status |= FORCE | DRIVELOC | PORTNODE;

    if (ResOptionsFlags & ResOpt_Debug)
    {
	/* Diagnostic */
	TxPrintf("Added port at %d %d %d %d\n",
		newdriver->rc_rect.r_xbot, newdriver->rc_rect.r_ybot,
		newdriver->rc_rect.r_xtop, newdriver->rc_rect.r_ytop);
    }

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
 *	Allocates memory for a devPtr, adds to the node's "devices" linked
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
    tptr->nextDev = node->devices;
    node->devices = tptr;
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
    int		rvalue, i, j, k, w, l, n;
    ExtDevice	*devptr;
    TileType	ttype;
    HashEntry	*entry;
    ResExtNode	*node;

    device = (RDev *)mallocMagic((unsigned)(sizeof(RDev)));

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

    /* Find the end of parameter arguments */
    for (i = DEV_Y; i < argc; i++)
    {
	char *eptr;
	if ((eptr = strchr(argv[i], '=')) == NULL)
	    if (!StrIsInt(argv[i]))
		break;
    }
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

    /* See code in extflat/EFbuild.c:  Devices do not necessarily
     * declare a substrate terminal.  This can be determined by
     * noting that all terminals other than the substrate come in
     * triplets of arguments, so if the remaining count of arguments
     * is divisible by 3, then there is no substrate node, and if
     * there is a remainder of 1, then there is.  It would probably
     * be simpler if all devices just put "None" in this position.
     */
    n = argc - i;
    if ((n % 3) == 1)
    {
	/* Device has a substrate argument or a numerical value */

	if (strcmp(argv[i], "None") && (!StrIsNumeric(argv[i])))
	{
	    entry = HashFind(&ResNodeTable, argv[i]);
	    device->subs = (ResExtNode *)HashGetValue(entry);
	    ResNodeAddDevice(device->subs, device, SUBS);
	}
	i++;
    }
    entry = HashFind(&ResNodeTable, argv[i]);
    device->gate = (ResExtNode *)HashGetValue(entry);
    device->rs_gattr = StrDup((char **)NULL, argv[i + 2]);
    l = atoi(argv[i + 1]);
    w = 0;
    ResNodeAddDevice(device->gate, device, GATE);
    i += 3;
    
    if (i < argc - 2)
    {
	entry = HashFind(&ResNodeTable, argv[i]);
	device->source = (ResExtNode *)HashGetValue(entry);
	device->rs_sattr = StrDup((char **)NULL, argv[i + 2]);
	w = atoi(argv[i + 1]);
	ResNodeAddDevice(device->source, device, SOURCE);
	i += 3;
    }

    if (i < argc - 2)
    {
	entry = HashFind(&ResNodeTable, argv[i]);
	device->drain = (ResExtNode *)HashGetValue(entry);
	device->rs_dattr = StrDup((char **)NULL, argv[i + 2]);
	w = MAX(w, atoi(argv[i + 1]));
	ResNodeAddDevice(device->drain, device, DRAIN);
	i += 3;
    }
    if (i < argc - 2)
    {
	TxError("Warning:  Device %s has more than 4 ports (not handled).\n", 
		argv[DEV_NAME]);
    }

    device->rs_ttype = extGetDevType(devptr->exts_deviceName);
    device->rs_wl = (l == 0) ? 0.0 : (float)w / (float)l;

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
    int		rvalue, i, j, k, w, l;
    ExtDevice	*devptr;
    TileType	ttype;
    HashEntry	*entry;
    ResExtNode	*node;

    device = (RDev *)mallocMagic((unsigned)(sizeof(RDev)));

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

    device->rs_gattr = RDEV_NOATTR;
    device->rs_sattr = RDEV_NOATTR;
    device->rs_dattr = RDEV_NOATTR;
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

    l = atoi(argv[FET_GATE_ATTR - 1]);
    w = atoi(argv[FET_SOURCE_ATTR - 1]);
    w = MAX(w, atoi(argv[FET_DRAIN_ATTR - 1]));
    device->rs_wl = (l == 0) ? 0.0 : (float)w / (float)l;

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
    node1 = ResExtInitNode(entry1);

    if (ResOptionsFlags & ResOpt_Signal)
    {
        node1->capacitance += MagAtof(argv[COUPLEVALUE]);
        entry2 = HashFind(&ResNodeTable, argv[COUPLETERMINAL2]);
        node2 = ResExtInitNode(entry2);
        node2->capacitance += MagAtof(argv[COUPLEVALUE]);
        return 0;
    }

    entry2 = HashFind(&ResNodeTable, argv[COUPLETERMINAL2]);
    node2 = ResExtInitNode(entry2);

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
	ResConnect *newdriver;

	/* Generate new drivepoint entry */

	newdriver = (ResConnect *)mallocMagic(sizeof(ResConnect));
    
	node->status |= DRIVELOC;
	newdriver->rc_rect.r_xbot = atoi(argv[RES_EXT_ATTR_X]);
	newdriver->rc_rect.r_ybot = atoi(argv[RES_EXT_ATTR_Y]);
	newdriver->rc_rect.r_xtop = atoi(argv[RES_EXT_ATTR_X]);
	newdriver->rc_rect.r_ytop = atoi(argv[RES_EXT_ATTR_Y]);
	newdriver->rc_type = DBTechNoisyNameType(argv[RES_EXT_ATTR_TYPE]);
	newdriver->rc_node = (resNode *)NULL;

	newdriver->rc_next = node->drivepoints;
	node->drivepoints = newdriver;
    }
    return 0;
}

/*
 *-------------------------------------------------------------------------
 *
 * ResExtInitNode --
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
ResExtInitNode(entry)
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
	node->cap_couple = 0;
	node->resistance = 0;
	node->type = 0;
	node->devices = NULL;
	node->name = entry->h_key.h_name;
	node->oldname = NULL;
	node->drivepoints = NULL;
	node->sinkpoints = NULL;
	node->location.p_x = INFINITY;
	node->location.p_y = INFINITY;
    }
    while (node->status & FORWARD)
    {
     	node = node->forward;
    }
    return node;
}
