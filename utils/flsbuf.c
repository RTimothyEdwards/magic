/*    NOTE: THIS CODE ONLY WORKS FOR THE VAX IMPLEMENTATION OF 4.2 BSD    */

/* flsbuf.c --
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
 * This file is a modified version of the standard system
 * routine, the big difference being that it can deal gracefully
 * with interrupts that cause I/O to be incomplete.  The version
 * from which this file is taken is:
 * @(#)flsbuf.c	4.5 (Berkeley) 12/16/82
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/flsbuf.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */


/* NOTE: do not move the #ifdef vax up since the loader doesn't like files
 * with no symbols.
 */

#if (defined(vax) && defined(BSD4_2))

#include	<stdio.h>
#include	<sys/param.h>
#include	<sys/stat.h>

/*
 * To prevent infinite loops if every attempt to write is
 * interrupted, we only retry an interrupted write a fixed number
 * of times, NWRITEATTEMPTS
 */

#define	NWRITEATTEMPTS	10

/* NOTE: we use the stdio version of malloc here! */
char	*malloc();

int
_flsbuf(c, iop)
    unsigned c;
    FILE *iop;
{
	char *base;
	n, rn;
	ntries;
	char c1;
	int size;
	struct stat stbuf;

	if (iop->_flag & _IORW) {
		iop->_flag |= _IOWRT;
		iop->_flag &= ~_IOEOF;
	}

	if ((iop->_flag&_IOWRT)==0)
		return(EOF);
tryagain:
	if (iop->_flag&_IOLBF) {
		base = iop->_base;
		*iop->_ptr++ = c;
		if (iop->_ptr >= base+iop->_bufsiz || c == '\n') {
			rn = iop->_ptr - base;
			iop->_ptr = base;
			ntries = NWRITEATTEMPTS;
			for (n=0; (rn > 0) && (n >= 0) && (ntries-- > 0);
				    rn -= n, base += n)
				n = write(fileno(iop), base, rn);
		} else
			n = 0;
		iop->_cnt = 0;
	} else if (iop->_flag&_IONBF) {
		c1 = c;
		ntries = NWRITEATTEMPTS;
		while ((n = write(fileno(iop), &c1, 1)) == 0 && ntries-- > 0)
			;
		iop->_cnt = 0;
	} else {
		if ((base=iop->_base)==NULL) {
			if (fstat(fileno(iop), &stbuf) < 0 ||
			    stbuf.st_blksize <= NULL)
				size = BUFSIZ;
			else
				size = stbuf.st_blksize;
			if ((iop->_base=base=malloc(size)) == NULL) {
				iop->_flag |= _IONBF;
				goto tryagain;
			}
			iop->_flag |= _IOMYBUF;
			iop->_bufsiz = size;
			if (iop==stdout && isatty(fileno(stdout))) {
				iop->_flag |= _IOLBF;
				iop->_ptr = base;
				goto tryagain;
			}
			rn = n = 0;
		} else if ((rn = n = iop->_ptr - base) > 0) {
			iop->_ptr = base;
			ntries = NWRITEATTEMPTS;
			for (n=0; (rn > 0) && (n >= 0) && (ntries-- > 0);
				    rn -= n, base += n)
				n = write(fileno(iop), base, rn);
			base = iop->_ptr;
		}
		iop->_cnt = iop->_bufsiz-1;
		*base++ = c;
		iop->_ptr = base;
	}
	if (n < 0) {
		iop->_flag |= _IOERR;
		return(EOF);
	}
	return(c);
}

int
fflush(iop)
    struct _iobuf *iop;
{
	char *base;
	n, rn;
	ntries;

	if ((iop->_flag&(_IONBF|_IOWRT))==_IOWRT
	 && (base=iop->_base)!=NULL && (rn=iop->_ptr-base)>0) {
		iop->_ptr = base;
		iop->_cnt = (iop->_flag&(_IOLBF|_IONBF)) ? 0 : iop->_bufsiz;
		ntries = NWRITEATTEMPTS;
		for (n = 0; (n >= 0) && (rn > 0) && (ntries-- > 0);
			    base += n, rn -= n)
			n = write(fileno(iop), base, rn);
		if (n < 0) {
			iop->_flag |= _IOERR;
			return(EOF);
		}
	}
	return(0);
}

int
fclose(iop)
    struct _iobuf *iop;
{
	int r;

	r = EOF;
	if (iop->_flag&(_IOREAD|_IOWRT|_IORW) && (iop->_flag&_IOSTRG)==0) {
		r = fflush(iop);
		if (close(fileno(iop)) < 0)
			r = EOF;
		if (iop->_flag&_IOMYBUF)
			free(iop->_base);
	}
	iop->_cnt = 0;
	iop->_base = (char *)NULL;
	iop->_ptr = (char *)NULL;
	iop->_bufsiz = 0;
	iop->_flag = 0;
	iop->_file = 0;
	return(r);
}

/*
 * Flush buffers on exit.  Note:  this procedure should only be present
 * in 4.2 Unix.  In 4.3 it will be ifdef'ed out.
 */

#ifdef	BSD4_2
void
_cleanup()
{
	struct _iobuf *iop;
	extern struct _iobuf *_lastbuf;

	for (iop = _iob; iop < _lastbuf; iop++)
		fclose(iop);
}
#endif	/* BSD4_2 */

#else  /* vax */

/* Dummy proc just so the file will have a proc in it to make things easier
 * on the linker.
 */

void
dummyFlsbuf()
{
}

#endif /* vax */
