/*
 * ext2spice.h --
 *	Definitions for ext2spice.c and ext2hier.c
 */

#ifndef _EXTTOSPICE_H
#define _EXTTOSPICE_H

/* cache list used to find parallel devs */
typedef struct _devMerge {
	float  l, w;
	EFNode *g, *s, *d, *b;
	Dev * dev;
	int	  esFMIndex;
	HierName *hierName;
	struct _devMerge *next;
} devMerge;

/* Forward declarations */
void CmdExtToSpice();
extern int spcParseArgs();
extern int spccapVisit(), spcdevVisit(), spcnodeVisit(), subcktVisit();
extern int spcresistVisit(), devMergeVisit(), devDistJunctVisit();
extern int spcsubVisit();
extern int subcktUndef();
extern EFNode *spcdevSubstrate();
extern char *nodeSpiceName();
extern int nodeVisitDebug();
extern void topVisit();
extern void swapDrainSource();

extern char *nodeSpiceHierName();
extern devMerge *mkDevMerge();
extern bool extHierSDAttr();
extern int esFreeNodeClient();

extern bool devIsKilled();
extern float getCurDevMult();
extern void addDevMult();
extern void setDevMult();

/* C99 compat */
extern int  EFHNSprintf();
extern int  printSubcktDict();
extern int  spcdevOutNode();
extern int  spcnAP();
extern int  parallelDevs();
extern int  nodeHspiceName();
extern int  devDistJunctHierVisit();
extern int  spcnAPHier();
extern void mergeAttr();
extern int  update_w();
extern void esSIvalue();

/* Options specific to ext2spice */
extern bool esDoExtResis;
extern bool esDoPorts;
extern bool esDoHierarchy;
extern bool esDoBlackBox;
extern bool esDoResistorTee;
extern int  esDoSubckt;
extern bool esDevNodesOnly;
extern bool esNoAttrs;
extern bool esHierAP;
extern char spcesDefaultOut[FNSIZE];
extern char *esSpiceCapNode;
extern char esSpiceDefaultGnd[];
extern char *spcesOutName;
extern FILE *esSpiceF;
extern float esScale;	/* negative if hspice the EFScale/100 otherwise */

extern unsigned short esFormat;
extern TileTypeBitMask initMask;

extern int esCapNum, esDevNum, esResNum, esDiodeNum, esVoltNum;
extern int esNodeNum;  /* just in case we're extracting spice2 */
extern int esSbckNum; 	/* used in hspice node name shortening   */
extern int esNoModelType;  /* index for device type "None" (model-less device) */

extern bool esMergeDevsA; /* aggressive merging of devs L1=L2 merge them */
extern bool esMergeDevsC; /* conservative merging of devs L1=L2 and W1=W2 */
			   /* used with the hspice multiplier */
extern bool esDistrJunct;

extern float	*esFMult;       /* the array itself */
extern int	 esFMIndex;     /* current index to it */
extern int	 esFMSize; 	/* its current size (growable) */

extern int	 esSpiceDevsMerged;
extern devMerge *devMergeList;

/*
 * The following hash table and associated functions are used only if
 * the format is hspice, to keep the translation between the hierarchical
 * prefix of a node and the x num that we use to output valid hspice
 * which also are meaningful.
 */
extern HashTable subcktNameTable ; /* the hash table itself */
extern DQueue    subcktNameQueue ; /* q used to print it sorted at the end*/


typedef struct {
   short resClassSource ;  /* the resistance class of the source of the dev */
   short resClassDrain ;  /* the resistance class of the drain of the dev */
   short resClassSub ; /* the resistance class of the substrate of the dev */
   char  *defSubs ;    /* the default substrate node */
} fetInfoList;

extern fetInfoList esFetInfo[TT_MAXTYPES];

#define MAX_STR_SIZE (1<<11) /* 2K should be enough for keeping temp
                                names even of the most complicated design */

/* Node clients for figuring out areas and perimeters of sources and drains */

typedef union {
	TileTypeBitMask	visitMask; /* mask for normal visits */
	float	*widths; /* width used for distributing area perim */
} maskOrWidth ;

typedef struct {
	char		*spiceNodeName;
	maskOrWidth	m_w;
} nodeClient;

typedef struct {
	HierName 	*lastPrefix;
	maskOrWidth	m_w;
} nodeClientHier;

#define	NO_RESCLASS	-1

#define markVisited(client, rclass) \
  { TTMaskSetType(&((client)->m_w.visitMask), rclass); }

#define clearVisited(client) \
   { TTMaskZero(&((client)->m_w.visitMask)); }

#define beenVisited(client, rclass)  \
   ( TTMaskHasType(&((client)->m_w.visitMask), rclass) )

#define initNodeClient(node) \
{ \
	(node)->efnode_client = (ClientData) mallocMagic((unsigned) (sizeof(nodeClient))); \
	(( nodeClient *)(node)->efnode_client)->spiceNodeName = NULL; \
	TTMaskZero (&((nodeClient *) (node)->efnode_client)->m_w.visitMask); \
	TTMaskSetMask(&(((nodeClient *)(node)->efnode_client)->m_w.visitMask), &initMask);\
}


#define initNodeClientHier(node) \
{ \
	(node)->efnode_client = (ClientData) mallocMagic((unsigned)(sizeof(nodeClientHier))); \
	TTMaskZero (&((nodeClientHier *) (node)->efnode_client)->m_w.visitMask); \
}

/* attributes controlling the Area/Perimeter extraction of dev terminals */
#define ATTR_FLATAP	"*[Ee][Xx][Tt]:[Aa][Pp][Ff]*"
#define ATTR_HIERAP	"*[Ee][Xx][Tt]:[Aa][Pp][Hh]*"
#define ATTR_SUBSAP	"*[Ee][Xx][Tt]:[Aa][Pp][Ss]*"

#define	SPICE2	0
#define	SPICE3	1
#define	HSPICE	2
#define	NGSPICE	3

#define AUTO	2	/* TRUE | FALSE | AUTO for esDoSubckt */

#define NOT_PARALLEL    0
#define PARALLEL        1
#define ANTIPARALLEL    2

/*
 *---------------------------------------------------------
 * Variables used for merging parallel devs
 * The merging of devs is based on the fact that spcdevVisit
 * visits the devs in the same order all the time so the
 * value of esFMult[i] keeps the multiplier for the ith dev
 *---------------------------------------------------------
 */
#define	DEV_KILLED	((float) -1.0)
#define	FMULT_SIZE	(1<<10)
#define	DEV_KILLED	((float) -1.0)

#ifdef MAGIC_WRAPPER
#define 	atoCap(s)	((EFCapValue)atof(s))
#endif

#endif  /* _EXTTOSPICE_H */
