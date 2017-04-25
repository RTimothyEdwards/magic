/*
 * EFint.h --
 *
 * Internal definitions for the procedures to flatten hierarchical
 * (.ext) circuit extraction files.
 *
 *     ********************************************************************* 
 *     * Copyright (C) 1985, 1990 Regents of the University of California. * 
 *     * Permission to use, copy, modify, and distribute this              * 
 *     * software and its documentation for any purpose and without        * 
 *     * fee is hereby granted, provided that the above copyright          * 
 *     * notice appear in all copies.  The University of California        * 
 *     * makes no representations about the suitability of this            * 
 *     * software for any purpose.  It is provided "as is" without         * 
 *     * express or implied warranty.  Export of this software outside     * 
 *     * of the United States of America may require an export license.    * 
 *     *********************************************************************
 *
 * rcsid $Header: /usr/cvsroot/magic-8.0/extflat/EFint.h,v 1.6 2010/12/16 18:59:03 tim Exp $
 */

#ifndef _EFINT_H
#define _EFINT_H

#include "utils/magic.h"

/*
 * Add in the next character value to a hash sum.
 * This function uses a 4-bit rotate to ensure that "ab" and "ba"
 * hash to different values; the 4 bits of rotation are provided
 * to allow even short strings to produce large hash values.
 * (Small hash values don't get randomized very well).
 *
 ***************************************************************
 * BE VERY CAREFUL ABOUT CHANGING THIS!  IT CAN MAKE AN ENORMOUS
 * DIFFERENCE TO THE PERFORMANCE OF THE HASHING ALGORITHM!
 ***************************************************************
 */
#define	HASHADDVAL(sum, val) \
	(((((unsigned) (sum)) >> 28) | (((unsigned) (sum)) << 4)) + (val))

/* ------------------------ Distance information ---------------------- */

/*
 * The .ext file also allows explicit distance information to be
 * represented.  This information is stored in a hash table in each
 * Def, keyed by this same structure.  Distances are between named
 * signals, rather than nodes, so we use HierNames instead of EFNodes
 * for the two points between which distance is measured.
 */
typedef struct dist
{
    int		 dist_min, dist_max;	/* Min and max distance (lambda) */
    HierName	*dist_1, *dist_2;	/* Two points */
} Distance;

/* ------------------------- Kill information ------------------------- */

/*
 * In order for resistance and distance extraction to work correctly,
 * it's sometimes necessary to override the structure of the graph
 * extracted at a lower level of the hierarchy.  The "killnode" line
 * in a .ext file specifies that a given node, and everything that
 * connects to it, have been re-extracted and are represented in
 * flat format later in the .ext file.
 *
 * The kill list bisects the .ext file.  Fets, resistors, distance
 * info, etc that appear before the first killnode in a .ext file
 * are processed by the flattener before processing the kill list
 * for a given cell, followed by processing those fets, resistors,
 * etc that appear after the first killnode line.
 *
 * To allow the distinction to be made between before-kill and after-kill
 * processing, node structures have a flag field that can have the
 * EF_AFTERKILL bit set, indicating that the node appeared after
 * the killnode lines.  This information isn't necessary for capacitors,
 * resistors, or fets, since they are implicitly killed whenever any
 * of their terminals is connected to a killed node.
 */
typedef struct kill
{
    struct kill	*kill_next;	/* Next kill in def */
    HierName	*kill_name;	/* Node to kill */
} Kill;

/* --------------------------- Connections ---------------------------- */

/*
 * Connections are used for a multitude of purposes.  They all
 * boil down to a need to represent hierarchical names with some
 * array range information present.
 *
 * Connections are used to represent that two nodes must be merged,
 * and the resultant node's resistance and capacitance adjusted
 * (conn_name2 non-NULL), or that the resistance and capacitance
 * of a single node be adjusted (conn_name2 NULL).
 *
 * They are also used to represent internodal capacitors when
 * they appear on the def_caps list of a Def, or explicit resistors
 * when appearing on the def_resistors list.  In these latter cases,
 * both conn_name1 and conn_name2 are non-NULL.
 */

    /* Max number of dimensions in an array */
#define	MAXSUBS	2

    /* Subscripts range from r_lo to r_hi inclusive; r_lo <= r_hi */
typedef struct
{
    int		 r_lo, r_hi;
} Range;

    /*
     * Each node in the connection may be accompanied by subscripts.
     * The range of values of the subscripts are stored here.
     * When cn_nsubs is 2, cn_subs[0] is x, cn_subs[1] is y.
     */
typedef struct
{
    char	*cn_name;
    int		 cn_nsubs;
    Range	 cn_subs[MAXSUBS];
} ConnName;

    /* Connection itself */
typedef struct conn
{
    ConnName	 conn_1;	/* First node in connection */
    ConnName	 conn_2;	/* Second (optional) node in connection */
    union {
	float	 conn_val_res;	/* Value of capacitance (attofarads) or */
	EFCapValue conn_val_cap;	/* resistance (milliohms). */
    } conn_value;

    struct conn	*conn_next;	/* Next connection in list */
    PerimArea	 conn_pa[1];	/* Dummy; each connection actually has
				 * efNumResistClasses array elements
				 * allocated to it.
				 */
} Connection;

    /* Abbreviations */
#define	conn_name1	conn_1.cn_name
#define	conn_name2	conn_2.cn_name
#define conn_res	conn_value.conn_val_res
#define conn_cap	conn_value.conn_val_cap

/* -------------------------- Defs and uses --------------------------- */

/* A Def exists for each .ext file */
typedef struct def
{
    char	*def_name;	/* Name of this def */
    float	 def_scale;	/* Multiply all dimensions by this */
    int		 def_flags;	/* Flags -- see below */
    HashTable	 def_nodes;	/* Map names into EFNodeNames */
    HashTable	 def_dists;	/* Map pairs of names into Distances */
    EFNode	 def_firstn;	/* Head of circular list of nodes */

	/* The following are all NULL-terminated lists */

    struct use	*def_uses;	/* Children of this def */
    Connection	*def_conns;	/* Hierarchical connections/adjustments */
    Connection	*def_caps;	/* Two-terminal capacitors */
    Connection	*def_resistors;	/* Two-terminal resistors */
    Dev		*def_devs;	/* Devices */
    Kill	*def_kills;	/* Used to modify hierarchical structure
				 * using information present only in the
				 * parent, e.g, to kill an old node and
				 * all its attached fets prior to replacing
				 * the node with several smaller ones,
				 * connected by explicit resistors.
				 */
} Def;

#define	DEF_AVAILABLE	0x01	/* This def has been read in */
#define	DEF_SUBCIRCUIT	0x02	/* This def defines subcircuit ports */
#define DEF_PROCESSED	0x04	/* This def processed in hierarchical output */
#define DEF_NODEVICES	0x08	/* This def contains no devices */
#define DEF_SUBSNODES	0x10	/* This def contains implicit substrate nodes */

/*
 * Every Def has a NULL-terminated list of uses that correspond
 * to the subcells of that Def.  If the use is an array, the ArrayInfo
 * structure describes the indices and the separation between elements
 * (for computing transforms).
 */
#ifndef _DATABASE_H
typedef struct
{
    int		 ar_xlo, ar_xhi;
    int		 ar_ylo, ar_yhi;
    int		 ar_xsep, ar_ysep;
} ArrayInfo;
#endif /* _DATABASE_H */

typedef struct use
{
    char	*use_id;	/* Use identifier (appears in hier paths) */
    Def		*use_def;	/* Sub def being used */
    // EFNodeName *use_ports;	/* Port connections, for hierarchical output */
    struct use	*use_next;	/* Next use in list (NULL-terminated) */
    Transform	 use_trans;	/* Transform up to parent coords (for fets) */
    ArrayInfo	 use_array;	/* Arraying information */
} Use;

#define	use_xlo	 use_array.ar_xlo
#define	use_xhi	 use_array.ar_xhi
#define	use_ylo	 use_array.ar_ylo
#define	use_yhi	 use_array.ar_yhi
#define	use_xsep use_array.ar_xsep
#define	use_ysep use_array.ar_ysep

#define	IsArray(u)  ((u)->use_xlo!=(u)->use_xhi || (u)->use_ylo!=(u)->use_yhi)

/* -------------------------------------------------------------------- */

/* Structure passed down during hierarchical searching */
typedef struct
{
    Use		*hc_use;	/* Use being visited */
    int		 hc_x, hc_y;	/* X and Y indices if array */
    Transform	 hc_trans;	/* Transform to flat coordinates (for fets) */
    HierName	*hc_hierName;	/* Ptr to trailing component of HierName list */
} HierContext;

/* ------------------------------ Debugging --------------------------- */

extern bool efHNStats;	/* TRUE if we keep statistics on HierNames */

/* -------------------------------------------------------------------- */

/* Structure for passing procedures and cdata to client functions */
typedef struct
{
    int	       (*ca_proc)();
    ClientData	 ca_cdata;
} CallArg;

/* -------------------------------------------------------------------- */

/*
 * Structure used as key in hash table of internodal capacitors.
 * Keys are EFNodes, since capacitors exist between a pair of electrical
 * nodes.  The keys are ordered so that the lowest-address EFNode is
 * first.
 */
typedef struct
{
    EFNode	*ck_1, *ck_2;
} EFCoupleKey;

/* -------------------------------------------------------------------- */

/* Max filename length */
#define	FNSIZE		1024

    /* Resistance is in milliohms, capacitance in attofarads */
extern bool efWarn;		/* If TRUE, warn about unusual occurrences */
extern bool efScaleChanged;	/* If TRUE, multiply all dimensions by scale
				 * factor on output; otherwise, leave them
				 * alone and output a global scale factor
				 * of efScale in the .sim file.
				 */

extern int efResists[];		/* Resistance per square for each class */
extern bool efResistChanged;	/* TRUE if some cells' resistclasses unequal */

extern HashTable efFreeHashTable;
extern HashTable efNodeHashTable;
extern HashTable efDevParamTable;
extern HashTable efHNUseHashTable;
extern HashTable efCapHashTable;
extern HashTable efDistHashTable;
extern HashTable efWatchTable;
extern bool efWatchNodes;
extern EFNode efNodeList;
extern Def *efFlatRootDef;

/* --------------------- Internally used procedures ------------------- */

    /* Def table management */
extern Def *efDefLook();
extern Def *efDefNew();
extern Def *EFRootDef();

    /* HierName manipulation */
extern HierName *efHNFromUse();
extern char *efHNToStrFunc();

    /* Functions for hashing of HierNames */
extern int efHNCompare();
extern int efHNHash(HierName *);

    /* Functions for hashing of Distances */
extern bool efHNDistCompare();
extern char *efHNDistCopy();
extern int efHNDistHash();
extern void efHNDistKill();

    /* Functions for hashing of use id HierNames */
extern bool efHNUseCompare();
extern int efHNUseHash();

extern EFCapValue CapHashGetValue();
extern void CapHashSetValue();

   /* efBuild procedures */
/* Warning: The capacitance argument to these should be always double
	    regardless of whether you set EFCapValue to float or double
	    This should be done to avoid trouble with argument promotion
	    that some ANSI C compilers introduce */

extern DevParam *efGetDeviceParams();
extern void efBuildNode();
extern void efBuildConnect();
extern void efBuildResistor();
extern void efBuildCap();
extern HierContext *EFFlatBuildOneLevel();

#endif /* _EFINT_H */
