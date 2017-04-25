/*
 * irDebug.h --
 *
 * Declarations of debugging flag "handles" for interactive router.
 *
 * This include file is referenced inside mzInternal.h, hence hiding this
 * file from make.  This is so that debuging flags can be added without
 * forcing recompilation of everything.
 *
 * Debug flags are defined and registered with the debug module in irMain.c
 * NOTE: the values of the externs defined below are NOT THE FLAG VALUES,
 * rather they are handles for the flags - flags are tested with calls to
 * DebugIsSet().
 *
 * Flags can be examined or set by the user via the ":*iroute debug" command.
 *
 *     ********************************************************************* 
 *     * Copyright (C) 1987, 1990 Michael H. Arnold, Walter S. Scott, and  *
 *     * the Regents of the University of California.                      *
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
 * rcsid $Header: /usr/cvsroot/magic-8.0/irouter/irDebug.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 *
 */

extern int irDebEndPts; /* traces endpoint processing */
extern int irDebNoClean; /* doesn't cleanup after route, so data strucs
			  * can be examined.
			  */
