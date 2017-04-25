/*
 * paths.h --
 *
 *     	Definitions of Unix filename paths used by Magic and related utility
 *	programs.
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
 * rcsid="$Header"
 */

#ifndef _PATHS_H
#define _PATHS_H

/*
 * Paths used by 'ext2sim' and 'magicusage'.
 *
 */

#define	EXT_PATH	"$CAD_ROOT/magic/%s $CAD_ROOT/magic/tutorial"
#define DOT_MAGIC_PATH	"$CAD_ROOT/magic/sys ~ ."

/*
 * Paths used by 'magic'.
 *
 */
#define MAGIC_CMOS_PATH	"$CAD_ROOT/magic/cmos $CAD_ROOT/magic/tutorial"
#define MAGIC_NMOS_PATH	"$CAD_ROOT/magic/nmos $CAD_ROOT/magic/tutorial"
#define MAGIC_SYS_PATH	". $CAD_ROOT/magic/sys $CAD_ROOT/magic/sys/current"
#define MAGIC_SYS_DOT	"$CAD_ROOT/magic/sys/.magicrc"
#define MAGIC_PRE_DOT	"$CAD_ROOT/magic/sys/.initrc"
#define MAGIC_LIB_PATH	"$CAD_ROOT/magic/%s $CAD_ROOT/magic/tutorial"
#define HELPER_PATH	". BIN_DIR"		/* Used by graphics drivers */

/*
 * Path to default pager
 */
#ifdef SYSV
# ifdef hpux
#  define PAGERDIR "/usr/bin/more"
# else
#  ifdef linux
#    define PAGERDIR "/bin/more"
#  else
#    define PAGERDIR "/usr/bin/pg"
#  endif
# endif
#else
# if defined(linux) || defined(__FreeBSD__)
#  define PAGERDIR "/usr/bin/more"
# elif defined(CYGWIN)
#  define PAGERDIR "./less"
# else
#  define PAGERDIR "/usr/ucb/more"
# endif
#endif

/*
 * Other common paths.
 */
#define CAD_LIB_PATH	". $CAD_ROOT/"

#endif /* _PATHS_H */
