/*
 * irouter.h --
 *
 * This file defines the interface provided by the interactive router
 * module, which is the top-level module that controls interactive
 * routing.
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
 * rcsid $Header: /usr/cvsroot/magic-8.0/irouter/irouter.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 */

#ifndef _IROUTER_H
#define _IROUTER_H

#include "utils/magic.h"

/*
 * Interface procedures.
 */

extern void IRDebugInit();
extern void IRTest();
extern void IRButtonProc();
extern void IRAfterTech();

/*
 * Technology file client procedures.
 * The sections should be processed in the order
 * listed below.
 */

    /* "irouter" section */
extern void IRTechInit();
extern bool IRTechLine();

    /* "drc" section */
extern void IRDRCInit();
extern bool IRDRCLine();

#endif /* _IROUTER_H */
