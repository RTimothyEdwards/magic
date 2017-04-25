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

    /* This is where we replace the home ".tclshrc" file with	*/
    /* magic's startup script.					*/

    Tcl_SetVar(interp, "tcl_rcFileName", TCL_DIR "/magic.tcl", TCL_GLOBAL_ONLY);

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
