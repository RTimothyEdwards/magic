/*************************************************************************
 *
 *  lispA-Z.c --
 *
 *   This module contains the builtin mini-scheme functions.
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
 *  $Id: lispA-Z.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 *
 *************************************************************************/

#include <stdio.h>

#include "lisp/lisp.h"
#include "lispInt.h"
#include "lispargs.h"
#include "textio/textio.h"
#include "utils/malloc.h"


/*
 *=============================================================================
 *
 *
 *   Functions that test the type of their argument.
 *
 *
 *=============================================================================
 */

/*-----------------------------------------------------------------------------
 *
 *  LispIsBool --
 *
 *      Return '#t' or '#f', depending on whether the type of the object.
 *      is a boolean.
 *
 *  Results:
 *      Returns result of evaluation.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispIsBool (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *r;
  if (!ARG1P(s) || ARG2P(s)) {
    TxPrintf ("Usage: (%s obj)\n",name);
    RETURN;
  }
  r = LispNewObj ();
  LTYPE(r) = S_BOOL;
  LBOOL(r) = !!(r && LTYPE(ARG1(s)) == S_BOOL);
  return r;
}


/*-----------------------------------------------------------------------------
 *
 *  LispIsSym --
 *
 *      Return '#t' or '#f', depending on whether the type of the object
 *      is a symbol.
 *
 *  Results:
 *      Returns result of evaluation.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispIsSym (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *r;
  if (!ARG1P(s) || ARG2P(s)) {
    TxPrintf ("Usage: (%s obj)\n",name);
    RETURN;
  }
  r = LispNewObj ();
  LTYPE(r) = S_BOOL;
  LBOOL(r) = !!(r && LTYPE(ARG1(s)) == S_SYM);
  return r;
}

/*-----------------------------------------------------------------------------
 *
 *  LispIsNumber --
 *
 *      Return '#t' or '#f', depending on whether the type of the object
 *      is a number.
 *
 *  Results:
 *      Returns result of evaluation.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispIsNumber (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *r;
  if (!ARG1P(s) || ARG2P(s)) {
    TxPrintf ("Usage: (%s obj)\n",name);
    RETURN;
  }
  r = LispNewObj ();
  LTYPE(r) = S_BOOL;
  LBOOL(r) = !!(r && (LTYPE(ARG1(s)) == S_INT ||
		      LTYPE(ARG1(s)) == S_FLOAT));
  return r;
}


/*-----------------------------------------------------------------------------
 *
 *  LispIsString --
 *
 *      Return '#t' or '#f', depending on whether the type of the object
 *      is a string.
 *
 *  Results:
 *      Returns result of evaluation.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispIsString (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *r;
  if (!ARG1P(s) || ARG2P(s)) {
    TxPrintf ("Usage: (%s obj)\n",name);
    RETURN;
  }
  r = LispNewObj ();
  LTYPE(r) = S_BOOL;
  LBOOL(r) = !!(r && LTYPE(ARG1(s)) == S_STRING);
  return r;
}



/*-----------------------------------------------------------------------------
 *
 *  LispIsProc --
 *
 *      Return '#t' or '#f', depending on whether the type of the object
 *      is a procedure.
 *
 *  Results:
 *      Returns result of evaluation.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispIsProc (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *r;
  if (!ARG1P(s) || ARG2P(s)) {
    TxPrintf ("Usage: (%s obj)\n",name);
    RETURN;
  }
  r = LispNewObj ();
  LTYPE(r) = S_BOOL;
  LBOOL(r) = !!(r && (LTYPE(ARG1(s)) == S_LAMBDA ||
		      LTYPE(ARG1(s)) == S_LAMBDA_BUILTIN ||
		      LTYPE(ARG1(s)) == S_MAGIC_BUILTIN));
  return r;
}



/*-----------------------------------------------------------------------------
 *
 *  LispIsList --
 *
 *      Return '#t' or '#f', depending on whether the type of the object
 *      is a list.
 *
 *  Results:
 *      Returns result of evaluation.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispIsList (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *r;
  if (!ARG1P(s) || ARG2P(s)) {
    TxPrintf ("Usage: (%s obj)\n",name);
    RETURN;
  }
  r = LispNewObj ();
  LTYPE(r) = S_BOOL;
  if (LTYPE(ARG1(s)) != S_LIST)
    LBOOL(r) = 0;
  else {
    s = LLIST(ARG1(s));
    while (s && LTYPE(CDR(s)) == S_LIST)
      s = LLIST(CDR(s));
    if (s)
      LBOOL(r) = 0;
    else
      LBOOL(r) = 1;
  }
  return r;
}



/*-----------------------------------------------------------------------------
 *
 *  LispIsPair --
 *
 *      Return '#t' or '#f', depending on whether the type of the object
 *      is a pair.
 *
 *  Results:
 *      Returns result of evaluation.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispIsPair (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *r;
  if (!ARG1P(s) || ARG2P(s)) {
    TxPrintf ("Usage: (%s obj)\n",name);
    RETURN;
  }
  r = LispNewObj ();
  LTYPE(r) = S_BOOL;
  if (LTYPE(ARG1(s)) != S_LIST)
    LBOOL(r) = 0;
  else {
    s = LLIST(ARG1(s));
    while (s && LTYPE(CDR(s)) == S_LIST)
      s = LLIST(CDR(s));
    if (s)
      LBOOL(r) = 1;
    else
      LBOOL(r) = 0;
  }
  return r;
}



static
int
EqualObjects (l1,l2)
     LispObj *l1;
     LispObj *l2;
{
  if (LTYPE(l1) != LTYPE(l2)) return 0;
  switch (LTYPE(l1)) {
  case S_LIST:
    return LLIST(l1) == LLIST(l2);
    break;
  case S_SYM:
    return LSYM(l1) == LSYM(l2);
    break;
  case S_MAGIC_BUILTIN:
    return LSYM(l1) == LSYM(l2);
    break;
  case S_LAMBDA_BUILTIN:
    return LBUILTIN(l1) == LBUILTIN(l2);
    break;
  case S_LAMBDA:
    return LUSERDEF(l1) == LUSERDEF(l2);
    break;
  case S_INT:
    return LINTEGER(l1) == LINTEGER(l2);
    break;
  case S_FLOAT:
    return LFLOAT(l1) == LFLOAT(l2);
    break;
  case S_STRING:
    return strcmp (LSTR(l1),LSTR(l2));
    break;
  case S_BOOL:
    return (LBOOL(l1) == LBOOL(l2));
    break;
  default:
    return 0;
    break;
  }
}



/*-----------------------------------------------------------------------------
 *
 *  LispEqv --
 *
 *      Compare two objects.
 *
 *  Results:
 *      Result of comparison.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispEqv (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  if (!ARG1P(s) || !ARG2P(s) || ARG3P(s)) {
    TxPrintf ("Usage: (%s obj1 obj2)\n", name);
    RETURN;
  }
  l = LispNewObj ();
  LTYPE(l) = S_BOOL;
  LINTEGER(l) = EqualObjects (ARG1(s), ARG2(s));
  return l;
}




/*
 *=============================================================================
 *
 *
 *   List manipulation
 *
 *
 *=============================================================================
 */

/*-----------------------------------------------------------------------------
 *
 *  LispCar --
 *
 *      Return car field of sexp. First argument must be a list.
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispCar (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  if (!ARG1P(s) || LTYPE(ARG1(s)) != S_LIST || !LLIST(ARG1(s)) || ARG2P(s)) {
    TxPrintf ("Usage: (%s pair)\n",name);
    RETURN;
  }
  return CAR(LLIST(ARG1(s)));
}



/*-----------------------------------------------------------------------------
 *
 *  LispCdr --
 *
 *      Return cdr field. First argument must be a list.
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispCdr (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  if (!ARG1P(s) || LTYPE(ARG1(s)) != S_LIST || !LLIST(ARG1(s)) || ARG2P(s)) {
    TxPrintf ("Usage: (%s pair)\n",name);
    RETURN;
  }
  return CDR(LLIST(ARG1(s)));
}



/*-----------------------------------------------------------------------------
 *
 *  LispCons --
 *
 *      Return a cons cell whose car field is arg1, and cdr field is arg2
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispCons (name,s,f)
     char *name; 
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  Sexp *t;

  if (!ARG1P(s) || !ARG2P(s) || ARG3P(s)) {
    TxPrintf ("Usage: (%s obj1 obj2)\n",name);
    RETURN;
  }
  l = LispNewObj ();
  LTYPE(l) = S_LIST;
  t = LLIST(l) = LispNewSexp ();
  CAR(t) = ARG1(s);
  CDR(t) = ARG2(s);
  return l;
}



/*-----------------------------------------------------------------------------
 *
 *  LispNull --
 *
 *      Determines whether a list is the empty list.
 *
 *  Results:
 *      #t => empty list
 *      #f => otherwise
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispNull (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  if (!ARG1P(s) || ARG2P(s)) {
    TxPrintf ("Usage: (%s list)\n", name);
    RETURN;
  }
  l = LispNewObj ();
  LTYPE(l) = S_BOOL;
  LBOOL(l) = (LTYPE(ARG1(s)) == S_LIST && LLIST(ARG1(s)) == NULL) ? 1 : 0;
  return l;
}



/*-----------------------------------------------------------------------------
 *
 *  LispList --
 *
 *      Takes a list of objects and returns a new sexp with those
 *      objects as members of the list.
 *
 *  Results:
 *      The newly created list.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispList (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  l = LispNewObj ();
  LTYPE(l) = S_LIST;
  LLIST(l) = s;
  return l;
}



/*-----------------------------------------------------------------------------
 *
 *  LispLength --
 *
 *      Computes the length of a list.
 *
 *  Results:
 *      integer . . . the length of the list.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispLength (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  int len;
  if (!ARG1P(s) || LTYPE(ARG1(s)) != S_LIST || ARG2P(s)) {
    TxPrintf ("Usage: (%s list)\n", name);
    RETURN;
  }
  len = 0;
  s = LLIST(ARG1(s));
  while (s) {
    len++;
    if (LTYPE(CDR(s)) == S_LIST)
      s = LLIST(CDR(s));
    else
      break;
  }
  if (s) {
    TxPrintf ("Usage: (%s list)\n", name);
    RETURN;
  }
  l = LispNewObj ();
  LTYPE(l) = S_INT;
  LINTEGER(l) = len;
  return l;
}






/*
 *=============================================================================
 *
 *
 *     Binding mechanisms and side-effects
 *
 *
 *=============================================================================
 */

/*-----------------------------------------------------------------------------
 *
 *  LispDefine --
 *
 *      Add a binding to the current frame.
 *      (define symbol #lambda)
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      Modifies frame.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispDefine (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;

  LispCollectAllocQ = 0;
  if (!ARG1P(s) || !ARG2P(s) || LTYPE(ARG1(s)) != S_SYM || ARG3P(s)) {
    TxPrintf ("Usage: (%s symbol obj)\n", name);
    RETURN;
  }
  LispAddBinding (ARG1(s), LispCopyObj(ARG2(s)), f);
  l = LispNewObj ();
  LTYPE(l) = S_BOOL;
  LBOOL(l) = 1;
  return l;
}



/*-----------------------------------------------------------------------------
 *
 *  LispSetBang --
 *
 *      "set!" lisp builtin.
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispSetBang (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;

  if (!ARG1P(s) || !ARG2P(s) || LTYPE(ARG1(s)) != S_SYM || ARG3P(s)) {
    TxPrintf ("Usage: (%s symbol obj)\n", name);
    RETURN;
  }
  LispCollectAllocQ = 0;
  if (LispModifyBinding (ARG1(s), ARG2(s), f)) {
    return ARG2(s);
  }
  else {
    TxPrintf ("%s: unknown symbol [%s]\n", name, LSYM(ARG1(s)));
    RETURN;
  }
}



/*-----------------------------------------------------------------------------
 *
 *  LispLet --
 *
 *      Let bindings.
 *      (let binding-list body)
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispLet (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *body, *l;
  Sexp *frame, *saved;
  if (!ARG1P(s) || !ARG2P(s) || LTYPE(ARG1(s)) != S_LIST || ARG3P(s)) {
    TxPrintf ("Usage: (%s list obj)\n", name);
    RETURN;
  }
  frame = LispFramePush (f);
  body = ARG2(s);
  s = LLIST(ARG1(s));

  LispGCAddSexp (saved = s);
  LispGCAddSexp (f);
  LispGCAddSexp (frame);
  
  while (s) {
    if (LTYPE(CAR(s)) != S_LIST || LTYPE(CDR(s)) != S_LIST) {
      TxPrintf ("%s: binding must be a list\n", name);
      LispGCRemoveSexp (frame);
      LispGCRemoveSexp (f);
      LispGCRemoveSexp (saved);
      RETURN;
    }
    if (LTYPE(CAR(LLIST(CAR(s)))) != S_SYM) {
      TxPrintf ("%s: can only bind to a symbol\n", name);
      LispGCRemoveSexp (frame);
      LispGCRemoveSexp (f);
      LispGCRemoveSexp (saved);
      RETURN;
    }
    l = LispEval (CAR(LLIST(CDR(LLIST(CAR(s))))),f);
    if (l == NULL) {
      LispGCRemoveSexp (frame);
      LispGCRemoveSexp (f);
      LispGCRemoveSexp (saved);
      return NULL;
    }
    LispAddBinding (CAR(LLIST(CAR(s))), l, frame);
    s = LLIST(CDR(s));
  }

  body = LispEval (body, frame);

  LispGCRemoveSexp (frame);
  LispGCRemoveSexp (f);
  LispGCRemoveSexp (saved);

  return body;
}  



/*-----------------------------------------------------------------------------
 *
 *  LispLetRec --
 *
 *      Letrec bindings.
 *      (letrec binding-list body)
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispLetRec (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *body, *l;
  Sexp *frame, *saved;
  if (!ARG1P(s) || !ARG2P(s) || LTYPE(ARG1(s)) != S_LIST || ARG3P(s)) {
    TxPrintf ("Usage: (%s list obj)\n", name);
    RETURN;
  }
  frame = LispFramePush (f);
  body = ARG2(s);
  s = LLIST(ARG1(s));

  LispGCAddSexp (saved = s);
  LispGCAddSexp (f);
  LispGCAddSexp (frame);

  while (s) {
    if (LTYPE(CAR(s)) != S_LIST || LTYPE(CDR(s)) != S_LIST) {
      TxPrintf ("%s: binding must be a list\n", name);
      LispGCRemoveSexp (frame);
      LispGCRemoveSexp (f);
      LispGCRemoveSexp (saved);
      RETURN;
    }
    if (LTYPE(CAR(LLIST(CAR(s)))) != S_SYM) {
      TxPrintf ("%s: can only bind to a symbol\n", name);
      LispGCRemoveSexp (frame);
      LispGCRemoveSexp (f);
      LispGCRemoveSexp (saved);
      RETURN;
    }
    l = LispEval (CAR(LLIST(CDR(LLIST(CAR(s))))),frame);
    if (l == NULL) {
      LispGCRemoveSexp (frame);
      LispGCRemoveSexp (f);
      LispGCRemoveSexp (saved);
      return NULL;
    }
    LispAddBinding (CAR(LLIST(CAR(s))), l, frame);
    s = LLIST(CDR(s));
  }

  body = LispEval (body, frame);
  
  LispGCRemoveSexp (frame);
  LispGCRemoveSexp (f);
  LispGCRemoveSexp (saved);

  return body;
}  



/*-----------------------------------------------------------------------------
 *
 *  LispLetStar --
 *
 *      LetStar bindings.
 *      (let* binding-list body)
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispLetStar (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *body, *l;
  Sexp *frame, *saved;

  if (!ARG1P(s) || !ARG2P(s) || LTYPE(ARG1(s)) != S_LIST || ARG3P(s)) {
    TxPrintf ("Usage: (%s list obj)\n", name);
    RETURN;
  }

  body = ARG2(s);
  s = LLIST(ARG1(s));
  frame = f;
  
  LispGCAddSexp (saved = s);
  LispGCAddSexp (f);
  LispGCAddSexp (frame);

  while (s) {
    LispGCRemoveSexp (frame);
    frame = LispFramePush (frame);
    LispGCAddSexp (frame);
    if (LTYPE(CAR(s)) != S_LIST || LTYPE(CDR(s)) != S_LIST) {
      TxPrintf ("%s: binding must be a list\n", name);
      LispGCRemoveSexp (frame);
      LispGCRemoveSexp (f);
      LispGCRemoveSexp (saved);
      RETURN;
    }
    if (LTYPE(CAR(LLIST(CAR(s)))) != S_SYM) {
      TxPrintf ("%s: can only bind to a symbol\n", name);
      LispGCRemoveSexp (frame);
      LispGCRemoveSexp (f);
      LispGCRemoveSexp (saved);
      RETURN;
    }
    l = LispEval (CAR(LLIST(CDR(LLIST(CAR(s))))),frame);
    if (l == NULL) {
      LispGCRemoveSexp (frame);
      LispGCRemoveSexp (f);
      LispGCRemoveSexp (saved);
      return NULL;
    }
    LispAddBinding (CAR(LLIST(CAR(s))), l, frame);
    s = LLIST(CDR(s));
  }

  body = LispEval (body, frame);

  LispGCRemoveSexp (frame);
  LispGCRemoveSexp (f);
  LispGCRemoveSexp (saved);

  return body;
}




/*-----------------------------------------------------------------------------
 *
 *  LispSetCarBang --
 *
 *      Modifies the pair
 *
 *  Results:
 *      New binding for pair
 *
 *  Side effects:
 *      Of course!
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispSetCarBang (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  
  if (!ARG1P(s) || !ARG2P(s) || LTYPE(ARG1(s)) != S_LIST || !LLIST(ARG1(s)) || ARG3P(s)) {
    TxPrintf ("Usage: (%s pair obj)\n", name);
    RETURN;
  }
  LispCollectAllocQ = 0;
  CAR(LLIST(ARG1(s))) = ARG2(s);
  return ARG1(s);
}


/*-----------------------------------------------------------------------------
 *
 *  LispSetCdrBang --
 *
 *      Modifies the pair
 *
 *  Results:
 *      New binding for pair
 *
 *  Side effects:
 *      Of course!
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispSetCdrBang (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  
  if (!ARG1P(s) || !ARG2P(s) || LTYPE(ARG1(s)) != S_LIST || !LLIST(ARG1(s)) || ARG3P(s)) {
    TxPrintf ("Usage: (%s pair obj)\n", name);
    RETURN;
  }
  LispCollectAllocQ = 0;
  CDR(LLIST(ARG1(s))) = ARG2(s);
  return ARG1(s);
}




/*
 *=============================================================================
 *
 *
 *   Control over what gets evaluated
 *
 *
 *=============================================================================
 */


/*-----------------------------------------------------------------------------
 *
 *  Lispeval --
 *
 *      Evaluate argument. 
 *      (eval object)
 *
 *  Results:
 *      result of evaluating its argument.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
Lispeval (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  if (!ARG1P(s) || ARG2P(s)) {
    TxPrintf ("Usage: (%s obj)\n", name);
    RETURN;
  }
  return LispEval (ARG1(s),f);
}



/*-----------------------------------------------------------------------------
 *
 *  LispQuote --
 *
 *      Return quoted argument.
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispQuote (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  if (!ARG1P(s) || ARG2P(s)) {
    TxPrintf ("Usage: (%s obj)\n",name);
    RETURN;
  }
  return ARG1(s);
}




/*
 *=============================================================================
 *
 *
 *    Control flow: sequencing and conditional execution
 *
 *
 *=============================================================================
 */

/*-----------------------------------------------------------------------------
 *
 *  LispIf --
 *
 *      Conditional evaluation.
 *      (if condition sexp1 sexp2)
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispIf (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  if (!ARG1P(s) || !ARG2P(s) || !ARG3P(s) || LTYPE(ARG1(s)) != S_BOOL 
      || ARG4P(s)) {
    TxPrintf ("Usage: (%s bool obj1 obj2)\n", name);
    RETURN;
  }

  /* s might be collected before this function returns. But that's okay. */

  if (LBOOL(ARG1(s)))
    return LispEval (ARG2(s), f);
  else 
    return LispEval (ARG3(s), f);
}



/*-----------------------------------------------------------------------------
 *
 *  LispCond --
 *
 *      cond evaluation
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispCond (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  LispObj *m;
  Sexp *t, *saved;
  
  if (!ARG1P(s)) {
    TxPrintf ("Usage: (%s (bool val) ...)\n", name);
    RETURN;
  }

  LispGCAddSexp (saved = s);
  LispGCAddSexp (f);

  while (ARG1P(s)) {
    if (LTYPE(CDR(s)) != S_LIST) {
      TxPrintf ("%s: argument is not a list!\n", name);
      LispGCRemoveSexp (f);
      LispGCRemoveSexp (saved);
      RETURN;
    }
    m = ARG1(s);
    if (LTYPE(m) != S_LIST) {
      TxPrintf ("%s: argument is not a list\n", name);
      LispGCRemoveSexp (f);
      LispGCRemoveSexp (saved);
      RETURN;
    }
    if (LTYPE(CDR(LLIST(m))) != S_LIST || !LLIST(CDR(LLIST(m)))) {
      TxPrintf ("%s: argument is not a list\n", name);
      LispGCRemoveSexp (f);
      LispGCRemoveSexp (saved);
      RETURN;
    }
    if (LTYPE(CDR(LLIST(CDR(LLIST(m))))) != S_LIST || LLIST(CDR(LLIST(CDR(LLIST(m)))))) {
      TxPrintf ("%s: argument is not a list with two elements\n", name);
      LispGCRemoveSexp (f);
      LispGCRemoveSexp (saved);
      RETURN;
    }

    l = LispEval (CAR(LLIST(m)),f);

    if (!l) {
      LispGCRemoveSexp (f);
      LispGCRemoveSexp (saved);
      return NULL;
    }

    if (LTYPE(l) != S_BOOL) {
      TxPrintf ("%s: first arg in list must be of type boolean\n", name);
      LispGCRemoveSexp (f);
      LispGCRemoveSexp (saved);
      RETURN;
    }

    if (LBOOL(l)) {
      l = LispEval (CAR(LLIST(CDR(LLIST(m)))),f);
      LispGCRemoveSexp (f);
      LispGCRemoveSexp (saved);
      return l;
    }
    s = LLIST(CDR(s));
  }
  l = LispNewObj ();
  LTYPE(l) = S_BOOL;
  LBOOL(l) = 0;
  LispGCRemoveSexp (f);
  LispGCRemoveSexp (saved);
  return l;
}



/*-----------------------------------------------------------------------------
 *
 *  LispBegin --
 *
 *      (begin a b c d . . .)
 *      Evaluates its arguments in order (l->r) and returns the result
 *      of the last evaluation.
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispBegin (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l, *m;
  Sexp *saved;

  if (!s) {
    TxPrintf ("Usage: (%s obj1 ...)\n", name);
    RETURN;
  }

  LispGCAddSexp (saved = s);
  LispGCAddSexp (f);

  while (s && LTYPE(CDR(s)) == S_LIST) {
    l = LispEval (CAR(s),f);
    if (l == NULL) {
      LispGCRemoveSexp (f);
      LispGCRemoveSexp (saved);
      return NULL;
    }
    s = LLIST(CDR(s));
  }
  if (s && LTYPE(CDR(s)) != S_LIST) {
    m = LispEval (CDR(s),f);
    if (m == NULL) {
      LispGCRemoveSexp (f);
      LispGCRemoveSexp (saved);
      return NULL;
    }
    s = LispNewSexp ();
    CAR(s) = l;
    CDR(s) = m;
    l = LispNewObj ();
    LTYPE(l) = S_LIST;
    LLIST(l) = s;
  }
  LispGCRemoveSexp (f);
  LispGCRemoveSexp (saved);
  return l;
}



/*
 *=============================================================================
 *
 *
 *   Functions
 *
 *
 *=============================================================================
 */

/*-----------------------------------------------------------------------------
 *
 *  LispApply --
 *
 *      Apply lambda to a list.
 *      (apply #lambda list)
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
Lispapply (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  if (!ARG1P(s) || !ARG2P(s) || LTYPE(ARG2(s)) != S_LIST ||
      (LTYPE(ARG1(s)) != S_LAMBDA && LTYPE(ARG1(s)) != S_LAMBDA_BUILTIN &&
       LTYPE(ARG1(s)) != S_MAGIC_BUILTIN) || ARG3P(s)) {
    TxPrintf ("Usage: (%s proc list)\n", name);
    RETURN;
  }
  if (LTYPE(ARG1(s)) == S_LAMBDA_BUILTIN)
    return LispBuiltinApply (LBUILTIN(ARG1(s)), LLIST(ARG2(s)), f);
  else if (LTYPE(ARG1(s)) == S_MAGIC_BUILTIN)
    return LispMagicSend (LSYM(ARG1(s)), LLIST(ARG2(s)), f);
  else
    return LispApply (LUSERDEF(ARG1(s)), LLIST(ARG2(s)), f);
}



/*-----------------------------------------------------------------------------
 *
 *  LispLambda --
 *
 *      Return a "lambda" construction.
 *      (lambda list list)
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispLambda (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  Sexp *s1;
  int number;
  Sexp *arglist;
  Sexp *frame;
  LispObj *evallist;

  if (!ARG1P(s) || !ARG2P(s) || LTYPE(ARG1(s)) != S_LIST || ARG3P(s)) {
    TxPrintf ("Usage: (%s list obj)\n", name);
    RETURN;
  }
  /* check first argument */
  number = 0;
  s1 = LLIST(ARG1(s));
  while (s1 && LTYPE(CDR(s1)) == S_LIST) {
    if (LTYPE(CAR(s1)) != S_SYM) {
      TxPrintf ("%s: all arguments must be symbols\n", name);
      RETURN;
    }
    number++;
    s1 = LLIST(CDR(s1));
  }
  if (s1)
    number=-(number+1);		/* dotted-pair args! */
  arglist = LLIST(ARG1(s));
  frame = f;
  evallist = ARG2(s);
  s1 = LispNewSexp ();
  CAR(s1) = evallist;
  CDR(s1) = LispNewObj ();
  LTYPE(CDR(s1)) = S_LIST;
  LLIST(CDR(s1)) = NULL;
  s = s1;
  s1 = LispNewSexp ();
  CDR(s1) = LispNewObj ();
  LTYPE(CDR(s1)) = S_LIST;
  LLIST(CDR(s1)) = s;
  CAR(s1) = LispNewObj ();
  LTYPE(CAR(s1)) = S_LIST;
  LLIST(CAR(s1)) = frame;
  s = s1;
  s1 = LispNewSexp ();
  CDR(s1) = LispNewObj ();
  LTYPE(CDR(s1)) = S_LIST;
  LLIST(CDR(s1)) = s;
  CAR(s1) = LispNewObj ();
  LTYPE(CAR(s1)) = S_LIST;
  LLIST(CAR(s1)) = arglist;
  s = s1;
  s1 = LispNewSexp ();
  CDR(s1) = LispNewObj ();
  LTYPE(CDR(s1)) = S_LIST;
  LLIST(CDR(s1)) = s;
  CAR(s1) = LispNewObj ();
  LTYPE(CAR(s1)) = S_INT;
  LINTEGER(CAR(s1)) = number;
  l = LispNewObj ();
  LTYPE(l) = S_LAMBDA;
  LUSERDEF(l) = s1;
  return l;
}



/*
 *=============================================================================
 *
 *
 *    Debugging/error reporting
 *
 *
 *=============================================================================
 */

/*-----------------------------------------------------------------------------
 *
 *  LispDisplayObj --
 *
 *      (display-object s)
 *      Prints object out to screen.
 *      
 *
 *  Results:
 *      returns #t.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispDisplayObj (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  if (!ARG1P(s) || ARG2P(s)) {
    TxPrintf ("Usage: (%s obj)\n", name);
    RETURN;
  }
  TxPrintf (" => type: ");
  LispPrintType (stdout,ARG1(s)); TxPrintf ("\n");
  TxPrintf ("   value: ");
  LispPrint (stdout,ARG1(s));
  TxPrintf ("\n");
  l = LispNewObj ();
  LTYPE(l) = S_BOOL;
  LBOOL(l) = 1;
  return l;
}



/*-----------------------------------------------------------------------------
 *
 *  LispPrintObj --
 *
 *      (print-object s)
 *      Prints object out to screen, no newlines
 *      
 *
 *  Results:
 *      returns #t.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispPrintObj (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  if (!ARG1P(s) || ARG2P(s)) {
    TxPrintf ("Usage: (%s obj)\n", name);
    RETURN;
  }
  LispPrint (stdout,ARG1(s));
  l = LispNewObj ();
  LTYPE(l) = S_BOOL;
  LBOOL(l) = 1;
  return l;
}



/*-----------------------------------------------------------------------------
 *
 *  LispError --
 *
 *      Echo error and abort evaluation.
 *
 *  Results:
 *      NULL
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispError (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  int i,j;
  char *str;
  if (!ARG1P(s) || LTYPE(ARG1(s)) != S_STRING || ARG2P(s)) {
    TxPrintf ("Usage: (%s str)\n", name);
    RETURN;
  }
  i = 0; j = 0;
  str = (char *) mallocMagic((unsigned) (strlen(LSTR(ARG1(s)))+1));
  strcpy (str, LSTR(ARG1(s)));
  while (str[i]) {
    if (str[i] == '\\')
      i++;
    str[j] = str[i];
    i++; j++;
  }
  str[j] = '\0';
  TxPrintf ("%s\n", str);
  freeMagic(str);
  RETURN;
}



/*-----------------------------------------------------------------------------
 *
 *  LispShowFrame --
 *
 *      Display the current frame.
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      Text appears in window.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispShowFrame (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;

  if (ARG1P(s)) {
    TxPrintf ("Usage: (%s)\n", name);
    RETURN;
  }

  TxPrintf (" >> ");
  l = LispNewObj ();
  LTYPE(l) = S_LIST;
  LLIST(l) = f;
  LispPrint (stdout,l);
  TxPrintf ("\n");
  l = LispNewObj ();
  LTYPE(l) = S_BOOL;
  LBOOL(l) = 1;
  return l;
}
