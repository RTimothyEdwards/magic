/*----------------------------------------------------------------------*/
/* magicdnull.c								*/
/*									*/
/* See comments for "magicexec.c".   This has the same function as	*/
/* magicexec.c, but does not initialize the Tk package.  This avoids	*/
/* bringing up a default window when "magic -dnull -noconsole" is	*/
/* called, running more efficiently when used in batch mode.		*/
/*----------------------------------------------------------------------*/

#include <stdio.h>

#include <tcl.h>

/*----------------------------------------------------------------------*/
/* Application initiation.  This is exactly like the AppInit routine	*/
/* for "wish", minus the cruft, but with "tcl_rcFileName" set to	*/
/* "magic.tcl" instead of "~/.wishrc".					*/
/*----------------------------------------------------------------------*/

int
magic_AppInit(interp)
    Tcl_Interp *interp;
{
    if (Tcl_Init(interp) == TCL_ERROR) {
	return TCL_ERROR;
    }
    Tcl_StaticPackage(interp, "Tcl", Tcl_Init, Tcl_Init);

    /* This is where we replace the home ".tclshrc" file with	*/
    /* magic's startup script.					*/
    /* Try to find magic.tcl relative to the current executable.
       If that fails, try to load it from the path where it was located
       at build time.
     */
    const char *magic_tcl_path = NULL;
    if (Tcl_ExprString(interp, "[file join [file dirname [info nameofexecutable]] magic.tcl]") == TCL_OK) {
        magic_tcl_path = Tcl_GetStringResult(interp);
    } else {
        magic_tcl_path = TCL_DIR "/magic.tcl";
    }
    Tcl_SetVar(interp, "tcl_rcFileName", magic_tcl_path, TCL_GLOBAL_ONLY);

    /* Additional variable can be used to tell if magic is in batch mode */
    Tcl_SetVar(interp, "batch_mode", "true", TCL_GLOBAL_ONLY);

    return TCL_OK;
}

/*----------------------------------------------------------------------*/
/* The main procedure;  replacement for "tclsh".			*/
/*----------------------------------------------------------------------*/

int
main(argc, argv)
   int argc;
   char **argv;
{
    Tcl_Main(argc, argv, magic_AppInit);
    return 0;
}

/*----------------------------------------------------------------------*/
