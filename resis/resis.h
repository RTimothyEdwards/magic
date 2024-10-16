 /* Header files for resistance extraction  */

/*  Type declarations  */

/* contact points:  keeps track where contacts are and what tiles they
   refer to both before and after processing.
*/

#ifndef _MAGIC__RESIS__RESIS_H
#define _MAGIC__RESIS__RESIS_H

#define LAYERS_PER_CONTACT 4
#define TILES_PER_JUNCTION 2

typedef struct contactpoint
{
     struct contactpoint	*cp_nextcontact;/* Next contact in linked */
     						/* list. 		  */
     Point  			cp_center;     	/*Center of contact   */
     Rect			cp_rect;	/* Tile rectangle     */
     Tile			*cp_contactTile;
     						/*
						   The following two keep
						   track of the tiles where
						   the contact was before
						   preprocessing, and the
						   next contact in that tile's
						   area.
						*/

     Tile                       *cp_tile[LAYERS_PER_CONTACT];
     int			cp_currentcontact; /* keeps track of tile
      						   being processed
						*/
     TileType                   cp_type;        /* Type of contact     */
     int			cp_width;	/* Width (in x) of contact region */
     int			cp_height;	/* Height (in y) of contact region */
     struct resnode		*cp_cnode[LAYERS_PER_CONTACT];/* this contact's nodes */
     int			cp_status;	/* status of processing on
     						   this contact
						*/
} ResContactPoint;

typedef struct resistor
{
     struct resistor    *rr_nextResistor; /*  Doubly linked list pointers */
     struct resistor    *rr_lastResistor;
     struct resnode  	*rr_node[2];
     float		rr_value;	  /* Resistor's value in milliohms  */
     int		rr_status;	  /* Status bit used for processing */
     union
     {
         float		rr_area;	  /* area in resistor. Used to 	  */
     					  /* distribute capacitance	  */
         float		rr_i;		  /* Branch current in mA */
     } rr_float;
     int		rr_cl;	  	  /* resistor centerline for geometry */
     int		rr_width;	  /* resistor width for geometry  */
     TileType		rr_tt;		  /* type that composes this 	  */
     					  /* resistor.			  */
#ifdef ARIEL
     int		rr_csArea; 	  /* crosssectional area in lamba**2*/
#endif
} resResistor;

#define  rr_connection1 	rr_node[0]
#define  rr_connection2		rr_node[1]

/* Definitions for old FET-style MOSFET devices */
#define RT_GATE		0
#define RT_SOURCE	1
#define RT_DRAIN	2
#define RT_SUBS		3

#define rd_fet_gate	rd_terminals[RT_GATE]
#define rd_fet_source	rd_terminals[RT_SOURCE]
#define rd_fet_drain	rd_terminals[RT_DRAIN]
#define rd_fet_subs	rd_terminals[RT_SUBS]

typedef struct device
{
     int		rd_status;	/* status bits 			  */
     struct device     *rd_nextDev;	/* next device in linked list	  */
     					/* terminals of device		  */
     struct resnode   **rd_terminals;
     int		rd_nterms;	/* number of terminals in rt_terminals */
     int		rd_perim;	/* info about device		*/
     int		rd_area;	/* used in .ext and .sim file   */
     int		rd_length;	/* patches.			*/
     int		rd_width;
     int		rd_tiles;	/* number of tiles in device    */
     int		rd_devtype;	/* tiletype of device.		*/
     Rect		rd_inside;	/* 1x1 rectangle inside device  */
     Tile	       *rd_tile;	/* pointer to a tile in device	*/
#ifdef ARIEL
     float		rd_i;		/* Current injected from this device */
     					/* in milliamps			     */
#endif
} resDevice;

/*
  a junction is formed when two tiles that connect are next to one another.
*/

typedef struct junction
{
     struct junction    *rj_nextjunction[TILES_PER_JUNCTION];
     Tile		*rj_Tile[TILES_PER_JUNCTION];
     Point		rj_loc;
     int		rj_status;
     struct resnode	*rj_jnode;
} ResJunction;

/*
 * A port is declared for subcircuits;  its name overrides any locally-
 * generated node name.
 */

typedef struct resport
{
     struct resport *rp_nextPort;
     Rect	    rp_bbox;
     Point	    rp_loc;
     char 	    *rp_nodename;
} resPort;

/*
  ?element are 'cons' cells used to make linked lists of their referential
  structures.
*/

typedef struct reselement
{
     struct reselement  *re_nextEl;
     resResistor	*re_thisEl;
} resElement;

typedef struct relement
{
     struct relement  *rel_nextEl;
     struct relement  *rel_lastEl;
     resResistor      *rel_thisEl;
} rElement;

typedef struct jelement
{
     struct jelement    *je_nextj;
     ResJunction	*je_thisj;
} jElement;

typedef struct telement
{
     struct telement    *te_nextt;
     resDevice		*te_thist;
} tElement;

typedef struct celement
{
     struct celement    *ce_nextc;
     ResContactPoint	*ce_thisc;
} cElement;

/*
   Nodes formed from network.  These are linked both forwards and backwords
   to other nodes.  Lists of devices, resistors, junctions, and contacts
   corresponding to this node are kept.
*/
typedef struct resnode
{
     struct resnode	*rn_more;	/* doubly linked list pointers */
     struct resnode	*rn_less;
     tElement		*rn_te;     /* widgets connected to this node */
     resElement		*rn_re;
     jElement		*rn_je;
     cElement		*rn_ce;
     int		rn_noderes;	/* resistance from origin node	*/
     Point		rn_loc;		/* location of node		*/
     unsigned		rn_why; 	/* Why is there a node here?    */
     int		rn_status;	/* Status bits			*/
     union {				/* At various times, we need to */
     					/* keep track of the node area, */
					/* node capacitance, and node	*/
     					/* voltage. Since none of these */
					/* values is used concurrently	*/
					/* only one word of storage is  */
					/* needed.			*/
         float		rn_area;	/* area of resistors collapsed  */
					/* into node.			*/
         float		rn_cap;		/* capacitance of node.		*/
     } rn_float;
     char		*rn_name;	/* Pointer to hash table name	*/
					/* for this node.		*/
     ClientData		rn_client;	/* Random pointer		*/
     int		rn_id;
} resNode;

typedef struct nelement
{
     struct nelement  *ne_nextEl;
     struct nelement  *ne_lastEl;
     resNode	      *ne_thisEl;
} nElement;

/*
   Breakpoints are places on a tile which may serve as sources/sinks of
   current. When resistance is calculated for a tile. this is calculated
   between these points.
*/

typedef struct breakpoint
{
     struct breakpoint	*br_next;
     resNode		*br_this;
     Point		br_loc;
     Rect		*br_crect;
} Breakpoint;

/*
  Each tile needs to keep track of the following things associated with it.
  Since there are too many things to fit in the single ti_client field,
  this 1 to 6 adaptor is used.
*/

typedef struct tilejunk
{
     cElement		*contactList;	  /*widgets connected to this tile */
     resDevice		*deviceList;
     resPort		*portList;
     ResJunction	*junctionList;
     Breakpoint		*breakList;
     int		sourceEdge;	/* used in device tiles to keep
     					 * of which diffusion edges are
					 * a transistor's source
					 */
     int		tj_status;	/* status of tile processing  */
} tileJunk;

/* ResDevTile keeps track of the location and type of devices.
   These areas are painted into our copied def after the tree is totally
   flattened. (They can't be painted right away becasue the copy routine
   uses the new def to keep track of where it is in the design. It is also
   used when devices are preproceesed.
*/

typedef struct resdevtile
{
     struct resdevtile	*nextDev;
     Rect		area;
     TileType		type;
     ExtDevice		*devptr;
     int		perim;
     int		overlap;
} ResDevTile;

/*
    Goodies contains random stuff passed between the node extractor
    and ResCheckSimNodes. The location of a start tile and the resistive
    tolerance are passed down, while the derived network is passed back.
*/

typedef struct goodstuff
{
     TileType	rg_ttype;
     float	rg_maxres;
     float	rg_nodecap;
     float	rg_Tdi;
     int	rg_bigdevres;
     int	rg_tilecount;
     int	rg_status;
     Point	*rg_devloc;
     char	*rg_name;
} ResGlobalParams;

/* Used in RC delay calculations for Tdi filter */
/* Attaches to rn_client field of  resNode	*/

typedef struct rcdelaystuff
{
     float	rc_Cdownstream;  /* capacitance down the tree from node */
     float	rc_Tdi;		 /* Tdi for node			*/
} RCDelayStuff;


/* ResSim.c type declarations */

typedef struct rdev
{
     struct rdev	*nextDev;	/* Next device in linked list */
     struct rdev 	*realDev;	/* Single Lumped Device for   */
     					/* devices connected in parallel  */
     resDevice		*layout;	/* pointer to resDevice that	  */
     					/* corresponds to RDev		  */
     int		status;
     struct ressimnode	*gate;		/* Terminals of transistor.	  */
     struct ressimnode	*source;
     struct ressimnode	*drain;
     struct ressimnode	*subs;		/* Used with subcircuit type only  */
     Point		location;	/* Location of lower left point of */
     					/* device.			   */
     float		resistance;     /* "Resistance" of device.	   */
     TileType		rs_ttype;	/* tile type for device		   */
     ExtDevice		*rs_devptr;	/* device extraction record	   */
     char               *rs_gattr;      /* Gate attributes, if any         */
     char               *rs_sattr;
     char               *rs_dattr;
} RDev;

typedef struct ressimnode
{
     struct ressimnode	*nextnode;	/* next node in OriginalNodes 	  */
     					/* linked list.			  */
     int		status;
     struct ressimnode	*forward;     	/* If node has been merged, this  */
     					/* points to the merged node.     */
     float		capacitance;	/* capacitance between node and   */
     					/* GND for power connections      */
					/* and all capacitance for every  */
					/* thing else.			  */
     float		cap_vdd;	/* capacitance to VDD (used for   */
     					/* power calculations only	  */
     float 		cap_couple;	/* coupling capacitance 	  */
     float		resistance;     /* lumped resistance 		  */
     float		minsizeres;	/* Minimum size resistor allowed  */
     Point		drivepoint;	/* optional, user specified drive */
     					/* point for network.		  */
     TileType		rs_ttype;	/* tiletype of drivepoint	  */
     Point		location;	/* location of bottom of leftmost */
					/* tile in the lowest numbered    */
					/* plane contained in the node .  */
     Rect		rs_bbox;	/* location of bottom of leftmost */
					/* tile in the lowest numbered    */
					/* plane contained in the node .  */
     TileType		type;		/* Tile type of tile at location  */
     struct devptr	*firstDev;	/* linked list of devices	  */
     					/* connected to node.		  */
     char		*name;		/* Pointer to name of node stored */
     					/* in hash table.		  */
     char		*oldname;	/* Pointer to previous name of    */
     					/* node, if it exists		  */
     tElement		*rs_sublist[2]; /* pointers to Gnd and Vdd sub	  */
     					/* strate connections,
							if they exist  	  */
} ResSimNode;

#define	RES_SUB_GND	0
#define RES_SUB_VDD	1

/* `cons' cell for linked list of devices connected to node	*/

typedef struct devptr
{
     struct devptr	*nextDev;
     struct rdev	*thisDev;
     int		terminal;	/* which terminal of device    */
					/* is connected to node.       */
} devPtr;

/* ResTime.c type declarations	*/

typedef struct resevent		/* Raw event list read in from rsim/tv */
{
     int	rv_node;	/* node number	*/
     int	rv_final;	/* final value; (0,1, or X)		*/
     int	rv_tmin;   	/* minimum event time in units of 100ps */
     int	rv_tmax;   	/* maximum event time in units of 100ps */
     float	rv_i;		/* event current in milliamps		*/
     resDevice *rv_dev;		/* device where charge drains */
} ResEvent;

typedef struct reseventcell
{
     ResEvent			*rl_this;
     struct reseventcell	*rl_next;
} REcell;

typedef struct rescurrentevent /* processed event used to feed relaxer */
{
     struct rescurrentevent 	*ri_next;
     float			 ri_i;
     resDevice			*ri_dev;
} ResCurrentEvent;

typedef struct restimebin     /* Holds one timestep's worth of Events */
{
     struct restimebin *rb_next;
     struct restimebin *rb_last;
     int	       rb_start;
     int	       rb_end;
     ResCurrentEvent   *rb_first;
} ResTimeBin;

typedef struct resfixpoint    /* Keeps track of where voltage sources are */
{
     struct resfixpoint		*fp_next;
     Point			fp_loc;
     TileType			fp_ttype;
     int			fp_status;
     Tile			*fp_tile;
     resNode			*fp_node;
     char			fp_name[1];
} ResFixPoint;

typedef struct	clump
{
     unsigned rp_status;
     rElement  *rp_grouplist;
     nElement  *rp_nodelist;
     rElement  *rp_downlist;
     rElement  *rp_singlelist;
} ResClump;

/* the first two fields of this plug must be the the same as for
	resDevice
*/
typedef struct plug
{
     float		rpl_i;		/* current injected through
     					   this plug
					*/
     int		rpl_status;	/* status bits for this plug */
     struct plug 	*rpl_next;	/* next plug in this bin */
     Point 		rpl_loc;	/*location of plug */
     int		rpl_type;	/*type of plug */
     resNode		*rpl_node;	/* this point's node */
} ResPlug;

typedef struct capval
{
     float	cap[1][2]; /* multipliers telling what portion of capacitance is
     			   to Vdd and what part is to ground.  Right now,
			   coupling capacitance is counted twice, so
			   cap[0]+cap[1] = (c_vdd+c_gnd+2*c_couple)/
			   			(c_vdd+c_gnd+c_couple);
			*/
} ResCapVal;

/*  node flags  */
#define		RES_REACHED_NODE	0x00200000
#define		RES_NODE_XADJ		0x00400000
#define		RES_NODE_YADJ		0x00800000

/* type of node flags */
#define RES_NODE_JUNCTION 		0x00000001
#define RES_NODE_DEVICE			0x00000002
#define RES_NODE_CONTACT		0x00000004
#define RES_NODE_ORIGIN 		0x00000008

/* resistor flags */
#define 	RES_DEADEND		0x00001000
#define 	RES_DONE_ONCE		0x00002000
#define 	RES_MARKED		0x00000100
#define		RES_EW			0x00000200
#define		RES_NS			0x00000400
#define		RES_DIAGONAL		0x00000800
#define		RES_TDI_IGNORE		0x00010000
#define		RES_REACHED_RESISTOR	0x00100000
#define		RES_HEAP		0x00200000

/* device flags  */
#define		RES_DEV_SAVE		0x00000001
#define		RES_DEV_PLUG		0x00000002

/* flags for tiles 				  	*/
/* A tile which is part of a substrate region.		*/
#define RES_TILE_SUBS	0x01
/* A tile which is part of a source/drain region. 	*/
#define RES_TILE_SD	0x02
/* A tile which is actually a device			*/
#define RES_TILE_DEV 	0x04
/* Indicates whether the tile has been processed or not */
#define RES_TILE_DONE	0x08
/*a temporary marking flag 				*/
#define RES_TILE_MARK	0x10
/*another temporary marking flag			*/
#define RES_TILE_PUSHED	0x20
/* indicates that tile has unidirectional current flow */
#ifdef LAPLACE
#define RES_TILE_1D	0x40
#define RES_TILE_GDONE	0x80
#endif
/* tree walking flags */
#define	RES_LOOP_OK	1
#define	RES_NO_LOOP	1
#define RES_DO_LAST	0
#define RES_DO_FIRST	1
#define RES_NO_FLAGS	0


/* ResSim Constants  */
#define		FORWARD			0x0000010
#define		SKIP			0x0000020
#define		FORCE			0x0000040
#define		MINSIZE			0x0000080
#define		DRIVELOC		0x0000100
#define		PORTNODE		0x0000200
#define		REDUNDANT		0x0000400
#define		DONTKILL		0x0000800

/* Capacitance table constants */
#define		RES_CAP_GND		0
#define		RES_CAP_VDD		1
#define		RES_CAP_COUPLE		2

#define OHMSTOMILLIOHMS		1000
#define FEMTOTOATTO		1000
#define ATTOTOFEMTO		0.001

#define UNTOUCHED 0
#define SERIES 1
#define PARALLEL 2
#define LOOP 4
#define SINGLE 8
#define TRIANGLE 32

#define RESTRUE 1
#define PENDING 2
#define FINISHED 4

#define LEFTEDGE 1
#define RIGHTEDGE 4
#define TOPEDGE 8
#define BOTTOMEDGE 16
#define OTHERPLANE 32

#define RN_MAXTDI 0x00001000

#define 	MARKED 			0x00000100

#define		GATE 1
#define		SOURCE 2
#define		DRAIN 3
#define		SUBS 4

#define 	DRIVEONLY	0x00001000
#define 	ORIGIN 		0x00000008

/* magic's normal value of infinity is too small- */
/* 67108863 is only 67K ohms.			  */

#define RES_INFINITY	0x3FFFFFFF

#define ResCheckIntegrity

/* The following turns on and off various options */

#define		ResOpt_ExtractAll	0x00000002
#define		ResOpt_Simplify		0x00000004
#define		ResOpt_DoExtFile	0x00000008
#define		ResOpt_DoRsmFile	0x00000010
#define		ResOpt_DoLumpFile	0x00000020
#define		ResOpt_RunSilent	0x00000040
#define		ResOpt_ExplicitRtol	0x00000080
#define		ResOpt_ExplicitTditol	0x00000100
#define		ResOpt_Tdi		0x00000200
#define		ResOpt_Stat		0x00000400
#define		ResOpt_Power		0x00000800
#define		ResOpt_Signal		0x00001000
#define		ResOpt_Pname		0x00002000
#define		ResOpt_Geometry		0x00004000
#define		ResOpt_FastHenry	0x00008000
#define		ResOpt_Blackbox		0x00010000
#define		ResOpt_Dump		0x00020000
#define 	ResOpt_DoSubstrate	0x00040000
#define		ResOpt_GndPlugs		0x00200000
#define		ResOpt_VddPlugs		0x00400000
#define 	ResOpt_CMOS		0x00800000
#define 	ResOpt_Bipolar		0x01000000
#define		ResOpt_Box		0x02000000
#ifdef LAPLACE
#define		ResOpt_DoLaplace	0x04000000
#define		ResOpt_CacheLaplace	0x08000000
#define		ResOpt_Checkpoint	0x80000000
#endif

#define		ResOpt_VDisplay		0x10000000
#define		ResOpt_IDisplay		0x20000000
#define		ResOpt_PDisplay		0x40000000

/* Assorted Variables */

extern RDev			*ResRDevList;
extern REcell			*ResBigEventList;
extern int 			ResOptionsFlags;
extern char			*ResCurrentNode;
extern ResSimNode		*ResOriginalNodes;
#ifdef ARIEL
extern int 			ResMinEventTime;
extern int 			ResMaxEventTime;
typedef 	float		ResCapElement[2];
extern	ResCapElement		*ResCapTableMax;
extern  ResCapElement		*ResCapTableMin;
extern HashTable 		ResPlugTable;
#endif

extern CellUse 			*ResUse;
extern CellDef 			*ResDef;
extern TileTypeBitMask 		ResConnectWithSD[NT];
extern TileTypeBitMask		ResCopyMask[NT];
extern resResistor 		*ResResList;
extern resNode     		*ResNodeList;
extern resDevice 		*ResDevList;
extern ResContactPoint		*ResContactList;
extern resNode			*ResNodeQueue;
extern resNode			*ResOriginNode;
extern resNode			*resCurrentNode;
extern HashTable 		ResNodeTable;
extern HashTable 		ResSimDevTable;
extern ResFixPoint		*ResFixList;
extern int			ResTileCount;
extern ResSimNode		**ResNodeArray;
extern CellDef			*mainDef;
extern TileTypeBitMask		ResSDTypesBitMask;
extern TileTypeBitMask		ResSubTypesBitMask;
extern	HashTable		ResDevTable;
extern TileTypeBitMask		ResNoMergeMask[NT];
extern	ResGlobalParams		gparams;
extern int			ResPortIndex;

extern int	      		ResSimDevice();
extern int	      		ResSimCombineParallel();
extern int	      		ResSimCapacitor();
extern int	      		ResSimResistor();
extern int	      		ResSimAttribute();
extern int			ResSimMerge();
extern int			ResSimSubckt();
extern int 			dbSrConnectStartFunc();
extern int			ResEach(),ResAddPlumbing(),ResRemovePlumbing();
extern float			ResCalculateChildCapacitance();
extern ResDevTile		*DBTreeCopyConnectDCS();
extern Tile			*ResFindTile();
extern resDevice		*ResImageAddPlug();
extern resDevice		*ResGetDevice();
extern tileJunk 		*resAddField();
extern int			ResCheckPorts();
extern int			ResCheckBlackbox();
extern void			ResCheckSimNodes();
extern void			ResSortByGate();
extern void			ResFixDevName();
extern void			ResWriteLumpFile();
extern void			ResSortBreaks();
extern Plane			*extResPrepSubstrate();

/* C99 compat */
extern void ResAddToQueue();
extern bool ResCalcTileResistance();
extern void ResCleanNode();
extern void ResCleanUpEverything();
extern void ResDeleteResPointer();
extern void ResDoContacts();
extern int  ResDoSimplify();
extern void ResDoneWithNode();
extern void ResEliminateResistor();
extern bool ResExtractNet();
extern int  ResFracture();
extern void ResMergeNodes();
extern void ResNewSDDevice();
extern void ResNewSubDevice();
extern void ResPreProcessDevices();
extern void ResPrintDeviceList();
extern void ResPrintExtDev();
extern void ResPrintReference();
extern void ResPrintResistorList();
extern void ResPrintStats();
extern void ResProcessJunction();
extern int  ResReadNode();
extern int  ResReadSim();
extern void ResRemoveFromQueue();
extern int  ResSimNewNode();
extern int  ResWriteExtFile();
extern void ResPrintExtNode();
extern void ResPrintExtRes();
extern void ResPrintFHNodes();
extern void ResPrintFHRects();
extern int  ResCreateCenterlines();
extern int  ResSeriesCheck();
extern int  ResParallelCheck();
extern int  ResTriangleCheck();
extern int  gettokens();
extern int  resWalkdown();
extern int  resWalkleft();
extern int  resWalkright();
extern int  resWalkup();


/* macros */

#define InitializeNode(node,x,y,why) \
{\
	  (node)->rn_te = NULL;\
	  (node)->rn_id=0;\
	  (node)->rn_float.rn_area = 0.0;\
	  (node)->rn_name = NULL;\
	  (node)->rn_client = (ClientData)NULL;\
	  (node)->rn_noderes = RES_INFINITY;\
	  (node)->rn_je = NULL;\
	  (node)->rn_status = FALSE;\
	  (node)->rn_loc.p_x = (x);\
	  (node)->rn_loc.p_y = (y);\
	  (node)->rn_why = (why);\
	  (node)->rn_ce = (cElement *) NULL;\
	  (node)->rn_re = (resElement *) NULL;\
}

#define ResJunkInit(Junk) \
{  \
          Junk->contactList = (cElement *) NULL; \
          Junk->deviceList = (resDevice *) NULL; \
          Junk->junctionList = (ResJunction *) NULL; \
          Junk->breakList = (Breakpoint *) NULL; \
	  Junk->portList = (resPort *) NULL; \
          Junk->tj_status = FALSE; \
	  Junk->sourceEdge = 0 ; \
}

#define NEWBREAK(node,tile,px,py,crect)\
{\
	Breakpoint	*bp;\
	tileJunk *jX_ = (tileJunk *)((tile)->ti_client); \
	bp = (Breakpoint *) mallocMagic((unsigned)(sizeof(Breakpoint))); \
        bp->br_next= jX_->breakList; \
	bp->br_this = (node); \
	bp->br_loc.p_x = px; \
	bp->br_loc.p_y = py; \
        bp->br_crect = (Rect *) (crect); \
	jX_->breakList = bp; \
}

#define NEWPORT(node,tile)\
{\
	resPort		*rp;\
	tileJunk *pX_ = (tileJunk *)((tile)->ti_client); \
	rp = (resPort *) mallocMagic((unsigned)(sizeof(resPort))); \
	rp->rp_nextPort = pX_->portList; \
	rp->rp_bbox = node->rs_bbox; \
	rp->rp_loc = node->drivepoint; \
	rp->rp_nodename = node->name; \
	pX_->portList = rp; \
}

#endif /* _MAGIC__RESIS__RESIS_H */
