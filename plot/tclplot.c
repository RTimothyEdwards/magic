/*--------------------------------------------------------------*/
/* tclplot.c							*/
/*								*/
/*	Allows the "plot" feature to be loaded as a module	*/
/*	under the Tcl/Tk version of magic.  Loading is		*/
/*	automatic upon invoking the "plot" command.		*/
/*--------------------------------------------------------------*/

#ifdef PLOT_AUTO

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
#include "plot/plot.h"
#include "dbwind/dbwind.h"

/* External routines */

extern void CmdPlot();

/*
 * ----------------------------------------------------------------------------
 *
 * Tcl package initialization function
 *
 * ----------------------------------------------------------------------------
 */

int
Tclplot_Init(interp)
    Tcl_Interp *interp;
{
    int n;
    SectionID invplot;

    /* Sanity checks! */
    if (interp == NULL) return TCL_ERROR;
    if (Tcl_PkgRequire(interp, "Tclmagic", MAGIC_VERSION, 0) == NULL)
	return TCL_ERROR;
    if (Tcl_InitStubs(interp, "8.1", 0) == NULL) return TCL_ERROR;

    TxPrintf("Auto-loading PLOT module\n");
    TxFlushOut();

    /* Replace the auto-load function with the ones defined in	*/
    /* this package in the command functions list.		*/

    if (WindReplaceCommand(DBWclientID, "plot", CmdPlot) < 0)
	return TCL_ERROR;

    /* Now we need to do TechAddClient and reload the tech file */

    TechAddClient("plot", PlotTechInit, PlotTechLine, PlotTechFinal,
		(SectionID) 0, (SectionID *) 0, FALSE);

    /* No initialization function to go here */

    invplot = TechSectionGetMask("plot", NULL);
    if (!TechLoad(NULL, invplot)) return TCL_ERROR;

    Tcl_PkgProvide(interp, "Plot", MAGIC_VERSION);
    return TCL_OK;
}

#endif /* PLOT_AUTO */
