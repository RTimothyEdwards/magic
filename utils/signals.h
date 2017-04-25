/*
 * signals.h --
 *
 * Routines to signals, such as handle keyboard interrupts
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
 * rcsid[]="$Header: /usr/cvsroot/magic-8.0/utils/signals.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
 */

#ifndef _MAGSIGNAL_H
#define _MAGSIGNAL_H

#include "utils/magic.h"

/* Some machines have signal handlers returning an int, while other machines
 * have it returning a void.  If you have a machine that requires ints put 
 * it in the list of machines in utils/magic.h.
 */
#ifdef	SIG_RETURNS_INT
#define	sigRetVal	int
#define sigReturn	return 0
#else
#define	sigRetVal	void
#define sigReturn	return
#endif

/* data structures */
extern bool SigInterruptPending;
extern bool SigIOReady;
extern char SigInterruptOnSigIO;
extern bool SigGotSigWinch;

/* procedures */
extern void SigInit();
extern void SigDisableInterrupts();
extern void SigEnableInterrupts();
extern void SigWatchFile();
extern void SigUnWatchFile();
extern bool SigCheckProcess();

extern void SigSetTimer();
extern void SigTimerInterrupts();
extern void SigTimerDisplay();

extern sigRetVal sigOnInterrupt();

#endif /* _MAGSIGNAL_H */
