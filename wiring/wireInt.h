/*
 * wireInt.h --
 *
 * Contains definitions for things that are used by more than
 * one file in the wiring module, but aren't exported.
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
 * rcsid $Header: /usr/cvsroot/magic-8.0/wiring/wireInt.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 */

#ifndef _WIREINT_H
#define _WIREINT_H

#include "utils/magic.h"
#include "database/database.h"

/* Undo-able wiring parameters: */

extern TileType WireType;
extern int WireWidth;
extern int WireLastDir;

/* Undo procedure: */

extern void WireRememberForUndo();
extern void WireUndoInit();

#endif /* _WIREINT_H */
