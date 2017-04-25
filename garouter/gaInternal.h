/*
 * gaInternal.h --
 *
 * This file defines data structures and constantds and declares
 * variables INTERNAL TO THE GAROUTER.
 * but shard by two or more soruce files.
 * * 
 * Structures etc. that are exported by the irouter are defined in
 * irouter.h.
 *
 * Structures etc. that are local to a given source 
 * file are declared at the top of that source file.
 *
 * Structures, etc.,  specific to a given function are usually defined at 
 * the head of that function.
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
 * rcsid $Header: /usr/cvsroot/magic-8.0/garouter/gaInternal.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 */

#ifndef _GAINTERNAL_H
#define _GAINTERNAL_H

#include "database/database.h"

/* Sets limit on amount of searching before mzrouter gives up during
 * stem generation.  The idea is not to waste time on routes that
 * fail.  The number is the maximum number of "blooms" permit during
 * searching.  See mzrouter documentation for details.
 */
#define MAZE_TIMEOUT 100;
/*
 * Netlist structure.
 * This is similar to the netlist structures defined in the netmenu
 * package, but contains enough additional information that we
 * maintain our own here.
 *
 * Each netlist consists of a set of Net structures.  These
 * point to a list of Terms that belong to the net.
 */

    /*
     * A Term is a point from which routing proceeds.
     * It is possible for the same label to appear at several different
     * places in a cell.  We assume that these points are connected
     * internally, and so only route to one.  The one we choose for
     * routing appears on the net_terms list; all other Terms appear
     * on the term_others list.
     */
typedef struct term
{
    Rect	 term_loc;	/* Connect to anywhere in this area */
    TileType	 term_layer;	/* What type of material */
    char	*term_name;	/* Pointer to HashEntry key, or NULL */
    struct net	*term_net;	/* Back-pointer to net */
    struct term	*term_next;	/* Next term in net */
    int		 term_flags;	/* See below */
    struct term	*term_others;	/* Other valid starting points */
} Term;

    /* Flags for above */
#define	TERM_FRINGE		0x01	/* Candidate for fringe routing */
#define	TERM_FEEDTHROUGH	0x02	/* Member of feedthrough list */
#define	TERM_ROUTED		0x04	/* Routed to rest of net */

    /* Everything in a Net is electrically identical */
typedef struct net
{
    struct net	*net_next;		/* Next net in nnl_netList */
    struct term	*net_terms;		/* List of Terms in this net */
    int		 net_prio;		/* If > 0 then user-specified priority
					 * with 1 meaning highest; if 0 then
					 * no priority specified.
					 * +++ NOT CURRENTLY USED +++
					 */
} Net;

    /* A NetList specifies a set of connections to be made */
typedef struct nnl
{
    HashTable	 nnl_termHash;		/* Map from names to Terms */
    struct net	*nnl_netList;		/* List of all Nets */
} NewNetList;

/* procedure declarations */
extern bool gaMazeInit();

#endif /* _GAINTERNAL_H */
