/*
 * commands.h --
 *
 * Definitions for the commands module.
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
 * Needs to include: tiles.h, database.h
 *
 * rcsid $Header: /usr/cvsroot/magic-8.0/commands/commands.h,v 1.3 2009/01/19 15:43:03 tim Exp $
 */

#ifndef _COMMANDS_H
#define	_COMMANDS_H

#include "windows/windows.h"
#include "database/database.h"

/*
 * Name of default yank buffer
 */

#define YANKBUFFERNAME	"y"

/*
 * Manipulation of user-supplied "layer" masks.
 * These may include both layers specifiable in a TileTypeMask,
 * and pseudo-layers such as "subcells" and "labels".
 *
 * These are treated just like other TileTypes, except they
 * reside in the uppermost TT_RESERVEDTYPES tile type numbers.
 */

#define	L_CELL	(TT_MAXTYPES-1)	/* Subcell layer */
#define	L_LABEL	(TT_MAXTYPES-2)	/* Label "layer" */

extern TileTypeBitMask CmdYMCell;
extern TileTypeBitMask CmdYMLabel;
extern TileTypeBitMask CmdYMAllButSpace;

/* --------------------- Global procedure headers --------------------- */

extern MagWindow *CmdGetRootBox();
extern MagWindow *CmdGetEditPoint();
extern MagWindow *CmdGetRootPoint();
extern bool CmdWarnWrite();
extern bool CmdParseLayers();
extern void CmdAddSlop();
extern void CmdLabelProc();
extern void CmdSetWindCaption();
extern CellUse *CmdGetSelectedCell();
extern bool CmdIllegalChars();
extern void CmdDoMacro();
extern TileType CmdFindNetProc();
extern bool CmdCheckForPaintFunc();

#endif /* _COMMANDS_H */
