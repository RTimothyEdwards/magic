/*
 * gaDebug.h --
 *
 * Definitions of debugging flags for the gate-array router.
 * This is a separate include file so that new debugging flags
 * can be added to it without forcing recompilation of the
 * entire module.
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
 * rcsid $Header: /usr/cvsroot/magic-8.0/garouter/gaDebug.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 */

    /* Flags */
extern int gaDebChanOnly;
extern int gaDebChanStats;
extern int gaDebMaze;
extern int gaDebNoSimple;
extern int gaDebPaintStems;
extern int gaDebShowChans;
extern int gaDebShowMaze;
extern int gaDebStems;
extern int gaDebVerbose;
extern int gaDebNoClean;
