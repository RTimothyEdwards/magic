/*
 * CmdAuto.c --
 *
 * This file contains functions which act as placeholders for command
 * functions which have been compiled as Tcl modules, using the Tcl
 * interface.
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
 */

#ifdef MAGIC_WRAPPER

#include <stdio.h>
#include <stdlib.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "windows/windows.h"
#include "textio/txcommands.h"

#ifdef EXT2SIM_AUTO
/*
 * ----------------------------------------------------------------------------
 * CmdAutoExtToSim() --
 *
 *	Tcl auto-loading module routine.
 *	This routine replaces CmdExtToSim.
 *	The exttosim routine is not loaded at runtime, but is
 *	loaded the first time the "exttosim" command is invoked.
 *
 *	The job of replacing the appropriate functionTable entries is
 *	the responsibility of the initialization function.  If something
 *	goes wrong with the auto-load, it MUST set an error condition in
 *	Tcl or this routine will infinitely recurse!
 *
 * ----------------------------------------------------------------------------
 */

void
CmdAutoExtToSim(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int result;

    /* Auto-load */
    result = Tcl_Eval(magicinterp, "load " TCL_DIR "/exttosim" SHDLIB_EXT);

    /* Call function which was originally intended */

    if (result == TCL_OK)
	WindSendCommand(w, cmd, FALSE);
}
#endif /* EXT2SIM_AUTO */

#ifdef EXT2SPICE_AUTO
/*
 * ----------------------------------------------------------------------------
 * CmdAutoExtToSpice() --
 *
 *	Tcl auto-loading module routine.
 *	This routine replaces CmdExtToSpice.
 *	The exttospice routine is not loaded at runtime, but is
 *	loaded the first time the "exttospice" command is invoked.
 *
 *	The job of replacing the appropriate functionTable entries is
 *	the responsibility of the initialization function.  If something
 *	goes wrong with the auto-load, it MUST set an error condition in
 *	Tcl or this routine will infinitely recurse!
 *
 * ----------------------------------------------------------------------------
 */

void
CmdAutoExtToSpice(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int result;

    /* Auto-load */
    result = Tcl_Eval(magicinterp, "load " TCL_DIR "/exttospice" SHDLIB_EXT);

    /* Call function which was originally intended */

    if (result == TCL_OK)
	WindSendCommand(w, cmd, FALSE);
}
#endif /* EXT2SPICE_AUTO */

#ifdef ROUTE_AUTO
/*
 * ----------------------------------------------------------------------------
 * CmdAutoRoute() --
 *
 *	Tcl auto-loading module routine.  If "route" is configured as a
 *	Tcl module, then this routine replaces CmdRoute and the other
 *	command routines associated with the routing functions.  Router
 *	routines are not loaded at runtime, but are loaded the first time
 *	the "route" ("iroute", "garoute", etc.) command is invoked.
 *
 *	The job of replacing the appropriate functionTable entries is
 *	the responsibility of the initialization function.  If something
 *	goes wrong with the auto-load, it MUST set an error condition in
 *	Tcl or this routine will infinitely recurse!
 *
 * ----------------------------------------------------------------------------
 */

void
CmdAutoRoute(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int result;

    /* Auto-load */
    result = Tcl_Eval(magicinterp, "load " TCL_DIR "/tclroute" SHDLIB_EXT);

    /* Call function which was originally intended */

    if (result == TCL_OK)
	WindSendCommand(w, cmd, FALSE);
}
#endif  /* ROUTE_AUTO */

#ifdef PLOT_AUTO
/*
 * ----------------------------------------------------------------------------
 * CmdAutoPlot() --
 *
 *	Tcl auto-loading module routine.  If "plot" is configured as a
 *	Tcl module, then this routine replaces CmdPlot.  Plot routines
 *	are not loaded at runtime, but are loaded the first time the
 *	"plot" command is invoked.
 *
 *	The job of replacing the appropriate functionTable entries is
 *	the responsibility of the initialization function.  If something
 *	goes wrong with the auto-load, it MUST set an error condition in
 *	Tcl or this routine will infinitely recurse!
 *
 * ----------------------------------------------------------------------------
 */

void
CmdAutoPlot(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int result;

    /* Auto-load */
    result = Tcl_Eval(magicinterp, "load " TCL_DIR "/tclplot" SHDLIB_EXT);

    /* Call function which was originally intended */

    if (result == TCL_OK)
	WindSendCommand(w, cmd, FALSE);
}
#endif  /* PLOT_AUTO */

#ifdef LEF_AUTO
/*
 * ----------------------------------------------------------------------------
 * CmdAutoLef() --
 *
 *	Tcl auto-loading module routine.  If "lef" is configured as a
 *	Tcl module, then this routine replaces CmdLef.  LEF routines
 *	are not loaded at runtime, but are loaded the first time the
 *	"lef" command is invoked.
 *
 *	The job of replacing the appropriate functionTable entries is
 *	the responsibility of the initialization function.  If something
 *	goes wrong with the auto-load, it MUST set an error condition in
 *	Tcl or this routine will infinitely recurse!
 *
 * ----------------------------------------------------------------------------
 */

void
CmdAutoLef(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int result;

    /* Auto-load */
    result = Tcl_Eval(magicinterp, "load " TCL_DIR "/magiclef" SHDLIB_EXT);

    /* Call function which was originally intended */

    if (result == TCL_OK)
	WindSendCommand(w, cmd, FALSE);
}
#endif	/* LEF_AUTO */

#endif  /* MAGIC_WRAPPER */
