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

#ifndef _MAGIC__TEXTIO__TEXTIO_H
#define _MAGIC__TEXTIO__TEXTIO_H

#include "utils/magic.h"
#include "utils/dqueue.h" /* DQueue */

#ifdef MAGIC_WRAPPER
extern char *TxBuffer;
extern unsigned char TxInputRedirect;

#define TX_INPUT_NORMAL 	0	/* keys translated as macros	   */
#define TX_INPUT_REDIRECTED	1	/* keys redirected to terminal	   */
#define TX_INPUT_PROCESSING	2	/* executing a command via redirection */
#define TX_INPUT_PENDING_RESET	3	/* about to switch back to state 0 */

#endif

#define TX_LOG_UPDATE		1	/* Update display after every log command */
#define TX_LOG_SUSPEND		2	/* Suspend output logging */

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
extern bool TxPrintOn(void);  	/* enables TxPrintf output */
extern bool TxPrintOff(void);	/* disables TxPrintf output */
extern void TxFlush(void);
extern void TxFlushOut(void);
extern void TxFlushErr(void);
extern void TxUseMore(void);
extern void TxStopMore(void);

/* printing procedures with variable arguments lists */
extern void TxError(const char *, ...) ATTR_FORMAT_PRINTF_1;
extern void TxErrorV(const char *, va_list args);
extern void TxPrintf(const char *, ...) ATTR_FORMAT_PRINTF_1;
extern char *TxPrintString(const char *, ...) ATTR_FORMAT_PRINTF_1;

/* input procedures */
extern char *TxGetLinePrompt(char *dest, int maxChars, const char *prompt);
extern char *TxGetLine(char *line, int len);
extern int TxGetChar(void);
extern int TxDialog(const char *prompt, const char * const *responses, int defresp);

/* prompting procedures */
extern void TxSetPrompt(char ch);
extern void TxPrompt(void);
extern void TxUnPrompt(void);
extern void TxRestorePrompt(void);
extern void TxReprint(void);

/* terminal-state procedures */
extern void TxSetTerminal(void);
extern void TxResetTerminal(bool force);
extern char TxEOFChar;			/* The current EOF character */
extern char TxInterruptChar;		/* The current interrupt character */

/* command procedures */
extern void TxDispatch(FILE *f);

/* C99 compat */
extern void TxMore(const char *mesg);
extern void txGetFileCommand(FILE *f, DQueue *queue);

/* variables that tell if stdin and stdout are to a terminal */
extern bool TxStdinIsatty;
extern bool TxStdoutIsatty;
#define TxInteractive	(TxStdinIsatty && TxStdoutIsatty)

/* Misc procs */
extern void TxInit(void);
#ifdef USE_READLINE
extern void TxInitReadline(void);
#endif

#endif /* _MAGIC__TEXTIO__TEXTIO_H */
