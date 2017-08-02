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
#ifdef MAGIC_WRAPPER
void CmdExtToSpice();
#endif
extern int spcmainArgs();
extern int spccapVisit(), spcdevVisit(), spcnodeVisit(), subcktVisit();
extern int spcresistVisit(), devMergeVisit(), devDistJunctVisit();
extern int spcsubVisit();
extern int subcktUndef();
extern EFNode *spcdevSubstrate();
extern char *nodeSpiceName();
extern int nodeVisitDebug();
extern void topVisit();
extern int _ext2spice_start();

extern EFNode *spcdevHierSubstrate();
extern char *nodeSpiceHierName();
extern devMerge *mkDevMerge();
extern bool extHierSDAttr();

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
extern int  esCapAccuracy;
extern char esSpiceCapFormat[FNSIZE];
extern char *spcesOutName;
extern FILE *esSpiceF;
extern float esScale;	/* negative if hspice the EFScale/100 otherwise */

extern unsigned short esFormat;
extern unsigned long initMask;

extern int esCapNum, esDevNum, esResNum, esDiodeNum;
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
   short resClassSD ;  /* the resistance class of the src/drn of the dev */
   short resClassSub ; /* the resistance class of the substrate of the dev */
   char  *defSubs ;    /* the default substrate node */
} fetInfoList;

extern fetInfoList esFetInfo[MAXDEVTYPES];

#define MAX_STR_SIZE (1<<11) /* 2K should be enough for keeping temp
                                names even of the most complicated design */

/* Node clients for figuring out areas and perimeters of sources and drains */
typedef struct { 
	long	_duml:MAXDEVTYPES; 
} _dum; /* if you get an error here you should change 
           the data structures and visitMask */

typedef union {
	long	visitMask; /* mask for normal visits */
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
  { (client)->m_w.visitMask |= (1<<rclass); }

#define clearVisited(client) \
   { (client)->m_w.visitMask = (long)0; }

#define beenVisited(client, rclass)  \
   ( (client)->m_w.visitMask & (1<<rclass))

/*
 * Used to mark the nodes which are connected to devs. initMask  is set to
 * DEV_CONNECT_MASK only when we are in visitDevs 
 */
#define	DEV_CONNECT_MASK	((unsigned long)1<<(sizeof(long)*BITSPERCHAR-1))

#define initNodeClient(node) \
{ \
	(node)->efnode_client = (ClientData) mallocMagic((unsigned) (sizeof(nodeClient))); \
	 (( nodeClient *)(node)->efnode_client)->spiceNodeName = NULL; \
	(( nodeClient *)(node)->efnode_client)->m_w.visitMask = (unsigned long)initMask; \
}


#define initNodeClientHier(node) \
{ \
	(node)->efnode_client = (ClientData) mallocMagic((unsigned)(sizeof(nodeClientHier))); \
	((nodeClientHier *) (node)->efnode_client)->m_w.visitMask = (unsigned long) 0; \
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
 * Variables & macros used for merging parallel devs       
 * The merging of devs is based on the fact that spcdevVisit 
 * visits the devs in the same order all the time so the 
 * value of esFMult[i] keeps the multiplier for the ith dev
 *---------------------------------------------------------
 */
#define	DEV_KILLED	((float) -1.0)
#define	FMULT_SIZE	(1<<10)

#define	devIsKilled(n) ( esFMult[(n)] <=(float)0.0 )

#define	DEV_KILLED	((float) -1.0)


/* macro to add a dev's multiplier to the table and grow it if necessary */
#define addDevMult(f) \
{  \
	if ( esFMult == NULL ) { \
	  esFMult = (float *) mallocMagic((unsigned) (esFMSize*sizeof(float)));  \
	} else if ( esFMIndex >= esFMSize ) {  \
	  int i;  \
	  float *op = esFMult ;  \
	  esFMult = (float *) mallocMagic((unsigned) ((esFMSize = esFMSize*2)*sizeof(float))); \
	  for ( i = 0 ; i < esFMSize/2 ; i++ ) esFMult[i] = op[i]; \
	  if (op) freeMagic(op); \
	}  \
	esFMult[esFMIndex++] = (float)(f); \
}

#define	setDevMult(i,f) { esFMult[(i)] = (float)(f); }

#define	getCurDevMult() ((esFMult && (esFMIndex > 0)) ? esFMult[esFMIndex-1] : (float)1.0)

#ifdef MAGIC_WRAPPER
#define 	atoCap(s)	((EFCapValue)atof(s))
#endif

#endif  /* _EXTTOSPICE_H */
