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

#ifndef	_MAGSGTTY_H
#define _MAGSGTTY_H

#if !defined(SYSV) && !defined(CYGWIN)
# ifdef	ALPHA
# undef MAX
# undef MIN
# endif
#include <sgtty.h>
#else
#include <termio.h>
#endif

#endif	/* _MAGSGTTY_H */
