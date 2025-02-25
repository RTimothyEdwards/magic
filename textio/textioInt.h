/*
 * textioInt.h --
 *
 * INTERNAL definitions for the textio module
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
 * rcsid $Header: /usr/cvsroot/magic-8.0/textio/textioInt.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 */

#ifndef _TEXTIOINT_H
#define _TEXTIOINT_H

#include "utils/magic.h"
#include "textio/textio.h"
#include "textio/txcommands.h"

extern bool txHavePrompt;

typedef struct {
    fd_set tx_fdmask;		/* A mask of the file descriptors for this
				 * device.
				 */
    cb_textio_input_t tx_inputProc;
				/* A procedure that fetches events and stores
				 * them in the input queue via TxAddEvent().
				 */
    ClientData tx_cdata;	/* Data to be passed back to caller. */
} txInputDevRec;

#define TX_PROMPT	'>'
#define TX_CMD_PROMPT	":"

/* all of the state associated with a tty terminal */
#if !defined(SYSV) && !defined(CYGWIN) && !defined(__OpenBSD__) && !defined(EMSCRIPTEN)
typedef struct {
    struct sgttyb tx_i_sgtty;
    struct tchars tx_i_tchars;
} txTermState;
#endif /* SYSV */

extern bool TxGetInputEvent(bool block, bool returnOnSigWinch);

/* Routines with variable argument lists */

extern void txFprintfBasic(FILE *f, const char *fmt, ...) ATTR_FORMAT_PRINTF_2;

/* C99 compat */
extern void txCommandsInit(void);
extern int TranslateChar(int key);
extern char *TxGetLineWPrompt(char *dest, int maxChars, const char *prompt, const char *prefix);

#ifdef MAGIC_WRAPPER
/* tcltk/tclmagic.c has a function implementation prototype mimics vfprintf() mapping
 * when ifndef MAGIC_WRAPPER
 */
extern int Tcl_printf(FILE *fp, const char *fmt, va_list ap);
#endif

#endif /* _TEXTIOINT_H */
