/*
 * txOutput.c --
 *
 * 	Handles 'stdout' and 'stderr' output.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/textio/txOutput.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#ifndef	SYSV
#include <sys/wait.h>
#endif /* SYSV */
#include <sys/stat.h>
#include <unistd.h>

#include "utils/magsgtty.h"
#include "utils/magic.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "textio/txcommands.h"
#include "textio/textioInt.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "utils/paths.h"
#include "utils/utils.h"
#include "utils/malloc.h"

/* When a pipe has been opened to "more", the following variables
 * keep track of the file and process.  The "TxMoreFile" variable is
 * public so that routines like vfprintf() can check it to see if it
 * is NULL or not.  It is guaranteed to be NULL if we don't want to send
 * stuff through more.
 */

FILE * TxMoreFile = NULL;
static int txMorePid;
static bool txPrintFlag = TRUE;


/*
 * ----------------------------------------------------------------------------
 * txFprintfBasic:
 *
 *	Textio's own version of printf.  Not to be used outside of this module.
 *
 * Tricks:
 *	Called with a variable number of arguments -- may not be portable.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	text appears on stdout on the text terminal
 *
 * Note: 
 *	Many thanks to Paul Chow at Stanford for getting this to run on
 *	a Pyramid machine.
 * ----------------------------------------------------------------------------
 */ 

void
txFprintfBasic(FILE *f, ...)
{
    va_list args;
    char *fmt;

    va_start(args, f);
    fmt = va_arg(args, char *);
    Vfprintf(f, fmt, args);
    va_end(args);
}


/*
 * ----------------------------------------------------------------------------
 * TxPrintf:
 *
 *	Magic's own version of printf
 *
 * Tricks:
 *	Called with a variable number of arguments -- may not be portable.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	text appears on stdout on the text terminal
 *
 * Note: 
 *	Many thanks to Paul Chow at Stanford for getting this to run on
 *	a Pyramid machine.
 * ----------------------------------------------------------------------------
 */ 

void
TxPrintf(char *fmt, ...)
{
    va_list args;
    FILE *f;

    if (txPrintFlag)
    {
	if (TxMoreFile != NULL) 
	{
	    f = TxMoreFile;
	}
	else
	{
	    f = stdout;
	}

	if (txHavePrompt)
	{
	    TxUnPrompt();
	    va_start(args, fmt);
	    Vfprintf(f, fmt, args);
	    va_end(args);
	    TxPrompt();
	}
	else 
	{
	    va_start(args, fmt);
	    Vfprintf(f, fmt, args);
	    va_end(args);
	}

	return;
    }
}

/*
 * ----------------------------------------------------------------------------
 * TxPrintString --
 *
 *	A version of printf which writes output into a string.
 *
 * Results:
 *	A string which is valid until the next call to TxPrintString().
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */ 

char *
TxPrintString(char *fmt, ...)
{
    va_list args;
    static char *outstr = NULL;
    static int outlen;
    int nchars;

    if (outstr == NULL)
    {
	outlen = 100;
	outstr = (char *) mallocMagic((unsigned) (outlen + 1));
    }

    va_start(args, fmt);
    nchars = vsnprintf(outstr, outlen, fmt, args);
    va_end(args);

    if (nchars >= outlen)
    {
	outlen = nchars + 1;
	freeMagic(outstr);
	outstr = (char *) mallocMagic((unsigned) (outlen + 1));
	va_start(args, fmt);
	vsnprintf(outstr, outlen, fmt, args);
	va_end(args);
    }
    if (nchars == -1)
	return NULL;

    return outstr;
}


/*
 * ----------------------------------------------------------------------------
 * TxPrintOn --
 *
 *	Enables TxPrintf() output.
 *
 * Results:
 *	Previous value of flag.
 *
 * ----------------------------------------------------------------------------
 */ 

bool
TxPrintOn()
{
    bool oldValue = txPrintFlag;

    txPrintFlag = TRUE;
    
    return oldValue;
}


/*
 * ----------------------------------------------------------------------------
 * TxPrintOff --
 *
 *	Disables TxPrintf() output.
 *
 * Results:
 *	Previous value of flag.
 *
 * ----------------------------------------------------------------------------
 */ 

bool
TxPrintOff()
{
    bool oldValue = txPrintFlag;

    txPrintFlag = FALSE;

    return oldValue;
}

#ifndef MAGIC_WRAPPER


/*
 * ----------------------------------------------------------------------------
 * TxFlush --
 *
 *	Flush the standard out and error out.
 *
 * Results:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */ 

void
TxFlushErr()
{
    (void) fflush(stderr);
}

/*----------------------------------------------------------------------------*/

void
TxFlushOut()
{
    (void) fflush(stdout);
}

/*----------------------------------------------------------------------------*/

void
TxFlush()
{
    TxFlushOut();
    TxFlushErr();
}

#endif


/*
 * ----------------------------------------------------------------------------
 * TxError:
 *
 *	Magic's own version of printf, but it goes to stderr
 *
 * Tricks:
 *	Called with a variable number of arguments -- may not be portable.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	text appears on stderr on the text terminal
 *
 * Note: 
 *	Many thanks to Paul Chow at Stanford for getting this to run on
 *	a Pyramid machine.
 * ----------------------------------------------------------------------------
 */

void
TxError(char *fmt, ...)
{
    va_list args;
    FILE *f;

    TxFlushOut();
    if (TxMoreFile != NULL) 
	f = TxMoreFile;
    else
	f = stderr;
    va_start(args, fmt);
    if (txHavePrompt)
    {
	TxUnPrompt();
	Vfprintf(f, fmt, args);
	TxPrompt();
    }
    else {
	Vfprintf(f, fmt, args);
    }
    va_end(args);
    TxFlushErr();
}

#ifndef MAGIC_WRAPPER


/*
 * ----------------------------------------------------------------------------
 *
 * TxUseMore --
 *
 * 	This procedure forks a "more" process and causes TxError and TxPrintf
 *	to send output through it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A file is opened.  When the caller is finished with output,
 *	it must call TxStopMore to clean up the process.
 *
 * ----------------------------------------------------------------------------
 */

void
TxUseMore()
{
    int pipeEnds[2];
    int moreRunning = TRUE;
    static int moreMsg = FALSE;
    char *pagername, *pagerpath, *useenv = NULL;
    extern char *getenv();
    struct stat buf;

    ASSERT(TxMoreFile == NULL, "TxUseMore");

    /* Determine if "more" executable exists and is world executable */
    /* before attempting a fork.  Check environment variable PAGER   */
    /* first before defaulting to the built-in value PAGERDIR	     */
    /* (see utils/paths.h).					     */

    if ((useenv = getenv("PAGER")) == NULL)
    {
	pagerpath = (char *) mallocMagic((unsigned) (strlen(PAGERDIR) + 1));
	strcpy(pagerpath, PAGERDIR);
    }
    else
	pagerpath = useenv;

    if ((stat(pagerpath, &buf) < 0) || !(buf.st_mode & S_IXOTH))
    {
	if (!moreMsg)
	{
	    TxError("Couldn't execute %s to filter output.\nTry setting "
		    "environment variable PAGER to your favorite pager\n\n",
		    pagerpath);
	    moreMsg = TRUE;
	}
	goto done;
    }

    pipe(pipeEnds);
    FORK(txMorePid);

    /* In the child process, move the pipe input to standard input,
     * delete the output stream, and then run "more".
     */

    if (txMorePid == 0)
    {
	char *argv[100];
	close(pipeEnds[1]);
	dup2(pipeEnds[0], 0);
	if ((pagername = strrchr(pagerpath, '/')) != (char *) 0) 
	    pagername++;
	else
	    pagername = pagerpath;
	execl(pagerpath, pagername, 0);

	/* Something went very wrong if it gets here. */

	_exit(-1);
    }

    /* This is the parent process.  Close the input descriptor and make
     * an official FILE for the output descriptor.
     */
    
    close(pipeEnds[0]);
    TxMoreFile = fdopen(pipeEnds[1], "w");

done:
    if (useenv == NULL) freeMagic(pagerpath);
    return;
}

/*
 * ----------------------------------------------------------------------------
 *
 * TxStopMore --
 *
 * 	Close the pipe connecting us to a "more" process and wait for
 *	the "more" process to die.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The connection to more is closed.
 *
 * ----------------------------------------------------------------------------
 */

void
TxStopMore()
{
    /* TxMoreFile may be NULL if the "more" executable was not found */
    if (TxMoreFile == NULL) return;

    /* Close the pipe. */
    ASSERT(txMorePid != 0, "TxStopMore");
    fclose(TxMoreFile);
    TxMoreFile = NULL;

    /* Wait until there are no child processes left.  This is a bit
     * of a kludge, and may screw up if other child processes are
     * created for other purposes at the same time, but I can't see
     * any way around it.
     */
  
    WaitPid (txMorePid, 0);
    txMorePid = 0;
}

#endif /* !MAGIC_WRAPPER */

#ifdef	NEED_VFPRINTF

int
vfprintf(FILR *iop, char *fmt, va_list args_in)
{
    va_list ap;
    int len;
#if defined(MIPSEB) && defined(SYSTYPE_BSD43)
    unsigned char localbuf[BUFSIZ];
#else
    char localbuf[BUFSIZ];
#endif

    va_copy(ap, args_in);
    if (iop->_flag & _IONBF) {
	iop->_flag &= ~_IONBF;
	iop->_ptr = iop->_base = localbuf;
	len = _doprnt(fmt, ap, iop);
	(void) fflush(iop);
	iop->_flag |= _IONBF;
	iop->_base = NULL;
	iop->_bufsiz = 0;
	iop->_cnt = 0;
    } else
	len = _doprnt(fmt, ap, iop);

    va_end(ap);
    return (ferror(iop) ? EOF : len);
}
#endif  /* NEED_VFPRINTF */

