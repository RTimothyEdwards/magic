/*
 * main.h --
 *
 * Header file containing global variables for all MAGIC modules and a 
 * couple of global procedures.
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
 * rcsid="$Header: /usr/cvsroot/magic-8.0/utils/main.h,v 1.2 2009/09/10 20:32:55 tim Exp $"
 */

#ifndef _MAIN_H
#define _MAIN_H

#include "windows/windows.h"
#include "database/database.h"

/* global data structures */

extern char	*Path;			/* Search path */
extern char	*CellLibPath;		/* Library search path for cells */
extern char	*SysLibPath;		/* Library search path for tech files,
					 * etc.
					 */
extern char	*MainMouseFile;		/* The filename of the mouse */
extern char	*MainGraphicsFile;	/* The filename of the display */
extern char	*MainDisplayType;
extern char	*MainMonType;

extern FILE	*mouseStream;		/* the mouse file */
extern FILE	*graphicsStream;	/* the graphics file */

extern short	RuntimeFlags;		/* A number of flags, defined below */

/*
 * RuntimeFlags bits:
 */

#define MAIN_DEBUG		0x1	/* Produce debugging output */
#define MAIN_RECOVER		0x2	/* Recover crash files	*/
#define MAIN_SILENT		0x4	/* Output as little as possible,
					 * for batch mode
					 */
#define MAIN_MAKE_WINDOW	0x8	/* True if we are to produce a window
					 * on startup.
					 */
#define MAIN_TK_CONSOLE		0x10	/* True if the Tcl version is running
					 * via the "tkcon" console window.
					 */
#define MAIN_TK_PRINTF		0x20	/* Set to 0 to redirect output from the
					 * console back to the terminal.
					 */
/*
 * Macros which convert the RuntimeFlags bits to their original use as
 * bool types.
 */
#define mainDebug	(((RuntimeFlags & MAIN_DEBUG) > 0) ? TRUE : FALSE)
#define mainRecover	(((RuntimeFlags & MAIN_RECOVER) > 0) ? TRUE : FALSE)
#define MakeMainWindow	(((RuntimeFlags & MAIN_MAKE_WINDOW) > 0) ? TRUE : FALSE)
#define TxTkConsole	(((RuntimeFlags & MAIN_TK_CONSOLE) > 0) ? TRUE : FALSE)
#define TxTkOutput	(((RuntimeFlags & MAIN_TK_PRINTF) > 0) ? TRUE : FALSE)
#define MainSilent	(((RuntimeFlags & MAIN_SILENT) > 0) ? TRUE : FALSE)

/*
 * The following information is kept about the Edit cell:
 *
 * EditCellUse		pointer to the CellUse from which the edit
 *			cell was selected.
 * EditRootDef		pointer to root def of window in which edit cell
 *			was selected.
 * EditToRootTransform	transform from coordinates of the Def of edit cell
 *			to those of EditRootDef.
 * RootToEditTransform	transform from coordinates EditRootDef to those
 *			of the Def of the edit cell.
 */

extern CellUse	*EditCellUse;
extern CellDef	*EditRootDef;
extern Transform EditToRootTransform;
extern Transform RootToEditTransform;

/* global procedures */

extern void MainExit(int);	/* a way of exiting that cleans up after itself */
// These are not declared anywhere
// extern bool MainLoadStyles(), MainLoadCursors();  /* Used during init & reset */

#endif /* _MAIN_H */
