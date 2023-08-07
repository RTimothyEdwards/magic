
#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/resis/ResReadSim.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

/*
 *-------------------------------------------------------------------------
 *
 * ResReadSim.c -- Routines to parse .sim files
 *
 *-------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "utils/magic.h"
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
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/utils.h"
#include "utils/tech.h"
#include "textio/txcommands.h"
#include "resis/resis.h"


/* constants defining where various fields can be found in .sim files. */
#define		RDEV_LENGTH		4
#define		RDEV_WIDTH		5
#define		RDEV_DEVX		6
#define		RDEV_DEVY		7
#define		RDEV_ATTR		8
#define		RDEV_NUM_ATTR		3
#define		RESNODENAME	1
#define		NODERESISTANCE	2
#define		COUPLETERMINAL1 1
#define		COUPLETERMINAL2 2
#define		COUPLEVALUE	3
#define		REALNAME	1
#define		ALIASNAME	2
#define		NODES_NODENAME	0
#define		NODES_NODEX	1
#define		NODES_NODEY	2
#define		NODES_NODETYPE	3
#define		NODE_BBOX_LL_X	5
#define		NODE_BBOX_LL_Y	6
#define		NODE_BBOX_UR_X	7
#define		NODE_BBOX_UR_Y	8
#define		NODELAMBDA	2
#define		NODEUNITS	1
#define		ATTRIBUTENODENAME	1
#define		ATTRIBUTEVALUE		2

#define		RES_EXT_ATTR		0
#define		RES_EXT_ATTR_NAME	1
#define		RES_EXT_ATTR_X		2
#define		RES_EXT_ATTR_Y		3
#define		RES_EXT_ATTR_TILE	6
#define		RES_EXT_ATTR_TEXT	7


#define MAXTOKEN 		1024
#define MAXLINE 		40
#define MAXDIGIT		20


ResSimNode *ResInitializeNode();

ResSimNode	*ResOriginalNodes;	/*Linked List of Nodes 	*/
static float	resscale=1.0;       	/* Scale factor		*/
char	RDEV_NOATTR[1]={'0'};
ResFixPoint		*ResFixList;

#define nodeinit(n)\
{\
     (n)->rn_more = ResNodeList;\
     (n)->rn_less = NULL;\
     if (ResNodeList)\
     ResNodeList->rn_less = n;\
     ResNodeList = n;\
     (n)->rn_te = NULL;\
     (n)->rn_re = NULL;\
     (n)->rn_je = NULL;\
     (n)->rn_ce = NULL;\
     (n)->rn_noderes = RES_INFINITY;\
     (n)->location.p_x = MINFINITY;\
     (n)->location.p_y = MINFINITY;\
     (n)->rn_why = 0;\
     (n)->rn_status = TRUE;\
}

/* Forward declarations */

extern void ResSimProcessDrivePoints();

/*
 *-------------------------------------------------------------------------
 *
 * ResReadSim--
 *
 * Results: returns 0 if sim file is correct, 1 if not.
 *
 * Side Effects:Reads in SimTable and makes a hash table of nodes.
 *
 *-------------------------------------------------------------------------
 */

int
ResReadSim(simfile, fetproc, capproc, resproc, attrproc, mergeproc, subproc)
    char *simfile;
    int	 (*fetproc)(), (*capproc)(), (*resproc)();
    int	 (*attrproc)(), (*mergeproc)(), (*subproc)();

{
    char line[MAXLINE][MAXTOKEN];
    int	result, fettype, extfile;
    FILE *fp, *fopen();

    fp = PaOpen(simfile, "r", ".sim", ".", (char *)NULL, (char **)NULL);
    if (fp == NULL)
    {
    	TxError("Cannot open file %s%s\n", simfile, ".sim");
	return 1;
    }
    extfile = 0;

    /* Read in file */
    while (gettokens(line, fp) != 0)
    {
	fettype = MINFINITY;
	switch(line[0][0])
	{
	    case '|':
		if (strcmp(line[NODEUNITS],"units:") == 0)
		{
		    resscale = (float)atof(line[NODELAMBDA]);
		    if (resscale == 0.0) resscale = 1.0;
		}
		result=0;
		break;
	    case 'e':
		fettype = DBTechNameType("efet");
	   	break;
	    case 'd':
		fettype = DBTechNameType("dfet");
		break;
	    case 'n':
		fettype = DBTechNameType("nfet");
		break;
	    case 'p':
		fettype = DBTechNameType("pfet");
		break;
	    case 'b':
		fettype = DBTechNameType("bnpn");
		break;
	    case 'C':
		if (capproc) result = (*capproc)(line);
		break;
	    case 'R':
		if (resproc) result = (*resproc)(line);
		break;
	    case '=':
		/* Do not merge nodes, as this interferes with	*/
		/* extresist's primary function.		*/
		/* if (mergeproc) result = (*mergeproc)(line);	*/
		break;
	    case 'A':
		if (attrproc)
		    result = (*attrproc)(line[ATTRIBUTENODENAME],
			    line[ATTRIBUTEVALUE], simfile, &extfile);
		    break;
	    case 'x':
		fettype = DBNumTypes;
		break;
	    case 'D':
	    case 'c':
	    case 'r':
		break;
	    default:
		result = 1;
		fclose(fp);
		break;
	}
        if (fettype == -1)
        {
	    TxError("Error in Reading device line of sim file.\n");
	    result = 1;
	}
	else if (fettype == DBNumTypes)
	{
	    result = (*subproc)(line);
	}
	else if (fettype != MINFINITY)
	{
	    float sheetr;
	    ExtDevice *devptr;

	    devptr = ExtCurStyle->exts_device[fettype];
	    sheetr = (float)devptr->exts_linearResist;
	    result = (*fetproc)(line, sheetr, devptr);
	}
	if (result != 0)
	{
	    TxError("Error in sim file %s\n", line[0]);
	    return 1;
	}
    }
    fclose(fp);
    return(result);
}


/*
 *-------------------------------------------------------------------------
 *
 * ResReadNode-- Reads in a node file, puts location of nodes into node
 *	structures.
 *
 * Results: returns 0 if nodes file is correct, 1 if not.
 *
 * Side Effects:see above
 *
 *-------------------------------------------------------------------------
 */

int
ResReadNode(nodefile)
    char	*nodefile;
{
    char line[MAXLINE][MAXTOKEN];
    FILE *fp, *fopen();
    HashEntry	*entry;
    ResSimNode	*node;
    char *cp;
    float lambda;

    fp = PaOpen(nodefile, "r", ".nodes", ".", (char *)NULL, (char **)NULL);
    if (fp == NULL)
    {
    	TxError("Cannot open file %s%s\n", nodefile, ".nodes");
	return 1;
    }
    while (gettokens(line,fp) != 0)
    {
	entry = HashFind(&ResNodeTable, line[NODES_NODENAME]);
	node = ResInitializeNode(entry);

	node->location.p_x = atoi(line[NODES_NODEX]);
	node->location.p_y = atoi(line[NODES_NODEY]);
#ifdef ARIEL
	node->rs_bbox.r_xbot = atoi(line[NODE_BBOX_LL_X]);
	node->rs_bbox.r_ybot = atoi(line[NODE_BBOX_LL_Y]);
	node->rs_bbox.r_xtop = atoi(line[NODE_BBOX_UR_X]);
	node->rs_bbox.r_ytop = atoi(line[NODE_BBOX_UR_Y]);
#endif
	if (cp = strchr(line[NODES_NODETYPE], ';')) *cp = '\0';
	node->type = DBTechNameType(line[NODES_NODETYPE]);

	if (node->type == -1)
	{
	    TxError("Bad tile type name in %s.nodes file for node %s\n",
			nodefile, node->name);
	    TxError("Did you use the newest version of ext2sim?\n");
	    fclose(fp);
	    return 1;
	}
    }
    fclose(fp);
    return 0;
}

/*
 *-------------------------------------------------------------------------
 *
 * getline-- Gets a  line from the current input file and breaks it into
 *	tokens.
 *
 * Results:returns the number of tokens in the current line
 *
 * Side Effects: loads up its input line with the tokens.
 *
 *-------------------------------------------------------------------------
 */

int
gettokens(line, fp)
    char line[][MAXTOKEN];
    FILE *fp;
{
    int i = 0, j = 0;
    int c;

    while ((c = getc(fp)) != EOF && c != '\n')
    {
     	switch(c)
	{
	    case '	':
	    case ' ' :
		line[i++][j] = '\0';
		j = 0;
	       	break;
	    default:
		line[i][j++] = c;
		break;
	}
    }
    if (c == '\n')
    {
     	line[i++][j] = '\0';
	j = 0;
    }
    for (j = i; j < MAXLINE; j++)
     	line[j][0] = '\0';

    return i;
}

/*
 *-------------------------------------------------------------------------
 *
 *  ResSimSubckt-- Processes a subcircuit line from a sim file.
 *	    This uses the "user subcircuit" extension defined in
 *	    IRSIM, although it is mostly intended as a way to work
 *	    around the device type limitations of the .sim format
 *	    when using extresist.
 *
 * Results: returns 0 if line was added correctly.
 *
 * Side Effects: Allocates devices and adds nodes to the node hash table.
 *
 *-------------------------------------------------------------------------
 */

int
ResSimSubckt(line)
    char line[][MAXTOKEN];
{
    RDev	*device;
    int		rvalue, i, j, k;
    static int	nowarning = TRUE;
    float	lambda;
    TileType	ttype = TT_SPACE;
    char	*lptr = NULL, *wptr = NULL;
    ExtDevice	*devptr;

    device = (RDev *) mallocMagic((unsigned) (sizeof(RDev)));

    device->status = FALSE;
    device->nextDev = ResRDevList;

    lambda = (float)ExtCurStyle->exts_unitsPerLambda / resscale;
    device->location.p_x = 0;
    device->location.p_y = 0;

    device->rs_gattr=RDEV_NOATTR;
    device->rs_sattr=RDEV_NOATTR;
    device->rs_dattr=RDEV_NOATTR;

    ResRDevList = device;
    device->layout = NULL;
    device->source = device->drain = device->gate = device->subs = NULL;

    /* The last argument is the name of the device */
    for (i = 1; line[i][0] != '\0'; i++);
    i--;

    /* To do:  Replace this search with a pre-prepared hash	*/
    /* table to key off of the device name.			*/
    for (j = 0; j < EFDevNumTypes; j++)
	if (!strcmp(EFDevTypes[j], line[i]))
	    break;

    /* Read attributes, especially to pick up values for L, W, X, and Y;
     * and source and drain area and perimeter, that are critical for use
     * by extresist.
     */
    for (k = 1; line[k][0] != '\0'; k++)
    {
	char *eqptr;
	eqptr = strchr(line[k], '=');
	if (eqptr != NULL)
	{
	    if (k < i) i = k;
	    eqptr++;
	    switch (line[k][0]) {
		case 'l':
		    lptr = eqptr;
		    break;
		case 'w':
		    wptr = eqptr;
		    break;
		case 'x':
		    device->location.p_x = (int)((float)atof(eqptr) / lambda);
		    break;
		case 'y':
		    device->location.p_y = (int)((float)atof(eqptr) / lambda);
		    break;
		case 's':
		    device->rs_sattr = StrDup((char **)NULL, eqptr);
		    break;
		case 'd':
		    device->rs_dattr = StrDup((char **)NULL, eqptr);
		    break;
	    }
	}
    }

    if (j == EFDevNumTypes)
    {
	TxError("Failure to find device type %s\n", line[i]);
	return 1;
    }
    ttype = extGetDevType(EFDevTypes[j]);

    /* Find the device record that corresponds to the device name */
    for (devptr = ExtCurStyle->exts_device[ttype]; devptr; devptr = devptr->exts_next)
	if (!strcmp(devptr->exts_deviceName, EFDevTypes[j]))
	    break;

    device->rs_devptr = devptr;
    device->rs_ttype = ttype;

    if (lptr != NULL && wptr != NULL)
    {
	float rpersquare;

	rpersquare =(float)devptr->exts_linearResist;
	/* Subcircuit types may not have a length or width value, in which  */
	/* case it is zero.  Don't induce a divide-by-zero error.	    */
	if (MagAtof(wptr) == 0)
	    device->resistance = 0;
	else
	    device->resistance = MagAtof(lptr) * rpersquare/MagAtof(wptr);
    }
    else
	device->resistance = 0;

    rvalue = 0;
    for (k = 1; k < i; k++)
    {
	if (k > SUBS)
	{
	    TxError("Device %s has more than 4 ports (not handled).\n", line[i]);
	    break;	    /* No method to handle more ports than this */
	}
	rvalue += ResSimNewNode(line[k], k, device);
    }

    return rvalue;
}

/*
 *-------------------------------------------------------------------------
 *
 *  ResSimDevice-- Processes a device line from a sim file.
 *
 * Results: returns 0 if line was added correctly.
 *
 * Side Effects: Allocates devices and adds nodes to the node hash table.
 *
 *-------------------------------------------------------------------------
 */

int
ResSimDevice(line, rpersquare, devptr)
    char	line[][MAXTOKEN];
    float	rpersquare;
    ExtDevice	*devptr;

{
    RDev	*device;
    int		rvalue, i, j, k;
    char	*newattr, tmpattr[MAXTOKEN];
    static int	nowarning = TRUE;
    float	lambda;
    ExtDevice *devtest;

    if ((line[RDEV_WIDTH][0] == '\0') || (line[RDEV_LENGTH][0] == '\0'))
    {
     	  TxError("error in input file:\n");
	  return 1;
    }

    device = (RDev *)mallocMagic((unsigned)(sizeof(RDev)));
    if (nowarning && rpersquare == 0)
    {
	TxError("Warning:  FET resistance not included or "
		"set to zero in technology file-\n");
	TxError("All driven nodes will be extracted\n");
	nowarning = FALSE;
    }
    if (MagAtof(line[RDEV_WIDTH]) != 0)
	device->resistance = MagAtof(line[RDEV_LENGTH]) * rpersquare /
		MagAtof(line[RDEV_WIDTH]);
    else
	device->resistance = 0;

    device->status = FALSE;
    device->nextDev = ResRDevList;

    /* Check that devptr matches the device name and number of terminals */
    /* Note that this routine is only called for the original "fet"	 */
    /* types with fixed names, so the names must match and there must	 */
    /* always be three terminals (two source/drain terminals).		 */

    if (devptr->exts_deviceSDCount != 2)
	for (devtest = devptr->exts_next; devtest; devtest = devtest->exts_next)
	    if (devtest->exts_deviceSDCount == 2)
	    {
		devptr = devtest;
		break;
	    }

    lambda = (float)ExtCurStyle->exts_unitsPerLambda / resscale;
    device->location.p_x = (int)((float)atof(line[RDEV_DEVX]) / lambda);
    device->location.p_y = (int)((float)atof(line[RDEV_DEVY]) / lambda);

    device->rs_gattr=RDEV_NOATTR;
    device->rs_sattr=RDEV_NOATTR;
    device->rs_dattr=RDEV_NOATTR;
    device->rs_devptr = devptr;

    device->gate = device->source = device->drain = device->subs = NULL;

    device->rs_ttype = extGetDevType(devptr->exts_deviceName);

    /* sim attributes look like g=a1,a2   	*/
    /* ext attributes are "a1","a2"	   	*/
    /* Do conversion from one to the other here	*/
    /* NOTE:  As of version 8.3.366, .ext attributes will end in two	*/
    /* integer values, not quoted, for device area and perimeter.  Do	*/
    /* not quote them.							*/

    for (i = RDEV_ATTR; i < RDEV_ATTR + RDEV_NUM_ATTR; i++)
    {
	char *cptr, *sptr;
	int d1, d2;

     	if (line[i][0] == '\0') break;

	sptr = &line[i][2];	/* Start after "s=" or "d=" */
	tmpattr[0] = '\0';
	while ((cptr = strchr(sptr, ',')) != NULL)
	{
	    if (sscanf(sptr, "%d,%d", &d1, &d2) == 2)
	    {
		strcat(tmpattr, sptr);
		sptr = NULL;
		break;
	    }
	    else
	    {
		*cptr = '\0';
		strcat(tmpattr, "\"");
		strcat(tmpattr, sptr);
		strcat(tmpattr, "\",");
		sptr = cptr + 1;
		*cptr = ',';
	    }
	}
	if (sptr && (strlen(sptr) != 0))
	{
	    strcat(tmpattr, "\"");
	    strcat(tmpattr, sptr);
	    strcat(tmpattr, "\"");
	}

	newattr = (char *)mallocMagic(strlen(tmpattr) + 1);
	strcpy(newattr, tmpattr);
	switch (line[i][0])
	{
	    case 'g':
		device->rs_gattr = newattr;
		break;
	    case 's':
		device->rs_sattr = newattr;
		break;
	    case 'd':
		device->rs_dattr = newattr;
		break;
	    default:
		TxError("Bad fet attribute\n");
	       	break;
	}
    }
    ResRDevList = device;
    device->layout = NULL;
    rvalue = ResSimNewNode(line[GATE], GATE, device) +
     	     ResSimNewNode(line[SOURCE], SOURCE, device) +
     	     ResSimNewNode(line[DRAIN], DRAIN, device);

    return rvalue;
}

/*
 *-------------------------------------------------------------------------
 *
 * ResSimNewNode-- Adds a new node to the Node Hash Table.
 *
 * Results: returns zero if node is added correctly, one otherwise.
 *
 * Side Effects: Allocates a new ResSimNode
 *
 *-------------------------------------------------------------------------
 */

int
ResSimNewNode(line, type, device)
    char 	line[];
    int		type;
    RDev	*device;

{
    HashEntry		*entry;
    ResSimNode		*node;
    devPtr		*tptr;

    if (line[0] == '\0')
    {
     	TxError("Missing device connection\n");
	return 1;
    }
    entry = HashFind(&ResNodeTable, line);
    node = ResInitializeNode(entry);
    tptr = (devPtr *)mallocMagic((unsigned)(sizeof(devPtr)));
    tptr->thisDev = device;
    tptr->nextDev = node->firstDev;
    node->firstDev = tptr;
    tptr->terminal = type;
    switch(type)
    {
     	case GATE:
	    device->gate = node;
	    break;
     	case SOURCE:
	    device->source = node;
	    break;
     	case DRAIN:
	    device->drain = node;
	    break;
     	case SUBS:
	    device->subs = node;
	    break;
	default:
	    TxError("Bad Terminal Specifier\n");
	    break;
    }
    return 0;
}

/*
 *-------------------------------------------------------------------------
 *
 *  ResSimCapacitor-- Adds the capacitance  from a C line to the appropriate
 *	node. Coupling capacitors are added twice, moving the capacitance
 *	to the substrate.
 *
 *  Results:
 *	Always return 0
 *
 *  Side Effects: modifies capacitance field  of ResSimNode.
 *
 *-------------------------------------------------------------------------
 */

int
ResSimCapacitor(line)
    char line[][MAXTOKEN];

{
    HashEntry	*entry1, *entry2;
    ResSimNode	*node1, *node2;

    if (line[COUPLETERMINAL1][0] == 0 || line[COUPLETERMINAL2][0] == 0)
    {
     	TxError("Bad Capacitor\n");
	return(1);
    }
    entry1 = HashFind(&ResNodeTable, line[COUPLETERMINAL1]);
    node1 = ResInitializeNode(entry1);
    if (ResOptionsFlags & ResOpt_Signal)
    {
        node1->capacitance += MagAtof(line[COUPLEVALUE]);
        if (strcmp(line[COUPLETERMINAL2], "GND") == 0 ||
		strcmp(line[COUPLETERMINAL2], "Vdd") == 0)
        {
            return 0;
        }
        entry2 = HashFind(&ResNodeTable, line[COUPLETERMINAL2]);
        node2 = ResInitializeNode(entry2);
        node2->capacitance += MagAtof(line[COUPLEVALUE]);
        return 0;
    }
    if (strcmp(line[COUPLETERMINAL2], "GND") == 0)
    {
        node1->capacitance += MagAtof(line[COUPLEVALUE]);
	return 0;
    }
    if (strcmp(line[COUPLETERMINAL2], "Vdd") == 0)
    {
        node1->cap_vdd += MagAtof(line[COUPLEVALUE]);
	return 0;
    }
    entry2 = HashFind(&ResNodeTable, line[COUPLETERMINAL2]);
    node2 = ResInitializeNode(entry2);
    if (strcmp(line[COUPLETERMINAL1], "GND") == 0)
    {
        node2->capacitance += MagAtof(line[COUPLEVALUE]);
	return 0;
    }
    if (strcmp(line[COUPLETERMINAL1], "Vdd") == 0)
    {
        node2->cap_vdd += MagAtof(line[COUPLEVALUE]);
	return 0;
    }
    node1->cap_couple += MagAtof(line[COUPLEVALUE]);
    node2->cap_couple += MagAtof(line[COUPLEVALUE]);
    return 0;
}


/*
 *-------------------------------------------------------------------------
 *
 *  ResSimResistor-- Adds the capacitance  from a R line to the appropriate
 *	node.
 *
 *  Results
 *	Return 0 to keep search going, 1 to abort
 *
 *  Side Effects: modifies resistance field of ResSimNode
 *
 *-------------------------------------------------------------------------
 */

int
ResSimResistor(line)
    char line[][MAXTOKEN];
{
    HashEntry	*entry;
    ResSimNode	*node;

    if (line[RESNODENAME][0] == 0)
    {
     	TxError("Bad Resistor\n");
	return 1;
    }
    entry = HashFind(&ResNodeTable, line[RESNODENAME]);
    node = ResInitializeNode(entry);
    if (node->resistance != 0)
    {
     	  TxError("Duplicate Resistance Entries\n");
	  return 1;
    }
    node->resistance = MagAtof(line[NODERESISTANCE]);
    return(0);
}

/*
 *-------------------------------------------------------------------------
 *
 *  ResSimAttribute--checks to see if a node attribute is a resistance
 *	attribute. If it is, add it to the correct node's status flag.
 *	Only works with 5.0 1/line attributes
 *
 *  Results:
 *	Return 0 to keep search going, 1 to abort
 *
 *  Side Effects: modifies resistance field of ResSimNode
 *
 *-------------------------------------------------------------------------
 */

int
ResSimAttribute(aname, avalue, rootname, readextfile)
    char *aname, *avalue, *rootname;
    int	 *readextfile;

{
    HashEntry	*entry;
    ResSimNode	*node;
    char	digit[MAXDIGIT];
    int		i;
    static int	notwarned=TRUE;

    if (aname[0] == 0)
    {
     	TxError("Bad Resistor\n");
	return 1;
    }
    entry = HashFind(&ResNodeTable, aname);
    node = ResInitializeNode(entry);
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
	{
	    TxError("Warning: Node %s is both skipped and forced \n", aname);
	}
	else
	{
	    node->status |= FORCE;
	}
    }
    else if (strncmp(avalue, "res:min=", 8) == 0)
    {
	node->status |= MINSIZE;
	for (i = 0, avalue += 8; *avalue != '\0' && *avalue != ','; avalue++)
	{
	    digit[i++] = *avalue;
	}
	digit[i++] = '\0';
	node->minsizeres = MagAtof(digit);
    }
    else if (strncmp(avalue, "res:drive", 9) == 0 &&
     	      (ResOptionsFlags & ResOpt_Signal))
    {
	if (*readextfile == 0)
	{
	    ResSimProcessDrivePoints(rootname);
	    *readextfile = 1;
	}
	/* is the attribute in root.ext? */
	if (node->drivepoint.p_x != INFINITY)
	{
	    node->status |= DRIVELOC;
	}
	else
	{
	    if (notwarned)
	    TxError("Drivepoint for %s not defined in %s.ext; is it "
		    "defined in a child  cell?\n", node->name, rootname);
	    notwarned = FALSE;
	}
    }
#ifdef ARIEL
    else if (strncmp(avalue, "res:fix", 7) == 0 &&
     	      (ResOptionsFlags & ResOpt_Power))
    {
	if (*readextfile == 0)
	{
	    ResSimProcessFixPoints(rootname);
	    *readextfile = 1;
	}
    }
#endif
    if (avalue = strchr(avalue, ','))
    {
        ResSimAttribute(aname, avalue + 1, rootname, readextfile);
    }
    return 0;
}

/*
 *-------------------------------------------------------------------------
 *
 * ResSimProcessDrivePoints -- if the sim file contains a res:drive attribute,
 *	and we are doing a signal extraction,
 *	we need to search through the .ext file looking for attr labels that
 *	contain this text. For efficiency, the .ext file is only parsed when
 *	the first res:drive is encountered.  res:drive labels only work if
 *	they are in the root cell.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *
 *-------------------------------------------------------------------------
 */

void
ResSimProcessDrivePoints(filename)
    char    *filename;

{
    char	line[MAXLINE][MAXTOKEN];
    FILE	*fp;
    HashEntry	*entry;
    ResSimNode	*node;

    fp = PaOpen(filename, "r", ".ext", (ExtLocalPath == NULL) ? "." : ExtLocalPath,
			(char *)NULL, (char **)NULL);
    if (fp == NULL)
    {
     	TxError("Cannot open file %s%s\n", filename, ".ext");
	return;
    }
    while (gettokens(line,fp) != 0)
    {
     	if (strncmp(line[RES_EXT_ATTR], "attr", 4) != 0 ||
		strncmp(line[RES_EXT_ATTR_TEXT], "\"res:drive\"", 11) != 0)
	    continue;

	entry = HashFind(&ResNodeTable, line[RES_EXT_ATTR_NAME]);
	node = ResInitializeNode(entry);
	node->drivepoint.p_x = atoi(line[RES_EXT_ATTR_X]);
	node->drivepoint.p_y = atoi(line[RES_EXT_ATTR_Y]);
	node->rs_ttype = DBTechNoisyNameType(line[RES_EXT_ATTR_TILE]);
    }
}

/*
 *-------------------------------------------------------------------------
 *
 * ResSimProcessFixPoints -- if the sim file contains a "res:fix:name" label
 *	and we are checking for power supply noise, then we have to
 *	parse the .ext file looking for the fix label locations.  This
 *	is only done after the first res:fix label is encountered.
 *
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	For each new name, allocate memory
 *
 *-------------------------------------------------------------------------
 */

void
ResSimProcessFixPoints(filename)
    char	*filename;
{
    char	line[MAXLINE][MAXTOKEN], *label, *c;
    FILE	*fp;
    ResFixPoint	*thisfix;

    fp = PaOpen(filename, "r", ".ext", (ExtLocalPath == NULL) ? "." : ExtLocalPath,
			(char *)NULL, (char **)NULL);
    if (fp == NULL)
    {
     	TxError("Cannot open file %s%s\n", filename, ".ext");
	return;
    }
    while (gettokens(line, fp) != 0)
    {
     	if (strncmp(line[RES_EXT_ATTR], "attr", 4) != 0 ||
		strncmp(line[RES_EXT_ATTR_TEXT], "\"res:fix", 8) != 0)
	    continue;
	label = line[RES_EXT_ATTR_TEXT];
	label += 8;
	if (*label == ':') label++;
	if ((c=strrchr(label, '"')) != NULL) *c = '\0';
	else if (*label != '\0')
	{
	    TxError("Bad res:fix attribute label %s\n",
	       			line[RES_EXT_ATTR_TEXT]);
	    *label ='\0';
	}
	thisfix = (ResFixPoint *)mallocMagic((unsigned)(sizeof(ResFixPoint)
		    + strlen(label)));
	thisfix->fp_next = ResFixList;
	ResFixList = thisfix;
	thisfix->fp_loc.p_x = atoi(line[RES_EXT_ATTR_X]);
	thisfix->fp_loc.p_y = atoi(line[RES_EXT_ATTR_Y]);
	thisfix->fp_ttype = DBTechNoisyNameType(line[RES_EXT_ATTR_TILE]);
	thisfix->fp_tile = NULL;
	strcpy(thisfix->fp_name, label);
    }
}


/*
 *-------------------------------------------------------------------------
 *
 * ResSimMerge-- Processes = line in sim file
 *
 * Results: Success/Failure
 *
 * Side Effects: The forward field of one node is set to point to the
 *	other node. All of the junkt from the first node is moved to
 *	the second node.
 *
 *-------------------------------------------------------------------------
 */

int
ResSimMerge(line)
    char line[][MAXTOKEN];

{
    ResSimNode	*node;
    devPtr	*ptr;

    if ((line[ALIASNAME][0] == '\0') || (line[REALNAME][0] == '\0'))
    {
     	TxError("Bad node alias line\n");
	return(1);
    }
    node = ResInitializeNode(HashFind(&ResNodeTable, line[ALIASNAME]));
    node->status |= FORWARD;
    node->forward = ResInitializeNode(HashFind(&ResNodeTable, line[REALNAME]));
    node->forward->resistance += node->resistance;
    node->forward->capacitance += node->capacitance;
    while (node->firstDev != NULL)
    {
     	ptr = node->firstDev;
	node->firstDev = node->firstDev->nextDev;
	ptr->nextDev = node->forward->firstDev;
	node->forward->firstDev = ptr;
    }
    return 0;
}

/*
 *-------------------------------------------------------------------------
 *
 * ResInitializeNode-- Gets the node corresponding to a given hash table
 *	entry.  If no such node exists, one is created.
 *
 * Results:Returns ResSimNode corresponding to entry.
 *
 * Side Effects: May allocate a new ResSimNode.
 *
 *-------------------------------------------------------------------------
 */

ResSimNode *
ResInitializeNode(entry)
    HashEntry	*entry;
{
    ResSimNode	*node;

    if ((node = (ResSimNode *) HashGetValue(entry)) == NULL)
    {
	node = (ResSimNode *)mallocMagic((unsigned)(sizeof(ResSimNode)));
	HashSetValue(entry, (char *) node);
	node->nextnode = ResOriginalNodes;
	ResOriginalNodes = node;
	node->status = FALSE;
	node->forward = (ResSimNode *) NULL;
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
