
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
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/utils.h"
#include "utils/tech.h"
#include "textio/txcommands.h"
#include	"resis/resis.h"


/* constants defining where various fields can be found in .sim files. */
#define		RTRAN_LENGTH		4
#define		RTRAN_WIDTH		5
#define		RTRAN_TRANX		6
#define		RTRAN_TRANY		7
#define		RTRAN_ATTR		8
#define		RTRAN_NUM_ATTR		3
#define		RESNODENAME	1
#define		NODERESISTANCE	2
#define		COUPLETERMINAL1 1
#define		COUPLETERMINAL2 2
#define		COUPLEVALUE	3
#define		REALNAME	1
#define		ALIASNAME	2
#define		NODECIFCOMMAND	0
#define		NODENODENAME	1
#define		NODENODEX	2
#define		NODENODEY	3
#define		NODETYPE	4
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


#define MAXTOKEN 		256
#define MAXLINE 		40
#define MAXDIGIT		20


ResSimNode *ResInitializeNode();

ResSimNode	*ResOriginalNodes;	/*Linked List of Nodes 	*/
static float	lambda=1.0;       	/* Scale factor		*/
char	RTRAN_NOATTR[1]={'0'};
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
     (n)->rn_je=NULL;\
     (n)->rn_ce=NULL;\
     (n)->rn_noderes=RES_INFINITY;\
     (n)->location.p_x=MINFINITY;\
     (n)->location.p_y=MINFINITY;\
     (n)->rn_why=0;\
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
ResReadSim(simfile,fetproc,capproc,resproc,attrproc,mergeproc)
     char	*simfile;
     int	(*fetproc)(),(*capproc)(),(*resproc)();
     int	(*attrproc)(),(*mergeproc)();

{
     char line[MAXLINE][MAXTOKEN];
     int	result,fettype,extfile;
     FILE *fp, *fopen();
     
         fp = PaOpen(simfile,"r",".sim",".",(char *) NULL, (char **) NULL);
         if (fp == NULL)
         {
    	      TxError("Cannot open file %s%s\n",simfile,".sim");
	      return(1);
         }
	 extfile = 0;

     /*read in file */
     while (gettokens(line,fp) != 0)
     {
	       fettype = MINFINITY;
	       switch(line[0][0])
	       {
	       	    case '|': 	
		    		if (strcmp(line[NODEUNITS],"units:") == 0)
				{
				     lambda = (float)atof(line[NODELAMBDA]);
				     if (lambda == 0.0) lambda = 1.0; 
				}
				result=0;
				break;
		    case 'e':   fettype = DBTechNameType("efet");
		    		break;
		    case 'd':   fettype = DBTechNameType("dfet");
		    		break;
		    case 'n':   fettype = DBTechNameType("nfet");
		    		break;
		    case 'p':   fettype = DBTechNameType("pfet");
		    		break;
		    case 'b':	fettype = DBTechNameType("bnpn");
		    		break;
		    case 'C':   if (capproc) result = (*capproc)(line);
		    		break;
		    case 'R':   if (resproc)result = (*resproc)(line);
		    		break;
		    case '=':   if (mergeproc)result = (*mergeproc)(line);
		    		break;
		    case 'A':	if (attrproc) result = 
		    			(*attrproc)(line[ATTRIBUTENODENAME],
						    line[ATTRIBUTEVALUE],
						    simfile, &extfile);
		    		break;
		    case 'D':
		    case 'c':
		    case 'r':	break;
		    default:	result = 1;
		    		(void)fclose(fp);
		    		break;
	       }
	       if (fettype == -1)
	       {
	       	    TxError("Error in Reading tran line of sim file.\n");
		    result = 1;
	       }
	       else if (fettype != MINFINITY)
	       {
		    float sheetr;

		    sheetr=(float)ExtCurStyle->exts_linearResist[fettype];
		    result = (*fetproc)(line,sheetr,fettype);
	       }
	       if (result != 0)
	       {
	       	    TxError("Error in sim file %s\n",line[0]);
		    return(1);
	       }
     }
     (void)fclose(fp);
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
     
    fp = PaOpen(nodefile,"r",".nodes",".", (char *) NULL, (char **) NULL);
    if (fp == NULL)
    {
    	TxError("Cannot open file %s%s\n",nodefile,".nodes");
	return(1);
    }
    while (gettokens(line,fp) != 0)
    {
	entry = HashFind(&ResNodeTable,line[NODENODENAME]);
	node = ResInitializeNode(entry);
	      
	node->location.p_x = (int)((float)atof(line[NODENODEX])/lambda);
	node->location.p_y = (int)((float)atof(line[NODENODEY])/lambda);
#ifdef ARIEL	      
	node->rs_bbox.r_xbot = (int)((float)atof(line[NODE_BBOX_LL_X])/lambda);
	node->rs_bbox.r_ybot = (int)((float)atof(line[NODE_BBOX_LL_Y])/lambda);
	node->rs_bbox.r_xtop = (int)((float)atof(line[NODE_BBOX_UR_X])/lambda);
	node->rs_bbox.r_ytop = (int)((float)atof(line[NODE_BBOX_UR_Y])/lambda);
#endif
	if (cp = strchr(line[NODETYPE], ';')) *cp = '\0';
	node->type = DBTechNameType(line[NODETYPE]);

	if (node->type == -1)
	{
	    TxError("Bad tile type name in %s.nodes file for node %s\n",nodefile,node->name);
	    TxError("Did you use the newest version of ext2sim?\n");
	    (void)fclose(fp);
	    return(1);
	}
    }
    (void)fclose(fp);
    return(0);
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
gettokens(line,fp)
	char line[][MAXTOKEN];
	FILE	*fp;

{
     int i=0,j=0;
     int c;
     
     while ((c = getc(fp)) != EOF && c != '\n')
     {
     	  switch(c)
	  {
		case '	':
	        case ' ' : line[i++][j] = '\0';
			   j=0;
	       		   break;
		default:   line[i][j++] = c;
			   break;
	  }
     }
     if (c == '\n')
     {
     	  line[i++][j] = '\0';
	  j=0;
     }
     for(j=i;j < MAXLINE;j++)
     {
     	  line[j][0] = '\0';
     }
     return(i);
}


/*
 *-------------------------------------------------------------------------
 *
 *  ResSimTransistor-- Processes a transistor line from a sim file.
 *
 * Results: returns 0 if line was added correctly.
 *
 * Side Effects: Allocates transistors and adds nodes to the node hash table.
 *
 *-------------------------------------------------------------------------
 */

int
ResSimTransistor(line,rpersquare,ttype)
	char line[][MAXTOKEN];
	float	rpersquare;
	TileType	ttype;

{
     RTran		*transistor;
     int		rvalue,i,j,k;
     char		*newattr,tmpattr[MAXTOKEN];
     static int		nowarning = TRUE;
     
     transistor = (RTran *) mallocMagic((unsigned) (sizeof(RTran)));
     if ((line[RTRAN_WIDTH][0] == '\0') || (line[RTRAN_LENGTH][0] == '\0'))
     {
     	  TxError("error in input file:\n");
	  return(1);
     }
     else
     {
	   if (nowarning && rpersquare == 0)
	   {
	   	TxError("Warning- FET resistance not included or set to zero in technology file-\n");
		TxError("All driven nodes will be extracted\n");
		nowarning = FALSE;
	   }
	   transistor->resistance = MagAtof(line[RTRAN_LENGTH]) * rpersquare/MagAtof(line[RTRAN_WIDTH]);
     }
     transistor->tnumber = ++Maxtnumber;
     transistor->status = FALSE;
     transistor->nextTran = ResTranList;
     transistor->location.p_x = atoi(line[RTRAN_TRANX]);
     transistor->location.p_y = atoi(line[RTRAN_TRANY]);
     transistor->rs_gattr=RTRAN_NOATTR;
     transistor->rs_sattr=RTRAN_NOATTR;
     transistor->rs_dattr=RTRAN_NOATTR;
     transistor->rs_ttype = ttype;
     
     /* sim attributes look like g=a1,a2   	 */
     /* ext attributes are "a1","a2"	   	 */
     /* do conversion from one to the other here */

     for (i=RTRAN_ATTR;i < RTRAN_ATTR+RTRAN_NUM_ATTR;i++)
     {
     	  if (line[i][0] == '\0') break;
	  k=0;
	  tmpattr[k++]='"';
	  for (j=2;line[i][j] != '\0';j++)
	  {
	       if (line[i][j] == ',')
	       {
	       	    tmpattr[k++] = '"';
	       	    tmpattr[k++] = ',';
	       	    tmpattr[k++] = '"';
	       }
	       else
	       {
	            tmpattr[k++] = line[i][j];
	       }
	  }
	  tmpattr[k++]='"';
	  tmpattr[k++]='\0';
	  newattr = (char *) mallocMagic((unsigned) k);
	  strncpy(newattr,tmpattr,k);
	  switch (line[i][0])
	  {
	       case 'g': transistor->rs_gattr =  newattr; break;
	       case 's': transistor->rs_sattr =  newattr; break;
	       case 'd': transistor->rs_dattr =  newattr; break;
	       default: TxError("Bad fet attribute\n");
	       		break;
	  }
     }
     ResTranList = transistor;
     transistor->layout = NULL;
     rvalue = ResSimNewNode(line[GATE],GATE,transistor) +
     	      ResSimNewNode(line[SOURCE],SOURCE,transistor) +
     	      ResSimNewNode(line[DRAIN],DRAIN,transistor);
     
     return(rvalue);
}


/*
 *-------------------------------------------------------------------------
 *
 *  ResSimNewNode-- Adds a new node to the Node Hash Table.
 *
 * Results: returns zero if node is added correctly, one otherwise.
 *
 * Side Effects: Allocates a new ResSimNode
 *
 *-------------------------------------------------------------------------
 */

int
ResSimNewNode(line,type,transistor)
	char 		line[];
	int		type;
	RTran		*transistor;

{
     HashEntry		*entry;
     ResSimNode		*node;
     tranPtr		*tptr;
     
     if (line[0] == '\0')
     {
     	  TxError("Missing transistor connection\n");
	  return(1);
     }
     entry = HashFind(&ResNodeTable,line);
     node = ResInitializeNode(entry);
     tptr = (tranPtr *) mallocMagic((unsigned) (sizeof(tranPtr)));
     tptr->thisTran = transistor;
     tptr->nextTran = node->firstTran;
     node->firstTran = tptr;
     tptr->terminal = type;
     switch(type)
     {
     	  case GATE:   transistor->gate = node;
	  	       break;
     	  case SOURCE: transistor->source = node;
	  	       break;
     	  case DRAIN:  transistor->drain = node;
	  	       break;
	  default:  TxError("Bad Terminal Specifier\n");
	  		break;
     }
     return(0);
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
     HashEntry		*entry1,*entry2;
     ResSimNode		*node1,*node2;
     
     if (line[COUPLETERMINAL1][0] == 0 || line[COUPLETERMINAL2][0] == 0)
     {
     	  TxError("Bad Capacitor\n");
	  return(1);
     }
     entry1 = HashFind(&ResNodeTable,line[COUPLETERMINAL1]);
     node1 = ResInitializeNode(entry1);
     if (ResOptionsFlags & ResOpt_Signal)
     {
          node1->capacitance += MagAtof(line[COUPLEVALUE]);
          if (strcmp(line[COUPLETERMINAL2],"GND") == 0 || 
     	  strcmp(line[COUPLETERMINAL2],"Vdd") == 0)
          {
               return(0);
          }
          entry2 = HashFind(&ResNodeTable,line[COUPLETERMINAL2]);
          node2 = ResInitializeNode(entry2);
          node2->capacitance += MagAtof(line[COUPLEVALUE]);
          return(0);
     }
     if (strcmp(line[COUPLETERMINAL2],"GND") == 0 )
     {
          node1->capacitance += MagAtof(line[COUPLEVALUE]);
	  return(0);
     }
     if (strcmp(line[COUPLETERMINAL2],"Vdd") == 0 )
     {
          node1->cap_vdd += MagAtof(line[COUPLEVALUE]);
	  return(0);
     }
     entry2 = HashFind(&ResNodeTable,line[COUPLETERMINAL2]);
     node2 = ResInitializeNode(entry2);
     if (strcmp(line[COUPLETERMINAL1],"GND") == 0 )
     {
          node2->capacitance += MagAtof(line[COUPLEVALUE]);
	  return(0);
     }
     if (strcmp(line[COUPLETERMINAL1],"Vdd") == 0 )
     {
          node2->cap_vdd += MagAtof(line[COUPLEVALUE]);
	  return(0);
     }
     node1->cap_couple += MagAtof(line[COUPLEVALUE]);
     node2->cap_couple += MagAtof(line[COUPLEVALUE]);
     return(0);
     
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
     HashEntry		*entry;
     ResSimNode		*node;
     
     if (line[RESNODENAME][0] == 0)
     {
     	  TxError("Bad Resistor\n");
	  return(1);
     }
     entry = HashFind(&ResNodeTable,line[RESNODENAME]);
     node = ResInitializeNode(entry);
     if (node->resistance != 0)
     {
     	  TxError("Duplicate Resistance Entries\n");
	  return(1);
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
ResSimAttribute(aname,avalue,rootname,readextfile)
	char *aname,*avalue,*rootname;
	int	*readextfile;

{
     HashEntry		*entry;
     ResSimNode		*node;
     char		digit[MAXDIGIT];
     int		i;
     static int		notwarned=TRUE;
     
     if (aname[0] == 0)
     {
     	  TxError("Bad Resistor\n");
	  return(1);
     }
     entry = HashFind(&ResNodeTable,aname);
     node = ResInitializeNode(entry);
     if (strncmp(avalue,"res:skip",8) == 0)
     {
     	  if (node->status & FORCE)
	  {
	       TxError("Warning: Node %s is both forced and skipped\n",aname);
	  }
	  else
	  {
	       node->status |= SKIP;	
	  }
     }
     else if (strncmp(avalue,"res:force",9) == 0)
     {
     	  if (node->status & SKIP)
	  {
	       TxError("Warning: Node %s is both skipped and forced \n",aname);
	  }
	  else
	  {
	       node->status |= FORCE;	
	  }
     }
     else if (strncmp(avalue,"res:min=",8) == 0)     	  
     {
	 node->status |= MINSIZE;
	 for(i=0,avalue += 8; *avalue != '\0' && *avalue != ','; avalue++)
	 {
	      digit[i++] = *avalue;
	 }
	 digit[i++]='\0';
	 node->minsizeres=MagAtof(digit);
     }
     else if (strncmp(avalue,"res:drive",9) == 0 &&
     	      (ResOptionsFlags & ResOpt_Signal))
     {
	  if (*readextfile == 0)
	  {
	       ResSimProcessDrivePoints(rootname);
	       *readextfile = 1;
	  }
	  /* is the attribute in root.ext?    */
	  if (node->drivepoint.p_x != INFINITY) 
	  {
	       node->status |= DRIVELOC;
	  }
	  else
	  {
	       if (notwarned)
	       TxError("Drivepoint for %s not defined in %s.ext; is it defined in a child  cell?\n",node->name,rootname);
	       notwarned = FALSE;
	  }
     }
#ifdef ARIEL
     else if (strncmp(avalue,"res:fix",7) == 0 && 
     	      (ResOptionsFlags & ResOpt_Power))
     {
	  if (*readextfile == 0)
	  {
	       ResSimProcessFixPoints(rootname);
	       *readextfile = 1;
	  }
     }
#endif
     if (avalue = strchr(avalue,',')) 
     {
          (void) ResSimAttribute(aname,avalue+1,rootname,readextfile);
     }
     return(0);
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
	char	*filename;

{
     char	line[MAXLINE][MAXTOKEN];
     FILE	*fp;
     HashEntry	*entry;
     ResSimNode	*node;
     
     fp = PaOpen(filename,"r",".ext",".",(char *) NULL,(char **) NULL);
     if (fp == NULL)
     {
     	  TxError("Cannot open file %s%s\n",filename,".ext");
	  return;
     }
     while (gettokens(line,fp) != 0)
     {
     	  if (strncmp(line[RES_EXT_ATTR],"attr",4) != 0 ||
	      strncmp(line[RES_EXT_ATTR_TEXT],"\"res:drive\"",11) != 0) continue;
	  
	 entry = HashFind(&ResNodeTable,line[RES_EXT_ATTR_NAME]);
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
     char	line[MAXLINE][MAXTOKEN],*label,*c;
     FILE	*fp;
     ResFixPoint	*thisfix;
     
     fp = PaOpen(filename,"r",".ext",".",(char *) NULL,(char **) NULL);
     if (fp == NULL)
     {
     	  TxError("Cannot open file %s%s\n",filename,".ext");
	  return;
     }
     while (gettokens(line,fp) != 0)
     {
     	  if (strncmp(line[RES_EXT_ATTR],"attr",4) != 0 ||
	      strncmp(line[RES_EXT_ATTR_TEXT],"\"res:fix",8) != 0) continue;
	  label = line[RES_EXT_ATTR_TEXT];
	  label += 8;
	  if (*label == ':') label++;
	  if ((c=strrchr(label,'"')) != NULL) *c='\0';
	  else if (*label == '\0');
	  else 
	  {
	       TxError("Bad res:fix attribute label %s\n",
	       			line[RES_EXT_ATTR_TEXT]);
	       *label ='\0';
	  }
	  thisfix = (ResFixPoint *) mallocMagic((unsigned) (sizeof(ResFixPoint)+strlen(label)));
	  thisfix->fp_next = ResFixList;
	  ResFixList = thisfix;
	  thisfix->fp_loc.p_x = atoi(line[RES_EXT_ATTR_X]);
	  thisfix->fp_loc.p_y = atoi(line[RES_EXT_ATTR_Y]);
	  thisfix->fp_ttype = DBTechNoisyNameType(line[RES_EXT_ATTR_TILE]);
	  thisfix->fp_tile=NULL;
	  strcpy(thisfix->fp_name,label);
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
     ResSimNode		*node;
     tranPtr		*ptr;
     
     if ((line[ALIASNAME][0] == '\0') || (line[REALNAME][0] == '\0'))
     {
     	  TxError("Bad node alias line\n");
	  return(1);
     }
     node = ResInitializeNode(HashFind(&ResNodeTable,line[ALIASNAME]));
     node->status |= FORWARD;
     node->forward = ResInitializeNode(HashFind(&ResNodeTable,line[REALNAME]));
     node->forward->resistance += node->resistance;
     node->forward->capacitance += node->capacitance;
     while (node->firstTran != NULL)
     {
     	  ptr=node->firstTran;
	  node->firstTran = node->firstTran->nextTran;
	  ptr->nextTran = node->forward->firstTran;
	  node->forward->firstTran = ptr;
     }
     return(0);
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
	  node = (ResSimNode *) mallocMagic((unsigned)(sizeof(ResSimNode)));
	  HashSetValue(entry,(char *) node);
	  node->nextnode = ResOriginalNodes;
	  ResOriginalNodes = node;
	  node->status = FALSE;
	  node->forward = (ResSimNode *) NULL;
	  node->capacitance = 0;
	  node->cap_vdd = 0;
	  node->cap_couple = 0;
	  node->resistance = 0;
	  node->type = 0;
	  node->firstTran = NULL;
	  node->name = entry->h_key.h_name;
	  node->oldname = NULL;
	  node->drivepoint.p_x = INFINITY;
	  node->drivepoint.p_y = INFINITY;
	  node->location.p_x = INFINITY;
	  node->location.p_y = INFINITY;
	  node->rs_sublist[0]=NULL;
	  node->rs_sublist[1]=NULL;
     }
     while (node->status & FORWARD)
     {
     	  node = node->forward;
     }
     return(node);
}
