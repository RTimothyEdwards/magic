/*
 * plow.h --
 *
 * Exported definitions for the plow module.
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
 * rcsid $Header: /usr/cvsroot/magic-8.0/plow/plow.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 */

#ifndef _MAGIC__PLOW__PLOW_H
#define _MAGIC__PLOW__PLOW_H

#include "utils/magic.h"

/* Technology file clients */
extern int PlowTechInit(), PlowTechFinal();
extern bool PlowTechLine();
extern int PlowDRCInit(), PlowDRCFinal();
extern bool PlowDRCLine();

/* Called by CmdPlow() */
extern bool Plow(CellDef *def, Rect *userRect, const TileTypeBitMask *layersp, int direction);

/* Debugging command procedure */
extern int PlowTest();

extern void PlowExtendJogHorizon();

/* Exported tile type masks */
extern TileTypeBitMask PlowFixedTypes;		/* Non-stretchable types */
extern TileTypeBitMask PlowContactTypes;	/* Contact types */
extern TileTypeBitMask PlowCoveredTypes; 	/* Types that cannot be
						 * uncovered by plowing.
						 */
extern TileTypeBitMask PlowDragTypes;		/* Types that drag along
						 * trailing min-width
						 * material when they move.
						 */

/* Jog horizon ("plow horizon" command) */
extern int PlowJogHorizon;

/* TRUE if we should eliminate jogs after each plow operation */
extern bool PlowDoStraighten;

/* C99 compat */
extern void DRCPlowScale();
extern void PlowInit();
extern void PlowAfterTech();

#endif /* _MAGIC__PLOW__PLOW_H */
