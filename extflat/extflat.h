/*
 * extflat.h --
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
 * rcsid $Header: /usr/cvsroot/magic-8.0/extflat/extflat.h,v 1.2 2008/12/03 14:12:09 tim Exp $
 */

#ifndef _EXTFLAT_H
#define _EXTFLAT_H

#include "utils/magic.h"

typedef unsigned char U_char;

/*
 * Arguments to EFFlatBuild().
 */
#define	EF_FLATNODES		0x01	/* Flatten nodes */
#define	EF_FLATCAPS		0x02	/* Flatten capacitors */
#define	EF_FLATRESISTS		0x04	/* Flatten resistors */
#define	EF_FLATDISTS		0x08	/* Flatten distances */
#define	EF_NOFLATSUBCKT		0x10	/* Don't flatten standard cells */
#define	EF_NONAMEMERGE		0x20	/* Don't merge unconnected nets	*/
					/* with the same name.		*/

/* Flags to control output of node names.  Stored in EFTrimFlags */
#define	EF_TRIMGLOB		0x01	/* Delete trailing '!' from names */
#define	EF_TRIMLOCAL		0x02	/* Delete trailing '#' from names */
#define EF_CONVERTCOMMAS	0x04	/* Change ',' to ';' in names */
#define EF_CONVERTEQUAL		0x08	/* Change '=' to ':' in names */

/*
 * capacitance type now set to float
 */
typedef float EFCapValue;

/* ------------------------ Hierarchical names ------------------------ */

/*
 * One of the biggest consumers of memory space when flattening a circuit
 * are the full hierarchical names of all nodes.  Most of this space is
 * wasted since it's redundant.  Also, a lot of time is spent comparing
 * long names whose initial components are identical.
 *
 * The following structure allows hierarchical names to be represented
 * with sharing.  Names are represented as a sequence of components,
 * from the lowest level of the hierarchy pointing back toward the root.
 * Hence, comparisons are likely to detect differences between names
 * early on.  Second, many children can share the same parent, so
 * storage space should be comparable to that needed for an unflattened
 * hierarchy (with arrays flattened, however).
 */
typedef struct hiername
{
    struct hiername	*hn_parent;	/* Back-pointer toward root */
    int			 hn_hash;	/* For speed in hashing */
    char		 hn_name[4];	/* String is allocated here */
} HierName;

/*
 * Size of a HierName big enough to hold a string containing
 * n bytes (not including the NULL byte).
 */
#define	HIERNAMESIZE(n)	((n) + sizeof (HierName) - 3)

/* Indicates where the HierName was allocated: passed to EFHNFree() */
#define	HN_ALLOC	0	/* Normal name (FromStr) */
#define	HN_CONCAT	1	/* Concatenation of two HierNames */
#define HN_GLOBAL	2	/* Global name */
#define HN_FROMUSE	3	/* From a cell use */

/* ----------------------- Node attribute lists ----------------------- */

typedef struct efattr
{
    struct efattr	*efa_next;	/* Next in list */
    Rect		 efa_loc;	/* Location of attr label */
    int			 efa_type;	/* Tile type attr attached to */
    char		 efa_text[4];	/* String is allocated here */
} EFAttr;

/*
 * Size of an EFAttr big enough to hold a string containing
 * n bytes (not including the NULL byte).
 */
#define	ATTRSIZE(n)	((n) + sizeof (EFAttr) - 3)

/* ------------------- Hierarchical and flat nodes -------------------- */

/*
 * Each entry in the a nodename hash table points to a EFNodeName.
 * Several EFNodeNames may point to the same EFNode.  Such EFNodeNames
 * are linked into a NULL-terminated list by the name_next pointers.
 * The first name in this list, pointed to by the efnode_name field of
 * the EFNode they all point to, is the canonical name for this node.
 *
 * The name_hier field points to the HierName for this node, which
 * will have only a single component for EFNodes within a Def, but
 * multiple components for hierarchical node names.
 */
typedef struct efnn
{
    struct efnode	*efnn_node;	/* Corresponding node */
    struct efnn		*efnn_next;	/* Next name for this node */
    HierName		*efnn_hier;	/* HierName for this node */
    int			 efnn_port;	/* Port number for this node */
} EFNodeName;

/*
 * Both hierarchical and flat nodes use the same structure.  Hierarchical
 * nodes appear along with each cell def.  Flat nodes are pointed to by
 * the global hash table.
 *
 * Hierarchical nodes are linked in a doubly-linked list with all
 * other nodes in the same cell, and flat nodes are similarly linked
 * with all other flat nodes in the circuit.  The list is doubly
 * linked to allow nodes to be deleted easily when it is necessary
 * to merge two nodes into a single node.
 *
 * There is a third way in which a node can exist if only its name is
 * of interest, namely as an EFNodeHdr.  The first part of an EFNode
 * is an EFNodeHdr.
 */

    /* Represents perimeter and area for a resistance class */
typedef struct
{
    int		 pa_area;
    int		 pa_perim;
} PerimArea;

typedef struct efnhdr
{
    int		 efnhdr_flags;	/* See below */
    EFNodeName	*efnhdr_name;	/* Canonical name for this node, this is a ptr
				 * to the first element in a null-terminated
				 * list of all the EFNodeNames for this node.
				 */
    struct efnhdr *efnhdr_next;	/* Next node in list */
    struct efnhdr *efnhdr_prev;	/* Previous node in list */
} EFNodeHdr;

/* Node flags */
    /*
     * If set, this node was killed and neither it nor anything connected
     * to it should be output.  There should have been a new, identical
     * structure in the input that was connected to the new node.
     */
#define	EF_KILLED	0x01

    /*
     * If set, this node was allocated as a substrate terminal for a
     * dev, and so should be automatically merged with nodes of the
     * same name after all nodes have been flattened, rather than
     * complaining about it being unconnected.
     */
#define	EF_DEVTERM	0x02

    /*
     * This can be used as a general-purpose flag.  It is used by
     * the LEF module to indicate that a node is a "special" net.
     */
#define EF_SPECIAL	0x04
    /*
     * If set, this node is a subcircuit port and should be treated
     * accordingly when writing netlist output.  The port number is
     * encoded in the efNodeName structure, since there may be
     * multiple ports per node (for example, a thru route).
     */
#define EF_PORT		0x08
    /*
     * This is used when a node is a substrate node with a local
     * node name, making it an implicitly-defined port.  It differs
     * from EF_DEVTERM in that EF_DEVTERM includes global substrate
     * nodes, which are not declared ports.
     */
#define EF_SUBS_PORT	0x10
    /*
     * EF_SUBS_NODE is defined for substrate nodes defined in the
     * .ext file.
     */
#define EF_SUBS_NODE	0x20

extern int efNumResistClasses;	/* Number of resistance classes in efResists */

typedef struct efnode
{
    EFNodeHdr	 efnode_hdr;	/* See above */
#define	efnode_name	efnode_hdr.efnhdr_name
#define	efnode_next	efnode_hdr.efnhdr_next
#define	efnode_prev	efnode_hdr.efnhdr_prev
#define	efnode_flags	efnode_hdr.efnhdr_flags

    EFCapValue	 efnode_cap;	/* Total capacitance to ground for this node */
    int		 efnode_type;	/* Index into type table for node */
    Rect	 efnode_loc;	/* Location of a 1x1 rect contained in this
				 * node.  This information is provided in the
				 * .ext file so it will be easy to map between
				 * node names and locations.
				 */
    EFAttr	*efnode_attrs;	/* Node attribute list */
    ClientData	 efnode_client;	/* For hire */
    PerimArea	 efnode_pa[1];	/* Dummy; each node actually has
				 * efNumResistClasses array elements
				 * allocated to it.
				 */
} EFNode;

/* -------------------------- Devices ----------------------------- */

/*
 * Each device can contain several terminals.
 * Each terminal is described by the following structure.
 * We use a EFNode pointer for the terminal to which a device connects;
 * this assumes that devices appear after all the nodes for a cell.
 */
typedef struct devterm
{
    EFNode	*dterm_node;	/* Node to which we're connected */
    char	*dterm_attrs;	/* Attribute list */
    int		 dterm_length;	/* Length of terminal connection to gate */
    int		 dterm_perim;	/* Terminal perimeter if passed as a param */
    int		 dterm_area;	/* Terminal area if passed as a param */
} DevTerm;

/*
 * Device itself.
 * The dev_substrate and dev_type pointers are actually pointer into shared
 * tables of names, rather than being individually allocated for each
 * transistor.
 */

typedef struct parm
{
    char	 parm_type[2];
    char	*parm_name;
    double	 parm_scale;
    struct parm	*parm_next;
} DevParam;

typedef struct dev
{
    struct dev	*dev_next;	/* Next device in def */
    U_char	 dev_class;	/* Device class (see extract/extract.h) */
    U_char	 dev_type;	/* Index into device type table */
    U_char	 dev_nterm;	/* Number of terminals in device */
    EFNode	*dev_subsnode;	/* Substrate node */
    Rect	 dev_rect;	/* 1x1 rectangle inside device */

    /* Most device types use only one or two of these, but subcircuits	*/
    /* may keep all values to pass along as parameters.			*/
    float	 dev_cap;	/* Capacitance for class "cap" or subckt */
    float	 dev_res;	/* Resistance for class "res" or subckt */
    int		 dev_area;
    int		 dev_perim;
    int		 dev_length;
    int		 dev_width;
    DevParam	 *dev_params;	/* List of subcircuit parameters to output */
    DevTerm	 dev_terms[1];	/* Terminals.  The actual number will depend
				 * on dev_nterm above, so the size of this
				 * structure will vary.
				 */
} Dev;

/* Size of a Dev structure for 'n' terminals (including the "gate") */
#define	DevSize(n)	(sizeof (Dev) + ((n)-1)*sizeof (DevTerm))

/* -------------------------------------------------------------------- */

/*
 * A big number, used for thresholds for capacitance and resistance
 * when no processing is desired (NOTE:  Probably should be using
 * C99 "INFINITY" here instead).
 */
#define	INFINITE_THRESHOLD	(((unsigned int) (~0)) >> 1)
#define	INFINITE_THRESHOLD_F	((EFCapValue)(1.0E38))
#define IS_FINITE_F(a)		(((EFCapValue)(a)) != INFINITE_THRESHOLD_F)

/* Max filename length */
#define	FNSIZE		1024


extern float EFScale;		/* Scale factor to multiply all coords by */
extern char *EFTech;		/* Technology of extracted circuit */
extern char *EFStyle;           /* Extraction style of extracted circuit */
extern char *EFSearchPath;	/* Path to search for .ext files */
extern char *EFLibPath;		/* Library search path */
extern char *EFVersion;		/* Version of extractor we work with */
extern char *EFArgTech;		/* Tech file given as command line argument */
extern bool  EFCompat;		/* Subtrate backwards-compatibility mode */

    /*
     * Thresholds used by various extflat clients to filter out
     * unwanted resistors and capacitors.  Resistance is in milliohms,
     * capacitance in attofarads.
     */
extern int EFResistThreshold;
extern EFCapValue EFCapThreshold;

    /* Table of transistor types */
extern char *EFDevTypes[];
extern int EFDevNumTypes;

    /* Table of Magic layers */
extern char *EFLayerNames[];
extern int EFLayerNumNames;

    /* Output control flags */
extern int EFTrimFlags;

/* -------------------------- Exported procedures --------------------- */

extern char *EFArgs();

    /* HierName manipulation */
extern HashEntry *EFHNLook();
extern HashEntry *EFHNConcatLook();
extern HierName *EFHNConcat();
extern HierName *EFStrToHN();
extern char *EFHNToStr();
extern int EFGetPortMax();

/* ------------------------- constants used by clients -------------- */
/* This gives us a 32 or 64 dev types which should be ok */
#define	BITSPERCHAR	8
#define MAXDEVTYPES (sizeof(long)*BITSPERCHAR) 

/*
 * ANSI C definitions of arguments to EFvisit procedures
 */

/* - left for documentation purposes

typedef int (*capproc)(HierName *, HierName *, double, ClientData );

extern int EFVisitCaps(capproc, ClientData );

typedef int (*nodeproc)(EFNode *, int , double, ClientData );

extern int EFVisitNodes(nodeproc , ClientData );

extern int EFReadFile(char *);

*/

#endif /* _EXTFLAT_H */
