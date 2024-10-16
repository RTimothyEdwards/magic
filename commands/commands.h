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

#ifndef _MAGIC__COMMANDS__COMMANDS_H
#define	_MAGIC__COMMANDS__COMMANDS_H

#include "windows/windows.h"
#include "database/database.h"
#include "textio/txcommands.h" /* TxCommand */

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

extern MagWindow *CmdGetEditPoint(Point *point, Rect *rect);
extern MagWindow *CmdGetRootPoint(Point *point, Rect *rect);
extern bool CmdWarnWrite(void);
extern bool CmdParseLayers(char *s, TileTypeBitMask *mask);
extern void CmdLabelProc(char *text, int font, int size, int rotate, int offx, int offy,
                         int pos, bool sticky, TileType type);
extern void CmdSetWindCaption(CellUse *newEditUse, CellDef *rootDef);
extern CellUse *CmdGetSelectedCell(Transform *pTrans);
extern bool CmdIllegalChars(char *string, char *illegal, char *msg);
extern TileType CmdFindNetProc(char *nodename, CellUse *use, Rect *rect, bool warn_not_found, bool *isvalid);
extern bool CmdCheckForPaintFunc(void);

/* C99 compat */
extern int cmdScaleCoord(MagWindow *w, char *arg, bool is_relative, bool is_x, int scale);
extern void FlatCopyAllLabels(SearchContext *scx, TileTypeBitMask *mask, int xMask, CellUse *targetUse);
extern bool cmdDumpParseArgs(char *cmdName, MagWindow *w, TxCommand *cmd, CellUse *dummy, SearchContext *scx);
extern void cmdFlushCell(CellDef *def, bool force_deref);
extern int cmdParseCoord(MagWindow *w, char *arg, bool is_relative, bool is_x);
extern void cmdSaveCell(CellDef *cellDef, char *newName, bool noninteractive, bool tryRename);
extern void CmdInit(void);

extern void CmdDoProperty(CellDef *def, TxCommand *cmd, int argstart);
extern void CmdPaintEraseButton(MagWindow *w, Point *refPoint, bool isPaint, bool isScreen);

#endif /* _MAGIC__COMMANDS__COMMANDS_H */
