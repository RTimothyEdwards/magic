/*************************************************************************
 *
 *  lispEval.c -- 
 *
 *   This module contains the core of the mini-scheme interpreter.
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
 *  $Id: lispEval.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 *
 *************************************************************************/

#include <stdio.h>

#include "lisp/lisp.h"
#include "lispInt.h"
#include "textio/textio.h"
#include "utils/malloc.h"
#include "lispA-Z.h"
#include "lispargs.h"
#include "utils/signals.h"

struct LispBuiltinFn {
  char *name;
  int id;
  int lazy;			/* != 0 if lazy */
  LispObj *(*f) ();		/* built-in */
};

static struct LispBuiltinFn FnTable[] = {

/*------------------------------------------------------------------------
 *      E a g e r   F u n c t i o n s
 *------------------------------------------------------------------------
 */

  /* inspect arguments */

  { "boolean?", -1, 0, LispIsBool },
  { "symbol?", -1, 0, LispIsSym },
  { "list?", -1, 0, LispIsList },
  { "pair?", -1, 0, LispIsPair },
  { "number?", -1, 0, LispIsNumber },
  { "string?", -1, 0, LispIsString },
  { "procedure?", -1, 0, LispIsProc },

  /* standard list manipulation */

  { "car", -1, 0, LispCar },
  { "cdr", -1, 0, LispCdr },
  { "cons", -1, 0, LispCons },
  { "set-car!", -1, 0, LispSetCarBang },
  { "set-cdr!", -1, 0, LispSetCdrBang },
  { "null?", -1, 0, LispNull },
  { "list", -1, 0, LispList },
  { "length", -1, 0, LispLength },

  { "eval", -1, 0, Lispeval },
  { "apply", -1, 0, Lispapply },

  { "eqv?", -1, 0, LispEqv },

  /* math */
  { "+", -1, 0, LispAdd },
  { "*", -1, 0, LispMult },
  { "-", -1, 0, LispSub },
  { "/", -1, 0, LispDiv },
  { "truncate", -1, 0, LispTruncate },


  /* comparison */
  { "zero?", -1, 0, LispZeroQ },
  { "positive?", -1, 0, LispPositiveQ },
  { "negative?", -1, 0, LispNegativeQ },

  /* string manipulation */
  { "string-append", -1, 0, LispStrCat },
  { "symbol->string", -1, 0, LispSymbolToString },
  { "string->symbol", -1, 0, LispStringToSymbol },
  { "number->string", -1, 0, LispNumberToString },
  { "string->number", -1, 0, LispStringToNumber },
  { "string-length", -1, 0, LispStringLength },
  { "string-compare", -1, 0, LispStringCompare },
  { "string-ref", -1, 0, LispStringRef },
  { "string-set!", -1, 0, LispStringSet },
  { "substring", -1, 0, LispSubString },

  /* file I/O and spawn/wait */
  { "load-scm", -1, 0, LispLoad },
  { "save-scm", -1, 0, LispWrite },
  { "spawn", -1, 0, LispSpawn },
  { "wait", -1, 0, LispWait },

  /* utilities */
  { "collect-garbage", -1, 0, LispCollectGarbage },

  /* debugging help */

  { "error", -1, 0, LispError },
  { "showframe", -1, 0, LispShowFrame },
  { "display-object", -1, 0, LispDisplayObj },
  { "print-object", -1, 0, LispPrintObj },

  /* magic */
  { "getpoint", -1, 0, LispGetPoint },
  { "getbox", -1, 0, LispGetbox },
  { "getpaint", -1, 0, LispGetPaint },
  { "getselpaint", -1, 0, LispGetSelPaint },
  { "getlabel", -1, 0, LispGetLabel },
  { "getsellabel", -1, 0, LispGetSelLabel },
  { "getcellnames", -1, 0, LispGetCellNames },
  { "magic", -1, 1, LispEvalMagic }, /* lazy */

/*------------------------------------------------------------------------
 *    N o t - s o - e a g e r   F u n c t i o n s
 *------------------------------------------------------------------------
 */
  /* lazy functions, don't evaluate any arguments */
  { "quote", -1, 1, LispQuote },
  { "lambda", -1, 1, LispLambda },
  { "let", -1, 1, LispLet },
  { "let*", -1, 1, LispLetStar },
  { "letrec", -1, 1, LispLetRec },
  { "cond", -1, 1, LispCond },
  
  { "begin", -1, 1, LispBegin },

  /* define: evaluate only second argument */
  { "define", -1, 2, LispDefine },
  { "set!",   -1, 2, LispSetBang },

  /* if: evaluate only first argument */
  { "if", -1, 3, LispIf },

  { NULL, 0, 0, NULL }
};

static LispObj *evalList ();



/*------------------------------------------------------------------------
 *
 *  LispFnInit --
 *
 *      Initialize function table.
 *
 *  Results:
 *      Returns result of evaluation.
 *
 *  Side effects:
 *      None.
 *
 *------------------------------------------------------------------------
 */

void
LispFnInit ()
{
  int i;
  for (i=0; FnTable[i].name; i++) {
    (void) LispNewString (FnTable[i].name);
    FnTable[i].id = LispStringId (FnTable[i].name);
  }
}


/*-----------------------------------------------------------------------------
 *
 *  ispair --
 *
 *      Checks if its argument is a dotted-pair.
 *
 *  Results:
 *      1 if dotted-pair, zero otherwise.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

int
ispair (s)
     Sexp *s;
{
  while (s && LTYPE(CDR(s)) == S_LIST)
    s = LLIST(CDR(s));
  if (s)
    return 1;
  else
    return 0;
}


/*------------------------------------------------------------------------
 *
 *  lookup --
 *
 *      Lookup a name in a frame.
 *
 *  Results:
 *      Returns result of lookup.
 *
 *  Side effects:
 *      None.
 *
 *------------------------------------------------------------------------
 */

LispObj *
lookup (s,f)
     char *s;
     Sexp *f;
{
  LispObj *l;
  Sexp *f1;
  int i, k;

  /* keywords have precedence */
  k = LispStringId (s);
  for (i=0; FnTable[i].name; i++)
    if (FnTable[i].id == k) {
      l = LispNewObj ();
      LTYPE(l) = S_LAMBDA_BUILTIN;
      LBUILTIN(l) = i;
      return l;
    }
  /* look in frame */
  l = LispFrameLookup (s,f);
  if (l) return l;
  /* assume that it is a magic command */
  l = LispNewObj ();
  LTYPE(l) = S_MAGIC_BUILTIN;
  LSYM(l) = s;
  return l;
}


/*------------------------------------------------------------------------
 *
 *  LispMagicSend --
 *
 *      Send magic command to magic window.
 *
 *  Results:
 *      Returns #t if magic command exists, #f otherwise.
 *
 *  Side effects:
 *      None.
 *
 *------------------------------------------------------------------------
 */
LispObj *
LispMagicSend (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  int trace;
  LispObj *l;
  int argc;
  char *argv[TX_MAXARGS];
  char argstring[TX_MAX_CMDLEN];
  int k = 0;
  int i, j;
  
  argc = 1;
  argv[0] = name;
  while (s) {
    l = CAR(s);
    if (LTYPE(CDR(s)) != S_LIST) {
      TxPrintf ("%s: invalid argument!\n",name);
      RETURN;
    }
    s = LLIST(CDR(s));
    switch (LTYPE(l)) {
    case S_INT:
      argv[argc] = argstring+k;
      sprintf (argstring+k, "%d", LINTEGER(l));
      k = k + strlen(argstring+k)+1;
      break;
    case S_FLOAT:
      argv[argc] = argstring+k;
      sprintf (argstring+k, "%lf", LFLOAT(l));
      k = k + strlen(argstring+k)+1;
      break;
    case S_STRING:
      /* undo one level of literal parsing . . . */
      argv[argc] = LSTR(l);
      i = 0; j = 0;
      while (argv[argc][i]) {
	if (argv[argc][i] == '\\')
	  i++;
	argv[argc][j] = argv[argc][i];
	i++; j++;
      }
      argv[argc][j] = '\0';
      break;
    case S_BOOL:
      argv[argc] = argstring+k;
      sprintf (argstring+k, "#%c", LINTEGER(l) ? 't' : 'f');
      k = k + strlen(argstring+k)+1;
      break;
    case S_SYM:
      argv[argc] = LSYM(l);
      break;
    case S_LAMBDA:
      TxPrintf ("%s: Type #proc in magic command argument.\n",name);
      RETURN;
      break;
    case S_LAMBDA_BUILTIN:
      argv[argc] = FnTable[LBUILTIN(l)].name;
      break;
    case S_MAGIC_BUILTIN:
      argv[argc] = LSYM(l);
      break;
    case S_LIST:
      TxPrintf ("%s: Type #list in magic command argument.\n",name);
      RETURN;
      break;
    default:
      argc--;
      break;
    }
    argc++;
  }
  l = LispFrameLookup (LispNewString ("scm-trace-magic"), f);
  if (!l)
    trace = 0;
  else if (LTYPE(l) != S_BOOL) {
    TxPrintf ("magic-dispatch: scm-trace-magic is not a boolean\n");
    RETURN;
  }
  else
    trace = LBOOL(l);
  if (!TxLispDispatch (argc, argv, trace, lispInFile))
    RETURN;
  l = LispNewObj ();
  LTYPE(l) = S_BOOL;
  LBOOL(l) = 1;
  return l;
}


/*-----------------------------------------------------------------------------
 *
 *  LispApply --
 *
 *      Evaluate a lambda.
 *          s = definition of the lambda
 *          l = list of arguments
 *          f = frame
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
LispApply (s,l,f)
     Sexp *s;
     Sexp *l;
     Sexp *f;
{
  int len;
  int dp;
  Sexp *t, *tp;
  int number, anum;
  Sexp *arglist;
  Sexp *frame;
  LispObj *eval;

  number = LINTEGER(ARG1(s));
  arglist = LLIST(ARG2(s));
  dp = ispair (arglist);
  frame = LLIST(ARG3(s));
  eval = ARG4(s);
  len=0;
  tp = NULL;
  t = l;
  while (t && LTYPE(CDR(t)) == S_LIST) {
    tp = t;
    t = LLIST(CDR(t));
    len++;
  }
  anum = (number < 0)  ? -number : number;
  if (len < anum) {
    TxPrintf ("apply: mismatch in # of arguments. Expected %d, got %d\n",
	      anum, len);
    RETURN;
  }
  t = arglist;
  f = LispFramePush (frame);
  while (t && LTYPE(CDR(t)) == S_LIST) {
    LispAddBinding (CAR(t),LispCopyObj(CAR(l)),f);
    t = LLIST(CDR(t));
    l = LLIST(CDR(l));
  }
  if (t) {
    LispAddBinding (CAR(t),LispCopyObj(CAR(l)),f);
    LispAddBinding (CDR(t),LispCopyObj(CDR(l)),f);
  }
  eval = LispEval (eval, f);
  return eval;
}
    


/*-----------------------------------------------------------------------------
 *
 *  LispBuiltinApply --
 *
 *      Apply a builtin function to a list
 *
 *  Results:
 *      The results of the builtin function
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispBuiltinApply (num,s,f)
     int num;
     Sexp *s;
     Sexp *f;
{
  return FnTable[num].f(FnTable[num].name, s, f);
}

  
/*------------------------------------------------------------------------
 *
 *  evalList --
 *
 *      Evaluate list
 *
 *  Results:
 *      Returns result of evaluation.
 *
 *  Side effects:
 *      None.
 *
 *------------------------------------------------------------------------
 */

static
LispObj *
evalList (s,f)
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  Sexp *t;

  if (!s) {
    l = LispNewObj ();
    LTYPE(l) = S_LIST;
    LLIST(l) = NULL;
    return l;
  }
  /* evaluate car field */
  s = LispCopySexp (s);

  LispGCAddSexp (s);
  CAR(s) = LispEval (CAR(s),f);
  LispGCRemoveSexp (s);

  if (!CAR(s))
    return NULL;

  if (LTYPE(CAR(s)) != S_MAGIC_BUILTIN &&
      LTYPE(CAR(s)) != S_LAMBDA_BUILTIN &&
      LTYPE(CAR(s)) != S_LAMBDA) {
    TxPrintf ("eval: First argument of list is not a procedure.\n");
    TxPrintf ("\t");
    if (CAR(s)) LispPrint (stdout, CAR(s));
    else TxPrintf ("()");
    TxPrintf ("\n");
    RETURN;
  }
  /* evaluate rest of list, if the car field corresponds to a non-lazy
     function.
  */
  if (LTYPE(CAR(s)) == S_LAMBDA_BUILTIN) {
    LispGCAddSexp (s);
    if (FnTable[LBUILTIN(CAR(s))].lazy == 2) {
      LispStackPush (FnTable[LBUILTIN(CAR(s))].name);
      /* define: evaluate second argument only */
      CDR(s) = LispCopyObj (CDR(s));
      if (LTYPE(CDR(s)) != S_LIST || LLIST(CDR(s)) == NULL) {
	TxPrintf ("define: argument error\n");
	LispGCRemoveSexp (s);
	RETURNPOP;
      }
      LLIST(CDR(s)) = LispCopySexp (LLIST(CDR(s)));
      t = LLIST(CDR(s));
      if (LTYPE(CDR(t)) != S_LIST || LLIST(CDR(t)) == NULL) {
	TxPrintf ("define: argument error\n");
	LispGCRemoveSexp (s);
	RETURNPOP;
      }
      CDR(t) = LispCopyObj (CDR(t));
      LLIST(CDR(t)) = LispCopySexp (LLIST(CDR(t)));
      t = LLIST(CDR(t));
      CAR(t) = LispEval (CAR(t),f);
      LispStackPop ();
      if (!CAR(t)) {
	LispGCRemoveSexp (s);
	return NULL;
      }
    }
    else if (FnTable[LBUILTIN(CAR(s))].lazy == 3) {
      /* if: evaluate first argument only */
      LispStackPush (FnTable[LBUILTIN(CAR(s))].name);
      CDR(s) = LispCopyObj (CDR(s));
      if (LTYPE(CDR(s)) != S_LIST || LLIST(CDR(s)) == NULL) {
	TxPrintf ("if: argument error\n");
	LispGCRemoveSexp (s);
	RETURNPOP;
      }
      LLIST(CDR(s)) = LispCopySexp (LLIST(CDR(s)));
      t = LLIST(CDR(s));
      CAR(t) = LispEval (CAR(t),f);
      LispStackPop ();
      if (!CAR(t)) {
	LispGCRemoveSexp (s);
	return NULL;
      }
    }
    LispGCRemoveSexp (s);
  }
  if (!(LTYPE(CAR(s)) == S_LAMBDA_BUILTIN && FnTable[LBUILTIN(CAR(s))].lazy)) {
    LispGCAddSexp (s);
    if (LTYPE(CAR(s)) == S_LAMBDA_BUILTIN)
      LispStackPush (FnTable[LBUILTIN(CAR(s))].name);
    else if (LTYPE(CAR(s)) == S_MAGIC_BUILTIN)
      LispStackPush (LSYM(CAR(s)));
    else {
      char *str;
      str = LispFrameRevLookup (CAR(s),f);
      LispStackPush (str ? str : "#proc-userdef");
    }
    t = s;
    while (LTYPE(CDR(t)) == S_LIST && LLIST(CDR(t))) {
      CDR(t) = LispCopyObj (CDR(t));
      LLIST(CDR(t)) = LispCopySexp (LLIST(CDR(t)));
      t = LLIST(CDR(t));
      CAR(t) = LispEval (CAR(t),f);
      if (CAR(t) == NULL) {
	LispStackPop ();
	LispGCRemoveSexp (s);
	return NULL;
      }
    }
    if (LTYPE(CDR(t)) != S_LIST) {
      CDR(t) = LispEval (CDR(t),f);
      if (CDR(t) == NULL) {
	LispStackPop ();
	LispGCRemoveSexp (s);
	return NULL;
      }
    }
    LispStackPop ();
    LispGCRemoveSexp (s);
  }
  if (LTYPE(CDR(s)) != S_LIST) {
    /* a dotted pair . . . */
    l = LispNewObj ();
    LTYPE(l) = S_LIST;
    LLIST(l) = s;
    return l;
  }
  /* dispatch function */
  if (LTYPE(CAR(s)) == S_LAMBDA_BUILTIN) {
    LispStackPush (FnTable[LBUILTIN(CAR(s))].name);
    l = LispBuiltinApply (LBUILTIN(CAR(s)), LLIST(CDR(s)), f);
    LispStackPop ();
  }
  else if (LTYPE(CAR(s)) == S_LAMBDA) {
    char *str;
    str = LispFrameRevLookup (CAR(s),f);
    LispStackPush (str ? str : "#proc-userdef");
    l = LispApply (LUSERDEF(CAR(s)), LLIST(CDR(s)),f);
    LispStackPop ();
  }
  else {
    LispStackPush (LSYM(CAR(s)));
    l = LispMagicSend (LSYM(CAR(s)),LLIST(CDR(s)), f);
    LispStackPop ();
  }
  return l;
}




/*------------------------------------------------------------------------
 *
 *  LispEval --
 *
 *      Evaluate object in a frame.
 *
 *  Results:
 *      Returns result of evaluation.
 *
 *  Side effects:
 *      None.
 *
 *------------------------------------------------------------------------
 */

LispObj *
LispEval (l,f)
     LispObj *l;
     Sexp *f;
{
  LispObj *ret;

  if (SigInterruptPending) return NULL;

  if (LTYPE(l) == S_LIST) {
    LispGCAddSexp (f);
    ret = evalList (LLIST(l),f);
    LispGCRemoveSexp (f);
    return ret;
  }
  else if (LTYPE(l) == S_SYM)
    return lookup (LSYM(l),f);
  else
    return l;
}
