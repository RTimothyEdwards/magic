/*
 * magsgtty.h --
 *
 *     	Magic's own sgtty.h file.  For porting
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
 */

#ifndef	_MAGIC__UTILS__MAGSGTTY_H
#define _MAGIC__UTILS__MAGSGTTY_H

/* maybe this can be #ifndef HAVE_TERMIO_H */
#if !defined(SYSV) && !defined(CYGWIN)

# ifdef	ALPHA
# undef MAX
# undef MIN
# endif

/* unclear what platform requires this OpenBSD/FreeBSD ? */
# ifndef COMPAT_43TTY
# define COMPAT_43TTY
# endif

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#ifdef HAVE_TERMIOS_H
#include <termios.h>
#else
/* unclear which platform(s) require <sys/ioctl_compat.h> and the structure
 *  of this file is such that it will try to include it by default, better
 *  to invert the #if and only select this on the known platforms that need
 *  it so that <termios.h> goes by default, which exists on MacOSX, Linux, etc..
 * many possible solutions to make this work by default:
 *   HAVE_SYS_IOCTL_COMPAT_H ?  HAVE_TERMIOS_H ?  !defined(linux) at top (MaxOSX is BSD type)
 */
#include <sys/ioctl_compat.h>
#endif

#else
#include <termio.h>
#endif

#endif	/* _MAGIC__UTILS__MAGSGTTY_H */
