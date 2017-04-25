/*
 * signals.c --
 *
 * Handles signals, such as stop, start, interrupt.
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

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/signals.c,v 1.2 2010/03/08 13:33:34 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>	/* for getpid() */
#include <sys/time.h>

#include "utils/main.h"

#ifndef	SIGEMT
#define	SIGEMT	7	/* EMT instruction (SIGUNUSED) */
#endif

#ifndef	SIGBUS
#define	SIGBUS	10	/* bus error (SIGUSR1) */
#endif

#ifndef	SIGSYS
#define	SIGSYS	12	/* bad argument to system call (SIGUSR2) */
#endif

#if !defined(SIGIOT) && defined(SIGABRT)
#define SIGIOT SIGABRT	/* io-trap signal redefined */
#endif

#ifdef	linux
#if SIGBUS == SIGUNUSED
#undef SIGBUS
#define SIGBUS SIGUSR1
#endif

#if SIGSYS == SIGUNUSED
#undef SIGSYS
#define SIGSYS SIGUSR2
#endif
#endif

#include <fcntl.h>

#include "utils/magic.h"
#include "utils/magsgtty.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "utils/signals.h"
#include "windows/windows.h"
#include "graphics/graphics.h"


#ifndef FASYNC
#  define	FASYNC	00100	/* kludge for SUN2s */
#endif

/* specially imported */
extern bool DBWriteBackup();

/* macs support BSD4.2 signals, so turn off the SYSV flag for this module */
#ifdef __APPLE__
#undef SYSV
#endif

void sigSetAction(int, sigRetVal (*)(int));

/* becomes true when we get an interrupt */
global bool SigInterruptPending = FALSE;

/* Becomes true when IO is possible on one of the files passed to SigWatchFile.
 * Spurious signals are sometimes generated -- use select() to make
 * sure that what you want is really there.
 */
global bool SigIOReady = FALSE;

/* If set to 1, we will set SigInterruptPending whenever we set SigIOReady. */
/* If set to -1, then SigInterruptPending is never set */
global char SigInterruptOnSigIO;

/*
 * Set to true when we recieve a SIGWINCH/SIGWINDOW signal 
 * (indicating that a window has changed size or otherwise needs attention).
 */
global bool SigGotSigWinch = FALSE;


/* 
 * Local data structures
 */
static bool sigInterruptReceived = FALSE;
static int sigNumDisables = 0;

/*---------------------------------------------------------
 * While we can conveniently run Ctrl-C interrupts from
 * the terminal at any time, we can't do this from the
 * window in Tcl/Tk because the GUI event handler is not
 * a separate process.  So we run a timer process during
 * WindUpdate() which periodically processes Tk events
 * in the window.
 *
 * This timer can be set for "secs" second intervals.  If
 * "secs" is zero, it defaults to a 1/4 second interval.
 *---------------------------------------------------------
 */

void
SigSetTimer(int secs)
{
    struct itimerval subsecond;	/* one-quarter second interval */
 
    /*
    if (GrDisplayStatus == DISPLAY_IDLE)
	fprintf(stderr, "Timer start\n");
    else
	fprintf(stderr, "Timer repeat\n");
    fflush(stderr);
    */
   
    subsecond.it_interval.tv_sec = 0;
    subsecond.it_interval.tv_usec = 0;
    subsecond.it_value.tv_sec = secs;
    subsecond.it_value.tv_usec = (secs == 0) ? 250000 : 0;

    setitimer(ITIMER_REAL, &subsecond, NULL);
}

/*---------------------------*/
/* Remove any existing timer */
/*---------------------------*/

void
SigRemoveTimer()
{
    struct itimerval zero;	/* zero time to stop the timer */
   
    /* fprintf(stderr, "Timer stop\n"); fflush(stderr); */

    zero.it_value.tv_sec = 0;
    zero.it_value.tv_usec = 0;
    zero.it_interval.tv_sec = 0;
    zero.it_interval.tv_usec = 0;

    setitimer(ITIMER_REAL, &zero, NULL);
}

sigRetVal
sigOnAlarm(int signo)
{
    if (GrDisplayStatus == DISPLAY_IN_PROGRESS)
	GrDisplayStatus = DISPLAY_BREAK_PENDING;

    sigReturn;
}

/*-----------------------------------------*/
/* Set timer to act as a display interrupt */
/*-----------------------------------------*/

void
SigTimerDisplay()
{
    sigSetAction(SIGALRM,  sigOnAlarm);
}

/*------------------------------------------*/
/* Set timer to act like a Ctrl-C interrupt */
/*------------------------------------------*/

void
SigTimerInterrupts()
{
    sigSetAction(SIGALRM,  sigOnInterrupt);
}

/*---------------------------------------------------------
 * sigOnStop:
 *	This procedure handles stop signals.
 *
 * Results:
 *	sigReturn result (see signals.h)
 *
 * Side Effects:
 *	The text display is reset, and we stop
 *---------------------------------------------------------
 */

sigRetVal
sigOnStop(int signo)
{
    /* fix things up */
    TxResetTerminal();
    GrStop();

    /* restore the default action and resend the signal */

    sigSetAction(SIGTSTP, SIG_DFL);
    kill(getpid(), 
#ifdef linux
    	SIGSTOP
#else
    	SIGTSTP
#endif
	); 
    
    /* -- we stop here -- */

    /* NOTE:  The following code really belongs in a routine that is
     * called in response to a SIGCONT signal, but it doesn't seem to
     * work that way.  Maybe there is a Unix bug with this???
     */

    GrResume();
    TxSetTerminal();
    TxReprint();

    /* catch future stops now that we have finished resuming */

    sigSetAction(SIGTSTP, sigOnStop);
    sigReturn;
}

/*
 *---------------------------------------------------------
 * SigCheckProcess ---
 *
 *	Check if a process exists by sending it a signal.
 *	Perhaps there are better ways to do this?
 *
 * Results:
 *	TRUE if the process exists, FALSE if not.
 *
 * Side effects:
 *	Whatever happens when the process gets SIGCONT;
 *	hopefully nothing.
 *---------------------------------------------------------
 */

bool
SigCheckProcess(pid)
    int pid;
{
    int result;
    result = kill((pid_t)pid, SIGCONT);

    if (result == 0) return TRUE;
    else return FALSE;
}

/*---------------------------------------------------------
 * sigEnableInterrupts:
 *	This procedure reenables our handling of interrupts.
 *
 * Results:	None.
 *
 * Side Effects:
 *	None.
 *---------------------------------------------------------
 */

void
SigEnableInterrupts()
{
    /*sigNumDisables--;*/
    if (sigNumDisables == 1)
    {
	SigInterruptPending = sigInterruptReceived;
	sigInterruptReceived = FALSE;
    }
    sigNumDisables--;
}


/*---------------------------------------------------------
 * sigDisableInterrupts:
 *	This procedure disables our handling of interrupts.
 *
 * Results:	None.
 *
 * Side Effects:
 *	None.
 *---------------------------------------------------------
 */

void
SigDisableInterrupts()
{
    sigNumDisables++;
    if (sigNumDisables == 1)
    {
	sigInterruptReceived = SigInterruptPending;
	SigInterruptPending = FALSE;
    }
    /*sigNumDisables++;*/
}


/*
 * ----------------------------------------------------------------------------
 * SigWatchFile --
 *
 *	Take interrupts on a given IO stream.  
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	SigIOReady will be set when the IO stream becomes ready.  It is
 *	the responsibility of the client to clear that flag when needed.
 * ----------------------------------------------------------------------------
 */

void
SigWatchFile(filenum, filename)
    int filenum;		/* A file descriptor number */
    char *filename;		/* Used to recognize special files that
				 * don't support a full range of fcntl
				 * calls (such as windows: /dev/winXX).
				 */
{
    int flags;
    bool iswindow;

    iswindow = (filename && (strncmp(filename, "/dev/win", 8) == 0));

    flags = fcntl(filenum, F_GETFL, 0);
    if (flags == -1)
    {
	perror("(Magic) SigWatchFile1");
	return;
    }

    if (!mainDebug)
    {
	/* turn on FASYNC */
#ifndef SYSV
#ifdef F_SETOWN
	if (!iswindow)
	{
	    if (fcntl(filenum, F_SETOWN, -getpid()) == -1)
		perror("(Magic) SigWatchFile2"); 
	}
#endif
#endif /* SYSV */
#ifdef FASYNC
	if (fcntl(filenum, F_SETFL, flags | FASYNC) == -1) 
	    perror("(Magic) SigWatchFile3");
#else
# ifdef FIOASYNC
	flags = 1;
	if (ioctl(filenum, FIOASYNC, &flags) == -1) 
	    perror("(Magic) SigWatchFile3a");
# endif
#endif
    }
    else
    {
#ifdef FASYNC
	/* turn off FASYNC */
	if (fcntl(filenum, F_SETFL, flags & (~FASYNC)) == -1) 
	    perror("(Magic) SigWatchFile4");
#else
# ifdef FIOASYNC
	flags = 0;
	if (ioctl(filenum, FIOASYNC, &flags) == -1) 
	    perror("(Magic) SigWatchFile3b");
# endif
#endif
    }
}


/*
 * ----------------------------------------------------------------------------
 * SigUnWatchFile --
 *
 *	Do not take interrupts on a given IO stream.  
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	SigIOReady will be not set when the IO stream becomes ready.  
 * ----------------------------------------------------------------------------
 */
 /*ARGSUSED*/

void
SigUnWatchFile(filenum, filename)
    int filenum;		/* A file descriptor number */
    char *filename;		/* Used to recognize special files that
				 * don't support a full range of fcntl
				 * calls (such as windows: /dev/winXX).
				 */
{
    int flags;

    flags = fcntl(filenum, F_GETFL, 0);
    if (flags == -1)
    {
	perror("(Magic) SigUnWatchFile1");
	return;
    }

#ifdef FASYNC
    /* turn off FASYNC */
    if (fcntl(filenum, F_SETFL, flags & (~FASYNC)) == -1) 
	perror("(Magic) SigUnWatchFile4");
#else
# ifdef FIOASYNC
    flags = 0;
    if (ioctl(filenum, FIOASYNC, &flags) == -1) 
	perror("(Magic) SigWatchFile3");
# endif
#endif
}


/*---------------------------------------------------------
 * sigOnInterrupt:
 *	This procedure handles interupt signals.
 *
 * Results:
 *	sigReturn result (see signals.h)
 *
 * Side Effects:
 *    A global flag is set
 *---------------------------------------------------------
 */

sigRetVal
sigOnInterrupt(int signo)
{
    if (sigNumDisables != 0)
	sigInterruptReceived = TRUE;
    else
	SigInterruptPending = TRUE;
    sigReturn;
}


/*
 * ----------------------------------------------------------------------------
 * sigOnTerm:
 *
 *	Catch the terminate (SIGTERM) signal.
 *	Force all modified cells to be written to disk (in new files,
 *	of course).
 *
 * Results:
 *	Function does not return.
 *
 * Side effects:
 *	Writes cells out to disk (by calling DBWriteBackup()).
 *	Exits.
 * ----------------------------------------------------------------------------
 */

sigRetVal
sigOnTerm(int signo)
{
    DBWriteBackup(NULL);
    exit (1);
}



/*
 * ----------------------------------------------------------------------------
 * sigOnWinch --
 *
 *	A window has changed size or otherwise needs attention.
 *
 * Results:
 *	sigReturn result (see signals.h)
 *
 * Side effects:
 *	Sets a global flag.
 * ----------------------------------------------------------------------------
 */

sigRetVal
sigOnWinch(int signo)
{
    SigGotSigWinch = TRUE;
    sigReturn;
}

/*
 * ----------------------------------------------------------------------------
 * sigIO --
 *
 *	Some IO device is ready (probably the keyboard or the mouse).
 *
 * Results:
 *	sigReturn result (see signals.h)
 *
 * Side effects:
 *	Sets a global flag.
 * ----------------------------------------------------------------------------
 */

sigRetVal
sigIO(int signo)
{
    SigIOReady = TRUE;
    if (SigInterruptOnSigIO == 1) sigOnInterrupt(0);
    sigReturn;
}

/*
 * ----------------------------------------------------------------------------
 *
 * sigCrash --
 *
 *	Something when wrong, reset the terminal and die.
 *
 * Results:
 *	Function never returns.
 *
 * Side Effects:
 *	We die.
 *
 * ----------------------------------------------------------------------------
 */

sigRetVal
sigCrash(signum)
    int signum;
{
    static int magicNumber = 1239987;
    char *msg;
    extern bool AbortFatal;

#ifndef	linux
    if (magicNumber == 1239987) {
	/* Things aren't screwed up that badly, try to reset the terminal */
	magicNumber = 0;
	switch (signum) {
	    case SIGILL: {msg = "Illegal Instruction"; break;};
	    case SIGTRAP: {msg = "Instruction Trap"; break;};
	    case SIGIOT: {msg = "IO Trap"; break;};
	    case SIGEMT: {msg = "EMT Trap"; break;};
	    case SIGFPE: {msg = "Floating Point Exception"; break;};
	    case SIGBUS: {msg = "Bus Error"; break;};
	    case SIGSEGV: {msg = "Segmentation Violation"; break;};
	    case SIGSYS: {msg = "Bad System Call"; break;};
	    default: {msg = "Unknown signal"; break;};
	};
	strcpy(AbortMessage, msg);
	AbortFatal = TRUE;
	niceabort();
	TxResetTerminal();
    }
#else
	if (magicNumber == 1239987) {
	  magicNumber = 0;
	  /* Don't use switch statement.  These values aren't mutually exclusive
	  * under Linux.
	  */
	  if (signum == SIGILL) msg = "Illegal Instruction";
	  else if (signum == SIGTRAP) msg = "Instruction Trap";
	  else if (signum == SIGIOT) msg = "IO Trap";
	  else if (signum == SIGEMT) msg = "EMT Trap";
	  else if (signum == SIGFPE) msg = "Floating Point Exception";
	  else if (signum == SIGBUS) msg = "Bus Error";
	  else if (signum == SIGSEGV) msg = "Segmentation Violation";
	  else if (signum == SIGSYS) msg = "Bad System Call";
	  else msg = "Unknown signal";
	  strcpy(AbortMessage, msg);
	  AbortFatal = TRUE;
	  niceabort();
	  TxResetTerminal();
	}
#endif

    /* Crash & burn */
    magicNumber = 0;
    exit(12);
}


/*
 * ----------------------------------------------------------------------------
 * SigInit:
 *
 *	Set up signal handling for all signals.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Signal handling is set up.
 * ----------------------------------------------------------------------------
 */

void
SigInit(batchmode)
    int batchmode;
{
    /* fprintf(stderr, "Establishing signal handlers.\n"); fflush(stderr); */

    if (batchmode)
    {
	SigInterruptOnSigIO = -1;
    }
    else
    {
	SigInterruptOnSigIO = 0;
	sigSetAction(SIGINT,  sigOnInterrupt);
	sigSetAction(SIGTERM, sigOnTerm);
    }

    /* Under Tcl, sigOnStop just causes Tcl to hang forever.  So don't	*/
    /* set any new actions.						*/

#ifndef MAGIC_WRAPPER
#ifdef SIGTSTP
    sigSetAction(SIGTSTP, sigOnStop);
#endif

#ifdef SIGWINCH
    sigSetAction(SIGWINCH, sigOnWinch);
#endif

#ifdef SIGWINDOW
    sigSetAction(SIGWINDOW, sigOnWinch);
#endif
#endif	/* MAGIC_WRAPPER */

    if (!mainDebug )
    {
	sigSetAction(SIGIO, sigIO);
#ifdef MAGIC_WRAPPER
	if (batchmode == 0)
	    SigTimerDisplay();
	else
	    sigSetAction(SIGALRM, SIG_IGN);
#else
	sigSetAction(SIGALRM, SIG_IGN);
#endif

	sigSetAction(SIGPIPE, SIG_IGN);

#ifdef SIGPOLL
	if (SIGIO != SIGPOLL)
	{
	    sigSetAction(SIGPOLL, SIG_IGN);
	}
#endif
    }

#if !defined(SYSV) && !defined(CYGWIN)
    sigsetmask(0);
#endif
}

void
sigSetAction(int signo, sigRetVal (*handler)(int))
{
#if defined(SYSV) || defined(CYGWIN) || defined(__NetBSD__)
    struct sigaction sa;

    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(signo, &sa, (struct sigaction *)NULL);
#else
    struct sigvec sv;

    sv.sv_handler = handler;
    sv.sv_mask    = 0;
    sv.sv_flags   = 0;
    sigvec(signo, &sv, (struct sigvec *)NULL);
#endif
}
