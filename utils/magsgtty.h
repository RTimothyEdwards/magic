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

#if defined(HAVE_TERMIOS_H) /* POSIX */
# include <termios.h>
#elif defined(HAVE_TERMIO_H) /* SYSV */
# include <termio.h>
# include <sys/ioctl.h>
#else /* Fallback for older BSD/V7 systems */
# if defined(HAVE_SGTTY_H)
#  include <sgtty.h>
# elif defined(HAVE_SYS_IOCTL_COMPAT_H)
#  include <sys/ioctl_compat.h>
# endif
# include <sys/ioctl.h>
#endif

#endif	/* _MAGIC__UTILS__MAGSGTTY_H */
