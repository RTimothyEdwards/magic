/*--------------------------------------------------------------*/
/* tcllef.c							*/
/*								*/
/*	Allows the "lef" feature to be loaded as a module	*/
/*	under the Tcl/Tk version of magic.  Loading is		*/
/*	automatic upon invoking the "lef" command.		*/
/*--------------------------------------------------------------*/

#ifdef LEF_AUTO

#include <stdio.h>
#include <string.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "commands/commands.h"
#include "utils/tech.h"
#include "dbwind/dbwind.h"

/* External routines */

extern void CmdLef();

/*
 * ----------------------------------------------------------------------------
 *
 * Tcl package initialization function
 *
 * ----------------------------------------------------------------------------
 */

int
Magiclef_Init(interp)
    Tcl_Interp *interp;
{
    /* Sanity checks! */
    if (interp == NULL) return TCL_ERROR;
    if (Tcl_PkgRequire(interp, "Tclmagic", MAGIC_VERSION, 0) == NULL)
	return TCL_ERROR;
    if (Tcl_InitStubs(interp, "8.1", 0) == NULL) return TCL_ERROR;

    TxPrintf("Auto-loading LEF/DEF module\n");
    TxFlushOut();

    /* Replace the auto-load function with the ones defined in	*/
    /* this package in the command functions list.		*/

    if (WindReplaceCommand(DBWclientID, "lef", CmdLef) < 0)
	return TCL_ERROR;

    if (WindReplaceCommand(DBWclientID, "def", CmdLef) < 0)
	return TCL_ERROR;

    Tcl_PkgProvide(interp, "MagicLEF", MAGIC_VERSION);
    return TCL_OK;
}

#endif /* LEF_AUTO */
