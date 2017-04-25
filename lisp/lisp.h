/*************************************************************************
 *
 *  lisp.h -- 
 *
 *   This module defines things that are exported by the
 *   mini-scheme interpreter command line language to the rest
 *   of the world.
 *
 *  (c) 1996 California Institute of Technology
 *  Department of Computer Science
 *  Pasadena, CA 91125.
 *
 *  Permission to use, copy, modify, and distribute this software
 *  and its documentation for any purpose and without fee is hereby
 *  granted, provided that the above copyright notice appear in all
 *  copies. The California Institute of Technology makes no representations
 *  about the suitability of this software for any purpose. It is
 *  provided "as is" without express or implied warranty. Export of this
 *  software outside of the United States of America may require an
 *  export license.
 *
 *  $Id: lisp.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 *
 *************************************************************************/
#ifndef __LISP_H__
#define __LISP_H__

/*
  Interface to the Lisp interpreter from the textio module
*/

extern void LispEvaluate();
extern void LispInit();
extern void LispSetTech();
extern void LispSetEdit();

#endif /* __LISP_H__ */

