/*--------------------------------------------------------------*/
/* tclroute.c							*/
/*								*/
/*	Allows the "router" feature to be loaded as a module	*/
/*	under the Tcl/Tk version of magic.  Loading is		*/
/*	automatic upon invoking one of the router commands.	*/
/*--------------------------------------------------------------*/

#ifdef ROUTE_AUTO

#include <stdio.h>
#include <string.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "utils/heap.h"
#include "utils/list.h"
#include "database/database.h"
#include "windows/windows.h"
#include "commands/commands.h"
#include "utils/tech.h"
#include "dbwind/dbwind.h"
#include "router/router.h"
#include "gcr/gcr.h"
#include "grouter/grouter.h"
#include "mzrouter/mzrouter.h"

/* External routines */

extern void CmdRoute(), CmdIRoute(), CmdGaRoute();
extern void CmdChannel(), CmdSeeFlags();
extern void CmdGARouterTest(), CmdGRouterTest();
extern void CmdIRouterTest(), CmdMZRouterTest(); 

/*
 * ----------------------------------------------------------------------------
 *
 * Tcl package initialization function
 *
 * ----------------------------------------------------------------------------
 */

int
Tclroute_Init(interp)
    Tcl_Interp *interp;
{
    SectionID invsec;

    /* Sanity checks! */
    if (interp == NULL) return TCL_ERROR;
    if (Tcl_PkgRequire(interp, "Tclmagic", MAGIC_VERSION, 0) == NULL)
	return TCL_ERROR;
    if (Tcl_InitStubs(interp, "8.1", 0) == NULL) return TCL_ERROR;

    TxPrintf("Auto-loading ROUTE module\n");
    TxFlushOut();

    /* Replace the auto-load function with the ones defined in	*/
    /* this package in the command functions list.		*/

    WindReplaceCommand(DBWclientID, "route", CmdRoute);
    WindReplaceCommand(DBWclientID, "garoute", CmdRoute);
    WindReplaceCommand(DBWclientID, "iroute", CmdIRoute);
    WindReplaceCommand(DBWclientID, "channels", CmdChannel);
    WindReplaceCommand(DBWclientID, "*garoute", CmdGARouterTest);
    WindReplaceCommand(DBWclientID, "*groute", CmdGRouterTest);
    WindReplaceCommand(DBWclientID, "*iroute", CmdIRouterTest);
    WindReplaceCommand(DBWclientID, "*mzroute", CmdMZRouterTest);
    WindReplaceCommand(DBWclientID, "*seeflags", CmdSeeFlags);

    /* Now we need to do TechAddClient and reload the tech file */

    TechAddClient("mzrouter", MZTechInit, MZTechLine, MZTechFinal,
		(SectionID) 0, (SectionID *) 0, FALSE);
    TechAddClient("router", RtrTechInit, RtrTechLine, RtrTechFinal,
		(SectionID) 0, (SectionID *) 0, FALSE);

    /* Initialization functions */
    NMinit();
    MZInit();
    IRInit();

    invsec = TechSectionGetMask("drc", NULL);
    invsec &= TechSectionGetMask("mzrouter", NULL);
    invsec &= TechSectionGetMask("router", NULL);
    if (!TechLoad(NULL, invsec)) return TCL_ERROR;

    Tcl_PkgProvide(interp, "Route", MAGIC_VERSION);
    return TCL_OK;
}

#endif /* ROUTE_AUTO */
