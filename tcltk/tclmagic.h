/*
 * tclmagic.h --
 *
 *	Header file for including Tcl/Tk stuff.  Note that this must be
 *	included prior to all the other magic include files; otherwise
 *	the definition for "ClientData" is in conflict.
 */

#ifndef _MAGIC__TCLTK__TCLMAGIC_H
#define _MAGIC__TCLTK__TCLMAGIC_H

#ifdef MAGIC_WRAPPER

#include <tcl.h>
#include <tk.h>

/* Externally-defined global variables */

extern Tcl_Interp *magicinterp;
extern Tcl_Interp *consoleinterp;

/* Forward declaration of procedures */

extern char *Tcl_escape();
extern int  TagVerify();

/* C99 compat */
extern int  Tcl_printf();
extern void MakeWindowCommand();

extern const char *Tclmagic_InitStubsVersion;

#endif	/* MAGIC_WRAPPER */
#endif /* _MAGIC__TCLTK__TCLMAGIC_H */
