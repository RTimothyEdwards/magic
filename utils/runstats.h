/*
 * runstats.h --
 *
 * Flags to RunStats()
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
 * rcsid "$Header: /usr/cvsroot/magic-8.0/utils/runstats.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $"
 */

#ifndef	_RUNSTATS_H
#define	_RUNSTATS_H

#define	RS_TCUM		01	/* Cumulative user and system time */
#define	RS_TINCR	02	/* User and system time since last call */
#define	RS_MEM		04	/* Size of heap area */

extern char *RunStats();
extern char *RunStatsRealTime();

#endif	/* _RUNSTATS_H */
