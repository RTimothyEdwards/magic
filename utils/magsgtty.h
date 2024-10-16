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

#if !defined(SYSV) && !defined(CYGWIN)

# ifdef	ALPHA
# undef MAX
# undef MIN
# endif

# ifndef COMPAT_43TTY
# define COMPAT_43TTY
# endif

#include <sys/ioctl.h>

#if defined(__OpenBSD__) || defined(EMSCRIPTEN)
#include <termios.h>
#else
#include <sys/ioctl_compat.h>
#endif

#else
#include <termio.h>
#endif

#endif	/* _MAGIC__UTILS__MAGSGTTY_H */
