/*
 * textio.h --
 *
 * Routines in the textio module
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
 * Needs:
 *	stdio.h
 *	magic.h
 *
 * rcsid $Header: /usr/cvsroot/magic-8.0/textio/textio.h,v 1.2 2008/02/18 01:01:35 tim Exp $
 */

#ifndef _TEXTIO_H
#define _TEXTIO_H

#include "utils/magic.h"

#ifdef MAGIC_WRAPPER
extern char *TxBuffer;
extern unsigned char TxInputRedirect;

#define TX_INPUT_NORMAL 	0	/* keys translated as macros	   */
#define TX_INPUT_REDIRECTED	1	/* keys redirected to terminal	   */
#define TX_INPUT_PROCESSING	2	/* executing a command via redirection */
#define TX_INPUT_PENDING_RESET	3	/* about to switch back to state 0 */

#endif

extern int TxCurButtons;

/* These should really be defined by the application, not hard-coded */
#define TX_LONG_CMD	':'	/* Way of invoking a long command. */
#define TX_LONG_CMD2	';'	/* Alternate way of invoking a long command. */

/*
 * Procedure to print text on stdout and stderr.
 */

#ifdef MAGIC_WRAPPER
#define Vfprintf Tcl_printf
#else
#define Vfprintf vfprintf
#endif  /* MAGIC_WRAPPER */

/* printing procedures */
extern bool TxPrintOn();  	/* enables TxPrintf output */
extern bool TxPrintOff();	/* disables TxPrintf output */
extern void TxFlush();
extern void TxFlushOut();
extern void TxFlushErr();
extern void TxVisChar();
extern void TxUseMore();
extern void TxStopMore();

/* printing procedures with variable arguments lists */
extern void TxError(char *, ...);
extern void TxPrintf(char *, ...);
extern char *TxPrintString(char *, ...);

/* input procedures */
extern char *TxGetLinePrompt();
extern char *TxGetLine();
extern int TxGetChar();
extern int TxDialog();

/* prompting procedures */
extern void TxSetPrompt();
extern void TxPrompt();
extern void TxPromptOnNewLine();
extern void TxUnPrompt();
extern void TxRestorePrompt();
extern void TxReprint();

/* terminal-state procedures */
extern void TxSetTerminal();
extern void TxResetTerminal();
extern char TxEOFChar;			/* The current EOF character */
extern char TxInterruptChar;		/* The current interrupt character */

/* command procedures */
extern void TxDispatch();

/* variables that tell if stdin and stdout are to a terminal */
extern bool TxStdinIsatty;
extern bool TxStdoutIsatty;
#define TxInteractive	(TxStdinIsatty && TxStdoutIsatty)

/* Misc procs */
void TxInit();
#ifdef USE_READLINE
void TxInitReadline();
#endif

#define   TX_MAX_OPEN_FILES       20

#endif /* _TEXTIO_H */
