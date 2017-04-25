/*
 * netlist.h --
 *
 * This file defines the structures that represent netlists.
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
 *
 * RCSID $Header: /usr/cvsroot/magic-8.0/utils/netlist.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 */

#ifndef _RMNETLIST_H
#define _RMNETLIST_H

#include "utils/geometry.h"
#include "utils/magic.h"
#include "database/database.h"

/* --------------------------- Netlists ------------------------------- */

/*
 * Nets and terminals.
 * The current netlist is read into these structures so
 * it can be worked with more conveniently and so its
 * components can be given unique names.
 */
typedef struct nlTermLoc
{
	/* Set prior to stem assignment */
    struct nlTermLoc	*nloc_next;	/* Next location in list */
    struct nlTerm	*nloc_term;	/* Back pointer to term */
    Rect		 nloc_rect;	/* Location of terminal itself */
    Label		*nloc_label;	/* Points to original label */

	/* Set during stem assignment */
    Point		 nloc_stem;	/* Point on channel boundary */
    int			 nloc_dir;	/* Direction from label to nloc_stem */
    struct chan		*nloc_chan;	/* Contains nloc_stem */
    struct pin		*nloc_pin;	/* Pin on nloc_chan boundary */

    struct region	*nloc_region;	/* Region containing stem */
    struct czone	*nloc_czone;	/* Range containing point */
    int			 nloc_stemcost;	/* Cost of stem	*/
    int			 nloc_flags;	/* Flags */
} NLTermLoc;

#define	NLOC_TERM	001		/* Location used in steiner in net	*/

    /*
     * Each NLTerm corresponds to a name, but that name may
     * have several locations if the label appears in several
     * places inside a cell.  It points back to the net to
     * to which it belongs.  The name nterm_name points to
     * the key in the glnl_names hash table, so it shouldn't
     * be freed explicitly.
     */
typedef struct nlTerm
{
    struct nlTerm	*nterm_next;	/* Next terminal in net */
    char		*nterm_name;	/* Name of this terminal */
    struct nlTermLoc	*nterm_locs;	/* List of equivalent locations */
    struct nlNet	*nterm_net;	/* Back pointer to net */
    int			 nterm_flags;	/* See below */
} NLTerm;

    /* Flags for above */
#define	RTERM_ISDRIVER	0x0001		/* Term is a transmission line driver */

    /*
     * Each NLNet consists of several terminals; in order to be
     * of interest to the router, it must contain at least two.
     */
typedef struct nlNet
{
    struct nlNet	*nnet_next;	/* Next net in netlist */
    struct nlTerm	*nnet_terms;	/* List of terminals */
    Rect		 nnet_area;	/* Bounding box for net's terms */
    ClientData		 nnet_cdata;	/* For hire */

    /********************************************************************
    New Info for Area  Router
    *********************************************************************/
    int			nnet_cost;	/* Cost of steiner tree		*/
    int			nnet_complexity;/* Complexity of steiner tree	*/
    struct	segment	*nnet_segm;	/* List of steiner segments	*/
    struct	via	*nnet_vias;	/* List of vias			*/
    struct	nlNet	*nnet_stack;	/* Queue of nets		*/
    struct	czlist	*nnet_czlist;	/* Czone list			*/
    int			nnet_attempts;	/* Iteration count		*/
    int			nnet_state;	/* Routing stats		*/
    int			nnet_flags;	/* Flags			*/
    struct	svi	*nnet_svi;	/* Maze route/sever info	*/
} NLNet;

#define	NT_ROUTED	001		/* Net successfully routed	*/
#define	NT_FAILED	002		/* Net unsuccessfully routed	*/
#define	NT_SEVERED	004		/* Net has been severed		*/

    /*
     * A NLNetList contains a list of nets, along with the
     * table that maps from signal names to terminals.
     */
typedef struct
{
    struct nlNet	*nnl_nets;	/* List of nets */
    int			 nnl_numNets;	/* # of nets in list (redundant, since
					 * the list is NULL-terminated, but it
					 * is convenient).
					 */
    HashTable		 nnl_names;	/* Maps names to NLTerms */
} NLNetList;

/* Exports */
extern char *NLNetName();

#endif /* _RMNETLIST_H */
