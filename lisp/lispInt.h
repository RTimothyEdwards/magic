/*************************************************************************
 *
 *  lispInt.h -- 
 *
 *   Internals of the lisp module.
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
 *  $Id: lispInt.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 *
 *************************************************************************/
#ifndef __LISPINT_H__
#define __LISPINT_H__

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "textio/txcommands.h"

/*
  Lisp Sexp's are allocated in blocks of the following size
*/
#define LISP_SEXP_BLKSZ  1000
#define LISP_MAX_LEN  (TX_MAX_CMDLEN+3)


enum Sexp_types {
  S_LIST,
  S_SYM,
  S_MAGIC_BUILTIN,
  S_LAMBDA_BUILTIN,
  S_LAMBDA,
  S_INT,
  S_FLOAT,
  S_STRING,
  S_BOOL
};

#define ATOM(t) ((t) == S_INT || (t) == S_FLOAT || \
		 (t) == S_STRING || (t) == S_BOOL)


#define LTYPE(l) ((l)->t)
#define LLIST(m) ((m)->u.l)
#define LUSERDEF(m) ((m)->u.l)
#define LBUILTIN(m) ((m)->u.i)
#define LSYM(l) ((l)->u.s)
#define LSTR(l) ((l)->u.s)
#define LBOOL(l) ((l)->u.i)
#define LFLOAT(l) ((l)->u.d)
#define LINTEGER(l) ((l)->u.i)

#define TYPECAR(s) LTYPE((s)->l[0])
#define TYPECDR(s) LTYPE((s)->l[1])

#define LCAR(s) LLIST(CAR(s))
#define SYMCAR(s) LSYM((s)->l[0])
#define STRCAR(s) LSTR((s)->l[0])
#define INTCAR(s) LINTEGER((s)->l[0])
#define FLOATCAR(s) LFLOAT((s)->l[0])
#define BOOLCAR(s) LBOOL((s)->l[0])

#define LCDR(s) LLIST((s)->l[1])
#define SYMCDR(s) LSYM((s)->l[1])
#define STRCDR(s) LSTR((s)->l[1])
#define INTCDR(s) LINTEGER((s)->l[1])
#define FLOATCDR(s) LFLOAT((s)->l[1])
#define BOOLCDR(s) LBOOL((s)->l[1])

#define CAR(s)  ((s)->l[0])
#define CDR(s)  ((s)->l[1])



#define NBITS (sizeof(unsigned int)*8)

struct Sexp;

typedef struct LispObj {
  struct LispObj *n;
  unsigned char t;		/* type */
  union {
    struct Sexp *l;
    int i;
    double d;
    char *s;
  } u;
} LispObj;
       
typedef struct Sexp {
  struct Sexp *n;
  LispObj *l[2];
} Sexp;

/*
 * Internal commands 
 */
extern void LispDispatch();

extern LispObj *LispNewObj ();
extern LispObj *LispCopyObj ();
extern Sexp *LispNewSexp ();
extern Sexp *LispCopySexp ();

extern LispObj *LispEval ();
extern LispObj *LispApply ();
extern LispObj *LispMagicSend ();
extern LispObj *LispBuiltinApply ();
extern void LispPrint ();
extern void LispPrintType ();

extern LispObj *LispParseString ();
extern LispObj *LispAtomParse ();
extern char *LispNewString ();
extern int  LispStringId ();
extern void LispFnInit ();

extern void LispGC ();
extern int LispGCHasWork;
extern int LispCollectAllocQ;
extern void LispGCAddSexp ();
extern void LispGCRemoveSexp ();

extern LispObj *LispFrameLookup ();
extern char *LispFrameRevLookup ();
extern void LispFrameInit ();
extern int  LispModifyFrame ();
extern void LispAddBinding ();
extern Sexp *LispFramePush ();

extern void LispStackPush ();
extern void LispStackPop ();
extern void LispStackDisplay ();
extern void LispStackClear ();

extern int lispInFile;		/* flag used for :setpoint */

#endif /* __LISPINT_H__ */
