/*
 *  utils/magic_stdbool.h
 *
 *  Copyright (C) 1985, 1990 Regents of the University of California.
 *
 *  Permission to use, copy, modify, and distribute this
 *  software and its documentation for any purpose and without
 *  fee is hereby granted, provided that the above copyright
 *  notice appear in all copies.  The University of California
 *  makes no representations about the suitability of this
 *  software for any purpose.  It is provided "as is" without
 *  express or implied warranty.  Export of this software outside
 *  of the United States of America may require an export license.
 *
 *  SPDX-License-Identifier: HPND-UC-export-US
 *
 *
 *  This file contains code snippets that are part of the GNU autoconf 2.69
 *  project documentation.  This project is licensed under GPLv3 with a
 *  Section 7 clause outlined in autoconf-2.69/COPYING.EXCEPTION.
 *
 *  The documentation for the autoconf project is licensed under GNU Free
 *  Documentation License Version 1.3 or any later version published by the
 *  Free Software Foundation.  The snippets in here are taken entirely from the
 *  documentation so fall under GFDL-1.3-or-later terms.
 *  The documentation published by the FSF can be seen at
 *   https://www.gnu.org/savannah-checkouts/gnu/autoconf/manual/autoconf-2.69/autoconf.html
 *  This section seeks to provide attribution by providing references to the source
 *  a link to the full license text and the required boilerplate text directly below:
 *
 *  Copyright (C) 1992-1996, 1998-2012 Free Software Foundation, Inc.
 *
 *    Permission is granted to copy, distribute and/or modify this
 *    document under the terms of the GNU Free Documentation License,
 *    Version 1.3 or any later version published by the Free Software
 *    Foundation; with no Invariant Sections, no Front-Cover texts, and
 *    no Back-Cover Texts.  A copy of the license is included in the
 *    section entitled "GNU Free Documentation License."
 *
 *  A full copy of the GNU Free Documentation License version 1.3 is available at:
 *    https://www.gnu.org/licenses/fdl-1.3.html
 *  SPDX-License-Identifier: GFDL-1.3-or-later
 *
 *
 *  All other lines in this file are licensed under the HPND-UC-export-US license
 *  at the top of this file inline with the main project licensing.
 *
 */

#ifndef _MAGIC__UTILS__MAGIC_STDBOOL_H
#define _MAGIC__UTILS__MAGIC_STDBOOL_H

#ifdef HAVE_MAGIC_AUTOCONF_CONFIG_H
#include "magic/autoconf/config.h"
#endif

/* This section is taken from autoconf-2.69 manual
 * https://www.gnu.org/software/autoconf/manual/autoconf-2.69/html_node/Particular-Headers.html
 * for HAVE_STDBOOL_H
 */
#ifdef HAVE_STDBOOL_H
 # include <stdbool.h>

 #ifndef TRUE
  #define	TRUE	(true)
 #endif
 #ifndef FALSE
  #define	FALSE	(false)
 #endif
#else
 # ifndef HAVE__BOOL
 #  ifdef __cplusplus
 typedef bool _Bool;
 #  else
 #   define _Bool signed char
 #  endif
 # endif
 # define bool _Bool
 # define false 0
 # define true 1
 # define __bool_true_false_are_defined 1

/* This section is taken from magic project, see historical magic.h */
 /* originally magic.h used unsigned char but should be source compatible with
  *  a real bool type such as that in C23/C++
  */

 /*typedef unsigned char bool;*/
/* commented out the original magic definition to adopt the historical C
 *  standard definition provided above.
 */

 #ifndef TRUE
  #define	TRUE	((bool)1)
 #endif
 #ifndef FALSE
  #define	FALSE	((bool)0)
 #endif
#endif

#endif /* _MAGIC__UTILS__MAGIC_STDBOOL_H */
