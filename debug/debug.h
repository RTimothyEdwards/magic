/*
 * debug.h --
 *
 * Defines the interface to the debugging module.
 * The debugging module provides a standard collection of
 * procedures for setting, examining, and testing debugging flags.
 * Also measurement code.
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
 * rcsid $Header: /usr/cvsroot/magic-8.0/debug/debug.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 */

#ifndef _DEBUG_H
#define _DEBUG_H

#include "utils/magic.h"

struct debugClient
{
    char		*dc_name;	/* Name of client */
    int			 dc_maxflags;	/* Maximum number of flags */
    int			 dc_nflags;	/* Number flags now in array */
    struct debugFlag	*dc_flags;	/* Array of flags */
};

struct debugFlag
{
    char	*df_name;	/* Name of debugging flag */
    bool	 df_value;	/* Current value of the flag */
};

/* A histogram counts of the number of data items in each of
 * a number of ranges.  It is defined by a low value, a bin size, and 
 * the number of bins.  Items falling in the range hi_lo..hi_lo+n*hi_step-1
 * go into hi_data[n], for n=1 to the number of bins.  Values outside the
 * range are stored in locations 0 and n+1.
 */
typedef struct histogram
{
    int                hi_lo;		/* Lowest bin value		*/
    int		       hi_step;		/* Size of a bin    		*/
    int                hi_bins;		/* Number of bins in histogram	*/
    int		       hi_max;		/* Largest item in the histogram*/
    int		       hi_min;		/* Smallest item in the histogram*/
    int		       hi_cum;		/* Cumulative item total 	*/
    char             * hi_title;	/* Histogram identifier 	*/
    bool	       hi_ptrKeys;	/* TRUE if title is a pointer   */
    int              * hi_data;		/* Buckets for histogram counts	*/
    struct histogram * hi_next;		/* Linked list to next histogram*/
} Histogram;

/* constants */
#define	MAXDEBUGCLIENTS	50	/* Maximum # of clients of debug module */

extern struct debugClient debugClients[];

#define	DebugIsSet(cid, f)	debugClients[(spointertype) cid].dc_flags[f].df_value

/* procedures */
extern void HistCreate();
extern void HistAdd();
extern void HistPrint();
extern ClientData DebugAddClient();
extern int DebugAddFlag();
extern void DebugShow(), DebugSet();

#endif /* _DEBUG_H */
