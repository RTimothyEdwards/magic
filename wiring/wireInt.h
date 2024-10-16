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
 * rcsid $Header$
 */

#ifndef _MAGIC__WIRING__WIREINT_H
#define _MAGIC__WIRING__WIREINT_H

#include "utils/magic.h"
#include "database/database.h"

/* Undo-able wiring parameters: */

extern TileType WireType;
extern int WireWidth;
extern int WireLastDir;
extern int WireUnits;

/* Undo procedure: */

extern void WireRememberForUndo(void);
extern void WireUndoInit(void);

#endif /* _MAGIC__WIRING__WIREINT_H */
