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

#include "textio/textio.h"
#include "textio/txcommands.h"

extern bool txHavePrompt;

typedef struct {
    fd_set tx_fdmask;		/* A mask of the file descriptors for this 
				 * device.
				 */
    void (*tx_inputProc)(); 	/* A procedure that fetches events and stores
				 * them in the input queue via TxAddEvent().
				 */
    ClientData tx_cdata;	/* Data to be passed back to caller. */
} txInputDevRec;

#define TX_PROMPT	'>'
#define TX_CMD_PROMPT	":"

/* all of the state associated with a tty terminal */
#if !defined(SYSV) && !defined(CYGWIN)
typedef struct {
    struct sgttyb tx_i_sgtty;
    struct tchars tx_i_tchars;
} txTermState;
#endif /* SYSV */

extern bool TxGetInputEvent();

/* Routines with variable argument lists */

extern void txFprintfBasic(FILE *, ...);

#endif /* _TEXTIOINT_H */
