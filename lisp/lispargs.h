/*************************************************************************
 *
 *  lispargs.h -- 
 *
 *   Macros for looking at various entries in a list.
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
 *  $Id: lispargs.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 *
 *  requires: lispInt.h
 *
 *************************************************************************/

#define ARG1P(s)   (s)
#define ARG1(s)    CAR(s)

#define EARG2P(s)  (LTYPE(CDR(s)) != S_LIST || LLIST(CDR(s)))
#define ARG2P(s)   (LTYPE(CDR(s)) == S_LIST && LLIST(CDR(s)))
#define ARG2(s)    CAR(LLIST(CDR(s)))

#define ARG3P(s)   (LTYPE(CDR(LLIST(CDR(s)))) == S_LIST && LLIST(CDR(LLIST(CDR(s)))))
#define ARG3(s)    CAR(LLIST(CDR(LLIST(CDR(s)))))

#define ARG4P(s)   (LTYPE(CDR(LLIST(CDR(LLIST(CDR(s)))))) == S_LIST && LLIST(CDR(LLIST(CDR(LLIST(CDR(s)))))))
#define ARG4(s)    CAR(LLIST(CDR(LLIST(CDR(LLIST(CDR(s)))))))

/*
   what i'd really like is
      do { ... } while (0)
   because that forces the user to use ";" at the end . . . but
   Sun's cc thinks you should be warned about such statements -sigh-
*/
#define RETURN     { LispStackDisplay (); return NULL; }
#define RETURNPOP  { LispStackDisplay (); LispStackPop(); return NULL; }


#define NUMBER(t) ((t) == S_INT || (t) == S_FLOAT)
