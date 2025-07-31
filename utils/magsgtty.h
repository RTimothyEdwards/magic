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


#if defined(HAVE_TERMIOS_H)
 /* In modern times everything has POSIX */
 #include <termios.h>

 #ifdef HAVE_SYS_IOCTL_H
  /* Linux glibx 2.x       - present
   * FreeBSD 14.3-RELEASE  - present
   * Solaris 11.4          - present
   */
  #include <sys/ioctl.h>
 #endif

#elif defined(HAVE_TERMIO_H)
 /* Linux glibx 2.x       - present (just includes termios.h & sys/ioctl.h)
  * Linux glibc 2.45+     - not present
  * FreeBSD 14.3-RELEASE  - not present
  * Solaris 11.4          - present
  */
 #include <termio.h>
#else /* sgtty */
 #if defined(HAVE_SYS_IOCTL_COMPAT_H)
  /* Linux glibc2.x        - not present
   * FreeBSD 14.3-RELEASE  - not present
   * Solaris 11.4          - not present
   */
  #include <sys/ioctl_compat.h> /* replaced sgtty.h */
 #elif defined(HAVE_SGTTY_H)
  /* Linux glibc2.x        - present (includes sys/ioctl.h)
   * FreeBSD 14.3-RELEASE  - not present
   * Solaris 11.4          - present
   */
  #include <sgtty.h> /* legacy - struct sgttyb{} defn */
 #endif
#endif


/* all of the state associated with a tty terminal */
typedef struct {
#if defined(HAVE_TERMIOS_H)
    struct termios termios;
#elif defined(HAVE_TERMIO_H)
    struct termio termio;
#else /* sgtty */
    struct sgttyb tx_i_sgtty;
    struct tchars tx_i_tchars;
#endif
} txTermState;


#endif	/* _MAGIC__UTILS__MAGSGTTY_H */
