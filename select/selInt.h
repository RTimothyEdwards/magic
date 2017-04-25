/*
 * selInt.h --
 *
 * Contains definitions that are private to the implementation of
 * the select module.  No other module should need anything in here.
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
 * rcsid="$Header: /usr/cvsroot/magic-8.0/select/selInt.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $"
 */

#ifndef _SELINT_H
#define _SELINT_H

#include "utils/magic.h"
#include "database/database.h"

/* Procedures, variables, and records that are shared between
 * files:
 */

extern int SelRedisplay();
extern void SelSetDisplay();
extern void SelUndoInit();
extern void SelRememberForUndo();
extern void SelectAndCopy2();

extern CellUse *Select2Use;
extern CellDef *Select2Def;

extern CellUse *selectLastUse;

#endif /* _SELINT_H */
